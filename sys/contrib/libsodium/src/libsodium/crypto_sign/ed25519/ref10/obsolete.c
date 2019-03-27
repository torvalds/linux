
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "crypto_hash_sha512.h"
#include "crypto_sign_edwards25519sha512batch.h"
#include "crypto_verify_32.h"
#include "private/ed25519_ref10.h"
#include "randombytes.h"
#include "utils.h"

int
crypto_sign_edwards25519sha512batch_keypair(unsigned char *pk,
                                            unsigned char *sk)
{
    ge25519_p3 A;

    randombytes_buf(sk, 32);
    crypto_hash_sha512(sk, sk, 32);
    sk[0] &= 248;
    sk[31] &= 127;
    sk[31] |= 64;
    ge25519_scalarmult_base(&A, sk);
    ge25519_p3_tobytes(pk, &A);

    return 0;
}

int
crypto_sign_edwards25519sha512batch(unsigned char       *sm,
                                    unsigned long long  *smlen_p,
                                    const unsigned char *m,
                                    unsigned long long   mlen,
                                    const unsigned char *sk)
{
    crypto_hash_sha512_state hs;
    unsigned char            nonce[64];
    unsigned char            hram[64];
    unsigned char            sig[64];
    ge25519_p3               A;
    ge25519_p3               R;

    crypto_hash_sha512_init(&hs);
    crypto_hash_sha512_update(&hs, sk + 32, 32);
    crypto_hash_sha512_update(&hs, m, mlen);
    crypto_hash_sha512_final(&hs, nonce);
    ge25519_scalarmult_base(&A, sk);
    ge25519_p3_tobytes(sig + 32, &A);
    sc25519_reduce(nonce);
    ge25519_scalarmult_base(&R, nonce);
    ge25519_p3_tobytes(sig, &R);
    crypto_hash_sha512_init(&hs);
    crypto_hash_sha512_update(&hs, sig, 32);
    crypto_hash_sha512_update(&hs, m, mlen);
    crypto_hash_sha512_final(&hs, hram);
    sc25519_reduce(hram);
    sc25519_muladd(sig + 32, hram, nonce, sk);
    sodium_memzero(hram, sizeof hram);
    memmove(sm + 32, m, (size_t) mlen);
    memcpy(sm, sig, 32);
    memcpy(sm + 32 + mlen, sig + 32, 32);
    *smlen_p = mlen + 64U;

    return 0;
}

int
crypto_sign_edwards25519sha512batch_open(unsigned char       *m,
                                         unsigned long long  *mlen_p,
                                         const unsigned char *sm,
                                         unsigned long long   smlen,
                                         const unsigned char *pk)
{
    unsigned char      h[64];
    unsigned char      t1[32], t2[32];
    unsigned long long mlen;
    ge25519_cached     Ai;
    ge25519_p1p1       csa;
    ge25519_p2         cs;
    ge25519_p3         A;
    ge25519_p3         R;
    ge25519_p3         cs3;

    *mlen_p = 0;
    if (smlen < 64 || smlen - 64 > crypto_sign_edwards25519sha512batch_MESSAGEBYTES_MAX) {
        return -1;
    }
    mlen = smlen - 64;
    if (sm[smlen - 1] & 224) {
        return -1;
    }
    if (ge25519_has_small_order(pk) != 0 ||
        ge25519_frombytes_negate_vartime(&A, pk) != 0 ||
        ge25519_has_small_order(sm) != 0 ||
        ge25519_frombytes_negate_vartime(&R, sm) != 0) {
        return -1;
    }
    ge25519_p3_to_cached(&Ai, &A);
    crypto_hash_sha512(h, sm, mlen + 32);
    sc25519_reduce(h);
    ge25519_scalarmult(&cs3, h, &R);
    ge25519_add(&csa, &cs3, &Ai);
    ge25519_p1p1_to_p2(&cs, &csa);
    ge25519_tobytes(t1, &cs);
    t1[31] ^= 1 << 7;
    ge25519_scalarmult_base(&R, sm + 32 + mlen);
    ge25519_p3_tobytes(t2, &R);
    if (crypto_verify_32(t1, t2) != 0) {
        return -1;
    }
    *mlen_p = mlen;
    memmove(m, sm + 32, mlen);

    return 0;
}
