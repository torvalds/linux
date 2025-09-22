/* ===-------- ia32intrin.h ---------------------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __X86INTRIN_H
#error "Never use <ia32intrin.h> directly; include <x86intrin.h> instead."
#endif

#ifndef __IA32INTRIN_H
#define __IA32INTRIN_H

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS __attribute__((__always_inline__, __nodebug__))
#define __DEFAULT_FN_ATTRS_CRC32 __attribute__((__always_inline__, __nodebug__, __target__("crc32")))

#if defined(__cplusplus) && (__cplusplus >= 201103L)
#define __DEFAULT_FN_ATTRS_CAST __attribute__((__always_inline__)) constexpr
#define __DEFAULT_FN_ATTRS_CONSTEXPR __DEFAULT_FN_ATTRS constexpr
#else
#define __DEFAULT_FN_ATTRS_CAST __attribute__((__always_inline__))
#define __DEFAULT_FN_ATTRS_CONSTEXPR __DEFAULT_FN_ATTRS
#endif

/// Finds the first set bit starting from the least significant bit. The result
///    is undefined if the input is 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSF instruction or the
///    \c TZCNT instruction.
///
/// \param __A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
/// \see _bit_scan_forward
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__bsfd(int __A) {
  return __builtin_ctz((unsigned int)__A);
}

/// Finds the first set bit starting from the most significant bit. The result
///    is undefined if the input is 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSR instruction or the
///    \c LZCNT instruction and an \c XOR.
///
/// \param __A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
/// \see _bit_scan_reverse
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__bsrd(int __A) {
  return 31 - __builtin_clz((unsigned int)__A);
}

/// Swaps the bytes in the input, converting little endian to big endian or
///    vice versa.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSWAP instruction.
///
/// \param __A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the swapped bytes.
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__bswapd(int __A) {
  return (int)__builtin_bswap32((unsigned int)__A);
}

/// Swaps the bytes in the input, converting little endian to big endian or
///    vice versa.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSWAP instruction.
///
/// \param __A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the swapped bytes.
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
_bswap(int __A) {
  return (int)__builtin_bswap32((unsigned int)__A);
}

/// Finds the first set bit starting from the least significant bit. The result
///    is undefined if the input is 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _bit_scan_forward(int A);
/// \endcode
///
/// This intrinsic corresponds to the \c BSF instruction or the
///    \c TZCNT instruction.
///
/// \param A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
/// \see __bsfd
#define _bit_scan_forward(A) __bsfd((A))

/// Finds the first set bit starting from the most significant bit. The result
///    is undefined if the input is 0.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _bit_scan_reverse(int A);
/// \endcode
///
/// This intrinsic corresponds to the \c BSR instruction or the
///    \c LZCNT instruction and an \c XOR.
///
/// \param A
///    A 32-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
/// \see __bsrd
#define _bit_scan_reverse(A) __bsrd((A))

#ifdef __x86_64__
/// Finds the first set bit starting from the least significant bit. The result
///    is undefined if the input is 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSF instruction or the
///    \c TZCNT instruction.
///
/// \param __A
///    A 64-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__bsfq(long long __A) {
  return (long long)__builtin_ctzll((unsigned long long)__A);
}

/// Finds the first set bit starting from the most significant bit. The result
///    is undefined if input is 0.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSR instruction or the
///    \c LZCNT instruction and an \c XOR.
///
/// \param __A
///    A 64-bit integer operand.
/// \returns A 32-bit integer containing the bit number.
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__bsrq(long long __A) {
  return 63 - __builtin_clzll((unsigned long long)__A);
}

/// Swaps the bytes in the input, converting little endian to big endian or
///    vice versa.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c BSWAP instruction.
///
/// \param __A
///    A 64-bit integer operand.
/// \returns A 64-bit integer containing the swapped bytes.
/// \see _bswap64
static __inline__ long long __DEFAULT_FN_ATTRS_CONSTEXPR
__bswapq(long long __A) {
  return (long long)__builtin_bswap64((unsigned long long)__A);
}

/// Swaps the bytes in the input, converting little endian to big endian or
///    vice versa.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// long long _bswap64(long long A);
/// \endcode
///
/// This intrinsic corresponds to the \c BSWAP instruction.
///
/// \param A
///    A 64-bit integer operand.
/// \returns A 64-bit integer containing the swapped bytes.
/// \see __bswapq
#define _bswap64(A) __bswapq((A))
#endif /* __x86_64__ */

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c POPCNT instruction or a
///    sequence of arithmetic and logic operations to calculate it.
///
/// \param __A
///    An unsigned 32-bit integer operand.
/// \returns A 32-bit integer containing the number of bits with value 1 in the
///    source operand.
/// \see _popcnt32
static __inline__ int __DEFAULT_FN_ATTRS_CONSTEXPR
__popcntd(unsigned int __A)
{
  return __builtin_popcount(__A);
}

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// int _popcnt32(int A);
/// \endcode
///
/// This intrinsic corresponds to the \c POPCNT instruction or a
///    sequence of arithmetic and logic operations to calculate it.
///
/// \param A
///    An unsigned 32-bit integer operand.
/// \returns A 32-bit integer containing the number of bits with value 1 in the
///    source operand.
/// \see __popcntd
#define _popcnt32(A) __popcntd((A))

#ifdef __x86_64__
/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c POPCNT instruction or a
///    sequence of arithmetic and logic operations to calculate it.
///
/// \param __A
///    An unsigned 64-bit integer operand.
/// \returns A 64-bit integer containing the number of bits with value 1 in the
///    source operand.
/// \see _popcnt64
static __inline__ long long __DEFAULT_FN_ATTRS_CONSTEXPR
__popcntq(unsigned long long __A)
{
  return __builtin_popcountll(__A);
}

/// Counts the number of bits in the source operand having a value of 1.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// long long _popcnt64(unsigned long long A);
/// \endcode
///
/// This intrinsic corresponds to the \c POPCNT instruction or a
///    sequence of arithmetic and logic operations to calculate it.
///
/// \param A
///    An unsigned 64-bit integer operand.
/// \returns A 64-bit integer containing the number of bits with value 1 in the
///    source operand.
/// \see __popcntq
#define _popcnt64(A) __popcntq((A))
#endif /* __x86_64__ */

#ifdef __x86_64__
/// Returns the program status-and-control \c RFLAGS register with the \c VM
///    and \c RF flags cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PUSHFQ + \c POP instruction sequence.
///
/// \returns The 64-bit value of the RFLAGS register.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__readeflags(void)
{
  return __builtin_ia32_readeflags_u64();
}

/// Writes the specified value to the program status-and-control \c RFLAGS
///    register. Reserved bits are not affected.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PUSH + \c POPFQ instruction sequence.
///
/// \param __f
///    The 64-bit value to write to \c RFLAGS.
static __inline__ void __DEFAULT_FN_ATTRS
__writeeflags(unsigned long long __f)
{
  __builtin_ia32_writeeflags_u64(__f);
}

#else /* !__x86_64__ */
/// Returns the program status-and-control \c EFLAGS register with the \c VM
///    and \c RF flags cleared.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PUSHFD + \c POP instruction sequence.
///
/// \returns The 32-bit value of the EFLAGS register.
static __inline__ unsigned int __DEFAULT_FN_ATTRS
__readeflags(void)
{
  return __builtin_ia32_readeflags_u32();
}

/// Writes the specified value to the program status-and-control \c EFLAGS
///    register. Reserved bits are not affected.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c PUSH + \c POPFD instruction sequence.
///
/// \param __f
///    The 32-bit value to write to \c EFLAGS.
static __inline__ void __DEFAULT_FN_ATTRS
__writeeflags(unsigned int __f)
{
  __builtin_ia32_writeeflags_u32(__f);
}
#endif /* !__x86_64__ */

/// Casts a 32-bit float value to a 32-bit unsigned integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VMOVD / \c MOVD instruction in x86_64,
///    and corresponds to the \c VMOVL / \c MOVL instruction in ia32.
///
/// \param __A
///    A 32-bit float value.
/// \returns A 32-bit unsigned integer containing the converted value.
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CAST
_castf32_u32(float __A) {
  return __builtin_bit_cast(unsigned int, __A);
}

/// Casts a 64-bit float value to a 64-bit unsigned integer value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VMOVQ / \c MOVQ instruction in x86_64,
///    and corresponds to the \c VMOVL / \c MOVL instruction in ia32.
///
/// \param __A
///    A 64-bit float value.
/// \returns A 64-bit unsigned integer containing the converted value.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS_CAST
_castf64_u64(double __A) {
  return __builtin_bit_cast(unsigned long long, __A);
}

/// Casts a 32-bit unsigned integer value to a 32-bit float value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VMOVQ / \c MOVQ instruction in x86_64,
///    and corresponds to the \c FLDS instruction in ia32.
///
/// \param __A
///    A 32-bit unsigned integer value.
/// \returns A 32-bit float value containing the converted value.
static __inline__ float __DEFAULT_FN_ATTRS_CAST
_castu32_f32(unsigned int __A) {
  return __builtin_bit_cast(float, __A);
}

/// Casts a 64-bit unsigned integer value to a 64-bit float value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c VMOVQ / \c MOVQ instruction in x86_64,
///    and corresponds to the \c FLDL instruction in ia32.
///
/// \param __A
///    A 64-bit unsigned integer value.
/// \returns A 64-bit float value containing the converted value.
static __inline__ double __DEFAULT_FN_ATTRS_CAST
_castu64_f64(unsigned long long __A) {
  return __builtin_bit_cast(double, __A);
}

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///     unsigned char operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c CRC32B instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 8-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CRC32
__crc32b(unsigned int __C, unsigned char __D)
{
  return __builtin_ia32_crc32qi(__C, __D);
}

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned short operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c CRC32W instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 16-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CRC32
__crc32w(unsigned int __C, unsigned short __D)
{
  return __builtin_ia32_crc32hi(__C, __D);
}

/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    second unsigned integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c CRC32D instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 32-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CRC32
__crc32d(unsigned int __C, unsigned int __D)
{
  return __builtin_ia32_crc32si(__C, __D);
}

#ifdef __x86_64__
/// Adds the unsigned integer operand to the CRC-32C checksum of the
///    unsigned 64-bit integer operand.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c CRC32Q instruction.
///
/// \param __C
///    An unsigned integer operand to add to the CRC-32C checksum of operand
///    \a  __D.
/// \param __D
///    An unsigned 64-bit integer operand used to compute the CRC-32C checksum.
/// \returns The result of adding operand \a __C to the CRC-32C checksum of
///    operand \a __D.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS_CRC32
__crc32q(unsigned long long __C, unsigned long long __D)
{
  return __builtin_ia32_crc32di(__C, __D);
}
#endif /* __x86_64__ */

/// Reads the specified performance-monitoring counter. Refer to your
///    processor's documentation to determine which performance counters are
///    supported.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c RDPMC instruction.
///
/// \param __A
///    The performance counter to read.
/// \returns The 64-bit value read from the performance counter.
/// \see _rdpmc
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__rdpmc(int __A) {
  return __builtin_ia32_rdpmc(__A);
}

/// Reads the processor's time-stamp counter and the \c IA32_TSC_AUX MSR
///    \c (0xc0000103).
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c RDTSCP instruction.
///
/// \param __A
///    The address of where to store the 32-bit \c IA32_TSC_AUX value.
/// \returns The 64-bit value of the time-stamp counter.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS
__rdtscp(unsigned int *__A) {
  return __builtin_ia32_rdtscp(__A);
}

/// Reads the processor's time-stamp counter.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _rdtsc();
/// \endcode
///
/// This intrinsic corresponds to the \c RDTSC instruction.
///
/// \returns The 64-bit value of the time-stamp counter.
#define _rdtsc() __rdtsc()

/// Reads the specified performance monitoring counter. Refer to your
///    processor's documentation to determine which performance counters are
///    supported.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _rdpmc(int A);
/// \endcode
///
/// This intrinsic corresponds to the \c RDPMC instruction.
///
/// \param A
///    The performance counter to read.
/// \returns The 64-bit value read from the performance counter.
/// \see __rdpmc
#define _rdpmc(A) __rdpmc(A)

static __inline__ void __DEFAULT_FN_ATTRS
_wbinvd(void) {
  __builtin_ia32_wbinvd();
}

/// Rotates an 8-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param __X
///    The unsigned 8-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
static __inline__ unsigned char __DEFAULT_FN_ATTRS_CONSTEXPR
__rolb(unsigned char __X, int __C) {
  return __builtin_rotateleft8(__X, __C);
}

/// Rotates an 8-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param __X
///    The unsigned 8-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
static __inline__ unsigned char __DEFAULT_FN_ATTRS_CONSTEXPR
__rorb(unsigned char __X, int __C) {
  return __builtin_rotateright8(__X, __C);
}

/// Rotates a 16-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param __X
///    The unsigned 16-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see _rotwl
static __inline__ unsigned short __DEFAULT_FN_ATTRS_CONSTEXPR
__rolw(unsigned short __X, int __C) {
  return __builtin_rotateleft16(__X, __C);
}

/// Rotates a 16-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param __X
///    The unsigned 16-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see _rotwr
static __inline__ unsigned short __DEFAULT_FN_ATTRS_CONSTEXPR
__rorw(unsigned short __X, int __C) {
  return __builtin_rotateright16(__X, __C);
}

/// Rotates a 32-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param __X
///    The unsigned 32-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see _rotl
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CONSTEXPR
__rold(unsigned int __X, int __C) {
  return __builtin_rotateleft32(__X, (unsigned int)__C);
}

/// Rotates a 32-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param __X
///    The unsigned 32-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see _rotr
static __inline__ unsigned int __DEFAULT_FN_ATTRS_CONSTEXPR
__rord(unsigned int __X, int __C) {
  return __builtin_rotateright32(__X, (unsigned int)__C);
}

#ifdef __x86_64__
/// Rotates a 64-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param __X
///    The unsigned 64-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS_CONSTEXPR
__rolq(unsigned long long __X, int __C) {
  return __builtin_rotateleft64(__X, (unsigned long long)__C);
}

/// Rotates a 64-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param __X
///    The unsigned 64-bit value to be rotated.
/// \param __C
///    The number of bits to rotate the value.
/// \returns The rotated value.
static __inline__ unsigned long long __DEFAULT_FN_ATTRS_CONSTEXPR
__rorq(unsigned long long __X, int __C) {
  return __builtin_rotateright64(__X, (unsigned long long)__C);
}
#endif /* __x86_64__ */

#ifndef _MSC_VER
/* These are already provided as builtins for MSVC. */
/* Select the correct function based on the size of long. */
#ifdef __LP64__
/// Rotates a 64-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _lrotl(unsigned long long a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param a
///    The unsigned 64-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rolq
#define _lrotl(a,b) __rolq((a), (b))

/// Rotates a 64-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned long long _lrotr(unsigned long long a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param a
///    The unsigned 64-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rorq
#define _lrotr(a,b) __rorq((a), (b))
#else // __LP64__
/// Rotates a 32-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _lrotl(unsigned int a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param a
///    The unsigned 32-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rold
#define _lrotl(a,b) __rold((a), (b))

/// Rotates a 32-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _lrotr(unsigned int a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param a
///    The unsigned 32-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rord
#define _lrotr(a,b) __rord((a), (b))
#endif // __LP64__

/// Rotates a 32-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _rotl(unsigned int a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param a
///    The unsigned 32-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rold
#define _rotl(a,b) __rold((a), (b))

/// Rotates a 32-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned int _rotr(unsigned int a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param a
///    The unsigned 32-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rord
#define _rotr(a,b) __rord((a), (b))
#endif // _MSC_VER

/* These are not builtins so need to be provided in all modes. */
/// Rotates a 16-bit value to the left by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned short _rotwl(unsigned short a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROL instruction.
///
/// \param a
///    The unsigned 16-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rolw
#define _rotwl(a,b) __rolw((a), (b))

/// Rotates a 16-bit value to the right by the specified number of bits.
///    This operation is undefined if the number of bits exceeds the size of
///    the value.
///
/// \headerfile <x86intrin.h>
///
/// \code
/// unsigned short _rotwr(unsigned short a, int b);
/// \endcode
///
/// This intrinsic corresponds to the \c ROR instruction.
///
/// \param a
///    The unsigned 16-bit value to be rotated.
/// \param b
///    The number of bits to rotate the value.
/// \returns The rotated value.
/// \see __rorw
#define _rotwr(a,b) __rorw((a), (b))

#undef __DEFAULT_FN_ATTRS
#undef __DEFAULT_FN_ATTRS_CAST
#undef __DEFAULT_FN_ATTRS_CRC32
#undef __DEFAULT_FN_ATTRS_CONSTEXPR

#endif /* __IA32INTRIN_H */
