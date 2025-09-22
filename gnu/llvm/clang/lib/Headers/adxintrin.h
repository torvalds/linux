/*===---- adxintrin.h - ADX intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <adxintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __ADXINTRIN_H
#define __ADXINTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("adx")))

/* Use C++ inline semantics in C++, GNU inline for C mode. */
#if defined(__cplusplus)
#define __INLINE __inline
#else
#define __INLINE static __inline
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* Intrinsics that are available only if __ADX__ is defined. */

/// Adds unsigned 32-bit integers \a __x and \a __y, plus 0 or 1 as indicated
///    by the carry flag \a __cf. Stores the unsigned 32-bit sum in the memory
///    at \a __p, and returns the 8-bit carry-out (carry flag).
///
/// \code{.operation}
/// temp := (__cf == 0) ? 0 : 1
/// Store32(__p, __x + __y + temp)
/// result := CF
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c ADCX instruction.
///
/// \param __cf
///    The 8-bit unsigned carry flag; any non-zero value indicates carry.
/// \param __x
///    A 32-bit unsigned addend.
/// \param __y
///    A 32-bit unsigned addend.
/// \param __p
///    Pointer to memory for storing the sum.
/// \returns The 8-bit unsigned carry-out value.
__INLINE unsigned char __DEFAULT_FN_ATTRS _addcarryx_u32(unsigned char __cf,
                                                         unsigned int __x,
                                                         unsigned int __y,
                                                         unsigned int *__p) {
  return __builtin_ia32_addcarryx_u32(__cf, __x, __y, __p);
}

#ifdef __x86_64__
/// Adds unsigned 64-bit integers \a __x and \a __y, plus 0 or 1 as indicated
///    by the carry flag \a __cf. Stores the unsigned 64-bit sum in the memory
///    at \a __p, and returns the 8-bit carry-out (carry flag).
///
/// \code{.operation}
/// temp := (__cf == 0) ? 0 : 1
/// Store64(__p, __x + __y + temp)
/// result := CF
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c ADCX instruction.
///
/// \param __cf
///    The 8-bit unsigned carry flag; any non-zero value indicates carry.
/// \param __x
///    A 64-bit unsigned addend.
/// \param __y
///    A 64-bit unsigned addend.
/// \param __p
///    Pointer to memory for storing the sum.
/// \returns The 8-bit unsigned carry-out value.
__INLINE unsigned char __DEFAULT_FN_ATTRS
_addcarryx_u64(unsigned char __cf, unsigned long long __x,
               unsigned long long __y, unsigned long long *__p) {
  return __builtin_ia32_addcarryx_u64(__cf, __x, __y, __p);
}
#endif

#if defined(__cplusplus)
}
#endif

#undef __INLINE
#undef __DEFAULT_FN_ATTRS

#endif /* __ADXINTRIN_H */
