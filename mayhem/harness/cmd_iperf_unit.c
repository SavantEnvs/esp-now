/*
 * Unit wrapper: src/debug/src/commands/cmd_iperf.c compiled unchanged (only #included), so the
 * harnesses can put its file-scope state (session config, the receive-handler owner flags,
 * the argtables re-allocated by every register call) back to power-on values between inputs.
 * A running session is first ended through the module's own `espnow_iperf -a` command by the
 * harness.
 *
 * espnow_iperf_responder() also keeps four function-local static counters (start_time,
 * recv_count, report_time, report_count) that nothing outside the function can reach; the
 * function itself resets all four when a session's first frame (seq == 0) arrives. After
 * `espnow_iperf -s`, fuzz_cmd_iperf_prime() delivers exactly that opening frame, as a real
 * initiator does, so every input starts from the same counters.
 */
#include "../../src/debug/src/commands/cmd_iperf.c"

#define FZ_FREE_ARGS(s) arg_freetable((void **)&(s), sizeof(s) / sizeof(void *)), memset(&(s), 0, sizeof(s))

void fuzz_cmd_iperf_reset(void)
{
    static const struct espnow_iperf_cfg power_on = {
        .finish = true,
        .packet_len      = ESPNOW_DATA_LEN,
        .transmit_time   = 60,
        .report_interval = 3,
        .ping_count      = 64,
        .frame_head.group     = false,
        .frame_head.security  = CONFIG_ESPNOW_DEBUG_SECURITY,
        .type            = ESPNOW_DATA_TYPE_RESERVED,
    };
    g_iperf_cfg = power_on;
    g_iperf_recv_owned = false;
    g_iperf_responder = false;
    g_iperf_sec_required_saved = true;
    FZ_FREE_ARGS(espnow_config_args);
    FZ_FREE_ARGS(espnow_iperf_args);
}

/* Call after `espnow_iperf -s` succeeded (g_iperf_cfg.finish is false): the opening
 * IPERF_BANDWIDTH frame with seq 0 makes espnow_iperf_responder() reset its static counters. */
void fuzz_cmd_iperf_prime(void)
{
    static const uint8_t initiator[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    uint8_t src[6];
    espnow_iperf_data_t first = { .type = IPERF_BANDWIDTH, .seq = 0 };
    wifi_pkt_rx_ctrl_t rx_ctrl = { 0 };
    memcpy(src, initiator, sizeof(src));
    if (g_iperf_cfg.finish
        || espnow_iperf_responder(src, &first, sizeof(first), &rx_ctrl) != ESP_OK) {
        abort(); /* the responder must be running and accept a session's opening frame */
    }
}
