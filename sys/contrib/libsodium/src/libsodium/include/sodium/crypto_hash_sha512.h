#ifndef crypto_hash_sha512_H
#define crypto_hash_sha512_H

/*
 * WARNING: Unless you absolutely need to use SHA512 for interoperatibility,
 * purposes, you might want to consider crypto_generichash() instead.
 * Unlike SHA512, crypto_generichash() is not vulnerable to length
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

typedef struct crypto_hash_sha512_state {
    uint64_t state[8];
    uint64_t count[2];
    uint8_t  buf[128];
} crypto_hash_sha512_state;

SODIUM_EXPORT
size_t crypto_hash_sha512_statebytes(void);

#define crypto_hash_sha512_BYTES 64U
SODIUM_EXPORT
size_t crypto_hash_sha512_bytes(void);

SODIUM_EXPORT
int crypto_hash_sha512(unsigned char *out, const unsigned char *in,
                       unsigned long long inlen);

SODIUM_EXPORT
int crypto_hash_sha512_init(crypto_hash_sha512_state *state);

SODIUM_EXPORT
int crypto_hash_sha512_update(crypto_hash_sha512_state *state,
                              const unsigned char *in,
                              unsigned long long inlen);

SODIUM_EXPORT
int crypto_hash_sha512_final(crypto_hash_sha512_state *state,
                             unsigned char *out);

#ifdef __cplusplus
}
#endif

#endif
