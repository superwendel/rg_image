#include "rg_rgi.h"
#include "rg_rgi.h"

#include <cstdint>
#include <cstring>

int main()
{
    const std::uint8_t pixels[8] = {1, 2, 3, 255, 4, 5, 6, 128};
    std::uint8_t encoded[64] = {};
    std::uint8_t decoded[8] = {};
    std::size_t encoded_size = rg_rgi_encode(pixels, 2u, 1u, encoded, sizeof(encoded));
    if (encoded_size == 0u || std::memcmp(encoded, "rgif", 4u) != 0) return 1;
    std::size_t decoded_size = rg_rgi_decode(encoded,
                                             encoded_size,
                                             decoded,
                                             sizeof(decoded),
                                             nullptr,
                                             nullptr);
    return decoded_size == sizeof(decoded) && std::memcmp(decoded, pixels, sizeof(pixels)) == 0
        ? 0
        : 1;
}
