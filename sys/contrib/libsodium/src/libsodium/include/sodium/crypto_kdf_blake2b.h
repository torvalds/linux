#ifndef crypto_kdf_blake2b_H
#define crypto_kdf_blake2b_H

#include <stddef.h>
#include <stdint.h>

#include "crypto_kdf_blake2b.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_kdf_blake2b_BYTES_MIN 16
SODIUM_EXPORT
size_t crypto_kdf_blake2b_bytes_min(void);

#define crypto_kdf_blake2b_BYTES_MAX 64
SODIUM_EXPORT
size_t crypto_kdf_blake2b_bytes_max(void);

#define crypto_kdf_blake2b_CONTEXTBYTES 8
SODIUM_EXPORT
size_t crypto_kdf_blake2b_contextbytes(void);

#define crypto_kdf_blake2b_KEYBYTES 32
SODIUM_EXPORT
size_t crypto_kdf_blake2b_keybytes(void);

SODIUM_EXPORT
int crypto_kdf_blake2b_derive_from_key(unsigned char *subkey, size_t subkey_len,
                                       uint64_t subkey_id,
                                       const char ctx[crypto_kdf_blake2b_CONTEXTBYTES],
                                       const unsigned char key[crypto_kdf_blake2b_KEYBYTES]);
#ifdef __cplusplus
}
#endif

#endif
