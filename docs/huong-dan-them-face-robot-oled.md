# Hướng dẫn thêm/chỉnh face robot cho OLED

Tài liệu này dùng cho luồng hiển thị OLED custom của board Bread Compact WiFi.

> Mẫu `excited` đã được thêm sẵn để bạn thử nhanh. Bạn có thể chỉnh trực tiếp trong `GetStyle(...)` của file `main/display/robot_face/robot_face_profiles.cc`.

## 1) Các file cần biết

- `main/display/oled_custom_emoji_display.cc`
  - Điều khiển vòng đời UI, timer blink và render mặt.
  - Các hàm quan trọng:
    - `SetupUI()`
    - `SetEmotion(const char* emotion)`
    - `RenderFace()`
    - `AnimateFaceTick()`

- `main/display/robot_face/robot_face_profiles.h`
  - Định nghĩa `robot_face::FaceStyle`.

- `main/display/robot_face/robot_face_profiles.cc`
  - Bảng style cho từng biểu cảm.
  - Đây là file chính để thêm/tinh chỉnh face.

- `main/boards/bread-compact-wifi/compact_wifi_board.cc`
  - Đảm bảo board đang khởi tạo `OledCustomEmojiDisplay`.

- `main/CMakeLists.txt`
  - Đảm bảo đã compile:
    - `display/oled_custom_emoji_display.cc`
    - `display/robot_face/robot_face_profiles.cc`

## 2) Cách thêm 1 biểu cảm mới (ví dụ: "excited")

### Bước A: Chuẩn hoá tên emotion

Mở `main/display/robot_face/robot_face_profiles.cc`, trong `NormalizeEmotion(...)` thêm map alias.

Ví dụ ý tưởng:
- Nhận `"excited"` => chuẩn hoá thành `"excited"` (hoặc map về `"happy"`).
- Có thể map nhiều alias: `"joy"`, `"yay"`, `"excited"` về cùng một key.

### Bước B: Thêm style cho emotion mới

Trong `GetStyle(...)` (switch/if theo emotion key), thêm nhánh mới cho `excited`.

Các tham số thường chỉnh trong `FaceStyle`:
- Mắt:
  - chiều rộng/chiều cao
  - bo góc
  - vị trí lệch trái/phải
- Miệng:
  - độ rộng/độ dày
  - vị trí theo trục Y
  - dạng cười/buồn/phẳng
- Blink:
  - khi blink thì giảm chiều cao mắt để tạo hiệu ứng nháy

## 3) Quy tắc animation blink

Trong `AnimateFaceTick()`:
- tăng `blink_phase_` theo timer,
- khi phase đổi thì gọi `RenderFace()`.

Trong `RenderFace()`:
- nếu đang blink thì thu nhỏ height mắt (ví dụ còn 1-2 px),
- hết blink thì trả lại kích thước mắt bình thường.

Bạn có thể đổi tốc độ bằng chu kỳ timer hoặc số phase blink.

## 4) Tuỳ chỉnh theo màn hình 128x32 và 128x64

Trong render có nhánh theo kích thước panel:
- 128x32: mắt/miệng mảnh hơn, khoảng dọc nhỏ.
- 128x64: có thể tăng khoảng cách mắt và độ dày miệng để mặt đầy hơn.

Khuyến nghị:
- Dùng biến mode compact (`is_compact`) để scale riêng từng profile.

## 5) Checklist khi thêm face mới

1. Thêm alias trong `NormalizeEmotion(...)`.
2. Thêm profile trong `GetStyle(...)`.
3. Build và flash.
4. Gọi `SetEmotion("excited")` từ luồng app để test.

## 6) Lỗi thường gặp

- Quên thêm source vào `main/CMakeLists.txt`.
- Emotion từ server không khớp key normalize.
- Blink timer chạy nhưng không gọi lại `RenderFace()`.
- Dùng cùng một thông số cho 128x32 và 128x64 làm mặt bị méo.

## 7) Quy trình tinh chỉnh nhanh

1. Chỉ sửa `robot_face_profiles.cc` để chỉnh giao diện.
2. Mỗi lần chỉ đổi 1-2 tham số.
3. Build + flash + nhìn trực tiếp trên phần cứng.
4. Khi ổn rồi mới thêm alias emotion.

## 8) Miệng nhép khi robot nói (lip-sync)

Đã có sẵn logic lip-sync trong `main/display/oled_custom_emoji_display.cc`:

- `SetStatus(const char* status)`
  - Bật/tắt `speaking_` khi status là `Lang::Strings::SPEAKING`.
- `AnimateFaceTick()`
  - Tăng `speaking_frame_` theo timer khi `speaking_ = true`.
- `RenderFace()`
  - Thay `style.mouth` bằng frame miệng động (`kTalkingMouthFrames`) khi đang nói.

Bạn có thể chỉnh độ “nhép” ở:
- Chu kỳ timer `lv_timer_create(..., 80, ...)` (nhanh/chậm toàn animation).
- Số frame và ký tự trong `kTalkingMouthFrames` (độ mở miệng).

## 9) Cách thêm face bằng nhiều frame bitmap (mảng)

Ví dụ áp dụng cho face `happy` với 2 frame `Fr2_GIF1`, `Fr2_GIF2` trên OLED 128x64.

### Bước 1: Tạo file chứa frame bitmap

- Tạo file `main/display/robot_face/happy_face_frames.h`:
  - Khai báo `kHappyBitmapWidth = 128`, `kHappyBitmapHeight = 64`.
  - Khai báo `extern const uint8_t Fr2_GIF1[];`, `extern const uint8_t Fr2_GIF2[];`.

- Tạo file `main/display/robot_face/happy_face_frames.cc`:
  - Định nghĩa đầy đủ dữ liệu mảng `Fr2_GIF1`, `Fr2_GIF2`.

> Lưu ý: dữ liệu 128x64 monochrome chuẩn là `128 * 64 / 8 = 1024` byte pixel.  
> Nếu dữ liệu dài hơn, chỉ nên dùng đúng phần tương ứng để tránh hiển thị sai.

### Bước 2: Đưa file mới vào build

- Mở `main/CMakeLists.txt`.
- Thêm source:
  - `display/robot_face/happy_face_frames.cc`

### Bước 3: Tích hợp render bitmap trong OLED display

Trong `main/display/oled_custom_emoji_display.h`:
- Thêm state:
  - `lv_obj_t* happy_bitmap_`
  - `uint8_t happy_frame_`

Trong `main/display/oled_custom_emoji_display.cc`:
- `#include "robot_face/happy_face_frames.h"`.
- Tạo `lv_image_dsc_t` cho từng frame.
- Tạo object ảnh `happy_bitmap_` trong `SetupFaceUI_128x64()` và ẩn mặc định.
- Trong `RenderFace()`:
  - Nếu emotion là `happy` và màn 128x64:
    - Ẩn mắt/miệng vector.
    - Hiện `happy_bitmap_` với frame hiện tại.
  - Emotion khác:
    - Ẩn `happy_bitmap_`.
    - Render lại theo style vector bình thường.

### Bước 4: Đảm bảo dữ liệu I1 đúng format LVGL

Với `LV_COLOR_FORMAT_I1`, data cần:
1. Palette 2 màu (`lv_color32_t` * 2) ở đầu buffer.
2. Pixel 1-bit ngay sau palette.

Nếu không có palette đúng, dễ gặp lỗi màn hình đen khi chuyển face.

### Bước 5: Map emotion đúng key

- Đảm bảo luồng emotion cuối cùng thực sự về `"happy"` (không bị map sang key khác).
- Có thể kiểm tra tại hàm `NormalizeEmotion(...)` và map folder emotion trong `oled_custom_emoji_display.cc`.

### Bước 6: Build/flash/test

1. Save toàn bộ file.
2. Build.
3. Flash.
4. Monitor log và test chuyển emotion sang `happy`.

## 10) Tốc độ chuyển frame phụ thuộc vào đâu?

Tốc độ phụ thuộc chính vào 2 yếu tố:

1. Chu kỳ timer UI (`lv_timer_create(..., 80, ...)`)
   - 80ms/tick => khoảng 12.5 tick/giây.

2. Logic tăng frame trong `AnimateFaceTick()`
   - Nếu mỗi tick tăng 1 frame (`happy_frame_ = (happy_frame_ + 1) % N`) thì:
     - FPS ≈ `1000 / timer_ms`.
   - Nếu tăng frame mỗi `k` tick thì:
     - FPS ≈ `1000 / (timer_ms * k)`.

Ví dụ:
- `timer_ms = 80`, `N = 2`, đổi frame mỗi tick:
  - tốc độ đổi frame ≈ 12.5 fps,
  - 1 vòng animation (2 frame) = `2 * 80 = 160ms`.

Muốn chậm hơn:
- tăng `timer_ms`, hoặc
- giữ `timer_ms` và chỉ đổi frame sau vài tick (khuyến nghị vì không ảnh hưởng blink/lip-sync quá nhiều).

---

Gợi ý mở rộng:
- Tách nhiều bộ profile:
  - `robot_face_profile_default.cc`
  - `robot_face_profile_cute.cc`
  - `robot_face_profile_sharp.cc`
- Chọn profile theo board hoặc setting runtime.