#include <statomic.h> // C11 原生原子操作，完美支持多核安全
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

// NimBLE 核心头文件
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define LED_PIN GPIO_NUM_12
static const char *TAG = "PRODUCTION_BLE";

// 全局句柄
static TaskHandle_t main_task_handle = NULL;
static esp_timer_handle_t led_timer = NULL;
static uint16_t conn_handle;

// 使用 C11 _Atomic 关键字，确保跨核（BLE线程与主线程）读写绝对安全
static _Atomic bool device_connected = false;
static _Atomic int current_mode = 2; // 默认模式 2：闪烁

// 硬件定时器回调：纳秒级响应，直接操作引脚寄存器
static void led_timer_callback(void *arg)
{
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(LED_PIN, led_state);
}

// 核心状态机更新：由事件驱动触发
void update_system_state(int mode)
{
    esp_timer_stop(led_timer);

    switch (mode)
    {
    case 0:
        gpio_set_level(LED_PIN, 0);
        break;
    case 1:
        gpio_set_level(LED_PIN, 1);
        break;
    case 2:
        // 启动定时器，每 500,000 微秒（500ms）触发一次中断
        esp_timer_start_periodic(led_timer, 500000);
        break;
    default:
        break;
    }
}

// 声明 GATT 读写回调
static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt,
                               void *arg);

// 定义 BLE GATT 目录树 (UUID 保持与原项目完全一致)
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00,
                                    0x40, 0x6E),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    // RX 特征值：支持 WRITE & WRITE_NO_RSP
                    .uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
                                                0x02, 0x00, 0x40, 0x6E),
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    // TX 特征值：支持 NOTIFY
                    .uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5,
                                                0x03, 0x00, 0x40, 0x6E),
                    .access_cb = gatt_svr_chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY,
                },
                {0} // 结束标志
            },
    },
    {0} // 结束标志
};

// 蓝牙广播初始化
void ble_app_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ESP32-BLE-IDF";
    fields.name_len = strlen((char *)fields.name);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // 默认高频快连广播间隔：100ms (160 * 0.625ms)
    adv_params.itvl_min = 160;
    adv_params.itvl_max = 160;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
}

// GAP 事件回调：处理连接与断开
static int ble_app_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            atomic_store(&device_connected, true);
            conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "!!! MAC OS connect success !!!");
        }
        else
        {
            ble_app_advertise(); // 连接失败，重新广播
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        atomic_store(&device_connected, false);
        ESP_LOGI(TAG, "!!! bluetooth disconnect !!!");
        ble_app_advertise(); // 断开连接，恢复广播
        break;
    }
    return 0;
}

// GATT 特征值读写核心回调
static int gatt_svr_chr_access(uint16_t conn_id, uint16_t attr_id, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len == 0) return 0;

        uint8_t cmd_buf[1];
        os_mbuf_copydata(ctxt->om, 0, 1, cmd_buf);
        char cmd = (char)cmd_buf[0];
        bool mode_changed = false;
        int target_mode = atomic_load(&current_mode);

        if (cmd == '0')
        {
            target_mode = 0;
            mode_changed = true;
        }
        else if (cmd == '1')
        {
            target_mode = 1;
            mode_changed = true;
        }
        else if (cmd == '2')
        {
            target_mode = 2;
            mode_changed = true;
        }

        if (mode_changed)
        {
            atomic_store(&current_mode, target_mode);
            ESP_LOGI(TAG, "[->] mode switch to %d, response: 9", target_mode);

            // 【事件驱动】：瞬间发送任务通知，跨线程秒级唤醒主任务
            if (main_task_handle != NULL)
            {
                xTaskNotifyGive(main_task_handle);
            }

            if (atomic_load(&device_connected))
            {
                struct os_mbuf *om = ble_hs_mbuf_from_flat("9\n", 2);
                ble_gattc_notify_custom(conn_handle, attr_id, om);
            }
        }
        else if (cmd == '9')
        {
            int active_mode = atomic_load(&current_mode);
            char resp[10];
            snprintf(resp, sizeof(resp), "%d\n", active_mode);
            ESP_LOGI(TAG, "[->] query status success, response: %d", active_mode);

            if (atomic_load(&device_connected))
            {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(resp, strlen(resp));
                ble_gattc_notify_custom(conn_handle, attr_id, om);
            }
        }
    }
    return 0;
}

// NimBLE 协议栈托管任务
void ble_host_task(void *param)
{
    nimble_port_run(); // 此函数会阻塞，接管蓝牙协议栈底层循环
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    // 1. 初始化存储（蓝牙配对及协议栈初始化依赖 NVS）
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 初始化原生 GPIO
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_PIN),
                             .mode = GPIO_MODE_OUTPUT,
                             .pull_up_en = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);

    // 获取当前主任务（app_main）的 FreeRTOS 句柄
    main_task_handle = xTaskGetCurrentTaskHandle();

    // 3. 创建原生高精度硬件定时器 `esp_timer`
    const esp_timer_create_args_t timer_args = {.callback = &led_timer_callback, .name = "led_timer"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &led_timer));
    update_system_state(atomic_load(&current_mode));

    // 4. 初始化 NimBLE 蓝牙协议栈
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_app_advertise;
    ble_hs_cfg.gatts_register_cb = gatt_svc_lcl_reg_cb;
    ble_hs_cfg.gap_cb = ble_app_gap_event;

    // 注册 GATT 服务树
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs);
    ble_gatts_add_svcs(gatt_svr_svcs);

    // 设置蓝牙设备名称
    ble_svc_gap_device_name_set("ESP32-BLE-IDF");

    // 启动 FreeRTOS 任务托管蓝牙栈
    xTaskCreate(ble_host_task, "ble_host_task", 4096, NULL, 5, NULL);

    // 5. 配置自动轻度睡眠 (Automatic Light Sleep)
    esp_pm_config_esp32_t pm_config = {.max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true};
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    ESP_LOGI(TAG, "ESP-IDF Native Ultra-Low Power Architecture Ready.");

    // 6. 终极事件死等循环
    while (1)
    {
        // 主任务彻底挂起，不消耗任何 CPU 轮询算力。
        // 当收到蓝牙写入中断唤醒时，瞬间完成刷新，然后再次闭眼休眠
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        update_system_state(atomic_load(&current_mode));
    }
}
