#ifndef crypto_core_ed25519_H
#define crypto_core_ed25519_H

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
extern "C" {
#endif

#define crypto_core_ed25519_BYTES 32
SODIUM_EXPORT
size_t crypto_core_ed25519_bytes(void);

#define crypto_core_ed25519_UNIFORMBYTES 32
SODIUM_EXPORT
size_t crypto_core_ed25519_uniformbytes(void);

SODIUM_EXPORT
int crypto_core_ed25519_is_valid_point(const unsigned char *p);

SODIUM_EXPORT
int crypto_core_ed25519_add(unsigned char *r,
                            const unsigned char *p, const unsigned char *q);

SODIUM_EXPORT
int crypto_core_ed25519_sub(unsigned char *r,
                            const unsigned char *p, const unsigned char *q);

SODIUM_EXPORT
int crypto_core_ed25519_from_uniform(unsigned char *p, const unsigned char *r);

#ifdef __cplusplus
}
#endif

#endif
