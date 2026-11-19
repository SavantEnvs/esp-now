/*
 * Shared setup for the debug-console targets: the device runs the ESP-NOW remote console
 * (espnow_console_init with monitor_command.espnow, src/debug/src/espnow_console.c), which
 * registers console_espnow_handle() for DEBUG_COMMAND frames; each such frame is NUL-terminated
 * and handed to ESP-IDF's esp_console_run() (real esp_console + argtable3 from
 * /opt/toolchains/deps/esp-idf-console-*), which splits it and runs the matching command.
 *
 * Commands registered (an allow-list, by registration): `help` (espnow_console_init's own),
 * the cmd_espnow.c `command`, `scan`, `control`, `ota` commands and cmd_iperf.c's
 * `espnow_config`. Left out: `provisioning` and `security` (their documented action is
 * esp_restart(), which ends the process), `beacon`, `log` and `sec_test` (registered only
 * together with those, through register_espnow()), the cmd_system/cmd_wifi/cmd_peripherals/
 * cmd_sdcard commands (Wi-Fi station, GPIO, SD card, deep sleep: hardware with no host
 * stand-in), and `espnow_iperf` on the remote console: its initiator mode sends for 60 s
 * without ever blocking, and host sends take no virtual time, so the session never ends.
 * fuzz_iperf runs `espnow_iperf` itself (responder mode, from the harness, not the radio).
 */
#pragma once
#include "fuzz_common.h"
#include "espnow_console.h"
#include "espnow_cmd.h"

/* public in cmd_espnow.c, not declared in espnow_cmd.h */
void register_command(void);
void register_scan(void);
void register_control(void);
void register_ota(void);
void register_espnow_config(void);
void register_espnow_iperf(void);

void fuzz_console_reset(void);
void fuzz_console_cmds_reset(void);
void fuzz_cmd_espnow_reset(void);
void fuzz_cmd_iperf_reset(void);
void fuzz_cmd_iperf_prime(void);

/* remote: run DEBUG_COMMAND frames as console commands (monitor_command.espnow);
 * iperf: also register `espnow_iperf` (see above) */
static void fz_console_up(bool sec_enable, bool remote, bool iperf)
{
    fz_device_up(sec_enable, true);
    espnow_console_config_t cfg = { .monitor_command.espnow = remote };
    if (espnow_console_init(&cfg) != ESP_OK) {
        abort();
    }
    register_command();
    register_scan();
    register_control();
    register_ota();
    register_espnow_config();
    if (iperf) {
        register_espnow_iperf();
    }
    shim_settle();
}

static void fz_console_down(void)
{
    int ret;
    /* end an iperf session the input started, through the command itself */
    esp_console_run("espnow_iperf -a", &ret);
    /* A command runs inside the ESP-NOW task (console_espnow_handle) and may still be waiting
     * for replies or retransmitting; let virtual time run until nothing is waiting on a
     * timeout any more (bounded: 600 virtual seconds), so the teardown below never frees the
     * console or the driver under a running command. */
    for (int i = 0; i < 6000 && shim_timed_pending(); i++) {
        shim_sleep(100);
    }
    shim_settle();
    espnow_console_deinit();
    fuzz_console_reset();
    fuzz_console_cmds_reset();
    fuzz_cmd_espnow_reset();
    fuzz_cmd_iperf_reset();
    fz_device_down();
}
