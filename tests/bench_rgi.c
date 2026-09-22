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

#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#if RG_COMPILER_MSVC
    #pragma warning(push)
    #pragma warning(disable: 4244)
#endif
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"
#if RG_COMPILER_MSVC
    #pragma warning(pop)
#endif

#include <limits.h>
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
    uint8_t* png;
    size_t png_size;
} BenchImage;

typedef struct PngBuffer
{
    uint8_t* data;
    size_t size;
    size_t capacity;
    int failed;
} PngBuffer;

typedef struct PngSizeSink
{
    size_t size;
    int failed;
} PngSizeSink;

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

static int generate_corpus(BenchImage images[BENCH_IMAGES])
{
    static const char* names[BENCH_IMAGES] = {"flat-runs", "tiles", "gradient", "noise"};
    uint32_t random_state = UINT32_C(0x52474946);
    for (size_t image = 0u; image < BENCH_IMAGES; image++)
    {
        images[image].name = names[image];
        images[image].pixels = (uint8_t*)malloc((size_t)BENCH_WIDTH * BENCH_HEIGHT * 4u);
        if (images[image].pixels == NULL) return 0;
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
    return 1;
}

static void png_write_callback(void* context, void* data, int size)
{
    PngBuffer* buffer = (PngBuffer*)context;
    size_t byte_count;
    size_t required;
    size_t capacity;
    uint8_t* resized;

    if (buffer->failed || size <= 0) return;
    byte_count = (size_t)size;
    if (buffer->size > SIZE_MAX - byte_count)
    {
        buffer->failed = 1;
        return;
    }

    required = buffer->size + byte_count;
    if (required > buffer->capacity)
    {
        capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
        while (capacity < required)
        {
            if (capacity > SIZE_MAX / 2u)
            {
                capacity = required;
                break;
            }
            capacity *= 2u;
        }
        resized = (uint8_t*)realloc(buffer->data, capacity);
        if (resized == NULL)
        {
            buffer->failed = 1;
            return;
        }
        buffer->data = resized;
        buffer->capacity = capacity;
    }

    memcpy(buffer->data + buffer->size, data, byte_count);
    buffer->size = required;
}

static int encode_png(const uint8_t* pixels, PngBuffer* output)
{
    int wrote_png;
    memset(output, 0, sizeof(*output));
    wrote_png = stbi_write_png_to_func(png_write_callback,
                                       output,
                                       BENCH_WIDTH,
                                       BENCH_HEIGHT,
                                       4,
                                       pixels,
                                       BENCH_WIDTH * 4);
    if (!wrote_png || output->failed || output->size == 0u)
    {
        free(output->data);
        memset(output, 0, sizeof(*output));
        return 0;
    }
    return 1;
}

static void png_size_callback(void* context, void* data, int size)
{
    PngSizeSink* sink = (PngSizeSink*)context;
    size_t byte_count;
    (void)data;
    if (sink->failed || size <= 0) return;
    byte_count = (size_t)size;
    if (sink->size > SIZE_MAX - byte_count)
    {
        sink->failed = 1;
        return;
    }
    sink->size += byte_count;
}

static int encode_png_size(const uint8_t* pixels, size_t* output_size)
{
    PngSizeSink sink;
    int wrote_png;
    memset(&sink, 0, sizeof(sink));
    wrote_png = stbi_write_png_to_func(png_size_callback,
                                       &sink,
                                       BENCH_WIDTH,
                                       BENCH_HEIGHT,
                                       4,
                                       pixels,
                                       BENCH_WIDTH * 4);
    if (!wrote_png || sink.failed || sink.size == 0u) return 0;
    *output_size = sink.size;
    return 1;
}

static int prepare_encoded(BenchImage images[BENCH_IMAGES])
{
    qoi_desc desc = {BENCH_WIDTH, BENCH_HEIGHT, 4, QOI_SRGB};
    size_t rgi_bound = rg_rgi_encode_bound(BENCH_WIDTH, BENCH_HEIGHT);
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        PngBuffer png;
        images[i].rgi = (uint8_t*)malloc(rgi_bound);
        if (images[i].rgi == NULL)
        {
            fprintf(stderr, "Failed to allocate RGI buffer for '%s'.\n", images[i].name);
            return 0;
        }
        images[i].rgi_size = rg_rgi_encode(images[i].pixels,
                                           BENCH_WIDTH,
                                           BENCH_HEIGHT,
                                           images[i].rgi,
                                           rgi_bound);
        images[i].qoi = qoi_encode(images[i].pixels, &desc, &images[i].qoi_size);
        if (images[i].rgi_size == 0u)
        {
            fprintf(stderr, "Failed to encode RGI benchmark image '%s'.\n", images[i].name);
            return 0;
        }
        if (images[i].qoi == NULL || images[i].qoi_size <= 0)
        {
            fprintf(stderr, "Failed to encode QOI benchmark image '%s'.\n", images[i].name);
            return 0;
        }
        if (!encode_png(images[i].pixels, &png))
        {
            fprintf(stderr, "Failed to encode PNG benchmark image '%s'.\n", images[i].name);
            return 0;
        }
        images[i].png = png.data;
        images[i].png_size = png.size;
    }
    return 1;
}

static int validate_png_round_trips(const BenchImage images[BENCH_IMAGES])
{
    size_t pixel_size = (size_t)BENCH_WIDTH * BENCH_HEIGHT * 4u;
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        uint8_t* decoded;
        if (images[i].png_size > INT_MAX)
        {
            fprintf(stderr, "PNG benchmark image '%s' is too large to decode.\n", images[i].name);
            return 0;
        }
        decoded = stbi_load_from_memory(images[i].png,
                                        (int)images[i].png_size,
                                        &width,
                                        &height,
                                        &channels,
                                        4);
        if (decoded == NULL)
        {
            const char* reason = stbi_failure_reason();
            fprintf(stderr,
                    "Failed to decode PNG benchmark image '%s': %s.\n",
                    images[i].name,
                    reason != NULL ? reason : "unknown stb_image error");
            return 0;
        }
        if (width != BENCH_WIDTH || height != BENCH_HEIGHT ||
            memcmp(images[i].pixels, decoded, pixel_size) != 0)
        {
            fprintf(stderr, "PNG round-trip mismatch for benchmark image '%s'.\n", images[i].name);
            stbi_image_free(decoded);
            return 0;
        }
        stbi_image_free(decoded);
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

static double benchmark_png_encode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            size_t size = 0u;
            if (!encode_png_size(images[i].pixels, &size))
            {
                fprintf(stderr,
                        "PNG encode failed during timed iteration %d for '%s'.\n",
                        iteration + 1,
                        images[i].name);
                return -1.0;
            }
            *checksum += size;
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

static double benchmark_png_decode(const BenchImage images[BENCH_IMAGES], volatile size_t* checksum)
{
    double start = now_seconds();
    for (int iteration = 0; iteration < BENCH_ITERATIONS; iteration++)
    {
        for (size_t i = 0u; i < BENCH_IMAGES; i++)
        {
            int width = 0;
            int height = 0;
            int channels = 0;
            uint8_t* output = stbi_load_from_memory(images[i].png,
                                                    (int)images[i].png_size,
                                                    &width,
                                                    &height,
                                                    &channels,
                                                    4);
            if (output == NULL)
            {
                const char* reason = stbi_failure_reason();
                fprintf(stderr,
                        "PNG decode failed during timed iteration %d for '%s': %s.\n",
                        iteration + 1,
                        images[i].name,
                        reason != NULL ? reason : "unknown stb_image error");
                return -1.0;
            }
            if (width != BENCH_WIDTH || height != BENCH_HEIGHT)
            {
                fprintf(stderr,
                        "PNG decode returned invalid dimensions during timed iteration %d for '%s'.\n",
                        iteration + 1,
                        images[i].name);
                stbi_image_free(output);
                return -1.0;
            }
            *checksum += (size_t)width * height * 4u;
            stbi_image_free(output);
        }
    }
    return now_seconds() - start;
}

static void free_corpus(BenchImage images[BENCH_IMAGES])
{
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        free(images[i].png);
        free(images[i].qoi);
        free(images[i].rgi);
        free(images[i].pixels);
    }
}

int main(void)
{
    BenchImage images[BENCH_IMAGES];
    int result = 1;
    memset(images, 0, sizeof(images));
    stbi_write_png_compression_level = 8;
    stbi_write_force_png_filter = -1;
    if (!generate_corpus(images))
    {
        fprintf(stderr, "Failed to allocate benchmark corpus.\n");
        goto cleanup;
    }
    if (!prepare_encoded(images))
    {
        fprintf(stderr, "Failed to prepare benchmark corpus.\n");
        goto cleanup;
    }
    if (!validate_png_round_trips(images))
    {
        goto cleanup;
    }

    size_t rgi_total = 0u;
    size_t qoi_total = 0u;
    size_t png_total = 0u;
    printf("RGI deterministic benchmark\n");
    printf("seed=0x52474946 images=%d dimensions=%dx%d iterations=%d\n",
           BENCH_IMAGES, BENCH_WIDTH, BENCH_HEIGHT, BENCH_ITERATIONS);
    printf("png=stb compression_level=8 filter=adaptive\n");
    #if RG_PLATFORM_WINDOWS
        printf("timer=QueryPerformanceCounter; timings include output allocation/free\n");
    #else
        printf("timer=clock CPU time; timings include output allocation/free\n");
    #endif
    for (size_t i = 0u; i < BENCH_IMAGES; i++)
    {
        rgi_total += images[i].rgi_size;
        qoi_total += (size_t)images[i].qoi_size;
        png_total += images[i].png_size;
        printf("%-12s rgi=%8zu profile=%u qoi=%8d png(stb)=%8zu\n",
               images[i].name,
               images[i].rgi_size,
               (unsigned)images[i].rgi[13],
               images[i].qoi_size,
               images[i].png_size);
    }
    printf("total        rgi=%8zu qoi=%8zu png(stb)=%8zu\n", rgi_total, qoi_total, png_total);

    volatile size_t checksum = 0u;
    double rgi_encode = benchmark_rgi_encode(images, &checksum);
    double qoi_encode_time = benchmark_qoi_encode(images, &checksum);
    double png_encode_time = benchmark_png_encode(images, &checksum);
    if (png_encode_time < 0.0) goto cleanup;
    double rgi_decode_time = benchmark_rgi_decode(images, &checksum);
    double qoi_decode_time = benchmark_qoi_decode(images, &checksum);
    double png_decode_time = benchmark_png_decode(images, &checksum);
    if (png_decode_time < 0.0) goto cleanup;
    double operations = (double)(BENCH_IMAGES * BENCH_ITERATIONS);
    printf("encode ms/image: RGI %.3f, QOI %.3f, PNG (stb) %.3f\n",
           rgi_encode * 1000.0 / operations,
           qoi_encode_time * 1000.0 / operations,
           png_encode_time * 1000.0 / operations);
    printf("decode ms/image: RGI %.3f, QOI %.3f, PNG (stb) %.3f\n",
           rgi_decode_time * 1000.0 / operations,
           qoi_decode_time * 1000.0 / operations,
           png_decode_time * 1000.0 / operations);
    printf("checksum=%zu\n", (size_t)checksum);
    result = 0;

cleanup:
    free_corpus(images);
    return result;
}
