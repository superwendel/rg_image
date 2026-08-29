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

int main(void)
{
    uint8_t pixel[4] = {1u, 2u, 3u, 4u};
    uint8_t encoded[RG_RGI_HEADER_SIZE + 5u + RG_RGI_END_SIZE];
    size_t written = rg_rgi_encode(pixel, 1u, 1u, encoded, sizeof(encoded));
    return allocation_attempted && written == 0u ? 0 : 1;
}
