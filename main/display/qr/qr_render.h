#pragma once
#include <cstdint>
#include <cstddef>

// Sinh QR code dạng bitmap I1 cho LVGL.
// Trả về con trỏ buffer cần free() khi xong, hoặc nullptr nếu lỗi.
// Buffer có cấu trúc: [palette 8 byte] + [pixel data]
// Caller chịu trách nhiệm free(buf).
uint8_t* qr_render_generate(
    const char* text,
    int max_w,
    int max_h,
    int* out_w,
    int* out_h,
    int* out_size);