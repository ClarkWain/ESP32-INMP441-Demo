#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"
#include "driver/i2s.h"
#include "lwip/sockets.h"
#include "freertos/queue.h"
#include <stdint.h>

// WiFi配置
#define WIFI_SSID      "GoodDayTmp"  // WiFi SSID
#define WIFI_PASSWORD  "11224455"    // WiFi 密码

// I2S 引脚定义
#define I2S_NUM         (I2S_NUM_0) // 使用 I2S0
#define I2S_SCK_PIN     38          // INMP441 的 SCK 引脚
#define I2S_WS_PIN      39          // INMP441 的 WS 引脚
#define I2S_SD_PIN      37          // INMP441 的 SD 引脚
#define I2S_SAMPLE_RATE 16000       // 采样率（常用 16kHz 或 44.1kHz）
#define I2S_BUF_SIZE    1024        // DMA 缓冲区大小

// 服务器配置
#define SERVER_IP   "192.168.1.6"   // 服务器 IP
#define SERVER_PORT 12345           // 服务器端口

// WiFi事件组
static EventGroupHandle_t s_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
static const char *TAG = "wifi_station";

// 定义音频数据队列
#define QUEUE_LENGTH 10
#define QUEUE_ITEM_SIZE I2S_BUF_SIZE
static QueueHandle_t audio_queue;

// 定义全局缓冲区
static int16_t i2s_read_buffer_global[I2S_BUF_SIZE];
static int16_t discard_buffer_global[QUEUE_ITEM_SIZE];

// I2S 配置
void i2s_init() {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,         // 主机模式，接收数据
        .sample_rate = I2S_SAMPLE_RATE,                // 采样率
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,  // 每样本 16 位
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // 使用左声道
        .communication_format = I2S_COMM_FORMAT_I2S,   // 标准 I2S 格式
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,      // 中断优先级
        .dma_buf_count = 4,                            // DMA 缓冲区数量
        .dma_buf_len = I2S_BUF_SIZE                    // 每个缓冲区的大小
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_PIN,          // SCK 引脚
        .ws_io_num = I2S_WS_PIN,            // WS 引脚
        .data_out_num = I2S_PIN_NO_CHANGE,  // 不使用输出
        .data_in_num = I2S_SD_PIN           // SD 引脚
    };

    // 安装并启动 I2S 驱动
    esp_err_t ret = i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        printf("Failed to install I2S driver!\n");
        return;
    }

    // 设置 I2S 引脚
    i2s_set_pin(I2S_NUM, &pin_config);

    // 检查实际采样率
    float actual_rate = i2s_get_clk(I2S_NUM);
    if (actual_rate != I2S_SAMPLE_RATE) {
        printf("Warning: actual I2S rate (%.2f) differs from expected (%d)\n",
               actual_rate, I2S_SAMPLE_RATE);
    }

    // 启动 I2S DMA 缓冲区，DMA即Direct Memory Access，直接内存访问，用于高速数据传输
    i2s_zero_dma_buffer(I2S_NUM);
}

// 读取 I2S 数据并发送到队列
void i2s_read_task(void *param) {
    size_t bytes_read;

    while (1) {
        // 从 I2S 读取数据
        esp_err_t ret = i2s_read(I2S_NUM, (void *)i2s_read_buffer_global,
                                 sizeof(i2s_read_buffer_global),
                                 &bytes_read,
                                 portMAX_DELAY);
        if (ret != ESP_OK || bytes_read == 0) {
            printf("i2s_read error or zero bytes (%d). Retrying...\n", bytes_read);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        printf("Read bytes from I2S: %d\n", bytes_read);

        // 发送数据到队列
        if (xQueueSend(audio_queue, i2s_read_buffer_global, 0) != pdPASS) {  // 修改为 i2s_read_buffer_global
            // 如果发送失败，移除最旧的数据
            if (xQueueReceive(audio_queue, discard_buffer_global, 0) == pdPASS) {
                printf("Queue full, discarded oldest data.\n");
            }
            // 尝试再次发送
            if (xQueueSend(audio_queue, i2s_read_buffer_global, 0) != pdPASS) {  // 修改为 i2s_read_buffer_global
                printf("Queue full, discard new data.\n");
            }
        }
    }
}

// 简单校验和计算（按字节）
uint32_t calculate_checksum(int16_t *data, size_t length) {
    uint32_t checksum = 0;
    uint8_t *byte_data = (uint8_t*)data;
    for (size_t i = 0; i < length * 2; i++) {  // 每个int16_t包含2个字节
        checksum += byte_data[i];
    }
    return checksum;
}

// 发送所有数据，确保全部发送
int send_all(int sock, uint8_t *buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        int sent = send(sock, buffer + total_sent, length - total_sent, 0);
        if (sent < 0) {
            return -1;  // 发送错误
        }
        total_sent += sent;
    }
    return total_sent;
}

// 发送任务
void socket_send_task(void *param) {
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(SERVER_PORT);

    int sock = -1;

    while (1) {
        // 如果套接字未创建或已关闭，创建新的套接字
        if (sock < 0) {
            sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                printf("Socket creation error, retry...\n");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
                printf("Socket connect failed, retry...\n");
                close(sock);
                sock = -1;
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            printf("TCP connected to server.\n");
        }

        // 从队列中接收数据
        int16_t buffer[QUEUE_ITEM_SIZE];
        if (xQueueReceive(audio_queue, buffer, portMAX_DELAY) == pdPASS) {
            // 计算校验和（按字节）
            uint32_t checksum = calculate_checksum(buffer, QUEUE_ITEM_SIZE);

            // 创建发送缓冲区：长度（4字节） + 数据 + 校验和（4字节）
            uint32_t length = QUEUE_ITEM_SIZE * sizeof(int16_t);
            size_t packet_size = sizeof(length) + length + sizeof(checksum);
            uint8_t *packet = malloc(packet_size);
            if (packet == NULL) {
                printf("Memory allocation failed.\n");
                continue;
            }

            // 填充长度（网络字节序）
            uint32_t net_length = htonl(length);
            memcpy(packet, &net_length, sizeof(net_length));

            // 填充数据
            memcpy(packet + sizeof(net_length), buffer, length);

            // 填充校验和（网络字节序）
            uint32_t net_checksum = htonl(checksum);
            memcpy(packet + sizeof(net_length) + length, &net_checksum, sizeof(net_checksum));

            // 发送数据包，确保全部发送
            int sent = send_all(sock, packet, packet_size);
            if (sent != packet_size) {
                printf("Send error, reconnect...\n");
                close(sock);
                sock = -1;
            }

            free(packet);
        }
    }
}

// 初始化SNTP
void initialise_sntp(void){
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);  // 使用新的函数
    esp_sntp_setservername(0, "pool.ntp.org");     // 使用新的函数
    esp_sntp_init();                               // 使用新的函数
}

// 事件处理程序
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();  // 尝试连接WiFi
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();  // 重新连接WiFi
        xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT);  // 清除连接位
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);  // 设置连接位
        initialise_sntp();  // 在连接后初始化SNTP
    }
}

// 初始化WiFi为STA模式
void wifi_init_sta(void)
{
    s_event_group = xEventGroupCreate();  // 创建事件组

    ESP_ERROR_CHECK(esp_netif_init());    // 初始化网络接口
    ESP_ERROR_CHECK(esp_event_loop_create_default());  // 创建默认事件循环
    esp_netif_create_default_wifi_sta();  // 创建默认的WiFi STA

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));  // 初始化WiFi

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;

    // 注册WiFi事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));  
    // 注册IP事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));  

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,  // 设置SSID
            .password = WIFI_PASSWORD,  // 设置密码
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,  // 设置认证模式
            .pmf_cfg = {    // 设置PMF配置，PMF即Protected Management Frames，用于保护管理帧
                .capable = true,    // 是否支持PMF
                .required = false   // 是否要求PMF
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );  // 设置WiFi模式为STA
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );  // 设置WiFi配置
    ESP_ERROR_CHECK(esp_wifi_start());  // 启动WiFi

    EventBits_t bits = xEventGroupWaitBits(s_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY);  // 等待连接事件

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "连接WiFi成功");  // 打印连接成功信息
    } else {
        ESP_LOGE(TAG, "连接WiFi失败");  // 打印连接失败信息
    }
}

void app_main(void) {
    printf("I2S INMP441 Example\n");

    // 初始化NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // 初始化WiFi
    wifi_init_sta();

    // 初始化 I2S
    i2s_init();

    // 创建音频数据队列
    audio_queue = xQueueCreate(QUEUE_LENGTH, sizeof(int16_t) * QUEUE_ITEM_SIZE);
    if (audio_queue == NULL) {
        printf("Failed to create audio queue.\n");
        return;
    }

    // 创建 I2S 读取任务，分配到核心 0
    xTaskCreatePinnedToCore(i2s_read_task, "i2s_read_task", 4096, NULL, 5, NULL, 0);

    // 创建发送任务，分配到核心 1
    xTaskCreatePinnedToCore(socket_send_task, "socket_send_task", 4096, NULL, 5, NULL, 1);
}