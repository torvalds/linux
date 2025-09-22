/*===------------------ enqcmdintrin.h - enqcmd intrinsics -----------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <enqcmdintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __ENQCMDINTRIN_H
#define __ENQCMDINTRIN_H

/* Define the default attributes for the functions in this file */
#define _DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("enqcmd")))

/// Reads 64-byte command pointed by \a __src, formats 64-byte enqueue store
///    data, and performs 64-byte enqueue store to memory pointed by \a __dst.
///    This intrinsics may only be used in User mode.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsics corresponds to the <c> ENQCMD </c> instruction.
///
/// \param __dst
///    Pointer to the destination of the enqueue store.
/// \param __src
///    Pointer to 64-byte command data.
/// \returns If the command data is successfully written to \a __dst then 0 is
///    returned. Otherwise 1 is returned.
static __inline__ int _DEFAULT_FN_ATTRS
_enqcmd (void *__dst, const void *__src)
{
  return __builtin_ia32_enqcmd(__dst, __src);
}

/// Reads 64-byte command pointed by \a __src, formats 64-byte enqueue store
///    data, and performs 64-byte enqueue store to memory pointed by \a __dst
///    This intrinsic may only be used in Privileged mode.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsics corresponds to the <c> ENQCMDS </c> instruction.
///
/// \param __dst
///    Pointer to the destination of the enqueue store.
/// \param __src
///    Pointer to 64-byte command data.
/// \returns If the command data is successfully written to \a __dst then 0 is
///    returned. Otherwise 1 is returned.
static __inline__ int _DEFAULT_FN_ATTRS
_enqcmds (void *__dst, const void *__src)
{
  return __builtin_ia32_enqcmds(__dst, __src);
}

#undef _DEFAULT_FN_ATTRS

#endif /* __ENQCMDINTRIN_H */
