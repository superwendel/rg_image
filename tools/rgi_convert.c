// rgi_convert - Convert images to RGI, QOI, or PNG
//
// Usage:
//   rgi_convert.exe <input.png> <output.rgi>
//   rgi_convert.exe <input.png> <output.qoi>
//   rgi_convert.exe <input.rgi> <output.png>
//   rgi_convert.exe --dir <input_dir> <output_dir> [--rgi|--qoi|--png] [--recursive] [--overwrite]
//
// Notes:
//   - RGI validates dimensions against RG_RGI_MAX_DIM.
//   - QOI output allows any dimensions (3/4 channels based on alpha).
//   - PNG decode uses tool-local stb_image after CRC and Adler-32 validation.

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
    #define _POSIX_C_SOURCE 200809L
#endif
#if !defined(_WIN32) && !defined(_XOPEN_SOURCE)
    #define _XOPEN_SOURCE 700
#endif

#include "../src/rg_rgi.h"

#define RG_RGI_CONVERT_USE_STB 1
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"
#include "../third_party/miniz/miniz_tinfl.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#if RG_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <direct.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

static void print_usage(void)
{
    printf("Usage:\n");
    printf("  rgi_convert.exe <input.png> <output.rgi>\n");
    printf("  rgi_convert.exe <input.png> <output.qoi>\n");
    printf("  rgi_convert.exe <input.rgi> <output.png>\n");
    printf("  rgi_convert.exe [--trusted-png] <input> <output>\n");
    printf("  rgi_convert.exe --dir <input_dir> <output_dir> [--rgi|--qoi|--png] [--recursive] [--overwrite] [--trusted-png]\n");
    printf("  Input/output formats: PNG, RGI, and QOI\n");
}

static int load_file(const char* path, uint8_t** out_data, size_t* out_size)
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
    if (size <= 0)
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

static char* make_temporary_output_path(const char* path)
{
    static unsigned int counter = 0u;
    unsigned long process_id = 0u;
    #if RG_PLATFORM_WINDOWS
        process_id = (unsigned long)GetCurrentProcessId();
    #else
        process_id = (unsigned long)getpid();
    #endif
    size_t length = strlen(path);
    char* temporary = (char*)malloc(length + 48u);
    if (temporary == NULL)
    {
        return NULL;
    }
    counter++;
    snprintf(temporary, length + 48u, "%s.tmp.%lu.%u", path, process_id, counter);
    return temporary;
}

static int commit_temporary_output(const char* temporary, const char* path)
{
    #if RG_PLATFORM_WINDOWS
        return MoveFileExA(temporary,
                           path,
                           MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    #else
        return rename(temporary, path) == 0;
    #endif
}

static int write_file(const char* path, const void* data, size_t size)
{
    char* temporary = make_temporary_output_path(path);
    if (temporary == NULL)
    {
        return 0;
    }
    FILE* file = fopen(temporary, "wb");
    if (file == NULL)
    {
        free(temporary);
        return 0;
    }

    size_t written = fwrite(data, 1, size, file);
    int closed = fclose(file) == 0;
    int ok = written == size && closed && commit_temporary_output(temporary, path);
    if (!ok)
    {
        remove(temporary);
    }
    free(temporary);
    return ok;
}

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c + ('a' - 'A');
    }
    return c;
}

static int path_has_extension(const char* path, const char* ext)
{
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (ext_len > path_len)
    {
        return 0;
    }
    const char* suffix = path + (path_len - ext_len);
    for (size_t i = 0; i < ext_len; i++)
    {
        if (ascii_tolower((unsigned char)suffix[i]) != ascii_tolower((unsigned char)ext[i]))
        {
            return 0;
        }
    }
    return 1;
}

static int path_is_rgi_extension(const char* path)
{
    return path_has_extension(path, ".rgi");
}

static void store_u32_be(uint8_t* ptr, uint32_t value)
{
    ptr[0] = (uint8_t)((value >> 24u) & 0xffu);
    ptr[1] = (uint8_t)((value >> 16u) & 0xffu);
    ptr[2] = (uint8_t)((value >> 8u) & 0xffu);
    ptr[3] = (uint8_t)(value & 0xffu);
}

static uint32_t load_u32_be_public(const uint8_t* ptr)
{
    return ((uint32_t)ptr[0] << 24u) |
           ((uint32_t)ptr[1] << 16u) |
           ((uint32_t)ptr[2] << 8u) |
           (uint32_t)ptr[3];
}

static int g_trusted_png = 0;

static int is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

static const char* find_last_sep(const char* path)
{
    const char* last = NULL;
    for (const char* p = path; *p != '\0'; p++)
    {
        if (is_path_sep(*p))
        {
            last = p;
        }
    }
    return last;
}

static int file_exists(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL)
    {
        return 0;
    }
    fclose(file);
    return 1;
}

static int make_dir(const char* path)
{
    #if RG_PLATFORM_WINDOWS
        if (_mkdir(path) == 0)
        {
            return 1;
        }
    #else
        if (mkdir(path, 0755) == 0)
        {
            return 1;
        }
    #endif
    return errno == EEXIST;
}

static int make_dirs(const char* path)
{
    size_t len = strlen(path);
    if (len == 0u)
    {
        return 1;
    }

    char* temp = (char*)malloc(len + 1u);
    if (temp == NULL)
    {
        return 0;
    }
    memcpy(temp, path, len + 1u);

    while (len > 0u && is_path_sep(temp[len - 1u]))
    {
        temp[len - 1u] = '\0';
        len--;
    }

    size_t start = 0u;
    #if RG_PLATFORM_WINDOWS
        if (len >= 3u && temp[1] == ':' && is_path_sep(temp[2]))
        {
            start = 3u;
        }
    #endif
    if (start == 0u && len > 0u && is_path_sep(temp[0]))
    {
        start = 1u;
    }

    for (size_t i = start; i < len; i++)
    {
        if (is_path_sep(temp[i]))
        {
            char saved = temp[i];
            temp[i] = '\0';
            if (temp[0] != '\0' && !make_dir(temp))
            {
                free(temp);
                return 0;
            }
            temp[i] = saved;
        }
    }

    if (!make_dir(temp))
    {
        free(temp);
        return 0;
    }

    free(temp);
    return 1;
}

static char* canonical_directory_path(const char* path)
{
    #if RG_PLATFORM_WINDOWS
        HANDLE handle = CreateFileA(path,
                                    FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    NULL,
                                    OPEN_EXISTING,
                                    FILE_FLAG_BACKUP_SEMANTICS,
                                    NULL);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return NULL;
        }
        DWORD required = GetFinalPathNameByHandleA(handle,
                                                    NULL,
                                                    0u,
                                                    FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (required == 0u)
        {
            CloseHandle(handle);
            return NULL;
        }
        char* result = (char*)malloc((size_t)required + 1u);
        if (result == NULL)
        {
            CloseHandle(handle);
            return NULL;
        }
        DWORD written = GetFinalPathNameByHandleA(handle,
                                                   result,
                                                   required + 1u,
                                                   FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(handle);
        if (written == 0u || written > required)
        {
            free(result);
            return NULL;
        }
        return result;
    #else
        return realpath(path, NULL);
    #endif
}

static int path_character_equal(char a, char b)
{
    if (is_path_sep(a) && is_path_sep(b))
    {
        return 1;
    }
    #if RG_PLATFORM_WINDOWS
        return ascii_tolower((unsigned char)a) == ascii_tolower((unsigned char)b);
    #else
        return a == b;
    #endif
}

static size_t path_length_without_trailing_separators(const char* path)
{
    size_t length = strlen(path);
    while (length > 1u && is_path_sep(path[length - 1u]))
    {
        #if RG_PLATFORM_WINDOWS
            if (length == 3u && path[1] == ':')
            {
                break;
            }
        #endif
        length--;
    }
    return length;
}

static int output_is_within_input(const char* input_dir, const char* output_dir)
{
    char* input = canonical_directory_path(input_dir);
    char* output = canonical_directory_path(output_dir);
    if (input == NULL || output == NULL)
    {
        free(output);
        free(input);
        return -1;
    }

    size_t input_length = path_length_without_trailing_separators(input);
    size_t output_length = path_length_without_trailing_separators(output);
    int within = output_length >= input_length;
    for (size_t i = 0u; within && i < input_length; i++)
    {
        within = path_character_equal(input[i], output[i]);
    }
    if (within && output_length > input_length &&
        !is_path_sep(input[input_length - 1u]) &&
        !is_path_sep(output[input_length]))
    {
        within = 0;
    }

    free(output);
    free(input);
    return within;
}

static int ensure_parent_dir(const char* path)
{
    const char* sep = find_last_sep(path);
    if (sep == NULL)
    {
        return 1;
    }

    size_t len = (size_t)(sep - path);
    if (len == 0u)
    {
        return 1;
    }

    char* dir = (char*)malloc(len + 1u);
    if (dir == NULL)
    {
        return 0;
    }
    memcpy(dir, path, len);
    dir[len] = '\0';

    int ok = make_dirs(dir);
    free(dir);
    return ok;
}

static char* join_path(const char* a, const char* b)
{
    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    if (a_len == 0u)
    {
        char* out = (char*)malloc(b_len + 1u);
        if (out != NULL)
        {
            memcpy(out, b, b_len + 1u);
        }
        return out;
    }
    if (b_len == 0u)
    {
        char* out = (char*)malloc(a_len + 1u);
        if (out != NULL)
        {
            memcpy(out, a, a_len + 1u);
        }
        return out;
    }

    int need_sep = !is_path_sep(a[a_len - 1u]);
    int skip_sep = is_path_sep(b[0]);
    size_t total = a_len + b_len + (need_sep ? 1u : 0u) - (skip_sep ? 1u : 0u) + 1u;

    char* out = (char*)malloc(total);
    if (out == NULL)
    {
        return NULL;
    }

    size_t pos = 0u;
    memcpy(out + pos, a, a_len);
    pos += a_len;
    if (need_sep)
    {
        out[pos++] = RG_PLATFORM_WINDOWS ? '\\' : '/';
    }
    if (skip_sep)
    {
        b++;
        b_len--;
    }
    memcpy(out + pos, b, b_len);
    pos += b_len;
    out[pos] = '\0';
    return out;
}

static char* replace_extension(const char* path, const char* new_ext)
{
    const char* sep = find_last_sep(path);
    const char* filename = (sep != NULL) ? (sep + 1u) : path;
    const char* dot = strrchr(filename, '.');

    size_t prefix_len = dot != NULL ? (size_t)(dot - path) : strlen(path);
    size_t ext_len = strlen(new_ext);

    char* out = (char*)malloc(prefix_len + ext_len + 1u);
    if (out == NULL)
    {
        return NULL;
    }

    memcpy(out, path, prefix_len);
    memcpy(out + prefix_len, new_ext, ext_len);
    out[prefix_len + ext_len] = '\0';
    return out;
}

static int is_supported_image_ext(const char* path)
{
    return path_has_extension(path, ".png") ||
           path_has_extension(path, ".rgi") ||
           path_has_extension(path, ".qoi");
}

static size_t qoi_encode_bound(uint32_t width, uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return 0u;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 5u))
    {
        return 0u;
    }

    uint64_t size64 = (uint64_t)RG_RGI_HEADER_SIZE +
                      pixel_count * 5u +
                      (uint64_t)RG_RGI_END_SIZE;
    if (size64 > (uint64_t)SIZE_MAX)
    {
        return 0u;
    }

    return (size_t)size64;
}

static size_t qoi_encode(const uint8_t* rgba,
                         uint32_t width,
                         uint32_t height,
                         uint8_t channels,
                         uint8_t* dst,
                         size_t dst_size)
{
    if (rgba == NULL || dst == NULL)
    {
        return 0u;
    }
    if (channels != 3u && channels != 4u)
    {
        return 0u;
    }

    size_t bound = qoi_encode_bound(width, height);
    if (bound == 0u || dst_size < bound)
    {
        return 0u;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;

    uint8_t* out = dst;
    out[0] = (uint8_t)'q';
    out[1] = (uint8_t)'o';
    out[2] = (uint8_t)'i';
    out[3] = (uint8_t)'f';
    store_u32_be(out + 4, width);
    store_u32_be(out + 8, height);
    out[12] = channels;
    out[13] = 0u;

    uint8_t* ptr = out + RG_RGI_HEADER_SIZE;

    RgRgiPixel index[64];
    memset(index, 0, sizeof(index));

    RgRgiPixel prev;
    prev.r = 0u;
    prev.g = 0u;
    prev.b = 0u;
    prev.a = 255u;

    int run = 0;

    for (uint64_t i = 0; i < pixel_count; i++)
    {
        RgRgiPixel px;
        size_t offset = (size_t)i * 4u;
        px.r = rgba[offset + 0u];
        px.g = rgba[offset + 1u];
        px.b = rgba[offset + 2u];
        px.a = (channels == 4u) ? rgba[offset + 3u] : 255u;

        if (rg_rgi_pixel_equal(px, prev))
        {
            run++;
            if (run == 62 || i == (pixel_count - 1u))
            {
                *ptr++ = (uint8_t)(RG_RGI_OP_RUN | (uint8_t)(run - 1));
                run = 0;
            }
            continue;
        }

        if (run > 0)
        {
            *ptr++ = (uint8_t)(RG_RGI_OP_RUN | (uint8_t)(run - 1));
            run = 0;
        }

        uint8_t index_pos = rg_rgi_hash_pixel(px);
        if (rg_rgi_pixel_equal(index[index_pos], px))
        {
            *ptr++ = (uint8_t)(RG_RGI_OP_INDEX | index_pos);
        }
        else
        {
            index[index_pos] = px;
            if (px.a == prev.a)
            {
                int dr = (int)px.r - (int)prev.r;
                int dg = (int)px.g - (int)prev.g;
                int db = (int)px.b - (int)prev.b;

                if (dr >= -2 && dr <= 1 &&
                    dg >= -2 && dg <= 1 &&
                    db >= -2 && db <= 1)
                {
                    *ptr++ = (uint8_t)(RG_RGI_OP_DIFF |
                                       ((uint8_t)(dr + 2) << 4) |
                                       ((uint8_t)(dg + 2) << 2) |
                                       (uint8_t)(db + 2));
                }
                else
                {
                    int dr_dg = dr - dg;
                    int db_dg = db - dg;
                    if (dg >= -32 && dg <= 31 &&
                        dr_dg >= -8 && dr_dg <= 7 &&
                        db_dg >= -8 && db_dg <= 7)
                    {
                        *ptr++ = (uint8_t)(RG_RGI_OP_LUMA | (uint8_t)(dg + 32));
                        *ptr++ = (uint8_t)(((uint8_t)(dr_dg + 8) << 4) |
                                           (uint8_t)(db_dg + 8));
                    }
                    else
                    {
                        *ptr++ = RG_RGI_OP_RGB;
                        *ptr++ = px.r;
                        *ptr++ = px.g;
                        *ptr++ = px.b;
                    }
                }
            }
            else
            {
                *ptr++ = RG_RGI_OP_RGBA;
                *ptr++ = px.r;
                *ptr++ = px.g;
                *ptr++ = px.b;
                *ptr++ = px.a;
            }
        }

        prev = px;
    }

    ptr[0] = 0u;
    ptr[1] = 0u;
    ptr[2] = 0u;
    ptr[3] = 0u;
    ptr[4] = 0u;
    ptr[5] = 0u;
    ptr[6] = 0u;
    ptr[7] = 1u;
    ptr += RG_RGI_END_SIZE;

    return (size_t)(ptr - out);
}

static int qoi_decode(const uint8_t* data,
                      size_t size,
                      uint8_t** out_pixels,
                      uint32_t* out_width,
                      uint32_t* out_height)
{
    if (data == NULL || out_pixels == NULL || size < 22u ||
        memcmp(data, "qoif", 4u) != 0)
    {
        return 0;
    }
    uint32_t width = load_u32_be_public(data + 4u);
    uint32_t height = load_u32_be_public(data + 8u);
    uint8_t channels = data[12];
    uint8_t colorspace = data[13];
    if (width == 0u || height == 0u || width > RG_RGI_MAX_DIM ||
        height > RG_RGI_MAX_DIM || (channels != 3u && channels != 4u) ||
        colorspace > 1u)
    {
        return 0;
    }
    uint64_t pixel_count64 = (uint64_t)width * (uint64_t)height;
    if (pixel_count64 > (uint64_t)(SIZE_MAX / 4u))
    {
        return 0;
    }
    size_t pixel_count = (size_t)pixel_count64;
    size_t output_size = pixel_count * 4u;
    uint8_t* pixels = (uint8_t*)malloc(output_size);
    if (pixels == NULL)
    {
        return 0;
    }

    RgRgiPixel index[64];
    memset(index, 0, sizeof(index));
    RgRgiPixel px = {0u, 0u, 0u, 255u};
    const uint8_t* ptr = data + 14u;
    const uint8_t* payload_end = data + size - 8u;
    size_t out_pos = 0u;
    while (out_pos < pixel_count)
    {
        if (ptr >= payload_end)
        {
            free(pixels);
            return 0;
        }
        uint8_t b1 = *ptr++;
        size_t run = 1u;
        if (b1 == 0xfeu)
        {
            if ((size_t)(payload_end - ptr) < 3u)
            {
                free(pixels);
                return 0;
            }
            px.r = ptr[0]; px.g = ptr[1]; px.b = ptr[2];
            ptr += 3u;
        }
        else if (b1 == 0xffu)
        {
            if ((size_t)(payload_end - ptr) < 4u)
            {
                free(pixels);
                return 0;
            }
            px.r = ptr[0]; px.g = ptr[1]; px.b = ptr[2]; px.a = ptr[3];
            ptr += 4u;
        }
        else
        {
            uint8_t tag = b1 & 0xc0u;
            if (tag == 0x00u)
            {
                px = index[b1 & 63u];
            }
            else if (tag == 0x40u)
            {
                px.r = (uint8_t)(px.r + ((b1 >> 4u) & 3u) - 2u);
                px.g = (uint8_t)(px.g + ((b1 >> 2u) & 3u) - 2u);
                px.b = (uint8_t)(px.b + (b1 & 3u) - 2u);
            }
            else if (tag == 0x80u)
            {
                if (ptr >= payload_end)
                {
                    free(pixels);
                    return 0;
                }
                uint8_t b2 = *ptr++;
                int dg = (int)(b1 & 63u) - 32;
                px.r = (uint8_t)((int)px.r + dg + (int)((b2 >> 4u) & 15u) - 8);
                px.g = (uint8_t)((int)px.g + dg);
                px.b = (uint8_t)((int)px.b + dg + (int)(b2 & 15u) - 8);
            }
            else
            {
                run = (size_t)(b1 & 63u) + 1u;
            }
        }
        if (run > pixel_count - out_pos)
        {
            free(pixels);
            return 0;
        }
        for (size_t i = 0u; i < run; i++)
        {
            size_t dst = (out_pos + i) * 4u;
            pixels[dst + 0u] = px.r;
            pixels[dst + 1u] = px.g;
            pixels[dst + 2u] = px.b;
            pixels[dst + 3u] = px.a;
            index[rg_rgi_hash_pixel(px)] = px;
        }
        out_pos += run;
    }
    if (ptr != payload_end || !rg_rgi__has_end_marker(payload_end))
    {
        free(pixels);
        return 0;
    }
    *out_pixels = pixels;
    if (out_width != NULL) *out_width = width;
    if (out_height != NULL) *out_height = height;
    return 1;
}

static uint32_t png_crc32_update(uint32_t crc, const uint8_t* data, size_t size)
{
    uint32_t value = crc;
    for (size_t i = 0u; i < size; i++)
    {
        value ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            uint32_t mask = (uint32_t)-(int)(value & 1u);
            value = (value >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return value;
}

static uint32_t png_crc32(const uint8_t* type, const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    crc = png_crc32_update(crc, type, 4u);
    if (data != NULL && size > 0u)
    {
        crc = png_crc32_update(crc, data, size);
    }
    return ~crc;
}

static int png_expected_inflated_size(uint32_t width,
                                      uint32_t height,
                                      uint8_t bit_depth,
                                      uint8_t color_type,
                                      uint8_t interlace,
                                      size_t* out_size)
{
    uint32_t channels;
    if (color_type == 0u) channels = 1u;
    else if (color_type == 2u) channels = 3u;
    else if (color_type == 3u) channels = 1u;
    else if (color_type == 4u) channels = 2u;
    else if (color_type == 6u) channels = 4u;
    else return 0;

    int depth_ok = bit_depth == 8u || bit_depth == 16u;
    if (color_type == 0u) depth_ok = bit_depth == 1u || bit_depth == 2u ||
                                      bit_depth == 4u || bit_depth == 8u || bit_depth == 16u;
    if (color_type == 3u) depth_ok = bit_depth == 1u || bit_depth == 2u ||
                                      bit_depth == 4u || bit_depth == 8u;
    if (!depth_ok || interlace > 1u || width == 0u || height == 0u) return 0;

    uint64_t bits_per_pixel = (uint64_t)channels * bit_depth;
    uint64_t total = 0u;
    if (interlace == 0u)
    {
        uint64_t row = ((uint64_t)width * bits_per_pixel + 7u) / 8u;
        total = (row + 1u) * (uint64_t)height;
    }
    else
    {
        static const uint8_t start_x[7] = {0u, 4u, 0u, 2u, 0u, 1u, 0u};
        static const uint8_t start_y[7] = {0u, 0u, 4u, 0u, 2u, 0u, 1u};
        static const uint8_t step_x[7] = {8u, 8u, 4u, 4u, 2u, 2u, 1u};
        static const uint8_t step_y[7] = {8u, 8u, 8u, 4u, 4u, 2u, 2u};
        for (size_t pass = 0u; pass < 7u; pass++)
        {
            uint64_t pass_width = width > start_x[pass]
                ? ((uint64_t)width - start_x[pass] + step_x[pass] - 1u) / step_x[pass]
                : 0u;
            uint64_t pass_height = height > start_y[pass]
                ? ((uint64_t)height - start_y[pass] + step_y[pass] - 1u) / step_y[pass]
                : 0u;
            if (pass_width > 0u && pass_height > 0u)
            {
                uint64_t row = (pass_width * bits_per_pixel + 7u) / 8u;
                total += (row + 1u) * pass_height;
            }
        }
    }
    if (total == 0u || total > (uint64_t)SIZE_MAX) return 0;
    *out_size = (size_t)total;
    return 1;
}

static int png_validate_integrity(const uint8_t* data, size_t size)
{
    static const uint8_t signature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    if (data == NULL || size < sizeof(signature) ||
        memcmp(data, signature, sizeof(signature)) != 0)
    {
        return 0;
    }

    size_t offset = sizeof(signature);
    size_t idat_size = 0u;
    int saw_ihdr = 0;
    int saw_idat = 0;
    int saw_iend = 0;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint8_t bit_depth = 0u;
    uint8_t color_type = 0u;
    uint8_t interlace = 0u;
    while (offset < size)
    {
        if (size - offset < 12u)
        {
            return 0;
        }
        uint32_t length32 = load_u32_be_public(data + offset);
        size_t length = (size_t)length32;
        const uint8_t* type = data + offset + 4u;
        const uint8_t* chunk = data + offset + 8u;
        if (length > size - offset - 12u)
        {
            return 0;
        }
        uint32_t expected_crc = load_u32_be_public(chunk + length);
        if (png_crc32(type, chunk, length) != expected_crc)
        {
            fprintf(stderr, "PNG CRC mismatch in %.4s\n", (const char*)type);
            return 0;
        }

        int is_ihdr = memcmp(type, "IHDR", 4u) == 0;
        int is_idat = memcmp(type, "IDAT", 4u) == 0;
        int is_iend = memcmp(type, "IEND", 4u) == 0;
        if (!saw_ihdr)
        {
            if (!is_ihdr || length != 13u)
            {
                return 0;
            }
            saw_ihdr = 1;
            width = load_u32_be_public(chunk);
            height = load_u32_be_public(chunk + 4u);
            bit_depth = chunk[8];
            color_type = chunk[9];
            interlace = chunk[12];
            if (width == 0u || height == 0u || width > RG_RGI_MAX_DIM ||
                height > RG_RGI_MAX_DIM || chunk[10] != 0u || chunk[11] != 0u)
            {
                return 0;
            }
        }
        else if (is_ihdr)
        {
            return 0;
        }
        if (is_idat)
        {
            if (idat_size > SIZE_MAX - length)
            {
                return 0;
            }
            idat_size += length;
            saw_idat = 1;
        }
        if (is_iend)
        {
            if (length != 0u || !saw_idat)
            {
                return 0;
            }
            saw_iend = 1;
        }

        offset += length + 12u;
        if (saw_iend)
        {
            if (offset != size)
            {
                return 0;
            }
            break;
        }
    }
    if (!saw_ihdr || !saw_idat || !saw_iend || idat_size == 0u)
    {
        return 0;
    }

    uint8_t* idat = (uint8_t*)malloc(idat_size);
    if (idat == NULL)
    {
        return 0;
    }
    offset = sizeof(signature);
    size_t write_offset = 0u;
    while (offset < size)
    {
        size_t length = (size_t)load_u32_be_public(data + offset);
        const uint8_t* type = data + offset + 4u;
        const uint8_t* chunk = data + offset + 8u;
        if (memcmp(type, "IDAT", 4u) == 0)
        {
            memcpy(idat + write_offset, chunk, length);
            write_offset += length;
        }
        offset += length + 12u;
    }

    size_t inflated_size = 0u;
    if (!png_expected_inflated_size(width,
                                    height,
                                    bit_depth,
                                    color_type,
                                    interlace,
                                    &inflated_size))
    {
        free(idat);
        return 0;
    }
    void* inflated = malloc(inflated_size);
    if (inflated == NULL)
    {
        free(idat);
        return 0;
    }
    size_t decompressed = tinfl_decompress_mem_to_mem(inflated,
                                                      inflated_size,
                                                      idat,
                                                      idat_size,
                                                      TINFL_FLAG_PARSE_ZLIB_HEADER);
    free(idat);
    int valid = decompressed == inflated_size;
    if (!valid)
    {
        fprintf(stderr, "PNG zlib validation failed (%zu/%zu bytes)\n",
                decompressed, inflated_size);
    }
    free(inflated);
    return valid;
}

static uint32_t png_adler32(const uint8_t* data, size_t size)
{
    const uint32_t mod = 65521u;
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t i = 0u; i < size; i++)
    {
        a += data[i];
        if (a >= mod)
        {
            a -= mod;
        }
        b += a;
        if (b >= mod)
        {
            b -= mod;
        }
    }
    return (b << 16u) | a;
}

static int png_write_chunk(FILE* file, const char* type, const uint8_t* data, size_t size)
{
    if (file == NULL || type == NULL || size > UINT32_MAX)
    {
        return 0;
    }

    uint8_t header[8];
    store_u32_be(header, (uint32_t)size);
    memcpy(header + 4u, type, 4u);

    if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
    {
        return 0;
    }
    if (size > 0u && fwrite(data, 1, size, file) != size)
    {
        return 0;
    }

    uint32_t crc = png_crc32((const uint8_t*)type, data, size);
    uint8_t crc_bytes[4];
    store_u32_be(crc_bytes, crc);
    return fwrite(crc_bytes, 1, sizeof(crc_bytes), file) == sizeof(crc_bytes);
}

static int write_png_rgba(const char* path,
                          const uint8_t* rgba,
                          uint32_t width,
                          uint32_t height)
{
    if (path == NULL || rgba == NULL || width == 0u || height == 0u)
    {
        return 0;
    }

    uint64_t stride64 = (uint64_t)width * 4u + 1u;
    uint64_t raw_size64 = stride64 * (uint64_t)height;
    if (raw_size64 == 0u || raw_size64 > (uint64_t)SIZE_MAX)
    {
        return 0;
    }

    size_t raw_size = (size_t)raw_size64;
    uint8_t* raw = (uint8_t*)malloc(raw_size);
    if (raw == NULL)
    {
        return 0;
    }

    size_t src_stride = (size_t)width * 4u;
    for (uint32_t row = 0u; row < height; row++)
    {
        size_t dst_off = (size_t)row * (size_t)stride64;
        raw[dst_off] = 0u;
        memcpy(raw + dst_off + 1u, rgba + (size_t)row * src_stride, src_stride);
    }

    uint64_t block_count = (raw_size64 + 65535u) / 65535u;
    uint64_t zlib_size64 = 2u + block_count * 5u + raw_size64 + 4u;
    if (zlib_size64 == 0u || zlib_size64 > (uint64_t)SIZE_MAX)
    {
        free(raw);
        return 0;
    }

    uint8_t* zlib = (uint8_t*)malloc((size_t)zlib_size64);
    if (zlib == NULL)
    {
        free(raw);
        return 0;
    }

    // PNG IDAT expects zlib; use uncompressed DEFLATE blocks for simplicity.
    uint8_t* ptr = zlib;
    *ptr++ = 0x78u;
    *ptr++ = 0x01u;

    size_t remaining = raw_size;
    const uint8_t* src = raw;
    while (remaining > 0u)
    {
        uint16_t chunk = (remaining > 65535u) ? 65535u : (uint16_t)remaining;
        uint8_t final_block = (remaining <= 65535u) ? 1u : 0u;
        *ptr++ = final_block;
        *ptr++ = (uint8_t)(chunk & 0xffu);
        *ptr++ = (uint8_t)((chunk >> 8u) & 0xffu);
        uint16_t nlen = (uint16_t)~chunk;
        *ptr++ = (uint8_t)(nlen & 0xffu);
        *ptr++ = (uint8_t)((nlen >> 8u) & 0xffu);
        memcpy(ptr, src, chunk);
        ptr += chunk;
        src += chunk;
        remaining -= chunk;
    }

    uint32_t adler = png_adler32(raw, raw_size);
    store_u32_be(ptr, adler);
    ptr += 4u;

    size_t zlib_size = (size_t)(ptr - zlib);

    char* temporary = make_temporary_output_path(path);
    if (temporary == NULL)
    {
        free(zlib);
        free(raw);
        return 0;
    }
    FILE* file = fopen(temporary, "wb");
    if (file == NULL)
    {
        free(temporary);
        free(zlib);
        free(raw);
        return 0;
    }

    static const uint8_t signature[8] = {137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u};
    int ok = fwrite(signature, 1, sizeof(signature), file) == sizeof(signature);

    uint8_t ihdr[13];
    store_u32_be(ihdr, width);
    store_u32_be(ihdr + 4u, height);
    ihdr[8] = 8u;
    ihdr[9] = 6u;
    ihdr[10] = 0u;
    ihdr[11] = 0u;
    ihdr[12] = 0u;

    ok = ok &&
         png_write_chunk(file, "IHDR", ihdr, sizeof(ihdr)) &&
         png_write_chunk(file, "IDAT", zlib, zlib_size) &&
         png_write_chunk(file, "IEND", NULL, 0u);

    int closed = fclose(file) == 0;
    ok = ok && closed;
    if (ok)
    {
        ok = commit_temporary_output(temporary, path);
    }
    if (!ok)
    {
        remove(temporary);
    }
    free(temporary);
    free(zlib);
    free(raw);
    return ok;
}

#if !RG_RGI_CONVERT_USE_STB
static uint32_t load_u32_be(const uint8_t* ptr)
{
    return ((uint32_t)ptr[0] << 24u) |
           ((uint32_t)ptr[1] << 16u) |
           ((uint32_t)ptr[2] << 8u) |
           (uint32_t)ptr[3];
}

static int has_png_signature(const uint8_t* data, size_t size)
{
    static const uint8_t signature[8] = {0x89u, 0x50u, 0x4eu, 0x47u, 0x0du, 0x0au, 0x1au, 0x0au};
    if (size < sizeof(signature))
    {
        return 0;
    }
    return memcmp(data, signature, sizeof(signature)) == 0;
}

static int png_read_header(const uint8_t* data,
                           size_t size,
                           uint32_t* out_width,
                           uint32_t* out_height,
                           uint8_t* out_color_type,
                           uint8_t* out_bit_depth)
{
    if (!has_png_signature(data, size))
    {
        return 0;
    }

    size_t offset = 8u;
    while (offset + 12u <= size)
    {
        uint32_t length = load_u32_be(data + offset);
        offset += 4u;
        if (offset + 4u > size)
        {
            return 0;
        }
        const uint8_t* type = data + offset;
        offset += 4u;
        if (offset + length + 4u > size)
        {
            return 0;
        }

        if (type[0] == (uint8_t)'I' &&
            type[1] == (uint8_t)'H' &&
            type[2] == (uint8_t)'D' &&
            type[3] == (uint8_t)'R')
        {
            if (length < 13u)
            {
                return 0;
            }
            uint32_t width = load_u32_be(data + offset);
            uint32_t height = load_u32_be(data + offset + 4u);
            uint8_t bit_depth = data[offset + 8u];
            uint8_t color_type = data[offset + 9u];
            uint8_t compression = data[offset + 10u];
            uint8_t filter = data[offset + 11u];
            uint8_t interlace = data[offset + 12u];

            if (width == 0u || height == 0u)
            {
                return 0;
            }
            if (bit_depth != 8u)
            {
                return 0;
            }
            if (color_type != 2u && color_type != 6u)
            {
                return 0;
            }
            if (compression != 0u || filter != 0u || interlace != 0u)
            {
                return 0;
            }

            if (out_width != NULL)
            {
                *out_width = width;
            }
            if (out_height != NULL)
            {
                *out_height = height;
            }
            if (out_color_type != NULL)
            {
                *out_color_type = color_type;
            }
            if (out_bit_depth != NULL)
            {
                *out_bit_depth = bit_depth;
            }
            return 1;
        }

        offset += length + 4u;
    }

    return 0;
}

static uint8_t png_paeth(uint8_t a, uint8_t b, uint8_t c)
{
    int pa = (int)a;
    int pb = (int)b;
    int pc = (int)c;
    int p = pa + pb - pc;
    int pa_diff = p - pa;
    int pb_diff = p - pb;
    int pc_diff = p - pc;
    if (pa_diff < 0) pa_diff = -pa_diff;
    if (pb_diff < 0) pb_diff = -pb_diff;
    if (pc_diff < 0) pc_diff = -pc_diff;
    if (pa_diff <= pb_diff && pa_diff <= pc_diff)
    {
        return a;
    }
    if (pb_diff <= pc_diff)
    {
        return b;
    }
    return c;
}

static int png_decode_rgba(const uint8_t* data,
                           size_t size,
                           uint8_t** out_pixels,
                           uint32_t* out_width,
                           uint32_t* out_height)
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t color_type = 0;
    uint8_t bit_depth = 0;
    if (!png_read_header(data, size, &width, &height, &color_type, &bit_depth))
    {
        return 0;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 4u))
    {
        return 0;
    }

    size_t out_bytes = (size_t)pixel_count * 4u;
    uint8_t* out = (uint8_t*)malloc(out_bytes);
    if (out == NULL)
    {
        return 0;
    }

    size_t offset = 8u;
    size_t idat_bytes = 0u;
    while (offset + 12u <= size)
    {
        uint32_t length = load_u32_be(data + offset);
        offset += 4u;
        if (offset + 4u > size)
        {
            free(out);
            return 0;
        }
        const uint8_t* type = data + offset;
        offset += 4u;
        if (offset + length + 4u > size)
        {
            free(out);
            return 0;
        }
        if (type[0] == (uint8_t)'I' &&
            type[1] == (uint8_t)'D' &&
            type[2] == (uint8_t)'A' &&
            type[3] == (uint8_t)'T')
        {
            idat_bytes += length;
        }
        if (type[0] == (uint8_t)'I' &&
            type[1] == (uint8_t)'E' &&
            type[2] == (uint8_t)'N' &&
            type[3] == (uint8_t)'D')
        {
            break;
        }
        offset += length + 4u;
    }

    if (idat_bytes == 0u)
    {
        free(out);
        return 0;
    }

    uint8_t* idat = (uint8_t*)malloc(idat_bytes);
    if (idat == NULL)
    {
        free(out);
        return 0;
    }

    offset = 8u;
    size_t idat_offset = 0u;
    while (offset + 12u <= size)
    {
        uint32_t length = load_u32_be(data + offset);
        offset += 4u;
        if (offset + 4u > size)
        {
            free(idat);
            free(out);
            return 0;
        }
        const uint8_t* type = data + offset;
        offset += 4u;
        if (offset + length + 4u > size)
        {
            free(idat);
            free(out);
            return 0;
        }
        if (type[0] == (uint8_t)'I' &&
            type[1] == (uint8_t)'D' &&
            type[2] == (uint8_t)'A' &&
            type[3] == (uint8_t)'T')
        {
            memcpy(idat + idat_offset, data + offset, length);
            idat_offset += length;
        }
        if (type[0] == (uint8_t)'I' &&
            type[1] == (uint8_t)'E' &&
            type[2] == (uint8_t)'N' &&
            type[3] == (uint8_t)'D')
        {
            break;
        }
        offset += length + 4u;
    }

    if (idat_offset != idat_bytes)
    {
        free(idat);
        free(out);
        return 0;
    }

    size_t bytes_per_pixel = (color_type == 6u) ? 4u : 3u;
    size_t stride = (size_t)width * bytes_per_pixel;
    size_t raw_size = (stride + 1u) * (size_t)height;

    uint8_t* raw = (uint8_t*)malloc(raw_size);
    if (raw == NULL)
    {
        free(idat);
        free(out);
        return 0;
    }

    size_t inflated = tinfl_decompress_mem_to_mem(raw,
                                                  raw_size,
                                                  idat,
                                                  idat_bytes,
                                                  TINFL_FLAG_PARSE_ZLIB_HEADER |
                                                  TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(idat);
    if (inflated != raw_size)
    {
        free(raw);
        free(out);
        return 0;
    }

    for (uint32_t y = 0; y < height; y++)
    {
        size_t row_offset = (size_t)y * (stride + 1u);
        uint8_t filter = raw[row_offset];
        uint8_t* row = raw + row_offset + 1u;
        uint8_t* prev = (y == 0u) ? NULL : (row - (stride + 1u));

        if (filter == 0u)
        {
            // None
        }
        else if (filter == 1u)
        {
            for (size_t i = 0; i < stride; i++)
            {
                uint8_t left = (i >= bytes_per_pixel) ? row[i - bytes_per_pixel] : 0u;
                row[i] = (uint8_t)(row[i] + left);
            }
        }
        else if (filter == 2u)
        {
            const uint8_t* prev_row = (prev != NULL) ? (prev + 1u) : NULL;
            for (size_t i = 0; i < stride; i++)
            {
                uint8_t up = (prev_row != NULL) ? prev_row[i] : 0u;
                row[i] = (uint8_t)(row[i] + up);
            }
        }
        else if (filter == 3u)
        {
            const uint8_t* prev_row = (prev != NULL) ? (prev + 1u) : NULL;
            for (size_t i = 0; i < stride; i++)
            {
                uint8_t left = (i >= bytes_per_pixel) ? row[i - bytes_per_pixel] : 0u;
                uint8_t up = (prev_row != NULL) ? prev_row[i] : 0u;
                row[i] = (uint8_t)(row[i] + (uint8_t)(((uint16_t)left + (uint16_t)up) >> 1));
            }
        }
        else if (filter == 4u)
        {
            const uint8_t* prev_row = (prev != NULL) ? (prev + 1u) : NULL;
            for (size_t i = 0; i < stride; i++)
            {
                uint8_t left = (i >= bytes_per_pixel) ? row[i - bytes_per_pixel] : 0u;
                uint8_t up = (prev_row != NULL) ? prev_row[i] : 0u;
                uint8_t up_left = (prev_row != NULL && i >= bytes_per_pixel) ? prev_row[i - bytes_per_pixel] : 0u;
                row[i] = (uint8_t)(row[i] + png_paeth(left, up, up_left));
            }
        }
        else
        {
            free(raw);
            free(out);
            return 0;
        }

        uint8_t* out_row = out + (size_t)y * (size_t)width * 4u;
        if (color_type == 6u)
        {
            memcpy(out_row, row, stride);
        }
        else
        {
            for (uint32_t x = 0; x < width; x++)
            {
                size_t src = (size_t)x * 3u;
                size_t dst = (size_t)x * 4u;
                out_row[dst + 0] = row[src + 0];
                out_row[dst + 1] = row[src + 1];
                out_row[dst + 2] = row[src + 2];
                out_row[dst + 3] = 255u;
            }
        }
    }

    free(raw);

    if (out_width != NULL)
    {
        *out_width = width;
    }
    if (out_height != NULL)
    {
        *out_height = height;
    }
    *out_pixels = out;
    return 1;
}
#endif

static int decode_png_to_rgba(const uint8_t* data,
                              size_t size,
                              uint8_t** out_pixels,
                              uint32_t* out_width,
                              uint32_t* out_height)
{
    if (!g_trusted_png && !png_validate_integrity(data, size))
    {
        return 0;
    }
    #if RG_RGI_CONVERT_USE_STB
        if (size > (size_t)INT_MAX)
        {
            return 0;
        }
        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* pixels = stbi_load_from_memory(data,
                                                      (int)size,
                                                      &width,
                                                      &height,
                                                      &channels,
                                                      4);
        if (pixels == NULL)
        {
            return 0;
        }
        if (width <= 0 || height <= 0 ||
            (uint32_t)width > RG_RGI_MAX_DIM || (uint32_t)height > RG_RGI_MAX_DIM)
        {
            stbi_image_free(pixels);
            return 0;
        }
        *out_pixels = (uint8_t*)pixels;
        if (out_width != NULL)
        {
            *out_width = (uint32_t)width;
        }
        if (out_height != NULL)
        {
            *out_height = (uint32_t)height;
        }
        return 1;
    #else
        return png_decode_rgba(data, size, out_pixels, out_width, out_height);
    #endif
}

static void free_pixels(uint8_t* pixels)
{
    #if RG_RGI_CONVERT_USE_STB
        stbi_image_free(pixels);
    #else
        free(pixels);
    #endif
}

static int image_has_alpha(const uint8_t* pixels, uint32_t width, uint32_t height)
{
    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    for (uint64_t i = 0; i < pixel_count; i++)
    {
        if (pixels[i * 4u + 3u] != 255u)
        {
            return 1;
        }
    }
    return 0;
}

typedef enum RgiConvertFormat
{
    RGI_CONVERT_FORMAT_RGI = 0,
    RGI_CONVERT_FORMAT_QOI = 1,
    RGI_CONVERT_FORMAT_PNG = 2
} RgiConvertFormat;

static const char* format_name(RgiConvertFormat format)
{
    if (format == RGI_CONVERT_FORMAT_QOI) return "QOI";
    if (format == RGI_CONVERT_FORMAT_PNG) return "PNG";
    return "RGI";
}

static const char* format_extension(RgiConvertFormat format)
{
    if (format == RGI_CONVERT_FORMAT_QOI) return ".qoi";
    if (format == RGI_CONVERT_FORMAT_PNG) return ".png";
    return ".rgi";
}

static int decode_input_to_rgba(const char* input_path,
                                const uint8_t* data,
                                size_t size,
                                uint8_t** out_pixels,
                                uint32_t* out_width,
                                uint32_t* out_height)
{
    if (path_has_extension(input_path, ".png"))
    {
        return decode_png_to_rgba(data, size, out_pixels, out_width, out_height);
    }
    if (path_has_extension(input_path, ".qoi"))
    {
        return qoi_decode(data, size, out_pixels, out_width, out_height);
    }
    if (path_is_rgi_extension(input_path))
    {
        uint32_t width = 0u;
        uint32_t height = 0u;
        if (!rg_rgi_read_header(data, size, &width, &height))
        {
            return 0;
        }
        uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
        if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 4u))
        {
            return 0;
        }
        size_t pixel_size = (size_t)pixel_count * 4u;
        uint8_t* pixels = (uint8_t*)malloc(pixel_size);
        if (pixels == NULL)
        {
            return 0;
        }
        if (rg_rgi_decode(data, size, pixels, pixel_size, NULL, NULL) != pixel_size)
        {
            free(pixels);
            return 0;
        }
        *out_pixels = pixels;
        if (out_width != NULL) *out_width = width;
        if (out_height != NULL) *out_height = height;
        return 1;
    }
    return 0;
}

static int convert_file(const char* input_path,
                        const char* output_path,
                        RgiConvertFormat output_format,
                        int overwrite)
{
    if (!overwrite && file_exists(output_path))
    {
        printf("Skipping %s (exists)\n", output_path);
        return 0;
    }
    if (!ensure_parent_dir(output_path))
    {
        printf("Failed to create output directory for %s\n", output_path);
        return -1;
    }

    uint8_t* input_data = NULL;
    size_t input_size = 0u;
    if (!load_file(input_path, &input_data, &input_size))
    {
        printf("Failed to read %s\n", input_path);
        return -1;
    }

    uint8_t* pixels = NULL;
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!decode_input_to_rgba(input_path, input_data, input_size, &pixels, &width, &height))
    {
        printf("Failed to decode %s\n", input_path);
        free(input_data);
        return -1;
    }

    if (output_format == RGI_CONVERT_FORMAT_PNG)
    {
        int ok = write_png_rgba(output_path, pixels, width, height);
        if (ok)
        {
            printf("Wrote %s (%ux%u, PNG)\n", output_path, width, height);
        }
        else
        {
            printf("Failed to write %s\n", output_path);
        }
        free_pixels(pixels);
        free(input_data);
        return ok ? 1 : -1;
    }

    uint8_t qoi_channels = 4u;
    size_t bound = 0u;
    if (output_format == RGI_CONVERT_FORMAT_QOI)
    {
        qoi_channels = image_has_alpha(pixels, width, height) ? 4u : 3u;
        bound = qoi_encode_bound(width, height);
    }
    else
    {
        bound = rg_rgi_encode_bound(width, height);
    }
    if (bound == 0u)
    {
        printf("Invalid %s dimensions (%ux%u).\n", format_name(output_format), width, height);
        free_pixels(pixels);
        free(input_data);
        return -1;
    }

    uint8_t* out = (uint8_t*)malloc(bound);
    if (out == NULL)
    {
        printf("Failed to allocate output buffer.\n");
        free_pixels(pixels);
        free(input_data);
        return -1;
    }
    size_t written = output_format == RGI_CONVERT_FORMAT_QOI
        ? qoi_encode(pixels, width, height, qoi_channels, out, bound)
        : rg_rgi_encode(pixels, width, height, out, bound);
    int ok = written > 0u && write_file(output_path, out, written);
    if (ok)
    {
        printf("Wrote %s (%ux%u, %zu bytes, %s)\n",
               output_path, width, height, written, format_name(output_format));
    }
    else
    {
        printf("Failed to encode or write %s\n", output_path);
    }

    free(out);
    free_pixels(pixels);
    free(input_data);
    return ok ? 1 : -1;
}

static int convert_directory_recursive(const char* input_root,
                                       const char* rel_dir,
                                       const char* output_root,
                                       RgiConvertFormat output_format,
                                       int recursive,
                                       int overwrite,
                                       size_t* converted,
                                       size_t* skipped,
                                       size_t* failed)
{
    char* current_dir = (rel_dir != NULL && rel_dir[0] != '\0')
        ? join_path(input_root, rel_dir)
        : join_path(input_root, "");
    if (current_dir == NULL)
    {
        return 0;
    }

    #if RG_PLATFORM_WINDOWS
        char* pattern = join_path(current_dir, "*");
        if (pattern == NULL)
        {
            free(current_dir);
            return 0;
        }
        WIN32_FIND_DATAA find_data;
        HANDLE handle = FindFirstFileA(pattern, &find_data);
        free(pattern);
        if (handle == INVALID_HANDLE_VALUE)
        {
            free(current_dir);
            return 0;
        }

        do
        {
            const char* name = find_data.cFileName;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            {
                continue;
            }

            int is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (is_dir)
            {
                if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
                {
                    continue;
                }
                if (recursive)
                {
                    char* next_rel = (rel_dir != NULL && rel_dir[0] != '\0')
                        ? join_path(rel_dir, name)
                        : join_path(name, "");
                    if (next_rel != NULL)
                    {
                        if (!convert_directory_recursive(input_root,
                                                         next_rel,
                                                         output_root,
                                                         output_format,
                                                         recursive,
                                                         overwrite,
                                                         converted,
                                                         skipped,
                                                         failed))
                        {
                            free(next_rel);
                            FindClose(handle);
                            free(current_dir);
                            return 0;
                        }
                        free(next_rel);
                    }
                }
                continue;
            }

            if (!is_supported_image_ext(name))
            {
                continue;
            }

            char* rel_path = (rel_dir != NULL && rel_dir[0] != '\0')
                ? join_path(rel_dir, name)
                : join_path(name, "");
            if (rel_path == NULL)
            {
                (*failed)++;
                continue;
            }

            char* rel_out = replace_extension(rel_path, format_extension(output_format));
            char* input_path = join_path(current_dir, name);
            char* output_path = (rel_out != NULL) ? join_path(output_root, rel_out) : NULL;
            free(rel_out);
            free(rel_path);

            if (input_path == NULL || output_path == NULL)
            {
                free(input_path);
                free(output_path);
                (*failed)++;
                continue;
            }

            int result = convert_file(input_path, output_path, output_format, overwrite);
            if (result > 0)
            {
                (*converted)++;
            }
            else if (result == 0)
            {
                (*skipped)++;
            }
            else
            {
                (*failed)++;
            }

            free(input_path);
            free(output_path);
        }
        while (FindNextFileA(handle, &find_data) != 0);

        FindClose(handle);
    #else
        DIR* dir = opendir(current_dir);
        if (dir == NULL)
        {
            free(current_dir);
            return 0;
        }
        struct dirent* entry = NULL;
        while ((entry = readdir(dir)) != NULL)
        {
            const char* name = entry->d_name;
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            {
                continue;
            }

            char* full_path = join_path(current_dir, name);
            if (full_path == NULL)
            {
                (*failed)++;
                continue;
            }

            struct stat st;
            if (lstat(full_path, &st) != 0)
            {
                free(full_path);
                (*failed)++;
                continue;
            }

            if (S_ISLNK(st.st_mode))
            {
                struct stat target;
                if (stat(full_path, &target) != 0)
                {
                    free(full_path);
                    (*failed)++;
                    continue;
                }
                if (S_ISDIR(target.st_mode))
                {
                    free(full_path);
                    continue;
                }
                st = target;
            }

            if (S_ISDIR(st.st_mode))
            {
                free(full_path);
                if (recursive)
                {
                    char* next_rel = (rel_dir != NULL && rel_dir[0] != '\0')
                        ? join_path(rel_dir, name)
                        : join_path(name, "");
                    if (next_rel != NULL)
                    {
                        if (!convert_directory_recursive(input_root,
                                                         next_rel,
                                                         output_root,
                                                         output_format,
                                                         recursive,
                                                         overwrite,
                                                         converted,
                                                         skipped,
                                                         failed))
                        {
                            free(next_rel);
                            closedir(dir);
                            free(current_dir);
                            return 0;
                        }
                        free(next_rel);
                    }
                }
                continue;
            }

            if (!is_supported_image_ext(name))
            {
                free(full_path);
                continue;
            }

            char* rel_path = (rel_dir != NULL && rel_dir[0] != '\0')
                ? join_path(rel_dir, name)
                : join_path(name, "");
            if (rel_path == NULL)
            {
                free(full_path);
                (*failed)++;
                continue;
            }

            char* rel_out = replace_extension(rel_path, format_extension(output_format));
            char* output_path = (rel_out != NULL) ? join_path(output_root, rel_out) : NULL;
            free(rel_out);
            free(rel_path);

            if (output_path == NULL)
            {
                free(full_path);
                (*failed)++;
                continue;
            }

            int result = convert_file(full_path, output_path, output_format, overwrite);
            if (result > 0)
            {
                (*converted)++;
            }
            else if (result == 0)
            {
                (*skipped)++;
            }
            else
            {
                (*failed)++;
            }

            free(full_path);
            free(output_path);
        }

        closedir(dir);
    #endif

    free(current_dir);
    return 1;
}

static int output_format_from_path(const char* path, RgiConvertFormat* out_format)
{
    if (path_is_rgi_extension(path))
    {
        *out_format = RGI_CONVERT_FORMAT_RGI;
        return 1;
    }
    if (path_has_extension(path, ".qoi"))
    {
        *out_format = RGI_CONVERT_FORMAT_QOI;
        return 1;
    }
    if (path_has_extension(path, ".png"))
    {
        *out_format = RGI_CONVERT_FORMAT_PNG;
        return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "--dir") == 0)
    {
        if (argc < 4)
        {
            print_usage();
            return 1;
        }

        const char* input_dir = argv[2];
        const char* output_dir = argv[3];
        RgiConvertFormat output_format = RGI_CONVERT_FORMAT_RGI;
        int recursive = 0;
        int overwrite = 0;

        for (int i = 4; i < argc; i++)
        {
            if (strcmp(argv[i], "--qoi") == 0)
            {
                output_format = RGI_CONVERT_FORMAT_QOI;
            }
            else if (strcmp(argv[i], "--rgi") == 0)
            {
                output_format = RGI_CONVERT_FORMAT_RGI;
            }
            else if (strcmp(argv[i], "--png") == 0)
            {
                output_format = RGI_CONVERT_FORMAT_PNG;
            }
            else if (strcmp(argv[i], "--recursive") == 0)
            {
                recursive = 1;
            }
            else if (strcmp(argv[i], "--overwrite") == 0)
            {
                overwrite = 1;
            }
            else if (strcmp(argv[i], "--trusted-png") == 0)
            {
                g_trusted_png = 1;
            }
            else
            {
                print_usage();
                return 1;
            }
        }

        if (!make_dirs(output_dir))
        {
            printf("Failed to create output directory %s\n", output_dir);
            return 1;
        }

        int output_relation = output_is_within_input(input_dir, output_dir);
        if (output_relation < 0)
        {
            printf("Failed to resolve input or output directory.\n");
            return 1;
        }
        if (output_relation > 0)
        {
            printf("Output directory must not equal or be inside the input directory.\n");
            return 1;
        }

        size_t converted = 0u;
        size_t skipped = 0u;
        size_t failed = 0u;
        if (!convert_directory_recursive(input_dir,
                                         "",
                                         output_dir,
                                         output_format,
                                         recursive,
                                         overwrite,
                                         &converted,
                                         &skipped,
                                         &failed))
        {
            printf("Failed to read directory %s\n", input_dir);
            return 1;
        }

        printf("Converted: %zu, skipped: %zu, failed: %zu\n", converted, skipped, failed);
        return failed ? 1 : 0;
    }

    int path_arg = 1;
    if (strcmp(argv[path_arg], "--trusted-png") == 0)
    {
        g_trusted_png = 1;
        path_arg++;
    }
    if (argc - path_arg != 2)
    {
        print_usage();
        return 1;
    }

    const char* input_path = argv[path_arg];
    const char* output_path = argv[path_arg + 1];
    if (!is_supported_image_ext(input_path))
    {
        printf("Input extension must be .rgi, .qoi, or .png\n");
        return 1;
    }
    RgiConvertFormat output_format;
    if (!output_format_from_path(output_path, &output_format))
    {
        printf("Output extension must be .rgi, .qoi, or .png\n");
        return 1;
    }

    int result = convert_file(input_path, output_path, output_format, 1);
    return (result > 0) ? 0 : 1;
}
