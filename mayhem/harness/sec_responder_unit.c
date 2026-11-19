/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/security/src/espnow_security_responder.c"

void fuzz_sec_responder_reset(void)
{
    memset(app_key, 0, sizeof(app_key));
    memset(&g_sec_info, 0, sizeof(g_sec_info));
    memset(&g_frame_config, 0, sizeof(g_frame_config));
    g_sec_session_start = 0;
    g_sec_key_installed = false;
}
