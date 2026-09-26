# Pixel-art optimization follow-up

These experiments start from commit
`ed45fad20a2a0da994e9e1928178fe28051f2d63`, after the improvements in
[the original results](benchmark-results.md). The order is loading,
encoding, then a palette experiment. The initial sample is the same 128
private source PNGs, read in place without copying artwork into the repository.
The native-asset follow-up below expands coverage to Mana's RGI artwork.
The machine and codec settings remain those in the original results.
Aggregate measurements and build fingerprints accompany this report in
[`optimization-followup.csv`](optimization-followup.csv) and
[`optimization-followup-environment.json`](optimization-followup-environment.json).

## Loading: reuse and mixed-image batches

The new `--gpu-scene` mode groups the selected assets into 16 scenes of eight
different images. Each encoded path decodes and uploads each image once per
scene; the predecoded RGBA8 control only uploads.
Texture/transfer creation happens outside timing; timing ends after all upload
fences complete. Individual submissions and one combined submission use the
same resources and pixels. CPU scratch reuse is measured separately from
allocated decoding. Grouping order alternates between scenes.

Each backend ran three processes with seven samples plus a discarded warmup
per case. Values below are the median of the three trial aggregates, in
milliseconds per eight-image scene. An aggregate is the sum of scene medians
divided by 16; it is not the median of all individual samples.

| Straight RGBA path | D3D12 | Vulkan |
| --- | ---: | ---: |
| RGI allocated CPU decode, individual submissions | 1.3018 | 1.1713 |
| RGI allocated CPU decode, batched submission | 1.1126 | 1.1065 |
| RGI reused CPU decode, individual submissions | 1.1564 | 0.9916 |
| RGI reused CPU decode, batched submission | 0.8245 | 0.8406 |
| RGI direct mapped decode, batched submission | 11.2345 | 0.7436 |
| QOI allocated decode, batched submission | 1.6084 | 1.6059 |
| PNG stb allocated decode, batched submission | 3.5682 | 3.4818 |
| PNG libpng allocated decode, batched submission | 3.9769 | 4.1130 |
| Predecoded RGBA8, batched submission | 0.6240 | 0.6721 |

Reusing CPU storage and batching reduces RGI scene completion time by 36.7%
on D3D12 and 28.2% on Vulkan relative to allocated decoding with individual
submissions. The three reused/batched trial aggregates range from 0.7465 to
0.8369 ms on D3D12 and 0.8375 to 0.9825 ms on Vulkan. These results support
reusable CPU staging and batched uploads as the portable loading approach.
Direct mapping remains an explicitly tested backend/device choice: it helps
Vulkan here, and hurts D3D12 substantially.

All warmup/final texture readbacks passed. Both backends also passed six
rounds of changing-content reuse across three odd-width image sizes,
including straight, premultiplied, allocated, reused, and direct paths.
Synthetic smoke tests cover an eight-image scene and a one-image tail.
Disk reads, resource creation, concurrent rendering, and readback are outside
scene timings; these are warm upload measurements rather than full game
startup times. QOI/PNG paths use their reference allocating APIs, while the
RGI scratch path uses its existing caller-buffer API.

The implementation is in the optional benchmark harness, with
[loading integration guidance](benchmarks.md#gpu-method). The repository is
a codec library, so no renderer or sibling application was changed.

Reproduce these measurements from an MSVC x64 environment, substituting a
local asset directory and fresh output directories:

```powershell
python benchmarks/run.py --legacy-encode --gpu direct3d12 --gpu-scene --corpus ASSETS --private-assets --per-category 16 --samples 7 --runs 3 --output build/scenes-d3d12
python benchmarks/run.py --legacy-encode --gpu vulkan --gpu-scene --corpus ASSETS --private-assets --per-category 16 --samples 7 --runs 3 --output build/scenes-vulkan
```

Detailed private results stay under ignored `build/benchmarks/phase2-scenes-*`.

## Encoding: one COPY search pass

The retained encoder writes profile 0, then attempts profile 1 directly into
the destination, stopping before a complete token would reach the profile-0
size. A winning profile 1 needs one COPY-search pass instead of estimation
followed by emission. A loss or tie regenerates profile 0. This uses the same
397,312-byte workspace and needs no extra allocation or image-sized scratch
buffer. This intermediate stage leaves the format, decoder, and public API
unchanged; the subsequent palette stage below adds profile 2.

On the 128 private assets, three alternating process pairs with seven samples
show a **49.46% reduction in median paired allocated encode time**, and 47.97%
with reused output/workspace. These are additional gains relative to the
already optimized `ed45fad` baseline. The 90 profile-1 images improve 50.97%
in allocated mode; the 38 profile-0 images regress 5.91%. Reused-mode changes
for those subsets are -49.64% and +8.16%, respectively. The fallback cost is
real, and favors the intended pixel-art workload.

The median trial aggregate for allocated RGI encoding falls from 3.9751 to
1.9831 ms/image. Candidate trial aggregates range from 1.9649 to 2.0194
ms/image. The nine generated patterns independently show a 48.99% median
paired allocated encode improvement across three pairs and seven samples.
Their aggregate falls from 22.5599 to 19.1745 ms/image (15.0%); large
profile-0 cases carry more weight in that statistic.

The 24 Kodak photos, all profile 0, regress 6.55% in allocated encoding and
6.96% with reused storage across three paired seven-sample runs. Their median
trial aggregate rises from 106.2355 to 114.8112 ms/image. Unchanged QOI/PNG
encoders also move +2.52% to +4.04%, so the observed difference includes
environment variation as well as fallback work. The primary pixel-art gain
justifies this tradeoff; photo pixels and encoded bytes remain unchanged.

Private-sample aggregates after this intermediate encoder change (median
across the three trial aggregates):

| Codec | Encode ms/image | Decode ms/image | Encoded bytes |
| --- | ---: | ---: | ---: |
| RGI | 1.9831 | 0.0450 | 1,000,184 |
| QOI reference | 0.3342 | 0.0985 | 1,941,614 |
| PNG stb | 3.2633 | 0.2969 | 1,377,810 |
| PNG libpng | 2.2943 | 0.3832 | 993,672 |

The decoder source did not change. Its observed allocated timing moved -6.97%
by paired per-image median, while unchanged QOI decoding moved -7.11% and
PNG decoding also improved. No decoder speedup is attributed to this patch.

The correctness run compared actual encoded bytes against the frozen baseline
on all **2,847 supported public inputs plus 128 private inputs**. All 2,975
streams matched exactly; both versions' checked/trusted decoding reproduced
1,303,694,926 pixels with intact destination guards. The same oversized public
web screenshot was excluded. This is full-corpus correctness coverage, not
a new repeated full-corpus timing run.

All 19 normal and scalar unit tests pass, including complete-token output
limits, no-write rejection, partial-prefix loss/tie recovery, independent
full-profile selection, and dirty workspace reuse. C++, custom allocation,
converter tests, MSVC AddressSanitizer, and Clang UndefinedBehaviorSanitizer
also pass; both sanitizers covered normal and scalar builds.

For a paired run, save the baseline header under a separate root with
`src/rg_rgi.h`, then use:

```powershell
python benchmarks/run.py --legacy-encode --baseline-root BASELINE --corpus ASSETS --private-assets --per-category 16 --samples 7 --runs 3 --output build/encoder-primary
python benchmarks/run.py --legacy-encode --baseline-root BASELINE --samples 7 --runs 3 --output build/encoder-synthetic
python benchmarks/run.py --legacy-encode --baseline-root BASELINE --corpus build/bench-deps/corpus/images --filter photo_kodak --samples 7 --runs 3 --output build/encoder-kodak
```

Detailed results stay in `build/benchmarks/phase2-encoder-*`; the exact-byte
verification and sanitizer logs are under `build/phase2-validation/`.

## Palette experiment and profile 2

The isolated RGIP prototype used packed 1/2/4/8-bit palette indices, short and
long runs, and nonoverlapping COPY packets. It preserved canonical RGBA8
exactly, including transparent pixels' RGB values. It tested every eligible
candidate even when the result was larger, and measured automatic fallback
separately. Native RGI and experimental codecs rotated within each sample.

Across the same 128 private inputs, 108 had at most 256 colors and 89 selected
the palette candidate. Total bytes fell from **1,000,184 to 803,945 (19.62%)**.
Three processes with seven samples showed a 7.33% improvement in median
paired allocated decode time; the median trial aggregate improved from
0.043986 to 0.041770 ms/image. Automatic encoding included both attempts and
regressed 102.33% by paired median, with its aggregate rising from 1.9054 to
3.3980 ms/image. This is an offline encoding cost in exchange for smaller
assets and faster measured allocated CPU decoding on the primary sample.

The nine generated cases show why the primary asset sample matters. Four
were eligible, three selected the palette stream, and bytes fell only from
1,474,583 to 1,471,045. The three selected pixel-art cases shrank from 15,256
to 11,718 bytes, but their median paired allocated decoding regressed 9.23%.
Palette selection minimizes size; it cannot guarantee faster decoding on
every image. The new profile should be judged on the application's assets.

These results justified integrating the palette payload as public **RGI
profile 2**. The production encoder uses the existing output and 397,312-byte
workspace, a fixed 2 KiB stack dictionary, and no image-sized palette scratch
allocation. A bounded attempt wins only when the complete file is smaller;
ineligible images and rejected candidates retain the legacy encoding.

The public function signatures and RGBA8 output are unchanged. The decoder
accepts profiles 0, 1, and 2; old decoders must be updated to read profile 2.
Defining `RG_RGI_NO_PALETTE_ENCODE` preserves legacy-only output while still
allowing all three profiles to be decoded. The trusted entry point currently
uses checked parsing for profile 2. The normative
[format specification](rgi_format.md#profile-2-packed-palette-payload) defines
the wire contract; the experimental `RGIP` magic is not a public asset format.

### Production measurements

Three alternating process pairs with seven samples compare the final encoder
against the retained profiles-0/1 implementation (header SHA-256
`cc4a695cc9b0702e777c001e10ce9c03e09ee42c5976aa388c895c5e9edfbe94`).
The final header fingerprint is
`582602171f423872431ec516e7ed1d1c4595691b50874544152bd61a2b90f686`.
The primary sample selected the same 89 palette streams and totaled 803,945
bytes. These measurements include the actual public encoder and decoder.

| RGI operation | Legacy ms/image | Current ms/image | Median paired change |
| --- | ---: | ---: | ---: |
| Encode, allocated | 1.8110 | 3.2470 | +104.51% |
| Encode, reused | 1.8136 | 3.2811 | +107.98% |
| Checked decode, allocated | 0.03980 | 0.03823 | -6.56% |
| Checked decode, reused | 0.02454 | 0.02200 | -6.60% |
| Trusted decode, reused | 0.02161 | 0.01946 | -6.41% |

The ms/image columns are medians of trial aggregates; paired changes are
medians of paired image/trial ratios (384 pairs here), so their percentages
differ. Each ratio compares that trial's seven-sample image medians. Allocated decoding
on the 89 selected images improves 12.00% by paired median; the 39 retained
legacy images move +0.79%. Unchanged QOI/PNG allocated decode controls move
0.00% to +0.34%, supporting the measured primary-sample improvement. Current
allocated RGI trial aggregates range from 0.03806 to 0.03830 ms/image.

| Current codec | Encode ms/image | Decode ms/image | Total bytes |
| --- | ---: | ---: | ---: |
| RGI automatic profiles 0/1/2 | 3.2470 | 0.03823 | 803,945 |
| QOI reference | 0.3127 | 0.09034 | 1,941,614 |
| PNG stb | 2.9893 | 0.27281 | 1,377,810 |
| PNG libpng | 2.1033 | 0.35161 | 993,672 |

On this sample RGI has 2.36 times QOI's aggregate allocated decode throughput,
uses 58.59% fewer bytes than QOI, and uses 19.09% fewer bytes than the generated
libpng files. QOI remains much faster to encode. Palette encoding is intended
for offline asset preparation; `--legacy-encode` remains available to trade
file size for faster encoding and compatibility with older readers.

The repeated production comparison on all nine generated patterns confirms
the same byte totals as the prototype. Overall allocated decoding moves
-0.44% by paired median (aggregate 0.20265 to 0.20509 ms/image); the three
selected palette cases move +0.89% allocated and +0.96% reused. These small
changes and allocation-sensitive aggregate variation do not support a general
synthetic decode improvement. Allocated encoding moves +34.51% by paired
median, with its aggregate increasing from 16.5880 to 18.8524 ms/image.

The production D3D12 scene comparison does **not** establish an additional GPU
completion speedup from the palette format. Reused/batched straight uploads
move -1.13% by median paired scene/trial ratio (48 pairs). The legacy/current aggregate medians are
0.9755/0.8556 ms per eight-image scene, but the current trial range is
0.8462..3.2479 ms; unchanged controls also have large aggregate variation.
The earlier reuse/batching result is a separate pipeline improvement. Every
completed production GPU path passed its fence/readback checks.

Vulkan reused/batched straight uploads improve 4.83% by paired scene median,
with aggregate medians of 0.8634 to 0.8417 ms/scene and a current range of
0.7812..0.9140 ms. Direct mapped batches measure 0.7140 ms on Vulkan versus
12.7074 ms on D3D12. These results retain the recommendation to use reusable
CPU staging as the portable default and benchmark direct mapping on each
backend/device. Both backends completed all three paired processes, seven
samples per scene, and changing-content reuse validation without pixel errors.

To compare the current codec against a saved legacy header, repeat the paired
commands above **without** `--legacy-encode`, using the retained legacy snapshot
as `BASELINE`. The production runs are `profile2-mana`, `profile2-synthetic`,
`profile2-scenes-d3d12`, and `profile2-scenes-vulkan` under `build/benchmarks/`.
Source fingerprints in the environment export identify the exact versions
used for each stage; the historical stage timings are not additive speedups.

### Validation

The production suite passes 22 normal and scalar test groups and 21 groups
with legacy-only encoding. MSVC AddressSanitizer and Clang
UndefinedBehaviorSanitizer each pass all three configurations. Independent
packets cover every palette bit width, invalid indices/padding, truncated
metadata/tokens, RUN/COPY bounds, end markers, and unaligned destination
guards. Encoder cases cover strict size limits, untouched rejection,
partial-write restoration, dirty workspace reuse, and automatic selection.
The allocator test proves that a winning palette stream still encodes in the
same caller-owned workspace without invoking the allocation hooks.

C++ inclusion and the converter, viewer, and thumbnail builds pass. The
converter test produces an eight-color, 584-byte profile-2 file and preserves
its exact bytes through RGI to PNG to QOI to RGI, including hidden RGB under
zero alpha. Detailed sanitizer evidence remains in
`build/profile2-validation/sanitizers-20260925-220007-16504/`; the accompanying
environment export retains the commands, hashes, and pass counts.

The final format audit covers **2,847 public plus 128 private inputs** and
1,303,694,926 pixels. All 2,975 supported images pass current checked/trusted
round-trips, legacy-stream decoding, decoding with the palette writer disabled,
and destination guards. Palette-disabled encoding matches the frozen legacy
encoder byte for byte on every input. Old readers correctly reject profile 2.
An additional 283 comparisons (27 generated cases and the first 128 inputs
from each manifest) match the isolated prototype's palette bodies and choices.

The public inputs select profile 2 on 1,184 images, reducing encoded bytes
from 1,325,051,132 to 1,285,053,471 (3.02%). The 128 private inputs select it
on 89 images and retain the measured 803,945-byte total. The only exclusion is
the public 1,313-by-20,667 image, exceeding the existing 16,384 dimension limit.
This audit proves correctness and file sizes; its runtime is not benchmark
evidence. Its commands, source/binary hashes, counts, and aggregate hashes are
included in the environment export, with detailed logs under
`build/profile2-validation/run-20260925-221021-9668/`.

### Full public-corpus timing method

The current full-corpus comparison uses the same public QOI benchmark archive,
canonical RGBA8 inputs, and four codec implementations as the original report:

```powershell
python benchmarks/run.py --corpus build/bench-deps/corpus/images --decode-only --samples 7 --runs 1 --output build/benchmarks/profile2-full-corpus
```

This measures seven decode samples after a discarded warmup for every supported
image, in one serial process. The one encode required to prepare each stream
appears as `preparation_single`; it is not a repeated encode measurement.
Generated PNG and original-source PNG decoding remain separate. Aggregate
throughput divides total pixels by the sum of per-image median times. The
three-pair private source-art sample above supplies repeated evidence for
the intended pixel-art use case; the native-asset follow-up expands that coverage.

The run completed all **2,847 supported images**, with the same single
dimension exclusion. Its file totals and profile counts match the independent
format audit exactly: 1,484 profile-0, 179 profile-1, and 1,184 profile-2 files.

| Codec / decode mode | ms/image | MP/s | Total encoded bytes |
| --- | ---: | ---: | ---: |
| RGI checked, allocated | 1.2439 | 365.64 | 1,285,053,471 |
| RGI checked, reused | 1.1182 | 406.74 | 1,285,053,471 |
| RGI trusted, reused | 1.0655 | 426.84 | 1,285,053,471 |
| QOI reference, allocated | 1.5172 | 299.76 | 1,344,553,132 |
| PNG stb, generated | 5.5487 | 81.97 | 1,747,669,990 |
| PNG libpng, generated | 4.7961 | 94.83 | 1,228,176,592 |
| Original PNG, stb decoder | 4.5224 | 100.57 | 1,138,722,336 |
| Original PNG, libpng decoder | 4.1677 | 109.13 | 1,138,722,336 |

Across this broader corpus, RGI's allocated decode throughput is 1.22 times
QOI's and its files use 4.43% fewer bytes. RGI is 4.63% larger than generated
libpng output; the original PNG files are smaller still. PNG retains its size
advantage across this broader corpus, while RGI's strongest measured result is
on the primary pixel-art sample. The aggregate CSV includes every public category, with private
experiments remaining aggregate-only. All raw public samples are under
`build/benchmarks/profile2-full-corpus/`.

## Native Mana artwork follow-up

The initial 128-image sample consists of source PNGs; only 41 have same-stem
RGI siblings. It is useful pixel-art coverage, but it is not a captured game
workload. A read-only inventory of `gfx/` found 42,077 PNGs (3,564 distinct
source-byte hashes) and 14,630 RGIs (2,882 distinct source-byte hashes).
The native RGI set includes asset groups absent from the earlier PNG sample.
Duplicate removal keeps the first path in sorted order for each SHA-256;
it does not merge different streams that happen to decode to the same pixels.

The expanded CPU comparison uses all 2,882 distinct native files and
249,157,476 pixels. GPU comparisons cap each top-level group at 64 after
deduplication, selecting 525 images in 66 consecutive groups of at most eight.
These groups are benchmark batches, not captured gameplay scenes. Each
comparison runs three alternating legacy/current process pairs, with seven
samples and a discarded warmup per case. CPU runs measure repeated decoding;
encoding is performed once per image to prepare each variant's stream.

Native source files are checked-decoded outside timing, then their exact RGBA
pixels are freshly encoded by RGI, QOI, stb PNG, and libpng. Thus these runs
compare re-encoded native artwork, not the existing on-disk streams. The
baseline is the frozen optimized profile-0/1 header used above; the candidate
is the current profile-0/1/2 header. Artwork stays in Mana, and manifests,
per-asset measurements, and source identities remain under ignored `build/`.

```powershell
python benchmarks/run.py --baseline-root BASELINE --corpus ASSETS --input-format rgi --deduplicate --private-assets --decode-only --samples 7 --runs 3 --output build/mana-native-cpu
python benchmarks/run.py --baseline-root BASELINE --corpus ASSETS --input-format rgi --deduplicate --private-assets --per-category 64 --gpu direct3d12 --gpu-scene --samples 7 --runs 3 --output build/mana-native-scenes-d3d12
python benchmarks/run.py --baseline-root BASELINE --corpus ASSETS --input-format rgi --deduplicate --private-assets --per-category 64 --gpu vulkan --gpu-scene --samples 7 --runs 3 --output build/mana-native-scenes-vulkan
```

### Native CPU results

All 2,882 inputs pass checked/trusted round-trips in all six processes, with
no exclusions. QOI and generated PNG stream hashes agree between variants.
The candidate selects 2,335 profile-2 files, 221 profile-1 files, and 326
profile-0 files. Every profile-2 selection is strictly smaller; the 547
fallback files match the baseline's encoded hashes and sizes.

| Measurement | Profiles 0/1 | Automatic profiles 0/1/2 |
| --- | ---: | ---: |
| Encoded bytes | 29,596,914 | 24,195,307 |
| Allocated checked decode, ms/image | 0.062178 | 0.058658 |
| Reused checked decode, ms/image | 0.033445 | 0.030497 |
| Trusted reused decode, ms/image | 0.027724 | 0.025149 |

Times are medians of the three trial aggregates. The allocated checked
aggregate falls 5.66%, and bytes fall 18.25%. The median of paired image/trial
ratios is -30.00% allocated and -34.11% reused; this larger percentage weights
tiny assets equally with large ones, so it is not a 30% whole-library loading
improvement. Candidate allocated aggregates range from 0.057711 to 0.058665
ms/image. Unchanged QOI/stb/libpng allocated controls move 0.00%/-0.07%/-0.30%
by paired median.

The same-run QOI result is 0.117213 ms/image and 40,774,295 bytes: current RGI
has 2.00 times its allocated decode throughput and uses 40.66% fewer bytes.
Generated libpng output is 29,225,647 bytes and decodes in 0.426640 ms/image;
RGI uses 17.21% fewer bytes. Generated stb PNG is 41,816,905 bytes and
0.348752 ms/image. These size comparisons are against generated RGBA PNGs,
not original authored PNG files, which are absent from this native input set.

Individual images can regress. Using each image's three-trial median,
19 of the 2,335 palette-selected images measure more than 5% slower in
allocated decoding; 2,171 measure more than 5% faster. The largest measured
added decode time among selected images is 103.8 microseconds. These are
descriptive counts, not significance tests or an asset-selection speed guarantee.

One of 795,432 raw timing samples measured zero at timer resolution on an 8-by-24 image's
trusted decoder; the seven-sample group median remains positive. The raw
zero is retained in the median and minimum, without filtering or clamping.
The report now records such groups in private `timing-quality.json` and
still rejects negative/nonfinite timings or a nonpositive group median.
Aggregate quality counts are included in the public environment export.
All 12 benchmark tooling tests pass, including this quantization handling.
The saved runs were summarized again without changing or rerunning samples.

### Native GPU results

The D3D12 comparison completes all 66 batches in three paired runs with no
readback errors. The table reports median trial aggregates in milliseconds
per batch of up to eight images; the paired change is the median of 198
scene/trial ratios, a different estimator.

| D3D12 path | Profiles 0/1, ms | Automatic 0/1/2, ms | Paired change |
| --- | ---: | ---: | ---: |
| Allocated CPU staging, individual submissions | 4.3778 | 4.2640 | -1.01% |
| Allocated CPU staging, batched submission | 4.0293 | 4.0887 | -0.89% |
| Reused CPU staging, batched submission | 2.8146 | 2.8457 | -2.64% |
| Direct mapped decoding, batched submission | 25.4528 | 26.4221 | +2.10% |

These staged results do not establish a substantial palette loading penalty
or a uniform speedup. Reused/batched timing improves by paired median while
its aggregate increases 1.10%; current trial aggregates span 2.6936 to 2.8609
ms. Unchanged predecoded RGBA, QOI, stb, and libpng batched straight controls
move +0.88%/-0.19%/+2.47%/-0.29% by paired median. The direct-mapped path
is already slow with profiles 0/1 and remains substantially slower than CPU
staging. Its smaller +2.10% paired regression on this broader sample should
not be confused with the earlier +20.02% result on 128 source PNGs.

Vulkan also completes all three pairs and readback checks. It uses the exact
same input manifest and generated codec bytes as the D3D12 comparison.

| Vulkan path | Profiles 0/1, ms | Automatic 0/1/2, ms | Paired change |
| --- | ---: | ---: | ---: |
| Allocated CPU staging, individual submissions | 4.2824 | 4.2820 | -2.41% |
| Allocated CPU staging, batched submission | 4.1920 | 4.1525 | -2.81% |
| Reused CPU staging, batched submission | 2.9074 | 2.8008 | -2.68% |
| Direct mapped decoding, batched submission | 2.5062 | 2.4508 | -3.07% |

The Vulkan reused/batched aggregate falls 3.67%, while allocated individual
submissions are effectively unchanged in aggregate. Candidate reused/batched
trial aggregates range from 2.7674 to 3.5503 ms; the unchanged predecoded RGBA
control also ranges from 2.0138 to 2.9555 ms. This variation limits claims of
small improvements. Batched straight controls move +1.08%/-1.65%/+3.73%/+0.63%
for predecoded RGBA/QOI/stb/libpng by paired median. Neither backend establishes
a substantial, consistent penalty from palette files on the CPU-staged paths.

Both variants upload the same 504,225,684 RGBA bytes per complete GPU corpus
pass. Palette-enabled files total 12,969,669 bytes for this sample, compared
with 16,177,262 for profiles 0/1. Smaller on-disk files do not reduce the RGBA
upload size or texture memory in these paths. All GPU timing groups have
positive samples. The evidence supports retaining palette encoding for these
assets with CPU staging, without promising that every image or backend is
faster. The format and its default selection policy were not changed in this
follow-up; the encoder still chooses by file size.

### Relationship to Mana's loader

Source inspection of Mana's `src/gpulib.h` finds checked RGI decoding into
allocated CPU memory, followed by a copy into a fresh mapped transfer buffer
and an individual RGBA8 texture upload. It also creates an alpha mask.
`src/game_app_lifecycle_render.c` uses a background decode worker and
budgeted main-thread uploads. `src/main.c` leaves SDL's GPU driver selection
automatic; source inspection does not establish the active backend.

The direct-mapped decoder benchmark is therefore not Mana's current loading
path. Allocated CPU staging is the closer benchmark comparison, while reused
staging and batches remain proposed integration choices. The scene measurements omit
file I/O, texture/transfer creation, alpha-mask generation, concurrent
rendering, and worker overlap, and wait for upload fences. Their timings do
not predict game startup duration. No Mana source or asset file was changed.
