/*===---- cetintrin.h - CET intrinsic --------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <cetintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __CETINTRIN_H
#define __CETINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("shstk")))

static __inline__ void __DEFAULT_FN_ATTRS _incsspd(int __a) {
  __builtin_ia32_incsspd((unsigned int)__a);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _incsspq(unsigned long long __a) {
  __builtin_ia32_incsspq(__a);
}
#endif /* __x86_64__ */

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _inc_ssp(unsigned int __a) {
  __builtin_ia32_incsspq(__a);
}
#else /* __x86_64__ */
static __inline__ void __DEFAULT_FN_ATTRS _inc_ssp(unsigned int __a) {
  __builtin_ia32_incsspd(__a);
}
#endif /* __x86_64__ */

static __inline__ unsigned int __DEFAULT_FN_ATTRS _rdsspd(unsigned int __a) {
  return __builtin_ia32_rdsspd(__a);
}

static __inline__ unsigned int __DEFAULT_FN_ATTRS _rdsspd_i32(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  unsigned int t;
  return __builtin_ia32_rdsspd(t);
#pragma clang diagnostic pop
}

#ifdef __x86_64__
static __inline__ unsigned long long __DEFAULT_FN_ATTRS _rdsspq(unsigned long long __a) {
  return __builtin_ia32_rdsspq(__a);
}

static __inline__ unsigned long long __DEFAULT_FN_ATTRS _rdsspq_i64(void) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wuninitialized"
  unsigned long long t;
  return __builtin_ia32_rdsspq(t);
#pragma clang diagnostic pop
}
#endif /* __x86_64__ */

#ifdef __x86_64__
static __inline__ unsigned long long __DEFAULT_FN_ATTRS _get_ssp(void) {
  return __builtin_ia32_rdsspq(0);
}
#else /* __x86_64__ */
static __inline__ unsigned int __DEFAULT_FN_ATTRS _get_ssp(void) {
  return __builtin_ia32_rdsspd(0);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _saveprevssp(void) {
  __builtin_ia32_saveprevssp();
}

static __inline__ void __DEFAULT_FN_ATTRS _rstorssp(void * __p) {
  __builtin_ia32_rstorssp(__p);
}

static __inline__ void __DEFAULT_FN_ATTRS _wrssd(unsigned int __a, void * __p) {
  __builtin_ia32_wrssd(__a, __p);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _wrssq(unsigned long long __a, void * __p) {
  __builtin_ia32_wrssq(__a, __p);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _wrussd(unsigned int __a, void * __p) {
  __builtin_ia32_wrussd(__a, __p);
}

#ifdef __x86_64__
static __inline__ void __DEFAULT_FN_ATTRS _wrussq(unsigned long long __a, void * __p) {
  __builtin_ia32_wrussq(__a, __p);
}
#endif /* __x86_64__ */

static __inline__ void __DEFAULT_FN_ATTRS _setssbsy(void) {
  __builtin_ia32_setssbsy();
}

static __inline__ void __DEFAULT_FN_ATTRS _clrssbsy(void * __p) {
  __builtin_ia32_clrssbsy(__p);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __CETINTRIN_H */
