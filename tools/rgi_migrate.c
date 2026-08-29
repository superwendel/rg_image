// rgi_migrate - Strict in-place migration from private RGI2 streams to RGI.

#include "../src/rg_rgi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if RG_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <fcntl.h>
    #include <io.h>
#else
    #include <unistd.h>
#endif

typedef enum RgiMigrationState
{
    RGI_MIGRATION_INVALID = 0,
    RGI_MIGRATION_LEGACY_RGI2 = 1,
    RGI_MIGRATION_CURRENT = 2
} RgiMigrationState;

static int read_stream(FILE* file, uint8_t** out_data, size_t* out_size)
{
    size_t size = 0u;
    size_t capacity = 4096u;
    uint8_t* data = (uint8_t*)malloc(capacity);
    if (data == NULL) return 0;
    for (;;)
    {
        if (size == capacity)
        {
            if (capacity > SIZE_MAX / 2u)
            {
                free(data);
                return 0;
            }
            capacity *= 2u;
            uint8_t* grown = (uint8_t*)realloc(data, capacity);
            if (grown == NULL)
            {
                free(data);
                return 0;
            }
            data = grown;
        }
        size_t got = fread(data + size, 1u, capacity - size, file);
        size += got;
        if (got == 0u)
        {
            if (ferror(file))
            {
                free(data);
                return 0;
            }
            break;
        }
    }
    *out_data = data;
    *out_size = size;
    return 1;
}

static int read_file(const char* path, uint8_t** out_data, size_t* out_size)
{
    FILE* file = fopen(path, "rb");
    if (file == NULL) return 0;
    int ok = read_stream(file, out_data, out_size);
    fclose(file);
    return ok;
}

static char* temporary_path(const char* path)
{
    static unsigned int counter = 0u;
    unsigned long process_id;
    #if RG_PLATFORM_WINDOWS
        process_id = (unsigned long)GetCurrentProcessId();
    #else
        process_id = (unsigned long)getpid();
    #endif
    size_t length = strlen(path);
    char* result = (char*)malloc(length + 48u);
    if (result == NULL) return NULL;
    counter++;
    snprintf(result, length + 48u, "%s.tmp.%lu.%u", path, process_id, counter);
    return result;
}

static int atomic_replace(const char* path, const uint8_t* data, size_t size)
{
    char* temporary = temporary_path(path);
    if (temporary == NULL) return 0;
    FILE* file = fopen(temporary, "wb");
    if (file == NULL)
    {
        free(temporary);
        return 0;
    }
    int ok = fwrite(data, 1u, size, file) == size;
    int closed = fclose(file) == 0;
    ok = ok && closed;
    if (ok)
    {
        #if RG_PLATFORM_WINDOWS
            ok = MoveFileExA(temporary,
                             path,
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
        #else
            ok = rename(temporary, path) == 0;
        #endif
    }
    if (!ok) remove(temporary);
    free(temporary);
    return ok;
}

static int validate_current(const uint8_t* data, size_t size)
{
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (!rg_rgi_read_header(data, size, &width, &height)) return 0;
    uint64_t count = (uint64_t)width * (uint64_t)height;
    if (count == 0u || count > (uint64_t)(SIZE_MAX / 4u)) return 0;
    size_t output_size = (size_t)count * 4u;
    uint8_t* pixels = (uint8_t*)malloc(output_size);
    if (pixels == NULL) return 0;
    int ok = rg_rgi_decode(data, size, pixels, output_size, NULL, NULL) == output_size;
    free(pixels);
    return ok;
}

static RgiMigrationState inspect_stream(const uint8_t* data, size_t size)
{
    if (data == NULL || size < RG_RGI_HEADER_SIZE + RG_RGI_END_SIZE) return RGI_MIGRATION_INVALID;
    if (memcmp(data, "rgif", 4u) == 0)
    {
        return validate_current(data, size) ? RGI_MIGRATION_CURRENT : RGI_MIGRATION_INVALID;
    }
    if (memcmp(data, "rgi2", 4u) != 0) return RGI_MIGRATION_INVALID;

    uint8_t* candidate = (uint8_t*)malloc(size);
    if (candidate == NULL) return RGI_MIGRATION_INVALID;
    memcpy(candidate, data, size);
    memcpy(candidate, "rgif", 4u);
    int ok = validate_current(candidate, size) && memcmp(candidate + 4u, data + 4u, size - 4u) == 0;
    free(candidate);
    return ok ? RGI_MIGRATION_LEGACY_RGI2 : RGI_MIGRATION_INVALID;
}

static int process_path(const char* path, int in_place)
{
    uint8_t* data = NULL;
    size_t size = 0u;
    if (!read_file(path, &data, &size))
    {
        fprintf(stderr, "read failed: %s\n", path);
        return 0;
    }
    RgiMigrationState state = inspect_stream(data, size);
    if (state == RGI_MIGRATION_INVALID)
    {
        fprintf(stderr, "invalid or unsupported stream: %s\n", path);
        free(data);
        return 0;
    }
    if (state == RGI_MIGRATION_CURRENT)
    {
        printf("current: %s\n", path);
        free(data);
        return 1;
    }
    if (!in_place)
    {
        printf("rgi2: %s\n", path);
        free(data);
        return 1;
    }

    memcpy(data, "rgif", 4u);
    if (!validate_current(data, size) || !atomic_replace(path, data, size))
    {
        fprintf(stderr, "migration failed: %s\n", path);
        free(data);
        return 0;
    }
    printf("migrated: %s\n", path);
    free(data);
    return 1;
}

static int process_list0(const char* list_path)
{
    uint8_t* list = NULL;
    size_t list_size = 0u;
    if (strcmp(list_path, "-") == 0)
    {
        #if RG_PLATFORM_WINDOWS
            _setmode(_fileno(stdin), _O_BINARY);
        #endif
        if (!read_stream(stdin, &list, &list_size)) return 0;
    }
    else if (!read_file(list_path, &list, &list_size))
    {
        return 0;
    }
    if (list_size == 0u || list[list_size - 1u] != 0u)
    {
        fprintf(stderr, "NUL-delimited list must end with NUL\n");
        free(list);
        return 0;
    }
    int all_ok = 1;
    size_t offset = 0u;
    while (offset < list_size)
    {
        const char* path = (const char*)list + offset;
        size_t remaining = list_size - offset;
        const void* terminator = memchr(path, 0, remaining);
        if (terminator == NULL)
        {
            all_ok = 0;
            break;
        }
        size_t length = (size_t)((const char*)terminator - path);
        if (length > 0u && !process_path(path, 1)) all_ok = 0;
        offset += length + 1u;
    }
    free(list);
    return all_ok;
}

static void usage(void)
{
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  rgi_migrate --check <file.rgi> [...]\n");
    fprintf(stderr, "  rgi_migrate --in-place <file.rgi> [...]\n");
    fprintf(stderr, "  rgi_migrate --in-place-list0 <path-list|->\n");
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
        return 1;
    }
    if (strcmp(argv[1], "--in-place-list0") == 0)
    {
        if (argc != 3)
        {
            usage();
            return 1;
        }
        return process_list0(argv[2]) ? 0 : 1;
    }
    int in_place;
    if (strcmp(argv[1], "--check") == 0) in_place = 0;
    else if (strcmp(argv[1], "--in-place") == 0) in_place = 1;
    else
    {
        usage();
        return 1;
    }
    int all_ok = 1;
    for (int i = 2; i < argc; i++)
    {
        if (!process_path(argv[i], in_place)) all_ok = 0;
    }
    return all_ok ? 0 : 1;
}
