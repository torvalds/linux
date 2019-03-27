
#include <stdint.h>
#include <string.h>

#include "blake2.h"
#include "private/common.h"
#include "private/sse2_64_32.h"

#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("sse2")
#  pragma GCC target("ssse3")
# endif

# include <emmintrin.h>
# include <tmmintrin.h>

# include "blake2b-compress-ssse3.h"

CRYPTO_ALIGN(64)
static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

int
blake2b_compress_ssse3(blake2b_state *S,
                       const uint8_t  block[BLAKE2B_BLOCKBYTES])
{
    __m128i       row1l, row1h;
    __m128i       row2l, row2h;
    __m128i       row3l, row3h;
    __m128i       row4l, row4h;
    __m128i       b0, b1;
    __m128i       t0, t1;
    const __m128i r16 =
        _mm_setr_epi8(2, 3, 4, 5, 6, 7, 0, 1, 10, 11, 12, 13, 14, 15, 8, 9);
    const __m128i r24 =
        _mm_setr_epi8(3, 4, 5, 6, 7, 0, 1, 2, 11, 12, 13, 14, 15, 8, 9, 10);
    const uint64_t m0  = ((uint64_t *) block)[0];
    const uint64_t m1  = ((uint64_t *) block)[1];
    const uint64_t m2  = ((uint64_t *) block)[2];
    const uint64_t m3  = ((uint64_t *) block)[3];
    const uint64_t m4  = ((uint64_t *) block)[4];
    const uint64_t m5  = ((uint64_t *) block)[5];
    const uint64_t m6  = ((uint64_t *) block)[6];
    const uint64_t m7  = ((uint64_t *) block)[7];
    const uint64_t m8  = ((uint64_t *) block)[8];
    const uint64_t m9  = ((uint64_t *) block)[9];
    const uint64_t m10 = ((uint64_t *) block)[10];
    const uint64_t m11 = ((uint64_t *) block)[11];
    const uint64_t m12 = ((uint64_t *) block)[12];
    const uint64_t m13 = ((uint64_t *) block)[13];
    const uint64_t m14 = ((uint64_t *) block)[14];
    const uint64_t m15 = ((uint64_t *) block)[15];

    row1l = LOADU(&S->h[0]);
    row1h = LOADU(&S->h[2]);
    row2l = LOADU(&S->h[4]);
    row2h = LOADU(&S->h[6]);
    row3l = LOADU(&blake2b_IV[0]);
    row3h = LOADU(&blake2b_IV[2]);
    row4l = _mm_xor_si128(LOADU(&blake2b_IV[4]), LOADU(&S->t[0]));
    row4h = _mm_xor_si128(LOADU(&blake2b_IV[6]), LOADU(&S->f[0]));
    ROUND(0);
    ROUND(1);
    ROUND(2);
    ROUND(3);
    ROUND(4);
    ROUND(5);
    ROUND(6);
    ROUND(7);
    ROUND(8);
    ROUND(9);
    ROUND(10);
    ROUND(11);
    row1l = _mm_xor_si128(row3l, row1l);
    row1h = _mm_xor_si128(row3h, row1h);
    STOREU(&S->h[0], _mm_xor_si128(LOADU(&S->h[0]), row1l));
    STOREU(&S->h[2], _mm_xor_si128(LOADU(&S->h[2]), row1h));
    row2l = _mm_xor_si128(row4l, row2l);
    row2h = _mm_xor_si128(row4h, row2h);
    STOREU(&S->h[4], _mm_xor_si128(LOADU(&S->h[4]), row2l));
    STOREU(&S->h[6], _mm_xor_si128(LOADU(&S->h[6]), row2h));
    return 0;
}

#endif
