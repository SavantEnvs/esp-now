/*
 * Unit wrapper: the module below is compiled unchanged (only #included) so the harnesses can
 * put its file-scope state back to power-on values between fuzz inputs. Resources are
 * released through the module's own stop/free API first; only plain state is reset here.
 */
#include "../../src/time/src/espnow_time.c"

void fuzz_time_reset(void)
{
    g_time_role = ESPNOW_TIME_ROLE_NONE;
    g_time_offset_us = 0;
    g_req_fail_ms = 0;
}
