#ifndef crypto_stream_H
#define crypto_stream_H

/*
 *  WARNING: This is just a stream cipher. It is NOT authenticated encryption.
 *  While it provides some protection against eavesdropping, it does NOT
 *  provide any security against active attacks.
 *  Unless you know what you're doing, what you are looking for is probably
 *  the crypto_box functions.
 */

#include <stddef.h>

#include "crypto_stream_xsalsa20.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_stream_KEYBYTES crypto_stream_xsalsa20_KEYBYTES
SODIUM_EXPORT
size_t  crypto_stream_keybytes(void);

#define crypto_stream_NONCEBYTES crypto_stream_xsalsa20_NONCEBYTES
SODIUM_EXPORT
size_t  crypto_stream_noncebytes(void);

#define crypto_stream_MESSAGEBYTES_MAX crypto_stream_xsalsa20_MESSAGEBYTES_MAX
SODIUM_EXPORT
size_t  crypto_stream_messagebytes_max(void);

#define crypto_stream_PRIMITIVE "xsalsa20"
SODIUM_EXPORT
const char *crypto_stream_primitive(void);

SODIUM_EXPORT
int crypto_stream(unsigned char *c, unsigned long long clen,
                  const unsigned char *n, const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_xor(unsigned char *c, const unsigned char *m,
                      unsigned long long mlen, const unsigned char *n,
                      const unsigned char *k);

SODIUM_EXPORT
void crypto_stream_keygen(unsigned char k[crypto_stream_KEYBYTES]);

#ifdef __cplusplus
}
#endif

#endif
