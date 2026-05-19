#include "referee-hud-crc.h"

namespace RefereeHudCrc {

uint8_t calculateCrc8(const uint8_t* data, uint32_t length, uint8_t init) {
    if (data == nullptr) {
        return 0;
    }

    uint8_t crc = init;
    while (length-- != 0U) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x01U) != 0U ? static_cast<uint8_t>((crc >> 1U) ^ 0x8CU)
                                      : static_cast<uint8_t>(crc >> 1U);
        }
    }
    return crc;
}

uint16_t calculateCrc16(const uint8_t* data, uint32_t length, uint16_t init) {
    if (data == nullptr) {
        return kCrc16Init;
    }

    uint16_t crc = init;
    while (length-- != 0U) {
        crc ^= *data++;
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x0001U) != 0U ? static_cast<uint16_t>((crc >> 1U) ^ 0x8408U)
                                        : static_cast<uint16_t>(crc >> 1U);
        }
    }
    return crc;
}

void appendCrc8(uint8_t* data, uint32_t length) {
    if (data == nullptr || length < 1U) {
        return;
    }
    data[length - 1U] = calculateCrc8(data, length - 1U);
}

void appendCrc16(uint8_t* data, uint32_t length) {
    if (data == nullptr || length < 2U) {
        return;
    }

    const uint16_t crc = calculateCrc16(data, length - 2U);
    data[length - 2U] = static_cast<uint8_t>(crc & 0x00FFU);
    data[length - 1U] = static_cast<uint8_t>((crc >> 8U) & 0x00FFU);
}

} // namespace RefereeHudCrc
