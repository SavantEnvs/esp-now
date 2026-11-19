/*
 * fuzz_espnow_recv — the ESP-NOW core receive path.
 *
 * Every frame goes through espnow_recv_cb() (the callback espnow_init registers with the
 * Wi-Fi driver): header checks, the duplicate caches, ACK handling, GROUP membership
 * frames (addrs_list parsing), relaying (forward_ttl), and in the driver task
 * espnow_recv_process() incl. the AEAD decrypt branch, then the application handler.
 *
 * byte 0: bit0 sec_enable (with a fixed application key installed),
 *         bit1 forward_enable, bit2 join group 'G','R','P',0,0,1 up front.
 */
#include "fuzz_common.h"

static volatile uint8_t g_sink;

/* Application handler for every data type: reads the whole payload it is given. */
static esp_err_t consume(uint8_t *src_addr, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    (void)rx_ctrl;
    uint8_t acc = src_addr[0];
    for (size_t i = 0; i < size; i++) {
        acc ^= ((const uint8_t *)data)[i];
    }
    g_sink = acc;
    return ESP_OK;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    uint8_t opt = data[0];
    fz_device_up(opt & 1, (opt >> 1) & 1);
    for (int t = 0; t < ESPNOW_DATA_TYPE_MAX; t++) {
        espnow_set_config_for_data_type(t, true,
                                        (t == ESPNOW_DATA_TYPE_ACK || t == ESPNOW_DATA_TYPE_GROUP) ? NULL : consume);
    }
    if (opt & 4) {
        static const espnow_group_t grp = { 'G', 'R', 'P', 0, 0, 1 };
        espnow_add_group(grp);
    }
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    /* let pending relays/acks finish */
    shim_sleep(100);
    fz_device_down();
    return 0;
}
