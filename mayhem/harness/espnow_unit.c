/*
 * The core ESP-NOW driver (src/espnow/src/espnow.c) compiled as part of this unit, so the
 * harnesses can put its file-scope state back to the power-on values between inputs:
 * the duplicate-frame caches would otherwise drop a frame because an EARLIER input sent
 * the same type+magic, and the cached application keys would leak across inputs.
 * The driver's code is unchanged; it is only #included.
 */
#include "../../src/espnow/src/espnow.c"

void fuzz_espnow_reset_statics(void)
{
    memset(g_msg_magic_cache, 0, sizeof(g_msg_magic_cache));
    memset(g_msg_magic_sec_cache, 0, sizeof(g_msg_magic_sec_cache));
    g_msg_magic_cache_next = 0;
    g_buffered_num = 0;
    g_set_channel_flag = true;
    g_sec_required_types = ESPNOW_SEC_REQUIRED_TYPES_DEFAULT;
    memset(g_espnow_sec_key, 0, sizeof(g_espnow_sec_key));
    memset(g_espnow_dec_key, 0, sizeof(g_espnow_dec_key));
    g_read_from_nvs = true;
    g_read_dec_from_nvs = true;
}
