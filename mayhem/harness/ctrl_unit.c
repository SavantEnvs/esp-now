/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/control/src/espnow_ctrl.c"

void fuzz_ctrl_reset(void)
{
    memset(&g_bindlist, 0, sizeof(g_bindlist));
#ifdef CONFIG_ESPNOW_CONTROL_AUTO_CHANNEL_SENDING
    g_saved_channel = 0;
#endif
}
