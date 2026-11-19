/*
 * Unit wrapper: ESP-IDF's console command registry (components/console/commands.c, from
 * /opt/toolchains/deps/esp-idf-console-*, found through -I) compiled unchanged (only
 * #included). esp_console_register_help_command(), called by every espnow_console_init(),
 * allocates the `help` argtable into a file-scope struct that esp_console_deinit() does not
 * free; the harnesses free it here between inputs.
 */
#include "commands.c"

void fuzz_console_cmds_reset(void)
{
    arg_freetable((void **)&help_args, sizeof(help_args) / sizeof(void *));
    memset(&help_args, 0, sizeof(help_args));
}
