#include <Arduino.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled!
#endif

#define LED_PIN 12

BluetoothSerial SerialBT;
volatile int currentMode = 2;

// 蓝牙连接事件回调
void bluetoothCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param) {
  if (event == ESP_SPP_SRV_OPEN_EVT) {
    Serial.println("\n=========================================");
    Serial.println("[BT STATUS] !!! MAC OS 真正连接成功了 !!!");
    Serial.println("=========================================");
  }
  if (event == ESP_SPP_CLOSE_EVT) {
    Serial.println("\n[BT STATUS] !!! 蓝牙连接已断开 !!!");
  }
}

// 核心业务处理
void handleCommand(char cmd, Stream &output) {
  if (cmd >= '0' && cmd <= '2') {
    currentMode = cmd - '0';
    output.print("Switching to Mode: ");
    output.println(currentMode);
  }
  else if (cmd == '9') {
    output.println(currentMode); // 必须带 println，向 Go 发送 \n
  }
  // 如果收到 \n 或 \r，这里不命中任何条件，会安全地自动忽略
}

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  SerialBT.register_callback(bluetoothCallback);

  if (!SerialBT.begin("ESP32-LED-Controller")) {
    Serial.println("Bluetooth initialization failed!");
  } else {
    Serial.println("Bluetooth Ready! Name: ESP32-LED-Controller");
  }
}

void loop() {
  // 渠道一：物理串口调试
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();
    handleCommand(incomingChar, Serial);
  }

  // 渠道二：无线蓝牙串口
  while (SerialBT.available() > 0) {
    char incomingChar = SerialBT.read();

    // 过滤掉不可见的换行符，避免串口监视器打印杂乱空格
    if (incomingChar != '\n' && incomingChar != '\r') {
      Serial.print("[Debug BT Input] Received: '");
      Serial.print(incomingChar);
      Serial.println("'");
    }

    // 投递给状态机执行
    handleCommand(incomingChar, SerialBT);
  }

  // LED 闪烁状态机
  static unsigned long previousMillis = 0;
  static bool ledState = LOW;
  switch (currentMode) {
    case 0: digitalWrite(LED_PIN, LOW); break;
    case 1: digitalWrite(LED_PIN, HIGH); break;
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

  vTaskDelay(pdMS_TO_TICKS(10));
}