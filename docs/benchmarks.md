# RGI benchmark methodology

Run `build.bat bench` from a Visual Studio Developer Command Prompt. The
benchmark generates four 512x512 RGBA8 images in memory from the fixed seed
`0x52474946`: long flat runs, repeating tiles, a gradient, and deterministic
xorshift noise. No private or externally licensed image corpus is required.

All three codecs process the same pixels. The comparison uses this repository's
RGI encoder/checked decoder, the vendored QOI reference implementation, and PNG
encoded by stb_image_write 1.16 with compression level 8 and adaptive filtering.
PNG is decoded directly to RGBA8 by the PNG-only stb_image 2.30 build. Each timed
operation includes allocation and release of its output buffer, uses 20
iterations over all four images, and reports milliseconds per image. The PNG
codec timing deliberately excludes the converter's PNG chunk CRC and zlib
Adler-32 integrity preflight. Windows uses `QueryPerformanceCounter`; other
builds report C `clock()` CPU time. Encoded byte totals are deterministic.
Timings are diagnostic and depend on the machine, compiler, power state, and
background load; the stb PNG result is a reference baseline rather than an
optimized PNG implementation.

## Preparation snapshot

Measured on 2026-08-29 with MSVC 19.44.35217 x64, Windows 10.0.26200.9168, and
an AMD64 Family 23 Model 96 processor:

| Metric | RGI | QOI reference | PNG (stb) |
| --- | ---: | ---: | ---: |
| Total encoded bytes | 1,325,122 | 2,363,598 | 1,087,515 |
| Encode time | 81.300 ms/image | 2.233 ms/image | 50.027 ms/image |
| Decode time | 0.693 ms/image | 1.635 ms/image | 1.838 ms/image |

The tile-heavy and run-heavy inputs select RGI profile 1; gradient and noise
select profile 0. These numbers characterize this corpus rather than making a
general performance guarantee. Re-run the benchmark on the intended target
hardware before choosing a production encoding pipeline.
