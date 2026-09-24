#pragma once
#include <stdint.h>
#include <assert.h>
typedef int BaseType_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define configASSERT(x) assert(x)
