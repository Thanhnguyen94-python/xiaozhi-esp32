#pragma once

namespace robot_face {

struct FaceStyle {
    // mắt trái
    int left_eye_w;
    int left_eye_h;
    int left_eye_y;
    int left_eye_x;
    // mắt phải
    int right_eye_w;
    int right_eye_h;
    int right_eye_y;
    int right_eye_x;
    // Tròng mắt (Pupil)
    bool show_pupil;       // Bật/Tắt vẽ tròng mắt
    int pupil_w;           // Chiều rộng tròng mắt
    int pupil_h;           // Chiều cao tròng mắt
    int pupil_left_x;      // Vị trí X tròng mắt trái
    int pupil_right_x;     // Vị trí X tròng mắt phải
    int pupil_left_y;           // Vị trí Y tròng mắt
    int pupil_right_y;          // Vị trí Y tròng mắt
    // miệng
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
