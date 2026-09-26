"""Build and run serial, reproducible RGI CPU or SDL GPU comparisons (Python 3.12+)."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import struct
import subprocess
import sys
import time
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def command_output(args, cwd=ROOT):
    result = subprocess.run(args, cwd=cwd, capture_output=True, text=True, errors="replace")
    return result.stdout.strip() or result.stderr.strip()


def find_images(root, input_format="png"):
    if input_format not in ("png", "rgi"):
        raise RuntimeError(f"Unsupported input format: {input_format}")
    def walk(directory):
        for path in sorted(directory.iterdir()):
            if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
                continue
            if path.is_dir():
                yield from walk(path)
            elif path.suffix.lower() == "." + input_format:
                yield path
    return sorted(walk(root), key=lambda p: p.relative_to(root).as_posix())


def write_manifest(args, output):
    if not args.corpus:
        return None, {"kind": "synthetic", "seed": "0x52474946", "patterns": 9,
                      "primary": "pixel-art", "filter": args.filter}
    root = args.corpus.resolve()
    input_format = getattr(args, "input_format", "png")
    deduplicate = getattr(args, "deduplicate", False)
    images = find_images(root, input_format)
    discovered = len(images)
    if args.filter:
        images = [p for p in images if args.filter in p.relative_to(root).as_posix()]
    filtered_count = len(images)
    hashes = {}
    if deduplicate:
        unique = []
        seen = set()
        for path in images:
            sha = digest(path)
            hashes[path] = sha
            if sha not in seen:
                unique.append(path)
                seen.add(sha)
        images = unique
    deduplicated_count = len(images)
    if args.per_category:
        categories = defaultdict(list)
        for path in images:
            parts = path.relative_to(root).parts
            categories[parts[0] if len(parts) > 1 else "root"].append(path)
        chosen = []
        for paths in categories.values():
            count = min(len(paths), args.per_category)
            indexes = [0] if count == 1 else [i * (len(paths) - 1) // (count - 1) for i in range(count)]
            chosen.extend(paths[i] for i in indexes)
        images = sorted(chosen, key=lambda p: p.relative_to(root).as_posix())
    if args.limit:
        images = images[:args.limit]
    if not images:
        raise RuntimeError(f"No {input_format.upper()} images matched")
    entries = []
    manifest = output / "inputs.tsv"
    with manifest.open("w", encoding="utf-8", newline="\n") as stream:
        for path in images:
            name = path.relative_to(root).as_posix()
            if any(c in str(path) + name for c in "\t\r\n"):
                raise RuntimeError(f"Unsupported control character in image path: {path}")
            with path.open("rb") as source:
                header = source.read(29)
            if input_format == "png":
                if len(header) < 29 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
                    raise RuntimeError(f"Not a PNG: {path}")
                width, height = struct.unpack(">II", header[16:24])
                properties = {"bit_depth": header[24], "color_type": header[25]}
            else:
                if len(header) < 14 or header[:4] != b"rgif" or header[12] != 4 or header[13] > 2:
                    raise RuntimeError(f"Invalid or unsupported RGI header: {path}")
                width, height = struct.unpack("<II", header[4:12])
                properties = {"source_profile": header[13]}
            entries.append({"name": name, "sha256": hashes[path] if path in hashes else digest(path),
                            "source_bytes": path.stat().st_size, "source_format": input_format,
                            "width": width, "height": height, **properties})
            stream.write(name + "\t" + str(path) + "\n")
    return manifest, {"kind": "corpus", "root": str(root), "discovered": discovered,
                      "input_format": input_format, "deduplicate": deduplicate,
                      "filtered_count": filtered_count, "deduplicated_count": deduplicated_count,
                      "duplicates_removed": filtered_count - deduplicated_count,
                      "private_assets": args.private_assets, "per_category": args.per_category,
                      "selected": len(entries), "images": entries}


def build(args, output, label, source_root):
    binary_dir = output / ("build-" + label)
    binary_dir.mkdir(parents=True, exist_ok=True)
    executable = binary_dir / ("bench_image.exe" if os.name == "nt" else "bench_image")
    includes = [source_root / "src", args.core_root / "src"]
    defines = []
    libraries = []
    fingerprints = {}
    if not args.no_libpng:
        prefix = args.png_prefix.resolve()
        if not (prefix / "include" / "png.h").exists():
            raise RuntimeError("libpng is missing; run python tools/prepare_bench.py --png or supply --png-prefix")
        includes.append(prefix / "include")
        defines.append("RG_IMAGE_BENCH_LIBPNG")
        names = ["libpng16_static.lib", "zs.lib"] if os.name == "nt" else ["libpng16.a", "libz.a"]
        for name in names:
            library = prefix / "lib" / name
            if not library.exists():
                raise RuntimeError(f"Missing static dependency: {library}")
            libraries.append(str(library))
            fingerprints[str(library)] = digest(library)
    if args.gpu:
        if not args.sdl_root:
            raise RuntimeError("Supply --sdl-root or SDL3_DIR for GPU benchmarks")
        includes.append(args.sdl_root / "include")
        defines.append("RG_IMAGE_BENCH_GPU")
        if os.name == "nt":
            libraries.append(str(args.sdl_root / "lib" / "x64" / "SDL3.lib"))
            fingerprints["SDL3.dll"] = digest(args.sdl_root / "lib" / "x64" / "SDL3.dll")
        else:
            libraries += ["-L" + str(args.sdl_root / "lib"), "-lSDL3"]
    if args.scalar:
        defines.append("RG_RGI_NO_SIMD")
    if args.legacy_encode:
        defines.append("RG_RGI_NO_PALETTE_ENCODE")
    palette_experiment = args.palette_experiment and (not args.baseline_root or label == "candidate")
    if palette_experiment:
        defines.append("RG_IMAGE_BENCH_PALETTE")
    sources = [ROOT / "benchmarks" / "bench_image.c", ROOT / "benchmarks" / "bench_sink.c"]
    if os.name == "nt":
        compiler = args.compiler or "cl"
        command = [compiler, "/nologo", "/O2", "/MD", "/W4", "/WX", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS"]
        command += ["/D" + x for x in defines] + ["/I" + str(x) for x in includes]
        command += list(map(str, sources)) + ["/Fe:" + str(executable), "/link", "/WX"] + libraries
    else:
        compiler = args.compiler or "clang"
        command = [compiler, "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror", "-D_POSIX_C_SOURCE=200809L"]
        command += ["-D" + x for x in defines] + ["-I" + str(x) for x in includes]
        command += list(map(str, sources)) + ["-o", str(executable)] + libraries + ["-lm"]
    print(f"Building {label}", flush=True)
    with (binary_dir / "build.log").open("w", encoding="utf-8") as log:
        subprocess.run(command, cwd=binary_dir, stdout=log, stderr=subprocess.STDOUT, check=True)
    inputs = sources + [ROOT / "benchmarks" / "bench_codecs.h", source_root / "src" / "rg_rgi.h"]
    if args.gpu:
        inputs.append(ROOT / "benchmarks" / "bench_gpu.h")
    if palette_experiment:
        inputs += [ROOT / "benchmarks" / "bench_palette.h"]
        inputs += sorted((ROOT / "benchmarks" / "experimental").glob("*.h"))
    inputs += sorted((ROOT / "third_party").glob("stb*.h")) + [ROOT / "third_party" / "qoi" / "qoi.h"]
    inputs += [args.core_root / "src" / x for x in ("rg_defs.h", "rg_time.h")]
    if args.gpu:
        inputs.append(args.core_root / "src" / "rg_gpu.h")
    fingerprints.update({str(p): digest(p) for p in inputs})
    metadata = {"source_root": str(source_root), "command": command, "inputs": fingerprints,
                "executable_sha256": digest(executable),
                "compiler": command_output([compiler, "/Bv"] if os.name == "nt" else [compiler, "--version"])}
    (binary_dir / "build.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    return executable, metadata


def summarize(output, files):
    groups = defaultdict(list)
    examples = {}
    exclusions = set()
    palettes = {}
    for label, trial, file in files:
        log = file.with_suffix(".log")
        if log.exists():
            for line in log.read_text(encoding="utf-8").splitlines():
                if line.startswith("EXCLUDED\t"):
                    exclusions.add(line)
                elif line.startswith("PALETTE\t"):
                    _, name, details = line.split("\t", 2)
                    palettes[(label, trial, name)] = {
                        key: int(value) for key, value in (field.split("=", 1) for field in details.split())}
        with file.open(newline="", encoding="utf-8") as stream:
            for row in csv.DictReader(stream):
                key = (label, trial, row["image"], row["codec"], row["operation"], row["mode"])
                elapsed = float(row["elapsed_ns"])
                if not math.isfinite(elapsed) or elapsed < 0:
                    raise RuntimeError(f"Negative or nonfinite timing for {key}")
                count = int(row["count"])
                if count <= 0:
                    raise RuntimeError(f"Nonpositive operation count for {key}")
                groups[key].append(elapsed / count)
                examples[key] = row
    medians = {key: statistics.median(samples) for key, samples in groups.items()}
    for key, ns in medians.items():
        if ns <= 0:
            raise RuntimeError(f"Nonpositive median timing for {key}; increase --iterations")
    key_fields = ["variant", "trial", "image", "codec", "operation", "mode"]
    affected = [{**dict(zip(key_fields, key)), "zero_samples": samples.count(0.0),
                 "raw_samples": len(samples), "median_ns": medians[key]}
                for key, samples in sorted(groups.items()) if 0.0 in samples]
    timing_quality = {"raw_sample_count": sum(len(samples) for samples in groups.values()),
                      "zero_sample_count": sum(item["zero_samples"] for item in affected),
                      "groups_affected_count": len(affected), "groups_affected": affected}
    (output / "timing-quality.json").write_text(json.dumps(timing_quality, indent=2) + "\n", encoding="utf-8")
    aggregate = defaultdict(lambda: {"pixels": 0, "ns": 0, "bytes": 0, "raw": 0, "images": 0})
    fields = ["variant", "trial", "image", "codec", "operation", "mode", "median_ns", "min_ns", "max_ns", "encoded_bytes", "encoded_hash", "profile"]
    with (output / "cases.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(fields)
        for key, samples in sorted(groups.items()):
            label, trial, name, codec, operation, mode = key
            row = examples[key]
            ns = medians[key]
            writer.writerow([*key, ns, min(samples), max(samples), row["encoded_bytes"], row["encoded_hash"], row["profile"]])
            category = name.rsplit("/", 1)[0] if "/" in name else "root"
            for scope in ("ALL", category):
                item = aggregate[(label, trial, scope, codec, operation, mode)]
                pixels = int(row["width"]) * int(row["height"])
                item["pixels"] += pixels
                item["ns"] += ns
                item["bytes"] += int(row["encoded_bytes"])
                item["raw"] += pixels * 4
                item["images"] += 1
    with (output / "totals.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["variant", "trial", "category", "codec", "operation", "mode", "images", "total_pixels", "total_ns", "mpps", "encoded_bytes", "raw_bytes", "size_percent"])
        for key, item in sorted(aggregate.items()):
            writer.writerow([*key, item["images"], item["pixels"], item["ns"], item["pixels"] * 1e3 / item["ns"], item["bytes"], item["raw"], item["bytes"] * 100 / item["raw"]])
    lines = ["# RGI benchmark results", "", "CPU allocated modes include output allocation; validation and release are outside timing. Reused modes exclude allocation.",
             "Preparation_single is one cold encode, not a repeated measurement. GPU latency includes resource creation and fence completion; streaming reuses resources.",
             "Throughput is total pixels / sum of per-case median times. A gpu_scene_* case is an entire mixed scene; all other cases are individual images. Source PNG rows are separate from generated PNG rows.", "",
             "| Variant | Trial | Codec | Operation | Mode | Cases | ms/case | MP/s | Encoded bytes |",
             "| --- | ---: | --- | --- | --- | ---: | ---: | ---: | ---: |"]
    for (label, trial, scope, codec, operation, mode), item in sorted(aggregate.items()):
        if scope != "ALL":
            continue
        lines.append(f"| {label} | {trial} | {codec} | {operation} | {mode} | {item['images']} | {item['ns'] / item['images'] / 1e6:.4f} | {item['pixels'] * 1e3 / item['ns']:.2f} | {item['bytes']} |")
    baseline = {(trial, name, codec, op, mode): statistics.median(values)
                for (label, trial, name, codec, op, mode), values in groups.items() if label == "baseline"}
    comparisons = defaultdict(list)
    for (label, trial, name, codec, operation, mode), values in groups.items():
        key = (trial, name, codec, operation, mode)
        if label == "candidate" and key in baseline:
            comparisons[(codec, operation, mode)].append(statistics.median(values) / baseline[key])
    if comparisons:
        lines += ["", "## Paired per-image changes", "", "Positive means slower. Confirm small differences in an independent run; medians across images are not aggregate throughput.", "",
                  "| Codec | Operation | Mode | Median change |", "| --- | --- | --- | ---: |"]
        for key, values in sorted(comparisons.items()):
            lines.append("| " + " | ".join(key) + f" | {(statistics.median(values) - 1) * 100:+.2f}% |")
    if palettes:
        with (output / "palette.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["variant", "trial", "image", "colors", "color_count_capped", "index_bits",
                             "eligible", "selected", "rgi_bytes", "rgip_bytes", "auto_bytes"])
            for key, item in sorted(palettes.items()):
                colors = item["colors"]
                bits = 0 if not item["eligible"] else 1 if colors <= 2 else 2 if colors <= 4 else 4 if colors <= 16 else 8
                writer.writerow([*key, colors, int(colors == 257), bits, item["eligible"], item["selected"],
                                 item["rgi_bytes"], item["rgip_bytes"], item["auto_bytes"]])
        subsets = defaultdict(lambda: {"pixels": 0, "ns": 0, "bytes": 0, "images": 0})
        for key, samples in groups.items():
            label, trial, name, codec, operation, mode = key
            if not ((codec == "rgi" and mode == "palette_control") or
                    (codec in ("rgip_auto", "rgip_forced") and mode == "allocated")):
                continue
            item = palettes.get((label, trial, name))
            if item is None:
                continue
            scopes = ["all"] if codec != "rgip_forced" else []
            if item["eligible"]:
                scopes.append("eligible")
            if item["selected"]:
                scopes.append("selected")
            else:
                if codec != "rgip_forced":
                    scopes.append("fallback")
                if item["eligible"]:
                    scopes.append("eligible_fallback")
            row = examples[key]
            for scope in scopes:
                total = subsets[(label, trial, scope, codec, operation, mode)]
                total["pixels"] += int(row["width"]) * int(row["height"])
                total["ns"] += statistics.median(samples)
                total["bytes"] += int(row["encoded_bytes"])
                total["images"] += 1
        with (output / "palette_subsets.csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.writer(stream)
            writer.writerow(["variant", "trial", "subset", "codec", "operation", "mode", "images",
                             "total_pixels", "total_ns", "mpps", "encoded_bytes"])
            for key, item in sorted(subsets.items()):
                writer.writerow([*key, item["images"], item["pixels"], item["ns"],
                                 item["pixels"] * 1e3 / item["ns"], item["bytes"]])
        lines += ["", "## Experimental palette comparison", "",
                  "RGIP is a private lossless experiment, not a public RGI profile. Profile 254 identifies RGIP rows.",
                  "rgip_auto includes native fallback when palette encoding is ineligible or not smaller. Its encode time includes both attempts.",
                  "rgip_forced reports every eligible input, including candidates larger than native RGI. Colors=257 means detection stopped at the 257th colour.",
                  "The native palette_control is interleaved with auto/forced measurements. Forced rows use matching eligible, selected and eligible_fallback subsets.",
                  "palette.csv retains raw candidate sizes; palette_subsets.csv separates all, eligible, selected, fallback and eligible_fallback aggregates.", "",
                  "| Variant | Trial | Inputs | Eligible | Selected | Native bytes | Automatic bytes |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
        counts = defaultdict(lambda: {"images": 0, "eligible": 0, "selected": 0, "native": 0, "auto": 0})
        for (label, trial, _), item in palettes.items():
            total = counts[(label, trial)]
            total["images"] += 1
            total["eligible"] += item["eligible"]
            total["selected"] += item["selected"]
            total["native"] += item["rgi_bytes"]
            total["auto"] += item["auto_bytes"]
        for (label, trial), item in sorted(counts.items()):
            lines.append(f"| {label} | {trial} | {item['images']} | {item['eligible']} | {item['selected']} | {item['native']} | {item['auto']} |")
    lines += ["", f"Excluded inputs: {len(exclusions)} distinct image(s); reasons are recorded in the run logs.",
              f"Timing quality: {timing_quality['zero_sample_count']} of {timing_quality['raw_sample_count']} raw samples "
              f"were zero at timer resolution, retained in {timing_quality['groups_affected_count']} positive-median group(s) "
              "without filtering or clamping. Details are in timing-quality.json.",
              "Raw samples, per-image results, category totals, exclusions, build commands and input hashes accompany this report.", ""]
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", choices=["bench", "bench_corpus", "bench_gpu", "test_gpu", "bench_profile"])
    parser.add_argument("--corpus", type=Path)
    parser.add_argument("--input-format", choices=["png", "rgi"], default="png",
                        help="source file type for --corpus; native RGI artwork is decoded before timing")
    parser.add_argument("--deduplicate", action="store_true",
                        help="keep first sorted path per source SHA256 before category sampling and limits")
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "benchmarks" / (time.strftime("%Y%m%d-%H%M%S") + f"-{os.getpid()}"))
    parser.add_argument("--core-root", type=Path, default=Path(os.environ.get("RG_CORE_DIR", ROOT.parent / "rg_core")))
    parser.add_argument("--candidate-root", type=Path, default=ROOT)
    parser.add_argument("--label", choices=["baseline", "candidate"], default="candidate")
    parser.add_argument("--baseline-root", type=Path)
    parser.add_argument("--png-prefix", type=Path, default=ROOT / "build" / "bench-deps" / "prefix")
    parser.add_argument("--no-libpng", action="store_true", help="explicit three-codec smoke benchmark")
    parser.add_argument("--gpu", choices=["direct3d12", "vulkan", "metal"])
    parser.add_argument("--gpu-scene", action="store_true", help="measure mixed groups of up to eight images; timings are per scene")
    parser.add_argument("--palette-experiment", action="store_true", help="add isolated lossless RGIP automatic/forced CPU comparisons")
    parser.add_argument("--sdl-root", type=Path, default=os.environ.get("SDL3_DIR"))
    parser.add_argument("--compiler")
    parser.add_argument("--scalar", action="store_true")
    parser.add_argument("--legacy-encode", action="store_true", help="emit only RGI profiles 0/1; retain decoding support for all profiles")
    parser.add_argument("--samples", type=int, default=7)
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--runs", type=int, default=1, help="alternating baseline/candidate process pairs")
    parser.add_argument("--filter")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--per-category", type=int, help="deterministic evenly spaced sample per top-level category")
    parser.add_argument("--private-assets", action="store_true", help="keep reports in ignored build output; never save image fixtures")
    parser.add_argument("--decode-only", action="store_true")
    parser.add_argument("--verify-only", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--profile-encoder", action="store_true")
    parser.add_argument("--save-fixtures", type=Path)
    args = parser.parse_args()
    if args.target == "bench_corpus" and args.corpus is None:
        args.corpus = ROOT / "build" / "bench-deps" / "corpus" / "images"
    if not args.corpus and (args.input_format != "png" or args.deduplicate):
        parser.error("--input-format rgi and --deduplicate require --corpus")
    if args.target in ("bench_gpu", "test_gpu") and not args.gpu:
        args.gpu = "direct3d12" if os.name == "nt" else "vulkan"
    if args.target == "test_gpu":
        args.samples = 1
    if args.target == "bench_profile":
        args.profile_encoder = True
    if args.palette_experiment and (args.gpu or args.gpu_scene or args.profile_encoder or args.save_fixtures):
        parser.error("--palette-experiment is CPU-only and cannot save encoded fixtures")
    if args.gpu_scene and (not args.gpu or args.save_fixtures or args.decode_only or args.verify_only or args.profile_encoder):
        parser.error("--gpu-scene requires --gpu and cannot be combined with CPU-only flags or fixtures")
    if args.gpu and not args.sdl_root and os.name == "nt":
        candidates = []
        for directory in Path("C:/libs").glob("SDL3-*"):
            try:
                version = tuple(map(int, directory.name.removeprefix("SDL3-").split(".")))
            except ValueError:
                continue
            if version >= (3, 4, 0) and (directory / "include" / "SDL3" / "SDL.h").exists():
                candidates.append((version, directory))
        if candidates:
            args.sdl_root = max(candidates)[1]
    if min(args.samples, args.iterations, args.runs) < 1 or (args.limit is not None and args.limit < 1):
        parser.error("sample, iteration, run and limit counts must be positive")
    if args.per_category is not None and args.per_category < 1:
        parser.error("--per-category must be positive")
    for name in ("core_root", "candidate_root", "baseline_root", "sdl_root"):
        if getattr(args, name):
            setattr(args, name, getattr(args, name).resolve())
    output = args.output.resolve()
    if args.private_assets and (args.save_fixtures or not output.is_relative_to(ROOT / "build")):
        parser.error("private asset results must stay under ignored build/ and cannot save fixtures")
    output.mkdir(parents=True, exist_ok=True)
    if (output / "environment.json").exists():
        raise RuntimeError(f"Results already exist at {output}; choose a new --output directory")
    manifest, corpus = write_manifest(args, output)
    (output / "corpus.json").write_text(json.dumps(corpus, indent=2) + "\n", encoding="utf-8")
    variants = {args.label: args.candidate_root}
    if args.baseline_root:
        variants = {"baseline": args.baseline_root, "candidate": args.candidate_root}
    builds = {label: build(args, output, label, source) for label, source in variants.items()}
    environment = {"platform": platform.platform(), "machine": platform.machine(), "processor": platform.processor(),
                   "python": sys.version, "utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                   "revision": command_output(["git", "rev-parse", "HEAD"]),
                   "core_revision": command_output(["git", "rev-parse", "HEAD"], args.core_root),
                   "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                   "builds": {label: data for label, (_, data) in builds.items()}}
    if os.name == "nt":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            environment["cpu_name"] = winreg.QueryValueEx(key, "ProcessorNameString")[0]
    (output / "environment.json").write_text(json.dumps(environment, indent=2) + "\n", encoding="utf-8")
    if args.build_only:
        print(f"Build metadata: {output}", flush=True)
        return
    files = []
    for trial in range(args.runs):
        order = list(builds)
        if trial % 2:
            order.reverse()
        for label in order:
            executable, metadata = builds[label]
            if digest(executable) != metadata["executable_sha256"]:
                raise RuntimeError("Benchmark executable changed after build")
            command = [str(executable), "--samples", str(args.samples), "--iterations", str(args.iterations)]
            if manifest:
                command += ["--manifest", str(manifest)]
            elif args.filter:
                command += ["--filter", args.filter]
            if args.gpu:
                command += ["--gpu", args.gpu]
            if args.gpu_scene:
                command += ["--gpu-scene"]
            if args.palette_experiment and (not args.baseline_root or label == "candidate"):
                command += ["--palette-experiment"]
            if args.decode_only:
                command += ["--decode-only"]
            if args.verify_only:
                command += ["--verify-only"]
            if args.profile_encoder:
                command += ["--profile-encoder"]
            if args.save_fixtures:
                fixture_dir = args.save_fixtures.resolve() / label
                fixture_dir.mkdir(parents=True, exist_ok=True)
                command += ["--save-fixtures", str(fixture_dir)]
            env = os.environ.copy()
            if args.gpu:
                if os.name == "nt":
                    variable, library_dir = "PATH", args.sdl_root / "lib" / "x64"
                else:
                    variable = "DYLD_LIBRARY_PATH" if sys.platform == "darwin" else "LD_LIBRARY_PATH"
                    library_dir = args.sdl_root / "lib"
                previous = env.get(variable, "")
                env[variable] = str(library_dir) + (os.pathsep + previous if previous else "")
            csv_path = output / f"{label}-{trial:02}.csv"
            log_path = output / f"{label}-{trial:02}.log"
            print(f"Running {label} trial {trial + 1}; progress: {log_path}", flush=True)
            with csv_path.open("w", encoding="utf-8") as samples, log_path.open("w", encoding="utf-8") as log:
                subprocess.run(command, cwd=ROOT, env=env, stdout=samples, stderr=log, check=True)
            files.append((label, trial, csv_path))
    summarize(output, files)
    print(f"Report: {output / 'report.md'}", flush=True)


if __name__ == "__main__":
    # Windows pipes otherwise use the active ANSI code page, which cannot
    # represent all valid input/output paths printed by the runner.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
