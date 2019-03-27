#ifndef crypto_onetimeauth_poly1305_H
#define crypto_onetimeauth_poly1305_H

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>

#include "export.h"

typedef struct CRYPTO_ALIGN(16) crypto_onetimeauth_poly1305_state {
    unsigned char opaque[256];
} crypto_onetimeauth_poly1305_state;

SODIUM_EXPORT
size_t crypto_onetimeauth_poly1305_statebytes(void);

#define crypto_onetimeauth_poly1305_BYTES 16U
SODIUM_EXPORT
size_t crypto_onetimeauth_poly1305_bytes(void);

#define crypto_onetimeauth_poly1305_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_onetimeauth_poly1305_keybytes(void);

SODIUM_EXPORT
int crypto_onetimeauth_poly1305(unsigned char *out,
                                const unsigned char *in,
                                unsigned long long inlen,
                                const unsigned char *k);

SODIUM_EXPORT
int crypto_onetimeauth_poly1305_verify(const unsigned char *h,
                                       const unsigned char *in,
                                       unsigned long long inlen,
                                       const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_onetimeauth_poly1305_init(crypto_onetimeauth_poly1305_state *state,
                                     const unsigned char *key);

SODIUM_EXPORT
int crypto_onetimeauth_poly1305_update(crypto_onetimeauth_poly1305_state *state,
                                       const unsigned char *in,
                                       unsigned long long inlen);

SODIUM_EXPORT
int crypto_onetimeauth_poly1305_final(crypto_onetimeauth_poly1305_state *state,
                                      unsigned char *out);

SODIUM_EXPORT
void crypto_onetimeauth_poly1305_keygen(unsigned char k[crypto_onetimeauth_poly1305_KEYBYTES]);

#ifdef __cplusplus
}
#endif

#endif
