#ifndef WS_LOG_BROADCASTER_H
#define WS_LOG_BROADCASTER_H

#include <cstdarg>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

class WsLogBroadcaster {
public:
    using BroadcastFn = std::function<void(const char*)>;

    static WsLogBroadcaster& GetInstance();

    // Khởi tạo hook esp_log_set_vprintf + queue + task. Idempotent.
    // broadcast_fn sẽ được gọi từ task pump nội bộ, không phải từ log hook.
    bool Init(BroadcastFn broadcast_fn);
    void Shutdown();

private:
    WsLogBroadcaster() = default;
    ~WsLogBroadcaster() = default;
    WsLogBroadcaster(const WsLogBroadcaster&) = delete;
    WsLogBroadcaster& operator=(const WsLogBroadcaster&) = delete;

    static constexpr size_t kQueueSize    = 32;
    static constexpr size_t kMaxMsgLen    = 180;
    static constexpr size_t kTaskStack    = 4096;
    static constexpr UBaseType_t kTaskPrio = tskIDLE_PRIORITY + 1;

    struct LogEntry {
        char level;                // 'I', 'W', 'E', 'D', 'V'
        char msg[kMaxMsgLen];      // "TAG: message" (không có ANSI)
    };

    static int  LogVprintfHook(const char* fmt, va_list args);
    static void PumpTaskEntry(void* arg);
    void        PumpLoop();

    QueueHandle_t queue_             = nullptr;
    TaskHandle_t  task_              = nullptr;
    BroadcastFn   broadcast_fn_      = nullptr;
    void*         original_vprintf_  = nullptr;   // vprintf_like_t cũ
    bool          initialized_       = false;
};

#endif  // WS_LOG_BROADCASTER_H