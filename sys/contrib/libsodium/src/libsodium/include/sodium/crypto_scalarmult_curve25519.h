#ifndef crypto_scalarmult_curve25519_H
#define crypto_scalarmult_curve25519_H

#include <stddef.h>

#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_scalarmult_curve25519_BYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_curve25519_bytes(void);

#define crypto_scalarmult_curve25519_SCALARBYTES 32U
SODIUM_EXPORT
size_t crypto_scalarmult_curve25519_scalarbytes(void);

/*
 * NOTE: Do not use the result of this function directly.
 *
 * Hash the result with the public keys in order to compute a shared
 * secret key: H(q || client_pk || server_pk)
 *
 * Or unless this is not an option, use the crypto_kx() API instead.
 */
SODIUM_EXPORT
int crypto_scalarmult_curve25519(unsigned char *q, const unsigned char *n,
                                 const unsigned char *p)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_scalarmult_curve25519_base(unsigned char *q, const unsigned char *n);

#ifdef __cplusplus
}
#endif

#endif
