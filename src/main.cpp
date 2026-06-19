#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_pm.h>

#define LED_PIN 12

// 当前模式：0-灭，1-亮，2-闪烁
// 使用 volatile 确保多任务/底层中断读取时的可见性
volatile int currentMode = 2;

void setup() {
  // 1. 彻底关闭无线模块（Wi-Fi与蓝牙），切断基础射频功耗
  esp_wifi_stop();
  esp_bluedroid_disable();
  esp_bt_controller_disable();

  // 2. 降低 CPU 主频至 80MHz（大幅降低动态运行功耗，同时保证外设时钟稳定）
  setCpuFrequencyMhz(80);

  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);

  // 3. 高级电源管理配置（若底层 sdkconfig 开启了 CONFIG_PM_ENABLE，则自动支持自动轻度睡眠）
#if CONFIG_PM_ENABLE
  esp_pm_config_esp32_t pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 10,
    .light_sleep_enable = true
};
  esp_pm_configure(&pm_config);
#endif

  Serial.println("ESP32 Cleaned & Enhanced Low-Power Firmware Ready.");
}

void loop() {
  // 【修订 1】非阻塞式串口监听：利用 while 迅速清空接收缓冲区，延迟从 500ms 降至 10ms 内
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();

    if (incomingChar >= '0' && incomingChar <= '2') {
      currentMode = incomingChar - '0';
      Serial.print("Switching to Mode: ");
      Serial.println(currentMode);
    }
    else if (incomingChar == '9') {
      // 直接本地回写当前状态，移除原跨核心 Queue 调度，彻底消除死锁隐患
      Serial.print(currentMode);
    }
  }

  // 【修订 2】非阻塞时间戳调度：替代原有的 vTaskDelay 强阻塞，使 loop 保持高速轮询
  static unsigned long previousMillis = 0;
  static bool ledState = LOW;

  switch (currentMode) {
  case 0:
    digitalWrite(LED_PIN, LOW);
    break;

  case 1:
    digitalWrite(LED_PIN, HIGH);
    break;

  case 2: {
      unsigned long currentMillis = millis();
      if (currentMillis - previousMillis >= 500) {
        previousMillis = currentMillis;
        ledState = !ledState;
        digitalWrite(LED_PIN, ledState);
      }
      break;
  }
  }

  // 【修订 3】自适应省电休眠：每次循环强制让出 10ms CPU 时间片。
  // 在这 10ms 内，CPU 没有计算任务，会自动挂起并触发 Light Sleep 自动节电；
  // 醒来后立刻以 10ms 的超高刷新率响应串口，兼顾了“极致省电”与“即时响应”。
  vTaskDelay(pdMS_TO_TICKS(10));
}