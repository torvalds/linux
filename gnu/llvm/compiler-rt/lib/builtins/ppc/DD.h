#ifndef COMPILERRT_DD_HEADER
#define COMPILERRT_DD_HEADER

#include "../int_lib.h"

typedef union {
  long double ld;
  struct {
    double hi;
    double lo;
  } s;
} DD;

typedef union {
  double d;
  uint64_t x;
} doublebits;

#define LOWORDER(xy, xHi, xLo, yHi, yLo)                                       \
  (((((xHi) * (yHi) - (xy)) + (xHi) * (yLo)) + (xLo) * (yHi)) + (xLo) * (yLo))

static __inline ALWAYS_INLINE double local_fabs(double x) {
  doublebits result = {.d = x};
  result.x &= UINT64_C(0x7fffffffffffffff);
  return result.d;
}

static __inline ALWAYS_INLINE double high26bits(double x) {
  doublebits result = {.d = x};
  result.x &= UINT64_C(0xfffffffff8000000);
  return result.d;
}

static __inline ALWAYS_INLINE int different_sign(double x, double y) {
  doublebits xsignbit = {.d = x}, ysignbit = {.d = y};
  int result = (int)(xsignbit.x >> 63) ^ (int)(ysignbit.x >> 63);
  return result;
}

long double __gcc_qadd(long double, long double);
long double __gcc_qsub(long double, long double);
long double __gcc_qmul(long double, long double);
long double __gcc_qdiv(long double, long double);

#endif // COMPILERRT_DD_HEADER
