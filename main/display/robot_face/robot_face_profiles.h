#pragma once

namespace robot_face {

struct FaceStyle {
    int eye_w;
    int eye_h;
    int eye_y;
    int left_x;
    int right_x;
    const char* mouth;
};

// Chuẩn hóa emotion từ app về nhóm emotion khuôn mặt.
// Các giá trị trả về hiện tại:
// neutral, happy, excited, sad, angry, surprised, thinking, sleepy
const char* NormalizeEmotion(const char* emotion);

// Lấy style khuôn mặt theo emotion + layout màn hình.
// - compact=true cho OLED 128x32
// - compact=false cho OLED 128x64
// - blink_closed=true để chớp mắt
FaceStyle GetStyle(const char* normalized_emotion, bool compact, bool blink_closed);

} // namespace robot_face
