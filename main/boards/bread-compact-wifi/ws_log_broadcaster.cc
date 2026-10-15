#include "ws_log_broadcaster.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

// Tag được đẩy lên web. Muốn thêm → append vào đây.
const char* kWhitelist[] = {
    "RobotControlHub",
    "RobotWebUI",
    "RcSbus",
    "DualDcMotor",
    "DualServo",
    "Application",
    "McpServer",
    "CompactWifiBoard",
    "RobotLife",
};

bool IsTagWhitelisted(const char* tag, size_t len) {
    for (const char* w : kWhitelist) {
        if (strlen(w) == len && strncmp(tag, w, len) == 0) return true;
    }
    return false;
}

// Xoá mọi ANSI escape (\x1b[...m) tại chỗ.
void StripAnsiInPlace(char* s) {
    if (s == nullptr) return;
    char* dst = s;
    char* src = s;
    while (*src) {
        if (*src == '\033' && src[1] == '[') {
            src += 2;
            while (*src && *src != 'm') src++;
            if (*src == 'm') src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

}  // namespace

WsLogBroadcaster& WsLogBroadcaster::GetInstance() {
    static WsLogBroadcaster inst;
    return inst;
}

bool WsLogBroadcaster::Init(BroadcastFn broadcast_fn) {
    if (initialized_) return true;
    if (!broadcast_fn) return false;

    queue_ = xQueueCreate(kQueueSize, sizeof(LogEntry));
    if (queue_ == nullptr) {
        printf("[WsLogBroadcaster] cannot create queue\n");
        return false;
    }

    broadcast_fn_ = std::move(broadcast_fn);

    BaseType_t ok = xTaskCreatePinnedToCore(
        &WsLogBroadcaster::PumpTaskEntry,
        "ws_log_pump",
        kTaskStack,
        this,
        kTaskPrio,
        &task_,
        0);
    if (ok != pdPASS) {
        vQueueDelete(queue_);
        queue_ = nullptr;
        printf("[WsLogBroadcaster] cannot create task\n");
        return false;
    }

    // Cài hook và giữ con trỏ hàm cũ để vẫn ghi ra UART
    original_vprintf_ = reinterpret_cast<void*>(
        esp_log_set_vprintf(&WsLogBroadcaster::LogVprintfHook));
    if (original_vprintf_ == nullptr) {
        original_vprintf_ = reinterpret_cast<void*>(&vprintf);
    }

    initialized_ = true;
    // KHÔNG dùng ESP_LOGx ở đây (hook đã bật) — dùng printf thô
    printf("[WsLogBroadcaster] init done, queue=%u entries, maxlen=%u\n",
           (unsigned)kQueueSize, (unsigned)kMaxMsgLen);
    return true;
}

void WsLogBroadcaster::Shutdown() {
    if (!initialized_) return;

    if (original_vprintf_ != nullptr) {
        esp_log_set_vprintf(reinterpret_cast<vprintf_like_t>(original_vprintf_));
        original_vprintf_ = nullptr;
    }
    if (task_ != nullptr) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (queue_ != nullptr) {
        vQueueDelete(queue_);
        queue_ = nullptr;
    }
    initialized_ = false;
}

int WsLogBroadcaster::LogVprintfHook(const char* fmt, va_list args) {
    auto& self = GetInstance();

    va_list args_orig;
    va_copy(args_orig, args);
    int ret = 0;
    if (self.original_vprintf_ != nullptr) {
        ret = reinterpret_cast<vprintf_like_t>(self.original_vprintf_)(fmt, args_orig);
    } else {
        ret = vprintf(fmt, args_orig);
    }
    va_end(args_orig);

    // ★★★ QUAN TRỌNG: KHÔNG đẩy vào queue khi ở ISR context ★★★
    // xQueueSend không an toàn trong ISR → sẽ corrupt FreeRTOS state.
    // Bỏ qua log từ ISR — chấp nhận mất một số debug log.
    if (xPortInIsrContext()) {
        return ret;
    }

    if (!self.initialized_ || self.queue_ == nullptr) return ret;

    // 2) Format ra buffer để parse
    char buf[256];
    va_list args_buf;
    va_copy(args_buf, args);
    int n = vsnprintf(buf, sizeof(buf), fmt, args_buf);
    va_end(args_buf);
    if (n <= 0) return ret;

    StripAnsiInPlace(buf);

    // 3) Parse ESP-IDF log: "L (timestamp) TAG: message\n"
    const char* p = buf;
    while (*p == ' ' || *p == '\t') p++;

    char level = '?';
    if (*p == 'I' || *p == 'W' || *p == 'E' || *p == 'D' || *p == 'V') {
        level = *p++;
    } else {
        return ret;
    }

    while (*p == ' ') p++;
    if (*p != '(') return ret;
    p++;
    while (*p && *p != ')') p++;
    if (*p != ')') return ret;
    p++;
    while (*p == ' ') p++;

    const char* tag_start = p;
    while (*p && *p != ':' && *p != '\n') p++;
    if (*p != ':') return ret;
    size_t tag_len = (size_t)(p - tag_start);
    p++;
    while (*p == ' ') p++;

    const char* msg_start = p;

    if (!IsTagWhitelisted(tag_start, tag_len)) return ret;

    // 4) Copy vào LogEntry (không block)
    LogEntry entry;
    entry.level = level;

    size_t tag_copy = tag_len < (kMaxMsgLen - 3) ? tag_len : (kMaxMsgLen - 3);
    memcpy(entry.msg, tag_start, tag_copy);
    entry.msg[tag_copy]     = ':';
    entry.msg[tag_copy + 1] = ' ';

    size_t remaining = kMaxMsgLen - 1 - (tag_copy + 2);
    size_t msg_len   = strlen(msg_start);
    while (msg_len > 0 &&
           (msg_start[msg_len - 1] == '\n' || msg_start[msg_len - 1] == '\r')) {
        msg_len--;
    }
    size_t msg_copy = msg_len < remaining ? msg_len : remaining;
    memcpy(entry.msg + tag_copy + 2, msg_start, msg_copy);
    entry.msg[tag_copy + 2 + msg_copy] = '\0';

    if (uxQueueSpacesAvailable(self.queue_) == 0) {
        return ret;
    }

    xQueueSend(self.queue_, &entry, 0);
    return ret;
}

void WsLogBroadcaster::PumpTaskEntry(void* arg) {
    static_cast<WsLogBroadcaster*>(arg)->PumpLoop();
}

void WsLogBroadcaster::PumpLoop() {
    LogEntry entry;
    char escaped[kMaxMsgLen * 2];
    char json[kMaxMsgLen * 2 + 64];

    while (true) {
        if (xQueueReceive(queue_, &entry, portMAX_DELAY) != pdTRUE) continue;
        if (broadcast_fn_ == nullptr) continue;

        // Escape JSON inline
        size_t j = 0;
        for (size_t i = 0; entry.msg[i] && j < sizeof(escaped) - 2; ++i) {
            char c = entry.msg[i];
            if (c == '"' || c == '\\') {
                escaped[j++] = '\\';
                escaped[j++] = c;
            } else if (c == '\n') {
                escaped[j++] = '\\';
                escaped[j++] = 'n';
            } else if ((unsigned char)c < 0x20) {
                // bỏ control chars
            } else {
                escaped[j++] = c;
            }
        }
        escaped[j] = '\0';

        snprintf(json, sizeof(json),
                 "{\"type\":\"log\",\"level\":\"%c\",\"msg\":\"%s\"}",
                 entry.level, escaped);

        broadcast_fn_(json);
        vTaskDelay(pdMS_TO_TICKS(20));   // chống flood
    }
}