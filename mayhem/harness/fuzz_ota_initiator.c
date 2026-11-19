/*
 * fuzz_ota_initiator — the OTA initiator (src/ota/espnow_ota_initiator.c).
 *
 * The harness task runs the initiator API as an application would: espnow_ota_initiator_scan()
 * then espnow_ota_initiator_send() of a small firmware to two responders. Meanwhile a
 * "radio" task delivers the fuzz frames (the responders' OTA_STATUS answers: INFO replies,
 * STATUS with progress bitmaps) through espnow_recv_cb(); when it runs out of frames it
 * calls espnow_ota_initiator_stop(), as a user aborting the update would.
 *
 * byte 0: bit0 skip the scan, bit1 skip the send, bits2-7 firmware size in 1 KiB units (+1).
 */
#include "fuzz_common.h"
#include "espnow_ota.h"

void fuzz_ota_initiator_reset(void);

static esp_err_t firmware_read(size_t src_offset, void *dst, size_t size)
{
    uint8_t *d = dst;
    for (size_t i = 0; i < size; i++) {
        d[i] = (uint8_t)(src_offset + i);
    }
    return ESP_OK;
}

typedef struct {
    fz_radio_t radio;
} ctx_t;

static void stopper_task(void *arg)
{
    fz_radio_t *r = arg;
    while (!r->done) {
        vTaskDelay(1);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    espnow_ota_initiator_stop();
    vTaskDelete(NULL);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    uint8_t opt = data[0];
    fz_device_up(false, true);

    fz_radio_t radio;
    fz_radio_start(&radio, data + 1, size - 1);
    xTaskCreate(stopper_task, "stopper", 4096, &radio, 1, NULL);

    if (!(opt & 1)) {
        espnow_ota_responder_t *list = NULL;
        size_t num = 0;
        espnow_ota_initiator_scan(&list, &num, pdMS_TO_TICKS(200));
        espnow_ota_initiator_scan_result_free();
    }
    if (!(opt & 2)) {
        static const uint8_t addrs[2][6] = {
            { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x33 },
            { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x34 },
        };
        uint8_t sha[ESPNOW_OTA_HASH_LEN];
        for (int i = 0; i < ESPNOW_OTA_HASH_LEN; i++) {
            sha[i] = (uint8_t)(0xa5 ^ i);
        }
        espnow_ota_result_t res = { 0 };
        espnow_ota_initiator_send(addrs, 2, sha, ((opt >> 2) + 1) * 1024, firmware_read, &res);
        espnow_ota_initiator_result_free(&res);
    }
    fz_radio_wait(&radio);
    shim_sleep(100); /* the stopper task finishes */
    fuzz_ota_initiator_reset();
    fz_device_down();
    return 0;
}
