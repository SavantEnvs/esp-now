/*
 * Unit wrapper: src/debug/src/espnow_console.c (the remote debug console) compiled unchanged
 * (only #included), so the harnesses can put its file-scope state back to power-on values
 * between inputs. espnow_console_deinit() is the module's own teardown; this only clears
 * what it leaves behind.
 */
#include <unistd.h> /* fsync(), which ESP-IDF's newlib stdio.h declares */
#include "../../src/debug/src/espnow_console.c"

void fuzz_console_reset(void)
{
    g_running_flag = false;
    g_store_history_filename = NULL;
}
