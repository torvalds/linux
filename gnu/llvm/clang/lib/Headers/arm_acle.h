/*===---- arm_acle.h - ARM Non-Neon intrinsics -----------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * The Arm C Language Extensions specifications can be found in the following
 * link: https://github.com/ARM-software/acle/releases
 *
 * The ACLE section numbers are subject to change. When consulting the
 * specifications, it is recommended to search using section titles if
 * the section numbers look outdated.
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __ARM_ACLE_H
#define __ARM_ACLE_H

#ifndef __ARM_ACLE
#error "ACLE intrinsics support not enabled."
#endif

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* 7 SYNCHRONIZATION, BARRIER AND HINT INTRINSICS */
/* 7.3 Memory barriers */
#if !__has_builtin(__dmb)
#define __dmb(i) __builtin_arm_dmb(i)
#endif
#if !__has_builtin(__dsb)
#define __dsb(i) __builtin_arm_dsb(i)
#endif
#if !__has_builtin(__isb)
#define __isb(i) __builtin_arm_isb(i)
#endif

/* 7.4 Hints */

#if !__has_builtin(__wfi)
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __wfi(void) {
  __builtin_arm_wfi();
}
#endif

#if !__has_builtin(__wfe)
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __wfe(void) {
  __builtin_arm_wfe();
}
#endif

#if !__has_builtin(__sev)
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __sev(void) {
  __builtin_arm_sev();
}
#endif

#if !__has_builtin(__sevl)
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __sevl(void) {
  __builtin_arm_sevl();
}
#endif

#if !__has_builtin(__yield)
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __yield(void) {
  __builtin_arm_yield();
}
#endif

#if defined(__ARM_32BIT_STATE) && __ARM_32BIT_STATE
#define __dbg(t) __builtin_arm_dbg(t)
#endif

#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
#define _CHKFEAT_GCS 1
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__chkfeat(uint64_t __features) {
  return __builtin_arm_chkfeat(__features) ^ __features;
}
#endif

/* 7.5 Swap */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__swp(uint32_t __x, volatile uint32_t *__p) {
  uint32_t v;
  do
    v = __builtin_arm_ldrex(__p);
  while (__builtin_arm_strex(__x, __p));
  return v;
}

/* 7.6 Memory prefetch intrinsics */
/* 7.6.1 Data prefetch */
#define __pld(addr) __pldx(0, 0, 0, addr)

#if defined(__ARM_32BIT_STATE) && __ARM_32BIT_STATE
#define __pldx(access_kind, cache_level, retention_policy, addr) \
  __builtin_arm_prefetch(addr, access_kind, 1)
#else
#define __pldx(access_kind, cache_level, retention_policy, addr) \
  __builtin_arm_prefetch(addr, access_kind, cache_level, retention_policy, 1)
#endif

/* 7.6.2 Instruction prefetch */
#define __pli(addr) __plix(0, 0, addr)

#if defined(__ARM_32BIT_STATE) && __ARM_32BIT_STATE
#define __plix(cache_level, retention_policy, addr) \
  __builtin_arm_prefetch(addr, 0, 0)
#else
#define __plix(cache_level, retention_policy, addr) \
  __builtin_arm_prefetch(addr, 0, cache_level, retention_policy, 0)
#endif

/* 7.7 NOP */
#if !defined(_MSC_VER) || (!defined(__aarch64__) && !defined(__arm64ec__))
static __inline__ void __attribute__((__always_inline__, __nodebug__)) __nop(void) {
  __builtin_arm_nop();
}
#endif

/* 8 DATA-PROCESSING INTRINSICS */
/* 8.2 Miscellaneous data-processing intrinsics */
/* ROR */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__ror(uint32_t __x, uint32_t __y) {
  __y %= 32;
  if (__y == 0)
    return __x;
  return (__x >> __y) | (__x << (32 - __y));
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rorll(uint64_t __x, uint32_t __y) {
  __y %= 64;
  if (__y == 0)
    return __x;
  return (__x >> __y) | (__x << (64 - __y));
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rorl(unsigned long __x, uint32_t __y) {
#if __SIZEOF_LONG__ == 4
  return __ror(__x, __y);
#else
  return __rorll(__x, __y);
#endif
}


/* CLZ */
static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clz(uint32_t __t) {
  return __builtin_arm_clz(__t);
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clzl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_arm_clz(__t);
#else
  return __builtin_arm_clz64(__t);
#endif
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clzll(uint64_t __t) {
  return __builtin_arm_clz64(__t);
}

/* CLS */
static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__cls(uint32_t __t) {
  return __builtin_arm_cls(__t);
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clsl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_arm_cls(__t);
#else
  return __builtin_arm_cls64(__t);
#endif
}

static __inline__ unsigned int __attribute__((__always_inline__, __nodebug__))
__clsll(uint64_t __t) {
  return __builtin_arm_cls64(__t);
}

/* REV */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rev(uint32_t __t) {
  return __builtin_bswap32(__t);
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__revl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __builtin_bswap32(__t);
#else
  return __builtin_bswap64(__t);
#endif
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__revll(uint64_t __t) {
  return __builtin_bswap64(__t);
}

/* REV16 */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rev16(uint32_t __t) {
  return __ror(__rev(__t), 16);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rev16ll(uint64_t __t) {
  return (((uint64_t)__rev16(__t >> 32)) << 32) | (uint64_t)__rev16((uint32_t)__t);
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rev16l(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
    return __rev16(__t);
#else
    return __rev16ll(__t);
#endif
}

/* REVSH */
static __inline__ int16_t __attribute__((__always_inline__, __nodebug__))
__revsh(int16_t __t) {
  return (int16_t)__builtin_bswap16((uint16_t)__t);
}

/* RBIT */
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__rbit(uint32_t __t) {
  return __builtin_arm_rbit(__t);
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__))
__rbitll(uint64_t __t) {
#if defined(__ARM_32BIT_STATE) && __ARM_32BIT_STATE
  return (((uint64_t)__builtin_arm_rbit(__t)) << 32) |
         __builtin_arm_rbit(__t >> 32);
#else
  return __builtin_arm_rbit64(__t);
#endif
}

static __inline__ unsigned long __attribute__((__always_inline__, __nodebug__))
__rbitl(unsigned long __t) {
#if __SIZEOF_LONG__ == 4
  return __rbit(__t);
#else
  return __rbitll(__t);
#endif
}

/* 8.3 16-bit multiplications */
#if defined(__ARM_FEATURE_DSP) && __ARM_FEATURE_DSP
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smulbb(int32_t __a, int32_t __b) {
  return __builtin_arm_smulbb(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smulbt(int32_t __a, int32_t __b) {
  return __builtin_arm_smulbt(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smultb(int32_t __a, int32_t __b) {
  return __builtin_arm_smultb(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smultt(int32_t __a, int32_t __b) {
  return __builtin_arm_smultt(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smulwb(int32_t __a, int32_t __b) {
  return __builtin_arm_smulwb(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__,__nodebug__))
__smulwt(int32_t __a, int32_t __b) {
  return __builtin_arm_smulwt(__a, __b);
}
#endif

/*
 * 8.4 Saturating intrinsics
 *
 * FIXME: Change guard to their corresponding __ARM_FEATURE flag when Q flag
 * intrinsics are implemented and the flag is enabled.
 */
/* 8.4.1 Width-specified saturation intrinsics */
#if defined(__ARM_FEATURE_SAT) && __ARM_FEATURE_SAT
#define __ssat(x, y) __builtin_arm_ssat(x, y)
#define __usat(x, y) __builtin_arm_usat(x, y)
#endif

/* 8.4.2 Saturating addition and subtraction intrinsics */
#if defined(__ARM_FEATURE_DSP) && __ARM_FEATURE_DSP
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__qadd(int32_t __t, int32_t __v) {
  return __builtin_arm_qadd(__t, __v);
}

static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__qsub(int32_t __t, int32_t __v) {
  return __builtin_arm_qsub(__t, __v);
}

static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__qdbl(int32_t __t) {
  return __builtin_arm_qadd(__t, __t);
}
#endif

/* 8.4.3 Accumulating multiplications */
#if defined(__ARM_FEATURE_DSP) && __ARM_FEATURE_DSP
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlabb(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlabb(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlabt(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlabt(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlatb(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlatb(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlatt(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlatt(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlawb(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlawb(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlawt(int32_t __a, int32_t __b, int32_t __c) {
  return __builtin_arm_smlawt(__a, __b, __c);
}
#endif


/* 8.5.4 Parallel 16-bit saturation */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
#define __ssat16(x, y) __builtin_arm_ssat16(x, y)
#define __usat16(x, y) __builtin_arm_usat16(x, y)
#endif

/* 8.5.5 Packing and unpacking */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
typedef int32_t int8x4_t;
typedef int32_t int16x2_t;
typedef uint32_t uint8x4_t;
typedef uint32_t uint16x2_t;

static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__sxtab16(int16x2_t __a, int8x4_t __b) {
  return __builtin_arm_sxtab16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__sxtb16(int8x4_t __a) {
  return __builtin_arm_sxtb16(__a);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__uxtab16(int16x2_t __a, int8x4_t __b) {
  return __builtin_arm_uxtab16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__uxtb16(int8x4_t __a) {
  return __builtin_arm_uxtb16(__a);
}
#endif

/* 8.5.6 Parallel selection */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__sel(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_sel(__a, __b);
}
#endif

/* 8.5.7 Parallel 8-bit addition and subtraction */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__qadd8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_qadd8(__a, __b);
}
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__qsub8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_qsub8(__a, __b);
}
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__sadd8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_sadd8(__a, __b);
}
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__shadd8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_shadd8(__a, __b);
}
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__shsub8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_shsub8(__a, __b);
}
static __inline__ int8x4_t __attribute__((__always_inline__, __nodebug__))
__ssub8(int8x4_t __a, int8x4_t __b) {
  return __builtin_arm_ssub8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__uadd8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_uadd8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__uhadd8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_uhadd8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__uhsub8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_uhsub8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__uqadd8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_uqadd8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__uqsub8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_uqsub8(__a, __b);
}
static __inline__ uint8x4_t __attribute__((__always_inline__, __nodebug__))
__usub8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_usub8(__a, __b);
}
#endif

/* 8.5.8 Sum of 8-bit absolute differences */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__usad8(uint8x4_t __a, uint8x4_t __b) {
  return __builtin_arm_usad8(__a, __b);
}
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__))
__usada8(uint8x4_t __a, uint8x4_t __b, uint32_t __c) {
  return __builtin_arm_usada8(__a, __b, __c);
}
#endif

/* 8.5.9 Parallel 16-bit addition and subtraction */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__qadd16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_qadd16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__qasx(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_qasx(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__qsax(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_qsax(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__qsub16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_qsub16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__sadd16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_sadd16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__sasx(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_sasx(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__shadd16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_shadd16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__shasx(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_shasx(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__shsax(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_shsax(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__shsub16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_shsub16(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__ssax(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_ssax(__a, __b);
}
static __inline__ int16x2_t __attribute__((__always_inline__, __nodebug__))
__ssub16(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_ssub16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uadd16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uadd16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uasx(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uasx(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uhadd16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uhadd16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uhasx(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uhasx(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uhsax(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uhsax(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uhsub16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uhsub16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uqadd16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uqadd16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uqasx(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uqasx(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uqsax(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uqsax(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__uqsub16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_uqsub16(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__usax(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_usax(__a, __b);
}
static __inline__ uint16x2_t __attribute__((__always_inline__, __nodebug__))
__usub16(uint16x2_t __a, uint16x2_t __b) {
  return __builtin_arm_usub16(__a, __b);
}
#endif

/* 8.5.10 Parallel 16-bit multiplication */
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlad(int16x2_t __a, int16x2_t __b, int32_t __c) {
  return __builtin_arm_smlad(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smladx(int16x2_t __a, int16x2_t __b, int32_t __c) {
  return __builtin_arm_smladx(__a, __b, __c);
}
static __inline__ int64_t __attribute__((__always_inline__, __nodebug__))
__smlald(int16x2_t __a, int16x2_t __b, int64_t __c) {
  return __builtin_arm_smlald(__a, __b, __c);
}
static __inline__ int64_t __attribute__((__always_inline__, __nodebug__))
__smlaldx(int16x2_t __a, int16x2_t __b, int64_t __c) {
  return __builtin_arm_smlaldx(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlsd(int16x2_t __a, int16x2_t __b, int32_t __c) {
  return __builtin_arm_smlsd(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smlsdx(int16x2_t __a, int16x2_t __b, int32_t __c) {
  return __builtin_arm_smlsdx(__a, __b, __c);
}
static __inline__ int64_t __attribute__((__always_inline__, __nodebug__))
__smlsld(int16x2_t __a, int16x2_t __b, int64_t __c) {
  return __builtin_arm_smlsld(__a, __b, __c);
}
static __inline__ int64_t __attribute__((__always_inline__, __nodebug__))
__smlsldx(int16x2_t __a, int16x2_t __b, int64_t __c) {
  return __builtin_arm_smlsldx(__a, __b, __c);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smuad(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_smuad(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smuadx(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_smuadx(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smusd(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_smusd(__a, __b);
}
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__))
__smusdx(int16x2_t __a, int16x2_t __b) {
  return __builtin_arm_smusdx(__a, __b);
}
#endif

/* 8.6 Floating-point data-processing intrinsics */
#if (defined(__ARM_FEATURE_DIRECTED_ROUNDING)    &&                         \
  (__ARM_FEATURE_DIRECTED_ROUNDING))             &&                         \
  (defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE)
static __inline__ double __attribute__((__always_inline__, __nodebug__))
__rintn(double __a) {
  return __builtin_roundeven(__a);
}

static __inline__ float __attribute__((__always_inline__, __nodebug__))
__rintnf(float __a) {
  return __builtin_roundevenf(__a);
}
#endif

/* 8.8 CRC32 intrinsics */
#if (defined(__ARM_FEATURE_CRC32) && __ARM_FEATURE_CRC32) ||                   \
    (defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE)
static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32b(uint32_t __a, uint8_t __b) {
  return __builtin_arm_crc32b(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32h(uint32_t __a, uint16_t __b) {
  return __builtin_arm_crc32h(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32w(uint32_t __a, uint32_t __b) {
  return __builtin_arm_crc32w(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32d(uint32_t __a, uint64_t __b) {
  return __builtin_arm_crc32d(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32cb(uint32_t __a, uint8_t __b) {
  return __builtin_arm_crc32cb(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32ch(uint32_t __a, uint16_t __b) {
  return __builtin_arm_crc32ch(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32cw(uint32_t __a, uint32_t __b) {
  return __builtin_arm_crc32cw(__a, __b);
}

static __inline__ uint32_t __attribute__((__always_inline__, __nodebug__, target("crc")))
__crc32cd(uint32_t __a, uint64_t __b) {
  return __builtin_arm_crc32cd(__a, __b);
}
#endif

/* 8.6 Floating-point data-processing intrinsics */
/* Armv8.3-A Javascript conversion intrinsic */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ int32_t __attribute__((__always_inline__, __nodebug__, target("v8.3a")))
__jcvt(double __a) {
  return __builtin_arm_jcvt(__a);
}
#endif

/* Armv8.5-A FP rounding intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ float __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint32zf(float __a) {
  return __builtin_arm_rint32zf(__a);
}

static __inline__ double __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint32z(double __a) {
  return __builtin_arm_rint32z(__a);
}

static __inline__ float __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint64zf(float __a) {
  return __builtin_arm_rint64zf(__a);
}

static __inline__ double __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint64z(double __a) {
  return __builtin_arm_rint64z(__a);
}

static __inline__ float __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint32xf(float __a) {
  return __builtin_arm_rint32xf(__a);
}

static __inline__ double __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint32x(double __a) {
  return __builtin_arm_rint32x(__a);
}

static __inline__ float __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint64xf(float __a) {
  return __builtin_arm_rint64xf(__a);
}

static __inline__ double __attribute__((__always_inline__, __nodebug__, target("v8.5a")))
__rint64x(double __a) {
  return __builtin_arm_rint64x(__a);
}
#endif

/* 8.9 Armv8.7-A load/store 64-byte intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
typedef struct {
    uint64_t val[8];
} data512_t;

static __inline__ data512_t __attribute__((__always_inline__, __nodebug__, target("ls64")))
__arm_ld64b(const void *__addr) {
  data512_t __value;
  __builtin_arm_ld64b(__addr, __value.val);
  return __value;
}
static __inline__ void __attribute__((__always_inline__, __nodebug__, target("ls64")))
__arm_st64b(void *__addr, data512_t __value) {
  __builtin_arm_st64b(__addr, __value.val);
}
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__, target("ls64")))
__arm_st64bv(void *__addr, data512_t __value) {
  return __builtin_arm_st64bv(__addr, __value.val);
}
static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__, target("ls64")))
__arm_st64bv0(void *__addr, data512_t __value) {
  return __builtin_arm_st64bv0(__addr, __value.val);
}
#endif

/* 11.1 Special register intrinsics */
#define __arm_rsr(sysreg) __builtin_arm_rsr(sysreg)
#define __arm_rsr64(sysreg) __builtin_arm_rsr64(sysreg)
#define __arm_rsr128(sysreg) __builtin_arm_rsr128(sysreg)
#define __arm_rsrp(sysreg) __builtin_arm_rsrp(sysreg)
#define __arm_rsrf(sysreg) __builtin_bit_cast(float, __arm_rsr(sysreg))
#define __arm_rsrf64(sysreg) __builtin_bit_cast(double, __arm_rsr64(sysreg))
#define __arm_wsr(sysreg, v) __builtin_arm_wsr(sysreg, v)
#define __arm_wsr64(sysreg, v) __builtin_arm_wsr64(sysreg, v)
#define __arm_wsr128(sysreg, v) __builtin_arm_wsr128(sysreg, v)
#define __arm_wsrp(sysreg, v) __builtin_arm_wsrp(sysreg, v)
#define __arm_wsrf(sysreg, v) __arm_wsr(sysreg, __builtin_bit_cast(uint32_t, v))
#define __arm_wsrf64(sysreg, v) __arm_wsr64(sysreg, __builtin_bit_cast(uint64_t, v))

/* 10.3 MTE intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
#define __arm_mte_create_random_tag(__ptr, __mask)  __builtin_arm_irg(__ptr, __mask)
#define __arm_mte_increment_tag(__ptr, __tag_offset)  __builtin_arm_addg(__ptr, __tag_offset)
#define __arm_mte_exclude_tag(__ptr, __excluded)  __builtin_arm_gmi(__ptr, __excluded)
#define __arm_mte_get_tag(__ptr) __builtin_arm_ldg(__ptr)
#define __arm_mte_set_tag(__ptr) __builtin_arm_stg(__ptr)
#define __arm_mte_ptrdiff(__ptra, __ptrb) __builtin_arm_subp(__ptra, __ptrb)

/* 18 memcpy family of operations intrinsics - MOPS */
#define __arm_mops_memset_tag(__tagged_address, __value, __size)    \
  __builtin_arm_mops_memset_tag(__tagged_address, __value, __size)
#endif

/* 11.3 Coprocessor Intrinsics */
#if defined(__ARM_FEATURE_COPROC)

#if (__ARM_FEATURE_COPROC & 0x1)

#if (__ARM_ARCH < 8)
#define __arm_cdp(coproc, opc1, CRd, CRn, CRm, opc2)                           \
  __builtin_arm_cdp(coproc, opc1, CRd, CRn, CRm, opc2)
#endif /* __ARM_ARCH < 8 */

#define __arm_ldc(coproc, CRd, p) __builtin_arm_ldc(coproc, CRd, p)
#define __arm_stc(coproc, CRd, p) __builtin_arm_stc(coproc, CRd, p)

#define __arm_mcr(coproc, opc1, value, CRn, CRm, opc2)                         \
  __builtin_arm_mcr(coproc, opc1, value, CRn, CRm, opc2)
#define __arm_mrc(coproc, opc1, CRn, CRm, opc2)                                \
  __builtin_arm_mrc(coproc, opc1, CRn, CRm, opc2)

#if (__ARM_ARCH != 4) && (__ARM_ARCH < 8)
#define __arm_ldcl(coproc, CRd, p) __builtin_arm_ldcl(coproc, CRd, p)
#define __arm_stcl(coproc, CRd, p) __builtin_arm_stcl(coproc, CRd, p)
#endif /* (__ARM_ARCH != 4) && (__ARM_ARCH != 8) */

#if (__ARM_ARCH_8M_MAIN__) || (__ARM_ARCH_8_1M_MAIN__)
#define __arm_cdp(coproc, opc1, CRd, CRn, CRm, opc2)                           \
  __builtin_arm_cdp(coproc, opc1, CRd, CRn, CRm, opc2)
#define __arm_ldcl(coproc, CRd, p) __builtin_arm_ldcl(coproc, CRd, p)
#define __arm_stcl(coproc, CRd, p) __builtin_arm_stcl(coproc, CRd, p)
#endif /* ___ARM_ARCH_8M_MAIN__ */

#endif /* __ARM_FEATURE_COPROC & 0x1 */

#if (__ARM_FEATURE_COPROC & 0x2)
#define __arm_cdp2(coproc, opc1, CRd, CRn, CRm, opc2)                          \
  __builtin_arm_cdp2(coproc, opc1, CRd, CRn, CRm, opc2)
#define __arm_ldc2(coproc, CRd, p) __builtin_arm_ldc2(coproc, CRd, p)
#define __arm_stc2(coproc, CRd, p) __builtin_arm_stc2(coproc, CRd, p)
#define __arm_ldc2l(coproc, CRd, p) __builtin_arm_ldc2l(coproc, CRd, p)
#define __arm_stc2l(coproc, CRd, p) __builtin_arm_stc2l(coproc, CRd, p)
#define __arm_mcr2(coproc, opc1, value, CRn, CRm, opc2)                        \
  __builtin_arm_mcr2(coproc, opc1, value, CRn, CRm, opc2)
#define __arm_mrc2(coproc, opc1, CRn, CRm, opc2)                               \
  __builtin_arm_mrc2(coproc, opc1, CRn, CRm, opc2)
#endif

#if (__ARM_FEATURE_COPROC & 0x4)
#define __arm_mcrr(coproc, opc1, value, CRm)                                   \
  __builtin_arm_mcrr(coproc, opc1, value, CRm)
#define __arm_mrrc(coproc, opc1, CRm) __builtin_arm_mrrc(coproc, opc1, CRm)
#endif

#if (__ARM_FEATURE_COPROC & 0x8)
#define __arm_mcrr2(coproc, opc1, value, CRm)                                  \
  __builtin_arm_mcrr2(coproc, opc1, value, CRm)
#define __arm_mrrc2(coproc, opc1, CRm) __builtin_arm_mrrc2(coproc, opc1, CRm)
#endif

#endif // __ARM_FEATURE_COPROC

/* 17 Transactional Memory Extension (TME) Intrinsics */
#if defined(__ARM_FEATURE_TME) && __ARM_FEATURE_TME

#define _TMFAILURE_REASON  0x00007fffu
#define _TMFAILURE_RTRY    0x00008000u
#define _TMFAILURE_CNCL    0x00010000u
#define _TMFAILURE_MEM     0x00020000u
#define _TMFAILURE_IMP     0x00040000u
#define _TMFAILURE_ERR     0x00080000u
#define _TMFAILURE_SIZE    0x00100000u
#define _TMFAILURE_NEST    0x00200000u
#define _TMFAILURE_DBG     0x00400000u
#define _TMFAILURE_INT     0x00800000u
#define _TMFAILURE_TRIVIAL 0x01000000u

#define __tstart()        __builtin_arm_tstart()
#define __tcommit()       __builtin_arm_tcommit()
#define __tcancel(__arg)  __builtin_arm_tcancel(__arg)
#define __ttest()         __builtin_arm_ttest()

#endif /* __ARM_FEATURE_TME */

/* 8.7 Armv8.5-A Random number generation intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ int __attribute__((__always_inline__, __nodebug__, target("rand")))
__rndr(uint64_t *__p) {
  return __builtin_arm_rndr(__p);
}
static __inline__ int __attribute__((__always_inline__, __nodebug__, target("rand")))
__rndrrs(uint64_t *__p) {
  return __builtin_arm_rndrrs(__p);
}
#endif

/* 11.2 Guarded Control Stack intrinsics */
#if defined(__ARM_64BIT_STATE) && __ARM_64BIT_STATE
static __inline__ void * __attribute__((__always_inline__, __nodebug__))
__gcspr() {
  return (void *)__builtin_arm_rsr64("gcspr_el0");
}

static __inline__ uint64_t __attribute__((__always_inline__, __nodebug__, target("gcs")))
__gcspopm() {
  return __builtin_arm_gcspopm(0);
}

static __inline__ const void * __attribute__((__always_inline__, __nodebug__, target("gcs")))
__gcsss(const void *__stack) {
  return __builtin_arm_gcsss(__stack);
}
#endif

#if defined(__cplusplus)
}
#endif

#endif /* __ARM_ACLE_H */
