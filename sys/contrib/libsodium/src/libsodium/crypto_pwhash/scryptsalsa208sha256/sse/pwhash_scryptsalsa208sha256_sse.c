/*-
 * Copyright 2009 Colin Percival
 * Copyright 2012,2013 Alexander Peslyak
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
 * This file was originally written by Colin Percival as part of the Tarsnap
 * online backup system.
 */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "private/common.h"
#include "private/sse2_64_32.h"

#ifdef HAVE_EMMINTRIN_H

# ifdef __GNUC__
#  pragma GCC target("sse2")
# endif
# include <emmintrin.h>
# if defined(__XOP__) && defined(DISABLED)
#  include <x86intrin.h>
# endif

# include "../crypto_scrypt.h"
# include "../pbkdf2-sha256.h"

# if defined(__XOP__) && defined(DISABLED)
#  define ARX(out, in1, in2, s) \
    out = _mm_xor_si128(out, _mm_roti_epi32(_mm_add_epi32(in1, in2), s));
# else
#  define ARX(out, in1, in2, s)                                    \
    {                                                              \
        __m128i T = _mm_add_epi32(in1, in2);                       \
        out       = _mm_xor_si128(out, _mm_slli_epi32(T, s));      \
        out       = _mm_xor_si128(out, _mm_srli_epi32(T, 32 - s)); \
    }
# endif

# define SALSA20_2ROUNDS              \
    /* Operate on "columns". */       \
    ARX(X1, X0, X3, 7)                \
    ARX(X2, X1, X0, 9)                \
    ARX(X3, X2, X1, 13)               \
    ARX(X0, X3, X2, 18)               \
                                      \
    /* Rearrange data. */             \
    X1 = _mm_shuffle_epi32(X1, 0x93); \
    X2 = _mm_shuffle_epi32(X2, 0x4E); \
    X3 = _mm_shuffle_epi32(X3, 0x39); \
                                      \
    /* Operate on "rows". */          \
    ARX(X3, X0, X1, 7)                \
    ARX(X2, X3, X0, 9)                \
    ARX(X1, X2, X3, 13)               \
    ARX(X0, X1, X2, 18)               \
                                      \
    /* Rearrange data. */             \
    X1 = _mm_shuffle_epi32(X1, 0x39); \
    X2 = _mm_shuffle_epi32(X2, 0x4E); \
    X3 = _mm_shuffle_epi32(X3, 0x93);

/**
 * Apply the salsa20/8 core to the block provided in (X0 ... X3) ^ (Z0 ... Z3).
 */
# define SALSA20_8_XOR(in, out)                               \
    {                                                         \
        __m128i Y0 = X0 = _mm_xor_si128(X0, (in)[0]);         \
        __m128i Y1 = X1 = _mm_xor_si128(X1, (in)[1]);         \
        __m128i Y2 = X2 = _mm_xor_si128(X2, (in)[2]);         \
        __m128i Y3 = X3 = _mm_xor_si128(X3, (in)[3]);         \
        SALSA20_2ROUNDS                                       \
        SALSA20_2ROUNDS                                       \
        SALSA20_2ROUNDS                                       \
        SALSA20_2ROUNDS(out)[0] = X0 = _mm_add_epi32(X0, Y0); \
        (out)[1] = X1 = _mm_add_epi32(X1, Y1);                \
        (out)[2] = X2 = _mm_add_epi32(X2, Y2);                \
        (out)[3] = X3 = _mm_add_epi32(X3, Y3);                \
    }

/**
 * blockmix_salsa8(Bin, Bout, r):
 * Compute Bout = BlockMix_{salsa20/8, r}(Bin).  The input Bin must be 128r
 * bytes in length; the output Bout must also be the same size.
 */
static inline void
blockmix_salsa8(const __m128i *Bin, __m128i *Bout, size_t r)
{
    __m128i X0, X1, X2, X3;
    size_t  i;

    /* 1: X <-- B_{2r - 1} */
    X0 = Bin[8 * r - 4];
    X1 = Bin[8 * r - 3];
    X2 = Bin[8 * r - 2];
    X3 = Bin[8 * r - 1];

    /* 3: X <-- H(X \xor B_i) */
    /* 4: Y_i <-- X */
    /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
    SALSA20_8_XOR(Bin, Bout)

    /* 2: for i = 0 to 2r - 1 do */
    r--;
    for (i = 0; i < r;) {
        /* 3: X <-- H(X \xor B_i) */
        /* 4: Y_i <-- X */
        /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
        SALSA20_8_XOR(&Bin[i * 8 + 4], &Bout[(r + i) * 4 + 4])

        i++;

        /* 3: X <-- H(X \xor B_i) */
        /* 4: Y_i <-- X */
        /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
        SALSA20_8_XOR(&Bin[i * 8], &Bout[i * 4])
    }

    /* 3: X <-- H(X \xor B_i) */
    /* 4: Y_i <-- X */
    /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
    SALSA20_8_XOR(&Bin[i * 8 + 4], &Bout[(r + i) * 4 + 4])
}

# define XOR4(in)                    \
    X0 = _mm_xor_si128(X0, (in)[0]); \
    X1 = _mm_xor_si128(X1, (in)[1]); \
    X2 = _mm_xor_si128(X2, (in)[2]); \
    X3 = _mm_xor_si128(X3, (in)[3]);

# define XOR4_2(in1, in2)                   \
    X0 = _mm_xor_si128((in1)[0], (in2)[0]); \
    X1 = _mm_xor_si128((in1)[1], (in2)[1]); \
    X2 = _mm_xor_si128((in1)[2], (in2)[2]); \
    X3 = _mm_xor_si128((in1)[3], (in2)[3]);

static inline uint32_t
blockmix_salsa8_xor(const __m128i *Bin1, const __m128i *Bin2, __m128i *Bout,
                    size_t r)
{
    __m128i X0, X1, X2, X3;
    size_t  i;

    /* 1: X <-- B_{2r - 1} */
    XOR4_2(&Bin1[8 * r - 4], &Bin2[8 * r - 4])

    /* 3: X <-- H(X \xor B_i) */
    /* 4: Y_i <-- X */
    /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
    XOR4(Bin1)
    SALSA20_8_XOR(Bin2, Bout)

    /* 2: for i = 0 to 2r - 1 do */
    r--;
    for (i = 0; i < r;) {
        /* 3: X <-- H(X \xor B_i) */
        /* 4: Y_i <-- X */
        /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
        XOR4(&Bin1[i * 8 + 4])
        SALSA20_8_XOR(&Bin2[i * 8 + 4], &Bout[(r + i) * 4 + 4])

        i++;

        /* 3: X <-- H(X \xor B_i) */
        /* 4: Y_i <-- X */
        /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
        XOR4(&Bin1[i * 8])
        SALSA20_8_XOR(&Bin2[i * 8], &Bout[i * 4])
    }

    /* 3: X <-- H(X \xor B_i) */
    /* 4: Y_i <-- X */
    /* 6: B' <-- (Y_0, Y_2 ... Y_{2r-2}, Y_1, Y_3 ... Y_{2r-1}) */
    XOR4(&Bin1[i * 8 + 4])
    SALSA20_8_XOR(&Bin2[i * 8 + 4], &Bout[(r + i) * 4 + 4])

    return _mm_cvtsi128_si32(X0);
}

# undef ARX
# undef SALSA20_2ROUNDS
# undef SALSA20_8_XOR
# undef XOR4
# undef XOR4_2

/**
 * integerify(B, r):
 * Return the result of parsing B_{2r-1} as a little-endian integer.
 * Note that B's layout is permuted compared to the generic implementation.
 */
static inline uint32_t
integerify(const void *B, size_t r)
{
    return *(const uint32_t *) ((uintptr_t)(B) + (2 * r - 1) * 64);
}

/**
 * smix(B, r, N, V, XY):
 * Compute B = SMix_r(B, N).  The input B must be 128r bytes in length;
 * the temporary storage V must be 128rN bytes in length; the temporary
 * storage XY must be 256r + 64 bytes in length.  The value N must be a
 * power of 2 greater than 1.  The arrays B, V, and XY must be aligned to a
 * multiple of 64 bytes.
 */
static void
smix(uint8_t *B, size_t r, uint32_t N, void *V, void *XY)
{
    size_t    s   = 128 * r;
    __m128i * X   = (__m128i *) V, *Y;
    uint32_t *X32 = (uint32_t *) V;
    uint32_t  i, j;
    size_t    k;

    /* 1: X <-- B */
    /* 3: V_i <-- X */
    for (k = 0; k < 2 * r; k++) {
        for (i = 0; i < 16; i++) {
            X32[k * 16 + i] = LOAD32_LE(&B[(k * 16 + (i * 5 % 16)) * 4]);
        }
    }

    /* 2: for i = 0 to N - 1 do */
    for (i = 1; i < N - 1; i += 2) {
        /* 4: X <-- H(X) */
        /* 3: V_i <-- X */
        Y = (__m128i *) ((uintptr_t)(V) + i * s);
        blockmix_salsa8(X, Y, r);

        /* 4: X <-- H(X) */
        /* 3: V_i <-- X */
        X = (__m128i *) ((uintptr_t)(V) + (i + 1) * s);
        blockmix_salsa8(Y, X, r);
    }

    /* 4: X <-- H(X) */
    /* 3: V_i <-- X */
    Y = (__m128i *) ((uintptr_t)(V) + i * s);
    blockmix_salsa8(X, Y, r);

    /* 4: X <-- H(X) */
    /* 3: V_i <-- X */
    X = (__m128i *) XY;
    blockmix_salsa8(Y, X, r);

    X32 = (uint32_t *) XY;
    Y   = (__m128i *) ((uintptr_t)(XY) + s);

    /* 7: j <-- Integerify(X) mod N */
    j = integerify(X, r) & (N - 1);

    /* 6: for i = 0 to N - 1 do */
    for (i = 0; i < N; i += 2) {
        __m128i *V_j = (__m128i *) ((uintptr_t)(V) + j * s);

        /* 8: X <-- H(X \xor V_j) */
        /* 7: j <-- Integerify(X) mod N */
        j   = blockmix_salsa8_xor(X, V_j, Y, r) & (N - 1);
        V_j = (__m128i *) ((uintptr_t)(V) + j * s);

        /* 8: X <-- H(X \xor V_j) */
        /* 7: j <-- Integerify(X) mod N */
        j = blockmix_salsa8_xor(Y, V_j, X, r) & (N - 1);
    }

    /* 10: B' <-- X */
    for (k = 0; k < 2 * r; k++) {
        for (i = 0; i < 16; i++) {
            STORE32_LE(&B[(k * 16 + (i * 5 % 16)) * 4], X32[k * 16 + i]);
        }
    }
}

/**
 * escrypt_kdf(local, passwd, passwdlen, salt, saltlen,
 *     N, r, p, buf, buflen):
 * Compute scrypt(passwd[0 .. passwdlen - 1], salt[0 .. saltlen - 1], N, r,
 * p, buflen) and write the result into buf.  The parameters r, p, and buflen
 * must satisfy r * p < 2^30 and buflen <= (2^32 - 1) * 32.  The parameter N
 * must be a power of 2 greater than 1.
 *
 * Return 0 on success; or -1 on error.
 */
int
escrypt_kdf_sse(escrypt_local_t *local, const uint8_t *passwd, size_t passwdlen,
                const uint8_t *salt, size_t saltlen, uint64_t N, uint32_t _r,
                uint32_t _p, uint8_t *buf, size_t buflen)
{
    size_t    B_size, V_size, XY_size, need;
    uint8_t * B;
    uint32_t *V, *XY;
    size_t    r = _r, p = _p;
    uint32_t  i;

/* Sanity-check parameters. */
# if SIZE_MAX > UINT32_MAX
/* LCOV_EXCL_START */
    if (buflen > (((uint64_t)(1) << 32) - 1) * 32) {
        errno = EFBIG;
        return -1;
    }
/* LCOV_EXCL_END */
# endif
    if ((uint64_t)(r) * (uint64_t)(p) >= ((uint64_t) 1 << 30)) {
        errno = EFBIG;
        return -1;
    }
    if (N > UINT32_MAX) {
        errno = EFBIG;
        return -1;
    }
    if (((N & (N - 1)) != 0) || (N < 2)) {
        errno = EINVAL;
        return -1;
    }
    if (r == 0 || p == 0) {
        errno = EINVAL;
        return -1;
    }
/* LCOV_EXCL_START */
    if ((r > SIZE_MAX / 128 / p) ||
# if SIZE_MAX / 256 <= UINT32_MAX
        (r > SIZE_MAX / 256) ||
# endif
        (N > SIZE_MAX / 128 / r)) {
        errno = ENOMEM;
        return -1;
    }
/* LCOV_EXCL_END */

    /* Allocate memory. */
    B_size = (size_t) 128 * r * p;
    V_size = (size_t) 128 * r * N;
    need   = B_size + V_size;
/* LCOV_EXCL_START */
    if (need < V_size) {
        errno = ENOMEM;
        return -1;
    }
/* LCOV_EXCL_END */
    XY_size = (size_t) 256 * r + 64;
    need += XY_size;
/* LCOV_EXCL_START */
    if (need < XY_size) {
        errno = ENOMEM;
        return -1;
    }
/* LCOV_EXCL_END */
    if (local->size < need) {
        if (free_region(local)) {
            return -1; /* LCOV_EXCL_LINE */
        }
        if (!alloc_region(local, need)) {
            return -1; /* LCOV_EXCL_LINE */
        }
    }
    B  = (uint8_t *) local->aligned;
    V  = (uint32_t *) ((uint8_t *) B + B_size);
    XY = (uint32_t *) ((uint8_t *) V + V_size);

    /* 1: (B_0 ... B_{p-1}) <-- PBKDF2(P, S, 1, p * MFLen) */
    PBKDF2_SHA256(passwd, passwdlen, salt, saltlen, 1, B, B_size);

    /* 2: for i = 0 to p - 1 do */
    for (i = 0; i < p; i++) {
        /* 3: B_i <-- MF(B_i, N) */
        smix(&B[(size_t) 128 * i * r], r, (uint32_t) N, V, XY);
    }

    /* 5: DK <-- PBKDF2(P, B, 1, dkLen) */
    PBKDF2_SHA256(passwd, passwdlen, B, B_size, 1, buf, buflen);

    /* Success! */
    return 0;
}
#endif
