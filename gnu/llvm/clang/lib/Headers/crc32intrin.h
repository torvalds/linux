/*===---- crc32intrin.h - SSE4.2 Accumulate CRC32 intrinsics ---------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CRC32INTRIN_H
#define __CRC32INTRIN_H

#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__, __target__("crc32")))

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned char operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32B </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 8-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u8(unsigned int __C, unsigned char __D)
{
  return __builtin_ia32_crc32qi(__C, __D);
}

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned short operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32W </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 16-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u16(unsigned int __C, unsigned short __D)
{
  return __builtin_ia32_crc32hi(__C, __D);
}

/// Adds the first unsigned integer operand to the CRC-32C checksum of
///    the second unsigned integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32L </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 32-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
_mm_crc32_u32(unsigned int __C, unsigned int __D)
{
  return __builtin_ia32_crc32si(__C, __D);
}

#ifdef __x86_64__
/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned 64-bit integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> CRC32Q </c> instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a __D.
/// \param __D
///    An unsigned 64-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
_mm_crc32_u64(unsigned long long __C, unsigned long long __D)
{
  return __builtin_ia32_crc32di(__C, __D);
}
#endif /* __x86_64__ */

#undef __DEFAULT_FN_ATTRS

#endif /* __CRC32INTRIN_H */
