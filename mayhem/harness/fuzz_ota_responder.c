/*
 * fuzz_ota_responder — the OTA responder (src/ota/espnow_ota_responder.c).
 *
 * espnow_ota_responder_start() registers the OTA_DATA handler; frames then arrive through
 * espnow_recv_cb() like on the device: REQUEST -> info reply, STATUS -> session (packet_num,
 * total_size, progress bitmap, esp_ota_begin, NVS persistence), DATA -> esp_partition_write
 * of attacker-sized packets at attacker-chosen seq. The partition is a RAM buffer.
 *
 * byte 0: bit0 skip_version_check, bits1-7 progress_report_interval.
 */
#include "fuzz_common.h"
#include "espnow_ota.h"

void fuzz_ota_responder_reset(void);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    fz_device_up(false, true);
    espnow_ota_config_t cfg = { .skip_version_check = data[0] & 1, .progress_report_interval = data[0] >> 1 };
    espnow_ota_responder_start(&cfg);
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    shim_sleep(10);
    espnow_ota_responder_stop();
    shim_settle();
    fuzz_ota_responder_reset();
    fz_device_down();
    return 0;
}
