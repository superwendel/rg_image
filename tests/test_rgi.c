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
