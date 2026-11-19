/*
 * Unit wrapper: src/debug/src/commands/cmd_espnow.c compiled unchanged (only #included).
 * Its register_*() functions allocate argtables into file-scope structs on every call; the
 * harness re-registers the commands for every input (esp_console_deinit() drops the command
 * list), so the previous argtables are freed here first. The `ota` handler's file-scope
 * firmware state (set by `ota -d` and `ota -s`) is put back to power-on values too, so no
 * input inherits a previous input's downloaded image.
 */
#include "../../src/debug/src/commands/cmd_espnow.c"

#define FZ_FREE_ARGS(s) arg_freetable((void **)&(s), sizeof(s) / sizeof(void *)), memset(&(s), 0, sizeof(s))

void fuzz_cmd_espnow_reset(void)
{
    FZ_FREE_ARGS(command_args);
    FZ_FREE_ARGS(scan_args);
    FZ_FREE_ARGS(control_args);
    FZ_FREE_ARGS(ota_args);
    g_ota_data_partition = NULL;
    g_ota_size = 0;
}
