
#include <stdint.h>
#include <string.h>

#include "../onetimeauth_poly1305.h"
#include "crypto_verify_16.h"
#include "poly1305_sse2.h"
#include "private/common.h"
#include "private/sse2_64_32.h"
#include "utils.h"

#if defined(HAVE_TI_MODE) && defined(HAVE_EMMINTRIN_H)

# ifdef __GNUC__
#  pragma GCC target("sse2")
# endif

# include <emmintrin.h>

typedef __m128i xmmi;

# if defined(_MSC_VER)
#  define POLY1305_NOINLINE __declspec(noinline)
# elif defined(__clang__) || defined(__GNUC__)
#  define POLY1305_NOINLINE __attribute__((noinline))
# else
#  define POLY1305_NOINLINE
# endif

# define poly1305_block_size 32

enum poly1305_state_flags_t {
    poly1305_started       = 1,
    poly1305_final_shift8  = 4,
    poly1305_final_shift16 = 8,
    poly1305_final_r2_r    = 16, /* use [r^2,r] for the final block */
    poly1305_final_r_1     = 32  /* use [r,1] for the final block */
};

typedef struct poly1305_state_internal_t {
    union {
        uint64_t h[3];
        uint32_t hh[10];
    } H;                                            /*  40 bytes  */
    uint32_t           R[5];                        /*  20 bytes  */
    uint32_t           R2[5];                       /*  20 bytes  */
    uint32_t           R4[5];                       /*  20 bytes  */
    uint64_t           pad[2];                      /*  16 bytes  */
    uint64_t           flags;                       /*   8 bytes  */
    unsigned long long leftover;                    /* 8 bytes */
    unsigned char      buffer[poly1305_block_size]; /* 32 bytes */
} poly1305_state_internal_t;                        /* 164 bytes total */

/*
 * _mm_loadl_epi64() is turned into a simple MOVQ. So, unaligned accesses are
 * totally fine, even though this intrinsic requires a __m128i* input.
 * This confuses dynamic analysis, so force alignment, only in debug mode.
 */
# ifdef DEBUG
static xmmi
_fakealign_mm_loadl_epi64(const void *m)
{
    xmmi tmp;
    memcpy(&tmp, m, 8);

    return _mm_loadl_epi64(&tmp);
}
# define _mm_loadl_epi64(X) _fakealign_mm_loadl_epi64(X)
#endif

/* copy 0-31 bytes */
static inline void
poly1305_block_copy31(unsigned char *dst, const unsigned char *src,
                      unsigned long long bytes)
{
    if (bytes & 16) {
        _mm_store_si128((xmmi *) (void *) dst,
                        _mm_loadu_si128((const xmmi *) (const void *) src));
        src += 16;
        dst += 16;
    }
    if (bytes & 8) {
        memcpy(dst, src, 8);
        src += 8;
        dst += 8;
    }
    if (bytes & 4) {
        memcpy(dst, src, 4);
        src += 4;
        dst += 4;
    }
    if (bytes & 2) {
        memcpy(dst, src, 2);
        src += 2;
        dst += 2;
    }
    if (bytes & 1) {
        *dst = *src;
    }
}

static POLY1305_NOINLINE void
poly1305_init_ext(poly1305_state_internal_t *st, const unsigned char key[32],
                  unsigned long long bytes)
{
    uint32_t          *R;
    uint128_t          d[3];
    uint64_t           r0, r1, r2;
    uint64_t           rt0, rt1, rt2, st2, c;
    uint64_t           t0, t1;
    unsigned long long i;

    if (!bytes) {
        bytes = ~(unsigned long long) 0;
    }
    /* H = 0 */
    _mm_storeu_si128((xmmi *) (void *) &st->H.hh[0], _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) &st->H.hh[4], _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) &st->H.hh[8], _mm_setzero_si128());

    /* clamp key */
    memcpy(&t0, key, 8);
    memcpy(&t1, key + 8, 8);
    r0 = t0 & 0xffc0fffffff;
    t0 >>= 44;
    t0 |= t1 << 20;
    r1 = t0 & 0xfffffc0ffff;
    t1 >>= 24;
    r2 = t1 & 0x00ffffffc0f;

    /* r^1 */
    R    = st->R;
    R[0] = (uint32_t)(r0) &0x3ffffff;
    R[1] = (uint32_t)((r0 >> 26) | (r1 << 18)) & 0x3ffffff;
    R[2] = (uint32_t)((r1 >> 8)) & 0x3ffffff;
    R[3] = (uint32_t)((r1 >> 34) | (r2 << 10)) & 0x3ffffff;
    R[4] = (uint32_t)((r2 >> 16));

    /* save pad */
    memcpy(&st->pad[0], key + 16, 8);
    memcpy(&st->pad[1], key + 24, 8);

    rt0 = r0;
    rt1 = r1;
    rt2 = r2;

    /* r^2, r^4 */
    for (i = 0; i < 2; i++) {
        if (i == 0) {
            R = st->R2;
            if (bytes <= 16) {
                break;
            }
        } else if (i == 1) {
            R = st->R4;
            if (bytes < 96) {
                break;
            }
        }
        st2 = rt2 * (5 << 2);

        d[0] = ((uint128_t) rt0 * rt0) + ((uint128_t)(rt1 * 2) * st2);
        d[1] = ((uint128_t) rt2 * st2) + ((uint128_t)(rt0 * 2) * rt1);
        d[2] = ((uint128_t) rt1 * rt1) + ((uint128_t)(rt2 * 2) * rt0);

        rt0 = (uint64_t) d[0] & 0xfffffffffff;
        c   = (uint64_t)(d[0] >> 44);
        d[1] += c;

        rt1 = (uint64_t) d[1] & 0xfffffffffff;
        c   = (uint64_t)(d[1] >> 44);
        d[2] += c;

        rt2 = (uint64_t) d[2] & 0x3ffffffffff;
        c   = (uint64_t)(d[2] >> 42);
        rt0 += c * 5;
        c   = (rt0 >> 44);
        rt0 = rt0 & 0xfffffffffff;
        rt1 += c;
        c   = (rt1 >> 44);
        rt1 = rt1 & 0xfffffffffff;
        rt2 += c; /* even if rt2 overflows, it will still fit in rp4 safely, and
                     is safe to multiply with */

        R[0] = (uint32_t)(rt0) &0x3ffffff;
        R[1] = (uint32_t)((rt0 >> 26) | (rt1 << 18)) & 0x3ffffff;
        R[2] = (uint32_t)((rt1 >> 8)) & 0x3ffffff;
        R[3] = (uint32_t)((rt1 >> 34) | (rt2 << 10)) & 0x3ffffff;
        R[4] = (uint32_t)((rt2 >> 16));
    }
    st->flags    = 0;
    st->leftover = 0U;
}

static POLY1305_NOINLINE void
poly1305_blocks(poly1305_state_internal_t *st, const unsigned char *m,
                unsigned long long bytes)
{
    CRYPTO_ALIGN(64)
    xmmi HIBIT =
        _mm_shuffle_epi32(_mm_cvtsi32_si128(1 << 24), _MM_SHUFFLE(1, 0, 1, 0));
    const xmmi MMASK = _mm_shuffle_epi32(_mm_cvtsi32_si128((1 << 26) - 1),
                                         _MM_SHUFFLE(1, 0, 1, 0));
    const xmmi FIVE =
        _mm_shuffle_epi32(_mm_cvtsi32_si128(5), _MM_SHUFFLE(1, 0, 1, 0));
    xmmi H0, H1, H2, H3, H4;
    xmmi T0, T1, T2, T3, T4, T5, T6, T7, T8;
    xmmi M0, M1, M2, M3, M4;
    xmmi M5, M6, M7, M8;
    xmmi C1, C2;
    xmmi R20, R21, R22, R23, R24, S21, S22, S23, S24;
    xmmi R40, R41, R42, R43, R44, S41, S42, S43, S44;

    if (st->flags & poly1305_final_shift8) {
        HIBIT = _mm_srli_si128(HIBIT, 8);
    }
    if (st->flags & poly1305_final_shift16) {
        HIBIT = _mm_setzero_si128();
    }
    if (!(st->flags & poly1305_started)) {
        /* H = [Mx,My] */
        T5 = _mm_unpacklo_epi64(
            _mm_loadl_epi64((const xmmi *) (const void *) (m + 0)),
            _mm_loadl_epi64((const xmmi *) (const void *) (m + 16)));
        T6 = _mm_unpacklo_epi64(
            _mm_loadl_epi64((const xmmi *) (const void *) (m + 8)),
            _mm_loadl_epi64((const xmmi *) (const void *) (m + 24)));
        H0 = _mm_and_si128(MMASK, T5);
        H1 = _mm_and_si128(MMASK, _mm_srli_epi64(T5, 26));
        T5 = _mm_or_si128(_mm_srli_epi64(T5, 52), _mm_slli_epi64(T6, 12));
        H2 = _mm_and_si128(MMASK, T5);
        H3 = _mm_and_si128(MMASK, _mm_srli_epi64(T5, 26));
        H4 = _mm_srli_epi64(T6, 40);
        H4 = _mm_or_si128(H4, HIBIT);
        m += 32;
        bytes -= 32;
        st->flags |= poly1305_started;
    } else {
        T0 = _mm_loadu_si128((const xmmi *) (const void *) &st->H.hh[0]);
        T1 = _mm_loadu_si128((const xmmi *) (const void *) &st->H.hh[4]);
        T2 = _mm_loadu_si128((const xmmi *) (const void *) &st->H.hh[8]);
        H0 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(1, 1, 0, 0));
        H1 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(3, 3, 2, 2));
        H2 = _mm_shuffle_epi32(T1, _MM_SHUFFLE(1, 1, 0, 0));
        H3 = _mm_shuffle_epi32(T1, _MM_SHUFFLE(3, 3, 2, 2));
        H4 = _mm_shuffle_epi32(T2, _MM_SHUFFLE(1, 1, 0, 0));
    }
    if (st->flags & (poly1305_final_r2_r | poly1305_final_r_1)) {
        if (st->flags & poly1305_final_r2_r) {
            /* use [r^2, r] */
            T2  = _mm_loadu_si128((const xmmi *) (const void *) &st->R[0]);
            T3  = _mm_cvtsi32_si128(st->R[4]);
            T0  = _mm_loadu_si128((const xmmi *) (const void *) &st->R2[0]);
            T1  = _mm_cvtsi32_si128(st->R2[4]);
            T4  = _mm_unpacklo_epi32(T0, T2);
            T5  = _mm_unpackhi_epi32(T0, T2);
            R24 = _mm_unpacklo_epi64(T1, T3);
        } else {
            /* use [r^1, 1] */
            T0  = _mm_loadu_si128((const xmmi *) (const void *) &st->R[0]);
            T1  = _mm_cvtsi32_si128(st->R[4]);
            T2  = _mm_cvtsi32_si128(1);
            T4  = _mm_unpacklo_epi32(T0, T2);
            T5  = _mm_unpackhi_epi32(T0, T2);
            R24 = T1;
        }
        R20 = _mm_shuffle_epi32(T4, _MM_SHUFFLE(1, 1, 0, 0));
        R21 = _mm_shuffle_epi32(T4, _MM_SHUFFLE(3, 3, 2, 2));
        R22 = _mm_shuffle_epi32(T5, _MM_SHUFFLE(1, 1, 0, 0));
        R23 = _mm_shuffle_epi32(T5, _MM_SHUFFLE(3, 3, 2, 2));
    } else {
        /* use [r^2, r^2] */
        T0  = _mm_loadu_si128((const xmmi *) (const void *) &st->R2[0]);
        T1  = _mm_cvtsi32_si128(st->R2[4]);
        R20 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(0, 0, 0, 0));
        R21 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(1, 1, 1, 1));
        R22 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(2, 2, 2, 2));
        R23 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(3, 3, 3, 3));
        R24 = _mm_shuffle_epi32(T1, _MM_SHUFFLE(0, 0, 0, 0));
    }
    S21 = _mm_mul_epu32(R21, FIVE);
    S22 = _mm_mul_epu32(R22, FIVE);
    S23 = _mm_mul_epu32(R23, FIVE);
    S24 = _mm_mul_epu32(R24, FIVE);

    if (bytes >= 64) {
        T0  = _mm_loadu_si128((const xmmi *) (const void *) &st->R4[0]);
        T1  = _mm_cvtsi32_si128(st->R4[4]);
        R40 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(0, 0, 0, 0));
        R41 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(1, 1, 1, 1));
        R42 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(2, 2, 2, 2));
        R43 = _mm_shuffle_epi32(T0, _MM_SHUFFLE(3, 3, 3, 3));
        R44 = _mm_shuffle_epi32(T1, _MM_SHUFFLE(0, 0, 0, 0));
        S41 = _mm_mul_epu32(R41, FIVE);
        S42 = _mm_mul_epu32(R42, FIVE);
        S43 = _mm_mul_epu32(R43, FIVE);
        S44 = _mm_mul_epu32(R44, FIVE);

        while (bytes >= 64) {
            xmmi v00, v01, v02, v03, v04;
            xmmi v10, v11, v12, v13, v14;
            xmmi v20, v21, v22, v23, v24;
            xmmi v30, v31, v32, v33, v34;
            xmmi v40, v41, v42, v43, v44;
            xmmi T14, T15;

            /* H *= [r^4,r^4], preload [Mx,My] */
            T15 = S42;
            T0  = H4;
            T0  = _mm_mul_epu32(T0, S41);
            v01 = H3;
            v01 = _mm_mul_epu32(v01, T15);
            T14 = S43;
            T1  = H4;
            T1  = _mm_mul_epu32(T1, T15);
            v11 = H3;
            v11 = _mm_mul_epu32(v11, T14);
            T2  = H4;
            T2  = _mm_mul_epu32(T2, T14);
            T0  = _mm_add_epi64(T0, v01);
            T15 = S44;
            v02 = H2;
            v02 = _mm_mul_epu32(v02, T14);
            T3  = H4;
            T3  = _mm_mul_epu32(T3, T15);
            T1  = _mm_add_epi64(T1, v11);
            v03 = H1;
            v03 = _mm_mul_epu32(v03, T15);
            v12 = H2;
            v12 = _mm_mul_epu32(v12, T15);
            T0  = _mm_add_epi64(T0, v02);
            T14 = R40;
            v21 = H3;
            v21 = _mm_mul_epu32(v21, T15);
            v31 = H3;
            v31 = _mm_mul_epu32(v31, T14);
            T0  = _mm_add_epi64(T0, v03);
            T4  = H4;
            T4  = _mm_mul_epu32(T4, T14);
            T1  = _mm_add_epi64(T1, v12);
            v04 = H0;
            v04 = _mm_mul_epu32(v04, T14);
            T2  = _mm_add_epi64(T2, v21);
            v13 = H1;
            v13 = _mm_mul_epu32(v13, T14);
            T3  = _mm_add_epi64(T3, v31);
            T15 = R41;
            v22 = H2;
            v22 = _mm_mul_epu32(v22, T14);
            v32 = H2;
            v32 = _mm_mul_epu32(v32, T15);
            T0  = _mm_add_epi64(T0, v04);
            v41 = H3;
            v41 = _mm_mul_epu32(v41, T15);
            T1  = _mm_add_epi64(T1, v13);
            v14 = H0;
            v14 = _mm_mul_epu32(v14, T15);
            T2  = _mm_add_epi64(T2, v22);
            T14 = R42;
            T5  = _mm_unpacklo_epi64(
                _mm_loadl_epi64((const xmmi *) (const void *) (m + 0)),
                _mm_loadl_epi64((const xmmi *) (const void *) (m + 16)));
            v23 = H1;
            v23 = _mm_mul_epu32(v23, T15);
            T3  = _mm_add_epi64(T3, v32);
            v33 = H1;
            v33 = _mm_mul_epu32(v33, T14);
            T4  = _mm_add_epi64(T4, v41);
            v42 = H2;
            v42 = _mm_mul_epu32(v42, T14);
            T1  = _mm_add_epi64(T1, v14);
            T15 = R43;
            T6  = _mm_unpacklo_epi64(
                _mm_loadl_epi64((const xmmi *) (const void *) (m + 8)),
                _mm_loadl_epi64((const xmmi *) (const void *) (m + 24)));
            v24 = H0;
            v24 = _mm_mul_epu32(v24, T14);
            T2  = _mm_add_epi64(T2, v23);
            v34 = H0;
            v34 = _mm_mul_epu32(v34, T15);
            T3  = _mm_add_epi64(T3, v33);
            M0  = _mm_and_si128(MMASK, T5);
            v43 = H1;
            v43 = _mm_mul_epu32(v43, T15);
            T4  = _mm_add_epi64(T4, v42);
            M1  = _mm_and_si128(MMASK, _mm_srli_epi64(T5, 26));
            v44 = H0;
            v44 = _mm_mul_epu32(v44, R44);
            T2  = _mm_add_epi64(T2, v24);
            T5  = _mm_or_si128(_mm_srli_epi64(T5, 52), _mm_slli_epi64(T6, 12));
            T3  = _mm_add_epi64(T3, v34);
            M3  = _mm_and_si128(MMASK, _mm_srli_epi64(T6, 14));
            T4  = _mm_add_epi64(T4, v43);
            M2  = _mm_and_si128(MMASK, T5);
            T4  = _mm_add_epi64(T4, v44);
            M4  = _mm_or_si128(_mm_srli_epi64(T6, 40), HIBIT);

            /* H += [Mx',My'] */
            T5 = _mm_loadu_si128((const xmmi *) (const void *) (m + 32));
            T6 = _mm_loadu_si128((const xmmi *) (const void *) (m + 48));
            T7 = _mm_unpacklo_epi32(T5, T6);
            T8 = _mm_unpackhi_epi32(T5, T6);
            M5 = _mm_unpacklo_epi32(T7, _mm_setzero_si128());
            M6 = _mm_unpackhi_epi32(T7, _mm_setzero_si128());
            M7 = _mm_unpacklo_epi32(T8, _mm_setzero_si128());
            M8 = _mm_unpackhi_epi32(T8, _mm_setzero_si128());
            M6 = _mm_slli_epi64(M6, 6);
            M7 = _mm_slli_epi64(M7, 12);
            M8 = _mm_slli_epi64(M8, 18);
            T0 = _mm_add_epi64(T0, M5);
            T1 = _mm_add_epi64(T1, M6);
            T2 = _mm_add_epi64(T2, M7);
            T3 = _mm_add_epi64(T3, M8);
            T4 = _mm_add_epi64(T4, HIBIT);

            /* H += [Mx,My]*[r^2,r^2] */
            T15 = S22;
            v00 = M4;
            v00 = _mm_mul_epu32(v00, S21);
            v01 = M3;
            v01 = _mm_mul_epu32(v01, T15);
            T14 = S23;
            v10 = M4;
            v10 = _mm_mul_epu32(v10, T15);
            v11 = M3;
            v11 = _mm_mul_epu32(v11, T14);
            T0  = _mm_add_epi64(T0, v00);
            v20 = M4;
            v20 = _mm_mul_epu32(v20, T14);
            T0  = _mm_add_epi64(T0, v01);
            T15 = S24;
            v02 = M2;
            v02 = _mm_mul_epu32(v02, T14);
            T1  = _mm_add_epi64(T1, v10);
            v30 = M4;
            v30 = _mm_mul_epu32(v30, T15);
            T1  = _mm_add_epi64(T1, v11);
            v03 = M1;
            v03 = _mm_mul_epu32(v03, T15);
            T2  = _mm_add_epi64(T2, v20);
            v12 = M2;
            v12 = _mm_mul_epu32(v12, T15);
            T0  = _mm_add_epi64(T0, v02);
            T14 = R20;
            v21 = M3;
            v21 = _mm_mul_epu32(v21, T15);
            T3  = _mm_add_epi64(T3, v30);
            v31 = M3;
            v31 = _mm_mul_epu32(v31, T14);
            T0  = _mm_add_epi64(T0, v03);
            v40 = M4;
            v40 = _mm_mul_epu32(v40, T14);
            T1  = _mm_add_epi64(T1, v12);
            v04 = M0;
            v04 = _mm_mul_epu32(v04, T14);
            T2  = _mm_add_epi64(T2, v21);
            v13 = M1;
            v13 = _mm_mul_epu32(v13, T14);
            T3  = _mm_add_epi64(T3, v31);
            T15 = R21;
            v22 = M2;
            v22 = _mm_mul_epu32(v22, T14);
            T4  = _mm_add_epi64(T4, v40);
            v32 = M2;
            v32 = _mm_mul_epu32(v32, T15);
            T0  = _mm_add_epi64(T0, v04);
            v41 = M3;
            v41 = _mm_mul_epu32(v41, T15);
            T1  = _mm_add_epi64(T1, v13);
            v14 = M0;
            v14 = _mm_mul_epu32(v14, T15);
            T2  = _mm_add_epi64(T2, v22);
            T14 = R22;
            v23 = M1;
            v23 = _mm_mul_epu32(v23, T15);
            T3  = _mm_add_epi64(T3, v32);
            v33 = M1;
            v33 = _mm_mul_epu32(v33, T14);
            T4  = _mm_add_epi64(T4, v41);
            v42 = M2;
            v42 = _mm_mul_epu32(v42, T14);
            T1  = _mm_add_epi64(T1, v14);
            T15 = R23;
            v24 = M0;
            v24 = _mm_mul_epu32(v24, T14);
            T2  = _mm_add_epi64(T2, v23);
            v34 = M0;
            v34 = _mm_mul_epu32(v34, T15);
            T3  = _mm_add_epi64(T3, v33);
            v43 = M1;
            v43 = _mm_mul_epu32(v43, T15);
            T4  = _mm_add_epi64(T4, v42);
            v44 = M0;
            v44 = _mm_mul_epu32(v44, R24);
            T2  = _mm_add_epi64(T2, v24);
            T3  = _mm_add_epi64(T3, v34);
            T4  = _mm_add_epi64(T4, v43);
            T4  = _mm_add_epi64(T4, v44);

            /* reduce */
            C1 = _mm_srli_epi64(T0, 26);
            C2 = _mm_srli_epi64(T3, 26);
            T0 = _mm_and_si128(T0, MMASK);
            T3 = _mm_and_si128(T3, MMASK);
            T1 = _mm_add_epi64(T1, C1);
            T4 = _mm_add_epi64(T4, C2);
            C1 = _mm_srli_epi64(T1, 26);
            C2 = _mm_srli_epi64(T4, 26);
            T1 = _mm_and_si128(T1, MMASK);
            T4 = _mm_and_si128(T4, MMASK);
            T2 = _mm_add_epi64(T2, C1);
            T0 = _mm_add_epi64(T0, _mm_mul_epu32(C2, FIVE));
            C1 = _mm_srli_epi64(T2, 26);
            C2 = _mm_srli_epi64(T0, 26);
            T2 = _mm_and_si128(T2, MMASK);
            T0 = _mm_and_si128(T0, MMASK);
            T3 = _mm_add_epi64(T3, C1);
            T1 = _mm_add_epi64(T1, C2);
            C1 = _mm_srli_epi64(T3, 26);
            T3 = _mm_and_si128(T3, MMASK);
            T4 = _mm_add_epi64(T4, C1);

            /* Final: H = (H*[r^4,r^4] + [Mx,My]*[r^2,r^2] + [Mx',My']) */
            H0 = T0;
            H1 = T1;
            H2 = T2;
            H3 = T3;
            H4 = T4;

            m += 64;
            bytes -= 64;
        }
    }

    if (bytes >= 32) {
        xmmi v01, v02, v03, v04;
        xmmi v11, v12, v13, v14;
        xmmi v21, v22, v23, v24;
        xmmi v31, v32, v33, v34;
        xmmi v41, v42, v43, v44;
        xmmi T14, T15;

        /* H *= [r^2,r^2] */
        T15 = S22;
        T0  = H4;
        T0  = _mm_mul_epu32(T0, S21);
        v01 = H3;
        v01 = _mm_mul_epu32(v01, T15);
        T14 = S23;
        T1  = H4;
        T1  = _mm_mul_epu32(T1, T15);
        v11 = H3;
        v11 = _mm_mul_epu32(v11, T14);
        T2  = H4;
        T2  = _mm_mul_epu32(T2, T14);
        T0  = _mm_add_epi64(T0, v01);
        T15 = S24;
        v02 = H2;
        v02 = _mm_mul_epu32(v02, T14);
        T3  = H4;
        T3  = _mm_mul_epu32(T3, T15);
        T1  = _mm_add_epi64(T1, v11);
        v03 = H1;
        v03 = _mm_mul_epu32(v03, T15);
        v12 = H2;
        v12 = _mm_mul_epu32(v12, T15);
        T0  = _mm_add_epi64(T0, v02);
        T14 = R20;
        v21 = H3;
        v21 = _mm_mul_epu32(v21, T15);
        v31 = H3;
        v31 = _mm_mul_epu32(v31, T14);
        T0  = _mm_add_epi64(T0, v03);
        T4  = H4;
        T4  = _mm_mul_epu32(T4, T14);
        T1  = _mm_add_epi64(T1, v12);
        v04 = H0;
        v04 = _mm_mul_epu32(v04, T14);
        T2  = _mm_add_epi64(T2, v21);
        v13 = H1;
        v13 = _mm_mul_epu32(v13, T14);
        T3  = _mm_add_epi64(T3, v31);
        T15 = R21;
        v22 = H2;
        v22 = _mm_mul_epu32(v22, T14);
        v32 = H2;
        v32 = _mm_mul_epu32(v32, T15);
        T0  = _mm_add_epi64(T0, v04);
        v41 = H3;
        v41 = _mm_mul_epu32(v41, T15);
        T1  = _mm_add_epi64(T1, v13);
        v14 = H0;
        v14 = _mm_mul_epu32(v14, T15);
        T2  = _mm_add_epi64(T2, v22);
        T14 = R22;
        v23 = H1;
        v23 = _mm_mul_epu32(v23, T15);
        T3  = _mm_add_epi64(T3, v32);
        v33 = H1;
        v33 = _mm_mul_epu32(v33, T14);
        T4  = _mm_add_epi64(T4, v41);
        v42 = H2;
        v42 = _mm_mul_epu32(v42, T14);
        T1  = _mm_add_epi64(T1, v14);
        T15 = R23;
        v24 = H0;
        v24 = _mm_mul_epu32(v24, T14);
        T2  = _mm_add_epi64(T2, v23);
        v34 = H0;
        v34 = _mm_mul_epu32(v34, T15);
        T3  = _mm_add_epi64(T3, v33);
        v43 = H1;
        v43 = _mm_mul_epu32(v43, T15);
        T4  = _mm_add_epi64(T4, v42);
        v44 = H0;
        v44 = _mm_mul_epu32(v44, R24);
        T2  = _mm_add_epi64(T2, v24);
        T3  = _mm_add_epi64(T3, v34);
        T4  = _mm_add_epi64(T4, v43);
        T4  = _mm_add_epi64(T4, v44);

        /* H += [Mx,My] */
        if (m) {
            T5 = _mm_loadu_si128((const xmmi *) (const void *) (m + 0));
            T6 = _mm_loadu_si128((const xmmi *) (const void *) (m + 16));
            T7 = _mm_unpacklo_epi32(T5, T6);
            T8 = _mm_unpackhi_epi32(T5, T6);
            M0 = _mm_unpacklo_epi32(T7, _mm_setzero_si128());
            M1 = _mm_unpackhi_epi32(T7, _mm_setzero_si128());
            M2 = _mm_unpacklo_epi32(T8, _mm_setzero_si128());
            M3 = _mm_unpackhi_epi32(T8, _mm_setzero_si128());
            M1 = _mm_slli_epi64(M1, 6);
            M2 = _mm_slli_epi64(M2, 12);
            M3 = _mm_slli_epi64(M3, 18);
            T0 = _mm_add_epi64(T0, M0);
            T1 = _mm_add_epi64(T1, M1);
            T2 = _mm_add_epi64(T2, M2);
            T3 = _mm_add_epi64(T3, M3);
            T4 = _mm_add_epi64(T4, HIBIT);
        }

        /* reduce */
        C1 = _mm_srli_epi64(T0, 26);
        C2 = _mm_srli_epi64(T3, 26);
        T0 = _mm_and_si128(T0, MMASK);
        T3 = _mm_and_si128(T3, MMASK);
        T1 = _mm_add_epi64(T1, C1);
        T4 = _mm_add_epi64(T4, C2);
        C1 = _mm_srli_epi64(T1, 26);
        C2 = _mm_srli_epi64(T4, 26);
        T1 = _mm_and_si128(T1, MMASK);
        T4 = _mm_and_si128(T4, MMASK);
        T2 = _mm_add_epi64(T2, C1);
        T0 = _mm_add_epi64(T0, _mm_mul_epu32(C2, FIVE));
        C1 = _mm_srli_epi64(T2, 26);
        C2 = _mm_srli_epi64(T0, 26);
        T2 = _mm_and_si128(T2, MMASK);
        T0 = _mm_and_si128(T0, MMASK);
        T3 = _mm_add_epi64(T3, C1);
        T1 = _mm_add_epi64(T1, C2);
        C1 = _mm_srli_epi64(T3, 26);
        T3 = _mm_and_si128(T3, MMASK);
        T4 = _mm_add_epi64(T4, C1);

        /* H = (H*[r^2,r^2] + [Mx,My]) */
        H0 = T0;
        H1 = T1;
        H2 = T2;
        H3 = T3;
        H4 = T4;
    }

    if (m) {
        T0 = _mm_shuffle_epi32(H0, _MM_SHUFFLE(0, 0, 2, 0));
        T1 = _mm_shuffle_epi32(H1, _MM_SHUFFLE(0, 0, 2, 0));
        T2 = _mm_shuffle_epi32(H2, _MM_SHUFFLE(0, 0, 2, 0));
        T3 = _mm_shuffle_epi32(H3, _MM_SHUFFLE(0, 0, 2, 0));
        T4 = _mm_shuffle_epi32(H4, _MM_SHUFFLE(0, 0, 2, 0));
        T0 = _mm_unpacklo_epi64(T0, T1);
        T1 = _mm_unpacklo_epi64(T2, T3);
        _mm_storeu_si128((xmmi *) (void *) &st->H.hh[0], T0);
        _mm_storeu_si128((xmmi *) (void *) &st->H.hh[4], T1);
        _mm_storel_epi64((xmmi *) (void *) &st->H.hh[8], T4);
    } else {
        uint32_t t0, t1, t2, t3, t4, b;
        uint64_t h0, h1, h2, g0, g1, g2, c, nc;

        /* H = H[0]+H[1] */
        T0 = H0;
        T1 = H1;
        T2 = H2;
        T3 = H3;
        T4 = H4;

        T0 = _mm_add_epi64(T0, _mm_srli_si128(T0, 8));
        T1 = _mm_add_epi64(T1, _mm_srli_si128(T1, 8));
        T2 = _mm_add_epi64(T2, _mm_srli_si128(T2, 8));
        T3 = _mm_add_epi64(T3, _mm_srli_si128(T3, 8));
        T4 = _mm_add_epi64(T4, _mm_srli_si128(T4, 8));

        t0 = _mm_cvtsi128_si32(T0);
        b  = (t0 >> 26);
        t0 &= 0x3ffffff;
        t1 = _mm_cvtsi128_si32(T1) + b;
        b  = (t1 >> 26);
        t1 &= 0x3ffffff;
        t2 = _mm_cvtsi128_si32(T2) + b;
        b  = (t2 >> 26);
        t2 &= 0x3ffffff;
        t3 = _mm_cvtsi128_si32(T3) + b;
        b  = (t3 >> 26);
        t3 &= 0x3ffffff;
        t4 = _mm_cvtsi128_si32(T4) + b;

        /* everything except t4 is in range, so this is all safe */
        h0 = (((uint64_t) t0) | ((uint64_t) t1 << 26)) & 0xfffffffffffull;
        h1 = (((uint64_t) t1 >> 18) | ((uint64_t) t2 << 8) |
              ((uint64_t) t3 << 34)) &
             0xfffffffffffull;
        h2 = (((uint64_t) t3 >> 10) | ((uint64_t) t4 << 16));

        c = (h2 >> 42);
        h2 &= 0x3ffffffffff;
        h0 += c * 5;
        c = (h0 >> 44);
        h0 &= 0xfffffffffff;
        h1 += c;
        c = (h1 >> 44);
        h1 &= 0xfffffffffff;
        h2 += c;
        c = (h2 >> 42);
        h2 &= 0x3ffffffffff;
        h0 += c * 5;
        c = (h0 >> 44);
        h0 &= 0xfffffffffff;
        h1 += c;

        g0 = h0 + 5;
        c  = (g0 >> 44);
        g0 &= 0xfffffffffff;
        g1 = h1 + c;
        c  = (g1 >> 44);
        g1 &= 0xfffffffffff;
        g2 = h2 + c - ((uint64_t) 1 << 42);

        c  = (g2 >> 63) - 1;
        nc = ~c;
        h0 = (h0 & nc) | (g0 & c);
        h1 = (h1 & nc) | (g1 & c);
        h2 = (h2 & nc) | (g2 & c);

        st->H.h[0] = h0;
        st->H.h[1] = h1;
        st->H.h[2] = h2;
    }
}

static void
poly1305_update(poly1305_state_internal_t *st, const unsigned char *m,
                unsigned long long bytes)
{
    unsigned long long i;

    /* handle leftover */
    if (st->leftover) {
        unsigned long long want = (poly1305_block_size - st->leftover);

        if (want > bytes) {
            want = bytes;
        }
        for (i = 0; i < want; i++) {
            st->buffer[st->leftover + i] = m[i];
        }
        bytes -= want;
        m += want;
        st->leftover += want;
        if (st->leftover < poly1305_block_size) {
            return;
        }
        poly1305_blocks(st, st->buffer, poly1305_block_size);
        st->leftover = 0;
    }

    /* process full blocks */
    if (bytes >= poly1305_block_size) {
        unsigned long long want = (bytes & ~(poly1305_block_size - 1));

        poly1305_blocks(st, m, want);
        m += want;
        bytes -= want;
    }

    /* store leftover */
    if (bytes) {
        for (i = 0; i < bytes; i++) {
            st->buffer[st->leftover + i] = m[i];
        }
        st->leftover += bytes;
    }
}

static POLY1305_NOINLINE void
poly1305_finish_ext(poly1305_state_internal_t *st, const unsigned char *m,
                    unsigned long long leftover, unsigned char mac[16])
{
    uint64_t h0, h1, h2;

    if (leftover) {
        CRYPTO_ALIGN(16) unsigned char final[32] = { 0 };

        poly1305_block_copy31(final, m, leftover);
        if (leftover != 16) {
            final[leftover] = 1;
        }
        st->flags |=
            (leftover >= 16) ? poly1305_final_shift8 : poly1305_final_shift16;
        poly1305_blocks(st, final, 32);
    }

    if (st->flags & poly1305_started) {
        /* finalize, H *= [r^2,r], or H *= [r,1] */
        if (!leftover || (leftover > 16)) {
            st->flags |= poly1305_final_r2_r;
        } else {
            st->flags |= poly1305_final_r_1;
        }
        poly1305_blocks(st, NULL, 32);
    }

    h0 = st->H.h[0];
    h1 = st->H.h[1];
    h2 = st->H.h[2];

    /* pad */
    h0 = ((h0) | (h1 << 44));
    h1 = ((h1 >> 20) | (h2 << 24));
#ifdef HAVE_AMD64_ASM
    __asm__ __volatile__(
        "addq %2, %0 ;\n"
        "adcq %3, %1 ;\n"
        : "+r"(h0), "+r"(h1)
        : "r"(st->pad[0]), "r"(st->pad[1])
        : "flags", "cc");
#else
    {
        uint128_t h;

        memcpy(&h, &st->pad[0], 16);
        h += ((uint128_t) h1 << 64) | h0;
        h0 = (uint64_t) h;
        h1 = (uint64_t)(h >> 64);
    }
#endif
    _mm_storeu_si128((xmmi *) (void *) st + 0, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 1, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 2, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 3, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 4, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 5, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 6, _mm_setzero_si128());
    _mm_storeu_si128((xmmi *) (void *) st + 7, _mm_setzero_si128());

    memcpy(&mac[0], &h0, 8);
    memcpy(&mac[8], &h1, 8);

    sodium_memzero((void *) st, sizeof *st);
}

static void
poly1305_finish(poly1305_state_internal_t *st, unsigned char mac[16])
{
    poly1305_finish_ext(st, st->buffer, st->leftover, mac);
}

static int
crypto_onetimeauth_poly1305_sse2_init(crypto_onetimeauth_poly1305_state *state,
                                      const unsigned char *key)
{
    COMPILER_ASSERT(sizeof(crypto_onetimeauth_poly1305_state) >=
                    sizeof(poly1305_state_internal_t));
    poly1305_init_ext((poly1305_state_internal_t *) (void *) state, key, 0U);

    return 0;
}

static int
crypto_onetimeauth_poly1305_sse2_update(
    crypto_onetimeauth_poly1305_state *state, const unsigned char *in,
    unsigned long long inlen)
{
    poly1305_update((poly1305_state_internal_t *) (void *) state, in, inlen);

    return 0;
}

static int
crypto_onetimeauth_poly1305_sse2_final(crypto_onetimeauth_poly1305_state *state,
                                       unsigned char *out)
{
    poly1305_finish((poly1305_state_internal_t *) (void *) state, out);

    return 0;
}

static int
crypto_onetimeauth_poly1305_sse2(unsigned char *out, const unsigned char *m,
                                 unsigned long long   inlen,
                                 const unsigned char *key)
{
    CRYPTO_ALIGN(64) poly1305_state_internal_t st;
    unsigned long long                         blocks;

    poly1305_init_ext(&st, key, inlen);
    blocks = inlen & ~31;
    if (blocks > 0) {
        poly1305_blocks(&st, m, blocks);
        m += blocks;
        inlen -= blocks;
    }
    poly1305_finish_ext(&st, m, inlen, out);

    return 0;
}

static int
crypto_onetimeauth_poly1305_sse2_verify(const unsigned char *h,
                                        const unsigned char *in,
                                        unsigned long long   inlen,
                                        const unsigned char *k)
{
    unsigned char correct[16];

    crypto_onetimeauth_poly1305_sse2(correct, in, inlen, k);

    return crypto_verify_16(h, correct);
}

struct crypto_onetimeauth_poly1305_implementation
    crypto_onetimeauth_poly1305_sse2_implementation = {
        SODIUM_C99(.onetimeauth =) crypto_onetimeauth_poly1305_sse2,
        SODIUM_C99(.onetimeauth_verify =)
            crypto_onetimeauth_poly1305_sse2_verify,
        SODIUM_C99(.onetimeauth_init =) crypto_onetimeauth_poly1305_sse2_init,
        SODIUM_C99(.onetimeauth_update =)
            crypto_onetimeauth_poly1305_sse2_update,
        SODIUM_C99(.onetimeauth_final =) crypto_onetimeauth_poly1305_sse2_final
    };

#endif
