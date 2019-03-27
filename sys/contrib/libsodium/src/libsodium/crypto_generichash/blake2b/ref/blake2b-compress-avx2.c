
#define BLAKE2_USE_SSSE3
#define BLAKE2_USE_SSE41
#define BLAKE2_USE_AVX2

#include <stdint.h>
#include <string.h>

#include "blake2.h"
#include "private/common.h"
#include "private/sse2_64_32.h"

#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_EMMINTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("sse2")
#  pragma GCC target("ssse3")
#  pragma GCC target("sse4.1")
#  pragma GCC target("avx2")
# endif

# include <emmintrin.h>
# include <immintrin.h>
# include <smmintrin.h>
# include <tmmintrin.h>

# include "blake2b-compress-avx2.h"

CRYPTO_ALIGN(64)
static const uint64_t blake2b_IV[8] = {
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
    0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

int
blake2b_compress_avx2(blake2b_state *S, const uint8_t block[BLAKE2B_BLOCKBYTES])
{
    __m256i a = LOADU(&S->h[0]);
    __m256i b = LOADU(&S->h[4]);
    BLAKE2B_COMPRESS_V1(a, b, block, S->t[0], S->t[1], S->f[0], S->f[1]);
    STOREU(&S->h[0], a);
    STOREU(&S->h[4], b);

    return 0;
}

#endif
