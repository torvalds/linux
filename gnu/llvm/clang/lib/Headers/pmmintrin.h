/*===---- pmmintrin.h - SSE3 intrinsics ------------------------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __PMMINTRIN_H
#define __PMMINTRIN_H

#if !defined(__i386__) && !defined(__x86_64__)
#error "This header is only meant to be used on x86 and x64 architecture"
#endif

#include <emmintrin.h>

/* Define the default attributes for the functions in this file. */
#define __DEFAULT_FN_ATTRS                                                     \
  __attribute__((__always_inline__, __nodebug__,                               \
                 __target__("sse3,no-evex512"), __min_vector_width__(128)))

/// Loads data from an unaligned memory location to elements in a 128-bit
///    vector.
///
///    If the address of the data is not 16-byte aligned, the instruction may
///    read two adjacent aligned blocks of memory to retrieve the requested
///    data.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VLDDQU </c> instruction.
///
/// \param __p
///    A pointer to a 128-bit integer vector containing integer values.
/// \returns A 128-bit vector containing the moved values.
static __inline__ __m128i __DEFAULT_FN_ATTRS
_mm_lddqu_si128(__m128i_u const *__p)
{
  return (__m128i)__builtin_ia32_lddqu((char const *)__p);
}

/// Adds the even-indexed values and subtracts the odd-indexed values of
///    two 128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSUBPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing the left source operand.
/// \param __b
///    A 128-bit vector of [4 x float] containing the right source operand.
/// \returns A 128-bit vector of [4 x float] containing the alternating sums and
///    differences of both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_addsub_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_addsubps((__v4sf)__a, (__v4sf)__b);
}

/// Horizontally adds the adjacent pairs of values contained in two
///    128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHADDPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The horizontal sums of the values are stored in the lower bits of the
///    destination.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The horizontal sums of the values are stored in the upper bits of the
///    destination.
/// \returns A 128-bit vector of [4 x float] containing the horizontal sums of
///    both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_hadd_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_haddps((__v4sf)__a, (__v4sf)__b);
}

/// Horizontally subtracts the adjacent pairs of values contained in two
///    128-bit vectors of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHSUBPS </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The horizontal differences between the values are stored in the lower
///    bits of the destination.
/// \param __b
///    A 128-bit vector of [4 x float] containing one of the source operands.
///    The horizontal differences between the values are stored in the upper
///    bits of the destination.
/// \returns A 128-bit vector of [4 x float] containing the horizontal
///    differences of both operands.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_hsub_ps(__m128 __a, __m128 __b)
{
  return __builtin_ia32_hsubps((__v4sf)__a, (__v4sf)__b);
}

/// Moves and duplicates odd-indexed values from a 128-bit vector
///    of [4 x float] to float values stored in a 128-bit vector of
///    [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSHDUP </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float]. \n
///    Bits [127:96] of the source are written to bits [127:96] and [95:64] of
///    the destination. \n
///    Bits [63:32] of the source are written to bits [63:32] and [31:0] of the
///    destination.
/// \returns A 128-bit vector of [4 x float] containing the moved and duplicated
///    values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_movehdup_ps(__m128 __a)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 1, 1, 3, 3);
}

/// Duplicates even-indexed values from a 128-bit vector of
///    [4 x float] to float values stored in a 128-bit vector of [4 x float].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVSLDUP </c> instruction.
///
/// \param __a
///    A 128-bit vector of [4 x float] \n
///    Bits [95:64] of the source are written to bits [127:96] and [95:64] of
///    the destination. \n
///    Bits [31:0] of the source are written to bits [63:32] and [31:0] of the
///    destination.
/// \returns A 128-bit vector of [4 x float] containing the moved and duplicated
///    values.
static __inline__ __m128 __DEFAULT_FN_ATTRS
_mm_moveldup_ps(__m128 __a)
{
  return __builtin_shufflevector((__v4sf)__a, (__v4sf)__a, 0, 0, 2, 2);
}

/// Adds the even-indexed values and subtracts the odd-indexed values of
///    two 128-bit vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VADDSUBPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing the left source operand.
/// \param __b
///    A 128-bit vector of [2 x double] containing the right source operand.
/// \returns A 128-bit vector of [2 x double] containing the alternating sums
///    and differences of both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_addsub_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_addsubpd((__v2df)__a, (__v2df)__b);
}

/// Horizontally adds the pairs of values contained in two 128-bit
///    vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHADDPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
///    The horizontal sum of the values is stored in the lower bits of the
///    destination.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
///    The horizontal sum of the values is stored in the upper bits of the
///    destination.
/// \returns A 128-bit vector of [2 x double] containing the horizontal sums of
///    both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_hadd_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_haddpd((__v2df)__a, (__v2df)__b);
}

/// Horizontally subtracts the pairs of values contained in two 128-bit
///    vectors of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VHSUBPD </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double] containing one of the source operands.
///    The horizontal difference of the values is stored in the lower bits of
///    the destination.
/// \param __b
///    A 128-bit vector of [2 x double] containing one of the source operands.
///    The horizontal difference of the values is stored in the upper bits of
///    the destination.
/// \returns A 128-bit vector of [2 x double] containing the horizontal
///    differences of both operands.
static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_hsub_pd(__m128d __a, __m128d __b)
{
  return __builtin_ia32_hsubpd((__v2df)__a, (__v2df)__b);
}

/// Moves and duplicates one double-precision value to double-precision
///    values stored in a 128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// \code
/// __m128d _mm_loaddup_pd(double const *dp);
/// \endcode
///
/// This intrinsic corresponds to the <c> VMOVDDUP </c> instruction.
///
/// \param dp
///    A pointer to a double-precision value to be moved and duplicated.
/// \returns A 128-bit vector of [2 x double] containing the moved and
///    duplicated values.
#define        _mm_loaddup_pd(dp)        _mm_load1_pd(dp)

/// Moves and duplicates the double-precision value in the lower bits of
///    a 128-bit vector of [2 x double] to double-precision values stored in a
///    128-bit vector of [2 x double].
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> VMOVDDUP </c> instruction.
///
/// \param __a
///    A 128-bit vector of [2 x double]. Bits [63:0] are written to bits
///    [127:64] and [63:0] of the destination.
/// \returns A 128-bit vector of [2 x double] containing the moved and
///    duplicated values.
static __inline__ __m128d __DEFAULT_FN_ATTRS
_mm_movedup_pd(__m128d __a)
{
  return __builtin_shufflevector((__v2df)__a, (__v2df)__a, 0, 0);
}

/// Establishes a linear address memory range to be monitored and puts
///    the processor in the monitor event pending state. Data stored in the
///    monitored address range causes the processor to exit the pending state.
///
/// The \c MONITOR instruction can be used in kernel mode, and in other modes
/// if MSR <c> C001_0015h[MonMwaitUserEn] </c> is set.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c MONITOR instruction.
///
/// \param __p
///    The memory range to be monitored. The size of the range is determined by
///    CPUID function 0000_0005h.
/// \param __extensions
///    Optional extensions for the monitoring state.
/// \param __hints
///    Optional hints for the monitoring state.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_monitor(void const *__p, unsigned __extensions, unsigned __hints)
{
  __builtin_ia32_monitor(__p, __extensions, __hints);
}

/// Used with the \c MONITOR instruction to wait while the processor is in
///    the monitor event pending state. Data stored in the monitored address
///    range, or an interrupt, causes the processor to exit the pending state.
///
/// The \c MWAIT instruction can be used in kernel mode, and in other modes if
/// MSR <c> C001_0015h[MonMwaitUserEn] </c> is set.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c MWAIT instruction.
///
/// \param __extensions
///    Optional extensions for the monitoring state, which can vary by
///    processor.
/// \param __hints
///    Optional hints for the monitoring state, which can vary by processor.
static __inline__ void __DEFAULT_FN_ATTRS
_mm_mwait(unsigned __extensions, unsigned __hints)
{
  __builtin_ia32_mwait(__extensions, __hints);
}

#undef __DEFAULT_FN_ATTRS

#endif /* __PMMINTRIN_H */
