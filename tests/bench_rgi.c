#include "rg_rgi.h"

#if RG_COMPILER_MSVC
    #pragma warning(push)
    #pragma warning(disable: 4244)
#endif
#define QOI_NO_STDIO
#define QOI_IMPLEMENTATION
#include "../third_party/qoi/qoi.h"
#if RG_COMPILER_MSVC
    #pragma warning(pop)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if RG_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

enum { BENCH_WIDTH = 512, BENCH_HEIGHT = 512, BENCH_IMAGES = 4, BENCH_ITERATIONS = 20 };

typedef struct BenchImage
{
    const char* name;
    uint8_t* pixels;
    uint8_t* rgi;
    size_t rgi_size;
    void* qoi;
    int qoi_size;
} BenchImage;

static double now_seconds(void)
{
    #if RG_PLATFORM_WINDOWS
        static LARGE_INTEGER frequency;
        LARGE_INTEGER counter;
        if (frequency.QuadPart == 0) QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        return (double)counter.QuadPart / (double)frequency.QuadPart;
    #else
        return (double)clock() / (double)CLOCKS_PER_SEC;
    #endif
}

static uint32_t next_random(uint32_t* state)
{
    uint32_t value = *state;
    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static void generate_corpus(BenchImage images[BENCH_IMAGES])
{
    static const char* names[BENCH_IMAGES] = {"flat-runs", "tiles", "gradient", "noise"};
    uint32_t random_state = UINT32_C(0x52474946);
    for (size_t image = 0u; image < BENCH_IMAGES; image++)
    {
        images[image].name = names[image];
        images[image].pixels = (uint8_t*)malloc((size_t)BENCH_WIDTH * BENCH_HEIGHT * 4u);
        for (uint32_t y = 0u; y < BENCH_HEIGHT; y++)
        {
            for (uint32_t x = 0u; x < BENCH_WIDTH; x++)
            {
                size_t offset = ((size_t)y * BENCH_WIDTH + x) * 4u;
                uint8_t r, g, b;
                if (image == 0u)
                {
                    r = (uint8_t)((y / 32u) * 13u);
                    g = (uint8_t)((y / 32u) * 7u);
                    b = 96u;
                }
                else if (image == 1u)
                {
                    uint32_t tile_x = x & 31u;
                    uint32_t tile_y = y & 31u;
                    r = (uint8_t)(tile_x * 8u);
                    g = (uint8_t)(tile_y * 8u);
                    b = (uint8_t)((tile_x ^ tile_y) * 8u);
                }
                else if (image == 2u)
                {
                    r = (uint8_t)x;
                    g = (uint8_t)y;
                    b = (uint8_t)(x + y);
                }
                else
                {
                    uint32_t value = next_random(&random_state);
                    r = (uint8_t)value;
                    g = (uint8_t)(value >> 8u);
                    b = (uint8_t)(value >> 16u);
                }
                images[image].pixels[offset + 0u] = r;
                images[image].pixels[offset + 1u] = g;
                images[image].pixels[offset + 2u] = b;
                images[image].pixels[offset + 3u] = 255u;
            }
        }
    }
}

static int prepare_encoded(BenchImage images[BENCH_IMAGES])
{
    qoi_desc desc = {BENCH_WIDTH, BENCH_HEIGHT, 4, QOI_SRGB};
    size_t rgi_bound = rg_rgi_encode_bound(BENCH_WIDTH, BENCH_HEIGHT);
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        images[i].rgi = (uint8_t*)malloc(rgi_bound);
        if (images[i].rgi == NULL) return 0;
        images[i].rgi_size = rg_rgi_encode(images[i].pixels,
                                           BENCH_WIDTH,
                                           BENCH_HEIGHT,
                                           images[i].rgi,
                                           rgi_bound);
        images[i].qoi = qoi_encode(images[i].pixels, &desc, &images[i].qoi_size);
        if (images[i].rgi_size == 0u || images[i].qoi == NULL || images[i].qoi_size <= 0) return 0;
    }
    return 1;
}

static double benchmark_rgi_encode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    size_t bound = rg_rgi_encode_bound(BENCH_WIDTH, BENCH_HEIGHT);
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            uint8_t* output = (uint8_t*)malloc(bound);
            size_t size = output == NULL ? 0u : rg_rgi_encode(images[i].pixels,
                                                              BENCH_WIDTH,
                                                              BENCH_HEIGHT,
                                                              output,
                                                              bound);
            *checksum += size;
            free(output);
        }
    }
    return now_seconds() - start;
}

static double benchmark_qoi_encode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    qoi_desc desc = {BENCH_WIDTH, BENCH_HEIGHT, 4, QOI_SRGB};
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            int size = 0;
            void* output = qoi_encode(images[i].pixels, &desc, &size);
            *checksum += (size_t)(size > 0 ? size : 0);
            free(output);
        }
    }
    return now_seconds() - start;
}

static double benchmark_rgi_decode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    size_t pixel_size = (size_t)BENCH_WIDTH * BENCH_HEIGHT * 4u;
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            uint8_t* output = (uint8_t*)malloc(pixel_size);
            size_t size = output == NULL ? 0u : rg_rgi_decode(images[i].rgi,
                                                              images[i].rgi_size,
                                                              output,
                                                              pixel_size,
                                                              NULL,
                                                              NULL);
            *checksum += size;
            free(output);
        }
    }
    return now_seconds() - start;
}

static double benchmark_qoi_decode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            qoi_desc desc;
            void* output = qoi_decode(images[i].qoi, images[i].qoi_size, &desc, 4);
            *checksum += output != NULL ? (size_t)desc.width * desc.height * 4u : 0u;
            free(output);
        }
    }
    return now_seconds() - start;
}

int main(void)
{
    BenchImage images[BENCH_IMAGES];
    memset(images, 0, sizeof(images));
    generate_corpus(images);
    if (!prepare_encoded(images))
    {
        fprintf(stderr, "Failed to prepare benchmark corpus.\n");
        return 1;
    }

    size_t rgi_total = 0u;
    size_t qoi_total = 0u;
    printf("RGI deterministic benchmark\n");
    printf("seed=0x52474946 images=%d dimensions=%dx%d iterations=%d\n",
           BENCH_IMAGES, BENCH_WIDTH, BENCH_HEIGHT, BENCH_ITERATIONS);
    #if RG_PLATFORM_WINDOWS
        printf("timer=QueryPerformanceCounter; timings include output allocation/free\n");
    #else
        printf("timer=clock CPU time; timings include output allocation/free\n");
    #endif
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        rgi_total += images[i].rgi_size;
        qoi_total += (size_t)images[i].qoi_size;
        printf("%-12s rgi=%8zu profile=%u qoi=%8d\n",
               images[i].name,
               images[i].rgi_size,
               (unsigned)images[i].rgi[13],
               images[i].qoi_size);
    }
    printf("total        rgi=%8zu qoi=%8zu\n", rgi_total, qoi_total);

    volatile size_t checksum = 0u;
    double rgi_encode = benchmark_rgi_encode(images, &checksum);
    double qoi_encode_time = benchmark_qoi_encode(images, &checksum);
    double rgi_decode_time = benchmark_rgi_decode(images, &checksum);
    double qoi_decode_time = benchmark_qoi_decode(images, &checksum);
    double operations = (double)(BENCH_IMAGES * BENCH_ITERATIONS);
    printf("encode ms/image: RGI %.3f, QOI %.3f\n",
           rgi_encode * 1000.0 / operations,
           qoi_encode_time * 1000.0 / operations);
    printf("decode ms/image: RGI %.3f, QOI %.3f\n",
           rgi_decode_time * 1000.0 / operations,
           qoi_decode_time * 1000.0 / operations);
    printf("checksum=%zu\n", (size_t)checksum);

    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        free(images[i].qoi);
        free(images[i].rgi);
        free(images[i].pixels);
    }
    return 0;
}
