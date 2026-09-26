// rg_rgi test suite
//
// Usage:
//   test_rgi.exe
//   test_rgi.exe test
//   test_rgi.exe decode <input.rgi> <output.rgba>

#ifndef RG_RGI_TEST_HEADER
#define RG_RGI_TEST_HEADER "../src/rg_rgi.h"
#endif
#include RG_RGI_TEST_HEADER

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// Test Framework
// =============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { g_tests_passed++; } while (0)

// =============================================================================
// Sample Data
// =============================================================================

static const uint8_t k_sample_rgi[] =
{
    0x72, 0x67, 0x69, 0x66,
    0x04, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00,
    0x04, 0x00,
    0xFF, 0x0A, 0x14, 0x1E, 0x28,
    0x76,
    0xAB, 0x67,
    0x0A,
    0xC2,
    0xFE, 0x05, 0x06, 0x07,
    0x7B,
    0x22,
    0xFF, 0xC8, 0x96, 0x64, 0xFF,
    0xC1,
    0xAA, 0x80,
    0x0C,
    0xFE, 0x08, 0x09, 0x0A,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

static const uint8_t k_sample_pixels[] =
{
    10, 20, 30, 40,
    11, 19, 30, 40,
    20, 30, 40, 40,
    11, 19, 30, 40,
    11, 19, 30, 40,
    11, 19, 30, 40,
    11, 19, 30, 40,
    5, 6, 7, 40,
    6, 6, 8, 40,
    20, 30, 40, 40,
    200, 150, 100, 255,
    200, 150, 100, 255,
    200, 150, 100, 255,
    210, 160, 102, 255,
    10, 20, 30, 40,
    8, 9, 10, 40
};

// =============================================================================
// Helpers
// =============================================================================

static void build_header(uint8_t* header,
                         uint32_t width,
                         uint32_t height,
                         uint8_t channels,
                         uint8_t colorspace)
{
    header[0] = (uint8_t)'r';
    header[1] = (uint8_t)'g';
    header[2] = (uint8_t)'i';
    header[3] = (uint8_t)'f';
    header[4] = (uint8_t)(width & 0xffu);
    header[5] = (uint8_t)((width >> 8u) & 0xffu);
    header[6] = (uint8_t)((width >> 16u) & 0xffu);
    header[7] = (uint8_t)((width >> 24u) & 0xffu);
    header[8] = (uint8_t)(height & 0xffu);
    header[9] = (uint8_t)((height >> 8u) & 0xffu);
    header[10] = (uint8_t)((height >> 16u) & 0xffu);
    header[11] = (uint8_t)((height >> 24u) & 0xffu);
    header[12] = channels;
    header[13] = colorspace;
}

static int read_file(const char* path, uint8_t** out_data, size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return 0;
    }

    long size = ftell(file);
    if (size < 0)
    {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0)
    {
        fclose(file);
        return 0;
    }

    uint8_t* data = (uint8_t*)malloc((size_t)size);
    if (data == NULL)
    {
        fclose(file);
        return 0;
    }

    size_t read = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (read != (size_t)size)
    {
        free(data);
        return 0;
    }

    *out_data = data;
    *out_size = (size_t)size;
    return 1;
}

static int write_file(const char* path, const void* data, size_t size)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL)
    {
        return 0;
    }

    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    return written == size;
}

static int run_decode(const char* input_path, const char* output_path)
{
    uint8_t* data = NULL;
    size_t data_size = 0;
    if (!read_file(input_path, &data, &data_size))
    {
        printf("Failed to read %s\n", input_path);
        return 1;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!rg_rgi_read_header(data, data_size, &width, &height))
    {
        printf("Invalid RGI header.\n");
        free(data);
        return 1;
    }

    size_t out_size = (size_t)width * (size_t)height * 4u;
    uint8_t* pixels = (uint8_t*)malloc(out_size);
    if (pixels == NULL)
    {
        printf("Failed to allocate output buffer.\n");
        free(data);
        return 1;
    }

    size_t written = rg_rgi_decode(data, data_size, pixels, out_size, &width, &height);
    if (written == 0u)
    {
        printf("Decode failed.\n");
        free(pixels);
        free(data);
        return 1;
    }

    if (output_path != NULL)
    {
        if (!write_file(output_path, pixels, written))
        {
            printf("Failed to write %s\n", output_path);
            free(pixels);
            free(data);
            return 1;
        }
    }

    printf("Decoded %ux%u (%zu bytes)\n", width, height, written);

    free(pixels);
    free(data);
    return 0;
}

// =============================================================================
// Tests
// =============================================================================

static void test_header_validation(void)
{
    uint8_t header[RG_RGI_HEADER_SIZE];
    uint32_t width = 0;
    uint32_t height = 0;

    build_header(header, 4u, 4u, 4u, 0u);
    TEST_ASSERT(rg_rgi_read_header(header, sizeof(header), &width, &height), "valid header");
    TEST_ASSERT(width == 4u && height == 4u, "header size");

    build_header(header, 3u, 5u, 4u, 0u);
    TEST_ASSERT(rg_rgi_read_header(header, sizeof(header), &width, &height), "npot dims valid");
    TEST_ASSERT(width == 3u && height == 5u, "npot dims parsed");

    header[0] = (uint8_t)'x';
    TEST_ASSERT(!rg_rgi_read_header(header, sizeof(header), NULL, NULL), "bad magic");

    build_header(header, 0u, 4u, 4u, 0u);
    TEST_ASSERT(!rg_rgi_read_header(header, sizeof(header), NULL, NULL), "zero width");

    build_header(header, 4u, 4u, 3u, 0u);
    TEST_ASSERT(!rg_rgi_read_header(header, sizeof(header), NULL, NULL), "bad channels");

    build_header(header, 4u, 4u, 4u, 1u);
    TEST_ASSERT(rg_rgi_read_header(header, sizeof(header), NULL, NULL), "extended profile valid");

    build_header(header, 4u, 4u, 4u, 2u);
    TEST_ASSERT(rg_rgi_read_header(header, sizeof(header), NULL, NULL), "palette profile valid");

    build_header(header, 4u, 4u, 4u, 3u);
    TEST_ASSERT(!rg_rgi_read_header(header, sizeof(header), NULL, NULL), "bad profile");

    build_header(header, RG_RGI_MAX_DIM * 2u, 4u, 4u, 0u);
    TEST_ASSERT(!rg_rgi_read_header(header, sizeof(header), NULL, NULL), "width too large");

    build_header(header, 4u, 4u, 4u, 0u);
    TEST_ASSERT(!rg_rgi_read_header(header, RG_RGI_HEADER_SIZE - 1u, NULL, NULL), "short header");

    TEST_PASS();
}

static void test_decode_sample(void)
{
    uint8_t output[sizeof(k_sample_pixels)];
    uint32_t width = 0;
    uint32_t height = 0;

    size_t written = rg_rgi_decode(k_sample_rgi,
                                   sizeof(k_sample_rgi),
                                   output,
                                   sizeof(output),
                                   &width,
                                   &height);
    TEST_ASSERT(written == sizeof(output), "decode size");
    TEST_ASSERT(width == 4u && height == 4u, "decode dims");
    TEST_ASSERT(memcmp(output, k_sample_pixels, sizeof(output)) == 0, "decode pixels");
    TEST_PASS();
}

static void test_decode_trusted_sample(void)
{
    uint8_t output[sizeof(k_sample_pixels)];
    uint32_t width = 0;
    uint32_t height = 0;

    size_t written = rg_rgi_decode_trusted(k_sample_rgi,
                                           sizeof(k_sample_rgi),
                                           output,
                                           sizeof(output),
                                           &width,
                                           &height);
    TEST_ASSERT(written == sizeof(output), "trusted decode size");
    TEST_ASSERT(width == 4u && height == 4u, "trusted decode dims");
    TEST_ASSERT(memcmp(output, k_sample_pixels, sizeof(output)) == 0, "trusted decode pixels");
    TEST_PASS();
}

static void test_encode_roundtrip(void)
{
    uint8_t encoded[RG_RGI_HEADER_SIZE + sizeof(k_sample_pixels) + RG_RGI_END_SIZE + 32u];
    uint8_t decoded[sizeof(k_sample_pixels)];

    size_t written = rg_rgi_encode(k_sample_pixels,
                                   4u,
                                   4u,
                                   encoded,
                                   sizeof(encoded));
    TEST_ASSERT(written > 0u, "encode written");
    TEST_ASSERT(encoded[4] == 4u && encoded[5] == 0u &&
                encoded[6] == 0u && encoded[7] == 0u &&
                encoded[8] == 4u && encoded[9] == 0u &&
                encoded[10] == 0u && encoded[11] == 0u,
                "encode little-endian dimensions");

    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         written,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "encode decode size");
    TEST_ASSERT(memcmp(decoded, k_sample_pixels, sizeof(decoded)) == 0, "encode decode pixels");
    TEST_PASS();
}

static void test_encode_copy_policy(void)
{
    enum { width = 128, height = 128, pixel_count = width * height };
    static uint8_t pixels[pixel_count * 4u];
    static uint8_t encoded[RG_RGI_HEADER_SIZE + pixel_count * 5u + RG_RGI_END_SIZE];
    static uint8_t decoded[pixel_count * 4u];

    for (size_t i = 0u; i < (size_t)pixel_count; i++)
    {
        size_t p = i & 127u;
        pixels[i * 4u + 0u] = (uint8_t)(p * 37u + 11u);
        pixels[i * 4u + 1u] = (uint8_t)(p * 73u + 19u);
        pixels[i * 4u + 2u] = (uint8_t)(p * 109u + 23u);
        pixels[i * 4u + 3u] = (uint8_t)(p * 17u + 31u);
    }

    size_t encoded_size = rg_rgi_encode(pixels,
                                        width,
                                        height,
                                        encoded,
                                        sizeof(encoded));
    TEST_ASSERT(encoded_size > 0u, "copy policy encode written");
    TEST_ASSERT(encoded[13] == RG_RGI_PROFILE_EXTENDED, "copy policy selects extended profile");

    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         encoded_size,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "copy policy decode size");
    TEST_ASSERT(memcmp(decoded, pixels, sizeof(decoded)) == 0, "copy policy decode pixels");
    TEST_PASS();
}

static void test_decode_long_run(void)
{
    uint8_t encoded[RG_RGI_HEADER_SIZE + 5u + 3u + RG_RGI_END_SIZE];
    uint8_t decoded[80u * 4u];
    uint8_t* ptr = encoded;
    build_header(ptr, 10u, 8u, 4u, RG_RGI_PROFILE_EXTENDED);
    ptr += RG_RGI_HEADER_SIZE;
    *ptr++ = RG_RGI_OP_RGBA;
    *ptr++ = 3u;
    *ptr++ = 7u;
    *ptr++ = 11u;
    *ptr++ = 255u;
    *ptr++ = RG_RGI_OP_LONG_RUN;
    *ptr++ = 79u;
    *ptr++ = 0u;
    memset(ptr, 0, RG_RGI_END_SIZE);
    ptr[RG_RGI_END_SIZE - 1u] = 1u;

    size_t encoded_size = (size_t)((ptr + RG_RGI_END_SIZE) - encoded);
    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         encoded_size,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "long run decode size");
    for (size_t i = 0u; i < sizeof(decoded); i += 4u)
    {
        TEST_ASSERT(decoded[i + 0u] == 3u &&
                    decoded[i + 1u] == 7u &&
                    decoded[i + 2u] == 11u &&
                    decoded[i + 3u] == 255u,
                    "long run decode pixel");
    }

    TEST_PASS();
}

static void test_decode_rawspan(void)
{
    uint8_t encoded[RG_RGI_HEADER_SIZE + 2u + 4u * 4u + RG_RGI_END_SIZE];
    uint8_t expected[4u * 4u] =
    {
        1u, 2u, 3u, 4u,
        5u, 6u, 7u, 8u,
        9u, 10u, 11u, 12u,
        13u, 14u, 15u, 16u
    };
    uint8_t decoded[sizeof(expected)];
    uint8_t* ptr = encoded;
    build_header(ptr, 4u, 1u, 4u, RG_RGI_PROFILE_EXTENDED);
    ptr += RG_RGI_HEADER_SIZE;
    *ptr++ = RG_RGI_OP_RAWSPAN;
    *ptr++ = 4u;
    memcpy(ptr, expected, sizeof(expected));
    ptr += sizeof(expected);
    memset(ptr, 0, RG_RGI_END_SIZE);
    ptr[RG_RGI_END_SIZE - 1u] = 1u;

    size_t encoded_size = (size_t)((ptr + RG_RGI_END_SIZE) - encoded);
    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         encoded_size,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "rawspan decode size");
    TEST_ASSERT(memcmp(decoded, expected, sizeof(expected)) == 0, "rawspan decode pixels");

    TEST_PASS();
}

static void test_decode_copy(void)
{
    uint8_t encoded[RG_RGI_HEADER_SIZE + 2u + 4u * 4u + 4u + RG_RGI_END_SIZE];
    uint8_t base[4u * 4u] =
    {
        1u, 2u, 3u, 255u,
        4u, 5u, 6u, 255u,
        7u, 8u, 9u, 255u,
        10u, 11u, 12u, 255u
    };
    uint8_t expected[8u * 4u];
    memcpy(expected, base, sizeof(base));
    memcpy(expected + sizeof(base), base, sizeof(base));

    uint8_t decoded[sizeof(expected)];
    uint8_t* ptr = encoded;
    build_header(ptr, 8u, 1u, 4u, RG_RGI_PROFILE_EXTENDED);
    ptr += RG_RGI_HEADER_SIZE;
    *ptr++ = RG_RGI_OP_RAWSPAN;
    *ptr++ = 4u;
    memcpy(ptr, base, sizeof(base));
    ptr += sizeof(base);
    *ptr++ = RG_RGI_OP_COPY;
    *ptr++ = 0u;
    *ptr++ = 4u;
    *ptr++ = 0u;
    memset(ptr, 0, RG_RGI_END_SIZE);
    ptr[RG_RGI_END_SIZE - 1u] = 1u;

    size_t encoded_size = (size_t)((ptr + RG_RGI_END_SIZE) - encoded);
    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         encoded_size,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "copy decode size");
    TEST_ASSERT(memcmp(decoded, expected, sizeof(expected)) == 0, "copy decode pixels");

    TEST_PASS();
}

static void test_decode_qoi_profile_reserved_run_bytes(void)
{
    uint8_t encoded[RG_RGI_HEADER_SIZE + 5u + 1u + 5u + 1u + RG_RGI_END_SIZE];
    uint8_t decoded[125u * 4u];
    uint8_t* ptr = encoded;
    build_header(ptr, 125u, 1u, 4u, RG_RGI_PROFILE_QOI);
    ptr += RG_RGI_HEADER_SIZE;
    *ptr++ = RG_RGI_OP_RGBA;
    *ptr++ = 1u;
    *ptr++ = 2u;
    *ptr++ = 3u;
    *ptr++ = 255u;
    *ptr++ = 0xFCu;
    *ptr++ = RG_RGI_OP_RGBA;
    *ptr++ = 5u;
    *ptr++ = 6u;
    *ptr++ = 7u;
    *ptr++ = 255u;
    *ptr++ = 0xFDu;
    memset(ptr, 0, RG_RGI_END_SIZE);
    ptr[RG_RGI_END_SIZE - 1u] = 1u;

    size_t encoded_size = (size_t)((ptr + RG_RGI_END_SIZE) - encoded);
    size_t decoded_bytes = rg_rgi_decode(encoded,
                                         encoded_size,
                                         decoded,
                                         sizeof(decoded),
                                         NULL,
                                         NULL);
    TEST_ASSERT(decoded_bytes == sizeof(decoded), "qoi profile reserved run decode size");
    for (size_t i = 0u; i < 62u; i++)
    {
        TEST_ASSERT(decoded[i * 4u + 0u] == 1u &&
                    decoded[i * 4u + 1u] == 2u &&
                    decoded[i * 4u + 2u] == 3u &&
                    decoded[i * 4u + 3u] == 255u,
                    "qoi profile 0xfc run pixel");
    }
    for (size_t i = 62u; i < 125u; i++)
    {
        TEST_ASSERT(decoded[i * 4u + 0u] == 5u &&
                    decoded[i * 4u + 1u] == 6u &&
                    decoded[i * 4u + 2u] == 7u &&
                    decoded[i * 4u + 3u] == 255u,
                    "qoi profile 0xfd run pixel");
    }

    TEST_PASS();
}

static void test_decode_truncated(void)
{
    uint8_t output[sizeof(k_sample_pixels)];
    size_t truncated_size = sizeof(k_sample_rgi) - (RG_RGI_END_SIZE + 2u);
    size_t written = rg_rgi_decode(k_sample_rgi,
                                   truncated_size,
                                   output,
                                   sizeof(output),
                                   NULL,
                                   NULL);
    TEST_ASSERT(written == 0u, "truncated decode");
    TEST_PASS();
}

static void test_wire_contract(void)
{
    uint8_t bad_magic[sizeof(k_sample_rgi)];
    uint8_t bad_marker[sizeof(k_sample_rgi)];
    uint8_t trailing[sizeof(k_sample_rgi) + 1u];
    uint8_t unaligned[sizeof(k_sample_pixels) + 1u];

    memcpy(bad_magic, k_sample_rgi, sizeof(bad_magic));
    bad_magic[0] = (uint8_t)'x';
    TEST_ASSERT(!rg_rgi_read_header(bad_magic, sizeof(bad_magic), NULL, NULL), "bad magic rejected");
    TEST_ASSERT(rg_rgi_decode(bad_magic, sizeof(bad_magic), unaligned + 1u,
                              sizeof(k_sample_pixels), NULL, NULL) == 0u,
                "checked decode rejects bad magic");
    TEST_ASSERT(rg_rgi_decode_trusted(bad_magic, sizeof(bad_magic), unaligned + 1u,
                                      sizeof(k_sample_pixels), NULL, NULL) == 0u,
                "trusted decode rejects bad magic");

    memcpy(bad_marker, k_sample_rgi, sizeof(bad_marker));
    bad_marker[sizeof(bad_marker) - 1u] = 0u;
    TEST_ASSERT(rg_rgi_decode(bad_marker, sizeof(bad_marker), unaligned + 1u,
                              sizeof(k_sample_pixels), NULL, NULL) == 0u,
                "bad end marker rejected");

    memcpy(trailing, k_sample_rgi, sizeof(k_sample_rgi));
    trailing[sizeof(trailing) - 1u] = 0u;
    TEST_ASSERT(rg_rgi_decode(trailing, sizeof(trailing), unaligned + 1u,
                              sizeof(k_sample_pixels), NULL, NULL) == 0u,
                "trailing byte rejected");

    TEST_ASSERT(rg_rgi_decode(k_sample_rgi, sizeof(k_sample_rgi), unaligned + 1u,
                              sizeof(k_sample_pixels), NULL, NULL) == sizeof(k_sample_pixels),
                "unaligned destination supported");
    TEST_ASSERT(memcmp(unaligned + 1u, k_sample_pixels, sizeof(k_sample_pixels)) == 0,
                "unaligned destination pixels");
    TEST_PASS();
}

static void test_encode_workspace(void)
{
    size_t bound = rg_rgi_encode_bound(4u, 4u);
    size_t workspace_size = rg_rgi_encode_workspace_size();
    TEST_ASSERT(workspace_size == 397312u, "encode workspace size remains compatible");
    uint8_t* encoded_a = (uint8_t*)malloc(bound);
    uint8_t* encoded_b = (uint8_t*)malloc(bound);
    void* workspace = malloc(workspace_size + 1u);
    TEST_ASSERT(encoded_a != NULL && encoded_b != NULL && workspace != NULL, "workspace allocations");

    size_t size_a = rg_rgi_encode_with_workspace(k_sample_pixels, 4u, 4u,
                                                 encoded_a, bound,
                                                 workspace, workspace_size);
    TEST_ASSERT(size_a > 0u, "workspace encode");
    TEST_ASSERT(rg_rgi_encode_with_workspace(k_sample_pixels, 4u, 4u,
                                             encoded_b, bound,
                                             workspace, workspace_size - 1u) == 0u,
                "undersized workspace rejected");
    TEST_ASSERT(rg_rgi_encode_with_workspace(k_sample_pixels, 4u, 4u,
                                             encoded_b, bound,
                                             (uint8_t*)workspace + 1u, workspace_size) == 0u,
                "unaligned workspace rejected");

    size_t size_b = rg_rgi_encode_with_workspace(k_sample_pixels, 4u, 4u,
                                                 encoded_b, bound,
                                                 workspace, workspace_size);
    TEST_ASSERT(size_a == size_b && memcmp(encoded_a, encoded_b, size_a) == 0,
                "workspace reuse deterministic");

    free(workspace);
    free(encoded_b);
    free(encoded_a);
    TEST_PASS();
}

// =============================================================================
// Runner
// =============================================================================

static void test_copy_match_boundaries(void)
{
    uint8_t storage[2201];
    uint8_t* pixels = storage + 1u;
    const size_t lengths[] = {4u, 5u, 7u, 8u, 15u, 16u, 258u, 259u};
    for (size_t test = 0; test < sizeof(lengths) / sizeof(lengths[0]); ++test)
    {
        size_t length = lengths[test];
        for (size_t i = 0; i < 550u; ++i)
        {
            pixels[i * 4u] = (uint8_t)i;
            pixels[i * 4u + 1u] = (uint8_t)(i >> 8u);
            pixels[i * 4u + 2u] = 71u;
            pixels[i * 4u + 3u] = 255u;
        }
        memcpy(pixels + 280u * 4u, pixels, length * 4u);
        size_t best = 0;
        uint16_t offset = 0;
        rg_rgi_consider_copy_candidate(pixels, 550u, 280u, 0u, &best, &offset);
        TEST_ASSERT(best == length && offset == 280u, "unaligned match length and tail");
        offset = 123u;
        rg_rgi_consider_copy_candidate(pixels, 550u, 280u, 0u, &best, &offset);
        TEST_ASSERT(best == length && offset == 123u, "equal length preserves earlier candidate");
        best = length - 1u;
        rg_rgi_consider_copy_candidate(pixels, 280u + length, 280u, 0u, &best, &offset);
        TEST_ASSERT(best == length && offset == 280u, "match at input boundary");
    }
    size_t best = 0;
    uint16_t offset = 0;
    rg_rgi_consider_copy_candidate(pixels, 550u, 280u, 278u, &best, &offset);
    TEST_ASSERT(best == 0u, "overlapping short candidate rejected");
    const size_t distant_count = 65536u + RG_RGI_COPY_MAX;
    uint8_t* distant = (uint8_t*)calloc(distant_count, 4u);
    TEST_ASSERT(distant != NULL, "distant copy allocation");
    rg_rgi_consider_copy_candidate(distant, distant_count, 65535u, 0u, &best, &offset);
    TEST_ASSERT(best == RG_RGI_COPY_MAX && offset == 65535u, "maximum legal copy offset");
    best = 0u;
    offset = 0u;
    rg_rgi_consider_copy_candidate(distant, distant_count, 65536u, 0u, &best, &offset);
    TEST_ASSERT(best == 0u && offset == 0u, "copy offset beyond wire limit rejected");
    free(distant);
    TEST_PASS();
}

static void test_profile_estimate_cutoff(void)
{
    uint8_t pixels[64u * 17u * 4u];
    uint32_t state = 0x13579bdfu;
    for (size_t i = 0; i < sizeof(pixels); ++i)
    {
        state ^= state << 13u; state ^= state >> 17u; state ^= state << 5u;
        pixels[i] = (uint8_t)state;
    }
    RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
    TEST_ASSERT(workspace != NULL, "estimate workspace allocated");
    memset(workspace, 0xa5, sizeof(*workspace));
    size_t full = rg_rgi_estimate_rawspan_payload_size_min_copy(pixels, 64u * 17u, 64u,
                                                               1, RG_RGI_COPY_MIN, &workspace->table);
    const size_t limits[] = {0u, 1u, 2u, 64u, 1024u, SIZE_MAX};
    for (size_t i = 0; i < sizeof(limits) / sizeof(limits[0]); ++i)
    {
        size_t result = rg_rgi__estimate_rawspan_payload_size_bounded(pixels, 64u * 17u, 64u,
                                                                    1, RG_RGI_COPY_MIN, &workspace->table, limits[i]);
        TEST_ASSERT(full < limits[i] ? result == full : result >= limits[i], "bounded estimate preserves selection");
    }
    size_t exact = rg_rgi__estimate_rawspan_payload_size_bounded(pixels, 64u * 17u, 64u,
                                                                1, RG_RGI_COPY_MIN, &workspace->table, full);
    TEST_ASSERT(exact == full, "exact estimate threshold");
    size_t bound = rg_rgi_encode_bound(64u, 17u);
    uint8_t* first = (uint8_t*)malloc(bound);
    uint8_t* second = (uint8_t*)malloc(bound);
    TEST_ASSERT(first && second, "encode output allocated");
    size_t a = rg_rgi_encode_with_workspace(pixels, 64u, 17u, first, bound, workspace, sizeof(*workspace));
    memset(workspace, 0x5a, sizeof(*workspace));
    size_t b = rg_rgi_encode_with_workspace(pixels, 64u, 17u, second, bound, workspace, sizeof(*workspace));
    TEST_ASSERT(a && a == b && memcmp(first, second, a) == 0, "dirty workspace does not affect encoding");
    free(second); free(first); free(workspace);
    TEST_PASS();
}

static void test_profile_emit_cutoff(void)
{
    static const uint8_t literal[] = {0xfcu, 1u, 17u, 23u, 47u, 91u};
    static const uint8_t short_run[] = {0xc3u};
    static const uint8_t long_run[] = {0xfdu, 186u, 0u};
    static const uint8_t partial[] = {0xfdu, 187u, 0u, 0xfcu, 1u, 17u, 23u, 47u, 91u};
    static const uint8_t copy[] = {
        0xfcu, 4u, 1u, 2u, 3u, 255u, 5u, 6u, 7u, 255u,
        9u, 10u, 11u, 255u, 13u, 14u, 15u, 255u, 0xfbu, 0u, 4u, 0u
    };
    static const uint8_t transparent[] = {0u, 0xc0u};
    const uint8_t* expected[] = {literal, short_run, long_run, partial, copy, transparent};
    const size_t sizes[] = {sizeof(literal), sizeof(short_run), sizeof(long_run),
                            sizeof(partial), sizeof(copy), sizeof(transparent)};
    const uint32_t counts[] = {1u, 4u, 186u, 188u, 8u, 2u};
    uint8_t pixels[188u * 4u];
    uint8_t guarded[66];
    RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
    TEST_ASSERT(workspace != NULL, "bounded emitter workspace allocated");
    memset(workspace, 0xa5, sizeof(*workspace));
    for (size_t test = 0; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        memset(pixels, 0, sizeof(pixels));
        for (size_t i = 0; i < counts[test]; ++i) pixels[i * 4u + 3u] = 255u;
        if (test == 0u || test == 3u)
            memcpy(pixels + (counts[test] - 1u) * 4u, literal + 2u, 4u);
        else if (test == 4u)
        {
            memcpy(pixels, copy + 2u, 16u);
            memcpy(pixels + 16u, copy + 2u, 16u);
        }
        else if (test == 5u) memset(pixels, 0, 8u);
        uint32_t width = test == 4u ? 4u : counts[test];
        for (size_t limit = 0; limit <= sizes[test] + 1u; ++limit)
        {
            memset(guarded, 0xa5, sizeof(guarded));
            size_t result = rg_rgi__encode_rawspan_payload_bounded(pixels, counts[test], width,
                guarded + 1u, 1, RG_RGI_COPY_MIN, &workspace->table, limit);
            TEST_ASSERT(result == (sizes[test] < limit ? sizes[test] : limit), "strict emitter cutoff result");
            if (sizes[test] < limit)
                TEST_ASSERT(memcmp(guarded + 1u, expected[test], result) == 0, "bounded opcode bytes exact");
            TEST_ASSERT(guarded[0] == 0xa5u, "bounded emitter leading guard");
            for (size_t i = limit + 1u; i < sizeof(guarded); ++i)
                TEST_ASSERT(guarded[i] == 0xa5u, "bounded emitter never writes at or beyond limit");
            if (test == 3u && limit == 4u)
                TEST_ASSERT(memcmp(guarded + 1u, partial, 3u) == 0 && guarded[4] == 0xa5u,
                            "rejected literal retains only the completed run prefix");
        }
        size_t result = rg_rgi_encode_rawspan_payload_min_copy(pixels, counts[test], width,
            guarded + 1u, 1, RG_RGI_COPY_MIN, &workspace->table);
        TEST_ASSERT(result == sizes[test] && memcmp(guarded + 1u, expected[test], result) == 0,
                    "unbounded emitter retains opcode bytes");
    }
    free(workspace);
    TEST_PASS();
}

static void test_profile_emit_fallback(void)
{
    const uint32_t widths[] = {1u, 186u, 187u, 188u, 17u, 260u, 255u, 256u, 64u};
    const uint32_t heights[] = {1u, 1u, 1u, 1u, 19u, 2u, 257u, 256u, 1u};
    const size_t max_pixels = 65536u;
    uint8_t* pixels = (uint8_t*)malloc(max_pixels * 4u);
    uint8_t* profile0 = (uint8_t*)malloc(max_pixels * 5u);
    uint8_t* profile1 = (uint8_t*)malloc(max_pixels * 6u);
    uint8_t* guarded = (uint8_t*)malloc(rg_rgi_encode_bound(256u, 256u) + 2u);
    RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
    TEST_ASSERT(pixels && profile0 && profile1 && guarded && workspace, "fallback allocations");
    memset(workspace, 0x5a, sizeof(*workspace));
    for (size_t test = 0; test < sizeof(widths) / sizeof(widths[0]); ++test)
    {
        size_t count = (size_t)widths[test] * heights[test];
        memset(pixels, 0, count * 4u);
        for (size_t i = 0; i < count; ++i) pixels[i * 4u + 3u] = 255u;
        if (test == 0u || test == 3u || test == 8u)
        {
            uint8_t* last = pixels + (count - 1u) * 4u;
            last[0] = 17u; last[1] = 23u; last[2] = 47u; last[3] = 91u;
        }
        else if (test == 4u)
        {
            uint32_t state = UINT32_C(0x13579bdf);
            for (size_t i = 0; i < count * 4u; ++i)
            {
                state ^= state << 13u; state ^= state >> 17u; state ^= state << 5u;
                pixels[i] = (uint8_t)state;
            }
        }
        else if (test == 5u)
        {
            for (size_t i = 0; i < count; ++i)
            {
                size_t position = i % 260u;
                pixels[i * 4u] = (uint8_t)position;
                pixels[i * 4u + 1u] = (uint8_t)(position >> 8u);
                pixels[i * 4u + 2u] = (uint8_t)(position * 13u);
                pixels[i * 4u + 3u] = (uint8_t)(position * 7u);
            }
        }
        // Materialize both full profiles independently of the automatic cutoff.
        size_t size0 = rg_rgi_encode_qoi_payload(pixels, count, profile0);
        size_t size1 = rg_rgi_encode_rawspan_payload_min_copy(pixels, count, widths[test], profile1,
                                                             1, RG_RGI_COPY_MIN, &workspace->table);
        if (test == 1u || test == 3u)
            TEST_ASSERT(size0 == size1, "fixture reaches exact profile-size tie");
        if (test == 8u)
            TEST_ASSERT(size0 == 7u && size1 == 9u, "fixture loses after a three-byte profile-1 prefix");
        size_t expected_size = size1 < size0 ? size1 : size0;
        const uint8_t* expected = size1 < size0 ? profile1 : profile0;
        size_t bound = rg_rgi_encode_bound(widths[test], heights[test]);
        memset(guarded, 0xa5, bound + 2u);
        uint8_t profile = 255u;
        size_t result = rg_rgi__encode_legacy_payload(pixels, count, widths[test], guarded + 1u,
                                                      &workspace->table, &profile);
        TEST_ASSERT(result == expected_size, "legacy selection preserves exact minimum size");
        TEST_ASSERT(profile == (size1 < size0 ? 1u : 0u), "profile 0 wins ties");
        TEST_ASSERT(memcmp(guarded + 1u, expected, expected_size) == 0,
                    "legacy payload matches full-profile oracle after overwrite or fallback");
        TEST_ASSERT(guarded[0] == 0xa5u && guarded[bound + 1u] == 0xa5u, "automatic encode keeps output guards");
    }
    free(workspace); free(guarded); free(profile1); free(profile0); free(pixels);
    TEST_PASS();
}

static void test_palette_encode_selection(void)
{
    static const uint32_t requested_colors[] = {1u, 2u, 3u, 4u, 5u, 16u, 17u, 256u, 257u};
    static const uint32_t counts[] = {1u, 24u, 1024u};
    uint8_t pixels[1024u * 4u], decoded[sizeof(pixels)];
    size_t capacity = rg_rgi_encode_bound(32u, 32u);
    uint8_t* legacy = (uint8_t*)malloc(capacity);
    uint8_t* encoded = (uint8_t*)malloc(capacity + 2u);
    uint8_t* repeated = (uint8_t*)malloc(capacity);
    RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
#if !defined(RG_RGI_NO_PALETTE_ENCODE)
    uint8_t* palette = (uint8_t*)malloc(capacity);
    unsigned selected = 0u;
    TEST_ASSERT(palette != NULL, "palette reference allocation");
#endif
    TEST_ASSERT(legacy && encoded && repeated && workspace, "palette selection allocations");
    for (size_t c = 0u; c < sizeof(counts) / sizeof(counts[0]); ++c)
    {
        uint32_t count = counts[c];
        uint32_t width = count == 1024u ? 32u : count;
        uint32_t height = count / width;
        size_t bound = rg_rgi_encode_bound(width, height);
        for (size_t test = 0u; test < sizeof(requested_colors) / sizeof(requested_colors[0]); ++test)
        {
            uint32_t colors = requested_colors[test] < count ? requested_colors[test] : count;
            uint32_t state = UINT32_C(0x12345678);
            for (uint32_t i = 0u; i < count; ++i)
            {
                state ^= state << 13u; state ^= state >> 17u; state ^= state << 5u;
                uint32_t index = i < colors ? i : state % colors;
                pixels[(size_t)i * 4u] = (uint8_t)index;
                pixels[(size_t)i * 4u + 1u] = (uint8_t)(index >> 8u);
                pixels[(size_t)i * 4u + 2u] = (uint8_t)(index * 113u);
                pixels[(size_t)i * 4u + 3u] = (uint8_t)(index % 3u ? 255u : 0u);
            }
            uint8_t expected_profile = 255u;
            size_t expected_size = rg_rgi__encode_legacy_payload(pixels, count, width, legacy,
                                                                 &workspace->table, &expected_profile);
            const uint8_t* expected = legacy;
#if !defined(RG_RGI_NO_PALETTE_ENCODE)
            int modified = -1;
            memset(palette, 0xa5, capacity);
            size_t palette_size = rg_rgi__encode_palette_payload_bounded(pixels, count, width, palette,
                                                                         &workspace->table, capacity, &modified);
            if (colors > 256u)
            {
                TEST_ASSERT(palette_size == capacity && modified == 0,
                            "257 colors rejects palette before writing");
                for (size_t i = 0u; i < capacity; ++i)
                    TEST_ASSERT(palette[i] == 0xa5u, "ineligible palette leaves destination intact");
            }
            else
            {
                TEST_ASSERT(palette_size < capacity && modified == 1, "eligible palette payload emitted");
                TEST_ASSERT(palette[0] == (uint8_t)colors && palette[1] == (uint8_t)(colors >> 8u),
                            "palette exact RGBA color count");
                build_header(repeated, width, height, 4u, RG_RGI_PROFILE_PALETTE);
                memcpy(repeated + RG_RGI_HEADER_SIZE, palette, palette_size);
                memset(repeated + RG_RGI_HEADER_SIZE + palette_size, 0, RG_RGI_END_SIZE);
                repeated[RG_RGI_HEADER_SIZE + palette_size + RG_RGI_END_SIZE - 1u] = 1u;
                TEST_ASSERT(rg_rgi_decode(repeated, RG_RGI_HEADER_SIZE + palette_size + RG_RGI_END_SIZE,
                                           decoded, (size_t)count * 4u, NULL, NULL) == (size_t)count * 4u &&
                            memcmp(pixels, decoded, (size_t)count * 4u) == 0,
                            "forced palette roundtrip preserves hidden RGB and alpha");
                if (palette_size < expected_size)
                {
                    expected_size = palette_size;
                    expected_profile = RG_RGI_PROFILE_PALETTE;
                    expected = palette;
                    ++selected;
                }
            }
#endif
            memset(encoded, 0xa5, capacity + 2u);
            memset(workspace, 0x5a, sizeof(*workspace));
            size_t written = rg_rgi_encode_with_workspace(pixels, width, height, encoded + 1u, bound,
                                                          workspace, sizeof(*workspace));
            TEST_ASSERT(written == RG_RGI_HEADER_SIZE + expected_size + RG_RGI_END_SIZE &&
                        encoded[14] == expected_profile, "automatic encoder selects strict minimum profile");
            TEST_ASSERT(memcmp(encoded + 1u + RG_RGI_HEADER_SIZE, expected, expected_size) == 0,
                        "automatic payload exact after palette overwrite or fallback");
            TEST_ASSERT(rg_rgi__has_end_marker(encoded + 1u + written - RG_RGI_END_SIZE), "palette selection footer");
            TEST_ASSERT(encoded[0] == 0xa5u && encoded[bound + 1u] == 0xa5u, "palette selection output guards");
            TEST_ASSERT(rg_rgi_decode_trusted(encoded + 1u, written, decoded, (size_t)count * 4u, NULL, NULL) ==
                        (size_t)count * 4u && memcmp(pixels, decoded, (size_t)count * 4u) == 0,
                        "automatic trusted roundtrip exact");
            memset(workspace, 0xa5, sizeof(*workspace));
            size_t again = rg_rgi_encode_with_workspace(pixels, width, height, repeated, bound,
                                                        workspace, sizeof(*workspace));
            TEST_ASSERT(again == written && memcmp(repeated, encoded + 1u, written) == 0,
                        "palette selection deterministic with dirty reused workspace");
        }
    }
#if !defined(RG_RGI_NO_PALETTE_ENCODE)
    TEST_ASSERT(selected > 0u, "palette selected for irregular low-color input");
    free(palette);
#endif
    free(workspace); free(repeated); free(encoded); free(legacy);
    TEST_PASS();
}

#if !defined(RG_RGI_NO_PALETTE_ENCODE)
static void test_palette_emit_cutoff(void)
{
    static const uint32_t counts[] = {24u, 129u, 21u};
    static const size_t metadata[] = {10u, 6u, 70u};
    uint8_t pixels[129u * 4u];
    uint8_t reference[128], guarded[130];
    RgRgiEncodeWorkspace* workspace = (RgRgiEncodeWorkspace*)malloc(sizeof(*workspace));
    TEST_ASSERT(workspace != NULL, "palette cutoff workspace allocation");
    for (size_t test = 0u; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        for (size_t i = 0u; i < counts[test]; ++i)
        {
            uint8_t index = test == 0u ? (uint8_t)(i == 0u ? 0u : (i < 18u ? 1u : i % 2u)) :
                            test == 1u ? 0u : (uint8_t)(i % 17u);
            pixels[i * 4u] = index;
            pixels[i * 4u + 1u] = pixels[i * 4u + 2u] = 0u;
            pixels[i * 4u + 3u] = (uint8_t)(index ? 255u : 0u);
        }
        int modified = -1;
        size_t full = rg_rgi__encode_palette_payload_bounded(pixels, counts[test], counts[test], reference,
                                                            &workspace->table, sizeof(reference), &modified);
        TEST_ASSERT(modified == 1 && full < sizeof(reference), "palette cutoff reference emitted");
        if (test == 0u)
            TEST_ASSERT(full <= 14u, "packed repeat does not waste literal padding");
        else if (test == 1u)
            TEST_ASSERT(full == 9u && reference[6] == 0x81u, "palette cutoff covers long RUN reservation");
        else
            TEST_ASSERT(reference[full - 3u] == 0xc0u, "palette cutoff covers short COPY reservation");
        for (size_t limit = 0u; limit <= full + 1u; ++limit)
        {
            memset(guarded, 0xa5, sizeof(guarded));
            modified = -1;
            size_t result = rg_rgi__encode_palette_payload_bounded(pixels, counts[test], counts[test], guarded + 1u,
                                                                   &workspace->table, limit, &modified);
            TEST_ASSERT(result == (full < limit ? full : limit), "palette emitter strict cutoff and tie");
            TEST_ASSERT(modified == (limit > metadata[test] ? 1 : 0), "palette modification flag tracks metadata writes");
            if (full < limit)
                TEST_ASSERT(memcmp(guarded + 1u, reference, full) == 0, "palette cutoff payload exact");
            TEST_ASSERT(guarded[0] == 0xa5u, "palette cutoff leading guard");
            for (size_t i = limit + 1u; i < sizeof(guarded); ++i)
                TEST_ASSERT(guarded[i] == 0xa5u, "palette cutoff never writes at or beyond limit");
        }
    }
    free(workspace);
    TEST_PASS();
}
#endif

#include "test_rgi_palette.h"

static void test_all_truncations(void)
{
    uint8_t guarded[sizeof(k_sample_pixels) + 2u];
    for (size_t length = 0; length < sizeof(k_sample_rgi); ++length)
    {
        memset(guarded, 0xa5, sizeof(guarded));
        TEST_ASSERT(rg_rgi_decode(k_sample_rgi, length, guarded + 1u,
                                  sizeof(k_sample_pixels), NULL, NULL) == 0u, "every truncated prefix rejected");
        TEST_ASSERT(guarded[0] == 0xa5u && guarded[sizeof(guarded) - 1u] == 0xa5u, "truncated decode keeps output guards");
    }
    TEST_PASS();
}

static void test_run_store_boundaries(void)
{
    const size_t counts[] = {1u, 3u, 4u, 7u, 15u, 16u, 17u, 59u, 60u, 62u, 255u, 259u, 65535u};
    uint8_t* guarded = (uint8_t*)malloc(65535u * 4u + 2u);
    TEST_ASSERT(guarded != NULL, "run output allocated");
    for (size_t test = 0; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        size_t count = counts[test];
        memset(guarded, 0xa5, count * 4u + 2u);
        rg_rgi_store_run_rgba_u32(guarded + 1u, count, UINT32_C(0x7f332211));
        for (size_t i = 0; i < count; ++i)
        {
            const uint8_t* pixel = guarded + 1u + i * 4u;
            TEST_ASSERT(pixel[0] == 0x11u && pixel[1] == 0x22u && pixel[2] == 0x33u && pixel[3] == 0x7fu,
                        "unaligned run pixels exact");
        }
        TEST_ASSERT(guarded[0] == 0xa5u && guarded[count * 4u + 1u] == 0xa5u, "run stores keep output guards");
    }
    free(guarded);
    TEST_PASS();
}

static void test_legacy_fixture(void)
{
    uint8_t* data = NULL;
    size_t size = 0;
    TEST_ASSERT(read_file("tests/fixtures/legacy/baseline/odd-small.rgi", &data, &size), "load pre-optimization fixture");
    uint8_t expected[17u * 19u * 4u];
    uint8_t decoded[sizeof(expected)];
    for (uint32_t y = 0; y < 19u; ++y)
    {
        for (uint32_t x = 0; x < 17u; ++x)
        {
            size_t offset = ((size_t)y * 17u + x) * 4u;
            expected[offset] = (uint8_t)x;
            expected[offset + 1u] = (uint8_t)y;
            expected[offset + 2u] = (uint8_t)(x + y);
            expected[offset + 3u] = (uint8_t)(x * 13u + y * 7u);
        }
    }
    TEST_ASSERT(rg_rgi_decode(data, size, decoded, sizeof(decoded), NULL, NULL) == sizeof(decoded), "legacy checked decode");
    TEST_ASSERT(memcmp(expected, decoded, sizeof(decoded)) == 0, "legacy checked pixels exact");
    TEST_ASSERT(rg_rgi_decode_trusted(data, size, decoded, sizeof(decoded), NULL, NULL) == sizeof(decoded), "legacy trusted decode");
    TEST_ASSERT(memcmp(expected, decoded, sizeof(decoded)) == 0, "legacy trusted pixels exact");
    size_t bound = rg_rgi_encode_bound(17u, 19u);
    uint8_t* encoded = (uint8_t*)malloc(bound);
    TEST_ASSERT(encoded != NULL, "legacy reencode allocated");
    size_t written = rg_rgi_encode(expected, 17u, 19u, encoded, bound);
    TEST_ASSERT(written == size && memcmp(encoded, data, size) == 0, "legacy encoder bytes unchanged");
    free(encoded); free(data);
    TEST_PASS();
}

static void run_all_tests(void)
{
    test_header_validation();
    test_decode_sample();
    test_decode_trusted_sample();
    test_encode_roundtrip();
    test_encode_copy_policy();
    test_decode_long_run();
    test_decode_rawspan();
    test_decode_copy();
    test_decode_qoi_profile_reserved_run_bytes();
    test_decode_truncated();
    test_wire_contract();
    test_encode_workspace();
    test_copy_match_boundaries();
    test_profile_estimate_cutoff();
    test_profile_emit_cutoff();
    test_profile_emit_fallback();
    test_palette_encode_selection();
#if !defined(RG_RGI_NO_PALETTE_ENCODE)
    test_palette_emit_cutoff();
#endif
    test_palette_decode();
    test_all_truncations();
    test_run_store_boundaries();
    test_legacy_fixture();
}

int main(int argc, char** argv)
{
    if (argc >= 2 && strcmp(argv[1], "decode") == 0)
    {
        const char* input_path = NULL;
        const char* output_path = NULL;
        if (argc >= 3)
        {
            input_path = argv[2];
        }
        if (argc >= 4)
        {
            output_path = argv[3];
        }
        if (input_path == NULL)
        {
            printf("Usage: test_rgi.exe decode <input.rgi> <output.rgba>\n");
            return 1;
        }
        if (output_path == NULL)
        {
            printf("No output path provided; decoding only.\n");
        }
        return run_decode(input_path, output_path);
    }

    if (argc >= 2 && strcmp(argv[1], "test") != 0)
    {
        printf("Usage: test_rgi.exe [test] | decode <input.rgi> <output.rgba>\n");
        return 1;
    }

    printf("rg_rgi tests\n");
    printf("============\n");

    run_all_tests();

    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed ? 1 : 0;
}
