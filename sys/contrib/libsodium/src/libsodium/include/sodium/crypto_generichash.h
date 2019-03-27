#ifndef crypto_generichash_H
#define crypto_generichash_H

#include <stddef.h>

#include "crypto_generichash_blake2b.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_generichash_BYTES_MIN crypto_generichash_blake2b_BYTES_MIN
SODIUM_EXPORT
size_t  crypto_generichash_bytes_min(void);

#define crypto_generichash_BYTES_MAX crypto_generichash_blake2b_BYTES_MAX
SODIUM_EXPORT
size_t  crypto_generichash_bytes_max(void);

#define crypto_generichash_BYTES crypto_generichash_blake2b_BYTES
SODIUM_EXPORT
size_t  crypto_generichash_bytes(void);

#define crypto_generichash_KEYBYTES_MIN crypto_generichash_blake2b_KEYBYTES_MIN
SODIUM_EXPORT
size_t  crypto_generichash_keybytes_min(void);

#define crypto_generichash_KEYBYTES_MAX crypto_generichash_blake2b_KEYBYTES_MAX
SODIUM_EXPORT
size_t  crypto_generichash_keybytes_max(void);

#define crypto_generichash_KEYBYTES crypto_generichash_blake2b_KEYBYTES
SODIUM_EXPORT
size_t  crypto_generichash_keybytes(void);

#define crypto_generichash_PRIMITIVE "blake2b"
SODIUM_EXPORT
const char *crypto_generichash_primitive(void);

/*
 * Important when writing bindings for other programming languages:
 * the state address *must* be 64-bytes aligned.
 */
typedef crypto_generichash_blake2b_state crypto_generichash_state;

SODIUM_EXPORT
size_t  crypto_generichash_statebytes(void);

SODIUM_EXPORT
int crypto_generichash(unsigned char *out, size_t outlen,
                       const unsigned char *in, unsigned long long inlen,
                       const unsigned char *key, size_t keylen);

SODIUM_EXPORT
int crypto_generichash_init(crypto_generichash_state *state,
                            const unsigned char *key,
                            const size_t keylen, const size_t outlen);

SODIUM_EXPORT
int crypto_generichash_update(crypto_generichash_state *state,
                              const unsigned char *in,
                              unsigned long long inlen);

SODIUM_EXPORT
int crypto_generichash_final(crypto_generichash_state *state,
                             unsigned char *out, const size_t outlen);

SODIUM_EXPORT
void crypto_generichash_keygen(unsigned char k[crypto_generichash_KEYBYTES]);

#ifdef __cplusplus
}
#endif

#endif
