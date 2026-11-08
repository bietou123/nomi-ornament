# NOMI 桌面表情摆件

ESP32 + 1.44" ST7735 彩屏的表情脸摆件。屏幕上的眼睛、道具和整张脸会缓慢旋转摇摆，两个舵机让"头"跟着视线转动。纯黑底、暖白空心线条眼，21 种情绪随机切换。

## 硬件

| 元件 | 说明 |
|------|------|
| ESP32-WROOM-32U | 主控（板级 esp32dev） |
| ST7735 1.44" 128×128 | SPI 彩屏 |
| 舵机 ×2 | 水平（yaw）+ 俯仰（pitch）云台 |
| 杜邦线 / 电源 | 舵机建议独立 5 V 供电并与主控共地 |

## 接线

| 信号 | GPIO |
|------|------|
| SCK (SCL) | 18 |
| MOSI (SDA) | 23 |
| RST | 4 |
| DC | 2 |
| CS | 5 |
| BLK | 3V3（背光常亮） |
| Yaw 舵机 | 13 |
| Pitch 舵机 | 25 |

## 表情与动作

- 21 种情绪：呆滞、开心、难过、惊讶、眨眼、爱心、哭、死亡、蚊香眼、傻笑、抽烟、睡眠、困倦、蝴蝶、苹果、转盘、收音机、咖啡、气泡、音乐等
- 整画面绕屏幕中心缓慢旋转 + 摇摆，切换表情时由小弹大的缩放过渡
- 随机眨眼（含慢眨），视线会缓慢漂移
- 舵机让头部跟着视线方向转，音乐/睡觉/死亡等状态有专属姿态
- 开机动画：烟盒升起、掀盖、抽出一根烟并点亮烟头
- 无联网；时间显示用编译时刻起算的本地 millis 计时

## 编译与烧录

```bash
pip install platformio
pio run                 # 编译
pio run -t upload       # 烧录（先在 platformio.ini 配置串口）
pio device monitor      # 串口日志 115200
```

依赖库：Adafruit GFX、Adafruit ST7735 and ST7789、ESP32Servo，已在 `platformio.ini` 中声明。

## 品牌图片说明

原项目里的 `LIQUN` 表情需要一张 128×128 RGB565 的品牌包装图，属于第三方商标素材，**不在本仓库提供**。仓库代码用 `__has_include("liqun_img.h")` 判断：没有该文件时该表情显示占位画面，其余功能不受影响。如需自行替换，在 `src/` 下放置自己的 `liqun_img.h`（数组名 `LIQUN_IMG`）即可。

## 附录：摇杆控制实验

`extras/` 下是同一摆件配套的摇杆实验代码，未接入主固件，可单独编译：

- `extras/joystick_tx/joystick_tx.ino` — Arduino Pro Mini 读取摇杆（A0/A1/D2），9600 串口输出 `x,y,button`
- `extras/joystick_tx/main.c` — STM32F103C6T6 裸机版本（寄存器级 ADC + USART，手写链接脚本 `stm32f103c6.ld`）
- `extras/joystick_relay/joystick_relay.ino` — ESP-01S 中继：串口收摇杆数据，UDP 转发给 ESP32；Wi-Fi 凭据经串口 `WIFI:ssid:pass` 写入 EEPROM
- `tools/flash_stm32.py` — 不依赖工具链，手工编码 Thumb-2 指令生成固件并通过 STM32 内置 bootloader 烧写：`python tools/flash_stm32.py COM5`

摇杆中继默认不写死任何 Wi-Fi 凭据，首次使用需先配置。

## 已知限制

- 舵机与屏幕共用电源时，舵机启动瞬间可能拉低电压导致屏幕闪烁或 ESP32 复位，建议分开供电
- 时间显示只是从编译时刻起算，长时间运行会漂移
- ST7735 常见 128×160 面板带 144 行偏移，代码用 BLACKTAB 清屏后再切换到 144GREENTAB，若换其他批次屏幕可能需要调整初始化参数

## 许可

原创源码使用 MIT 许可证，见 [LICENSE](LICENSE)。
