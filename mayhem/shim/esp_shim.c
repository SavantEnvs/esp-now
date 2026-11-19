/*
 * Host stand-ins for the ESP-IDF services esp-now calls: Wi-Fi/ESP-NOW driver, NVS, OTA
 * partitions, events, timers, RNG. They keep deterministic RAM state that shim_reset()
 * clears between fuzz inputs.
 */
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include <mbedtls/sha256.h>

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);
ESP_EVENT_DEFINE_BASE(IP_EVENT);

const uint8_t shim_self_mac[6] = { 0x24, 0x0a, 0xc4, 0x00, 0x00, 0x01 };

void shim_reset_clock(void);

/* ------------------------------------------------------------------ errors */
const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK: return "ESP_OK";
    case ESP_FAIL: return "ESP_FAIL";
    case ESP_ERR_NO_MEM: return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG: return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE: return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE: return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND: return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_TIMEOUT: return "ESP_ERR_TIMEOUT";
    default: return "ESP_ERR_UNKNOWN";
    }
}

void shim_abort_on_error(esp_err_t rc, const char *file, int line, const char *expr)
{
    fprintf(stderr, "ESP_ERROR_CHECK failed: esp_err_t 0x%x (%s) at %s:%d\nexpression: %s\n",
            rc, esp_err_to_name(rc), file, line, expr);
    abort();
}

static int g_log_on = -1;

void shim_log(int level, const char *tag, const char *fmt, ...)
{
    if (g_log_on < 0) {
        g_log_on = getenv("ESPNOW_SHIM_LOG") != NULL;
    }
    if (!g_log_on) {
        return;
    }
    static const char lv[] = "NEWIDV";
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%c (%s) ", lv[level < 6 ? level : 0], tag);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

uint32_t esp_log_timestamp(void) { return xTaskGetTickCount(); }
uint32_t esp_get_free_heap_size(void) { return 200 * 1024; }
uint32_t esp_get_minimum_free_heap_size(void) { return 150 * 1024; }

/* --------------------------------------------------------------- random */
static uint64_t g_rng = 0x9e3779b97f4a7c15ULL;

uint32_t esp_random(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}

void esp_fill_random(void *buf, size_t len)
{
    uint8_t *p = buf;
    for (size_t i = 0; i < len; i++) {
        p[i] = (uint8_t)esp_random();
    }
}

void esp_restart(void)
{
    fprintf(stderr, "esp_restart() called\n");
    abort();
}

uint32_t esp_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

uint32_t esp_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int k = 0; k < 8; k++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : crc >> 1;
        }
    }
    return (uint16_t)~crc;
}

uint8_t esp_crc8_le(uint8_t crc, const uint8_t *buf, uint32_t len)
{
    crc = ~crc;
    while (len--) {
        crc ^= *buf++;
        for (int k = 0; k < 8; k++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8c : crc >> 1;
        }
    }
    return (uint8_t)~crc;
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type)
{
    memcpy(mac, shim_self_mac, 6);
    mac[5] += (uint8_t)type;
    return ESP_OK;
}

esp_err_t esp_light_sleep_start(void) { return ESP_OK; }
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t us) { (void)us; return ESP_OK; }
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t s) { (void)s; return ESP_OK; }

/* ------------------------------------------------------------- wall clock */
static int64_t g_wall_offset_us;  /* settimeofday() adjustment */
#define SHIM_EPOCH_US (1767225600LL * 1000000LL)  /* 2026-01-01 */

int shim_gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    int64_t us = SHIM_EPOCH_US + g_wall_offset_us + (int64_t)xTaskGetTickCount() * 1000;
    tv->tv_sec = us / 1000000;
    tv->tv_usec = us % 1000000;
    return 0;
}

int shim_settimeofday(const struct timeval *tv, const void *tz)
{
    (void)tz;
    if (!tv) {
        return 0;
    }
    g_wall_offset_us = (int64_t)tv->tv_sec * 1000000 + tv->tv_usec - SHIM_EPOCH_US - (int64_t)xTaskGetTickCount() * 1000;
    return 0;
}

time_t shim_time(time_t *t)
{
    struct timeval tv;
    shim_gettimeofday(&tv, NULL);
    if (t) {
        *t = tv.tv_sec;
    }
    return tv.tv_sec;
}

/* mbedTLS seeds its DRBG from the OS; give it the device's deterministic RNG instead
 * (on the ESP32 the entropy comes from the hardware RNG behind esp_random). */
int mbedtls_entropy_func(void *data, unsigned char *output, size_t len)
{
    (void)data;
    esp_fill_random(output, len);
    return 0;
}

/* -------------------------------------------------------------- esp_timer */
int64_t esp_timer_get_time(void) { return (int64_t)xTaskGetTickCount() * 1000; }

struct shim_esp_timer {
    esp_timer_create_args_t args;
    TimerHandle_t t;
};

static void esp_timer_trampoline(TimerHandle_t t)
{
    struct shim_esp_timer *e = pvTimerGetTimerID(t);
    e->args.callback(e->args.arg);
}

esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out)
{
    struct shim_esp_timer *e = calloc(1, sizeof(*e));
    e->args = *args;
    e->t = xTimerCreate(args->name, 1, 0, e, esp_timer_trampoline);
    *out = e;
    return ESP_OK;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t e, uint64_t us)
{
    vTimerSetReloadMode(e->t, 0);
    xTimerChangePeriod(e->t, (TickType_t)(us / 1000 ? us / 1000 : 1), 0);
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t e, uint64_t us)
{
    vTimerSetReloadMode(e->t, 1);
    xTimerChangePeriod(e->t, (TickType_t)(us / 1000 ? us / 1000 : 1), 0);
    return ESP_OK;
}

esp_err_t esp_timer_stop(esp_timer_handle_t e) { xTimerStop(e->t, 0); return ESP_OK; }

esp_err_t esp_timer_delete(esp_timer_handle_t e)
{
    xTimerDelete(e->t, 0);
    free(e);
    return ESP_OK;
}

/* ----------------------------------------------------------------- events */
#define SHIM_HANDLERS 16
static struct { esp_event_base_t base; int32_t id; esp_event_handler_t h; void *arg; } g_handlers[SHIM_HANDLERS];

esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t h, void *arg)
{
    for (int i = 0; i < SHIM_HANDLERS; i++) {
        if (!g_handlers[i].h) {
            g_handlers[i].base = base;
            g_handlers[i].id = id;
            g_handlers[i].h = h;
            g_handlers[i].arg = arg;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t h)
{
    for (int i = 0; i < SHIM_HANDLERS; i++) {
        if (g_handlers[i].h == h && g_handlers[i].base == base && g_handlers[i].id == id) {
            g_handlers[i].h = NULL;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

/* Events are recorded for the harnesses/tests; registered handlers run synchronously
 * (the default event loop would run them in its own task, with a copy of the data). */
uint32_t shim_event_count;
int32_t shim_event_last_id = -1;

esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data, size_t size, uint32_t ticks)
{
    (void)ticks;
    shim_event_count++;
    shim_event_last_id = id;
    void *copy = NULL;
    if (size) {
        copy = malloc(size);
        memcpy(copy, data, size);
    }
    for (int i = 0; i < SHIM_HANDLERS; i++) {
        if (g_handlers[i].h && g_handlers[i].base == base && (g_handlers[i].id == ESP_EVENT_ANY_ID || g_handlers[i].id == id)) {
            g_handlers[i].h(g_handlers[i].arg, base, id, copy);
        }
    }
    free(copy);
    return ESP_OK;
}

/* ------------------------------------------------------------------- wifi */
static uint8_t g_channel = 1;

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6])
{
    memcpy(mac, shim_self_mac, 6);
    mac[5] += (uint8_t)ifx;
    return ESP_OK;
}

esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second)
{
    *primary = g_channel;
    if (second) {
        *second = WIFI_SECOND_CHAN_NONE;
    }
    return ESP_OK;
}

esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second)
{
    (void)second;
    if (primary < 1 || primary > 14) {
        return ESP_ERR_INVALID_ARG;
    }
    g_channel = primary;
    return ESP_OK;
}

esp_err_t esp_wifi_get_country(wifi_country_t *c)
{
    memset(c, 0, sizeof(*c));
    memcpy(c->cc, "CN", 3);
    c->schan = 1;
    c->nchan = 13;
    c->policy = WIFI_COUNTRY_POLICY_AUTO;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mode(wifi_mode_t *mode) { *mode = WIFI_MODE_STA; return ESP_OK; }
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) { (void)ap; return ESP_FAIL; /* not connected */ }
esp_err_t esp_wifi_force_wakeup_acquire(void) { return ESP_OK; }
esp_err_t esp_wifi_force_wakeup_release(void) { return ESP_OK; }

esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf)
{
    (void)interface;
    memset(conf, 0, sizeof(*conf));
    return ESP_OK;
}

/* ---------------------------------------------------------------- esp_now */
static esp_now_recv_cb_t g_recv_cb;
static esp_now_send_cb_t g_send_cb;
static uint8_t g_peers[SHIM_PEER_MAX][6];
static int g_npeers;
static shim_sent_t g_sent[SHIM_SENT_MAX];
static size_t g_nsent;
void (*shim_on_send)(const uint8_t *dest, const uint8_t *data, size_t len);

esp_err_t esp_now_init(void) { return ESP_OK; }

esp_err_t esp_now_deinit(void)
{
    g_npeers = 0;
    return ESP_OK;
}

esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) { g_recv_cb = cb; return ESP_OK; }
esp_err_t esp_now_unregister_recv_cb(void) { g_recv_cb = NULL; return ESP_OK; }
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) { g_send_cb = cb; return ESP_OK; }
esp_err_t esp_now_unregister_send_cb(void) { g_send_cb = NULL; return ESP_OK; }
esp_err_t esp_now_set_pmk(const uint8_t *pmk) { (void)pmk; return ESP_OK; }

static int peer_index(const uint8_t *addr)
{
    for (int i = 0; i < g_npeers; i++) {
        if (!memcmp(g_peers[i], addr, 6)) {
            return i;
        }
    }
    return -1;
}

bool esp_now_is_peer_exist(const uint8_t *addr) { return peer_index(addr) >= 0; }

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer)
{
    if (peer_index(peer->peer_addr) >= 0) {
        return ESP_ERR_ESPNOW_EXIST;
    }
    if (g_npeers >= SHIM_PEER_MAX) {
        return ESP_ERR_ESPNOW_FULL;
    }
    memcpy(g_peers[g_npeers++], peer->peer_addr, 6);
    return ESP_OK;
}

esp_err_t esp_now_mod_peer(const esp_now_peer_info_t *peer)
{
    return peer_index(peer->peer_addr) >= 0 ? ESP_OK : ESP_ERR_ESPNOW_NOT_FOUND;
}

esp_err_t esp_now_del_peer(const uint8_t *addr)
{
    int i = peer_index(addr);
    if (i < 0) {
        return ESP_ERR_ESPNOW_NOT_FOUND;
    }
    memmove(g_peers[i], g_peers[i + 1], (size_t)(g_npeers - i - 1) * 6);
    g_npeers--;
    return ESP_OK;
}

/* The driver copies the frame and reports the result from the Wi-Fi task; here the
 * frame is logged and the send callback runs before esp_now_send returns. */
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len)
{
    static const uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    if (!data || len == 0 || len > 1470) {
        return ESP_ERR_ESPNOW_ARG;
    }
    if (peer_addr && !esp_now_is_peer_exist(peer_addr)) {
        return ESP_ERR_ESPNOW_NOT_FOUND;
    }
    const uint8_t *dest = peer_addr ? peer_addr : bcast;
    shim_sent_t *s = &g_sent[g_nsent % SHIM_SENT_MAX];
    memcpy(s->dest, dest, 6);
    s->len = len;
    memcpy(s->data, data, len);
    g_nsent++;
    if (g_send_cb) {
        g_send_cb(dest, ESP_NOW_SEND_SUCCESS);
    }
    if (shim_on_send) {
        shim_on_send(dest, data, len);
    }
    return ESP_OK;
}

size_t shim_sent_count(void) { return g_nsent; }

const shim_sent_t *shim_sent_get(size_t i)
{
    if (i >= g_nsent || g_nsent - i > SHIM_SENT_MAX) {
        return NULL;
    }
    return &g_sent[i % SHIM_SENT_MAX];
}

void shim_deliver(const uint8_t *src_mac, const uint8_t *data, size_t len, int8_t rssi, uint8_t channel)
{
    if (!g_recv_cb) {
        return;
    }
    wifi_pkt_rx_ctrl_t rx = { 0 };
    rx.rssi = rssi;
    rx.channel = channel & 0x0f;
    uint8_t src[6], des[6];
    memcpy(src, src_mac, 6);
    memcpy(des, shim_self_mac, 6);
    esp_now_recv_info_t info = { .src_addr = src, .des_addr = des, .rx_ctrl = &rx };
    g_recv_cb(&info, data, (int)len);
}

/* -------------------------------------------------------------------- nvs */
typedef struct nvs_ent { char ns[16]; char key[16]; size_t len; uint8_t *val; struct nvs_ent *next; } nvs_ent_t;
static nvs_ent_t *g_nvs;
static char g_nvs_ns[8][16];
static int g_nvs_nns;

esp_err_t nvs_flash_init(void) { return ESP_OK; }

static void nvs_clear(void)
{
    while (g_nvs) {
        nvs_ent_t *e = g_nvs;
        g_nvs = e->next;
        free(e->val);
        free(e);
    }
}

esp_err_t nvs_flash_erase(void) { nvs_clear(); return ESP_OK; }

esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
    (void)mode;
    for (int i = 0; i < g_nvs_nns; i++) {
        if (!strncmp(g_nvs_ns[i], ns, 15)) {
            *out = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    if (g_nvs_nns >= 8 || strlen(ns) > 15) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(g_nvs_ns[g_nvs_nns], 16, "%s", ns);
    *out = (nvs_handle_t)(++g_nvs_nns);
    return ESP_OK;
}

void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }

static nvs_ent_t **nvs_find(nvs_handle_t h, const char *key)
{
    for (nvs_ent_t **pp = &g_nvs; *pp; pp = &(*pp)->next) {
        if (!strcmp((*pp)->ns, g_nvs_ns[h - 1]) && !strncmp((*pp)->key, key, 15)) {
            return pp;
        }
    }
    return NULL;
}

esp_err_t nvs_erase_all(nvs_handle_t h)
{
    for (nvs_ent_t **pp = &g_nvs; *pp;) {
        if (!strcmp((*pp)->ns, g_nvs_ns[h - 1])) {
            nvs_ent_t *e = *pp;
            *pp = e->next;
            free(e->val);
            free(e);
        } else {
            pp = &(*pp)->next;
        }
    }
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    nvs_ent_t **pp = nvs_find(h, key);
    if (!pp) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    nvs_ent_t *e = *pp;
    *pp = e->next;
    free(e->val);
    free(e);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length)
{
    if (strlen(key) > 15 || length > 508000) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_ent_t **pp = nvs_find(h, key);
    nvs_ent_t *e = pp ? *pp : calloc(1, sizeof(*e));
    if (!pp) {
        snprintf(e->ns, 16, "%s", g_nvs_ns[h - 1]);
        snprintf(e->key, 16, "%s", key);
        e->next = g_nvs;
        g_nvs = e;
    }
    free(e->val);
    e->val = malloc(length ? length : 1);
    memcpy(e->val, value, length);
    e->len = length;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    nvs_ent_t **pp = nvs_find(h, key);
    if (!pp) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (!out) {
        *length = (*pp)->len;
        return ESP_OK;
    }
    if (*length < (*pp)->len) {
        *length = (*pp)->len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, (*pp)->val, (*pp)->len);
    *length = (*pp)->len;
    return ESP_OK;
}

/* --------------------------------------------------------- partitions / ota */
#define SHIM_PART_SIZE (CONFIG_ESPNOW_OTA_FIRMWARE_SIZE_MAX)
static esp_partition_t g_part_run = { .type = ESP_PARTITION_TYPE_APP, .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0,
                                      .address = 0x10000, .size = SHIM_PART_SIZE, .erase_size = 4096, .label = "ota_0" };
static esp_partition_t g_part_upd = { .type = ESP_PARTITION_TYPE_APP, .subtype = ESP_PARTITION_SUBTYPE_APP_OTA_0 + 1,
                                      .address = 0x410000, .size = SHIM_PART_SIZE, .erase_size = 4096, .label = "ota_1" };
static uint8_t *g_flash_upd;     /* contents of the update partition (lazily allocated) */
static const esp_partition_t *g_boot = &g_part_run;
static int g_ota_open;
static const esp_app_desc_t g_app_desc = {
    .magic_word = 0xABCD5432, .secure_version = 0, .version = "1.0.0", .project_name = "espnow_fuzz",
    .time = "00:00:00", .date = "Jan  1 2026", .idf_ver = "v5.3.2",
};

const esp_partition_t *esp_ota_get_running_partition(void) { return &g_part_run; }
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *s) { (void)s; return &g_part_upd; }
const esp_partition_t *esp_ota_get_boot_partition(void) { return g_boot; }
const esp_app_desc_t *esp_app_get_description(void) { return &g_app_desc; }
const esp_app_desc_t *esp_ota_get_app_description(void) { return &g_app_desc; }

static uint8_t *upd_flash(void)
{
    if (!g_flash_upd) {
        g_flash_upd = malloc(SHIM_PART_SIZE);
        memset(g_flash_upd, 0xff, SHIM_PART_SIZE);
    }
    return g_flash_upd;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *p, esp_app_desc_t *d)
{
    if (p == &g_part_run) {
        *d = g_app_desc;
        return ESP_OK;
    }
    if (p == &g_part_upd) {
        /* image header (24) + segment header (8) precede the app description */
        memcpy(d, upd_flash() + 32, sizeof(*d));
        return d->magic_word == 0xABCD5432 ? ESP_OK : ESP_ERR_NOT_FOUND;
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t esp_ota_begin(const esp_partition_t *p, size_t image_size, esp_ota_handle_t *out)
{
    if (p != &g_part_upd || (image_size != OTA_SIZE_UNKNOWN && image_size > p->size)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(upd_flash(), 0xff, SHIM_PART_SIZE);
    g_ota_open++;
    *out = (esp_ota_handle_t)g_ota_open;
    return ESP_OK;
}

esp_err_t esp_ota_write(esp_ota_handle_t h, const void *data, size_t size) { (void)h; (void)data; (void)size; return ESP_OK; }
esp_err_t esp_ota_end(esp_ota_handle_t h) { (void)h; return ESP_OK; }
esp_err_t esp_ota_abort(esp_ota_handle_t h) { (void)h; return ESP_OK; }
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *p) { g_boot = p; return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { esp_restart(); }
uint32_t esp_efuse_read_secure_version(void) { return 0; }

esp_err_t esp_partition_write(const esp_partition_t *p, size_t off, const void *src, size_t size)
{
    if (p != &g_part_upd) {
        return ESP_ERR_INVALID_ARG;
    }
    if (off > p->size || size > p->size - off) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *f = upd_flash();
    const uint8_t *s = src;
    for (size_t i = 0; i < size; i++) {
        f[off + i] &= s[i];  /* NOR flash: writes only clear bits */
    }
    return ESP_OK;
}

esp_err_t esp_partition_read(const esp_partition_t *p, size_t off, void *dst, size_t size)
{
    if (p != &g_part_upd) {
        memset(dst, 0xff, size);
        return ESP_OK;
    }
    if (off > p->size || size > p->size - off) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(dst, upd_flash() + off, size);
    return ESP_OK;
}

esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t off, size_t size)
{
    if (p == &g_part_upd && off <= p->size && size <= p->size - off) {
        memset(upd_flash() + off, 0xff, size);
    }
    return ESP_OK;
}

esp_err_t esp_partition_get_sha256(const esp_partition_t *p, uint8_t *sha)
{
    memset(sha, 0, 32);
    memcpy(sha, p->label, strlen(p->label));
    return ESP_OK;
}

const esp_partition_t *esp_partition_find_first(esp_partition_type_t t, esp_partition_subtype_t st, const char *label)
{
    (void)t; (void)st; (void)label;
    return NULL;
}

/* ------------------------------------------------------------------- reset */
void shim_reset(void)
{
    g_rng = 0x9e3779b97f4a7c15ULL;
    nvs_clear();
    if (g_flash_upd) {
        memset(g_flash_upd, 0xff, SHIM_PART_SIZE);
    }
    g_boot = &g_part_run;
    g_npeers = 0;
    g_nsent = 0;
    g_channel = 1;
    shim_on_send = NULL;
    shim_event_count = 0;
    shim_event_last_id = -1;
    g_wall_offset_us = 0;
    shim_reset_clock();
}
