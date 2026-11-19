/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/ota/espnow_ota_initiator.c"

void fuzz_ota_initiator_reset(void)
{
    espnow_ota_initiator_scan_result_free();
    g_ota_send_running_flag = false;
    g_scan_num = 0;
    g_info_en = false;
}
