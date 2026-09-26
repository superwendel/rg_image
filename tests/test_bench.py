"""Checks for corpus selection and weighted reporting, independent of codec timings."""
import csv
import hashlib
import importlib.util
import json
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
    @staticmethod
    def _write_timing_samples(path, elapsed_values):
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["image", "width", "height", "codec", "operation", "mode", "sample",
                             "encoded_bytes", "profile", "encoded_hash", "elapsed_ns", "count"])
            for sample, elapsed in enumerate(elapsed_values):
                writer.writerow(["synthetic/tiny", 17, 19, "rgi", "decode", "reused", sample,
                                 100, 2, 123, elapsed, 1])

    def test_zero_timing_samples_remain_in_statistics_and_quality_report(self):
        with test_directory() as base:
            samples = base / "samples.csv"
            self._write_timing_samples(samples, [0, 20, 40])
            raw = samples.read_bytes()
            runner.summarize(base, [("candidate", 3, samples)])
            self.assertEqual(samples.read_bytes(), raw)
            with (base / "cases.csv").open(encoding="utf-8", newline="") as stream:
                case = next(csv.DictReader(stream))
            self.assertEqual(float(case["median_ns"]), 20)
            self.assertEqual(float(case["min_ns"]), 0)
            self.assertEqual(float(case["max_ns"]), 40)
            quality = json.loads((base / "timing-quality.json").read_text(encoding="utf-8"))
            self.assertEqual(quality["raw_sample_count"], 3)
            self.assertEqual(quality["zero_sample_count"], 1)
            self.assertEqual(quality["groups_affected_count"], 1)
            self.assertEqual(quality["groups_affected"], [{
                "variant": "candidate", "trial": 3, "image": "synthetic/tiny", "codec": "rgi",
                "operation": "decode", "mode": "reused", "zero_samples": 1,
                "raw_samples": 3, "median_ns": 20,
            }])

    def test_nonpositive_group_median_requests_more_iterations(self):
        for elapsed in ([0, 0, 0], [0, 0, 40]):
            with self.subTest(elapsed=elapsed), test_directory() as base:
                samples = base / "samples.csv"
                self._write_timing_samples(samples, elapsed)
                with self.assertRaisesRegex(RuntimeError, "increase --iterations"):
                    runner.summarize(base, [("candidate", 0, samples)])

    def test_negative_and_nonfinite_timing_samples_are_rejected(self):
        for invalid in (-1, float("nan"), float("inf"), float("-inf")):
            with self.subTest(invalid=invalid), test_directory() as base:
                samples = base / "samples.csv"
                self._write_timing_samples(samples, [20, invalid, 40])
                with self.assertRaises(RuntimeError):
                    runner.summarize(base, [("candidate", 0, samples)])

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
            self.assertEqual(data["input_format"], "png")
            self.assertFalse(data["deduplicate"])
            self.assertEqual(data["filtered_count"], 9)
            self.assertEqual(data["deduplicated_count"], 9)
            self.assertEqual(data["images"][0]["source_format"], "png")
            self.assertEqual(data["images"][0]["bit_depth"], 8)
            self.assertEqual(data["images"][0]["color_type"], 6)

    def test_rgi_manifest_profiles_and_little_endian_dimensions(self):
        with test_directory() as base:
            corpus = base / "images"
            corpus.mkdir()
            # Only metadata is parsed here; the native verifier checks payloads.
            for profile in (2, 0, 1):
                suffix = ".RGI" if profile == 1 else ".rgi"
                header = b"rgif" + struct.pack("<II", 258 + profile, 513) + bytes([4, profile])
                (corpus / f"profile{profile}{suffix}").write_bytes(header + b"\xc0" + b"\0" * 7 + b"\1")
            (corpus / "ignored.png").write_bytes(b"not read in RGI mode")
            args = SimpleNamespace(corpus=corpus, input_format="rgi", deduplicate=False,
                                   filter=None, per_category=None, limit=None, private_assets=True)
            manifest, data = runner.write_manifest(args, base)
            self.assertEqual(data["input_format"], "rgi")
            self.assertEqual((data["discovered"], data["filtered_count"],
                              data["deduplicated_count"], data["selected"]), (3, 3, 3, 3))
            self.assertEqual([entry["source_profile"] for entry in data["images"]], [0, 1, 2])
            self.assertEqual([entry["width"] for entry in data["images"]], [258, 259, 260])
            for entry in data["images"]:
                self.assertEqual(entry["source_format"], "rgi")
                self.assertEqual(entry["height"], 513)
            self.assertEqual(len(manifest.read_text(encoding="utf-8").splitlines()), 3)

    def test_manifest_deduplicates_full_sources_before_sampling_and_limit(self):
        with test_directory() as base:
            corpus = base / "images"
            header = (b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" +
                      struct.pack(">II", 17, 19) + bytes([8, 6, 0, 0, 0]))
            files = {"alpha/a.png": b"A", "alpha/b.png": b"B", "alpha/c.png": b"C",
                     "alpha/d.png": b"A", "beta/a.png": b"A", "beta/b.png": b"D",
                     "beta/c.png": b"D"}
            # Creation order must not affect the globally retained first path.
            for name, payload in reversed(list(files.items())):
                path = corpus / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(header + payload)
            args = SimpleNamespace(corpus=corpus, input_format="png", deduplicate=True,
                                   filter=None, per_category=2, limit=None, private_assets=True)
            _, data = runner.write_manifest(args, base)
            self.assertEqual((data["discovered"], data["filtered_count"],
                              data["deduplicated_count"], data["duplicates_removed"],
                              data["selected"]), (7, 7, 4, 3, 3))
            self.assertEqual([entry["name"] for entry in data["images"]],
                             ["alpha/a.png", "alpha/c.png", "beta/b.png"])
            self.assertTrue(data["deduplicate"])
            self.assertEqual(data["images"][0]["sha256"], hashlib.sha256(header + b"A").hexdigest())
            # The same header with different payload bytes is a distinct source.
            args.per_category = None
            args.limit = 4
            _, data = runner.write_manifest(args, base)
            self.assertEqual([entry["name"] for entry in data["images"]],
                             ["alpha/a.png", "alpha/b.png", "alpha/c.png", "beta/b.png"])
            self.assertEqual(data["selected"], 4)

    def test_manifest_filters_before_global_deduplication(self):
        with test_directory() as base:
            corpus = base / "images"
            header = b"rgif" + struct.pack("<II", 17, 19) + bytes([4, 2])
            files = {"drop/a.rgi": b"A", "keep/a.rgi": b"A",
                     "keep/b.rgi": b"B", "keep/c.rgi": b"B"}
            for name, payload in files.items():
                path = corpus / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(header + payload)
            args = SimpleNamespace(corpus=corpus, input_format="rgi", deduplicate=True,
                                   filter="keep/", per_category=None, limit=None, private_assets=True)
            _, data = runner.write_manifest(args, base)
            self.assertEqual((data["discovered"], data["filtered_count"],
                              data["deduplicated_count"], data["duplicates_removed"],
                              data["selected"]), (4, 3, 2, 1, 2))
            self.assertEqual([entry["name"] for entry in data["images"]],
                             ["keep/a.rgi", "keep/b.rgi"])

    def test_manifest_rejects_invalid_source_headers(self):
        rgi = b"rgif" + struct.pack("<II", 17, 19) + bytes([4, 2])
        png = (b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" +
               struct.pack(">II", 17, 19) + bytes([8, 6, 0, 0, 0]))
        invalid = [("rgi", rgi[:13]), ("rgi", b"RGIP" + rgi[4:]),
                   ("rgi", rgi[:12] + bytes([3, 2])), ("rgi", rgi[:13] + bytes([3])),
                   ("png", png[:28]), ("png", b"badmagic" + png[8:]),
                   ("png", png[:12] + b"IDAT" + png[16:])]
        for source_format, data in invalid:
            with self.subTest(source_format=source_format, header=data), test_directory() as base:
                corpus = base / "images"
                corpus.mkdir()
                (corpus / ("invalid." + source_format)).write_bytes(data)
                args = SimpleNamespace(corpus=corpus, input_format=source_format, deduplicate=False,
                                       filter=None, per_category=None, limit=None, private_assets=True)
                with self.assertRaises(RuntimeError):
                    runner.write_manifest(args, base)

    def test_manifest_retains_oversized_dimensions_for_explicit_exclusion(self):
        with test_directory() as base:
            corpus = base / "images"
            corpus.mkdir()
            (corpus / "large.rgi").write_bytes(b"rgif" + struct.pack("<II", 16385, 1) + bytes([4, 0]))
            args = SimpleNamespace(corpus=corpus, input_format="rgi", deduplicate=False,
                                   filter=None, per_category=None, limit=None, private_assets=True)
            _, data = runner.write_manifest(args, base)
            self.assertEqual(data["selected"], 1)
            self.assertEqual(data["images"][0]["width"], 16385)

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

    def test_palette_subsets_match_native_controls(self):
        with test_directory() as base:
            samples = base / "samples.csv"
            cases = [("selected", 2, 1, 1, 100, 60), ("eligible_loser", 3, 1, 0, 100, 120),
                     ("ineligible", 257, 0, 0, 100, 0)]
            lines = []
            with samples.open("w", newline="", encoding="utf-8") as stream:
                writer = csv.writer(stream)
                writer.writerow(["image", "width", "height", "codec", "operation", "mode", "sample",
                                 "encoded_bytes", "profile", "encoded_hash", "elapsed_ns", "count"])
                for name, colors, eligible, selected, native, forced in cases:
                    automatic = forced if selected else native
                    lines.append(f"PALETTE\t{name}\tcolors={colors} eligible={eligible} selected={selected} "
                                 f"rgi_bytes={native} rgip_bytes={forced} auto_bytes={automatic}")
                    writer.writerow([name, 10, 10, "rgi", "decode", "palette_control", 0, native, 0, 1, 100, 1])
                    writer.writerow([name, 10, 10, "rgip_auto", "decode", "allocated", 0, automatic, 254, 1, 80, 1])
                    if eligible:
                        writer.writerow([name, 10, 10, "rgip_forced", "decode", "allocated", 0, forced, 254, 1, 70, 1])
            samples.with_suffix(".log").write_text("\n".join(lines), encoding="utf-8")
            runner.summarize(base, [("candidate", 0, samples)])
            with (base / "palette_subsets.csv").open(encoding="utf-8", newline="") as stream:
                totals = {(r["subset"], r["codec"]): r for r in csv.DictReader(stream)}
            for subset, count in [("eligible", 2), ("selected", 1), ("eligible_fallback", 1)]:
                for codec in ("rgi", "rgip_auto", "rgip_forced"):
                    self.assertEqual(int(totals[(subset, codec)]["images"]), count)
            self.assertNotIn(("all", "rgip_forced"), totals)
            self.assertEqual(int(totals[("all", "rgip_auto")]["encoded_bytes"]), 260)
            self.assertEqual(int(totals[("eligible_fallback", "rgip_forced")]["encoded_bytes"]), 120)
            with (base / "palette.csv").open(encoding="utf-8", newline="") as stream:
                metadata = {r["image"]: r for r in csv.DictReader(stream)}
            self.assertEqual(metadata["ineligible"]["color_count_capped"], "1")
            self.assertEqual(metadata["eligible_loser"]["index_bits"], "2")


if __name__ == "__main__":
    unittest.main()
