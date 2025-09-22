/*===---- vecintrin.h - Vector intrinsics ----------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if defined(__s390x__) && defined(__VEC__)

#define __ATTRS_ai __attribute__((__always_inline__))
#define __ATTRS_o __attribute__((__overloadable__))
#define __ATTRS_o_ai __attribute__((__overloadable__, __always_inline__))

#define __constant(PARM) \
  __attribute__((__enable_if__ ((PARM) == (PARM), \
     "argument must be a constant integer")))
#define __constant_range(PARM, LOW, HIGH) \
  __attribute__((__enable_if__ ((PARM) >= (LOW) && (PARM) <= (HIGH), \
     "argument must be a constant integer from " #LOW " to " #HIGH)))
#define __constant_pow2_range(PARM, LOW, HIGH) \
  __attribute__((__enable_if__ ((PARM) >= (LOW) && (PARM) <= (HIGH) && \
                                ((PARM) & ((PARM) - 1)) == 0, \
     "argument must be a constant power of 2 from " #LOW " to " #HIGH)))

/*-- __lcbb -----------------------------------------------------------------*/

extern __ATTRS_o unsigned int
__lcbb(const void *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#define __lcbb(X, Y) ((__typeof__((__lcbb)((X), (Y)))) \
  __builtin_s390_lcbb((X), __builtin_constant_p((Y))? \
                           ((Y) == 64 ? 0 : \
                            (Y) == 128 ? 1 : \
                            (Y) == 256 ? 2 : \
                            (Y) == 512 ? 3 : \
                            (Y) == 1024 ? 4 : \
                            (Y) == 2048 ? 5 : \
                            (Y) == 4096 ? 6 : 0) : 0))

/*-- vec_extract ------------------------------------------------------------*/

static inline __ATTRS_o_ai signed char
vec_extract(__vector signed char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai unsigned char
vec_extract(__vector __bool char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai unsigned char
vec_extract(__vector unsigned char __vec, int __index) {
  return __vec[__index & 15];
}

static inline __ATTRS_o_ai signed short
vec_extract(__vector signed short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai unsigned short
vec_extract(__vector __bool short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai unsigned short
vec_extract(__vector unsigned short __vec, int __index) {
  return __vec[__index & 7];
}

static inline __ATTRS_o_ai signed int
vec_extract(__vector signed int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai unsigned int
vec_extract(__vector __bool int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai unsigned int
vec_extract(__vector unsigned int __vec, int __index) {
  return __vec[__index & 3];
}

static inline __ATTRS_o_ai signed long long
vec_extract(__vector signed long long __vec, int __index) {
  return __vec[__index & 1];
}

static inline __ATTRS_o_ai unsigned long long
vec_extract(__vector __bool long long __vec, int __index) {
  return __vec[__index & 1];
}

static inline __ATTRS_o_ai unsigned long long
vec_extract(__vector unsigned long long __vec, int __index) {
  return __vec[__index & 1];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai float
vec_extract(__vector float __vec, int __index) {
  return __vec[__index & 3];
}
#endif

static inline __ATTRS_o_ai double
vec_extract(__vector double __vec, int __index) {
  return __vec[__index & 1];
}

/*-- vec_insert -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_insert(signed char __scalar, __vector signed char __vec, int __index) {
  __vec[__index & 15] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_insert(unsigned char __scalar, __vector __bool char __vec, int __index) {
  __vector unsigned char __newvec = (__vector unsigned char)__vec;
  __newvec[__index & 15] = (unsigned char)__scalar;
  return __newvec;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_insert(unsigned char __scalar, __vector unsigned char __vec, int __index) {
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed short
vec_insert(signed short __scalar, __vector signed short __vec, int __index) {
  __vec[__index & 7] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_insert(unsigned short __scalar, __vector __bool short __vec,
           int __index) {
  __vector unsigned short __newvec = (__vector unsigned short)__vec;
  __newvec[__index & 7] = (unsigned short)__scalar;
  return __newvec;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_insert(unsigned short __scalar, __vector unsigned short __vec,
           int __index) {
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed int
vec_insert(signed int __scalar, __vector signed int __vec, int __index) {
  __vec[__index & 3] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_insert(unsigned int __scalar, __vector __bool int __vec, int __index) {
  __vector unsigned int __newvec = (__vector unsigned int)__vec;
  __newvec[__index & 3] = __scalar;
  return __newvec;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_insert(unsigned int __scalar, __vector unsigned int __vec, int __index) {
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed long long
vec_insert(signed long long __scalar, __vector signed long long __vec,
           int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_insert(unsigned long long __scalar, __vector __bool long long __vec,
           int __index) {
  __vector unsigned long long __newvec = (__vector unsigned long long)__vec;
  __newvec[__index & 1] = __scalar;
  return __newvec;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_insert(unsigned long long __scalar, __vector unsigned long long __vec,
           int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_insert(float __scalar, __vector float __vec, int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_insert(double __scalar, __vector double __vec, int __index) {
  __vec[__index & 1] = __scalar;
  return __vec;
}

/*-- vec_promote ------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_promote(signed char __scalar, int __index) {
  const __vector signed char __zero = (__vector signed char)0;
  __vector signed char __vec = __builtin_shufflevector(__zero, __zero,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_promote(unsigned char __scalar, int __index) {
  const __vector unsigned char __zero = (__vector unsigned char)0;
  __vector unsigned char __vec = __builtin_shufflevector(__zero, __zero,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 15] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed short
vec_promote(signed short __scalar, int __index) {
  const __vector signed short __zero = (__vector signed short)0;
  __vector signed short __vec = __builtin_shufflevector(__zero, __zero,
                                -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_promote(unsigned short __scalar, int __index) {
  const __vector unsigned short __zero = (__vector unsigned short)0;
  __vector unsigned short __vec = __builtin_shufflevector(__zero, __zero,
                                  -1, -1, -1, -1, -1, -1, -1, -1);
  __vec[__index & 7] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed int
vec_promote(signed int __scalar, int __index) {
  const __vector signed int __zero = (__vector signed int)0;
  __vector signed int __vec = __builtin_shufflevector(__zero, __zero,
                                                      -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_promote(unsigned int __scalar, int __index) {
  const __vector unsigned int __zero = (__vector unsigned int)0;
  __vector unsigned int __vec = __builtin_shufflevector(__zero, __zero,
                                                        -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed long long
vec_promote(signed long long __scalar, int __index) {
  const __vector signed long long __zero = (__vector signed long long)0;
  __vector signed long long __vec = __builtin_shufflevector(__zero, __zero,
                                                            -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_promote(unsigned long long __scalar, int __index) {
  const __vector unsigned long long __zero = (__vector unsigned long long)0;
  __vector unsigned long long __vec = __builtin_shufflevector(__zero, __zero,
                                                              -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_promote(float __scalar, int __index) {
  const __vector float __zero = (__vector float)0.0f;
  __vector float __vec = __builtin_shufflevector(__zero, __zero,
                                                 -1, -1, -1, -1);
  __vec[__index & 3] = __scalar;
  return __vec;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_promote(double __scalar, int __index) {
  const __vector double __zero = (__vector double)0.0;
  __vector double __vec = __builtin_shufflevector(__zero, __zero, -1, -1);
  __vec[__index & 1] = __scalar;
  return __vec;
}

/*-- vec_insert_and_zero ----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_insert_and_zero(const signed char *__ptr) {
  __vector signed char __vec = (__vector signed char)0;
  __vec[7] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_insert_and_zero(const unsigned char *__ptr) {
  __vector unsigned char __vec = (__vector unsigned char)0;
  __vec[7] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed short
vec_insert_and_zero(const signed short *__ptr) {
  __vector signed short __vec = (__vector signed short)0;
  __vec[3] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_insert_and_zero(const unsigned short *__ptr) {
  __vector unsigned short __vec = (__vector unsigned short)0;
  __vec[3] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed int
vec_insert_and_zero(const signed int *__ptr) {
  __vector signed int __vec = (__vector signed int)0;
  __vec[1] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_insert_and_zero(const unsigned int *__ptr) {
  __vector unsigned int __vec = (__vector unsigned int)0;
  __vec[1] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector signed long long
vec_insert_and_zero(const signed long long *__ptr) {
  __vector signed long long __vec = (__vector signed long long)0;
  __vec[0] = *__ptr;
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_insert_and_zero(const unsigned long long *__ptr) {
  __vector unsigned long long __vec = (__vector unsigned long long)0;
  __vec[0] = *__ptr;
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_insert_and_zero(const float *__ptr) {
  __vector float __vec = (__vector float)0.0f;
  __vec[1] = *__ptr;
  return __vec;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_insert_and_zero(const double *__ptr) {
  __vector double __vec = (__vector double)0.0;
  __vec[0] = *__ptr;
  return __vec;
}

/*-- vec_perm ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_perm(__vector signed char __a, __vector signed char __b,
         __vector unsigned char __c) {
  return (__vector signed char)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_perm(__vector unsigned char __a, __vector unsigned char __b,
         __vector unsigned char __c) {
  return (__vector unsigned char)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector __bool char
vec_perm(__vector __bool char __a, __vector __bool char __b,
         __vector unsigned char __c) {
  return (__vector __bool char)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector signed short
vec_perm(__vector signed short __a, __vector signed short __b,
         __vector unsigned char __c) {
  return (__vector signed short)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_perm(__vector unsigned short __a, __vector unsigned short __b,
         __vector unsigned char __c) {
  return (__vector unsigned short)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector __bool short
vec_perm(__vector __bool short __a, __vector __bool short __b,
         __vector unsigned char __c) {
  return (__vector __bool short)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector signed int
vec_perm(__vector signed int __a, __vector signed int __b,
         __vector unsigned char __c) {
  return (__vector signed int)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_perm(__vector unsigned int __a, __vector unsigned int __b,
         __vector unsigned char __c) {
  return (__vector unsigned int)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector __bool int
vec_perm(__vector __bool int __a, __vector __bool int __b,
         __vector unsigned char __c) {
  return (__vector __bool int)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector signed long long
vec_perm(__vector signed long long __a, __vector signed long long __b,
         __vector unsigned char __c) {
  return (__vector signed long long)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_perm(__vector unsigned long long __a, __vector unsigned long long __b,
         __vector unsigned char __c) {
  return (__vector unsigned long long)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_perm(__vector __bool long long __a, __vector __bool long long __b,
         __vector unsigned char __c) {
  return (__vector __bool long long)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_perm(__vector float __a, __vector float __b,
         __vector unsigned char __c) {
  return (__vector float)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_perm(__vector double __a, __vector double __b,
         __vector unsigned char __c) {
  return (__vector double)__builtin_s390_vperm(
           (__vector unsigned char)__a, (__vector unsigned char)__b, __c);
}

/*-- vec_permi --------------------------------------------------------------*/

// This prototype is deprecated.
extern __ATTRS_o __vector signed long long
vec_permi(__vector signed long long __a, __vector signed long long __b,
          int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o __vector unsigned long long
vec_permi(__vector unsigned long long __a, __vector unsigned long long __b,
          int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o __vector __bool long long
vec_permi(__vector __bool long long __a, __vector __bool long long __b,
          int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o __vector double
vec_permi(__vector double __a, __vector double __b, int __c)
  __constant_range(__c, 0, 3);

#define vec_permi(X, Y, Z) ((__typeof__((vec_permi)((X), (Y), (Z)))) \
  __builtin_s390_vpdi((__vector unsigned long long)(X), \
                      (__vector unsigned long long)(Y), \
                      (((Z) & 2) << 1) | ((Z) & 1)))

/*-- vec_bperm_u128 ---------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai __vector unsigned long long
vec_bperm_u128(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vbperm(__a, __b);
}
#endif

/*-- vec_revb ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_revb(__vector signed short __vec) {
  return (__vector signed short)
         __builtin_s390_vlbrh((__vector unsigned short)__vec);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_revb(__vector unsigned short __vec) {
  return __builtin_s390_vlbrh(__vec);
}

static inline __ATTRS_o_ai __vector signed int
vec_revb(__vector signed int __vec) {
  return (__vector signed int)
         __builtin_s390_vlbrf((__vector unsigned int)__vec);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_revb(__vector unsigned int __vec) {
  return __builtin_s390_vlbrf(__vec);
}

static inline __ATTRS_o_ai __vector signed long long
vec_revb(__vector signed long long __vec) {
  return (__vector signed long long)
         __builtin_s390_vlbrg((__vector unsigned long long)__vec);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_revb(__vector unsigned long long __vec) {
  return __builtin_s390_vlbrg(__vec);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_revb(__vector float __vec) {
  return (__vector float)
         __builtin_s390_vlbrf((__vector unsigned int)__vec);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_revb(__vector double __vec) {
  return (__vector double)
         __builtin_s390_vlbrg((__vector unsigned long long)__vec);
}

/*-- vec_reve ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_reve(__vector signed char __vec) {
  return (__vector signed char) { __vec[15], __vec[14], __vec[13], __vec[12],
                                  __vec[11], __vec[10], __vec[9], __vec[8],
                                  __vec[7], __vec[6], __vec[5], __vec[4],
                                  __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector unsigned char
vec_reve(__vector unsigned char __vec) {
  return (__vector unsigned char) { __vec[15], __vec[14], __vec[13], __vec[12],
                                    __vec[11], __vec[10], __vec[9], __vec[8],
                                    __vec[7], __vec[6], __vec[5], __vec[4],
                                    __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector __bool char
vec_reve(__vector __bool char __vec) {
  return (__vector __bool char) { __vec[15], __vec[14], __vec[13], __vec[12],
                                  __vec[11], __vec[10], __vec[9], __vec[8],
                                  __vec[7], __vec[6], __vec[5], __vec[4],
                                  __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector signed short
vec_reve(__vector signed short __vec) {
  return (__vector signed short) { __vec[7], __vec[6], __vec[5], __vec[4],
                                   __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector unsigned short
vec_reve(__vector unsigned short __vec) {
  return (__vector unsigned short) { __vec[7], __vec[6], __vec[5], __vec[4],
                                     __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector __bool short
vec_reve(__vector __bool short __vec) {
  return (__vector __bool short) { __vec[7], __vec[6], __vec[5], __vec[4],
                                   __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector signed int
vec_reve(__vector signed int __vec) {
  return (__vector signed int) { __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector unsigned int
vec_reve(__vector unsigned int __vec) {
  return (__vector unsigned int) { __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector __bool int
vec_reve(__vector __bool int __vec) {
  return (__vector __bool int) { __vec[3], __vec[2], __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector signed long long
vec_reve(__vector signed long long __vec) {
  return (__vector signed long long) { __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_reve(__vector unsigned long long __vec) {
  return (__vector unsigned long long) { __vec[1], __vec[0] };
}

static inline __ATTRS_o_ai __vector __bool long long
vec_reve(__vector __bool long long __vec) {
  return (__vector __bool long long) { __vec[1], __vec[0] };
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_reve(__vector float __vec) {
  return (__vector float) { __vec[3], __vec[2], __vec[1], __vec[0] };
}
#endif

static inline __ATTRS_o_ai __vector double
vec_reve(__vector double __vec) {
  return (__vector double) { __vec[1], __vec[0] };
}

/*-- vec_sel ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_sel(__vector signed char __a, __vector signed char __b,
        __vector unsigned char __c) {
  return (((__vector signed char)__c & __b) |
          (~(__vector signed char)__c & __a));
}

static inline __ATTRS_o_ai __vector signed char
vec_sel(__vector signed char __a, __vector signed char __b,
        __vector __bool char __c) {
  return (((__vector signed char)__c & __b) |
          (~(__vector signed char)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool char
vec_sel(__vector __bool char __a, __vector __bool char __b,
        __vector unsigned char __c) {
  return (((__vector __bool char)__c & __b) |
          (~(__vector __bool char)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool char
vec_sel(__vector __bool char __a, __vector __bool char __b,
        __vector __bool char __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_sel(__vector unsigned char __a, __vector unsigned char __b,
        __vector unsigned char __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_sel(__vector unsigned char __a, __vector unsigned char __b,
        __vector __bool char __c) {
  return (((__vector unsigned char)__c & __b) |
          (~(__vector unsigned char)__c & __a));
}

static inline __ATTRS_o_ai __vector signed short
vec_sel(__vector signed short __a, __vector signed short __b,
        __vector unsigned short __c) {
  return (((__vector signed short)__c & __b) |
          (~(__vector signed short)__c & __a));
}

static inline __ATTRS_o_ai __vector signed short
vec_sel(__vector signed short __a, __vector signed short __b,
        __vector __bool short __c) {
  return (((__vector signed short)__c & __b) |
          (~(__vector signed short)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool short
vec_sel(__vector __bool short __a, __vector __bool short __b,
        __vector unsigned short __c) {
  return (((__vector __bool short)__c & __b) |
          (~(__vector __bool short)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool short
vec_sel(__vector __bool short __a, __vector __bool short __b,
        __vector __bool short __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_sel(__vector unsigned short __a, __vector unsigned short __b,
        __vector unsigned short __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_sel(__vector unsigned short __a, __vector unsigned short __b,
        __vector __bool short __c) {
  return (((__vector unsigned short)__c & __b) |
          (~(__vector unsigned short)__c & __a));
}

static inline __ATTRS_o_ai __vector signed int
vec_sel(__vector signed int __a, __vector signed int __b,
        __vector unsigned int __c) {
  return (((__vector signed int)__c & __b) |
          (~(__vector signed int)__c & __a));
}

static inline __ATTRS_o_ai __vector signed int
vec_sel(__vector signed int __a, __vector signed int __b,
        __vector __bool int __c) {
  return (((__vector signed int)__c & __b) |
          (~(__vector signed int)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool int
vec_sel(__vector __bool int __a, __vector __bool int __b,
        __vector unsigned int __c) {
  return (((__vector __bool int)__c & __b) |
          (~(__vector __bool int)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool int
vec_sel(__vector __bool int __a, __vector __bool int __b,
        __vector __bool int __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_sel(__vector unsigned int __a, __vector unsigned int __b,
        __vector unsigned int __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_sel(__vector unsigned int __a, __vector unsigned int __b,
        __vector __bool int __c) {
  return (((__vector unsigned int)__c & __b) |
          (~(__vector unsigned int)__c & __a));
}

static inline __ATTRS_o_ai __vector signed long long
vec_sel(__vector signed long long __a, __vector signed long long __b,
        __vector unsigned long long __c) {
  return (((__vector signed long long)__c & __b) |
          (~(__vector signed long long)__c & __a));
}

static inline __ATTRS_o_ai __vector signed long long
vec_sel(__vector signed long long __a, __vector signed long long __b,
        __vector __bool long long __c) {
  return (((__vector signed long long)__c & __b) |
          (~(__vector signed long long)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool long long
vec_sel(__vector __bool long long __a, __vector __bool long long __b,
        __vector unsigned long long __c) {
  return (((__vector __bool long long)__c & __b) |
          (~(__vector __bool long long)__c & __a));
}

static inline __ATTRS_o_ai __vector __bool long long
vec_sel(__vector __bool long long __a, __vector __bool long long __b,
        __vector __bool long long __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_sel(__vector unsigned long long __a, __vector unsigned long long __b,
        __vector unsigned long long __c) {
  return (__c & __b) | (~__c & __a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_sel(__vector unsigned long long __a, __vector unsigned long long __b,
        __vector __bool long long __c) {
  return (((__vector unsigned long long)__c & __b) |
          (~(__vector unsigned long long)__c & __a));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_sel(__vector float __a, __vector float __b, __vector unsigned int __c) {
  return (__vector float)((__c & (__vector unsigned int)__b) |
                          (~__c & (__vector unsigned int)__a));
}

static inline __ATTRS_o_ai __vector float
vec_sel(__vector float __a, __vector float __b, __vector __bool int __c) {
  __vector unsigned int __ac = (__vector unsigned int)__a;
  __vector unsigned int __bc = (__vector unsigned int)__b;
  __vector unsigned int __cc = (__vector unsigned int)__c;
  return (__vector float)((__cc & __bc) | (~__cc & __ac));
}
#endif

static inline __ATTRS_o_ai __vector double
vec_sel(__vector double __a, __vector double __b,
        __vector unsigned long long __c) {
  return (__vector double)((__c & (__vector unsigned long long)__b) |
                         (~__c & (__vector unsigned long long)__a));
}

static inline __ATTRS_o_ai __vector double
vec_sel(__vector double __a, __vector double __b,
        __vector __bool long long __c) {
  __vector unsigned long long __ac = (__vector unsigned long long)__a;
  __vector unsigned long long __bc = (__vector unsigned long long)__b;
  __vector unsigned long long __cc = (__vector unsigned long long)__c;
  return (__vector double)((__cc & __bc) | (~__cc & __ac));
}

/*-- vec_gather_element -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed int
vec_gather_element(__vector signed int __vec,
                   __vector unsigned int __offset,
                   const signed int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const signed int *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai __vector __bool int
vec_gather_element(__vector __bool int __vec,
                   __vector unsigned int __offset,
                   const unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const unsigned int *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_gather_element(__vector unsigned int __vec,
                   __vector unsigned int __offset,
                   const unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const unsigned int *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai __vector signed long long
vec_gather_element(__vector signed long long __vec,
                   __vector unsigned long long __offset,
                   const signed long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const signed long long *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai __vector __bool long long
vec_gather_element(__vector __bool long long __vec,
                   __vector unsigned long long __offset,
                   const unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const unsigned long long *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_gather_element(__vector unsigned long long __vec,
                   __vector unsigned long long __offset,
                   const unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const unsigned long long *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_gather_element(__vector float __vec,
                   __vector unsigned int __offset,
                   const float *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  __vec[__index] = *(const float *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_gather_element(__vector double __vec,
                   __vector unsigned long long __offset,
                   const double *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  __vec[__index] = *(const double *)(
    (const char *)__ptr + __offset[__index]);
  return __vec;
}

/*-- vec_scatter_element ----------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_scatter_element(__vector signed int __vec,
                    __vector unsigned int __offset,
                    signed int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(signed int *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(__vector __bool int __vec,
                    __vector unsigned int __offset,
                    unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(unsigned int *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(__vector unsigned int __vec,
                    __vector unsigned int __offset,
                    unsigned int *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(unsigned int *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(__vector signed long long __vec,
                    __vector unsigned long long __offset,
                    signed long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(signed long long *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(__vector __bool long long __vec,
                    __vector unsigned long long __offset,
                    unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(unsigned long long *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

static inline __ATTRS_o_ai void
vec_scatter_element(__vector unsigned long long __vec,
                    __vector unsigned long long __offset,
                    unsigned long long *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(unsigned long long *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_scatter_element(__vector float __vec,
                    __vector unsigned int __offset,
                    float *__ptr, int __index)
  __constant_range(__index, 0, 3) {
  *(float *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}
#endif

static inline __ATTRS_o_ai void
vec_scatter_element(__vector double __vec,
                    __vector unsigned long long __offset,
                    double *__ptr, int __index)
  __constant_range(__index, 0, 1) {
  *(double *)((char *)__ptr + __offset[__index]) =
    __vec[__index];
}

/*-- vec_xl -----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_xl(long __offset, const signed char *__ptr) {
  __vector signed char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed char));
  return V;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_xl(long __offset, const unsigned char *__ptr) {
  __vector unsigned char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned char));
  return V;
}

static inline __ATTRS_o_ai __vector signed short
vec_xl(long __offset, const signed short *__ptr) {
  __vector signed short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed short));
  return V;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_xl(long __offset, const unsigned short *__ptr) {
  __vector unsigned short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned short));
  return V;
}

static inline __ATTRS_o_ai __vector signed int
vec_xl(long __offset, const signed int *__ptr) {
  __vector signed int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed int));
  return V;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_xl(long __offset, const unsigned int *__ptr) {
  __vector unsigned int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned int));
  return V;
}

static inline __ATTRS_o_ai __vector signed long long
vec_xl(long __offset, const signed long long *__ptr) {
  __vector signed long long V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed long long));
  return V;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_xl(long __offset, const unsigned long long *__ptr) {
  __vector unsigned long long V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned long long));
  return V;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_xl(long __offset, const float *__ptr) {
  __vector float V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector float));
  return V;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_xl(long __offset, const double *__ptr) {
  __vector double V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector double));
  return V;
}

/*-- vec_xld2 ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_xld2(long __offset, const signed char *__ptr) {
  __vector signed char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed char));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_xld2(long __offset, const unsigned char *__ptr) {
  __vector unsigned char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned char));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_xld2(long __offset, const signed short *__ptr) {
  __vector signed short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed short));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_xld2(long __offset, const unsigned short *__ptr) {
  __vector unsigned short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned short));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_xld2(long __offset, const signed int *__ptr) {
  __vector signed int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed int));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_xld2(long __offset, const unsigned int *__ptr) {
  __vector unsigned int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned int));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_xld2(long __offset, const signed long long *__ptr) {
  __vector signed long long V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed long long));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_xld2(long __offset, const unsigned long long *__ptr) {
  __vector unsigned long long V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned long long));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_xld2(long __offset, const double *__ptr) {
  __vector double V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector double));
  return V;
}

/*-- vec_xlw4 ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_xlw4(long __offset, const signed char *__ptr) {
  __vector signed char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed char));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_xlw4(long __offset, const unsigned char *__ptr) {
  __vector unsigned char V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned char));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_xlw4(long __offset, const signed short *__ptr) {
  __vector signed short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed short));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_xlw4(long __offset, const unsigned short *__ptr) {
  __vector unsigned short V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned short));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_xlw4(long __offset, const signed int *__ptr) {
  __vector signed int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector signed int));
  return V;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_xlw4(long __offset, const unsigned int *__ptr) {
  __vector unsigned int V;
  __builtin_memcpy(&V, ((const char *)__ptr + __offset),
                   sizeof(__vector unsigned int));
  return V;
}

/*-- vec_xst ----------------------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_xst(__vector signed char __vec, long __offset, signed char *__ptr) {
  __vector signed char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed char));
}

static inline __ATTRS_o_ai void
vec_xst(__vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  __vector unsigned char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned char));
}

static inline __ATTRS_o_ai void
vec_xst(__vector signed short __vec, long __offset, signed short *__ptr) {
  __vector signed short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed short));
}

static inline __ATTRS_o_ai void
vec_xst(__vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  __vector unsigned short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned short));
}

static inline __ATTRS_o_ai void
vec_xst(__vector signed int __vec, long __offset, signed int *__ptr) {
  __vector signed int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector signed int));
}

static inline __ATTRS_o_ai void
vec_xst(__vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  __vector unsigned int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned int));
}

static inline __ATTRS_o_ai void
vec_xst(__vector signed long long __vec, long __offset,
        signed long long *__ptr) {
  __vector signed long long V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed long long));
}

static inline __ATTRS_o_ai void
vec_xst(__vector unsigned long long __vec, long __offset,
        unsigned long long *__ptr) {
  __vector unsigned long long V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned long long));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_xst(__vector float __vec, long __offset, float *__ptr) {
  __vector float V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector float));
}
#endif

static inline __ATTRS_o_ai void
vec_xst(__vector double __vec, long __offset, double *__ptr) {
  __vector double V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector double));
}

/*-- vec_xstd2 --------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector signed char __vec, long __offset, signed char *__ptr) {
  __vector signed char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed char));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  __vector unsigned char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned char));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector signed short __vec, long __offset, signed short *__ptr) {
  __vector signed short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed short));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  __vector unsigned short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned short));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector signed int __vec, long __offset, signed int *__ptr) {
  __vector signed int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector signed int));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  __vector unsigned int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned int));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector signed long long __vec, long __offset,
          signed long long *__ptr) {
  __vector signed long long V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed long long));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector unsigned long long __vec, long __offset,
          unsigned long long *__ptr) {
  __vector unsigned long long V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned long long));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstd2(__vector double __vec, long __offset, double *__ptr) {
  __vector double V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector double));
}

/*-- vec_xstw4 --------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector signed char __vec, long __offset, signed char *__ptr) {
  __vector signed char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed char));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector unsigned char __vec, long __offset, unsigned char *__ptr) {
  __vector unsigned char V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned char));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector signed short __vec, long __offset, signed short *__ptr) {
  __vector signed short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector signed short));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector unsigned short __vec, long __offset, unsigned short *__ptr) {
  __vector unsigned short V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned short));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector signed int __vec, long __offset, signed int *__ptr) {
  __vector signed int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V, sizeof(__vector signed int));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai void
vec_xstw4(__vector unsigned int __vec, long __offset, unsigned int *__ptr) {
  __vector unsigned int V = __vec;
  __builtin_memcpy(((char *)__ptr + __offset), &V,
                   sizeof(__vector unsigned int));
}

/*-- vec_load_bndry ---------------------------------------------------------*/

extern __ATTRS_o __vector signed char
vec_load_bndry(const signed char *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector unsigned char
vec_load_bndry(const unsigned char *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector signed short
vec_load_bndry(const signed short *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector unsigned short
vec_load_bndry(const unsigned short *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector signed int
vec_load_bndry(const signed int *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector unsigned int
vec_load_bndry(const unsigned int *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector signed long long
vec_load_bndry(const signed long long *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

extern __ATTRS_o __vector unsigned long long
vec_load_bndry(const unsigned long long *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#if __ARCH__ >= 12
extern __ATTRS_o __vector float
vec_load_bndry(const float *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);
#endif

extern __ATTRS_o __vector double
vec_load_bndry(const double *__ptr, unsigned short __len)
  __constant_pow2_range(__len, 64, 4096);

#define vec_load_bndry(X, Y) ((__typeof__((vec_load_bndry)((X), (Y)))) \
  __builtin_s390_vlbb((X), ((Y) == 64 ? 0 : \
                            (Y) == 128 ? 1 : \
                            (Y) == 256 ? 2 : \
                            (Y) == 512 ? 3 : \
                            (Y) == 1024 ? 4 : \
                            (Y) == 2048 ? 5 : \
                            (Y) == 4096 ? 6 : -1)))

/*-- vec_load_len -----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_load_len(const signed char *__ptr, unsigned int __len) {
  return (__vector signed char)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_load_len(const unsigned char *__ptr, unsigned int __len) {
  return (__vector unsigned char)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector signed short
vec_load_len(const signed short *__ptr, unsigned int __len) {
  return (__vector signed short)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_load_len(const unsigned short *__ptr, unsigned int __len) {
  return (__vector unsigned short)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector signed int
vec_load_len(const signed int *__ptr, unsigned int __len) {
  return (__vector signed int)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_load_len(const unsigned int *__ptr, unsigned int __len) {
  return (__vector unsigned int)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector signed long long
vec_load_len(const signed long long *__ptr, unsigned int __len) {
  return (__vector signed long long)__builtin_s390_vll(__len, __ptr);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_load_len(const unsigned long long *__ptr, unsigned int __len) {
  return (__vector unsigned long long)__builtin_s390_vll(__len, __ptr);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_load_len(const float *__ptr, unsigned int __len) {
  return (__vector float)__builtin_s390_vll(__len, __ptr);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_load_len(const double *__ptr, unsigned int __len) {
  return (__vector double)__builtin_s390_vll(__len, __ptr);
}

/*-- vec_load_len_r ---------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai __vector unsigned char
vec_load_len_r(const unsigned char *__ptr, unsigned int __len) {
  return (__vector unsigned char)__builtin_s390_vlrlr(__len, __ptr);
}
#endif

/*-- vec_store_len ----------------------------------------------------------*/

static inline __ATTRS_o_ai void
vec_store_len(__vector signed char __vec, signed char *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector unsigned char __vec, unsigned char *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector signed short __vec, signed short *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector unsigned short __vec, unsigned short *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector signed int __vec, signed int *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector unsigned int __vec, unsigned int *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector signed long long __vec, signed long long *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

static inline __ATTRS_o_ai void
vec_store_len(__vector unsigned long long __vec, unsigned long long *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai void
vec_store_len(__vector float __vec, float *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}
#endif

static inline __ATTRS_o_ai void
vec_store_len(__vector double __vec, double *__ptr,
              unsigned int __len) {
  __builtin_s390_vstl((__vector signed char)__vec, __len, __ptr);
}

/*-- vec_store_len_r --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai void
vec_store_len_r(__vector unsigned char __vec, unsigned char *__ptr,
                unsigned int __len) {
  __builtin_s390_vstrlr((__vector signed char)__vec, __len, __ptr);
}
#endif

/*-- vec_load_pair ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed long long
vec_load_pair(signed long long __a, signed long long __b) {
  return (__vector signed long long)(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_load_pair(unsigned long long __a, unsigned long long __b) {
  return (__vector unsigned long long)(__a, __b);
}

/*-- vec_genmask ------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_genmask(unsigned short __mask)
  __constant(__mask) {
  return (__vector unsigned char)(
    __mask & 0x8000 ? 0xff : 0,
    __mask & 0x4000 ? 0xff : 0,
    __mask & 0x2000 ? 0xff : 0,
    __mask & 0x1000 ? 0xff : 0,
    __mask & 0x0800 ? 0xff : 0,
    __mask & 0x0400 ? 0xff : 0,
    __mask & 0x0200 ? 0xff : 0,
    __mask & 0x0100 ? 0xff : 0,
    __mask & 0x0080 ? 0xff : 0,
    __mask & 0x0040 ? 0xff : 0,
    __mask & 0x0020 ? 0xff : 0,
    __mask & 0x0010 ? 0xff : 0,
    __mask & 0x0008 ? 0xff : 0,
    __mask & 0x0004 ? 0xff : 0,
    __mask & 0x0002 ? 0xff : 0,
    __mask & 0x0001 ? 0xff : 0);
}

/*-- vec_genmasks_* ---------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_genmasks_8(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 7;
  unsigned char __bit2 = __last & 7;
  unsigned char __mask1 = (unsigned char)(1U << (7 - __bit1) << 1) - 1;
  unsigned char __mask2 = (unsigned char)(1U << (7 - __bit2)) - 1;
  unsigned char __value = (__bit1 <= __bit2 ?
                           __mask1 & ~__mask2 :
                           __mask1 | ~__mask2);
  return (__vector unsigned char)__value;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_genmasks_16(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 15;
  unsigned char __bit2 = __last & 15;
  unsigned short __mask1 = (unsigned short)(1U << (15 - __bit1) << 1) - 1;
  unsigned short __mask2 = (unsigned short)(1U << (15 - __bit2)) - 1;
  unsigned short __value = (__bit1 <= __bit2 ?
                            __mask1 & ~__mask2 :
                            __mask1 | ~__mask2);
  return (__vector unsigned short)__value;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_genmasks_32(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 31;
  unsigned char __bit2 = __last & 31;
  unsigned int __mask1 = (1U << (31 - __bit1) << 1) - 1;
  unsigned int __mask2 = (1U << (31 - __bit2)) - 1;
  unsigned int __value = (__bit1 <= __bit2 ?
                          __mask1 & ~__mask2 :
                          __mask1 | ~__mask2);
  return (__vector unsigned int)__value;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_genmasks_64(unsigned char __first, unsigned char __last)
  __constant(__first) __constant(__last) {
  unsigned char __bit1 = __first & 63;
  unsigned char __bit2 = __last & 63;
  unsigned long long __mask1 = (1ULL << (63 - __bit1) << 1) - 1;
  unsigned long long __mask2 = (1ULL << (63 - __bit2)) - 1;
  unsigned long long __value = (__bit1 <= __bit2 ?
                                __mask1 & ~__mask2 :
                                __mask1 | ~__mask2);
  return (__vector unsigned long long)__value;
}

/*-- vec_splat --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_splat(__vector signed char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (__vector signed char)__vec[__index];
}

static inline __ATTRS_o_ai __vector __bool char
vec_splat(__vector __bool char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (__vector __bool char)(__vector unsigned char)__vec[__index];
}

static inline __ATTRS_o_ai __vector unsigned char
vec_splat(__vector unsigned char __vec, int __index)
  __constant_range(__index, 0, 15) {
  return (__vector unsigned char)__vec[__index];
}

static inline __ATTRS_o_ai __vector signed short
vec_splat(__vector signed short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (__vector signed short)__vec[__index];
}

static inline __ATTRS_o_ai __vector __bool short
vec_splat(__vector __bool short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (__vector __bool short)(__vector unsigned short)__vec[__index];
}

static inline __ATTRS_o_ai __vector unsigned short
vec_splat(__vector unsigned short __vec, int __index)
  __constant_range(__index, 0, 7) {
  return (__vector unsigned short)__vec[__index];
}

static inline __ATTRS_o_ai __vector signed int
vec_splat(__vector signed int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (__vector signed int)__vec[__index];
}

static inline __ATTRS_o_ai __vector __bool int
vec_splat(__vector __bool int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (__vector __bool int)(__vector unsigned int)__vec[__index];
}

static inline __ATTRS_o_ai __vector unsigned int
vec_splat(__vector unsigned int __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (__vector unsigned int)__vec[__index];
}

static inline __ATTRS_o_ai __vector signed long long
vec_splat(__vector signed long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (__vector signed long long)__vec[__index];
}

static inline __ATTRS_o_ai __vector __bool long long
vec_splat(__vector __bool long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return ((__vector __bool long long)
          (__vector unsigned long long)__vec[__index]);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_splat(__vector unsigned long long __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (__vector unsigned long long)__vec[__index];
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_splat(__vector float __vec, int __index)
  __constant_range(__index, 0, 3) {
  return (__vector float)__vec[__index];
}
#endif

static inline __ATTRS_o_ai __vector double
vec_splat(__vector double __vec, int __index)
  __constant_range(__index, 0, 1) {
  return (__vector double)__vec[__index];
}

/*-- vec_splat_s* -----------------------------------------------------------*/

static inline __ATTRS_ai __vector signed char
vec_splat_s8(signed char __scalar)
  __constant(__scalar) {
  return (__vector signed char)__scalar;
}

static inline __ATTRS_ai __vector signed short
vec_splat_s16(signed short __scalar)
  __constant(__scalar) {
  return (__vector signed short)__scalar;
}

static inline __ATTRS_ai __vector signed int
vec_splat_s32(signed short __scalar)
  __constant(__scalar) {
  return (__vector signed int)(signed int)__scalar;
}

static inline __ATTRS_ai __vector signed long long
vec_splat_s64(signed short __scalar)
  __constant(__scalar) {
  return (__vector signed long long)(signed long)__scalar;
}

/*-- vec_splat_u* -----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_splat_u8(unsigned char __scalar)
  __constant(__scalar) {
  return (__vector unsigned char)__scalar;
}

static inline __ATTRS_ai __vector unsigned short
vec_splat_u16(unsigned short __scalar)
  __constant(__scalar) {
  return (__vector unsigned short)__scalar;
}

static inline __ATTRS_ai __vector unsigned int
vec_splat_u32(signed short __scalar)
  __constant(__scalar) {
  return (__vector unsigned int)(signed int)__scalar;
}

static inline __ATTRS_ai __vector unsigned long long
vec_splat_u64(signed short __scalar)
  __constant(__scalar) {
  return (__vector unsigned long long)(signed long long)__scalar;
}

/*-- vec_splats -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_splats(signed char __scalar) {
  return (__vector signed char)__scalar;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_splats(unsigned char __scalar) {
  return (__vector unsigned char)__scalar;
}

static inline __ATTRS_o_ai __vector signed short
vec_splats(signed short __scalar) {
  return (__vector signed short)__scalar;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_splats(unsigned short __scalar) {
  return (__vector unsigned short)__scalar;
}

static inline __ATTRS_o_ai __vector signed int
vec_splats(signed int __scalar) {
  return (__vector signed int)__scalar;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_splats(unsigned int __scalar) {
  return (__vector unsigned int)__scalar;
}

static inline __ATTRS_o_ai __vector signed long long
vec_splats(signed long long __scalar) {
  return (__vector signed long long)__scalar;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_splats(unsigned long long __scalar) {
  return (__vector unsigned long long)__scalar;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_splats(float __scalar) {
  return (__vector float)__scalar;
}
#endif

static inline __ATTRS_o_ai __vector double
vec_splats(double __scalar) {
  return (__vector double)__scalar;
}

/*-- vec_extend_s64 ---------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed long long
vec_extend_s64(__vector signed char __a) {
  return (__vector signed long long)(__a[7], __a[15]);
}

static inline __ATTRS_o_ai __vector signed long long
vec_extend_s64(__vector signed short __a) {
  return (__vector signed long long)(__a[3], __a[7]);
}

static inline __ATTRS_o_ai __vector signed long long
vec_extend_s64(__vector signed int __a) {
  return (__vector signed long long)(__a[1], __a[3]);
}

/*-- vec_mergeh -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_mergeh(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector __bool char
vec_mergeh(__vector __bool char __a, __vector __bool char __b) {
  return (__vector __bool char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_mergeh(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3],
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector signed short
vec_mergeh(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector __bool short
vec_mergeh(__vector __bool short __a, __vector __bool short __b) {
  return (__vector __bool short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mergeh(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)(
    __a[0], __b[0], __a[1], __b[1], __a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector signed int
vec_mergeh(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai __vector __bool int
vec_mergeh(__vector __bool int __a, __vector __bool int __b) {
  return (__vector __bool int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mergeh(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)(__a[0], __b[0], __a[1], __b[1]);
}

static inline __ATTRS_o_ai __vector signed long long
vec_mergeh(__vector signed long long __a, __vector signed long long __b) {
  return (__vector signed long long)(__a[0], __b[0]);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_mergeh(__vector __bool long long __a, __vector __bool long long __b) {
  return (__vector __bool long long)(__a[0], __b[0]);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_mergeh(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned long long)(__a[0], __b[0]);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_mergeh(__vector float __a, __vector float __b) {
  return (__vector float)(__a[0], __b[0], __a[1], __b[1]);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_mergeh(__vector double __a, __vector double __b) {
  return (__vector double)(__a[0], __b[0]);
}

/*-- vec_mergel -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_mergel(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai __vector __bool char
vec_mergel(__vector __bool char __a, __vector __bool char __b) {
  return (__vector __bool char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_mergel(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)(
    __a[8], __b[8], __a[9], __b[9], __a[10], __b[10], __a[11], __b[11],
    __a[12], __b[12], __a[13], __b[13], __a[14], __b[14], __a[15], __b[15]);
}

static inline __ATTRS_o_ai __vector signed short
vec_mergel(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector __bool short
vec_mergel(__vector __bool short __a, __vector __bool short __b) {
  return (__vector __bool short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mergel(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)(
    __a[4], __b[4], __a[5], __b[5], __a[6], __b[6], __a[7], __b[7]);
}

static inline __ATTRS_o_ai __vector signed int
vec_mergel(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector __bool int
vec_mergel(__vector __bool int __a, __vector __bool int __b) {
  return (__vector __bool int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mergel(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)(__a[2], __b[2], __a[3], __b[3]);
}

static inline __ATTRS_o_ai __vector signed long long
vec_mergel(__vector signed long long __a, __vector signed long long __b) {
  return (__vector signed long long)(__a[1], __b[1]);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_mergel(__vector __bool long long __a, __vector __bool long long __b) {
  return (__vector __bool long long)(__a[1], __b[1]);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_mergel(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned long long)(__a[1], __b[1]);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_mergel(__vector float __a, __vector float __b) {
  return (__vector float)(__a[2], __b[2], __a[3], __b[3]);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_mergel(__vector double __a, __vector double __b) {
  return (__vector double)(__a[1], __b[1]);
}

/*-- vec_pack ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_pack(__vector signed short __a, __vector signed short __b) {
  __vector signed char __ac = (__vector signed char)__a;
  __vector signed char __bc = (__vector signed char)__b;
  return (__vector signed char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai __vector __bool char
vec_pack(__vector __bool short __a, __vector __bool short __b) {
  __vector __bool char __ac = (__vector __bool char)__a;
  __vector __bool char __bc = (__vector __bool char)__b;
  return (__vector __bool char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_pack(__vector unsigned short __a, __vector unsigned short __b) {
  __vector unsigned char __ac = (__vector unsigned char)__a;
  __vector unsigned char __bc = (__vector unsigned char)__b;
  return (__vector unsigned char)(
    __ac[1], __ac[3], __ac[5], __ac[7], __ac[9], __ac[11], __ac[13], __ac[15],
    __bc[1], __bc[3], __bc[5], __bc[7], __bc[9], __bc[11], __bc[13], __bc[15]);
}

static inline __ATTRS_o_ai __vector signed short
vec_pack(__vector signed int __a, __vector signed int __b) {
  __vector signed short __ac = (__vector signed short)__a;
  __vector signed short __bc = (__vector signed short)__b;
  return (__vector signed short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai __vector __bool short
vec_pack(__vector __bool int __a, __vector __bool int __b) {
  __vector __bool short __ac = (__vector __bool short)__a;
  __vector __bool short __bc = (__vector __bool short)__b;
  return (__vector __bool short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_pack(__vector unsigned int __a, __vector unsigned int __b) {
  __vector unsigned short __ac = (__vector unsigned short)__a;
  __vector unsigned short __bc = (__vector unsigned short)__b;
  return (__vector unsigned short)(
    __ac[1], __ac[3], __ac[5], __ac[7],
    __bc[1], __bc[3], __bc[5], __bc[7]);
}

static inline __ATTRS_o_ai __vector signed int
vec_pack(__vector signed long long __a, __vector signed long long __b) {
  __vector signed int __ac = (__vector signed int)__a;
  __vector signed int __bc = (__vector signed int)__b;
  return (__vector signed int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

static inline __ATTRS_o_ai __vector __bool int
vec_pack(__vector __bool long long __a, __vector __bool long long __b) {
  __vector __bool int __ac = (__vector __bool int)__a;
  __vector __bool int __bc = (__vector __bool int)__b;
  return (__vector __bool int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_pack(__vector unsigned long long __a, __vector unsigned long long __b) {
  __vector unsigned int __ac = (__vector unsigned int)__a;
  __vector unsigned int __bc = (__vector unsigned int)__b;
  return (__vector unsigned int)(__ac[1], __ac[3], __bc[1], __bc[3]);
}

/*-- vec_packs --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_packs(__vector signed short __a, __vector signed short __b) {
  return __builtin_s390_vpksh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_packs(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vpklsh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_packs(__vector signed int __a, __vector signed int __b) {
  return __builtin_s390_vpksf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_packs(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vpklsf(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_packs(__vector signed long long __a, __vector signed long long __b) {
  return __builtin_s390_vpksg(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_packs(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vpklsg(__a, __b);
}

/*-- vec_packs_cc -----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_packs_cc(__vector signed short __a, __vector signed short __b, int *__cc) {
  return __builtin_s390_vpkshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_packs_cc(__vector unsigned short __a, __vector unsigned short __b,
             int *__cc) {
  return __builtin_s390_vpklshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_packs_cc(__vector signed int __a, __vector signed int __b, int *__cc) {
  return __builtin_s390_vpksfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_packs_cc(__vector unsigned int __a, __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vpklsfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_packs_cc(__vector signed long long __a, __vector signed long long __b,
             int *__cc) {
  return __builtin_s390_vpksgs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_packs_cc(__vector unsigned long long __a, __vector unsigned long long __b,
             int *__cc) {
  return __builtin_s390_vpklsgs(__a, __b, __cc);
}

/*-- vec_packsu -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_packsu(__vector signed short __a, __vector signed short __b) {
  const __vector signed short __zero = (__vector signed short)0;
  return __builtin_s390_vpklsh(
    (__vector unsigned short)(__a >= __zero) & (__vector unsigned short)__a,
    (__vector unsigned short)(__b >= __zero) & (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_packsu(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vpklsh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_packsu(__vector signed int __a, __vector signed int __b) {
  const __vector signed int __zero = (__vector signed int)0;
  return __builtin_s390_vpklsf(
    (__vector unsigned int)(__a >= __zero) & (__vector unsigned int)__a,
    (__vector unsigned int)(__b >= __zero) & (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_packsu(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vpklsf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_packsu(__vector signed long long __a, __vector signed long long __b) {
  const __vector signed long long __zero = (__vector signed long long)0;
  return __builtin_s390_vpklsg(
    (__vector unsigned long long)(__a >= __zero) &
    (__vector unsigned long long)__a,
    (__vector unsigned long long)(__b >= __zero) &
    (__vector unsigned long long)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_packsu(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vpklsg(__a, __b);
}

/*-- vec_packsu_cc ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_packsu_cc(__vector unsigned short __a, __vector unsigned short __b,
              int *__cc) {
  return __builtin_s390_vpklshs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_packsu_cc(__vector unsigned int __a, __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vpklsfs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_packsu_cc(__vector unsigned long long __a, __vector unsigned long long __b,
              int *__cc) {
  return __builtin_s390_vpklsgs(__a, __b, __cc);
}

/*-- vec_unpackh ------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_unpackh(__vector signed char __a) {
  return __builtin_s390_vuphb(__a);
}

static inline __ATTRS_o_ai __vector __bool short
vec_unpackh(__vector __bool char __a) {
  return ((__vector __bool short)
          __builtin_s390_vuphb((__vector signed char)__a));
}

static inline __ATTRS_o_ai __vector unsigned short
vec_unpackh(__vector unsigned char __a) {
  return __builtin_s390_vuplhb(__a);
}

static inline __ATTRS_o_ai __vector signed int
vec_unpackh(__vector signed short __a) {
  return __builtin_s390_vuphh(__a);
}

static inline __ATTRS_o_ai __vector __bool int
vec_unpackh(__vector __bool short __a) {
  return (__vector __bool int)__builtin_s390_vuphh((__vector signed short)__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_unpackh(__vector unsigned short __a) {
  return __builtin_s390_vuplhh(__a);
}

static inline __ATTRS_o_ai __vector signed long long
vec_unpackh(__vector signed int __a) {
  return __builtin_s390_vuphf(__a);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_unpackh(__vector __bool int __a) {
  return ((__vector __bool long long)
          __builtin_s390_vuphf((__vector signed int)__a));
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_unpackh(__vector unsigned int __a) {
  return __builtin_s390_vuplhf(__a);
}

/*-- vec_unpackl ------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_unpackl(__vector signed char __a) {
  return __builtin_s390_vuplb(__a);
}

static inline __ATTRS_o_ai __vector __bool short
vec_unpackl(__vector __bool char __a) {
  return ((__vector __bool short)
          __builtin_s390_vuplb((__vector signed char)__a));
}

static inline __ATTRS_o_ai __vector unsigned short
vec_unpackl(__vector unsigned char __a) {
  return __builtin_s390_vupllb(__a);
}

static inline __ATTRS_o_ai __vector signed int
vec_unpackl(__vector signed short __a) {
  return __builtin_s390_vuplhw(__a);
}

static inline __ATTRS_o_ai __vector __bool int
vec_unpackl(__vector __bool short __a) {
  return ((__vector __bool int)
          __builtin_s390_vuplhw((__vector signed short)__a));
}

static inline __ATTRS_o_ai __vector unsigned int
vec_unpackl(__vector unsigned short __a) {
  return __builtin_s390_vupllh(__a);
}

static inline __ATTRS_o_ai __vector signed long long
vec_unpackl(__vector signed int __a) {
  return __builtin_s390_vuplf(__a);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_unpackl(__vector __bool int __a) {
  return ((__vector __bool long long)
          __builtin_s390_vuplf((__vector signed int)__a));
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_unpackl(__vector unsigned int __a) {
  return __builtin_s390_vupllf(__a);
}

/*-- vec_cmpeq --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmpeq(__vector __bool char __a, __vector __bool char __b) {
  return (__vector __bool char)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmpeq(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmpeq(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpeq(__vector __bool short __a, __vector __bool short __b) {
  return (__vector __bool short)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpeq(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpeq(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpeq(__vector __bool int __a, __vector __bool int __b) {
  return (__vector __bool int)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpeq(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpeq(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpeq(__vector __bool long long __a, __vector __bool long long __b) {
  return (__vector __bool long long)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpeq(__vector signed long long __a, __vector signed long long __b) {
  return (__vector __bool long long)(__a == __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpeq(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector __bool long long)(__a == __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool int
vec_cmpeq(__vector float __a, __vector float __b) {
  return (__vector __bool int)(__a == __b);
}
#endif

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpeq(__vector double __a, __vector double __b) {
  return (__vector __bool long long)(__a == __b);
}

/*-- vec_cmpge --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmpge(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmpge(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpge(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpge(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpge(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpge(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpge(__vector signed long long __a, __vector signed long long __b) {
  return (__vector __bool long long)(__a >= __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpge(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector __bool long long)(__a >= __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool int
vec_cmpge(__vector float __a, __vector float __b) {
  return (__vector __bool int)(__a >= __b);
}
#endif

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpge(__vector double __a, __vector double __b) {
  return (__vector __bool long long)(__a >= __b);
}

/*-- vec_cmpgt --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmpgt(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmpgt(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpgt(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpgt(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpgt(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpgt(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpgt(__vector signed long long __a, __vector signed long long __b) {
  return (__vector __bool long long)(__a > __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpgt(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector __bool long long)(__a > __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool int
vec_cmpgt(__vector float __a, __vector float __b) {
  return (__vector __bool int)(__a > __b);
}
#endif

static inline __ATTRS_o_ai __vector __bool long long
vec_cmpgt(__vector double __a, __vector double __b) {
  return (__vector __bool long long)(__a > __b);
}

/*-- vec_cmple --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmple(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmple(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmple(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmple(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmple(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmple(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmple(__vector signed long long __a, __vector signed long long __b) {
  return (__vector __bool long long)(__a <= __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmple(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector __bool long long)(__a <= __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool int
vec_cmple(__vector float __a, __vector float __b) {
  return (__vector __bool int)(__a <= __b);
}
#endif

static inline __ATTRS_o_ai __vector __bool long long
vec_cmple(__vector double __a, __vector double __b) {
  return (__vector __bool long long)(__a <= __b);
}

/*-- vec_cmplt --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmplt(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cmplt(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmplt(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmplt(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmplt(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmplt(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmplt(__vector signed long long __a, __vector signed long long __b) {
  return (__vector __bool long long)(__a < __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_cmplt(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector __bool long long)(__a < __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool int
vec_cmplt(__vector float __a, __vector float __b) {
  return (__vector __bool int)(__a < __b);
}
#endif

static inline __ATTRS_o_ai __vector __bool long long
vec_cmplt(__vector double __a, __vector double __b) {
  return (__vector __bool long long)(__a < __b);
}

/*-- vec_all_eq -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_eq(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_eq(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_eq(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_eq(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_ne -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_ne(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ne(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ne(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ne(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_ge -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_ge(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (__vector signed char)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (__vector unsigned char)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b,
                        (__vector unsigned char)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (__vector signed short)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (__vector unsigned short)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b,
                        (__vector unsigned short)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (__vector signed int)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (__vector unsigned int)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b,
                        (__vector unsigned int)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (__vector signed long long)__a, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b, __a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (__vector unsigned long long)__a, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_ge(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b,
                        (__vector unsigned long long)__a, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ge(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ge(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_gt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_gt(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (__vector signed char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (__vector signed short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (__vector signed int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (__vector signed long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__a, __b, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_gt(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_gt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_gt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc == 0;
}

/*-- vec_all_le -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_le(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (__vector signed char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (__vector signed short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (__vector signed int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (__vector signed long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__a, __b, &__cc);
  return __cc == 3;
}

static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc == 3;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_le(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc == 3;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_le(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_le(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc == 0;
}

/*-- vec_all_lt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_all_lt(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (__vector signed char)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (__vector unsigned char)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b,
                        (__vector unsigned char)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (__vector signed short)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (__vector unsigned short)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b,
                        (__vector unsigned short)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (__vector signed int)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (__vector unsigned int)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b,
                        (__vector unsigned int)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (__vector signed long long)__a, &__cc);
  return __cc == 0;
}

static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b, __a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (__vector unsigned long long)__a, &__cc);
  return __cc == 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_all_lt(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b,
                        (__vector unsigned long long)__a, &__cc);
  return __cc == 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_lt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_lt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc == 0;
}

/*-- vec_all_nge ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nge(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nge(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_ngt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_ngt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_ngt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc == 3;
}

/*-- vec_all_nle ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nle(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nle(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc == 3;
}

/*-- vec_all_nlt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nlt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nlt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc == 3;
}

/*-- vec_all_nan ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_nan(__vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc == 0;
}
#endif

static inline __ATTRS_o_ai int
vec_all_nan(__vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc == 0;
}

/*-- vec_all_numeric --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_all_numeric(__vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc == 3;
}
#endif

static inline __ATTRS_o_ai int
vec_all_numeric(__vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc == 3;
}

/*-- vec_any_eq -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_eq(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_eq(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_eq(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_eq(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_ne -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_ne(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vceqbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vceqhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vceqfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ne(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vceqgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ne(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfcesbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ne(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfcedbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_ge -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_ge(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (__vector signed char)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (__vector unsigned char)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b,
                        (__vector unsigned char)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (__vector signed short)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (__vector unsigned short)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b,
                        (__vector unsigned short)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (__vector signed int)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (__vector unsigned int)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b,
                        (__vector unsigned int)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (__vector signed long long)__a, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b, __a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (__vector unsigned long long)__a, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_ge(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b,
                        (__vector unsigned long long)__a, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ge(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ge(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_gt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_gt(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (__vector signed char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (__vector signed short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (__vector signed int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (__vector signed long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__a, __b, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_gt(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_gt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_gt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc <= 1;
}

/*-- vec_any_le -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_le(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs(__a, (__vector signed char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs(__a, (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__a,
                        (__vector unsigned char)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs(__a, (__vector signed short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs(__a, (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__a,
                        (__vector unsigned short)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs(__a, (__vector signed int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs(__a, (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__a,
                        (__vector unsigned int)__b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs(__a, (__vector signed long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__a, __b, &__cc);
  return __cc != 0;
}

static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__a, (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a, __b, &__cc);
  return __cc != 0;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_le(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__a,
                        (__vector unsigned long long)__b, &__cc);
  return __cc != 0;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_le(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_le(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc <= 1;
}

/*-- vec_any_lt -------------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_any_lt(__vector signed char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector signed char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchbs((__vector signed char)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool char __a, __vector signed char __b) {
  int __cc;
  __builtin_s390_vchbs(__b, (__vector signed char)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool char __a, __vector unsigned char __b) {
  int __cc;
  __builtin_s390_vchlbs(__b, (__vector unsigned char)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool char __a, __vector __bool char __b) {
  int __cc;
  __builtin_s390_vchlbs((__vector unsigned char)__b,
                        (__vector unsigned char)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector signed short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector signed short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchhs((__vector signed short)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool short __a, __vector signed short __b) {
  int __cc;
  __builtin_s390_vchhs(__b, (__vector signed short)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool short __a, __vector unsigned short __b) {
  int __cc;
  __builtin_s390_vchlhs(__b, (__vector unsigned short)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool short __a, __vector __bool short __b) {
  int __cc;
  __builtin_s390_vchlhs((__vector unsigned short)__b,
                        (__vector unsigned short)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector signed int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector signed int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchfs((__vector signed int)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool int __a, __vector signed int __b) {
  int __cc;
  __builtin_s390_vchfs(__b, (__vector signed int)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool int __a, __vector unsigned int __b) {
  int __cc;
  __builtin_s390_vchlfs(__b, (__vector unsigned int)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool int __a, __vector __bool int __b) {
  int __cc;
  __builtin_s390_vchlfs((__vector unsigned int)__b,
                        (__vector unsigned int)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector signed long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector signed long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchgs((__vector signed long long)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool long long __a, __vector signed long long __b) {
  int __cc;
  __builtin_s390_vchgs(__b, (__vector signed long long)__a, &__cc);
  return __cc <= 1;
}

static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector unsigned long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b, __a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool long long __a, __vector unsigned long long __b) {
  int __cc;
  __builtin_s390_vchlgs(__b, (__vector unsigned long long)__a, &__cc);
  return __cc <= 1;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai int
vec_any_lt(__vector __bool long long __a, __vector __bool long long __b) {
  int __cc;
  __builtin_s390_vchlgs((__vector unsigned long long)__b,
                        (__vector unsigned long long)__a, &__cc);
  return __cc <= 1;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_lt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc <= 1;
}
#endif

static inline __ATTRS_o_ai int
vec_any_lt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc <= 1;
}

/*-- vec_any_nge ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nge(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nge(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_ngt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_ngt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__a, __b, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_ngt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__a, __b, &__cc);
  return __cc != 0;
}

/*-- vec_any_nle ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nle(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchesbs(__b, __a, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nle(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchedbs(__b, __a, &__cc);
  return __cc != 0;
}

/*-- vec_any_nlt ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nlt(__vector float __a, __vector float __b) {
  int __cc;
  __builtin_s390_vfchsbs(__b, __a, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nlt(__vector double __a, __vector double __b) {
  int __cc;
  __builtin_s390_vfchdbs(__b, __a, &__cc);
  return __cc != 0;
}

/*-- vec_any_nan ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_nan(__vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc != 3;
}
#endif

static inline __ATTRS_o_ai int
vec_any_nan(__vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc != 3;
}

/*-- vec_any_numeric --------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_any_numeric(__vector float __a) {
  int __cc;
  __builtin_s390_vftcisb(__a, 15, &__cc);
  return __cc != 0;
}
#endif

static inline __ATTRS_o_ai int
vec_any_numeric(__vector double __a) {
  int __cc;
  __builtin_s390_vftcidb(__a, 15, &__cc);
  return __cc != 0;
}

/*-- vec_andc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_andc(__vector __bool char __a, __vector __bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector signed char
vec_andc(__vector signed char __a, __vector signed char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_andc(__vector __bool char __a, __vector signed char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_andc(__vector signed char __a, __vector __bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_andc(__vector unsigned char __a, __vector unsigned char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_andc(__vector __bool char __a, __vector unsigned char __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_andc(__vector unsigned char __a, __vector __bool char __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector __bool short
vec_andc(__vector __bool short __a, __vector __bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector signed short
vec_andc(__vector signed short __a, __vector signed short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_andc(__vector __bool short __a, __vector signed short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_andc(__vector signed short __a, __vector __bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_andc(__vector unsigned short __a, __vector unsigned short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_andc(__vector __bool short __a, __vector unsigned short __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_andc(__vector unsigned short __a, __vector __bool short __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector __bool int
vec_andc(__vector __bool int __a, __vector __bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector signed int
vec_andc(__vector signed int __a, __vector signed int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_andc(__vector __bool int __a, __vector signed int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_andc(__vector signed int __a, __vector __bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_andc(__vector unsigned int __a, __vector unsigned int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_andc(__vector __bool int __a, __vector unsigned int __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_andc(__vector unsigned int __a, __vector __bool int __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector __bool long long
vec_andc(__vector __bool long long __a, __vector __bool long long __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector signed long long
vec_andc(__vector signed long long __a, __vector signed long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_andc(__vector __bool long long __a, __vector signed long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_andc(__vector signed long long __a, __vector __bool long long __b) {
  return __a & ~__b;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_andc(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_andc(__vector __bool long long __a, __vector unsigned long long __b) {
  return __a & ~__b;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_andc(__vector unsigned long long __a, __vector __bool long long __b) {
  return __a & ~__b;
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_andc(__vector float __a, __vector float __b) {
  return (__vector float)((__vector unsigned int)__a &
                         ~(__vector unsigned int)__b);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_andc(__vector double __a, __vector double __b) {
  return (__vector double)((__vector unsigned long long)__a &
                         ~(__vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_andc(__vector __bool long long __a, __vector double __b) {
  return (__vector double)((__vector unsigned long long)__a &
                         ~(__vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_andc(__vector double __a, __vector __bool long long __b) {
  return (__vector double)((__vector unsigned long long)__a &
                         ~(__vector unsigned long long)__b);
}

/*-- vec_nor ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_nor(__vector __bool char __a, __vector __bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector signed char
vec_nor(__vector signed char __a, __vector signed char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_nor(__vector __bool char __a, __vector signed char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_nor(__vector signed char __a, __vector __bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_nor(__vector unsigned char __a, __vector unsigned char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_nor(__vector __bool char __a, __vector unsigned char __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_nor(__vector unsigned char __a, __vector __bool char __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_nor(__vector __bool short __a, __vector __bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_nor(__vector signed short __a, __vector signed short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_nor(__vector __bool short __a, __vector signed short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_nor(__vector signed short __a, __vector __bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_nor(__vector unsigned short __a, __vector unsigned short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_nor(__vector __bool short __a, __vector unsigned short __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_nor(__vector unsigned short __a, __vector __bool short __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_nor(__vector __bool int __a, __vector __bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_nor(__vector signed int __a, __vector signed int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_nor(__vector __bool int __a, __vector signed int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_nor(__vector signed int __a, __vector __bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_nor(__vector unsigned int __a, __vector unsigned int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_nor(__vector __bool int __a, __vector unsigned int __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_nor(__vector unsigned int __a, __vector __bool int __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_nor(__vector __bool long long __a, __vector __bool long long __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_nor(__vector signed long long __a, __vector signed long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_nor(__vector __bool long long __a, __vector signed long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_nor(__vector signed long long __a, __vector __bool long long __b) {
  return ~(__a | __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_nor(__vector unsigned long long __a, __vector unsigned long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_nor(__vector __bool long long __a, __vector unsigned long long __b) {
  return ~(__a | __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_nor(__vector unsigned long long __a, __vector __bool long long __b) {
  return ~(__a | __b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_nor(__vector float __a, __vector float __b) {
  return (__vector float)~((__vector unsigned int)__a |
                         (__vector unsigned int)__b);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_nor(__vector double __a, __vector double __b) {
  return (__vector double)~((__vector unsigned long long)__a |
                          (__vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_nor(__vector __bool long long __a, __vector double __b) {
  return (__vector double)~((__vector unsigned long long)__a |
                          (__vector unsigned long long)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_nor(__vector double __a, __vector __bool long long __b) {
  return (__vector double)~((__vector unsigned long long)__a |
                          (__vector unsigned long long)__b);
}

/*-- vec_orc ----------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool char
vec_orc(__vector __bool char __a, __vector __bool char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector signed char
vec_orc(__vector signed char __a, __vector signed char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_orc(__vector unsigned char __a, __vector unsigned char __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector __bool short
vec_orc(__vector __bool short __a, __vector __bool short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector signed short
vec_orc(__vector signed short __a, __vector signed short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_orc(__vector unsigned short __a, __vector unsigned short __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector __bool int
vec_orc(__vector __bool int __a, __vector __bool int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector signed int
vec_orc(__vector signed int __a, __vector signed int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_orc(__vector unsigned int __a, __vector unsigned int __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector __bool long long
vec_orc(__vector __bool long long __a, __vector __bool long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector signed long long
vec_orc(__vector signed long long __a, __vector signed long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_orc(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __a | ~__b;
}

static inline __ATTRS_o_ai __vector float
vec_orc(__vector float __a, __vector float __b) {
  return (__vector float)((__vector unsigned int)__a |
                        ~(__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector double
vec_orc(__vector double __a, __vector double __b) {
  return (__vector double)((__vector unsigned long long)__a |
                         ~(__vector unsigned long long)__b);
}
#endif

/*-- vec_nand ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool char
vec_nand(__vector __bool char __a, __vector __bool char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector signed char
vec_nand(__vector signed char __a, __vector signed char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_nand(__vector unsigned char __a, __vector unsigned char __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_nand(__vector __bool short __a, __vector __bool short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_nand(__vector signed short __a, __vector signed short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_nand(__vector unsigned short __a, __vector unsigned short __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_nand(__vector __bool int __a, __vector __bool int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_nand(__vector signed int __a, __vector signed int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_nand(__vector unsigned int __a, __vector unsigned int __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_nand(__vector __bool long long __a, __vector __bool long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_nand(__vector signed long long __a, __vector signed long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_nand(__vector unsigned long long __a, __vector unsigned long long __b) {
  return ~(__a & __b);
}

static inline __ATTRS_o_ai __vector float
vec_nand(__vector float __a, __vector float __b) {
  return (__vector float)~((__vector unsigned int)__a &
                         (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector double
vec_nand(__vector double __a, __vector double __b) {
  return (__vector double)~((__vector unsigned long long)__a &
                          (__vector unsigned long long)__b);
}
#endif

/*-- vec_eqv ----------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector __bool char
vec_eqv(__vector __bool char __a, __vector __bool char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector signed char
vec_eqv(__vector signed char __a, __vector signed char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_eqv(__vector unsigned char __a, __vector unsigned char __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector __bool short
vec_eqv(__vector __bool short __a, __vector __bool short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_eqv(__vector signed short __a, __vector signed short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_eqv(__vector unsigned short __a, __vector unsigned short __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector __bool int
vec_eqv(__vector __bool int __a, __vector __bool int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_eqv(__vector signed int __a, __vector signed int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_eqv(__vector unsigned int __a, __vector unsigned int __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector __bool long long
vec_eqv(__vector __bool long long __a, __vector __bool long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_eqv(__vector signed long long __a, __vector signed long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_eqv(__vector unsigned long long __a, __vector unsigned long long __b) {
  return ~(__a ^ __b);
}

static inline __ATTRS_o_ai __vector float
vec_eqv(__vector float __a, __vector float __b) {
  return (__vector float)~((__vector unsigned int)__a ^
                         (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector double
vec_eqv(__vector double __a, __vector double __b) {
  return (__vector double)~((__vector unsigned long long)__a ^
                          (__vector unsigned long long)__b);
}
#endif

/*-- vec_cntlz --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cntlz(__vector signed char __a) {
  return __builtin_s390_vclzb((__vector unsigned char)__a);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cntlz(__vector unsigned char __a) {
  return __builtin_s390_vclzb(__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cntlz(__vector signed short __a) {
  return __builtin_s390_vclzh((__vector unsigned short)__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cntlz(__vector unsigned short __a) {
  return __builtin_s390_vclzh(__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cntlz(__vector signed int __a) {
  return __builtin_s390_vclzf((__vector unsigned int)__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cntlz(__vector unsigned int __a) {
  return __builtin_s390_vclzf(__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_cntlz(__vector signed long long __a) {
  return __builtin_s390_vclzg((__vector unsigned long long)__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_cntlz(__vector unsigned long long __a) {
  return __builtin_s390_vclzg(__a);
}

/*-- vec_cnttz --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cnttz(__vector signed char __a) {
  return __builtin_s390_vctzb((__vector unsigned char)__a);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cnttz(__vector unsigned char __a) {
  return __builtin_s390_vctzb(__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cnttz(__vector signed short __a) {
  return __builtin_s390_vctzh((__vector unsigned short)__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cnttz(__vector unsigned short __a) {
  return __builtin_s390_vctzh(__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cnttz(__vector signed int __a) {
  return __builtin_s390_vctzf((__vector unsigned int)__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cnttz(__vector unsigned int __a) {
  return __builtin_s390_vctzf(__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_cnttz(__vector signed long long __a) {
  return __builtin_s390_vctzg((__vector unsigned long long)__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_cnttz(__vector unsigned long long __a) {
  return __builtin_s390_vctzg(__a);
}

/*-- vec_popcnt -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_popcnt(__vector signed char __a) {
  return __builtin_s390_vpopctb((__vector unsigned char)__a);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_popcnt(__vector unsigned char __a) {
  return __builtin_s390_vpopctb(__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_popcnt(__vector signed short __a) {
  return __builtin_s390_vpopcth((__vector unsigned short)__a);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_popcnt(__vector unsigned short __a) {
  return __builtin_s390_vpopcth(__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_popcnt(__vector signed int __a) {
  return __builtin_s390_vpopctf((__vector unsigned int)__a);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_popcnt(__vector unsigned int __a) {
  return __builtin_s390_vpopctf(__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_popcnt(__vector signed long long __a) {
  return __builtin_s390_vpopctg((__vector unsigned long long)__a);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_popcnt(__vector unsigned long long __a) {
  return __builtin_s390_vpopctg(__a);
}

/*-- vec_rl -----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_rl(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_verllvb(
    (__vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_rl(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_verllvb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_rl(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_verllvh(
    (__vector unsigned short)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_rl(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_verllvh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_rl(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_verllvf(
    (__vector unsigned int)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_rl(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_verllvf(__a, __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_rl(__vector signed long long __a, __vector unsigned long long __b) {
  return (__vector signed long long)__builtin_s390_verllvg(
    (__vector unsigned long long)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_rl(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_verllvg(__a, __b);
}

/*-- vec_rli ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_rli(__vector signed char __a, unsigned long __b) {
  return (__vector signed char)__builtin_s390_verllb(
    (__vector unsigned char)__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_rli(__vector unsigned char __a, unsigned long __b) {
  return __builtin_s390_verllb(__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_rli(__vector signed short __a, unsigned long __b) {
  return (__vector signed short)__builtin_s390_verllh(
    (__vector unsigned short)__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_rli(__vector unsigned short __a, unsigned long __b) {
  return __builtin_s390_verllh(__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_rli(__vector signed int __a, unsigned long __b) {
  return (__vector signed int)__builtin_s390_verllf(
    (__vector unsigned int)__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_rli(__vector unsigned int __a, unsigned long __b) {
  return __builtin_s390_verllf(__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_rli(__vector signed long long __a, unsigned long __b) {
  return (__vector signed long long)__builtin_s390_verllg(
    (__vector unsigned long long)__a, (unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_rli(__vector unsigned long long __a, unsigned long __b) {
  return __builtin_s390_verllg(__a, (unsigned char)__b);
}

/*-- vec_rl_mask ------------------------------------------------------------*/

extern __ATTRS_o __vector signed char
vec_rl_mask(__vector signed char __a, __vector unsigned char __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector unsigned char
vec_rl_mask(__vector unsigned char __a, __vector unsigned char __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector signed short
vec_rl_mask(__vector signed short __a, __vector unsigned short __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector unsigned short
vec_rl_mask(__vector unsigned short __a, __vector unsigned short __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector signed int
vec_rl_mask(__vector signed int __a, __vector unsigned int __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector unsigned int
vec_rl_mask(__vector unsigned int __a, __vector unsigned int __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector signed long long
vec_rl_mask(__vector signed long long __a, __vector unsigned long long __b,
            unsigned char __c) __constant(__c);

extern __ATTRS_o __vector unsigned long long
vec_rl_mask(__vector unsigned long long __a, __vector unsigned long long __b,
            unsigned char __c) __constant(__c);

#define vec_rl_mask(X, Y, Z) ((__typeof__((vec_rl_mask)((X), (Y), (Z)))) \
  __extension__ ({ \
    __vector unsigned char __res; \
    __vector unsigned char __x = (__vector unsigned char)(X); \
    __vector unsigned char __y = (__vector unsigned char)(Y); \
    switch (sizeof ((X)[0])) { \
    case 1: __res = (__vector unsigned char) __builtin_s390_verimb( \
             (__vector unsigned char)__x, (__vector unsigned char)__x, \
             (__vector unsigned char)__y, (Z)); break; \
    case 2: __res = (__vector unsigned char) __builtin_s390_verimh( \
             (__vector unsigned short)__x, (__vector unsigned short)__x, \
             (__vector unsigned short)__y, (Z)); break; \
    case 4: __res = (__vector unsigned char) __builtin_s390_verimf( \
             (__vector unsigned int)__x, (__vector unsigned int)__x, \
             (__vector unsigned int)__y, (Z)); break; \
    default: __res = (__vector unsigned char) __builtin_s390_verimg( \
             (__vector unsigned long long)__x, (__vector unsigned long long)__x, \
             (__vector unsigned long long)__y, (Z)); break; \
    } __res; }))

/*-- vec_sll ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_sll(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_sll(__vector signed char __a, __vector unsigned short __b) {
  return (__vector signed char)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_sll(__vector signed char __a, __vector unsigned int __b) {
  return (__vector signed char)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sll(__vector __bool char __a, __vector unsigned char __b) {
  return (__vector __bool char)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sll(__vector __bool char __a, __vector unsigned short __b) {
  return (__vector __bool char)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sll(__vector __bool char __a, __vector unsigned int __b) {
  return (__vector __bool char)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_sll(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsl(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_sll(__vector unsigned char __a, __vector unsigned short __b) {
  return __builtin_s390_vsl(__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_sll(__vector unsigned char __a, __vector unsigned int __b) {
  return __builtin_s390_vsl(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_sll(__vector signed short __a, __vector unsigned char __b) {
  return (__vector signed short)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_sll(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_sll(__vector signed short __a, __vector unsigned int __b) {
  return (__vector signed short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sll(__vector __bool short __a, __vector unsigned char __b) {
  return (__vector __bool short)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sll(__vector __bool short __a, __vector unsigned short __b) {
  return (__vector __bool short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sll(__vector __bool short __a, __vector unsigned int __b) {
  return (__vector __bool short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_sll(__vector unsigned short __a, __vector unsigned char __b) {
  return (__vector unsigned short)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_sll(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_sll(__vector unsigned short __a, __vector unsigned int __b) {
  return (__vector unsigned short)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_sll(__vector signed int __a, __vector unsigned char __b) {
  return (__vector signed int)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_sll(__vector signed int __a, __vector unsigned short __b) {
  return (__vector signed int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_sll(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sll(__vector __bool int __a, __vector unsigned char __b) {
  return (__vector __bool int)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sll(__vector __bool int __a, __vector unsigned short __b) {
  return (__vector __bool int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sll(__vector __bool int __a, __vector unsigned int __b) {
  return (__vector __bool int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_sll(__vector unsigned int __a, __vector unsigned char __b) {
  return (__vector unsigned int)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_sll(__vector unsigned int __a, __vector unsigned short __b) {
  return (__vector unsigned int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_sll(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_sll(__vector signed long long __a, __vector unsigned char __b) {
  return (__vector signed long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_sll(__vector signed long long __a, __vector unsigned short __b) {
  return (__vector signed long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_sll(__vector signed long long __a, __vector unsigned int __b) {
  return (__vector signed long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sll(__vector __bool long long __a, __vector unsigned char __b) {
  return (__vector __bool long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sll(__vector __bool long long __a, __vector unsigned short __b) {
  return (__vector __bool long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sll(__vector __bool long long __a, __vector unsigned int __b) {
  return (__vector __bool long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_sll(__vector unsigned long long __a, __vector unsigned char __b) {
  return (__vector unsigned long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_sll(__vector unsigned long long __a, __vector unsigned short __b) {
  return (__vector unsigned long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_sll(__vector unsigned long long __a, __vector unsigned int __b) {
  return (__vector unsigned long long)__builtin_s390_vsl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_slb ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_slb(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed char
vec_slb(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vslb(
    (__vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_slb(__vector unsigned char __a, __vector signed char __b) {
  return __builtin_s390_vslb(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_slb(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vslb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_slb(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_slb(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_slb(__vector unsigned short __a, __vector signed short __b) {
  return (__vector unsigned short)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_slb(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_slb(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_slb(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_slb(__vector unsigned int __a, __vector signed int __b) {
  return (__vector unsigned int)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_slb(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_slb(__vector signed long long __a, __vector signed long long __b) {
  return (__vector signed long long)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_slb(__vector signed long long __a, __vector unsigned long long __b) {
  return (__vector signed long long)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_slb(__vector unsigned long long __a, __vector signed long long __b) {
  return (__vector unsigned long long)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_slb(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned long long)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_slb(__vector float __a, __vector signed int __b) {
  return (__vector float)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector float
vec_slb(__vector float __a, __vector unsigned int __b) {
  return (__vector float)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_slb(__vector double __a, __vector signed long long __b) {
  return (__vector double)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector double
vec_slb(__vector double __a, __vector unsigned long long __b) {
  return (__vector double)__builtin_s390_vslb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_sld ----------------------------------------------------------------*/

extern __ATTRS_o __vector signed char
vec_sld(__vector signed char __a, __vector signed char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector __bool char
vec_sld(__vector __bool char __a, __vector __bool char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector unsigned char
vec_sld(__vector unsigned char __a, __vector unsigned char __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector signed short
vec_sld(__vector signed short __a, __vector signed short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector __bool short
vec_sld(__vector __bool short __a, __vector __bool short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector unsigned short
vec_sld(__vector unsigned short __a, __vector unsigned short __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector signed int
vec_sld(__vector signed int __a, __vector signed int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector __bool int
vec_sld(__vector __bool int __a, __vector __bool int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector unsigned int
vec_sld(__vector unsigned int __a, __vector unsigned int __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector signed long long
vec_sld(__vector signed long long __a, __vector signed long long __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector __bool long long
vec_sld(__vector __bool long long __a, __vector __bool long long __b, int __c)
  __constant_range(__c, 0, 15);

extern __ATTRS_o __vector unsigned long long
vec_sld(__vector unsigned long long __a, __vector unsigned long long __b,
        int __c)
  __constant_range(__c, 0, 15);

#if __ARCH__ >= 12
extern __ATTRS_o __vector float
vec_sld(__vector float __a, __vector float __b, int __c)
  __constant_range(__c, 0, 15);
#endif

extern __ATTRS_o __vector double
vec_sld(__vector double __a, __vector double __b, int __c)
  __constant_range(__c, 0, 15);

#define vec_sld(X, Y, Z) ((__typeof__((vec_sld)((X), (Y), (Z)))) \
  __builtin_s390_vsldb((__vector unsigned char)(X), \
                       (__vector unsigned char)(Y), (Z)))

/*-- vec_sldw ---------------------------------------------------------------*/

extern __ATTRS_o __vector signed char
vec_sldw(__vector signed char __a, __vector signed char __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector unsigned char
vec_sldw(__vector unsigned char __a, __vector unsigned char __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector signed short
vec_sldw(__vector signed short __a, __vector signed short __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector unsigned short
vec_sldw(__vector unsigned short __a, __vector unsigned short __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector signed int
vec_sldw(__vector signed int __a, __vector signed int __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector unsigned int
vec_sldw(__vector unsigned int __a, __vector unsigned int __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector signed long long
vec_sldw(__vector signed long long __a, __vector signed long long __b, int __c)
  __constant_range(__c, 0, 3);

extern __ATTRS_o __vector unsigned long long
vec_sldw(__vector unsigned long long __a, __vector unsigned long long __b,
         int __c)
  __constant_range(__c, 0, 3);

// This prototype is deprecated.
extern __ATTRS_o __vector double
vec_sldw(__vector double __a, __vector double __b, int __c)
  __constant_range(__c, 0, 3);

#define vec_sldw(X, Y, Z) ((__typeof__((vec_sldw)((X), (Y), (Z)))) \
  __builtin_s390_vsldb((__vector unsigned char)(X), \
                       (__vector unsigned char)(Y), (Z) * 4))

/*-- vec_sldb ---------------------------------------------------------------*/

#if __ARCH__ >= 13

extern __ATTRS_o __vector signed char
vec_sldb(__vector signed char __a, __vector signed char __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned char
vec_sldb(__vector unsigned char __a, __vector unsigned char __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed short
vec_sldb(__vector signed short __a, __vector signed short __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned short
vec_sldb(__vector unsigned short __a, __vector unsigned short __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed int
vec_sldb(__vector signed int __a, __vector signed int __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned int
vec_sldb(__vector unsigned int __a, __vector unsigned int __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed long long
vec_sldb(__vector signed long long __a, __vector signed long long __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned long long
vec_sldb(__vector unsigned long long __a, __vector unsigned long long __b,
         int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector float
vec_sldb(__vector float __a, __vector float __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector double
vec_sldb(__vector double __a, __vector double __b, int __c)
  __constant_range(__c, 0, 7);

#define vec_sldb(X, Y, Z) ((__typeof__((vec_sldb)((X), (Y), (Z)))) \
  __builtin_s390_vsld((__vector unsigned char)(X), \
                      (__vector unsigned char)(Y), (Z)))

#endif

/*-- vec_sral ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_sral(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_sral(__vector signed char __a, __vector unsigned short __b) {
  return (__vector signed char)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_sral(__vector signed char __a, __vector unsigned int __b) {
  return (__vector signed char)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sral(__vector __bool char __a, __vector unsigned char __b) {
  return (__vector __bool char)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sral(__vector __bool char __a, __vector unsigned short __b) {
  return (__vector __bool char)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_sral(__vector __bool char __a, __vector unsigned int __b) {
  return (__vector __bool char)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_sral(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsra(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_sral(__vector unsigned char __a, __vector unsigned short __b) {
  return __builtin_s390_vsra(__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_sral(__vector unsigned char __a, __vector unsigned int __b) {
  return __builtin_s390_vsra(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_sral(__vector signed short __a, __vector unsigned char __b) {
  return (__vector signed short)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_sral(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_sral(__vector signed short __a, __vector unsigned int __b) {
  return (__vector signed short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sral(__vector __bool short __a, __vector unsigned char __b) {
  return (__vector __bool short)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sral(__vector __bool short __a, __vector unsigned short __b) {
  return (__vector __bool short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_sral(__vector __bool short __a, __vector unsigned int __b) {
  return (__vector __bool short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_sral(__vector unsigned short __a, __vector unsigned char __b) {
  return (__vector unsigned short)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_sral(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_sral(__vector unsigned short __a, __vector unsigned int __b) {
  return (__vector unsigned short)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_sral(__vector signed int __a, __vector unsigned char __b) {
  return (__vector signed int)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_sral(__vector signed int __a, __vector unsigned short __b) {
  return (__vector signed int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_sral(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sral(__vector __bool int __a, __vector unsigned char __b) {
  return (__vector __bool int)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sral(__vector __bool int __a, __vector unsigned short __b) {
  return (__vector __bool int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_sral(__vector __bool int __a, __vector unsigned int __b) {
  return (__vector __bool int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_sral(__vector unsigned int __a, __vector unsigned char __b) {
  return (__vector unsigned int)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_sral(__vector unsigned int __a, __vector unsigned short __b) {
  return (__vector unsigned int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_sral(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_sral(__vector signed long long __a, __vector unsigned char __b) {
  return (__vector signed long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_sral(__vector signed long long __a, __vector unsigned short __b) {
  return (__vector signed long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_sral(__vector signed long long __a, __vector unsigned int __b) {
  return (__vector signed long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sral(__vector __bool long long __a, __vector unsigned char __b) {
  return (__vector __bool long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sral(__vector __bool long long __a, __vector unsigned short __b) {
  return (__vector __bool long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_sral(__vector __bool long long __a, __vector unsigned int __b) {
  return (__vector __bool long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_sral(__vector unsigned long long __a, __vector unsigned char __b) {
  return (__vector unsigned long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_sral(__vector unsigned long long __a, __vector unsigned short __b) {
  return (__vector unsigned long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_sral(__vector unsigned long long __a, __vector unsigned int __b) {
  return (__vector unsigned long long)__builtin_s390_vsra(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_srab ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_srab(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed char
vec_srab(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vsrab(
    (__vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_srab(__vector unsigned char __a, __vector signed char __b) {
  return __builtin_s390_vsrab(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_srab(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsrab(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_srab(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_srab(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_srab(__vector unsigned short __a, __vector signed short __b) {
  return (__vector unsigned short)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_srab(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_srab(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_srab(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_srab(__vector unsigned int __a, __vector signed int __b) {
  return (__vector unsigned int)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_srab(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_srab(__vector signed long long __a, __vector signed long long __b) {
  return (__vector signed long long)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_srab(__vector signed long long __a, __vector unsigned long long __b) {
  return (__vector signed long long)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_srab(__vector unsigned long long __a, __vector signed long long __b) {
  return (__vector unsigned long long)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_srab(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned long long)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_srab(__vector float __a, __vector signed int __b) {
  return (__vector float)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector float
vec_srab(__vector float __a, __vector unsigned int __b) {
  return (__vector float)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_srab(__vector double __a, __vector signed long long __b) {
  return (__vector double)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector double
vec_srab(__vector double __a, __vector unsigned long long __b) {
  return (__vector double)__builtin_s390_vsrab(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_srl ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_srl(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_srl(__vector signed char __a, __vector unsigned short __b) {
  return (__vector signed char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_srl(__vector signed char __a, __vector unsigned int __b) {
  return (__vector signed char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_srl(__vector __bool char __a, __vector unsigned char __b) {
  return (__vector __bool char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_srl(__vector __bool char __a, __vector unsigned short __b) {
  return (__vector __bool char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool char
vec_srl(__vector __bool char __a, __vector unsigned int __b) {
  return (__vector __bool char)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_srl(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsrl(__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_srl(__vector unsigned char __a, __vector unsigned short __b) {
  return __builtin_s390_vsrl(__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_srl(__vector unsigned char __a, __vector unsigned int __b) {
  return __builtin_s390_vsrl(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_srl(__vector signed short __a, __vector unsigned char __b) {
  return (__vector signed short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_srl(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_srl(__vector signed short __a, __vector unsigned int __b) {
  return (__vector signed short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_srl(__vector __bool short __a, __vector unsigned char __b) {
  return (__vector __bool short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_srl(__vector __bool short __a, __vector unsigned short __b) {
  return (__vector __bool short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool short
vec_srl(__vector __bool short __a, __vector unsigned int __b) {
  return (__vector __bool short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_srl(__vector unsigned short __a, __vector unsigned char __b) {
  return (__vector unsigned short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_srl(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_srl(__vector unsigned short __a, __vector unsigned int __b) {
  return (__vector unsigned short)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_srl(__vector signed int __a, __vector unsigned char __b) {
  return (__vector signed int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_srl(__vector signed int __a, __vector unsigned short __b) {
  return (__vector signed int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_srl(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_srl(__vector __bool int __a, __vector unsigned char __b) {
  return (__vector __bool int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_srl(__vector __bool int __a, __vector unsigned short __b) {
  return (__vector __bool int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool int
vec_srl(__vector __bool int __a, __vector unsigned int __b) {
  return (__vector __bool int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_srl(__vector unsigned int __a, __vector unsigned char __b) {
  return (__vector unsigned int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_srl(__vector unsigned int __a, __vector unsigned short __b) {
  return (__vector unsigned int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_srl(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_srl(__vector signed long long __a, __vector unsigned char __b) {
  return (__vector signed long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_srl(__vector signed long long __a, __vector unsigned short __b) {
  return (__vector signed long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_srl(__vector signed long long __a, __vector unsigned int __b) {
  return (__vector signed long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_srl(__vector __bool long long __a, __vector unsigned char __b) {
  return (__vector __bool long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_srl(__vector __bool long long __a, __vector unsigned short __b) {
  return (__vector __bool long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector __bool long long
vec_srl(__vector __bool long long __a, __vector unsigned int __b) {
  return (__vector __bool long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_srl(__vector unsigned long long __a, __vector unsigned char __b) {
  return (__vector unsigned long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, __b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_srl(__vector unsigned long long __a, __vector unsigned short __b) {
  return (__vector unsigned long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_srl(__vector unsigned long long __a, __vector unsigned int __b) {
  return (__vector unsigned long long)__builtin_s390_vsrl(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_srb ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_srb(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed char
vec_srb(__vector signed char __a, __vector unsigned char __b) {
  return (__vector signed char)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_srb(__vector unsigned char __a, __vector signed char __b) {
  return __builtin_s390_vsrlb(__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_srb(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsrlb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_srb(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed short
vec_srb(__vector signed short __a, __vector unsigned short __b) {
  return (__vector signed short)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_srb(__vector unsigned short __a, __vector signed short __b) {
  return (__vector unsigned short)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_srb(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector unsigned short)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_srb(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed int
vec_srb(__vector signed int __a, __vector unsigned int __b) {
  return (__vector signed int)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_srb(__vector unsigned int __a, __vector signed int __b) {
  return (__vector unsigned int)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_srb(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned int)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_srb(__vector signed long long __a, __vector signed long long __b) {
  return (__vector signed long long)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_srb(__vector signed long long __a, __vector unsigned long long __b) {
  return (__vector signed long long)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_srb(__vector unsigned long long __a, __vector signed long long __b) {
  return (__vector unsigned long long)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_srb(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned long long)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_srb(__vector float __a, __vector signed int __b) {
  return (__vector float)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector float
vec_srb(__vector float __a, __vector unsigned int __b) {
  return (__vector float)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_srb(__vector double __a, __vector signed long long __b) {
  return (__vector double)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector double
vec_srb(__vector double __a, __vector unsigned long long __b) {
  return (__vector double)__builtin_s390_vsrlb(
    (__vector unsigned char)__a, (__vector unsigned char)__b);
}

/*-- vec_srdb ---------------------------------------------------------------*/

#if __ARCH__ >= 13

extern __ATTRS_o __vector signed char
vec_srdb(__vector signed char __a, __vector signed char __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned char
vec_srdb(__vector unsigned char __a, __vector unsigned char __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed short
vec_srdb(__vector signed short __a, __vector signed short __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned short
vec_srdb(__vector unsigned short __a, __vector unsigned short __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed int
vec_srdb(__vector signed int __a, __vector signed int __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned int
vec_srdb(__vector unsigned int __a, __vector unsigned int __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector signed long long
vec_srdb(__vector signed long long __a, __vector signed long long __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector unsigned long long
vec_srdb(__vector unsigned long long __a, __vector unsigned long long __b,
         int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector float
vec_srdb(__vector float __a, __vector float __b, int __c)
  __constant_range(__c, 0, 7);

extern __ATTRS_o __vector double
vec_srdb(__vector double __a, __vector double __b, int __c)
  __constant_range(__c, 0, 7);

#define vec_srdb(X, Y, Z) ((__typeof__((vec_srdb)((X), (Y), (Z)))) \
  __builtin_s390_vsrd((__vector unsigned char)(X), \
                      (__vector unsigned char)(Y), (Z)))

#endif

/*-- vec_abs ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_abs(__vector signed char __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (__vector signed char)0));
}

static inline __ATTRS_o_ai __vector signed short
vec_abs(__vector signed short __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (__vector signed short)0));
}

static inline __ATTRS_o_ai __vector signed int
vec_abs(__vector signed int __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (__vector signed int)0));
}

static inline __ATTRS_o_ai __vector signed long long
vec_abs(__vector signed long long __a) {
  return vec_sel(__a, -__a, vec_cmplt(__a, (__vector signed long long)0));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_abs(__vector float __a) {
  return __builtin_s390_vflpsb(__a);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_abs(__vector double __a) {
  return __builtin_s390_vflpdb(__a);
}

/*-- vec_nabs ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_nabs(__vector float __a) {
  return __builtin_s390_vflnsb(__a);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_nabs(__vector double __a) {
  return __builtin_s390_vflndb(__a);
}

/*-- vec_max ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_max(__vector signed char __a, __vector signed char __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_max(__vector signed char __a, __vector __bool char __b) {
  __vector signed char __bc = (__vector signed char)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_max(__vector __bool char __a, __vector signed char __b) {
  __vector signed char __ac = (__vector signed char)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned char
vec_max(__vector unsigned char __a, __vector unsigned char __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_max(__vector unsigned char __a, __vector __bool char __b) {
  __vector unsigned char __bc = (__vector unsigned char)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_max(__vector __bool char __a, __vector unsigned char __b) {
  __vector unsigned char __ac = (__vector unsigned char)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed short
vec_max(__vector signed short __a, __vector signed short __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_max(__vector signed short __a, __vector __bool short __b) {
  __vector signed short __bc = (__vector signed short)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_max(__vector __bool short __a, __vector signed short __b) {
  __vector signed short __ac = (__vector signed short)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned short
vec_max(__vector unsigned short __a, __vector unsigned short __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_max(__vector unsigned short __a, __vector __bool short __b) {
  __vector unsigned short __bc = (__vector unsigned short)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_max(__vector __bool short __a, __vector unsigned short __b) {
  __vector unsigned short __ac = (__vector unsigned short)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed int
vec_max(__vector signed int __a, __vector signed int __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_max(__vector signed int __a, __vector __bool int __b) {
  __vector signed int __bc = (__vector signed int)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_max(__vector __bool int __a, __vector signed int __b) {
  __vector signed int __ac = (__vector signed int)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned int
vec_max(__vector unsigned int __a, __vector unsigned int __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_max(__vector unsigned int __a, __vector __bool int __b) {
  __vector unsigned int __bc = (__vector unsigned int)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_max(__vector __bool int __a, __vector unsigned int __b) {
  __vector unsigned int __ac = (__vector unsigned int)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed long long
vec_max(__vector signed long long __a, __vector signed long long __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_max(__vector signed long long __a, __vector __bool long long __b) {
  __vector signed long long __bc = (__vector signed long long)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_max(__vector __bool long long __a, __vector signed long long __b) {
  __vector signed long long __ac = (__vector signed long long)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_max(__vector unsigned long long __a, __vector unsigned long long __b) {
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_max(__vector unsigned long long __a, __vector __bool long long __b) {
  __vector unsigned long long __bc = (__vector unsigned long long)__b;
  return vec_sel(__bc, __a, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_max(__vector __bool long long __a, __vector unsigned long long __b) {
  __vector unsigned long long __ac = (__vector unsigned long long)__a;
  return vec_sel(__b, __ac, vec_cmpgt(__ac, __b));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_max(__vector float __a, __vector float __b) {
  return __builtin_s390_vfmaxsb(__a, __b, 0);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_max(__vector double __a, __vector double __b) {
#if __ARCH__ >= 12
  return __builtin_s390_vfmaxdb(__a, __b, 0);
#else
  return vec_sel(__b, __a, vec_cmpgt(__a, __b));
#endif
}

/*-- vec_min ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_min(__vector signed char __a, __vector signed char __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_min(__vector signed char __a, __vector __bool char __b) {
  __vector signed char __bc = (__vector signed char)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed char
vec_min(__vector __bool char __a, __vector signed char __b) {
  __vector signed char __ac = (__vector signed char)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned char
vec_min(__vector unsigned char __a, __vector unsigned char __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_min(__vector unsigned char __a, __vector __bool char __b) {
  __vector unsigned char __bc = (__vector unsigned char)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned char
vec_min(__vector __bool char __a, __vector unsigned char __b) {
  __vector unsigned char __ac = (__vector unsigned char)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed short
vec_min(__vector signed short __a, __vector signed short __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_min(__vector signed short __a, __vector __bool short __b) {
  __vector signed short __bc = (__vector signed short)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed short
vec_min(__vector __bool short __a, __vector signed short __b) {
  __vector signed short __ac = (__vector signed short)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned short
vec_min(__vector unsigned short __a, __vector unsigned short __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_min(__vector unsigned short __a, __vector __bool short __b) {
  __vector unsigned short __bc = (__vector unsigned short)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned short
vec_min(__vector __bool short __a, __vector unsigned short __b) {
  __vector unsigned short __ac = (__vector unsigned short)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed int
vec_min(__vector signed int __a, __vector signed int __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_min(__vector signed int __a, __vector __bool int __b) {
  __vector signed int __bc = (__vector signed int)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed int
vec_min(__vector __bool int __a, __vector signed int __b) {
  __vector signed int __ac = (__vector signed int)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned int
vec_min(__vector unsigned int __a, __vector unsigned int __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_min(__vector unsigned int __a, __vector __bool int __b) {
  __vector unsigned int __bc = (__vector unsigned int)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned int
vec_min(__vector __bool int __a, __vector unsigned int __b) {
  __vector unsigned int __ac = (__vector unsigned int)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector signed long long
vec_min(__vector signed long long __a, __vector signed long long __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_min(__vector signed long long __a, __vector __bool long long __b) {
  __vector signed long long __bc = (__vector signed long long)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_min(__vector __bool long long __a, __vector signed long long __b) {
  __vector signed long long __ac = (__vector signed long long)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_min(__vector unsigned long long __a, __vector unsigned long long __b) {
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_min(__vector unsigned long long __a, __vector __bool long long __b) {
  __vector unsigned long long __bc = (__vector unsigned long long)__b;
  return vec_sel(__a, __bc, vec_cmpgt(__a, __bc));
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_min(__vector __bool long long __a, __vector unsigned long long __b) {
  __vector unsigned long long __ac = (__vector unsigned long long)__a;
  return vec_sel(__ac, __b, vec_cmpgt(__ac, __b));
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_min(__vector float __a, __vector float __b) {
  return __builtin_s390_vfminsb(__a, __b, 0);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_min(__vector double __a, __vector double __b) {
#if __ARCH__ >= 12
  return __builtin_s390_vfmindb(__a, __b, 0);
#else
  return vec_sel(__a, __b, vec_cmpgt(__a, __b));
#endif
}

/*-- vec_add_u128 -----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_add_u128(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         ((__int128)__a + (__int128)__b);
}

/*-- vec_addc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_addc(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vaccb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_addc(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vacch(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_addc(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vaccf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_addc(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vaccg(__a, __b);
}

/*-- vec_addc_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_addc_u128(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vaccq((unsigned __int128)__a, (unsigned __int128)__b);
}

/*-- vec_adde_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_adde_u128(__vector unsigned char __a, __vector unsigned char __b,
              __vector unsigned char __c) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vacq((unsigned __int128)__a, (unsigned __int128)__b,
                             (unsigned __int128)__c);
}

/*-- vec_addec_u128 ---------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_addec_u128(__vector unsigned char __a, __vector unsigned char __b,
               __vector unsigned char __c) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vacccq((unsigned __int128)__a, (unsigned __int128)__b,
                               (unsigned __int128)__c);
}

/*-- vec_avg ----------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_avg(__vector signed char __a, __vector signed char __b) {
  return __builtin_s390_vavgb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_avg(__vector signed short __a, __vector signed short __b) {
  return __builtin_s390_vavgh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_avg(__vector signed int __a, __vector signed int __b) {
  return __builtin_s390_vavgf(__a, __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_avg(__vector signed long long __a, __vector signed long long __b) {
  return __builtin_s390_vavgg(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_avg(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vavglb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_avg(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vavglh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_avg(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vavglf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_avg(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vavglg(__a, __b);
}

/*-- vec_checksum -----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned int
vec_checksum(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vcksm(__a, __b);
}

/*-- vec_gfmsum -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned short
vec_gfmsum(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vgfmb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_gfmsum(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vgfmh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_gfmsum(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vgfmf(__a, __b);
}

/*-- vec_gfmsum_128 ---------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_gfmsum_128(__vector unsigned long long __a,
               __vector unsigned long long __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vgfmg(__a, __b);
}

/*-- vec_gfmsum_accum -------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned short
vec_gfmsum_accum(__vector unsigned char __a, __vector unsigned char __b,
                 __vector unsigned short __c) {
  return __builtin_s390_vgfmab(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_gfmsum_accum(__vector unsigned short __a, __vector unsigned short __b,
                 __vector unsigned int __c) {
  return __builtin_s390_vgfmah(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_gfmsum_accum(__vector unsigned int __a, __vector unsigned int __b,
                 __vector unsigned long long __c) {
  return __builtin_s390_vgfmaf(__a, __b, __c);
}

/*-- vec_gfmsum_accum_128 ---------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_gfmsum_accum_128(__vector unsigned long long __a,
                     __vector unsigned long long __b,
                     __vector unsigned char __c) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vgfmag(__a, __b, (unsigned __int128)__c);
}

/*-- vec_mladd --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_mladd(__vector signed char __a, __vector signed char __b,
          __vector signed char __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed char
vec_mladd(__vector unsigned char __a, __vector signed char __b,
          __vector signed char __c) {
  return (__vector signed char)__a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed char
vec_mladd(__vector signed char __a, __vector unsigned char __b,
          __vector unsigned char __c) {
  return __a * (__vector signed char)__b + (__vector signed char)__c;
}

static inline __ATTRS_o_ai __vector unsigned char
vec_mladd(__vector unsigned char __a, __vector unsigned char __b,
          __vector unsigned char __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed short
vec_mladd(__vector signed short __a, __vector signed short __b,
          __vector signed short __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed short
vec_mladd(__vector unsigned short __a, __vector signed short __b,
          __vector signed short __c) {
  return (__vector signed short)__a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed short
vec_mladd(__vector signed short __a, __vector unsigned short __b,
          __vector unsigned short __c) {
  return __a * (__vector signed short)__b + (__vector signed short)__c;
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mladd(__vector unsigned short __a, __vector unsigned short __b,
          __vector unsigned short __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed int
vec_mladd(__vector signed int __a, __vector signed int __b,
          __vector signed int __c) {
  return __a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed int
vec_mladd(__vector unsigned int __a, __vector signed int __b,
          __vector signed int __c) {
  return (__vector signed int)__a * __b + __c;
}

static inline __ATTRS_o_ai __vector signed int
vec_mladd(__vector signed int __a, __vector unsigned int __b,
          __vector unsigned int __c) {
  return __a * (__vector signed int)__b + (__vector signed int)__c;
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mladd(__vector unsigned int __a, __vector unsigned int __b,
          __vector unsigned int __c) {
  return __a * __b + __c;
}

/*-- vec_mhadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_mhadd(__vector signed char __a, __vector signed char __b,
          __vector signed char __c) {
  return __builtin_s390_vmahb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_mhadd(__vector unsigned char __a, __vector unsigned char __b,
          __vector unsigned char __c) {
  return __builtin_s390_vmalhb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed short
vec_mhadd(__vector signed short __a, __vector signed short __b,
          __vector signed short __c) {
  return __builtin_s390_vmahh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mhadd(__vector unsigned short __a, __vector unsigned short __b,
          __vector unsigned short __c) {
  return __builtin_s390_vmalhh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed int
vec_mhadd(__vector signed int __a, __vector signed int __b,
          __vector signed int __c) {
  return __builtin_s390_vmahf(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mhadd(__vector unsigned int __a, __vector unsigned int __b,
          __vector unsigned int __c) {
  return __builtin_s390_vmalhf(__a, __b, __c);
}

/*-- vec_meadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_meadd(__vector signed char __a, __vector signed char __b,
          __vector signed short __c) {
  return __builtin_s390_vmaeb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_meadd(__vector unsigned char __a, __vector unsigned char __b,
          __vector unsigned short __c) {
  return __builtin_s390_vmaleb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed int
vec_meadd(__vector signed short __a, __vector signed short __b,
          __vector signed int __c) {
  return __builtin_s390_vmaeh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_meadd(__vector unsigned short __a, __vector unsigned short __b,
          __vector unsigned int __c) {
  return __builtin_s390_vmaleh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed long long
vec_meadd(__vector signed int __a, __vector signed int __b,
          __vector signed long long __c) {
  return __builtin_s390_vmaef(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_meadd(__vector unsigned int __a, __vector unsigned int __b,
          __vector unsigned long long __c) {
  return __builtin_s390_vmalef(__a, __b, __c);
}

/*-- vec_moadd --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_moadd(__vector signed char __a, __vector signed char __b,
          __vector signed short __c) {
  return __builtin_s390_vmaob(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_moadd(__vector unsigned char __a, __vector unsigned char __b,
          __vector unsigned short __c) {
  return __builtin_s390_vmalob(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed int
vec_moadd(__vector signed short __a, __vector signed short __b,
          __vector signed int __c) {
  return __builtin_s390_vmaoh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_moadd(__vector unsigned short __a, __vector unsigned short __b,
          __vector unsigned int __c) {
  return __builtin_s390_vmaloh(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector signed long long
vec_moadd(__vector signed int __a, __vector signed int __b,
          __vector signed long long __c) {
  return __builtin_s390_vmaof(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_moadd(__vector unsigned int __a, __vector unsigned int __b,
          __vector unsigned long long __c) {
  return __builtin_s390_vmalof(__a, __b, __c);
}

/*-- vec_mulh ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_mulh(__vector signed char __a, __vector signed char __b) {
  return __builtin_s390_vmhb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_mulh(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vmlhb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_mulh(__vector signed short __a, __vector signed short __b) {
  return __builtin_s390_vmhh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mulh(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vmlhh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_mulh(__vector signed int __a, __vector signed int __b) {
  return __builtin_s390_vmhf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mulh(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vmlhf(__a, __b);
}

/*-- vec_mule ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_mule(__vector signed char __a, __vector signed char __b) {
  return __builtin_s390_vmeb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mule(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vmleb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_mule(__vector signed short __a, __vector signed short __b) {
  return __builtin_s390_vmeh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mule(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vmleh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_mule(__vector signed int __a, __vector signed int __b) {
  return __builtin_s390_vmef(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_mule(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vmlef(__a, __b);
}

/*-- vec_mulo ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed short
vec_mulo(__vector signed char __a, __vector signed char __b) {
  return __builtin_s390_vmob(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_mulo(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vmlob(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_mulo(__vector signed short __a, __vector signed short __b) {
  return __builtin_s390_vmoh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_mulo(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vmloh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed long long
vec_mulo(__vector signed int __a, __vector signed int __b) {
  return __builtin_s390_vmof(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_mulo(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vmlof(__a, __b);
}

/*-- vec_msum_u128 ----------------------------------------------------------*/

#if __ARCH__ >= 12
extern __ATTRS_o __vector unsigned char
vec_msum_u128(__vector unsigned long long __a, __vector unsigned long long __b,
              __vector unsigned char __c, int __d)
  __constant_range(__d, 0, 15);

#define vec_msum_u128(X, Y, Z, W) \
  ((__typeof__((vec_msum_u128)((X), (Y), (Z), (W)))) \
   (unsigned __int128 __attribute__((__vector_size__(16)))) \
   __builtin_s390_vmslg((X), (Y), (unsigned __int128)(Z), (W)))
#endif

/*-- vec_sub_u128 -----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_sub_u128(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         ((__int128)__a - (__int128)__b);
}

/*-- vec_subc ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_subc(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vscbib(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_subc(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vscbih(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_subc(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vscbif(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_subc(__vector unsigned long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vscbig(__a, __b);
}

/*-- vec_subc_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_subc_u128(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vscbiq((unsigned __int128)__a, (unsigned __int128)__b);
}

/*-- vec_sube_u128 ----------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_sube_u128(__vector unsigned char __a, __vector unsigned char __b,
              __vector unsigned char __c) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vsbiq((unsigned __int128)__a, (unsigned __int128)__b,
                              (unsigned __int128)__c);
}

/*-- vec_subec_u128 ---------------------------------------------------------*/

static inline __ATTRS_ai __vector unsigned char
vec_subec_u128(__vector unsigned char __a, __vector unsigned char __b,
               __vector unsigned char __c) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vsbcbiq((unsigned __int128)__a, (unsigned __int128)__b,
                                (unsigned __int128)__c);
}

/*-- vec_sum2 ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned long long
vec_sum2(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vsumgh(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned long long
vec_sum2(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vsumgf(__a, __b);
}

/*-- vec_sum_u128 -----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_sum_u128(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vsumqf(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_sum_u128(__vector unsigned long long __a, __vector unsigned long long __b) {
  return (__vector unsigned char)
         (unsigned __int128 __attribute__((__vector_size__(16))))
         __builtin_s390_vsumqg(__a, __b);
}

/*-- vec_sum4 ---------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned int
vec_sum4(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vsumb(__a, __b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_sum4(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vsumh(__a, __b);
}

/*-- vec_test_mask ----------------------------------------------------------*/

static inline __ATTRS_o_ai int
vec_test_mask(__vector signed char __a, __vector unsigned char __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vtm(__a, __b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector signed short __a, __vector unsigned short __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector signed int __a, __vector unsigned int __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector signed long long __a, __vector unsigned long long __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai int
vec_test_mask(__vector unsigned long long __a,
              __vector unsigned long long __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

#if __ARCH__ >= 12
static inline __ATTRS_o_ai int
vec_test_mask(__vector float __a, __vector unsigned int __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}
#endif

static inline __ATTRS_o_ai int
vec_test_mask(__vector double __a, __vector unsigned long long __b) {
  return __builtin_s390_vtm((__vector unsigned char)__a,
                            (__vector unsigned char)__b);
}

/*-- vec_madd ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_madd(__vector float __a, __vector float __b, __vector float __c) {
  return __builtin_s390_vfmasb(__a, __b, __c);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_madd(__vector double __a, __vector double __b, __vector double __c) {
  return __builtin_s390_vfmadb(__a, __b, __c);
}

/*-- vec_msub ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_msub(__vector float __a, __vector float __b, __vector float __c) {
  return __builtin_s390_vfmssb(__a, __b, __c);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_msub(__vector double __a, __vector double __b, __vector double __c) {
  return __builtin_s390_vfmsdb(__a, __b, __c);
}

/*-- vec_nmadd ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_nmadd(__vector float __a, __vector float __b, __vector float __c) {
  return __builtin_s390_vfnmasb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector double
vec_nmadd(__vector double __a, __vector double __b, __vector double __c) {
  return __builtin_s390_vfnmadb(__a, __b, __c);
}
#endif

/*-- vec_nmsub ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_nmsub(__vector float __a, __vector float __b, __vector float __c) {
  return __builtin_s390_vfnmssb(__a, __b, __c);
}

static inline __ATTRS_o_ai __vector double
vec_nmsub(__vector double __a, __vector double __b, __vector double __c) {
  return __builtin_s390_vfnmsdb(__a, __b, __c);
}
#endif

/*-- vec_sqrt ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_sqrt(__vector float __a) {
  return __builtin_s390_vfsqsb(__a);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_sqrt(__vector double __a) {
  return __builtin_s390_vfsqdb(__a);
}

/*-- vec_ld2f ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_ai __vector double
vec_ld2f(const float *__ptr) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  return __builtin_convertvector(*(const __v2f32 *)__ptr, __vector double);
}

/*-- vec_st2f ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_ai void
vec_st2f(__vector double __a, float *__ptr) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  *(__v2f32 *)__ptr = __builtin_convertvector(__a, __v2f32);
}

/*-- vec_ctd ----------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_ctd(__vector signed long long __a, int __b)
  __constant_range(__b, 0, 31) {
  __vector double __conv = __builtin_convertvector(__a, __vector double);
  __conv *= ((__vector double)(__vector unsigned long long)
             ((0x3ffULL - __b) << 52));
  return __conv;
}

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector double
vec_ctd(__vector unsigned long long __a, int __b)
  __constant_range(__b, 0, 31) {
  __vector double __conv = __builtin_convertvector(__a, __vector double);
  __conv *= ((__vector double)(__vector unsigned long long)
             ((0x3ffULL - __b) << 52));
  return __conv;
}

/*-- vec_ctsl ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector signed long long
vec_ctsl(__vector double __a, int __b)
  __constant_range(__b, 0, 31) {
  __a *= ((__vector double)(__vector unsigned long long)
          ((0x3ffULL + __b) << 52));
  return __builtin_convertvector(__a, __vector signed long long);
}

/*-- vec_ctul ---------------------------------------------------------------*/

// This prototype is deprecated.
static inline __ATTRS_o_ai __vector unsigned long long
vec_ctul(__vector double __a, int __b)
  __constant_range(__b, 0, 31) {
  __a *= ((__vector double)(__vector unsigned long long)
          ((0x3ffULL + __b) << 52));
  return __builtin_convertvector(__a, __vector unsigned long long);
}

/*-- vec_doublee ------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai __vector double
vec_doublee(__vector float __a) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  __v2f32 __pack = __builtin_shufflevector(__a, __a, 0, 2);
  return __builtin_convertvector(__pack, __vector double);
}
#endif

/*-- vec_floate -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_ai __vector float
vec_floate(__vector double __a) {
  typedef float __v2f32 __attribute__((__vector_size__(8)));
  __v2f32 __pack = __builtin_convertvector(__a, __v2f32);
  return __builtin_shufflevector(__pack, __pack, 0, -1, 1, -1);
}
#endif

/*-- vec_double -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector double
vec_double(__vector signed long long __a) {
  return __builtin_convertvector(__a, __vector double);
}

static inline __ATTRS_o_ai __vector double
vec_double(__vector unsigned long long __a) {
  return __builtin_convertvector(__a, __vector double);
}

/*-- vec_float --------------------------------------------------------------*/

#if __ARCH__ >= 13

static inline __ATTRS_o_ai __vector float
vec_float(__vector signed int __a) {
  return __builtin_convertvector(__a, __vector float);
}

static inline __ATTRS_o_ai __vector float
vec_float(__vector unsigned int __a) {
  return __builtin_convertvector(__a, __vector float);
}

#endif

/*-- vec_signed -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed long long
vec_signed(__vector double __a) {
  return __builtin_convertvector(__a, __vector signed long long);
}

#if __ARCH__ >= 13
static inline __ATTRS_o_ai __vector signed int
vec_signed(__vector float __a) {
  return __builtin_convertvector(__a, __vector signed int);
}
#endif

/*-- vec_unsigned -----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned long long
vec_unsigned(__vector double __a) {
  return __builtin_convertvector(__a, __vector unsigned long long);
}

#if __ARCH__ >= 13
static inline __ATTRS_o_ai __vector unsigned int
vec_unsigned(__vector float __a) {
  return __builtin_convertvector(__a, __vector unsigned int);
}
#endif

/*-- vec_roundp -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_roundp(__vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 6);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_roundp(__vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 6);
}

/*-- vec_ceil ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_ceil(__vector float __a) {
  // On this platform, vec_ceil never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 6);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_ceil(__vector double __a) {
  // On this platform, vec_ceil never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 6);
}

/*-- vec_roundm -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_roundm(__vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 7);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_roundm(__vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 7);
}

/*-- vec_floor --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_floor(__vector float __a) {
  // On this platform, vec_floor never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 7);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_floor(__vector double __a) {
  // On this platform, vec_floor never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 7);
}

/*-- vec_roundz -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_roundz(__vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 5);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_roundz(__vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 5);
}

/*-- vec_trunc --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_trunc(__vector float __a) {
  // On this platform, vec_trunc never triggers the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 4, 5);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_trunc(__vector double __a) {
  // On this platform, vec_trunc never triggers the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 4, 5);
}

/*-- vec_roundc -------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_roundc(__vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 0);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_roundc(__vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 0);
}

/*-- vec_rint ---------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_rint(__vector float __a) {
  // vec_rint may trigger the IEEE-inexact exception.
  return __builtin_s390_vfisb(__a, 0, 0);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_rint(__vector double __a) {
  // vec_rint may trigger the IEEE-inexact exception.
  return __builtin_s390_vfidb(__a, 0, 0);
}

/*-- vec_round --------------------------------------------------------------*/

#if __ARCH__ >= 12
static inline __ATTRS_o_ai __vector float
vec_round(__vector float __a) {
  return __builtin_s390_vfisb(__a, 4, 4);
}
#endif

static inline __ATTRS_o_ai __vector double
vec_round(__vector double __a) {
  return __builtin_s390_vfidb(__a, 4, 4);
}

/*-- vec_fp_test_data_class -------------------------------------------------*/

#if __ARCH__ >= 12
extern __ATTRS_o __vector __bool int
vec_fp_test_data_class(__vector float __a, int __b, int *__c)
  __constant_range(__b, 0, 4095);

extern __ATTRS_o __vector __bool long long
vec_fp_test_data_class(__vector double __a, int __b, int *__c)
  __constant_range(__b, 0, 4095);

#define vec_fp_test_data_class(X, Y, Z) \
  ((__typeof__((vec_fp_test_data_class)((X), (Y), (Z)))) \
   __extension__ ({ \
     __vector unsigned char __res; \
     __vector unsigned char __x = (__vector unsigned char)(X); \
     int *__z = (Z); \
     switch (sizeof ((X)[0])) { \
     case 4:  __res = (__vector unsigned char) \
                      __builtin_s390_vftcisb((__vector float)__x, (Y), __z); \
              break; \
     default: __res = (__vector unsigned char) \
                      __builtin_s390_vftcidb((__vector double)__x, (Y), __z); \
              break; \
     } __res; }))
#else
#define vec_fp_test_data_class(X, Y, Z) \
  ((__vector __bool long long)__builtin_s390_vftcidb((X), (Y), (Z)))
#endif

#define __VEC_CLASS_FP_ZERO_P (1 << 11)
#define __VEC_CLASS_FP_ZERO_N (1 << 10)
#define __VEC_CLASS_FP_ZERO (__VEC_CLASS_FP_ZERO_P | __VEC_CLASS_FP_ZERO_N)
#define __VEC_CLASS_FP_NORMAL_P (1 << 9)
#define __VEC_CLASS_FP_NORMAL_N (1 << 8)
#define __VEC_CLASS_FP_NORMAL (__VEC_CLASS_FP_NORMAL_P | \
                               __VEC_CLASS_FP_NORMAL_N)
#define __VEC_CLASS_FP_SUBNORMAL_P (1 << 7)
#define __VEC_CLASS_FP_SUBNORMAL_N (1 << 6)
#define __VEC_CLASS_FP_SUBNORMAL (__VEC_CLASS_FP_SUBNORMAL_P | \
                                  __VEC_CLASS_FP_SUBNORMAL_N)
#define __VEC_CLASS_FP_INFINITY_P (1 << 5)
#define __VEC_CLASS_FP_INFINITY_N (1 << 4)
#define __VEC_CLASS_FP_INFINITY (__VEC_CLASS_FP_INFINITY_P | \
                                 __VEC_CLASS_FP_INFINITY_N)
#define __VEC_CLASS_FP_QNAN_P (1 << 3)
#define __VEC_CLASS_FP_QNAN_N (1 << 2)
#define __VEC_CLASS_FP_QNAN (__VEC_CLASS_FP_QNAN_P | __VEC_CLASS_FP_QNAN_N)
#define __VEC_CLASS_FP_SNAN_P (1 << 1)
#define __VEC_CLASS_FP_SNAN_N (1 << 0)
#define __VEC_CLASS_FP_SNAN (__VEC_CLASS_FP_SNAN_P | __VEC_CLASS_FP_SNAN_N)
#define __VEC_CLASS_FP_NAN (__VEC_CLASS_FP_QNAN | __VEC_CLASS_FP_SNAN)
#define __VEC_CLASS_FP_NOT_NORMAL (__VEC_CLASS_FP_NAN | \
                                   __VEC_CLASS_FP_SUBNORMAL | \
                                   __VEC_CLASS_FP_ZERO | \
                                   __VEC_CLASS_FP_INFINITY)

/*-- vec_extend_to_fp32_hi --------------------------------------------------*/

#if __ARCH__ >= 14
#define vec_extend_to_fp32_hi(X, W) \
  ((__vector float)__builtin_s390_vclfnhs((X), (W)));
#endif

/*-- vec_extend_to_fp32_hi --------------------------------------------------*/

#if __ARCH__ >= 14
#define vec_extend_to_fp32_lo(X, W) \
  ((__vector float)__builtin_s390_vclfnls((X), (W)));
#endif

/*-- vec_round_from_fp32 ----------------------------------------------------*/

#if __ARCH__ >= 14
#define vec_round_from_fp32(X, Y, W) \
  ((__vector unsigned short)__builtin_s390_vcrnfs((X), (Y), (W)));
#endif

/*-- vec_convert_to_fp16 ----------------------------------------------------*/

#if __ARCH__ >= 14
#define vec_convert_to_fp16(X, W) \
  ((__vector unsigned short)__builtin_s390_vcfn((X), (W)));
#endif

/*-- vec_convert_from_fp16 --------------------------------------------------*/

#if __ARCH__ >= 14
#define vec_convert_from_fp16(X, W) \
  ((__vector unsigned short)__builtin_s390_vcnf((X), (W)));
#endif

/*-- vec_cp_until_zero ------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cp_until_zero(__vector signed char __a) {
  return ((__vector signed char)
          __builtin_s390_vistrb((__vector unsigned char)__a));
}

static inline __ATTRS_o_ai __vector __bool char
vec_cp_until_zero(__vector __bool char __a) {
  return ((__vector __bool char)
          __builtin_s390_vistrb((__vector unsigned char)__a));
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cp_until_zero(__vector unsigned char __a) {
  return __builtin_s390_vistrb(__a);
}

static inline __ATTRS_o_ai __vector signed short
vec_cp_until_zero(__vector signed short __a) {
  return ((__vector signed short)
          __builtin_s390_vistrh((__vector unsigned short)__a));
}

static inline __ATTRS_o_ai __vector __bool short
vec_cp_until_zero(__vector __bool short __a) {
  return ((__vector __bool short)
          __builtin_s390_vistrh((__vector unsigned short)__a));
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cp_until_zero(__vector unsigned short __a) {
  return __builtin_s390_vistrh(__a);
}

static inline __ATTRS_o_ai __vector signed int
vec_cp_until_zero(__vector signed int __a) {
  return ((__vector signed int)
          __builtin_s390_vistrf((__vector unsigned int)__a));
}

static inline __ATTRS_o_ai __vector __bool int
vec_cp_until_zero(__vector __bool int __a) {
  return ((__vector __bool int)
          __builtin_s390_vistrf((__vector unsigned int)__a));
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cp_until_zero(__vector unsigned int __a) {
  return __builtin_s390_vistrf(__a);
}

/*-- vec_cp_until_zero_cc ---------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cp_until_zero_cc(__vector signed char __a, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vistrbs((__vector unsigned char)__a, __cc);
}

static inline __ATTRS_o_ai __vector __bool char
vec_cp_until_zero_cc(__vector __bool char __a, int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vistrbs((__vector unsigned char)__a, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cp_until_zero_cc(__vector unsigned char __a, int *__cc) {
  return __builtin_s390_vistrbs(__a, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_cp_until_zero_cc(__vector signed short __a, int *__cc) {
  return (__vector signed short)
    __builtin_s390_vistrhs((__vector unsigned short)__a, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cp_until_zero_cc(__vector __bool short __a, int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vistrhs((__vector unsigned short)__a, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cp_until_zero_cc(__vector unsigned short __a, int *__cc) {
  return __builtin_s390_vistrhs(__a, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_cp_until_zero_cc(__vector signed int __a, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vistrfs((__vector unsigned int)__a, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cp_until_zero_cc(__vector __bool int __a, int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vistrfs((__vector unsigned int)__a, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cp_until_zero_cc(__vector unsigned int __a, int *__cc) {
  return __builtin_s390_vistrfs(__a, __cc);
}

/*-- vec_cmpeq_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpeq_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfeeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfeeb((__vector unsigned char)__a,
                              (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfeeb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpeq_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfeeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfeeh((__vector unsigned short)__a,
                              (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfeeh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpeq_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfeef((__vector unsigned int)__a,
                         (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfeef((__vector unsigned int)__a,
                              (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfeef(__a, __b);
}

/*-- vec_cmpeq_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpeq_idx_cc(__vector signed char __a, __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfeebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_idx_cc(__vector __bool char __a, __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfeebs((__vector unsigned char)__a,
                               (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                 int *__cc) {
  return __builtin_s390_vfeebs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpeq_idx_cc(__vector signed short __a, __vector signed short __b,
                 int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfeehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_idx_cc(__vector __bool short __a, __vector __bool short __b, int *__cc) {
  return __builtin_s390_vfeehs((__vector unsigned short)__a,
                               (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                 int *__cc) {
  return __builtin_s390_vfeehs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpeq_idx_cc(__vector signed int __a, __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfeefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_idx_cc(__vector __bool int __a, __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfeefs((__vector unsigned int)__a,
                               (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                 int *__cc) {
  return __builtin_s390_vfeefs(__a, __b, __cc);
}

/*-- vec_cmpeq_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpeq_or_0_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfeezb((__vector unsigned char)__a,
                          (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_or_0_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfeezb((__vector unsigned char)__a,
                               (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_or_0_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfeezb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpeq_or_0_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfeezh((__vector unsigned short)__a,
                          (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_or_0_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfeezh((__vector unsigned short)__a,
                               (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_or_0_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfeezh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpeq_or_0_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfeezf((__vector unsigned int)__a,
                          (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_or_0_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfeezf((__vector unsigned int)__a,
                               (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_or_0_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfeezf(__a, __b);
}

/*-- vec_cmpeq_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpeq_or_0_idx_cc(__vector signed char __a, __vector signed char __b,
                      int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfeezbs((__vector unsigned char)__a,
                           (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_or_0_idx_cc(__vector __bool char __a, __vector __bool char __b,
                      int *__cc) {
  return __builtin_s390_vfeezbs((__vector unsigned char)__a,
                                (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpeq_or_0_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                      int *__cc) {
  return __builtin_s390_vfeezbs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpeq_or_0_idx_cc(__vector signed short __a, __vector signed short __b,
                      int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfeezhs((__vector unsigned short)__a,
                           (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_or_0_idx_cc(__vector __bool short __a, __vector __bool short __b,
                      int *__cc) {
  return __builtin_s390_vfeezhs((__vector unsigned short)__a,
                                (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpeq_or_0_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                      int *__cc) {
  return __builtin_s390_vfeezhs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpeq_or_0_idx_cc(__vector signed int __a, __vector signed int __b,
                      int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfeezfs((__vector unsigned int)__a,
                           (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_or_0_idx_cc(__vector __bool int __a, __vector __bool int __b,
                      int *__cc) {
  return __builtin_s390_vfeezfs((__vector unsigned int)__a,
                                (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpeq_or_0_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                      int *__cc) {
  return __builtin_s390_vfeezfs(__a, __b, __cc);
}

/*-- vec_cmpne_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpne_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfeneb((__vector unsigned char)__a,
                          (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfeneb((__vector unsigned char)__a,
                               (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfeneb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpne_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfeneh((__vector unsigned short)__a,
                          (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfeneh((__vector unsigned short)__a,
                               (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfeneh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpne_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfenef((__vector unsigned int)__a,
                          (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfenef((__vector unsigned int)__a,
                               (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfenef(__a, __b);
}

/*-- vec_cmpne_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpne_idx_cc(__vector signed char __a, __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfenebs((__vector unsigned char)__a,
                           (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_idx_cc(__vector __bool char __a, __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfenebs((__vector unsigned char)__a,
                                (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                 int *__cc) {
  return __builtin_s390_vfenebs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpne_idx_cc(__vector signed short __a, __vector signed short __b,
                 int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfenehs((__vector unsigned short)__a,
                           (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_idx_cc(__vector __bool short __a, __vector __bool short __b,
                 int *__cc) {
  return __builtin_s390_vfenehs((__vector unsigned short)__a,
                                (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                 int *__cc) {
  return __builtin_s390_vfenehs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpne_idx_cc(__vector signed int __a, __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfenefs((__vector unsigned int)__a,
                           (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_idx_cc(__vector __bool int __a, __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfenefs((__vector unsigned int)__a,
                                (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                 int *__cc) {
  return __builtin_s390_vfenefs(__a, __b, __cc);
}

/*-- vec_cmpne_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpne_or_0_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfenezb((__vector unsigned char)__a,
                           (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_or_0_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfenezb((__vector unsigned char)__a,
                                (__vector unsigned char)__b);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_or_0_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfenezb(__a, __b);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpne_or_0_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfenezh((__vector unsigned short)__a,
                           (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_or_0_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfenezh((__vector unsigned short)__a,
                                (__vector unsigned short)__b);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_or_0_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfenezh(__a, __b);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpne_or_0_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfenezf((__vector unsigned int)__a,
                           (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_or_0_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfenezf((__vector unsigned int)__a,
                                (__vector unsigned int)__b);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_or_0_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfenezf(__a, __b);
}

/*-- vec_cmpne_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_cmpne_or_0_idx_cc(__vector signed char __a, __vector signed char __b,
                      int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfenezbs((__vector unsigned char)__a,
                            (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_or_0_idx_cc(__vector __bool char __a, __vector __bool char __b,
                      int *__cc) {
  return __builtin_s390_vfenezbs((__vector unsigned char)__a,
                                 (__vector unsigned char)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpne_or_0_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                      int *__cc) {
  return __builtin_s390_vfenezbs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_cmpne_or_0_idx_cc(__vector signed short __a, __vector signed short __b,
                      int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfenezhs((__vector unsigned short)__a,
                            (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_or_0_idx_cc(__vector __bool short __a, __vector __bool short __b,
                      int *__cc) {
  return __builtin_s390_vfenezhs((__vector unsigned short)__a,
                                 (__vector unsigned short)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpne_or_0_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                      int *__cc) {
  return __builtin_s390_vfenezhs(__a, __b, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_cmpne_or_0_idx_cc(__vector signed int __a, __vector signed int __b,
                      int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfenezfs((__vector unsigned int)__a,
                            (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_or_0_idx_cc(__vector __bool int __a, __vector __bool int __b,
                      int *__cc) {
  return __builtin_s390_vfenezfs((__vector unsigned int)__a,
                                 (__vector unsigned int)__b, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpne_or_0_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                      int *__cc) {
  return __builtin_s390_vfenezfs(__a, __b, __cc);
}

/*-- vec_cmprg --------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmprg(__vector unsigned char __a, __vector unsigned char __b,
          __vector unsigned char __c) {
  return (__vector __bool char)__builtin_s390_vstrcb(__a, __b, __c, 4);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmprg(__vector unsigned short __a, __vector unsigned short __b,
          __vector unsigned short __c) {
  return (__vector __bool short)__builtin_s390_vstrch(__a, __b, __c, 4);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmprg(__vector unsigned int __a, __vector unsigned int __b,
          __vector unsigned int __c) {
  return (__vector __bool int)__builtin_s390_vstrcf(__a, __b, __c, 4);
}

/*-- vec_cmprg_cc -----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmprg_cc(__vector unsigned char __a, __vector unsigned char __b,
             __vector unsigned char __c, int *__cc) {
  return (__vector __bool char)__builtin_s390_vstrcbs(__a, __b, __c, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmprg_cc(__vector unsigned short __a, __vector unsigned short __b,
             __vector unsigned short __c, int *__cc) {
  return (__vector __bool short)__builtin_s390_vstrchs(__a, __b, __c, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmprg_cc(__vector unsigned int __a, __vector unsigned int __b,
             __vector unsigned int __c, int *__cc) {
  return (__vector __bool int)__builtin_s390_vstrcfs(__a, __b, __c, 4, __cc);
}

/*-- vec_cmprg_idx ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmprg_idx(__vector unsigned char __a, __vector unsigned char __b,
              __vector unsigned char __c) {
  return __builtin_s390_vstrcb(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmprg_idx(__vector unsigned short __a, __vector unsigned short __b,
              __vector unsigned short __c) {
  return __builtin_s390_vstrch(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmprg_idx(__vector unsigned int __a, __vector unsigned int __b,
              __vector unsigned int __c) {
  return __builtin_s390_vstrcf(__a, __b, __c, 0);
}

/*-- vec_cmprg_idx_cc -------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmprg_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                 __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrcbs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmprg_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                 __vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrchs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmprg_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                 __vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrcfs(__a, __b, __c, 0, __cc);
}

/*-- vec_cmprg_or_0_idx -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmprg_or_0_idx(__vector unsigned char __a, __vector unsigned char __b,
                   __vector unsigned char __c) {
  return __builtin_s390_vstrczb(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmprg_or_0_idx(__vector unsigned short __a, __vector unsigned short __b,
                   __vector unsigned short __c) {
  return __builtin_s390_vstrczh(__a, __b, __c, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmprg_or_0_idx(__vector unsigned int __a, __vector unsigned int __b,
                   __vector unsigned int __c) {
  return __builtin_s390_vstrczf(__a, __b, __c, 0);
}

/*-- vec_cmprg_or_0_idx_cc --------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmprg_or_0_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                      __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrczbs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmprg_or_0_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                      __vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrczhs(__a, __b, __c, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmprg_or_0_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                      __vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrczfs(__a, __b, __c, 0, __cc);
}

/*-- vec_cmpnrg -------------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmpnrg(__vector unsigned char __a, __vector unsigned char __b,
           __vector unsigned char __c) {
  return (__vector __bool char)__builtin_s390_vstrcb(__a, __b, __c, 12);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpnrg(__vector unsigned short __a, __vector unsigned short __b,
           __vector unsigned short __c) {
  return (__vector __bool short)__builtin_s390_vstrch(__a, __b, __c, 12);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpnrg(__vector unsigned int __a, __vector unsigned int __b,
           __vector unsigned int __c) {
  return (__vector __bool int)__builtin_s390_vstrcf(__a, __b, __c, 12);
}

/*-- vec_cmpnrg_cc ----------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_cmpnrg_cc(__vector unsigned char __a, __vector unsigned char __b,
              __vector unsigned char __c, int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vstrcbs(__a, __b, __c, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_cmpnrg_cc(__vector unsigned short __a, __vector unsigned short __b,
              __vector unsigned short __c, int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vstrchs(__a, __b, __c, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_cmpnrg_cc(__vector unsigned int __a, __vector unsigned int __b,
              __vector unsigned int __c, int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vstrcfs(__a, __b, __c, 12, __cc);
}

/*-- vec_cmpnrg_idx ---------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpnrg_idx(__vector unsigned char __a, __vector unsigned char __b,
               __vector unsigned char __c) {
  return __builtin_s390_vstrcb(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpnrg_idx(__vector unsigned short __a, __vector unsigned short __b,
               __vector unsigned short __c) {
  return __builtin_s390_vstrch(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpnrg_idx(__vector unsigned int __a, __vector unsigned int __b,
               __vector unsigned int __c) {
  return __builtin_s390_vstrcf(__a, __b, __c, 8);
}

/*-- vec_cmpnrg_idx_cc ------------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpnrg_idx_cc(__vector unsigned char __a, __vector unsigned char __b,
                  __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrcbs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpnrg_idx_cc(__vector unsigned short __a, __vector unsigned short __b,
                  __vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrchs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpnrg_idx_cc(__vector unsigned int __a, __vector unsigned int __b,
                  __vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrcfs(__a, __b, __c, 8, __cc);
}

/*-- vec_cmpnrg_or_0_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpnrg_or_0_idx(__vector unsigned char __a, __vector unsigned char __b,
                    __vector unsigned char __c) {
  return __builtin_s390_vstrczb(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpnrg_or_0_idx(__vector unsigned short __a, __vector unsigned short __b,
                    __vector unsigned short __c) {
  return __builtin_s390_vstrczh(__a, __b, __c, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpnrg_or_0_idx(__vector unsigned int __a, __vector unsigned int __b,
                    __vector unsigned int __c) {
  return __builtin_s390_vstrczf(__a, __b, __c, 8);
}

/*-- vec_cmpnrg_or_0_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai __vector unsigned char
vec_cmpnrg_or_0_idx_cc(__vector unsigned char __a,
                       __vector unsigned char __b,
                       __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrczbs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_cmpnrg_or_0_idx_cc(__vector unsigned short __a,
                       __vector unsigned short __b,
                       __vector unsigned short __c, int *__cc) {
  return __builtin_s390_vstrczhs(__a, __b, __c, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_cmpnrg_or_0_idx_cc(__vector unsigned int __a,
                       __vector unsigned int __b,
                       __vector unsigned int __c, int *__cc) {
  return __builtin_s390_vstrczfs(__a, __b, __c, 8, __cc);
}

/*-- vec_find_any_eq --------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq(__vector __bool char __a, __vector __bool char __b) {
  return (__vector __bool char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)__builtin_s390_vfaeb(__a, __b, 4);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq(__vector __bool short __a, __vector __bool short __b) {
  return (__vector __bool short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)__builtin_s390_vfaeh(__a, __b, 4);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq(__vector __bool int __a, __vector __bool int __b) {
  return (__vector __bool int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 4);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)__builtin_s390_vfaef(__a, __b, 4);
}

/*-- vec_find_any_eq_cc -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq_cc(__vector signed char __a, __vector signed char __b,
                   int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq_cc(__vector __bool char __a, __vector __bool char __b,
                   int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_eq_cc(__vector unsigned char __a, __vector unsigned char __b,
                   int *__cc) {
  return (__vector __bool char)__builtin_s390_vfaebs(__a, __b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq_cc(__vector signed short __a, __vector signed short __b,
                   int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq_cc(__vector __bool short __a, __vector __bool short __b,
                   int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_eq_cc(__vector unsigned short __a, __vector unsigned short __b,
                   int *__cc) {
  return (__vector __bool short)__builtin_s390_vfaehs(__a, __b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq_cc(__vector signed int __a, __vector signed int __b,
                   int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq_cc(__vector __bool int __a, __vector __bool int __b,
                   int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 4, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_eq_cc(__vector unsigned int __a, __vector unsigned int __b,
                   int *__cc) {
  return (__vector __bool int)__builtin_s390_vfaefs(__a, __b, 4, __cc);
}

/*-- vec_find_any_eq_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_eq_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfaeb((__vector unsigned char)__a,
                              (__vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfaeb(__a, __b, 0);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_eq_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfaeh((__vector unsigned short)__a,
                              (__vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfaeh(__a, __b, 0);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_eq_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfaef((__vector unsigned int)__a,
                              (__vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfaef(__a, __b, 0);
}

/*-- vec_find_any_eq_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_eq_idx_cc(__vector signed char __a,
                       __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_idx_cc(__vector __bool char __a,
                       __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfaebs((__vector unsigned char)__a,
                               (__vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_idx_cc(__vector unsigned char __a,
                       __vector unsigned char __b, int *__cc) {
  return __builtin_s390_vfaebs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_eq_idx_cc(__vector signed short __a,
                       __vector signed short __b, int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_idx_cc(__vector __bool short __a,
                       __vector __bool short __b, int *__cc) {
  return __builtin_s390_vfaehs((__vector unsigned short)__a,
                               (__vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_idx_cc(__vector unsigned short __a,
                       __vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaehs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_eq_idx_cc(__vector signed int __a,
                       __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_idx_cc(__vector __bool int __a,
                       __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfaefs((__vector unsigned int)__a,
                               (__vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_idx_cc(__vector unsigned int __a,
                       __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfaefs(__a, __b, 0, __cc);
}

/*-- vec_find_any_eq_or_0_idx -----------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_eq_or_0_idx(__vector signed char __a,
                         __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfaezb((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_or_0_idx(__vector __bool char __a,
                         __vector __bool char __b) {
  return __builtin_s390_vfaezb((__vector unsigned char)__a,
                               (__vector unsigned char)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_or_0_idx(__vector unsigned char __a,
                         __vector unsigned char __b) {
  return __builtin_s390_vfaezb(__a, __b, 0);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_eq_or_0_idx(__vector signed short __a,
                         __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfaezh((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_or_0_idx(__vector __bool short __a,
                         __vector __bool short __b) {
  return __builtin_s390_vfaezh((__vector unsigned short)__a,
                               (__vector unsigned short)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_or_0_idx(__vector unsigned short __a,
                         __vector unsigned short __b) {
  return __builtin_s390_vfaezh(__a, __b, 0);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_eq_or_0_idx(__vector signed int __a,
                         __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfaezf((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_or_0_idx(__vector __bool int __a,
                         __vector __bool int __b) {
  return __builtin_s390_vfaezf((__vector unsigned int)__a,
                               (__vector unsigned int)__b, 0);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_or_0_idx(__vector unsigned int __a,
                         __vector unsigned int __b) {
  return __builtin_s390_vfaezf(__a, __b, 0);
}

/*-- vec_find_any_eq_or_0_idx_cc --------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_eq_or_0_idx_cc(__vector signed char __a,
                            __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfaezbs((__vector unsigned char)__a,
                           (__vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_or_0_idx_cc(__vector __bool char __a,
                            __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfaezbs((__vector unsigned char)__a,
                                (__vector unsigned char)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_eq_or_0_idx_cc(__vector unsigned char __a,
                            __vector unsigned char __b, int *__cc) {
  return __builtin_s390_vfaezbs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_eq_or_0_idx_cc(__vector signed short __a,
                            __vector signed short __b, int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfaezhs((__vector unsigned short)__a,
                           (__vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_or_0_idx_cc(__vector __bool short __a,
                            __vector __bool short __b, int *__cc) {
  return __builtin_s390_vfaezhs((__vector unsigned short)__a,
                                (__vector unsigned short)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_eq_or_0_idx_cc(__vector unsigned short __a,
                            __vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaezhs(__a, __b, 0, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_eq_or_0_idx_cc(__vector signed int __a,
                            __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfaezfs((__vector unsigned int)__a,
                           (__vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_or_0_idx_cc(__vector __bool int __a,
                            __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfaezfs((__vector unsigned int)__a,
                                (__vector unsigned int)__b, 0, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_eq_or_0_idx_cc(__vector unsigned int __a,
                            __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfaezfs(__a, __b, 0, __cc);
}

/*-- vec_find_any_ne --------------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne(__vector signed char __a, __vector signed char __b) {
  return (__vector __bool char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne(__vector __bool char __a, __vector __bool char __b) {
  return (__vector __bool char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne(__vector unsigned char __a, __vector unsigned char __b) {
  return (__vector __bool char)__builtin_s390_vfaeb(__a, __b, 12);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne(__vector signed short __a, __vector signed short __b) {
  return (__vector __bool short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne(__vector __bool short __a, __vector __bool short __b) {
  return (__vector __bool short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne(__vector unsigned short __a, __vector unsigned short __b) {
  return (__vector __bool short)__builtin_s390_vfaeh(__a, __b, 12);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne(__vector signed int __a, __vector signed int __b) {
  return (__vector __bool int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne(__vector __bool int __a, __vector __bool int __b) {
  return (__vector __bool int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 12);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne(__vector unsigned int __a, __vector unsigned int __b) {
  return (__vector __bool int)__builtin_s390_vfaef(__a, __b, 12);
}

/*-- vec_find_any_ne_cc -----------------------------------------------------*/

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne_cc(__vector signed char __a,
                   __vector signed char __b, int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne_cc(__vector __bool char __a,
                   __vector __bool char __b, int *__cc) {
  return (__vector __bool char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool char
vec_find_any_ne_cc(__vector unsigned char __a,
                   __vector unsigned char __b, int *__cc) {
  return (__vector __bool char)__builtin_s390_vfaebs(__a, __b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne_cc(__vector signed short __a,
                   __vector signed short __b, int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne_cc(__vector __bool short __a,
                   __vector __bool short __b, int *__cc) {
  return (__vector __bool short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool short
vec_find_any_ne_cc(__vector unsigned short __a,
                   __vector unsigned short __b, int *__cc) {
  return (__vector __bool short)__builtin_s390_vfaehs(__a, __b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne_cc(__vector signed int __a,
                   __vector signed int __b, int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne_cc(__vector __bool int __a,
                   __vector __bool int __b, int *__cc) {
  return (__vector __bool int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 12, __cc);
}

static inline __ATTRS_o_ai __vector __bool int
vec_find_any_ne_cc(__vector unsigned int __a,
                   __vector unsigned int __b, int *__cc) {
  return (__vector __bool int)__builtin_s390_vfaefs(__a, __b, 12, __cc);
}

/*-- vec_find_any_ne_idx ----------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_ne_idx(__vector signed char __a, __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfaeb((__vector unsigned char)__a,
                         (__vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_idx(__vector __bool char __a, __vector __bool char __b) {
  return __builtin_s390_vfaeb((__vector unsigned char)__a,
                              (__vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_idx(__vector unsigned char __a, __vector unsigned char __b) {
  return __builtin_s390_vfaeb(__a, __b, 8);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_ne_idx(__vector signed short __a, __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfaeh((__vector unsigned short)__a,
                         (__vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_idx(__vector __bool short __a, __vector __bool short __b) {
  return __builtin_s390_vfaeh((__vector unsigned short)__a,
                              (__vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_idx(__vector unsigned short __a, __vector unsigned short __b) {
  return __builtin_s390_vfaeh(__a, __b, 8);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_ne_idx(__vector signed int __a, __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfaef((__vector unsigned int)__a,
                         (__vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_idx(__vector __bool int __a, __vector __bool int __b) {
  return __builtin_s390_vfaef((__vector unsigned int)__a,
                              (__vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_idx(__vector unsigned int __a, __vector unsigned int __b) {
  return __builtin_s390_vfaef(__a, __b, 8);
}

/*-- vec_find_any_ne_idx_cc -------------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_ne_idx_cc(__vector signed char __a,
                       __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfaebs((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_idx_cc(__vector __bool char __a,
                       __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfaebs((__vector unsigned char)__a,
                               (__vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_idx_cc(__vector unsigned char __a,
                       __vector unsigned char __b,
                       int *__cc) {
  return __builtin_s390_vfaebs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_ne_idx_cc(__vector signed short __a,
                       __vector signed short __b, int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfaehs((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_idx_cc(__vector __bool short __a,
                       __vector __bool short __b, int *__cc) {
  return __builtin_s390_vfaehs((__vector unsigned short)__a,
                               (__vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_idx_cc(__vector unsigned short __a,
                       __vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaehs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_ne_idx_cc(__vector signed int __a,
                       __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfaefs((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_idx_cc(__vector __bool int __a,
                       __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfaefs((__vector unsigned int)__a,
                               (__vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_idx_cc(__vector unsigned int __a,
                       __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfaefs(__a, __b, 8, __cc);
}

/*-- vec_find_any_ne_or_0_idx -----------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_ne_or_0_idx(__vector signed char __a,
                         __vector signed char __b) {
  return (__vector signed char)
    __builtin_s390_vfaezb((__vector unsigned char)__a,
                          (__vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_or_0_idx(__vector __bool char __a,
                         __vector __bool char __b) {
  return __builtin_s390_vfaezb((__vector unsigned char)__a,
                               (__vector unsigned char)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_or_0_idx(__vector unsigned char __a,
                         __vector unsigned char __b) {
  return __builtin_s390_vfaezb(__a, __b, 8);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_ne_or_0_idx(__vector signed short __a,
                         __vector signed short __b) {
  return (__vector signed short)
    __builtin_s390_vfaezh((__vector unsigned short)__a,
                          (__vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_or_0_idx(__vector __bool short __a,
                         __vector __bool short __b) {
  return __builtin_s390_vfaezh((__vector unsigned short)__a,
                               (__vector unsigned short)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_or_0_idx(__vector unsigned short __a,
                         __vector unsigned short __b) {
  return __builtin_s390_vfaezh(__a, __b, 8);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_ne_or_0_idx(__vector signed int __a,
                         __vector signed int __b) {
  return (__vector signed int)
    __builtin_s390_vfaezf((__vector unsigned int)__a,
                          (__vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_or_0_idx(__vector __bool int __a,
                         __vector __bool int __b) {
  return __builtin_s390_vfaezf((__vector unsigned int)__a,
                               (__vector unsigned int)__b, 8);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_or_0_idx(__vector unsigned int __a,
                         __vector unsigned int __b) {
  return __builtin_s390_vfaezf(__a, __b, 8);
}

/*-- vec_find_any_ne_or_0_idx_cc --------------------------------------------*/

static inline __ATTRS_o_ai __vector signed char
vec_find_any_ne_or_0_idx_cc(__vector signed char __a,
                            __vector signed char __b, int *__cc) {
  return (__vector signed char)
    __builtin_s390_vfaezbs((__vector unsigned char)__a,
                           (__vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_or_0_idx_cc(__vector __bool char __a,
                            __vector __bool char __b, int *__cc) {
  return __builtin_s390_vfaezbs((__vector unsigned char)__a,
                                (__vector unsigned char)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_find_any_ne_or_0_idx_cc(__vector unsigned char __a,
                            __vector unsigned char __b, int *__cc) {
  return __builtin_s390_vfaezbs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai __vector signed short
vec_find_any_ne_or_0_idx_cc(__vector signed short __a,
                            __vector signed short __b, int *__cc) {
  return (__vector signed short)
    __builtin_s390_vfaezhs((__vector unsigned short)__a,
                           (__vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_or_0_idx_cc(__vector __bool short __a,
                            __vector __bool short __b, int *__cc) {
  return __builtin_s390_vfaezhs((__vector unsigned short)__a,
                                (__vector unsigned short)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned short
vec_find_any_ne_or_0_idx_cc(__vector unsigned short __a,
                            __vector unsigned short __b, int *__cc) {
  return __builtin_s390_vfaezhs(__a, __b, 8, __cc);
}

static inline __ATTRS_o_ai __vector signed int
vec_find_any_ne_or_0_idx_cc(__vector signed int __a,
                            __vector signed int __b, int *__cc) {
  return (__vector signed int)
    __builtin_s390_vfaezfs((__vector unsigned int)__a,
                           (__vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_or_0_idx_cc(__vector __bool int __a,
                            __vector __bool int __b, int *__cc) {
  return __builtin_s390_vfaezfs((__vector unsigned int)__a,
                                (__vector unsigned int)__b, 8, __cc);
}

static inline __ATTRS_o_ai __vector unsigned int
vec_find_any_ne_or_0_idx_cc(__vector unsigned int __a,
                            __vector unsigned int __b, int *__cc) {
  return __builtin_s390_vfaezfs(__a, __b, 8, __cc);
}

/*-- vec_search_string_cc ---------------------------------------------------*/

#if __ARCH__ >= 13

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector signed char __a, __vector signed char __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsb((__vector unsigned char)__a,
                               (__vector unsigned char)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector __bool char __a, __vector __bool char __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsb((__vector unsigned char)__a,
                               (__vector unsigned char)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector unsigned char __a, __vector unsigned char __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsb(__a, __b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector signed short __a, __vector signed short __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsh((__vector unsigned short)__a,
                               (__vector unsigned short)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector __bool short __a, __vector __bool short __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsh((__vector unsigned short)__a,
                               (__vector unsigned short)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector unsigned short __a, __vector unsigned short __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsh(__a, __b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector signed int __a, __vector signed int __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsf((__vector unsigned int)__a,
                               (__vector unsigned int)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector __bool int __a, __vector __bool int __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsf((__vector unsigned int)__a,
                               (__vector unsigned int)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_cc(__vector unsigned int __a, __vector unsigned int __b,
                     __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrsf(__a, __b, __c, __cc);
}

#endif

/*-- vec_search_string_until_zero_cc ----------------------------------------*/

#if __ARCH__ >= 13

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector signed char __a,
                                __vector signed char __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszb((__vector unsigned char)__a,
                                (__vector unsigned char)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector __bool char __a,
                                __vector __bool char __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszb((__vector unsigned char)__a,
                                (__vector unsigned char)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector unsigned char __a,
                                __vector unsigned char __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszb(__a, __b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector signed short __a,
                                __vector signed short __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszh((__vector unsigned short)__a,
                                (__vector unsigned short)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector __bool short __a,
                                __vector __bool short __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszh((__vector unsigned short)__a,
                                (__vector unsigned short)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector unsigned short __a,
                                __vector unsigned short __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszh(__a, __b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector signed int __a,
                                __vector signed int __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszf((__vector unsigned int)__a,
                                (__vector unsigned int)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector __bool int __a,
                                __vector __bool int __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszf((__vector unsigned int)__a,
                                (__vector unsigned int)__b, __c, __cc);
}

static inline __ATTRS_o_ai __vector unsigned char
vec_search_string_until_zero_cc(__vector unsigned int __a,
                                __vector unsigned int __b,
                                __vector unsigned char __c, int *__cc) {
  return __builtin_s390_vstrszf(__a, __b, __c, __cc);
}

#endif

#undef __constant_pow2_range
#undef __constant_range
#undef __constant
#undef __ATTRS_o
#undef __ATTRS_o_ai
#undef __ATTRS_ai

#else

#error "Use -fzvector to enable vector extensions"

#endif
