#ifndef crypto_scalarmult_H
#define crypto_scalarmult_H

#include <stddef.h>

#include "crypto_scalarmult_curve25519.h"
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_scalarmult_BYTES crypto_scalarmult_curve25519_BYTES
SODIUM_EXPORT
size_t  crypto_scalarmult_bytes(void);

#define crypto_scalarmult_SCALARBYTES crypto_scalarmult_curve25519_SCALARBYTES
SODIUM_EXPORT
size_t  crypto_scalarmult_scalarbytes(void);

#define crypto_scalarmult_PRIMITIVE "curve25519"
SODIUM_EXPORT
const char *crypto_scalarmult_primitive(void);

SODIUM_EXPORT
int crypto_scalarmult_base(unsigned char *q, const unsigned char *n);

/*
 * NOTE: Do not use the result of this function directly.
 *
 * Hash the result with the public keys in order to compute a shared
 * secret key: H(q || client_pk || server_pk)
 *
 * Or unless this is not an option, use the crypto_kx() API instead.
 */
SODIUM_EXPORT
int crypto_scalarmult(unsigned char *q, const unsigned char *n,
                      const unsigned char *p)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
