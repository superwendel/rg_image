#include <stddef.h>

static int allocation_attempted = 0;

static void* failing_allocate(size_t size)
{
    (void)size;
    allocation_attempted = 1;
    return NULL;
}

static void failing_free(void* pointer)
{
    (void)pointer;
}

#define RG_RGI_MALLOC(size) failing_allocate(size)
#define RG_RGI_FREE(pointer) failing_free(pointer)
#include "rg_rgi.h"

#include <string.h>

int main(void)
{
    uint8_t pixel[4] = {1u, 2u, 3u, 4u};
    uint8_t encoded[RG_RGI_HEADER_SIZE + 5u + RG_RGI_END_SIZE];
    size_t written = rg_rgi_encode(pixel, 1u, 1u, encoded, sizeof(encoded));
    if (!allocation_attempted || written != 0u) return 1;

    // A small, irregular palette wins over both legacy profiles. The caller's
    // existing workspace must suffice without invoking the failing allocator.
    static RgRgiEncodeWorkspace workspace;
    static uint8_t pixels[1024u * 4u];
    static uint8_t output[RG_RGI_HEADER_SIZE + 1024u * 5u + RG_RGI_END_SIZE];
    static uint8_t decoded[sizeof(pixels)];
    uint32_t state = UINT32_C(0x12345678);
    for (size_t i = 0u; i < 1024u; ++i)
    {
        state ^= state << 13u; state ^= state >> 17u; state ^= state << 5u;
        uint8_t index = (uint8_t)(state & 7u);
        pixels[i * 4u] = (uint8_t)(index * 31u);
        pixels[i * 4u + 1u] = (uint8_t)(index * 71u);
        pixels[i * 4u + 2u] = (uint8_t)(index * 113u);
        pixels[i * 4u + 3u] = (uint8_t)(index & 1u ? 255u : 0u);
    }
    allocation_attempted = 0;
    if (rg_rgi_encode_workspace_size() != 397312u) return 2;
    written = rg_rgi_encode_with_workspace(pixels, 32u, 32u, output, sizeof(output),
                                           &workspace, sizeof(workspace));
    if (allocation_attempted || written == 0u) return 3;
#if defined(RG_RGI_NO_PALETTE_ENCODE)
    if (output[13] > RG_RGI_PROFILE_EXTENDED) return 4;
#else
    if (output[13] != RG_RGI_PROFILE_PALETTE) return 4;
#endif
    if (rg_rgi_decode(output, written, decoded, sizeof(decoded), NULL, NULL) != sizeof(decoded) ||
        memcmp(pixels, decoded, sizeof(pixels)) != 0 || allocation_attempted) return 5;
    if (rg_rgi_decode_trusted(output, written, decoded, sizeof(decoded), NULL, NULL) != sizeof(decoded) ||
        memcmp(pixels, decoded, sizeof(pixels)) != 0 || allocation_attempted) return 6;
    return 0;
}
