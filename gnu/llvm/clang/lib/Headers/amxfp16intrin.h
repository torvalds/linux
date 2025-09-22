/*===------------- amxfp16intrin.h - AMX_FP16 intrinsics -*- C++ -*---------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===------------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <amxfp16intrin.h> directly; use <immintrin.h> instead."
#endif /* __IMMINTRIN_H */

#ifndef __AMX_FP16INTRIN_H
#define __AMX_FP16INTRIN_H
#ifdef __x86_64__

/// Compute dot-product of FP16 (16-bit) floating-point pairs in tiles \a a
///    and \a b, accumulating the intermediate single-precision (32-bit)
///    floating-point elements with elements in \a dst, and store the 32-bit
///    result back to tile \a dst.
///
/// \headerfile <immintrin.h>
///
/// \code
/// void _tile_dpfp16ps (__tile dst, __tile a, __tile b)
/// \endcode
///
/// \code{.operation}
/// FOR m := 0 TO dst.rows - 1
///	tmp := dst.row[m]
///	FOR k := 0 TO (a.colsb / 4) - 1
///		FOR n := 0 TO (dst.colsb / 4) - 1
///			tmp.fp32[n] += FP32(a.row[m].fp16[2*k+0]) *
///					FP32(b.row[k].fp16[2*n+0])
///			tmp.fp32[n] += FP32(a.row[m].fp16[2*k+1]) *
///					FP32(b.row[k].fp16[2*n+1])
///		ENDFOR
///	ENDFOR
///	write_row_and_zero(dst, m, tmp, dst.colsb)
/// ENDFOR
/// zero_upper_rows(dst, dst.rows)
/// zero_tileconfig_start()
/// \endcode
///
/// This intrinsic corresponds to the \c TDPFP16PS instruction.
///
/// \param dst
///    The destination tile. Max size is 1024 Bytes.
/// \param a
///    The 1st source tile. Max size is 1024 Bytes.
/// \param b
///    The 2nd source tile. Max size is 1024 Bytes.
#define _tile_dpfp16ps(dst, a, b)                                \
  __builtin_ia32_tdpfp16ps(dst, a, b)

#endif /* __x86_64__ */
#endif /* __AMX_FP16INTRIN_H */
