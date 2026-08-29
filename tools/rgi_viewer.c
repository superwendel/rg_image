// rgi_viewer - Simple SDL3 viewer for .rgi images
//
// Usage:
//   rgi_viewer.exe [input.rgi]
//
// Controls:
//   Esc - Quit
//   Left/Right - Previous/next image in folder
//   I - Toggle info overlay
//   F11 - Toggle fullscreen
//   Mouse wheel - Zoom
//   Middle drag - Pan
//   Drag + drop .rgi to load

#include <rg_rgi.h>
#include <rg_sprintf.h>

#include <SDL3/SDL.h>

#if RG_PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <shellapi.h>
    #include <shlobj.h>
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void choose_window_size(uint32_t img_w, uint32_t img_h, int* out_w, int* out_h);
static uint64_t file_timestamp_ns(const char* path);
static int should_hot_reload(uint64_t now_ns, uint64_t* last_check_ns, uint64_t* last_stamp_ns, const char* path);
static uint64_t file_size_bytes(const char* path);
static void format_size(char* buffer, size_t buffer_size, uint64_t bytes);
static SDL_Texture* load_rgi_to_texture(SDL_Renderer* renderer, const char* path, uint32_t* out_width, uint32_t* out_height);
#if RG_PLATFORM_WINDOWS
static int register_rgi_file_association(void);
static int unregister_rgi_file_association(void);
static int register_rgi_thumbnail_provider(void);
static int unregister_rgi_thumbnail_provider(void);
#endif

static void print_usage(const char* exe_name)
{
    const char* name = exe_name ? exe_name : "rgi_viewer.exe";
    printf("Usage:\n");
    printf("  %s [input.rgi]\n", name);
    printf("Controls:\n");
    printf("  Esc - Quit\n");
    printf("  Left/Right - Previous/next image in folder\n");
    printf("  I - Toggle info overlay\n");
    printf("  F11 - Toggle fullscreen\n");
    printf("  Mouse wheel - Zoom\n");
    printf("  Middle drag - Pan\n");
    printf("  Drag + drop .rgi to load\n");
    printf("  Hot-reload on file change\n");
#if RG_PLATFORM_WINDOWS
    printf("Options:\n");
    printf("  --register   Register .rgi association (per-user)\n");
    printf("  --unregister Remove .rgi association (per-user)\n");
    printf("  --register-thumbnail   Register thumbnail provider (per-user)\n");
    printf("  --unregister-thumbnail Remove thumbnail provider (per-user)\n");
    printf("  --register-all Register associations and thumbnail provider (per-user)\n");
    printf("  --unregister-all Remove associations and thumbnail provider (per-user)\n");
#endif
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

static const char* path_basename(const char* path)
{
    const char* name = path ? path : "rgi_viewer";
    if (path == NULL)
    {
        return name;
    }

    for (const char* p = path; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
        {
            name = p + 1;
        }
    }

    return name;
}

static int ascii_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c + ('a' - 'A');
    }
    return c;
}

#if RG_PLATFORM_WINDOWS
static int ascii_stricmp(const char* a, const char* b)
{
    while (*a != '\0' && *b != '\0')
    {
        int ca = ascii_tolower((unsigned char)*a);
        int cb = ascii_tolower((unsigned char)*b);
        if (ca != cb)
        {
            return ca - cb;
        }
        a++;
        b++;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}
#endif

static int is_path_sep(char c)
{
    return c == '/' || c == '\\';
}

static char* string_duplicate(const char* src)
{
    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1u);
    if (out == NULL)
    {
        return NULL;
    }
    memcpy(out, src, len + 1u);
    return out;
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

static char* path_dirname_alloc(const char* path)
{
    if (path == NULL || path[0] == '\0')
    {
        return string_duplicate(".");
    }

    const char* last_sep = NULL;
    for (const char* p = path; *p != '\0'; p++)
    {
        if (is_path_sep(*p))
        {
            last_sep = p;
        }
    }

    if (last_sep == NULL)
    {
        return string_duplicate(".");
    }

    size_t len = (size_t)(last_sep - path);
    if (len == 0u)
    {
        len = 1u;
    }
#if RG_PLATFORM_WINDOWS
    if (len == 2u && path[1] == ':')
    {
        len = 3u;
    }
#endif

    char* out = (char*)malloc(len + 1u);
    if (out == NULL)
    {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static char* path_join_alloc(const char* dir, const char* name)
{
    if (dir == NULL || dir[0] == '\0' || (dir[0] == '.' && dir[1] == '\0'))
    {
        return string_duplicate(name);
    }

    size_t dir_len = strlen(dir);
    size_t name_len = strlen(name);
    int needs_sep = dir_len > 0u ? !is_path_sep(dir[dir_len - 1u]) : 1;
#if RG_PLATFORM_WINDOWS
    char sep = '\\';
#else
    char sep = '/';
#endif
    size_t total = dir_len + (needs_sep ? 1u : 0u) + name_len;
    char* out = (char*)malloc(total + 1u);
    if (out == NULL)
    {
        return NULL;
    }
    memcpy(out, dir, dir_len);
    size_t offset = dir_len;
    if (needs_sep)
    {
        out[offset++] = sep;
    }
    memcpy(out + offset, name, name_len);
    out[total] = '\0';
    return out;
}

static int path_equals(const char* a, const char* b)
{
    if (a == NULL || b == NULL)
    {
        return 0;
    }
#if RG_PLATFORM_WINDOWS
    while (*a != '\0' && *b != '\0')
    {
        char ca = (char)ascii_tolower((unsigned char)*a);
        char cb = (char)ascii_tolower((unsigned char)*b);
        if (is_path_sep(ca))
        {
            ca = '\\';
        }
        if (is_path_sep(cb))
        {
            cb = '\\';
        }
        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
#else
    return strcmp(a, b) == 0;
#endif
}

#if RG_PLATFORM_WINDOWS
static int set_registry_string(HKEY root, const char* subkey, const char* value_name, const char* value)
{
    HKEY key = NULL;
    DWORD disposition = 0;
    if (RegCreateKeyExA(root, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, &disposition) != ERROR_SUCCESS)
    {
        return 0;
    }

    DWORD size = (DWORD)strlen(value) + 1u;
    LONG result = RegSetValueExA(key, value_name, 0, REG_SZ, (const BYTE*)value, size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

static const char* rgi_thumbnail_clsid_string(void)
{
    return "{D5E3F7C6-6C7A-4E3E-8F6C-3B2E1C74D9A4}";
}

static int register_rgi_file_association(void)
{
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
    if (len == 0 || len >= (DWORD)sizeof(exe_path))
    {
        return 0;
    }

    char command[1024];
    (void)rg_snprintf(command, sizeof(command), "\"%s\" \"%%1\"", exe_path);

    int ok = 1;
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\.rgi", NULL, "rgi_auto_file");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\.rgi", "PerceivedType", "image");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\.rgi", "Content Type", "image/rgi");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\rgi_auto_file", NULL, "RGI Image");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\rgi_auto_file\\DefaultIcon", NULL,
                              "%SystemRoot%\\System32\\imageres.dll,-70");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\rgi_auto_file\\shell\\open\\command", NULL,
                              command);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

static int unregister_rgi_file_association(void)
{
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.rgi");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\rgi_auto_file");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return 1;
}

static int register_rgi_thumbnail_provider(void)
{
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, (DWORD)sizeof(exe_path));
    if (len == 0 || len >= (DWORD)sizeof(exe_path))
    {
        return 0;
    }

    char* dir = path_dirname_alloc(exe_path);
    if (dir == NULL)
    {
        return 0;
    }

    char* dll_path = path_join_alloc(dir, "rgi_thumbnail.dll");
    free(dir);
    if (dll_path == NULL)
    {
        return 0;
    }

    DWORD attrs = GetFileAttributesA(dll_path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        free(dll_path);
        return 0;
    }

    char clsid_key[256];
    char inproc_key[256];
    const char* clsid = rgi_thumbnail_clsid_string();
    (void)rg_snprintf(clsid_key, sizeof(clsid_key), "Software\\Classes\\CLSID\\%s", clsid);
    (void)rg_snprintf(inproc_key, sizeof(inproc_key), "%s\\InprocServer32", clsid_key);
    char rgi_ext_shellex[256];
    char rgi_sys_shellex[256];
    (void)rg_snprintf(rgi_ext_shellex, sizeof(rgi_ext_shellex),
                      "Software\\Classes\\.rgi\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    (void)rg_snprintf(rgi_sys_shellex, sizeof(rgi_sys_shellex),
                      "Software\\Classes\\SystemFileAssociations\\.rgi\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");

    int ok = 1;
    ok &= set_registry_string(HKEY_CURRENT_USER, clsid_key, NULL, "RGI Thumbnail Provider");
    ok &= set_registry_string(HKEY_CURRENT_USER, inproc_key, NULL, dll_path);
    ok &= set_registry_string(HKEY_CURRENT_USER, inproc_key, "ThreadingModel", "Apartment");
    ok &= set_registry_string(HKEY_CURRENT_USER,
                              "Software\\Classes\\rgi_auto_file\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}",
                              NULL, clsid);
    ok &= set_registry_string(HKEY_CURRENT_USER, rgi_ext_shellex, NULL, clsid);
    ok &= set_registry_string(HKEY_CURRENT_USER, rgi_sys_shellex, NULL, clsid);
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\SystemFileAssociations\\.rgi", "PerceivedType", "image");
    ok &= set_registry_string(HKEY_CURRENT_USER, "Software\\Classes\\SystemFileAssociations\\.rgi", "Content Type", "image/rgi");

    free(dll_path);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return ok;
}

static int unregister_rgi_thumbnail_provider(void)
{
    char clsid_key[256];
    const char* clsid = rgi_thumbnail_clsid_string();
    (void)rg_snprintf(clsid_key, sizeof(clsid_key), "Software\\Classes\\CLSID\\%s", clsid);
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\rgi_auto_file\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.rgi\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\SystemFileAssociations\\.rgi\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}");
    RegDeleteTreeA(HKEY_CURRENT_USER, clsid_key);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return 1;
}
#endif

typedef struct RgiFileList
{
    char** items;
    size_t count;
    size_t capacity;
    size_t current;
} RgiFileList;

typedef struct OverlayCache
{
    char line1[256];
    char line2[256];
    char line3[256];
    size_t len1;
    size_t len2;
    size_t len3;
    size_t max_len;
    int box_w;
    int box_h;
    int line_height;
} OverlayCache;

static void file_list_reset(RgiFileList* list)
{
    if (list == NULL)
    {
        return;
    }

    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
    list->current = 0u;
}

static int file_list_push(RgiFileList* list, char* path)
{
    if (list->count == list->capacity)
    {
        size_t new_cap = list->capacity == 0u ? 16u : list->capacity * 2u;
        char** items = (char**)realloc(list->items, new_cap * sizeof(*items));
        if (items == NULL)
        {
            free(path);
            return 0;
        }
        list->items = items;
        list->capacity = new_cap;
    }

    list->items[list->count++] = path;
    return 1;
}

static int file_list_compare(const void* a, const void* b)
{
    const char* path_a = *(const char* const*)a;
    const char* path_b = *(const char* const*)b;
    const char* name_a = path_basename(path_a);
    const char* name_b = path_basename(path_b);
#if RG_PLATFORM_WINDOWS
    return ascii_stricmp(name_a, name_b);
#else
    return strcmp(name_a, name_b);
#endif
}

static int file_list_build(RgiFileList* list, const char* path)
{
    file_list_reset(list);

    char* dir = path_dirname_alloc(path);
    if (dir == NULL)
    {
        return 0;
    }

#if RG_PLATFORM_WINDOWS
    {
        char* pattern = path_join_alloc(dir, "*");
        if (pattern)
        {
            WIN32_FIND_DATAA data;
            HANDLE handle = FindFirstFileA(pattern, &data);
            free(pattern);
            if (handle != INVALID_HANDLE_VALUE)
            {
                do
                {
                    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    {
                        continue;
                    }
                    if (!path_is_rgi_extension(data.cFileName))
                    {
                        continue;
                    }
                    char* full_path = path_join_alloc(dir, data.cFileName);
                    if (full_path)
                    {
                        file_list_push(list, full_path);
                    }
                } while (FindNextFileA(handle, &data));
                FindClose(handle);
            }
        }
    }
#else
    {
        DIR* dir_handle = opendir(dir);
        if (dir_handle)
        {
            struct dirent* entry = NULL;
            while ((entry = readdir(dir_handle)) != NULL)
            {
                if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                {
                    continue;
                }
                if (!path_is_rgi_extension(entry->d_name))
                {
                    continue;
                }
                char* full_path = path_join_alloc(dir, entry->d_name);
                if (full_path)
                {
                    file_list_push(list, full_path);
                }
            }
            closedir(dir_handle);
        }
    }
#endif

    free(dir);

    if (list->count == 0u)
    {
        char* copy = string_duplicate(path);
        if (copy == NULL)
        {
            return 0;
        }
        file_list_push(list, copy);
        list->current = 0u;
        return 1;
    }

    qsort(list->items, list->count, sizeof(*list->items), file_list_compare);

    const char* target_name = path_basename(path);
    list->current = 0u;
    for (size_t i = 0; i < list->count; i++)
    {
        const char* name = path_basename(list->items[i]);
#if RG_PLATFORM_WINDOWS
        if (ascii_stricmp(name, target_name) == 0)
#else
        if (strcmp(name, target_name) == 0)
#endif
        {
            list->current = i;
            break;
        }
    }

    return 1;
}

static size_t file_list_offset_index(const RgiFileList* list, int delta)
{
    if (list->count == 0u)
    {
        return 0u;
    }

    if (delta > 0)
    {
        return (list->current + 1u) % list->count;
    }
    if (delta < 0)
    {
        return (list->current + list->count - 1u) % list->count;
    }
    return list->current;
}

static void build_window_title(char* buffer, size_t buffer_size, const char* path)
{
    const char* basename = path_basename(path);
    (void)rg_snprintf(buffer, buffer_size, "rgi_viewer - %s", basename);
}

static SDL_Texture* decode_rgi_to_texture(SDL_Renderer* renderer,
                                          const uint8_t* data,
                                          size_t data_size,
                                          uint32_t width,
                                          uint32_t height)
{
    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 4u))
    {
        return NULL;
    }

    size_t out_size = (size_t)pixel_count * 4u;
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                             (int)width, (int)height);
    if (texture == NULL)
    {
        return NULL;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    void* pixels = NULL;
    int pitch = 0;
    if (!SDL_LockTexture(texture, NULL, &pixels, &pitch))
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    if (pitch < (int)(width * 4u))
    {
        SDL_UnlockTexture(texture);
        SDL_DestroyTexture(texture);
        return NULL;
    }

    uint32_t decode_w = width;
    uint32_t decode_h = height;
    size_t written = 0u;
    if (pitch == (int)(width * 4u))
    {
        written = rg_rgi_decode(data, data_size, pixels, out_size, &decode_w, &decode_h);
    }
    else
    {
        uint8_t* temp = (uint8_t*)malloc(out_size);
        if (temp == NULL)
        {
            SDL_UnlockTexture(texture);
            SDL_DestroyTexture(texture);
            return NULL;
        }

        written = rg_rgi_decode(data, data_size, temp, out_size, &decode_w, &decode_h);
        if (written != 0u)
        {
            const uint8_t* src = temp;
            uint8_t* dst = (uint8_t*)pixels;
            size_t row_bytes = (size_t)width * 4u;
            for (uint32_t y = 0; y < height; y++)
            {
                memcpy(dst, src, row_bytes);
                src += row_bytes;
                dst += pitch;
            }
        }
        free(temp);
    }

    SDL_UnlockTexture(texture);

    if (written == 0u || decode_w != width || decode_h != height)
    {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    return texture;
}

static SDL_Texture* load_rgi_to_texture(SDL_Renderer* renderer,
                                        const char* path,
                                        uint32_t* out_width,
                                        uint32_t* out_height)
{
    uint8_t* data = NULL;
    size_t data_size = 0u;
    if (!read_file(path, &data, &data_size))
    {
        return NULL;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!rg_rgi_read_header(data, data_size, &width, &height))
    {
        free(data);
        return NULL;
    }

    SDL_Texture* texture = decode_rgi_to_texture(renderer, data, data_size, width, height);
    free(data);
    if (texture == NULL)
    {
        return NULL;
    }

    if (out_width)
    {
        *out_width = width;
    }
    if (out_height)
    {
        *out_height = height;
    }

    return texture;
}

static float compute_fit_scale(int output_w, int output_h, uint32_t img_w, uint32_t img_h)
{
    if (output_w <= 0 || output_h <= 0 || img_w == 0u || img_h == 0u)
    {
        return 1.0f;
    }

    float scale_x = (float)output_w / (float)img_w;
    float scale_y = (float)output_h / (float)img_h;
    return scale_x < scale_y ? scale_x : scale_y;
}

static float compute_min_scale(int output_w, int output_h, uint32_t img_w, uint32_t img_h)
{
    float fit_scale = compute_fit_scale(output_w, output_h, img_w, img_h);
    return fit_scale < 1.0f ? fit_scale : 1.0f;
}

static void reset_view(SDL_Renderer* renderer,
                       uint32_t img_w,
                       uint32_t img_h,
                       float* zoom,
                       float* pan_x,
                       float* pan_y)
{
    int output_w = 0;
    int output_h = 0;
    SDL_GetRenderOutputSize(renderer, &output_w, &output_h);

    *zoom = compute_min_scale(output_w, output_h, img_w, img_h);
    *pan_x = 0.0f;
    *pan_y = 0.0f;
}

static int reload_image(SDL_Renderer* renderer,
                        SDL_Window* window,
                        const char* path,
                        SDL_Texture** texture,
                        uint32_t* width,
                        uint32_t* height,
                        uint64_t* file_size,
                        float* zoom,
                        float* pan_x,
                        float* pan_y,
                        char* title,
                        size_t title_size,
                        int resize_window)
{
    uint32_t new_w = 0;
    uint32_t new_h = 0;
    SDL_Texture* new_texture = load_rgi_to_texture(renderer, path, &new_w, &new_h);
    if (new_texture == NULL)
    {
        return 0;
    }

    if (*texture)
    {
        SDL_DestroyTexture(*texture);
    }
    *texture = new_texture;
    *width = new_w;
    *height = new_h;

    build_window_title(title, title_size, path);
    SDL_SetWindowTitle(window, title);

    if (resize_window)
    {
        int window_w = 0;
        int window_h = 0;
        choose_window_size(*width, *height, &window_w, &window_h);
        SDL_SetWindowSize(window, window_w, window_h);
    }

    if (file_size)
    {
        *file_size = file_size_bytes(path);
    }

    reset_view(renderer, *width, *height, zoom, pan_x, pan_y);
    return 1;
}

static uint64_t file_timestamp_ns(const char* path)
{
#if RG_PLATFORM_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
    {
        return 0u;
    }

    ULARGE_INTEGER ticks;
    ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
    ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
    return (uint64_t)ticks.QuadPart * 100u;
#else
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return 0u;
    }
    return (uint64_t)st.st_mtime * 1000000000ull;
#endif
}

static int should_hot_reload(uint64_t now_ns,
                             uint64_t* last_check_ns,
                             uint64_t* last_stamp_ns,
                             const char* path)
{
    const uint64_t kIntervalNs = 250000000ull;
    if (now_ns < *last_check_ns + kIntervalNs)
    {
        return 0;
    }

    *last_check_ns = now_ns;
    uint64_t stamp = file_timestamp_ns(path);
    if (stamp != 0u && stamp != *last_stamp_ns)
    {
        *last_stamp_ns = stamp;
        return 1;
    }
    return 0;
}

static uint64_t file_size_bytes(const char* path)
{
#if RG_PLATFORM_WINDOWS
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data))
    {
        return 0u;
    }

    ULARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return (uint64_t)size.QuadPart;
#else
    struct stat st;
    if (stat(path, &st) != 0)
    {
        return 0u;
    }
    return (uint64_t)st.st_size;
#endif
}

static void format_size(char* buffer, size_t buffer_size, uint64_t bytes)
{
    const double kb = 1024.0;
    const double mb = 1024.0 * 1024.0;
    const double gb = 1024.0 * 1024.0 * 1024.0;

    if (bytes >= (uint64_t)gb)
    {
        (void)rg_snprintf(buffer, buffer_size, "%.2f GB", (double)bytes / gb);
    }
    else if (bytes >= (uint64_t)mb)
    {
        (void)rg_snprintf(buffer, buffer_size, "%.2f MB", (double)bytes / mb);
    }
    else if (bytes >= (uint64_t)kb)
    {
        (void)rg_snprintf(buffer, buffer_size, "%.2f KB", (double)bytes / kb);
    }
    else
    {
        (void)rg_snprintf(buffer, buffer_size, "%llu B", (unsigned long long)bytes);
    }
}

static void overlay_update(OverlayCache* cache,
                           const char* path,
                           size_t index,
                           size_t count,
                           uint32_t width,
                           uint32_t height,
                           float zoom,
                           uint64_t file_size)
{
    const int font = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
    const int line_gap = 2;
    const int pad = 6;

    char size_text[64];
    format_size(size_text, sizeof(size_text), file_size);

    size_t total = count > 0u ? count : 1u;
    size_t current = count > 0u ? index + 1u : 1u;
    int zoom_pct = (int)lrintf(zoom * 100.0f);

    (void)rg_snprintf(cache->line1, sizeof(cache->line1), "File: %s (%zu/%zu)", path_basename(path), current, total);
    (void)rg_snprintf(cache->line2, sizeof(cache->line2), "Size: %ux%u  Zoom: %d%%", width, height, zoom_pct);
    (void)rg_snprintf(cache->line3, sizeof(cache->line3), "Bytes: %s", size_text);

    cache->len1 = strlen(cache->line1);
    cache->len2 = strlen(cache->line2);
    cache->len3 = strlen(cache->line3);
    cache->max_len = cache->len1;
    if (cache->len2 > cache->max_len)
    {
        cache->max_len = cache->len2;
    }
    if (cache->len3 > cache->max_len)
    {
        cache->max_len = cache->len3;
    }

    cache->line_height = font + line_gap;
    cache->box_w = (int)(cache->max_len * (size_t)font) + pad * 2;
    cache->box_h = cache->line_height * 3 - line_gap + pad * 2;
}

static void overlay_draw(const OverlayCache* cache, SDL_Renderer* renderer, int x, int y)
{
    const int pad = 6;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 15, 15, 15, 200);

    SDL_FRect box;
    box.x = (float)x;
    box.y = (float)y;
    box.w = (float)cache->box_w;
    box.h = (float)cache->box_h;
    SDL_RenderFillRect(renderer, &box);

    SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
    SDL_RenderDebugText(renderer, (float)(x + pad), (float)(y + pad), cache->line1);
    SDL_RenderDebugText(renderer, (float)(x + pad),
                        (float)(y + pad + cache->line_height), cache->line2);
    SDL_RenderDebugText(renderer, (float)(x + pad),
                        (float)(y + pad + cache->line_height * 2), cache->line3);
}

static void choose_window_size(uint32_t img_w, uint32_t img_h, int* out_w, int* out_h)
{
    const int padding = 96;
    const int min_w = 480;
    const int min_h = 360;
    int max_w = 1280;
    int max_h = 720;

    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds;
    if (display != 0 && SDL_GetDisplayBounds(display, &bounds))
    {
        max_w = (int)((float)bounds.w * 0.85f);
        max_h = (int)((float)bounds.h * 0.85f);
        if (max_w < min_w)
        {
            max_w = min_w;
        }
        if (max_h < min_h)
        {
            max_h = min_h;
        }
    }

    int w = (int)img_w + padding * 2;
    int h = (int)img_h + padding * 2;

    if (w < min_w)
    {
        w = min_w;
    }
    if (h < min_h)
    {
        h = min_h;
    }

    if (w > max_w || h > max_h)
    {
        float scale_x = (float)max_w / (float)w;
        float scale_y = (float)max_h / (float)h;
        float scale = scale_x < scale_y ? scale_x : scale_y;
        w = (int)((float)w * scale);
        h = (int)((float)h * scale);
    }

    if (w < 1)
    {
        w = 1;
    }
    if (h < 1)
    {
        h = 1;
    }

    *out_w = w;
    *out_h = h;
}

int main(int argc, char** argv)
{
    int want_register = 0;
    int want_unregister = 0;
    int want_register_thumbnail = 0;
    int want_unregister_thumbnail = 0;
    char* path = NULL;
    if (argc >= 2)
    {
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            {
                print_usage(argv[0]);
                return 0;
            }
            if (strcmp(argv[i], "--register") == 0)
            {
                want_register = 1;
                continue;
            }
            if (strcmp(argv[i], "--unregister") == 0)
            {
                want_unregister = 1;
                continue;
            }
            if (strcmp(argv[i], "--register-thumbnail") == 0)
            {
                want_register_thumbnail = 1;
                continue;
            }
            if (strcmp(argv[i], "--register-all") == 0)
            {
                want_register = 1;
                want_register_thumbnail = 1;
                continue;
            }
            if (strcmp(argv[i], "--unregister-thumbnail") == 0)
            {
                want_unregister_thumbnail = 1;
                continue;
            }
            if (strcmp(argv[i], "--unregister-all") == 0)
            {
                want_unregister = 1;
                want_unregister_thumbnail = 1;
                continue;
            }
            if (path == NULL)
            {
                path = string_duplicate(argv[i]);
                if (path == NULL)
                {
                    fprintf(stderr, "Failed to allocate path.\n");
                    return 1;
                }
            }
        }
    }

    if (want_register || want_unregister || want_register_thumbnail || want_unregister_thumbnail)
    {
#if RG_PLATFORM_WINDOWS
        int ok = 1;
        if ((want_register && want_unregister) ||
            (want_register_thumbnail && want_unregister_thumbnail))
        {
            fprintf(stderr, "Conflicting register/unregister options.\n");
            ok = 0;
        }
        if (ok && want_register)
        {
            ok &= register_rgi_file_association();
        }
        if (ok && want_unregister)
        {
            ok &= unregister_rgi_file_association();
        }
        if (ok && want_register_thumbnail)
        {
            ok &= register_rgi_thumbnail_provider();
        }
        if (ok && want_unregister_thumbnail)
        {
            ok &= unregister_rgi_thumbnail_provider();
        }
        return ok ? 0 : 1;
#else
        fprintf(stderr, "Registration options are only supported on Windows.\n");
        free(path);
        return 1;
#endif
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        free(path);
        return 1;
    }

    char title[256];
    build_window_title(title, sizeof(title), path);

    int window_w = 0;
    int window_h = 0;
    choose_window_size(640u, 360u, &window_w, &window_h);

    SDL_Window* window = NULL;
    SDL_Renderer* renderer = NULL;
    if (!SDL_CreateWindowAndRenderer(title, window_w, window_h,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &window, &renderer))
    {
        fprintf(stderr, "SDL_CreateWindowAndRenderer failed: %s\n", SDL_GetError());
        SDL_Quit();
        free(path);
        return 1;
    }

    SDL_SetRenderVSync(renderer, 1);

    SDL_Texture* texture = NULL;
    uint32_t width = 0;
    uint32_t height = 0;

    RgiFileList files = {0};
    uint64_t file_size = 0u;
    uint64_t last_check_ns = 0u;
    uint64_t last_stamp_ns = 0u;

    float zoom = 1.0f;
    float pan_x = 0.0f;
    float pan_y = 0.0f;

    if (path)
    {
        if (reload_image(renderer, window, path, &texture, &width, &height, &file_size,
                         &zoom, &pan_x, &pan_y, title, sizeof(title), 1))
        {
            file_list_build(&files, path);
            last_stamp_ns = file_timestamp_ns(path);
        }
        else
        {
            fprintf(stderr, "Failed to load %s\n", path);
            build_window_title(title, sizeof(title), NULL);
            SDL_SetWindowTitle(window, title);
            free(path);
            path = NULL;
        }
    }

    int show_info = 1;
    int fullscreen = 0;
    SDL_WindowID window_id = SDL_GetWindowID(window);
    int output_w = 0;
    int output_h = 0;
    int output_dirty = 1;
    SDL_GetRenderOutputSize(renderer, &output_w, &output_h);
    output_dirty = 0;

    if (texture == NULL)
    {
        zoom = 1.0f;
        pan_x = 0.0f;
        pan_y = 0.0f;
    }
    float last_zoom = zoom;

    OverlayCache overlay = {0};
    int overlay_dirty = 1;

    const float max_scale = 32.0f;
    int panning = 0;
    char* pending_drop = NULL;
    int running = 1;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = 0;
            }
            else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            {
                running = 0;
            }
            else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
            {
                if (event.window.windowID == window_id)
                {
                    output_w = event.window.data1;
                    output_h = event.window.data2;
                    output_dirty = 0;
                }
            }
            else if (event.type == SDL_EVENT_WINDOW_RESIZED ||
                     event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
                     event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
                     event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
            {
                if (event.window.windowID == window_id)
                {
                    output_dirty = 1;
                    if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN)
                    {
                        fullscreen = 1;
                    }
                    else if (event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN)
                    {
                        fullscreen = 0;
                    }
                }
            }
            else if (event.type == SDL_EVENT_KEY_DOWN)
            {
                if (event.key.key == SDLK_ESCAPE)
                {
                    running = 0;
                }
                else if (event.key.key == SDLK_I)
                {
                    show_info = !show_info;
                    if (show_info)
                    {
                        overlay_dirty = 1;
                    }
                }
                else if (event.key.key == SDLK_F11)
                {
                    int desired = fullscreen ? 0 : 1;
                    if (!SDL_SetWindowFullscreen(window, desired != 0))
                    {
                        fprintf(stderr, "Failed to toggle fullscreen: %s\n", SDL_GetError());
                    }
                    else
                    {
                        fullscreen = desired;
                    }
                    output_dirty = 1;
                }
                else if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT)
                {
                    int delta = (event.key.key == SDLK_RIGHT) ? 1 : -1;
                    size_t next_index = file_list_offset_index(&files, delta);
                    if (files.count > 0u && next_index != files.current)
                    {
                        const char* next_path = files.items[next_index];
                        if (reload_image(renderer, window, next_path, &texture, &width, &height, &file_size,
                                         &zoom, &pan_x, &pan_y, title, sizeof(title), 0))
                        {
                            files.current = next_index;
                            free(path);
                            path = string_duplicate(next_path);
                            last_stamp_ns = file_timestamp_ns(path);
                            overlay_dirty = 1;
                            last_zoom = zoom;
                        }
                        else
                        {
                            fprintf(stderr, "Failed to load %s\n", next_path);
                        }
                    }
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            {
                if (event.button.button == SDL_BUTTON_MIDDLE)
                {
                    panning = 1;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            {
                if (event.button.button == SDL_BUTTON_MIDDLE)
                {
                    panning = 0;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_MOTION)
            {
                if (panning)
                {
                    pan_x += event.motion.xrel;
                    pan_y += event.motion.yrel;
                }
            }
            else if (event.type == SDL_EVENT_MOUSE_WHEEL)
            {
                float wheel_y = event.wheel.y;
                if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                {
                    wheel_y = -wheel_y;
                }

                if (wheel_y != 0.0f)
                {
                    if (output_dirty)
                    {
                        SDL_GetRenderOutputSize(renderer, &output_w, &output_h);
                        output_dirty = 0;
                    }
                    if (output_w > 0 && output_h > 0)
                    {
                        float min_scale = compute_min_scale(output_w, output_h, width, height);
                        float zoom_factor = powf(1.1f, wheel_y);
                        float new_zoom = zoom * zoom_factor;
                        if (new_zoom < min_scale)
                        {
                            new_zoom = min_scale;
                        }
                        if (new_zoom > max_scale)
                        {
                            new_zoom = max_scale;
                        }

                        if (new_zoom != zoom)
                        {
                            float base_old_x = ((float)output_w - (float)width * zoom) * 0.5f;
                            float base_old_y = ((float)output_h - (float)height * zoom) * 0.5f;
                            float anchor_x = (float)output_w * 0.5f;
                            float anchor_y = (float)output_h * 0.5f;
                            float img_x = (anchor_x - base_old_x - pan_x) / zoom;
                            float img_y = (anchor_y - base_old_y - pan_y) / zoom;
                            float base_new_x = ((float)output_w - (float)width * new_zoom) * 0.5f;
                            float base_new_y = ((float)output_h - (float)height * new_zoom) * 0.5f;
                            pan_x = anchor_x - base_new_x - img_x * new_zoom;
                            pan_y = anchor_y - base_new_y - img_y * new_zoom;
                            zoom = new_zoom;
                            overlay_dirty = 1;
                        }
                    }
                }
            }
            else if (event.type == SDL_EVENT_DROP_FILE)
            {
                const char* drop_path = event.drop.data;
                if (drop_path && drop_path[0] != '\0')
                {
                    if (path_is_rgi_extension(drop_path))
                    {
                        char* copy = string_duplicate(drop_path);
                        if (copy != NULL)
                        {
                            free(pending_drop);
                            pending_drop = copy;
                        }
                    }
                }
            }
        }

        if (output_dirty)
        {
            SDL_GetRenderOutputSize(renderer, &output_w, &output_h);
            output_dirty = 0;
        }
        if (output_w <= 0 || output_h <= 0)
        {
            SDL_Delay(1);
            continue;
        }

        if (pending_drop)
        {
            int same_path = path != NULL && path_equals(pending_drop, path);
            if (!same_path &&
                reload_image(renderer, window, pending_drop, &texture, &width, &height, &file_size,
                             &zoom, &pan_x, &pan_y, title, sizeof(title), fullscreen ? 0 : 1))
            {
                file_list_build(&files, pending_drop);
                free(path);
                path = pending_drop;
                pending_drop = NULL;
                last_stamp_ns = file_timestamp_ns(path);
                last_check_ns = 0u;
                overlay_dirty = 1;
                last_zoom = zoom;
                output_dirty = 1;
            }
            else
            {
                if (!same_path)
                {
                    fprintf(stderr, "Failed to load %s\n", pending_drop);
                }
            }

            if (pending_drop)
            {
                free(pending_drop);
                pending_drop = NULL;
            }
        }

        uint64_t now_ns = SDL_GetTicksNS();
        if (path && should_hot_reload(now_ns, &last_check_ns, &last_stamp_ns, path))
        {
            if (reload_image(renderer, window, path, &texture, &width, &height, &file_size,
                             &zoom, &pan_x, &pan_y, title, sizeof(title), 0))
            {
                overlay_dirty = 1;
                last_zoom = zoom;
            }
        }

        float min_scale = compute_min_scale(output_w, output_h, width, height);
        if (zoom < min_scale)
        {
            zoom = min_scale;
        }
        if (zoom > max_scale)
        {
            zoom = max_scale;
        }
        if (fabsf(zoom - last_zoom) > 0.0001f)
        {
            overlay_dirty = 1;
            last_zoom = zoom;
        }
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
        SDL_RenderClear(renderer);
        if (texture)
        {
            float scale = zoom;
            float dst_w = (float)width * scale;
            float dst_h = (float)height * scale;
            SDL_FRect dst;
            dst.x = ((float)output_w - dst_w) * 0.5f + pan_x;
            dst.y = ((float)output_h - dst_h) * 0.5f + pan_y;
            dst.w = dst_w;
            dst.h = dst_h;
            SDL_RenderTexture(renderer, texture, NULL, &dst);
        }

        if (show_info)
        {
            if (overlay_dirty)
            {
                overlay_update(&overlay, path, files.current, files.count, width, height, zoom, file_size);
                overlay_dirty = 0;
            }
            overlay_draw(&overlay, renderer, 8, 8);
        }

        SDL_RenderPresent(renderer);
    }

    if (texture)
    {
        SDL_DestroyTexture(texture);
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    file_list_reset(&files);
    free(pending_drop);
    free(path);
    return 0;
}
