import concurrent.futures
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("versioning", Path(__file__).resolve().parents[1] / "versioning.py")
versioning = importlib.util.module_from_spec(spec)
spec.loader.exec_module(versioning)


class SharedVersionTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        for project in ("sine stack au", "sine stack vst3"):
            base = self.root / project
            (base / "Source").mkdir(parents=True)
            (base / "cmake").mkdir()
            (base / "CMakeLists.txt").write_text("project fixture")
            (base / "CMakePresets.json").write_text("{}")
            (base / "Source/Plugin.cpp").write_text("first source")
        (self.root / "scripts").mkdir()
        for name in ("versioning.py", "update_local.py"):
            (self.root / "scripts" / name).write_text("script fixture")
        (self.root / "version.json").write_text(json.dumps({
            "version": "0.1.0", "fingerprint": None, "built_at_utc": "2026-11-11T08:12:04Z"}))

    def prepare(self):
        return versioning.prepare(self.root)

    def test_one_version_for_both_formats_and_retries(self):
        first = self.prepare()
        self.assertEqual(first["version"], "0.1.1")
        manifest = self.root / "version.json"
        modified = manifest.stat().st_mtime_ns
        for _ in range(4):  # AU, Standalone, VST3, failed-build retry/reinstall.
            self.assertEqual(self.prepare(), first)
        self.assertEqual(manifest.stat().st_mtime_ns, modified)
        (self.root / "README.md").write_text("Documentation only")
        (self.root / "sine stack au/Source/Plugin.cpp").touch()
        self.assertEqual(self.prepare(), first)
        (self.root / "sine stack vst3/Source/Plugin.cpp").write_text("changed source")
        second = self.prepare()
        self.assertEqual(second["version"], "0.1.2")
        self.assertNotEqual(second["fingerprint"], first["fingerprint"])
        self.assertEqual(self.prepare(), second)

    def test_parallel_configurations_reserve_once(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            results = list(pool.map(lambda _: self.prepare(), range(16)))
        self.assertTrue(all(result == results[0] for result in results))
        self.assertEqual(results[0]["version"], "0.1.1")

    def test_added_removed_sources_and_build_settings(self):
        self.prepare()
        new_file = self.root / "sine stack au/Source/New.h"
        new_file.write_text("new source")
        self.assertEqual(self.prepare()["version"], "0.1.2")
        new_file.unlink()
        self.assertEqual(self.prepare()["version"], "0.1.3")
        (self.root / "sine stack au/CMakePresets.json").write_text('{"changed":true}')
        self.assertEqual(self.prepare()["version"], "0.1.4")

    def test_stale_bundle_refused_before_install(self):
        state = self.prepare()
        info = {"CFBundleShortVersionString": state["version"], "CFBundleVersion": state["version"],
                "SineStackBuildUTC": state["built_at_utc"], "SineStackSourceFingerprint": state["fingerprint"]}
        versioning.verify_bundle(info, state)
        for key in info:
            with self.assertRaises(RuntimeError):
                versioning.verify_bundle({**info, key: "old"}, state)

    def test_release_tooling_changes_bump_but_tests_do_not(self):
        self.prepare()
        folder = self.root / "packaging"
        folder.mkdir()
        (folder / "install.sh").write_text("installer revision")
        second = self.prepare()
        self.assertEqual(second["version"], "0.1.2")
        (folder / "tests").mkdir()
        (folder / "tests/check.py").write_text("tests only")
        self.assertEqual(self.prepare(), second)
        (folder / "resources").mkdir()
        (folder / "resources/welcome.html").write_text("installer resources")
        self.assertEqual(self.prepare()["version"], "0.1.3")

    def test_icon_changes_share_version_and_ignore_timestamp_only_changes(self):
        self.prepare()
        icon = self.root / "assets/SineStackIcon.png"
        icon.parent.mkdir()
        icon.write_bytes(b"first icon")
        second = self.prepare()
        self.assertEqual(second["version"], "0.1.2")
        icon.touch()
        self.assertEqual(self.prepare(), second)
        icon.write_bytes(b"updated icon")
        self.assertEqual(self.prepare()["version"], "0.1.3")

    def test_corrupt_state_is_not_reset(self):
        path = self.root / "version.json"
        path.write_text("broken")
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertEqual(path.read_text(), "broken")

    def test_au_version_byte_rollover(self):
        self.assertEqual(versioning.next_version("0.1.254"), "0.1.255")
        self.assertEqual(versioning.next_version("0.1.255"), "0.2.0")
        self.assertEqual(versioning.next_version("0.255.255"), "1.0.0")
        with self.assertRaises(ValueError):
            versioning.next_version("invalid")


if __name__ == "__main__":
    unittest.main()
