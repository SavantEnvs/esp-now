/*
 * fuzz_sec_initiator — the security handshake initiator (src/security/src/
 * espnow_security_initiator.c + protocomm/security/client_security1.c).
 *
 * The harness task runs espnow_sec_initiator_scan() then espnow_sec_initiator_start() towards
 * two responders while a "radio" task delivers the fuzz frames (their SECURITY_STATUS info
 * replies and SECURITY handshake answers, SessionData protobufs) through espnow_recv_cb().
 * When the radio runs out of frames it calls espnow_sec_initiator_stop().
 *
 * byte 0: bit0 skip the scan, bit1 skip the handshake.
 */
#include "fuzz_common.h"
#include "espnow_security_handshake.h"

void fuzz_sec_initiator_reset(void);

static void stopper_task(void *arg)
{
    fz_radio_t *r = arg;
    while (!r->done) {
        vTaskDelay(1);
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    espnow_sec_initiator_stop();
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
        espnow_sec_responder_t *list = NULL;
        size_t num = 0;
        espnow_sec_initiator_scan(&list, &num, pdMS_TO_TICKS(200));
        espnow_sec_initiator_scan_result_free();
    }
    if (!(opt & 2)) {
        static const uint8_t addrs[2][6] = {
            { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x33 },
            { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x34 },
        };
        uint8_t key[APP_KEY_LEN];
        memcpy(key, FZ_APP_KEY, sizeof(key));
        espnow_sec_result_t res = { 0 };
        espnow_sec_initiator_start(key, "espnow_pop", addrs, 2, &res);
        espnow_sec_initiator_result_free(&res);
    }
    fz_radio_wait(&radio);
    shim_sleep(100);
    fuzz_sec_initiator_reset();
    fz_device_down();
    return 0;
}
