// Optional CPU-only comparison. Runtime RGI and the standard codec slots stay unchanged.
#ifndef RG_IMAGE_BENCH_PALETTE_H
#define RG_IMAGE_BENCH_PALETTE_H

#include "experimental/rg_palette.h"
#include "experimental/test_palette.h"

static BenchBuffer bench_palette_encode_forced(const BenchImage* image, u16* colors)
{
	BenchBuffer result = {0};
	result.data = rg_palette_encode(image->pixels, image->width, image->height, &result.size, colors);
	if (!result.data && *colors != 257u) bench_die("palette encoding failed");
	return result;
}

static BenchBuffer bench_palette_encode_auto(const BenchImage* image)
{
	// These are real attempts on every timed call, including the >256-colour
	// rejection and the stream that ultimately loses the size comparison.
	BenchBuffer original = bench_encode(image, 0);
	u16 colors = 0;
	BenchBuffer palette = bench_palette_encode_forced(image, &colors);
	if (palette.data && palette.size < original.size)
	{
		bench_release(&original);
		return palette;
	}
	bench_release(&palette);
	return original;
}

static BenchBuffer bench_palette_decode(const BenchImage* image, const BenchBuffer* input, int automatic)
{
	BenchBuffer result = {0};
	result.capacity = bench_pixel_bytes(image);
	result.data = (u8*)malloc(result.capacity);
	if (!result.data) bench_die("palette decode allocation failed");
	u32 width = 0, height = 0;
	// Format selection is inside this function, hence inside decode timing.
	if (!automatic || (input->size >= 4u && !memcmp(input->data, "RGIP", 4u)))
		result.size = rg_palette_decode(input->data, input->size, result.data, result.capacity, &width, &height);
	else
		result.size = rg_rgi_decode(input->data, input->size, result.data, result.capacity, &width, &height);
	if (result.size != result.capacity || width != image->width || height != image->height)
		bench_die("palette decode failed or dimensions differ");
	return result;
}

static void bench_palette_record(const BenchImage* image, int codec, const char* operation,
                                  const char* mode, int sample, const BenchBuffer* encoded,
                                  u64 hash, double elapsed)
{
	// 254 is a benchmark-only RGIP discriminator; it is not an RGI profile ID.
	u32 profile = !memcmp(encoded->data, "RGIP", 4u) ? 254u : encoded->data[13];
	bench_csv_string(image->name);
	printf(",%u,%u,%s,%s,%s,%d,%zu,%u,%llu,%.3f,1,0,0,0,0\n",
	       image->width, image->height, bench_codec_names[codec], operation, mode, sample,
	       encoded->size, profile, (unsigned long long)hash, elapsed);
	fflush(stdout);
}

static void bench_palette(const BenchImage* image, const BenchOptions* options)
{
	static u32 rotation;
	static int tested;
	if (!tested) { bench_palette_selftest(); tested = 1; }
	size_t bytes = bench_pixel_bytes(image);
	u64 pixels_hash = bench_consume(image->pixels, bytes);
	u16 colors = 0;
	double start = bench_now();
	BenchBuffer forced = bench_palette_encode_forced(image, &colors);
	double preparation_ns = bench_now() - start;
	int selected = forced.data && forced.size < image->encoded[0].size;
	const BenchBuffer* automatic = selected ? &forced : &image->encoded[0];
	if (forced.data)
	{
		BenchBuffer decoded = bench_palette_decode(image, &forced, 0);
		if (memcmp(decoded.data, image->pixels, bytes)) bench_die("forced palette pixels differ");
		bench_release(&decoded);
	}
	BenchBuffer decoded = bench_palette_decode(image, automatic, 1);
	if (memcmp(decoded.data, image->pixels, bytes)) bench_die("automatic palette pixels differ");
	bench_release(&decoded);
	fprintf(stderr, "PALETTE\t%s\tcolors=%u eligible=%d selected=%d rgi_bytes=%zu rgip_bytes=%zu auto_bytes=%zu\n",
	        image->name, (unsigned)colors, forced.data != NULL, selected,
	        image->encoded[0].size, forced.size, automatic->size);
	if (options->verify_only) { bench_release(&forced); return; }
	// Alternate whole blocks across inputs so experimental codecs do not
	// systematically run before or after the unchanged standard comparisons.
	if (!(rotation & 1u)) bench_cpu(image, options);
	u64 hashes[3] = {bench_consume(image->encoded[0].data, image->encoded[0].size),
	                  bench_consume(automatic->data, automatic->size), 0};
	if (forced.data) hashes[2] = bench_consume(forced.data, forced.size);
	if (options->decode_only && forced.data)
		bench_palette_record(image, 6, "encode", "preparation_single", 0, &forced, hashes[2], preparation_ns);
	// Automatic preparation borrows the separately prepared native stream;
	// never present that borrowed setup as a timed automatic encode operation.
	int iterations = options->iterations;
	if (bytes < 16384u && iterations < 32) iterations = 32;
	int codecs = forced.data ? 3 : 2;
	for (int sample = -1; sample < options->samples; ++sample)
	{
		for (int order = 0; order < codecs; ++order)
		{
			int which = (order + sample + 1 + (int)(rotation % (u32)codecs)) % codecs;
			const BenchBuffer* encoded = which == 0 ? &image->encoded[0] : which == 1 ? automatic : &forced;
			for (int operation = options->decode_only ? 1 : 0; operation < 2; ++operation)
			{
				double elapsed = 0;
				for (int iteration = 0; iteration < iterations; ++iteration)
				{
					start = bench_now();
					BenchBuffer output;
					if (operation) output = which == 0 ? bench_decode(image, 0, 0) :
					    bench_palette_decode(image, encoded, which == 1);
					else if (which == 0) output = bench_encode(image, 0);
					else if (which == 1) output = bench_palette_encode_auto(image);
					else
					{
						u16 repeated_colors = 0;
						output = bench_palette_encode_forced(image, &repeated_colors);
					}
					elapsed += bench_now() - start;
					u64 hash = bench_consume(output.data, output.size);
					if (output.size != (operation ? bytes : encoded->size) ||
					    hash != (operation ? pixels_hash : hashes[which])) bench_die("timed palette output changed");
					bench_checksum ^= hash;
					bench_release(&output);
				}
				if (sample >= 0) bench_palette_record(image, which == 0 ? 0 : 4 + which,
				    operation ? "decode" : "encode", which == 0 ? "palette_control" : "allocated",
				    sample, encoded, hashes[which], elapsed / iterations);
			}
		}
	}
	if (rotation & 1u) bench_cpu(image, options);
	++rotation;
	bench_release(&forced);
}

#endif
