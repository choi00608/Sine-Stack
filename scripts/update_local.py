#!/usr/bin/env python3
"""Build and install the current user's AU, VST3 and Sine Stack standalone app."""
import argparse
import plistlib
import shutil
import subprocess
import uuid
from pathlib import Path
from versioning import fingerprint, prepare, read_state, verify_bundle

ROOT = Path(__file__).resolve().parent.parent


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--build-only", action="store_true", help="Build and verify all formats without installing")
    args = parser.parse_args()
    revision = prepare(ROOT)
    print(f"Building shared version {revision['version']} / {revision['built_at_utc']}", flush=True)
    for project, target in (("sine stack au", "SineStack_AU"), ("sine stack vst3", "SineStack_VST3")):
        # Explicitly configure both projects, including when the other project's inputs changed.
        run(args.cmake, "-S", ROOT / project, "-B", ROOT / project / "build")
        run(args.cmake, "--build", ROOT / project / "build", "--target", target,
            "SineStack_Standalone", "--parallel", "3")

    home = Path.home()
    bundles = [
        ("sine stack au", "AU/Sine Stack.component", home / "Library/Audio/Plug-Ins/Components/Sine Stack.component"),
        ("sine stack vst3", "VST3/Sine Stack.vst3", home / "Library/Audio/Plug-Ins/VST3/Sine Stack.vst3"),
        ("sine stack au", "Standalone/Sine Stack.app", home / "Applications/Sine Stack.app"),
    ]
    staged = []
    installed = []
    token = uuid.uuid4().hex
    try:
        # Prepare all three before changing any installed bundle.
        if read_state(ROOT) != revision or fingerprint(ROOT) != revision["fingerprint"]:
            raise RuntimeError("Sources changed during the build; rerun before installing")
        for project, relative, destination in bundles:
            source = ROOT / project / "build/SineStack_artefacts/Release" / relative
            info = plistlib.loads((source / "Contents/Info.plist").read_bytes())
            verify_bundle(info, revision)
            if source.suffix == ".app" and not info.get("NSBluetoothAlwaysUsageDescription", "").strip():
                raise RuntimeError("Standalone app is missing its Bluetooth usage description")
            run("/usr/bin/lipo", source / "Contents/MacOS" / info["CFBundleExecutable"],
                "-verify_arch", "arm64", "x86_64")
            # CMake can regenerate Info.plist without relinking. Seal the finished local build.
            run("/usr/bin/codesign", "--force", "--sign", "-", "--timestamp=none", source)
            run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", source)
            if args.build_only:
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            stage = destination.parent / (".SineStack-stage-" + token + source.suffix)
            backup = destination.parent / (".SineStack-previous-" + token + source.suffix)
            staged.append((stage, destination, backup))
            run("/usr/bin/ditto", "--noextattr", "--norsrc", source, stage)
            run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", stage)

        if read_state(ROOT) != revision or fingerprint(ROOT) != revision["fingerprint"]:
            raise RuntimeError("Sources changed while staging; rerun before installing")
        for stage, destination, backup in staged:
            if destination.exists():
                destination.rename(backup)
            installed.append((stage, destination, backup))
            stage.rename(destination)
    except Exception:
        for stage, destination, backup in reversed(installed):
            if not stage.exists() and destination.exists():
                destination.rename(stage)
            if backup.exists():
                backup.rename(destination)
        raise
    finally:
        for stage, _, _ in staged:
            if stage.exists():
                shutil.rmtree(stage)
    for _, destination, backup in installed:
        if backup.exists():
            shutil.rmtree(backup)
        print(f"Installed: {destination}")
    if args.build_only:
        print(f"Verified all three builds at v{revision['version']}; no installation performed.")
    else:
        print(f"All three updated to v{revision['version']}. Reopen running DAWs and Sine Stack.")


if __name__ == "__main__":
    main()
