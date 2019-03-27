
#ifndef blake2b_compress_avx2_H
#define blake2b_compress_avx2_H

#define LOADU128(p) _mm_loadu_si128((__m128i *) (p))
#define STOREU128(p, r) _mm_storeu_si128((__m128i *) (p), r)

#define LOAD(p) _mm256_load_si256((__m256i *) (p))
#define STORE(p, r) _mm256_store_si256((__m256i *) (p), r)

#define LOADU(p) _mm256_loadu_si256((__m256i *) (p))
#define STOREU(p, r) _mm256_storeu_si256((__m256i *) (p), r)

static inline uint64_t
LOADU64(const void *p)
{
    uint64_t v;
    memcpy(&v, p, sizeof v);
    return v;
}

#define ROTATE16                                                              \
    _mm256_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9, 2, \
                     3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9)

#define ROTATE24                                                              \
    _mm256_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10, 3, \
                     4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10)

#define ADD(a, b) _mm256_add_epi64(a, b)
#define SUB(a, b) _mm256_sub_epi64(a, b)

#define XOR(a, b) _mm256_xor_si256(a, b)
#define AND(a, b) _mm256_and_si256(a, b)
#define OR(a, b) _mm256_or_si256(a, b)

#define ROT32(x) _mm256_shuffle_epi32((x), _MM_SHUFFLE(2, 3, 0, 1))
#define ROT24(x) _mm256_shuffle_epi8((x), ROTATE24)
#define ROT16(x) _mm256_shuffle_epi8((x), ROTATE16)
#define ROT63(x) _mm256_or_si256(_mm256_srli_epi64((x), 63), ADD((x), (x)))

#define BLAKE2B_G1_V1(a, b, c, d, m) \
    do {                             \
        a = ADD(a, m);               \
        a = ADD(a, b);               \
        d = XOR(d, a);               \
        d = ROT32(d);                \
        c = ADD(c, d);               \
        b = XOR(b, c);               \
        b = ROT24(b);                \
    } while (0)

#define BLAKE2B_G2_V1(a, b, c, d, m) \
    do {                             \
        a = ADD(a, m);               \
        a = ADD(a, b);               \
        d = XOR(d, a);               \
        d = ROT16(d);                \
        c = ADD(c, d);               \
        b = XOR(b, c);               \
        b = ROT63(b);                \
    } while (0)

#define BLAKE2B_DIAG_V1(a, b, c, d)                               \
    do {                                                          \
        d = _mm256_permute4x64_epi64(d, _MM_SHUFFLE(2, 1, 0, 3)); \
        c = _mm256_permute4x64_epi64(c, _MM_SHUFFLE(1, 0, 3, 2)); \
        b = _mm256_permute4x64_epi64(b, _MM_SHUFFLE(0, 3, 2, 1)); \
    } while (0)

#define BLAKE2B_UNDIAG_V1(a, b, c, d)                             \
    do {                                                          \
        d = _mm256_permute4x64_epi64(d, _MM_SHUFFLE(0, 3, 2, 1)); \
        c = _mm256_permute4x64_epi64(c, _MM_SHUFFLE(1, 0, 3, 2)); \
        b = _mm256_permute4x64_epi64(b, _MM_SHUFFLE(2, 1, 0, 3)); \
    } while (0)

#include "blake2b-load-avx2.h"

#define BLAKE2B_ROUND_V1(a, b, c, d, r, m) \
    do {                                   \
        __m256i b0;                        \
        BLAKE2B_LOAD_MSG_##r##_1(b0);      \
        BLAKE2B_G1_V1(a, b, c, d, b0);     \
        BLAKE2B_LOAD_MSG_##r##_2(b0);      \
        BLAKE2B_G2_V1(a, b, c, d, b0);     \
        BLAKE2B_DIAG_V1(a, b, c, d);       \
        BLAKE2B_LOAD_MSG_##r##_3(b0);      \
        BLAKE2B_G1_V1(a, b, c, d, b0);     \
        BLAKE2B_LOAD_MSG_##r##_4(b0);      \
        BLAKE2B_G2_V1(a, b, c, d, b0);     \
        BLAKE2B_UNDIAG_V1(a, b, c, d);     \
    } while (0)

#define BLAKE2B_ROUNDS_V1(a, b, c, d, m)       \
    do {                                       \
        BLAKE2B_ROUND_V1(a, b, c, d, 0, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 1, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 2, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 3, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 4, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 5, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 6, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 7, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 8, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 9, (m));  \
        BLAKE2B_ROUND_V1(a, b, c, d, 10, (m)); \
        BLAKE2B_ROUND_V1(a, b, c, d, 11, (m)); \
    } while (0)

#define DECLARE_MESSAGE_WORDS(m)                                         \
    const __m256i m0 = _mm256_broadcastsi128_si256(LOADU128((m) + 0));   \
    const __m256i m1 = _mm256_broadcastsi128_si256(LOADU128((m) + 16));  \
    const __m256i m2 = _mm256_broadcastsi128_si256(LOADU128((m) + 32));  \
    const __m256i m3 = _mm256_broadcastsi128_si256(LOADU128((m) + 48));  \
    const __m256i m4 = _mm256_broadcastsi128_si256(LOADU128((m) + 64));  \
    const __m256i m5 = _mm256_broadcastsi128_si256(LOADU128((m) + 80));  \
    const __m256i m6 = _mm256_broadcastsi128_si256(LOADU128((m) + 96));  \
    const __m256i m7 = _mm256_broadcastsi128_si256(LOADU128((m) + 112)); \
    __m256i       t0, t1;

#define BLAKE2B_COMPRESS_V1(a, b, m, t0, t1, f0, f1)                      \
    do {                                                                  \
        DECLARE_MESSAGE_WORDS(m)                                          \
        const __m256i iv0 = a;                                            \
        const __m256i iv1 = b;                                            \
        __m256i       c   = LOAD(&blake2b_IV[0]);                         \
        __m256i       d =                                                 \
            XOR(LOAD(&blake2b_IV[4]), _mm256_set_epi64x(f1, f0, t1, t0)); \
        BLAKE2B_ROUNDS_V1(a, b, c, d, m);                                 \
        a = XOR(a, c);                                                    \
        b = XOR(b, d);                                                    \
        a = XOR(a, iv0);                                                  \
        b = XOR(b, iv1);                                                  \
    } while (0)

#endif
