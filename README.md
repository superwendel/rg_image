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
bounds validation and is only for complete streams that have already passed a
checked decode. Encoding automatically selects the smaller of the two public
payload profiles. Reuse caller-owned memory with
`rg_rgi_encode_with_workspace`, or use `rg_rgi_encode` and optionally define a
paired `RG_RGI_MALLOC`/`RG_RGI_FREE` allocator.

The complete, normative wire contract is in
[`docs/rgi_format.md`](docs/rgi_format.md). RGI streams use `.rgi` and the
four-byte magic `rgif`.

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

Optional `rgi_viewer` and `rgi_thumbnail` targets require SDL3 and Windows,
respectively. `tools/install_rgi_windows.bat` registers `.rgi`, `image/rgi`, and
Explorer thumbnails for the current user; the matching uninstall script
reverses those registrations.

## Build, test, and benchmark

```bat
build.bat test
build.bat test_scalar
build.bat bench
```

The aggregate test covers checked and trusted decode, both profiles, malformed
streams, workspace encoding, converter round trips, C++ inclusion, and
tool builds. The benchmark corpus is generated deterministically in memory and
compares automatic RGI with the QOI reference codec and PNG via stb; results are
machine- and workload-specific, so the benchmark prints its seed, corpus
dimensions, byte totals, and timing method with every run. See
[`docs/benchmarks.md`](docs/benchmarks.md) for the exact methodology and
preparation snapshot.

## Third-party code

Tooling vendors stb_image 2.30, stb_image_write 1.16, miniz 3.1.0, and the QOI
reference implementation under their respective permissive terms; see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). None of these libraries is
part of the public runtime dependency graph.

## License and trademark

The software and documentation are available under the [MIT License](LICENSE).
Reverse Gravity is a registered trademark of Steven Wendel in the United
States. The license grants rights to the software and documentation, but not to
the Reverse Gravity name or trademark except to identify the origin of this
software.
