/* Host build: the UART console branch of espnow_console_init() is compiled but not taken
 * (monitor_command.uart = false); these declarations only let it compile and link. */
#pragma once
#include "esp_shim.h"
typedef enum { UART_DATA_8_BITS = 3 } uart_word_length_t;
typedef enum { UART_PARITY_DISABLE = 0 } uart_parity_t;
typedef enum { UART_STOP_BITS_1 = 1 } uart_stop_bits_t;
typedef enum { UART_SCLK_REF_TICK = 1, UART_SCLK_XTAL = 2 } uart_sclk_t;
typedef enum { ESP_LINE_ENDINGS_CRLF, ESP_LINE_ENDINGS_CR, ESP_LINE_ENDINGS_LF } esp_line_endings_t;
typedef struct {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_sclk_t source_clk;
} uart_config_t;
esp_err_t uart_driver_install(int port, int rx, int tx, int qsize, void *queue, int flags);
esp_err_t uart_param_config(int port, const uart_config_t *cfg);
void uart_vfs_dev_port_set_rx_line_endings(int port, esp_line_endings_t e);
void uart_vfs_dev_port_set_tx_line_endings(int port, esp_line_endings_t e);
void uart_vfs_dev_use_driver(int port);
