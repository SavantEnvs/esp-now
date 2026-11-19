/*
 * fuzz_sec_responder — the security handshake responder (src/security/src/
 * espnow_security_responder.c) on top of ESP-IDF protocomm security1 (SessionData protobuf,
 * Curve25519, AES-CTR) and the in-band application key delivery.
 *
 * espnow_sec_responder_start("espnow_pop") registers the SECURITY_STATUS/SECURITY/
 * SECURITY_DATA handlers; frames arrive through espnow_recv_cb().
 *
 * byte 0: bit0 sec_enable on the device.
 */
#include "fuzz_common.h"
#include "espnow_security_handshake.h"

void fuzz_sec_responder_reset(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    fz_device_up(data[0] & 1, true);
    espnow_sec_responder_start("espnow_pop");
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    shim_sleep(10);
    espnow_sec_responder_stop();
    shim_settle();
    fuzz_sec_responder_reset();
    fz_device_down();
    return 0;
}
