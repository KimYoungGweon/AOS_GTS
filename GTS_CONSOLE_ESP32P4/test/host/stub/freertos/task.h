#pragma once
#include "freertos/FreeRTOS.h"
typedef void* TaskHandle_t;
static inline void vTaskDelay(TickType_t t){ (void)t; }
static inline int xTaskCreate(void (*f)(void*), const char*n, uint32_t s, void*p, uint32_t pr, TaskHandle_t*h)
{ (void)f;(void)n;(void)s;(void)p;(void)pr;(void)h; return 1; }
