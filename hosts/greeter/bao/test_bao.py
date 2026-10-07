import tempfile
import unittest
from pathlib import Path

import bao


class BaoTests(unittest.TestCase):
    def test_quiet_zone_pads_one_cell(self):
        zoned = bao.quiet_zone([[1]], 1)
        self.assertEqual(
            zoned, [[0, 0, 0], [0, 1, 0], [0, 0, 0]])

    def test_boing_is_a_wav(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "boing.wav"
            bao.write_boing(path)
            data = path.read_bytes()
        self.assertEqual(data[:4], b"RIFF")
        self.assertGreater(len(data), 1000)


if __name__ == "__main__":
    unittest.main()
