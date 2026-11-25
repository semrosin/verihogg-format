import tempfile
import unittest
from pathlib import Path

from history import record
from history_page import render_history
from test_history import snapshot


class HistoryPageTest(unittest.TestCase):
    def test_history_does_not_label_unverified_diagnostics_as_support(self):
        first = snapshot("a", {"Module": ["a"]}, [])
        second = snapshot("b", {"Port": ["b"]}, [])
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)
            record(archive, first, None)
            record(archive, second, 1)
            page = render_history(archive, "verihogg/verihogg-format", second)
            self.assertIn("1 diagnostic no longer observed", page)
            self.assertIn("1 new diagnostic", page)
            self.assertEqual(page.count('<details class="diagnostic-change">'), 2)
            self.assertNotIn("newly supported", page)

    def test_records_once_per_commit_and_config_and_resets_baseline(self):
        first = snapshot("a", {"Module": ["a"]}, [])
        second = snapshot("b", {}, ["a"], subject="Support <Module>")
        third = snapshot("c", {}, ["a"])
        same_commit_new_suite = snapshot("c", {"Port": ["c"]}, [], "suite-b")
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory)
            self.assertTrue(record(archive, first, None))
            self.assertTrue(record(archive, second, 2))
            self.assertTrue(record(archive, third, 1))
            self.assertFalse(record(archive, third, 0))
            self.assertTrue(record(archive, same_commit_new_suite, 0))
            page = render_history(archive, "verihogg/verihogg-format", same_commit_new_suite)
            self.assertEqual(len(list((archive / "snapshots").glob("*/*.json"))), 4)
            self.assertIn("Support &lt;Module&gt;", page)
            self.assertIn("1 newly supported construct", page)
            self.assertIn("1 tested commit with no confirmed support changes", page)
            self.assertIn("2 commits since the previous run", page)
            self.assertIn("Test configuration changed", page)
            self.assertEqual(page.count("See full report"), 1)
