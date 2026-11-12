"""Exercise the real macOS installer against signed release bundles in temporary user folders."""
import hashlib
import io
import json
import plistlib
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path

ARCHIVE = Path(sys.argv.pop(1)).resolve()
SCRIPT = Path(__file__).resolve().parents[1] / 'install.sh'
CHECKSUMS = ARCHIVE.parent / 'SHA256SUMS.txt'
VERSION = ARCHIVE.name.removeprefix('SineStack-').removesuffix('-macOS-Universal.tar.gz')
ROOT_NAME = 'SineStack-' + VERSION


def snapshot(folder):
    return {str(p.relative_to(folder)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in folder.rglob('*') if p.is_file()}


class TerminalInstallerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sinestack-terminal-check-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.user = self.root / 'Test User'
        self.user.mkdir()
        self.settings = self.user / 'Library/Application Support/SineStack/Appearance.settings'
        self.settings.parent.mkdir(parents=True)
        self.settings.write_text('keep my theme')
        self.preset = self.user / 'Documents/Personal.sinestack'
        self.preset.parent.mkdir()
        self.preset.write_text('keep my preset')
        self.bundles = [self.user / 'Library/Audio/Plug-Ins/Components/Sine Stack.component',
                        self.user / 'Library/Audio/Plug-Ins/VST3/Sine Stack.vst3',
                        self.user / 'Applications/Sine Stack.app']
        self.state = self.settings.parent / 'Installer/state.plist'

    def install(self, *options, script=SCRIPT, archive=ARCHIVE, online=False, ok=True):
        command = ['/bin/bash', str(script), '--user-root', str(self.user)]
        if not online:
            command.extend(['--archive', str(archive)])
        result = subprocess.run([*command, *map(str, options)], capture_output=True, text=True)
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def assert_preserved(self):
        self.assertEqual(self.settings.read_text(), 'keep my theme')
        self.assertEqual(self.preset.read_text(), 'keep my preset')

    def change_installed_version(self, version):
        for bundle in self.bundles:
            p = bundle / 'Contents/Info.plist'
            data = plistlib.loads(p.read_bytes())
            data['CFBundleShortVersionString'] = data['CFBundleVersion'] = version
            p.write_bytes(plistlib.dumps(data))

    def tampered_archive(self, kind):
        folder = self.root / kind
        folder.mkdir()
        archive = folder / ARCHIVE.name
        with tarfile.open(ARCHIVE, 'r:gz') as original, tarfile.open(archive, 'w:gz') as out:
            for member in original.getmembers():
                if kind == 'missing-app' and member.name.startswith(ROOT_NAME + '/Standalone'):
                    continue
                data = original.extractfile(member).read() if member.isfile() else None
                if kind == 'bad-signature' and member.name.endswith('Sine Stack.app/Contents/Info.plist'):
                    info = plistlib.loads(data)
                    info['CFBundleName'] = 'Tampered signature fixture'
                    data = plistlib.dumps(info)
                    member.size = len(data)
                if kind == 'version-mismatch' and member.name.endswith('Sine Stack.app/Contents/Info.plist'):
                    info = plistlib.loads(data)
                    info['CFBundleShortVersionString'] = '9.9.9'
                    data = plistlib.dumps(info)
                    member.size = len(data)
                out.addfile(member, io.BytesIO(data) if data is not None else None)
            if kind in ('traversal', 'symlink'):
                entry = tarfile.TarInfo(ROOT_NAME + ('/../../escaped' if kind == 'traversal' else '/link'))
                if kind == 'symlink':
                    entry.type = tarfile.SYMTYPE
                    entry.linkname = '../../escaped'
                    out.addfile(entry)
                else:
                    entry.size = 4
                    out.addfile(entry, io.BytesIO(b'oops'))
        (folder / 'SHA256SUMS.txt').write_text(hashlib.sha256(archive.read_bytes()).hexdigest() + '  ' + archive.name + '\n')
        return archive

    def mock_download_script(self, invalid=False):
        fetch = self.root / 'mock-curl.py'
        fetch.write_text(f'''#!{sys.executable}
import json, shutil, sys
from pathlib import Path
args=sys.argv[1:]; destination=Path(args[args.index('--output')+1]); url=args[-1]
if url.endswith('/releases/latest'):
    destination.write_text(json.dumps({{'tag_name': {'"../../unexpected"' if invalid else repr('v'+VERSION)}}}))
elif url.endswith('/SHA256SUMS.txt'):
    shutil.copyfile({str(CHECKSUMS)!r},destination)
elif url.endswith('/{ARCHIVE.name}'):
    shutil.copyfile({str(ARCHIVE)!r},destination)
else:
    sys.exit(22)
''')
        fetch.chmod(0o755)
        script = self.root / 'online-installer.sh'
        script.write_text(SCRIPT.read_text().replace('/usr/bin/curl ', shlex.quote(str(fetch)) + ' '))
        return script

    def test_check_only_does_not_install(self):
        before = snapshot(self.user)
        self.install('--check')
        self.assertEqual(snapshot(self.user), before)

    def test_first_install_reinstall_and_update_preserve_settings(self):
        self.install()
        for bundle in self.bundles:
            self.assertEqual(plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'], VERSION)
        self.assertEqual((self.state.parent / 'install.sh').read_text(), SCRIPT.read_text())
        inodes = [p.stat().st_ino for p in self.bundles]
        self.install()
        self.assertEqual([p.stat().st_ino for p in self.bundles], inodes)
        self.change_installed_version('0.0.0')
        self.install()
        self.assert_preserved()
        self.assertTrue(all(plistlib.loads((p / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString'] == VERSION for p in self.bundles))

    def test_downgrade_needs_explicit_flag(self):
        self.install()
        self.change_installed_version('99.0.0')
        before = snapshot(self.user)
        result = self.install(ok=False)
        self.assertIn('newer', result.stderr)
        self.assertEqual(snapshot(self.user), before)
        self.install('--allow-downgrade')
        self.assert_preserved()

    def test_checksum_failure_changes_nothing(self):
        folder = self.root / 'broken'; folder.mkdir()
        archive = folder / ARCHIVE.name
        archive.write_bytes(ARCHIVE.read_bytes() + b'corruption')
        shutil.copy2(CHECKSUMS, folder / CHECKSUMS.name)
        before = snapshot(self.user)
        self.assertIn('Checksum mismatch', self.install(archive=archive, ok=False).stderr)
        self.assertEqual(snapshot(self.user), before)

    def test_missing_component_and_mismatched_versions_rejected(self):
        for kind in ('missing-app', 'version-mismatch', 'bad-signature'):
            with self.subTest(kind=kind):
                before = snapshot(self.user)
                self.install(archive=self.tampered_archive(kind), ok=False)
                self.assertEqual(snapshot(self.user), before)

    def test_unsafe_archive_paths_and_links_rejected_before_extraction(self):
        for kind in ('traversal', 'symlink'):
            with self.subTest(kind=kind):
                self.install(archive=self.tampered_archive(kind), ok=False)
                self.assertFalse((self.root / 'escaped').exists())
                self.assertFalse(self.bundles[0].exists())

    def test_partial_commit_failure_restores_all_previous_bundles(self):
        self.install()
        self.change_installed_version('0.0.0')
        before = snapshot(self.user)
        move = self.root / 'fail-move.sh'
        move.write_text('#!/bin/bash\nif [ "${1##*/}" = new ] && [ "$2" = ' + shlex.quote(str(self.bundles[1])) + ' ]; then exit 42; fi\nexec /bin/mv "$@"\n')
        move.chmod(0o755)
        script = self.root / 'failing-installer.sh'
        script.write_text(SCRIPT.read_text().replace('/bin/mv ', shlex.quote(str(move)) + ' '))
        self.install(script=script, ok=False)
        self.assertEqual(snapshot(self.user), before)
        self.assertFalse(list(self.user.rglob('.SineStack-stage.*')))

    def test_existing_install_lock_is_not_removed(self):
        lock = self.settings.parent / '.install-lock'
        lock.mkdir(); (lock / 'pid').write_text('fixture')
        self.install(ok=False)
        self.assertEqual((lock / 'pid').read_text(), 'fixture')

    def test_online_latest_and_remembered_repository(self):
        script = self.mock_download_script()
        self.install('--repo', 'example/sine-stack', script=script, online=True)
        self.assertEqual(plistlib.loads(self.state.read_bytes())['Repository'], 'example/sine-stack')
        self.install(script=script, online=True)
        self.assert_preserved()

    def test_default_repository_and_pinned_release(self):
        script = self.mock_download_script()
        self.install(script=script, online=True)
        self.assertEqual(plistlib.loads(self.state.read_bytes())['Repository'], 'choi00608/Sine-Stack')
        # Invalid latest metadata must not matter when the caller pins a version.
        script = self.mock_download_script(invalid=True)
        self.install('--version', VERSION, script=script, online=True)
        self.assert_preserved()

    def test_invalid_online_tag_is_rejected(self):
        before = snapshot(self.user)
        script = self.mock_download_script(invalid=True)
        self.install('--repo', 'example/sine-stack', script=script, online=True, ok=False)
        self.assertEqual(snapshot(self.user), before)


if __name__ == '__main__':
    unittest.main()
