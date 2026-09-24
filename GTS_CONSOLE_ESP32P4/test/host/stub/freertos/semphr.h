#pragma once
#include "freertos/FreeRTOS.h"
typedef void* SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void){ return (void*)1; }
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){ return (void*)1; }
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t s, TickType_t t){ (void)s;(void)t; return 1; }
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t s){ (void)s; return 1; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, TickType_t t){ (void)s;(void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s){ (void)s; return 1; }
