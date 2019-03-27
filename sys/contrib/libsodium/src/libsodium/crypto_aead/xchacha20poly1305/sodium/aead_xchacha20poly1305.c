
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#include "core.h"
#include "crypto_aead_xchacha20poly1305.h"
#include "crypto_aead_chacha20poly1305.h"
#include "crypto_core_hchacha20.h"
#include "randombytes.h"
#include "utils.h"

#include "private/common.h"

int
crypto_aead_xchacha20poly1305_ietf_encrypt_detached(unsigned char *c,
                                                    unsigned char *mac,
                                                    unsigned long long *maclen_p,
                                                    const unsigned char *m,
                                                    unsigned long long mlen,
                                                    const unsigned char *ad,
                                                    unsigned long long adlen,
                                                    const unsigned char *nsec,
                                                    const unsigned char *npub,
                                                    const unsigned char *k)
{
    unsigned char k2[crypto_core_hchacha20_OUTPUTBYTES];
    unsigned char npub2[crypto_aead_chacha20poly1305_ietf_NPUBBYTES] = { 0 };
    int           ret;

    crypto_core_hchacha20(k2, npub, k, NULL);
    memcpy(npub2 + 4, npub + crypto_core_hchacha20_INPUTBYTES,
           crypto_aead_chacha20poly1305_ietf_NPUBBYTES - 4);
    ret = crypto_aead_chacha20poly1305_ietf_encrypt_detached
        (c, mac, maclen_p, m, mlen, ad, adlen, nsec, npub2, k2);
    sodium_memzero(k2, crypto_core_hchacha20_OUTPUTBYTES);

    return ret;
}

int
crypto_aead_xchacha20poly1305_ietf_encrypt(unsigned char *c,
                                           unsigned long long *clen_p,
                                           const unsigned char *m,
                                           unsigned long long mlen,
                                           const unsigned char *ad,
                                           unsigned long long adlen,
                                           const unsigned char *nsec,
                                           const unsigned char *npub,
                                           const unsigned char *k)
{
    unsigned long long clen = 0ULL;
    int                ret;

    if (mlen > crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX) {
        sodium_misuse();
    }
    ret = crypto_aead_xchacha20poly1305_ietf_encrypt_detached
        (c, c + mlen, NULL, m, mlen, ad, adlen, nsec, npub, k);
    if (clen_p != NULL) {
        if (ret == 0) {
            clen = mlen + crypto_aead_xchacha20poly1305_ietf_ABYTES;
        }
        *clen_p = clen;
    }
    return ret;
}

int
crypto_aead_xchacha20poly1305_ietf_decrypt_detached(unsigned char *m,
                                                    unsigned char *nsec,
                                                    const unsigned char *c,
                                                    unsigned long long clen,
                                                    const unsigned char *mac,
                                                    const unsigned char *ad,
                                                    unsigned long long adlen,
                                                    const unsigned char *npub,
                                                    const unsigned char *k)
{
    unsigned char k2[crypto_core_hchacha20_OUTPUTBYTES];
    unsigned char npub2[crypto_aead_chacha20poly1305_ietf_NPUBBYTES] = { 0 };
    int           ret;

    crypto_core_hchacha20(k2, npub, k, NULL);
    memcpy(npub2 + 4, npub + crypto_core_hchacha20_INPUTBYTES,
           crypto_aead_chacha20poly1305_ietf_NPUBBYTES - 4);
    ret = crypto_aead_chacha20poly1305_ietf_decrypt_detached
        (m, nsec, c, clen, mac, ad, adlen, npub2, k2);
    sodium_memzero(k2, crypto_core_hchacha20_OUTPUTBYTES);

    return ret;

}

int
crypto_aead_xchacha20poly1305_ietf_decrypt(unsigned char *m,
                                           unsigned long long *mlen_p,
                                           unsigned char *nsec,
                                           const unsigned char *c,
                                           unsigned long long clen,
                                           const unsigned char *ad,
                                           unsigned long long adlen,
                                           const unsigned char *npub,
                                           const unsigned char *k)
{
    unsigned long long mlen = 0ULL;
    int                ret = -1;

    if (clen >= crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        ret = crypto_aead_xchacha20poly1305_ietf_decrypt_detached
            (m, nsec,
             c, clen - crypto_aead_xchacha20poly1305_ietf_ABYTES,
             c + clen - crypto_aead_xchacha20poly1305_ietf_ABYTES,
             ad, adlen, npub, k);
    }
    if (mlen_p != NULL) {
        if (ret == 0) {
            mlen = clen - crypto_aead_xchacha20poly1305_ietf_ABYTES;
        }
        *mlen_p = mlen;
    }
    return ret;
}

size_t
crypto_aead_xchacha20poly1305_ietf_keybytes(void)
{
    return crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
}

size_t
crypto_aead_xchacha20poly1305_ietf_npubbytes(void)
{
    return crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
}

size_t
crypto_aead_xchacha20poly1305_ietf_nsecbytes(void)
{
    return crypto_aead_xchacha20poly1305_ietf_NSECBYTES;
}

size_t
crypto_aead_xchacha20poly1305_ietf_abytes(void)
{
    return crypto_aead_xchacha20poly1305_ietf_ABYTES;
}

size_t
crypto_aead_xchacha20poly1305_ietf_messagebytes_max(void)
{
    return crypto_aead_xchacha20poly1305_ietf_MESSAGEBYTES_MAX;
}

void
crypto_aead_xchacha20poly1305_ietf_keygen(unsigned char k[crypto_aead_xchacha20poly1305_ietf_KEYBYTES])
{
    randombytes_buf(k, crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
}
