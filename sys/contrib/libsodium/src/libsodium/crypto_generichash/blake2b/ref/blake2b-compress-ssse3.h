
#ifndef blake2b_compress_ssse3_H
#define blake2b_compress_ssse3_H

#define LOADU(p) _mm_loadu_si128((const __m128i *) (const void *) (p))
#define STOREU(p, r) _mm_storeu_si128((__m128i *) (void *) (p), r)

#define _mm_roti_epi64(x, c)                                         \
    (-(c) == 32)                                                     \
        ? _mm_shuffle_epi32((x), _MM_SHUFFLE(2, 3, 0, 1))            \
        : (-(c) == 24)                                               \
              ? _mm_shuffle_epi8((x), r24)                           \
              : (-(c) == 16)                                         \
                    ? _mm_shuffle_epi8((x), r16)                     \
                    : (-(c) == 63)                                   \
                          ? _mm_xor_si128(_mm_srli_epi64((x), -(c)), \
                                          _mm_add_epi64((x), (x)))   \
                          : _mm_xor_si128(_mm_srli_epi64((x), -(c)), \
                                          _mm_slli_epi64((x), 64 - (-(c))))

#define G1(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1) \
    row1l = _mm_add_epi64(_mm_add_epi64(row1l, b0), row2l);                \
    row1h = _mm_add_epi64(_mm_add_epi64(row1h, b1), row2h);                \
                                                                           \
    row4l = _mm_xor_si128(row4l, row1l);                                   \
    row4h = _mm_xor_si128(row4h, row1h);                                   \
                                                                           \
    row4l = _mm_roti_epi64(row4l, -32);                                    \
    row4h = _mm_roti_epi64(row4h, -32);                                    \
                                                                           \
    row3l = _mm_add_epi64(row3l, row4l);                                   \
    row3h = _mm_add_epi64(row3h, row4h);                                   \
                                                                           \
    row2l = _mm_xor_si128(row2l, row3l);                                   \
    row2h = _mm_xor_si128(row2h, row3h);                                   \
                                                                           \
    row2l = _mm_roti_epi64(row2l, -24);                                    \
    row2h = _mm_roti_epi64(row2h, -24);

#define G2(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1) \
    row1l = _mm_add_epi64(_mm_add_epi64(row1l, b0), row2l);                \
    row1h = _mm_add_epi64(_mm_add_epi64(row1h, b1), row2h);                \
                                                                           \
    row4l = _mm_xor_si128(row4l, row1l);                                   \
    row4h = _mm_xor_si128(row4h, row1h);                                   \
                                                                           \
    row4l = _mm_roti_epi64(row4l, -16);                                    \
    row4h = _mm_roti_epi64(row4h, -16);                                    \
                                                                           \
    row3l = _mm_add_epi64(row3l, row4l);                                   \
    row3h = _mm_add_epi64(row3h, row4h);                                   \
                                                                           \
    row2l = _mm_xor_si128(row2l, row3l);                                   \
    row2h = _mm_xor_si128(row2h, row3h);                                   \
                                                                           \
    row2l = _mm_roti_epi64(row2l, -63);                                    \
    row2h = _mm_roti_epi64(row2h, -63);

#define DIAGONALIZE(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h) \
    t0    = _mm_alignr_epi8(row2h, row2l, 8);                               \
    t1    = _mm_alignr_epi8(row2l, row2h, 8);                               \
    row2l = t0;                                                             \
    row2h = t1;                                                             \
                                                                            \
    t0    = row3l;                                                          \
    row3l = row3h;                                                          \
    row3h = t0;                                                             \
                                                                            \
    t0    = _mm_alignr_epi8(row4h, row4l, 8);                               \
    t1    = _mm_alignr_epi8(row4l, row4h, 8);                               \
    row4l = t1;                                                             \
    row4h = t0;

#define UNDIAGONALIZE(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h) \
    t0    = _mm_alignr_epi8(row2l, row2h, 8);                                 \
    t1    = _mm_alignr_epi8(row2h, row2l, 8);                                 \
    row2l = t0;                                                               \
    row2h = t1;                                                               \
                                                                              \
    t0    = row3l;                                                            \
    row3l = row3h;                                                            \
    row3h = t0;                                                               \
                                                                              \
    t0    = _mm_alignr_epi8(row4l, row4h, 8);                                 \
    t1    = _mm_alignr_epi8(row4h, row4l, 8);                                 \
    row4l = t1;                                                               \
    row4h = t0;

#include "blake2b-load-sse2.h"

#define ROUND(r)                                                         \
    LOAD_MSG_##r##_1(b0, b1);                                            \
    G1(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1);  \
    LOAD_MSG_##r##_2(b0, b1);                                            \
    G2(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1);  \
    DIAGONALIZE(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h); \
    LOAD_MSG_##r##_3(b0, b1);                                            \
    G1(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1);  \
    LOAD_MSG_##r##_4(b0, b1);                                            \
    G2(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h, b0, b1);  \
    UNDIAGONALIZE(row1l, row2l, row3l, row4l, row1h, row2h, row3h, row4h);

#endif
