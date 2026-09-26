// Independent correctness checks for the benchmark-only RGIP experiment.
#ifndef RG_IMAGE_TEST_PALETTE_H
#define RG_IMAGE_TEST_PALETTE_H

static void bench_palette_test_require(int condition, const char* message)
{
    if (!condition) bench_die(message);
}

static void bench_palette_test_le32(u8* dst, u32 value)
{
    dst[0] = (u8)value;
    dst[1] = (u8)(value >> 8u);
    dst[2] = (u8)(value >> 16u);
    dst[3] = (u8)(value >> 24u);
}

static u8* bench_palette_test_packet(u32 width, u32 height, u16 colors,
                                     const u8* palette, const u8* payload,
                                     size_t payload_size, size_t* size)
{
    *size = 16u + (size_t)colors * 4u + payload_size + 8u;
    u8* packet = (u8*)malloc(*size);
    if (!packet) bench_die("palette selftest packet allocation");
    memset(packet, 0, *size);
    memcpy(packet, "RGIP", 4u);
    bench_palette_test_le32(packet + 4u, width);
    bench_palette_test_le32(packet + 8u, height);
    packet[12] = (u8)colors;
    packet[13] = (u8)(colors >> 8u);
    memcpy(packet + 16u, palette, (size_t)colors * 4u);
    memcpy(packet + 16u + (size_t)colors * 4u, payload, payload_size);
    packet[*size - 1u] = 1u;
    return packet;
}

static void bench_palette_test_guards(const u8* storage, size_t bytes)
{
    for (size_t i = 0u; i < 9u; ++i)
    {
        bench_palette_test_require(storage[i] == 0xa5u &&
                                  storage[9u + bytes + i] == 0xa5u,
                                  "palette selftest destination guard changed");
    }
}

static void bench_palette_test_valid(const u8* packet, size_t size,
                                    const u8* pixels, u32 width, u32 height)
{
    size_t bytes = (size_t)width * height * 4u;
    u8* guarded = (u8*)malloc(bytes + 18u);
    if (!guarded) bench_die("palette selftest destination allocation");
    memset(guarded, 0xa5, bytes + 18u);
    u32 decoded_width = 0u, decoded_height = 0u;
    size_t decoded = rg_palette_decode(packet, size, guarded + 9u, bytes,
                                       &decoded_width, &decoded_height);
    bench_palette_test_require(decoded == bytes && decoded_width == width &&
                              decoded_height == height,
                              "palette selftest valid stream rejected");
    bench_palette_test_require(memcmp(guarded + 9u, pixels, bytes) == 0,
                              "palette selftest decoded pixels differ");
    bench_palette_test_guards(guarded, bytes);
    free(guarded);
}

static void bench_palette_test_invalid(const u8* packet, size_t size, size_t bytes)
{
    u8* guarded = (u8*)malloc(bytes + 18u);
    if (!guarded) bench_die("palette selftest destination allocation");
    memset(guarded, 0xa5, bytes + 18u);
    bench_palette_test_require(rg_palette_decode(packet, size, guarded + 9u,
                                                bytes, NULL, NULL) == 0u,
                              "palette selftest malformed stream accepted");
    bench_palette_test_guards(guarded, bytes);
    free(guarded);
}

static void bench_palette_test_truncations(const u8* packet, size_t size,
                                          size_t bytes)
{
    for (size_t prefix = 0u; prefix < size; ++prefix)
        bench_palette_test_invalid(packet, prefix, bytes);
}

static void bench_palette_test_roundtrips(void)
{
    static const u16 counts[] = {1u, 2u, 3u, 4u, 5u, 16u, 17u, 256u};
    for (size_t test = 0u; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        u16 colors = counts[test];
        u32 width = (u32)colors * 2u + 3u;
        u8* pixels = (u8*)malloc((size_t)width * 4u);
        if (!pixels) bench_die("palette selftest pixel allocation");
        for (u32 i = 0u; i < width; ++i)
        {
            u32 index = i % colors;
            pixels[(size_t)i * 4u] = (u8)index;
            pixels[(size_t)i * 4u + 1u] = (u8)(index * 17u);
            pixels[(size_t)i * 4u + 2u] = (u8)(index * 31u);
            pixels[(size_t)i * 4u + 3u] = (u8)(index % 3u ? 255u : 0u);
        }
        size_t size = 0u;
        u16 encoded_colors = 0u;
        u8* encoded = rg_palette_encode(pixels, width, 1u, &size, &encoded_colors);
        bench_palette_test_require(encoded != NULL && encoded_colors == colors,
                                  "palette selftest eligible encode failed");
        bench_palette_test_require(size >= 24u + (size_t)colors * 4u &&
                                  encoded[12] == (u8)colors &&
                                  encoded[13] == (u8)(colors >> 8u),
                                  "palette selftest palette count differs");
        bench_palette_test_valid(encoded, size, pixels, width, 1u);
        if (colors == 17u)
            bench_palette_test_truncations(encoded, size, (size_t)width * 4u);
        free(encoded);
        free(pixels);
    }

    // The all-zero key, hidden transparent RGB, and alpha-only changes are distinct.
    static const u8 rgba[] = {0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
                             0u, 0u, 0u, 255u, 0u, 0u, 0u, 0u};
    size_t size = 0u;
    u16 colors = 0u;
    u8* encoded = rg_palette_encode(rgba, 4u, 1u, &size, &colors);
    bench_palette_test_require(encoded != NULL && colors == 3u,
                              "palette selftest RGBA keys were merged");
    bench_palette_test_valid(encoded, size, rgba, 4u, 1u);
    free(encoded);

    // Detect colour 257 both at the final pixel and before a trailing region.
    u8 many[273u * 4u];
    for (u32 i = 0u; i < 273u; ++i)
    {
        u32 value = i < 257u ? i : 0u;
        many[(size_t)i * 4u] = (u8)value;
        many[(size_t)i * 4u + 1u] = (u8)(value >> 8u);
        many[(size_t)i * 4u + 2u] = 0u;
        many[(size_t)i * 4u + 3u] = 0u;
    }
    for (u32 width = 257u; width <= 273u; width += 16u)
    {
        colors = 0u;
        encoded = rg_palette_encode(many, width, 1u, &size, &colors);
        bench_palette_test_require(encoded == NULL && colors == 257u,
                                  "palette selftest 257-colour fallback failed");
    }
}

static void bench_palette_test_literal_packets(void)
{
    // Independent literals exercise low-bit-first order and partial final bytes.
    static const u16 counts[] = {2u, 3u, 5u, 17u, 255u, 256u};
    static const u8 payloads[][4] = {
        {0x02u, 0x02u, 0u, 0u},       // 1-bit [0,1,0]
        {0x02u, 0x24u, 0u, 0u},       // 2-bit [0,1,2]
        {0x02u, 0x40u, 0x01u, 0u},    // 4-bit [0,4,1]
        {0x02u, 0x00u, 0x10u, 0x01u}, // 8-bit [0,16,1]
        {0x02u, 0x00u, 0xfeu, 0x01u}, // 8-bit [0,254,1]
        {0x02u, 0x00u, 0xffu, 0x01u}  // 8-bit [0,255,1]
    };
    static const size_t lengths[] = {2u, 2u, 3u, 4u, 4u, 4u};
    static const u8 indices[][3] = {{0u, 1u, 0u}, {0u, 1u, 2u},
                                   {0u, 4u, 1u}, {0u, 16u, 1u}, {0u, 254u, 1u},
                                   {0u, 255u, 1u}};
    u8 palette[256u * 4u];
    for (u32 i = 0u; i < 256u; ++i)
    {
        palette[(size_t)i * 4u] = (u8)i;
        palette[(size_t)i * 4u + 1u] = (u8)(i * 13u);
        palette[(size_t)i * 4u + 2u] = 0u;
        palette[(size_t)i * 4u + 3u] = 255u;
    }
    for (size_t test = 0u; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        u8 expected[12];
        for (size_t i = 0u; i < 3u; ++i)
            memcpy(expected + i * 4u, palette + (size_t)indices[test][i] * 4u, 4u);
        size_t size = 0u;
        u8* packet = bench_palette_test_packet(3u, 1u, counts[test], palette,
                                               payloads[test], lengths[test], &size);
        bench_palette_test_valid(packet, size, expected, 3u, 1u);
        size_t payload = 16u + (size_t)counts[test] * 4u;
        if (test < 3u)
        {
            packet[payload + lengths[test] - 1u] |= 0x80u;
            bench_palette_test_invalid(packet, size, sizeof(expected));
        }
        memcpy(packet + payload, payloads[test], lengths[test]);
        if (counts[test] == 3u || counts[test] == 5u || counts[test] == 17u ||
            counts[test] == 255u)
        {
            packet[payload + 1u] = (u8)(counts[test] == 3u ? 0x27u : counts[test]);
            bench_palette_test_invalid(packet, size, sizeof(expected));
        }
        free(packet);
    }
}

static void bench_palette_test_run_cost(void)
{
    // A + 17 B pixels + ABABAB fits in one four-byte packed literal. Splitting
    // after A/B wastes padding and a second literal header around RUN16.
    static const u8 palette[] = {0u, 0u, 0u, 0u, 1u, 0u, 0u, 255u};
    u8 pixels[24u * 4u];
    for (size_t i = 0u; i < 24u; ++i)
    {
        size_t index = i == 0u ? 0u : (i < 18u ? 1u : i % 2u);
        memcpy(pixels + i * 4u, palette + index * 4u, 4u);
    }
    size_t size = 0u;
    u16 colors = 0u;
    u8* encoded = rg_palette_encode(pixels, 24u, 1u, &size, &colors);
    bench_palette_test_require(encoded != NULL && colors == 2u && size <= 36u,
                              "palette selftest short run inflated packed literals");
    bench_palette_test_valid(encoded, size, pixels, 24u, 1u);
    free(encoded);
}

static void bench_palette_test_copy_and_errors(void)
{
    static const u8 palette[] = {10u, 20u, 30u, 0u, 40u, 50u, 60u, 255u};
    static const u8 payload[] = {0x03u, 0x0au, 0xc0u, 0x04u, 0x00u, 0x41u};
    u8 expected[40];
    for (size_t i = 0u; i < 10u; ++i)
        memcpy(expected + i * 4u, palette + ((i < 8u ? i % 2u : 1u) * 4u), 4u);
    size_t size = 0u;
    u8* packet = bench_palette_test_packet(10u, 1u, 2u, palette, payload,
                                           sizeof(payload), &size);
    bench_palette_test_valid(packet, size, expected, 10u, 1u);
    bench_palette_test_truncations(packet, size, sizeof(expected));
    bench_palette_test_invalid(packet, size, sizeof(expected) - 1u);
    bench_palette_test_invalid(packet, size, 0u);
    bench_palette_test_invalid(NULL, size, sizeof(expected));
    bench_palette_test_require(rg_palette_decode(packet, size, NULL,
                                                sizeof(expected), NULL, NULL) == 0u,
                              "palette selftest null destination accepted");
    u8 overlap[96], unchanged[96];
    memset(overlap, 0xa5, sizeof(overlap));
    bench_palette_test_require(size + 9u <= sizeof(overlap),
                              "palette selftest overlap fixture too large");
    memcpy(overlap + 9u, packet, size);
    memcpy(unchanged, overlap, sizeof(overlap));
    bench_palette_test_require(rg_palette_decode(overlap + 9u, size, overlap + 10u,
                                                sizeof(expected), NULL, NULL) == 0u &&
                              memcmp(overlap, unchanged, sizeof(overlap)) == 0,
                              "palette selftest overlapping output accepted");

    // Each mutation changes one independent validity condition.
    static const struct { size_t offset; u8 value; } mutations[] = {
        {0u, 'X'}, {4u, 0u}, {8u, 0u}, {12u, 0u}, {13u, 1u},
        {14u, 1u}, {15u, 1u}, {26u, 0xc1u}, {27u, 0u},
        {27u, 3u}, {27u, 5u}, {28u, 0xffu}, {29u, 0x42u},
        {29u, 0x82u}, {29u, 0xbfu}, {37u, 0u}
    };
    for (size_t i = 0u; i < sizeof(mutations) / sizeof(mutations[0]); ++i)
    {
        size_t offset = mutations[i].offset;
        u8 saved = packet[offset];
        packet[offset] = mutations[i].value;
        bench_palette_test_invalid(packet, size, sizeof(expected));
        packet[offset] = saved;
    }
    bench_palette_test_le32(packet + 4u, 16385u);
    bench_palette_test_invalid(packet, size, sizeof(expected));
    bench_palette_test_le32(packet + 4u, 10u);
    bench_palette_test_le32(packet + 8u, 0xffffffffu);
    bench_palette_test_invalid(packet, size, sizeof(expected));
    bench_palette_test_le32(packet + 8u, 1u);

    u8* extra = (u8*)malloc(size + 1u);
    if (!extra) bench_die("palette selftest trailing data allocation");
    memcpy(extra, packet, size);
    extra[size] = 0u;
    bench_palette_test_invalid(extra, size + 1u, sizeof(expected));
    memcpy(extra, packet, size - 8u);
    extra[size - 8u] = 0x40u;
    memcpy(extra + size - 7u, packet + size - 8u, 8u);
    bench_palette_test_invalid(extra, size + 1u, sizeof(expected));
    free(extra);
    free(packet);
}

static void bench_palette_test_long_tokens(void)
{
    static const u8 palette[] = {7u, 19u, 37u, 0u};
    static const u16 copies[] = {4u, 67u, 68u, 259u};
    for (size_t test = 0u; test < sizeof(copies) / sizeof(copies[0]); ++test)
    {
        u16 count = copies[test];
        // Short lengths in longCOPY are valid reader inputs too.
        u8 payload[] = {0x81u, (u8)count, (u8)(count >> 8u),
                        0x80u, (u8)(count - 4u), (u8)count, (u8)(count >> 8u)};
        size_t bytes = (size_t)count * 8u;
        u8* expected = (u8*)malloc(bytes);
        if (!expected) bench_die("palette selftest long copy allocation");
        for (size_t i = 0u; i < bytes; i += 4u) memcpy(expected + i, palette, 4u);
        size_t size = 0u;
        u8* packet = bench_palette_test_packet((u32)count * 2u, 1u, 1u, palette,
                                               payload, sizeof(payload), &size);
        bench_palette_test_valid(packet, size, expected, (u32)count * 2u, 1u);
        packet[25u] = (u8)(count - 1u); // COPY count now exceeds its offset.
        bench_palette_test_invalid(packet, size, bytes);
        free(packet);
        free(expected);
    }

    // Maximum u16 run and offset, followed by a legal non-overlapping COPY.
    static const u8 payload[] = {0x81u, 0xffu, 0xffu, 0xc1u, 0xffu, 0xffu};
    size_t bytes = 65540u * 4u;
    u8* expected = (u8*)malloc(bytes);
    if (!expected) bench_die("palette selftest long run allocation");
    for (size_t i = 0u; i < bytes; i += 4u) memcpy(expected + i, palette, 4u);
    size_t size = 0u;
    u8* packet = bench_palette_test_packet(10u, 6554u, 1u, palette, payload,
                                           sizeof(payload), &size);
    bench_palette_test_valid(packet, size, expected, 10u, 6554u);
    free(packet);
    free(expected);

    static const u8 one_run[] = {0x81u, 0x01u, 0x00u};
    packet = bench_palette_test_packet(1u, 1u, 1u, palette, one_run,
                                       sizeof(one_run), &size);
    bench_palette_test_valid(packet, size, palette, 1u, 1u);
    packet[21u] = 0u;
    bench_palette_test_invalid(packet, size, 4u); // Zero-length longRUN.
    packet[21u] = 2u;
    bench_palette_test_invalid(packet, size, 4u); // RUN exceeds remaining pixels.
    free(packet);
}

static void bench_palette_selftest(void)
{
    bench_palette_test_roundtrips();
    bench_palette_test_literal_packets();
    bench_palette_test_run_cost();
    bench_palette_test_copy_and_errors();
    bench_palette_test_long_tokens();
}

#endif
