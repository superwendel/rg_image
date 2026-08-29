#include "rg_rgi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t load_u32_be(const uint8_t* bytes)
{
    return ((uint32_t)bytes[0] << 24u) | ((uint32_t)bytes[1] << 16u) |
           ((uint32_t)bytes[2] << 8u) | (uint32_t)bytes[3];
}

static void store_u32_be(uint8_t* bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24u);
    bytes[1] = (uint8_t)(value >> 16u);
    bytes[2] = (uint8_t)(value >> 8u);
    bytes[3] = (uint8_t)value;
}

static uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0u; i < size; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            uint32_t mask = (uint32_t)-(int)(crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
    }
    return ~crc;
}

static int read_file(const char* path, uint8_t** out_data, size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) return 0;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return 0; }
    long length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 0; }
    uint8_t* data = (uint8_t*)malloc((size_t)length);
    if (data == NULL) { fclose(file); return 0; }
    int ok = fread(data, 1u, (size_t)length, file) == (size_t)length;
    fclose(file);
    if (!ok) { free(data); return 0; }
    *out_data = data;
    *out_size = (size_t)length;
    return 1;
}

static int write_file(const char* path, const uint8_t* data, size_t size)
{
    FILE* file = fopen(path, "wb");
    if (file == NULL) return 0;
    int ok = fwrite(data, 1u, size, file) == size && fclose(file) == 0;
    return ok;
}

static int generate_rgi_fixture(void)
{
    enum { WIDTH = 32, HEIGHT = 32 };
    uint8_t pixels[WIDTH * HEIGHT * 4];
    for (uint32_t y = 0u; y < HEIGHT; y++)
    {
        for (uint32_t x = 0u; x < WIDTH; x++)
        {
            size_t i = ((size_t)y * WIDTH + x) * 4u;
            pixels[i + 0u] = (uint8_t)((x & 7u) * 31u);
            pixels[i + 1u] = (uint8_t)((y & 7u) * 31u);
            pixels[i + 2u] = (uint8_t)(((x + y) & 7u) * 31u);
            pixels[i + 3u] = 255u;
        }
    }
    size_t bound = rg_rgi_encode_bound(WIDTH, HEIGHT);
    uint8_t* encoded = (uint8_t*)malloc(bound);
    if (encoded == NULL) return 0;
    size_t size = rg_rgi_encode(pixels, WIDTH, HEIGHT, encoded, bound);
    int ok = size > 0u && write_file("build/tool_current.rgi", encoded, size);
    free(encoded);
    return ok;
}

static int corrupt_png(const char* input, const char* crc_path, const char* adler_path)
{
    uint8_t* data = NULL;
    size_t size = 0u;
    if (!read_file(input, &data, &size) || size < 33u) return 0;
    uint8_t* adler = (uint8_t*)malloc(size);
    if (adler == NULL) { free(data); return 0; }
    memcpy(adler, data, size);

    data[29] ^= 1u;
    int ok = write_file(crc_path, data, size);

    size_t offset = 8u;
    int changed = 0;
    while (offset + 12u <= size)
    {
        size_t length = (size_t)load_u32_be(adler + offset);
        if (length > size - offset - 12u) break;
        uint8_t* type = adler + offset + 4u;
        uint8_t* chunk = adler + offset + 8u;
        if (memcmp(type, "IDAT", 4u) == 0 && length >= 4u)
        {
            chunk[length - 1u] ^= 1u;
            store_u32_be(chunk + length, crc32(type, length + 4u));
            changed = 1;
            break;
        }
        offset += length + 12u;
    }
    ok = ok && changed && write_file(adler_path, adler, size);
    free(adler);
    free(data);
    return ok;
}

int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "generate") == 0)
    {
        return generate_rgi_fixture() ? 0 : 1;
    }
    if (argc == 5 && strcmp(argv[1], "corrupt-png") == 0)
    {
        return corrupt_png(argv[2], argv[3], argv[4]) ? 0 : 1;
    }
    return 1;
}
