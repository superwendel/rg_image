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

static void bench_gpu_stage_image(u8* target, u32 pitch, const BenchImage* image, int codec,
                                  int direct, int premultiply, u8* reused, BenchGpuTimes* times)
{
	double begin;
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
		if (reused)
		{
			begin = bench_now();
			size_t written = rg_rgi_decode(image->encoded[0].data, image->encoded[0].size,
			                               reused, bench_pixel_bytes(image), NULL, NULL);
			times->decode += bench_now() - begin;
			if (written != bench_pixel_bytes(image)) bench_die("reused CPU decode failed");
			pixels = reused;
		}
		else if (codec != 4)
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

static void bench_gpu_submit(BenchGpuSlot* slot, const BenchImage* image, int codec,
                              int direct, int premultiply, u32 batch, u8* reused, BenchGpuTimes* times)
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
		bench_gpu_stage_image(target, pitch, image, codec, direct, premultiply, reused, times);
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
	BenchImage images[3] = {0};
	static const u32 widths[3] = {17u, 31u, 257u};
	static const u32 heights[3] = {19u, 17u, 5u};
	size_t capacity = 0;
	BenchGpuSlot slots[3];
	for (int i = 0; i < 3; ++i)
	{
		images[i].width = widths[i]; images[i].height = heights[i];
		size_t bytes = bench_pixel_bytes(&images[i]);
		images[i].pixels = (u8*)malloc(bytes);
		if (!images[i].pixels) bench_die("reuse validation allocation failed");
		if (bytes > capacity) capacity = bytes;
		bench_gpu_slot_init(&slots[i], &images[i], 1u, 0);
	}
	u8* reused = (u8*)malloc(capacity);
	if (!reused) bench_die("reuse validation scratch allocation failed");
	for (u32 round = 0; round < 6u; ++round)
	{
		BenchGpuTimes times = {0};
		int direct = round % 3u == 2u;
		int premultiply = round >= 3u && !direct;
		for (int i = 0; i < 3; ++i)
		{
			BenchImage* image = &images[i];
			for (u32 y = 0; y < image->height; ++y)
				for (u32 x = 0; x < image->width; ++x)
				{
					u8* pixel = image->pixels + ((size_t)y * image->width + x) * 4u;
					pixel[0] = (u8)(x * 11u + round * 17u);
					pixel[1] = (u8)(y * 9u + (u32)i * 13u);
					pixel[2] = (u8)(x ^ y ^ round);
					pixel[3] = (u8)(((x + y + round) % 4u) * 85u);
				}
			image->encoded[0] = bench_encode(image, 0);
			bench_gpu_submit(&slots[i], image, 0, direct, premultiply, 1u,
			                 round % 3u == 1u ? reused : NULL, &times);
			bench_release(&image->encoded[0]);
		}
		for (int i = 0; i < 3; ++i)
		{
			bench_gpu_wait(&slots[i], &times);
			bench_gpu_verify(slots[i].textures[0], &images[i], premultiply);
		}
	}
	for (int i = 0; i < 3; ++i)
	{
		bench_gpu_slot_destroy(&slots[i], 1u);
		free(images[i].pixels);
	}
	free(reused);
	fprintf(stderr, "GPU_REUSE mixed-dimensions=passed slots=3 rounds=6 allocated+reused+direct+premultiplied\n");
}

static void bench_gpu_case(const BenchImage* image, const BenchOptions* options, int codec,
                            int direct, int premultiply, int streaming, int reuse)
{
	const char* mode = direct ? "direct_tight_rgba" : reuse ?
	    (premultiply ? "staged_reused_premultiplied" : "staged_reused_rgba") :
	    (premultiply ? "staged_premultiplied" : "staged_rgba");
	u8* reused = reuse ? (u8*)malloc(bench_pixel_bytes(image)) : NULL;
	if (reuse && !reused) bench_die("GPU CPU scratch allocation failed");
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
			bench_gpu_submit(&slots[i], image, codec, direct, premultiply, batch, reused, &times);
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
	free(reused);
}

typedef struct BenchGpuScene
{
	RgGpuUploadRing ring;
	SDL_GPUTexture* textures[8];
	SDL_GPUFence* fences[8];
	u32 offsets[8], pitches[8], strides[8];
	u32 fence_count;
} BenchGpuScene;

static void bench_gpu_scene_init(BenchGpuScene* scene, const BenchImage* images, u32 count, int direct)
{
	memset(scene, 0, sizeof(*scene));
	u64 bytes = 0;
	for (u32 i = 0; i < count; ++i)
	{
		scene->pitches[i] = bench_gpu_pitch(&images[i], direct);
		scene->strides[i] = bench_gpu_stride(&images[i], direct);
		if (bytes + scene->strides[i] > UINT32_MAX) bench_die("scene transfer exceeds SDL size limit");
		scene->offsets[i] = (u32)bytes;
		bytes += scene->strides[i];
	}
	bench_gpu_check(rg_gpu_upload_ring_init(&scene->ring, bench_device, (u32)bytes), "scene transfer allocation failed");
	for (u32 i = 0; i < count; ++i)
	{
		SDL_GPUTextureCreateInfo info = {0};
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = images[i].width;
		info.height = images[i].height;
		info.layer_count_or_depth = 1u;
		info.num_levels = 1u;
		scene->textures[i] = SDL_CreateGPUTexture(bench_device, &info);
		bench_gpu_check(scene->textures[i] != NULL, "scene texture allocation failed");
	}
}

static void bench_gpu_scene_submit(BenchGpuScene* scene, const BenchImage* images, u32 count,
                                   int codec, int direct, int premultiply, u8* reused,
                                   u32 batch, BenchGpuTimes* times)
{
	if (scene->fence_count) bench_die("scene reused before upload completion");
	double begin = bench_now();
	rg_gpu_upload_ring_begin(&scene->ring, 0);
	bench_gpu_check(scene->ring.mapped != NULL, "scene transfer mapping failed");
	times->stage += bench_now() - begin;
	for (u32 i = 0; i < count; ++i)
	{
		RgGpuUploadSlice slice;
		bench_gpu_check(rg_gpu_upload_ring_alloc(&scene->ring, scene->strides[i], 512u, &slice),
		                "scene transfer exhausted");
		if (slice.offset != scene->offsets[i]) bench_die("scene transfer layout mismatch");
		u8* target = (u8*)rg_gpu_upload_ring_ptr(&scene->ring, &slice);
		bench_gpu_stage_image(target, scene->pitches[i], &images[i], codec, direct, premultiply, reused, times);
	}
	begin = bench_now();
	rg_gpu_upload_ring_end(&scene->ring);
	times->stage += bench_now() - begin;
	for (u32 first = 0; first < count; first += batch)
	{
		SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(bench_device);
		bench_gpu_check(command != NULL, "scene command acquisition failed");
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
		bench_gpu_check(copy != NULL, "scene copy pass failed");
		u32 end = first + batch < count ? first + batch : count;
		for (u32 i = first; i < end; ++i)
		{
			SDL_GPUTextureTransferInfo source = {0};
			source.transfer_buffer = scene->ring.buffer;
			source.offset = scene->offsets[i];
			source.pixels_per_row = scene->pitches[i] / 4u;
			source.rows_per_layer = images[i].height;
			SDL_GPUTextureRegion destination = {0};
			destination.texture = scene->textures[i];
			destination.w = images[i].width;
			destination.h = images[i].height;
			destination.d = 1u;
			SDL_UploadToGPUTexture(copy, &source, &destination, false);
		}
		SDL_EndGPUCopyPass(copy);
		begin = bench_now();
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
		times->submit += bench_now() - begin;
		bench_gpu_check(fence != NULL, "scene submission failed");
		scene->fences[scene->fence_count++] = fence;
	}
	begin = bench_now();
	bench_gpu_check(SDL_WaitForGPUFences(bench_device, true, scene->fences, scene->fence_count),
	                "scene completion wait failed");
	times->completion += bench_now() - begin;
	for (u32 i = 0; i < scene->fence_count; ++i) SDL_ReleaseGPUFence(bench_device, scene->fences[i]);
	scene->fence_count = 0u;
}

static void bench_gpu_scene_case(const BenchImage* images, u32 count, const BenchImage* record,
                                 const BenchOptions* options, int codec, int direct,
                                 int premultiply, int reuse, u32 batch)
{
	const char* mode = direct ? "direct_tight_rgba" : reuse ?
	    (premultiply ? "staged_reused_premultiplied" : "staged_reused_rgba") :
	    (premultiply ? "staged_premultiplied" : "staged_rgba");
	BenchGpuScene scene;
	bench_gpu_scene_init(&scene, images, count, direct);
	size_t capacity = 0, encoded = 0;
	u64 hash = 14695981039346656037ull;
	for (u32 i = 0; i < count; ++i)
	{
		size_t bytes = bench_pixel_bytes(&images[i]);
		if (bytes > capacity) capacity = bytes;
		const void* data = codec == 4 ? images[i].pixels : images[i].encoded[codec].data;
		size_t size = codec == 4 ? bytes : images[i].encoded[codec].size;
		if (size > SIZE_MAX - encoded) bench_die("scene encoded size overflow");
		encoded += size;
		// Ordered composition of each image's dimensions and full-stream hash.
		hash = (hash ^ images[i].width) * 1099511628211ull;
		hash = (hash ^ images[i].height) * 1099511628211ull;
		hash = (hash ^ bench_consume(data, size)) * 1099511628211ull;
	}
	u8* reused = reuse ? (u8*)malloc(capacity) : NULL;
	if (reuse && !reused) bench_die("scene CPU scratch allocation failed");
	for (int sample = -1; sample < options->samples; ++sample)
	{
		BenchGpuTimes times = {0};
		double start = bench_now();
		bench_gpu_scene_submit(&scene, images, count, codec, direct, premultiply, reused, batch, &times);
		double elapsed = bench_now() - start;
		if (sample == -1 || sample == options->samples - 1)
			for (u32 i = 0; i < count; ++i) bench_gpu_verify(scene.textures[i], &images[i], premultiply);
		if (sample >= 0) bench_record(record, codec, batch == 1u ? "gpu_scene_single" : "gpu_scene_batch", mode,
		    sample, encoded, hash, elapsed, 1u, times.decode, times.stage, times.submit, times.completion);
	}
	for (u32 i = 0; i < count; ++i) SDL_ReleaseGPUTexture(bench_device, scene.textures[i]);
	rg_gpu_upload_ring_destroy(&scene.ring);
	free(reused);
}

// Caller retains 1..8 prepared images until this function returns. These modes
// time each distinct image once and report whole-scene totals (count=1).
static void bench_gpu_scene(const BenchImage* images, u32 count, const BenchOptions* options)
{
	static u32 sequence;
	if (!count || count > 8u) bench_die("GPU scene requires 1..8 images");
	u64 pixels = 0;
	for (u32 i = 0; i < count; ++i) pixels += (u64)images[i].width * images[i].height;
	if (!pixels || pixels > UINT32_MAX) bench_die("scene pixel count exceeds report limit");
	char name[64];
	int length = snprintf(name, sizeof(name), "scene/%04u", sequence);
	if (length < 0 || (size_t)length >= sizeof(name)) bench_die("scene name overflow");
	u8 profile[14] = {0};
	profile[13] = 255u; // Aggregate profile marker, never an encoded RGI stream.
	BenchImage record = {0};
	record.name = name;
	record.width = (u32)pixels; record.height = 1u;
	record.encoded[0].data = profile;
	fprintf(stderr, "SCENE\t%s\timages=%u pixels=%llu\n", name, count, (unsigned long long)pixels);
	for (u32 grouping = 0; grouping < 2u; ++grouping)
	{
		// Keep the batch operation distinct even for a final one-image scene.
		u32 batch = ((grouping + sequence) & 1u) == 0u ? 1u : 8u;
		bench_gpu_scene_case(images, count, &record, options, 4, 0, 0, 0, batch);
		for (u32 order = 0; order < BENCH_CODEC_COUNT; ++order)
		{
			int codec = (int)((order + sequence) % BENCH_CODEC_COUNT);
			bench_gpu_scene_case(images, count, &record, options, codec, 0, 0, 0, batch);
			bench_gpu_scene_case(images, count, &record, options, codec, 0, 1, 0, batch);
		}
		bench_gpu_scene_case(images, count, &record, options, 0, 0, 0, 1, batch);
		bench_gpu_scene_case(images, count, &record, options, 0, 0, 1, 1, batch);
		bench_gpu_scene_case(images, count, &record, options, 0, 1, 0, 0, batch);
	}
	++sequence;
}

static void bench_gpu(const BenchImage* image, const BenchOptions* options)
{
	static u32 rotation;
	for (int streaming = 0; streaming < 2; ++streaming)
	{
		bench_gpu_case(image, options, 4, 0, 0, streaming, 0);
		for (int order = 0; order < BENCH_CODEC_COUNT; ++order)
		{
			int codec = (order + (int)(rotation % BENCH_CODEC_COUNT)) % BENCH_CODEC_COUNT;
			bench_gpu_case(image, options, codec, 0, 0, streaming, 0);
			bench_gpu_case(image, options, codec, 0, 1, streaming, 0);
		}
		bench_gpu_case(image, options, 0, 0, 0, streaming, 1);
		bench_gpu_case(image, options, 0, 0, 1, streaming, 1);
		bench_gpu_case(image, options, 0, 1, 0, streaming, 0);
	}
	++rotation;
}
#endif
