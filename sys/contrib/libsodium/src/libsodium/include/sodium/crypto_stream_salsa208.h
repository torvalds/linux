#ifndef crypto_stream_salsa208_H
#define crypto_stream_salsa208_H

/*
 *  WARNING: This is just a stream cipher. It is NOT authenticated encryption.
 *  While it provides some protection against eavesdropping, it does NOT
 *  provide any security against active attacks.
 *  Unless you know what you're doing, what you are looking for is probably
 *  the crypto_box functions.
 */

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_stream_salsa208_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_stream_salsa208_keybytes(void)
            __attribute__ ((deprecated));

#define crypto_stream_salsa208_NONCEBYTES 8U
SODIUM_EXPORT
size_t crypto_stream_salsa208_noncebytes(void)
            __attribute__ ((deprecated));

#define crypto_stream_salsa208_MESSAGEBYTES_MAX SODIUM_SIZE_MAX
    SODIUM_EXPORT
size_t crypto_stream_salsa208_messagebytes_max(void)
            __attribute__ ((deprecated));

SODIUM_EXPORT
int crypto_stream_salsa208(unsigned char *c, unsigned long long clen,
                           const unsigned char *n, const unsigned char *k)
            __attribute__ ((deprecated));

SODIUM_EXPORT
int crypto_stream_salsa208_xor(unsigned char *c, const unsigned char *m,
                               unsigned long long mlen, const unsigned char *n,
                               const unsigned char *k)
            __attribute__ ((deprecated));

SODIUM_EXPORT
void crypto_stream_salsa208_keygen(unsigned char k[crypto_stream_salsa208_KEYBYTES])
            __attribute__ ((deprecated));

#ifdef __cplusplus
}
#endif

#endif
