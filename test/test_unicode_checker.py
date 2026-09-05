"""Checker plumbing tests; mocks are NOT an extension or Unicode conformance oracle.

Run: PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s test -p 'test_unicode*.py'
"""

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

SPEC = importlib.util.spec_from_file_location(
    "check_unicode", Path(__file__).resolve().parents[1] / "scripts/check_unicode.py"
)
assert SPEC is not None and SPEC.loader is not None
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class UnicodeCheckerTests(unittest.TestCase):
    def test_scalar_domain_and_batch_boundaries(self):
        values = list(checker.scalars())
        self.assertEqual(len(values), 1112064)
        self.assertEqual(values[0], 0)
        self.assertEqual(values[-1], 0x10FFFF)
        self.assertEqual(values[0xD800 - 1 : 0xD800 + 1], [0xD7FF, 0xE000])
        self.assertEqual(list(checker.batches(range(5), 2)), [[0, 1], [2, 3], [4]])

    def test_scalar_mismatch_counts_and_truncated_oracle(self):
        connection = Mock()
        # Space verifies all three single/edge trim expectations. A deliberately
        # broken uppercase result verifies that mismatches cannot become a pass.
        connection.execute.return_value.fetchall.return_value = [
            (" ", " ", "", "", "", "x", "x ", " x", " "),
            ("a", "wrong", "A", "A", "A", "AxA", "AxA", "AxA", "A"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            oracle = Path(temporary) / "scalars.tsv"
            oracle.write_text("32\t32\t32\ttrue\t20\n65\t97\t65\tfalse\t41\n", encoding="ascii")
            with patch.object(checker, "scalars", return_value=iter([32, 65])), patch.object(
                checker, "SCALAR_COUNT", 2
            ), redirect_stdout(io.StringIO()):
                self.assertEqual(checker.check_scalars(connection, oracle, {}, 2), 1)
            connection.execute.assert_called_once()
            self.assertEqual(connection.execute.call_args.args[1], [[" ", "A"]])
            with patch.object(checker, "scalars", return_value=iter([32, 65, 66])), patch.object(
                checker, "SCALAR_COUNT", 3
            ), redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(ValueError, "Incomplete scalar"):
                    checker.check_scalars(connection, oracle, {}, 2)

    def test_whole_sequences_and_three_way_mismatches(self):
        cases = [("compose", "e\u0301", "\u00e9"), ("empty", "", "")]
        connection = Mock()
        connection.execute.return_value.fetchall.return_value = [("\u00e9",), ("",)]
        with tempfile.TemporaryDirectory() as temporary:
            oracle = Path(temporary) / "normalized.hex"
            oracle.write_text("c3a9\n\n", encoding="ascii")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(checker.check_sequences(connection, cases, oracle, 2), 0)
            self.assertEqual(connection.execute.call_args.args[1], [["e\u0301", ""]])
            connection.execute.return_value.fetchall.return_value = [("e\u0301",), ("",)]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(checker.check_sequences(connection, cases, oracle, 2), 2)
            connection.execute.return_value.fetchall.return_value = [(None,), ("",)]
            with redirect_stdout(io.StringIO()):
                self.assertEqual(checker.check_sequences(connection, cases, oracle, 2), 2)
            # An oracle bug is independently caught even if the extension agrees.
            connection.execute.return_value.fetchall.return_value = [("e\u0301",), ("",)]
            oracle.write_text("65cc81\n\n", encoding="ascii")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(checker.check_sequences(connection, cases, oracle, 2), 2)
            oracle.write_text("c3a9\n", encoding="ascii")
            with self.assertRaises(ValueError):
                checker.check_sequences(connection, cases, oracle, 2)

    def test_unpinned_normalization_file_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary) / "NormalizationTest.txt"
            data.write_text("# NormalizationTest-16.0.0.txt\n", encoding="ascii")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                checker.normalization_cases(data)

    def test_all_five_nfc_invariants_and_part1_identity_map(self):
        with tempfile.TemporaryDirectory() as temporary:
            data = Path(temporary) / "synthetic.txt"
            # A compatibility ligature must stay intact in c1..c3 but not c4..c5.
            data.write_text(
                "@Part1 # synthetic fixture\n" + "FB01;FB01;FB01;0066 0069;0066 0069; # fi\n" * 19965, encoding="ascii"
            )
            with patch.object(checker, "NORMALIZATION_SHA256", checker.sha256(data)):
                cases, part1 = checker.normalization_cases(data)
            self.assertEqual(len(cases), 99825)
            self.assertEqual([row[2] for row in cases[:5]], ["\ufb01"] * 3 + ["fi"] * 2)
            self.assertEqual(part1, {0xFB01: "\ufb01"})

    def test_canary_file_is_not_silently_skipped(self):
        connection = Mock()
        connection.execute.return_value.fetchall.return_value = [(True,)]
        with redirect_stdout(io.StringIO()):
            self.assertEqual(checker.canaries(connection), 0)
        self.assertEqual(connection.execute.call_count, 12)
        connection.execute.return_value.fetchall.return_value = [(False,)]
        with redirect_stdout(io.StringIO()):
            self.assertEqual(checker.canaries(connection), 12)


if __name__ == "__main__":
    unittest.main()
