#ifndef crypto_stream_chacha20_H
#define crypto_stream_chacha20_H

/*
 *  WARNING: This is just a stream cipher. It is NOT authenticated encryption.
 *  While it provides some protection against eavesdropping, it does NOT
 *  provide any security against active attacks.
 *  Unless you know what you're doing, what you are looking for is probably
 *  the crypto_box functions.
 */

#include <stddef.h>
#include <stdint.h>
#include "export.h"

#ifdef __cplusplus
# ifdef __GNUC__
#  pragma GCC diagnostic ignored "-Wlong-long"
# endif
extern "C" {
#endif

#define crypto_stream_chacha20_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_stream_chacha20_keybytes(void);

#define crypto_stream_chacha20_NONCEBYTES 8U
SODIUM_EXPORT
size_t crypto_stream_chacha20_noncebytes(void);

#define crypto_stream_chacha20_MESSAGEBYTES_MAX SODIUM_SIZE_MAX
SODIUM_EXPORT
size_t crypto_stream_chacha20_messagebytes_max(void);

/* ChaCha20 with a 64-bit nonce and a 64-bit counter, as originally designed */

SODIUM_EXPORT
int crypto_stream_chacha20(unsigned char *c, unsigned long long clen,
                           const unsigned char *n, const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_chacha20_xor(unsigned char *c, const unsigned char *m,
                               unsigned long long mlen, const unsigned char *n,
                               const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_chacha20_xor_ic(unsigned char *c, const unsigned char *m,
                                  unsigned long long mlen,
                                  const unsigned char *n, uint64_t ic,
                                  const unsigned char *k);

SODIUM_EXPORT
void crypto_stream_chacha20_keygen(unsigned char k[crypto_stream_chacha20_KEYBYTES]);

/* ChaCha20 with a 96-bit nonce and a 32-bit counter (IETF) */

#define crypto_stream_chacha20_ietf_KEYBYTES 32U
SODIUM_EXPORT
size_t crypto_stream_chacha20_ietf_keybytes(void);

#define crypto_stream_chacha20_ietf_NONCEBYTES 12U
SODIUM_EXPORT
size_t crypto_stream_chacha20_ietf_noncebytes(void);

#define crypto_stream_chacha20_ietf_MESSAGEBYTES_MAX \
    SODIUM_MIN(SODIUM_SIZE_MAX, 64ULL * (1ULL << 32))
SODIUM_EXPORT
size_t crypto_stream_chacha20_ietf_messagebytes_max(void);

SODIUM_EXPORT
int crypto_stream_chacha20_ietf(unsigned char *c, unsigned long long clen,
                                const unsigned char *n, const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_chacha20_ietf_xor(unsigned char *c, const unsigned char *m,
                                    unsigned long long mlen, const unsigned char *n,
                                    const unsigned char *k);

SODIUM_EXPORT
int crypto_stream_chacha20_ietf_xor_ic(unsigned char *c, const unsigned char *m,
                                       unsigned long long mlen,
                                       const unsigned char *n, uint32_t ic,
                                       const unsigned char *k);

SODIUM_EXPORT
void crypto_stream_chacha20_ietf_keygen(unsigned char k[crypto_stream_chacha20_ietf_KEYBYTES]);

/* Aliases */

#define crypto_stream_chacha20_IETF_KEYBYTES crypto_stream_chacha20_ietf_KEYBYTES
#define crypto_stream_chacha20_IETF_NONCEBYTES crypto_stream_chacha20_ietf_NONCEBYTES
#define crypto_stream_chacha20_IETF_MESSAGEBYTES_MAX crypto_stream_chacha20_ietf_MESSAGEBYTES_MAX

#ifdef __cplusplus
}
#endif

#endif
