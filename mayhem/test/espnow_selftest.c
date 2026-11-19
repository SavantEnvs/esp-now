/*
 * espnow_selftest — functional known-answer tests of the esp-now component on the host shim
 * (upstream ships no tests that run off-target). Each check drives the public API and the
 * real receive callback, and asserts on what the component delivers or transmits.
 * Prints "PASS <name>" / "FAIL <name>" per check and "SUMMARY <passed> <failed>".
 */
#include "../harness/fuzz_common.h"
#include "espnow_ota.h"
#include "espnow_ctrl.h"

typedef struct {
    uint8_t type    : 4;
    uint8_t version : 2;
    uint8_t         : 2;
    uint8_t size;
    espnow_frame_head_t frame_head;
    uint8_t dest_addr[6];
    uint8_t src_addr[6];
    uint8_t payload[0];
} __attribute__((packed)) wire_t;

static const uint8_t SELF[6] = { 0x24, 0x0a, 0xc4, 0x00, 0x00, 0x01 };
static const uint8_t PEER[6] = { 0x30, 0xae, 0xa4, 0x11, 0x22, 0x33 };
static const uint8_t BCAST[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static int g_pass, g_fail;
static void check(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    ok ? g_pass++ : g_fail++;
}

/* last payload delivered to the DATA handler */
static uint8_t g_rx[256];
static size_t g_rx_len;
static int g_rx_count;
static uint8_t g_rx_src[6];

static esp_err_t on_data(uint8_t *src, void *data, size_t size, wifi_pkt_rx_ctrl_t *rx)
{
    (void)rx;
    memcpy(g_rx_src, src, 6);
    g_rx_len = size < sizeof(g_rx) ? size : sizeof(g_rx);
    memcpy(g_rx, data, g_rx_len);
    g_rx_count++;
    return ESP_OK;
}

static size_t mk(uint8_t *f, int type, const uint8_t *dest, espnow_frame_head_t h, const void *p, size_t n)
{
    memset(f, 0, sizeof(wire_t) + n);
    wire_t *w = (wire_t *)f;
    w->type = type;
    w->version = 2;
    w->size = (uint8_t)n;
    w->frame_head = h;
    memcpy(w->dest_addr, dest, 6);
    memcpy(w->src_addr, PEER, 6);
    if (n) {
        memcpy(w->payload, p, n);
    }
    return sizeof(wire_t) + n;
}

static void rx(const uint8_t *f, size_t n)
{
    fz_deliver(f, n, -40);
    shim_settle();
}

static espnow_frame_head_t hd(uint16_t magic, int bcast)
{
    espnow_frame_head_t h = { 0 };
    h.magic = magic;
    h.broadcast = bcast;
    h.retransmit_count = 1;
    return h;
}

/* index of the last sent frame of `type`, or -1 */
static long last_sent(int type)
{
    for (long i = (long)shim_sent_count() - 1; i >= 0; i--) {
        const shim_sent_t *s = shim_sent_get(i);
        if (!s) {
            break;
        }
        if (((const wire_t *)s->data)->type == type) {
            return i;
        }
    }
    return -1;
}

static void test_core(void)
{
    uint8_t f[300];
    static const char msg[] = "known payload 123";
    fz_device_up(false, true);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, on_data);

    g_rx_count = 0;
    rx(f, mk(f, ESPNOW_DATA_TYPE_DATA, SELF, hd(0x1001, 0), msg, sizeof(msg)));
    check("data_frame_delivered", g_rx_count == 1 && g_rx_len == sizeof(msg) && !memcmp(g_rx, msg, sizeof(msg))
          && !memcmp(g_rx_src, PEER, 6));

    rx(f, mk(f, ESPNOW_DATA_TYPE_DATA, SELF, hd(0x1001, 0), msg, sizeof(msg)));
    check("duplicate_magic_dropped", g_rx_count == 1);

    mk(f, ESPNOW_DATA_TYPE_DATA, SELF, hd(0x1002, 0), msg, sizeof(msg));
    ((wire_t *)f)->version = 1;
    rx(f, sizeof(wire_t) + sizeof(msg));
    check("wrong_version_dropped", g_rx_count == 1);

    rx(f, 10);
    check("short_frame_dropped", g_rx_count == 1);

    espnow_frame_head_t h = hd(0x1003, 0);
    h.ack = 1;
    size_t before = shim_sent_count();
    rx(f, mk(f, ESPNOW_DATA_TYPE_DATA, SELF, h, msg, sizeof(msg)));
    long a = last_sent(ESPNOW_DATA_TYPE_ACK);
    const wire_t *ack = a >= 0 ? (const wire_t *)shim_sent_get(a)->data : NULL;
    check("ack_sent_for_ack_request", g_rx_count == 2 && shim_sent_count() > before && ack
          && ack->frame_head.magic == 0x1003 && !memcmp(ack->dest_addr, PEER, 6) && !memcmp(ack->src_addr, SELF, 6));

    /* relay: a broadcast for someone else is re-sent with forward_ttl - 1 */
    uint8_t other[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
    h = hd(0x1004, 1);
    h.forward_ttl = 3;
    h.forward_rssi = -90;
    rx(f, mk(f, ESPNOW_DATA_TYPE_DATA, other, h, msg, sizeof(msg)));
    shim_sleep(50);
    long fw = last_sent(ESPNOW_DATA_TYPE_DATA);
    const wire_t *fwd = fw >= 0 ? (const wire_t *)shim_sent_get(fw)->data : NULL;
    check("broadcast_forwarded_ttl_decremented", fwd && fwd->frame_head.magic == 0x1004 && fwd->frame_head.forward_ttl == 2
          && g_rx_count == 2);

    /* GROUP membership */
    uint8_t g[1 + 6 + 1 + 12] = { 1, 'G', 'R', 'P', 0, 0, 7, 2 };
    memcpy(g + 8, other, 6);
    memcpy(g + 14, SELF, 6);
    const uint8_t gid[6] = { 'G', 'R', 'P', 0, 0, 7 };
    rx(f, mk(f, ESPNOW_DATA_TYPE_GROUP, BCAST, hd(0x1005, 1), g, sizeof(g)));
    check("group_join_frame_applied", espnow_is_my_group(gid) && espnow_get_group_num() == 1);

    h = hd(0x1006, 1);
    h.group = 1;
    rx(f, mk(f, ESPNOW_DATA_TYPE_DATA, gid, h, msg, sizeof(msg)));
    check("group_addressed_frame_delivered", g_rx_count == 3);

    g[0] = 0;
    rx(f, mk(f, ESPNOW_DATA_TYPE_GROUP, BCAST, hd(0x1007, 1), g, sizeof(g)));
    check("group_leave_frame_applied", !espnow_is_my_group(gid) && espnow_get_group_num() == 0);

    /* addrs_num claims 5 addresses, the frame holds 2: must be rejected */
    g[0] = 1;
    g[7] = 5;
    rx(f, mk(f, ESPNOW_DATA_TYPE_GROUP, BCAST, hd(0x1008, 1), g, sizeof(g)));
    check("truncated_group_frame_rejected", !espnow_is_my_group(gid));
    fz_device_down();
}

static void test_security(void)
{
    static const char secret[] = "encrypted payload";
    fz_device_up(true, false);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, on_data);
    espnow_frame_head_t h = hd(0x2001, 1);
    h.security = 1;
    espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, secret, sizeof(secret), &h, pdMS_TO_TICKS(100));
    long i = last_sent(ESPNOW_DATA_TYPE_DATA);
    uint8_t f[300];
    size_t n = 0;
    if (i >= 0) {
        n = shim_sent_get(i)->len;
        memcpy(f, shim_sent_get(i)->data, n);
        memcpy(((wire_t *)f)->src_addr, PEER, 6);
    }
    check("encrypted_frame_not_plaintext", n > sizeof(wire_t) + sizeof(secret)
          && !memmem(f, n, secret, sizeof(secret) - 1));
    g_rx_count = 0;
    rx(f, n);
    check("encrypted_frame_decrypted", g_rx_count == 1 && g_rx_len == sizeof(secret) && !memcmp(g_rx, secret, sizeof(secret)));
    ((wire_t *)f)->frame_head.magic ^= 1;
    f[sizeof(wire_t) + 2] ^= 0x40;
    rx(f, n);
    check("tampered_ciphertext_rejected", g_rx_count == 1);
    fz_device_down();
}

void fuzz_ota_responder_reset(void);

static void test_ota_responder(void)
{
    uint8_t f[300];
    fz_device_up(false, true);
    espnow_ota_config_t cfg = { .skip_version_check = true, .progress_report_interval = 10 };
    espnow_ota_responder_start(&cfg);
    uint8_t req = ESPNOW_OTA_TYPE_REQUEST;
    rx(f, mk(f, ESPNOW_DATA_TYPE_OTA_DATA, BCAST, hd(0x3001, 1), &req, 1));
    long i = last_sent(ESPNOW_DATA_TYPE_OTA_STATUS);
    const wire_t *w = i >= 0 ? (const wire_t *)shim_sent_get(i)->data : NULL;
    const espnow_ota_info_t *info = w ? (const espnow_ota_info_t *)w->payload : NULL;
    check("ota_request_answered_with_info", info && info->type == ESPNOW_OTA_TYPE_INFO
          && !strcmp(info->app_desc.project_name, "espnow_fuzz") && !memcmp(w->dest_addr, PEER, 6));

    /* a STATUS whose packet_num does not match total_size is refused */
    uint8_t b[128] = { 0 };
    espnow_ota_status_t *st = (espnow_ota_status_t *)b;
    st->type = ESPNOW_OTA_TYPE_STATUS;
    memset(st->sha_256, 0x42, sizeof(st->sha_256));
    st->total_size = 1000;
    st->packet_num = 99;
    size_t before = shim_sent_count();
    rx(f, mk(f, ESPNOW_DATA_TYPE_OTA_DATA, SELF, hd(0x3002, 0), st, sizeof(*st)));
    check("ota_inconsistent_status_refused", shim_sent_count() == before);

    st->packet_num = (1000 + ESPNOW_OTA_PACKET_MAX_SIZE - 1) / ESPNOW_OTA_PACKET_MAX_SIZE;
    rx(f, mk(f, ESPNOW_DATA_TYPE_OTA_DATA, SELF, hd(0x3003, 0), st, sizeof(*st)));
    espnow_ota_status_t cur = { 0 };
    check("ota_session_started", espnow_ota_responder_get_status(&cur) == ESP_OK && cur.total_size == 1000
          && cur.error_code == ESP_OK && cur.written_size == 0);

    espnow_ota_packet_t pk = { .type = ESPNOW_OTA_TYPE_DATA, .seq = 0, .size = ESPNOW_OTA_PACKET_MAX_SIZE };
    memset(pk.data, 0x5a, sizeof(pk.data));
    rx(f, mk(f, ESPNOW_DATA_TYPE_OTA_DATA, SELF, hd(0x3004, 0), &pk, 4 + pk.size));
    uint8_t flash[ESPNOW_OTA_PACKET_MAX_SIZE];
    esp_partition_read(esp_ota_get_next_update_partition(NULL), 0, flash, sizeof(flash));
    espnow_ota_responder_get_status(&cur);
    check("ota_data_written_to_partition", cur.written_size == ESPNOW_OTA_PACKET_MAX_SIZE && flash[0] == 0x5a
          && flash[ESPNOW_OTA_PACKET_MAX_SIZE - 1] == 0x5a);
    espnow_ota_responder_stop();
    shim_settle();
    fuzz_ota_responder_reset();
    fz_device_down();
}

void fuzz_ctrl_reset(void);
static int g_ctrl_value = -1;
static bool bind_ok(espnow_attribute_t a, uint8_t mac[6], int8_t rssi) { (void)a; (void)mac; (void)rssi; return true; }
static void ctrl_data(espnow_attribute_t ia, espnow_attribute_t ra, uint32_t v) { (void)ia; (void)ra; g_ctrl_value = (int)v; }

static void test_ctrl(void)
{
    uint8_t f[300];
    fz_device_up(false, true);
    espnow_ctrl_responder_clear_bindlist();
    espnow_ctrl_responder_bind(30 * 1000, -55, bind_ok);
    espnow_ctrl_responder_data(ctrl_data);
    espnow_ctrl_data_t d = { .initiator_attribute = ESPNOW_ATTRIBUTE_KEY_1, .responder_attribute = ESPNOW_ATTRIBUTE_POWER };
    d.responder_value_i = 1;
    rx(f, mk(f, ESPNOW_DATA_TYPE_CONTROL_BIND, BCAST, hd(0x4001, 1), &d, sizeof(d)));
    espnow_ctrl_bind_info_t list[4];
    size_t n = 4;
    espnow_ctrl_responder_get_bindlist(list, &n);
    check("ctrl_bind_recorded", n == 1 && !memcmp(list[0].mac, PEER, 6));
    d.responder_value_i = 7;
    rx(f, mk(f, ESPNOW_DATA_TYPE_CONTROL_DATA, BCAST, hd(0x4002, 1), &d, sizeof(d)));
    check("ctrl_data_from_bound_initiator", g_ctrl_value == 7);
    espnow_ctrl_responder_clear_bindlist();
    fuzz_ctrl_reset();
    fz_device_down();
}

int main(void)
{
    test_core();
    test_security();
    test_ota_responder();
    test_ctrl();
    printf("SUMMARY %d %d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
