#include "oled_custom_emoji_display.h"
#include "robot_face/robot_face_profiles.h"

#include <unordered_map>
#include <string>
#include <cstring>

#include <esp_log.h>
#include <font_awesome.h>
#include "assets/lang_config.h"

LV_FONT_DECLARE(font_awesome_20_4);

namespace {
const char* TAG = "OledCustomEmoji";

// Map emotion chuẩn của app → tên folder emoji (giữ nguyên logic cũ)
const std::unordered_map<std::string, const char*> kEmotionToFolderEmojiName = {
    {"neutral", "1macdinh"},   {"happy", "vuive"},       {"excited", "vuimung"},
    {"laughing", "cuoito"},    {"funny", "nhaymat"},     {"sad", "buon"},
    {"crying", "buon2"},       {"sleepy", "buonngu"},    {"silly", "duamat"},
    {"angry", "tucgian"},      {"surprised", "ngacnhien2"},{"shocked", "hoamat"},
    {"thinking", "suynghi2"},  {"winking", "nhaymat"},   {"loving", "nheomat"},
    {"embarrassed", "domohoi"},{"confident", "vuimung"}, {"relaxed", "binhthuong"},
    {"confused", "chamhoi"},   {"delicious", "vuive"},   {"kissy", "nheomat"},
    {"cool", "camlang"},
};

const std::unordered_map<std::string, const char*> kFolderEmojiNameToOledEmotion = {
    {"1macdinh", "neutral"},   {"binhthuong", "relaxed"}, {"buon", "sad"},
    {"buon2", "crying"},       {"buonngu", "sleepy"},     {"camlang", "cool"},
    {"chamhoi", "confused"},   {"cuoito", "laughing"},    {"domohoi", "embarrassed"},
    {"doxet", "angry"},        {"duamat", "silly"},       {"hoamat", "shocked"},
    {"khongchiudau", "angry"}, {"ngacnhien2", "surprised"},{"nhaymat", "winking"},
    {"nheomat", "loving"},     {"suynghi2", "thinking"},  {"tucgian", "angry"},
    {"tucgian2", "angry"},     {"vuimung", "excited"},    {"vuive", "happy"},
};

const char* ResolveOledEmotionFromFolderProfile(const char* emotion) {
    auto folder_it = kEmotionToFolderEmojiName.find(emotion);
    if (folder_it == kEmotionToFolderEmojiName.end()) return nullptr;
    auto oled_it = kFolderEmojiNameToOledEmotion.find(folder_it->second);
    if (oled_it == kFolderEmojiNameToOledEmotion.end()) return nullptr;
    return oled_it->second;
}
}
const char* DetectEmotionFromText(const char* text) {
    if (text == nullptr || text[0] == '\0') return nullptr;

    auto has = [text](const char* needle) {
        return std::strstr(text, needle) != nullptr;
    };

    // ANGRY — từ chửi
    if (has("đcm") || has("đkm") || has("Buồn đéo gì") || has("đờ cờ mờ") || has("đm ")|| has("Đờ mờ ") || has("chửi") ||
        has("đéo") || has("vãi cả choá") || has("vcl") || has("vờ cờ lờ") || has("vkl") || has("vãi") ||
        has("cặc") || has("địt") || has("bực thì bực") || has("Mệt vãi") ||
        has("chửi") || has("bực mình") || has("bực hết cả choá") || has("tức không chịu nổi") || has("tức không chịu được")|| has("khó chịu")) {
        return "angry";
    }
    // THINKING — đang tra cứu
    if (has("Đợi tao tí") || has("Để tao xem") || has("Tao tìm xem") || has("Đợi tí")|| has("Tao tra đã")||
        has("Để tao tra ") || has("tra cứu ")) {
        return "thinking";
    }
    // SURPRISED — bất ngờ
    if (has("trời ơi") || has("thật à") || has("sốc vãi") ||
        has("vãi thiệt chứ") || has("kinh vãi") || has("gì cơ")) {
        return "surprised";
    }
    // SAD
    if (has("buồn") || has("tiếc") || has("khóc") || has("thất vọng")) {
        return "sad";
    }
    // SLEEPY
    if (has("mệt") || has("chán") || has("ngủ") || has("buồn ngủ")) {
        return "sleepy";
    }
    // HAPPY
    if (has("haha") || has("hehe") || has("vui") || has("mừng") ||
        has("ngon") || has("được đấy")|| has("quá đã")) {
        return "happy";
    }
    return nullptr;
}

// Tạo cung tròn mượt dùng lv_arc
//   size       = đường kính của hình tròn ảo chứa cung
//   start,end  = góc (0°=phải, 90°=dưới, 180°=trái, 270°=trên)
//   thickness  = độ dày nét vẽ
lv_obj_t* MakeArc(lv_obj_t* parent, int size, int start_angle, int end_angle, int thickness) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);

    // Chỉ hiện nét cung, không hiện nền tròn hay indicator
    lv_obj_set_style_arc_width(arc, thickness, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(arc, 0, LV_PART_MAIN);

    lv_arc_set_bg_angles(arc, start_angle, end_angle);
    lv_arc_set_angles(arc, 0, 0);           // indicator rỗng
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN); // mặc định ẩn
    return arc;
}

OledCustomEmojiDisplay::~OledCustomEmojiDisplay() {
    if (face_timer_ != nullptr) {
        lv_timer_delete(face_timer_);
        face_timer_ = nullptr;
    }
}

bool OledCustomEmojiDisplay::IsFaceMode() const {
    return width_ == 128 && (height_ == 64 || height_ == 32);
}

void OledCustomEmojiDisplay::SetupFaceUI_128x64() {
    // ✅ Dùng đúng display của mình, không dùng lv_screen_active() chung chung
    auto screen = lv_display_get_screen_active(display_);
    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    face_root_ = lv_obj_create(screen);
    lv_obj_set_size(face_root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(face_root_, 0, 0);
    lv_obj_set_style_pad_all(face_root_, 0, 0);
    lv_obj_set_style_border_width(face_root_, 0, 0);
    lv_obj_set_style_bg_color(face_root_, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(face_root_, LV_SCROLLBAR_MODE_OFF);

    // --- MẮT TRÁI (hình khối) ---
    left_eye_ = lv_obj_create(face_root_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(left_eye_, 0, 0);
    lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);

    left_pupil_ = lv_obj_create(left_eye_);
    lv_obj_set_style_bg_color(left_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(left_pupil_, 0, 0);
    lv_obj_set_style_radius(left_pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(left_pupil_, 0, 0);

    // --- MẮT PHẢI (hình khối) ---
    right_eye_ = lv_obj_create(face_root_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(right_eye_, 0, 0);
    lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);

    right_pupil_ = lv_obj_create(right_eye_);
    lv_obj_set_style_bg_color(right_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(right_pupil_, 0, 0);
    lv_obj_set_style_radius(right_pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(right_pupil_, 0, 0);

    // --- MẮT XIÊN (dùng cho tức giận) ---
    left_eye_line_ = lv_line_create(face_root_);
    lv_obj_set_style_line_width(left_eye_line_, 6, 0);
    lv_obj_set_style_line_color(left_eye_line_, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(left_eye_line_, true, 0);
    lv_obj_add_flag(left_eye_line_, LV_OBJ_FLAG_HIDDEN);

    right_eye_line_ = lv_line_create(face_root_);
    lv_obj_set_style_line_width(right_eye_line_, 6, 0);
    lv_obj_set_style_line_color(right_eye_line_, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(right_eye_line_, true, 0);
    lv_obj_add_flag(right_eye_line_, LV_OBJ_FLAG_HIDDEN);

    // --- MIỆNG (dùng cho emotion) ---
    mouth_line_ = lv_line_create(face_root_);
    lv_obj_set_style_line_width(mouth_line_, 4, 0);
    lv_obj_set_style_line_color(mouth_line_, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(mouth_line_, true, 0);
    lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);

    // --- MIỆNG ĐỘNG (2 lớp: viền môi đen + hốc miệng trắng) ---
    mouth_obj_ = lv_obj_create(face_root_);
    lv_obj_set_style_bg_color(mouth_obj_, lv_color_black(), 0);
    lv_obj_set_style_border_width(mouth_obj_, 0, 0);
    lv_obj_set_style_radius(mouth_obj_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(mouth_obj_, 0, 0);
    lv_obj_add_flag(mouth_obj_, LV_OBJ_FLAG_HIDDEN);

    // Hốc miệng (màu trắng) — nằm trong mouth_obj_, tạo cảm giác miệng mở có chiều sâu
    mouth_inner_ = lv_obj_create(mouth_obj_);
    lv_obj_set_style_bg_color(mouth_inner_, lv_color_white(), 0);
    lv_obj_set_style_border_width(mouth_inner_, 0, 0);
    lv_obj_set_style_radius(mouth_inner_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(mouth_inner_, 0, 0);
    lv_obj_center(mouth_inner_);

    // Reset trạng thái
    blink_closed_ = false;
    frame_count_ = 0;
    blink_phase_ = 0;
    next_blink_frame_ = 24;
    speaking_ = false;
    speaking_frame_ = 0;
    current_face_emotion_ = "neutral";

    RenderFace();

    // ============ HAPPY FACE ARCS ============
    // Mắt trái ^ — cung nửa trên, góc 200°→340° (span 140°)
    // Widget 22x22 → tâm (11,11). Muốn đỉnh ^ ở (33,20) → widget pos (22, 20)
    happy_left_eye_arc_ = MakeArc(face_root_, 22, 200, 340, 5);
    lv_obj_set_pos(happy_left_eye_arc_, 22, 16);

    // Mắt phải ^
    happy_right_eye_arc_ = MakeArc(face_root_, 22, 200, 340, 5);
    lv_obj_set_pos(happy_right_eye_arc_, 84, 18);

    // Miệng ◡ — cung nửa dưới, góc 20°→160° (span 140°)
    // Widget 34x34 → tâm (17,17). Muốn tâm miệng ở (64, 48) → widget pos (47, 31)
    happy_mouth_arc_ = MakeArc(face_root_, 34, 20, 160, 5);
    lv_obj_set_pos(happy_mouth_arc_, 47, 28);

        // ============ SAD FACE ============
    // Mắt trái: hình tròn đen
    sad_left_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_left_eye_, 34, 26);
    // lv_obj_set_style_radius(sad_left_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(sad_left_eye_, 6, 0); 
    lv_obj_set_style_bg_color(sad_left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sad_left_eye_, 0, 0);
    lv_obj_set_style_pad_all(sad_left_eye_, 0, 0);
    lv_obj_set_pos(sad_left_eye_, 22, 12);
    lv_obj_add_flag(sad_left_eye_, LV_OBJ_FLAG_HIDDEN);

    // Khối trắng xoay đè lên trên mắt trái — tạo cạnh xiên cắt qua hình tròn
    sad_left_eye_cut_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_left_eye_cut_, 60, 24);
    lv_obj_set_style_bg_color(sad_left_eye_cut_, lv_color_white(), 0);
    lv_obj_set_style_border_width(sad_left_eye_cut_, 0, 0);
    lv_obj_set_style_radius(sad_left_eye_cut_, 0, 0);
    lv_obj_set_style_pad_all(sad_left_eye_cut_, 0, 0);
    lv_obj_set_pos(sad_left_eye_cut_, 7, 4);
    // Pivot ở tâm khối trắng
    lv_obj_set_style_transform_pivot_x(sad_left_eye_cut_, 30, 0);
    lv_obj_set_style_transform_pivot_y(sad_left_eye_cut_, 12, 0);
    // Xoay -18° (đơn vị 0.1°) — TUNE SỐ NÀY nếu cạnh xiên chưa đúng
    lv_obj_set_style_transform_rotation(sad_left_eye_cut_, -180, 0);
    lv_obj_add_flag(sad_left_eye_cut_, LV_OBJ_FLAG_HIDDEN);

    // Mắt phải: hình tròn đen
    sad_right_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_right_eye_, 34, 26);
    // lv_obj_set_style_radius(sad_right_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(sad_right_eye_, 6, 0);
    lv_obj_set_style_bg_color(sad_right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sad_right_eye_, 0, 0);
    lv_obj_set_style_pad_all(sad_right_eye_, 0, 0);
    lv_obj_set_pos(sad_right_eye_, 76, 12);
    lv_obj_add_flag(sad_right_eye_, LV_OBJ_FLAG_HIDDEN);

    sad_right_eye_cut_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_right_eye_cut_, 60, 24);
    lv_obj_set_style_bg_color(sad_right_eye_cut_, lv_color_white(), 0);
    lv_obj_set_style_border_width(sad_right_eye_cut_, 0, 0);
    lv_obj_set_style_radius(sad_right_eye_cut_, 0, 0);
    lv_obj_set_style_pad_all(sad_right_eye_cut_, 0, 0);
    lv_obj_set_pos(sad_right_eye_cut_, 61, 4);
    lv_obj_set_style_transform_pivot_x(sad_right_eye_cut_, 30, 0);
    lv_obj_set_style_transform_pivot_y(sad_right_eye_cut_, 12, 0);
    // Mirror: +18°
    lv_obj_set_style_transform_rotation(sad_right_eye_cut_, 180, 0);
    lv_obj_add_flag(sad_right_eye_cut_, LV_OBJ_FLAG_HIDDEN);

    // Miệng buồn: cung nửa TRÊN (frown ∩) — góc 20°→160°
    sad_mouth_arc_ = MakeArc(face_root_, 40, 20, 160, 5);
    lv_obj_set_pos(sad_mouth_arc_, 44, 40);
    lv_obj_add_flag(sad_mouth_arc_, LV_OBJ_FLAG_HIDDEN);

    // Nước mắt trái
    sad_left_tear_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_left_tear_, 5, 8);
    lv_obj_set_style_radius(sad_left_tear_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sad_left_tear_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sad_left_tear_, 0, 0);
    lv_obj_set_style_pad_all(sad_left_tear_, 0, 0);
    lv_obj_add_flag(sad_left_tear_, LV_OBJ_FLAG_HIDDEN);

    // Nước mắt phải
    sad_right_tear_ = lv_obj_create(face_root_);
    lv_obj_set_size(sad_right_tear_, 5, 8);
    lv_obj_set_style_radius(sad_right_tear_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sad_right_tear_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sad_right_tear_, 0, 0);
    lv_obj_set_style_pad_all(sad_right_tear_, 0, 0);
    lv_obj_add_flag(sad_right_tear_, LV_OBJ_FLAG_HIDDEN);

        // ============ ANGRY FACE ============
    // Mắt trái: hình chữ nhật bo cạnh
    angry_left_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(angry_left_eye_, 30, 30);
    lv_obj_set_style_radius(angry_left_eye_, 6, 0);
    lv_obj_set_style_bg_color(angry_left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(angry_left_eye_, 0, 0);
    lv_obj_set_style_pad_all(angry_left_eye_, 0, 0);
    lv_obj_set_pos(angry_left_eye_, 16, 14);          // ⬅️ đẩy xa hơn sad (22 → 16)
    lv_obj_add_flag(angry_left_eye_, LV_OBJ_FLAG_HIDDEN);

    // Khối trắng cắt — ĐẢO DẤU so với sad (+18° thay vì -18°) để cạnh xiên quay ngược
    angry_left_eye_cut_ = lv_obj_create(face_root_);
    lv_obj_set_size(angry_left_eye_cut_, 60, 24);
    lv_obj_set_style_bg_color(angry_left_eye_cut_, lv_color_white(), 0);
    lv_obj_set_style_border_width(angry_left_eye_cut_, 0, 0);
    lv_obj_set_style_radius(angry_left_eye_cut_, 0, 0);
    lv_obj_set_style_pad_all(angry_left_eye_cut_, 0, 0);
    lv_obj_set_pos(angry_left_eye_cut_, 1, 4);
    lv_obj_set_style_transform_pivot_x(angry_left_eye_cut_, 30, 0);
    lv_obj_set_style_transform_pivot_y(angry_left_eye_cut_, 12, 0);
    lv_obj_set_style_transform_rotation(angry_left_eye_cut_, 180, 0);   // ⬅️ +18° (sad là -18°)
    lv_obj_add_flag(angry_left_eye_cut_, LV_OBJ_FLAG_HIDDEN);

    // Mắt phải
    angry_right_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(angry_right_eye_, 30, 30);
    lv_obj_set_style_radius(angry_right_eye_, 6, 0);
    lv_obj_set_style_bg_color(angry_right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(angry_right_eye_, 0, 0);
    lv_obj_set_style_pad_all(angry_right_eye_, 0, 0);
    lv_obj_set_pos(angry_right_eye_, 82, 14);         // ⬅️ đẩy xa hơn sad (76 → 82)
    lv_obj_add_flag(angry_right_eye_, LV_OBJ_FLAG_HIDDEN);

    angry_right_eye_cut_ = lv_obj_create(face_root_);
    lv_obj_set_size(angry_right_eye_cut_, 60, 24);
    lv_obj_set_style_bg_color(angry_right_eye_cut_, lv_color_white(), 0);
    lv_obj_set_style_border_width(angry_right_eye_cut_, 0, 0);
    lv_obj_set_style_radius(angry_right_eye_cut_, 0, 0);
    lv_obj_set_style_pad_all(angry_right_eye_cut_, 0, 0);
    lv_obj_set_pos(angry_right_eye_cut_, 67, 4);
    lv_obj_set_style_transform_pivot_x(angry_right_eye_cut_, 30, 0);
    lv_obj_set_style_transform_pivot_y(angry_right_eye_cut_, 12, 0);
    lv_obj_set_style_transform_rotation(angry_right_eye_cut_, -180, 0); // ⬅️ -18° (sad là +18°)
    lv_obj_add_flag(angry_right_eye_cut_, LV_OBJ_FLAG_HIDDEN);

    // ============ DẤU GIẬN: 4 GÓC VUÔNG ============
    // Container 24x12 đặt trên trán phía phải, có thể rung nhẹ
    angry_mark_root_ = lv_obj_create(face_root_);
    lv_obj_set_size(angry_mark_root_, 24, 12);
    lv_obj_set_pos(angry_mark_root_, 98, 2);
    lv_obj_set_style_bg_opa(angry_mark_root_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(angry_mark_root_, 0, 0);
    lv_obj_set_style_pad_all(angry_mark_root_, 0, 0);
    lv_obj_add_flag(angry_mark_root_, LV_OBJ_FLAG_HIDDEN);

    // 4 góc chữ L, mỗi góc 3 điểm, đỉnh L quay vào tâm (12, 6)
    // Toạ độ trong container 24x12:
    //   Góc trên-trái : (0,4) → (6,4) → (6,0)
    //   Góc trên-phải : (24,4) → (18,4) → (18,0)
    //   Góc dưới-trái : (0,8) → (6,8) → (6,12)
    //   Góc dưới-phải : (24,8) → (18,8) → (18,12)
    static const lv_coord_t kMarkCoords[4][3][2] = {
        {{0,4},{6,4},{6,0}},
        {{24,4},{18,4},{18,0}},
        {{0,8},{6,8},{6,12}},
        {{24,8},{18,8},{18,12}},
    };

    for (int i = 0; i < 4; i++) {
        angry_mark_pts_[i][0] = { (lv_value_precise_t)kMarkCoords[i][0][0], (lv_value_precise_t)kMarkCoords[i][0][1] };
        angry_mark_pts_[i][1] = { (lv_value_precise_t)kMarkCoords[i][1][0], (lv_value_precise_t)kMarkCoords[i][1][1] };
        angry_mark_pts_[i][2] = { (lv_value_precise_t)kMarkCoords[i][2][0], (lv_value_precise_t)kMarkCoords[i][2][1] };

        angry_mark_lines_[i] = lv_line_create(angry_mark_root_);
        lv_line_set_points(angry_mark_lines_[i], angry_mark_pts_[i], 3);
        lv_obj_set_style_line_width(angry_mark_lines_[i], 3, 0);
        lv_obj_set_style_line_color(angry_mark_lines_[i], lv_color_black(), 0);
        lv_obj_set_style_line_rounded(angry_mark_lines_[i], false, 0);
    }
    // ============ SURPRISED FACE ============
    // Miệng chữ O: oval đen nhỏ, hơi dài theo chiều dọc
    surprised_mouth_ = lv_obj_create(face_root_);
    lv_obj_set_size(surprised_mouth_, 12, 16);
    lv_obj_set_style_radius(surprised_mouth_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(surprised_mouth_, lv_color_black(), 0);
    lv_obj_set_style_border_width(surprised_mouth_, 0, 0);
    lv_obj_set_style_pad_all(surprised_mouth_, 0, 0);
    lv_obj_set_pos(surprised_mouth_, 58, 42);   // căn giữa ngang (128/2 - 6 = 58)
    lv_obj_add_flag(surprised_mouth_, LV_OBJ_FLAG_HIDDEN);

        // ============ SLEEPY FACE ============
    // Mắt trái: chữ nhật bo góc
    sleepy_left_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(sleepy_left_eye_, 26, 4);            // ⬅️ mỏng
    lv_obj_set_style_radius(sleepy_left_eye_, 2, 0);     // ⬅️ bo tròn nhẹ 2 đầu
    lv_obj_set_style_bg_color(sleepy_left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sleepy_left_eye_, 0, 0);
    lv_obj_set_style_pad_all(sleepy_left_eye_, 0, 0);
    lv_obj_set_pos(sleepy_left_eye_, 29, 20);
    lv_obj_set_style_transform_pivot_x(sleepy_left_eye_, 13, 0);   // tâm thanh
    lv_obj_set_style_transform_pivot_y(sleepy_left_eye_, 2,  0);
    lv_obj_add_flag(sleepy_left_eye_, LV_OBJ_FLAG_HIDDEN);

    // Mắt phải: chữ nhật bo góc
    sleepy_right_eye_ = lv_obj_create(face_root_);
    lv_obj_set_size(sleepy_right_eye_, 26, 4);
    lv_obj_set_style_radius(sleepy_right_eye_, 2, 0);
    lv_obj_set_style_bg_color(sleepy_right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sleepy_right_eye_, 0, 0);
    lv_obj_set_style_pad_all(sleepy_right_eye_, 0, 0);
    lv_obj_set_pos(sleepy_right_eye_, 73, 20);
    lv_obj_set_style_transform_pivot_x(sleepy_right_eye_, 13, 0);
    lv_obj_set_style_transform_pivot_y(sleepy_right_eye_, 2,  0);
    lv_obj_add_flag(sleepy_right_eye_, LV_OBJ_FLAG_HIDDEN);

    // Miệng: hình tròn nhỏ ban đầu
    sleepy_mouth_ = lv_obj_create(face_root_);
    lv_obj_set_size(sleepy_mouth_, 14, 14);
    lv_obj_set_style_radius(sleepy_mouth_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sleepy_mouth_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sleepy_mouth_, 0, 0);
    lv_obj_set_style_pad_all(sleepy_mouth_, 0, 0);
    lv_obj_set_pos(sleepy_mouth_, 57, 39);
    lv_obj_add_flag(sleepy_mouth_, LV_OBJ_FLAG_HIDDEN);

    // Giọt nước miếng
    sleepy_drool_ = lv_obj_create(face_root_);
    lv_obj_set_size(sleepy_drool_, 5, 8);
    lv_obj_set_style_radius(sleepy_drool_, 4, 0);
    lv_obj_set_style_bg_color(sleepy_drool_, lv_color_black(), 0);
    lv_obj_set_style_border_width(sleepy_drool_, 0, 0);
    lv_obj_set_style_pad_all(sleepy_drool_, 0, 0);
    lv_obj_add_flag(sleepy_drool_, LV_OBJ_FLAG_HIDDEN);

    // 3 chữ Z vẽ bằng line (4 điểm: 2 ngang trên, 2 ngang dưới, 1 xiên nối)
    static const lv_coord_t kZBase[4][2] = {{0,0}, {8,0}, {0,8}, {8,8}};
    for (int i = 0; i < 3; i++) {
        sleepy_z_pts_[i][0] = {(lv_value_precise_t)kZBase[0][0], (lv_value_precise_t)kZBase[0][1]};
        sleepy_z_pts_[i][1] = {(lv_value_precise_t)kZBase[1][0], (lv_value_precise_t)kZBase[1][1]};
        sleepy_z_pts_[i][2] = {(lv_value_precise_t)kZBase[2][0], (lv_value_precise_t)kZBase[2][1]};
        sleepy_z_pts_[i][3] = {(lv_value_precise_t)kZBase[3][0], (lv_value_precise_t)kZBase[3][1]};

        sleepy_z_[i] = lv_line_create(face_root_);
        lv_line_set_points(sleepy_z_[i], sleepy_z_pts_[i], 4);
        lv_obj_set_style_line_width(sleepy_z_[i], 3, 0);
        lv_obj_set_style_line_color(sleepy_z_[i], lv_color_black(), 0);
        lv_obj_set_style_line_rounded(sleepy_z_[i], false, 0);
        lv_obj_set_style_transform_pivot_x(sleepy_z_[i], 0, 0);
        lv_obj_set_style_transform_pivot_y(sleepy_z_[i], 0, 0);
        lv_obj_add_flag(sleepy_z_[i], LV_OBJ_FLAG_HIDDEN);
    }

        // ============ THINKING FACE: BÓNG ĐÈN ============
    // Bóng đèn 18x18, đặt cao ở góc phải
    thinking_bulb_ = lv_obj_create(face_root_);
    lv_obj_set_size(thinking_bulb_, 18, 18);
    lv_obj_set_style_radius(thinking_bulb_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(thinking_bulb_, lv_color_black(), 0);
    lv_obj_set_style_border_width(thinking_bulb_, 0, 0);
    lv_obj_set_style_pad_all(thinking_bulb_, 0, 0);
    lv_obj_set_pos(thinking_bulb_, 104, 2);
    lv_obj_add_flag(thinking_bulb_, LV_OBJ_FLAG_HIDDEN);

    // Vòng trắng bên trong (18 - 2*3 = 12)
    thinking_bulb_inner_ = lv_obj_create(thinking_bulb_);
    lv_obj_set_size(thinking_bulb_inner_, 12, 12);
    lv_obj_set_style_radius(thinking_bulb_inner_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(thinking_bulb_inner_, lv_color_white(), 0);
    lv_obj_set_style_border_width(thinking_bulb_inner_, 0, 0);
    lv_obj_set_style_pad_all(thinking_bulb_inner_, 0, 0);
    lv_obj_center(thinking_bulb_inner_);

    // Tia sét bên trong — toạ độ tương đối trong bulb 18x18
    thinking_zap_ = lv_line_create(thinking_bulb_);
    thinking_zap_pts_[0] = {11, 3};
    thinking_zap_pts_[1] = {6, 8};
    thinking_zap_pts_[2] = {10, 8};
    thinking_zap_pts_[3] = {6, 15};
    lv_line_set_points(thinking_zap_, thinking_zap_pts_, 4);
    lv_obj_set_style_line_width(thinking_zap_, 2, 0);
    lv_obj_set_style_line_color(thinking_zap_, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(thinking_zap_, true, 0);
    lv_obj_add_flag(thinking_zap_, LV_OBJ_FLAG_HIDDEN);

    // Chuôi đèn dài hơn: 8x6
    thinking_base_ = lv_obj_create(face_root_);
    lv_obj_set_size(thinking_base_, 8, 6);
    lv_obj_set_style_radius(thinking_base_, 1, 0);
    lv_obj_set_style_bg_color(thinking_base_, lv_color_black(), 0);
    lv_obj_set_style_border_width(thinking_base_, 0, 0);
    lv_obj_set_style_pad_all(thinking_base_, 0, 0);
    lv_obj_set_pos(thinking_base_, 109, 18);   // ngay dưới bulb (bulb bottom = 20)
    lv_obj_add_flag(thinking_base_, LV_OBJ_FLAG_HIDDEN);

    // 3 tia sáng quanh đỉnh bulb (dịch theo bulb mới)
    static const lv_coord_t kRayData[3][4] = {
        {  98,  5,   95,  2 },   // trên-trái
        { 113,  1,  113, 0 },    // trên
        { 128,  5,  131,  2 },   // trên-phải
    };
    for (int i = 0; i < 3; i++) {
        thinking_ray_pts_[i][0] = {(lv_value_precise_t)kRayData[i][0], (lv_value_precise_t)kRayData[i][1]};
        thinking_ray_pts_[i][1] = {(lv_value_precise_t)kRayData[i][2], (lv_value_precise_t)kRayData[i][3]};
        thinking_rays_[i] = lv_line_create(face_root_);
        lv_line_set_points(thinking_rays_[i], thinking_ray_pts_[i], 2);
        lv_obj_set_style_line_width(thinking_rays_[i], 2, 0);
        lv_obj_set_style_line_color(thinking_rays_[i], lv_color_black(), 0);
        lv_obj_set_style_line_rounded(thinking_rays_[i], true, 0);
        lv_obj_add_flag(thinking_rays_[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ============ BOB ANIMATION ============
    // Nhấp nhô face lên xuống 0 → 3px, ping-pong, ~2.6s/vòng
    lv_anim_init(&bob_anim_);
    lv_anim_set_var(&bob_anim_, face_root_);
    lv_anim_set_exec_cb(&bob_anim_, [](void* var, int32_t v) {
        lv_obj_set_y(static_cast<lv_obj_t*>(var), v);
    });
    lv_anim_set_values(&bob_anim_, 0, 3);
    lv_anim_set_duration(&bob_anim_, 800);
    lv_anim_set_playback_duration(&bob_anim_, 800);
    lv_anim_set_repeat_count(&bob_anim_, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&bob_anim_, lv_anim_path_ease_in_out);
    lv_anim_start(&bob_anim_);

    // Khởi tạo animation state cho neutral (chỉ chạy 1 lần lúc boot)
    neutral_anim_tick_  = 0;
    eye_fall_state_     = 0;
    eye_fall_side_      = 0;
    eye_fall_next_tick_ = 200;   // ~16s sau boot, mắt rơi lần đầu

    if (face_timer_ != nullptr) lv_timer_delete(face_timer_);
    face_timer_ = lv_timer_create(FaceTimerCallback, 80, this);
}

void OledCustomEmojiDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping");
        return;
    }

    if (IsFaceMode()) {
        Display::SetupUI();
        DisplayLockGuard lock(this);
        SetupFaceUI_128x64();
        ESP_LOGI(TAG, "Enabled 128x%d full-screen robot face mode", height_);
        return;
    }

    OledDisplay::SetupUI();
    if (emotion_label_ != nullptr) {
        lv_obj_set_style_text_font(emotion_label_, &font_awesome_20_4, 0);
        lv_obj_set_style_pad_top(emotion_label_, 0, 0);
    }
    SetEmotion("neutral");
}

void OledCustomEmojiDisplay::SetStatus(const char* status) {
    OledDisplay::SetStatus(status);
    if (!IsFaceMode() || face_root_ == nullptr || status == nullptr) return;

    // ---- Xử lý lip-sync khi SPEAKING ----
    bool speaking = std::strcmp(status, Lang::Strings::SPEAKING) == 0;
    if (speaking_ != speaking) {
        speaking_ = speaking;
        speaking_frame_ = 0;
        ESP_LOGI(TAG, "lip-sync %s", speaking_ ? "ON" : "OFF");
        DisplayLockGuard lock(this);
        RenderFace();
    }

    // ---- Auto emotion theo trạng thái hệ thống ----
    // Chỉ đổi mặt khi KHÔNG đang nói (tránh giật face giữa câu)
    if (speaking_) return;

    if (std::strcmp(status, Lang::Strings::STANDBY) == 0) {
        // Vào standby: đánh dấu mốc thời gian, chưa sleepy vội
        in_standby_ = true;
        standby_enter_tick_ = frame_count_;
        if (std::strcmp(current_face_emotion_, "sleepy") != 0 &&
            std::strcmp(current_face_emotion_, "neutral") != 0) {
            ESP_LOGI(TAG, "auto face: standby -> neutral (waiting to sleep)");
            current_face_emotion_ = "neutral";
            DisplayLockGuard lock(this);
            RenderFace();
        }
    } else if (std::strcmp(status, Lang::Strings::LISTENING) == 0) {
        // Có wake → hủy hẹn sleepy, về neutral
        in_standby_ = false;
        if (std::strcmp(current_face_emotion_, "sleepy") == 0 ||
            std::strcmp(current_face_emotion_, "neutral") != 0) {
            ESP_LOGI(TAG, "auto face: listening -> neutral (wake up)");
            current_face_emotion_ = "neutral";
            DisplayLockGuard lock(this);
            RenderFace();
        }
    }
}

void OledCustomEmojiDisplay::FaceTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<OledCustomEmojiDisplay*>(lv_timer_get_user_data(timer));
    if (self) self->AnimateFaceTick();
}

void OledCustomEmojiDisplay::AnimateFaceTick() {
    // ✅ ĐÃ SỬA: dùng mouth_line_ thay vì mouth_label_
    if (!IsFaceMode() || face_root_ == nullptr || mouth_line_ == nullptr) return;

    bool need_render = false;
    ++frame_count_;

    // Đang hiện QR → không animate face (OledDisplay tự ẩn QR khi hết giờ)
    if (IsQrVisible()) {
        return;
    }

    if (std::strcmp(current_face_emotion_, "sleepy") != 0 &&
        std::strcmp(current_face_emotion_, "thinking") != 0) {
        if (blink_phase_ == 0 && frame_count_ >= next_blink_frame_) {
            blink_phase_ = 1;
            blink_closed_ = true;
            need_render = true;
        } else if (blink_phase_ == 1) {
            blink_phase_ = 2;
        } else if (blink_phase_ == 2) {
            blink_closed_ = false;
            blink_phase_ = 0;
            next_blink_frame_ = frame_count_ + 22;
            need_render = true;
        }
    }

    if (speaking_) {
        // Chia nhịp: 1 frame mới mỗi 2 tick (160ms) → mượt và tự nhiên hơn
        static uint8_t tick_divider = 0;
        if (++tick_divider >= 2) {
            tick_divider = 0;
            // Sequence preset để miệng "nhảy" tự nhiên, không tuần tự 0→1→2→3
            static const uint8_t kSeq[] = { 2, 3, 1, 4, 2, 3, 5, 0, 2, 3, 4, 1 };
            static uint8_t seq_idx = 0;
            speaking_frame_ = kSeq[seq_idx];
            seq_idx = (seq_idx + 1) % (sizeof(kSeq) / sizeof(kSeq[0]));
            need_render = true;
        }
    }
    // ===== SAD ANIMATION: nước mắt chảy =====
    if (std::strcmp(current_face_emotion_, "sad") == 0 &&
        sad_left_tear_ && sad_right_tear_) {

        sad_anim_tick_++;

        // Tear trái: chu kỳ 50 tick (~4s). Chảy trong 35 tick đầu, ẩn 15 tick sau.
        int lp = sad_anim_tick_ % 50;
        if (lp >= 5 && lp < 40) {
            int t = lp - 5;                      // 0..34
            int y = 42 + (t * 16 / 35);          // 42 → 58 (chảy xuống 16px)
            lv_obj_set_pos(sad_left_tear_, 30, y);
            lv_obj_clear_flag(sad_left_tear_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sad_left_tear_, LV_OBJ_FLAG_HIDDEN);
        }

        // Tear phải: chu kỳ lệch 25 tick để 2 mắt không chảy cùng lúc
        int rp = (sad_anim_tick_ + 25) % 50;
        if (rp >= 5 && rp < 40) {
            int t = rp - 5;
            int y = 42 + (t * 16 / 35);
            lv_obj_set_pos(sad_right_tear_, 93, y);
            lv_obj_clear_flag(sad_right_tear_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sad_right_tear_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    // ===== ANGRY ANIMATION: dấu giận rung nhẹ =====
    if (std::strcmp(current_face_emotion_, "angry") == 0 && angry_mark_root_) {
        angry_anim_tick_++;
        // Rung kiểu bất đối xứng: 2 tick đổi X, 3 tick đổi Y
        int dx = (angry_anim_tick_ % 4 < 2) ? 0 : 1;
        int dy = (angry_anim_tick_ % 6 < 3) ? 0 : 1;
        lv_obj_set_pos(angry_mark_root_, 98 + dx, 2 + dy);
        // Không set need_render — dùng lv_obj_set_pos trực tiếp, không cần vẽ lại toàn face
    }

        // ===== SURPRISED ANIMATION: tròng mắt rung nhẹ =====
    if (std::strcmp(current_face_emotion_, "surprised") == 0 &&
        left_pupil_ && right_pupil_ && !blink_closed_) {

        surprised_anim_tick_++;
        // Cứ 4 tick (~320ms) đổi vị trí rung
        int dx = 0, dy = 0;
        switch (surprised_anim_tick_ % 4) {
            case 0: dx = 0; dy = 0; break;
            case 1: dx = 1; dy = 0; break;
            case 2: dx = 0; dy = 1; break;
            case 3: dx =-1; dy = 0; break;
        }
        // Tròng trái gốc ở (15, 9), tròng phải gốc ở (3, 9)
        lv_obj_set_pos(left_pupil_,  15 + dx, 9 + dy);
        lv_obj_set_pos(right_pupil_, 3  - dx, 9 + dy);
        // Rung đối xứng: khi tròng trái dịch phải thì tròng phải dịch trái
    }

        // ===== SLEEPY ANIMATION =====
    if (std::strcmp(current_face_emotion_, "sleepy") == 0 &&
        sleepy_left_eye_ && sleepy_right_eye_ && sleepy_mouth_ && sleepy_drool_) {

        sleepy_anim_tick_ = (sleepy_anim_tick_ + 1) % 180;
        int tick = sleepy_anim_tick_;

        int mouth_size;
        int eye_rot;
        bool show_drool = false;
        bool mouth_as_line = false;

        // ⬇️ Mắt LUÔN là thanh mỏng 26x4, chỉ thay đổi GÓC XOAY
        if (tick < 30) {                 // Phase 0: bình thường — nghiêng nhẹ
            mouth_size = 14;
            eye_rot = 100;               // 10° — nhìn hơi buồn ngủ
        } else if (tick < 55) {          // Phase 1: ngáp mở — nghiêng tăng dần
            int t = tick - 30;
            mouth_size = 14 + (t * 26 / 25);       // 14 → 40
            eye_rot = 100 + (t * 150 / 25);        // 10° → 25°
        } else if (tick < 75) {          // Phase 2: giữ đỉnh — nghiêng nhất
            mouth_size = 40;
            eye_rot = 250;               // 25°
        } else if (tick < 100) {         // Phase 3: khép — nghiêng giảm dần
            int t = tick - 75;
            mouth_size = 40 - (t * 26 / 25);       // 40 → 14
            eye_rot = 250 - (t * 150 / 25);        // 25° → 10°
        } else {                          // Phase 4: buồn ngủ sâu
            mouth_size = 14;
            eye_rot = 100;               // 10° — mắt cụp xuống
            show_drool = true;
            mouth_as_line = true;
        }

        // Mắt — tâm cố định, gần nhau
        const int left_center_x  = 42;
        const int right_center_x = 86;
        const int eye_w = 26, eye_h = 4;
        lv_obj_set_size(sleepy_left_eye_,  eye_w, eye_h);
        lv_obj_set_size(sleepy_right_eye_, eye_w, eye_h);
        lv_obj_set_pos(sleepy_left_eye_,  left_center_x  - eye_w/2, 20);
        lv_obj_set_pos(sleepy_right_eye_, right_center_x - eye_w/2, 20);

        // ĐẢO DẤU — 2 đầu trên hướng vào nhau: `/ \`
        lv_obj_set_style_transform_rotation(sleepy_left_eye_,  -eye_rot, 0);
        lv_obj_set_style_transform_rotation(sleepy_right_eye_,  eye_rot, 0);

        // Miệng — tâm ở (64, 42)
        if (mouth_as_line) {
            lv_obj_set_size(sleepy_mouth_, 22, 4);
            lv_obj_set_style_radius(sleepy_mouth_, 2, 0);
            lv_obj_set_pos(sleepy_mouth_, 64 - 11, 42);
        } else {
            lv_obj_set_size(sleepy_mouth_, mouth_size, mouth_size);
            lv_obj_set_style_radius(sleepy_mouth_, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_pos(sleepy_mouth_, 64 - mouth_size/2, 42 - mouth_size/2);
        }

        // Nước miếng — rơi từ mép dưới miệng, KHÔNG fade
        if (show_drool) {
            int dp = (tick - 100) % 40;
            if (dp < 32) {
                int dy = 48 + (dp * 12 / 32);       // 48 → 60
                lv_obj_clear_flag(sleepy_drool_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(sleepy_drool_, 70, dy);
                lv_obj_set_style_opa(sleepy_drool_, LV_OPA_COVER, 0);
            } else {
                lv_obj_add_flag(sleepy_drool_, LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            lv_obj_add_flag(sleepy_drool_, LV_OBJ_FLAG_HIDDEN);
        }

        for (int i = 0; i < 3; i++) {
            int zi = (tick + i * 30) % 90;
            if (zi < 80) {
                float zp = zi / 80.0f;
                // int zx  = 28 - (int)(zp * 24);
                // int zy  = 22 - (int)(zp * 22);
                int zx  = 26 - (int)(zp * 22);
                int zy  = 20 - (int)(zp * 20);
                // Scale: 0.5x → 1.5x  (LVGL 9: 256 = 1.0x)
                int scale = 128 + (int)(zp * 256);

                lv_obj_clear_flag(sleepy_z_[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(sleepy_z_[i], zx, zy);
                lv_obj_set_style_opa(sleepy_z_[i], LV_OPA_COVER, 0);
                lv_obj_set_style_transform_scale(sleepy_z_[i], scale, 0);
            } else {
                lv_obj_add_flag(sleepy_z_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        // need_render = true;
    }

        // ===== NEUTRAL (LISTENING) ANIMATION =====
    if (std::strcmp(current_face_emotion_, "neutral") == 0 &&
        left_eye_ && right_eye_ && left_pupil_ && right_pupil_ && !speaking_) {

        neutral_anim_tick_++;

        // === GAZE — nhanh hơn: mỗi mốc 8 tick (640ms) ===
        static const int8_t kGazeTable[18] = {
            0, -30, -60, -90, -100, -100, -100, -60, 0, 60,
            100, 100, 100, 60, 0, 0, 0, 0
        };
        int gi = (neutral_anim_tick_ / 2) % 18;    // ⬅️ /10 → /8
        int gaze = kGazeTable[gi];

        // === MẮT ===
        int base_w = 28, base_h = 36;
        int size_diff = gaze / 15;
        int left_w  = base_w + size_diff;
        int left_h  = base_h + size_diff;
        int right_w = base_w - size_diff;
        int right_h = base_h - size_diff;
        if (left_w  < 20) left_w  = 20;
        if (left_h  < 28) left_h  = 28;
        if (right_w < 20) right_w = 20;
        if (right_h < 28) right_h = 28;

        lv_obj_set_size(left_eye_,  left_w,  left_h);
        lv_obj_set_size(right_eye_, right_w, right_h);

        int left_x  = 40 - left_w  / 2;
        int right_x = 88 - right_w / 2;

        // === TRÒNG MẮT ===
        int pupil_base = 10;
        int pupil_diff = gaze / 22;
        int lp_w = pupil_base + pupil_diff;
        int lp_h = pupil_base + pupil_diff;
        int rp_w = pupil_base - pupil_diff;
        int rp_h = pupil_base - pupil_diff;
        if (lp_w < 5) lp_w = 5;
        if (lp_h < 5) lp_h = 5;
        if (rp_w < 5) rp_w = 5;
        if (rp_h < 5) rp_h = 5;

        lv_obj_set_size(left_pupil_,  lp_w, lp_h);
        lv_obj_set_size(right_pupil_, rp_w, rp_h);

        int lp_range = left_w  - lp_w - 4;
        int rp_range = right_w - rp_w - 4;
        if (lp_range < 1) lp_range = 1;
        if (rp_range < 1) rp_range = 1;
        int lp_x = 2 + (gaze + 100) * lp_range / 200;
        int rp_x = 2 + (gaze + 100) * rp_range / 200;
        int lp_y = (left_h  - lp_h) / 2;
        int rp_y = (right_h - rp_h) / 2;
        lv_obj_set_pos(left_pupil_,  lp_x, lp_y);
        lv_obj_set_pos(right_pupil_, rp_x, rp_y);

        // === MẮT TUỘT XUỐNG ===
        int left_y  = 15;
        int right_y = 15;

        if (eye_fall_state_ == 0) {
            if (frame_count_ >= eye_fall_next_tick_) {
                eye_fall_state_ = 1;
                eye_fall_tick_  = 0;
                eye_fall_side_  = ((neutral_anim_tick_ / 10) % 2 == 0) ? 1 : 2;
            }
        } else if (eye_fall_state_ == 1) {
            eye_fall_tick_++;
            int fall_y = eye_fall_tick_ / 2;
            if (fall_y >= 15) {
                fall_y = 15;
                eye_fall_state_ = 2;
                eye_fall_tick_  = 0;
            }
            if (eye_fall_side_ == 1) left_y  = 15 + fall_y;
            else                     right_y = 15 + fall_y;
        } else if (eye_fall_state_ == 2) {
            eye_fall_tick_++;
            int fall_y = 15 - (int)eye_fall_tick_ * 4;
            if (fall_y <= 0) {
                fall_y = 0;
                eye_fall_state_ = 0;
                eye_fall_side_  = 0;
                eye_fall_next_tick_ = frame_count_ + 100 + (frame_count_ % 200);
            }
            if (eye_fall_side_ == 1) left_y  = 15 + fall_y;
            else                     right_y = 15 + fall_y;
        }

        lv_obj_set_pos(left_eye_,  left_x,  left_y);
        lv_obj_set_pos(right_eye_, right_x, right_y);

        // === MIỆNG DI CHUYỂN THEO GAZE ===
        // gaze = -100 → miệng dịch trái 5px
        // gaze = +100 → miệng dịch phải 5px
        int mouth_dx = gaze / 18;
        mouth_pts_[0].x = 54 + mouth_dx;
        mouth_pts_[1].x = 74 + mouth_dx;
        mouth_pts_[0].y = 50;
        mouth_pts_[1].y = 50;
        lv_line_set_points(mouth_line_, mouth_pts_, 2);

        need_render = true;
    }

        // ===== AUTO SLEEPY SAU KHI IDLE =====
    // Test: 5 giây = 5 * 1000 / 80 = 62 tick
    // Production (5 phút): đổi thành 3750
    constexpr uint32_t kSleepyDelayTicks = 62;

    if (in_standby_ &&
        (frame_count_ - standby_enter_tick_) >= kSleepyDelayTicks &&
        std::strcmp(current_face_emotion_, "sleepy") != 0) {
        ESP_LOGI(TAG, "auto face: idle timeout -> sleepy");
        current_face_emotion_ = "sleepy";
        need_render = true;
    }

        // ===== THINKING ANIMATION =====
    if (std::strcmp(current_face_emotion_, "thinking") == 0 &&
        thinking_bulb_ && thinking_rays_[0]) {

        thinking_anim_tick_++;

        // ⬇️ Mắt trượt lên trong 20 tick (~1.6s)
        int eye_y;
        if (thinking_anim_tick_ < 20) {
            eye_y = 15 - (thinking_anim_tick_ * 11 / 20);
        } else {
            eye_y = 4;
        }
        lv_obj_set_pos(left_eye_,  28, eye_y);
        lv_obj_set_pos(right_eye_, 72, eye_y);

        // Tia sáng nhấp nháy
        for (int i = 0; i < 3; i++) {
            int ph = (thinking_anim_tick_ + i * 3) % 16;
            if (ph < 8) {
                lv_obj_clear_flag(thinking_rays_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(thinking_rays_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        // ⬇️ Tròng mắt đảo qua lại sau khi đã lên đỉnh
        // Sau tick 20, dùng bảng gaze giống neutral (mốc 8 tick)
        static const int8_t kThinkGaze[10] = {
            0, 60, 100, 100, 60, 0, -60, -100, -100, -60
        };
        int gaze_idx = 0;
        if (thinking_anim_tick_ >= 20) {
            gaze_idx = ((thinking_anim_tick_ - 20) / 8) % 10;
        }
        int gaze = kThinkGaze[gaze_idx];   // -100..+100

        // Vị trí gốc của tròng khi mắt ở trên đỉnh
        // (pupil trái 7x7 trong eye 24x32, pupil phải 11x11)
        int lp_range = 24 - 7 - 4;   // = 13
        int rp_range = 24 - 11 - 4;  // = 9
        int lp_x = 2 + (gaze + 100) * lp_range / 200;
        int rp_x = 2 + (gaze + 100) * rp_range / 200;

        lv_obj_set_pos(left_pupil_,  lp_x, 2);
        lv_obj_set_pos(right_pupil_, rp_x, 1);

        // Miệng nghiêng theo gaze:
        //   gaze = -100 (nhìn trái)  → miệng `\` (trái cao, phải thấp)
        //   gaze = +100 (nhìn phải) → miệng `/` (trái thấp, phải cao)
        int tilt_y = gaze * 4 / 100;    // -4..+4
        mouth_pts_[0].x = 54;
        mouth_pts_[1].x = 74;
        mouth_pts_[0].y = 54 + tilt_y;  // đầu trái
        mouth_pts_[1].y = 54 - tilt_y;  // đầu phải
        lv_line_set_points(mouth_line_, mouth_pts_, 2);
    
    }


    if (need_render) RenderFace();
}   

void OledCustomEmojiDisplay::RenderFace() {
    if (!IsFaceMode() || face_root_ == nullptr || mouth_line_ == nullptr) return;

    // Ẩn hết mọi thứ trước khi vẽ
    lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(left_eye_line_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(right_eye_line_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    if (mouth_obj_ != nullptr) lv_obj_add_flag(mouth_obj_, LV_OBJ_FLAG_HIDDEN);
    if (mouth_inner_ != nullptr) lv_obj_add_flag(mouth_inner_, LV_OBJ_FLAG_HIDDEN);
    if (mouth_obj_ != nullptr) lv_obj_add_flag(mouth_obj_, LV_OBJ_FLAG_HIDDEN);
    if (mouth_inner_ != nullptr) lv_obj_add_flag(mouth_inner_, LV_OBJ_FLAG_HIDDEN);

    if (happy_left_eye_arc_)  lv_obj_add_flag(happy_left_eye_arc_,  LV_OBJ_FLAG_HIDDEN);
    if (happy_right_eye_arc_) lv_obj_add_flag(happy_right_eye_arc_, LV_OBJ_FLAG_HIDDEN);
    if (happy_mouth_arc_)     lv_obj_add_flag(happy_mouth_arc_,     LV_OBJ_FLAG_HIDDEN);

    if (sad_left_eye_)       lv_obj_add_flag(sad_left_eye_,       LV_OBJ_FLAG_HIDDEN);
    if (sad_right_eye_)      lv_obj_add_flag(sad_right_eye_,      LV_OBJ_FLAG_HIDDEN);
    if (sad_left_eye_cut_)   lv_obj_add_flag(sad_left_eye_cut_,   LV_OBJ_FLAG_HIDDEN);
    if (sad_right_eye_cut_)  lv_obj_add_flag(sad_right_eye_cut_,  LV_OBJ_FLAG_HIDDEN);
    if (sad_mouth_arc_)      lv_obj_add_flag(sad_mouth_arc_,      LV_OBJ_FLAG_HIDDEN);
    if (sad_left_tear_)      lv_obj_add_flag(sad_left_tear_,      LV_OBJ_FLAG_HIDDEN);
    if (sad_right_tear_)     lv_obj_add_flag(sad_right_tear_,     LV_OBJ_FLAG_HIDDEN);

    if (angry_left_eye_)       lv_obj_add_flag(angry_left_eye_,       LV_OBJ_FLAG_HIDDEN);
    if (angry_right_eye_)      lv_obj_add_flag(angry_right_eye_,      LV_OBJ_FLAG_HIDDEN);
    if (angry_left_eye_cut_)   lv_obj_add_flag(angry_left_eye_cut_,   LV_OBJ_FLAG_HIDDEN);
    if (angry_right_eye_cut_)  lv_obj_add_flag(angry_right_eye_cut_,  LV_OBJ_FLAG_HIDDEN);
    if (angry_mark_root_)      lv_obj_add_flag(angry_mark_root_,      LV_OBJ_FLAG_HIDDEN);

    if (surprised_mouth_)     lv_obj_add_flag(surprised_mouth_,     LV_OBJ_FLAG_HIDDEN);

    if (sleepy_left_eye_)   lv_obj_add_flag(sleepy_left_eye_,   LV_OBJ_FLAG_HIDDEN);
    if (sleepy_right_eye_)  lv_obj_add_flag(sleepy_right_eye_,  LV_OBJ_FLAG_HIDDEN);
    if (sleepy_mouth_)      lv_obj_add_flag(sleepy_mouth_,      LV_OBJ_FLAG_HIDDEN);
    if (sleepy_drool_)      lv_obj_add_flag(sleepy_drool_,      LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        if (sleepy_z_[i]) lv_obj_add_flag(sleepy_z_[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (thinking_bulb_)       lv_obj_add_flag(thinking_bulb_,       LV_OBJ_FLAG_HIDDEN);
    if (thinking_bulb_inner_) lv_obj_add_flag(thinking_bulb_inner_, LV_OBJ_FLAG_HIDDEN);
    if (thinking_zap_)        lv_obj_add_flag(thinking_zap_,        LV_OBJ_FLAG_HIDDEN);
    if (thinking_base_)       lv_obj_add_flag(thinking_base_,       LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 3; i++) {
        if (thinking_rays_[i]) lv_obj_add_flag(thinking_rays_[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (strcmp(current_face_emotion_, "happy") == 0) {
        // Mặt vui: mắt ^^ và miệng ◡ — dùng arc mượt
        lv_obj_clear_flag(happy_left_eye_arc_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(happy_right_eye_arc_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(happy_mouth_arc_,     LV_OBJ_FLAG_HIDDEN);

    } else if (strcmp(current_face_emotion_, "surprised") == 0) {
        // Mặt ngạc nhiên: mắt to tròn + lé vào trong + miệng chữ O
        lv_obj_clear_flag(left_eye_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);

        if (blink_closed_) {
            // Đang chớp: mắt dẹp thành đường ngang
            lv_obj_set_size(left_eye_,  24, 3);
            lv_obj_set_pos(left_eye_,   30, 32);
            lv_obj_set_size(right_eye_, 24, 3);
            lv_obj_set_pos(right_eye_,  74, 32);

            // Chớp thì ẩn tròng
            lv_obj_add_flag(left_pupil_,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Mắt to tròn
            lv_obj_set_size(left_eye_,  26, 26);
            lv_obj_set_pos(left_eye_,   28, 20);
            lv_obj_set_size(right_eye_, 26, 26);
            lv_obj_set_pos(right_eye_,  74, 20);

            // Hiện tròng trắng, đặt lệch VÀO TRONG để lé tụ lại gần nhau
            lv_obj_clear_flag(left_pupil_,  LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(left_pupil_,  8, 8);
            lv_obj_set_size(right_pupil_, 8, 8);

            // Mắt trái 26x26: tròng ở góc PHẢI (lé sang phải)
            //   x = 26 - 8 - 3 = 15 (sát mép phải)
            //   y = (26 - 8) / 2 = 9 (giữa theo chiều dọc)
            // Mắt phải 26x26: tròng ở góc TRÁI (lé sang trái)
            //   x = 3 (sát mép trái)
            //   y = 9
            lv_obj_set_pos(left_pupil_,  15, 9);
            lv_obj_set_pos(right_pupil_, 3,  9);
        }

        // Miệng chữ O
        lv_obj_clear_flag(surprised_mouth_, LV_OBJ_FLAG_HIDDEN);
        // Reset tick khi vừa vào trạng thái surprised
        surprised_anim_tick_ = 0;
    
    } else if (strcmp(current_face_emotion_, "sleepy") == 0) {
        // Mặt buồn ngủ: mắt + miệng + nước miếng + Z
        lv_obj_clear_flag(sleepy_left_eye_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sleepy_right_eye_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sleepy_mouth_,     LV_OBJ_FLAG_HIDDEN);
        
        // Z và drool sẽ do AnimateFaceTick điều khiển

    } else if (strcmp(current_face_emotion_, "sad") == 0) {
        // Mặt buồn: 2 mắt viên phân nghiêng 
        lv_obj_clear_flag(sad_left_eye_,      LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sad_right_eye_,     LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sad_left_eye_cut_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sad_right_eye_cut_, LV_OBJ_FLAG_HIDDEN);
        // lv_obj_clear_flag(sad_mouth_arc_,     LV_OBJ_FLAG_HIDDEN);

    } else if (strcmp(current_face_emotion_, "angry") == 0) {
        // Mặt tức giận: 2 mắt xiên ngược + dấu giận 4 góc
        lv_obj_clear_flag(angry_left_eye_,       LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(angry_right_eye_,      LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(angry_left_eye_cut_,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(angry_right_eye_cut_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(angry_mark_root_,      LV_OBJ_FLAG_HIDDEN);
        // Reset dấu giận về vị trí gốc
        lv_obj_set_pos(angry_mark_root_, 98, 2);
        angry_anim_tick_ = 0;

        } else if (strcmp(current_face_emotion_, "thinking") == 0) {
        // Mắt bắt đầu từ vị trí neutral (y=15), AnimateFaceTick sẽ kéo lên dần
        lv_obj_clear_flag(left_eye_,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_eye_,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(left_pupil_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(left_eye_,  24, 32);
        lv_obj_set_pos(left_eye_,   26, 15);   // ⬅️ bắt đầu từ neutral, sẽ được slide lên
        lv_obj_set_size(right_eye_, 24, 32);
        lv_obj_set_pos(right_eye_,  74, 15);

        lv_obj_set_size(left_pupil_,  7, 7);
        lv_obj_set_pos(left_pupil_,   10, 2);
        lv_obj_set_size(right_pupil_, 11, 11);
        lv_obj_set_pos(right_pupil_,  7, 1);

        mouth_pts_[0] = {54, 52};
        mouth_pts_[1] = {74, 52};
        lv_line_set_points(mouth_line_, mouth_pts_, 2);
        lv_obj_set_style_line_width(mouth_line_, 4, 0);
        lv_obj_clear_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_clear_flag(thinking_bulb_,       LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(thinking_bulb_inner_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(thinking_zap_,        LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(thinking_base_,       LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < 3; i++) {
            lv_obj_clear_flag(thinking_rays_[i], LV_OBJ_FLAG_HIDDEN);
        }
        thinking_anim_tick_ = 0;

    } else {
        // --- NEUTRAL / LISTENING ---
        lv_obj_clear_flag(left_eye_,   LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_eye_,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(left_pupil_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_pupil_,LV_OBJ_FLAG_HIDDEN);

        // mouth_pts_[0] = {54, 50};
        // mouth_pts_[1] = {74, 50};
        lv_line_set_points(mouth_line_, mouth_pts_, 2);
        lv_obj_set_style_line_width(mouth_line_, 4, 0);
        lv_obj_clear_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    }
        // ===== LIP-SYNC OVERRIDE =====
    if (speaking_ && mouth_obj_ != nullptr) {
        lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
        if (happy_mouth_arc_) lv_obj_add_flag(happy_mouth_arc_, LV_OBJ_FLAG_HIDDEN);
        if (sad_mouth_arc_)   lv_obj_add_flag(sad_mouth_arc_,   LV_OBJ_FLAG_HIDDEN);
        if (surprised_mouth_) lv_obj_add_flag(surprised_mouth_, LV_OBJ_FLAG_HIDDEN);
        if (sleepy_mouth_)    lv_obj_add_flag(sleepy_mouth_,    LV_OBJ_FLAG_HIDDEN);   
        if (sleepy_drool_)    lv_obj_add_flag(sleepy_drool_,    LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(mouth_obj_, LV_OBJ_FLAG_HIDDEN);

        // Bảng frame miệng: {rộng_ngoài, cao_ngoài, rộng_trong, cao_trong}
        // trong = 0 → ẩn hốc (miệng đóng/gần đóng)
        // Tâm miệng cố định ở (64, 50)
        static const struct { int wo, ho, wi, hi; } kFrames[6] = {
            { 22,  4,  0,  0 },   // 0: đóng — dẹp ngang
            { 20,  6, 12,  2 },   // 1: hé nhẹ
            { 24, 12, 16,  6 },   // 2: mở vừa — oval ngang
            { 22, 20, 14, 14 },   // 3: mở to — gần tròn
            { 26, 14, 20,  8 },   // 4: oval rộng
            { 20, 16, 14, 10 },   // 5: oval dọc nhỏ
        };
        uint8_t f = speaking_frame_ % 6;

        int wo = kFrames[f].wo;
        int ho = kFrames[f].ho;
        lv_obj_set_size(mouth_obj_, wo, ho);
        lv_obj_set_pos(mouth_obj_, 64 - wo / 2, 50 - ho / 2);

        if (kFrames[f].wi > 0 && kFrames[f].hi > 0) {
            lv_obj_clear_flag(mouth_inner_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_size(mouth_inner_, kFrames[f].wi, kFrames[f].hi);
            lv_obj_center(mouth_inner_);
        } else {
            lv_obj_add_flag(mouth_inner_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}


void OledCustomEmojiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') emotion = "neutral";

    if (IsFaceMode() && face_root_ != nullptr) {
        if (in_standby_ && std::strcmp(current_face_emotion_, "sleepy") == 0) {
            return;
        }

        const char* mapped = ResolveOledEmotionFromFolderProfile(emotion);
        if (mapped == nullptr) mapped = emotion;
        const char* new_emotion = robot_face::NormalizeEmotion(mapped);

        // ⬇️ Chỉ RenderFace khi emotion THỰC SỰ đổi
        if (std::strcmp(current_face_emotion_, new_emotion) == 0) {
            return;
        }

        current_face_emotion_ = new_emotion;
        ESP_LOGI(TAG, "face emotion '%s' -> '%s'", emotion, current_face_emotion_);
        DisplayLockGuard lock(this);
        RenderFace();
        return;
    }


    const char* oled_emotion = ResolveOledEmotionFromFolderProfile(emotion);
    if (oled_emotion != nullptr) {
        OledDisplay::SetEmotion(oled_emotion);
        return;
    }
    OledDisplay::SetEmotion(emotion);
}
void OledCustomEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    // Log role để debug lần đầu — xem role name thực tế là gì
    ESP_LOGI(TAG, "SetChatMessage role='%s'", role ? role : "null");

    // Chỉ phân tích emotion khi ROBOT nói (role = "assistant" hoặc tương tự)
    if (IsFaceMode() && face_root_ != nullptr &&
        role != nullptr && content != nullptr &&
        std::strcmp(role, "assistant") == 0) {

        const char* override_emo = DetectEmotionFromText(content);
        if (override_emo != nullptr &&
            std::strcmp(current_face_emotion_, override_emo) != 0) {
            ESP_LOGI(TAG, "override from text: '%s' (was '%s')",
                     override_emo, current_face_emotion_);
            current_face_emotion_ = override_emo;
            DisplayLockGuard lock(this);
            RenderFace();
        }
    }

    // Gọi parent để giữ hành vi cũ (với face-only thì parent return sớm)
    OledDisplay::SetChatMessage(role, content);
}

void OledCustomEmojiDisplay::OnQrVisibilityChanged(bool visible) {
    if (face_root_ == nullptr) return;
    if (visible) {
        lv_obj_add_flag(face_root_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(face_root_, LV_OBJ_FLAG_HIDDEN);
    }
}
