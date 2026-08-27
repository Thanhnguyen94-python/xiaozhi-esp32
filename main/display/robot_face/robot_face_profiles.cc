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
    FaceStyle style = {
        .eye_w = compact ? 12 : 18,
        .eye_h = blink_closed ? 2 : (compact ? 8 : 12),
        .eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 8 : 16),
        .left_x = compact ? 26 : 28,
        .right_x = compact ? 90 : 82,
        .mouth = " - - ",
    };

    if (normalized_emotion == nullptr) {
        return style;
    }

    if (std::strcmp(normalized_emotion, "happy") == 0) {
        style.mouth = "\\___/";
        return style;
    }

    if (std::strcmp(normalized_emotion, "excited") == 0) {
        style.eye_w = compact ? 13 : 20;
        style.eye_h = blink_closed ? 2 : (compact ? 9 : 14);
        style.eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 7 : 13);
        style.left_x = compact ? 25 : 26;
        style.right_x = compact ? 90 : 82;
        style.mouth = "\\_U_/";
        return style;
    }

    if (std::strcmp(normalized_emotion, "sad") == 0) {
        style.mouth = "/---\\";
        style.eye_h = blink_closed ? style.eye_h : (compact ? 6 : 9);
        style.eye_y = blink_closed ? style.eye_y : (compact ? 10 : 18);
        return style;
    }

    if (std::strcmp(normalized_emotion, "angry") == 0) {
        style.mouth = "===_===";
        return style;
    }

    if (std::strcmp(normalized_emotion, "surprised") == 0) {
        style.eye_w = compact ? 10 : 14;
        style.eye_h = blink_closed ? 2 : (compact ? 10 : 16);
        style.eye_y = blink_closed ? (compact ? 12 : 22) : (compact ? 7 : 14);
        style.left_x = compact ? 27 : 30;
        style.right_x = compact ? 91 : 84;
        style.mouth = "  o  ";
        return style;
    }

    if (std::strcmp(normalized_emotion, "thinking") == 0) {
        style.eye_h = blink_closed ? style.eye_h : (compact ? 7 : 10);
        style.eye_y = blink_closed ? style.eye_y : (compact ? 9 : 17);
        style.mouth = " - o ";
        return style;
    }

    if (std::strcmp(normalized_emotion, "sleepy") == 0) {
        style.eye_h = 2;
        style.eye_y = compact ? 12 : 22;
        style.mouth = " ... ";
        return style;
    }

    return style;
}

} // namespace robot_face
