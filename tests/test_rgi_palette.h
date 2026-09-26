// Independent profile-2 packets: no encoder output is used as an oracle.
#ifndef RG_RGI_TEST_PALETTE_H
#define RG_RGI_TEST_PALETTE_H

static void palette_test_le32(uint8_t* dst, uint32_t value)
{
    for (unsigned i = 0u; i < 4u; ++i) dst[i] = (uint8_t)(value >> (i * 8u));
}

static uint8_t* palette_test_packet(uint32_t width, uint32_t height, uint16_t colors,
                                    const uint8_t* palette, const uint8_t* payload,
                                    size_t payload_size, size_t* size)
{
    *size = 16u + (size_t)colors * 4u + payload_size + 8u;
    uint8_t* packet = (uint8_t*)malloc(*size);
    if (!packet) return NULL;
    memset(packet, 0, *size);
    memcpy(packet, "rgif", 4u);
    palette_test_le32(packet + 4u, width);
    palette_test_le32(packet + 8u, height);
    packet[12] = 4u;
    packet[13] = 2u;
    packet[14] = (uint8_t)colors;
    packet[15] = (uint8_t)(colors >> 8u);
    memcpy(packet + 16u, palette, (size_t)colors * 4u);
    memcpy(packet + 16u + (size_t)colors * 4u, payload, payload_size);
    packet[*size - 1u] = 1u;
    return packet;
}

static int palette_test_guards(const uint8_t* storage, size_t bytes)
{
    for (size_t i = 0u; i < 9u; ++i)
        if (storage[i] != 0xa5u || storage[9u + bytes + i] != 0xa5u) return 0;
    return 1;
}

static int palette_test_valid(const uint8_t* packet, size_t size,
                              const uint8_t* pixels, uint32_t width, uint32_t height)
{
    size_t bytes = (size_t)width * height * 4u;
    uint8_t* guarded = (uint8_t*)malloc(bytes + 18u);
    if (!guarded) return 0;
    int valid = 1;
    for (int trusted = 0; trusted < 2 && valid; ++trusted)
    {
        memset(guarded, 0xa5, bytes + 18u);
        uint32_t decoded_width = 0u, decoded_height = 0u;
        size_t decoded = trusted ?
            rg_rgi_decode_trusted(packet, size, guarded + 9u, bytes, &decoded_width, &decoded_height) :
            rg_rgi_decode(packet, size, guarded + 9u, bytes, &decoded_width, &decoded_height);
        valid = decoded == bytes && decoded_width == width && decoded_height == height &&
                memcmp(guarded + 9u, pixels, bytes) == 0 && palette_test_guards(guarded, bytes);
    }
    free(guarded);
    return valid;
}

static int palette_test_invalid(const uint8_t* packet, size_t size, size_t bytes)
{
    uint8_t* guarded = (uint8_t*)malloc(bytes + 18u);
    if (!guarded) return 0;
    int valid = 1;
    for (int trusted = 0; trusted < 2 && valid; ++trusted)
    {
        memset(guarded, 0xa5, bytes + 18u);
        size_t decoded = trusted ?
            rg_rgi_decode_trusted(packet, size, guarded + 9u, bytes, NULL, NULL) :
            rg_rgi_decode(packet, size, guarded + 9u, bytes, NULL, NULL);
        valid = decoded == 0u && palette_test_guards(guarded, bytes);
    }
    free(guarded);
    return valid;
}

static int palette_test_truncations(const uint8_t* packet, size_t size, size_t bytes)
{
    for (size_t prefix = 0u; prefix < size; ++prefix)
        if (!palette_test_invalid(packet, prefix, bytes)) return 0;
    return 1;
}

static int palette_test_literals(void)
{
    static const uint16_t counts[] = {1u, 2u, 3u, 4u, 5u, 16u, 17u, 255u, 256u};
    static const uint8_t payloads[][4] = {
        {0x02u, 0x00u, 0u, 0u}, {0x02u, 0x02u, 0u, 0u},
        {0x02u, 0x24u, 0u, 0u}, {0x02u, 0x1cu, 0u, 0u},
        {0x02u, 0x40u, 0x01u, 0u}, {0x02u, 0xf0u, 0x01u, 0u},
        {0x02u, 0x00u, 0x10u, 0x01u}, {0x02u, 0x00u, 0xfeu, 0x01u},
        {0x02u, 0x00u, 0xffu, 0x01u}
    };
    static const size_t lengths[] = {2u, 2u, 2u, 2u, 3u, 3u, 4u, 4u, 4u};
    static const uint8_t indices[][3] = {{0u, 0u, 0u}, {0u, 1u, 0u}, {0u, 1u, 2u},
        {0u, 3u, 1u}, {0u, 4u, 1u}, {0u, 15u, 1u}, {0u, 16u, 1u},
        {0u, 254u, 1u}, {0u, 255u, 1u}};
    uint8_t palette[256u * 4u];
    for (uint32_t i = 0u; i < 256u; ++i)
    {
        palette[(size_t)i * 4u] = (uint8_t)i;
        palette[(size_t)i * 4u + 1u] = (uint8_t)(i * 13u);
        palette[(size_t)i * 4u + 2u] = 0u;
        palette[(size_t)i * 4u + 3u] = (uint8_t)(i % 3u ? 255u : 0u);
    }
    for (size_t test = 0u; test < sizeof(counts) / sizeof(counts[0]); ++test)
    {
        uint8_t expected[12];
        for (size_t i = 0u; i < 3u; ++i)
            memcpy(expected + i * 4u, palette + (size_t)indices[test][i] * 4u, 4u);
        size_t size = 0u;
        uint8_t* packet = palette_test_packet(3u, 1u, counts[test], palette,
                                              payloads[test], lengths[test], &size);
        if (!packet) return 0;
        int valid = palette_test_valid(packet, size, expected, 3u, 1u);
        size_t payload = 16u + (size_t)counts[test] * 4u;
        if (test < 6u)
        {
            packet[payload + lengths[test] - 1u] |= 0x80u;
            valid = valid && palette_test_invalid(packet, size, sizeof(expected));
        }
        memcpy(packet + payload, payloads[test], lengths[test]);
        if (counts[test] == 1u || counts[test] == 3u || counts[test] == 5u ||
            counts[test] == 17u || counts[test] == 255u)
        {
            packet[payload + 1u] = (uint8_t)counts[test];
            valid = valid && palette_test_invalid(packet, size, sizeof(expected));
        }
        memcpy(packet + payload, payloads[test], lengths[test]);
        if (counts[test] == 17u)
            valid = valid && palette_test_truncations(packet, size, sizeof(expected));
        free(packet);
        if (!valid) return 0;
    }
    // Equal RGB with different alpha and hidden RGB under alpha zero remain exact.
    static const uint8_t rgba[] = {0u, 0u, 0u, 0u, 1u, 0u, 0u, 0u,
                                   0u, 0u, 0u, 255u};
    static const uint8_t payload[] = {0x02u, 0x24u};
    size_t size = 0u;
    uint8_t* packet = palette_test_packet(3u, 1u, 3u, rgba, payload, sizeof(payload), &size);
    if (!packet) return 0;
    int valid = palette_test_valid(packet, size, rgba, 3u, 1u);
    free(packet);
    return valid;
}

static int palette_test_copy_errors(void)
{
    static const uint8_t palette[] = {10u, 20u, 30u, 0u, 40u, 50u, 60u, 255u};
    static const uint8_t payload[] = {0x03u, 0x0au, 0xc0u, 0x04u, 0x00u, 0x41u};
    uint8_t expected[40];
    for (size_t i = 0u; i < 10u; ++i)
        memcpy(expected + i * 4u, palette + (i < 8u ? i % 2u : 1u) * 4u, 4u);
    size_t size = 0u;
    uint8_t* packet = palette_test_packet(10u, 1u, 2u, palette, payload, sizeof(payload), &size);
    if (!packet) return 0;
    int valid = palette_test_valid(packet, size, expected, 10u, 1u) &&
                palette_test_truncations(packet, size, sizeof(expected)) &&
                palette_test_invalid(packet, size, sizeof(expected) - 1u) &&
                palette_test_invalid(packet, size, 0u) &&
                palette_test_invalid(NULL, size, sizeof(expected));
    static const struct { size_t offset; uint8_t value; } mutations[] = {
        {0u, 'X'}, {4u, 0u}, {8u, 0u}, {12u, 3u}, {14u, 0u}, {15u, 1u},
        {26u, 0xc1u}, {27u, 0u}, {27u, 3u}, {27u, 5u}, {28u, 0xffu},
        {29u, 0x42u}, {29u, 0x82u}, {29u, 0xbfu}, {37u, 0u}
    };
    for (size_t i = 0u; i < sizeof(mutations) / sizeof(mutations[0]); ++i)
    {
        size_t offset = mutations[i].offset;
        uint8_t saved = packet[offset];
        packet[offset] = mutations[i].value;
        valid = valid && palette_test_invalid(packet, size, sizeof(expected));
        packet[offset] = saved;
    }
    packet[14u] = 1u;
    packet[15u] = 1u;
    valid = valid && palette_test_invalid(packet, size, sizeof(expected)); // 257 colors.
    packet[14u] = 2u;
    packet[15u] = 0u;
    palette_test_le32(packet + 4u, 16385u);
    valid = valid && palette_test_invalid(packet, size, sizeof(expected));
    palette_test_le32(packet + 4u, 10u);
    palette_test_le32(packet + 8u, UINT32_MAX);
    valid = valid && palette_test_invalid(packet, size, sizeof(expected));
    palette_test_le32(packet + 8u, 1u);
    uint8_t extra[96], overlap[96], saved[96];
    memcpy(extra, packet, size);
    extra[size] = 0u;
    valid = valid && palette_test_invalid(extra, size + 1u, sizeof(expected));
    memcpy(extra, packet, size - 8u);
    extra[size - 8u] = 0x40u;
    memcpy(extra + size - 7u, packet + size - 8u, 8u);
    valid = valid && palette_test_invalid(extra, size + 1u, sizeof(expected));
    for (int trusted = 0; trusted < 2 && valid; ++trusted)
    {
        memset(overlap, 0xa5, sizeof(overlap));
        memcpy(overlap + 9u, packet, size);
        memcpy(saved, overlap, sizeof(saved));
        size_t result = trusted ?
            rg_rgi_decode_trusted(overlap + 9u, size, overlap + 10u, sizeof(expected), NULL, NULL) :
            rg_rgi_decode(overlap + 9u, size, overlap + 10u, sizeof(expected), NULL, NULL);
        valid = result == 0u && memcmp(overlap, saved, sizeof(overlap)) == 0;
        result = trusted ? rg_rgi_decode_trusted(packet, size, NULL, sizeof(expected), NULL, NULL) :
                           rg_rgi_decode(packet, size, NULL, sizeof(expected), NULL, NULL);
        valid = valid && result == 0u;
    }
    free(packet);
    return valid;
}

static int palette_test_long_tokens(void)
{
    static const uint8_t palette[] = {7u, 19u, 37u, 0u};
    static const uint16_t copies[] = {4u, 67u, 68u, 259u};
    for (size_t test = 0u; test < sizeof(copies) / sizeof(copies[0]); ++test)
    {
        uint16_t count = copies[test];
        uint8_t payload[] = {0x81u, (uint8_t)count, (uint8_t)(count >> 8u),
            0x80u, (uint8_t)(count - 4u), (uint8_t)count, (uint8_t)(count >> 8u)};
        size_t bytes = (size_t)count * 8u, size = 0u;
        uint8_t* expected = (uint8_t*)malloc(bytes);
        uint8_t* packet = palette_test_packet((uint32_t)count * 2u, 1u, 1u,
                                              palette, payload, sizeof(payload), &size);
        if (!expected || !packet) { free(expected); free(packet); return 0; }
        for (size_t i = 0u; i < bytes; i += 4u) memcpy(expected + i, palette, 4u);
        int valid = palette_test_valid(packet, size, expected, (uint32_t)count * 2u, 1u);
        packet[25u] = (uint8_t)(count - 1u);
        valid = valid && palette_test_invalid(packet, size, bytes);
        free(packet);
        if (count <= 67u)
        {
            uint8_t short_payload[] = {0x81u, (uint8_t)count, 0u,
                (uint8_t)(0xc0u | (count - 4u)), (uint8_t)count, 0u};
            packet = palette_test_packet((uint32_t)count * 2u, 1u, 1u,
                                          palette, short_payload, sizeof(short_payload), &size);
            valid = valid && packet != NULL &&
                    palette_test_valid(packet, size, expected, (uint32_t)count * 2u, 1u);
            free(packet);
        }
        free(expected);
        if (!valid) return 0;
    }
    static const uint8_t payload[] = {0x81u, 0xffu, 0xffu, 0xc1u, 0xffu, 0xffu};
    size_t bytes = 65540u * 4u, size = 0u;
    uint8_t* expected = (uint8_t*)malloc(bytes);
    uint8_t* packet = palette_test_packet(10u, 6554u, 1u, palette, payload, sizeof(payload), &size);
    if (!expected || !packet) { free(expected); free(packet); return 0; }
    for (size_t i = 0u; i < bytes; i += 4u) memcpy(expected + i, palette, 4u);
    int valid = palette_test_valid(packet, size, expected, 10u, 6554u);
    free(packet); free(expected);
    static const uint8_t one_run[] = {0x81u, 0x01u, 0x00u};
    packet = palette_test_packet(1u, 1u, 1u, palette, one_run, sizeof(one_run), &size);
    if (!packet) return 0;
    valid = valid && palette_test_valid(packet, size, palette, 1u, 1u);
    packet[21u] = 0u;
    valid = valid && palette_test_invalid(packet, size, 4u);
    packet[21u] = 2u;
    valid = valid && palette_test_invalid(packet, size, 4u);
    free(packet);
    return valid;
}

static void test_palette_decode(void)
{
    TEST_ASSERT(palette_test_literals(), "profile-2 packed indices, RGBA, and canonical padding");
    TEST_ASSERT(palette_test_copy_errors(), "profile-2 COPY/RUN, truncations, malformed input, guards");
    TEST_ASSERT(palette_test_long_tokens(), "profile-2 long COPY/RUN and maximum offsets");
    TEST_PASS();
}

#endif
