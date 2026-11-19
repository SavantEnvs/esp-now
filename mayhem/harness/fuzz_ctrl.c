/*
 * fuzz_ctrl — the control layer (src/control/src/espnow_ctrl.c): bind/unbind requests and
 * the bind list (NVS-backed), control data to bound devices (attribute/value), the raw
 * control receive path, and the initiator's ACK handling.
 *
 * byte 0: bit0 use espnow_ctrl_recv() (raw callback) instead of espnow_ctrl_responder_data(),
 *         bit1 also run espnow_ctrl_initiator_bind() while the frames arrive (ACK path),
 *         bit2 the bind callback rejects.
 */
#include "fuzz_common.h"
#include "espnow_ctrl.h"

void fuzz_ctrl_reset(void);

static bool g_bind_accept;
static volatile uint32_t g_sink;

static bool bind_cb(espnow_attribute_t initiator_attribute, uint8_t mac[6], int8_t rssi)
{
    g_sink += initiator_attribute + mac[5] + (uint8_t)rssi;
    return g_bind_accept;
}

static void data_cb(espnow_attribute_t initiator_attribute, espnow_attribute_t responder_attribute, uint32_t value)
{
    g_sink += initiator_attribute + responder_attribute + value;
}

static void raw_cb(espnow_addr_t src_addr, espnow_ctrl_data_t *d, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    g_sink += src_addr[0] + d->initiator_attribute + d->responder_attribute + d->responder_value_i + rx_ctrl->rssi;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    uint8_t opt = data[0];
    fz_device_up(false, true);
    espnow_ctrl_responder_clear_bindlist();
    g_bind_accept = !(opt & 4);
    espnow_ctrl_responder_bind(30 * 1000, -55, bind_cb);
    if (opt & 1) {
        espnow_ctrl_recv(raw_cb);
    } else {
        espnow_ctrl_responder_data(data_cb);
    }
    if (opt & 2) {
        fz_radio_t radio;
        fz_radio_start(&radio, data + 1, size - 1);
        espnow_ctrl_initiator_bind(ESPNOW_ATTRIBUTE_KEY_1, true);
        fz_radio_wait(&radio);
    } else {
        fz_stream_t s = { data + 1, size - 1 };
        fz_feed_all(&s);
    }
    shim_sleep(10);
    espnow_ctrl_responder_clear_bindlist();
    fuzz_ctrl_reset();
    fz_device_down();
    return 0;
}
