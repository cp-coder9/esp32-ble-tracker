#include "detection_cache.h"

#include <inttypes.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static btrpa_detection_t s_cache[BTRPA_DETECTION_CACHE_SIZE];
static size_t s_count;
static SemaphoreHandle_t s_lock;
static btrpa_track_config_t s_track = {.meters = 5.0f, .min_rssi = -70};

static void json_escape(const char *in, char *out, size_t out_len)
{
    if (out_len == 0) return;
    size_t pos = 0;
    if (in == NULL) in = "";
    for (const unsigned char *p = (const unsigned char *)in; *p != '\0' && pos + 1 < out_len; ++p) {
        unsigned char c = *p;
        if ((c == '\\' || c == '"') && pos + 2 < out_len) {
            out[pos++] = '\\';
            out[pos++] = (char)c;
        } else if (c == '\n' && pos + 2 < out_len) {
            out[pos++] = '\\'; out[pos++] = 'n';
        } else if (c == '\r' && pos + 2 < out_len) {
            out[pos++] = '\\'; out[pos++] = 'r';
        } else if (c == '\t' && pos + 2 < out_len) {
            out[pos++] = '\\'; out[pos++] = 't';
        } else if (c >= 0x20) {
            out[pos++] = (char)c;
        }
    }
    out[pos] = '\0';
}

static void lock_cache(void)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock_cache(void)
{
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static bool contains_case_insensitive(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) return false;
    if (*needle == '\0') return true;
    for (const unsigned char *h = (const unsigned char *)haystack; *h != '\0'; ++h) {
        const unsigned char *hp = h;
        const unsigned char *np = (const unsigned char *)needle;
        while (*hp != '\0' && *np != '\0' && tolower(*hp) == tolower(*np)) {
            ++hp;
            ++np;
        }
        if (*np == '\0') return true;
    }
    return false;
}

static float estimate_distance(int rssi, int tx) { if (tx == 0) tx = -59; float d = powf(10.0f, ((float)tx - (float)rssi) / 30.0f); if (d < 0.3f) d = 0.3f; if (d > 80.0f) d = 80.0f; return d; }
void btrpa_track_get(btrpa_track_config_t *out) { if (!out) return; lock_cache(); *out = s_track; unlock_cache(); }
void btrpa_track_set(const btrpa_track_config_t *cfg) { if (!cfg) return; lock_cache(); s_track = *cfg; s_track.enabled = true; if (s_track.meters <= 0) s_track.meters = 5.0f; unlock_cache(); }
void btrpa_track_off(void) { lock_cache(); memset(&s_track, 0, sizeof(s_track)); s_track.meters = 5.0f; s_track.min_rssi = -70; unlock_cache(); }
bool btrpa_track_update(const btrpa_detection_t *d, float *distance_m, bool *near) { if (!d) return false; lock_cache(); bool match = s_track.enabled && ((s_track.address[0] && strcasecmp(s_track.address, d->address) == 0) || (s_track.name[0] && contains_case_insensitive(d->name, s_track.name))); float dist = estimate_distance(d->rssi, d->tx); bool is_near = match && (s_track.use_rssi ? d->rssi >= s_track.min_rssi : dist <= s_track.meters); if (match) { s_track.last_match_ms = d->last_seen_ms; strlcpy(s_track.last_address, d->address, sizeof(s_track.last_address)); strlcpy(s_track.last_name, d->name, sizeof(s_track.last_name)); s_track.last_distance_m = dist; s_track.last_rssi = d->rssi; s_track.near = is_near; } unlock_cache(); if (distance_m) *distance_m = dist; if (near) *near = is_near; return match; }
size_t btrpa_track_json(char *out, size_t out_len) { btrpa_track_config_t t; btrpa_track_get(&t); char address[32], name[128], last_address[32], last_name[128]; json_escape(t.address, address, sizeof(address)); json_escape(t.name, name, sizeof(name)); json_escape(t.last_address, last_address, sizeof(last_address)); json_escape(t.last_name, last_name, sizeof(last_name)); return snprintf(out, out_len, "{\"enabled\":%s,\"address\":\"%s\",\"name\":\"%s\",\"meters\":%.1f,\"min_rssi\":%d,\"condition\":\"%s\",\"near\":%s,\"last_address\":\"%s\",\"last_name\":\"%s\",\"last_distance_m\":%.1f,\"last_rssi\":%d,\"last_match_ms\":%u}", t.enabled?"true":"false", address, name, t.meters, t.min_rssi, t.use_rssi?"rssi":"meters", t.near?"true":"false", last_address, last_name, t.last_distance_m, t.last_rssi, (unsigned)t.last_match_ms); }

void btrpa_detection_cache_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
}

void btrpa_detection_cache_upsert(const btrpa_detection_t *detection)
{
    if (detection == NULL || detection->address[0] == '\0') {
        return;
    }

    lock_cache();
    size_t slot = BTRPA_DETECTION_CACHE_SIZE;
    size_t oldest_slot = 0;
    uint32_t oldest = UINT32_MAX;
    for (size_t i = 0; i < s_count; ++i) {
        if (strcmp(s_cache[i].address, detection->address) == 0) {
            slot = i;
            break;
        }
        if (s_cache[i].last_seen_ms < oldest) {
            oldest = s_cache[i].last_seen_ms;
            oldest_slot = i;
        }
    }
    if (s_count < BTRPA_DETECTION_CACHE_SIZE && slot == BTRPA_DETECTION_CACHE_SIZE) {
        slot = s_count++;
    } else if (slot == BTRPA_DETECTION_CACHE_SIZE) {
        slot = oldest_slot;
    }

    uint32_t first_seen = detection->first_seen_ms;
    uint32_t count = detection->count;
    if (slot < s_count && strcmp(s_cache[slot].address, detection->address) == 0) {
        first_seen = s_cache[slot].first_seen_ms;
        count = s_cache[slot].count + 1;
    } else if (s_count == BTRPA_DETECTION_CACHE_SIZE && slot < s_count) {
        first_seen = detection->first_seen_ms;
        count = 1;
    }

    s_cache[slot] = *detection;
    s_cache[slot].first_seen_ms = first_seen;
    s_cache[slot].count = count == 0 ? 1 : count;
    unlock_cache();
}

size_t btrpa_detection_cache_snapshot(btrpa_detection_t *out, size_t max_items)
{
    if (out == NULL || max_items == 0) {
        return 0;
    }
    lock_cache();
    size_t n = s_count < max_items ? s_count : max_items;
    memcpy(out, s_cache, n * sizeof(out[0]));
    unlock_cache();
    return n;
}

bool btrpa_detection_cache_latest(btrpa_detection_t *out)
{
    if (out == NULL) {
        return false;
    }

    lock_cache();
    if (s_count == 0) {
        unlock_cache();
        return false;
    }

    size_t latest_slot = 0;
    uint32_t latest_seen = s_cache[0].last_seen_ms;
    for (size_t i = 1; i < s_count; ++i) {
        if (s_cache[i].last_seen_ms > latest_seen) {
            latest_seen = s_cache[i].last_seen_ms;
            latest_slot = i;
        }
    }
    *out = s_cache[latest_slot];
    unlock_cache();
    return true;
}

size_t btrpa_detection_to_json(const btrpa_detection_t *d, char *out, size_t out_len)
{
    if (d == NULL || out == NULL || out_len == 0) {
        return 0;
    }
    char gps_tail[64] = {0};
    if (d->has_gps) {
        snprintf(gps_tail, sizeof(gps_tail), ",\"lat\":%.7f,\"lon\":%.7f", d->lat, d->lon);
    }
    char address[32], name[128], address_type[32], adv_type[48], manufacturer_data[256], service_uuids[256];
    json_escape(d->address, address, sizeof(address));
    json_escape(d->name, name, sizeof(name));
    json_escape(d->address_type, address_type, sizeof(address_type));
    json_escape(d->adv_type, adv_type, sizeof(adv_type));
    json_escape(d->manufacturer_data, manufacturer_data, sizeof(manufacturer_data));
    json_escape(d->service_uuids, service_uuids, sizeof(service_uuids));

    int n = snprintf(out, out_len,
                     "{\"address\":\"%s\",\"name\":\"%s\",\"local_name\":\"%s\","
                     "\"rssi\":%d,\"tx\":%d,\"timestamp\":%" PRIu32 ","
                     "\"source\":\"ESP32\",\"gps\":\"%s\",\"gps_source\":\"%s\","
                     "\"address_type\":\"%s\",\"adv_type\":\"%s\","
                     "\"manufacturer_data\":\"%s\",\"service_uuids\":\"%s\"%s}",
                     address, name, name, d->rssi, d->tx, d->last_seen_ms,
                     d->has_gps ? "locked" : "absent", d->has_gps ? "esp32" : "phone_fallback_supported",
                     address_type, adv_type, manufacturer_data, service_uuids,
                     gps_tail);
    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    return (size_t)n < out_len ? (size_t)n : out_len - 1;
}

size_t btrpa_detection_cache_jsonl(char *out, size_t out_len)
{
    btrpa_detection_t items[BTRPA_DETECTION_CACHE_SIZE];
    size_t n = btrpa_detection_cache_snapshot(items, BTRPA_DETECTION_CACHE_SIZE);
    size_t pos = 0;
    for (size_t i = 0; i < n && pos + 2 < out_len; ++i) {
        pos += btrpa_detection_to_json(&items[i], &out[pos], out_len - pos);
        if (pos + 1 < out_len) {
            out[pos++] = '\n';
            out[pos] = '\0';
        }
    }
    return pos;
}
