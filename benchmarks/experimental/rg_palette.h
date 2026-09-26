// Benchmark-only RGIP experiment. This is not an RGI format profile or public API.
#ifndef RG_IMAGE_EXPERIMENTAL_PALETTE_H
#define RG_IMAGE_EXPERIMENTAL_PALETTE_H

#include "rg_rgi.h"

#define RG_PALETTE_HEADER_SIZE 16u
#define RG_PALETTE_END_SIZE 8u

static u8 rg_palette_bits(u16 colors)
{
	return colors <= 2u ? 1u : colors <= 4u ? 2u : colors <= 16u ? 4u : 8u;
}

static size_t rg_palette_literal_cost(size_t count, u8 bits)
{
	return (count + 63u) / 64u + (count * bits + 7u) / 8u;
}

static int rg_palette_token_wins(size_t pending, size_t count, size_t token_bytes,
                                 u8 bits, int terminal)
{
	if (terminal)
		return rg_palette_literal_cost(pending, bits) + token_bytes <=
		       rg_palette_literal_cost(pending + count, bits);
	// A split inside an existing literal can add another header and padding
	// byte. Budget both against the minimum packed bytes removed, independent
	// of the next span's phase.
	// This keeps lone/paired repeats inside low-bit-depth literal packets.
	return token_bytes + (pending != 0u) + ((pending * bits & 7u) != 0u) <= count * bits / 8u;
}

static size_t rg_palette_run(const u8* pixels, size_t count, size_t pos, u32 previous)
{
	size_t run = 0;
	while (pos + run < count && run < 65535u &&
	       rg_rgi_load_u32_le(pixels + (pos + run) * 4u) == previous) ++run;
	return run;
}

static size_t rg_palette_run_cost(size_t run)
{
	return run <= 64u ? 1u : run <= 128u ? 2u : 3u;
}

// NULL with out_colors=257 means ineligible. All other NULL results are errors.
// The caller owns the returned allocation. RGB beneath zero alpha is preserved.
static u8* rg_palette_encode(const void* rgba, u32 width, u32 height,
                             size_t* out_size, u16* out_colors)
{
	if (out_size) *out_size = 0;
	if (out_colors) *out_colors = 0;
	if (!rgba || !out_size || !out_colors || !rg_rgi_encode_bound(width, height)) return NULL;
	size_t count = (size_t)width * height;
	if (count > (SIZE_MAX - 1048u) / 2u) return NULL;
	const u8* pixels = (const u8*)rgba;
	u8* indices = (u8*)malloc(count);
	if (!indices) return NULL;
	u32 palette[256];
	u16 slots[512] = {0};
	u16 colors = 0;
	for (size_t i = 0; i < count; ++i)
	{
		u32 color = rg_rgi_load_u32_le(pixels + i * 4u);
		u32 slot = (color * 2654435761u) >> 23u;
		while (slots[slot] && palette[slots[slot] - 1u] != color) slot = (slot + 1u) & 511u;
		if (!slots[slot])
		{
			if (colors == 256u) { *out_colors = 257u; free(indices); return NULL; }
			palette[colors] = color;
			slots[slot] = ++colors;
		}
		indices[i] = (u8)(slots[slot] - 1u);
	}
	*out_colors = colors;
	u8 bits = rg_palette_bits(colors);
	// Every token costs at most two bytes per covered pixel. Palette/header
	// overhead is bounded separately, regardless of literal segmentation.
	u8* output = (u8*)malloc(1048u + count * 2u);
	RgRgiCopyTable* table = (RgRgiCopyTable*)malloc(sizeof(*table));
	if (!output || !table) { free(output); free(table); free(indices); return NULL; }
	memset(table->counts, 0, sizeof(table->counts));
	memcpy(output, "RGIP", 4u);
	rg_rgi_store_u32_le(output + 4u, width);
	rg_rgi_store_u32_le(output + 8u, height);
	rg_rgi_store_u16_le(output + 12u, colors);
	output[14] = output[15] = 0u;
	u8* ptr = output + RG_PALETTE_HEADER_SIZE;
	for (u16 i = 0; i < colors; ++i) { rg_rgi_store_u32_le(ptr, palette[i]); ptr += 4u; }
	u32 previous = palette[0];
	size_t pos = 0;
	while (pos < count)
	{
		size_t run = rg_palette_run(pixels, count, pos, previous);
		if (run && rg_palette_token_wins(0u, run, rg_palette_run_cost(run), bits, pos + run == count))
		{
			if (run <= 128u)
			{
				size_t first = run < 64u ? run : 64u;
				*ptr++ = (u8)(0x40u | (first - 1u));
				if (run > first) *ptr++ = (u8)(0x40u | (run - first - 1u));
			}
			else { *ptr++ = 0x81u; rg_rgi_store_u16_le(ptr, (u16)run); ptr += 2u; }
			rg_rgi_copy_table_insert_range(table, pixels, count, pos, run);
			pos += run;
			continue;
		}
		u16 offset = 0;
		size_t copy = rg_rgi_find_copy(table, pixels, count, width, pos, &offset);
		size_t copy_bytes = copy <= 67u ? 3u : 4u;
		if (copy && rg_palette_token_wins(0u, copy, copy_bytes, bits, pos + copy == count))
		{
			if (copy <= 67u) *ptr++ = (u8)(0xc0u | (copy - 4u));
			else { *ptr++ = 0x80u; *ptr++ = (u8)(copy - 4u); }
			rg_rgi_store_u16_le(ptr, offset); ptr += 2u;
			previous = rg_rgi_load_u32_le(pixels + (pos + copy - 1u) * 4u);
			rg_rgi_copy_table_insert_range(table, pixels, count, pos, copy);
			pos += copy;
			continue;
		}
		size_t start = pos, span = 0;
		while (pos < count && span < 64u)
		{
			if (span)
			{
				run = rg_palette_run(pixels, count, pos, previous);
				if (run && rg_palette_token_wins(span, run, rg_palette_run_cost(run), bits, pos + run == count)) break;
				copy = rg_rgi_find_copy(table, pixels, count, width, pos, &offset);
				copy_bytes = copy <= 67u ? 3u : 4u;
				if (copy && rg_palette_token_wins(span, copy, copy_bytes, bits, pos + copy == count)) break;
			}
			previous = rg_rgi_load_u32_le(pixels + pos * 4u);
			rg_rgi_copy_table_insert(table, pixels, count, pos);
			++pos; ++span;
		}
		*ptr++ = (u8)(span - 1u);
		size_t bytes = (span * bits + 7u) / 8u;
		memset(ptr, 0, bytes);
		for (size_t i = 0; i < span; ++i)
			ptr[i * bits / 8u] |= (u8)(indices[start + i] << ((i * bits) & 7u));
		ptr += bytes;
	}
	memset(ptr, 0, RG_PALETTE_END_SIZE); ptr[7] = 1u; ptr += RG_PALETTE_END_SIZE;
	*out_size = (size_t)(ptr - output);
	free(table); free(indices);
	return output;
}

static size_t rg_palette_decode(const void* src, size_t src_size, void* dst, size_t dst_size,
                                u32* out_width, u32* out_height)
{
	if (!src || !dst || src_size < RG_PALETTE_HEADER_SIZE + 4u + RG_PALETTE_END_SIZE) return 0u;
	const u8* data = (const u8*)src;
	if (memcmp(data, "RGIP", 4u) || data[14] || data[15]) return 0u;
	u32 width = rg_rgi_load_u32_le(data + 4u), height = rg_rgi_load_u32_le(data + 8u);
	if (!rg_rgi_encode_bound(width, height)) return 0u;
	size_t count = (size_t)width * height, bytes = count * 4u;
	if (dst_size < bytes || rg_rgi__ranges_overlap(src, src_size, dst, bytes)) return 0u;
	u16 colors = rg_rgi_load_u16_le(data + 12u);
	if (!colors || colors > 256u || src_size < RG_PALETTE_HEADER_SIZE + (size_t)colors * 4u + RG_PALETTE_END_SIZE) return 0u;
	const u8* end = data + src_size - RG_PALETTE_END_SIZE;
	if (!rg_rgi__has_end_marker(end)) return 0u;
	u32 palette[256];
	const u8* ptr = data + RG_PALETTE_HEADER_SIZE;
	for (u16 i = 0; i < colors; ++i) { palette[i] = rg_rgi_load_u32_le(ptr); ptr += 4u; }
	u8 bits = rg_palette_bits(colors);
	u32 mask = (1u << bits) - 1u, previous = palette[0];
	u8* out = (u8*)dst;
	size_t pos = 0;
	while (pos < count)
	{
		if (ptr == end) return 0u;
		u8 op = *ptr++;
		if (op < 0x40u)
		{
			size_t span = (size_t)op + 1u, packed = (span * bits + 7u) / 8u;
			if (span > count - pos || packed > (size_t)(end - ptr)) return 0u;
			u32 used = (u32)(span * bits) & 7u;
			if (used && (ptr[packed - 1u] >> used)) return 0u;
			for (size_t i = 0; i < span; ++i)
			{
				u32 index = (ptr[i * bits / 8u] >> ((i * bits) & 7u)) & mask;
				if (index >= colors) return 0u;
				previous = palette[index];
				rg_rgi_store_pixel_u32(out + (pos + i) * 4u, previous);
			}
			ptr += packed; pos += span;
		}
		else if (op < 0x80u || op == 0x81u)
		{
			size_t run = (size_t)(op & 63u) + 1u;
			if (op == 0x81u)
			{
				if ((size_t)(end - ptr) < 2u) return 0u;
				run = rg_rgi_load_u16_le(ptr); ptr += 2u;
			}
			if (!run || run > count - pos) return 0u;
			rg_rgi_store_run_rgba_u32(out + pos * 4u, run, previous); pos += run;
		}
		else if (op >= 0xc0u || op == 0x80u)
		{
			size_t copy = (size_t)(op & 63u) + 4u;
			if (op == 0x80u)
			{
				if (ptr == end) return 0u;
				copy = (size_t)*ptr++ + 4u;
			}
			if ((size_t)(end - ptr) < 2u) return 0u;
			size_t offset = rg_rgi_load_u16_le(ptr); ptr += 2u;
			if (copy > count - pos || offset < copy || offset > pos) return 0u;
			memcpy(out + pos * 4u, out + (pos - offset) * 4u, copy * 4u);
			pos += copy;
			previous = rg_rgi_load_u32_le(out + (pos - 1u) * 4u);
		}
		else return 0u;
	}
	if (ptr != end) return 0u;
	if (out_width) *out_width = width;
	if (out_height) *out_height = height;
	return bytes;
}

#endif
