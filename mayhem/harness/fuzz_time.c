/*
 * fuzz_time — time synchronisation over ESP-NOW (src/time/src/espnow_time.c): the
 * TIMESYNC receive handler in the responder role (accepts broadcasts, drift check,
 * settimeofday) or the initiator role (answers requests).
 *
 * byte 0: bit0 initiator role (else responder), bits1-7 responder max_drift_ms.
 */
#include "fuzz_common.h"
#include "espnow_time.h"

void fuzz_time_reset(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    uint8_t opt = data[0];
    fz_device_up(false, true);
    if (opt & 1) {
        espnow_time_initiator_config_t cfg = ESPNOW_TIME_INITIATOR_CONFIG_DEFAULT();
        espnow_time_initiator_start(&cfg);
    } else {
        espnow_time_responder_config_t cfg = ESPNOW_TIME_RESPONDER_CONFIG_DEFAULT();
        cfg.max_drift_ms = opt >> 1;
        espnow_time_responder_start(&cfg);
    }
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    shim_sleep(10);
    if (opt & 1) {
        espnow_time_initiator_stop();
    } else {
        espnow_time_responder_stop();
    }
    shim_settle();
    fuzz_time_reset();
    fz_device_down();
    return 0;
}
