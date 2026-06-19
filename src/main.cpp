#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define LED_PIN 12

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
volatile int currentMode = 2; // 默认闪烁模式

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("\n[BLE STATUS] !!! MAC OS connect success !!!");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("\n[BLE STATUS] !!! blueteeth disconnect !!!");
      BLEDevice::startAdvertising();
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue().c_str();

      if (rxValue.length() > 0) {
        Serial.print("[Debug BLE Input] Received Command: ");
        Serial.println(rxValue);

        bool modeChanged = false;

        // ================================================================
        // 【新逻辑】：解析 0, 1, 2 指令并拦截
        // ================================================================
        if (rxValue.indexOf('0') != -1) { currentMode = 0; modeChanged = true; }
        else if (rxValue.indexOf('1') != -1) { currentMode = 1; modeChanged = true; }
        else if (rxValue.indexOf('2') != -1) { currentMode = 2; modeChanged = true; }

        if (modeChanged) {
          // 1. 串口打印响应
          Serial.println("[->] mode switch success, response: 9");

          // 2. 向 BLE 无线回传数字 9 (带换行符以保持流整洁)
          const char* successResponse = "9\n";
          pTxCharacteristic->setValue((uint8_t*)successResponse, strlen(successResponse));
          pTxCharacteristic->notify();
        }
        // 保留原逻辑：如果收到 '9'，则返回当前的实际模式数字 (0, 1 或 2)
        else if (rxValue.indexOf('9') != -1) {
          char responseMsg[10];
          snprintf(responseMsg, sizeof(responseMsg), "%d\n", currentMode);

          pTxCharacteristic->setValue((uint8_t*)responseMsg, strlen(responseMsg));
          pTxCharacteristic->notify();
          Serial.print("[->]  query status succes, responce: ");
          Serial.println(currentMode);
        }
      }
    }
};

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  setCpuFrequencyMhz(160);

  BLEDevice::init("ESP32-BLE-Controller");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(
                      TX_CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pTxCharacteristic->addDescriptor(new BLE2902());

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                       RX_CHARACTERISTIC_UUID,
                       BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
                     );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->getAdvertising()->start();
  Serial.println("[*] BLE serial port services ready, wait for  Mac control...");
}

void loop() {
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
  delay(10);
}