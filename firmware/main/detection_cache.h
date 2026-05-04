#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_DETECTION_CACHE_SIZE 64
#define BTRPA_DETECTION_JSON_MAX 384

typedef struct { bool enabled; char address[18]; char name[64]; float meters; int min_rssi; bool use_rssi; bool near; uint32_t last_match_ms; char last_address[18]; char last_name[64]; float last_distance_m; int last_rssi; } btrpa_track_config_t;

typedef struct {
    char address[18];
    char address_type[16];
    char name[64];
    char manufacturer_data[128];
    char service_uuids[192];
    char adv_type[24];
    int rssi;
    int tx;
    uint32_t first_seen_ms;
    uint32_t last_seen_ms;
    uint32_t count;
    bool has_gps;
    double lat;
    double lon;
} btrpa_detection_t;

void btrpa_detection_cache_init(void);
void btrpa_detection_cache_upsert(const btrpa_detection_t *detection);
size_t btrpa_detection_cache_snapshot(btrpa_detection_t *out, size_t max_items);
bool btrpa_detection_cache_latest(btrpa_detection_t *out);
size_t btrpa_detection_to_json(const btrpa_detection_t *detection, char *out, size_t out_len);
size_t btrpa_detection_cache_jsonl(char *out, size_t out_len);
void btrpa_track_get(btrpa_track_config_t *out);
void btrpa_track_set(const btrpa_track_config_t *cfg);
void btrpa_track_off(void);
bool btrpa_track_update(const btrpa_detection_t *detection, float *distance_m, bool *near);
size_t btrpa_track_json(char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
