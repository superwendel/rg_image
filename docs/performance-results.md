# Performance results

RGI is a lossless RGBA8 format intended for game assets. On the private
pixel-art library measured here, the current codec decodes about twice as fast
as reference QOI with 40.66% fewer encoded bytes. Encoding is substantially
slower than QOI, and PNG has a compression advantage on the broader public
corpus. These results support an offline asset pipeline; they do not predict
every image's performance or a game's startup time.

All results below use production RGI profiles 0/1/2. Every profile preserves
all RGBA values, including RGB beneath zero alpha. See the
[format specification](rgi_format.md) for compatibility and decoding contracts.

## Datasets and measurement

Measurements were collected on September 25-26, 2026 using an Intel Core
i7-12700KF, Windows 11, and MSVC 19.44 x64 with `/O2 /MD`. GPU measurements
used an NVIDIA GeForce RTX 2060 and SDL 3.4.10 with D3D12 and Vulkan.
The [benchmark guide](benchmarks.md) records codec versions, PNG settings,
timing boundaries, and setup instructions.

| Dataset | Coverage | Purpose |
| --- | --- | --- |
| Private game-asset library | 2,882 distinct RGI source files; 249,157,476 pixels | CPU decode and encoded size |
| Private source-art sample | 128 source PNGs, selected with at most 16 per top-level category | Repeated encode and decode timings |
| Public QOI benchmark corpus | 2,847 supported images from a 2,848-image archive | Broader image comparison |
| Private GPU subset | 525 images from the game-asset library, in 66 batches | Decode plus completed texture upload |

The game-asset library contains 14,630 RGI files before deduplication by
source-file SHA-256. Different streams that decode to the same pixels can
still count separately. This is a library inventory, not a workload weighted
by gameplay frequency. Private artwork, manifests, and per-image results are
not distributed, so readers cannot independently rerun those exact datasets.
The public corpus is available through the benchmark preparation script.

Inputs are decoded outside timing and freshly encoded by each codec from
identical RGBA8 pixels. Generated PNGs use the configured RGBA encoders; they
are not optimized indexed PNGs. Original-source PNG results are reported
separately where source PNGs exist. Each trial uses seven samples after a
discarded warmup. Its ms/image aggregate is the sum of per-image median times
divided by image count. Private comparisons use the median of three alternating
baseline/current trial pairs; the public comparison uses one process.

## Pixel-art CPU results

These results cover all 2,882 distinct files in the private game-asset library.
The table includes output allocation in decode timing. Disk reads, output
release, correctness hashing, and GPU uploads are outside timing.

| Codec | Allocated decode, ms/image | Total encoded bytes |
| --- | ---: | ---: |
| RGI, checked | 0.058658 | 24,195,307 |
| QOI reference | 0.117213 | 40,774,295 |
| PNG, stb | 0.348752 | 41,816,905 |
| PNG, libpng | 0.426640 | 29,225,647 |

RGI has 2.00 times QOI's aggregate allocated decode throughput and uses
40.66% fewer bytes. It uses 17.21% fewer bytes than generated libpng output.
Original authored PNGs are not part of this input set. PNG decode paths also
differ in integrity checking; see the [CPU method](benchmarks.md#cpu-method).

Compared with the optimized RGI encoder restricted to profiles 0/1, palette
selection reduces encoded size from 29,596,914 to 24,195,307 bytes (18.25%)
and allocated decode time from 0.062178 to 0.058658 ms/image (5.66%). The
encoder selects profile 2 for 2,335 images; the other 547 retain their previous
encoded bytes. All inputs pass checked and trusted round trips in every run.

Palette selection minimizes file size, not decode time. Individual images
can become slower. These aggregate changes also differ from a median of
per-image percentage changes, which weights tiny and large images equally.

## Encoding cost

Repeated encoding was measured on the separate 128-image private source-art
sample. Both time columns below include output allocation and use the median
of three trial aggregates.

| Codec | Encode, ms/image | Decode, ms/image | Total encoded bytes |
| --- | ---: | ---: | ---: |
| RGI, automatic profiles 0/1/2 | 3.2470 | 0.03823 | 803,945 |
| QOI reference | 0.3127 | 0.09034 | 1,941,614 |
| PNG, stb | 2.9893 | 0.27281 | 1,377,810 |
| PNG, libpng | 2.1033 | 0.35161 | 993,672 |

RGI spends more time preparing these assets to obtain smaller files and fast
decoding. Applications that encode frequently should measure this cost.
The larger game-asset and public-corpus runs encode each input once to prepare
the decode comparison; their `preparation_single` rows are not repeated
encoder benchmarks.

## Public QOI corpus

The public archive includes photographs, screenshots, game images, icons, and
textures. RGI supports 2,847 of its 2,848 images. The excluded image is
1,313 by 20,667 pixels, exceeding RGI's 16,384-pixel dimension limit.
All codecs in this table are compared on the same supported inputs.

| Codec / allocated decode | ms/image | MP/s | Total encoded bytes |
| --- | ---: | ---: | ---: |
| RGI, checked | 1.2439 | 365.64 | 1,285,053,471 |
| QOI reference | 1.5172 | 299.76 | 1,344,553,132 |
| PNG stb, generated | 5.5487 | 81.97 | 1,747,669,990 |
| PNG libpng, generated | 4.7961 | 94.83 | 1,228,176,592 |
| Original PNG, stb decoder | 4.5224 | 100.57 | 1,138,722,336 |
| Original PNG, libpng decoder | 4.1677 | 109.13 | 1,138,722,336 |

Throughput divides total pixels by the sum of per-image median times. RGI
reaches 1.22 times QOI's allocated decode throughput with 4.43% fewer bytes.
Its files are 4.63% larger than generated libpng output; the original PNG
files are smaller still. This is broader coverage with one process, not the
three-pair evidence used for the private pixel-art measurements. Photographic
pixels remain lossless even when RGI's compression or speed is less favorable.

## GPU uploads

Both profile-0/1 and profile-0/1/2 decoders produce ordinary RGBA8 pixels.
Palette files do not introduce an indexed GPU texture or a shader palette
lookup in these paths. Both variants upload the same 504,225,684 RGBA bytes
per complete GPU-subset pass and use the same texture memory.

The GPU subset takes at most 64 images per top-level asset group after
deduplication. Its 525 images form 66 batches of at most eight; these are
benchmark batches, not captured gameplay scenes. Each backend uses three
alternating baseline/current process pairs and seven samples after warmup.
The following are median trial aggregates, in milliseconds per batch, using
reusable CPU staging and one submission per batch:

| Backend | RGI profiles 0/1 | RGI profiles 0/1/2 |
| --- | ---: | ---: |
| D3D12 | 2.8146 | 2.8457 |
| Vulkan | 2.9074 | 2.8008 |

The D3D12 aggregate increases 1.10%; Vulkan decreases 3.67%. Current trial
aggregates range from 2.6936 to 2.8609 ms on D3D12 and 2.7674 to 3.5503 ms
on Vulkan, with variation in unchanged controls too. These measurements do
not establish a substantial, consistent palette penalty or a uniform speedup.
All batches pass texture readback checks.

Direct decoding into mapped transfer memory measures 26.4221 ms/batch on
D3D12 and 2.4508 ms/batch on Vulkan. CPU staging is the portable starting
point; direct mapping needs measurement on the target backend and device.
The [GPU method](benchmarks.md#gpu-method) explains the upload paths.

Timing ends after upload fences complete. It excludes disk I/O, texture and
transfer-buffer creation, readback, concurrent rendering, and worker overlap.
The results do not measure complete application loading or startup.

## Reproduce and inspect

Prepare dependencies and rerun the public corpus from an MSVC x64 environment
with Python and the required `rg_core` include path, as described in the
[setup guide](benchmarks.md#running):

```powershell
python tools/prepare_bench.py --png --corpus
python benchmarks/run.py --corpus build/bench-deps/corpus/images --decode-only --samples 7 --runs 1 --output build/public-corpus
```

For your own game assets, replace `ASSETS` with their directory:

```powershell
python benchmarks/run.py --corpus ASSETS --input-format rgi --deduplicate --private-assets --decode-only --samples 7 --runs 3 --output build/game-assets
```

This measures the current codec against QOI/PNG on your inputs. A paired RGI
version comparison additionally requires `--baseline-root` pointing to the
desired baseline header; see [paired comparisons](benchmarks.md#cpu-method).
Raw samples from a new run remain under its ignored `build/` output directory.

The committed [aggregate CSV](performance-results.csv) and
[environment metadata](performance-environment.json) retain measured values,
source and binary hashes, settings, and validation evidence. Current results
in this page use these experiment labels:

| Experiment label | Dataset |
| --- | --- |
| `private-game-assets-cpu` | 2,882-image CPU comparison |
| `private-source-art` | 128-image repeated encoding comparison |
| `profile2-full-corpus` | 2,847-image public comparison |
| `private-game-assets-d3d12` / `private-game-assets-vulkan` | 525-image GPU comparisons |

For codec comparison tables, select `variant=candidate` and `category=ALL`;
CPU decoding uses `operation=decode`, `mode=allocated` (or `source_png` for
original PNGs). The GPU table uses `codec=rgi`, `category=ALL`, both variants,
`operation=gpu_scene_batch`, and `mode=staged_reused_rgba`. Other rows retain
supporting categories and historical experiments; their timings must not be
combined as cumulative improvements.
Export labels describe the datasets. Original build command paths remain in
the metadata as provenance, not as required paths on another machine.

The measured production header has SHA-256
`582602171f423872431ec516e7ed1d1c4595691b50874544152bd61a2b90f686`;
the profile-0/1 comparison header has SHA-256
`cc4a695cc9b0702e777c001e10ce9c03e09ee42c5976aa388c895c5e9edfbe94`.
Public inputs can be downloaded and remeasured. Private results provide
workload evidence with auditable aggregates, but their underlying artwork
and per-image timings are unavailable to readers.
