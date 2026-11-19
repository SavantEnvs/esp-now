/*
 * Host stand-ins for the remaining ESP-IDF calls of the remote debug console
 * (src/debug/src/espnow_console.c) and its commands (src/debug/src/commands/cmd_espnow.c,
 * cmd_iperf.c). The console itself (esp_console, argtable3, linenoise) is ESP-IDF's real
 * code from /opt/toolchains/deps/esp-idf-console-*; only hardware-facing calls are here.
 *
 * Wi-Fi settings follow esp_wifi's documented argument checks and return codes, so a bad
 * value from a command reaches the command's own error handling (or its ESP_ERROR_CHECK)
 * exactly as on the device.
 */
#include <string.h>
#include "esp_shim.h"
#include "esp_spiffs.h"
#include "esp_http_client.h"
#include "driver/uart.h"
#include "espnow_log.h"

/* ----------------------------------------------------------------- Wi-Fi */
esp_err_t esp_wifi_get_max_tx_power(int8_t *power)
{
    if (!power) {
        return ESP_ERR_INVALID_ARG;
    }
    *power = 80;
    return ESP_OK;
}

esp_err_t esp_wifi_set_max_tx_power(int8_t power)
{
    /* esp_wifi: range [8, 84] in 0.25 dBm units */
    return (power < 8 || power > 84) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap)
{
    if (!protocol_bitmap || ifx > WIFI_IF_AP) {
        return ESP_ERR_INVALID_ARG;
    }
    *protocol_bitmap = 0x7; /* 11B | 11G | 11N */
    return ESP_OK;
}

esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap)
{
    /* 11B 1, 11G 2, 11N 4, LR 8 */
    if (ifx > WIFI_IF_AP || protocol_bitmap == 0 || (protocol_bitmap & ~0xf)) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_set_country(const wifi_country_t *country)
{
    if (!country || country->schan < 1 || country->nchan < 1 || country->schan + country->nchan - 1 > 14) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t esp_now_set_peer_rate_config(const uint8_t *peer_addr, esp_now_rate_config_t *config)
{
    if (!peer_addr || !config || (unsigned)config->phymode > WIFI_PHY_MODE_HT20) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf)
{
    return (!conf || interface > WIFI_IF_AP) ? ESP_ERR_INVALID_ARG : ESP_OK;
}

/* No access point is reachable from the host: connecting starts and never completes. */
esp_err_t esp_wifi_connect(void) { return ESP_OK; }
esp_err_t esp_wifi_disconnect(void) { return ESP_OK; }
esp_err_t esp_wifi_restore(void) { return ESP_OK; }

size_t heap_caps_get_total_size(uint32_t caps)
{
    (void)caps;
    return 320 * 1024;
}

/* ------------------------------------------------------ UART, SPIFFS (unused) */
/* espnow_console_init()'s UART and command-history branches; the harnesses configure neither. */
esp_err_t uart_driver_install(int port, int rx, int tx, int qsize, void *queue, int flags)
{
    (void)port; (void)rx; (void)tx; (void)qsize; (void)queue; (void)flags;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t uart_param_config(int port, const uart_config_t *cfg) { (void)port; (void)cfg; return ESP_ERR_NOT_SUPPORTED; }
void uart_vfs_dev_port_set_rx_line_endings(int port, esp_line_endings_t e) { (void)port; (void)e; }
void uart_vfs_dev_port_set_tx_line_endings(int port, esp_line_endings_t e) { (void)port; (void)e; }
void uart_vfs_dev_use_driver(int port) { (void)port; }
esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf) { (void)conf; return ESP_ERR_NOT_FOUND; }
esp_err_t esp_spiffs_info(const char *label, size_t *total, size_t *used)
{
    (void)label; (void)total; (void)used;
    return ESP_ERR_NOT_FOUND;
}

/* --------------------------------------------------------- HTTP client */
/* No IP link on the host: a client is created, connecting to the server fails. */
struct esp_http_client { int unused; };

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    if (!config || !config->url) {
        return NULL;
    }
    return calloc(1, sizeof(struct esp_http_client));
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len)
{
    (void)client; (void)write_len;
    return ESP_FAIL;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) { (void)client; return -1; }
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len)
{
    (void)client; (void)buffer; (void)len;
    return -1;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t client) { (void)client; return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client)
{
    free(client);
    return ESP_OK;
}

/* ------------------------------------------------------------ espnow_log */
/* Referenced only by cmd_espnow.c's `log` command, which the harnesses do not register
 * (src/debug/src/espnow_log.c needs the flash log partition). */
esp_err_t espnow_log_get_config(espnow_log_config_t *config) { (void)config; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_log_set_config(const espnow_log_config_t *config) { (void)config; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t espnow_log_flash_read(char *data, size_t *size) { (void)data; (void)size; return ESP_ERR_NOT_SUPPORTED; }
size_t espnow_log_flash_size(void) { return 0; }
