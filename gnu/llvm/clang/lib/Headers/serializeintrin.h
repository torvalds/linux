/*===--------------- serializeintrin.h - serialize intrinsics --------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <serializeintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __SERIALIZEINTRIN_H
#define __SERIALIZEINTRIN_H

/// Serialize instruction fetch and execution.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the <c> SERIALIZE </c> instruction.
///
static __inline__ void
__attribute__((__always_inline__, __nodebug__, __target__("serialize")))
_serialize (void)
{
  __builtin_ia32_serialize ();
}

#endif /* __SERIALIZEINTRIN_H */
