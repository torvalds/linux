/*===---- adcintrin.h - ADC intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __ADCINTRIN_H
#define __ADCINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 and x64 architecture"
#endif

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__))

/* Use C++ inline semantics in C++, GNU inline for C mode. */
#if defined(__cplusplus)
#define __INLINE __inline
#else
#define __INLINE static __inline
#endif

#if defined(__cplusplus)
extern "C" {
#endif

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
/// This intrinsic corresponds to the \c ADC instruction.
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
__INLINE unsigned char __DEFAULT_FN_ATTRS _addcarry_u32(unsigned char __cf,
                                                        unsigned int __x,
                                                        unsigned int __y,
                                                        unsigned int *__p) {
  return __builtin_ia32_addcarryx_u32(__cf, __x, __y, __p);
}

/// Adds unsigned 32-bit integer \a __y to 0 or 1 as indicated by the carry
///    flag \a __cf, and subtracts the result from unsigned 32-bit integer
///    \a __x. Stores the unsigned 32-bit difference in the memory at \a __p,
///    and returns the 8-bit carry-out (carry or overflow flag).
///
/// \code{.operation}
/// temp := (__cf == 0) ? 0 : 1
/// Store32(__p, __x - (__y + temp))
/// result := CF
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c SBB instruction.
///
/// \param __cf
///    The 8-bit unsigned carry flag; any non-zero value indicates carry.
/// \param __x
///    The 32-bit unsigned minuend.
/// \param __y
///    The 32-bit unsigned subtrahend.
/// \param __p
///    Pointer to memory for storing the difference.
/// \returns The 8-bit unsigned carry-out value.
__INLINE unsigned char __DEFAULT_FN_ATTRS _subborrow_u32(unsigned char __cf,
                                                         unsigned int __x,
                                                         unsigned int __y,
                                                         unsigned int *__p) {
  return __builtin_ia32_subborrow_u32(__cf, __x, __y, __p);
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
/// This intrinsic corresponds to the \c ADC instruction.
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
_addcarry_u64(unsigned char __cf, unsigned long long __x,
              unsigned long long __y, unsigned long long *__p) {
  return __builtin_ia32_addcarryx_u64(__cf, __x, __y, __p);
}

/// Adds unsigned 64-bit integer \a __y to 0 or 1 as indicated by the carry
///    flag \a __cf, and subtracts the result from unsigned 64-bit integer
///    \a __x. Stores the unsigned 64-bit difference in the memory at \a __p,
///    and returns the 8-bit carry-out (carry or overflow flag).
///
/// \code{.operation}
/// temp := (__cf == 0) ? 0 : 1
/// Store64(__p, __x - (__y + temp))
/// result := CF
/// \endcode
///
/// \headerfile <immintrin.h>
///
/// This intrinsic corresponds to the \c ADC instruction.
///
/// \param __cf
///    The 8-bit unsigned carry flag; any non-zero value indicates carry.
/// \param __x
///    The 64-bit unsigned minuend.
/// \param __y
///    The 64-bit unsigned subtrahend.
/// \param __p
///    Pointer to memory for storing the difference.
/// \returns The 8-bit unsigned carry-out value.
__INLINE unsigned char __DEFAULT_FN_ATTRS
_subborrow_u64(unsigned char __cf, unsigned long long __x,
               unsigned long long __y, unsigned long long *__p) {
  return __builtin_ia32_subborrow_u64(__cf, __x, __y, __p);
}
#endif

#if defined(__cplusplus)
}
#endif

#undef __INLINE
#undef __DEFAULT_FN_ATTRS

#endif /* __ADCINTRIN_H */
