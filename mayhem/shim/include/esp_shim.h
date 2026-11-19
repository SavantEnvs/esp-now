/*
 * Host shim of the ESP-IDF / FreeRTOS APIs esp-now uses, so the component's
 * sources compile and run unmodified on Linux under libFuzzer.
 *
 * FreeRTOS is emulated by a COOPERATIVE scheduler on pthreads: exactly one task
 * runs at a time, a task switch happens only when the running task blocks or
 * yields, and time is VIRTUAL (it advances only when every task is blocked).
 * Each run is therefore deterministic, and a 10 s protocol timeout costs nothing.
 *
 * Configuration mirrors the Kconfig defaults of the component (see sdkconfig.h).
 */
#pragma once

#include "sdkconfig.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <inttypes.h>
#include <time.h>
#include <sys/queue.h>

#ifndef SLIST_FOREACH_SAFE
#define SLIST_FOREACH_SAFE(var, head, field, tvar) \
    for ((var) = SLIST_FIRST((head)); (var) && ((tvar) = SLIST_NEXT((var), field), 1); (var) = (tvar))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ version */
#define ESP_IDF_VERSION_VAL(major, minor, patch) ((major << 16) | (minor << 8) | (patch))
#define ESP_IDF_VERSION_MAJOR 5
#define ESP_IDF_VERSION_MINOR 3
#define ESP_IDF_VERSION_PATCH 2
#define ESP_IDF_VERSION ESP_IDF_VERSION_VAL(ESP_IDF_VERSION_MAJOR, ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH)

/* -------------------------------------------------------------------- errors */
typedef int esp_err_t;
#define ESP_OK                     0
#define ESP_FAIL                   -1
#define ESP_ERR_NO_MEM             0x101
#define ESP_ERR_INVALID_ARG        0x102
#define ESP_ERR_INVALID_STATE      0x103
#define ESP_ERR_INVALID_SIZE       0x104
#define ESP_ERR_NOT_FOUND          0x105
#define ESP_ERR_NOT_SUPPORTED      0x106
#define ESP_ERR_TIMEOUT            0x107
#define ESP_ERR_INVALID_RESPONSE   0x108
#define ESP_ERR_INVALID_CRC        0x109
#define ESP_ERR_INVALID_VERSION    0x10A
#define ESP_ERR_INVALID_MAC        0x10B
#define ESP_ERR_NOT_FINISHED       0x10C
#define ESP_ERR_NOT_ALLOWED        0x10D
#define ESP_ERR_WIFI_BASE          0x3000
#define ESP_ERR_WIFI_TIMEOUT       (ESP_ERR_WIFI_BASE + 12)
#define ESP_ERR_ESPNOW_BASE        (ESP_ERR_WIFI_BASE + 100)
#define ESP_ERR_ESPNOW_NOT_INIT    (ESP_ERR_ESPNOW_BASE + 1)
#define ESP_ERR_ESPNOW_ARG         (ESP_ERR_ESPNOW_BASE + 2)
#define ESP_ERR_ESPNOW_NO_MEM      (ESP_ERR_ESPNOW_BASE + 3)
#define ESP_ERR_ESPNOW_FULL        (ESP_ERR_ESPNOW_BASE + 4)
#define ESP_ERR_ESPNOW_NOT_FOUND   (ESP_ERR_ESPNOW_BASE + 5)
#define ESP_ERR_ESPNOW_INTERNAL    (ESP_ERR_ESPNOW_BASE + 6)
#define ESP_ERR_ESPNOW_EXIST       (ESP_ERR_ESPNOW_BASE + 7)
#define ESP_ERR_ESPNOW_IF          (ESP_ERR_ESPNOW_BASE + 8)
#define ESP_ERR_NVS_BASE           0x1100
#define ESP_ERR_NVS_NOT_FOUND      (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_NO_FREE_PAGES  (ESP_ERR_NVS_BASE + 0x0d)
#define ESP_ERR_NVS_NEW_VERSION_FOUND (ESP_ERR_NVS_BASE + 0x10)
#define ESP_ERR_OTA_BASE           0x1500
#define ESP_ERR_OTA_VALIDATE_FAILED (ESP_ERR_OTA_BASE + 0x03)

const char *esp_err_to_name(esp_err_t code);
void shim_abort_on_error(esp_err_t rc, const char *file, int line, const char *expr);
#define ESP_ERROR_CHECK(x) do { esp_err_t __rc = (x); if (__rc != ESP_OK) shim_abort_on_error(__rc, __FILE__, __LINE__, #x); } while (0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) ({ esp_err_t __rc = (x); __rc; })

/* ----------------------------------------------------------------------- log */
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO, ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
void shim_log(int level, const char *tag, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
/* As on a default device build (CONFIG_LOG_MAXIMUM_LEVEL=INFO): DEBUG/VERBOSE are compiled out,
 * so their arguments are never evaluated; ERROR/WARN/INFO are evaluated and printed only when
 * ESPNOW_SHIM_LOG is set in the environment. */
#define ESP_LOGE(tag, fmt, ...) shim_log(ESP_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) shim_log(ESP_LOG_WARN, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) shim_log(ESP_LOG_INFO, tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) do { if (0) shim_log(ESP_LOG_DEBUG, tag, fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGV(tag, fmt, ...) do { if (0) shim_log(ESP_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__); } while (0)
#define ESP_EARLY_LOGE ESP_LOGE
#define ESP_EARLY_LOGW ESP_LOGW
#define ESP_EARLY_LOGI ESP_LOGI
#define ESP_LOG_LEVEL(level, tag, fmt, ...) do { if ((level) <= ESP_LOG_INFO) shim_log(level, tag, fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOG_BUFFER_HEXDUMP(tag, buf, len, level) do { (void)(tag); (void)(buf); (void)(len); (void)(level); } while (0)
#define ESP_LOG_BUFFER_HEX(tag, buf, len) do { (void)(tag); (void)(buf); (void)(len); } while (0)
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buf, len, level) do { (void)(tag); (void)(buf); (void)(len); } while (0)
#define ESP_LOG_BUFFER_CHAR_LEVEL(tag, buf, len, level) do { (void)(tag); (void)(buf); (void)(len); } while (0)
#define ESP_LOG_BUFFER_CHAR(tag, buf, len) do { (void)(tag); (void)(buf); (void)(len); } while (0)
static inline void esp_log_level_set(const char *tag, esp_log_level_t level) { (void)tag; (void)level; }
uint32_t esp_log_timestamp(void);
#define LOG_COLOR_I ""
#define LOG_RESET_COLOR ""

/* ------------------------------------------------------------------- misc */
/* idf_component.yml version, which the component's CMake (cu_pkg_define_version) defines */
#define ESP_NOW_VER_MAJOR 2
#define ESP_NOW_VER_MINOR 5
#define ESP_NOW_VER_PATCH 3

#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif
#define BIT0 0x00000001
#define BIT1 0x00000002
#define BIT2 0x00000004
#define BIT3 0x00000008
#define BIT4 0x00000010
#define BIT5 0x00000020
#define BIT6 0x00000040
#define BIT7 0x00000080
#define IRAM_ATTR
#define DRAM_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define EXT_RAM_ATTR
#define __NOINIT_ATTR
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

#define MALLOC_CAP_DEFAULT  (1 << 12)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT     (1 << 2)
static inline void *heap_caps_malloc(size_t s, uint32_t c) { (void)c; return malloc(s); }
static inline void *heap_caps_calloc(size_t n, size_t s, uint32_t c) { (void)c; return calloc(n, s); }
static inline void *heap_caps_realloc(void *p, size_t s, uint32_t c) { (void)c; return realloc(p, s); }
static inline size_t heap_caps_get_free_size(uint32_t c) { (void)c; return 256 * 1024; }
uint32_t esp_get_free_heap_size(void);
static inline bool heap_caps_check_integrity_all(bool p) { (void)p; return true; }
uint32_t esp_get_minimum_free_heap_size(void);

uint32_t esp_random(void);
void esp_fill_random(void *buf, size_t len);
void esp_restart(void) __attribute__((noreturn));
uint32_t esp_crc16_le(uint16_t crc, const uint8_t *buf, uint32_t len);
uint32_t esp_crc32_le(uint32_t crc, const uint8_t *buf, uint32_t len);
uint8_t esp_crc8_le(uint8_t crc, const uint8_t *buf, uint32_t len);

typedef enum { ESP_MAC_WIFI_STA, ESP_MAC_WIFI_SOFTAP, ESP_MAC_BT, ESP_MAC_ETH } esp_mac_type_t;
esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);

/* ------------------------------------------------------------- wall clock
 * The device clock follows the virtual tick count (from a fixed epoch), so a run never
 * depends on the host's real time. */
int shim_gettimeofday(struct timeval *tv, void *tz);
int shim_settimeofday(const struct timeval *tv, const void *tz);
#define gettimeofday(tv, tz) shim_gettimeofday((tv), (tz))
#define settimeofday(tv, tz) shim_settimeofday((tv), (tz))
#define time(t) shim_time(t)
time_t shim_time(time_t *t);

/* ------------------------------------------------------------------ sntp */
#define SNTP_OPMODE_POLL 0
static inline bool sntp_enabled(void) { return false; }
static inline void sntp_setoperatingmode(int m) { (void)m; }
static inline void sntp_setservername(int i, const char *n) { (void)i; (void)n; }
static inline void sntp_init(void) {}
#define esp_sntp_enabled sntp_enabled
#define esp_sntp_setoperatingmode sntp_setoperatingmode
#define esp_sntp_setservername sntp_setservername
#define esp_sntp_init sntp_init
#define ESP_SNTP_OPMODE_POLL SNTP_OPMODE_POLL

/* ----------------------------------------------------------------- sleep */
esp_err_t esp_light_sleep_start(void);
esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
typedef enum { ESP_SLEEP_WAKEUP_ALL = 0, ESP_SLEEP_WAKEUP_TIMER = 4 } esp_sleep_source_t;
esp_err_t esp_sleep_disable_wakeup_source(esp_sleep_source_t source);

/* ----------------------------------------------------------------- timer */
int64_t esp_timer_get_time(void);
typedef struct shim_esp_timer *esp_timer_handle_t;
typedef void (*esp_timer_cb_t)(void *arg);
typedef enum { ESP_TIMER_TASK } esp_timer_dispatch_t;
typedef struct {
    esp_timer_cb_t callback;
    void *arg;
    esp_timer_dispatch_t dispatch_method;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_once(esp_timer_handle_t t, uint64_t timeout_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t t, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t t);
esp_err_t esp_timer_delete(esp_timer_handle_t t);

/* ----------------------------------------------------------------- events */
typedef const char *esp_event_base_t;
#define ESP_EVENT_DECLARE_BASE(id) extern esp_event_base_t const id
#define ESP_EVENT_DEFINE_BASE(id) esp_event_base_t const id = #id
#define ESP_EVENT_ANY_ID -1
typedef void (*esp_event_handler_t)(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
typedef void *esp_event_handler_instance_t;
ESP_EVENT_DECLARE_BASE(WIFI_EVENT);
ESP_EVENT_DECLARE_BASE(IP_EVENT);
esp_err_t esp_event_handler_register(esp_event_base_t base, int32_t id, esp_event_handler_t h, void *arg);
esp_err_t esp_event_handler_unregister(esp_event_base_t base, int32_t id, esp_event_handler_t h);
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data, size_t size, uint32_t ticks);

/* ------------------------------------------------------------------ wifi */
typedef enum { WIFI_MODE_NULL = 0, WIFI_MODE_STA, WIFI_MODE_AP, WIFI_MODE_APSTA, WIFI_MODE_MAX } wifi_mode_t;
typedef enum { WIFI_IF_STA = 0, WIFI_IF_AP = 1 } wifi_interface_t;
typedef enum { WIFI_SECOND_CHAN_NONE = 0, WIFI_SECOND_CHAN_ABOVE, WIFI_SECOND_CHAN_BELOW } wifi_second_chan_t;
typedef enum { WIFI_COUNTRY_POLICY_AUTO, WIFI_COUNTRY_POLICY_MANUAL } wifi_country_policy_t;
typedef enum { WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM } wifi_ps_type_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK } wifi_auth_mode_t;
typedef enum { WIFI_PHY_RATE_1M_L = 0 } wifi_phy_rate_t;
typedef enum { WIFI_PHY_MODE_LR, WIFI_PHY_MODE_11B, WIFI_PHY_MODE_11G, WIFI_PHY_MODE_HT20 } wifi_phy_mode_t;
typedef enum { WIFI_FAST_SCAN = 0, WIFI_ALL_CHANNEL_SCAN } wifi_scan_method_t;
typedef struct {
    wifi_phy_mode_t phymode;
    wifi_phy_rate_t rate;
    bool ersu;
    bool dcm;
} esp_now_rate_config_t;
typedef struct {
    char cc[3];
    uint8_t schan;
    uint8_t nchan;
    int8_t max_tx_power;
    wifi_country_policy_t policy;
} wifi_country_t;
typedef struct {
    signed rssi: 8;
    unsigned rate: 5;
    unsigned : 1;
    unsigned sig_mode: 2;
    unsigned : 16;
    unsigned mcs: 7;
    unsigned cwb: 1;
    unsigned : 16;
    unsigned smoothing: 1;
    unsigned not_sounding: 1;
    unsigned : 1;
    unsigned aggregation: 1;
    unsigned stbc: 2;
    unsigned fec_coding: 1;
    unsigned sgi: 1;
    signed noise_floor: 8;
    unsigned ampdu_cnt: 8;
    unsigned channel: 4;
    unsigned secondary_channel: 4;
    unsigned : 8;
    unsigned timestamp: 32;
    unsigned : 32;
    unsigned : 31;
    unsigned ant: 1;
    unsigned sig_len: 12;
    unsigned : 12;
    unsigned rx_state: 8;
} wifi_pkt_rx_ctrl_t;
typedef struct {
    wifi_pkt_rx_ctrl_t rx_ctrl;
    uint8_t payload[0];
} wifi_promiscuous_pkt_t;
typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[33];
    uint8_t primary;
    wifi_second_chan_t second;
    int8_t rssi;
    wifi_auth_mode_t authmode;
    wifi_country_t country;
} wifi_ap_record_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t bssid_set;
    uint8_t bssid[6];
    uint8_t channel;
    uint8_t scan_method; /* wifi_scan_method_t */
} wifi_sta_config_t;
typedef struct {
    uint8_t ssid[32];
    uint8_t password[64];
    uint8_t ssid_len;
    uint8_t channel;
} wifi_ap_config_t;
typedef union {
    wifi_ap_config_t ap;
    wifi_sta_config_t sta;
} wifi_config_t;
typedef enum {
    WIFI_EVENT_WIFI_READY = 0, WIFI_EVENT_SCAN_DONE, WIFI_EVENT_STA_START, WIFI_EVENT_STA_STOP,
    WIFI_EVENT_STA_CONNECTED, WIFI_EVENT_STA_DISCONNECTED, WIFI_EVENT_STA_AUTHMODE_CHANGE,
    WIFI_EVENT_STA_WPS_ER_SUCCESS, WIFI_EVENT_STA_WPS_ER_FAILED, WIFI_EVENT_STA_WPS_ER_TIMEOUT,
    WIFI_EVENT_STA_WPS_ER_PIN, WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP, WIFI_EVENT_AP_START, WIFI_EVENT_AP_STOP,
    WIFI_EVENT_AP_STACONNECTED, WIFI_EVENT_AP_STADISCONNECTED,
} wifi_event_t;
typedef struct { uint8_t ssid[32]; uint8_t ssid_len; uint8_t bssid[6]; uint8_t channel; wifi_auth_mode_t authmode; uint16_t aid; } wifi_event_sta_connected_t;
typedef struct { uint8_t mac[6]; uint8_t aid; bool is_mesh_child; } wifi_event_ap_staconnected_t;
typedef struct { uint8_t mac[6]; uint8_t aid; bool is_mesh_child; uint8_t reason; } wifi_event_ap_stadisconnected_t;

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]);
/* used by the debug console commands (src/debug/src/commands) */
esp_err_t esp_wifi_get_max_tx_power(int8_t *power);
esp_err_t esp_wifi_set_max_tx_power(int8_t power);
esp_err_t esp_wifi_get_protocol(wifi_interface_t ifx, uint8_t *protocol_bitmap);
esp_err_t esp_wifi_set_protocol(wifi_interface_t ifx, uint8_t protocol_bitmap);
size_t heap_caps_get_total_size(uint32_t caps);
esp_err_t esp_wifi_set_country(const wifi_country_t *country);
esp_err_t esp_now_set_peer_rate_config(const uint8_t *peer_addr, esp_now_rate_config_t *config);
esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_restore(void);
esp_err_t esp_wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second);
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second);
esp_err_t esp_wifi_get_country(wifi_country_t *country);
esp_err_t esp_wifi_get_mode(wifi_mode_t *mode);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_force_wakeup_acquire(void);
esp_err_t esp_wifi_force_wakeup_release(void);
esp_err_t esp_wifi_get_config(wifi_interface_t interface, wifi_config_t *conf);

/* ---------------------------------------------------------------- esp_now */
#define ESP_NOW_ETH_ALEN 6
#define ESP_NOW_KEY_LEN 16
#define ESP_NOW_MAX_DATA_LEN 250
typedef enum { ESP_NOW_SEND_SUCCESS = 0, ESP_NOW_SEND_FAIL } esp_now_send_status_t;
typedef struct {
    uint8_t peer_addr[ESP_NOW_ETH_ALEN];
    uint8_t lmk[ESP_NOW_KEY_LEN];
    uint8_t channel;
    wifi_interface_t ifidx;
    bool encrypt;
    void *priv;
} esp_now_peer_info_t;
typedef struct {
    uint8_t *src_addr;
    uint8_t *des_addr;
    wifi_pkt_rx_ctrl_t *rx_ctrl;
} esp_now_recv_info_t;
typedef void (*esp_now_recv_cb_t)(const esp_now_recv_info_t *info, const uint8_t *data, int len);
typedef void (*esp_now_send_cb_t)(const uint8_t *mac_addr, esp_now_send_status_t status);
esp_err_t esp_now_init(void);
esp_err_t esp_now_deinit(void);
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb);
esp_err_t esp_now_unregister_recv_cb(void);
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb);
esp_err_t esp_now_unregister_send_cb(void);
esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len);
esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer);
esp_err_t esp_now_del_peer(const uint8_t *peer_addr);
esp_err_t esp_now_mod_peer(const esp_now_peer_info_t *peer);
bool esp_now_is_peer_exist(const uint8_t *peer_addr);
esp_err_t esp_now_set_pmk(const uint8_t *pmk);

/* -------------------------------------------------------------------- nvs */
typedef uint32_t nvs_handle_t;
typedef nvs_handle_t nvs_handle;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;
typedef nvs_open_mode_t nvs_open_mode;
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_erase(void);
esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out);
void nvs_close(nvs_handle_t h);
esp_err_t nvs_commit(nvs_handle_t h);
esp_err_t nvs_erase_all(nvs_handle_t h);
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key);
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t length);
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out_value, size_t *length);

/* --------------------------------------------------------- partitions / ota */
typedef enum { ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_TYPE_DATA = 1, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_APP_FACTORY = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10, ESP_PARTITION_SUBTYPE_ANY = 0xff } esp_partition_subtype_t;
typedef struct {
    void *flash_chip;
    esp_partition_type_t type;
    esp_partition_subtype_t subtype;
    uint32_t address;
    uint32_t size;
    uint32_t erase_size;
    char label[17];
    bool encrypted;
    bool readonly;
} esp_partition_t;
typedef struct {
    uint32_t magic_word;
    uint32_t secure_version;
    uint32_t reserv1[2];
    char version[32];
    char project_name[32];
    char time[16];
    char date[16];
    char idf_ver[32];
    uint8_t app_elf_sha256[32];
    uint32_t reserv2[20];
} esp_app_desc_t;
typedef uint32_t esp_ota_handle_t;
#define OTA_SIZE_UNKNOWN 0xffffffff
const esp_partition_t *esp_ota_get_running_partition(void);
const esp_partition_t *esp_ota_get_next_update_partition(const esp_partition_t *start_from);
const esp_partition_t *esp_ota_get_boot_partition(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *app_desc);
const esp_app_desc_t *esp_app_get_description(void);
const esp_app_desc_t *esp_ota_get_app_description(void);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size, esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);
esp_err_t esp_partition_write(const esp_partition_t *p, size_t dst_offset, const void *src, size_t size);
esp_err_t esp_partition_read(const esp_partition_t *p, size_t src_offset, void *dst, size_t size);
esp_err_t esp_partition_erase_range(const esp_partition_t *p, size_t offset, size_t size);
esp_err_t esp_partition_get_sha256(const esp_partition_t *p, uint8_t *sha_256);
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);
uint32_t esp_efuse_read_secure_version(void);

/* ------------------------------------------------------------------ shim API
 * Used by the harnesses in mayhem/ to drive the emulated device. */
#define SHIM_PEER_MAX 64
/* Self MAC address of the emulated device. */
extern const uint8_t shim_self_mac[6];
/* Reset the per-input emulated device state: PRNG, RAM NVS, RAM flash, peer table, the
 * sent-frame log and the virtual clock (only when no task has a pending timeout). */
void shim_reset(void);
/* Let every runnable task run until all of them block, without advancing time. */
void shim_settle(void);
/* Block the calling task for `ticks` of virtual time, running everything else meanwhile. */
void shim_sleep(uint32_t ticks);
/* Number of live tasks (including the caller). */
int shim_task_count(void);
int shim_timed_pending(void);
/* Sent frames recorded by esp_now_send (most recent SHIM_SENT_MAX). */
#define SHIM_SENT_MAX 64
typedef struct { uint8_t dest[6]; size_t len; uint8_t data[1500]; } shim_sent_t;
size_t shim_sent_count(void);
const shim_sent_t *shim_sent_get(size_t i);
/* Optional hook called (in the sender's task) after each esp_now_send. */
extern void (*shim_on_send)(const uint8_t *dest, const uint8_t *data, size_t len);
/* Deliver one received frame to the registered esp_now receive callback. */
void shim_deliver(const uint8_t *src_mac, const uint8_t *data, size_t len, int8_t rssi, uint8_t channel);

#ifdef __cplusplus
}
#endif
