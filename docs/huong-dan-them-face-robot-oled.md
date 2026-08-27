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

---

Gợi ý mở rộng:
- Tách nhiều bộ profile:
  - `robot_face_profile_default.cc`
  - `robot_face_profile_cute.cc`
  - `robot_face_profile_sharp.cc`
- Chọn profile theo board hoặc setting runtime.