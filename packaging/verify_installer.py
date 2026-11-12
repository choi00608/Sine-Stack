#!/usr/bin/env python3
"""Extract and verify installer payloads/choices without installing anything."""
import argparse
import hashlib
import plistlib
import subprocess
import tempfile
import itertools
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent


def run(*args):
    return subprocess.check_output([str(arg) for arg in args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--check-choices", action="store_true", help="Query Installer against / without installing")
    args = parser.parse_args()
    package = args.package.resolve()
    with tempfile.TemporaryDirectory(prefix="sinestack-package-check-") as temporary:
        work = Path(temporary)
        expanded = work / "expanded"
        run("/usr/sbin/pkgutil", "--expand-full", package, expanded)
        for key, project, fmt, extension, folder in [
            ("au", "sine stack au", "AU", "component", "/Library/Audio/Plug-Ins/Components"),
            ("vst3", "sine stack vst3", "VST3", "vst3", "/Library/Audio/Plug-Ins/VST3"),
            ("app", "sine stack au", "Standalone", "app", "/Applications"),
        ]:
            component = expanded / f"{key}.pkg"
            info = ET.parse(component / "PackageInfo").getroot()
            assert info.get("install-location") == folder
            assert info.get("identifier") == f"audio.sinestack.pkg.{key}"
            assert info.get("relocatable") == "false"
            assert len(info.find("relocate")) == 0
            assert len(info.find("upgrade-bundle")) == 1
            assert info.find("scripts") is None
            bundle_name = f"Sine Stack.{extension}"
            payload = component / "Payload"
            assert {p.name for p in payload.iterdir()} == {bundle_name}
            bundle = payload / bundle_name
            original = ROOT / project / "build/SineStack_artefacts/Release" / fmt / bundle_name
            def files(base):
                return {str(p.relative_to(base)): hashlib.sha256(p.read_bytes()).hexdigest()
                        for p in base.rglob("*") if p.is_file()}
            assert files(bundle) == files(original), f"{key}: payload differs from validated build"
            binary = bundle / "Contents/MacOS/Sine Stack"
            assert binary.stat().st_mode & 0o111
            run("/usr/bin/lipo", binary, "-verify_arch", "arm64", "x86_64")
            run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", bundle)
            bom = run("/usr/bin/lsbom", "-s", component / "Bom").decode()
            assert bundle_name + "/Contents/MacOS/Sine Stack" in bom
            print(f"PASS {fmt}: exact payload, Universal binary, signatures, destination and upgrade rules")
        distribution = ET.parse(expanded / "Distribution").getroot()
        assert distribution.find("volume-check/allowed-os-versions/os-version").get("min") == "11.0"
        assert distribution.find("options").get("hostArchitectures") == "arm64,x86_64"
        assert distribution.find("domains").get("enable_localSystem") == "true"
        assert {c.get("id") for c in distribution.findall("choice")} == {"au", "vst3", "app"}
        assert all(c.get("start_selected") == "true" for c in distribution.findall("choice"))
        for resource in ("welcome", "readme", "conclusion"):
            assert (expanded / "Resources" / distribution.find(resource).get("file")).is_file()
        print("PASS distribution: macOS 11+, both architectures, all-user domain, installer resources")
        if args.check_choices:
            for au, vst3, app in itertools.product((0, 1), repeat=3):
                if not (au or vst3 or app):
                    continue
                changes = work / "choices.plist"
                with changes.open("wb") as stream:
                    plistlib.dump([{"choiceIdentifier": key, "choiceAttribute": "selected", "attributeSetting": selected}
                                   for key, selected in (("au", au), ("vst3", vst3), ("app", app))], stream)
                result = plistlib.loads(run("/usr/sbin/installer", "-showChoicesAfterApplyingChangesXML", changes,
                                             "-pkg", package, "-target", "/"))
                selection = {c["choiceIdentifier"]: int(c["attributeSetting"]) for c in result
                             if c["choiceAttribute"] == "selected" and c["choiceIdentifier"] in ("au", "vst3", "app")}
                assert selection == {"au": au, "vst3": vst3, "app": app}, selection
                print(f"PASS Installer selection: AU={au}, VST3={vst3}, app={app}")
        print("PASS: package verified; no installation performed")


if __name__ == "__main__":
    main()
