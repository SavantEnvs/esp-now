/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/ota/espnow_ota_responder.c"

void fuzz_ota_responder_reset(void)
{
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_OTA_DATA, 0, NULL);
    ESP_FREE(g_ota_config);
    ESP_FREE(g_espnow_ota_config);
    g_ota_finished_flag = false;
    g_ota_last_begin_time = 0;
}
