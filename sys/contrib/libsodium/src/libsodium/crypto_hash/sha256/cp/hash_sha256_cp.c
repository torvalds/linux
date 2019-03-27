
/*-
 * Copyright 2005,2007,2009 Colin Percival
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "crypto_hash_sha256.h"
#include "private/common.h"
#include "utils.h"

static void
be32enc_vect(unsigned char *dst, const uint32_t *src, size_t len)
{
    size_t i;

    for (i = 0; i < len / 4; i++) {
        STORE32_BE(dst + i * 4, src[i]);
    }
}

static void
be32dec_vect(uint32_t *dst, const unsigned char *src, size_t len)
{
    size_t i;

    for (i = 0; i < len / 4; i++) {
        dst[i] = LOAD32_BE(src + i * 4);
    }
}

static const uint32_t Krnd[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

#define Ch(x, y, z) ((x & (y ^ z)) ^ z)
#define Maj(x, y, z) ((x & (y | z)) | (y & z))
#define SHR(x, n) (x >> n)
#define ROTR(x, n) ROTR32(x, n)
#define S0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define S1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define s0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ SHR(x, 3))
#define s1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ SHR(x, 10))

#define RND(a, b, c, d, e, f, g, h, k) \
    h += S1(e) + Ch(e, f, g) + k;      \
    d += h;                            \
    h += S0(a) + Maj(a, b, c);

#define RNDr(S, W, i, ii)                                                   \
    RND(S[(64 - i) % 8], S[(65 - i) % 8], S[(66 - i) % 8], S[(67 - i) % 8], \
        S[(68 - i) % 8], S[(69 - i) % 8], S[(70 - i) % 8], S[(71 - i) % 8], \
        W[i + ii] + Krnd[i + ii])

#define MSCH(W, ii, i) \
    W[i + ii + 16] =   \
        s1(W[i + ii + 14]) + W[i + ii + 9] + s0(W[i + ii + 1]) + W[i + ii]

static void
SHA256_Transform(uint32_t state[8], const uint8_t block[64], uint32_t W[64],
                 uint32_t S[8])
{
    int i;

    be32dec_vect(W, block, 64);
    memcpy(S, state, 32);
    for (i = 0; i < 64; i += 16) {
        RNDr(S, W, 0, i);
        RNDr(S, W, 1, i);
        RNDr(S, W, 2, i);
        RNDr(S, W, 3, i);
        RNDr(S, W, 4, i);
        RNDr(S, W, 5, i);
        RNDr(S, W, 6, i);
        RNDr(S, W, 7, i);
        RNDr(S, W, 8, i);
        RNDr(S, W, 9, i);
        RNDr(S, W, 10, i);
        RNDr(S, W, 11, i);
        RNDr(S, W, 12, i);
        RNDr(S, W, 13, i);
        RNDr(S, W, 14, i);
        RNDr(S, W, 15, i);
        if (i == 48) {
            break;
        }
        MSCH(W, 0, i);
        MSCH(W, 1, i);
        MSCH(W, 2, i);
        MSCH(W, 3, i);
        MSCH(W, 4, i);
        MSCH(W, 5, i);
        MSCH(W, 6, i);
        MSCH(W, 7, i);
        MSCH(W, 8, i);
        MSCH(W, 9, i);
        MSCH(W, 10, i);
        MSCH(W, 11, i);
        MSCH(W, 12, i);
        MSCH(W, 13, i);
        MSCH(W, 14, i);
        MSCH(W, 15, i);
    }
    for (i = 0; i < 8; i++) {
        state[i] += S[i];
    }
}

static const uint8_t PAD[64] = { 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

static void
SHA256_Pad(crypto_hash_sha256_state *state, uint32_t tmp32[64 + 8])
{
    unsigned int r;
    unsigned int i;

    r = (unsigned int) ((state->count >> 3) & 0x3f);
    if (r < 56) {
        for (i = 0; i < 56 - r; i++) {
            state->buf[r + i] = PAD[i];
        }
    } else {
        for (i = 0; i < 64 - r; i++) {
            state->buf[r + i] = PAD[i];
        }
        SHA256_Transform(state->state, state->buf, &tmp32[0], &tmp32[64]);
        memset(&state->buf[0], 0, 56);
    }
    STORE64_BE(&state->buf[56], state->count);
    SHA256_Transform(state->state, state->buf, &tmp32[0], &tmp32[64]);
}

int
crypto_hash_sha256_init(crypto_hash_sha256_state *state)
{
    static const uint32_t sha256_initial_state[8] = { 0x6a09e667, 0xbb67ae85,
                                                      0x3c6ef372, 0xa54ff53a,
                                                      0x510e527f, 0x9b05688c,
                                                      0x1f83d9ab, 0x5be0cd19 };

    state->count = (uint64_t) 0U;
    memcpy(state->state, sha256_initial_state, sizeof sha256_initial_state);

    return 0;
}

int
crypto_hash_sha256_update(crypto_hash_sha256_state *state,
                          const unsigned char *in, unsigned long long inlen)
{
    uint32_t           tmp32[64 + 8];
    unsigned long long i;
    unsigned long long r;

    if (inlen <= 0U) {
        return 0;
    }
    r = (unsigned long long) ((state->count >> 3) & 0x3f);

    state->count += ((uint64_t) inlen) << 3;
    if (inlen < 64 - r) {
        for (i = 0; i < inlen; i++) {
            state->buf[r + i] = in[i];
        }
        return 0;
    }
    for (i = 0; i < 64 - r; i++) {
        state->buf[r + i] = in[i];
    }
    SHA256_Transform(state->state, state->buf, &tmp32[0], &tmp32[64]);
    in += 64 - r;
    inlen -= 64 - r;

    while (inlen >= 64) {
        SHA256_Transform(state->state, in, &tmp32[0], &tmp32[64]);
        in += 64;
        inlen -= 64;
    }
    inlen &= 63;
    for (i = 0; i < inlen; i++) {
        state->buf[i] = in[i];
    }
    sodium_memzero((void *) tmp32, sizeof tmp32);

    return 0;
}

int
crypto_hash_sha256_final(crypto_hash_sha256_state *state, unsigned char *out)
{
    uint32_t tmp32[64 + 8];

    SHA256_Pad(state, tmp32);
    be32enc_vect(out, state->state, 32);
    sodium_memzero((void *) tmp32, sizeof tmp32);
    sodium_memzero((void *) state, sizeof *state);

    return 0;
}

int
crypto_hash_sha256(unsigned char *out, const unsigned char *in,
                   unsigned long long inlen)
{
    crypto_hash_sha256_state state;

    crypto_hash_sha256_init(&state);
    crypto_hash_sha256_update(&state, in, inlen);
    crypto_hash_sha256_final(&state, out);

    return 0;
}
