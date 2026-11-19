#pragma once
#include "esp_shim.h"
typedef struct esp_http_client *esp_http_client_handle_t;
enum { HTTP_TRANSPORT_UNKNOWN = 0, HTTP_TRANSPORT_OVER_TCP, HTTP_TRANSPORT_OVER_SSL };
typedef struct {
    const char *url;
    const char *cert_pem;
    int timeout_ms;
    bool keep_alive_enable;
    bool skip_cert_common_name_check;
    int transport_type; /* HTTP_TRANSPORT_* */
    int buffer_size;
} esp_http_client_config_t;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int len);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
