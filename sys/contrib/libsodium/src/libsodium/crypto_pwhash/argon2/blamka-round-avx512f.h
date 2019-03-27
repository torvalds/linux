#ifndef blamka_round_avx512f_H
#define blamka_round_avx512f_H

#include "private/common.h"
#include "private/sse2_64_32.h"

#define ror64(x, n) _mm512_ror_epi64((x), (n))

static inline __m512i
muladd(__m512i x, __m512i y)
{
    __m512i z = _mm512_mul_epu32(x, y);

    return _mm512_add_epi64(_mm512_add_epi64(x, y), _mm512_add_epi64(z, z));
}

#define G1_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        A0 = muladd(A0, B0); \
        A1 = muladd(A1, B1); \
        \
        D0 = _mm512_xor_si512(D0, A0); \
        D1 = _mm512_xor_si512(D1, A1); \
        \
        D0 = ror64(D0, 32); \
        D1 = ror64(D1, 32); \
        \
        C0 = muladd(C0, D0); \
        C1 = muladd(C1, D1); \
        \
        B0 = _mm512_xor_si512(B0, C0); \
        B1 = _mm512_xor_si512(B1, C1); \
        \
        B0 = ror64(B0, 24); \
        B1 = ror64(B1, 24); \
    } while ((void)0, 0)

#define G2_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        A0 = muladd(A0, B0); \
        A1 = muladd(A1, B1); \
        \
        D0 = _mm512_xor_si512(D0, A0); \
        D1 = _mm512_xor_si512(D1, A1); \
        \
        D0 = ror64(D0, 16); \
        D1 = ror64(D1, 16); \
        \
        C0 = muladd(C0, D0); \
        C1 = muladd(C1, D1); \
        \
        B0 = _mm512_xor_si512(B0, C0); \
        B1 = _mm512_xor_si512(B1, C1); \
        \
        B0 = ror64(B0, 63); \
        B1 = ror64(B1, 63); \
    } while ((void)0, 0)

#define DIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        B0 = _mm512_permutex_epi64(B0, _MM_SHUFFLE(0, 3, 2, 1)); \
        B1 = _mm512_permutex_epi64(B1, _MM_SHUFFLE(0, 3, 2, 1)); \
        \
        C0 = _mm512_permutex_epi64(C0, _MM_SHUFFLE(1, 0, 3, 2)); \
        C1 = _mm512_permutex_epi64(C1, _MM_SHUFFLE(1, 0, 3, 2)); \
        \
        D0 = _mm512_permutex_epi64(D0, _MM_SHUFFLE(2, 1, 0, 3)); \
        D1 = _mm512_permutex_epi64(D1, _MM_SHUFFLE(2, 1, 0, 3)); \
    } while ((void)0, 0)

#define UNDIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        B0 = _mm512_permutex_epi64(B0, _MM_SHUFFLE(2, 1, 0, 3)); \
        B1 = _mm512_permutex_epi64(B1, _MM_SHUFFLE(2, 1, 0, 3)); \
        \
        C0 = _mm512_permutex_epi64(C0, _MM_SHUFFLE(1, 0, 3, 2)); \
        C1 = _mm512_permutex_epi64(C1, _MM_SHUFFLE(1, 0, 3, 2)); \
        \
        D0 = _mm512_permutex_epi64(D0, _MM_SHUFFLE(0, 3, 2, 1)); \
        D1 = _mm512_permutex_epi64(D1, _MM_SHUFFLE(0, 3, 2, 1)); \
    } while ((void)0, 0)

#define BLAKE2_ROUND(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        G1_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1); \
        G2_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1); \
        \
        DIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1); \
        \
        G1_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1); \
        G2_AVX512F(A0, B0, C0, D0, A1, B1, C1, D1); \
        \
        UNDIAGONALIZE(A0, B0, C0, D0, A1, B1, C1, D1); \
    } while ((void)0, 0)

#define SWAP_HALVES(A0, A1) \
    do { \
        __m512i t0, t1; \
        t0 = _mm512_shuffle_i64x2(A0, A1, _MM_SHUFFLE(1, 0, 1, 0)); \
        t1 = _mm512_shuffle_i64x2(A0, A1, _MM_SHUFFLE(3, 2, 3, 2)); \
        A0 = t0; \
        A1 = t1; \
    } while((void)0, 0)

#define SWAP_QUARTERS(A0, A1) \
    do { \
        SWAP_HALVES(A0, A1); \
        A0 = _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 1, 4, 5, 2, 3, 6, 7), A0); \
        A1 = _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 1, 4, 5, 2, 3, 6, 7), A1); \
    } while((void)0, 0)

#define UNSWAP_QUARTERS(A0, A1) \
    do { \
        A0 = _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 1, 4, 5, 2, 3, 6, 7), A0); \
        A1 = _mm512_permutexvar_epi64(_mm512_setr_epi64(0, 1, 4, 5, 2, 3, 6, 7), A1); \
        SWAP_HALVES(A0, A1); \
    } while((void)0, 0)

#define BLAKE2_ROUND_1(A0, C0, B0, D0, A1, C1, B1, D1) \
    do { \
        SWAP_HALVES(A0, B0); \
        SWAP_HALVES(C0, D0); \
        SWAP_HALVES(A1, B1); \
        SWAP_HALVES(C1, D1); \
        BLAKE2_ROUND(A0, B0, C0, D0, A1, B1, C1, D1); \
        SWAP_HALVES(A0, B0); \
        SWAP_HALVES(C0, D0); \
        SWAP_HALVES(A1, B1); \
        SWAP_HALVES(C1, D1); \
    } while ((void)0, 0)

#define BLAKE2_ROUND_2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do { \
        SWAP_QUARTERS(A0, A1); \
        SWAP_QUARTERS(B0, B1); \
        SWAP_QUARTERS(C0, C1); \
        SWAP_QUARTERS(D0, D1); \
        BLAKE2_ROUND(A0, B0, C0, D0, A1, B1, C1, D1); \
        UNSWAP_QUARTERS(A0, A1); \
        UNSWAP_QUARTERS(B0, B1); \
        UNSWAP_QUARTERS(C0, C1); \
        UNSWAP_QUARTERS(D0, D1); \
    } while ((void)0, 0)

#endif
