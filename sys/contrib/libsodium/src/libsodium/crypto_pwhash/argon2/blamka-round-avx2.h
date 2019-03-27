#ifndef blamka_round_avx2_H
#define blamka_round_avx2_H

#include "private/common.h"
#include "private/sse2_64_32.h"

#define rotr32(x) _mm256_shuffle_epi32(x, _MM_SHUFFLE(2, 3, 0, 1))
#define rotr24(x) _mm256_shuffle_epi8(x, _mm256_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10, 3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10))
#define rotr16(x) _mm256_shuffle_epi8(x, _mm256_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9, 2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9))
#define rotr63(x) _mm256_xor_si256(_mm256_srli_epi64((x), 63), _mm256_add_epi64((x), (x)))

#define G1_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do { \
        __m256i ml = _mm256_mul_epu32(A0, B0); \
        ml = _mm256_add_epi64(ml, ml); \
        A0 = _mm256_add_epi64(A0, _mm256_add_epi64(B0, ml)); \
        D0 = _mm256_xor_si256(D0, A0); \
        D0 = rotr32(D0); \
        \
        ml = _mm256_mul_epu32(C0, D0); \
        ml = _mm256_add_epi64(ml, ml); \
        C0 = _mm256_add_epi64(C0, _mm256_add_epi64(D0, ml)); \
        \
        B0 = _mm256_xor_si256(B0, C0); \
        B0 = rotr24(B0); \
        \
        ml = _mm256_mul_epu32(A1, B1); \
        ml = _mm256_add_epi64(ml, ml); \
        A1 = _mm256_add_epi64(A1, _mm256_add_epi64(B1, ml)); \
        D1 = _mm256_xor_si256(D1, A1); \
        D1 = rotr32(D1); \
        \
        ml = _mm256_mul_epu32(C1, D1); \
        ml = _mm256_add_epi64(ml, ml); \
        C1 = _mm256_add_epi64(C1, _mm256_add_epi64(D1, ml)); \
        \
        B1 = _mm256_xor_si256(B1, C1); \
        B1 = rotr24(B1); \
    } while((void)0, 0);

#define G2_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do { \
        __m256i ml = _mm256_mul_epu32(A0, B0); \
        ml = _mm256_add_epi64(ml, ml); \
        A0 = _mm256_add_epi64(A0, _mm256_add_epi64(B0, ml)); \
        D0 = _mm256_xor_si256(D0, A0); \
        D0 = rotr16(D0); \
        \
        ml = _mm256_mul_epu32(C0, D0); \
        ml = _mm256_add_epi64(ml, ml); \
        C0 = _mm256_add_epi64(C0, _mm256_add_epi64(D0, ml)); \
        B0 = _mm256_xor_si256(B0, C0); \
        B0 = rotr63(B0); \
        \
        ml = _mm256_mul_epu32(A1, B1); \
        ml = _mm256_add_epi64(ml, ml); \
        A1 = _mm256_add_epi64(A1, _mm256_add_epi64(B1, ml)); \
        D1 = _mm256_xor_si256(D1, A1); \
        D1 = rotr16(D1); \
        \
        ml = _mm256_mul_epu32(C1, D1); \
        ml = _mm256_add_epi64(ml, ml); \
        C1 = _mm256_add_epi64(C1, _mm256_add_epi64(D1, ml)); \
        B1 = _mm256_xor_si256(B1, C1); \
        B1 = rotr63(B1); \
    } while((void)0, 0);

#define DIAGONALIZE_1(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        B0 = _mm256_permute4x64_epi64(B0, _MM_SHUFFLE(0, 3, 2, 1)); \
        C0 = _mm256_permute4x64_epi64(C0, _MM_SHUFFLE(1, 0, 3, 2)); \
        D0 = _mm256_permute4x64_epi64(D0, _MM_SHUFFLE(2, 1, 0, 3)); \
        \
        B1 = _mm256_permute4x64_epi64(B1, _MM_SHUFFLE(0, 3, 2, 1)); \
        C1 = _mm256_permute4x64_epi64(C1, _MM_SHUFFLE(1, 0, 3, 2)); \
        D1 = _mm256_permute4x64_epi64(D1, _MM_SHUFFLE(2, 1, 0, 3)); \
    } while((void)0, 0);

#define DIAGONALIZE_2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do { \
        __m256i tmp1 = _mm256_blend_epi32(B0, B1, 0xCC); \
        __m256i tmp2 = _mm256_blend_epi32(B0, B1, 0x33); \
        B1 = _mm256_permute4x64_epi64(tmp1, _MM_SHUFFLE(2,3,0,1)); \
        B0 = _mm256_permute4x64_epi64(tmp2, _MM_SHUFFLE(2,3,0,1)); \
        \
        tmp1 = C0; \
        C0 = C1; \
        C1 = tmp1; \
        \
        tmp1 = _mm256_blend_epi32(D0, D1, 0xCC); \
        tmp2 = _mm256_blend_epi32(D0, D1, 0x33); \
        D0 = _mm256_permute4x64_epi64(tmp1, _MM_SHUFFLE(2,3,0,1)); \
        D1 = _mm256_permute4x64_epi64(tmp2, _MM_SHUFFLE(2,3,0,1)); \
    } while(0);

#define UNDIAGONALIZE_1(A0, B0, C0, D0, A1, B1, C1, D1) \
    do { \
        B0 = _mm256_permute4x64_epi64(B0, _MM_SHUFFLE(2, 1, 0, 3)); \
        C0 = _mm256_permute4x64_epi64(C0, _MM_SHUFFLE(1, 0, 3, 2)); \
        D0 = _mm256_permute4x64_epi64(D0, _MM_SHUFFLE(0, 3, 2, 1)); \
        \
        B1 = _mm256_permute4x64_epi64(B1, _MM_SHUFFLE(2, 1, 0, 3)); \
        C1 = _mm256_permute4x64_epi64(C1, _MM_SHUFFLE(1, 0, 3, 2)); \
        D1 = _mm256_permute4x64_epi64(D1, _MM_SHUFFLE(0, 3, 2, 1)); \
    } while((void)0, 0);

#define UNDIAGONALIZE_2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do { \
        __m256i tmp1 = _mm256_blend_epi32(B0, B1, 0xCC); \
        __m256i tmp2 = _mm256_blend_epi32(B0, B1, 0x33); \
        B0 = _mm256_permute4x64_epi64(tmp1, _MM_SHUFFLE(2,3,0,1)); \
        B1 = _mm256_permute4x64_epi64(tmp2, _MM_SHUFFLE(2,3,0,1)); \
        \
        tmp1 = C0; \
        C0 = C1; \
        C1 = tmp1; \
        \
        tmp1 = _mm256_blend_epi32(D0, D1, 0x33); \
        tmp2 = _mm256_blend_epi32(D0, D1, 0xCC); \
        D0 = _mm256_permute4x64_epi64(tmp1, _MM_SHUFFLE(2,3,0,1)); \
        D1 = _mm256_permute4x64_epi64(tmp2, _MM_SHUFFLE(2,3,0,1)); \
    } while((void)0, 0);

#define BLAKE2_ROUND_1(A0, A1, B0, B1, C0, C1, D0, D1) \
    do{ \
        G1_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        G2_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        \
        DIAGONALIZE_1(A0, B0, C0, D0, A1, B1, C1, D1) \
        \
        G1_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        G2_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        \
        UNDIAGONALIZE_1(A0, B0, C0, D0, A1, B1, C1, D1) \
    } while((void)0, 0);

#define BLAKE2_ROUND_2(A0, A1, B0, B1, C0, C1, D0, D1) \
    do{ \
        G1_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        G2_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        \
        DIAGONALIZE_2(A0, A1, B0, B1, C0, C1, D0, D1) \
        \
        G1_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        G2_AVX2(A0, A1, B0, B1, C0, C1, D0, D1) \
        \
        UNDIAGONALIZE_2(A0, A1, B0, B1, C0, C1, D0, D1) \
    } while((void)0, 0);

#endif
