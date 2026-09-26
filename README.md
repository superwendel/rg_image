# rg_image by Reverse Gravity

`rg_image` is the public home of RGI (Reverse Gravity Image), a lossless RGBA8
format and single-header C codec built for fast game-asset loading. The runtime
surface is deliberately small: add `rg_image/src` and `rg_core/src` to the
compiler include path, then include `rg_rgi.h`.

The codec uses internal linkage and needs no implementation macro or separate
library target. Its only runtime dependency is `rg_core`'s `rg_defs.h`; PNG,
QOI, SDL3, COM, stb, and miniz are tooling dependencies and do not enter the
runtime header.

## Quick start

```c
#include "rg_rgi.h"

#include <stdlib.h>

b32 load_rgi(const void* data, size_t data_size, u8** out_pixels,
	         u32* out_width, u32* out_height)
{
	u32 width = 0;
	u32 height = 0;
	if (!rg_rgi_read_header(data, data_size, &width, &height))
	{
		return 0;
	}

	size_t pixel_bytes = (size_t)width * (size_t)height * 4u;
	u8* pixels = (u8*)malloc(pixel_bytes);
	if (pixels == NULL)
	{
		return 0;
	}

	if (rg_rgi_decode(data, data_size, pixels, pixel_bytes,
	                  out_width, out_height) != pixel_bytes)
	{
		free(pixels);
		return 0;
	}
	*out_pixels = pixels;
	return 1;
}
```

MSVC:

```bat
cl /nologo /W4 /O2 /I path\to\rg_image\src /I path\to\rg_core\src example.c
```

GCC or Clang:

```sh
cc -std=c99 -Wall -Wextra -O2 \
  -Ipath/to/rg_image/src -Ipath/to/rg_core/src example.c -o example
```

Use `rg_rgi_decode` for untrusted files. `rg_rgi_decode_trusted` skips payload
bounds validation for profiles 0/1 and is only for complete streams that have
already passed a checked decode; profile 2 currently uses checked decoding.
Encoding automatically selects the smallest of three payload profiles,
including packed palette indices for images with at most 256 exact RGBA colors.
Reuse caller-owned memory with
`rg_rgi_encode_with_workspace`, or use `rg_rgi_encode` and optionally define a
paired `RG_RGI_MALLOC`/`RG_RGI_FREE` allocator.

The complete, normative wire contract is in
[`docs/rgi_format.md`](docs/rgi_format.md). RGI streams use `.rgi` and the
four-byte magic `rgif`.

Read the [introduction and measured tradeoffs](docs/blog/introducing-rgi.md), or
download the [two-page specification sheet](output/pdf/rgi-specification.pdf).
The sheet's [editable source](docs/rgi-specification-sheet.md) and
[PDF build instructions](docs/documentation.md) are included.

The current decoder reads existing profile-0/1 files. New profile-2 files need
an updated reader; rebuild converters, viewers, and thumbnail handlers together
with your application. Define `RG_RGI_NO_PALETTE_ENCODE` before including the
header when producing assets for older readers. This leaves all decoding
support enabled. Every profile preserves RGBA8 exactly, including transparent
pixels' RGB values.

## Tools

From a Visual Studio Developer Command Prompt, with sibling `rg_core` or an
explicit `RG_CORE_DIR`:

```bat
build.bat test
build.bat rgi_convert
```

`rgi_convert` converts any pair of PNG, RGI, and QOI files and supports batch
directory conversion. PNG input is decoded by a tool-local PNG-only build of
stb_image after PNG chunk CRC and zlib Adler-32 validation. The explicit
`--trusted-png` option skips that integrity preflight for already-trusted input.
Outputs are staged beside their destination and atomically replaced.

For recursive directory conversion, the output must be outside the input tree;
directory symlinks and Windows reparse points are skipped.

### RGI viewer

[`rgi_viewer`](tools/rgi_viewer.c) lives in this repository and builds directly
against the codec in `src/rg_rgi.h`. It supports every current `rgif` profile,
including exact palette compression, and displays ordinary RGBA textures with
nearest-neighbor sampling for pixel art.

From a Visual Studio Developer Command Prompt:

```bat
set SDL3_DIR=C:\libs\SDL3-3.2.26
build.bat rgi_viewer
build.bat test_viewer
set "PATH=%SDL3_DIR%\lib\x64;%PATH%"
rgi_viewer.exe path\to\image.rgi
```

Set `SDL3_DIR` to your SDL3 development package. When launching outside that
prompt, place the matching `SDL3.dll` beside `rgi_viewer.exe` or on `PATH`.
The viewer supports drag-and-drop, folder navigation with Left/Right, wheel
zoom, middle-button pan, an info overlay with I, fullscreen with F11, and live
reload when the image changes.

`build.bat test_viewer` checks all three profiles through the actual
texture-loading path and exact RGBA readback, including transparent pixels'
hidden RGB, using an SDL software renderer without opening a window. It also
checks malformed input rejection.
Windows CI builds the viewer and thumbnail provider and runs this test.

The optional Windows `rgi_thumbnail` target builds the Explorer thumbnail
provider. `tools/install_rgi_windows.bat` registers `.rgi`, `image/rgi`, and
Explorer thumbnails for the current user; the matching uninstall script reverses
those registrations. Building and testing the viewer does not register it.

## Build, test, and benchmark

```bat
build.bat test
build.bat test_scalar
build.bat bench
```

The aggregate test covers checked and trusted decode, all profiles, malformed
streams, workspace encoding, converter round trips, C++ inclusion, and
tool builds. The benchmark runner compares RGI, reference QOI, stb PNG, and
optional libpng on generated pixel art, the public QOI corpus, or local assets.
It supports paired comparisons, reusable buffers, and SDL GPU upload completion
on D3D12 and Vulkan. Private assets are read in place and their results stay
under ignored `build/`. See [`docs/benchmarks.md`](docs/benchmarks.md) for setup
and methodology. The [performance results](docs/performance-results.md) compare
the current codec with QOI and PNG on a private pixel-art library and the public
QOI corpus, including encoding cost, GPU uploads, and measurement limits.
The [initial encoder report](docs/benchmark-results.md) is historical and
predates palette support.

## Third-party code

Tooling vendors stb_image 2.30, stb_image_write 1.16, miniz 3.1.0, and the QOI
reference implementation under their respective permissive terms; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). None of these libraries is
part of the public runtime dependency graph.

## License and trademark

The software and documentation are available under the [MIT License](LICENSE).
See the separate [trademark notice](TRADEMARK.md) for the Reverse Gravity name.
