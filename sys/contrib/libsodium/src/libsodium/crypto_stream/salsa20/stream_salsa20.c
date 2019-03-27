#include "crypto_stream_salsa20.h"
#include "private/common.h"
#include "private/implementations.h"
#include "randombytes.h"
#include "runtime.h"
#include "stream_salsa20.h"

#ifdef HAVE_AMD64_ASM
# include "xmm6/salsa20_xmm6.h"
#else
# include "ref/salsa20_ref.h"
#endif
#if !defined(HAVE_AMD64_ASM) && defined(HAVE_EMMINTRIN_H)
# include "xmm6int/salsa20_xmm6int-sse2.h"
#endif
#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_EMMINTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)
# include "xmm6int/salsa20_xmm6int-avx2.h"
#endif

#if HAVE_AMD64_ASM
static const crypto_stream_salsa20_implementation *implementation =
    &crypto_stream_salsa20_xmm6_implementation;
#else
static const crypto_stream_salsa20_implementation *implementation =
    &crypto_stream_salsa20_ref_implementation;
#endif

size_t
crypto_stream_salsa20_keybytes(void)
{
    return crypto_stream_salsa20_KEYBYTES;
}

size_t
crypto_stream_salsa20_noncebytes(void)
{
    return crypto_stream_salsa20_NONCEBYTES;
}

size_t
crypto_stream_salsa20_messagebytes_max(void)
{
    return crypto_stream_salsa20_MESSAGEBYTES_MAX;
}

int
crypto_stream_salsa20(unsigned char *c, unsigned long long clen,
                      const unsigned char *n, const unsigned char *k)
{
    return implementation->stream(c, clen, n, k);
}

int
crypto_stream_salsa20_xor_ic(unsigned char *c, const unsigned char *m,
                             unsigned long long mlen,
                             const unsigned char *n, uint64_t ic,
                             const unsigned char *k)
{
    return implementation->stream_xor_ic(c, m, mlen, n, ic, k);
}

int
crypto_stream_salsa20_xor(unsigned char *c, const unsigned char *m,
                          unsigned long long mlen, const unsigned char *n,
                          const unsigned char *k)
{
    return implementation->stream_xor_ic(c, m, mlen, n, 0U, k);
}

void
crypto_stream_salsa20_keygen(unsigned char k[crypto_stream_salsa20_KEYBYTES])
{
    randombytes_buf(k, crypto_stream_salsa20_KEYBYTES);
}

int
_crypto_stream_salsa20_pick_best_implementation(void)
{
#ifdef HAVE_AMD64_ASM
    implementation = &crypto_stream_salsa20_xmm6_implementation;
#else
    implementation = &crypto_stream_salsa20_ref_implementation;
#endif

#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_EMMINTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_avx2()) {
        implementation = &crypto_stream_salsa20_xmm6int_avx2_implementation;
        return 0;
    }
#endif
#if !defined(HAVE_AMD64_ASM) && defined(HAVE_EMMINTRIN_H)
    if (sodium_runtime_has_sse2()) {
        implementation = &crypto_stream_salsa20_xmm6int_sse2_implementation;
        return 0;
    }
#endif
    return 0; /* LCOV_EXCL_LINE */
}
