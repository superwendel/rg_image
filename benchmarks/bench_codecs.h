// Benchmark-only codec adapters. The runtime header has no PNG/QOI dependency.
#ifndef RG_IMAGE_BENCH_CODECS_H
#define RG_IMAGE_BENCH_CODECS_H

#include "rg_rgi.h"
#include "rg_time.h"
#include <stdio.h>
#include <limits.h>
#include <errno.h>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4267 4996)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "../third_party/qoi/qoi.h"
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#ifdef RG_IMAGE_BENCH_LIBPNG
#include <png.h>
#include <zlib.h>
#endif

typedef struct BenchBuffer
{
	u8* data;
	size_t size;
	size_t capacity;
	int failed;
} BenchBuffer;

typedef struct BenchImage
{
	const char* name;
	u32 width, height;
	u8* pixels;
	BenchBuffer source;
	BenchBuffer encoded[4];
	double prepare_encode_ns[4];
} BenchImage;

static const char* const bench_codec_names[5] = {"rgi", "qoi", "png_stb", "png_libpng", "rgba8"};
#ifdef RG_IMAGE_BENCH_LIBPNG
#define BENCH_CODEC_COUNT 4
#else
#define BENCH_CODEC_COUNT 3
#endif

// Defined in a separate translation unit without LTO. Keeps output observable.
extern u64 bench_consume(const void* data, size_t size);

static void bench_die(const char* message)
{
	fprintf(stderr, "ERROR: %s\n", message);
	exit(1);
}

// Manifests and command-line paths use UTF-8 on every platform.
static FILE* bench_fopen(const char* path, const char* mode)
{
#if RG_PLATFORM_WINDOWS
	int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
	if (!length) { errno = EILSEQ; return NULL; }
	wchar_t* wide_path = (wchar_t*)malloc((size_t)length * sizeof(*wide_path));
	if (!wide_path) { errno = ENOMEM; return NULL; }
	wchar_t wide_mode[8];
	if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide_path, length) ||
	    !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, mode, -1, wide_mode,
	                         (int)(sizeof(wide_mode) / sizeof(wide_mode[0]))))
	{
		free(wide_path);
		errno = EILSEQ;
		return NULL;
	}
	FILE* file = _wfopen(wide_path, wide_mode);
	free(wide_path);
	return file;
#else
	return fopen(path, mode);
#endif
}

static size_t bench_pixel_bytes(const BenchImage* image)
{
	return (size_t)image->width * image->height * 4u;
}

static void bench_release(BenchBuffer* buffer)
{
	free(buffer->data);
	memset(buffer, 0, sizeof(*buffer));
}

static void bench_append(BenchBuffer* buffer, const void* data, size_t bytes)
{
	if (buffer->failed) return;
	if (bytes > SIZE_MAX - buffer->size) { buffer->failed = 1; return; }
	size_t required = buffer->size + bytes;
	if (required > buffer->capacity)
	{
		size_t capacity = required > SIZE_MAX / 2u ? required : required * 2u;
		u8* resized = (u8*)realloc(buffer->data, capacity);
		if (!resized) { buffer->failed = 1; return; }
		buffer->data = resized;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->size, data, bytes);
	buffer->size = required;
}

static BenchBuffer bench_read_file(const char* path)
{
	BenchBuffer result = {0};
	FILE* file = bench_fopen(path, "rb");
	if (!file) bench_die("cannot open input file");
	u8 chunk[16384];
	size_t size;
	while ((size = fread(chunk, 1u, sizeof(chunk), file)) != 0u) bench_append(&result, chunk, size);
	int failed = ferror(file);
	fclose(file);
	if (failed || result.failed || !result.size) bench_die("cannot read input file");
	return result;
}

static void bench_png_write(void* context, void* data, int size)
{
	if (size < 0) { ((BenchBuffer*)context)->failed = 1; return; }
	bench_append((BenchBuffer*)context, data, (size_t)size);
}

#ifdef RG_IMAGE_BENCH_LIBPNG
static void bench_libpng_write(png_structp png, png_bytep data, png_size_t size)
{
	BenchBuffer* output = (BenchBuffer*)png_get_io_ptr(png);
	bench_append(output, data, size);
	if (output->failed) png_error(png, "output allocation failed");
}

typedef struct BenchPngRead
{
	const u8* data;
	size_t remaining;
} BenchPngRead;

static void bench_libpng_read(png_structp png, png_bytep data, png_size_t size)
{
	BenchPngRead* input = (BenchPngRead*)png_get_io_ptr(png);
	if (size > input->remaining) png_error(png, "truncated input");
	memcpy(data, input->data, size);
	input->data += size;
	input->remaining -= size;
}

static BenchBuffer bench_encode_libpng(const BenchImage* image)
{
	// Heap context remains well defined after a libpng longjmp.
	BenchBuffer* output = (BenchBuffer*)calloc(1u, sizeof(*output));
	if (!output) bench_die("libpng context allocation failed");
	png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) bench_die("png_create_write_struct failed");
	png_infop info = png_create_info_struct(png);
	if (!info) bench_die("png_create_info_struct failed");
	if (setjmp(png_jmpbuf(png))) bench_die("libpng encoding failed");
	png_set_write_fn(png, output, bench_libpng_write, NULL);
	png_set_compression_level(png, 6);
	png_set_filter(png, PNG_FILTER_TYPE_BASE, PNG_ALL_FILTERS);
	png_set_IHDR(png, info, image->width, image->height, 8, PNG_COLOR_TYPE_RGBA,
	             PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
	png_write_info(png, info);
	for (u32 y = 0; y < image->height; ++y)
		png_write_row(png, image->pixels + (size_t)y * image->width * 4u);
	png_write_end(png, info);
	png_destroy_write_struct(&png, &info);
	BenchBuffer result = *output;
	free(output);
	return result;
}

static BenchBuffer bench_decode_libpng(const BenchBuffer* input, u32* width, u32* height)
{
	BenchBuffer* output = (BenchBuffer*)calloc(1u, sizeof(*output));
	if (!output) bench_die("libpng context allocation failed");
	png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) bench_die("png_create_read_struct failed");
	png_infop info = png_create_info_struct(png);
	if (!info) bench_die("png_create_info_struct failed");
	if (setjmp(png_jmpbuf(png))) bench_die("libpng decoding failed");
	BenchPngRead source = {input->data, input->size};
	png_set_read_fn(png, &source, bench_libpng_read);
	png_read_info(png, info);
	*width = png_get_image_width(png, info);
	*height = png_get_image_height(png, info);
	int depth = png_get_bit_depth(png, info);
	int color = png_get_color_type(png, info);
	if (depth == 16) png_set_strip_16(png);
	if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
	if (color == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
	int transparent = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
	if (transparent) png_set_tRNS_to_alpha(png);
	if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
	if (!(color & PNG_COLOR_MASK_ALPHA) && !transparent) png_set_add_alpha(png, 255u, PNG_FILLER_AFTER);
	int passes = png_set_interlace_handling(png);
	png_read_update_info(png, info);
	if (*width == 0u || *height == 0u || (size_t)*width > SIZE_MAX / 4u / *height)
		bench_die("PNG dimensions overflow");
	size_t row = (size_t)*width * 4u;
	if (png_get_rowbytes(png, info) != row) bench_die("unexpected PNG row format");
	output->size = row * *height;
	output->data = (u8*)calloc(1u, output->size);
	if (!output->data) bench_die("PNG pixel allocation failed");
	for (int pass = 0; pass < passes; ++pass)
		for (u32 y = 0; y < *height; ++y) png_read_row(png, output->data + y * row, NULL);
	png_read_end(png, info);
	png_destroy_read_struct(&png, &info, NULL);
	BenchBuffer result = *output;
	free(output);
	return result;
}
#endif

static BenchBuffer bench_encode(const BenchImage* image, int codec)
{
	BenchBuffer result = {0};
	if (codec == 0)
	{
		result.capacity = rg_rgi_encode_bound(image->width, image->height);
		result.data = (u8*)malloc(result.capacity);
		if (result.data) result.size = rg_rgi_encode(image->pixels, image->width, image->height,
		                                           result.data, result.capacity);
	}
	else if (codec == 1)
	{
		qoi_desc desc = {image->width, image->height, 4, QOI_SRGB};
		int size = 0;
		result.data = (u8*)qoi_encode(image->pixels, &desc, &size);
		result.size = size > 0 ? (size_t)size : 0u;
	}
	else if (codec == 2)
	{
		if (!stbi_write_png_to_func(bench_png_write, &result, (int)image->width, (int)image->height,
		                            4, image->pixels, (int)image->width * 4)) result.failed = 1;
	}
#ifdef RG_IMAGE_BENCH_LIBPNG
	else if (codec == 3) result = bench_encode_libpng(image);
#endif
	if (!result.data || !result.size || result.failed) bench_die("encoding failed");
	return result;
}

static BenchBuffer bench_decode(const BenchImage* image, int codec, int source_png)
{
	const BenchBuffer* input = source_png ? &image->source : &image->encoded[codec];
	BenchBuffer result = {0};
	u32 width = 0, height = 0;
	if (codec == 0)
	{
		result.capacity = bench_pixel_bytes(image);
		result.data = (u8*)malloc(result.capacity);
		if (result.data) result.size = rg_rgi_decode(input->data, input->size, result.data,
		                                           result.capacity, &width, &height);
	}
	else if (codec == 1)
	{
		if (input->size > INT_MAX) bench_die("QOI input exceeds reference API limit");
		qoi_desc desc = {0};
		result.data = (u8*)qoi_decode(input->data, (int)input->size, &desc, 4);
		width = desc.width;
		height = desc.height;
		result.size = (size_t)width * height * 4u;
	}
	else if (codec == 2)
	{
		if (input->size > INT_MAX) bench_die("PNG input exceeds stb API limit");
		int w = 0, h = 0, channels = 0;
		result.data = stbi_load_from_memory(input->data, (int)input->size, &w, &h, &channels, 4);
		width = (u32)w;
		height = (u32)h;
		result.size = (size_t)width * height * 4u;
	}
#ifdef RG_IMAGE_BENCH_LIBPNG
	else if (codec == 3) result = bench_decode_libpng(input, &width, &height);
#endif
	if (!result.data || result.size != bench_pixel_bytes(image) ||
	    width != image->width || height != image->height) bench_die("decoding failed or dimensions differ");
	return result;
}

static u32 bench_random(u32* state)
{
	u32 value = *state;
	value ^= value << 13u; value ^= value >> 17u; value ^= value << 5u;
	return *state = value;
}

static void bench_synthetic(BenchImage* image, int pattern)
{
	static const char* names[] = {"synthetic/flat-runs", "synthetic/tiles", "synthetic/gradient",
	                             "synthetic/noise", "synthetic/alpha", "synthetic/odd-small",
	                             "pixel-art/palette-sprites", "pixel-art/tilemap", "pixel-art/animation-sheet"};
	image->name = names[pattern];
	image->width = pattern == 4 || pattern == 8 ? 257u : pattern == 5 ? 17u : pattern == 6 ? 256u : pattern == 7 ? 640u : 512u;
	image->height = pattern == 4 || pattern == 8 ? 129u : pattern == 5 ? 19u : pattern == 6 ? 256u : pattern == 7 ? 360u : 512u;
	image->pixels = (u8*)malloc(bench_pixel_bytes(image));
	if (!image->pixels) bench_die("synthetic allocation failed");
	u32 random = 0x52474946u;
	for (u32 y = 0; y < image->height; ++y)
	{
		for (u32 x = 0; x < image->width; ++x)
		{
			u8* p = image->pixels + ((size_t)y * image->width + x) * 4u;
			p[3] = 255u;
			if (pattern >= 6)
			{
				u32 cell_x = x / 32u, cell_y = y / 32u;
				u32 local_x = x & 31u, local_y = y & 31u;
				u32 tile = pattern == 7 ? ((cell_x * 7u + cell_y * 3u) & 7u) : cell_y & 3u;
				u32 color = ((local_x / 4u) ^ (local_y / 4u) ^ tile) & 15u;
				p[0] = (u8)(color * 17u); p[1] = (u8)(((color * 5u) & 15u) * 17u); p[2] = (u8)(((color * 11u) & 15u) * 17u);
				if (pattern != 7)
				{
					u32 shift = pattern == 8 ? cell_x % 5u : 0u;
					if (local_x < 4u + shift || local_x > 25u + shift || local_y < 3u || local_y > 28u) p[3] = 0u;
					else if (local_x == 4u + shift || local_y == 3u) p[3] = 128u;
				}
			}
			else if (pattern == 0) { p[0] = (u8)((y / 32u) * 13u); p[1] = (u8)((y / 32u) * 7u); p[2] = 96u; }
			else if (pattern == 1) { p[0] = (u8)((x & 31u) * 8u); p[1] = (u8)((y & 31u) * 8u); p[2] = (u8)(((x ^ y) & 31u) * 8u); }
			else if (pattern == 3) { u32 value = bench_random(&random); p[0] = (u8)value; p[1] = (u8)(value >> 8u); p[2] = (u8)(value >> 16u); }
			else { p[0] = (u8)x; p[1] = (u8)y; p[2] = (u8)(x + y); if (pattern >= 4) p[3] = (u8)(x * 13u + y * 7u); }
		}
	}
}

static int bench_load_image(BenchImage* image, const char* path, const char* name)
{
	image->name = name;
	image->source = bench_read_file(path);
	if (image->source.size > INT_MAX) bench_die("input exceeds stb API limit");
	int width = 0, height = 0, channels = 0;
	if (!stbi_info_from_memory(image->source.data, (int)image->source.size, &width, &height, &channels))
		bench_die("invalid PNG input header");
	if (width <= 0 || height <= 0 || width > (int)RG_RGI_MAX_DIM || height > (int)RG_RGI_MAX_DIM)
	{
		fprintf(stderr, "EXCLUDED\t%s\tunsupported dimensions %dx%d\n", name, width, height);
		bench_release(&image->source);
		return 0;
	}
	image->pixels = stbi_load_from_memory(image->source.data, (int)image->source.size,
	                                    &width, &height, &channels, 4);
	if (!image->pixels) bench_die("source PNG decoding failed");
	image->width = (u32)width;
	image->height = (u32)height;
	return 1;
}

static void bench_prepare(BenchImage* image)
{
	for (int codec = 0; codec < BENCH_CODEC_COUNT; ++codec)
	{
		u64 start = rg_time_ticks();
		image->encoded[codec] = bench_encode(image, codec);
		image->prepare_encode_ns[codec] = rg_time_ticks_to_ms(rg_time_ticks() - start) * 1000000.0;
		BenchBuffer decoded = bench_decode(image, codec, 0);
		if (memcmp(decoded.data, image->pixels, decoded.size)) bench_die("codec roundtrip pixel mismatch");
		bench_release(&decoded);
		if (image->source.data && codec >= 2)
		{
			decoded = bench_decode(image, codec, 1);
			if (memcmp(decoded.data, image->pixels, decoded.size)) bench_die("source PNG decoders disagree");
			bench_release(&decoded);
		}
	}
	// Trusted benchmarks only receive a stream already checked above.
	u8* decoded = (u8*)malloc(bench_pixel_bytes(image));
	if (!decoded || rg_rgi_decode_trusted(image->encoded[0].data, image->encoded[0].size,
	    decoded, bench_pixel_bytes(image), NULL, NULL) != bench_pixel_bytes(image) ||
	    memcmp(decoded, image->pixels, bench_pixel_bytes(image))) bench_die("trusted decode mismatch");
	free(decoded);
}

static void bench_free_image(BenchImage* image)
{
	for (int codec = 0; codec < BENCH_CODEC_COUNT; ++codec) bench_release(&image->encoded[codec]);
	bench_release(&image->source);
	free(image->pixels);
	memset(image, 0, sizeof(*image));
}

#endif
