
#define BLAKE2_USE_SSSE3
#define BLAKE2_USE_SSE41

#include <stdint.h>
#include <string.h>

#include "blake2.h"
#include "private/common.h"
#include "private/sse2_64_32.h"

#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H) && \
    defined(HAVE_SMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("sse2")
#  pragma GCC target("ssse3")
#  pragma GCC target("sse4.1")
# endif

# include <emmintrin.h>
# include <smmintrin.h>
# include <tmmintrin.h>

# include "blake2b-compress-sse41.h"

CRYPTO_ALIGN(64)
static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

int
blake2b_compress_sse41(blake2b_state *S,
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
    const __m128i m0 = LOADU(block + 00);
    const __m128i m1 = LOADU(block + 16);
    const __m128i m2 = LOADU(block + 32);
    const __m128i m3 = LOADU(block + 48);
    const __m128i m4 = LOADU(block + 64);
    const __m128i m5 = LOADU(block + 80);
    const __m128i m6 = LOADU(block + 96);
    const __m128i m7 = LOADU(block + 112);
    row1l            = LOADU(&S->h[0]);
    row1h            = LOADU(&S->h[2]);
    row2l            = LOADU(&S->h[4]);
    row2h            = LOADU(&S->h[6]);
    row3l            = LOADU(&blake2b_IV[0]);
    row3h            = LOADU(&blake2b_IV[2]);
    row4l            = _mm_xor_si128(LOADU(&blake2b_IV[4]), LOADU(&S->t[0]));
    row4h            = _mm_xor_si128(LOADU(&blake2b_IV[6]), LOADU(&S->f[0]));
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
