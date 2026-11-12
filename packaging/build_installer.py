#!/usr/bin/env python3
"""Build the AU/VST3/app package and a terminal-installable Universal release."""
import argparse
import hashlib
import plistlib
import re
import shutil
import subprocess
import tempfile
import sys
import tarfile
from pathlib import Path
from xml.etree import ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from versioning import fingerprint, read_state, verify_bundle


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "dist")
    args = parser.parse_args()
    revision = read_state(ROOT)
    if fingerprint(ROOT) != revision["fingerprint"]:
        raise RuntimeError("Sources changed: build all formats with scripts/update_local.py --build-only first")
    formats = [
        ("au", ROOT / "sine stack au/build/SineStack_artefacts/Release/AU/Sine Stack.component", "/Library/Audio/Plug-Ins/Components", "AU — Logic Pro / GarageBand"),
        ("vst3", ROOT / "sine stack vst3/build/SineStack_artefacts/Release/VST3/Sine Stack.vst3", "/Library/Audio/Plug-Ins/VST3", "VST3 — VST3-compatible DAWs"),
        ("app", ROOT / "sine stack au/build/SineStack_artefacts/Release/Standalone/Sine Stack.app", "/Applications", "Sine Stack.app — Standalone"),
    ]
    versions = set()
    for _, bundle, _, _ in formats:
        with (bundle / "Contents/Info.plist").open("rb") as stream:
            info = plistlib.load(stream)
        verify_bundle(info, revision)
        if bundle.suffix == ".app" and not info.get("NSBluetoothAlwaysUsageDescription", "").strip():
            raise RuntimeError("Standalone is missing its Bluetooth usage description")
        versions.add(info["CFBundleShortVersionString"])
        binary = bundle / "Contents/MacOS" / info["CFBundleExecutable"]
        run("/usr/bin/lipo", binary, "-verify_arch", "arm64", "x86_64")
        run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", bundle)
        load_commands = subprocess.check_output(["/usr/bin/otool", "-l", str(binary)], text=True)
        minimums = re.findall(r"\bminos ([\d.]+)", load_commands)
        if len(minimums) != 2 or any(tuple(map(int, v.split("."))) > (11, 0) for v in minimums):
            raise RuntimeError(f"Expected macOS 11-compatible Universal binary: {binary}: {minimums}")
    if len(versions) != 1:
        raise RuntimeError(f"AU/VST3/Standalone version mismatch: {versions}")
    version = versions.pop()
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise RuntimeError(f"Unexpected version: {version}")
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    filename = f"SineStack-{version}-macOS-Universal-unsigned.pkg"
    archive_name = f"SineStack-{version}-macOS-Universal.tar.gz"
    with tempfile.TemporaryDirectory(prefix="sinestack-installer-") as temporary:
        work = Path(temporary)
        packages = work / "packages"
        packages.mkdir()
        for key, bundle, directory, _ in formats:
            payload = work / key
            payload.mkdir()
            run("/usr/bin/ditto", "--noextattr", "--norsrc", bundle, payload / bundle.name)
            run("/usr/bin/codesign", "--verify", "--deep", "--strict", "--all-architectures", payload / bundle.name)
            components = work / f"{key}-components.plist"
            with components.open("wb") as stream:
                plistlib.dump([{
                    "RootRelativeBundlePath": bundle.name,
                    "BundleIsRelocatable": False,
                    "BundleIsVersionChecked": True,
                    "BundleHasStrictIdentifier": True,
                    "BundleOverwriteAction": "upgrade",
                }], stream)
            run("/usr/bin/pkgbuild", "--root", payload,
                "--component-plist", components,
                "--identifier", f"audio.sinestack.pkg.{key}", "--version", version,
                "--install-location", directory,
                "--ownership", "recommended", packages / f"{key}.pkg")
        distribution = ET.Element("installer-gui-script", {"minSpecVersion": "2"})
        ET.SubElement(distribution, "title").text = f"Sine Stack {version}"
        ET.SubElement(distribution, "options", {
            "customize": "always", "require-scripts": "false", "hostArchitectures": "arm64,x86_64"})
        ET.SubElement(distribution, "domains", {
            "enable_localSystem": "true", "enable_currentUserHome": "false", "enable_anywhere": "false"})
        ET.SubElement(distribution, "welcome", {"file": "welcome.html", "mime-type": "text/html"})
        ET.SubElement(distribution, "readme", {"file": "readme.html", "mime-type": "text/html"})
        ET.SubElement(distribution, "conclusion", {"file": "conclusion.html", "mime-type": "text/html"})
        volume = ET.SubElement(distribution, "volume-check")
        ET.SubElement(ET.SubElement(volume, "allowed-os-versions"), "os-version", {"min": "11.0"})
        outline = ET.SubElement(distribution, "choices-outline")
        for key, _, directory, title in formats:
            ET.SubElement(outline, "line", {"choice": key})
            choice = ET.SubElement(distribution, "choice", {
                "id": key, "title": title, "start_selected": "true",
                "description": f"Universal (Apple Silicon + Intel) • {directory}"})
            ET.SubElement(choice, "pkg-ref", {"id": f"audio.sinestack.pkg.{key}"})
            ET.SubElement(distribution, "pkg-ref", {
                "id": f"audio.sinestack.pkg.{key}", "version": version, "onConclusion": "none"}).text = f"{key}.pkg"
        xml = work / "Distribution.xml"
        ET.indent(distribution)
        ET.ElementTree(distribution).write(xml, encoding="utf-8", xml_declaration=True)
        run("/usr/bin/productbuild", "--distribution", xml,
            "--resources", ROOT / "packaging/resources", "--package-path", packages, work / filename)
        release = work / f"SineStack-{version}"
        release.mkdir()
        for key, bundle, _, _ in formats:
            parent = release / {"au": "AU", "vst3": "VST3", "app": "Standalone"}[key]
            parent.mkdir()
            run("/usr/bin/ditto", "--noextattr", "--norsrc", bundle, parent / bundle.name)
        for name in ("install.sh", "INSTALL.txt"):
            shutil.copy2(ROOT / "packaging" / name, release / name)
        with (release / "release.plist").open("wb") as stream:
            plistlib.dump({"Format": "SineStackRelease", "FormatVersion": 1, "Version": version,
                           "BuildUTC": revision["built_at_utc"], "SourceFingerprint": revision["fingerprint"]}, stream)
        with tarfile.open(work / archive_name, "w:gz", format=tarfile.PAX_FORMAT) as archive:
            archive.add(release, arcname=release.name)
        shutil.copy2(work / filename, output / filename)
        shutil.copy2(work / archive_name, output / archive_name)
    shutil.copy2(ROOT / "packaging/INSTALL.txt", output / "INSTALL.txt")
    shutil.copy2(ROOT / "packaging/install.sh", output / "install.sh")
    assets = (filename, archive_name, "install.sh", "INSTALL.txt")
    sums = "".join(f"{hashlib.sha256((output / name).read_bytes()).hexdigest()}  {name}\n" for name in assets)
    (output / "SHA256SUMS.txt").write_text(sums, encoding="utf-8")
    print(f"Created: {output / filename}\nCreated: {output / archive_name}\nUnsigned package; bundles are ad-hoc signed, not notarized.")


if __name__ == "__main__":
    main()
