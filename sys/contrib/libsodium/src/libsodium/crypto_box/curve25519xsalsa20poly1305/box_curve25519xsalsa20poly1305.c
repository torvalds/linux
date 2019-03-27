#include <string.h>

#include "crypto_box_curve25519xsalsa20poly1305.h"
#include "crypto_core_hsalsa20.h"
#include "crypto_hash_sha512.h"
#include "crypto_scalarmult_curve25519.h"
#include "crypto_secretbox_xsalsa20poly1305.h"
#include "randombytes.h"
#include "utils.h"

int
crypto_box_curve25519xsalsa20poly1305_seed_keypair(unsigned char *pk,
                                                   unsigned char *sk,
                                                   const unsigned char *seed)
{
    unsigned char hash[64];

    crypto_hash_sha512(hash, seed, 32);
    memcpy(sk, hash, 32);
    sodium_memzero(hash, sizeof hash);

    return crypto_scalarmult_curve25519_base(pk, sk);
}

int
crypto_box_curve25519xsalsa20poly1305_keypair(unsigned char *pk,
                                              unsigned char *sk)
{
    randombytes_buf(sk, 32);

    return crypto_scalarmult_curve25519_base(pk, sk);
}

int
crypto_box_curve25519xsalsa20poly1305_beforenm(unsigned char *k,
                                               const unsigned char *pk,
                                               const unsigned char *sk)
{
    static const unsigned char zero[16] = { 0 };
    unsigned char s[32];

    if (crypto_scalarmult_curve25519(s, sk, pk) != 0) {
        return -1;
    }
    return crypto_core_hsalsa20(k, zero, s, NULL);
}

int
crypto_box_curve25519xsalsa20poly1305_afternm(unsigned char *c,
                                              const unsigned char *m,
                                              unsigned long long mlen,
                                              const unsigned char *n,
                                              const unsigned char *k)
{
    return crypto_secretbox_xsalsa20poly1305(c, m, mlen, n, k);
}

int
crypto_box_curve25519xsalsa20poly1305_open_afternm(unsigned char *m,
                                                   const unsigned char *c,
                                                   unsigned long long clen,
                                                   const unsigned char *n,
                                                   const unsigned char *k)
{
    return crypto_secretbox_xsalsa20poly1305_open(m, c, clen, n, k);
}

int
crypto_box_curve25519xsalsa20poly1305(unsigned char *c, const unsigned char *m,
                                      unsigned long long   mlen,
                                      const unsigned char *n,
                                      const unsigned char *pk,
                                      const unsigned char *sk)
{
    unsigned char k[crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES];
    int           ret;

    if (crypto_box_curve25519xsalsa20poly1305_beforenm(k, pk, sk) != 0) {
        return -1;
    }
    ret = crypto_box_curve25519xsalsa20poly1305_afternm(c, m, mlen, n, k);
    sodium_memzero(k, sizeof k);

    return ret;
}

int
crypto_box_curve25519xsalsa20poly1305_open(
    unsigned char *m, const unsigned char *c, unsigned long long clen,
    const unsigned char *n, const unsigned char *pk, const unsigned char *sk)
{
    unsigned char k[crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES];
    int           ret;

    if (crypto_box_curve25519xsalsa20poly1305_beforenm(k, pk, sk) != 0) {
        return -1;
    }
    ret = crypto_box_curve25519xsalsa20poly1305_open_afternm(m, c, clen, n, k);
    sodium_memzero(k, sizeof k);

    return ret;
}

size_t
crypto_box_curve25519xsalsa20poly1305_seedbytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_SEEDBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_publickeybytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_PUBLICKEYBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_secretkeybytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_SECRETKEYBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_beforenmbytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_BEFORENMBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_noncebytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_NONCEBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_zerobytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_ZEROBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_boxzerobytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_BOXZEROBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_macbytes(void)
{
    return crypto_box_curve25519xsalsa20poly1305_MACBYTES;
}

size_t
crypto_box_curve25519xsalsa20poly1305_messagebytes_max(void)
{
    return crypto_box_curve25519xsalsa20poly1305_MESSAGEBYTES_MAX;
}
