/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/provisioning/src/espnow_prov.c"

void fuzz_prov_reset(void)
{
    if (g_prov_resp) {
        ESP_FREE(g_prov_resp->wifi_config);
        ESP_FREE(g_prov_resp);
    }
    ESP_FREE(g_prov_init);
    ESP_FREE(g_beacon_prov_data);
    g_beacon_stop_tick = 0;
}
