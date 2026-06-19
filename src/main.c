#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"


// NimBLE 核心头文件
#include "host/ble_hs.h"
#include "host/ble_gap.h" // 【修正】添加此行以解决函数定义缺失
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gap.h"


#define LED_PIN GPIO_NUM_12
static const char *TAG = "PRODUCTION_BLE"; // 【修正】如果还报 unused，检查 log 是否使用了它

static TaskHandle_t main_task_handle = NULL;
static esp_timer_handle_t led_timer = NULL;
static uint16_t conn_handle;

static SemaphoreHandle_t global_mutex = NULL;
static bool device_connected = false;
static int current_mode = 2;

static void led_timer_callback(void *arg) {
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(LED_PIN, led_state);
}

void update_system_state(int mode) {
    esp_timer_stop(led_timer);
    switch (mode) {
        case 0: gpio_set_level(LED_PIN, 0); break;
        case 1: gpio_set_level(LED_PIN, 1); break;
        case 2: esp_timer_start_periodic(led_timer, 500000); break;
    }
}

static int gatt_svr_chr_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E),
        .characteristics = (struct ble_gatt_chr_def[]){
            {.uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E), .access_cb = gatt_svr_chr_access, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
            {.uuid = BLE_UUID128_DECLARE(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E), .access_cb = gatt_svr_chr_access, .flags = BLE_GATT_CHR_F_NOTIFY},
            {0}
        },
    },
    {0}
};

void ble_app_advertise(void) {
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ESP32-BLE-IDF";
    fields.name_len = strlen((char *)fields.name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = 160;
    adv_params.itvl_max = 160;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
}

static int ble_app_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                if (xSemaphoreTake(global_mutex, portMAX_DELAY)) { device_connected = true; xSemaphoreGive(global_mutex); }
                conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Connected"); // 使用 TAG 防止未使用警告
            } else ble_app_advertise();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            if (xSemaphoreTake(global_mutex, portMAX_DELAY)) { device_connected = false; xSemaphoreGive(global_mutex); }
            ble_app_advertise();
            break;
    }
    return 0;
}

static int gatt_svr_chr_access(uint16_t conn_id, uint16_t attr_id, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t cmd_buf[1];
        os_mbuf_copydata(ctxt->om, 0, 1, cmd_buf);
        char cmd = (char)cmd_buf[0];
        int target = 2;
        bool connected = false;
        if (xSemaphoreTake(global_mutex, portMAX_DELAY)) { target = current_mode; connected = device_connected; xSemaphoreGive(global_mutex); }
        bool changed = false;
        if (cmd >= '0' && cmd <= '2') { target = cmd - '0'; changed = true; }
        if (changed) {
            if (xSemaphoreTake(global_mutex, portMAX_DELAY)) { current_mode = target; xSemaphoreGive(global_mutex); }
            xTaskNotifyGive(main_task_handle);
            if (connected) { struct os_mbuf *om = ble_hs_mbuf_from_flat("9\n", 2); ble_gattc_notify_custom(conn_handle, attr_id, om); }
        } else if (cmd == '9' && connected) {
            char resp[4]; snprintf(resp, sizeof(resp), "%d\n", target);
            struct os_mbuf *om = ble_hs_mbuf_from_flat(resp, strlen(resp));
            ble_gattc_notify_custom(conn_handle, attr_id, om);
        }
    }
    return 0;
}

void ble_host_task(void *param) { nimble_port_run(); nimble_port_freertos_deinit(); }

void app_main(void) {
    global_mutex = xSemaphoreCreateMutex();
    nvs_flash_init();
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << LED_PIN), .mode = GPIO_MODE_OUTPUT, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
    gpio_config(&io_conf);
    main_task_handle = xTaskGetCurrentTaskHandle();

    const esp_timer_create_args_t timer_args = {.callback = &led_timer_callback, .name = "led_timer"};
    esp_timer_create(&timer_args, &led_timer);
    update_system_state(2);

    nimble_port_init();
    // 【修正】直接设置回调，不再依赖可能找不到的函数
    // ble_hs_cfg.gap_cb = ble_app_gap_event;

    // ble_gap_event_handler_set(ble_app_gap_event);


    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svr_svcs); ble_gatts_add_svcs(gatt_svr_svcs);
    ble_svc_gap_device_name_set("ESP32-BLE-IDF");
    xTaskCreate(ble_host_task, "ble_host_task", 4096, NULL, 5, NULL);

    esp_pm_config_t pm_config = {.max_freq_mhz = 160, .min_freq_mhz = 40, .light_sleep_enable = true};
    esp_pm_configure(&pm_config);

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int mode = 2;
        if (xSemaphoreTake(global_mutex, portMAX_DELAY)) { mode = current_mode; xSemaphoreGive(global_mutex); }
        update_system_state(mode);
    }
}