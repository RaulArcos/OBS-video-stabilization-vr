"""tools/package_release.py validates staged files before writing any release artifact."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'package_release.py'
STAGING = 'release/staging/obs-vr-stabilizer'
REQUIRED = [
    'bin/64bit/obs-vr-stabilizer.dll',
    'data/shaders/luma-downsample.effect',
    'data/shaders/warp.effect',
    'data/locale/en-US.ini',
    'data/locale/es-ES.ini',
]
ZIP_NAME = 'release/obs-vr-stabilizer-0.1.0-windows-x64.zip'
SUM_NAME = ZIP_NAME + '.sha256'


class PackageReleaseTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.root = Path(self._tmp.name)
        (self.root / 'tools').mkdir()
        shutil.copy(SCRIPT, self.root / 'tools' / 'package_release.py')
        (self.root / 'buildspec.json').write_text(
            json.dumps({'name': 'obs-vr-stabilizer', 'version': '0.1.0'}), encoding='utf-8')
        (self.root / 'README.md').write_text('readme', encoding='utf-8')
        (self.root / 'LICENSE').write_text('license', encoding='utf-8')
        for rel in REQUIRED + ['bin/64bit/obs-vr-stabilizer.pdb']:
            self.write(f'{STAGING}/{rel}', rel)
        # Older artifacts that must survive every run.
        self.write('release/obs-vr-stabilizer-0.0.9-windows-x64.zip', 'old zip')
        self.write('release/notes.txt', 'other file')
        self.keep = self.snapshot()

    def write(self, rel, text):
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in (self.root / 'release').rglob('*')
                if p.is_file() and 'staging' not in p.relative_to(self.root).parts}

    def run_tool(self, *args):
        return subprocess.run(
            [sys.executable, '-I', str(self.root / 'tools' / 'package_release.py'), *args],
            cwd=self.root, capture_output=True, text=True, timeout=30)

    def assert_rejected(self, message):
        result = self.run_tool()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, '')
        self.assertEqual(result.stderr, message + '\n')
        self.assertEqual(self.snapshot(), self.keep)

    def test_valid_package_layout_and_checksum(self):
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f'wrote {ZIP_NAME}\nwrote {SUM_NAME}\n')
        self.assertEqual(result.stderr, '')
        with zipfile.ZipFile(self.root / ZIP_NAME) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(sorted(archive.namelist()),
                             sorted(['LICENSE', 'README.md'] + [f'obs-vr-stabilizer/{r}' for r in REQUIRED]))
        digest = hashlib.sha256((self.root / ZIP_NAME).read_bytes()).hexdigest()
        self.assertEqual((self.root / SUM_NAME).read_text(encoding='utf-8'),
                         f'{digest}  obs-vr-stabilizer-0.1.0-windows-x64.zip\n')
        for rel, data in self.keep.items():
            self.assertEqual((self.root / rel).read_bytes(), data)

    def test_checksum_file_uses_lf_line_ending_bytes(self):
        self.assertEqual(self.run_tool().returncode, 0)
        digest = hashlib.sha256((self.root / ZIP_NAME).read_bytes()).hexdigest()
        self.assertEqual((self.root / SUM_NAME).read_bytes(),
                         f'{digest}  obs-vr-stabilizer-0.1.0-windows-x64.zip\n'.encode())

    def test_rerun_replaces_only_target_outputs_and_adds_present_docs(self):
        self.assertEqual(self.run_tool().returncode, 0)
        self.write('docs/INSTALL.md', 'install')
        self.write('docs/RELEASE-0.1.0.md', 'notes')
        result = self.run_tool()
        self.assertEqual(result.returncode, 0, result.stderr)
        with zipfile.ZipFile(self.root / ZIP_NAME) as archive:
            self.assertIn('docs/INSTALL.md', archive.namelist())
            self.assertIn('docs/RELEASE-0.1.0.md', archive.namelist())
        for rel, data in self.keep.items():
            self.assertEqual((self.root / rel).read_bytes(), data)

    def test_missing_required_file_rejects_without_artifacts(self):
        (self.root / STAGING / 'data/shaders/warp.effect').unlink()
        self.assert_rejected(f'package_release: missing required file: {STAGING}/data/shaders/warp.effect')

    def test_empty_required_file_rejects_without_artifacts(self):
        self.write(f'{STAGING}/data/locale/es-ES.ini', '')
        self.assert_rejected(f'package_release: empty required file: {STAGING}/data/locale/es-ES.ini')

    def make_link(self, link, target):
        try:
            link.symlink_to(target, target_is_directory=True)
        except OSError:  # Windows without symlink rights: a junction is the same escape.
            made = sys.platform == 'win32' and subprocess.run(
                ['cmd', '/c', 'mklink', '/J', str(link), str(target)],
                capture_output=True, timeout=30).returncode == 0
            if not made:
                self.skipTest('symlinks unavailable')

    def test_symlink_rejects_without_artifacts(self):
        target = self.root / 'outside'
        target.mkdir()
        (target / 'secret.txt').write_text('outside', encoding='utf-8')
        self.make_link(self.root / STAGING / 'data/evil', target)
        self.assert_rejected(f'package_release: refusing symlink: {STAGING}/data/evil')

    def test_linked_stage_root_rejects_without_artifacts(self):
        outside = self.root / 'outside'
        outside.mkdir()
        shutil.copytree(self.root / STAGING, outside / 'obs-vr-stabilizer')
        shutil.rmtree(self.root / STAGING)
        self.make_link(self.root / STAGING, outside / 'obs-vr-stabilizer')
        self.assert_rejected(f'package_release: refusing symlink: {STAGING}')

    def test_linked_staging_parent_rejects_without_artifacts(self):
        outside = self.root / 'outside'
        outside.mkdir()
        shutil.copytree(self.root / STAGING, outside / 'obs-vr-stabilizer')
        shutil.rmtree(self.root / 'release/staging')
        self.make_link(self.root / 'release/staging', outside)
        self.assert_rejected('package_release: refusing symlink: release/staging')

    def test_optional_demo_gif_is_packaged_and_empty_one_rejected(self):
        self.write('docs/demo.gif', 'gif')
        self.assertEqual(self.run_tool().returncode, 0)
        with zipfile.ZipFile(self.root / ZIP_NAME) as archive:
            self.assertEqual(archive.read('docs/demo.gif'), b'gif')
        self.keep = self.snapshot()
        self.write('docs/demo.gif', '')
        self.assert_rejected('package_release: empty required file: docs/demo.gif')


if __name__ == '__main__':
    unittest.main()
