#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <atomic>
#include <Ticker.h>      // 硬件定时器库
#include "esp_pm.h"      // 电源管理库

#define LED_PIN 12
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define RX_CHARACTERISTIC_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define TX_CHARACTERISTIC_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

// 全局/静态句柄
BLEServer *pServer = NULL;
BLECharacteristic *pTxCharacteristic = NULL;
BLEAdvertising *pAdvertising = NULL;

std::atomic<bool> deviceConnected(false);
std::atomic<int> currentMode(2);

Ticker ledTicker;
Ticker advTimeoutTicker; // 用于动态调整蓝牙广播频率的定时器
TaskHandle_t mainTaskHandle = NULL;

// 硬件中断回调：翻转 LED
void toggleLED() {
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
}

// 降频广播回调：当断开连接超过 30 秒未重连，拉长广播间隔以极端省电
void switchToLowPowerAdvertising() {
    if (!deviceConnected.load() && pAdvertising != NULL) {
        Serial.println("[BLE] 30s timeout reached. Switching to Low-Power Advertising (1000ms)...");
        Serial.flush(); // 确保串口打印完成再调整

        pAdvertising->stop();
        // 1000ms * 0.625 = 1600
        pAdvertising->setMinInterval(1600);
        pAdvertising->setMaxInterval(1600);
        pAdvertising->start();
    }
}

// 核心状态机更新
void updateSystemState(int mode) {
    ledTicker.detach();

    switch (mode) {
        case 0:
            digitalWrite(LED_PIN, LOW);
            break;
        case 1:
            digitalWrite(LED_PIN, HIGH);
            break;
        case 2:
            ledTicker.attach(0.5, toggleLED);
            break;
        default:
            break;
    }
}

// 产品化改进：移除匿名的 `new` 分配，使用标准的类定义
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        deviceConnected.store(true);
        advTimeoutTicker.detach(); // 成功连接，取消降频定时器
        Serial.println("\n[BLE STATUS] !!! MAC OS connect success !!!");
        Serial.flush();
    }

    void onDisconnect(BLEServer* pServer) override {
        deviceConnected.store(false);
        Serial.println("\n[BLE STATUS] !!! bluetooth disconnect !!!");
        Serial.println("[BLE] Starting Fast Advertising (100ms) for 30 seconds...");
        Serial.flush();

        // 断开后，先恢复高频快连广播（100ms 间隔）
        if (pAdvertising != NULL) {
            pAdvertising->stop();
            // 100ms * 0.625 = 160
            pAdvertising->setMinInterval(160);
            pAdvertising->setMaxInterval(160);
            pAdvertising->start();
        }

        // 开启一个 30 秒的单次定时器，30 秒后若无人连接，则触发降频省电
        advTimeoutTicker.once(30.0, switchToLowPowerAdvertising);
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        uint8_t* data = pCharacteristic->getData();
        size_t len = pCharacteristic->getLength();

        if (len == 0) return;

        char cmd = (char)data[0];
        bool modeChanged = false;
        int targetMode = currentMode.load();

        if (cmd == '0') { targetMode = 0; modeChanged = true; }
        else if (cmd == '1') { targetMode = 1; modeChanged = true; }
        else if (cmd == '2') { targetMode = 2; modeChanged = true; }

        if (modeChanged) {
            currentMode.store(targetMode);
            Serial.printf("[->] mode switch to %d, response: 9\n", targetMode);
            Serial.flush(); // 关键：休眠前强制排空串口

            if (mainTaskHandle != NULL) {
                xTaskNotifyGive(mainTaskHandle);
            }

            if (deviceConnected.load()) {
                const char* successResponse = "9\n";
                pTxCharacteristic->setValue((uint8_t*)successResponse, strlen(successResponse));
                pTxCharacteristic->notify();
            }
        }
        else if (cmd == '9') {
            int activeMode = currentMode.load();
            char responseMsg[10];
            snprintf(responseMsg, sizeof(responseMsg), "%d\n", activeMode);

            if (deviceConnected.load()) {
                pTxCharacteristic->setValue((uint8_t*)responseMsg, strlen(responseMsg));
                pTxCharacteristic->notify();
            }
            Serial.printf("[->] query status success, response: %d\n", activeMode);
            Serial.flush();
        }
    }
};

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);

    mainTaskHandle = xTaskGetCurrentTaskHandle();

    // 1. 初始化 BLE
    BLEDevice::init("ESP32-BLE-Controller");
    pServer = BLEDevice::createServer();

    // 产品化优化：使用静态局部变量代替裸指针 new，防止堆内存碎片与悬空
    static MyServerCallbacks serverCallbacks;
    pServer->setCallbacks(&serverCallbacks);

    BLEService *pService = pServer->createService(SERVICE_UUID);

    pTxCharacteristic = pService->createCharacteristic(TX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    static BLE2902 txDescriptor; // 使用静态变量代替 new
    pTxCharacteristic->addDescriptor(&txDescriptor);

    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(RX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    static MyCallbacks rxCallbacks; // 使用静态变量代替 new
    pRxCharacteristic->setCallbacks(&rxCallbacks);

    pService->start();

    // 2. 配置初始广播参数（默认高频快连）
    pAdvertising = pServer->getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinInterval(160); // 100ms
    pAdvertising->setMaxInterval(160);
    pAdvertising->start();

    // 3. 初始化硬件状态
    updateSystemState(currentMode.load());

    // 4. 配置自动轻度睡眠 (Automatic Light Sleep)
    esp_pm_config_esp32_t pm_config;
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 40;
    pm_config.light_sleep_enable = true;
    esp_pm_configure(&pm_config);

    Serial.println("[*] Production-Ready Ultra-Low Power BLE Controller Started.");
    Serial.flush();
}

void loop() {
    // 死等 FreeRTOS 任务通知，此时 CPU 核心完全挂起，配合 Light Sleep 降频
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // 只有收到蓝牙写入指令触发跨线程唤醒后，才会执行到这里
    updateSystemState(currentMode.load());
}