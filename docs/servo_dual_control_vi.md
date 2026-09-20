# Điều khiển 2 servo Pan-Tilt cho ESP32-S3 bread-compact-wifi (OLED 0.96)

Tài liệu này áp dụng cho board `bread-compact-wifi` (ESP32-S3) trong dự án này.

## 1) Tính năng đã thêm

- Đã tách thành 2 cụm servo độc lập:
  - Cụm CHÂN: GPIO9/GPIO11
  - Cụm ĐẦU pan-tilt: GPIO1/GPIO2
- Các hàm cảm xúc mặc định đã có trong firmware:
  - `nodYes()`
  - `shakeNo()`
  - `talkingMotion()`
  - `curiousHeadTilt()`
- Có trigger theo cảm xúc từ server (`type=llm`, trường `emotion`) và có cooldown chống giật liên tục.
- Có cơ chế JSON trên thẻ SD để mở rộng/chỉnh sửa hành động cảm xúc.

## 2) MCP tools để AI gọi bằng giọng nói

### 2.1 Cụm CHÂN (giữ tương thích lệnh cũ)

- `self.robot.dual_servo.get_state`
- `self.robot.dual_servo.nod_yes`
- `self.robot.dual_servo.shake_no`
- `self.robot.dual_servo.talking_motion`
- `self.robot.dual_servo.curious_head_tilt`
- `self.robot.dual_servo.set_angles` (tham số `pan`, `tilt`)
- `self.robot.dual_servo.run_action` (chạy theo tên action)
- `self.robot.dual_servo.stop`
- `self.robot.dual_servo.reload_config`

### 2.2 Cụm ĐẦU pan-tilt

- `self.robot.head_servo.get_state`
- `self.robot.head_servo.nod_yes`
- `self.robot.head_servo.shake_no`
- `self.robot.head_servo.talking_motion`
- `self.robot.head_servo.curious_head_tilt`
- `self.robot.head_servo.set_angles`
- `self.robot.head_servo.run_action`
- `self.robot.head_servo.stop`
- `self.robot.head_servo.reload_config`

## 3) Đấu dây phần cứng

### Chân mặc định trong code

- Cụm chân: `LEG_SERVO_PAN_GPIO = GPIO9`, `LEG_SERVO_TILT_GPIO = GPIO11`
- Cụm đầu: `HEAD_SERVO_PAN_GPIO = GPIO1`, `HEAD_SERVO_TILT_GPIO = GPIO2`

Bạn có thể đổi trong [main/boards/bread-compact-wifi/config.h](../main/boards/bread-compact-wifi/config.h).

### Sơ đồ nguồn (quan trọng)

- Không nên cấp 2 servo trực tiếp từ chân 3V3 của ESP32-S3.
- Dùng nguồn ngoài 5V đủ dòng (khuyến nghị từ 2A).
- Nối dây:
  - Servo Pan tín hiệu -> GPIO1
  - Servo Tilt tín hiệu -> GPIO2
  - GND servo -> GND nguồn ngoài
  - GND nguồn ngoài -> GND ESP32-S3 (bắt buộc chung mass)
  - VCC servo -> 5V nguồn ngoài

Nếu servo rung hoặc ESP reset thì thường là do thiếu dòng hoặc chưa chung mass.

## 4) JSON mở rộng hành động cảm xúc

Tạo file JSON riêng:

- Cụm chân: `/sdcard/robot/leg_servo_actions.json`
- Cụm đầu: `/sdcard/robot/head_servo_actions.json`

Ví dụ:

```json
{
  "defaults": {
    "min_angle": 0,
    "max_angle": 180,
    "center_pan": 90,
    "center_tilt": 90,
    "default_step_ms": 260,
    "emotion_cooldown_ms": 1400
  },
  "emotions": {
    "surprised": "step_back",
    "happy": "nodYes",
    "angry": "shakeNo",
    "curious": "curiousHeadTilt",
    "neutral": "center"
  },
  "actions": {
    "nodYes": [
      { "pan": 90, "tilt": 104, "duration_ms": 170 },
      { "pan": 90, "tilt": 76, "duration_ms": 170 },
      { "pan": 90, "tilt": 100, "duration_ms": 170 },
      { "pan": 90, "tilt": 80, "duration_ms": 170 },
      { "pan": 90, "tilt": 90, "duration_ms": 160 }
    ],
    "shakeNo": [
      { "pan": 114, "tilt": 90, "duration_ms": 150 },
      { "pan": 66, "tilt": 90, "duration_ms": 150 },
      { "pan": 114, "tilt": 90, "duration_ms": 150 },
      { "pan": 66, "tilt": 90, "duration_ms": 150 },
      { "pan": 90, "tilt": 90, "duration_ms": 150 }
    ],
    "curiousHeadTilt": [
      { "pan": 100, "tilt": 100, "duration_ms": 250 },
      { "pan": 104, "tilt": 104, "duration_ms": 280 },
      { "pan": 90, "tilt": 90, "duration_ms": 220 }
    ],
    "step_back": [
      { "pan": 130, "tilt": 70, "duration_ms": 180 },
      { "pan": 140, "tilt": 64, "duration_ms": 180 },
      { "pan": 90, "tilt": 90, "duration_ms": 180 }
    ]
  }
}
```

Ghi chú:
- Hỗ trợ cả khóa `pan/tilt` và tương thích cũ `left/right`.
- Action trùng tên sẽ ghi đè action mặc định trong firmware.
- `talkingMotion` là action động ngẫu nhiên theo thời gian chạy.

## 5) Sử dụng nhanh bằng giọng nói

Ví dụ câu lệnh tự nhiên:

- “Gật đầu đồng ý đi”
- “Lắc đầu không”
- “Nói chuyện thì lắc nhẹ đầu tự nhiên”
- “Nghiêng đầu tỏ vẻ tò mò”
- “Đặt pan 120 độ, tilt 70 độ”

## 6) Reload JSON không cần nạp lại firmware

Sau khi sửa JSON trên SD, gọi:

- `self.robot.dual_servo.reload_config`

## 7) Tệp mã nguồn liên quan

- [main/boards/common/dual_servo_controller.h](../main/boards/common/dual_servo_controller.h)
- [main/boards/bread-compact-wifi/compact_wifi_board.cc](../main/boards/bread-compact-wifi/compact_wifi_board.cc)
- [main/boards/bread-compact-wifi/config.h](../main/boards/bread-compact-wifi/config.h)
- [main/application.cc](../main/application.cc)
