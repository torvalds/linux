#ifndef blake2b_load_avx2_H
#define blake2b_load_avx2_H

#define BLAKE2B_LOAD_MSG_0_1(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m0, m1);    \
        t1 = _mm256_unpacklo_epi64(m2, m3);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_0_2(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m0, m1);    \
        t1 = _mm256_unpackhi_epi64(m2, m3);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_0_3(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m4, m5);    \
        t1 = _mm256_unpacklo_epi64(m6, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_0_4(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m4, m5);    \
        t1 = _mm256_unpackhi_epi64(m6, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_1_1(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m7, m2);    \
        t1 = _mm256_unpackhi_epi64(m4, m6);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_1_2(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m5, m4);    \
        t1 = _mm256_alignr_epi8(m3, m7, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_1_3(b0)                                \
    do {                                                        \
        t0 = _mm256_shuffle_epi32(m0, _MM_SHUFFLE(1, 0, 3, 2)); \
        t1 = _mm256_unpackhi_epi64(m5, m2);                     \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0);                  \
    } while (0)

#define BLAKE2B_LOAD_MSG_1_4(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m6, m1);    \
        t1 = _mm256_unpackhi_epi64(m3, m1);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_2_1(b0)               \
    do {                                       \
        t0 = _mm256_alignr_epi8(m6, m5, 8);    \
        t1 = _mm256_unpackhi_epi64(m2, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_2_2(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m4, m0);    \
        t1 = _mm256_blend_epi32(m6, m1, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_2_3(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m1, m5, 0x33); \
        t1 = _mm256_unpackhi_epi64(m3, m4);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_2_4(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m7, m3);    \
        t1 = _mm256_alignr_epi8(m2, m0, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_3_1(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m3, m1);    \
        t1 = _mm256_unpackhi_epi64(m6, m5);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_3_2(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m4, m0);    \
        t1 = _mm256_unpacklo_epi64(m6, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_3_3(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m2, m1, 0x33); \
        t1 = _mm256_blend_epi32(m7, m2, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_3_4(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m3, m5);    \
        t1 = _mm256_unpacklo_epi64(m0, m4);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_4_1(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m4, m2);    \
        t1 = _mm256_unpacklo_epi64(m1, m5);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_4_2(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m3, m0, 0x33); \
        t1 = _mm256_blend_epi32(m7, m2, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_4_3(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m5, m7, 0x33); \
        t1 = _mm256_blend_epi32(m1, m3, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_4_4(b0)               \
    do {                                       \
        t0 = _mm256_alignr_epi8(m6, m0, 8);    \
        t1 = _mm256_blend_epi32(m6, m4, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_5_1(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m1, m3);    \
        t1 = _mm256_unpacklo_epi64(m0, m4);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_5_2(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m6, m5);    \
        t1 = _mm256_unpackhi_epi64(m5, m1);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_5_3(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m3, m2, 0x33); \
        t1 = _mm256_unpackhi_epi64(m7, m0);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_5_4(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m6, m2);    \
        t1 = _mm256_blend_epi32(m4, m7, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_6_1(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m0, m6, 0x33); \
        t1 = _mm256_unpacklo_epi64(m7, m2);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_6_2(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m2, m7);    \
        t1 = _mm256_alignr_epi8(m5, m6, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_6_3(b0)                                \
    do {                                                        \
        t0 = _mm256_unpacklo_epi64(m0, m3);                     \
        t1 = _mm256_shuffle_epi32(m4, _MM_SHUFFLE(1, 0, 3, 2)); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0);                  \
    } while (0)

#define BLAKE2B_LOAD_MSG_6_4(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m3, m1);    \
        t1 = _mm256_blend_epi32(m5, m1, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_7_1(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m6, m3);    \
        t1 = _mm256_blend_epi32(m1, m6, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_7_2(b0)               \
    do {                                       \
        t0 = _mm256_alignr_epi8(m7, m5, 8);    \
        t1 = _mm256_unpackhi_epi64(m0, m4);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_7_3(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m2, m7);    \
        t1 = _mm256_unpacklo_epi64(m4, m1);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_7_4(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m0, m2);    \
        t1 = _mm256_unpacklo_epi64(m3, m5);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_8_1(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m3, m7);    \
        t1 = _mm256_alignr_epi8(m0, m5, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_8_2(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m7, m4);    \
        t1 = _mm256_alignr_epi8(m4, m1, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_8_3(b0)               \
    do {                                       \
        t0 = m6;                               \
        t1 = _mm256_alignr_epi8(m5, m0, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_8_4(b0)               \
    do {                                       \
        t0 = _mm256_blend_epi32(m3, m1, 0x33); \
        t1 = m2;                               \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_9_1(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m5, m4);    \
        t1 = _mm256_unpackhi_epi64(m3, m0);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_9_2(b0)               \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m1, m2);    \
        t1 = _mm256_blend_epi32(m2, m3, 0x33); \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_9_3(b0)               \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m7, m4);    \
        t1 = _mm256_unpackhi_epi64(m1, m6);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_9_4(b0)               \
    do {                                       \
        t0 = _mm256_alignr_epi8(m7, m5, 8);    \
        t1 = _mm256_unpacklo_epi64(m6, m0);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_10_1(b0)              \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m0, m1);    \
        t1 = _mm256_unpacklo_epi64(m2, m3);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_10_2(b0)              \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m0, m1);    \
        t1 = _mm256_unpackhi_epi64(m2, m3);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_10_3(b0)              \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m4, m5);    \
        t1 = _mm256_unpacklo_epi64(m6, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_10_4(b0)              \
    do {                                       \
        t0 = _mm256_unpackhi_epi64(m4, m5);    \
        t1 = _mm256_unpackhi_epi64(m6, m7);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_11_1(b0)              \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m7, m2);    \
        t1 = _mm256_unpackhi_epi64(m4, m6);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_11_2(b0)              \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m5, m4);    \
        t1 = _mm256_alignr_epi8(m3, m7, 8);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#define BLAKE2B_LOAD_MSG_11_3(b0)                               \
    do {                                                        \
        t0 = _mm256_shuffle_epi32(m0, _MM_SHUFFLE(1, 0, 3, 2)); \
        t1 = _mm256_unpackhi_epi64(m5, m2);                     \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0);                  \
    } while (0)

#define BLAKE2B_LOAD_MSG_11_4(b0)              \
    do {                                       \
        t0 = _mm256_unpacklo_epi64(m6, m1);    \
        t1 = _mm256_unpackhi_epi64(m3, m1);    \
        b0 = _mm256_blend_epi32(t0, t1, 0xF0); \
    } while (0)

#endif
