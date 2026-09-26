# RGI performance evaluation

Pixel art is the primary workload: sprites, transparent UI, tilemaps, and
animation sheets. Photographs are secondary stress tests. All codecs must
preserve canonical RGBA8 pixels exactly, including RGB under zero alpha.
See [performance results](performance-results.md) for current measurements,
hardware, and workload limitations.
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
python benchmarks\run.py --corpus path\to\assets --input-format rgi --deduplicate --private-assets --decode-only --runs 3
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
`--input-format png|rgi` selects corpus file extensions (PNG by default).
`--deduplicate` keeps the first sorted path for each complete source-file SHA256,
after filtering and before category sampling or the global limit. This removes
byte-identical files, not different encodings of identical pixels. A duplicate
shared by categories belongs to the first retained category. The manifest records
discovered, filtered, deduplicated, and selected counts, removed duplicates,
source format, dimensions, source hashes, and original RGI profiles. Without
deduplication, every selected path remains a separate input.

For a native game-asset comparison, use an RGI corpus with deduplication and a
saved legacy `src/rg_rgi.h` as `--baseline-root`. Omitting `--per-category` measures
every distinct source file; a category cap provides broader coverage in a shorter
GPU run. Keep the corpus root fixed: changing it changes category boundaries.
Neither a category cap nor a byte-unique census represents how often a game loads
each asset. Small source layers, large sheets, and real scene loading frequencies
need separate interpretation. Consecutive GPU groups are synthetic scene batches.

```bat
python benchmarks\run.py --corpus path\to\assets --input-format rgi --deduplicate --private-assets --baseline-root path\to\legacy --decode-only --samples 7 --runs 3
python benchmarks\run.py --corpus path\to\assets --input-format rgi --deduplicate --private-assets --baseline-root path\to\legacy --per-category 64 --gpu direct3d12 --gpu-scene --samples 7 --runs 3
```

`--legacy-encode` defines `RG_RGI_NO_PALETTE_ENCODE` in each benchmark variant,
limiting output to profiles 0/1 without removing profile-2 decoding support.
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
Native RGI inputs are checked and decoded to RGBA8 outside timing, then freshly
encoded by every codec. These results measure re-encoded native artwork, not the
performance of the original shipped streams or complete game startup. The source
RGI must be supported by both benchmark variants: a legacy reader rejects an
input already using profile 2. Inputs are read in place and never rewritten.
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
can differ. These rows are emitted only for PNG source inputs; there is no
original-source timing row for RGI inputs. stb timing excludes the converter's CRC/Adler integrity preflight;
libpng retains normal integrity checks. These pipelines do different work.

`allocated` includes output allocation but excludes validation, hashing, and
output release for all codecs. RGI `reused` excludes caller buffer/workspace
allocation; `trusted_reused` is a separate validated-input-only result.
Each image gets one discarded warmup and seven samples by default, rotating
codec order. Each sample averages `--iterations` calls (at least 32 below
16 KiB). Timing uses monotonic `rg_time`; Windows CPU runs pin to one available
logical processor.

Very short calls can produce zero samples at the timer's resolution. The
summary retains those observations in minima and medians without dropping or
clamping them; `timing-quality.json` records their counts and affected groups.
Negative or nonfinite timings are rejected, as is any nonpositive group median
(rerun that workload with more iterations). Tiny-image relative differences
near the timer resolution should not be treated as precise speedups.

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
`staged_reused_rgba` and `staged_reused_premultiplied` use one CPU decode buffer
allocated before timing and shared sequentially across uploads. The original
`staged_rgba` and `staged_premultiplied` modes retain their per-image allocation.
Use aligned staging as the D3D12 default; direct mapped decoding remains an
explicit option to evaluate on Vulkan and other target devices.

For a game loader, retain a CPU decode buffer sized for the largest queued
image and reuse its transfer arena and destination textures.
[`bench_gpu_stage_image`](../benchmarks/bench_gpu.h#L125) demonstrates checked
decoding into that buffer followed by aligned packing. The CPU buffer can be
reused immediately after packing because the GPU reads the transfer arena.
[`bench_gpu_scene_submit`](../benchmarks/bench_gpu.h#L388) demonstrates batching
pending uploads into one copy pass and waiting for completion. Finish the
arena's previous submissions before mapping it again; use additional arenas
when the loader needs work in flight. These are tested integration examples,
not additions to the codec's public API.

`--gpu-scene` groups consecutive prepared inputs into scenes of up to eight
distinct images, flushing before their combined RGBA8 size exceeds 64 MiB
(a larger single image is allowed). A scene owns reusable textures and one
transfer arena, with separately aligned regions for each image. Each timed
sample decodes and packs every image once, then submits either one command per
texture (`gpu_scene_single`) or one command containing all uploads
(`gpu_scene_batch`). It waits for every submission to finish. Grouping order
alternates between scenes. Resource and CPU scratch allocation are outside
these measurements; allocated codec adapters still allocate decoded pixels.
This measures complete scene uploads without overlapping rendering or disk I/O.

Scene CSV rows represent a whole scene with `count=1`, `width=total pixels`,
`height=1`, and summed encoded bytes. Their synthetic dimensions are reporting
metadata, not an image shape. RGI `profile=255` marks a scene containing separate
streams; it is not a format profile. Scene checksums combine each input's
dimensions and encoded-byte hash in sequence (pixel hashes for the RGBA8
control). Scene membership and original dimensions remain in the ordered
manifest and `IMAGE`/`SCENE` log entries. Compare scene totals with scene totals,
not individual-image latency.

Raw CSV records CPU decode, staging, submission, and fence-wait intervals.
These are host measurements, not GPU timestamp queries; total time includes
other overhead such as allocation and command recording. Readback, outside
timing, checks every texture after warmup and the final sample. A separate
alternating-content test checks three-slot reuse over six rounds using distinct
odd-width image sizes, nonuniform RGBA pixels, and allocated/reused/direct
decoding with straight/premultiplied uploads. Presentation, shaders, mip generation,
disk I/O, and readback are excluded.

## Isolated palette experiment

`--palette-experiment` adds a CPU-only lossless RGIP prototype. It uses its
own magic and decoder under `benchmarks/experimental/`. Its successful pixel-art
results led to public RGI profile 2, which uses the same palette/tokens with a
normal `rgif` header. The isolated prototype remains available for comparison;
use `--legacy-encode` to reproduce its comparison against profiles 0/1.
No encoded fixtures are saved.

```powershell
python benchmarks/run.py --legacy-encode --palette-experiment --verify-only --output build/palette-check
python benchmarks/run.py --legacy-encode --palette-experiment --filter pixel-art --samples 7 --runs 3 --output build/palette-generated
python benchmarks/run.py --legacy-encode --palette-experiment --corpus ASSETS --private-assets --per-category 16 --samples 7 --runs 3 --output build/palette-assets
```

The format has a 16-byte header, up to 256 exact RGBA palette entries, and an
8-byte end marker (24 fixed bytes, versus profiles 0/1's 22). Indices use 1, 2, 4, or 8
bits. Literal packets hold up to 64 indices; short/long runs and nonoverlapping
COPY packets reuse previously decoded pixels. Palette entries preserve RGB
under zero alpha. The checked parser validates palette indices, packed padding,
input/output bounds, COPY offsets, dimensions, the footer, and exact consumption.

`rgip_auto` actually encodes both native RGI and the prototype on every timed
encode call, retaining the smaller complete stream. Inputs with more than 256
distinct RGBA values fall back to native RGI. `rgip_forced` reports every
eligible candidate, including larger outputs. Automatic decoder dispatch is
inside timing. Both use allocated output; there is no trusted RGIP decoder.
The prototype encoder allocates one index byte per pixel, an output buffer
bounded by two bytes per pixel plus 1,048 bytes, and the existing COPY table;
automatic selection also retains the native candidate during the second attempt.

Native RGI `palette_control` and automatic/forced RGIP rotate within every
image's samples. Ordinary QOI/PNG/RGI comparisons also run, alternating their
block order across images. Use the interleaved control for palette conclusions.
`palette.csv` records eligibility, selection, and raw candidate sizes;
`palette_subsets.csv` provides matching populations for native, automatic, and
forced measurements. A colour count of 257 means detection stopped at the
257th distinct value, not that the image contains exactly 257 colours.
CSV `profile=254` identifies experimental RGIP data and is not a public RGI
profile ID. Private per-image details remain under ignored `build/`.

Independent selftests run even with `--verify-only`, covering handcrafted
packets, palette boundaries, hidden RGB, truncated/corrupt input, destination
guards, overlapping buffers, and literal/run packing choices. Normal RGI and
GPU builds do not compile this prototype unless the experiment is selected.

## Regression checks and ecosystem fit

Tests cover all three profiles, a frozen pre-change synthetic fixture,
truncated streams and destination guards, copy/run boundaries, unaligned
buffers, bounded estimation/emission, partial-write fallback and profile ties,
dirty workspace reuse, C++, custom allocation,
explicit scalar builds, and converter round trips. Existing `rgi_convert`
can re-encode `.rgi` to `.rgi`. Update readers before distributing profile-2
assets; old profile-0/1 assets continue to decode. A converter built with
`RG_RGI_NO_PALETTE_ENCODE` can re-encode profile 2 for older readers.
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
