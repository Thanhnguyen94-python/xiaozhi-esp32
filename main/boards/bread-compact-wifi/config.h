#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/uart.h>

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_4
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_5
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_7
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_15
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_16

#else

#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif


#define BUILTIN_LED_GPIO        GPIO_NUM_48
#define BOOT_BUTTON_GPIO        GPIO_NUM_0
#define TOUCH_BUTTON_GPIO       GPIO_NUM_47
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_40
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_39

#define DISPLAY_SDA_PIN GPIO_NUM_41
#define DISPLAY_SCL_PIN GPIO_NUM_42
#define DISPLAY_WIDTH   128

#if CONFIG_OLED_SSD1306_128X32
#define DISPLAY_HEIGHT  32
#elif CONFIG_OLED_SSD1306_128X64
#define DISPLAY_HEIGHT  64
#elif CONFIG_OLED_SH1106_128X64
#define DISPLAY_HEIGHT  64
#define SH1106
#else
#error "OLED display type is not selected"
#endif

#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true

// SD card over SPI (4-wire)
#define SDCARD_SPI_MISO GPIO_NUM_12
#define SDCARD_SPI_MOSI GPIO_NUM_13
#define SDCARD_SPI_SCLK GPIO_NUM_14
// CS là bắt buộc cho SDSPI. Nếu dây CS của bạn đang nối chân khác, sửa lại macro này.
// Mặc định dùng GPIO10 (thường dễ đi dây trên ESP32-S3 mini board).
#define SDCARD_SPI_CS   GPIO_NUM_10
#define SDCARD_SPI_MAX_FREQ_KHZ 4000
#define SDCARD_MOUNT_POINT "/sdcard"


// A MCP Test: Control a lamp
#define LAMP_GPIO GPIO_NUM_18

// Wheel motors (2 DC motors via H-bridge)
// Mapping for common drivers (L298N/TB6612-like):
// Left motor: IN1/IN2, Right motor: IN3/IN4
#define WHEEL_MOTOR_LEFT_IN1_GPIO   GPIO_NUM_9
#define WHEEL_MOTOR_LEFT_IN2_GPIO   GPIO_NUM_8
#define WHEEL_MOTOR_RIGHT_IN1_GPIO  GPIO_NUM_11
#define WHEEL_MOTOR_RIGHT_IN2_GPIO  GPIO_NUM_17

// RC receiver input (MC7RE M.BUS / SBUS)
#define RC_SBUS_UART_PORT           UART_NUM_1
#define RC_SBUS_RX_PIN              GPIO_NUM_19
#define RC_SBUS_TX_PIN              GPIO_NUM_NC
#define RC_SBUS_BAUDRATE            100000
#define RC_SBUS_DEADZONE_PERCENT    10
#define RC_SBUS_FRAME_TIMEOUT_MS    300

// Head servos (khớp cổ 2 trục pan-tilt)
#define HEAD_SERVO_PAN_GPIO  GPIO_NUM_1
#define HEAD_SERVO_TILT_GPIO GPIO_NUM_2

#endif // _BOARD_CONFIG_H_

// sơ đồ đấu nối phần cứng cho board "Bread Compact WiFi" (ESP32-S3 mini) với các module ngoại vi:
/*
1) Nguồn và mass (quan trọng nhất)
Tất cả module phải nối chung GND với ESP32.
GPIO ESP32 là 3.3V logic → không đưa 5V trực tiếp vào chân GPIO.
Motor + servo nên cấp nguồn riêng (ví dụ 5V/6V), không lấy trực tiếp từ 3V3 ESP.
Khi dùng nguồn ngoài cho motor/servo:
GND nguồn ngoài ↔ GND ESP32 (bắt buộc).
2) I2S Audio (đang ở chế độ AUDIO_I2S_METHOD_SIMPLEX)
Mic I2S
MIC WS/LRCLK → GPIO4
MIC SCK/BCLK → GPIO5
MIC SD/DOUT → GPIO6
MIC VCC → 3V3
MIC GND → GND
Amp/Speaker I2S (vd MAX98357A)
AMP DIN → GPIO7
AMP BCLK → GPIO15
AMP LRC → GPIO16
AMP VIN → 5V (hoặc theo module)
AMP GND → GND
Speaker đấu vào ngõ ra SPK của amp.
3) OLED I2C
SDA → GPIO41
SCL → GPIO42
VCC → 3V3 (hoặc theo module)
GND → GND
Nếu OLED không lên, kiểm tra địa chỉ I2C (0x3C/0x3D) và loại màn hình trong menuconfig.

4) SD Card SPI (SDSPI 4-wire)
MISO → GPIO12
MOSI → GPIO13
SCLK → GPIO14
CS → GPIO10
VCC → 3V3 (ưu tiên module hỗ trợ 3.3V)
GND → GND
5) Nút bấm / LED
BOOT_BUTTON → GPIO0 (thường là nút BOOT onboard)
TOUCH_BUTTON → GPIO47
VOL+ → GPIO40
VOL- → GPIO39
BUILTIN_LED → GPIO48
Cách đấu nút rời phổ biến:

Một chân nút → GPIO
Chân còn lại → GND
Dùng pull-up nội (nếu code đã bật), nhấn = mức 0.
6) Đèn lamp
LAMP_GPIO → GPIO18
Nếu là LED nhỏ:

GPIO18 → điện trở 220–1kΩ → LED → GND.
Nếu là tải lớn (đèn/relay):

Dùng transistor/MOSFET/relay driver, không kéo trực tiếp từ GPIO.
7) Motor bánh xe (H-bridge)
Theo config:

Left IN1 → GPIO9
Left IN2 → GPIO8
Right IN1 → GPIO11
Right IN2 → GPIO17
Đấu với driver L298N/TB6612:

ESP GPIO ↔ chân IN tương ứng driver
Driver VM ↔ nguồn motor
Driver VCC ↔ 3V3/5V logic (theo driver)
Driver GND ↔ GND chung
Motor trái/phải ↔ OUTA/OUTB của driver
Nếu driver có ENA/ENB:

Kéo lên mức HIGH (hoặc nối PWM nếu muốn điều tốc).
8) Servo đầu pan-tilt
PAN → GPIO1
TILT → GPIO2
Servo V+ → 5V/6V nguồn ngoài
Servo GND → GND chung với ESP
Lưu ý:

Servo hút dòng cao lúc khởi động/quay tải → nguồn phải đủ dòng.
Không cấp servo từ chân 3V3 của ESP.*/