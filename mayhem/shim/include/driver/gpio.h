/* Host build: GPIOs (iperf's optional timing pin) have no host stand-in; the calls are no-ops. */
#pragma once
#include "esp_shim.h"
typedef int gpio_num_t;
typedef enum { GPIO_MODE_OUTPUT = 2 } gpio_mode_t;
enum { GPIO_NUM_2 = 2, GPIO_NUM_4 = 4, GPIO_NUM_12 = 12, GPIO_NUM_13 = 13, GPIO_NUM_15 = 15 };
static inline esp_err_t gpio_reset_pin(gpio_num_t n) { (void)n; return ESP_OK; }
static inline esp_err_t gpio_set_direction(gpio_num_t n, gpio_mode_t m) { (void)n; (void)m; return ESP_OK; }
static inline esp_err_t gpio_set_level(gpio_num_t n, uint32_t l) { (void)n; (void)l; return ESP_OK; }
