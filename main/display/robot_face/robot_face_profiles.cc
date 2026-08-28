#include "robot_face_profiles.h"

#include <cstring>

namespace robot_face {

const char* NormalizeEmotion(const char* emotion) {
    if (emotion == nullptr) {
        return "neutral";
    }

    if (std::strcmp(emotion, "happy") == 0 || std::strcmp(emotion, "laughing") == 0 || std::strcmp(emotion, "funny") == 0 ||
        std::strcmp(emotion, "loving") == 0 || std::strcmp(emotion, "winking") == 0 || std::strcmp(emotion, "cool") == 0 ||
        std::strcmp(emotion, "delicious") == 0 || std::strcmp(emotion, "kissy") == 0 || std::strcmp(emotion, "confident") == 0) {
        return "happy";
    }
    if (std::strcmp(emotion, "excited") == 0 || std::strcmp(emotion, "yay") == 0 || std::strcmp(emotion, "joy") == 0) {
        return "excited";
    }
    if (std::strcmp(emotion, "sad") == 0 || std::strcmp(emotion, "crying") == 0 || std::strcmp(emotion, "confused") == 0) {
        return "sad";
    }
    if (std::strcmp(emotion, "angry") == 0) {
        return "angry";
    }
    if (std::strcmp(emotion, "surprised") == 0 || std::strcmp(emotion, "shocked") == 0) {
        return "surprised";
    }
    if (std::strcmp(emotion, "thinking") == 0 || std::strcmp(emotion, "embarrassed") == 0) {
        return "thinking";
    }
    if (std::strcmp(emotion, "sleepy") == 0 || std::strcmp(emotion, "silly") == 0 || std::strcmp(emotion, "relaxed") == 0) {
        return "sleepy";
    }
    return "neutral";
}

FaceStyle GetStyle(const char* normalized_emotion, bool compact, bool blink_closed) {
    // Base style (neutral)
    FaceStyle style{};
    // Mắt trái (Rộng 25, Cao 18)
    style.left_eye_w = compact ? 12 : 25;
    style.left_eye_h = blink_closed ? 2 : (compact ? 8 : 18);
    style.left_eye_x = compact ? 26 : 28;
    style.left_eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 8 : 16);

    // Mắt phải (Rộng 18, Cao 12)
    style.right_eye_w = compact ? 12 : 18;
    style.right_eye_h = blink_closed ? 2 : (compact ? 8 : 12);
    style.right_eye_x = compact ? 90 : 82;
    style.right_eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 8 : 16);

    // 1. Kích thước tròng mắt (Tăng lên 6x6 px để nhìn rõ hơn)
    style.show_pupil = true;
    style.pupil_w = compact ? 4 : 6;
    style.pupil_h = compact ? 4 : 6;

    // 2. Tọa độ Y (Căn giữa theo chiều cao của khung mắt)
    // Mắt trái cao 18px, tròng cao 6px => (18 - 6)/2 = 6px từ mép trên
    style.pupil_left_y = compact ? 2 : 8; 
    style.pupil_right_y = compact ? 2 : 5;


    // 3. TẠO HIỆU ỨNG MẮT LÉ:
    // Tròng mắt trái kéo sang mép PHẢI của khung mắt trái:
    // Mắt trái rộng 25px - tròng rộng 6px - lề 2px = 17px
    style.pupil_left_x = compact ? 6 : 17; 

    // Tròng mắt phải giữ ở sát mép TRÁI của khung mắt phải:
    // Đặt 2px để cách viền trái của mắt phải một khoảng nhỏ
    style.pupil_right_x = compact ? 2 : 2;

    style.mouth = " - - ";

    if (normalized_emotion == nullptr) {
        return style;
    }

    if (std::strcmp(normalized_emotion, "happy") == 0) {
        // mắt trái to tròn
        style.left_eye_w = compact ? 16 : 22;
        style.left_eye_h = compact ? 16 : 22;
        style.left_eye_y = compact ? 6 : 11;

        // mắt phải nhỏ ty hí
        style.right_eye_w = compact ? 8 : 10;
        style.right_eye_h = compact ? 3 : 4;
        style.right_eye_y = compact ? 12 : 20;

        style.mouth = "~u~";
        return style;
    }

    if (std::strcmp(normalized_emotion, "excited") == 0) {
        // Cả hai mắt mở to hơn, cân đối 2 bên
        style.left_eye_w = compact ? 13 : 20;
        style.left_eye_h = blink_closed ? 2 : (compact ? 9 : 14);
        style.left_eye_x = compact ? 25 : 26;
        style.left_eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 7 : 13);

        style.right_eye_w = compact ? 13 : 20;
        style.right_eye_h = blink_closed ? 2 : (compact ? 9 : 14);
        style.right_eye_x = compact ? 90 : 82;
        style.right_eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 7 : 13);

        // Tròng mắt nhìn thẳng để dễ thấy trạng thái excited
        style.show_pupil = !blink_closed;
        style.pupil_w = compact ? 4 : 6;
        style.pupil_h = compact ? 4 : 6;
        style.pupil_left_x = compact ? 4 : 7;
        style.pupil_right_x = compact ? 4 : 7;
        style.pupil_left_y = compact ? 2 : 4;
        style.pupil_right_y = compact ? 2 : 4;

        style.mouth = "\\_U_/";
        return style;
    }

    // if (std::strcmp(normalized_emotion, "sad") == 0) {
    //     style.mouth = "/---\\";
    //     style.eye_h = blink_closed ? style.eye_h : (compact ? 6 : 9);
    //     style.eye_y = blink_closed ? style.eye_y : (compact ? 10 : 18);
    //     return style;
    // }

    // if (std::strcmp(normalized_emotion, "angry") == 0) {
    //     style.mouth = "===_===";
    //     return style;
    // }

    // if (std::strcmp(normalized_emotion, "surprised") == 0) {
    //     style.eye_w = compact ? 10 : 14;
    //     style.eye_h = blink_closed ? 2 : (compact ? 10 : 16);
    //     style.eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 7 : 14);
    //     style.left_x = compact ? 27 : 30;
    //     style.right_x = compact ? 91 : 84;
    //     style.mouth = "  o  ";
    //     return style;
    // }

    // if (std::strcmp(normalized_emotion, "thinking") == 0) {
    //     style.eye_h = blink_closed ? style.eye_h : (compact ? 7 : 10);
    //     style.eye_y = blink_closed ? style.eye_y : (compact ? 9 : 17);
    //     style.mouth = " - o ";
    //     return style;
    // }

    // if (std::strcmp(normalized_emotion, "sleepy") == 0) {
    //     style.eye_h = 2;
    //     style.eye_y = compact ? 12 : 22;
    //     style.mouth = " ... ";
    //     return style;
    // }

    return style;
}

} // namespace robot_face
