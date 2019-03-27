#ifndef crypto_auth_H
#define crypto_auth_H

#include <stddef.h>

#include "crypto_auth_hmacsha512256.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_auth_BYTES crypto_auth_hmacsha512256_BYTES
SODIUM_EXPORT
size_t  crypto_auth_bytes(void);

#define crypto_auth_KEYBYTES crypto_auth_hmacsha512256_KEYBYTES
SODIUM_EXPORT
size_t  crypto_auth_keybytes(void);

#define crypto_auth_PRIMITIVE "hmacsha512256"
SODIUM_EXPORT
const char *crypto_auth_primitive(void);

SODIUM_EXPORT
int crypto_auth(unsigned char *out, const unsigned char *in,
                unsigned long long inlen, const unsigned char *k);

SODIUM_EXPORT
int crypto_auth_verify(const unsigned char *h, const unsigned char *in,
                       unsigned long long inlen, const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
void crypto_auth_keygen(unsigned char k[crypto_auth_KEYBYTES]);

#ifdef __cplusplus
}
#endif

#endif
