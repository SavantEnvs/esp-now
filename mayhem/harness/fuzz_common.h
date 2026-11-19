/*
 * Shared driver for the esp-now harnesses: brings the emulated device up, turns the fuzz
 * input into radio frames delivered through the real ESP-NOW receive callback
 * (espnow_recv_cb, registered by espnow_init), and tears everything down again.
 *
 * Input layout (all targets):
 *   byte 0          option bits for the target (meaning documented per harness)
 *   then frames:    [delay][rssi][len] + len bytes of raw ESP-NOW frame (espnow_data_t + payload)
 *                   delay = virtual ms to wait before the frame arrives, rssi = int8 dBm.
 * Each frame is handed over in an exact-size heap buffer so ASan sees reads past its end.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "espnow.h"
#include "espnow_storage.h"

void fuzz_espnow_reset_statics(void);

/* The (MAC-layer) address frames come from; the espnow src_addr inside a frame is fuzzed. */
static const uint8_t FZ_PEER_MAC[6] = { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x33 };

typedef struct {
    const uint8_t *p;
    size_t n;
} fz_stream_t;

/* Next frame, or 0 when the input is used up. A frame whose length runs past the end of
 * the input is cut to what is left. */
static int fz_next_frame(fz_stream_t *s, uint8_t *delay, int8_t *rssi, const uint8_t **frame, size_t *len)
{
    if (s->n < 3) {
        return 0;
    }
    *delay = s->p[0];
    *rssi = (int8_t)s->p[1];
    size_t l = s->p[2];
    s->p += 3;
    s->n -= 3;
    if (l > s->n) {
        l = s->n;
    }
    *frame = s->p;
    *len = l;
    s->p += l;
    s->n -= l;
    return 1;
}

static void fz_deliver(const uint8_t *frame, size_t len, int8_t rssi)
{
    uint8_t *copy = malloc(len ? len : 1);
    memcpy(copy, frame, len);
    shim_deliver(FZ_PEER_MAC, copy, len, rssi, 1);
    free(copy);
}

/* Deliver every frame of the stream from the calling task, letting the device run between them. */
static void fz_feed_all(fz_stream_t *s)
{
    uint8_t delay;
    int8_t rssi;
    const uint8_t *f;
    size_t len;
    while (fz_next_frame(s, &delay, &rssi, &f, &len)) {
        if (delay) {
            shim_sleep(delay);
        }
        fz_deliver(f, len, rssi);
        shim_settle();
    }
}

/* A "radio" task that delivers the frames over virtual time while the harness's own task is
 * blocked in an initiator API (scan/send/handshake waiting for responses). */
typedef struct {
    fz_stream_t s;
    volatile int done;
} fz_radio_t;

static void fz_radio_task(void *arg)
{
    fz_radio_t *r = arg;
    uint8_t delay;
    int8_t rssi;
    const uint8_t *f;
    size_t len;
    while (fz_next_frame(&r->s, &delay, &rssi, &f, &len)) {
        vTaskDelay(delay + 1);
        fz_deliver(f, len, rssi);
    }
    r->done = 1;
    vTaskDelete(NULL);
}

static void fz_radio_start(fz_radio_t *r, const uint8_t *p, size_t n)
{
    r->s.p = p;
    r->s.n = n;
    r->done = 0;
    xTaskCreate(fz_radio_task, "radio", 4096, r, 5, NULL);
}

static void fz_radio_wait(fz_radio_t *r)
{
    while (!r->done) {
        shim_sleep(1);
    }
    shim_settle();
}

static const uint8_t FZ_APP_KEY[32] = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
};

/* Bring the device up: fresh emulated hardware, power-on driver state, espnow_init(). */
static void fz_device_up(bool sec_enable, bool forward_enable)
{
    shim_reset();
    fuzz_espnow_reset_statics();
    espnow_storage_init();
    espnow_config_t cfg = ESPNOW_INIT_CONFIG_DEFAULT();
    cfg.sec_enable = sec_enable;
    cfg.forward_enable = forward_enable;
    if (espnow_init(&cfg) != ESP_OK) {
        abort();
    }
    shim_settle();
    if (sec_enable) {
        uint8_t key[32];
        memcpy(key, FZ_APP_KEY, sizeof(key));
        espnow_set_key(key);
        memcpy(key, FZ_APP_KEY, sizeof(key));
        espnow_set_dec_key(key);
    }
}

/* Tear the device down so the next input starts from the same state. */
static void fz_device_down(void)
{
    shim_settle();
    espnow_deinit();
    shim_settle();
    espnow_group_t groups[CONFIG_ESPNOW_GROUP_COUNT_MAX + 4];
    int n = espnow_get_group_num();
    if (n > 0) {
        if (n > (int)(sizeof(groups) / sizeof(groups[0]))) {
            n = sizeof(groups) / sizeof(groups[0]);
        }
        espnow_get_group_list(groups, n);
        for (int i = 0; i < n; i++) {
            espnow_del_group(groups[i]);
        }
    }
}
