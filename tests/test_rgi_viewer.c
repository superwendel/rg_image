// Exercise the real viewer upload path without a window or graphics device.
#define main rgi_viewer_application_main
#include "../tools/rgi_viewer.c"
#undef main

#define VIEW_CHECK(condition, message) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: %s (line %d): %s\n", message, __LINE__, SDL_GetError()); \
		goto cleanup; \
	} \
} while (0)

int main(void)
{
	enum { WIDTH = 73, HEIGHT = 71 };
	const size_t bytes = (size_t)WIDTH * HEIGHT * 4u;
	const size_t bound = rg_rgi_encode_bound(WIDTH, HEIGHT);
	uint8_t* pixels = (uint8_t*)malloc(bytes);
	uint8_t* encoded = (uint8_t*)malloc(bound);
	SDL_Surface* target = NULL;
	SDL_Surface* readback = NULL;
	SDL_Surface* converted = NULL;
	SDL_Renderer* renderer = NULL;
	SDL_Texture* texture = NULL;
	FILE* file = NULL;
	char path[160];
	(void)snprintf(path, sizeof(path), "build/rgi_viewer_%llu.rgi",
		(unsigned long long)SDL_GetPerformanceCounter());
	int ok = 0;
	VIEW_CHECK(pixels && encoded, "allocate original synthetic artwork");
	uint32_t random = 0x13579bdfu;
	for (size_t i = 0u; i < (size_t)WIDTH * HEIGHT; ++i)
	{
		random ^= random << 13u; random ^= random >> 17u; random ^= random << 5u;
		uint8_t color = (uint8_t)(random & 7u);
		pixels[i * 4u] = (uint8_t)(13u + color * 29u);
		pixels[i * 4u + 1u] = (uint8_t)(239u - color * 17u);
		pixels[i * 4u + 2u] = (uint8_t)(37u + color * 23u);
		pixels[i * 4u + 3u] = (uint8_t)(color == 0u ? 0u : color == 1u ? 128u : 255u);
	}
	VIEW_CHECK(SDL_Init(0), "initialize SDL without video");
	target = SDL_CreateSurface(WIDTH, HEIGHT, SDL_PIXELFORMAT_RGBA32);
	VIEW_CHECK(target != NULL, "create offscreen RGBA surface");
	renderer = SDL_CreateSoftwareRenderer(target);
	VIEW_CHECK(renderer != NULL, "create headless software renderer");

	for (uint8_t profile = 0u; profile <= 2u; ++profile)
	{
		size_t encoded_size;
		if (profile == 2u)
		{
			encoded_size = rg_rgi_encode(pixels, WIDTH, HEIGHT, encoded, bound);
			VIEW_CHECK(encoded_size > 22u && memcmp(encoded, "rgif", 4u) == 0 && encoded[13] == 2u,
				"synthetic fixture selects current palette profile 2");
		}
		else
		{
			// Independent profile-0/1 streams: RGBA literals are valid in both.
			static const uint8_t header[14] = {
				'r', 'g', 'i', 'f', WIDTH, 0, 0, 0, HEIGHT, 0, 0, 0, 4, 0
			};
			VIEW_CHECK(bound >= 14u + (bytes / 4u) * 5u + 8u, "literal fixture buffer capacity");
			memcpy(encoded, header, sizeof(header));
			encoded[13] = profile;
			encoded_size = sizeof(header);
			for (size_t i = 0u; i < bytes; i += 4u)
			{
				encoded[encoded_size++] = 0xffu;
				memcpy(encoded + encoded_size, pixels + i, 4u);
				encoded_size += 4u;
			}
			memset(encoded + encoded_size, 0, 8u);
			encoded[encoded_size + 7u] = 1u;
			encoded_size += 8u;
		}

		file = fopen(path, "wb");
		VIEW_CHECK(file != NULL && fwrite(encoded, 1u, encoded_size, file) == encoded_size,
			"write temporary viewer input");
		int close_result = fclose(file);
		file = NULL;
		VIEW_CHECK(close_result == 0, "close temporary viewer input");
		uint32_t width = 0u, height = 0u;
		texture = load_rgi_to_texture(renderer, path, &width, &height);
		VIEW_CHECK(texture != NULL && width == WIDTH && height == HEIGHT,
			"viewer loads each profile into a streaming texture");
		SDL_ScaleMode scale_mode = SDL_SCALEMODE_LINEAR;
		VIEW_CHECK(SDL_GetTextureScaleMode(texture, &scale_mode) && scale_mode == SDL_SCALEMODE_NEAREST,
			"viewer selects nearest-neighbor scaling for pixel art");
		VIEW_CHECK(SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE), "disable blending for exact upload check");
		VIEW_CHECK(SDL_RenderTexture(renderer, texture, NULL, NULL), "render uploaded texture offscreen");
		readback = SDL_RenderReadPixels(renderer, NULL);
		VIEW_CHECK(readback != NULL, "read back software-rendered texture");
		converted = SDL_ConvertSurface(readback, SDL_PIXELFORMAT_RGBA32);
		VIEW_CHECK(converted != NULL && converted->w == WIDTH && converted->h == HEIGHT,
			"normalize readback byte order");
		for (size_t y = 0u; y < HEIGHT; ++y)
		{
			VIEW_CHECK(memcmp((const uint8_t*)converted->pixels + y * (size_t)converted->pitch,
				pixels + y * WIDTH * 4u, WIDTH * 4u) == 0,
				"viewer texture pixels retain exact RGBA including hidden RGB");
		}
		SDL_DestroySurface(converted); converted = NULL;
		SDL_DestroySurface(readback); readback = NULL;
		SDL_DestroyTexture(texture); texture = NULL;

		encoded[encoded_size - 1u] = 0u;
		file = fopen(path, "wb");
		VIEW_CHECK(file != NULL && fwrite(encoded, 1u, encoded_size, file) == encoded_size,
			"write malformed temporary viewer input");
		close_result = fclose(file);
		file = NULL;
		VIEW_CHECK(close_result == 0, "close malformed temporary viewer input");
		texture = load_rgi_to_texture(renderer, path, NULL, NULL);
		VIEW_CHECK(texture == NULL, "viewer rejects corrupt footer in every profile");
	}
	ok = 1;
cleanup:
	if (file) fclose(file);
	SDL_DestroyTexture(texture);
	SDL_DestroySurface(converted);
	SDL_DestroySurface(readback);
	SDL_DestroyRenderer(renderer);
	SDL_DestroySurface(target);
	SDL_Quit();
	free(encoded); free(pixels);
	(void)remove(path);
	if (ok) puts("rgi_viewer headless upload: profiles 0/1/2, nearest scaling, and malformed input passed");
	return ok ? 0 : 1;
}
