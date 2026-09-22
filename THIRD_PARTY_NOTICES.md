# Third-party notices

The public RGI runtime in `src/rg_rgi.h` does not use these dependencies. They
are vendored only for the PNG converter and deterministic benchmark.

## stb_image 2.30

`third_party/stb_image.h` is by Sean Barrett and contributors. It is available
under the public-domain dedication or MIT license reproduced at the end of that
file. The converter and benchmark compile it with `STBI_ONLY_PNG` and
`STBI_NO_STDIO`; the benchmark uses it for direct RGBA8 PNG decoding.

## stb_image_write 1.16

`third_party/stb_image_write.h` is by Sean Barrett and contributors. It is
available under the public-domain dedication or MIT license reproduced at the
end of that file. The deterministic benchmark uses its in-memory PNG encoder
with compression level 8 and adaptive filtering.

## miniz 3.1.0

The files under `third_party/miniz` are copyright 2013-2014 RAD Game Tools and
Valve Software, and copyright 2010-2014 Rich Geldreich and Tenacious Software
LLC. They are distributed under the MIT license in
`third_party/miniz/LICENSE`. Only the inflate implementation is compiled, to
validate the Adler-32 checksum on PNG IDAT zlib streams.

## QOI reference implementation

`third_party/qoi/qoi.h` is copyright Dominic Szablewski and is distributed
under the MIT license in `third_party/qoi/LICENSE`. It is used only by the
deterministic benchmark comparison.
