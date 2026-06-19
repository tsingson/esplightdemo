#include <Arduino.h>
#include <esp_bt.h>      // 控制器层 API (如 esp_bt_controller_disable)
#include <esp_bt_main.h> // 主机层 Bluedroid API (如 esp_bluedroid_disable)
// #include <WiFi.h>        // 显式引入 WiFi 库以修复 'WiFi' was not declared 错误
#include <esp_pm.h>
#include <esp_wifi.h>
#include <esp_bt.h>

#define LED_PIN 12

// 共享变量与互斥锁
int currentMode = 2;
SemaphoreHandle_t modeMutex;

// FreeRTOS 队列：用于核心 1 向核心 0 传递 '9' 指令
QueueHandle_t serialQueue;

TaskHandle_t QueryTaskHandle = NULL;

// Core 0 任务：完全基于队列阻塞，0% CPU 空转功耗
[[noreturn]] void queryTask(void *pvParameters) {
  (void) pvParameters;
  char receivedChar;

  for (;;) {
    // 强阻塞：如果队列里没有数据，当前核心完全挂起进入休眠
    if (xQueueReceive(serialQueue, &receivedChar, portMAX_DELAY) == pdTRUE) {
      if (receivedChar == '9') {
        if (xSemaphoreTake(modeMutex, portMAX_DELAY) == pdTRUE) {
          int modeCopy = currentMode;
          xSemaphoreGive(modeMutex);

          Serial.print(modeCopy); // 回写模式号
        }
      }
    }
  }
}

void setup() {
  // 1. 彻底关闭无线模块降功耗
  // WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
  esp_bluedroid_disable();
  esp_bt_controller_disable();

  // 2. 降低主频至 80MHz 限制动态功耗
  setCpuFrequencyMhz(80);

  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  // 3. 初始化 FreeRTOS 通信组件
  modeMutex = xSemaphoreCreateMutex();
  serialQueue = xQueueCreate(5, sizeof(char)); // 创建容量为 5 的字符队列

  // 4. 创建 Core 0 查询任务
  xTaskCreatePinnedToCore(
    queryTask,
    "QueryTask",
    2048,
    NULL,
    2,                  // 稍微提高优先级，确保响应及时
    &QueryTaskHandle,
    0
  );

  // 5. 高级电源管理配置（防止串口由于睡眠而死锁/乱码）
  #if CONFIG_PM_ENABLE
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);
  #endif

  Serial.println("ESP32 Ultra-Stable Low-Power Firmware Ready.");
}

void loop() {
  // 唯一数据源：由 Core 1 统一接收处理串口所有数据
  if (Serial.available() > 0) {
    char incomingChar = Serial.read();

    if (incomingChar == '0' || incomingChar == '1' || incomingChar == '2') {
      if (xSemaphoreTake(modeMutex, portMAX_DELAY) == pdTRUE) {
        currentMode = incomingChar - '0';
        int modeCopy = currentMode;
        xSemaphoreGive(modeMutex);

        Serial.print("Switching to Mode: ");
        Serial.println(modeCopy);
      }
    }
    else if (incomingChar == '9') {
      // 核心 1 收到 '9' 后，丢进队列交给核心 0
      xQueueSend(serialQueue, &incomingChar, 0);
    }
  }

  // 获取当前模式执行 LED 逻辑
  int localMode = 2;
  if (xSemaphoreTake(modeMutex, portMAX_DELAY) == pdTRUE) {
    localMode = currentMode;
    xSemaphoreGive(modeMutex);
  }

  // LED 调度状态机
  static bool ledState = LOW;
  switch (localMode) {
    case 0:
      digitalWrite(LED_PIN, LOW);
      vTaskDelay(pdMS_TO_TICKS(50)); // 静态模式，延长挂起时间，深度睡眠
      break;

    case 1:
      digitalWrite(LED_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(50)); // 静态模式，延长挂起时间，深度睡眠
      break;

    case 2:
      // 改用标准的 FreeRTOS 500ms 强阻塞延时
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      vTaskDelay(pdMS_TO_TICKS(500)); // 这 500ms 内，CPU 会进入 Light Sleep 节电状态
      break;
  }
}