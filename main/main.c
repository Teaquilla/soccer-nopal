/*
 * =========================================================================================
 * Project: Soccer Nopal - Native ESP-IDF v5 Firmware
 * Target: ESP32-D0WD-V3 (Xtensa Dual-Core @ 240MHz, 4MB Flash)
 * Framework: Espressif IoT Development Framework (ESP-IDF v5.3.2)
 * =========================================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_bt.h"
#include "ps3.h"

static const char *TAG = "SOCCER_NOPAL";

// -----------------------------------------------------------------------------
// Pin Configuration & LEDC PWM Channels
// -----------------------------------------------------------------------------
#define PIN_MRA   25 // Right Motor PWM A
#define PIN_MRB   26 // Right Motor PWM B
#define PIN_MLA   33 // Left Motor PWM A
#define PIN_MLB   32 // Left Motor PWM B

#define PIN_SOLENOID      5   // Primary Solenoid Kicker (GPIO 5)
#define PIN_SOLENOID_AUX  15  // Auxiliary Kicker Line (GPIO 15)
#define KICK_DURATION_US  (50 * 1000) // 50ms safe pulse duration

#define PWM_FREQ_HZ       20000 // 20 kHz (Silent Ultrasonic)
#define PWM_DUTY_RES      LEDC_TIMER_8_BIT // 8-bit (0 - 255)
#define PWM_SPEED_MODE    LEDC_LOW_SPEED_MODE

#define CH_MRA            LEDC_CHANNEL_0
#define CH_MRB            LEDC_CHANNEL_1
#define CH_MLA            LEDC_CHANNEL_2
#define CH_MLB            LEDC_CHANNEL_3

// -----------------------------------------------------------------------------
// Default Speeds & Ramps (Loaded from NVS if available)
// -----------------------------------------------------------------------------
static uint8_t mac_bytes[6] = {0x22, 0x3d, 0x75, 0x2a, 0xf0, 0xb2};

static uint32_t ramp_up   = 200; // ms
static uint32_t ramp_down = 250; // ms

static uint8_t F_FAST_L    = 255, F_FAST_R    = 255;
static uint8_t F_MEDIUM_L  = 61,  F_MEDIUM_R  = 61;
static uint8_t F_LOW_L     = 35,  F_LOW_R     = 35;

static uint8_t B_FAST_L    = 171, B_FAST_R    = 171;
static uint8_t B_MEDIUM_L  = 61,  B_MEDIUM_R  = 61;
static uint8_t B_LOW_L     = 35,  B_LOW_R     = 35;

static uint8_t TR_LOW_LR   = 45;
static uint8_t TR_MEDIUM_LR= 75;
static uint8_t TR_FAST_LR  = 115;

// -----------------------------------------------------------------------------
// Control State
// -----------------------------------------------------------------------------
static volatile int target_speed_l = 0;
static volatile int target_speed_r = 0;
static float current_speed_l = 0.0f;
static float current_speed_r = 0.0f;

static volatile int64_t last_packet_time_us = 0;
#define FAILSAFE_TIMEOUT_US (250 * 1000) // 250 ms timeout

// -----------------------------------------------------------------------------
// Motor Actuation
// -----------------------------------------------------------------------------
static inline int clamp255(int val) {
    if (val > 255) return 255;
    if (val < -255) return -255;
    return val;
}

static void set_motor_speeds(int speed_l, int speed_r) {
    speed_l = clamp255(speed_l);
    speed_r = clamp255(speed_r);

    // Left Motor
    if (speed_l > 0) {
        ledc_set_duty(PWM_SPEED_MODE, CH_MLA, speed_l);
        ledc_set_duty(PWM_SPEED_MODE, CH_MLB, 0);
    } else if (speed_l < 0) {
        ledc_set_duty(PWM_SPEED_MODE, CH_MLA, 0);
        ledc_set_duty(PWM_SPEED_MODE, CH_MLB, -speed_l);
    } else {
        // Active Dynamic Brake
        ledc_set_duty(PWM_SPEED_MODE, CH_MLA, 0);
        ledc_set_duty(PWM_SPEED_MODE, CH_MLB, 0);
    }
    ledc_update_duty(PWM_SPEED_MODE, CH_MLA);
    ledc_update_duty(PWM_SPEED_MODE, CH_MLB);

    // Right Motor
    if (speed_r > 0) {
        ledc_set_duty(PWM_SPEED_MODE, CH_MRA, speed_r);
        ledc_set_duty(PWM_SPEED_MODE, CH_MRB, 0);
    } else if (speed_r < 0) {
        ledc_set_duty(PWM_SPEED_MODE, CH_MRA, 0);
        ledc_set_duty(PWM_SPEED_MODE, CH_MRB, -speed_r);
    } else {
        // Active Dynamic Brake
        ledc_set_duty(PWM_SPEED_MODE, CH_MRA, 0);
        ledc_set_duty(PWM_SPEED_MODE, CH_MRB, 0);
    }
    ledc_update_duty(PWM_SPEED_MODE, CH_MRA);
    ledc_update_duty(PWM_SPEED_MODE, CH_MRB);
}

static void init_motor_pwm(void) {
    ledc_timer_config_t timer_conf = {
        .speed_mode       = PWM_SPEED_MODE,
        .duty_resolution  = PWM_DUTY_RES,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = PWM_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf[4] = {
        {.channel = CH_MRA, .duty = 0, .gpio_num = PIN_MRA, .speed_mode = PWM_SPEED_MODE, .timer_sel = LEDC_TIMER_0},
        {.channel = CH_MRB, .duty = 0, .gpio_num = PIN_MRB, .speed_mode = PWM_SPEED_MODE, .timer_sel = LEDC_TIMER_0},
        {.channel = CH_MLA, .duty = 0, .gpio_num = PIN_MLA, .speed_mode = PWM_SPEED_MODE, .timer_sel = LEDC_TIMER_0},
        {.channel = CH_MLB, .duty = 0, .gpio_num = PIN_MLB, .speed_mode = PWM_SPEED_MODE, .timer_sel = LEDC_TIMER_0},
    };

    for (int i = 0; i < 4; i++) {
        ESP_ERROR_CHECK(ledc_channel_config(&ch_conf[i]));
    }

    set_motor_speeds(0, 0);
    ESP_LOGI(TAG, "Motor PWM initialized at %d Hz (Ultrasonic) on pins [25, 26, 33, 32]", PWM_FREQ_HZ);
}

// -----------------------------------------------------------------------------
// Solenoid Kicker (Non-blocking 50ms Safety Pulse on GPIO 5 & 15)
// -----------------------------------------------------------------------------
static volatile bool kick_active = false;
static volatile int64_t kick_start_us = 0;

static void init_solenoid(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PIN_SOLENOID) | (1ULL << PIN_SOLENOID_AUX),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PIN_SOLENOID, 0);
    gpio_set_level(PIN_SOLENOID_AUX, 0);
    ESP_LOGI(TAG, "Solenoid Kicker initialized on GPIO %d (aux: GPIO %d)", PIN_SOLENOID, PIN_SOLENOID_AUX);
}

static void trigger_solenoid(void) {
    if (!kick_active) {
        gpio_set_level(PIN_SOLENOID, 1);
        gpio_set_level(PIN_SOLENOID_AUX, 1);
        kick_start_us = esp_timer_get_time();
        kick_active = true;
        ESP_LOGI(TAG, "KICK! Solenoid triggered (50ms pulse on GPIO %d)", PIN_SOLENOID);
    }
}

static void update_solenoid(int64_t now_us) {
    if (kick_active && (now_us - kick_start_us >= KICK_DURATION_US)) {
        gpio_set_level(PIN_SOLENOID, 0);
        gpio_set_level(PIN_SOLENOID_AUX, 0);
        kick_active = false;
    }
}

// -----------------------------------------------------------------------------
// Core 1 Dedicated Real-Time Motor Loop (100 Hz / 10ms)
// -----------------------------------------------------------------------------
static void motor_task(void *pvParameters) {
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(10); // 10 ms (100 Hz)

    while (1) {
        int64_t now_us = esp_timer_get_time();

        // 0. Update Solenoid safety timer
        update_solenoid(now_us);

        // 1. Failsafe Watchdog: Cut motors if no packets received within 250ms
        if (ps3IsConnected()) {
            if (now_us - last_packet_time_us > FAILSAFE_TIMEOUT_US) {
                target_speed_l = 0;
                target_speed_r = 0;
            }
        } else {
            target_speed_l = 0;
            target_speed_r = 0;
        }

        // 2. High-Precision Ramp
        float step_up   = 255.0f / (ramp_up > 0 ? (ramp_up / 10.0f) : 1.0f);
        float step_down = 255.0f / (ramp_down > 0 ? (ramp_down / 10.0f) : 1.0f);

        // Left wheel ramp
        if (current_speed_l < target_speed_l) {
            current_speed_l += step_up;
            if (current_speed_l > target_speed_l) current_speed_l = target_speed_l;
        } else if (current_speed_l > target_speed_l) {
            current_speed_l -= step_down;
            if (current_speed_l < target_speed_l) current_speed_l = target_speed_l;
        }

        // Right wheel ramp
        if (current_speed_r < target_speed_r) {
            current_speed_r += step_up;
            if (current_speed_r > target_speed_r) current_speed_r = target_speed_r;
        } else if (current_speed_r > target_speed_r) {
            current_speed_r -= step_down;
            if (current_speed_r < target_speed_r) current_speed_r = target_speed_r;
        }

        // 3. Actuate outputs
        set_motor_speeds((int)current_speed_l, (int)current_speed_r);

        vTaskDelayUntil(&last_wake_time, period);
    }
}

// -----------------------------------------------------------------------------
// PS3 Controller Event Handling
// -----------------------------------------------------------------------------
static void controller_event_cb(ps3_t ps3, ps3_event_t event) {
    last_packet_time_us = esp_timer_get_time();

    // Speed modifiers (R1/R2 = Fast, L1/L2 = Medium, default = Low)
    bool is_fast   = ps3.button.r1 || ps3.button.r2;
    bool is_medium = ps3.button.l1 || ps3.button.l2;

    int ly = ps3.analog.stick.ly;
    int lx = ps3.analog.stick.lx;
    int ry = ps3.analog.stick.ry;
    int rx = ps3.analog.stick.rx;

    // Support both Left stick and Right stick
    int stick_y = (abs(ly) > abs(ry)) ? ly : ry;
    int stick_x = (abs(lx) > abs(rx)) ? lx : rx;

    // Classic PS3 Controller Layout (D-Pad, Face Buttons, & Sticks):
    // - Forward:  D-Pad UP, Triangle, Stick Up
    // - Backward: D-Pad DOWN, Cross, Stick Down
    // - Left:     D-Pad LEFT, Square, Stick Left
    // - Right:    D-Pad RIGHT, Circle, Stick Right
    bool move_forward  = ps3.button.up    || ps3.button.triangle || (stick_y < -40);
    bool move_backward = ps3.button.down  || ps3.button.cross    || (stick_y > 40);
    bool turn_left     = ps3.button.left  || ps3.button.square   || (stick_x < -40);
    bool turn_right    = ps3.button.right || ps3.button.circle   || (stick_x > 40);

    if (move_forward) {
        if (is_fast) {
            target_speed_l = F_FAST_L; target_speed_r = F_FAST_R;
        } else if (is_medium) {
            target_speed_l = F_MEDIUM_L; target_speed_r = F_MEDIUM_R;
        } else {
            target_speed_l = F_LOW_L; target_speed_r = F_LOW_R;
        }
    } else if (move_backward) {
        if (is_fast) {
            target_speed_l = -B_FAST_L; target_speed_r = -B_FAST_R;
        } else if (is_medium) {
            target_speed_l = -B_MEDIUM_L; target_speed_r = -B_MEDIUM_R;
        } else {
            target_speed_l = -B_LOW_L; target_speed_r = -B_LOW_R;
        }
    } else if (turn_left) {
        int tr = is_fast ? TR_FAST_LR : (is_medium ? TR_MEDIUM_LR : TR_LOW_LR);
        target_speed_l = -tr; target_speed_r = tr;
    } else if (turn_right) {
        int tr = is_fast ? TR_FAST_LR : (is_medium ? TR_MEDIUM_LR : TR_LOW_LR);
        target_speed_l = tr; target_speed_r = -tr;
    } else {
        target_speed_l = 0; target_speed_r = 0;
    }

    // Solenoid Kicker Trigger (R2 button or R3 thumbstick click)
    if (ps3.button.r2 || ps3.button.r3) {
        trigger_solenoid();
    }
}

// -----------------------------------------------------------------------------
// Load Calibrated Settings from NVS
// -----------------------------------------------------------------------------
static void load_nvs_settings(void) {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("config", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t str_len = 18;
        char mac_str[20] = {0};
        if (nvs_get_str(my_handle, "ps3_mac", mac_str, &str_len) == ESP_OK) {
            sscanf(mac_str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &mac_bytes[0], &mac_bytes[1], &mac_bytes[2],
                   &mac_bytes[3], &mac_bytes[4], &mac_bytes[5]);
            ESP_LOGI(TAG, "NVS loaded PS3 Master MAC: %s", mac_str);
        }
        nvs_get_u32(my_handle, "ramp_up", &ramp_up);
        nvs_get_u32(my_handle, "ramp_down", &ramp_down);
        nvs_get_u8(my_handle, "F_FAST_L", &F_FAST_L);
        nvs_get_u8(my_handle, "F_FAST_R", &F_FAST_R);
        nvs_get_u8(my_handle, "F_MEDIUM_L", &F_MEDIUM_L);
        nvs_get_u8(my_handle, "F_MEDIUM_R", &F_MEDIUM_R);
        nvs_get_u8(my_handle, "F_LOW_L", &F_LOW_L);
        nvs_get_u8(my_handle, "F_LOW_R", &F_LOW_R);
        nvs_get_u8(my_handle, "B_FAST_L", &B_FAST_L);
        nvs_get_u8(my_handle, "B_FAST_R", &B_FAST_R);
        nvs_get_u8(my_handle, "B_MEDIUM_L", &B_MEDIUM_L);
        nvs_get_u8(my_handle, "B_MEDIUM_R", &B_MEDIUM_R);
        nvs_get_u8(my_handle, "B_LOW_L", &B_LOW_L);
        nvs_get_u8(my_handle, "B_LOW_R", &B_LOW_R);
        nvs_get_u8(my_handle, "TR_FAST_LR", &TR_FAST_LR);
        nvs_get_u8(my_handle, "TR_MEDIUM_LR", &TR_MEDIUM_LR);
        nvs_get_u8(my_handle, "TR_LOW_LR", &TR_LOW_LR);
        nvs_close(my_handle);
        ESP_LOGI(TAG, "NVS settings successfully loaded.");
    } else {
        ESP_LOGW(TAG, "NVS config not found, using default calibrations.");
    }
}

// -----------------------------------------------------------------------------
// Application Entry Point
// -----------------------------------------------------------------------------
void app_main(void) {
    ESP_LOGI(TAG, "=======================================================");
    ESP_LOGI(TAG, "  SOCCER NOPAL - NATIVE ESP-IDF v5.3.2 FIRMWARE");
    ESP_LOGI(TAG, "=======================================================");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    load_nvs_settings();

    // 2. Initialize Motor PWM Hardware & Solenoid Kicker
    init_motor_pwm();
    init_solenoid();

    // 3. Configure Bluetooth Stack & PS3 Host
    ESP_LOGI(TAG, "Initializing PS3 Bluetooth Host with Master MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             mac_bytes[0], mac_bytes[1], mac_bytes[2], mac_bytes[3], mac_bytes[4], mac_bytes[5]);

    ps3SetEventCallback(controller_event_cb);
    ps3SetBluetoothMacAddress(mac_bytes);
    ps3Init();

    // Force Max Bluetooth RF Power (+9 dBm)
    esp_bredr_tx_power_set(ESP_PWR_LVL_P9, ESP_PWR_LVL_P9);
    ESP_LOGI(TAG, "Bluetooth RF Transmit Power set to MAX (+9 dBm)");

    // 4. Create Dedicated Motion Task on Core 1 (Priority 5, Real-Time)
    xTaskCreatePinnedToCore(
        motor_task,
        "motor_loop",
        4096,
        NULL,
        5,
        NULL,
        1 // Core 1 (App Core)
    );
    ESP_LOGI(TAG, "Real-Time Motion Task spawned on Core 1 @ 100 Hz (vTaskDelayUntil)");

    ESP_LOGI(TAG, "System Ready! Press PS button on controller to begin.");
}
