// rgi_thumbnail_provider - Windows Explorer thumbnail provider for .rgi

#include <rg_rgi.h>

#if !RG_PLATFORM_WINDOWS
    #error rgi_thumbnail_provider is Windows-only.
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <thumbcache.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


#define RGI_CONTAINER_OF(ptr, type, member) ((type*)((char*)(ptr)-offsetof(type, member)))

static const CLSID CLSID_RgiThumbnailProvider = {0xd5e3f7c6, 0x6c7a, 0x4e3e, {0x8f, 0x6c, 0x3b, 0x2e, 0x1c, 0x74, 0xd9, 0xa4}};

static LONG g_object_count = 0;
static LONG g_lock_count = 0;

typedef struct RgiThumbnailProvider
{
    IThumbnailProvider thumbnail_iface;
    IInitializeWithStream stream_iface;
    LONG ref_count;
    IStream* stream;
} RgiThumbnailProvider;

typedef struct RgiClassFactory
{
    IClassFactory iface;
    LONG ref_count;
} RgiClassFactory;

static HRESULT rgi_provider_query_interface(RgiThumbnailProvider* provider, REFIID riid, void** ppv);
static ULONG rgi_provider_addref(RgiThumbnailProvider* provider);
static ULONG rgi_provider_release(RgiThumbnailProvider* provider);

static HRESULT STDMETHODCALLTYPE rgi_thumbnail_query_interface(IThumbnailProvider* self, REFIID riid, void** ppv);
static ULONG STDMETHODCALLTYPE rgi_thumbnail_addref(IThumbnailProvider* self);
static ULONG STDMETHODCALLTYPE rgi_thumbnail_release(IThumbnailProvider* self);
static HRESULT STDMETHODCALLTYPE rgi_thumbnail_get_thumbnail(IThumbnailProvider* self, UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha);

static HRESULT STDMETHODCALLTYPE rgi_stream_query_interface(IInitializeWithStream* self, REFIID riid, void** ppv);
static ULONG STDMETHODCALLTYPE rgi_stream_addref(IInitializeWithStream* self);
static ULONG STDMETHODCALLTYPE rgi_stream_release(IInitializeWithStream* self);
static HRESULT STDMETHODCALLTYPE rgi_stream_initialize(IInitializeWithStream* self, IStream* stream, DWORD grfMode);

static HRESULT STDMETHODCALLTYPE rgi_class_query_interface(IClassFactory* self, REFIID riid, void** ppv);
static ULONG STDMETHODCALLTYPE rgi_class_addref(IClassFactory* self);
static ULONG STDMETHODCALLTYPE rgi_class_release(IClassFactory* self);
static HRESULT STDMETHODCALLTYPE rgi_class_create_instance(IClassFactory* self, IUnknown* outer, REFIID riid, void** ppv);
static HRESULT STDMETHODCALLTYPE rgi_class_lock_server(IClassFactory* self, BOOL lock);

static IThumbnailProviderVtbl g_thumbnail_vtbl =
{
    rgi_thumbnail_query_interface,
    rgi_thumbnail_addref,
    rgi_thumbnail_release,
    rgi_thumbnail_get_thumbnail
};

static IInitializeWithStreamVtbl g_stream_vtbl =
{
    rgi_stream_query_interface,
    rgi_stream_addref,
    rgi_stream_release,
    rgi_stream_initialize
};

static IClassFactoryVtbl g_class_vtbl =
{
    rgi_class_query_interface,
    rgi_class_addref,
    rgi_class_release,
    rgi_class_create_instance,
    rgi_class_lock_server
};

static int rgi_read_stream(IStream* stream, uint8_t** out_data, size_t* out_size)
{
    if (stream == NULL || out_data == NULL || out_size == NULL)
    {
        return 0;
    }

    STATSTG stat;
    if (FAILED(stream->lpVtbl->Stat(stream, &stat, STATFLAG_NONAME)))
    {
        return 0;
    }

    if (stat.cbSize.QuadPart == 0 || stat.cbSize.QuadPart > SIZE_MAX || stat.cbSize.QuadPart > ULONG_MAX)
    {
        return 0;
    }

    size_t size = (size_t)stat.cbSize.QuadPart;
    uint8_t* data = (uint8_t*)malloc(size);
    if (data == NULL)
    {
        return 0;
    }

    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    (void)stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, NULL);

    ULONG read = 0;
    HRESULT hr = stream->lpVtbl->Read(stream, data, (ULONG)size, &read);
    if (FAILED(hr) || read != (ULONG)size)
    {
        free(data);
        return 0;
    }

    *out_data = data;
    *out_size = size;
    return 1;
}

static void rgi_compute_thumb_size(uint32_t src_w, uint32_t src_h, UINT max_dim, uint32_t* out_w, uint32_t* out_h)
{
    if (max_dim == 0u)
    {
        max_dim = 256u;
    }

    uint32_t w = src_w;
    uint32_t h = src_h;
    if (src_w > max_dim || src_h > max_dim)
    {
        if (src_w >= src_h)
        {
            w = (uint32_t)max_dim;
            h = (uint32_t)((uint64_t)src_h * max_dim / src_w);
            if (h == 0u)
            {
                h = 1u;
            }
        }
        else
        {
            h = (uint32_t)max_dim;
            w = (uint32_t)((uint64_t)src_w * max_dim / src_h);
            if (w == 0u)
            {
                w = 1u;
            }
        }
    }

    *out_w = w;
    *out_h = h;
}

static void rgi_scale_to_bgra(uint8_t* dst,
                              uint32_t dst_w,
                              uint32_t dst_h,
                              const uint8_t* src,
                              uint32_t src_w,
                              uint32_t src_h)
{
    for (uint32_t y = 0; y < dst_h; y++)
    {
        uint32_t src_y = (uint32_t)((uint64_t)y * src_h / dst_h);
        const uint8_t* src_row = src + (size_t)src_y * src_w * 4u;
        uint8_t* dst_row = dst + (size_t)y * dst_w * 4u;
        for (uint32_t x = 0; x < dst_w; x++)
        {
            uint32_t src_x = (uint32_t)((uint64_t)x * src_w / dst_w);
            const uint8_t* sp = src_row + src_x * 4u;
            uint8_t* dp = dst_row + x * 4u;
            dp[0] = sp[2];
            dp[1] = sp[1];
            dp[2] = sp[0];
            dp[3] = sp[3];
        }
    }
}

static HRESULT rgi_provider_query_interface(RgiThumbnailProvider* provider, REFIID riid, void** ppv)
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IThumbnailProvider))
    {
        *ppv = &provider->thumbnail_iface;
    }
    else if (IsEqualIID(riid, &IID_IInitializeWithStream))
    {
        *ppv = &provider->stream_iface;
    }
    else
    {
        return E_NOINTERFACE;
    }

    rgi_provider_addref(provider);
    return S_OK;
}

static ULONG rgi_provider_addref(RgiThumbnailProvider* provider)
{
    return (ULONG)InterlockedIncrement(&provider->ref_count);
}

static ULONG rgi_provider_release(RgiThumbnailProvider* provider)
{
    LONG ref = InterlockedDecrement(&provider->ref_count);
    if (ref == 0)
    {
        if (provider->stream)
        {
            provider->stream->lpVtbl->Release(provider->stream);
            provider->stream = NULL;
        }
        CoTaskMemFree(provider);
        InterlockedDecrement(&g_object_count);
    }
    return (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE rgi_thumbnail_query_interface(IThumbnailProvider* self, REFIID riid, void** ppv)
{
    return rgi_provider_query_interface((RgiThumbnailProvider*)self, riid, ppv);
}

static ULONG STDMETHODCALLTYPE rgi_thumbnail_addref(IThumbnailProvider* self)
{
    return rgi_provider_addref((RgiThumbnailProvider*)self);
}

static ULONG STDMETHODCALLTYPE rgi_thumbnail_release(IThumbnailProvider* self)
{
    return rgi_provider_release((RgiThumbnailProvider*)self);
}

static HRESULT STDMETHODCALLTYPE rgi_thumbnail_get_thumbnail(IThumbnailProvider* self,
                                                             UINT cx,
                                                             HBITMAP* phbmp,
                                                             WTS_ALPHATYPE* pdwAlpha)
{
    if (phbmp == NULL || pdwAlpha == NULL)
    {
        return E_POINTER;
    }

    *phbmp = NULL;
    *pdwAlpha = WTSAT_UNKNOWN;

    RgiThumbnailProvider* provider = (RgiThumbnailProvider*)self;
    if (provider->stream == NULL)
    {
        return E_FAIL;
    }

    uint8_t* data = NULL;
    size_t data_size = 0u;
    if (!rgi_read_stream(provider->stream, &data, &data_size))
    {
        return E_FAIL;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!rg_rgi_read_header(data, data_size, &width, &height))
    {
        free(data);
        return E_FAIL;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 4u))
    {
        free(data);
        return E_FAIL;
    }

    size_t rgba_size = (size_t)pixel_count * 4u;
    uint8_t* rgba = (uint8_t*)malloc(rgba_size);
    if (rgba == NULL)
    {
        free(data);
        return E_OUTOFMEMORY;
    }

    uint32_t decode_w = width;
    uint32_t decode_h = height;
    if (rg_rgi_decode(data, data_size, rgba, rgba_size, &decode_w, &decode_h) == 0u ||
        decode_w != width || decode_h != height)
    {
        free(rgba);
        free(data);
        return E_FAIL;
    }
    free(data);

    uint32_t out_w = 0;
    uint32_t out_h = 0;
    rgi_compute_thumb_size(width, height, cx, &out_w, &out_h);

    BITMAPINFO bmi;
    ZeroMemory(&bmi, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = (LONG)out_w;
    bmi.bmiHeader.biHeight = -(LONG)out_h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (bitmap == NULL || bits == NULL)
    {
        free(rgba);
        return E_FAIL;
    }

    rgi_scale_to_bgra((uint8_t*)bits, out_w, out_h, rgba, width, height);
    free(rgba);

    *phbmp = bitmap;
    *pdwAlpha = WTSAT_ARGB;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rgi_stream_query_interface(IInitializeWithStream* self, REFIID riid, void** ppv)
{
    RgiThumbnailProvider* provider = RGI_CONTAINER_OF(self, RgiThumbnailProvider, stream_iface);
    return rgi_provider_query_interface(provider, riid, ppv);
}

static ULONG STDMETHODCALLTYPE rgi_stream_addref(IInitializeWithStream* self)
{
    RgiThumbnailProvider* provider = RGI_CONTAINER_OF(self, RgiThumbnailProvider, stream_iface);
    return rgi_provider_addref(provider);
}

static ULONG STDMETHODCALLTYPE rgi_stream_release(IInitializeWithStream* self)
{
    RgiThumbnailProvider* provider = RGI_CONTAINER_OF(self, RgiThumbnailProvider, stream_iface);
    return rgi_provider_release(provider);
}

static HRESULT STDMETHODCALLTYPE rgi_stream_initialize(IInitializeWithStream* self, IStream* stream, DWORD grfMode)
{
    (void)grfMode;
    if (stream == NULL)
    {
        return E_INVALIDARG;
    }

    RgiThumbnailProvider* provider = RGI_CONTAINER_OF(self, RgiThumbnailProvider, stream_iface);
    if (provider->stream != NULL)
    {
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }

    provider->stream = stream;
    provider->stream->lpVtbl->AddRef(provider->stream);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE rgi_class_query_interface(IClassFactory* self, REFIID riid, void** ppv)
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory))
    {
        *ppv = self;
        rgi_class_addref(self);
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE rgi_class_addref(IClassFactory* self)
{
    RgiClassFactory* factory = (RgiClassFactory*)self;
    return (ULONG)InterlockedIncrement(&factory->ref_count);
}

static ULONG STDMETHODCALLTYPE rgi_class_release(IClassFactory* self)
{
    RgiClassFactory* factory = (RgiClassFactory*)self;
    LONG ref = InterlockedDecrement(&factory->ref_count);
    if (ref == 0)
    {
        CoTaskMemFree(factory);
        InterlockedDecrement(&g_object_count);
    }
    return (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE rgi_class_create_instance(IClassFactory* self, IUnknown* outer, REFIID riid, void** ppv)
{
    (void)self;
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (outer != NULL)
    {
        return CLASS_E_NOAGGREGATION;
    }

    RgiThumbnailProvider* provider = (RgiThumbnailProvider*)CoTaskMemAlloc(sizeof(*provider));
    if (provider == NULL)
    {
        return E_OUTOFMEMORY;
    }

    ZeroMemory(provider, sizeof(*provider));
    provider->thumbnail_iface.lpVtbl = &g_thumbnail_vtbl;
    provider->stream_iface.lpVtbl = &g_stream_vtbl;
    provider->ref_count = 1;
    provider->stream = NULL;
    InterlockedIncrement(&g_object_count);

    HRESULT hr = rgi_provider_query_interface(provider, riid, ppv);
    rgi_provider_release(provider);
    return hr;
}

static HRESULT STDMETHODCALLTYPE rgi_class_lock_server(IClassFactory* self, BOOL lock)
{
    (void)self;
    if (lock)
    {
        InterlockedIncrement(&g_lock_count);
    }
    else
    {
        InterlockedDecrement(&g_lock_count);
    }
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    if (ppv == NULL)
    {
        return E_POINTER;
    }

    *ppv = NULL;
    if (!IsEqualCLSID(rclsid, &CLSID_RgiThumbnailProvider))
    {
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    RgiClassFactory* factory = (RgiClassFactory*)CoTaskMemAlloc(sizeof(*factory));
    if (factory == NULL)
    {
        return E_OUTOFMEMORY;
    }

    ZeroMemory(factory, sizeof(*factory));
    factory->iface.lpVtbl = &g_class_vtbl;
    factory->ref_count = 1;
    InterlockedIncrement(&g_object_count);

    HRESULT hr = rgi_class_query_interface(&factory->iface, riid, ppv);
    rgi_class_release(&factory->iface);
    return hr;
}

STDAPI DllCanUnloadNow(void)
{
    return (g_object_count == 0 && g_lock_count == 0) ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
