"""One source revision, version and build time shared by every Sine Stack format."""
import datetime
import fcntl
import hashlib
import json
import os
import re
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def fingerprint(root):
    files = []
    for project in ("sine stack au", "sine stack vst3"):
        base = root / project
        files.extend((base / "CMakeLists.txt", base / "CMakePresets.json"))
        for directory in ("Source", "cmake"):
            files.extend(p for p in (base / directory).rglob("*") if p.is_file())
    files.extend(p for p in (root / "cmake").rglob("*.cmake") if p.is_file())
    files.extend(root / "scripts" / name for name in ("versioning.py", "update_local.py"))
    # Release tooling and bundled installation instructions define a release too.
    # Only top-level tooling/resources are included; packaging/tests stays excluded.
    for pattern in ("*.py", "*.sh", "*.txt", "resources/*"):
        files.extend(p for p in (root / "packaging").glob(pattern) if p.is_file())
    digest = hashlib.sha256()
    for path in sorted(set(files)):
        name = path.relative_to(root).as_posix().encode()
        content = path.read_bytes()
        digest.update(len(name).to_bytes(8, "big") + name)
        digest.update(len(content).to_bytes(8, "big") + content)
    return digest.hexdigest()


def next_version(version):
    if not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid shared version; refusing to reset the version counter")
    major, minor, patch = map(int, version.split("."))
    if any(value > 255 for value in (major, minor, patch)):
        raise ValueError("Version exceeds JUCE/AU's three-byte version range")
    patch += 1
    if patch == 256:
        patch = 0
        minor += 1
    if minor == 256:
        minor = 0
        major += 1
    if major > 255:
        raise ValueError("Version counter exhausted")
    return f"{major}.{minor}.{patch}"


def read_state(root=ROOT):
    state = json.loads((root / "version.json").read_text())
    next_version(state["version"])  # Validate even when this is a no-op build.
    datetime.datetime.strptime(state["built_at_utc"], "%Y-%m-%dT%H:%M:%SZ")
    value = state["fingerprint"]
    if value is not None and not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("Invalid source fingerprint")
    return state


def prepare(root=ROOT):
    # Independent AU/VST3 configurations can run together; only one reserves the version.
    with (root / ".sinestack-version.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        state = read_state(root)
        current = fingerprint(root)
        if current != state["fingerprint"]:
            state = {
                "version": next_version(state["version"]),
                "fingerprint": current,
                "built_at_utc": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
            }
            # A failed build keeps this reservation. Retrying the same inputs does not bump again.
            temporary = None
            try:
                with tempfile.NamedTemporaryFile(mode="w", dir=root, prefix=".version-", delete=False) as output:
                    temporary = Path(output.name)
                    json.dump(state, output, indent=2)
                    output.write("\n")
                    output.flush()
                    os.fsync(output.fileno())
                temporary.replace(root / "version.json")
            finally:
                if temporary is not None:
                    temporary.unlink(missing_ok=True)
        return state


def verify_bundle(info, state):
    expected = {
        "CFBundleShortVersionString": state["version"],
        "CFBundleVersion": state["version"],
        "SineStackBuildUTC": state["built_at_utc"],
        "SineStackSourceFingerprint": state["fingerprint"],
    }
    for key, value in expected.items():
        if info.get(key) != value:
            raise RuntimeError(f"Stale or mismatched bundle: {key}={info.get(key)!r}, expected {value!r}")


if __name__ == "__main__":
    print(json.dumps(prepare()))
