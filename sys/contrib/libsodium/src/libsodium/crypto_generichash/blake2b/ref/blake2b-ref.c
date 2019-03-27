/*
   BLAKE2 reference source code package - C implementations

   Written in 2012 by Samuel Neves <sneves@dei.uc.pt>

   To the extent possible under law, the author(s) have dedicated all copyright
   and related and neighboring rights to this software to the public domain
   worldwide. This software is distributed without any warranty.

   You should have received a copy of the CC0 Public Domain Dedication along
   with
   this software. If not, see
   <http://creativecommons.org/publicdomain/zero/1.0/>.
*/

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "blake2.h"
#include "core.h"
#include "private/common.h"
#include "runtime.h"
#include "utils.h"

static blake2b_compress_fn blake2b_compress = blake2b_compress_ref;

static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

/* LCOV_EXCL_START */
static inline int
blake2b_set_lastnode(blake2b_state *S)
{
    S->f[1] = -1;
    return 0;
}
/* LCOV_EXCL_STOP */

static inline int
blake2b_is_lastblock(const blake2b_state *S)
{
    return S->f[0] != 0;
}

static inline int
blake2b_set_lastblock(blake2b_state *S)
{
    if (S->last_node) {
        blake2b_set_lastnode(S);
    }
    S->f[0] = -1;
    return 0;
}

static inline int
blake2b_increment_counter(blake2b_state *S, const uint64_t inc)
{
#ifdef HAVE_TI_MODE
    uint128_t t = ((uint128_t) S->t[1] << 64) | S->t[0];
    t += inc;
    S->t[0] = (uint64_t)(t >> 0);
    S->t[1] = (uint64_t)(t >> 64);
#else
    S->t[0] += inc;
    S->t[1] += (S->t[0] < inc);
#endif
    return 0;
}

/* Parameter-related functions */
static inline int
blake2b_param_set_salt(blake2b_param *P, const uint8_t salt[BLAKE2B_SALTBYTES])
{
    memcpy(P->salt, salt, BLAKE2B_SALTBYTES);
    return 0;
}

static inline int
blake2b_param_set_personal(blake2b_param *P,
                           const uint8_t  personal[BLAKE2B_PERSONALBYTES])
{
    memcpy(P->personal, personal, BLAKE2B_PERSONALBYTES);
    return 0;
}

static inline int
blake2b_init0(blake2b_state *S)
{
    int i;

    for (i  = 0; i < 8; i++) {
        S->h[i] = blake2b_IV[i];
    }
    memset(S->t, 0, offsetof(blake2b_state, last_node) + sizeof(S->last_node)
           - offsetof(blake2b_state, t));
    return 0;
}

/* init xors IV with input parameter block */
int
blake2b_init_param(blake2b_state *S, const blake2b_param *P)
{
    size_t         i;
    const uint8_t *p;

    COMPILER_ASSERT(sizeof *P == 64);
    blake2b_init0(S);
    p = (const uint8_t *) (P);

    /* IV XOR ParamBlock */
    for (i = 0; i < 8; i++) {
        S->h[i] ^= LOAD64_LE(p + sizeof(S->h[i]) * i);
    }
    return 0;
}

int
blake2b_init(blake2b_state *S, const uint8_t outlen)
{
    blake2b_param P[1];

    if ((!outlen) || (outlen > BLAKE2B_OUTBYTES)) {
        sodium_misuse();
    }
    P->digest_length = outlen;
    P->key_length    = 0;
    P->fanout        = 1;
    P->depth         = 1;
    STORE32_LE(P->leaf_length, 0);
    STORE64_LE(P->node_offset, 0);
    P->node_depth   = 0;
    P->inner_length = 0;
    memset(P->reserved, 0, sizeof(P->reserved));
    memset(P->salt, 0, sizeof(P->salt));
    memset(P->personal, 0, sizeof(P->personal));
    return blake2b_init_param(S, P);
}

int
blake2b_init_salt_personal(blake2b_state *S, const uint8_t outlen,
                           const void *salt, const void *personal)
{
    blake2b_param P[1];

    if ((!outlen) || (outlen > BLAKE2B_OUTBYTES)) {
        sodium_misuse();
    }
    P->digest_length = outlen;
    P->key_length    = 0;
    P->fanout        = 1;
    P->depth         = 1;
    STORE32_LE(P->leaf_length, 0);
    STORE64_LE(P->node_offset, 0);
    P->node_depth   = 0;
    P->inner_length = 0;
    memset(P->reserved, 0, sizeof(P->reserved));
    if (salt != NULL) {
        blake2b_param_set_salt(P, (const uint8_t *) salt);
    } else {
        memset(P->salt, 0, sizeof(P->salt));
    }
    if (personal != NULL) {
        blake2b_param_set_personal(P, (const uint8_t *) personal);
    } else {
        memset(P->personal, 0, sizeof(P->personal));
    }
    return blake2b_init_param(S, P);
}

int
blake2b_init_key(blake2b_state *S, const uint8_t outlen, const void *key,
                 const uint8_t keylen)
{
    blake2b_param P[1];

    if ((!outlen) || (outlen > BLAKE2B_OUTBYTES)) {
        sodium_misuse();
    }
    if (!key || !keylen || keylen > BLAKE2B_KEYBYTES) {
        sodium_misuse();
    }
    P->digest_length = outlen;
    P->key_length    = keylen;
    P->fanout        = 1;
    P->depth         = 1;
    STORE32_LE(P->leaf_length, 0);
    STORE64_LE(P->node_offset, 0);
    P->node_depth   = 0;
    P->inner_length = 0;
    memset(P->reserved, 0, sizeof(P->reserved));
    memset(P->salt, 0, sizeof(P->salt));
    memset(P->personal, 0, sizeof(P->personal));

    if (blake2b_init_param(S, P) < 0) {
        sodium_misuse();
    }
    {
        uint8_t block[BLAKE2B_BLOCKBYTES];
        memset(block, 0, BLAKE2B_BLOCKBYTES);
        memcpy(block, key, keylen); /* keylen cannot be 0 */
        blake2b_update(S, block, BLAKE2B_BLOCKBYTES);
        sodium_memzero(block, BLAKE2B_BLOCKBYTES); /* Burn the key from stack */
    }
    return 0;
}

int
blake2b_init_key_salt_personal(blake2b_state *S, const uint8_t outlen,
                               const void *key, const uint8_t keylen,
                               const void *salt, const void *personal)
{
    blake2b_param P[1];

    if ((!outlen) || (outlen > BLAKE2B_OUTBYTES)) {
        sodium_misuse();
    }
    if (!key || !keylen || keylen > BLAKE2B_KEYBYTES) {
        sodium_misuse();
    }
    P->digest_length = outlen;
    P->key_length    = keylen;
    P->fanout        = 1;
    P->depth         = 1;
    STORE32_LE(P->leaf_length, 0);
    STORE64_LE(P->node_offset, 0);
    P->node_depth   = 0;
    P->inner_length = 0;
    memset(P->reserved, 0, sizeof(P->reserved));
    if (salt != NULL) {
        blake2b_param_set_salt(P, (const uint8_t *) salt);
    } else {
        memset(P->salt, 0, sizeof(P->salt));
    }
    if (personal != NULL) {
        blake2b_param_set_personal(P, (const uint8_t *) personal);
    } else {
        memset(P->personal, 0, sizeof(P->personal));
    }

    if (blake2b_init_param(S, P) < 0) {
        sodium_misuse();
    }
    {
        uint8_t block[BLAKE2B_BLOCKBYTES];
        memset(block, 0, BLAKE2B_BLOCKBYTES);
        memcpy(block, key, keylen); /* keylen cannot be 0 */
        blake2b_update(S, block, BLAKE2B_BLOCKBYTES);
        sodium_memzero(block, BLAKE2B_BLOCKBYTES); /* Burn the key from stack */
    }
    return 0;
}

/* inlen now in bytes */
int
blake2b_update(blake2b_state *S, const uint8_t *in, uint64_t inlen)
{
    while (inlen > 0) {
        size_t left = S->buflen;
        size_t fill = 2 * BLAKE2B_BLOCKBYTES - left;

        if (inlen > fill) {
            memcpy(S->buf + left, in, fill); /* Fill buffer */
            S->buflen += fill;
            blake2b_increment_counter(S, BLAKE2B_BLOCKBYTES);
            blake2b_compress(S, S->buf); /* Compress */
            memcpy(S->buf, S->buf + BLAKE2B_BLOCKBYTES,
                   BLAKE2B_BLOCKBYTES); /* Shift buffer left */
            S->buflen -= BLAKE2B_BLOCKBYTES;
            in += fill;
            inlen -= fill;
        } else /* inlen <= fill */
        {
            memcpy(S->buf + left, in, inlen);
            S->buflen += inlen; /* Be lazy, do not compress */
            in += inlen;
            inlen -= inlen;
        }
    }

    return 0;
}

int
blake2b_final(blake2b_state *S, uint8_t *out, uint8_t outlen)
{
    unsigned char buffer[BLAKE2B_OUTBYTES];

    if (!outlen || outlen > BLAKE2B_OUTBYTES) {
        sodium_misuse();
    }
    if (blake2b_is_lastblock(S)) {
        return -1;
    }
    if (S->buflen > BLAKE2B_BLOCKBYTES) {
        blake2b_increment_counter(S, BLAKE2B_BLOCKBYTES);
        blake2b_compress(S, S->buf);
        S->buflen -= BLAKE2B_BLOCKBYTES;
        assert(S->buflen <= BLAKE2B_BLOCKBYTES);
        memcpy(S->buf, S->buf + BLAKE2B_BLOCKBYTES, S->buflen);
    }

    blake2b_increment_counter(S, S->buflen);
    blake2b_set_lastblock(S);
    memset(S->buf + S->buflen, 0,
           2 * BLAKE2B_BLOCKBYTES - S->buflen); /* Padding */
    blake2b_compress(S, S->buf);

    COMPILER_ASSERT(sizeof buffer == 64U);
    STORE64_LE(buffer + 8 * 0, S->h[0]);
    STORE64_LE(buffer + 8 * 1, S->h[1]);
    STORE64_LE(buffer + 8 * 2, S->h[2]);
    STORE64_LE(buffer + 8 * 3, S->h[3]);
    STORE64_LE(buffer + 8 * 4, S->h[4]);
    STORE64_LE(buffer + 8 * 5, S->h[5]);
    STORE64_LE(buffer + 8 * 6, S->h[6]);
    STORE64_LE(buffer + 8 * 7, S->h[7]);
    memcpy(out, buffer, outlen); /* outlen <= BLAKE2B_OUTBYTES (64) */

    sodium_memzero(S->h, sizeof S->h);
    sodium_memzero(S->buf, sizeof S->buf);

    return 0;
}

/* inlen, at least, should be uint64_t. Others can be size_t. */
int
blake2b(uint8_t *out, const void *in, const void *key, const uint8_t outlen,
        const uint64_t inlen, uint8_t keylen)
{
    CRYPTO_ALIGN(64) blake2b_state S[1];

    /* Verify parameters */
    if (NULL == in && inlen > 0) {
        sodium_misuse();
    }
    if (NULL == out) {
        sodium_misuse();
    }
    if (!outlen || outlen > BLAKE2B_OUTBYTES) {
        sodium_misuse();
    }
    if (NULL == key && keylen > 0) {
        sodium_misuse();
    }
    if (keylen > BLAKE2B_KEYBYTES) {
        sodium_misuse();
    }
    if (keylen > 0) {
        if (blake2b_init_key(S, outlen, key, keylen) < 0) {
            sodium_misuse();
        }
    } else {
        if (blake2b_init(S, outlen) < 0) {
            sodium_misuse();
        }
    }

    blake2b_update(S, (const uint8_t *) in, inlen);
    blake2b_final(S, out, outlen);
    return 0;
}

int
blake2b_salt_personal(uint8_t *out, const void *in, const void *key,
                      const uint8_t outlen, const uint64_t inlen,
                      uint8_t keylen, const void *salt, const void *personal)
{
    CRYPTO_ALIGN(64) blake2b_state S[1];

    /* Verify parameters */
    if (NULL == in && inlen > 0) {
        sodium_misuse();
    }
    if (NULL == out) {
        sodium_misuse();
    }
    if (!outlen || outlen > BLAKE2B_OUTBYTES) {
        sodium_misuse();
    }
    if (NULL == key && keylen > 0) {
        sodium_misuse();
    }
    if (keylen > BLAKE2B_KEYBYTES) {
        sodium_misuse();
    }
    if (keylen > 0) {
        if (blake2b_init_key_salt_personal(S, outlen, key, keylen, salt,
                                           personal) < 0) {
            sodium_misuse();
        }
    } else {
        if (blake2b_init_salt_personal(S, outlen, salt, personal) < 0) {
            sodium_misuse();
        }
    }

    blake2b_update(S, (const uint8_t *) in, inlen);
    blake2b_final(S, out, outlen);
    return 0;
}

int
blake2b_pick_best_implementation(void)
{
/* LCOV_EXCL_START */
#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_TMMINTRIN_H) && \
    defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_avx2()) {
        blake2b_compress = blake2b_compress_avx2;
        return 0;
    }
#endif
#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H) && \
    defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_sse41()) {
        blake2b_compress = blake2b_compress_sse41;
        return 0;
    }
#endif
#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H)
    if (sodium_runtime_has_ssse3()) {
        blake2b_compress = blake2b_compress_ssse3;
        return 0;
    }
#endif
    blake2b_compress = blake2b_compress_ref;

    return 0;
    /* LCOV_EXCL_STOP */
}
