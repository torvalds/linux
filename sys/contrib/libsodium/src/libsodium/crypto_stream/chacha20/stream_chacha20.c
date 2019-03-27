#include "crypto_stream_chacha20.h"
#include "private/common.h"
#include "private/implementations.h"
#include "randombytes.h"
#include "runtime.h"
#include "stream_chacha20.h"

#include "ref/chacha20_ref.h"
#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_EMMINTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)
# include "dolbeau/chacha20_dolbeau-avx2.h"
#endif
#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H)
# include "dolbeau/chacha20_dolbeau-ssse3.h"
#endif

static const crypto_stream_chacha20_implementation *implementation =
    &crypto_stream_chacha20_ref_implementation;

size_t
crypto_stream_chacha20_keybytes(void) {
    return crypto_stream_chacha20_KEYBYTES;
}

size_t
crypto_stream_chacha20_noncebytes(void) {
    return crypto_stream_chacha20_NONCEBYTES;
}

size_t
crypto_stream_chacha20_messagebytes_max(void)
{
    return crypto_stream_chacha20_MESSAGEBYTES_MAX;
}

size_t
crypto_stream_chacha20_ietf_keybytes(void) {
    return crypto_stream_chacha20_ietf_KEYBYTES;
}

size_t
crypto_stream_chacha20_ietf_noncebytes(void) {
    return crypto_stream_chacha20_ietf_NONCEBYTES;
}

size_t
crypto_stream_chacha20_ietf_messagebytes_max(void)
{
    return crypto_stream_chacha20_ietf_MESSAGEBYTES_MAX;
}

int
crypto_stream_chacha20(unsigned char *c, unsigned long long clen,
                       const unsigned char *n, const unsigned char *k)
{
    return implementation->stream(c, clen, n, k);
}

int
crypto_stream_chacha20_ietf(unsigned char *c, unsigned long long clen,
                            const unsigned char *n, const unsigned char *k)
{
    return implementation->stream_ietf(c, clen, n, k);
}

int
crypto_stream_chacha20_xor_ic(unsigned char *c, const unsigned char *m,
                              unsigned long long mlen,
                              const unsigned char *n, uint64_t ic,
                              const unsigned char *k)
{
    return implementation->stream_xor_ic(c, m, mlen, n, ic, k);
}

int
crypto_stream_chacha20_ietf_xor_ic(unsigned char *c, const unsigned char *m,
                                   unsigned long long mlen,
                                   const unsigned char *n, uint32_t ic,
                                   const unsigned char *k)
{
    return implementation->stream_ietf_xor_ic(c, m, mlen, n, ic, k);
}

int
crypto_stream_chacha20_xor(unsigned char *c, const unsigned char *m,
                           unsigned long long mlen, const unsigned char *n,
                           const unsigned char *k)
{
    return implementation->stream_xor_ic(c, m, mlen, n, 0U, k);
}

int
crypto_stream_chacha20_ietf_xor(unsigned char *c, const unsigned char *m,
                                unsigned long long mlen, const unsigned char *n,
                                const unsigned char *k)
{
    return implementation->stream_ietf_xor_ic(c, m, mlen, n, 0U, k);
}

void
crypto_stream_chacha20_ietf_keygen(unsigned char k[crypto_stream_chacha20_ietf_KEYBYTES])
{
    randombytes_buf(k, crypto_stream_chacha20_ietf_KEYBYTES);
}

void
crypto_stream_chacha20_keygen(unsigned char k[crypto_stream_chacha20_KEYBYTES])
{
    randombytes_buf(k, crypto_stream_chacha20_KEYBYTES);
}

int
_crypto_stream_chacha20_pick_best_implementation(void)
{
    implementation = &crypto_stream_chacha20_ref_implementation;
#if defined(HAVE_AVX2INTRIN_H) && defined(HAVE_EMMINTRIN_H) && \
    defined(HAVE_TMMINTRIN_H) && defined(HAVE_SMMINTRIN_H)
    if (sodium_runtime_has_avx2()) {
        implementation = &crypto_stream_chacha20_dolbeau_avx2_implementation;
        return 0;
    }
#endif
#if defined(HAVE_EMMINTRIN_H) && defined(HAVE_TMMINTRIN_H)
    if (sodium_runtime_has_ssse3()) {
        implementation = &crypto_stream_chacha20_dolbeau_ssse3_implementation;
        return 0;
    }
#endif
    return 0;
}
