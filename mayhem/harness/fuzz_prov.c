/*
 * fuzz_prov — Wi-Fi provisioning over ESP-NOW (src/provisioning/src/espnow_prov.c), both
 * roles: the responder (beacons, device-info requests, sends Wi-Fi config) and the
 * initiator (scan for beacons, send device info, receive Wi-Fi config). Frames arrive
 * through espnow_recv_cb().
 *
 * byte 0: bit0 initiator role (else responder), bit1 the responder callback rejects.
 */
#include "fuzz_common.h"
#include "espnow_prov.h"

void fuzz_prov_reset(void);

static volatile uint32_t g_sink;
static bool g_accept;

static esp_err_t prov_cb(uint8_t *src_addr, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx_ctrl)
{
    uint32_t acc = src_addr[0];
    for (size_t i = 0; i < size; i++) {
        acc += ((uint8_t *)data)[i];
    }
    g_sink = acc + (rx_ctrl ? rx_ctrl->rssi : 0);
    return g_accept ? ESP_OK : ESP_FAIL;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    uint8_t opt = data[0];
    g_accept = !(opt & 2);
    fz_device_up(false, true);
    if (opt & 1) {
        fz_radio_t radio;
        fz_radio_start(&radio, data + 1, size - 1);
        espnow_addr_t responder;
        espnow_prov_responder_t info;
        wifi_pkt_rx_ctrl_t rx;
        if (espnow_prov_initiator_scan(responder, &info, &rx, pdMS_TO_TICKS(500)) == ESP_OK) {
            uint8_t buf[sizeof(espnow_prov_initiator_t) + 4] = { 0 };
            espnow_prov_initiator_t *ini = (espnow_prov_initiator_t *)buf;
            strcpy(ini->product_id, "fuzz_product");
            strcpy(ini->device_name, "fuzz_device");
            ini->auth_mode = ESPNOW_PROV_AUTH_PRODUCT;
            strcpy(ini->product_secret, "secret");
            ini->custom_size = 4;
            memcpy(ini->custom_data, "abcd", 4);
            espnow_prov_initiator_send(responder, ini, prov_cb, pdMS_TO_TICKS(500));
        }
        fz_radio_wait(&radio);
    } else {
        espnow_prov_responder_t info = { .product_id = "fuzz_product", .device_name = "fuzz_resp" };
        uint8_t buf[sizeof(espnow_prov_wifi_t) + 8] = { 0 };
        espnow_prov_wifi_t *wifi = (espnow_prov_wifi_t *)buf;
        wifi->mode = WIFI_MODE_STA;
        strcpy((char *)wifi->sta.ssid, "fuzz_ap");
        strcpy((char *)wifi->sta.password, "fuzz_password");
        strcpy(wifi->token, "token");
        wifi->custom_size = 8;
        memcpy(wifi->custom_data, "custom!!", 8);
        espnow_prov_responder_start(&info, pdMS_TO_TICKS(3000), wifi, prov_cb);
        fz_stream_t s = { data + 1, size - 1 };
        fz_feed_all(&s);
    }
    shim_sleep(4000); /* let the responder beacon session expire */
    fuzz_prov_reset();
    fz_device_down();
    return 0;
}
