# RGI File Format

RGI (Reverse Gravity Image) is a lossless, metadata-free RGBA8 image format. It is
designed for fast decoding and compact game assets. The canonical extension is
`.rgi`, the MIME type is `image/rgi`, and all multibyte integers are little-endian.

This document defines the stream identified by the four-byte magic `rgif`.
Historical private `rgi1`, `rgi2`, and experimental `rgix` streams are not RGI and
must be rejected by conforming readers.

## Header

Every stream starts with this fixed 14-byte header:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `rgif` |
| 4 | 4 | Width as unsigned little-endian 32-bit integer |
| 8 | 4 | Height as unsigned little-endian 32-bit integer |
| 12 | 1 | Channels; must be `4` |
| 13 | 1 | Payload profile; `0` or `1` |

Width and height must each be in `1..16384`. Pixels follow left-to-right and then
top-to-bottom, with four bytes per pixel in red, green, blue, alpha order. RGI has
no colorspace, gamma, orientation, animation, metadata, or alpha-premultiplication
field; interpretation beyond raw RGBA8 belongs to the application.

The decoder begins with previous pixel `(0, 0, 0, 255)` and a 64-entry pixel index
filled with `(0, 0, 0, 0)`. The index slot for a pixel is:

```text
(r * 3 + g * 5 + b * 7 + a * 11) & 63
```

Component arithmetic wraps modulo 256.

## Profile 0: QOI-style payload

Profile 0 uses the following chunks. `xx` denotes bits stored in the opcode.

| Bytes | Meaning |
| --- | --- |
| `00xxxxxx` | INDEX: output index entry `xxxxxx` |
| `01rrggbb` | DIFF: add two-bit deltas minus 2 to RGB |
| `10gggggg rrrrbbbb` | LUMA: add green delta minus 32, plus red/blue deltas relative to green minus 8 |
| `11xxxxxx` (`0xC0..0xFD`) | RUN: repeat the previous pixel `xxxxxx + 1` times |
| `0xFE r g b` | RGB: replace RGB and retain previous alpha |
| `0xFF r g b a` | RGBA: replace all components |

After RGB, RGBA, DIFF, or LUMA, store the resulting pixel in its hash slot. INDEX
does not otherwise change the index. A run beginning at output pixel zero stores
the initial previous pixel in its hash slot; later runs do not change the index.

## Profile 1: extended payload

Profile 1 keeps INDEX, DIFF, LUMA, RGB, and RGBA unchanged. Short RUN is restricted
to `0xC0..0xFA`, representing 1 through 59 pixels, and adds:

| Bytes | Meaning |
| --- | --- |
| `0xFB count_minus_4 offset_lo offset_hi` | COPY 4..259 previously decoded pixels |
| `0xFC count rgba...` | RAWSPAN of 1..255 literal RGBA pixels |
| `0xFD count_lo count_hi` | LONG_RUN of 1..65535 previous pixels |

COPY offset is measured in pixels behind the current output position. It must be
nonzero, no larger than the number of pixels already emitted, and at least the
copy count. Copies therefore never overlap and are equivalent to `memcpy` from
the previously decoded output.

COPY and RAWSPAN set the previous pixel to their final emitted pixel but do not
modify the 64-entry index. RUN and LONG_RUN follow the profile-0 run cache rule.
All counts must be nonzero and no chunk may emit beyond `width * height` pixels.

## End marker and canonical stream

Immediately after the chunk that emits the final pixel, the stream must contain:

```text
00 00 00 00 00 00 00 01
```

No byte may appear after this marker. The marker is structural, not a checksum;
containers that need corruption detection must provide it separately.

An encoder may choose either profile. The reference encoder encodes profile 0,
estimates profile 1, and selects profile 1 only when it is strictly smaller, so
ties remain profile 0. Decoder behavior, not a particular encoder heuristic, is
the compatibility contract. Profile values 2 through 255 are reserved.

## Validation requirements

A checked decoder must reject bad magic, unsupported profiles, invalid dimensions
or channels, arithmetic overflow, truncated chunks, invalid counts or copy
offsets, output overrun, a missing or altered end marker, and trailing bytes.
Input and output buffers must not overlap. Trusted decoding may omit payload bounds
checks only when a complete stream has already passed checked validation, but it
must still reject non-`rgif` headers.
