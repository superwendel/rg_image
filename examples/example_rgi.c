#include "rg_rgi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	enum { WIDTH = 32, HEIGHT = 32 };
	u8 pixels[WIDTH * HEIGHT * 4];
	u8 decoded[sizeof(pixels)];
	for (u32 y = 0u; y < HEIGHT; y++)
	{
		for (u32 x = 0u; x < WIDTH; x++)
		{
			size_t i = ((size_t)y * WIDTH + x) * 4u;
			pixels[i + 0u] = (u8)(x * 8u);
			pixels[i + 1u] = (u8)(y * 8u);
			pixels[i + 2u] = (u8)((x ^ y) * 8u);
			pixels[i + 3u] = 255u;
		}
	}

	size_t bound = rg_rgi_encode_bound(WIDTH, HEIGHT);
	u8* encoded = (u8*)malloc(bound);
	if (encoded == NULL)
	{
		return 1;
	}

	size_t encoded_size = rg_rgi_encode(pixels, WIDTH, HEIGHT, encoded, bound);
	u32 width = 0u;
	u32 height = 0u;
	size_t decoded_size = rg_rgi_decode(encoded,
	                                    encoded_size,
	                                    decoded,
	                                    sizeof(decoded),
	                                    &width,
	                                    &height);
	b32 ok = encoded_size > 0u && decoded_size == sizeof(decoded) &&
	         width == WIDTH && height == HEIGHT &&
	         memcmp(pixels, decoded, sizeof(pixels)) == 0;
	if (ok)
	{
		printf("RGI %ux%u: %zu RGBA bytes -> %zu encoded bytes (profile %u)\n",
		       width, height, sizeof(pixels), encoded_size, (unsigned)encoded[13]);
	}
	free(encoded);
	return ok ? 0 : 1;
}
