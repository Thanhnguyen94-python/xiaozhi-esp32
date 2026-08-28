#include "oled_custom_emoji_display.h"
#include "robot_face/happy_face_frames.h"
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

lv_image_dsc_t MakeMonoBitmap(const uint8_t* data, uint32_t size, uint16_t w, uint16_t h) {
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_I1;
    image.header.w = w;
    image.header.h = h;
    image.data_size = size;
    image.data = data;
    return image;
}

constexpr uint32_t kI1PaletteBytes = sizeof(lv_color32_t) * 2;
constexpr uint32_t kHappyLvglDataSize = kI1PaletteBytes + robot_face::kHappyBitmapDataSize;
uint8_t gHappyBitmap1Lvgl[kHappyLvglDataSize];
uint8_t gHappyBitmap2Lvgl[kHappyLvglDataSize];
lv_image_dsc_t kHappyBitmap1 = {};
lv_image_dsc_t kHappyBitmap2 = {};
bool gHappyBitmapReady = false;

void EnsureHappyBitmapReady() {
    if (gHappyBitmapReady) {
        return;
    }

    lv_color32_t white{};
    white.red = 0xFF;
    white.green = 0xFF;
    white.blue = 0xFF;
    white.alpha = 0xFF;

    lv_color32_t black{};
    black.red = 0x00;
    black.green = 0x00;
    black.blue = 0x00;
    black.alpha = 0xFF;

    std::memcpy(gHappyBitmap1Lvgl, &white, sizeof(lv_color32_t));
    std::memcpy(gHappyBitmap1Lvgl + sizeof(lv_color32_t), &black, sizeof(lv_color32_t));
    std::memcpy(gHappyBitmap1Lvgl + kI1PaletteBytes, robot_face::Fr2_GIF1, robot_face::kHappyBitmapDataSize);

    std::memcpy(gHappyBitmap2Lvgl, &white, sizeof(lv_color32_t));
    std::memcpy(gHappyBitmap2Lvgl + sizeof(lv_color32_t), &black, sizeof(lv_color32_t));
    std::memcpy(gHappyBitmap2Lvgl + kI1PaletteBytes, robot_face::Fr2_GIF2, robot_face::kHappyBitmapDataSize);

    kHappyBitmap1 = MakeMonoBitmap(gHappyBitmap1Lvgl, kHappyLvglDataSize,
        robot_face::kHappyBitmapWidth, robot_face::kHappyBitmapHeight);
    kHappyBitmap2 = MakeMonoBitmap(gHappyBitmap2Lvgl, kHappyLvglDataSize,
        robot_face::kHappyBitmapWidth, robot_face::kHappyBitmapHeight);
    gHappyBitmapReady = true;
}
}

namespace {

// OLED SSD1306 là màn hình đơn sắc, nên không phát GIF trực tiếp.
// Ở đây ta "gắn" toàn bộ profile emoji trong thư mục /emoji vào luồng cảm xúc
// bằng cách map emotion chuẩn -> tên emoji GIF -> icon cảm xúc gần nhất của OLED.

// Emotion chuẩn của app -> tên GIF trong thư mục emoji/emoji_doi_mat_1 (2)
const std::unordered_map<std::string, const char*> kEmotionToFolderEmojiName = {
    {"neutral", "1macdinh"},
    {"happy", "vuive"},
    {"excited", "vuimung"},
    {"laughing", "cuoito"},
    {"funny", "nhaymat"},
    {"sad", "buon"},
    {"crying", "buon2"},
    {"sleepy", "buonngu"},
    {"silly", "duamat"},
    {"angry", "tucgian"},
    {"surprised", "ngacnhien2"},
    {"shocked", "hoamat"},
    {"thinking", "suynghi2"},
    {"winking", "nhaymat"},
    {"loving", "nheomat"},
    {"embarrassed", "domohoi"},
    {"confident", "vuimung"},
    {"relaxed", "binhthuong"},
    {"confused", "chamhoi"},
    {"delicious", "vuive"},
    {"kissy", "nheomat"},
    {"cool", "camlang"},
};

// Tên GIF trong thư mục emoji -> emotion icon gần nhất mà OLED hỗ trợ
const std::unordered_map<std::string, const char*> kFolderEmojiNameToOledEmotion = {
    {"1macdinh", "neutral"},
    {"binhthuong", "relaxed"},
    {"buon", "sad"},
    {"buon2", "crying"},
    {"buonngu", "sleepy"},
    {"camlang", "cool"},
    {"chamhoi", "confused"},
    {"cuoito", "laughing"},
    {"domohoi", "embarrassed"},
    {"doxet", "angry"},
    {"duamat", "silly"},
    {"hoamat", "shocked"},
    {"khongchiudau", "angry"},
    {"ngacnhien2", "surprised"},
    {"nhaymat", "winking"},
    {"nheomat", "loving"},
    {"suynghi2", "thinking"},
    {"tucgian", "angry"},
    {"tucgian2", "angry"},
    {"vuimung", "excited"},
    {"vuive", "happy"},
};

const char* ResolveOledEmotionFromFolderProfile(const char* emotion) {
    auto folder_it = kEmotionToFolderEmojiName.find(emotion);
    if (folder_it == kEmotionToFolderEmojiName.end()) {
        return nullptr;
    }

    auto oled_it = kFolderEmojiNameToOledEmotion.find(folder_it->second);
    if (oled_it == kFolderEmojiNameToOledEmotion.end()) {
        return nullptr;
    }

    return oled_it->second;
}

} // namespace

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
    auto screen = lv_screen_active();

    EnsureHappyBitmapReady();

    lv_obj_set_style_bg_color(screen, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    face_root_ = lv_obj_create(screen);
    lv_obj_set_size(face_root_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_radius(face_root_, 0, 0);
    lv_obj_set_style_pad_all(face_root_, 0, 0);
    lv_obj_set_style_border_width(face_root_, 0, 0);
    lv_obj_set_style_bg_color(face_root_, lv_color_white(), 0);
    lv_obj_set_scrollbar_mode(face_root_, LV_SCROLLBAR_MODE_OFF);

    // 1. Khung mắt trái (Đen)
    left_eye_ = lv_obj_create(face_root_);
    lv_obj_set_style_bg_color(left_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(left_eye_, 0, 0);
    lv_obj_set_style_radius(left_eye_, 3, 0);
    lv_obj_set_style_pad_all(left_eye_, 0, 0); // ĐÃ THÊM: Xóa padding để tròng mắt không bị đẩy

    // Tròng mắt trái (Trắng - để nổi bật trên nền mắt đen)
    left_pupil_ = lv_obj_create(left_eye_);
    lv_obj_set_style_bg_color(left_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(left_pupil_, 0, 0);
    lv_obj_set_style_radius(left_pupil_, 0, 0);
    lv_obj_set_style_pad_all(left_pupil_, 0, 0);

    // 2. Khung mắt phải (Đen)
    right_eye_ = lv_obj_create(face_root_);
    lv_obj_set_style_bg_color(right_eye_, lv_color_black(), 0);
    lv_obj_set_style_border_width(right_eye_, 0, 0);
    lv_obj_set_style_radius(right_eye_, 3, 0);
    lv_obj_set_style_pad_all(right_eye_, 0, 0); // ĐÃ THÊM: Xóa padding

    // Tròng mắt phải (Trắng)
    right_pupil_ = lv_obj_create(right_eye_);
    lv_obj_set_style_bg_color(right_pupil_, lv_color_white(), 0);
    lv_obj_set_style_border_width(right_pupil_, 0, 0);
    lv_obj_set_style_radius(right_pupil_, 0, 0);
    lv_obj_set_style_pad_all(right_pupil_, 0, 0);

    mouth_label_ = lv_label_create(face_root_);
    lv_obj_set_width(mouth_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(mouth_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(mouth_label_, lv_color_black(), 0);
    lv_obj_align(mouth_label_, LV_ALIGN_TOP_MID, 0, (height_ == 32) ? 43 : 21);

    happy_bitmap_ = lv_image_create(face_root_);
    lv_obj_align(happy_bitmap_, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_flag(happy_bitmap_, LV_OBJ_FLAG_HIDDEN);

    blink_closed_ = false;
    frame_count_ = 0;
    blink_phase_ = 0;
    next_blink_frame_ = 24;
    speaking_ = false;
    speaking_frame_ = 0;
    current_face_emotion_ = "neutral";
    RenderFace();

    if (face_timer_ != nullptr) {
        lv_timer_delete(face_timer_);
        face_timer_ = nullptr;
    }
    face_timer_ = lv_timer_create(FaceTimerCallback, 80, this);
}

void OledCustomEmojiDisplay::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
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
        // Giảm kích thước icon để tránh cảm giác "phóng to" ở layout OLED mặc định.
        lv_obj_set_style_text_font(emotion_label_, &font_awesome_20_4, 0);
        lv_obj_set_style_pad_top(emotion_label_, 0, 0);
    }

    // Ghi đè icon mặc định của nhà sản xuất ngay sau khi tạo UI.
    SetEmotion("neutral");
}

void OledCustomEmojiDisplay::SetStatus(const char* status) {
    OledDisplay::SetStatus(status);

    if (!IsFaceMode() || face_root_ == nullptr || status == nullptr) {
        return;
    }

    bool speaking = std::strcmp(status, Lang::Strings::SPEAKING) == 0;
    if (speaking_ == speaking) {
        return;
    }

    speaking_ = speaking;
    speaking_frame_ = 0;
    ESP_LOGI(TAG, "lip-sync %s", speaking_ ? "ON" : "OFF");
    DisplayLockGuard lock(this);
    RenderFace();
}

void OledCustomEmojiDisplay::FaceTimerCallback(lv_timer_t* timer) {
    auto* self = static_cast<OledCustomEmojiDisplay*>(lv_timer_get_user_data(timer));
    if (self == nullptr) {
        return;
    }
    self->AnimateFaceTick();
}

void OledCustomEmojiDisplay::AnimateFaceTick() {
    if (!IsFaceMode() || left_eye_ == nullptr || right_eye_ == nullptr || mouth_label_ == nullptr) {
        return;
    }

    bool need_render = false;
    ++frame_count_;

    if (std::strcmp(current_face_emotion_, "sleepy") == 0) {
        blink_closed_ = true;
        if (speaking_) {
            speaking_ = false;
            speaking_frame_ = 0;
        }
        need_render = true;
    } else {
        if (blink_phase_ == 0 && frame_count_ >= next_blink_frame_) {
            blink_phase_ = 1;
            blink_closed_ = true;
            need_render = true;
        } else if (blink_phase_ == 1) {
            blink_phase_ = 2;
        } else if (blink_phase_ == 2) {
            blink_closed_ = false;
            blink_phase_ = 0;
            next_blink_frame_ = frame_count_ + 22; // ~1.76s tại chu kỳ 80ms
            need_render = true;
        }
    }

    if (speaking_) {
        speaking_frame_ = (speaking_frame_ + 1) % 4;
        need_render = true;
    }

    if (std::strcmp(current_face_emotion_, "happy") == 0 && height_ == 64) {
        happy_frame_ = (happy_frame_ + 1) % 2;
        need_render = true;
    }

    if (need_render) {
        RenderFace();
    }
}

void OledCustomEmojiDisplay::RenderFace() {
    if (!IsFaceMode() || left_eye_ == nullptr || right_eye_ == nullptr || mouth_label_ == nullptr) {
        return;
    }

    const bool compact = (height_ <= 32);

    if (!compact && std::strcmp(current_face_emotion_, "happy") == 0 && happy_bitmap_ != nullptr) {
        lv_obj_add_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_label_, LV_OBJ_FLAG_HIDDEN);
        if (left_pupil_ != nullptr) {
            lv_obj_add_flag(left_pupil_, LV_OBJ_FLAG_HIDDEN);
        }
        if (right_pupil_ != nullptr) {
            lv_obj_add_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);
        }

        lv_image_set_src(happy_bitmap_, (happy_frame_ == 0) ? &kHappyBitmap1 : &kHappyBitmap2);
        lv_obj_clear_flag(happy_bitmap_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (happy_bitmap_ != nullptr) {
        lv_obj_add_flag(happy_bitmap_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(left_eye_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(right_eye_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mouth_label_, LV_OBJ_FLAG_HIDDEN);

    const robot_face::FaceStyle style = robot_face::GetStyle(current_face_emotion_, compact, blink_closed_);

    // vẽ mắt trái
    lv_obj_set_size(left_eye_, style.left_eye_w, style.left_eye_h);
    lv_obj_set_pos(left_eye_, style.left_eye_x, style.left_eye_y);

    // vẽ mắt phải
    lv_obj_set_size(right_eye_, style.right_eye_w, style.right_eye_h);
    lv_obj_set_pos(right_eye_, style.right_eye_x, style.right_eye_y);

    // Vẽ tròng mắt
    if (style.show_pupil && !blink_closed_) {
        lv_obj_clear_flag(left_pupil_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);

        lv_obj_set_size(left_pupil_, style.pupil_w, style.pupil_h);
        lv_obj_set_pos(left_pupil_, style.pupil_left_x, style.pupil_left_y);

        lv_obj_set_size(right_pupil_, style.pupil_w, style.pupil_h);
        lv_obj_set_pos(right_pupil_, style.pupil_right_x, style.pupil_right_y);
    } else {
        // Ẩn tròng mắt khi chớp mắt hoặc khi style không bật
        lv_obj_add_flag(left_pupil_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_pupil_, LV_OBJ_FLAG_HIDDEN);
    }

    const char* mouth = style.mouth;
    if (speaking_ && std::strcmp(current_face_emotion_, "sleepy") != 0) {
        static const char* kTalkingMouthFrames[] = {
            "  -  ",
            "  o  ",
            "  O  ",
            "  o  ",
        };
        mouth = kTalkingMouthFrames[speaking_frame_ % 4];
    }

    lv_label_set_text(mouth_label_, mouth);
    lv_obj_align(mouth_label_, LV_ALIGN_TOP_MID, 0, compact ? 16 : 34);
}

void OledCustomEmojiDisplay::SetEmotion(const char* emotion) {
    if (emotion == nullptr || emotion[0] == '\0') {
        emotion = "neutral";
    }

    if (IsFaceMode() && face_root_ != nullptr) {
        const char* mapped = ResolveOledEmotionFromFolderProfile(emotion);
        if (mapped == nullptr) {
            mapped = emotion;
        }
        current_face_emotion_ = robot_face::NormalizeEmotion(mapped);
        ESP_LOGI(TAG, "face emotion '%s' -> '%s'", emotion, current_face_emotion_);
        DisplayLockGuard lock(this);
        RenderFace();
        return;
    }

    const char* oled_emotion = ResolveOledEmotionFromFolderProfile(emotion);
    if (oled_emotion != nullptr) {
        ESP_LOGI(TAG, "emotion '%s' -> folder profile -> '%s'", emotion, oled_emotion);
        OledDisplay::SetEmotion(oled_emotion);
        return;
    }

    // Fallback: giữ nguyên hành vi cũ nếu emotion không nằm trong profile riêng.
    ESP_LOGI(TAG, "emotion '%s' -> fallback base mapping", emotion);
    OledDisplay::SetEmotion(emotion);
}
