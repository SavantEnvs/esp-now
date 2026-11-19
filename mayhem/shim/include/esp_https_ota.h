/* Host build: no network stack. The `ota` console command's HTTP download (esp_http_client)
 * fails to connect, as on a device without an IP link; see console_shim.c. */
#pragma once
#include "esp_http_client.h"
