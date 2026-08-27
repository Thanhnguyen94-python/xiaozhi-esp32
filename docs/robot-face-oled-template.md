# OLED Robot Face Template (for custom practice)

Mục tiêu: giúp bạn tự vẽ và mở rộng nhiều khuôn mặt robot trên OLED.

## Nền tảng đã chuẩn bị

- Bộ điều khiển hiển thị chính: [main/display/oled_custom_emoji_display.cc](main/display/oled_custom_emoji_display.cc)
- Khai báo class hiển thị: [main/display/oled_custom_emoji_display.h](main/display/oled_custom_emoji_display.h)
- Module profile khuôn mặt (để bạn thêm face mới): [main/display/robot_face/robot_face_profiles.h](main/display/robot_face/robot_face_profiles.h)
- Cài đặt profile khuôn mặt: [main/display/robot_face/robot_face_profiles.cc](main/display/robot_face/robot_face_profiles.cc)

## Cấu trúc mở rộng chuẩn

1. Chuẩn hóa emotion app trong `NormalizeEmotion(...)`.
2. Mỗi frame khuôn mặt được mô tả bằng struct `FaceStyle`:
   - vị trí mắt trái/phải
   - kích thước mắt
   - miệng (`mouth`)
3. Hàm `GetStyle(...)` trả style theo:
   - emotion đã chuẩn hóa
   - loại màn hình (`compact` cho 128x32, không compact cho 128x64)
   - trạng thái chớp mắt (`blink_closed`)
4. `RenderFace()` chỉ việc lấy style rồi render.

## Cách bạn thêm face mới

### Bước 1: thêm nhóm emotion
Sửa trong [main/display/robot_face/robot_face_profiles.cc](main/display/robot_face/robot_face_profiles.cc):
- thêm nhánh trong `NormalizeEmotion(...)`, ví dụ map `"excited"` -> `"excited"`.

### Bước 2: thêm style cho emotion đó
Trong `GetStyle(...)`, thêm block:
- chỉnh `eye_w`, `eye_h`, `eye_y`, `left_x`, `right_x`, `mouth`.

### Bước 3: test runtime
- Gọi `SetEmotion("excited")` từ luồng app (hoặc map từ emotion hiện có).
- Quan sát mắt/miệng trên màn hình.

## Mẫu face đã có sẵn để bạn dựa theo

Các mood mẫu hiện có:
- neutral
- happy
- sad
- angry
- surprised
- thinking
- sleepy

Bạn chỉ cần copy 1 mood gần nhất rồi chỉnh tọa độ + miệng để ra face mới.

## Gợi ý tinh chỉnh nhanh

- Mắt to hơn: tăng `eye_w`, `eye_h`
- Mắt gần nhau hơn: tăng `left_x`, giảm `right_x`
- Miệng thấp hơn: chỉnh offset align trong [main/display/oled_custom_emoji_display.cc](main/display/oled_custom_emoji_display.cc)
- Chớp mắt nhanh/chậm: chỉnh chu kỳ timer + `next_blink_frame_` trong [main/display/oled_custom_emoji_display.cc](main/display/oled_custom_emoji_display.cc)

## Lưu ý

- OLED đơn sắc không cần GIF để làm animation mặt robot.
- Animation được tạo bằng vẽ lại frame theo timer.
