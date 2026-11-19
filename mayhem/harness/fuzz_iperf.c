/*
 * fuzz_iperf — the ESP-NOW iperf responder (src/debug/src/commands/cmd_iperf.c).
 *
 * The device is put in responder mode the way an operator does it, by running the console
 * command `espnow_iperf -s` (plus options chosen by byte 0); espnow_iperf_responder() is then
 * the RESERVED-type handler, and frames arrive through espnow_recv_cb(): bandwidth reports,
 * PING -> PING_ACK echo of the attacker-sized frame, BANDWIDTH_STOP -> STOP_ACK.
 *
 * The remote console is not enabled here (DEBUG_COMMAND frames are fuzz_console's), so the
 * frames reach only the ESP-NOW driver and the iperf handler.
 *
 * The opening seq-0 bandwidth frame a real initiator sends first is delivered by the harness
 * (fuzz_cmd_iperf_prime) so the responder's static counters never carry over between inputs.
 *
 * byte 0: bit0 broadcast replies (-b 1), bit1 ack (-A), bits2-7 report interval (-i).
 */
#include <stdio.h>
#include "fuzz_console_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    fz_console_up(false, false, true);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "espnow_iperf -s -i %u%s%s", data[0] >> 2,
             (data[0] & 1) ? " -b 1" : "", (data[0] & 2) ? " -A" : "");
    int ret = 0;
    if (esp_console_run(cmd, &ret) != ESP_OK || ret != ESP_OK) {
        abort(); /* the responder must start on a fresh device */
    }
    fuzz_cmd_iperf_prime(); /* opening seq-0 frame: resets the responder's static counters */
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    fz_console_down();
    return 0;
}
