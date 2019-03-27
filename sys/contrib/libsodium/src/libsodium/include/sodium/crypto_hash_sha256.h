#ifndef crypto_hash_sha256_H
#define crypto_hash_sha256_H

/*
 * WARNING: Unless you absolutely need to use SHA256 for interoperatibility,
 * purposes, you might want to consider crypto_generichash() instead.
 * Unlike SHA256, crypto_generichash() is not vulnerable to length
 * extension attacks.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

typedef struct crypto_hash_sha256_state {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} crypto_hash_sha256_state;

SODIUM_EXPORT
size_t crypto_hash_sha256_statebytes(void);

#define crypto_hash_sha256_BYTES 32U
SODIUM_EXPORT
size_t crypto_hash_sha256_bytes(void);

SODIUM_EXPORT
int crypto_hash_sha256(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen);

SODIUM_EXPORT
int crypto_hash_sha256_init(crypto_hash_sha256_state *state);

SODIUM_EXPORT
int crypto_hash_sha256_update(crypto_hash_sha256_state *state,
                              const unsigned char *in,
                              unsigned long long inlen);

SODIUM_EXPORT
int crypto_hash_sha256_final(crypto_hash_sha256_state *state,
                             unsigned char *out);

#ifdef __cplusplus
}
#endif

#endif
