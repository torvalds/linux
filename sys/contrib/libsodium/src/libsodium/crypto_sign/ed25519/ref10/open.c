
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "crypto_hash_sha512.h"
#include "crypto_sign_ed25519.h"
#include "crypto_verify_32.h"
#include "sign_ed25519_ref10.h"
#include "private/ed25519_ref10.h"
#include "utils.h"

int
_crypto_sign_ed25519_verify_detached(const unsigned char *sig,
                                     const unsigned char *m,
                                     unsigned long long   mlen,
                                     const unsigned char *pk,
                                     int prehashed)
{
    crypto_hash_sha512_state hs;
    unsigned char            h[64];
    unsigned char            rcheck[32];
    ge25519_p3               A;
    ge25519_p2               R;

#ifdef ED25519_COMPAT
    if (sig[63] & 224) {
        return -1;
    }
#else
    if (sc25519_is_canonical(sig + 32) == 0 ||
        ge25519_has_small_order(sig) != 0) {
        return -1;
    }
    if (ge25519_is_canonical(pk) == 0 ||
        ge25519_has_small_order(pk) != 0) {
        return -1;
    }
#endif
    if (ge25519_frombytes_negate_vartime(&A, pk) != 0) {
        return -1;
    }
    _crypto_sign_ed25519_ref10_hinit(&hs, prehashed);
    crypto_hash_sha512_update(&hs, sig, 32);
    crypto_hash_sha512_update(&hs, pk, 32);
    crypto_hash_sha512_update(&hs, m, mlen);
    crypto_hash_sha512_final(&hs, h);
    sc25519_reduce(h);

    ge25519_double_scalarmult_vartime(&R, h, &A, sig + 32);
    ge25519_tobytes(rcheck, &R);

    return crypto_verify_32(rcheck, sig) | (-(rcheck == sig)) |
           sodium_memcmp(sig, rcheck, 32);
}

int
crypto_sign_ed25519_verify_detached(const unsigned char *sig,
                                    const unsigned char *m,
                                    unsigned long long   mlen,
                                    const unsigned char *pk)
{
    return _crypto_sign_ed25519_verify_detached(sig, m, mlen, pk, 0);
}

int
crypto_sign_ed25519_open(unsigned char *m, unsigned long long *mlen_p,
                         const unsigned char *sm, unsigned long long smlen,
                         const unsigned char *pk)
{
    unsigned long long mlen;

    if (smlen < 64 || smlen - 64 > crypto_sign_ed25519_MESSAGEBYTES_MAX) {
        goto badsig;
    }
    mlen = smlen - 64;
    if (crypto_sign_ed25519_verify_detached(sm, sm + 64, mlen, pk) != 0) {
        memset(m, 0, mlen);
        goto badsig;
    }
    if (mlen_p != NULL) {
        *mlen_p = mlen;
    }
    memmove(m, sm + 64, mlen);

    return 0;

badsig:
    if (mlen_p != NULL) {
        *mlen_p = 0;
    }
    return -1;
}
