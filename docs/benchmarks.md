# RGI performance evaluation

Pixel art is the primary workload: sprites, transparent UI, tilemaps, and
animation sheets. Photographs are secondary stress tests. All codecs must
preserve canonical RGBA8 pixels exactly, including RGB under zero alpha.
See [measured results](benchmark-results.md) for decisions and hardware.
Timings are not directly comparable to the [QOI website](https://qoiformat.org/benchmark/).

## Running

Use Python 3.12+, CMake, and an x64 C compiler. On Windows use a Visual Studio
Developer Command Prompt. `RG_CORE_DIR` defaults to sibling `rg_core`.

```bat
build.bat test
build.bat test_bench
build.bat bench
python tools\prepare_bench.py --png --corpus
python benchmarks\run.py --samples 7 --runs 3
build.bat bench_corpus --decode-only --samples 7
python benchmarks\run.py --baseline-root path\to\baseline --filter pixel-art --runs 3
python benchmarks\run.py --corpus path\to\assets --private-assets --per-category 16 --runs 3
build.bat bench_profile --filter pixel-art
```

`build.bat bench` explicitly selects the three-codec smoke test without libpng.
The Python runner defaults to four codecs and fails if libpng is absent.
Preparation downloads SHA256-pinned libpng 1.6.58 and zlib 1.3.2, builds static
Release libraries, and downloads the QOI corpus into ignored `build/bench-deps/`.
It records the archive and image hashes. External images are not redistributed.
If an archive changes or extraction is interrupted, use a fresh `--directory`;
the preparer refuses to overlay a different corpus onto existing images.
For comparisons, the baseline directory must contain its original `src/rg_rgi.h`;
both variants use the current harness. Linux uses Clang by default.
`--png-prefix`, `--core-root`, and `--compiler` support custom dependencies.

`--private-assets` reads in place, restricts results to ignored `build/`, and
forbids fixture output. Do not publish private manifests, logs, or per-image
results. `--per-category` selects evenly spaced paths from each sorted top-level
category; it is reproducible, not random or weighted by in-game use. `--filter`,
`--limit`, `--scalar`, and `--verify-only` support focused checks.
`--profile-encoder` separately measures profile-0 encoding, the unbounded
profile-1 estimator, and forced profile-1 emission; these diagnostic stages
do not replace automatic encoder measurements. Stage sizes exclude the file
header and end marker; the estimator's checksum field is its estimated size.

GPU tests require SDL3 >= 3.4 and a supported device. Set `SDL3_DIR` or pass
`--sdl-root`; Windows also discovers SDKs under `C:/libs`. Optional
`python tools\prepare_bench.py --sdl` downloads the pinned Windows 3.4.14 SDK.

```bat
build.bat test_gpu --gpu direct3d12 --filter odd-small
build.bat test_gpu --gpu vulkan --filter odd-small
build.bat bench_gpu --gpu direct3d12 --filter pixel-art --samples 7
build.bat bench_gpu --gpu vulkan --corpus path\to\assets --private-assets --per-category 16
```

The requested backend must match the actual backend; no silent fallback.
Runtime RGI still depends only on `rg_defs.h`, not PNG, QOI, SDL, or Python.

## CPU method

Without a corpus, nine original patterns use seed `0x52474946`: flat runs,
repeating tiles, gradient, noise, varying alpha, an odd-size image, palette
sprites, a tilemap, and an animation sheet. The last three form `pixel-art`.
Dimensions include 17x19, 257x129, 256x256, 640x360, and 512x512.

All codecs receive identical tightly packed RGBA8 pixels. Source PNGs are
normalized without gamma conversion; 16-bit channels reduce to 8 bits.
Losslessness refers to these canonical pixels, not source metadata or 16-bit
samples. Every image gets a byte-for-byte round-trip check through every codec
before timing. Original PNGs are independently decoded by both PNG libraries.
Trusted RGI decode follows successful checked decode. Timed output is fully
hashed outside timing in a separate translation unit without LTO. Hashing also
makes these warm-memory measurements.

Codecs are automatic-profile RGI, vendored reference QOI, stb PNG
(stb_image_write 1.16, compression 8, adaptive filters; stb_image 2.30), and
libpng 1.6.58/zlib 1.3.2 (compression 6, all filters). All encoded output is
materialized, not merely counted. PNG implementations are separate results.
`source_png` measures original corpus PNGs, whose palettes/filters/bit depths
can differ. stb timing excludes the converter's CRC/Adler integrity preflight;
libpng retains normal integrity checks. These pipelines do different work.

`allocated` includes output allocation but excludes validation, hashing, and
output release for all codecs. RGI `reused` excludes caller buffer/workspace
allocation; `trusted_reused` is a separate validated-input-only result.
Each image gets one discarded warmup and seven samples by default, rotating
codec order. Each sample averages `--iterations` calls (at least 32 below
16 KiB). Timing uses monotonic `rg_time`; Windows CPU runs pin to one available
logical processor.

`--decode-only` still encodes once, records sizes, and verifies round trips.
Its `encode/preparation_single` rows are cold single observations, not repeated
encoder measurements. This makes full-corpus coverage practical without
spending most of a run on photo encoding. Use repeated primary-asset runs for
encoder decisions. Variants build before measurement and run serially, reversing
process order on alternating trials. Do not run builds/tests/benchmarks concurrently.

Outputs retain raw samples, per-image medians/extrema, category totals, profiles,
checksums, build commands, executable/source/input hashes, dimensions, and
environment metadata. Default output directories are unique; existing runs
cannot be overwritten. Aggregate throughput is total pixels divided by the sum
of per-image median times, not an average of rates. Paired percentage changes
are medians of per-image candidate/baseline ratios, a different statistic.
Malformed input or codec/pixel failure is fatal; unsupported RGI dimensions
are explicitly logged as `EXCLUDED`. Interrupted runs are incomplete.
The raw `encoded_hash` field hashes encoded bytes for generated codec streams;
`source_png` rows instead carry the canonical pixel hash, and the GPU `rgba8`
control hashes its uncompressed pixels. Source-file SHA256 is in the manifest.

## GPU method

The harness uses `rg_core` upload rings and SDL GPU RGBA8_UNORM textures.
`gpu_latency` includes texture/staging creation, decode, packing, submission,
and upload fence completion. `gpu_stream` reuses three independent slots and
textures, submits all three before waiting, and normalizes by uploaded texture
count. Each slot holds up to eight copies of the current image, targeting
64 MiB total staging memory (one large image can exceed this target).
This is repeated single-image batch streaming, not mixed-scene or disk streaming.

All codecs have straight and premultiplied staging paths. Premultiplication
uses `(channel * alpha + 127) / 255`, matching `rg_text`. Staged rows align to
256 bytes and offsets to 512 bytes. RGI also decodes directly into mapped
transfer memory with tight rows; SDL can internally repack these on D3D12.
Predecoded RGBA8 upload provides an overhead control. Direct decoding and
reused CPU decoding have different allocation costs and are reported separately.

Raw CSV records CPU decode, staging, submission, and fence-wait intervals.
These are host measurements, not GPU timestamp queries; total time includes
other overhead such as allocation and command recording. Readback, outside
timing, checks every texture after warmup and the final sample. A separate
alternating-content test checks three-slot reuse over four rounds, with
odd-width tight and padded uploads. Presentation, shaders, mip generation,
disk I/O, and readback are excluded.

## Regression checks and ecosystem fit

Tests cover both legacy profiles, a frozen pre-change synthetic fixture,
truncated streams and destination guards, copy/run boundaries, unaligned
buffers, bounded estimation, dirty workspace reuse, C++, custom allocation,
explicit scalar builds, and converter round trips. Existing `rgi_convert`
can re-encode `.rgi` to `.rgi`; the retained change needs no format migration.
CI configures Windows MSVC, Linux Clang sanitizers, and a GPU compile check.
Device/full-corpus tests are explicit; timing changes never fail CI.

The review used [rg_core](https://github.com/superwendel/rg_core/tree/d5d3f4413da22568572a37f5c6bf4e0506c68a2a),
[rg_gui](https://github.com/superwendel/rg_gui/tree/f7a65957787159d8f25c6ee9f2ca41dbec6c76e7), and
[rg_text](https://github.com/superwendel/rg_text/tree/5331db7dee83338dacbf8f2e7b90d69acb60bac1)
at the linked revisions. RGI retains internal linkage,
shared core definitions, workspace/allocator hooks, and its small dependency
surface. Benchmarks use core timing/upload helpers and renderer-compatible
premultiplication. This is a convention and upload compatibility review, not
full application integration. No sibling repository was changed.
