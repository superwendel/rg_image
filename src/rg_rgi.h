#ifndef RG_RGI_H
#define RG_RGI_H

// rg_rgi - RGI (Reverse Gravity Image) codec
//
// Part of the Reverse Gravity (rg_) gamedev suite.
// Single-header C99 library providing checked/trusted decode and automatic encode.
//
// USAGE:
//   #include "rg_rgi.h"
//
//   uint32_t width = 0;
//   uint32_t height = 0;
//   if (!rg_rgi_read_header(data, data_size, &width, &height))
//   {
//       return 0;
//   }
//
//   size_t out_size = (size_t)width * (size_t)height * 4u;
//   uint8_t* pixels = (uint8_t*)malloc(out_size);
//   size_t written = rg_rgi_decode(data, data_size, pixels, out_size, &width, &height);
//
// OPTIONS:
//   #define RG_RGI_MAX_DIM      - Consumer maximum width/height (default: 16384)
//   #define RG_RGI_NO_SIMD      - Disable SIMD run stores
//   #define RG_RGI_MALLOC(size) - Custom allocation for rg_rgi_encode
//   #define RG_RGI_FREE(ptr)    - Matching custom free for rg_rgi_encode
//
// NOTES:
//   - Format is QOI-like with the public wire magic "rgif".
//   - Width and height are stored little-endian.
//   - Header byte 13 selects payload profile: 0 = QOI-style, 1 = extended.
//   - Extended profile reserves 0xFC for raw RGBA spans.
//   - Extended profile reserves 0xFD for a little-endian u16 long-run chunk.
//   - Checked decode requires the canonical end marker and rejects trailing data.
//   - Trusted decode requires a previously validated complete stream and buffers.
//   - Output is always RGBA8.
//
// Author: Steven Wendel (superwendel)

#include "rg_defs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// All functions are static for unity build compatibility

// =============================================================================
// CONFIGURATION
// =============================================================================

#define RG_RGI_FORMAT_MAX_DIM 16384u
#ifndef RG_RGI_MAX_DIM
    #define RG_RGI_MAX_DIM RG_RGI_FORMAT_MAX_DIM
#elif RG_RGI_MAX_DIM > RG_RGI_FORMAT_MAX_DIM
    #error "RG_RGI_MAX_DIM may only lower the format limit"
#endif

#if defined(RG_RGI_MALLOC) != defined(RG_RGI_FREE)
    #error "RG_RGI_MALLOC and RG_RGI_FREE must be defined together"
#endif
#ifndef RG_RGI_MALLOC
    #define RG_RGI_MALLOC(size) malloc(size)
    #define RG_RGI_FREE(ptr) free(ptr)
#endif

#ifndef RG_RGI_UNUSED
    #define RG_RGI_UNUSED(x) (void)(x)
#endif

// SIMD detection and includes
#ifndef RG_RGI_NO_SIMD
    #if RG_ARCH_X64
        #include <emmintrin.h>
        #define RG_RGI_HAS_SSE2 1
    #endif
    #if RG_ARCH_ARM64
        #include <arm_neon.h>
        #define RG_RGI_HAS_NEON 1
    #endif
#endif

#ifndef RG_RGI_HAS_SSE2
    #define RG_RGI_HAS_SSE2 0
#endif

#ifndef RG_RGI_HAS_NEON
    #define RG_RGI_HAS_NEON 0
#endif

#define RG_RGI_HEADER_SIZE 14u
#define RG_RGI_END_SIZE 8u

#define RG_RGI_MAGIC_0 'r'
#define RG_RGI_MAGIC_1 'g'
#define RG_RGI_MAGIC_2 'i'
#define RG_RGI_MAGIC_3 'f'

#define RG_RGI_OP_INDEX 0x00u
#define RG_RGI_OP_DIFF  0x40u
#define RG_RGI_OP_LUMA  0x80u
#define RG_RGI_OP_RUN   0xC0u
#define RG_RGI_OP_COPY 0xFBu
#define RG_RGI_OP_RAWSPAN 0xFCu
#define RG_RGI_OP_LONG_RUN 0xFDu
#define RG_RGI_OP_RGB   0xFEu
#define RG_RGI_OP_RGBA  0xFFu
#define RG_RGI_OP_MASK  0xC0u
#define RG_RGI_PROFILE_QOI 0u
#define RG_RGI_PROFILE_EXTENDED 1u
#define RG_RGI_QOI_RUN_MAX_SHORT 62u
#define RG_RGI_RUN_MAX_SHORT 59u
#define RG_RGI_RUN_MAX_LONG 65535u
#define RG_RGI_COPY_MIN 4u
#define RG_RGI_COPY_MAX 259u
#define RG_RGI_COPY_MAX_OFFSET 65535u
#define RG_RGI_COPY_TABLE_SIZE 4096u
#ifndef RG_RGI_COPY_BUCKET_DEPTH
    #define RG_RGI_COPY_BUCKET_DEPTH 12u
#endif
#ifndef RG_RGI_COPY_ROW_LOCAL_SEARCH
    #define RG_RGI_COPY_ROW_LOCAL_SEARCH 512u
#endif
#ifndef RG_RGI_COPY_PREV_ROW_SEARCH
    #define RG_RGI_COPY_PREV_ROW_SEARCH 32u
#endif
#ifndef RG_RGI_COPY_LAZY_LOOKAHEAD
    #define RG_RGI_COPY_LAZY_LOOKAHEAD 0u
#endif
#ifndef RG_RGI_COPY_LAZY_MIN_GAIN_BYTES
    #define RG_RGI_COPY_LAZY_MIN_GAIN_BYTES 0u
#endif

// =============================================================================
// TYPE DEFINITIONS
// =============================================================================

typedef struct RgRgiPixel
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} RgRgiPixel;

typedef struct RgRgiCopyTable
{
    uint64_t buckets[RG_RGI_COPY_TABLE_SIZE][RG_RGI_COPY_BUCKET_DEPTH];
    uint8_t counts[RG_RGI_COPY_TABLE_SIZE];
} RgRgiCopyTable;

/** Reusable, correctly aligned storage for rg_rgi_encode_with_workspace. */
typedef union RgRgiEncodeWorkspace
{
    RgRgiCopyTable table;
    uint64_t alignment;
} RgRgiEncodeWorkspace;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Read and validate the RGI header
 * @param src Input bytes
 * @param src_size Input size in bytes
 * @param out_width Output width (optional)
 * @param out_height Output height (optional)
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_rgi_read_header(const void* src,
                                size_t src_size,
                                uint32_t* out_width,
                                uint32_t* out_height);

/**
 * @brief Decode RGI image into RGBA8 output
 * @param src Input bytes
 * @param src_size Input size in bytes
 * @param dst Output buffer (RGBA8)
 * @param dst_size Output buffer size in bytes
 * @param out_width Output width (optional)
 * @param out_height Output height (optional)
 * @return Bytes written to dst, or 0 on failure
 */
RGINLINE size_t rg_rgi_decode(const void* src,
                              size_t src_size,
                              void* dst,
                              size_t dst_size,
                              uint32_t* out_width,
                              uint32_t* out_height);

/**
 * @brief Decode a previously validated RGI image with minimal checks
 * @param src Complete valid RGI stream
 * @param src_size Complete input size (trusted precondition)
 * @param dst Non-overlapping RGBA8 output buffer
 * @param dst_size Output buffer size (trusted precondition)
 * @param out_width Output width (optional)
 * @param out_height Output height (optional)
 * @return Bytes written to dst
 */
RGINLINE size_t rg_rgi_decode_trusted(const void* src,
                                      size_t src_size,
                                      void* dst,
                                      size_t dst_size,
                                      uint32_t* out_width,
                                      uint32_t* out_height);

/**
 * @brief Maximum encoded size for an RGI image
 * @param width Image width
 * @param height Image height
 * @return Max bytes required for encoded output, or 0 on invalid/overflow
 */
RGINLINE size_t rg_rgi_encode_bound(uint32_t width, uint32_t height);

/**
 * @brief Bytes of aligned reusable workspace needed by the encoder
 */
RGINLINE size_t rg_rgi_encode_workspace_size(void);

/**
 * @brief Encode RGBA8 pixels using caller-owned reusable workspace
 *
 * Pass an RgRgiEncodeWorkspace object or equivalently aligned dynamic storage.
 * Workspace must not overlap input or output.
 */
RGINLINE size_t rg_rgi_encode_with_workspace(const void* rgba,
                                             uint32_t width,
                                             uint32_t height,
                                             void* dst,
                                             size_t dst_size,
                                             void* workspace,
                                             size_t workspace_size);

/**
 * @brief Encode RGBA8 pixels using a temporary allocator-backed workspace
 * @param rgba Input pixels (RGBA8)
 * @param width Image width
 * @param height Image height
 * @param dst Output buffer
 * @param dst_size Output buffer size in bytes
 * @return Bytes written to dst, or 0 on failure
 */
RGINLINE size_t rg_rgi_encode(const void* rgba,
                              uint32_t width,
                              uint32_t height,
                              void* dst,
                              size_t dst_size);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

RGINLINE size_t rg_rgi__decode_payload_trusted_profile(const void* src,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       uint8_t profile,
                                                       void* dst);

RGINLINE uint32_t rg_rgi_load_u32_le(const void* ptr)
{
    const uint8_t* bytes = (const uint8_t*)ptr;
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) |
           ((uint32_t)bytes[3] << 24u);
}

RGINLINE void rg_rgi_store_u32_le(void* ptr, uint32_t value)
{
    uint8_t* bytes = (uint8_t*)ptr;
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
    bytes[2] = (uint8_t)((value >> 16u) & 0xffu);
    bytes[3] = (uint8_t)((value >> 24u) & 0xffu);
}

RGINLINE int rg_rgi__ranges_overlap(const void* a, size_t a_size,
                                    const void* b, size_t b_size)
{
    uintptr_t ap = (uintptr_t)a;
    uintptr_t bp = (uintptr_t)b;
    if (a_size == 0u || b_size == 0u)
    {
        return 0;
    }
    if (ap <= bp)
    {
        return (bp - ap) < a_size;
    }
    return (ap - bp) < b_size;
}

RGINLINE int rg_rgi__has_end_marker(const uint8_t* ptr)
{
    return ptr[0] == 0u && ptr[1] == 0u && ptr[2] == 0u && ptr[3] == 0u &&
           ptr[4] == 0u && ptr[5] == 0u && ptr[6] == 0u && ptr[7] == 1u;
}

RGINLINE uint16_t rg_rgi_load_u16_le(const void* ptr)
{
    const uint8_t* bytes = (const uint8_t*)ptr;
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

RGINLINE void rg_rgi_store_u16_le(void* ptr, uint16_t value)
{
    uint8_t* bytes = (uint8_t*)ptr;
    bytes[0] = (uint8_t)(value & 0xffu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
}

RGINLINE uint8_t rg_rgi_hash_pixel(RgRgiPixel px)
{
    uint32_t value = (uint32_t)px.r * 3u +
                     (uint32_t)px.g * 5u +
                     (uint32_t)px.b * 7u +
                     (uint32_t)px.a * 11u;
    return (uint8_t)(value & 63u);
}

RGINLINE int rg_rgi_pixel_equal(RgRgiPixel a, RgRgiPixel b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

RGINLINE uint32_t rg_rgi_pack_rgba(RgRgiPixel px)
{
    return (uint32_t)px.r |
           ((uint32_t)px.g << 8u) |
           ((uint32_t)px.b << 16u) |
           ((uint32_t)px.a << 24u);
}

RGINLINE uint32_t rg_rgi_pack_rgba_u32(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (uint32_t)r |
           ((uint32_t)g << 8u) |
           ((uint32_t)b << 16u) |
           ((uint32_t)a << 24u);
}

RGINLINE uint8_t rg_rgi_hash_pixel_u32(uint32_t px)
{
    uint32_t value = (px & 0xffu) * 3u +
                     ((px >> 8u) & 0xffu) * 5u +
                     ((px >> 16u) & 0xffu) * 7u +
                     ((px >> 24u) & 0xffu) * 11u;
    return (uint8_t)(value & 63u);
}

RGINLINE uint32_t rg_rgi_hash_copy4(const uint8_t* pixels, uint64_t pos)
{
    const uint8_t* ptr = pixels + pos * 4u;
    uint32_t h = rg_rgi_load_u32_le(ptr + 0u) * 0x9e3779b1u;
    h ^= rg_rgi_load_u32_le(ptr + 4u) * 0x85ebca6bu;
    h ^= rg_rgi_load_u32_le(ptr + 8u) * 0xc2b2ae35u;
    h ^= rg_rgi_load_u32_le(ptr + 12u) * 0x27d4eb2fu;
    return h & (RG_RGI_COPY_TABLE_SIZE - 1u);
}

RGINLINE void rg_rgi_copy_table_insert(RgRgiCopyTable* table,
                                       const uint8_t* pixels,
                                       uint64_t pixel_count,
                                       uint64_t pos)
{
    if (table == NULL || pos + RG_RGI_COPY_MIN > pixel_count)
    {
        return;
    }
    uint32_t hash = rg_rgi_hash_copy4(pixels, pos);
    uint8_t count = table->counts[hash];
    uint64_t value = pos + 1u;
    for (uint8_t i = 0u; i < count; i++)
    {
        if (table->buckets[hash][i] == value)
        {
            for (uint8_t j = i; j > 0u; j--)
            {
                table->buckets[hash][j] = table->buckets[hash][j - 1u];
            }
            table->buckets[hash][0] = value;
            return;
        }
    }
    if (count < RG_RGI_COPY_BUCKET_DEPTH)
    {
        count++;
        table->counts[hash] = count;
    }
    for (uint8_t i = (uint8_t)(count - 1u); i > 0u; i--)
    {
        table->buckets[hash][i] = table->buckets[hash][i - 1u];
    }
    table->buckets[hash][0] = value;
}

RGINLINE void rg_rgi_copy_table_insert_range(RgRgiCopyTable* table,
                                             const uint8_t* pixels,
                                             uint64_t pixel_count,
                                             uint64_t start,
                                             size_t count)
{
    uint64_t end = start + (uint64_t)count;
    for (uint64_t pos = start; pos < end; pos++)
    {
        rg_rgi_copy_table_insert(table, pixels, pixel_count, pos);
    }
}

RGINLINE void rg_rgi_consider_copy_candidate(const uint8_t* pixels,
                                             uint64_t pixel_count,
                                             uint64_t pos,
                                             uint64_t src,
                                             size_t* best_count,
                                             uint16_t* best_offset)
{
    if (src >= pos)
    {
        return;
    }
    uint64_t offset64 = pos - src;
    if (offset64 == 0u || offset64 > RG_RGI_COPY_MAX_OFFSET)
    {
        return;
    }
    uint64_t max_count = pixel_count - pos;
    if (max_count > RG_RGI_COPY_MAX)
    {
        max_count = RG_RGI_COPY_MAX;
    }
    if (max_count > offset64)
    {
        max_count = offset64;
    }
    if (max_count <= (uint64_t)(*best_count) ||
        max_count < RG_RGI_COPY_MIN)
    {
        return;
    }

    size_t count = 0u;
    while ((uint64_t)count < max_count &&
           rg_rgi_load_u32_le(pixels + (src + (uint64_t)count) * 4u) ==
           rg_rgi_load_u32_le(pixels + (pos + (uint64_t)count) * 4u))
    {
        count++;
    }

    if (count >= RG_RGI_COPY_MIN && count > *best_count)
    {
        *best_count = count;
        *best_offset = (uint16_t)offset64;
    }
}

RGINLINE size_t rg_rgi_find_copy(const RgRgiCopyTable* table,
                                 const uint8_t* pixels,
                                 uint64_t pixel_count,
                                 uint32_t width,
                                 uint64_t pos,
                                 uint16_t* out_offset)
{
    if (table == NULL || out_offset == NULL || pos + RG_RGI_COPY_MIN > pixel_count)
    {
        return 0u;
    }

    size_t best_count = 0u;
    uint16_t best_offset = 0u;
    uint32_t hash = rg_rgi_hash_copy4(pixels, pos);
    uint8_t count = table->counts[hash];
    for (uint8_t i = 0u; i < count; i++)
    {
        uint64_t entry = table->buckets[hash][i];
        if (entry != 0u)
        {
            rg_rgi_consider_copy_candidate(pixels,
                                           pixel_count,
                                           pos,
                                           entry - 1u,
                                           &best_count,
                                           &best_offset);
        }
    }

    if (width > 0u)
    {
        uint64_t row_width = (uint64_t)width;
        uint64_t same_x = row_width;
        for (uint32_t row = 0u; row < RG_RGI_COPY_PREV_ROW_SEARCH && same_x <= pos; row++)
        {
            rg_rgi_consider_copy_candidate(pixels,
                                           pixel_count,
                                           pos,
                                           pos - same_x,
                                           &best_count,
                                           &best_offset);
            same_x += row_width;
        }

        uint64_t row_start = (pos / row_width) * row_width;
        uint64_t scan_start = pos > RG_RGI_COPY_ROW_LOCAL_SEARCH ?
                              pos - RG_RGI_COPY_ROW_LOCAL_SEARCH :
                              0u;
        if (scan_start < row_start)
        {
            scan_start = row_start;
        }
        for (uint64_t src = pos; src > scan_start;)
        {
            src--;
            rg_rgi_consider_copy_candidate(pixels,
                                           pixel_count,
                                           pos,
                                           src,
                                           &best_count,
                                           &best_offset);
        }
    }

    if (best_count < RG_RGI_COPY_MIN)
    {
        return 0u;
    }
    *out_offset = best_offset;
    return best_count;
}

RGINLINE size_t rg_rgi_estimate_skip_cost(const uint8_t* pixels,
                                          uint64_t pixel_count,
                                          uint64_t pos,
                                          size_t skip,
                                          uint32_t prev_px,
                                          const uint32_t index[64])
{
    uint32_t local_index[64];
    memcpy(local_index, index, sizeof(local_index));
    uint32_t px = prev_px;
    size_t cost = 0u;
    for (size_t i = 0u; i < skip && pos + (uint64_t)i < pixel_count; i++)
    {
        uint32_t cur = rg_rgi_load_u32_le(pixels + (pos + (uint64_t)i) * 4u);
        if (cur == px)
        {
            cost += 1u;
            continue;
        }

        uint8_t hash = rg_rgi_hash_pixel_u32(cur);
        if (local_index[hash] == cur)
        {
            cost += 1u;
        }
        else
        {
            cost += 6u;
        }
        local_index[hash] = cur;
        px = cur;
    }
    return cost;
}

RGINLINE int rg_rgi_should_defer_copy(const RgRgiCopyTable* table,
                                      const uint8_t* pixels,
                                      uint64_t pixel_count,
                                      uint32_t width,
                                      uint64_t pos,
                                      size_t current_count,
                                      size_t min_copy,
                                      uint32_t prev_px,
                                      const uint32_t index[64])
{
#if RG_RGI_COPY_LAZY_LOOKAHEAD > 0
    if (table == NULL || current_count < min_copy)
    {
        return 0;
    }

    size_t max_skip = (size_t)RG_RGI_COPY_LAZY_LOOKAHEAD;
    if (max_skip > 2u)
    {
        max_skip = 2u;
    }
    if ((uint64_t)max_skip + pos >= pixel_count)
    {
        max_skip = (size_t)(pixel_count - pos - 1u);
    }

    for (size_t skip = 1u; skip <= max_skip; skip++)
    {
        uint16_t next_offset = 0u;
        size_t next_count = rg_rgi_find_copy(table,
                                             pixels,
                                             pixel_count,
                                             width,
                                             pos + (uint64_t)skip,
                                             &next_offset);
        RG_RGI_UNUSED(next_offset);
        if (next_count < min_copy)
        {
            continue;
        }

        if (next_count <= current_count)
        {
            continue;
        }

        size_t extra_pixels = next_count - current_count;
        size_t skip_cost = rg_rgi_estimate_skip_cost(pixels,
                                                     pixel_count,
                                                     pos,
                                                     skip,
                                                     prev_px,
                                                     index);
        size_t gain_bytes = extra_pixels * 4u;
        if (gain_bytes >= skip_cost + (size_t)RG_RGI_COPY_LAZY_MIN_GAIN_BYTES)
        {
            return 1;
        }
    }
#else
    RG_RGI_UNUSED(table);
    RG_RGI_UNUSED(pixels);
    RG_RGI_UNUSED(pixel_count);
    RG_RGI_UNUSED(width);
    RG_RGI_UNUSED(pos);
    RG_RGI_UNUSED(current_count);
    RG_RGI_UNUSED(min_copy);
    RG_RGI_UNUSED(prev_px);
    RG_RGI_UNUSED(index);
#endif
    return 0;
}

RGINLINE void rg_rgi_store_pixel(uint8_t* dst, RgRgiPixel px)
{
    dst[0] = px.r;
    dst[1] = px.g;
    dst[2] = px.b;
    dst[3] = px.a;
}

RGINLINE void rg_rgi_store_pixel_u32(uint8_t* dst, uint32_t px)
{
    dst[0] = (uint8_t)(px & 0xffu);
    dst[1] = (uint8_t)((px >> 8u) & 0xffu);
    dst[2] = (uint8_t)((px >> 16u) & 0xffu);
    dst[3] = (uint8_t)((px >> 24u) & 0xffu);
}

RGINLINE void rg_rgi_store_run_rgba_u32(uint8_t* dst, size_t count, uint32_t px)
{
    if (count == 0u)
    {
        return;
    }

    #if RG_RGI_HAS_SSE2
        __m128i v = _mm_set1_epi32((int)px);
        while (count >= 4u)
        {
            _mm_storeu_si128((__m128i*)(void*)dst, v);
            dst += 16u;
            count -= 4u;
        }
    #elif RG_RGI_HAS_NEON
        uint8_t pattern[16];
        for (size_t i = 0u; i < 16u; i += 4u)
        {
            rg_rgi_store_pixel_u32(pattern + i, px);
        }
        uint8x16_t bytes = vld1q_u8(pattern);
        while (count >= 4u)
        {
            vst1q_u8(dst, bytes);
            dst += 16u;
            count -= 4u;
        }
    #endif

    while (count > 0u)
    {
        rg_rgi_store_pixel_u32(dst, px);
        dst += 4u;
        count--;
    }
}

RGINLINE void rg_rgi_emit_run(uint8_t** ptr, size_t run)
{
    while (run > 0u)
    {
        if (run <= RG_RGI_RUN_MAX_SHORT)
        {
            *(*ptr)++ = (uint8_t)(RG_RGI_OP_RUN | (uint8_t)(run - 1u));
            run = 0u;
        }
        else
        {
            size_t count = run > RG_RGI_RUN_MAX_LONG ? RG_RGI_RUN_MAX_LONG : run;
            *(*ptr)++ = RG_RGI_OP_LONG_RUN;
            rg_rgi_store_u16_le(*ptr, (uint16_t)count);
            *ptr += 2u;
            run -= count;
        }
    }
}

RGINLINE void rg_rgi_emit_qoi_run(uint8_t** ptr, size_t run)
{
    while (run > 0u)
    {
        size_t count = run > RG_RGI_QOI_RUN_MAX_SHORT ? RG_RGI_QOI_RUN_MAX_SHORT : run;
        *(*ptr)++ = (uint8_t)(RG_RGI_OP_RUN | (uint8_t)(count - 1u));
        run -= count;
    }
}

RGINLINE size_t rg_rgi_encode_qoi_payload(const uint8_t* pixels, uint64_t pixel_count, uint8_t* ptr)
{
    uint8_t* start = ptr;
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
        px.r = pixels[i * 4u + 0u];
        px.g = pixels[i * 4u + 1u];
        px.b = pixels[i * 4u + 2u];
        px.a = pixels[i * 4u + 3u];

        if (rg_rgi_pixel_equal(px, prev))
        {
            run++;
            if (run == (int)RG_RGI_QOI_RUN_MAX_SHORT || i == (pixel_count - 1u))
            {
                rg_rgi_emit_qoi_run(&ptr, (size_t)run);
                run = 0;
            }
            continue;
        }

        if (run > 0)
        {
            rg_rgi_emit_qoi_run(&ptr, (size_t)run);
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

    return (size_t)(ptr - start);
}

RGINLINE size_t rg_rgi_estimate_rawspan_payload_size_min_copy(const uint8_t* pixels,
                                                              uint64_t pixel_count,
                                                              uint32_t width,
                                                              int use_copy,
                                                              size_t min_copy,
                                                              RgRgiCopyTable* copy_table)
{
    uint32_t index[64];
    memset(index, 0, sizeof(index));
    if (use_copy)
    {
        memset(copy_table, 0, sizeof(*copy_table));
    }
    uint32_t px = 0xff000000u;
    size_t size = 0u;
    if (min_copy < RG_RGI_COPY_MIN)
    {
        min_copy = RG_RGI_COPY_MIN;
    }

    for (uint64_t i = 0u; i < pixel_count;)
    {
        uint32_t cur = rg_rgi_load_u32_le(pixels + i * 4u);
        if (cur == px)
        {
            size_t run = 1u;
            while (i + run < pixel_count &&
                   rg_rgi_load_u32_le(pixels + (i + run) * 4u) == px &&
                   run < RG_RGI_RUN_MAX_LONG)
            {
                run++;
            }
            size += run <= RG_RGI_RUN_MAX_SHORT ? 1u : 3u;
            if (use_copy)
            {
                rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, i, run);
            }
            i += run;
            continue;
        }

        uint16_t copy_offset = 0u;
        size_t copy_count = use_copy ? rg_rgi_find_copy(copy_table, pixels, pixel_count, width, i, &copy_offset) : 0u;
        if (use_copy && copy_count >= min_copy &&
            !rg_rgi_should_defer_copy(copy_table,
                                      pixels,
                                      pixel_count,
                                      width,
                                      i,
                                      copy_count,
                                      min_copy,
                                      px,
                                      index))
        {
            size += 4u;
            px = rg_rgi_load_u32_le(pixels + (i + (uint64_t)copy_count - 1u) * 4u);
            rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, i, copy_count);
            i += copy_count;
            continue;
        }

        uint8_t hash = rg_rgi_hash_pixel_u32(cur);
        if (index[hash] == cur)
        {
            size += 1u;
            px = cur;
            if (use_copy)
            {
                rg_rgi_copy_table_insert(copy_table, pixels, pixel_count, i);
            }
            i++;
            continue;
        }

        uint64_t start = i;
        size_t count = 0u;
        uint32_t prev = px;
        while (i < pixel_count && count < 255u)
        {
            cur = rg_rgi_load_u32_le(pixels + i * 4u);
            hash = rg_rgi_hash_pixel_u32(cur);
            copy_count = use_copy ? rg_rgi_find_copy(copy_table, pixels, pixel_count, width, i, &copy_offset) : 0u;
            if (cur == prev || index[hash] == cur)
            {
                break;
            }
            if (use_copy && copy_count >= min_copy &&
                !rg_rgi_should_defer_copy(copy_table,
                                          pixels,
                                          pixel_count,
                                          width,
                                          i,
                                          copy_count,
                                          min_copy,
                                          prev,
                                          index))
            {
                break;
            }
            prev = cur;
            i++;
            count++;
        }
        if (count == 0u)
        {
            count = 1u;
            i++;
        }

        size += 2u + count * 4u;
        px = rg_rgi_load_u32_le(pixels + (start + count - 1u) * 4u);
        if (use_copy)
        {
            rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, start, count);
        }
    }

    return size;
}

RGINLINE size_t rg_rgi_encode_rawspan_payload_min_copy(const uint8_t* pixels,
                                                       uint64_t pixel_count,
                                                       uint32_t width,
                                                       uint8_t* ptr,
                                                       int use_copy,
                                                       size_t min_copy,
                                                       RgRgiCopyTable* copy_table)
{
    uint8_t* start = ptr;
    uint32_t index[64];
    memset(index, 0, sizeof(index));
    if (use_copy)
    {
        memset(copy_table, 0, sizeof(*copy_table));
    }
    uint32_t px = 0xff000000u;
    if (min_copy < RG_RGI_COPY_MIN)
    {
        min_copy = RG_RGI_COPY_MIN;
    }

    for (uint64_t i = 0u; i < pixel_count;)
    {
        uint32_t cur = rg_rgi_load_u32_le(pixels + i * 4u);
        if (cur == px)
        {
            size_t run = 1u;
            while (i + run < pixel_count &&
                   rg_rgi_load_u32_le(pixels + (i + run) * 4u) == px &&
                   run < RG_RGI_RUN_MAX_LONG)
            {
                run++;
            }
            rg_rgi_emit_run(&ptr, run);
            if (use_copy)
            {
                rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, i, run);
            }
            i += run;
            continue;
        }

        uint16_t copy_offset = 0u;
        size_t copy_count = use_copy ? rg_rgi_find_copy(copy_table, pixels, pixel_count, width, i, &copy_offset) : 0u;
        if (use_copy && copy_count >= min_copy &&
            !rg_rgi_should_defer_copy(copy_table,
                                      pixels,
                                      pixel_count,
                                      width,
                                      i,
                                      copy_count,
                                      min_copy,
                                      px,
                                      index))
        {
            *ptr++ = RG_RGI_OP_COPY;
            *ptr++ = (uint8_t)(copy_count - RG_RGI_COPY_MIN);
            rg_rgi_store_u16_le(ptr, copy_offset);
            ptr += 2u;
            px = rg_rgi_load_u32_le(pixels + (i + (uint64_t)copy_count - 1u) * 4u);
            rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, i, copy_count);
            i += copy_count;
            continue;
        }

        uint8_t hash = rg_rgi_hash_pixel_u32(cur);
        if (index[hash] == cur)
        {
            *ptr++ = (uint8_t)(RG_RGI_OP_INDEX | hash);
            px = cur;
            if (use_copy)
            {
                rg_rgi_copy_table_insert(copy_table, pixels, pixel_count, i);
            }
            i++;
            continue;
        }

        uint64_t span_start = i;
        size_t count = 0u;
        uint32_t prev = px;
        while (i < pixel_count && count < 255u)
        {
            cur = rg_rgi_load_u32_le(pixels + i * 4u);
            hash = rg_rgi_hash_pixel_u32(cur);
            copy_count = use_copy ? rg_rgi_find_copy(copy_table, pixels, pixel_count, width, i, &copy_offset) : 0u;
            if (cur == prev || index[hash] == cur)
            {
                break;
            }
            if (use_copy && copy_count >= min_copy &&
                !rg_rgi_should_defer_copy(copy_table,
                                          pixels,
                                          pixel_count,
                                          width,
                                          i,
                                          copy_count,
                                          min_copy,
                                          prev,
                                          index))
            {
                break;
            }
            prev = cur;
            i++;
            count++;
        }
        if (count == 0u)
        {
            count = 1u;
            i++;
        }

        *ptr++ = RG_RGI_OP_RAWSPAN;
        *ptr++ = (uint8_t)count;
        memcpy(ptr, pixels + span_start * 4u, count * 4u);
        ptr += count * 4u;
        px = rg_rgi_load_u32_le(pixels + (span_start + count - 1u) * 4u);
        if (use_copy)
        {
            rg_rgi_copy_table_insert_range(copy_table, pixels, pixel_count, span_start, count);
        }
    }

    return (size_t)(ptr - start);
}

RGINLINE int rg_rgi_read_header(const void* src,
                                size_t src_size,
                                uint32_t* out_width,
                                uint32_t* out_height)
{
    if (src == NULL || src_size < RG_RGI_HEADER_SIZE)
    {
        return 0;
    }

    const uint8_t* data = (const uint8_t*)src;
    if (data[0] != (uint8_t)RG_RGI_MAGIC_0 ||
        data[1] != (uint8_t)RG_RGI_MAGIC_1 ||
        data[2] != (uint8_t)RG_RGI_MAGIC_2 ||
        data[3] != (uint8_t)RG_RGI_MAGIC_3)
    {
        return 0;
    }

    uint32_t width = rg_rgi_load_u32_le(data + 4);
    uint32_t height = rg_rgi_load_u32_le(data + 8);
    uint8_t channels = data[12];
    uint8_t profile = data[13];

    if (width == 0u || height == 0u)
    {
        return 0;
    }
    if (width > RG_RGI_MAX_DIM || height > RG_RGI_MAX_DIM)
    {
        return 0;
    }
    if (channels != 4u || profile > RG_RGI_PROFILE_EXTENDED)
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

    return 1;
}

RGINLINE size_t rg_rgi_decode(const void* src,
                              size_t src_size,
                              void* dst,
                              size_t dst_size,
                              uint32_t* out_width,
                              uint32_t* out_height)
{
    if (src == NULL || dst == NULL)
    {
        return 0;
    }
    if (src_size < RG_RGI_HEADER_SIZE + RG_RGI_END_SIZE)
    {
        return 0;
    }

    uint32_t width = 0;
    uint32_t height = 0;
    if (!rg_rgi_read_header(src, src_size, &width, &height))
    {
        return 0;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count == 0u || pixel_count > (uint64_t)(SIZE_MAX / 4u))
    {
        return 0;
    }

    size_t out_bytes = (size_t)pixel_count * 4u;
    if (dst_size < out_bytes)
    {
        return 0;
    }
    if (rg_rgi__ranges_overlap(src, src_size, dst, out_bytes))
    {
        return 0;
    }

    const uint8_t* ptr = (const uint8_t*)src + RG_RGI_HEADER_SIZE;
    const uint8_t* end = (const uint8_t*)src + src_size - RG_RGI_END_SIZE;
    uint8_t profile = ((const uint8_t*)src)[13];
    uint8_t* out = (uint8_t*)dst;
    uint8_t* out_ptr = out;

    uint32_t index[64];
    memset(index, 0, sizeof(index));

    uint32_t px = 0xff000000u;

    uint64_t px_pos = 0;

    while (px_pos < pixel_count)
    {
        if (ptr >= end)
        {
            return 0;
        }

        uint8_t b1 = *ptr++;
        if (b1 == RG_RGI_OP_RGB)
        {
            if ((size_t)(end - ptr) < 3u)
            {
                return 0;
            }
            px = (px & 0xff000000u) |
                 (uint32_t)ptr[0] |
                 ((uint32_t)ptr[1] << 8u) |
                 ((uint32_t)ptr[2] << 16u);
            ptr += 3;
        }
        else if (b1 == RG_RGI_OP_RGBA)
        {
            if ((size_t)(end - ptr) < 4u)
            {
                return 0;
            }
            px = rg_rgi_pack_rgba_u32(ptr[0], ptr[1], ptr[2], ptr[3]);
            ptr += 4;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_INDEX)
        {
            px = index[b1 & 63u];
            rg_rgi_store_pixel_u32(out_ptr, px);
            out_ptr += 4u;
            px_pos++;
            continue;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_DIFF)
        {
            int dr = ((b1 >> 4) & 3) - 2;
            int dg = ((b1 >> 2) & 3) - 2;
            int db = (b1 & 3) - 2;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_LUMA)
        {
            if (ptr >= end)
            {
                return 0;
            }
            uint8_t b2 = *ptr++;
            int dg = (b1 & 0x3F) - 32;
            int dr = ((b2 >> 4) & 0x0F) - 8 + dg;
            int db = (b2 & 0x0F) - 8 + dg;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_RUN)
        {
            if (profile == RG_RGI_PROFILE_EXTENDED && b1 == RG_RGI_OP_COPY)
            {
                if ((size_t)(end - ptr) < 3u)
                {
                    return 0;
                }
                size_t count = (size_t)ptr[0] + RG_RGI_COPY_MIN;
                size_t offset = (size_t)rg_rgi_load_u16_le(ptr + 1u);
                ptr += 3u;
                size_t bytes = count * 4u;
                uint64_t remaining = pixel_count - px_pos;
                if (offset == 0u || offset > (size_t)px_pos || offset < count ||
                    (uint64_t)count > remaining)
                {
                    return 0;
                }
                memcpy(out_ptr, out_ptr - offset * 4u, bytes);
                px = rg_rgi_load_u32_le(out_ptr + bytes - 4u);
                out_ptr += bytes;
                px_pos += count;
                continue;
            }
            if (profile == RG_RGI_PROFILE_EXTENDED && b1 == RG_RGI_OP_RAWSPAN)
            {
                if (ptr >= end)
                {
                    return 0;
                }
                size_t count = (size_t)*ptr++;
                size_t bytes = count * 4u;
                uint64_t remaining = pixel_count - px_pos;
                if (count == 0u || (uint64_t)count > remaining || (size_t)(end - ptr) < bytes)
                {
                    return 0;
                }
                memcpy(out_ptr, ptr, bytes);
                px = rg_rgi_load_u32_le(ptr + bytes - 4u);
                ptr += bytes;
                out_ptr += bytes;
                px_pos += count;
                continue;
            }
            if (profile == RG_RGI_PROFILE_EXTENDED && b1 == RG_RGI_OP_LONG_RUN)
            {
                if ((size_t)(end - ptr) < 2u)
                {
                    return 0;
                }
                size_t count = (size_t)rg_rgi_load_u16_le(ptr);
                ptr += 2u;
                uint64_t remaining = pixel_count - px_pos;
                if (count == 0u || (uint64_t)count > remaining)
                {
                    return 0;
                }
                if (px_pos == 0u)
                {
                    index[rg_rgi_hash_pixel_u32(px)] = px;
                }
                rg_rgi_store_run_rgba_u32(out_ptr, count, px);
                out_ptr += count * 4u;
                px_pos += count;
                continue;
            }
            size_t count = (size_t)(b1 & 0x3Fu) + 1u;
            uint64_t remaining = pixel_count - px_pos;
            if ((uint64_t)count > remaining)
            {
                return 0;
            }
            if (px_pos == 0u)
            {
                index[rg_rgi_hash_pixel_u32(px)] = px;
            }
            rg_rgi_store_run_rgba_u32(out_ptr, count, px);
            out_ptr += count * 4u;
            px_pos += count;
            continue;
        }

        index[rg_rgi_hash_pixel_u32(px)] = px;

        rg_rgi_store_pixel_u32(out_ptr, px);
        out_ptr += 4u;
        px_pos++;
    }

    if (ptr != end || !rg_rgi__has_end_marker(end))
    {
        return 0u;
    }
    if (out_width != NULL)
    {
        *out_width = width;
    }
    if (out_height != NULL)
    {
        *out_height = height;
    }
    return out_bytes;
}

RGINLINE size_t rg_rgi_decode_trusted(const void* src,
                                      size_t src_size,
                                      void* dst,
                                      size_t dst_size,
                                      uint32_t* out_width,
                                      uint32_t* out_height)
{
    uint32_t width = 0u;
    uint32_t height = 0u;
    if (dst == NULL || !rg_rgi_read_header(src, src_size, &width, &height))
    {
        return 0u;
    }
    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    if (pixel_count > (uint64_t)(SIZE_MAX / 4u) ||
        dst_size < (size_t)pixel_count * 4u)
    {
        return 0u;
    }
    const uint8_t* data = (const uint8_t*)src;

    if (out_width != NULL)
    {
        *out_width = width;
    }
    if (out_height != NULL)
    {
        *out_height = height;
    }

    return rg_rgi__decode_payload_trusted_profile(data + RG_RGI_HEADER_SIZE,
                                                  width,
                                                  height,
                                                  data[13],
                                                  dst);
}

RGINLINE size_t rg_rgi__decode_payload_trusted_qoi(const void* src,
                                                   uint32_t width,
                                                   uint32_t height,
                                                   void* dst)
{
    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    size_t out_bytes = (size_t)pixel_count * 4u;

    const uint8_t* ptr = (const uint8_t*)src;
    uint8_t* out_ptr = (uint8_t*)dst;

    uint32_t index[64];
    memset(index, 0, sizeof(index));

    uint32_t px = 0xff000000u;
    uint64_t px_pos = 0u;

    while (px_pos < pixel_count)
    {
        uint8_t b1 = *ptr++;
        if (b1 == RG_RGI_OP_RGB)
        {
            px = (px & 0xff000000u) |
                 (uint32_t)ptr[0] |
                 ((uint32_t)ptr[1] << 8u) |
                 ((uint32_t)ptr[2] << 16u);
            ptr += 3u;
        }
        else if (b1 == RG_RGI_OP_RGBA)
        {
            px = rg_rgi_pack_rgba_u32(ptr[0], ptr[1], ptr[2], ptr[3]);
            ptr += 4u;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_INDEX)
        {
            px = index[b1 & 63u];
            rg_rgi_store_pixel_u32(out_ptr, px);
            out_ptr += 4u;
            px_pos++;
            continue;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_DIFF)
        {
            int dr = ((b1 >> 4) & 3) - 2;
            int dg = ((b1 >> 2) & 3) - 2;
            int db = (b1 & 3) - 2;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_LUMA)
        {
            uint8_t b2 = *ptr++;
            int dg = (b1 & 0x3F) - 32;
            int dr = ((b2 >> 4) & 0x0F) - 8 + dg;
            int db = (b2 & 0x0F) - 8 + dg;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else
        {
            if (px_pos == 0u)
            {
                index[rg_rgi_hash_pixel_u32(px)] = px;
            }
            size_t count = (size_t)(b1 & 0x3Fu) + 1u;
            rg_rgi_store_run_rgba_u32(out_ptr, count, px);
            out_ptr += count * 4u;
            px_pos += count;
            continue;
        }

        index[rg_rgi_hash_pixel_u32(px)] = px;
        rg_rgi_store_pixel_u32(out_ptr, px);
        out_ptr += 4u;
        px_pos++;
    }

    return out_bytes;
}

RGINLINE size_t rg_rgi__decode_payload_trusted_profile(const void* src,
                                                       uint32_t width,
                                                       uint32_t height,
                                                       uint8_t profile,
                                                       void* dst)
{
    if (profile > RG_RGI_PROFILE_EXTENDED)
    {
        return 0u;
    }
    if (profile == RG_RGI_PROFILE_QOI)
    {
        return rg_rgi__decode_payload_trusted_qoi(src, width, height, dst);
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    size_t out_bytes = (size_t)pixel_count * 4u;

    const uint8_t* ptr = (const uint8_t*)src;
    uint8_t* out = (uint8_t*)dst;
    uint8_t* out_ptr = out;

    uint32_t index[64];
    memset(index, 0, sizeof(index));

    uint32_t px = 0xff000000u;

    uint64_t px_pos = 0;

    while (px_pos < pixel_count)
    {
        uint8_t b1 = *ptr++;
        if (b1 == RG_RGI_OP_RGB)
        {
            px = (px & 0xff000000u) |
                 (uint32_t)ptr[0] |
                 ((uint32_t)ptr[1] << 8u) |
                 ((uint32_t)ptr[2] << 16u);
            ptr += 3;
        }
        else if (b1 == RG_RGI_OP_RGBA)
        {
            px = rg_rgi_pack_rgba_u32(ptr[0], ptr[1], ptr[2], ptr[3]);
            ptr += 4;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_INDEX)
        {
            px = index[b1 & 63u];
            rg_rgi_store_pixel_u32(out_ptr, px);
            out_ptr += 4u;
            px_pos++;
            continue;
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_DIFF)
        {
            int dr = ((b1 >> 4) & 3) - 2;
            int dg = ((b1 >> 2) & 3) - 2;
            int db = (b1 & 3) - 2;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else if ((b1 & RG_RGI_OP_MASK) == RG_RGI_OP_LUMA)
        {
            uint8_t b2 = *ptr++;
            int dg = (b1 & 0x3F) - 32;
            int dr = ((b2 >> 4) & 0x0F) - 8 + dg;
            int db = (b2 & 0x0F) - 8 + dg;
            uint8_t r = (uint8_t)((uint8_t)(px & 0xffu) + dr);
            uint8_t g = (uint8_t)((uint8_t)((px >> 8u) & 0xffu) + dg);
            uint8_t b = (uint8_t)((uint8_t)((px >> 16u) & 0xffu) + db);
            px = (px & 0xff000000u) |
                 (uint32_t)r |
                 ((uint32_t)g << 8u) |
                 ((uint32_t)b << 16u);
        }
        else
        {
            if (b1 == RG_RGI_OP_COPY)
            {
                size_t count = (size_t)ptr[0] + RG_RGI_COPY_MIN;
                size_t offset = (size_t)rg_rgi_load_u16_le(ptr + 1u);
                ptr += 3u;
                size_t bytes = count * 4u;
                memcpy(out_ptr, out_ptr - offset * 4u, bytes);
                px = rg_rgi_load_u32_le(out_ptr + bytes - 4u);
                out_ptr += bytes;
                px_pos += count;
                continue;
            }
            if (b1 == RG_RGI_OP_RAWSPAN)
            {
                size_t count = (size_t)*ptr++;
                size_t bytes = count * 4u;
                memcpy(out_ptr, ptr, bytes);
                px = rg_rgi_load_u32_le(ptr + bytes - 4u);
                ptr += bytes;
                out_ptr += bytes;
                px_pos += count;
                continue;
            }
            if (b1 == RG_RGI_OP_LONG_RUN)
            {
                size_t count = (size_t)rg_rgi_load_u16_le(ptr);
                ptr += 2u;
                if (px_pos == 0u)
                {
                    index[rg_rgi_hash_pixel_u32(px)] = px;
                }
                rg_rgi_store_run_rgba_u32(out_ptr, count, px);
                out_ptr += count * 4u;
                px_pos += count;
                continue;
            }
            if (px_pos == 0u)
            {
                index[rg_rgi_hash_pixel_u32(px)] = px;
            }
            size_t count = (size_t)(b1 & 0x3Fu) + 1u;
            rg_rgi_store_run_rgba_u32(out_ptr, count, px);
            out_ptr += count * 4u;
            px_pos += count;
            continue;
        }

        index[rg_rgi_hash_pixel_u32(px)] = px;
        rg_rgi_store_pixel_u32(out_ptr, px);
        out_ptr += 4u;
        px_pos++;
    }

    return out_bytes;
}

RGINLINE size_t rg_rgi_encode_bound(uint32_t width, uint32_t height)
{
    if (width == 0u || height == 0u)
    {
        return 0u;
    }
    if (width > RG_RGI_MAX_DIM || height > RG_RGI_MAX_DIM)
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

RGINLINE size_t rg_rgi_encode_workspace_size(void)
{
    return sizeof(RgRgiEncodeWorkspace);
}

RGINLINE size_t rg_rgi_encode_with_workspace(const void* rgba,
                                             uint32_t width,
                                             uint32_t height,
                                             void* dst,
                                             size_t dst_size,
                                             void* workspace,
                                             size_t workspace_size)
{
    if (rgba == NULL || dst == NULL || workspace == NULL)
    {
        return 0u;
    }

    size_t bound = rg_rgi_encode_bound(width, height);
    if (bound == 0u || dst_size < bound)
    {
        return 0u;
    }
    if (workspace_size < sizeof(RgRgiEncodeWorkspace) ||
        !RG_IS_ALIGNED(workspace, RG_ALIGNOF(RgRgiEncodeWorkspace)))
    {
        return 0u;
    }

    uint64_t pixel_count = (uint64_t)width * (uint64_t)height;
    size_t input_size = (size_t)pixel_count * 4u;
    if (rg_rgi__ranges_overlap(rgba, input_size, dst, bound) ||
        rg_rgi__ranges_overlap(rgba, input_size, workspace, sizeof(RgRgiEncodeWorkspace)) ||
        rg_rgi__ranges_overlap(dst, bound, workspace, sizeof(RgRgiEncodeWorkspace)))
    {
        return 0u;
    }
    RgRgiCopyTable* copy_table = &((RgRgiEncodeWorkspace*)workspace)->table;

    uint8_t* out = (uint8_t*)dst;
    out[0] = (uint8_t)RG_RGI_MAGIC_0;
    out[1] = (uint8_t)RG_RGI_MAGIC_1;
    out[2] = (uint8_t)RG_RGI_MAGIC_2;
    out[3] = (uint8_t)RG_RGI_MAGIC_3;
    rg_rgi_store_u32_le(out + 4, width);
    rg_rgi_store_u32_le(out + 8, height);
    out[12] = 4u;
    out[13] = RG_RGI_PROFILE_QOI;

    uint8_t* ptr = out + RG_RGI_HEADER_SIZE;
    const uint8_t* pixels = (const uint8_t*)rgba;
    ptr += rg_rgi_encode_qoi_payload(pixels, pixel_count, ptr);

    size_t baseline_payload_size = (size_t)(ptr - (out + RG_RGI_HEADER_SIZE));
    size_t baseline_size = RG_RGI_HEADER_SIZE + baseline_payload_size + RG_RGI_END_SIZE;
    size_t copy_payload_size = rg_rgi_estimate_rawspan_payload_size_min_copy(pixels,
                                                                             pixel_count,
                                                                             width,
                                                                             1,
                                                                             RG_RGI_COPY_MIN,
                                                                             copy_table);
    size_t copy_size = RG_RGI_HEADER_SIZE + copy_payload_size + RG_RGI_END_SIZE;

    if (copy_size < baseline_size)
    {
        out[13] = RG_RGI_PROFILE_EXTENDED;
        ptr = out + RG_RGI_HEADER_SIZE;
        ptr += rg_rgi_encode_rawspan_payload_min_copy(pixels,
                                                      pixel_count,
                                                      width,
                                                      ptr,
                                                      1,
                                                      RG_RGI_COPY_MIN,
                                                      copy_table);
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

RGINLINE size_t rg_rgi_encode(const void* rgba,
                              uint32_t width,
                              uint32_t height,
                              void* dst,
                              size_t dst_size)
{
    size_t workspace_size = rg_rgi_encode_workspace_size();
    void* workspace = RG_RGI_MALLOC(workspace_size);
    if (workspace == NULL)
    {
        return 0u;
    }
    size_t result = rg_rgi_encode_with_workspace(rgba,
                                                 width,
                                                 height,
                                                 dst,
                                                 dst_size,
                                                 workspace,
                                                 workspace_size);
    RG_RGI_FREE(workspace);
    return result;
}

#endif
