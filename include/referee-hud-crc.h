#ifndef REFEREE_HUD_CRC_H
#define REFEREE_HUD_CRC_H

#include <cstdint>

namespace RefereeHudCrc {

constexpr uint8_t kCrc8Init = 0xFF;
constexpr uint16_t kCrc16Init = 0xFFFF;

uint8_t calculateCrc8(const uint8_t* data, uint32_t length, uint8_t init = kCrc8Init);
uint16_t calculateCrc16(const uint8_t* data, uint32_t length, uint16_t init = kCrc16Init);
void appendCrc8(uint8_t* data, uint32_t length);
void appendCrc16(uint8_t* data, uint32_t length);

} // namespace RefereeHudCrc

#endif
