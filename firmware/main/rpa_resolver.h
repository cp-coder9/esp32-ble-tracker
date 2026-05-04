#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BTRPA_IRK_LEN 16
#define BTRPA_ADDR_LEN 6
#define BTRPA_PRAND_LEN 3
#define BTRPA_HASH_LEN 3

/**
 * Return true when a 6-octet Bluetooth address is a Resolvable Private
 * Address (RPA). Per Bluetooth Core Spec, the two most significant bits of
 * prand are 0b01. This project stores addresses in display order, matching
 * the Python helper: prand is addr[0..2], hash is addr[3..5].
 */
bool btrpa_is_rpa(const uint8_t addr[BTRPA_ADDR_LEN]);

/**
 * Bluetooth privacy ah() function from Core Spec Vol 3, Part H, 2.2.2.
 * Computes AES-128-ECB(IRK, padding || prand) and returns the 24-bit hash.
 *
 * IRK and prand use the same byte ordering as btrpa-scan's Python helper.
 */
bool btrpa_ah(const uint8_t irk[BTRPA_IRK_LEN],
              const uint8_t prand[BTRPA_PRAND_LEN],
              uint8_t out_hash[BTRPA_HASH_LEN]);

/** Resolve a binary RPA against one IRK. */
bool btrpa_resolve_rpa(const uint8_t irk[BTRPA_IRK_LEN],
                       const uint8_t addr[BTRPA_ADDR_LEN]);

/** Parse XX:XX:XX:XX:XX:XX or XX-XX-XX-XX-XX-XX into six bytes. */
bool btrpa_parse_addr(const char *text, uint8_t out_addr[BTRPA_ADDR_LEN]);

/** Resolve a textual Bluetooth address against one IRK. */
bool btrpa_resolve_rpa_text(const uint8_t irk[BTRPA_IRK_LEN],
                            const char *addr_text);

#ifdef __cplusplus
}
#endif
