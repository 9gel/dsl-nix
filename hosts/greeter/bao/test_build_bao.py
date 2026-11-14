import sys
import tempfile
import unittest
from pathlib import Path

import build_bao


class BaoTests(unittest.TestCase):
    def test_quiet_zone_pads_one_cell(self):
        zoned = build_bao.quiet_zone([[1]], 1)
        self.assertEqual(
            zoned, [[0, 0, 0], [0, 1, 0], [0, 0, 0]])

    def test_boing_is_a_wav(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "boing.wav"
            build_bao.write_boing(path)
            data = path.read_bytes()
        self.assertEqual(data[:4], b"RIFF")
        self.assertGreater(len(data), 1000)

    def test_urls_and_logo_lines(self):
        self.assertEqual(
            build_bao.MEETUP,
            "https://www.meetup.com/dimsumlabs/",
        )
        self.assertEqual(
            build_bao.TELEGRAM,
            "https://t.me/dimsumlabs",
        )
        self.assertEqual(
            build_bao.LOGO_LINES,
            ("DIM", "SUM", "LABS"),
        )

    def test_stacked_logo_geometry_from_circuit_glyphs(self):
        width, height, stroke, radius, placed = (
            build_bao.stacked_logo_geometry(1024)
        )
        self.assertEqual(height, 1024)
        self.assertGreater(width, 750)
        self.assertLess(width, 1100)
        self.assertGreater(stroke, 20)
        self.assertGreater(radius, stroke)
        self.assertEqual([len(r) for r in placed], [3, 3, 4])
        # D, A, and B preserve their counter holes.
        self.assertEqual(len(placed[0][0]), 2)
        self.assertEqual(len(placed[2][1]), 2)
        self.assertEqual(len(placed[2][2]), 3)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(
        BaoTests)
    result = unittest.TestResult()
    suite.run(result)
    bad = result.failures + result.errors
    if bad:
        for case, text in bad:
            sys.stderr.write(f"{case}\n{text}\n")
        raise SystemExit(1)
    print(f"build {result.testsRun}")
