"""Checks for corpus selection and weighted reporting, independent of codec timings."""
import csv
import importlib.util
from pathlib import Path
import struct
import shutil
from contextlib import contextmanager
import uuid
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("bench_runner", ROOT / "benchmarks" / "run.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


@contextmanager
def test_directory():
    root = (ROOT / "build" / "bench-tests").resolve()
    directory = root / uuid.uuid4().hex
    directory.mkdir(parents=True)
    try:
        yield directory
    finally:
        if not directory.resolve().is_relative_to(root):
            raise RuntimeError("Test cleanup escaped its output directory")
        shutil.rmtree(directory)


class BenchTests(unittest.TestCase):
    def test_stratified_manifest(self):
        with test_directory() as temp:
            base = Path(temp)
            corpus = base / "images"
            header = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 17, 19) + bytes([8, 6, 0, 0, 0])
            for category, count in [("sprites", 7), ("tiles", 2)]:
                (corpus / category).mkdir(parents=True)
                for i in range(count):
                    (corpus / category / f"{i}.png").write_bytes(header)
            args = SimpleNamespace(corpus=corpus, filter=None, per_category=3, limit=None, private_assets=True)
            path, data = runner.write_manifest(args, base)
            self.assertEqual(data["discovered"], 9)
            self.assertEqual(data["selected"], 5)
            self.assertEqual([x["name"] for x in data["images"]],
                             ["sprites/0.png", "sprites/3.png", "sprites/6.png", "tiles/0.png", "tiles/1.png"])
            self.assertEqual(len(path.read_text().splitlines()), 5)
            self.assertTrue(data["private_assets"])
            self.assertEqual(data["images"][0]["width"], 17)

    def test_aggregate_uses_total_pixels_over_total_time(self):
        with test_directory() as temp:
            base = Path(temp)
            samples = base / "samples.csv"
            fields = ["image", "width", "height", "codec", "operation", "mode", "sample", "encoded_bytes",
                      "profile", "encoded_hash", "elapsed_ns", "count"]
            with samples.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(fields)
                for sample, elapsed in enumerate([800, 880, 720]):
                    writer.writerow(["group/large", 100, 100, "rgi", "gpu_stream", "staged_rgba", sample, 1000, 1, 123, elapsed, 8])
                    writer.writerow(["group/small", 1, 1, "rgi", "gpu_stream", "staged_rgba", sample, 25, 0, 456, elapsed, 8])
            runner.summarize(base, [("candidate", 0, samples)])
            with (base / "totals.csv").open() as stream:
                totals = list(csv.DictReader(stream))
            total = next(row for row in totals if row["category"] == "ALL")
            self.assertEqual(float(total["total_ns"]), 200)
            self.assertEqual(int(total["total_pixels"]), 10001)
            self.assertAlmostEqual(float(total["mpps"]), 10001 * 1000 / 200)
            self.assertEqual(int(total["encoded_bytes"]), 1025)
            self.assertEqual(int(total["images"]), 2)

    def test_unicode_corpus_paths_survive_reporting(self):
        with test_directory() as temp:
            base = Path(temp)
            corpus = base / "images-\u96ea"
            image = corpus / "sprites" / "caf\u00e9-\u96ea.png"
            image.parent.mkdir(parents=True)
            image.write_bytes(b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" +
                              struct.pack(">II", 17, 19) + bytes([8, 6, 0, 0, 0]))
            args = SimpleNamespace(corpus=corpus, filter=None, per_category=None, limit=None,
                                   private_assets=True)
            manifest, data = runner.write_manifest(args, base)
            name = image.relative_to(corpus).as_posix()
            self.assertEqual(manifest.read_text(encoding="utf-8"), name + "\t" + str(image) + "\n")
            self.assertEqual(data["images"][0]["name"], name)

            samples = base / "samples.csv"
            with samples.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                writer.writerow(["image", "width", "height", "codec", "operation", "mode", "sample",
                                 "encoded_bytes", "profile", "encoded_hash", "elapsed_ns", "count"])
                writer.writerow([name, 17, 19, "rgi", "decode", "allocated", 0, 25, 0, 123, 800, 1])
            samples.with_suffix(".log").write_text("IMAGE\t" + name + "\t17x19\n", encoding="utf-8")
            runner.summarize(base, [("candidate", 0, samples)])
            with (base / "cases.csv").open(encoding="utf-8", newline="") as stream:
                self.assertEqual(next(csv.DictReader(stream))["image"], name)


if __name__ == "__main__":
    unittest.main()
