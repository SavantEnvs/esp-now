/*
 * Cooperative FreeRTOS emulation on pthreads with virtual time (see esp_shim.h).
 *
 * Every FreeRTOS task is a pthread, but only the task that holds the "CPU" (g_cur) runs;
 * all others wait on their own condition variable. The running task gives the CPU away
 * only when it blocks (queue/semaphore/event group/delay) or yields. When no task is
 * runnable, the virtual clock jumps to the earliest pending timeout. Software timers run
 * in a timer-service task, like FreeRTOS's own.
 */
#include <pthread.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"

enum { T_READY, T_BLOCKED, T_DEAD };

struct shim_task {
    pthread_t th;
    pthread_cond_t cv;
    int state;
    void *wait_obj;
    uint64_t wake;          /* absolute tick, UINT64_MAX = forever */
    int timed_out;
    TaskFunction_t fn;
    void *arg;
    char name[16];
    uint32_t notify;
    struct shim_task *next; /* ring of live tasks, in creation order */
};

static pthread_mutex_t K = PTHREAD_MUTEX_INITIALIZER;
static struct shim_task *g_cur;
static struct shim_task *g_ring;   /* any member of the ring */
static int g_ntasks;
static uint64_t g_now;

#define FOREVER UINT64_MAX

static void kinit(void)
{
    if (g_cur) {
        return;
    }
    struct shim_task *t = calloc(1, sizeof(*t));
    pthread_cond_init(&t->cv, NULL);
    t->th = pthread_self();
    strcpy(t->name, "main");
    t->state = T_READY;
    t->wake = FOREVER;
    t->next = t;
    g_ring = g_cur = t;
    g_ntasks = 1;
}

static void ring_remove(struct shim_task *t)
{
    struct shim_task *p = t;
    while (p->next != t) {
        p = p->next;
    }
    p->next = t->next;
    if (g_ring == t) {
        g_ring = (t->next == t) ? NULL : t->next;
    }
    g_ntasks--;
}

/* Choose the next task to run after `from` (which is no longer runnable or yields).
 * Advances virtual time when nothing is runnable. K held. */
static struct shim_task *pick_next(struct shim_task *from, int from_in_ring)
{
    for (;;) {
        struct shim_task *start = from_in_ring ? from->next : g_ring;
        struct shim_task *p = start;
        if (p) {
            do {
                if (p->state == T_READY) {
                    return p;
                }
                p = p->next;
            } while (p != start);
        }
        /* Nothing runnable: advance the clock to the earliest timeout. */
        uint64_t min = FOREVER;
        p = g_ring;
        if (p) {
            do {
                if (p->state == T_BLOCKED && p->wake < min) {
                    min = p->wake;
                }
                p = p->next;
            } while (p != g_ring);
        }
        if (min == FOREVER) {
            fprintf(stderr, "esp-now shim: every task is blocked forever (deadlock)\n");
            abort();
        }
        g_now = min;
        p = g_ring;
        do {
            if (p->state == T_BLOCKED && p->wake <= g_now) {
                p->state = T_READY;
                p->timed_out = 1;
                p->wait_obj = NULL;
            }
            p = p->next;
        } while (p != g_ring);
    }
}

static void wait_for_cpu(struct shim_task *self)
{
    while (g_cur != self && self->state != T_DEAD) {
        pthread_cond_wait(&self->cv, &K);
    }
    if (self->state == T_DEAD) {
        pthread_mutex_unlock(&K);
        pthread_cond_destroy(&self->cv);
        free(self);
        pthread_exit(NULL);
    }
}

/* Hand the CPU to the next task and wait until we get it back. K held. */
static void reschedule(void)
{
    struct shim_task *self = g_cur;
    struct shim_task *n = pick_next(self, 1);
    if (n != self) {
        g_cur = n;
        pthread_cond_signal(&n->cv);
        wait_for_cpu(self);
    }
}

/* Block the running task on `obj` for at most `ticks`. Returns 1 on timeout. K held. */
static int block_on(void *obj, TickType_t ticks)
{
    struct shim_task *self = g_cur;
    if (ticks == 0) {
        return 1;
    }
    self->state = T_BLOCKED;
    self->wait_obj = obj;
    self->timed_out = 0;
    self->wake = (ticks == portMAX_DELAY) ? FOREVER : g_now + ticks;
    reschedule();
    self->wake = FOREVER;
    return self->timed_out;
}

static void wake_all(void *obj)
{
    struct shim_task *p = g_ring;
    if (!p) {
        return;
    }
    do {
        if (p->state == T_BLOCKED && p->wait_obj == obj) {
            p->state = T_READY;
            p->wait_obj = NULL;
        }
        p = p->next;
    } while (p != g_ring);
}

/* remaining ticks of a wait that started at `start` with budget `ticks` */
static TickType_t remaining(uint64_t start, TickType_t ticks)
{
    if (ticks == portMAX_DELAY) {
        return portMAX_DELAY;
    }
    uint64_t end = start + ticks;
    return g_now >= end ? 0 : (TickType_t)(end - g_now);
}

static void *task_trampoline(void *a)
{
    struct shim_task *self = a;
    pthread_mutex_lock(&K);
    wait_for_cpu(self);
    pthread_mutex_unlock(&K);
    self->fn(self->arg);
    vTaskDelete(NULL); /* a FreeRTOS task must not return; treat it as deleting itself */
    return NULL;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg,
                                   UBaseType_t prio, TaskHandle_t *out, BaseType_t core)
{
    (void)stack; (void)prio; (void)core;
    pthread_mutex_lock(&K);
    kinit();
    struct shim_task *t = calloc(1, sizeof(*t));
    pthread_cond_init(&t->cv, NULL);
    t->fn = fn;
    t->arg = arg;
    t->state = T_READY;
    t->wake = FOREVER;
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "task");
    /* insert just before g_cur so it runs after the tasks already waiting */
    t->next = g_cur->next;
    g_cur->next = t;
    g_ntasks++;
    if (out) {
        *out = t;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t->th, &attr, task_trampoline, t) != 0) {
        abort();
    }
    pthread_attr_destroy(&attr);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t t)
{
    pthread_mutex_lock(&K);
    kinit();
    struct shim_task *self = g_cur;
    if (!t || t == self) {
        self->state = T_DEAD;          /* skipped by pick_next, still linked for the walk */
        struct shim_task *n = pick_next(self, 1);
        ring_remove(self);
        g_cur = n;
        pthread_cond_signal(&n->cv);
        pthread_mutex_unlock(&K);
        pthread_cond_destroy(&self->cv);
        free(self);
        pthread_exit(NULL);
    }
    ring_remove(t);
    t->state = T_DEAD;
    pthread_cond_signal(&t->cv); /* it frees itself and exits */
    pthread_mutex_unlock(&K);
}

void vTaskDelay(TickType_t ticks)
{
    pthread_mutex_lock(&K);
    kinit();
    if (ticks == 0) {
        reschedule();
    } else {
        block_on(&g_now /* nobody wakes this */, ticks);
    }
    pthread_mutex_unlock(&K);
}

void taskYIELD(void)
{
    vTaskDelay(0);
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)g_now;
}

TickType_t xTaskGetTickCountFromISR(void)
{
    return (TickType_t)g_now;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    pthread_mutex_lock(&K);
    kinit();
    TaskHandle_t t = g_cur;
    pthread_mutex_unlock(&K);
    return t;
}

void vTaskGetRunTimeStats(char *buf) { if (buf) buf[0] = 0; }
void vTaskList(char *buf) { if (buf) buf[0] = 0; }
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t t) { (void)t; return 1024; }
const char *pcTaskGetName(TaskHandle_t t) { return t ? t->name : g_cur ? g_cur->name : "main"; }
void vTaskSuspend(TaskHandle_t t) { (void)t; }
void vTaskResume(TaskHandle_t t) { (void)t; }

uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t ticks)
{
    pthread_mutex_lock(&K);
    kinit();
    struct shim_task *self = g_cur;
    uint64_t start = g_now;
    while (self->notify == 0) {
        TickType_t r = remaining(start, ticks);
        if (r == 0 || block_on(self, r)) {
            break;
        }
    }
    uint32_t v = self->notify;
    if (v) {
        self->notify = clear ? 0 : v - 1;
    }
    pthread_mutex_unlock(&K);
    return v;
}

BaseType_t xTaskNotifyGive(TaskHandle_t t)
{
    pthread_mutex_lock(&K);
    t->notify++;
    wake_all(t);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

/* ------------------------------------------------------------------ queues */
struct shim_queue {
    UBaseType_t len, item, count, head;
    int is_mutex;
    uint8_t *buf;
};

static QueueHandle_t queue_new(UBaseType_t len, UBaseType_t item)
{
    struct shim_queue *q = calloc(1, sizeof(*q));
    if (!q) {
        return NULL;
    }
    q->len = len;
    q->item = item;
    q->buf = calloc(len ? len : 1, item ? item : 1);
    return q;
}

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t item_size)
{
    return queue_new(len, item_size);
}

void vQueueDelete(QueueHandle_t q)
{
    if (!q) {
        return;
    }
    free(q->buf);
    free(q);
}

static BaseType_t queue_send(QueueHandle_t q, const void *item, TickType_t ticks, int front)
{
    pthread_mutex_lock(&K);
    kinit();
    uint64_t start = g_now;
    while (q->count >= q->len) {
        TickType_t r = remaining(start, ticks);
        if (r == 0 || block_on(q, r)) {
            if (q->count >= q->len) {
                pthread_mutex_unlock(&K);
                return errQUEUE_FULL;
            }
        }
    }
    UBaseType_t slot;
    if (front) {
        q->head = (q->head + q->len - 1) % q->len;
        slot = q->head;
    } else {
        slot = (q->head + q->count) % q->len;
    }
    if (q->item) {
        memcpy(q->buf + (size_t)slot * q->item, item, q->item);
    }
    q->count++;
    wake_all(q);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    return queue_send(q, item, ticks, 0);
}

BaseType_t xQueueSendToFront(QueueHandle_t q, const void *item, TickType_t ticks)
{
    return queue_send(q, item, ticks, 1);
}

static BaseType_t queue_recv(QueueHandle_t q, void *item, TickType_t ticks, int peek)
{
    pthread_mutex_lock(&K);
    kinit();
    uint64_t start = g_now;
    while (q->count == 0) {
        TickType_t r = remaining(start, ticks);
        if (r == 0 || block_on(q, r)) {
            if (q->count == 0) {
                pthread_mutex_unlock(&K);
                return errQUEUE_EMPTY;
            }
        }
    }
    if (q->item && item) {
        memcpy(item, q->buf + (size_t)q->head * q->item, q->item);
    }
    if (!peek) {
        q->head = (q->head + 1) % q->len;
        q->count--;
        wake_all(q);
    }
    pthread_mutex_unlock(&K);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    return queue_recv(q, item, ticks, 0);
}

BaseType_t xQueuePeek(QueueHandle_t q, void *item, TickType_t ticks)
{
    return queue_recv(q, item, ticks, 1);
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    pthread_mutex_lock(&K);
    q->count = q->head = 0;
    wake_all(q);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q->count; }
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) { return q->len - q->count; }

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return queue_new(1, 0);
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max, UBaseType_t init)
{
    QueueHandle_t q = queue_new(max, 0);
    if (q) {
        q->count = init;
    }
    return q;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    QueueHandle_t q = queue_new(1, 0);
    if (q) {
        q->count = 1;
        q->is_mutex = 1;
    }
    return q;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void) { return xSemaphoreCreateMutex(); }
SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *buf) { (void)buf; return xSemaphoreCreateMutex(); }
SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *buf) { (void)buf; return xSemaphoreCreateBinary(); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    return queue_recv(s, NULL, ticks, 0);
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    return queue_send(s, NULL, 0, 0);
}

void vSemaphoreDelete(SemaphoreHandle_t s) { vQueueDelete(s); }
UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t s) { return s->count; }

/* ------------------------------------------------------------ event groups */
struct shim_evgroup {
    EventBits_t bits;
};

EventGroupHandle_t xEventGroupCreate(void)
{
    return calloc(1, sizeof(struct shim_evgroup));
}

void vEventGroupDelete(EventGroupHandle_t g) { free(g); }

EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits)
{
    pthread_mutex_lock(&K);
    kinit();
    g->bits |= bits;
    EventBits_t v = g->bits;
    wake_all(g);
    pthread_mutex_unlock(&K);
    return v;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t g, EventBits_t bits)
{
    pthread_mutex_lock(&K);
    EventBits_t v = g->bits;
    g->bits &= ~bits;
    pthread_mutex_unlock(&K);
    return v;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t g) { return g->bits; }

EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits, BaseType_t clear_on_exit,
                                BaseType_t wait_all, TickType_t ticks)
{
    pthread_mutex_lock(&K);
    kinit();
    uint64_t start = g_now;
    for (;;) {
        EventBits_t have = g->bits & bits;
        if (wait_all ? have == bits : have != 0) {
            EventBits_t v = g->bits;
            if (clear_on_exit) {
                g->bits &= ~bits;
            }
            pthread_mutex_unlock(&K);
            return v;
        }
        TickType_t r = remaining(start, ticks);
        if (r == 0 || block_on(g, r)) {
            have = g->bits & bits;
            if (!(wait_all ? have == bits : have != 0)) {
                EventBits_t v = g->bits;
                pthread_mutex_unlock(&K);
                return v;
            }
        }
    }
}

/* ------------------------------------------------------------------ timers */
struct shim_timer {
    char name[16];
    TickType_t period;
    UBaseType_t reload;
    void *id;
    TimerCallbackFunction_t cb;
    int active;
    int deleted;
    uint64_t expiry;
    struct shim_timer *next;
};

static struct shim_timer *g_timers;
static int g_timer_task_started;
static int g_timer_list_obj;

static void timer_task(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&K);
    for (;;) {
        /* reap deleted timers */
        for (struct shim_timer **pp = &g_timers; *pp;) {
            if ((*pp)->deleted) {
                struct shim_timer *d = *pp;
                *pp = d->next;
                free(d);
            } else {
                pp = &(*pp)->next;
            }
        }
        struct shim_timer *due = NULL;
        uint64_t min = FOREVER;
        for (struct shim_timer *t = g_timers; t; t = t->next) {
            if (t->active && t->expiry < min) {
                min = t->expiry;
                due = t;
            }
        }
        if (due && min <= g_now) {
            if (due->reload) {
                due->expiry = g_now + (due->period ? due->period : 1);
            } else {
                due->active = 0;
            }
            pthread_mutex_unlock(&K);
            due->cb(due);
            pthread_mutex_lock(&K);
            continue;
        }
        block_on(&g_timer_list_obj, min == FOREVER ? portMAX_DELAY : (TickType_t)(min - g_now));
    }
}

TimerHandle_t xTimerCreate(const char *name, TickType_t period, UBaseType_t auto_reload, void *id,
                           TimerCallbackFunction_t cb)
{
    struct shim_timer *t = calloc(1, sizeof(*t));
    if (!t) {
        return NULL;
    }
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "tmr");
    t->period = period;
    t->reload = auto_reload;
    t->id = id;
    t->cb = cb;
    pthread_mutex_lock(&K);
    kinit();
    t->next = g_timers;
    g_timers = t;
    int start = !g_timer_task_started;
    g_timer_task_started = 1;
    pthread_mutex_unlock(&K);
    if (start) {
        xTaskCreate(timer_task, "Tmr Svc", 4096, NULL, 1, NULL);
    }
    return t;
}

BaseType_t xTimerStart(TimerHandle_t t, TickType_t ticks)
{
    (void)ticks;
    pthread_mutex_lock(&K);
    t->active = 1;
    t->expiry = g_now + (t->period ? t->period : 1);
    wake_all(&g_timer_list_obj);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

BaseType_t xTimerReset(TimerHandle_t t, TickType_t ticks) { return xTimerStart(t, ticks); }

BaseType_t xTimerStop(TimerHandle_t t, TickType_t ticks)
{
    (void)ticks;
    pthread_mutex_lock(&K);
    t->active = 0;
    wake_all(&g_timer_list_obj);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t t, TickType_t ticks)
{
    (void)ticks;
    pthread_mutex_lock(&K);
    t->active = 0;
    t->deleted = 1;
    wake_all(&g_timer_list_obj);
    pthread_mutex_unlock(&K);
    return pdPASS;
}

BaseType_t xTimerChangePeriod(TimerHandle_t t, TickType_t period, TickType_t ticks)
{
    t->period = period;
    return xTimerStart(t, ticks);
}

BaseType_t xTimerIsTimerActive(TimerHandle_t t) { return t->active; }
void vTimerSetReloadMode(TimerHandle_t t, UBaseType_t auto_reload) { t->reload = auto_reload; }
void *pvTimerGetTimerID(TimerHandle_t t) { return t->id; }

/* -------------------------------------------------------------- shim API */
void shim_settle(void)
{
    pthread_mutex_lock(&K);
    kinit();
    for (;;) {
        int other_ready = 0;
        struct shim_task *p = g_cur->next;
        while (p != g_cur) {
            if (p->state == T_READY) {
                other_ready = 1;
                break;
            }
            p = p->next;
        }
        if (!other_ready) {
            break;
        }
        reschedule();
    }
    pthread_mutex_unlock(&K);
}

void shim_sleep(uint32_t ticks)
{
    vTaskDelay(ticks);
    shim_settle();
}

int shim_task_count(void)
{
    pthread_mutex_lock(&K);
    kinit();
    int n = g_ntasks;
    pthread_mutex_unlock(&K);
    return n;
}

/* 1 while another task is blocked with a finite timeout (a handler still waiting for a
 * reply, a retransmission, a delay), i.e. while advancing virtual time can still make
 * progress. A task blocked forever (an idle queue receive) does not count. */
int shim_timed_pending(void)
{
    pthread_mutex_lock(&K);
    kinit();
    int pending = 0;
    struct shim_task *p = g_ring;
    do {
        if (p != g_cur && p->state == T_BLOCKED && p->wake != FOREVER) {
            pending = 1;
        }
        p = p->next;
    } while (p != g_ring);
    pthread_mutex_unlock(&K);
    return pending;
}

void shim_reset_clock(void)
{
    pthread_mutex_lock(&K);
    kinit();
    int pending = 0;
    struct shim_task *p = g_ring;
    do {
        if (p != g_cur && p->state == T_BLOCKED && p->wake != FOREVER) {
            pending = 1;
        }
        p = p->next;
    } while (p != g_ring);
    for (struct shim_timer *t = g_timers; t; t = t->next) {
        if (t->active) {
            pending = 1;
        }
    }
    if (!pending) {
        g_now = 0;
    }
    pthread_mutex_unlock(&K);
}
