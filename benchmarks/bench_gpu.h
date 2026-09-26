// Offscreen SDL GPU loading tests. No presentation, shaders, or disk I/O in timing.
#ifndef RG_IMAGE_BENCH_GPU_H
#define RG_IMAGE_BENCH_GPU_H
#include "rg_gpu.h"

static SDL_GPUDevice* bench_device;
static void bench_gpu_check_reuse(void);

typedef struct BenchGpuSlot
{
	RgGpuUploadRing ring;
	SDL_GPUTexture* textures[8];
	SDL_GPUFence* fence;
} BenchGpuSlot;

typedef struct BenchGpuTimes
{
	double decode, stage, submit, completion;
} BenchGpuTimes;

static void bench_gpu_check(int result, const char* what)
{
	if (!result)
	{
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		bench_die(what);
	}
}

static void bench_gpu_init(const char* backend)
{
	bench_gpu_check(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
	RgGpuDeviceDesc desc = {0};
	desc.name = backend;
	bench_device = rg_gpu_device_create(&desc);
	bench_gpu_check(bench_device != NULL, "requested GPU backend unavailable");
	const char* actual = SDL_GetGPUDeviceDriver(bench_device);
	bench_gpu_check(actual && !strcmp(actual, backend), "GPU backend differs from requested backend");
	SDL_PropertiesID properties = SDL_GetGPUDeviceProperties(bench_device);
	fprintf(stderr, "GPU backend=%s device=%s driver=%s SDL=%d\n", actual,
	        SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
	        SDL_GetStringProperty(properties, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, "unknown"), SDL_GetVersion());
	bench_gpu_check_reuse();
}

static void bench_gpu_shutdown(void)
{
	rg_gpu_device_destroy(bench_device);
	bench_device = NULL;
	SDL_Quit();
}

static u32 bench_gpu_pitch(const BenchImage* image, int direct)
{
	u32 bytes = image->width * 4u;
	return direct ? bytes : (bytes + 255u) & ~255u;
}

static u32 bench_gpu_stride(const BenchImage* image, int direct)
{
	u64 bytes = (u64)bench_gpu_pitch(image, direct) * image->height;
	bytes = (bytes + 511u) & ~(u64)511u;
	if (bytes > UINT32_MAX) bench_die("texture transfer exceeds SDL size limit");
	return (u32)bytes;
}

static void bench_gpu_slot_init(BenchGpuSlot* slot, const BenchImage* image, u32 batch, int direct)
{
	memset(slot, 0, sizeof(*slot));
	u64 bytes = (u64)bench_gpu_stride(image, direct) * batch;
	if (bytes > UINT32_MAX) bench_die("batch transfer exceeds SDL size limit");
	bench_gpu_check(rg_gpu_upload_ring_init(&slot->ring, bench_device, (u32)bytes), "upload ring allocation failed");
	SDL_GPUTextureCreateInfo info = {0};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = image->width;
	info.height = image->height;
	info.layer_count_or_depth = 1u;
	info.num_levels = 1u;
	for (u32 i = 0; i < batch; ++i)
	{
		slot->textures[i] = SDL_CreateGPUTexture(bench_device, &info);
		bench_gpu_check(slot->textures[i] != NULL, "texture allocation failed");
	}
}

static void bench_gpu_wait(BenchGpuSlot* slot, BenchGpuTimes* times)
{
	if (!slot->fence) return;
	double start = bench_now();
	bench_gpu_check(SDL_WaitForGPUFences(bench_device, true, &slot->fence, 1u), "GPU fence wait failed");
	times->completion += bench_now() - start;
	SDL_ReleaseGPUFence(bench_device, slot->fence);
	slot->fence = NULL;
}

static void bench_gpu_slot_destroy(BenchGpuSlot* slot, u32 batch)
{
	if (slot->fence) bench_die("attempt to release a pending upload slot");
	for (u32 i = 0; i < batch; ++i) SDL_ReleaseGPUTexture(bench_device, slot->textures[i]);
	rg_gpu_upload_ring_destroy(&slot->ring);
}

static void bench_gpu_pack(u8* output, u32 pitch, const u8* pixels, const BenchImage* image, int premultiply)
{
	for (u32 y = 0; y < image->height; ++y)
	{
		const u8* source = pixels + (size_t)y * image->width * 4u;
		u8* target = output + (size_t)y * pitch;
		if (!premultiply) memcpy(target, source, (size_t)image->width * 4u);
		else
		{
			for (u32 x = 0; x < image->width; ++x)
			{
				u32 alpha = source[x * 4u + 3u];
				for (u32 channel = 0; channel < 3u; ++channel)
					target[x * 4u + channel] = (u8)(((u32)source[x * 4u + channel] * alpha + 127u) / 255u);
				target[x * 4u + 3u] = (u8)alpha;
			}
		}
	}
}

static void bench_gpu_submit(BenchGpuSlot* slot, const BenchImage* image, int codec,
                              int direct, int premultiply, u32 batch, BenchGpuTimes* times)
{
	if (slot->fence) bench_die("upload slot reused before completion");
	u32 pitch = bench_gpu_pitch(image, direct);
	u32 stride = bench_gpu_stride(image, direct);
	double begin = bench_now();
	rg_gpu_upload_ring_begin(&slot->ring, 0);
	bench_gpu_check(slot->ring.mapped != NULL, "transfer mapping failed");
	times->stage += bench_now() - begin;
	for (u32 i = 0; i < batch; ++i)
	{
		RgGpuUploadSlice slice;
		bench_gpu_check(rg_gpu_upload_ring_alloc(&slot->ring, stride, 512u, &slice), "upload ring exhausted");
		u8* target = (u8*)rg_gpu_upload_ring_ptr(&slot->ring, &slice);
		if (direct)
		{
			begin = bench_now();
			size_t written = rg_rgi_decode(image->encoded[0].data, image->encoded[0].size,
			                               target, bench_pixel_bytes(image), NULL, NULL);
			times->decode += bench_now() - begin;
			if (written != bench_pixel_bytes(image)) bench_die("direct mapped decode failed");
		}
		else
		{
			BenchBuffer decoded = {0};
			const u8* pixels = image->pixels;
			if (codec != 4)
			{
				begin = bench_now();
				decoded = bench_decode(image, codec, 0);
				times->decode += bench_now() - begin;
				pixels = decoded.data;
			}
			begin = bench_now();
			bench_gpu_pack(target, pitch, pixels, image, premultiply);
			times->stage += bench_now() - begin;
			bench_release(&decoded);
		}
	}
	begin = bench_now();
	rg_gpu_upload_ring_end(&slot->ring);
	times->stage += bench_now() - begin;
	SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(bench_device);
	bench_gpu_check(command != NULL, "GPU command acquisition failed");
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
	bench_gpu_check(copy != NULL, "GPU copy pass failed");
	for (u32 i = 0; i < batch; ++i)
	{
		SDL_GPUTextureTransferInfo source = {0};
		source.transfer_buffer = slot->ring.buffer;
		source.offset = i * stride;
		source.pixels_per_row = pitch / 4u;
		source.rows_per_layer = image->height;
		SDL_GPUTextureRegion destination = {0};
		destination.texture = slot->textures[i];
		destination.w = image->width;
		destination.h = image->height;
		destination.d = 1u;
		SDL_UploadToGPUTexture(copy, &source, &destination, false);
	}
	SDL_EndGPUCopyPass(copy);
	begin = bench_now();
	slot->fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
	times->submit += bench_now() - begin;
	bench_gpu_check(slot->fence != NULL, "GPU submission failed");
}

static void bench_gpu_verify(SDL_GPUTexture* texture, const BenchImage* image, int premultiply)
{
	u32 pitch = bench_gpu_pitch(image, 0);
	SDL_GPUTransferBufferCreateInfo info = {0};
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = bench_gpu_stride(image, 0);
	SDL_GPUTransferBuffer* download = SDL_CreateGPUTransferBuffer(bench_device, &info);
	bench_gpu_check(download != NULL, "readback allocation failed");
	SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(bench_device);
	bench_gpu_check(command != NULL, "readback command acquisition failed");
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
	bench_gpu_check(copy != NULL, "readback copy pass failed");
	SDL_GPUTextureRegion source = {0};
	source.texture = texture; source.w = image->width; source.h = image->height; source.d = 1u;
	SDL_GPUTextureTransferInfo destination = {0};
	destination.transfer_buffer = download;
	destination.pixels_per_row = pitch / 4u;
	destination.rows_per_layer = image->height;
	SDL_DownloadFromGPUTexture(copy, &source, &destination);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
	bench_gpu_check(fence != NULL, "readback submission failed");
	bench_gpu_check(SDL_WaitForGPUFences(bench_device, true, &fence, 1u), "readback wait failed");
	SDL_ReleaseGPUFence(bench_device, fence);
	const u8* pixels = (const u8*)SDL_MapGPUTransferBuffer(bench_device, download, false);
	bench_gpu_check(pixels != NULL, "readback mapping failed");
	for (u32 y = 0; y < image->height; ++y)
	{
		for (u32 x = 0; x < image->width; ++x)
		{
			const u8* expected = image->pixels + ((size_t)y * image->width + x) * 4u;
			const u8* actual = pixels + (size_t)y * pitch + x * 4u;
			for (u32 channel = 0; channel < 4u; ++channel)
			{
				u8 value = expected[channel];
				if (premultiply && channel != 3u) value = (u8)(((u32)value * expected[3] + 127u) / 255u);
				if (actual[channel] != value) bench_die("GPU readback pixel mismatch");
			}
		}
	}
	SDL_UnmapGPUTransferBuffer(bench_device, download);
	SDL_ReleaseGPUTransferBuffer(bench_device, download);
}

static void bench_gpu_check_reuse(void)
{
	BenchImage image = {0};
	image.width = 17u; image.height = 19u;
	image.pixels = (u8*)malloc(bench_pixel_bytes(&image));
	if (!image.pixels) bench_die("reuse validation allocation failed");
	BenchGpuSlot slots[3];
	for (int i = 0; i < 3; ++i) bench_gpu_slot_init(&slots[i], &image, 1u, 0);
	for (int round = 0; round < 4; ++round)
	{
		BenchGpuTimes times = {0};
		for (int i = 0; i < 3; ++i)
		{
			memset(image.pixels, 1 + (round * 3 + i) * 19, bench_pixel_bytes(&image));
			image.encoded[0] = bench_encode(&image, 0);
			bench_gpu_submit(&slots[i], &image, 0, round & 1, 0, 1u, &times);
			bench_release(&image.encoded[0]);
		}
		for (int i = 0; i < 3; ++i)
		{
			bench_gpu_wait(&slots[i], &times);
			memset(image.pixels, 1 + (round * 3 + i) * 19, bench_pixel_bytes(&image));
			bench_gpu_verify(slots[i].textures[0], &image, 0);
		}
	}
	for (int i = 0; i < 3; ++i) bench_gpu_slot_destroy(&slots[i], 1u);
	free(image.pixels);
	fprintf(stderr, "GPU_REUSE alternating-content=passed slots=3 rounds=4\n");
}

static void bench_gpu_case(const BenchImage* image, const BenchOptions* options, int codec,
                            int direct, int premultiply, int streaming)
{
	const char* mode = direct ? "direct_tight_rgba" : premultiply ? "staged_premultiplied" : "staged_rgba";
	u32 batch = 1u;
	BenchGpuSlot slots[3] = {0};
	if (streaming)
	{
		u64 stride = bench_gpu_stride(image, direct);
		batch = (u32)((64u * 1024u * 1024u) / (stride * 3u));
		if (batch < 1u) batch = 1u;
		if (batch > 8u) batch = 8u;
		for (int i = 0; i < 3; ++i) bench_gpu_slot_init(&slots[i], image, batch, direct);
	}
	u32 count = streaming ? batch * 3u : 1u;
	size_t encoded = codec == 4 ? bench_pixel_bytes(image) : image->encoded[codec].size;
	u64 hash = codec == 4 ? bench_consume(image->pixels, bench_pixel_bytes(image)) :
	    bench_consume(image->encoded[codec].data, image->encoded[codec].size);
	for (int sample = -1; sample < options->samples; ++sample)
	{
		BenchGpuTimes times = {0};
		double start = bench_now();
		if (!streaming) bench_gpu_slot_init(&slots[0], image, batch, direct);
		for (int i = 0; i < (streaming ? 3 : 1); ++i)
			bench_gpu_submit(&slots[i], image, codec, direct, premultiply, batch, &times);
		for (int i = 0; i < (streaming ? 3 : 1); ++i) bench_gpu_wait(&slots[i], &times);
		double elapsed = bench_now() - start;
		// Warmup and final readbacks verify both initial upload and resource reuse.
		if (sample == -1 || sample == options->samples - 1)
			for (int i = 0; i < (streaming ? 3 : 1); ++i)
				for (u32 j = 0; j < batch; ++j) bench_gpu_verify(slots[i].textures[j], image, premultiply);
		if (sample >= 0) bench_record(image, codec, streaming ? "gpu_stream" : "gpu_latency", mode,
		    sample, encoded, hash, elapsed, count, times.decode, times.stage, times.submit, times.completion);
		if (!streaming) bench_gpu_slot_destroy(&slots[0], batch);
	}
	if (streaming) for (int i = 0; i < 3; ++i) bench_gpu_slot_destroy(&slots[i], batch);
}

static void bench_gpu(const BenchImage* image, const BenchOptions* options)
{
	static u32 rotation;
	for (int streaming = 0; streaming < 2; ++streaming)
	{
		bench_gpu_case(image, options, 4, 0, 0, streaming);
		for (int order = 0; order < BENCH_CODEC_COUNT; ++order)
		{
			int codec = (order + (int)(rotation % BENCH_CODEC_COUNT)) % BENCH_CODEC_COUNT;
			bench_gpu_case(image, options, codec, 0, 0, streaming);
			bench_gpu_case(image, options, codec, 0, 1, streaming);
		}
		bench_gpu_case(image, options, 0, 1, 0, streaming);
	}
	++rotation;
}
#endif
