/*===------------- tsxldtrkintrin.h - tsxldtrk intrinsics ------------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __IMMINTRIN_H
#error "Never use <tsxldtrkintrin.h> directly; include <immintrin.h> instead."
#endif

#ifndef __TSXLDTRKINTRIN_H
#define __TSXLDTRKINTRIN_H

/* Define the default attributes for the functions in this file */
#define _DEFAULT_FN_ATTRS \
  __attribute__((__always_inline__, __nodebug__, __target__("tsxldtrk")))

/// Marks the start of an TSX (RTM) suspend load address tracking region. If
///    this intrinsic is used inside a transactional region, subsequent loads
///    are not added to the read set of the transaction. If it's used inside a
///    suspend load address tracking region it will cause transaction abort.
///    If it's used outside of a transactional region it behaves like a NOP.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c XSUSLDTRK instruction.
///
static __inline__ void _DEFAULT_FN_ATTRS
_xsusldtrk (void)
{
    __builtin_ia32_xsusldtrk();
}

/// Marks the end of an TSX (RTM) suspend load address tracking region. If this
///    intrinsic is used inside a suspend load address tracking region it will
///    end the suspend region and all following load addresses will be added to
///    the transaction read set. If it's used inside an active transaction but
///    not in a suspend region it will cause transaction abort. If it's used
///    outside of a transactional region it behaves like a NOP.
///
/// \headerfile <x86intrin.h>
///
/// This intrinsic corresponds to the \c XRESLDTRK instruction.
///
static __inline__ void _DEFAULT_FN_ATTRS
_xresldtrk (void)
{
    __builtin_ia32_xresldtrk();
}

#undef _DEFAULT_FN_ATTRS

#endif /* __TSXLDTRKINTRIN_H */
