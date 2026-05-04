#include "rpa_resolver.h"

#include <ctype.h>
#include <string.h>

#include "mbedtls/aes.h"

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    return -1;
}

bool btrpa_is_rpa(const uint8_t addr[BTRPA_ADDR_LEN])
{
    return addr != NULL && ((addr[0] >> 6) == 0x01);
}

bool btrpa_ah(const uint8_t irk[BTRPA_IRK_LEN],
              const uint8_t prand[BTRPA_PRAND_LEN],
              uint8_t out_hash[BTRPA_HASH_LEN])
{
    if (irk == NULL || prand == NULL || out_hash == NULL) {
        return false;
    }

    /*
     * Bluetooth ah() is AES-128-ECB over one block: 13 zero bytes followed
     * by the three prand bytes. ECB is required by the Bluetooth spec here;
     * this single-block use is not general-purpose encryption.
     */
    uint8_t plaintext[16] = {0};
    uint8_t ciphertext[16] = {0};
    memcpy(&plaintext[13], prand, BTRPA_PRAND_LEN);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    int rc = mbedtls_aes_setkey_enc(&aes, irk, 128);
    if (rc == 0) {
        rc = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plaintext, ciphertext);
    }
    mbedtls_aes_free(&aes);

    if (rc != 0) {
        return false;
    }

    memcpy(out_hash, &ciphertext[13], BTRPA_HASH_LEN);
    return true;
}

bool btrpa_resolve_rpa(const uint8_t irk[BTRPA_IRK_LEN],
                       const uint8_t addr[BTRPA_ADDR_LEN])
{
    if (irk == NULL || addr == NULL || !btrpa_is_rpa(addr)) {
        return false;
    }

    uint8_t computed_hash[BTRPA_HASH_LEN] = {0};
    if (!btrpa_ah(irk, addr, computed_hash)) {
        return false;
    }

    return memcmp(computed_hash, &addr[3], BTRPA_HASH_LEN) == 0;
}

bool btrpa_parse_addr(const char *text, uint8_t out_addr[BTRPA_ADDR_LEN])
{
    if (text == NULL || out_addr == NULL) {
        return false;
    }

    size_t idx = 0;
    int high = -1;
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p == ':' || *p == '-') {
            continue;
        }
        int n = hex_nibble(*p);
        if (n < 0) {
            return false;
        }
        if (high < 0) {
            high = n;
        } else {
            if (idx >= BTRPA_ADDR_LEN) {
                return false;
            }
            out_addr[idx++] = (uint8_t)((high << 4) | n);
            high = -1;
        }
    }

    return idx == BTRPA_ADDR_LEN && high < 0;
}

bool btrpa_resolve_rpa_text(const uint8_t irk[BTRPA_IRK_LEN],
                            const char *addr_text)
{
    uint8_t addr[BTRPA_ADDR_LEN] = {0};
    if (!btrpa_parse_addr(addr_text, addr)) {
        return false;
    }
    return btrpa_resolve_rpa(irk, addr);
}
