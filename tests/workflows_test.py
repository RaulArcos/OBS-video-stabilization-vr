"""Repository automation must be explicitly triggered and never publish releases."""
from pathlib import Path
import unittest
import yaml


def workflow_files(root):
    folder = root / '.github/workflows'
    return sorted([*folder.glob('*.yaml'), *folder.glob('*.yml')])


def triggers_of(path):
    document = yaml.safe_load(path.read_text(encoding='utf-8'))
    # YAML 1.1 treats the GitHub Actions key 'on' as boolean True.
    return document, document.get('on', document.get(True))


class WorkflowTests(unittest.TestCase):
    def test_manual_entry_points_and_read_only_contents(self):
        root = Path(__file__).resolve().parents[1]
        for path in workflow_files(root):
            with self.subTest(workflow=path.name):
                document = yaml.safe_load(path.read_text(encoding='utf-8'))
                # YAML 1.1 treats the GitHub Actions key 'on' as boolean True.
                triggers = document.get('on', document.get(True))
                self.assertIsInstance(triggers, dict)
                self.assertLessEqual(set(triggers), {'workflow_dispatch', 'workflow_call'})
                self.assertNotIn('contents: write', path.read_text(encoding='utf-8'))
                for job in document['jobs'].values():
                    for step in job.get('steps', []):
                        self.assertNotIn('action-gh-release', step.get('uses', ''))
                        self.assertNotIn('gh release', step.get('run', ''))

    def test_single_manual_entry_calls_read_only_reusable_workflows(self):
        root = Path(__file__).resolve().parents[1]
        entries = {path.name: triggers_of(path) for path in workflow_files(root)
                   if 'workflow_dispatch' in triggers_of(path)[1]}
        self.assertEqual(list(entries), ['manual-build.yaml'])
        document, _ = entries['manual-build.yaml']
        self.assertEqual(document['permissions'], {'contents': 'read'})
        called = {job['uses'] for job in document['jobs'].values()}
        self.assertEqual(called, {'./.github/workflows/check-format.yaml',
                                  './.github/workflows/build-project.yaml'})
        for name in ('check-format.yaml', 'build-project.yaml'):
            _, triggers = triggers_of(root / '.github/workflows' / name)
            self.assertEqual(set(triggers), {'workflow_call'})


if __name__ == '__main__':
    unittest.main()
