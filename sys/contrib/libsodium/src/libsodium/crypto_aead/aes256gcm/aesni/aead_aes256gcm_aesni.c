
/*
 * AES256-GCM, based on the "Intel Carry-Less Multiplication Instruction and its Usage for Computing
 * the GCM Mode" paper and reference code, using the aggregated reduction method.
 * Originally adapted by Romain Dolbeau.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "core.h"
#include "crypto_aead_aes256gcm.h"
#include "export.h"
#include "private/common.h"
#include "private/sse2_64_32.h"
#include "randombytes.h"
#include "runtime.h"
#include "utils.h"

#if defined(HAVE_TMMINTRIN_H) && defined(HAVE_WMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("ssse3")
#  pragma GCC target("aes")
#  pragma GCC target("pclmul")
# endif

#include <tmmintrin.h>
#include <wmmintrin.h>

#ifndef ENOSYS
# define ENOSYS ENXIO
#endif

#if defined(__INTEL_COMPILER) || defined(_bswap64)
#elif defined(_MSC_VER)
# define _bswap64(a) _byteswap_uint64(a)
#elif defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 2))
# define _bswap64(a) __builtin_bswap64(a)
#else
static inline uint64_t
_bswap64(const uint64_t x)
{
    return
        ((x << 56) & 0xFF00000000000000UL) | ((x << 40) & 0x00FF000000000000UL) |
        ((x << 24) & 0x0000FF0000000000UL) | ((x <<  8) & 0x000000FF00000000UL) |
        ((x >>  8) & 0x00000000FF000000UL) | ((x >> 24) & 0x0000000000FF0000UL) |
        ((x >> 40) & 0x000000000000FF00UL) | ((x >> 56) & 0x00000000000000FFUL);
}
#endif

typedef struct context {
    CRYPTO_ALIGN(16) unsigned char H[16];
    __m128i          rkeys[16];
} context;

static inline void
aesni_key256_expand(const unsigned char *key, __m128i * const rkeys)
{
    __m128i  X0, X1, X2, X3;
    int      i = 0;

    X0 = _mm_loadu_si128((const __m128i *) &key[0]);
    rkeys[i++] = X0;

    X2 = _mm_loadu_si128((const __m128i *) &key[16]);
    rkeys[i++] = X2;

#define EXPAND_KEY_1(S) do { \
    X1 = _mm_shuffle_epi32(_mm_aeskeygenassist_si128(X2, (S)), 0xff); \
    X3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(X3), _mm_castsi128_ps(X0), 0x10)); \
    X0 = _mm_xor_si128(X0, X3); \
    X3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(X3), _mm_castsi128_ps(X0), 0x8c)); \
    X0 = _mm_xor_si128(_mm_xor_si128(X0, X3), X1); \
    rkeys[i++] = X0; \
} while (0)

#define EXPAND_KEY_2(S) do { \
    X1 = _mm_shuffle_epi32(_mm_aeskeygenassist_si128(X0, (S)), 0xaa); \
    X3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(X3), _mm_castsi128_ps(X2), 0x10)); \
    X2 = _mm_xor_si128(X2, X3); \
    X3 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(X3), _mm_castsi128_ps(X2), 0x8c)); \
    X2 = _mm_xor_si128(_mm_xor_si128(X2, X3), X1); \
    rkeys[i++] = X2; \
} while (0)

    X3 = _mm_setzero_si128();
    EXPAND_KEY_1(0x01); EXPAND_KEY_2(0x01);
    EXPAND_KEY_1(0x02); EXPAND_KEY_2(0x02);
    EXPAND_KEY_1(0x04); EXPAND_KEY_2(0x04);
    EXPAND_KEY_1(0x08); EXPAND_KEY_2(0x08);
    EXPAND_KEY_1(0x10); EXPAND_KEY_2(0x10);
    EXPAND_KEY_1(0x20); EXPAND_KEY_2(0x20);
    EXPAND_KEY_1(0x40);
}

/** single, by-the-book AES encryption with AES-NI */
static inline void
aesni_encrypt1(unsigned char *out, __m128i nv, const __m128i *rkeys)
{
    __m128i temp = _mm_xor_si128(nv, rkeys[0]);

    temp = _mm_aesenc_si128(temp, rkeys[1]);
    temp = _mm_aesenc_si128(temp, rkeys[2]);
    temp = _mm_aesenc_si128(temp, rkeys[3]);
    temp = _mm_aesenc_si128(temp, rkeys[4]);
    temp = _mm_aesenc_si128(temp, rkeys[5]);
    temp = _mm_aesenc_si128(temp, rkeys[6]);
    temp = _mm_aesenc_si128(temp, rkeys[7]);
    temp = _mm_aesenc_si128(temp, rkeys[8]);
    temp = _mm_aesenc_si128(temp, rkeys[9]);
    temp = _mm_aesenc_si128(temp, rkeys[10]);
    temp = _mm_aesenc_si128(temp, rkeys[11]);
    temp = _mm_aesenc_si128(temp, rkeys[12]);
    temp = _mm_aesenc_si128(temp, rkeys[13]);

    temp = _mm_aesenclast_si128(temp, rkeys[14]);
    _mm_storeu_si128((__m128i *) out, temp);
}

/** multiple-blocks-at-once AES encryption with AES-NI ;
    on Haswell, aesenc has a latency of 7 and a throughput of 1
    so the sequence of aesenc should be bubble-free if you
    have at least 8 blocks. Let's build an arbitratry-sized
    function */
/* Step 1 : loading the nonce */
/* load & increment the n vector (non-vectorized, unused for now) */
#define NVDECLx(a)                                                             \
    __m128i nv##a

#define NVx(a)                                                                 \
    nv##a = _mm_shuffle_epi8(_mm_load_si128((const __m128i *) n), pt);         \
    n[3]++

/* Step 2 : define value in round one (xor with subkey #0, aka key) */
#define TEMPDECLx(a) \
    __m128i temp##a

#define TEMPx(a) \
    temp##a = _mm_xor_si128(nv##a, rkeys[0])

/* Step 3: one round of AES */
#define AESENCx(a) \
    temp##a = _mm_aesenc_si128(temp##a, rkeys[roundctr])

/* Step 4: last round of AES */
#define AESENCLASTx(a) \
    temp##a = _mm_aesenclast_si128(temp##a, rkeys[14])

/* Step 5: store result */
#define STOREx(a) \
    _mm_storeu_si128((__m128i *) (out + (a * 16)), temp##a)

/* all the MAKE* macros are for automatic explicit unrolling */
#define MAKE4(X) \
    X(0);        \
    X(1);        \
    X(2);        \
    X(3)

#define MAKE8(X) \
    X(0);        \
    X(1);        \
    X(2);        \
    X(3);        \
    X(4);        \
    X(5);        \
    X(6);        \
    X(7)

#define COUNTER_INC2(N) (N)[3] += 2

/* create a function of unrolling N ; the MAKEN is the unrolling
   macro, defined above. The N in MAKEN must match N, obviously. */
#define FUNC(N, MAKEN)                                                                                \
    static inline void aesni_encrypt##N(unsigned char *out, uint32_t *n, const __m128i *rkeys)        \
    {                                                                                                 \
        const __m128i pt = _mm_set_epi8(12, 13, 14, 15, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0);        \
        int           roundctr;                                                                       \
        MAKEN(NVDECLx);                                                                               \
        MAKEN(TEMPDECLx);                                                                             \
                                                                                                      \
        MAKEN(NVx);                                                                                   \
        MAKEN(TEMPx);                                                                                 \
        for (roundctr = 1; roundctr < 14; roundctr++) {                                               \
            MAKEN(AESENCx);                                                                           \
        }                                                                                             \
        MAKEN(AESENCLASTx);                                                                           \
        MAKEN(STOREx);                                                                                \
    }

FUNC(8, MAKE8)

/* all GF(2^128) fnctions are by the book, meaning this one:
   <https://software.intel.com/sites/default/files/managed/72/cc/clmul-wp-rev-2.02-2014-04-20.pdf>
*/

static inline void
addmul(unsigned char *c, const unsigned char *a, unsigned int xlen, const unsigned char *b)
{
    const __m128i rev = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    __m128i       A, B, C;
    __m128i       tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;
    __m128i       tmp10, tmp11, tmp12, tmp13, tmp14, tmp15, tmp16, tmp17, tmp18;
    __m128i       tmp19, tmp20, tmp21, tmp22, tmp23, tmp24, tmp25, tmp26, tmp27;
    __m128i       tmp28, tmp29, tmp30, tmp31, tmp32, tmp33, tmp34, tmp35, tmp36;

    if (xlen >= 16) {
        A = _mm_loadu_si128((const __m128i *) a);
    } else {
        CRYPTO_ALIGN(16) unsigned char padded[16];
        unsigned int i;

        memset(padded, 0, 16);
        for (i = 0; i < xlen; i++) {
            padded[i] = a[i];
        }
        A = _mm_load_si128((const __m128i *) padded);
    }
    A = _mm_shuffle_epi8(A, rev);
    B = _mm_loadu_si128((const __m128i *) b);
    C = _mm_loadu_si128((const __m128i *) c);
    A = _mm_xor_si128(A, C);
    tmp3 = _mm_clmulepi64_si128(A, B, 0x00);
    tmp4 = _mm_clmulepi64_si128(A, B, 0x10);
    tmp5 = _mm_clmulepi64_si128(A, B, 0x01);
    tmp6 = _mm_clmulepi64_si128(A, B, 0x11);
    tmp10 = _mm_xor_si128(tmp4, tmp5);
    tmp13 = _mm_slli_si128(tmp10, 8);
    tmp11 = _mm_srli_si128(tmp10, 8);
    tmp15 = _mm_xor_si128(tmp3, tmp13);
    tmp17 = _mm_xor_si128(tmp6, tmp11);
    tmp7 = _mm_srli_epi32(tmp15, 31);
    tmp8 = _mm_srli_epi32(tmp17, 31);
    tmp16 = _mm_slli_epi32(tmp15, 1);
    tmp18 = _mm_slli_epi32(tmp17, 1);
    tmp9 = _mm_srli_si128(tmp7, 12);
    tmp22 = _mm_slli_si128(tmp8, 4);
    tmp25 = _mm_slli_si128(tmp7, 4);
    tmp29 = _mm_or_si128(tmp16, tmp25);
    tmp19 = _mm_or_si128(tmp18, tmp22);
    tmp20 = _mm_or_si128(tmp19, tmp9);
    tmp26 = _mm_slli_epi32(tmp29, 31);
    tmp23 = _mm_slli_epi32(tmp29, 30);
    tmp32 = _mm_slli_epi32(tmp29, 25);
    tmp27 = _mm_xor_si128(tmp26, tmp23);
    tmp28 = _mm_xor_si128(tmp27, tmp32);
    tmp24 = _mm_srli_si128(tmp28, 4);
    tmp33 = _mm_slli_si128(tmp28, 12);
    tmp30 = _mm_xor_si128(tmp29, tmp33);
    tmp2 = _mm_srli_epi32(tmp30, 1);
    tmp12 = _mm_srli_epi32(tmp30, 2);
    tmp14 = _mm_srli_epi32(tmp30, 7);
    tmp34 = _mm_xor_si128(tmp2, tmp12);
    tmp35 = _mm_xor_si128(tmp34, tmp14);
    tmp36 = _mm_xor_si128(tmp35, tmp24);
    tmp31 = _mm_xor_si128(tmp30, tmp36);
    tmp21 = _mm_xor_si128(tmp20, tmp31);
    _mm_storeu_si128((__m128i *) c, tmp21);
}

/* pure multiplication, for pre-computing powers of H */
static inline __m128i
mulv(__m128i A, __m128i B)
{
    __m128i tmp3 = _mm_clmulepi64_si128(A, B, 0x00);
    __m128i tmp4 = _mm_clmulepi64_si128(A, B, 0x10);
    __m128i tmp5 = _mm_clmulepi64_si128(A, B, 0x01);
    __m128i tmp6 = _mm_clmulepi64_si128(A, B, 0x11);
    __m128i tmp10 = _mm_xor_si128(tmp4, tmp5);
    __m128i tmp13 = _mm_slli_si128(tmp10, 8);
    __m128i tmp11 = _mm_srli_si128(tmp10, 8);
    __m128i tmp15 = _mm_xor_si128(tmp3, tmp13);
    __m128i tmp17 = _mm_xor_si128(tmp6, tmp11);
    __m128i tmp7 = _mm_srli_epi32(tmp15, 31);
    __m128i tmp8 = _mm_srli_epi32(tmp17, 31);
    __m128i tmp16 = _mm_slli_epi32(tmp15, 1);
    __m128i tmp18 = _mm_slli_epi32(tmp17, 1);
    __m128i tmp9 = _mm_srli_si128(tmp7, 12);
    __m128i tmp22 = _mm_slli_si128(tmp8, 4);
    __m128i tmp25 = _mm_slli_si128(tmp7, 4);
    __m128i tmp29 = _mm_or_si128(tmp16, tmp25);
    __m128i tmp19 = _mm_or_si128(tmp18, tmp22);
    __m128i tmp20 = _mm_or_si128(tmp19, tmp9);
    __m128i tmp26 = _mm_slli_epi32(tmp29, 31);
    __m128i tmp23 = _mm_slli_epi32(tmp29, 30);
    __m128i tmp32 = _mm_slli_epi32(tmp29, 25);
    __m128i tmp27 = _mm_xor_si128(tmp26, tmp23);
    __m128i tmp28 = _mm_xor_si128(tmp27, tmp32);
    __m128i tmp24 = _mm_srli_si128(tmp28, 4);
    __m128i tmp33 = _mm_slli_si128(tmp28, 12);
    __m128i tmp30 = _mm_xor_si128(tmp29, tmp33);
    __m128i tmp2 = _mm_srli_epi32(tmp30, 1);
    __m128i tmp12 = _mm_srli_epi32(tmp30, 2);
    __m128i tmp14 = _mm_srli_epi32(tmp30, 7);
    __m128i tmp34 = _mm_xor_si128(tmp2, tmp12);
    __m128i tmp35 = _mm_xor_si128(tmp34, tmp14);
    __m128i tmp36 = _mm_xor_si128(tmp35, tmp24);
    __m128i tmp31 = _mm_xor_si128(tmp30, tmp36);
    __m128i C = _mm_xor_si128(tmp20, tmp31);

    return C;
}

/* 4 multiply-accumulate at once; again
   <https://software.intel.com/sites/default/files/managed/72/cc/clmul-wp-rev-2.02-2014-04-20.pdf>
   for the Aggregated Reduction Method & sample code.
   Algorithm by Krzysztof Jankowski, Pierre Laurent - Intel */

#define RED_DECL(a) __m128i H##a##_X##a##_lo, H##a##_X##a##_hi, tmp##a, tmp##a##B
#define RED_SHUFFLE(a) X##a = _mm_shuffle_epi8(X##a, rev)
#define RED_MUL_LOW(a) H##a##_X##a##_lo = _mm_clmulepi64_si128(H##a, X##a, 0x00)
#define RED_MUL_HIGH(a) H##a##_X##a##_hi = _mm_clmulepi64_si128(H##a, X##a, 0x11)
#define RED_MUL_MID(a)                          \
    tmp##a = _mm_shuffle_epi32(H##a, 0x4e);     \
    tmp##a##B = _mm_shuffle_epi32(X##a, 0x4e);  \
    tmp##a = _mm_xor_si128(tmp##a, H##a);       \
    tmp##a##B = _mm_xor_si128(tmp##a##B, X##a); \
    tmp##a = _mm_clmulepi64_si128(tmp##a, tmp##a##B, 0x00)

#define MULREDUCE4(rev, H0_, H1_, H2_, H3_, X0_, X1_, X2_, X3_, accv) \
do { \
    MAKE4(RED_DECL); \
    __m128i lo, hi; \
    __m128i tmp8, tmp9; \
    __m128i H0 = H0_; \
    __m128i H1 = H1_; \
    __m128i H2 = H2_; \
    __m128i H3 = H3_; \
    __m128i X0 = X0_; \
    __m128i X1 = X1_; \
    __m128i X2 = X2_; \
    __m128i X3 = X3_; \
\
/* byte-revert the inputs & xor the first one into the accumulator */ \
\
    MAKE4(RED_SHUFFLE); \
    X3 = _mm_xor_si128(X3, accv); \
\
/* 4 low H*X (x0*h0) */ \
\
    MAKE4(RED_MUL_LOW); \
    lo = _mm_xor_si128(H0_X0_lo, H1_X1_lo); \
    lo = _mm_xor_si128(lo, H2_X2_lo); \
    lo = _mm_xor_si128(lo, H3_X3_lo); \
\
/* 4 high H*X (x1*h1) */ \
\
    MAKE4(RED_MUL_HIGH); \
    hi = _mm_xor_si128(H0_X0_hi, H1_X1_hi); \
    hi = _mm_xor_si128(hi, H2_X2_hi); \
    hi = _mm_xor_si128(hi, H3_X3_hi); \
\
/* 4 middle H*X, using Karatsuba, i.e. \
     x1*h0+x0*h1 =(x1+x0)*(h1+h0)-x1*h1-x0*h0 \
     we already have all x1y1 & x0y0 (accumulated in hi & lo) \
     (0 is low half and 1 is high half) \
  */ \
/* permute the high and low 64 bits in H1 & X1, \
     so create (h0,h1) from (h1,h0) and (x0,x1) from (x1,x0), \
     then compute (h0+h1,h1+h0) and (x0+x1,x1+x0), \
     and finally multiply \
  */ \
    MAKE4(RED_MUL_MID); \
\
/* substracts x1*h1 and x0*h0 */ \
    tmp0 = _mm_xor_si128(tmp0, lo); \
    tmp0 = _mm_xor_si128(tmp0, hi); \
    tmp0 = _mm_xor_si128(tmp1, tmp0); \
    tmp0 = _mm_xor_si128(tmp2, tmp0); \
    tmp0 = _mm_xor_si128(tmp3, tmp0);\
\
    /* reduction */ \
    tmp0B = _mm_slli_si128(tmp0, 8); \
    tmp0 = _mm_srli_si128(tmp0, 8); \
    lo = _mm_xor_si128(tmp0B, lo); \
    hi = _mm_xor_si128(tmp0, hi); \
    tmp3 = lo; \
    tmp2B = hi; \
    tmp3B = _mm_srli_epi32(tmp3, 31); \
    tmp8 = _mm_srli_epi32(tmp2B, 31); \
    tmp3 = _mm_slli_epi32(tmp3, 1); \
    tmp2B = _mm_slli_epi32(tmp2B, 1); \
    tmp9 = _mm_srli_si128(tmp3B, 12); \
    tmp8 = _mm_slli_si128(tmp8, 4); \
    tmp3B = _mm_slli_si128(tmp3B, 4); \
    tmp3 = _mm_or_si128(tmp3, tmp3B); \
    tmp2B = _mm_or_si128(tmp2B, tmp8); \
    tmp2B = _mm_or_si128(tmp2B, tmp9); \
    tmp3B = _mm_slli_epi32(tmp3, 31); \
    tmp8 = _mm_slli_epi32(tmp3, 30); \
    tmp9 = _mm_slli_epi32(tmp3, 25); \
    tmp3B = _mm_xor_si128(tmp3B, tmp8); \
    tmp3B = _mm_xor_si128(tmp3B, tmp9); \
    tmp8 = _mm_srli_si128(tmp3B, 4); \
    tmp3B = _mm_slli_si128(tmp3B, 12); \
    tmp3 = _mm_xor_si128(tmp3, tmp3B); \
    tmp2 = _mm_srli_epi32(tmp3, 1); \
    tmp0B = _mm_srli_epi32(tmp3, 2); \
    tmp1B = _mm_srli_epi32(tmp3, 7); \
    tmp2 = _mm_xor_si128(tmp2, tmp0B); \
    tmp2 = _mm_xor_si128(tmp2, tmp1B); \
    tmp2 = _mm_xor_si128(tmp2, tmp8); \
    tmp3 = _mm_xor_si128(tmp3, tmp2); \
    tmp2B = _mm_xor_si128(tmp2B, tmp3); \
\
    accv = tmp2B; \
} while(0)

#define XORx(a)                                                       \
        temp##a = _mm_xor_si128(temp##a,                              \
                                _mm_loadu_si128((const __m128i *) (in + a * 16)))

#define LOADx(a)                                                      \
    __m128i in##a = _mm_loadu_si128((const __m128i *) (in + a * 16))

/* full encrypt & checksum 8 blocks at once */
#define aesni_encrypt8full(out_, n_, rkeys, in_, accum, hv_, h2v_, h3v_, h4v_, rev) \
do { \
    unsigned char *out = out_; \
    uint32_t *n = n_; \
    const unsigned char *in = in_; \
    const __m128i hv = hv_; \
    const __m128i h2v = h2v_; \
    const __m128i h3v = h3v_; \
    const __m128i h4v = h4v_; \
    const __m128i pt = _mm_set_epi8(12, 13, 14, 15, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0); \
    __m128i       accv_; \
    int           roundctr; \
    \
    MAKE8(NVDECLx); \
    MAKE8(TEMPDECLx); \
    MAKE8(NVx); \
    MAKE8(TEMPx); \
    for (roundctr = 1; roundctr < 14; roundctr++) { \
        MAKE8(AESENCx); \
    } \
    MAKE8(AESENCLASTx); \
    MAKE8(XORx); \
    MAKE8(STOREx); \
    accv_ = _mm_load_si128((const __m128i *) accum); \
    MULREDUCE4(rev, hv, h2v, h3v, h4v, temp3, temp2, temp1, temp0, accv_); \
    MULREDUCE4(rev, hv, h2v, h3v, h4v, temp7, temp6, temp5, temp4, accv_); \
    _mm_store_si128((__m128i *) accum, accv_); \
} while(0)

/* checksum 8 blocks at once */
#define aesni_addmul8full(in_, accum, hv_, h2v_, h3v_, h4v_, rev) \
do { \
    const unsigned char *in = in_; \
    const __m128i hv = hv_; \
    const __m128i h2v = h2v_; \
    const __m128i h3v = h3v_; \
    const __m128i h4v = h4v_; \
    __m128i accv_; \
    \
    MAKE8(LOADx); \
    accv_ = _mm_load_si128((const __m128i *) accum); \
    MULREDUCE4(rev, hv, h2v, h3v, h4v, in3, in2, in1, in0, accv_); \
    MULREDUCE4(rev, hv, h2v, h3v, h4v, in7, in6, in5, in4, accv_); \
    _mm_store_si128((__m128i *) accum, accv_); \
} while(0)

/* decrypt 8 blocks at once */
#define aesni_decrypt8full(out_, n_, rkeys, in_) \
do { \
    unsigned char       *out = out_; \
    uint32_t            *n = n_; \
    const unsigned char *in = in_; \
    const __m128i        pt = _mm_set_epi8(12, 13, 14, 15, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0); \
    int                  roundctr; \
\
    MAKE8(NVDECLx); \
    MAKE8(TEMPDECLx); \
    MAKE8(NVx); \
    MAKE8(TEMPx); \
    for (roundctr = 1; roundctr < 14; roundctr++) { \
        MAKE8(AESENCx); \
    } \
    MAKE8(AESENCLASTx); \
    MAKE8(XORx); \
    MAKE8(STOREx); \
} while(0)

int
crypto_aead_aes256gcm_beforenm(crypto_aead_aes256gcm_state *ctx_,
                               const unsigned char *k)
{
    context       *ctx = (context *) ctx_;
    __m128i       *rkeys = ctx->rkeys;
    __m128i        zero = _mm_setzero_si128();
    unsigned char *H = ctx->H;

    COMPILER_ASSERT((sizeof *ctx_) >= (sizeof *ctx));
    aesni_key256_expand(k, rkeys);
    aesni_encrypt1(H, zero, rkeys);

    return 0;
}

int
crypto_aead_aes256gcm_encrypt_detached_afternm(unsigned char *c,
                                               unsigned char *mac, unsigned long long *maclen_p,
                                               const unsigned char *m, unsigned long long mlen,
                                               const unsigned char *ad, unsigned long long adlen,
                                               const unsigned char *nsec,
                                               const unsigned char *npub,
                                               const crypto_aead_aes256gcm_state *ctx_)
{
    const __m128i       rev = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const context      *ctx = (const context *) ctx_;
    const __m128i      *rkeys = ctx->rkeys;
    __m128i             Hv, H2v, H3v, H4v, accv;
    unsigned long long  i, j;
    unsigned long long  adlen_rnd64 = adlen & ~63ULL;
    unsigned long long  mlen_rnd128 = mlen & ~127ULL;
    CRYPTO_ALIGN(16) uint32_t      n2[4];
    CRYPTO_ALIGN(16) unsigned char H[16];
    CRYPTO_ALIGN(16) unsigned char T[16];
    CRYPTO_ALIGN(16) unsigned char accum[16];
    CRYPTO_ALIGN(16) unsigned char fb[16];

    (void) nsec;
    memcpy(H, ctx->H, sizeof H);
    if (mlen > crypto_aead_aes256gcm_MESSAGEBYTES_MAX) {
        sodium_misuse(); /* LCOV_EXCL_LINE */
    }
    memcpy(&n2[0], npub, 3 * 4);
    n2[3] = 0x01000000;
    aesni_encrypt1(T, _mm_load_si128((const __m128i *) n2), rkeys);
    {
        uint64_t x;
        x = _bswap64((uint64_t) (8 * adlen));
        memcpy(&fb[0], &x, sizeof x);
        x = _bswap64((uint64_t) (8 * mlen));
        memcpy(&fb[8], &x, sizeof x);
    }
    /* we store H (and it's power) byte-reverted once and for all */
    Hv = _mm_shuffle_epi8(_mm_load_si128((const __m128i *) H), rev);
    _mm_store_si128((__m128i *) H, Hv);
    H2v = mulv(Hv, Hv);
    H3v = mulv(H2v, Hv);
    H4v = mulv(H3v, Hv);

    accv = _mm_setzero_si128();
    /* unrolled by 4 GCM (by 8 doesn't improve using MULREDUCE4) */
    for (i = 0; i < adlen_rnd64; i += 64) {
        __m128i X4_ = _mm_loadu_si128((const __m128i *) (ad + i + 0));
        __m128i X3_ = _mm_loadu_si128((const __m128i *) (ad + i + 16));
        __m128i X2_ = _mm_loadu_si128((const __m128i *) (ad + i + 32));
        __m128i X1_ = _mm_loadu_si128((const __m128i *) (ad + i + 48));
        MULREDUCE4(rev, Hv, H2v, H3v, H4v, X1_, X2_, X3_, X4_, accv);
    }
    _mm_store_si128((__m128i *) accum, accv);

    /* GCM remainder loop */
    for (i = adlen_rnd64; i < adlen; i += 16) {
        unsigned int blocklen = 16;

        if (i + (unsigned long long) blocklen > adlen) {
            blocklen = (unsigned int) (adlen - i);
        }
        addmul(accum, ad + i, blocklen, H);
    }

/* this only does 8 full blocks, so no fancy bounds checking is necessary*/
#define LOOPRND128                                                                                   \
    do {                                                                                             \
        const int iter = 8;                                                                          \
        const int lb = iter * 16;                                                                    \
                                                                                                     \
        for (i = 0; i < mlen_rnd128; i += lb) {                                                      \
            aesni_encrypt8full(c + i, n2, rkeys, m + i, accum, Hv, H2v, H3v, H4v, rev);              \
        }                                                                                            \
    } while(0)

/* remainder loop, with the slower GCM update to accommodate partial blocks */
#define LOOPRMD128                                           \
    do {                                                     \
        const int iter = 8;                                  \
        const int lb = iter * 16;                            \
                                                             \
        for (i = mlen_rnd128; i < mlen; i += lb) {           \
            CRYPTO_ALIGN(16) unsigned char outni[8 * 16];    \
            unsigned long long mj = lb;                      \
                                                             \
            aesni_encrypt8(outni, n2, rkeys);                \
            if ((i + mj) >= mlen) {                          \
                mj = mlen - i;                               \
            }                                                \
            for (j = 0; j < mj; j++) {                       \
                c[i + j] = m[i + j] ^ outni[j];              \
            }                                                \
            for (j = 0; j < mj; j += 16) {                   \
                unsigned int bl = 16;                        \
                                                             \
                if (j + (unsigned long long) bl >= mj) {     \
                    bl = (unsigned int) (mj - j);            \
                }                                            \
                addmul(accum, c + i + j, bl, H);             \
            }                                                \
        }                                                    \
    } while(0)

    n2[3] &= 0x00ffffff;
    COUNTER_INC2(n2);
    LOOPRND128;
    LOOPRMD128;

    addmul(accum, fb, 16, H);

    for (i = 0; i < 16; ++i) {
        mac[i] = T[i] ^ accum[15 - i];
    }
    if (maclen_p != NULL) {
        *maclen_p = 16;
    }
    return 0;
}

int
crypto_aead_aes256gcm_encrypt_afternm(unsigned char *c, unsigned long long *clen_p,
                                      const unsigned char *m, unsigned long long mlen,
                                      const unsigned char *ad, unsigned long long adlen,
                                      const unsigned char *nsec,
                                      const unsigned char *npub,
                                      const crypto_aead_aes256gcm_state *ctx_)
{
    int ret = crypto_aead_aes256gcm_encrypt_detached_afternm(c,
                                                             c + mlen, NULL,
                                                             m, mlen,
                                                             ad, adlen,
                                                             nsec, npub, ctx_);
    if (clen_p != NULL) {
        *clen_p = mlen + crypto_aead_aes256gcm_ABYTES;
    }
    return ret;
}

int
crypto_aead_aes256gcm_decrypt_detached_afternm(unsigned char *m, unsigned char *nsec,
                                               const unsigned char *c, unsigned long long clen,
                                               const unsigned char *mac,
                                               const unsigned char *ad, unsigned long long adlen,
                                               const unsigned char *npub,
                                               const crypto_aead_aes256gcm_state *ctx_)
{
    const __m128i       rev = _mm_set_epi8(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);
    const context      *ctx = (const context *) ctx_;
    const __m128i      *rkeys = ctx->rkeys;
    __m128i             Hv, H2v, H3v, H4v, accv;
    unsigned long long  i, j;
    unsigned long long  adlen_rnd64 = adlen & ~63ULL;
    unsigned long long  mlen;
    unsigned long long  mlen_rnd128;
    CRYPTO_ALIGN(16) uint32_t      n2[4];
    CRYPTO_ALIGN(16) unsigned char H[16];
    CRYPTO_ALIGN(16) unsigned char T[16];
    CRYPTO_ALIGN(16) unsigned char accum[16];
    CRYPTO_ALIGN(16) unsigned char fb[16];

    (void) nsec;
    if (clen > crypto_aead_aes256gcm_MESSAGEBYTES_MAX) {
        sodium_misuse(); /* LCOV_EXCL_LINE */
    }
    mlen = clen;

    memcpy(&n2[0], npub, 3 * 4);
    n2[3] = 0x01000000;
    aesni_encrypt1(T, _mm_load_si128((const __m128i *) n2), rkeys);

    {
        uint64_t x;
        x = _bswap64((uint64_t)(8 * adlen));
        memcpy(&fb[0], &x, sizeof x);
        x = _bswap64((uint64_t)(8 * mlen));
        memcpy(&fb[8], &x, sizeof x);
    }

    memcpy(H, ctx->H, sizeof H);
    Hv = _mm_shuffle_epi8(_mm_load_si128((const __m128i *) H), rev);
    _mm_store_si128((__m128i *) H, Hv);
    H2v = mulv(Hv, Hv);
    H3v = mulv(H2v, Hv);
    H4v = mulv(H3v, Hv);

    accv = _mm_setzero_si128();
    for (i = 0; i < adlen_rnd64; i += 64) {
        __m128i X4_ = _mm_loadu_si128((const __m128i *) (ad + i + 0));
        __m128i X3_ = _mm_loadu_si128((const __m128i *) (ad + i + 16));
        __m128i X2_ = _mm_loadu_si128((const __m128i *) (ad + i + 32));
        __m128i X1_ = _mm_loadu_si128((const __m128i *) (ad + i + 48));
        MULREDUCE4(rev, Hv, H2v, H3v, H4v, X1_, X2_, X3_, X4_, accv);
    }
    _mm_store_si128((__m128i *) accum, accv);

    for (i = adlen_rnd64; i < adlen; i += 16) {
        unsigned int blocklen = 16;
        if (i + (unsigned long long) blocklen > adlen) {
            blocklen = (unsigned int) (adlen - i);
        }
        addmul(accum, ad + i, blocklen, H);
    }

    mlen_rnd128 = mlen & ~127ULL;

#define LOOPACCUMDRND128                                                                          \
    do {                                                                                          \
        const int iter = 8;                                                                       \
        const int lb = iter * 16;                                                                 \
        for (i = 0; i < mlen_rnd128; i += lb) {                                                   \
            aesni_addmul8full(c + i, accum, Hv, H2v, H3v, H4v, rev);                              \
        }                                                                                         \
    } while(0)

#define LOOPDRND128                                                                               \
    do {                                                                                          \
        const int iter = 8;                                                                       \
        const int lb = iter * 16;                                                                 \
                                                                                                  \
        for (i = 0; i < mlen_rnd128; i += lb) {                                                   \
            aesni_decrypt8full(m + i, n2, rkeys, c + i);                                          \
        }                                                                                         \
    } while(0)

#define LOOPACCUMDRMD128                                     \
    do {                                                     \
        const int iter = 8;                                  \
        const int lb = iter * 16;                            \
                                                             \
        for (i = mlen_rnd128; i < mlen; i += lb) {           \
            unsigned long long mj = lb;                      \
                                                             \
            if ((i + mj) >= mlen) {                          \
                mj = mlen - i;                               \
            }                                                \
            for (j = 0; j < mj; j += 16) {                   \
                unsigned int bl = 16;                        \
                                                             \
                if (j + (unsigned long long) bl >= mj) {     \
                    bl = (unsigned int) (mj - j);            \
                }                                            \
                addmul(accum, c + i + j, bl, H);             \
            }                                                \
        }                                                    \
    } while(0)

#define LOOPDRMD128                                          \
    do {                                                     \
        const int iter = 8;                                  \
        const int lb = iter * 16;                            \
                                                             \
        for (i = mlen_rnd128; i < mlen; i += lb) {           \
            CRYPTO_ALIGN(16) unsigned char outni[8 * 16];    \
            unsigned long long mj = lb;                      \
                                                             \
            if ((i + mj) >= mlen) {                          \
                mj = mlen - i;                               \
            }                                                \
            aesni_encrypt8(outni, n2, rkeys);                \
            for (j = 0; j < mj; j++) {                       \
                m[i + j] = c[i + j] ^ outni[j];              \
            }                                                \
        }                                                    \
    } while(0)

    n2[3] &= 0x00ffffff;

    COUNTER_INC2(n2);
    LOOPACCUMDRND128;
    LOOPACCUMDRMD128;
    addmul(accum, fb, 16, H);
    {
        unsigned char d = 0;

        for (i = 0; i < 16; i++) {
            d |= (mac[i] ^ (T[i] ^ accum[15 - i]));
        }
        if (d != 0) {
            if (m != NULL) {
                memset(m, 0, mlen);
            }
            return -1;
        }
        if (m == NULL) {
            return 0;
        }
    }
    n2[3] = 0U;
    COUNTER_INC2(n2);
    LOOPDRND128;
    LOOPDRMD128;

    return 0;
}

int
crypto_aead_aes256gcm_decrypt_afternm(unsigned char *m, unsigned long long *mlen_p,
                                      unsigned char *nsec,
                                      const unsigned char *c, unsigned long long clen,
                                      const unsigned char *ad, unsigned long long adlen,
                                      const unsigned char *npub,
                                      const crypto_aead_aes256gcm_state *ctx_)
{
    unsigned long long mlen = 0ULL;
    int                ret = -1;

    if (clen >= crypto_aead_aes256gcm_ABYTES) {
        ret = crypto_aead_aes256gcm_decrypt_detached_afternm
            (m, nsec, c, clen - crypto_aead_aes256gcm_ABYTES,
             c + clen - crypto_aead_aes256gcm_ABYTES,
             ad, adlen, npub, ctx_);
    }
    if (mlen_p != NULL) {
        if (ret == 0) {
            mlen = clen - crypto_aead_aes256gcm_ABYTES;
        }
        *mlen_p = mlen;
    }
    return ret;
}

int
crypto_aead_aes256gcm_encrypt_detached(unsigned char *c,
                                       unsigned char *mac,
                                       unsigned long long *maclen_p,
                                       const unsigned char *m,
                                       unsigned long long mlen,
                                       const unsigned char *ad,
                                       unsigned long long adlen,
                                       const unsigned char *nsec,
                                       const unsigned char *npub,
                                       const unsigned char *k)
{
    CRYPTO_ALIGN(16) crypto_aead_aes256gcm_state ctx;

    crypto_aead_aes256gcm_beforenm(&ctx, k);

    return crypto_aead_aes256gcm_encrypt_detached_afternm
        (c, mac, maclen_p, m, mlen, ad, adlen, nsec, npub,
            (const crypto_aead_aes256gcm_state *) &ctx);
}

int
crypto_aead_aes256gcm_encrypt(unsigned char *c,
                              unsigned long long *clen_p,
                              const unsigned char *m,
                              unsigned long long mlen,
                              const unsigned char *ad,
                              unsigned long long adlen,
                              const unsigned char *nsec,
                              const unsigned char *npub,
                              const unsigned char *k)
{
    CRYPTO_ALIGN(16) crypto_aead_aes256gcm_state ctx;
    int ret;

    crypto_aead_aes256gcm_beforenm(&ctx, k);

    ret = crypto_aead_aes256gcm_encrypt_afternm
        (c, clen_p, m, mlen, ad, adlen, nsec, npub,
            (const crypto_aead_aes256gcm_state *) &ctx);
    sodium_memzero(ctx, sizeof ctx);

    return ret;
}

int
crypto_aead_aes256gcm_decrypt_detached(unsigned char *m,
                                       unsigned char *nsec,
                                       const unsigned char *c,
                                       unsigned long long clen,
                                       const unsigned char *mac,
                                       const unsigned char *ad,
                                       unsigned long long adlen,
                                       const unsigned char *npub,
                                       const unsigned char *k)
{
    CRYPTO_ALIGN(16) crypto_aead_aes256gcm_state ctx;

    crypto_aead_aes256gcm_beforenm(&ctx, k);

    return crypto_aead_aes256gcm_decrypt_detached_afternm
        (m, nsec, c, clen, mac, ad, adlen, npub,
            (const crypto_aead_aes256gcm_state *) &ctx);
}

int
crypto_aead_aes256gcm_decrypt(unsigned char *m,
                              unsigned long long *mlen_p,
                              unsigned char *nsec,
                              const unsigned char *c,
                              unsigned long long clen,
                              const unsigned char *ad,
                              unsigned long long adlen,
                              const unsigned char *npub,
                              const unsigned char *k)
{
    CRYPTO_ALIGN(16) crypto_aead_aes256gcm_state ctx;
    int ret;

    crypto_aead_aes256gcm_beforenm(&ctx, k);

    ret = crypto_aead_aes256gcm_decrypt_afternm
        (m, mlen_p, nsec, c, clen, ad, adlen, npub,
         (const crypto_aead_aes256gcm_state *) &ctx);
    sodium_memzero(ctx, sizeof ctx);

    return ret;
}

int
crypto_aead_aes256gcm_is_available(void)
{
    return sodium_runtime_has_pclmul() & sodium_runtime_has_aesni();
}

#else

int
crypto_aead_aes256gcm_encrypt_detached(unsigned char *c,
                                       unsigned char *mac,
                                       unsigned long long *maclen_p,
                                       const unsigned char *m,
                                       unsigned long long mlen,
                                       const unsigned char *ad,
                                       unsigned long long adlen,
                                       const unsigned char *nsec,
                                       const unsigned char *npub,
                                       const unsigned char *k)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_encrypt(unsigned char *c, unsigned long long *clen_p,
                              const unsigned char *m, unsigned long long mlen,
                              const unsigned char *ad, unsigned long long adlen,
                              const unsigned char *nsec, const unsigned char *npub,
                              const unsigned char *k)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_decrypt_detached(unsigned char *m,
                                       unsigned char *nsec,
                                       const unsigned char *c,
                                       unsigned long long clen,
                                       const unsigned char *mac,
                                       const unsigned char *ad,
                                       unsigned long long adlen,
                                       const unsigned char *npub,
                                       const unsigned char *k)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_decrypt(unsigned char *m, unsigned long long *mlen_p,
                              unsigned char *nsec, const unsigned char *c,
                              unsigned long long clen, const unsigned char *ad,
                              unsigned long long adlen, const unsigned char *npub,
                              const unsigned char *k)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_beforenm(crypto_aead_aes256gcm_state *ctx_,
                               const unsigned char *k)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_encrypt_detached_afternm(unsigned char *c,
                                               unsigned char *mac, unsigned long long *maclen_p,
                                               const unsigned char *m, unsigned long long mlen,
                                               const unsigned char *ad, unsigned long long adlen,
                                               const unsigned char *nsec,
                                               const unsigned char *npub,
                                               const crypto_aead_aes256gcm_state *ctx_)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_encrypt_afternm(unsigned char *c, unsigned long long *clen_p,
                                      const unsigned char *m, unsigned long long mlen,
                                      const unsigned char *ad, unsigned long long adlen,
                                      const unsigned char *nsec, const unsigned char *npub,
                                      const crypto_aead_aes256gcm_state *ctx_)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_decrypt_detached_afternm(unsigned char *m, unsigned char *nsec,
                                               const unsigned char *c, unsigned long long clen,
                                               const unsigned char *mac,
                                               const unsigned char *ad, unsigned long long adlen,
                                               const unsigned char *npub,
                                               const crypto_aead_aes256gcm_state *ctx_)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_decrypt_afternm(unsigned char *m, unsigned long long *mlen_p,
                                      unsigned char *nsec,
                                      const unsigned char *c, unsigned long long clen,
                                      const unsigned char *ad, unsigned long long adlen,
                                      const unsigned char *npub,
                                      const crypto_aead_aes256gcm_state *ctx_)
{
    errno = ENOSYS;
    return -1;
}

int
crypto_aead_aes256gcm_is_available(void)
{
    return 0;
}

#endif

size_t
crypto_aead_aes256gcm_keybytes(void)
{
    return crypto_aead_aes256gcm_KEYBYTES;
}

size_t
crypto_aead_aes256gcm_nsecbytes(void)
{
    return crypto_aead_aes256gcm_NSECBYTES;
}

size_t
crypto_aead_aes256gcm_npubbytes(void)
{
    return crypto_aead_aes256gcm_NPUBBYTES;
}

size_t
crypto_aead_aes256gcm_abytes(void)
{
    return crypto_aead_aes256gcm_ABYTES;
}

size_t
crypto_aead_aes256gcm_statebytes(void)
{
    return (sizeof(crypto_aead_aes256gcm_state) + (size_t) 15U) & ~(size_t) 15U;
}

size_t
crypto_aead_aes256gcm_messagebytes_max(void)
{
    return crypto_aead_aes256gcm_MESSAGEBYTES_MAX;
}

void
crypto_aead_aes256gcm_keygen(unsigned char k[crypto_aead_aes256gcm_KEYBYTES])
{
    randombytes_buf(k, crypto_aead_aes256gcm_KEYBYTES);
}
