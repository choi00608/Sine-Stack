"""Exercise actual CMake reconfiguration without rebuilding JUCE in a test fixture."""
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[2]
cmake = sys.argv[1] if len(sys.argv) > 1 else "cmake"
with tempfile.TemporaryDirectory(prefix="sinestack-version-check-") as temporary:
    fixture = Path(temporary)
    (fixture / "scripts").mkdir()
    (fixture / "cmake").mkdir()
    shutil.copy2(root / "scripts/versioning.py", fixture / "scripts/versioning.py")
    (fixture / "scripts/update_local.py").write_text("# fixture")
    shutil.copy2(root / "cmake/SharedVersion.cmake", fixture / "cmake/SharedVersion.cmake")
    (fixture / "version.json").write_text(json.dumps({
        "version": "0.1.0", "fingerprint": None, "built_at_utc": "2026-11-11T08:12:04Z"}))
    for name in ("sine stack au", "sine stack vst3"):
        project = fixture / name
        (project / "Source").mkdir(parents=True)
        (project / "cmake").mkdir()
        (project / "Source/synth.h").write_text("// original")
        (project / "CMakePresets.json").write_text("{}")
        (project / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.22)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/SharedVersion.cmake")
project(Fixture VERSION ${SINESTACK_VERSION} LANGUAGES NONE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/version.txt" "${PROJECT_VERSION}")
add_custom_target(fixture ALL COMMAND "${CMAKE_COMMAND}" -E true)
''')

    def run(*args):
        result = subprocess.run([cmake, *map(str, args)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def version(name):
        return (fixture / name / "build/version.txt").read_text()

    for name in ("sine stack au", "sine stack vst3"):
        run("-S", fixture / name, "-B", fixture / name / "build")
        run("--build", fixture / name / "build")
        assert version(name) == "0.1.1"
    # Change AU source, then invoke only the VST3 build command: it must detect the shared change.
    (fixture / "sine stack au/Source/synth.h").write_text("// edited source")
    run("--build", fixture / "sine stack vst3/build")
    assert version("sine stack vst3") == "0.1.2"
    run("--build", fixture / "sine stack au/build")
    assert version("sine stack au") == "0.1.2"
    state = (fixture / "version.json").read_bytes()
    for name in ("sine stack au", "sine stack vst3"):
        run("--build", fixture / name / "build")
    assert (fixture / "version.json").read_bytes() == state
    (fixture / "packaging").mkdir()
    (fixture / "packaging/install.sh").write_text("# new release installer")
    for name in ("sine stack au", "sine stack vst3"):
        run("--build", fixture / name / "build")
        assert version(name) == "0.1.3"
    print("PASS: direct CMake builds detect cross-project changes, share one version, and preserve no-op builds")
