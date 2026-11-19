/*
 * Seed generator for the esp-now harnesses (run by hand; its output is committed under
 * mayhem/<target>/testsuite/). Frames are built with the component's own structs so the
 * packed bit-field layout matches the code under test.
 *   cc -Imayhem/shim/include -Isrc/... mayhem/tools/gen_seeds.c -o gen && ./gen mayhem
 */
#include <stdio.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "espnow.h"
#include "espnow_ota.h"
#include "espnow_ctrl.h"
#include "espnow_prov.h"
#include "espnow_time.h"
#include "espnow_security_handshake.h"
#include "session.pb-c.h"

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

typedef struct { uint8_t buf[8192]; size_t n; } seed_t;

static void s_opt(seed_t *s, uint8_t opt) { s->n = 0; s->buf[s->n++] = opt; }

/* append one [delay][rssi][len] frame */
static void s_frame(seed_t *s, uint8_t delay, int type, const uint8_t *dest, espnow_frame_head_t head,
                    const void *payload, size_t plen)
{
    uint8_t f[256] = { 0 };
    wire_t *w = (wire_t *)f;
    w->type = type;
    w->version = 2;
    w->size = (uint8_t)plen;
    w->frame_head = head;
    memcpy(w->dest_addr, dest, 6);
    memcpy(w->src_addr, PEER, 6);
    memcpy(w->payload, payload, plen);
    size_t len = sizeof(wire_t) + plen;
    s->buf[s->n++] = delay;
    s->buf[s->n++] = (uint8_t)(int8_t)-40;
    s->buf[s->n++] = (uint8_t)len;
    memcpy(s->buf + s->n, f, len);
    s->n += len;
}

static void s_write(const char *root, const char *target, const char *name, const seed_t *s)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/%s", root, target);
    mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/%s/testsuite", root, target);
    mkdir(p, 0755);
    snprintf(p, sizeof(p), "%s/%s/testsuite/%s", root, target, name);
    FILE *f = fopen(p, "wb");
    fwrite(s->buf, 1, s->n, f);
    fclose(f);
}

static espnow_frame_head_t head(uint16_t magic)
{
    espnow_frame_head_t h = { 0 };
    h.magic = magic;
    h.broadcast = 1;
    h.retransmit_count = 1;
    return h;
}

static void core(const char *root)
{
    const char *T = "fuzz_espnow_recv";
    seed_t s;
    espnow_frame_head_t h;
    static const char msg[] = "hello esp-now";

    s_opt(&s, 2);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_DATA, BCAST, head(0x1111), msg, sizeof(msg));
    h = head(0x1112); h.ack = 1; h.broadcast = 0;
    s_frame(&s, 1, ESPNOW_DATA_TYPE_DATA, SELF, h, msg, sizeof(msg));
    s_frame(&s, 1, ESPNOW_DATA_TYPE_DATA, SELF, h, msg, sizeof(msg)); /* duplicate */
    s_write(root, T, "data_ack_dup", &s);

    /* GROUP: type, group_id[6], addrs_num, addrs_list[][6] */
    uint8_t g[1 + 6 + 1 + 12] = { 1, 'G', 'R', 'P', 0, 0, 2, 2 };
    memcpy(g + 8, PEER, 6);
    memcpy(g + 14, SELF, 6);
    s_opt(&s, 2);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_GROUP, BCAST, head(0x2001), g, sizeof(g));
    h = head(0x2002); h.group = 1;
    uint8_t gid[6] = { 'G', 'R', 'P', 0, 0, 2 };
    s_frame(&s, 1, ESPNOW_DATA_TYPE_DATA, gid, h, msg, sizeof(msg));
    s_write(root, T, "group_join_then_data", &s);

    /* relayed broadcast */
    s_opt(&s, 2);
    h = head(0x3001); h.forward_ttl = 3; h.forward_rssi = -90;
    uint8_t other[6] = { 0x10, 0x20, 0x30, 0x40, 0x50, 0x60 };
    s_frame(&s, 0, ESPNOW_DATA_TYPE_DATA, other, h, msg, sizeof(msg));
    s_frame(&s, 0, ESPNOW_DATA_TYPE_FORWARD, BCAST, h, msg, sizeof(msg));
    s_write(root, T, "forward", &s);

    /* ack frame for us, and a frame with security set on a sec-enabled device */
    s_opt(&s, 3);
    h = head(0x4001); h.broadcast = 0;
    s_frame(&s, 0, ESPNOW_DATA_TYPE_ACK, SELF, h, NULL, 0);
    h = head(0x4002); h.security = 1;
    uint8_t enc[13 + 4 + 8] = { 0 };
    s_frame(&s, 0, ESPNOW_DATA_TYPE_DATA, BCAST, h, enc, sizeof(enc));
    s_write(root, T, "ack_and_encrypted", &s);
}

static espnow_frame_head_t uhead(uint16_t magic)
{
    espnow_frame_head_t h = head(magic);
    h.broadcast = 0;
    return h;
}

static void ota_responder(const char *root)
{
    const char *T = "fuzz_ota_responder";
    seed_t s;
    uint8_t req = ESPNOW_OTA_TYPE_REQUEST;
    s_opt(&s, 1);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_OTA_DATA, BCAST, head(0x5001), &req, 1);
    s_write(root, T, "request", &s);

    uint8_t buf[256] = { 0 };
    espnow_ota_status_t *st = (espnow_ota_status_t *)buf;
    st->type = ESPNOW_OTA_TYPE_STATUS;
    for (int i = 0; i < ESPNOW_OTA_HASH_LEN; i++) st->sha_256[i] = 0x40 + i;
    st->total_size = 3 * ESPNOW_OTA_PACKET_MAX_SIZE - 10;
    st->packet_num = 3;
    s_opt(&s, 1 | (10 << 1));
    s_frame(&s, 0, ESPNOW_DATA_TYPE_OTA_DATA, SELF, uhead(0x5002), st, sizeof(*st));
    for (int q = 0; q < 3; q++) {
        espnow_ota_packet_t *pk = (espnow_ota_packet_t *)buf;
        memset(buf, 0, sizeof(buf));
        pk->type = ESPNOW_OTA_TYPE_DATA;
        pk->seq = q;
        pk->size = q < 2 ? ESPNOW_OTA_PACKET_MAX_SIZE : ESPNOW_OTA_PACKET_MAX_SIZE - 10;
        memset(pk->data, 0x30 + q, pk->size);
        s_frame(&s, 1, ESPNOW_DATA_TYPE_OTA_DATA, SELF, uhead(0x5010 + q), pk, 4 + pk->size);
    }
    memset(buf, 0, sizeof(buf));
    st->type = ESPNOW_OTA_TYPE_STATUS;
    for (int i = 0; i < ESPNOW_OTA_HASH_LEN; i++) st->sha_256[i] = 0x40 + i;
    st->total_size = 3 * ESPNOW_OTA_PACKET_MAX_SIZE - 10;
    st->packet_num = 3;
    s_frame(&s, 1, ESPNOW_DATA_TYPE_OTA_DATA, SELF, uhead(0x5020), st, sizeof(*st));
    s_write(root, T, "status_data_session", &s);
}

static void ota_initiator(const char *root)
{
    const char *T = "fuzz_ota_initiator";
    seed_t s;
    uint8_t buf[256] = { 0 };
    espnow_ota_info_t *info = (espnow_ota_info_t *)buf;
    info->type = ESPNOW_OTA_TYPE_INFO;
    strcpy(info->app_desc.version, "0.9.0");
    strcpy(info->app_desc.project_name, "peer");
    s_opt(&s, 2);
    s_frame(&s, 5, ESPNOW_DATA_TYPE_OTA_STATUS, SELF, uhead(0x6001), info, sizeof(*info));
    s_write(root, T, "scan_info", &s);

    /* send of 1 KiB (opt bits2-7 = 0): status replies with progress */
    s_opt(&s, 1);
    memset(buf, 0, sizeof(buf));
    espnow_ota_status_t *st = (espnow_ota_status_t *)buf;
    st->type = ESPNOW_OTA_TYPE_STATUS;
    for (int i = 0; i < ESPNOW_OTA_HASH_LEN; i++) st->sha_256[i] = 0xa5 ^ i;
    st->total_size = 1024;
    st->packet_num = (1024 + ESPNOW_OTA_PACKET_MAX_SIZE - 1) / ESPNOW_OTA_PACKET_MAX_SIZE;
    st->error_code = 0;
    st->written_size = 0;
    s_frame(&s, 3, ESPNOW_DATA_TYPE_OTA_STATUS, SELF, uhead(0x6002), st, sizeof(*st) + 1);
    st->written_size = 1024;
    st->error_code = 0x3000 + 100 + 20; /* any nonzero code */
    s_frame(&s, 30, ESPNOW_DATA_TYPE_OTA_STATUS, SELF, uhead(0x6003), st, sizeof(*st) + 1);
    s_write(root, T, "send_status", &s);
}

static size_t pack_session(uint8_t *out, int msg, const uint8_t *pub, const uint8_t *rnd, const uint8_t *verify)
{
    SessionData sd = SESSION_DATA__INIT;
    Sec1Payload p = SEC1_PAYLOAD__INIT;
    SessionCmd0 c0 = SESSION_CMD0__INIT;
    SessionResp0 r0 = SESSION_RESP0__INIT;
    SessionCmd1 c1 = SESSION_CMD1__INIT;
    SessionResp1 r1 = SESSION_RESP1__INIT;
    sd.sec_ver = SEC_SCHEME_VERSION__SecScheme1;
    sd.proto_case = SESSION_DATA__PROTO_SEC1;
    sd.sec1 = &p;
    p.msg = msg;
    switch (msg) {
    case SEC1_MSG_TYPE__Session_Command0:
        p.payload_case = SEC1_PAYLOAD__PAYLOAD_SC0; p.sc0 = &c0;
        c0.client_pubkey.data = (uint8_t *)pub; c0.client_pubkey.len = 32; break;
    case SEC1_MSG_TYPE__Session_Response0:
        p.payload_case = SEC1_PAYLOAD__PAYLOAD_SR0; p.sr0 = &r0;
        r0.device_pubkey.data = (uint8_t *)pub; r0.device_pubkey.len = 32;
        r0.device_random.data = (uint8_t *)rnd; r0.device_random.len = 16; break;
    case SEC1_MSG_TYPE__Session_Command1:
        p.payload_case = SEC1_PAYLOAD__PAYLOAD_SC1; p.sc1 = &c1;
        c1.client_verify_data.data = (uint8_t *)verify; c1.client_verify_data.len = 32; break;
    default:
        p.payload_case = SEC1_PAYLOAD__PAYLOAD_SR1; p.sr1 = &r1;
        r1.device_verify_data.data = (uint8_t *)verify; r1.device_verify_data.len = 32; break;
    }
    return session_data__pack(&sd, out);
}

static void sec_frame(seed_t *s, uint8_t delay, int type, uint16_t magic, const uint8_t *pb, size_t n)
{
    uint8_t buf[256];
    espnow_sec_packet_t *pk = (espnow_sec_packet_t *)buf;
    pk->type = type;
    pk->size = (uint8_t)n;
    memcpy(pk->data, pb, n);
    s_frame(s, delay, ESPNOW_DATA_TYPE_SECURITY, SELF, uhead(magic), pk, sizeof(*pk) + n);
}

static void security(const char *root)
{
    seed_t s;
    uint8_t pub[32], rnd[16], ver[32], pb[200];
    for (int i = 0; i < 32; i++) { pub[i] = 9 + i; ver[i] = 0x77 ^ i; }
    for (int i = 0; i < 16; i++) rnd[i] = 0x11 * i;

    espnow_sec_info_t info = { .type = ESPNOW_SEC_TYPE_REQUEST, .sec_ver = ESPNOW_SEC_VER_V1_0 };
    memcpy(info.client_mac, PEER, 6);
    s_opt(&s, 0);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_SECURITY_STATUS, BCAST, head(0x7001), &info, sizeof(info));
    sec_frame(&s, 1, ESPNOW_SEC_TYPE_HANDSHAKE, 0x7002, pb, pack_session(pb, SEC1_MSG_TYPE__Session_Command0, pub, rnd, ver));
    sec_frame(&s, 1, ESPNOW_SEC_TYPE_HANDSHAKE, 0x7003, pb, pack_session(pb, SEC1_MSG_TYPE__Session_Command1, pub, rnd, ver));
    uint8_t key[2 + 32 + 16] = { ESPNOW_SEC_TYPE_KEY, 48 };
    s_frame(&s, 1, ESPNOW_DATA_TYPE_SECURITY, SELF, uhead(0x7004), key, sizeof(key));
    s_write(root, "fuzz_sec_responder", "handshake", &s);

    info.type = ESPNOW_SEC_TYPE_INFO;
    s_opt(&s, 2);
    s_frame(&s, 5, ESPNOW_DATA_TYPE_SECURITY_STATUS, SELF, uhead(0x7101), &info, sizeof(info));
    s_write(root, "fuzz_sec_initiator", "scan_info", &s);
    s_opt(&s, 1);
    sec_frame(&s, 3, ESPNOW_SEC_TYPE_HANDSHAKE, 0x7102, pb, pack_session(pb, SEC1_MSG_TYPE__Session_Response0, pub, rnd, ver));
    sec_frame(&s, 3, ESPNOW_SEC_TYPE_HANDSHAKE, 0x7103, pb, pack_session(pb, SEC1_MSG_TYPE__Session_Response1, pub, rnd, ver));
    uint8_t kr[1] = { ESPNOW_SEC_TYPE_KEY_RESP };
    s_frame(&s, 3, ESPNOW_DATA_TYPE_SECURITY, SELF, uhead(0x7104), kr, 1);
    s_write(root, "fuzz_sec_initiator", "handshake", &s);
}

static void ctrl(const char *root)
{
    seed_t s;
    uint8_t buf[64] = { 0 };
    espnow_ctrl_data_t *d = (espnow_ctrl_data_t *)buf;
    d->initiator_attribute = ESPNOW_ATTRIBUTE_KEY_1;
    d->responder_attribute = ESPNOW_ATTRIBUTE_POWER;
    d->responder_value_i = 1;
    s_opt(&s, 0);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_CONTROL_BIND, BCAST, head(0x8001), d, sizeof(*d));
    s_frame(&s, 1, ESPNOW_DATA_TYPE_CONTROL_DATA, BCAST, head(0x8002), d, sizeof(*d));
    d->responder_value_i = 0;
    s_frame(&s, 1, ESPNOW_DATA_TYPE_CONTROL_BIND, BCAST, head(0x8003), d, sizeof(*d));
    s_write(root, "fuzz_ctrl", "bind_data_unbind", &s);

    s_opt(&s, 1);
    memset(buf, 0, sizeof(buf));
    d->initiator_attribute = ESPNOW_ATTRIBUTE_KEY_1;
    d->responder_attribute = ESPNOW_ATTRIBUTE_POWER;
    d->responder_value_s_size = 5;
    memcpy(d->responder_value_s, "hello", 5);
    s_frame(&s, 0, ESPNOW_DATA_TYPE_CONTROL_DATA, BCAST, head(0x8101), d, sizeof(*d) + 5);
    s_write(root, "fuzz_ctrl", "raw_string", &s);

    s_opt(&s, 2);
    espnow_frame_head_t h = uhead(0x8201);
    s_frame(&s, 2, ESPNOW_DATA_TYPE_ACK, SELF, h, NULL, 0);
    s_write(root, "fuzz_ctrl", "initiator_ack", &s);
}

static void prov(const char *root)
{
    seed_t s;
    uint8_t buf[256] = { 0 };
    buf[0] = 1; /* ESPNOW_PROV_TYPE_DEVICE */
    espnow_prov_initiator_t *ini = (espnow_prov_initiator_t *)(buf + 1);
    strcpy(ini->product_id, "fuzz_product");
    strcpy(ini->device_name, "dev");
    ini->auth_mode = ESPNOW_PROV_AUTH_PRODUCT;
    ini->custom_size = 3;
    memcpy(ini->custom_data, "xyz", 3);
    s_opt(&s, 0);
    s_frame(&s, 2, ESPNOW_DATA_TYPE_PROV, SELF, uhead(0x9001), buf, 1 + sizeof(*ini) + 3);
    s_write(root, "fuzz_prov", "responder_device", &s);

    memset(buf, 0, sizeof(buf));
    buf[0] = 0; /* BEACON */
    espnow_prov_responder_t *r = (espnow_prov_responder_t *)(buf + 1);
    strcpy(r->product_id, "fuzz_product");
    strcpy(r->device_name, "resp");
    s_opt(&s, 1);
    s_frame(&s, 10, ESPNOW_DATA_TYPE_PROV, BCAST, head(0x9101), buf, 1 + sizeof(*r));
    memset(buf, 0, sizeof(buf));
    buf[0] = 2; /* WIFI */
    espnow_prov_wifi_t *w = (espnow_prov_wifi_t *)(buf + 1);
    w->mode = WIFI_MODE_STA;
    strcpy((char *)w->sta.ssid, "ap");
    strcpy((char *)w->sta.password, "pw123456");
    w->custom_size = 2;
    memcpy(w->custom_data, "cc", 2);
    s_frame(&s, 50, ESPNOW_DATA_TYPE_PROV, SELF, uhead(0x9102), buf, 1 + sizeof(*w) + 2);
    s_write(root, "fuzz_prov", "initiator_scan_wifi", &s);
}

static void timesync(const char *root)
{
    seed_t s;
    espnow_time_packet_t p = { .version = 1, .type = 1, .timestamp_us = 5000000, .utc_time_us = 1767225600LL * 1000000 + 12345 };
    s_opt(&s, 100 << 1);
    s_frame(&s, 1, ESPNOW_DATA_TYPE_TIMESYNC, BCAST, head(0xa001), &p, sizeof(p));
    s_write(root, "fuzz_time", "responder_broadcast", &s);
    p.type = 2;
    s_opt(&s, 1);
    s_frame(&s, 1, ESPNOW_DATA_TYPE_TIMESYNC, BCAST, head(0xa101), &p, sizeof(p));
    s_write(root, "fuzz_time", "initiator_request", &s);
}

/* remote debug console: DEBUG_COMMAND frames carrying console command lines */
static void console(const char *root)
{
    static const char *cmds[][3] = {
        { "help", "espnow_config -i", NULL },
        { "espnow_config -c 6 -C US -p 3", "espnow_config -r 1 -r 2 -r 0 -r 0 -t 40", "espnow_config -C XX" },
        { "control -l", "control -b 1 -t 2 -v 3", "control -c 257 -m 30:ae:a4:11:22:33 -a" },
        { "command ff:ff:ff:ff:ff:ff \"help\"", "command 30:ae:a4:11:22:33,30:ae:a4:11:22:34 \"scan -a\" -a", NULL },
        { "scan -r -60", "ota -f 100", "ota -s 30:ae:a4:11:22:33 -d http://192.168.1.1/fw.bin" },
        { "help espnow_config", "help ota", "ota -d http://192.168.1.1/fw.bin" },
    };
    static const char *names[] = { "help_info", "config_set", "control", "command_relay", "scan_ota", "help_ota_download" };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        seed_t s;
        s_opt(&s, 0);
        for (int j = 0; j < 3 && cmds[i][j]; j++) {
            s_frame(&s, 1, ESPNOW_DATA_TYPE_DEBUG_COMMAND, SELF, head(0xc000 + i * 4 + j), cmds[i][j], strlen(cmds[i][j]) + 1);
        }
        s_write(root, "fuzz_console", names[i], &s);
    }
}

/* iperf responder: RESERVED frames carrying espnow_iperf_data_t { uint32 type, uint32 seq, data } */
static void iperf(const char *root)
{
    struct { uint32_t type, seq; uint8_t data[32]; } __attribute__((packed)) d = { 0 };
    seed_t s;
    s_opt(&s, 1 << 2);
    for (uint32_t q = 0; q < 3; q++) {
        d.type = 0; d.seq = q; /* IPERF_BANDWIDTH */
        s_frame(&s, 200, ESPNOW_DATA_TYPE_RESERVED, SELF, head(0xd000 + q), &d, sizeof(d));
    }
    d.type = 1; d.seq = 3; /* IPERF_BANDWIDTH_STOP */
    s_frame(&s, 1, ESPNOW_DATA_TYPE_RESERVED, SELF, head(0xd010), &d, 8);
    s_write(root, "fuzz_iperf", "bandwidth_stop", &s);
    s_opt(&s, 2);
    d.type = 3; d.seq = 0; /* IPERF_PING */
    s_frame(&s, 1, ESPNOW_DATA_TYPE_RESERVED, SELF, head(0xd020), &d, sizeof(d));
    d.seq = 1;
    s_frame(&s, 1, ESPNOW_DATA_TYPE_RESERVED, SELF, head(0xd021), &d, sizeof(d));
    s_write(root, "fuzz_iperf", "ping", &s);
}

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "mayhem";
    printf("sizeof(espnow_frame_head_t)=%zu sizeof(wire_t)=%zu\n", sizeof(espnow_frame_head_t), sizeof(wire_t));
    core(root);
    ota_responder(root);
    ota_initiator(root);
    security(root);
    ctrl(root);
    prov(root);
    timesync(root);
    console(root);
    iperf(root);
    return 0;
}
