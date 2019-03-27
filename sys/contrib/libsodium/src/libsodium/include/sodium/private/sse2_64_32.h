#ifndef sse2_64_32_H
#define sse2_64_32_H 1

#include "common.h"

#ifdef HAVE_INTRIN_H
# include <intrin.h>
#endif

#if defined(HAVE_EMMINTRIN_H) && \
    !(defined(__amd64) || defined(__amd64__) || defined(__x86_64__) || \
      defined(_M_X64) || defined(_M_AMD64))

# include <emmintrin.h>
# include <stdint.h>

# ifndef _mm_set_epi64x
#  define _mm_set_epi64x(Q0, Q1) sodium__mm_set_epi64x((Q0), (Q1))
static inline __m128i
sodium__mm_set_epi64x(int64_t q1, int64_t q0)
{
    union { int64_t as64; int32_t as32[2]; } x0, x1;
    x0.as64 = q0; x1.as64 = q1;
    return _mm_set_epi32(x1.as32[1], x1.as32[0], x0.as32[1], x0.as32[0]);
}
# endif

# ifndef _mm_set1_epi64x
#  define _mm_set1_epi64x(Q) sodium__mm_set1_epi64x(Q)
static inline __m128i
sodium__mm_set1_epi64x(int64_t q)
{
    return _mm_set_epi64x(q, q);
}
# endif

# ifndef _mm_cvtsi64_si128
#  define _mm_cvtsi64_si128(Q) sodium__mm_cvtsi64_si128(Q)
static inline __m128i
sodium__mm_cvtsi64_si128(int64_t q)
{
    union { int64_t as64; int32_t as32[2]; } x;
    x.as64 = q;
    return _mm_setr_epi32(x.as32[0], x.as32[1], 0, 0);
}
# endif

#endif

#endif
