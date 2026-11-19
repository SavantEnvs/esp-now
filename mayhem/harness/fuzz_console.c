/*
 * fuzz_console — the ESP-NOW remote debug console (src/debug/src/espnow_console.c).
 *
 * DEBUG_COMMAND frames arrive through espnow_recv_cb() like on the device and reach
 * console_espnow_handle() -> esp_console_run(): command-line splitting, argtable3 option
 * parsing and the registered command handlers (see fuzz_console_common.h for the list).
 *
 * byte 0: bit0 sec_enable (with a fixed application key installed).
 */
#include "fuzz_console_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1) {
        return -1;
    }
    fz_console_up(data[0] & 1, true, false);
    fz_stream_t s = { data + 1, size - 1 };
    fz_feed_all(&s);
    fz_console_down();
    return 0;
}
