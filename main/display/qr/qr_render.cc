#include "qr/qr_render.h"
#include "qr/qrcodegen.hpp"
#include <cstring>
#include <cstdlib>
#include <algorithm>

using qrcodegen::QrCode;

uint8_t* qr_render_generate(
    const char* text,
    int max_w,
    int max_h,
    int* out_w,
    int* out_h,
    int* out_size) {

    if (text == nullptr || text[0] == '\0') return nullptr;
    if (out_w == nullptr || out_h == nullptr || out_size == nullptr) return nullptr;

    // Sinh QR ở mức sửa lỗi thấp (ít module nhất, phù hợp URL ngắn)
    QrCode qr = QrCode::encodeText(text, QrCode::Ecc::LOW);
    int N = qr.getSize();   // 21, 25, 29...

    // Thử quiet zone 4 → 2 → 1 để fit vào màn
    int quiet = 4;
    int scale = 1;
    int total = N + 2 * 4;
    for (int q : {4, 3, 2, 1, 0}) {
        int total_q = N + 2 * q;
        int sc = std::min(max_w / total_q, max_h / total_q);
        if (sc > scale) {
            scale = sc;
            quiet = q;
            total = total_q;
        }
    }
    if (scale < 1) scale = 1;

    int w = total * scale;
    int h = w;                        // QR vuông
    int stride = (w + 7) / 8;
    int pixel_size = stride * h;
    const int palette_size = 8;
    int data_size = palette_size + pixel_size;

    uint8_t* buf = (uint8_t*)calloc(1, data_size);
    if (buf == nullptr) return nullptr;

    // Palette: index 0 = trắng, index 1 = đen (LVGL I1 convention)
    // lv_color32_t: {blue, green, red, alpha}
    buf[0] = 0xFF; buf[1] = 0xFF; buf[2] = 0xFF; buf[3] = 0xFF;  // trắng
    buf[4] = 0x00; buf[5] = 0x00; buf[6] = 0x00; buf[7] = 0xFF;  // đen

    uint8_t* px = buf + palette_size;

    // Vẽ từng module đen (bit = 1)
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (!qr.getModule(x, y)) continue;   // module trắng, bỏ qua
            int px_start = (x + quiet) * scale;
            int py_start = (y + quiet) * scale;
            for (int dy = 0; dy < scale; dy++) {
                int py = py_start + dy;
                if (py >= h) break;
                for (int dx = 0; dx < scale; dx++) {
                    int pxx = px_start + dx;
                    if (pxx >= w) break;
                    int byte_i = py * stride + pxx / 8;
                    int bit_i = 7 - (pxx % 8);
                    px[byte_i] |= (1 << bit_i);
                }
            }
        }
    }

    *out_w = w;
    *out_h = h;
    *out_size = data_size;
    return buf;
}