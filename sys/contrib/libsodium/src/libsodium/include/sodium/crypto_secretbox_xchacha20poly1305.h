#ifndef crypto_secretbox_xchacha20poly1305_H
#define crypto_secretbox_xchacha20poly1305_H

#include <stddef.h>
#include "crypto_stream_xchacha20.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_secretbox_xchacha20poly1305_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_secretbox_xchacha20poly1305_keybytes(void);

#define crypto_secretbox_xchacha20poly1305_NONCEBYTES 24U
SODIUM_EXPORT
size_t crypto_secretbox_xchacha20poly1305_noncebytes(void);

#define crypto_secretbox_xchacha20poly1305_MACBYTES 16U
SODIUM_EXPORT
size_t crypto_secretbox_xchacha20poly1305_macbytes(void);

#define crypto_secretbox_xchacha20poly1305_MESSAGEBYTES_MAX \
    (crypto_stream_xchacha20_MESSAGEBYTES_MAX - crypto_secretbox_xchacha20poly1305_MACBYTES)
SODIUM_EXPORT
size_t crypto_secretbox_xchacha20poly1305_messagebytes_max(void);

SODIUM_EXPORT
int crypto_secretbox_xchacha20poly1305_easy(unsigned char *c,
                                            const unsigned char *m,
                                            unsigned long long mlen,
                                            const unsigned char *n,
                                            const unsigned char *k);

SODIUM_EXPORT
int crypto_secretbox_xchacha20poly1305_open_easy(unsigned char *m,
                                                 const unsigned char *c,
                                                 unsigned long long clen,
                                                 const unsigned char *n,
                                                 const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_secretbox_xchacha20poly1305_detached(unsigned char *c,
                                                unsigned char *mac,
                                                const unsigned char *m,
                                                unsigned long long mlen,
                                                const unsigned char *n,
                                                const unsigned char *k);

SODIUM_EXPORT
int crypto_secretbox_xchacha20poly1305_open_detached(unsigned char *m,
                                                     const unsigned char *c,
                                                     const unsigned char *mac,
                                                     unsigned long long clen,
                                                     const unsigned char *n,
                                                     const unsigned char *k)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
