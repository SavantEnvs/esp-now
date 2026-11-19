/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/security/src/espnow_security_initiator.c"

void fuzz_sec_initiator_reset(void)
{
    espnow_sec_initiator_scan_result_free();
    memset(app_key, 0, sizeof(app_key));
    g_sec_initiator_flag = false;
    g_scan_num = 0;
}
