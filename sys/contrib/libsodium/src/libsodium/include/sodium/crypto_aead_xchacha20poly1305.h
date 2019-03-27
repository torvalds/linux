#ifndef crypto_aead_xchacha20poly1305_H
#define crypto_aead_xchacha20poly1305_H

#include <stddef.h>
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_aead_xchacha20poly1305_ietf_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_aead_xchacha20poly1305_ietf_keybytes(void);

#define crypto_aead_xchacha20poly1305_ietf_NSECBYTES 0U
SODIUM_EXPORT
size_t crypto_aead_xchacha20poly1305_ietf_nsecbytes(void);

#define crypto_aead_xchacha20poly1305_ietf_NPUBBYTES 24U
SODIUM_EXPORT
size_t crypto_aead_xchacha20poly1305_ietf_npubbytes(void);

#define crypto_aead_xchacha20poly1305_ietf_ABYTES 16U
SODIUM_EXPORT
size_t crypto_aead_xchacha20poly1305_ietf_abytes(void);

#define crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX \
    (SODIUM_SIZE_MAX - crypto_aead_xchacha20poly1305_ietf_ABYTES)
SODIUM_EXPORT
size_t crypto_aead_xchacha20poly1305_ietf_messagebytes_max(void);

SODIUM_EXPORT
int crypto_aead_xchacha20poly1305_ietf_encrypt(unsigned char *c,
                                               unsigned long long *clen_p,
                                               const unsigned char *m,
                                               unsigned long long mlen,
                                               const unsigned char *ad,
                                               unsigned long long adlen,
                                               const unsigned char *nsec,
                                               const unsigned char *npub,
                                               const unsigned char *k);

SODIUM_EXPORT
int crypto_aead_xchacha20poly1305_ietf_decrypt(unsigned char *m,
                                               unsigned long long *mlen_p,
                                               unsigned char *nsec,
                                               const unsigned char *c,
                                               unsigned long long clen,
                                               const unsigned char *ad,
                                               unsigned long long adlen,
                                               const unsigned char *npub,
                                               const unsigned char *k)
            __attribute__ ((warn_unused_result));

SODIUM_EXPORT
int crypto_aead_xchacha20poly1305_ietf_encrypt_detached(unsigned char *c,
                                                        unsigned char *mac,
                                                        unsigned long long *maclen_p,
                                                        const unsigned char *m,
                                                        unsigned long long mlen,
                                                        const unsigned char *ad,
                                                        unsigned long long adlen,
                                                        const unsigned char *nsec,
                                                        const unsigned char *npub,
                                                        const unsigned char *k);

SODIUM_EXPORT
int crypto_aead_xchacha20poly1305_ietf_decrypt_detached(unsigned char *m,
                                                        unsigned char *nsec,
                                                        const unsigned char *c,
                                                        unsigned long long clen,
                                                        const unsigned char *mac,
                                                        const unsigned char *ad,
                                                        unsigned long long adlen,
                                                        const unsigned char *npub,
                                                        const unsigned char *k)
        __attribute__ ((warn_unused_result));

SODIUM_EXPORT
void crypto_aead_xchacha20poly1305_ietf_keygen(unsigned char k[crypto_aead_xchacha20poly1305_ietf_KEYBYTES]);

/* Aliases */

#define crypto_aead_xchacha20poly1305_IETF_KEYBYTES         crypto_aead_xchacha20poly1305_ietf_KEYBYTES
#define crypto_aead_xchacha20poly1305_IETF_NSECBYTES        crypto_aead_xchacha20poly1305_ietf_NSECBYTES
#define crypto_aead_xchacha20poly1305_IETF_NPUBBYTES        crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define crypto_aead_xchacha20poly1305_IETF_ABYTES           crypto_aead_xchacha20poly1305_ietf_ABYTES
#define crypto_aead_xchacha20poly1305_IETF_MESSAGEBYTES_MAX crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX

#ifdef __cplusplus
}
#endif

#endif
