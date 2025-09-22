/*===---- bmiintrin.h - BMI intrinsics -------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#if !defined __X86INTRIN_H && !defined __IMMINTRIN_H
#error "Never use <bmiintrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __BMIINTRIN_H
#define __BMIINTRIN_H

/* Allow using the tzcnt intrinsics even for non-BMI targets. Since the TZCNT
   instruction behaves as BSF on non-BMI targets, there is code that expects
   to use it as a potentially faster version of BSF. */
#define __RELAXED_FN_ATTRS __attribute__((__always_inline__, __nodebug__))

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 16-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 16-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see _tzcnt_u16
static __inline__ unsigned short __RELAXED_FN_ATTRS
__tzcnt_u16(unsigned short __X)
{
  return __builtin_ia32_tzcnt_u16(__X);
}

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned short _tzcnt_u16(unsigned short __X);
/// \endcode
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 16-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 16-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see __tzcnt_u16
#define _tzcnt_u16 __tzcnt_u16

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 32-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 32-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see { _mm_tzcnt_32 _tzcnt_u32 }
static __inline__ unsigned int __RELAXED_FN_ATTRS
__tzcnt_u32(unsigned int __X)
{
  return __builtin_ia32_tzcnt_u32(__X);
}

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 32-bit integer whose trailing zeros are to be counted.
/// \returns A 32-bit integer containing the number of trailing zero bits in
///    the operand.
/// \see { __tzcnt_u32 _tzcnt_u32 }
static __inline__ int __RELAXED_FN_ATTRS
_mm_tzcnt_32(unsigned int __X)
{
  return (int)__builtin_ia32_tzcnt_u32(__X);
}

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _tzcnt_u32(unsigned int __X);
/// \endcode
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 32-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 32-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see { _mm_tzcnt_32 __tzcnt_u32 }
#define _tzcnt_u32 __tzcnt_u32

#ifdef __x86_64__

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 64-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see { _mm_tzcnt_64 _tzcnt_u64 }
static __inline__ unsigned long long __RELAXED_FN_ATTRS
__tzcnt_u64(unsigned long long __X)
{
  return __builtin_ia32_tzcnt_u64(__X);
}

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose trailing zeros are to be counted.
/// \returns An 64-bit integer containing the number of trailing zero bits in
///    the operand.
/// \see { __tzcnt_u64 _tzcnt_u64 }
static __inline__ long long __RELAXED_FN_ATTRS
_mm_tzcnt_64(unsigned long long __X)
{
  return (long long)__builtin_ia32_tzcnt_u64(__X);
}

/// Counts the number of trailing zero bits in the operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _tzcnt_u64(unsigned long long __X);
/// \endcode
///
/// This intrinsic corresponds to the \c TZCNT instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose trailing zeros are to be counted.
/// \returns An unsigned 64-bit integer containing the number of trailing zero
///    bits in the operand.
/// \see { _mm_tzcnt_64 __tzcnt_u64
#define _tzcnt_u64 __tzcnt_u64

#endif /* __x86_64__ */

#undef __RELAXED_FN_ATTRS

#if !defined(__SCE__) || __has_feature(modules) || defined(__BMI__)

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__, __target__("bmi")))

/// Performs a bitwise AND of the second operand with the one's
///    complement of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ANDN instruction.
///
/// \param __X
///    An unsigned integer containing one of the operands.
/// \param __Y
///    An unsigned integer containing one of the operands.
/// \returns An unsigned integer containing the bitwise AND of the second
///    operand with the one's complement of the first operand.
/// \see _andn_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__andn_u32(unsigned int __X, unsigned int __Y)
{
  return ~__X & __Y;
}

/// Performs a bitwise AND of the second operand with the one's
///    complement of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _andn_u32(unsigned int __X, unsigned int __Y);
/// \endcode
///
/// This intrinsic corresponds to the \c ANDN instruction.
///
/// \param __X
///    An unsigned integer containing one of the operands.
/// \param __Y
///    An unsigned integer containing one of the operands.
/// \returns An unsigned integer containing the bitwise AND of the second
///    operand with the one's complement of the first operand.
/// \see __andn_u32
#define _andn_u32 __andn_u32

/* AMD-specified, double-leading-underscore version of BEXTR */
/// Extracts the specified bits from the first operand and returns them
///    in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned integer whose bits are to be extracted.
/// \param __Y
///    An unsigned integer used to specify which bits are extracted. Bits [7:0]
///    specify the index of the least significant bit. Bits [15:8] specify the
///    number of bits to be extracted.
/// \returns An unsigned integer whose least significant bits contain the
///    extracted bits.
/// \see _bextr_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__bextr_u32(unsigned int __X, unsigned int __Y)
{
  return __builtin_ia32_bextr_u32(__X, __Y);
}

/* Intel-specified, single-leading-underscore version of BEXTR */
/// Extracts the specified bits from the first operand and returns them
///    in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned integer whose bits are to be extracted.
/// \param __Y
///    An unsigned integer used to specify the index of the least significant
///    bit for the bits to be extracted. Bits [7:0] specify the index.
/// \param __Z
///    An unsigned integer used to specify the number of bits to be extracted.
///    Bits [7:0] specify the number of bits.
/// \returns An unsigned integer whose least significant bits contain the
///    extracted bits.
/// \see __bextr_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_bextr_u32(unsigned int __X, unsigned int __Y, unsigned int __Z)
{
  return __builtin_ia32_bextr_u32 (__X, ((__Y & 0xff) | ((__Z & 0xff) << 8)));
}

/* Intel-specified, single-leading-underscore version of BEXTR2 */
/// Extracts the specified bits from the first operand and returns them
///    in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned integer whose bits are to be extracted.
/// \param __Y
///    An unsigned integer used to specify which bits are extracted. Bits [7:0]
///    specify the index of the least significant bit. Bits [15:8] specify the
///    number of bits to be extracted.
/// \returns An unsigned integer whose least significant bits contain the
///    extracted bits.
/// \see __bextr_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_bextr2_u32(unsigned int __X, unsigned int __Y) {
  return __builtin_ia32_bextr_u32(__X, __Y);
}

/// Clears all bits in the source except for the least significant bit
///    containing a value of 1 and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSI instruction.
///
/// \param __X
///    An unsigned integer whose bits are to be cleared.
/// \returns An unsigned integer containing the result of clearing the bits from
///    the source operand.
/// \see _blsi_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__blsi_u32(unsigned int __X)
{
  return __X & -__X;
}

/// Clears all bits in the source except for the least significant bit
///    containing a value of 1 and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _blsi_u32(unsigned int __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSI instruction.
///
/// \param __X
///    An unsigned integer whose bits are to be cleared.
/// \returns An unsigned integer containing the result of clearing the bits from
///    the source operand.
/// \see __blsi_u32
#define _blsi_u32 __blsi_u32

/// Creates a mask whose bits are set to 1, using bit 0 up to and
///    including the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSMSK instruction.
///
/// \param __X
///    An unsigned integer used to create the mask.
/// \returns An unsigned integer containing the newly created mask.
/// \see _blsmsk_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__blsmsk_u32(unsigned int __X)
{
  return __X ^ (__X - 1);
}

/// Creates a mask whose bits are set to 1, using bit 0 up to and
///    including the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _blsmsk_u32(unsigned int __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSMSK instruction.
///
/// \param __X
///    An unsigned integer used to create the mask.
/// \returns An unsigned integer containing the newly created mask.
/// \see __blsmsk_u32
#define _blsmsk_u32 __blsmsk_u32

/// Clears the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSR instruction.
///
/// \param __X
///    An unsigned integer containing the operand to be cleared.
/// \returns An unsigned integer containing the result of clearing the source
///    operand.
/// \see _blsr_u32
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__blsr_u32(unsigned int __X)
{
  return __X & (__X - 1);
}

/// Clears the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _bls4_u32(unsigned int __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSR instruction.
///
/// \param __X
///    An unsigned integer containing the operand to be cleared.
/// \returns An unsigned integer containing the result of clearing the source
///    operand.
/// \see __blsr_u32
#define _blsr_u32 __blsr_u32

#ifdef __x86_64__

/// Performs a bitwise AND of the second operand with the one's
///    complement of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ANDN instruction.
///
/// \param __X
///    An unsigned 64-bit integer containing one of the operands.
/// \param __Y
///    An unsigned 64-bit integer containing one of the operands.
/// \returns An unsigned 64-bit integer containing the bitwise AND of the second
///    operand with the one's complement of the first operand.
/// \see _andn_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__andn_u64 (unsigned long long __X, unsigned long long __Y)
{
  return ~__X & __Y;
}

/// Performs a bitwise AND of the second operand with the one's
///    complement of the first operand.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _andn_u64(unsigned long long __X,
///                              unsigned long long __Y);
/// \endcode
///
/// This intrinsic corresponds to the \c ANDN instruction.
///
/// \param __X
///    An unsigned 64-bit integer containing one of the operands.
/// \param __Y
///    An unsigned 64-bit integer containing one of the operands.
/// \returns An unsigned 64-bit integer containing the bitwise AND of the second
///    operand with the one's complement of the first operand.
/// \see __andn_u64
#define _andn_u64 __andn_u64

/* AMD-specified, double-leading-underscore version of BEXTR */
/// Extracts the specified bits from the first operand and returns them
///    in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose bits are to be extracted.
/// \param __Y
///    An unsigned 64-bit integer used to specify which bits are extracted. Bits
///    [7:0] specify the index of the least significant bit. Bits [15:8] specify
///    the number of bits to be extracted.
/// \returns An unsigned 64-bit integer whose least significant bits contain the
///    extracted bits.
/// \see _bextr_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__bextr_u64(unsigned long long __X, unsigned long long __Y)
{
  return __builtin_ia32_bextr_u64(__X, __Y);
}

/* Intel-specified, single-leading-underscore version of BEXTR */
/// Extracts the specified bits from the first operand and returns them
///     in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose bits are to be extracted.
/// \param __Y
///    An unsigned integer used to specify the index of the least significant
///    bit for the bits to be extracted. Bits [7:0] specify the index.
/// \param __Z
///    An unsigned integer used to specify the number of bits to be extracted.
///    Bits [7:0] specify the number of bits.
/// \returns An unsigned 64-bit integer whose least significant bits contain the
///    extracted bits.
/// \see __bextr_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_bextr_u64(unsigned long long __X, unsigned int __Y, unsigned int __Z)
{
  return __builtin_ia32_bextr_u64 (__X, ((__Y & 0xff) | ((__Z & 0xff) << 8)));
}

/* Intel-specified, single-leading-underscore version of BEXTR2 */
/// Extracts the specified bits from the first operand and returns them
///    in the least significant bits of the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BEXTR instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose bits are to be extracted.
/// \param __Y
///    An unsigned 64-bit integer used to specify which bits are extracted. Bits
///    [7:0] specify the index of the least significant bit. Bits [15:8] specify
///    the number of bits to be extracted.
/// \returns An unsigned 64-bit integer whose least significant bits contain the
///    extracted bits.
/// \see __bextr_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_bextr2_u64(unsigned long long __X, unsigned long long __Y) {
  return __builtin_ia32_bextr_u64(__X, __Y);
}

/// Clears all bits in the source except for the least significant bit
///    containing a value of 1 and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSI instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose bits are to be cleared.
/// \returns An unsigned 64-bit integer containing the result of clearing the
///    bits from the source operand.
/// \see _blsi_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__blsi_u64(unsigned long long __X)
{
  return __X & -__X;
}

/// Clears all bits in the source except for the least significant bit
///    containing a value of 1 and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _blsi_u64(unsigned long long __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSI instruction.
///
/// \param __X
///    An unsigned 64-bit integer whose bits are to be cleared.
/// \returns An unsigned 64-bit integer containing the result of clearing the
///    bits from the source operand.
/// \see __blsi_u64
#define _blsi_u64 __blsi_u64

/// Creates a mask whose bits are set to 1, using bit 0 up to and
///    including the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSMSK instruction.
///
/// \param __X
///    An unsigned 64-bit integer used to create the mask.
/// \returns An unsigned 64-bit integer containing the newly created mask.
/// \see _blsmsk_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__blsmsk_u64(unsigned long long __X)
{
  return __X ^ (__X - 1);
}

/// Creates a mask whose bits are set to 1, using bit 0 up to and
///    including the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _blsmsk_u64(unsigned long long __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSMSK instruction.
///
/// \param __X
///    An unsigned 64-bit integer used to create the mask.
/// \returns An unsigned 64-bit integer containing the newly created mask.
/// \see __blsmsk_u64
#define _blsmsk_u64 __blsmsk_u64

/// Clears the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BLSR instruction.
///
/// \param __X
///    An unsigned 64-bit integer containing the operand to be cleared.
/// \returns An unsigned 64-bit integer containing the result of clearing the
///    source operand.
/// \see _blsr_u64
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__blsr_u64(unsigned long long __X)
{
  return __X & (__X - 1);
}

/// Clears the least significant bit that is set to 1 in the source
///    operand and returns the result.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _blsr_u64(unsigned long long __X);
/// \endcode
///
/// This intrinsic corresponds to the \c BLSR instruction.
///
/// \param __X
///    An unsigned 64-bit integer containing the operand to be cleared.
/// \returns An unsigned 64-bit integer containing the result of clearing the
///    source operand.
/// \see __blsr_u64
#define _blsr_u64 __blsr_u64

#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* !defined(__SCE__) || __has_feature(modules) || defined(__BMI__) */

#endif /* __BMIINTRIN_H */
