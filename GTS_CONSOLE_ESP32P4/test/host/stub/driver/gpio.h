#pragma once
#include <stdint.h>
#include <assert.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERROR_CHECK(x) do{ esp_err_t _e=(x); assert(_e==ESP_OK);}while(0)
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
static inline esp_err_t gpio_config(const gpio_config_t*c){ (void)c; return ESP_OK; }
static inline int gpio_get_level(int p){ (void)p; return 1; }
