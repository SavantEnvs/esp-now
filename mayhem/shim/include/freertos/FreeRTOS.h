/* Host shim of the FreeRTOS API (cooperative scheduler, virtual time): see esp_shim.h. */
#pragma once
#include "esp_shim.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t EventBits_t;
typedef uint32_t StackType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdPASS 1
#define pdFAIL 0
#define errQUEUE_EMPTY 0
#define errQUEUE_FULL 0
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define configTICK_RATE_HZ 1000
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define pdMS_TO_TICKS(ms) ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000U))
#define pdTICKS_TO_MS(t) ((uint32_t)(((uint64_t)(t) * 1000U) / configTICK_RATE_HZ))
#define tskNO_AFFINITY 0x7fffffff
#define configMAX_PRIORITIES 25
#define tskIDLE_PRIORITY 0
#define portYIELD_FROM_ISR(x) (void)(x)
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)
#define portMUX_INITIALIZER_UNLOCKED 0
typedef int portMUX_TYPE;
#define taskENTER_CRITICAL(m) (void)(m)
#define taskEXIT_CRITICAL(m) (void)(m)
#define configASSERT(x) do { if (!(x)) abort(); } while (0)

typedef struct shim_task *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);
typedef struct shim_queue *QueueHandle_t;
typedef QueueHandle_t SemaphoreHandle_t;
typedef struct shim_evgroup *EventGroupHandle_t;
typedef struct shim_timer *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
typedef struct { uint8_t opaque[64]; } StaticSemaphore_t;
typedef struct { uint8_t opaque[64]; } StaticQueue_t;
typedef struct { uint8_t opaque[64]; } StaticEventGroup_t;
typedef struct { uint8_t opaque[64]; } StaticTask_t;

/* task */
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *out, BaseType_t core);
#define xTaskCreate(fn, name, stack, arg, prio, out) xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, tskNO_AFFINITY)
void vTaskDelete(TaskHandle_t t);
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);
TickType_t xTaskGetTickCountFromISR(void);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
void taskYIELD(void);
void vTaskGetRunTimeStats(char *buf);
void vTaskList(char *buf);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t);
const char *pcTaskGetName(TaskHandle_t t);
void vTaskSuspend(TaskHandle_t t);
void vTaskResume(TaskHandle_t t);

/* queue */
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t ticks);
#define xQueueSendToBack xQueueSend
#define xQueueSendFromISR(q, item, woken) xQueueSend(q, item, 0)
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t q);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q);

/* semaphores (a queue of zero-size items, as in FreeRTOS) */
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t init);
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf);
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buf);
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive
#define xSemaphoreGiveFromISR(s, woken) xSemaphoreGive(s)
void vSemaphoreDelete(SemaphoreHandle_t s);
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t s);

/* event groups */
EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t g);
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t g);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear_on_exit,
                                BaseType_t wait_all, TickType_t ticks);

/* software timers */
TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload, void *id,
                           TimerCallbackFunction_t cb);
BaseType_t xTimerStart(TimerHandle_t t, TickType_t ticks);
BaseType_t xTimerStop(TimerHandle_t t, TickType_t ticks);
BaseType_t xTimerReset(TimerHandle_t t, TickType_t ticks);
BaseType_t xTimerDelete(TimerHandle_t t, TickType_t ticks);
BaseType_t xTimerChangePeriod(TimerHandle_t t, TickType_t period, TickType_t ticks);
BaseType_t xTimerIsTimerActive(TimerHandle_t t);
void vTimerSetReloadMode(TimerHandle_t t, UBaseType_t auto_reload);
void *pvTimerGetTimerID(TimerHandle_t t);

/* task notifications */
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks);
BaseType_t xTaskNotifyGive(TaskHandle_t t);

#ifdef __cplusplus
}
#endif
