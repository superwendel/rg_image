# RGI: Lossless image compression for pixel-art games

I want loading a sprite to be an ordinary operation: read some bytes, decode
them, and put the pixels in a texture. The interesting work should be in the
game. That led me to RGI, the Reverse Gravity Image format: a lossless RGBA8
codec built around sprites, tiles, UI, and animation sheets, with more work
allowed during asset preparation to make runtime decoding practical.

QOI was an important inspiration. Dominic Szablewski's
[original article introducing QOI](https://phoboslab.org/log/2021/11/qoi-fast-lossless-image-compression)
showed how much useful compression could come from a small collection of
operations on pixels. RGI builds on that approach, particularly runs,
recent-color indexing, and small color differences. QOI also remains a useful
comparison: its encoder is exceptionally quick in my measurements, and its
reference implementation makes a straightforward control.

Pixel art gives a codec some useful material. Colors recur, transparent
regions can be large, and pieces of a tile or animation frame often repeat.
I wanted to exploit those patterns while keeping the decoded result ordinary
RGBA pixels. The requirement is exact preservation. Accepting weaker results
on photographs means accepting worse size or speed on those workloads;
every supplied RGBA8 pixel, including a photograph's pixels, still survives
unchanged.

An RGI file has one `rgif` signature, a 14-byte header, and one of three
payload profiles. The header carries dimensions, four channels, and the
profile number. Profile 0 uses QOI-style pixel operations. Profile 1 adds
literal RGBA spans, longer runs, and COPY commands that reproduce an earlier
sequence of decoded pixels. Those copies never overlap their source, so the
decoder can copy a block directly. The [format specification](../rgi_format.md)
defines the exact rules, limits, and required end marker.

Profile 2 is where small palettes become useful. If an image contains at
most 256 distinct RGBA colors, the encoder can store those colors once and
pack their indices into 1, 2, 4, or 8 bits. Runs and COPY commands compress
repeated decoded pixels. An eight-color sprite can therefore
use four-bit literals without giving up its repeated rows or flat areas.

This palette is exact. There is no color quantization, dithering, or alpha
threshold. Two pixels with the same RGB and different alpha are different
entries. RGB values underneath fully transparent pixels are preserved too.
The encoder tries the available representations and keeps a palette stream
only when the complete file is smaller than the winning profile-0/1 stream.
Palette overhead can make a tiny image larger, so having few colors alone
doesn't force profile 2. Selection optimizes size; it cannot promise that
every selected image will decode faster.

I started measuring source artwork, then expanded the test to a private
game-asset tree containing 14,630 RGI files. Removing byte-identical files
left **2,882 inputs and 249 million pixels**. That is a census of distinct
source streams, rather than a sample weighted by gameplay frequency.
Different encodings of the same pixels can still count separately. The
artwork stays private; the published evidence contains aggregate results.

For the comparison, each input was decoded outside timing and its pixels
freshly encoded with every codec. The PNG columns below are generated
RGBA8 PNGs, rather than original authored files or specially optimized
indexed PNGs. All four codecs reproduce the same pixels.

| Codec | Encoded size, MiB | Allocated decode, ms/image |
| --- | ---: | ---: |
| RGI, checked | 23.07 | 0.05866 |
| QOI reference | 38.89 | 0.11721 |
| PNG, stb | 39.88 | 0.34875 |
| PNG, libpng | 27.87 | 0.42664 |

These measurements used an Intel Core i7-12700KF, Windows 11, and MSVC
19.44 x64 with `/O2 /MD`. There were three alternating baseline/current
process pairs and seven samples per image after a discarded warmup. Each
trial averages the per-image median times; the table reports the median
of those three trial aggregates. Output allocation is timed, while output
release and correctness hashing are outside timing. These are warm-memory
CPU measurements: disk reads and GPU uploads are excluded. The
[methodology](../benchmarks.md#cpu-method) records codec versions, compression
settings, iteration counts, and differences in decoder integrity checking.

On this asset set, RGI delivers about **twice QOI's allocated decode
throughput with 40.66% fewer bytes**. It also uses 17.21% fewer bytes than
the generated libpng output. Against the optimized RGI encoder restricted
to profiles 0/1, adding palette selection reduces total size by 18.25% and
the aggregate allocated decode time by 5.66%. Those are whole-corpus
aggregates, not promises for individual images or game startup. The
[current results](../optimization-followup.md#native-mana-artwork-follow-up)
and [CSV](../optimization-followup.csv) contain the underlying totals.

I also used the public QOI benchmark corpus to look beyond game art.
Its downloaded archive contains 2,848 images; 2,847 fit RGI's dimensions.
One tall screenshot exceeds the 16,384-pixel limit. This broader comparison
used seven decode samples in one process, rather than the three paired
runs above. RGI reached 365.64 megapixels per second against QOI's 299.76,
with 4.43% fewer encoded bytes. RGI files were 4.63% larger than generated
libpng output, and the original PNG files were smaller still. Those mixed-corpus
results are separate from the pixel-art results. Photographs remain useful
stress cases, and PNG keeps a compression advantage across this broader
collection. The [public-corpus report](../optimization-followup.md#full-public-corpus-timing-method)
includes original-PNG decode results as well.

Encoding is the conspicuous cost. On a separate, repeatedly measured
128-image source-art sample, the current RGI encoder averaged 3.2470
ms/image, versus 0.3127 for QOI, 2.9893 for stb PNG, and 2.1033 for libpng,
using the same median-of-trial-aggregates definition. The large native-asset
run prepared each encoding once, so I do not use it to claim an encoder
speedup. I see RGI fitting an offline asset pipeline; applications that
encode images frequently should measure that cost before choosing it.

Smaller files also leave an important part of loading untouched: decoding
still produces four bytes per pixel, and the tested upload paths still
transfer full RGBA textures. Palette compression does not reduce their GPU
memory footprint. The separate D3D12 and Vulkan experiments support reusing
CPU staging storage and batching uploads. Direct decoding into mapped
transfer memory behaved very differently between those backends. I would
measure the application's complete loading path before predicting a frame
time or startup improvement.

The [codec source](../../src/rg_rgi.h) is a single C99 header using
`rg_core`'s shared definitions. Start with the checked decoder; the trusted
entry point has a stricter input contract. Existing `rgif` profiles 0/1
remain readable, while older readers need updating for profile 2.
`RG_RGI_NO_PALETTE_ENCODE` keeps output compatible with those older readers.
The [getting-started instructions](../../README.md#quick-start),
[wire specification](../rgi_format.md), and
[SDL3 viewer](../../README.md#rgi-viewer) are in the repository. My next
question for any project adopting RGI would be the same one that shaped it:
what happens on the images your game actually loads?
