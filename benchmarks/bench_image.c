// Corpus benchmark. Run through run.py to capture builds, inputs and reports.
#include "bench_codecs.h"

typedef struct BenchOptions
{
	const char* manifest;
	const char* filter;
	const char* gpu;
	const char* fixtures;
	int samples;
	int iterations;
	int decode_only;
	int verify_only;
	int profile_encoder;
	int gpu_scene;
	int palette_experiment;
} BenchOptions;

static volatile u64 bench_checksum;

static double bench_now(void)
{
	return rg_time_ticks_to_ms(rg_time_ticks()) * 1000000.0;
}

static void bench_csv_string(const char* value)
{
	putchar('"');
	for (; *value; ++value)
	{
		if (*value == '"') putchar('"');
		putchar(*value);
	}
	putchar('"');
}

static void bench_record(const BenchImage* image, int codec, const char* operation,
                         const char* mode, int sample, size_t encoded_bytes, u64 hash,
                         double elapsed, u32 count, double decode, double stage,
                         double submit, double completion)
{
	bench_csv_string(image->name);
	printf(",%u,%u,%s,%s,%s,%d,%zu,%u,%llu,%.3f,%u,%.3f,%.3f,%.3f,%.3f\n",
	       image->width, image->height, bench_codec_names[codec], operation, mode,
	       sample, encoded_bytes, codec == 0 ? image->encoded[0].data[13] : 0u,
	       (unsigned long long)hash, elapsed, count, decode, stage, submit, completion);
	fflush(stdout);
}

static void bench_cpu(const BenchImage* image, const BenchOptions* options)
{
	size_t bytes = bench_pixel_bytes(image);
	size_t bound = rg_rgi_encode_bound(image->width, image->height);
	u8* reused = (u8*)malloc(bound > bytes ? bound : bytes);
	void* workspace = malloc(rg_rgi_encode_workspace_size());
	if (!reused || !workspace) bench_die("reusable buffer allocation failed");
	u64 hashes[4];
	for (int codec = 0; codec < BENCH_CODEC_COUNT; ++codec)
		hashes[codec] = bench_consume(image->encoded[codec].data, image->encoded[codec].size);
	u64 pixels_hash = bench_consume(image->pixels, bytes);
	int iterations = options->iterations;
	if (bytes < 16384u && iterations < 32) iterations = 32;

	// Sample -1 is an untimed-for-report warmup. Rotate codecs each sample.
	for (int sample = -1; sample < options->samples; ++sample)
	{
		for (int order = 0; order < BENCH_CODEC_COUNT; ++order)
		{
			int codec = (order + sample + 1) % BENCH_CODEC_COUNT;
			for (int operation = options->decode_only ? 1 : 0; operation < 2; ++operation)
			{
				double total = 0.0;
				for (int iteration = 0; iteration < iterations; ++iteration)
				{
					double start = bench_now();
					BenchBuffer output = operation ? bench_decode(image, codec, 0) : bench_encode(image, codec);
					total += bench_now() - start;
					// Consume and verify outside timing. Allocation is inside; release is outside.
					u64 hash = bench_consume(output.data, output.size);
					if (hash != (operation ? pixels_hash : hashes[codec])) bench_die("timed output changed");
					bench_checksum ^= hash;
					bench_release(&output);
				}
				if (sample >= 0) bench_record(image, codec, operation ? "decode" : "encode", "allocated",
				    sample, image->encoded[codec].size, hashes[codec], total / iterations, 1u, 0, 0, 0, 0);
			}
			if (codec >= 2 && image->source_is_png)
			{
				double total = 0;
				for (int iteration = 0; iteration < iterations; ++iteration)
				{
					double start = bench_now();
					BenchBuffer output = bench_decode(image, codec, 1);
					total += bench_now() - start;
					if (bench_consume(output.data, output.size) != pixels_hash) bench_die("source decode changed");
					bench_release(&output);
				}
				if (sample >= 0) bench_record(image, codec, "decode", "source_png", sample,
				    image->source.size, pixels_hash, total / iterations, 1u, 0, 0, 0, 0);
			}
		}
		for (int mode = options->decode_only ? 1 : 0; mode < 3; ++mode)
		{
			double total = 0;
			for (int iteration = 0; iteration < iterations; ++iteration)
			{
				double start = bench_now();
				size_t written;
				if (mode == 0) written = rg_rgi_encode_with_workspace(image->pixels, image->width, image->height,
				    reused, bound, workspace, rg_rgi_encode_workspace_size());
				else if (mode == 1) written = rg_rgi_decode(image->encoded[0].data, image->encoded[0].size,
				    reused, bytes, NULL, NULL);
				else written = rg_rgi_decode_trusted(image->encoded[0].data, image->encoded[0].size,
				    reused, bytes, NULL, NULL);
				total += bench_now() - start;
				if (written != (mode == 0 ? image->encoded[0].size : bytes) ||
				    bench_consume(reused, written) != (mode == 0 ? hashes[0] : pixels_hash)) bench_die("reused output differs");
			}
			if (sample >= 0) bench_record(image, 0, mode == 0 ? "encode" : "decode",
			    mode == 2 ? "trusted_reused" : "reused", sample, image->encoded[0].size, hashes[0],
			    total / iterations, 1u, 0, 0, 0, 0);
		}
	}
	free(workspace);
	free(reused);
}

#ifdef RG_IMAGE_BENCH_PALETTE
#include "bench_palette.h"
#endif

#ifdef RG_IMAGE_BENCH_GPU
#include "bench_gpu.h"
#endif

static void bench_encoder_profile(const BenchImage* image, const BenchOptions* options)
{
	u64 pixels = (u64)image->width * image->height;
	if (pixels > (SIZE_MAX - 64u) / 6u) bench_die("profile output size overflow");
	u8* output = (u8*)malloc((size_t)pixels * 6u + 64u);
	RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
	if (!output || !workspace) bench_die("encoder profile allocation failed");
	static const char* stages[] = {"encode_profile0", "estimate_profile1", "emit_profile1"};
	for (int sample = -1; sample < options->samples; ++sample)
	{
		for (int stage = 0; stage < 3; ++stage)
		{
			double start = bench_now();
			size_t size;
			if (stage == 0) size = rg_rgi_encode_qoi_payload(image->pixels, pixels, output);
			else if (stage == 1) size = rg_rgi_estimate_rawspan_payload_size_min_copy(image->pixels, pixels,
			    image->width, 1, RG_RGI_COPY_MIN, &workspace->table);
			else size = rg_rgi_encode_rawspan_payload_min_copy(image->pixels, pixels, image->width,
			    output, 1, RG_RGI_COPY_MIN, &workspace->table);
			double elapsed = bench_now() - start;
			u64 hash = stage == 1 ? (u64)size : bench_consume(output, size);
			bench_checksum ^= hash;
			if (sample >= 0) bench_record(image, 0, stages[stage], "profile_unbounded", sample,
			    size, hash, elapsed, 1u, 0, 0, 0, 0);
		}
	}
	free(workspace); free(output);
}

#ifdef RG_IMAGE_BENCH_GPU
static BenchImage bench_scene_images[8];
static u32 bench_scene_count;
static size_t bench_scene_bytes;

static void bench_scene_flush(const BenchOptions* options)
{
	if (!bench_scene_count) return;
	bench_gpu_scene(bench_scene_images, bench_scene_count, options);
	for (u32 i = 0; i < bench_scene_count; ++i)
	{
		free((void*)bench_scene_images[i].name);
		bench_free_image(&bench_scene_images[i]);
	}
	bench_scene_count = 0;
	bench_scene_bytes = 0;
}
#endif

static void bench_process(BenchImage* image, const BenchOptions* options)
{
	fprintf(stderr, "IMAGE\t%s\t%ux%u\n", image->name, image->width, image->height);
	fflush(stderr);
	bench_prepare(image);
#ifdef RG_IMAGE_BENCH_GPU
	if (options->gpu_scene)
	{
		size_t bytes = bench_pixel_bytes(image);
		if (bench_scene_count && (bench_scene_count == 8u ||
		    bench_scene_bytes + bytes > 64u * 1024u * 1024u)) bench_scene_flush(options);
		char* name = (char*)malloc(strlen(image->name) + 1u);
		if (!name) bench_die("scene name allocation failed");
		strcpy(name, image->name);
		bench_scene_images[bench_scene_count] = *image;
		bench_scene_images[bench_scene_count++].name = name;
		bench_scene_bytes += bytes;
		memset(image, 0, sizeof(*image));
		return;
	}
#endif
	if (options->decode_only && !options->gpu && !options->verify_only)
	{
		for (int codec = 0; codec < BENCH_CODEC_COUNT; ++codec)
			bench_record(image, codec, "encode", "preparation_single", 0, image->encoded[codec].size,
			    bench_consume(image->encoded[codec].data, image->encoded[codec].size),
			    image->prepare_encode_ns[codec], 1u, 0, 0, 0, 0);
	}
#ifdef RG_IMAGE_BENCH_PALETTE
	if (options->palette_experiment)
	{
		bench_palette(image, options);
		bench_free_image(image);
		return;
	}
#endif
	if (options->fixtures)
	{
		char path[4096];
		const char* name = strrchr(image->name, '/');
		name = name ? name + 1 : image->name;
		int length = snprintf(path, sizeof(path), "%s/%s.rgi", options->fixtures, name);
		if (length < 0 || (size_t)length >= sizeof(path)) bench_die("fixture path too long");
		FILE* file = bench_fopen(path, "wb");
		if (!file) bench_die("cannot create fixture");
		if (fwrite(image->encoded[0].data, 1u, image->encoded[0].size, file) != image->encoded[0].size)
			bench_die("fixture write failed");
		if (fclose(file)) bench_die("fixture close failed");
	}
	if (options->profile_encoder) bench_encoder_profile(image, options);
	else if (!options->verify_only)
	{
#ifdef RG_IMAGE_BENCH_GPU
		if (options->gpu) bench_gpu(image, options);
		else
#endif
		bench_cpu(image, options);
	}
	bench_free_image(image);
}

static int bench_positive(const char* value)
{
	char* end = NULL;
	errno = 0;
	long result = strtol(value, &end, 10);
	if (errno || !*value || *end || result < 1 || result > 100000) bench_die("invalid sample/iteration count");
	return (int)result;
}

static int bench_main(int argc, char** argv)
{
	BenchOptions options = {0};
	options.samples = 7;
	options.iterations = 1;
	for (int i = 1; i < argc; ++i)
	{
		const char* arg = argv[i];
		if (!strcmp(arg, "--decode-only")) options.decode_only = 1;
		else if (!strcmp(arg, "--verify-only")) options.verify_only = 1;
		else if (!strcmp(arg, "--profile-encoder")) options.profile_encoder = 1;
		else if (!strcmp(arg, "--gpu-scene")) options.gpu_scene = 1;
		else if (!strcmp(arg, "--palette-experiment")) options.palette_experiment = 1;
		else
		{
			if (i + 1 >= argc) bench_die("option requires a value");
			const char* value = argv[++i];
			if (!strcmp(arg, "--manifest")) options.manifest = value;
			else if (!strcmp(arg, "--samples")) options.samples = bench_positive(value);
			else if (!strcmp(arg, "--iterations")) options.iterations = bench_positive(value);
			else if (!strcmp(arg, "--filter")) options.filter = value;
			else if (!strcmp(arg, "--gpu")) options.gpu = value;
			else if (!strcmp(arg, "--save-fixtures")) options.fixtures = value;
			else bench_die("unknown option");
		}
	}
#ifndef RG_IMAGE_BENCH_GPU
	if (options.gpu) bench_die("this executable was built without SDL GPU support");
#endif
#ifndef RG_IMAGE_BENCH_PALETTE
	if (options.palette_experiment) bench_die("this executable was built without the palette experiment");
#endif
	if (options.palette_experiment && (options.gpu || options.profile_encoder || options.fixtures))
		bench_die("--palette-experiment is CPU-only and does not save encoded fixtures");
	if (options.gpu_scene && (!options.gpu || options.fixtures || options.decode_only))
		bench_die("--gpu-scene requires --gpu and does not support fixtures or CPU-only flags");
	if (options.gpu && (options.verify_only || options.profile_encoder))
		bench_die("GPU validation is always enabled; use --samples 1, without CPU-only verification/profiling flags");
	rg_time_init();
#if RG_PLATFORM_WINDOWS
	if (!options.gpu)
	{
		DWORD_PTR process_mask = 0, system_mask = 0;
		if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) bench_die("cannot query CPU affinity");
		DWORD_PTR selected = (process_mask & 4u) ? 4u : process_mask & (~process_mask + 1u);
		if (!SetThreadAffinityMask(GetCurrentThread(), selected)) bench_die("cannot set benchmark CPU affinity");
		fprintf(stderr, "AFFINITY mask=%llu\n", (unsigned long long)selected);
	}
#endif
	stbi_write_png_compression_level = 8;
	stbi_write_force_png_filter = -1;
	fprintf(stderr, "CONFIG samples=%d iterations=%d rgba=8x4 stb_png_level=8 adaptive=1 workspace=%zu\n",
	        options.samples, options.iterations, rg_rgi_encode_workspace_size());
#ifdef RG_IMAGE_BENCH_LIBPNG
	fprintf(stderr, "LIBPNG version=%s zlib=%s level=6 filters=all\n", PNG_LIBPNG_VER_STRING, zlibVersion());
#endif
	puts("image,width,height,codec,operation,mode,sample,encoded_bytes,profile,encoded_hash,elapsed_ns,count,decode_ns,stage_ns,submit_ns,completion_ns");
#ifdef RG_IMAGE_BENCH_GPU
	if (options.gpu) bench_gpu_init(options.gpu);
#endif
	int processed = 0;
	if (options.manifest)
	{
		FILE* manifest = bench_fopen(options.manifest, "rb");
		if (!manifest) bench_die("cannot open manifest");
		char line[8192];
		while (fgets(line, sizeof(line), manifest))
		{
			size_t length = strlen(line);
			if (length == sizeof(line) - 1u && line[length - 1u] != '\n') bench_die("manifest line too long");
			while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) line[--length] = 0;
			if (!length) continue;
			char* path = strchr(line, '\t');
			if (!path) bench_die("manifest requires name TAB absolute-path");
			*path++ = 0;
			if (options.filter && !strstr(line, options.filter)) continue;
			BenchImage image = {0};
			if (!bench_load_image(&image, path, line)) continue;
			bench_process(&image, &options);
			++processed;
		}
		if (ferror(manifest)) bench_die("manifest read failed");
		fclose(manifest);
	}
	else
	{
		for (int pattern = 0; pattern < 9; ++pattern)
		{
			BenchImage image = {0};
			bench_synthetic(&image, pattern);
			if (options.filter && !strstr(image.name, options.filter)) { bench_free_image(&image); continue; }
			bench_process(&image, &options);
			++processed;
		}
	}
#ifdef RG_IMAGE_BENCH_GPU
	if (options.gpu_scene) bench_scene_flush(&options);
	if (options.gpu) bench_gpu_shutdown();
#endif
	if (!processed) bench_die("no supported images matched");
	fprintf(stderr, "COMPLETE images=%d checksum=%llu\n", processed, (unsigned long long)bench_checksum);
	return 0;
}

#if RG_PLATFORM_WINDOWS
int wmain(int argc, wchar_t** wide_argv)
{
	char** argv = (char**)calloc((size_t)argc + 1u, sizeof(*argv));
	if (!argv) bench_die("command-line allocation failed");
	for (int i = 0; i < argc; ++i)
	{
		int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
		                                 NULL, 0, NULL, NULL);
		if (!length) bench_die("invalid Unicode command-line argument");
		argv[i] = (char*)malloc((size_t)length);
		if (!argv[i] || !WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1,
		                                    argv[i], length, NULL, NULL))
			bench_die("command-line UTF-8 conversion failed");
	}
	int result = bench_main(argc, argv);
	for (int i = 0; i < argc; ++i) free(argv[i]);
	free(argv);
	return result;
}
#else
int main(int argc, char** argv)
{
	return bench_main(argc, argv);
}
#endif
