#ifndef crypto_box_H
#define crypto_box_H

/*
 * THREAD SAFETY: crypto_box_keypair() is thread-safe,
 * provided that sodium_init() was called before.
 *
 * Other functions are always thread-safe.
 */

#include <stddef.h>

#include "crypto_box_curve25519xsalsa20poly1305.h"
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_box_SEEDBYTES crypto_box_curve25519xsalsa20poly1305_SEEDBYTES
SODIUM_EXPORT
size_t  crypto_box_seedbytes(void);

#define crypto_box_PUBLICKEYBYTES crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES
SODIUM_EXPORT
size_t  crypto_box_publickeybytes(void);

#define crypto_box_SECRETKEYBYTES crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES
SODIUM_EXPORT
size_t  crypto_box_secretkeybytes(void);

#define crypto_box_NONCEBYTES crypto_box_curve25519xsalsa20poly1305_NONCEBYTES
SODIUM_EXPORT
size_t  crypto_box_noncebytes(void);

#define crypto_box_MACBYTES crypto_box_curve25519xsalsa20poly1305_MACBYTES
SODIUM_EXPORT
size_t  crypto_box_macbytes(void);

#define crypto_box_MESSAGEBYTES_MAX crypto_box_curve25519xsalsa20poly1305_MESSAGEBYTES_MAX
SODIUM_EXPORT
size_t  crypto_box_messagebytes_max(void);

#define crypto_box_PRIMITIVE "curve25519xsalsa20poly1305"
SODIUM_EXPORT
const char *crypto_box_primitive(void);

SODIUM_EXPORT
int crypto_box_seed_keypair(unsigned char *pk, unsigned char *sk,
                            const unsigned char *seed);

SODIUM_EXPORT
int crypto_box_keypair(unsigned char *pk, unsigned char *sk);

SODIUM_EXPORT
int crypto_box_easy(unsigned char *c, const unsigned char *m,
                    unsigned long long mlen, const unsigned char *n,
                    const unsigned char *pk, const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_open_easy(unsigned char *m, const unsigned char *c,
                         unsigned long long clen, const unsigned char *n,
                         const unsigned char *pk, const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_detached(unsigned char *c, unsigned char *mac,
                        const unsigned char *m, unsigned long long mlen,
                        const unsigned char *n, const unsigned char *pk,
                        const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_open_detached(unsigned char *m, const unsigned char *c,
                             const unsigned char *mac,
                             unsigned long long clen,
                             const unsigned char *n,
                             const unsigned char *pk,
                             const unsigned char *sk)
            __attribute__ ((warn_unused_result));

/* -- Precomputation interface -- */

#define crypto_box_BEFORENMBYTES crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES
SODIUM_EXPORT
size_t  crypto_box_beforenmbytes(void);

SODIUM_EXPORT
int crypto_box_beforenm(unsigned char *k, const unsigned char *pk,
                        const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_easy_afternm(unsigned char *c, const unsigned char *m,
                            unsigned long long mlen, const unsigned char *n,
                            const unsigned char *k);

SODIUM_EXPORT
int crypto_box_open_easy_afternm(unsigned char *m, const unsigned char *c,
                                 unsigned long long clen, const unsigned char *n,
                                 const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_detached_afternm(unsigned char *c, unsigned char *mac,
                                const unsigned char *m, unsigned long long mlen,
                                const unsigned char *n, const unsigned char *k);

SODIUM_EXPORT
int crypto_box_open_detached_afternm(unsigned char *m, const unsigned char *c,
                                     const unsigned char *mac,
                                     unsigned long long clen, const unsigned char *n,
                                     const unsigned char *k)
            __attribute__ ((warn_unused_result));

/* -- Ephemeral SK interface -- */

#define crypto_box_SEALBYTES (crypto_box_PUBLICKEYBYTES + crypto_box_MACBYTES)
SODIUM_EXPORT
size_t crypto_box_sealbytes(void);

SODIUM_EXPORT
int crypto_box_seal(unsigned char *c, const unsigned char *m,
                    unsigned long long mlen, const unsigned char *pk);

SODIUM_EXPORT
int crypto_box_seal_open(unsigned char *m, const unsigned char *c,
                         unsigned long long clen,
                         const unsigned char *pk, const unsigned char *sk)
            __attribute__ ((warn_unused_result));

/* -- NaCl compatibility interface ; Requires padding -- */

#define crypto_box_ZEROBYTES crypto_box_curve25519xsalsa20poly1305_ZEROBYTES
SODIUM_EXPORT
size_t  crypto_box_zerobytes(void);

#define crypto_box_BOXZEROBYTES crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES
SODIUM_EXPORT
size_t  crypto_box_boxzerobytes(void);

SODIUM_EXPORT
int crypto_box(unsigned char *c, const unsigned char *m,
               unsigned long long mlen, const unsigned char *n,
               const unsigned char *pk, const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_open(unsigned char *m, const unsigned char *c,
                    unsigned long long clen, const unsigned char *n,
                    const unsigned char *pk, const unsigned char *sk)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_box_afternm(unsigned char *c, const unsigned char *m,
                       unsigned long long mlen, const unsigned char *n,
                       const unsigned char *k);

SODIUM_EXPORT
int crypto_box_open_afternm(unsigned char *m, const unsigned char *c,
                            unsigned long long clen, const unsigned char *n,
                            const unsigned char *k)
            __attribute__ ((warn_unused_result));

#ifdef __cplusplus
}
#endif

#endif
