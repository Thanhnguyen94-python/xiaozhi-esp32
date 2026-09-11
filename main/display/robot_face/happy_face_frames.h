#pragma once

#include <cstdint>

namespace robot_face {

constexpr uint16_t kHappyBitmapWidth = 128;
constexpr uint16_t kHappyBitmapHeight = 64;
constexpr uint32_t kHappyBitmapDataSize = (kHappyBitmapWidth * kHappyBitmapHeight) / 8;
constexpr uint16_t kHappyFrameCount = 3;

extern const uint8_t Fr2_GIF1[];
extern const uint8_t Fr2_GIF2[];
extern const uint8_t Fr2_GIF3[];


} // namespace robot_face
