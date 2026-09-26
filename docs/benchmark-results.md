# Pixel-art performance results

> Historical report: this measures the initial profile-0/1 encoder optimization,
> before profile 2 was added. For the current codec, palette results, full public
> corpus, and game assets, see the [performance results](performance-results.md).
> New profile-2 files require an updated reader; see [compatibility](rgi_format.md).

The change measured here speeds encoding without changing the decoder, wire format,
or image quality. It compares COPY candidates in four-pixel chunks, rejects
candidates that cannot beat the current match, stops searches at the maximum
possible match, clears only live copy-table counts, and stops profile-1
estimation when it can no longer beat profile 0. Candidate ordering and ties
are preserved. That optimization alone required no format migration.

## Primary asset sample

Measured on 2026-09-25 using an Intel Core i7-12700KF, Windows 11 build 26200,
MSVC 19.44.35219 x64 `/O2 /MD`, and the [documented method](benchmarks.md).
Baseline is `14e48b2970aacfce45d852708cbefaeaf3f91fe8`. The private sample has
128 pixel-art assets selected with at most 16 evenly spaced files per top-level
category. Three alternating baseline/candidate process pairs each used seven
samples. No private images, filenames, or per-image results are published.

RGI allocated encoding improved **24.05%** by median paired per-image time;
reused-workspace encoding improved **25.40%**. All 128 encoded sizes, profiles,
and encoded checksums matched the baseline. Allocated decode changed +0.92%,
reused decode +0.72%, and trusted reused decode +1.82% by the same statistic.
These do not establish a decoder improvement; decoder source is unchanged.

Candidate results below are medians across three trials. Time is the sum of
per-image medians divided by image count; MP/s uses total pixels/total time.
These aggregate statistics differ from the paired per-image percentages above.

| Codec | Encode ms/image | Decode ms/image | Decode MP/s | Total encoded bytes |
| --- | ---: | ---: | ---: | ---: |
| RGI | 3.5380 | 0.0425 | 1,626.45 | 1,000,184 |
| QOI reference | 0.3156 | 0.0915 | 756.40 | 1,941,614 |
| PNG stb | 3.0548 | 0.2833 | 244.19 | 1,377,810 |
| PNG libpng | 2.1445 | 0.3581 | 193.20 | 993,672 |

On this sample RGI decodes about 2.15x as fast as QOI with 48.5% fewer bytes,
and about 8.42x as fast as libpng with 0.66% more bytes. RGI encoding remains
substantially slower than QOI and slower than both PNG encoders. This supports
an offline-encode/runtime-decode use case, not a general fastest-codec claim.

## Experiments not retained

Three generated pixel-art cases also showed a 17.33% median allocated encoder
improvement in three paired seven-sample runs. This is supporting evidence;
the private real-asset sample is the primary encoding decision set.

Separate decoder experiments tested profile specialization, wider unrolled
run stores, and their combination against the encoder-only candidate. Each
used three paired runs, 11 samples, and 16 iterations on generated pixel art.
Profile specialization improved allocated decode by 6.58% but regressed reused
decode by 8.36%. Wider stores changed allocated decode by -0.99% and reused
decode by +2.89%; the combination also lacked a consistent gain. All passed
correctness checks, but none justified additional decoder complexity. They
remain local experiments, outside the shipped header. Format changes were
allowed but were not needed for the supported improvement.

## Public QOI corpus coverage

The downloaded archive contains 2,848 PNG inputs. One image,
`screenshot_web/phoboslab.org.png` (1,313x20,667), exceeds RGI's 16,384-pixel
per-dimension limit and is explicitly excluded. Coverage is therefore 2,847
images, not every archive entry. This tall web screenshot does not justify
changing the format limit for the primary pixel-art workload.

Archive SHA256: `00166f555ca760e23647025e46d9b046e03cd2e22869d4ecb496edc527cd0d7d`.
The public run uses one preparation encode per codec and seven repeated decode
samples. Preparation encode timings are cold diagnostics; encoder improvement
claims above use the repeated primary-asset runs instead.
The candidate corpus process was interrupted and resumed with the same
executable: complete per-image sample sets were retained, and the incomplete
image was measured again. Local raw segments and resume metadata are preserved.

Allocated decode throughput is in MP/s. Size columns are RGI bytes divided by
the other codec's bytes; below 100% means RGI is smaller. PNG columns describe
newly generated RGBA8 PNGs, with the settings in the methodology.

| Category | Images | RGI | QOI | PNG stb | PNG libpng | Size / QOI | Size / libpng |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ALL | 2847 | 313.2 | 270.7 | 73.3 | 85.9 | 98.5% | 107.9% |
| icon_512 | 213 | 692.2 | 521.7 | 168.8 | 159.0 | 96.7% | 161.8% |
| icon_64 | 213 | 393.5 | 331.2 | 86.8 | 69.3 | 99.5% | 120.9% |
| photo_kodak | 24 | 192.3 | 176.4 | 45.5 | 56.5 | 100.0% | 93.6% |
| photo_tecnick | 100 | 197.1 | 179.5 | 46.3 | 59.3 | 100.0% | 104.7% |
| photo_wikipedia | 49 | 198.9 | 180.7 | 45.8 | 60.0 | 100.0% | 102.8% |
| pngimg | 187 | 405.9 | 341.6 | 87.4 | 100.1 | 99.5% | 119.0% |
| screenshot_game | 618 | 310.2 | 270.7 | 79.5 | 89.8 | 95.1% | 110.1% |
| screenshot_web | 13 | 725.7 | 552.0 | 153.1 | 149.9 | 98.3% | 104.1% |
| textures_photo | 20 | 217.1 | 186.0 | 50.1 | 63.5 | 100.0% | 100.2% |
| textures_pk | 1002 | 221.3 | 198.8 | 50.6 | 58.8 | 99.7% | 84.2% |
| textures_pk01 | 113 | 313.8 | 248.3 | 66.2 | 74.7 | 97.0% | 105.7% |
| textures_pk02 | 235 | 239.8 | 199.4 | 57.3 | 72.1 | 100.0% | 112.0% |
| textures_plants | 60 | 430.5 | 361.0 | 89.4 | 100.1 | 100.0% | 107.6% |

Total RGI size is 1,325,051,132 bytes, versus QOI 1,344,553,132, stb PNG
1,747,669,990, and libpng PNG 1,228,176,592. Original PNG files total
1,138,722,336 bytes and decode at 89.41 MP/s with stb or 98.88 MP/s with libpng;
their palettes, channel layouts, and compression differ from generated PNGs.
The public game and icon categories are not exclusively pixel art, so they
should not substitute for representative game assets.

Candidate allocated decode was 4.85% slower by median paired per-image time
in this broad run; unchanged control codecs also moved +1.46% to +3.81%.
The decoder source is unchanged, and this interrupted, single-pair corpus run
does not isolate a small code-layout or environment effect. No decoder speedup
is claimed. The repeated primary sample showed a much smaller +0.92% change.

All 2,847 RGI encoded sizes, profiles, and checksums matched the baseline.

## GPU completion on the primary sample

Measured with SDL 3.4.10 on an NVIDIA GeForce RTX 2060, driver 595.97
(D3D12 reports 32.0.15.9597). Each backend ran the same 128 private assets,
one process with seven samples per image. Every warmup/final texture readback
and the alternating-content reuse test passed. These are host-observed upload
completion results, without disk I/O or rendering traffic.

Values are ms per texture, summed per-image medians divided by image count.
Streaming normalizes batched uploads and reuses resources. Latency includes
texture/staging creation. Straight RGBA results:

| Path | D3D12 latency | D3D12 stream | Vulkan latency | Vulkan stream |
| --- | ---: | ---: | ---: | ---: |
| Predecoded RGBA8 control | 0.8128 | 0.0617 | 0.3049 | 0.0482 |
| RGI staged | 0.8170 | 0.0851 | 0.3345 | 0.0865 |
| QOI staged | 0.9037 | 0.1331 | 0.3830 | 0.1411 |
| PNG stb staged | 1.1478 | 0.3517 | 0.6009 | 0.3384 |
| PNG libpng staged | 1.2145 | 0.4175 | 0.6964 | 0.4039 |
| RGI direct mapped, tight rows | 2.0683 | 1.2841 | 0.3019 | 0.0589 |

Staged RGI streaming is about 1.56x QOI on D3D12 and 1.63x on Vulkan.
Direct mapped decoding is about 15.1x slower than staged RGI on D3D12, but
about 1.47x as fast on Vulkan. Keep CPU staging as the portable default and
enable direct mapping only after testing the actual backend/device. Resource
creation and GPU completion substantially reduce the CPU-only advantage in
single-texture latency.

Premultiplication remains a separate, explicit conversion. Its streaming
ms/texture results are:

| Codec | D3D12 | Vulkan |
| --- | ---: | ---: |
| RGI | 0.1734 | 0.1672 |
| QOI | 0.2288 | 0.2229 |
| PNG stb | 0.4436 | 0.4246 |
| PNG libpng | 0.5115 | 0.4898 |

GPU codec order rotates between images, not between individual samples.
Small differences and backend rankings need independent confirmation; this
test does not reproduce a mixed scene or simultaneous rendering workload.

## Final checks and reproducibility

All 17 codec tests passed in normal and explicit-scalar MSVC builds, MSVC
AddressSanitizer builds, and Clang UndefinedBehaviorSanitizer builds with
recovery disabled. C++ inclusion, custom allocation, converter integrity and
round-trip tests, strict Clang C99 four-codec verification, three Python
reporting tests, and native/Python Unicode-path checks passed. Legacy RGI
migration and RGI-to-PNG-to-RGI checks reproduced the frozen fixture bytes.
Linux sanitizer CI is configured but was not executed on this Windows host.

The final nine-pattern synthetic comparison used three paired seven-sample
runs and showed a 21.64% median allocated encoder improvement, with allocated
decode changing -0.39%. A separate three-case pixel-art stage profile found
the unbounded COPY estimator 18.21% faster and profile-1 emission 21.10% faster
by median per-image time, supporting the focus on COPY search work.

[Aggregate CSV](benchmark-results.csv) and [environment/source fingerprints](benchmark-environment.json)
accompany these results. Private data is restricted to whole-sample aggregates;
all source images remain in their original location. Detailed local results
are under ignored `build/benchmarks/`, including raw samples, input hashes,
interrupted segments, and rejected decoder experiments.
