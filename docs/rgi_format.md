# RGI File Format

RGI (Reverse Gravity Image) is a lossless, metadata-free RGBA8 image format. It is
designed for fast decoding and compact game assets. The canonical extension is
`.rgi`, the MIME type is `image/rgi`, and all multibyte integers are little-endian.

This document defines the stream identified by the four-byte magic `rgif`.

## Header

Every stream starts with this fixed 14-byte header:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `rgif` |
| 4 | 4 | Width as unsigned little-endian 32-bit integer |
| 8 | 4 | Height as unsigned little-endian 32-bit integer |
| 12 | 1 | Channels; must be `4` |
| 13 | 1 | Payload profile; `0`, `1`, or `2` |

Width and height must each be in `1..16384`. Pixels follow left-to-right and then
top-to-bottom, with four bytes per pixel in red, green, blue, alpha order. RGI has
no colorspace, gamma, orientation, animation, metadata, or alpha-premultiplication
field; interpretation beyond raw RGBA8 belongs to the application.

For profiles 0 and 1, the decoder begins with previous pixel `(0, 0, 0, 255)` and a 64-entry pixel index
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

## Profile 2: packed palette payload

The payload begins with a little-endian unsigned 16-bit palette count at file
offset 14. The count must be in `1..256`. Starting at offset 16, exactly that
many four-byte RGBA entries follow. Palette entries retain all four channels,
including RGB under zero alpha. Entries need not be unique.

Indices occupy 1 bit for 1..2 entries, 2 bits for 3..4 entries, 4 bits for
5..16 entries, and 8 bits for 17..256 entries. The initial previous pixel is
palette entry zero. There is no profile-0/1 hash index.

| Bytes | Meaning |
| --- | --- |
| `00xxxxxx packed...` | LITERAL: output `xxxxxx + 1` palette indices (1..64) |
| `01xxxxxx` | RUN: repeat the previous pixel `xxxxxx + 1` times (1..64) |
| `0x80 count_minus_4 offset_lo offset_hi` | COPY 4..259 previously decoded pixels |
| `0x81 count_lo count_hi` | LONG_RUN of 1..65535 previous pixels |
| `0x82..0xBF` | Invalid/reserved |
| `11xxxxxx offset_lo offset_hi` | Short COPY of `xxxxxx + 4` pixels (4..67) |

Each LITERAL stores its first index in the least significant bits of its first
byte, continuing toward higher bits and then the next byte. Its byte count is
`ceil(count * index_bits / 8)`. Unused high bits in the last byte must be zero,
and every index must be less than the palette count. Packing starts at a new
byte for each LITERAL.

COPY offsets are measured in pixels and follow the same nonoverlapping rules
as profile 1: `count <= offset <= pixels_already_emitted`. LITERAL and COPY set
the previous pixel to their final output pixel. Runs retain it. Every token
must fit within the remaining image pixels; LONG_RUN count zero is invalid.

## End marker and canonical stream

Immediately after the chunk that emits the final pixel, the stream must contain:

```text
00 00 00 00 00 00 00 01
```

No byte may appear after this marker. The marker is structural, not a checksum;
containers that need corruption detection must provide it separately.

An encoder may choose any supported profile. The reference encoder encodes profile 0,
then attempts profile 1 within that payload size. It selects profile 1 only
when strictly smaller, otherwise restoring profile 0; ties remain profile 0.
It then attempts profile 2 for images with at most 256 exact RGBA colors and
selects it only if the complete stream is strictly smaller. A rejected attempt
restores the winning legacy profile. Define `RG_RGI_NO_PALETTE_ENCODE` to limit
encoding to profiles 0/1 while retaining decoding for all three profiles.
Decoder behavior, not a particular encoder heuristic, is the compatibility
contract. Profile values 3 through 255 are reserved. Existing readers that only
support profiles 0/1 must be updated before receiving profile-2 assets.

## Validation requirements

A checked decoder must reject bad magic, unsupported profiles, invalid dimensions
or channels, arithmetic overflow, truncated chunks, invalid counts or copy
offsets, output overrun, a missing or altered end marker, and trailing bytes.
Input and output buffers must not overlap. Trusted decoding may omit payload bounds
checks only when a complete stream has already passed checked validation, but it
must still reject non-`rgif` headers.
The reference trusted decoder currently shares checked parsing for profile 2.
