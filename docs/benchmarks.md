# RGI benchmark methodology

Run `build.bat bench` from a Visual Studio Developer Command Prompt. The
benchmark generates four 512x512 RGBA8 images in memory from the fixed seed
`0x52474946`: long flat runs, repeating tiles, a gradient, and deterministic
xorshift noise. No private or externally licensed image corpus is required.

Both codecs process the same pixels. The comparison uses this repository's RGI
encoder/checked decoder and the vendored QOI reference implementation. Each
timed operation includes allocation and release of its output buffer, uses 20
iterations over all four images, and reports milliseconds per image. Windows
uses `QueryPerformanceCounter`; other builds report C `clock()` CPU time.
Encoded byte totals are deterministic. Timings are diagnostic and depend on the
machine, compiler, power state, and background load.

## Preparation snapshot

Measured on 2026-08-29 with MSVC 19.44.35217 x64, Windows 10.0.26200.9168, and
an AMD64 Family 23 Model 96 processor:

| Metric | RGI | QOI reference |
| --- | ---: | ---: |
| Total encoded bytes | 1,325,122 | 2,363,598 |
| Encode time | 124.005 ms/image | 2.758 ms/image |
| Decode time | 1.108 ms/image | 2.328 ms/image |

The tile-heavy and run-heavy inputs select RGI profile 1; gradient and noise
select profile 0. These numbers characterize this corpus rather than making a
general performance guarantee. Re-run the benchmark on the intended target
hardware before choosing a production encoding pipeline.
