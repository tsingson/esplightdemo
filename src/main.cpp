#include <Arduino.h>
#include "BluetoothSerial.h"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled!
#endif

#define LED_PIN 12

BluetoothSerial SerialBT;
volatile int currentMode = 2;

// ================================================================
// 【核心新增】：蓝牙底层状态监听器
// 彻底监视 Mac 是否真正与 ESP32 完成了无线的空中牵手
// ================================================================
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

void handleCommand(char cmd, Stream &output) {
  if (cmd >= '0' && cmd <= '2') {
    currentMode = cmd - '0';
    output.print("Switching to Mode: ");
    output.println(currentMode);
  }
  else if (cmd == '9') {
    output.println(currentMode);
  }
}

void setup() {
  setCpuFrequencyMhz(160); // 确保蓝牙时钟稳定
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  // 【关键步骤】：在蓝牙启动前，注册状态监听回调函数
  SerialBT.register_callback(bluetoothCallback);

  if (!SerialBT.begin("ESP32-LED-Controller")) {
    Serial.println("Bluetooth initialization failed!");
  } else {
    Serial.println("Bluetooth Ready! Name: ESP32-LED-Controller");
  }
}

void loop() {
  // 渠道一：物理串口
  while (Serial.available() > 0) {
    char incomingChar = Serial.read();
    handleCommand(incomingChar, Serial);
  }

  // 渠道二：蓝牙无线串口
  while (SerialBT.available() > 0) {
    char incomingChar = SerialBT.read();

    // 如果有数据进来，这里一定会打印
    Serial.print("[Debug BT Input] Received: '");
    Serial.print(incomingChar);
    Serial.println("'");

    handleCommand(incomingChar, SerialBT);
  }

  // LED 状态机保持非阻塞轮询
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