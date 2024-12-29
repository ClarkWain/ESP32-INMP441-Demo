# ESP32 + INMP441 音频采集演示

## 项目简介
Hi，我最近在学习ESP32，写了一个麦克风的Demo。
这个Demo演示了使用 ESP32 和 INMP441 麦克风模块进行音频数据采集，并通过 Wi-Fi 将数据发送到服务器进行处理。

**工作流程：**
1. ESP32 通过 I2S 接口从 INMP441 麦克风模块采集音频数据。
2. 采集到的音频数据通过 TCP 协议发送到配置的服务器。
3. 服务器接收音频数据并保存为 `.wav` 文件。

## 硬件需求
- ESP32 开发板
- INMP441 数字麦克风模块
- 连接线

## 连线方式

ESP32 与 INMP441 的连接如下：

| INMP441 引脚 | ESP32 引脚  | 作用说明                                      |
|-------------|-------------|---------------------------------------------|
| **SCK (时钟)**  | GPIO 38     | 提供时钟信号，用于同步数据传输。                       |
| **WS (帧选择)** | GPIO 39     | 用于选择左声道或右声道的数据帧。                       |
| **SD (数据)**    | GPIO 37     | 传输数字音频数据。                                 |
| **L/R (声道选择)** | GND     | 选择录制的声道（左声道或右声道）。                      |
| **VCC**         | 3.3V        | 为 INMP441 模块供电。                              |
| **GND**         | GND         | 接地，完成电路连接。                                |

**详细说明：**

- **SCK (时钟) -> GPIO 38**: 提供I2S通信的时钟信号，确保数据按正确的时序传输。
- **WS (帧选择) -> GPIO 39**: 用于帧同步，标识当前数据帧属于左声道还是右声道。
- **SD (数据) -> GPIO 37**: 数据线，用于传输从麦克风采集到的数字音频信号。
- **L/R (声道选择) -> GPIO 40**: 控制录制的声道，可以选择左声道或右声道进行音频数据采集。
- **VCC -> 3.3V**: 为 INMP441 模块提供工作电压。
- **GND -> GND**: 接地，确保电路的稳定性和可靠性。

## 软件需求
- [ESP-IDF](https://github.com/espressif/esp-idf) 开发框架
- Python 3.x

## 配置步骤

### 1. 克隆仓库
```bash
git clone [<仓库地址>](https://github.com/ClarkWain/ESP32-INMP441-Demo)
cd ESP32-INMP441-Demo
```

### 2. 设置 ESP-IDF 环境
按照 [ESP-IDF 官方文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) 安装并配置 ESP-IDF 环境。

### 3. 配置 Wi-Fi 连接
编辑 `main/main.c` 文件，修改以下宏定义以匹配您的 Wi-Fi 网络:
```c
#define WIFI_SSID      "Your_SSID"      // WiFi SSID
#define WIFI_PASSWORD  "Your_Password"  // WiFi 密码
```
### 4. 连接服务端配置
编辑 `main/main.c` 文件，修改以下宏定义以匹配您的服务器配置:
```c
#define SERVER_IP   "192.168.1.6"   // 服务器 IP
#define SERVER_PORT 12345           // 服务器端口
```


### 5. I2S 配置
编辑 `main/main.c` 文件，修改以下宏定义以匹配您的 I2S 配置:
```c
#define I2S_NUM         (I2S_NUM_0) // 使用 I2S0
#define I2S_SCK_PIN     38          // INMP441 的 SCK 引脚
#define I2S_WS_PIN      39          // INMP441 的 WS 引脚
#define I2S_SD_PIN      37          // INMP441 的 SD 引脚
#define I2S_SAMPLE_RATE 16000       // 采样率（常用 16kHz 或 44.1kHz）
#define I2S_BUF_SIZE    1024        // DMA 缓冲区大小
```

### 6. 构建并烧录固件
```bash
idf.py build
idf.py flash
```

## 运行服务器
在开发主机上运行 Python 服务器脚本以接收音频数据:
```bash
python audio_server.py
```

服务器将监听 `12345` 端口，并将接收到的音频数据保存为 `.wav` 文件。

## 使用说明
1. 烧录固件并启动 ESP32。
2. 确保开发主机上的服务器脚本正在运行。
3. ESP32 连接到指定的 Wi-Fi 网络，并开始采集音频数据。
4. 采集到的音频数据将通过 TCP 发送到服务器，并保存为音频文件。

## 项目结构
- `main/main.c`: 主程序文件，包含 Wi-Fi 初始化、I2S 配置和数据传输逻辑。
- `CMakeLists.txt`: CMake 配置文件，用于构建 ESP-IDF 项目。
- `audio_server.py`: Python 脚本，用于接收和保存音频数据。

## 许可证
随便用吧，别忘了给我点个赞哦~
