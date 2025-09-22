//===-- arm-ehabi-helpers.h - Supplementary ARM EHABI declarations --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===--------------------------------------------------------------------===//

#ifndef UNWIND_EHABI_HELPERS_H
#define UNWIND_EHABI_HELPERS_H

#include <stdint.h>
// NOTE: see reasoning for this inclusion below
#include <unwind.h>

#if !defined(__ARM_EABI_UNWINDER__)

// NOTE: _URC_OK, _URC_FAILURE must be present as preprocessor tokens.  This
// allows for a substitution of a constant which can be cast into the
// appropriate enumerated type.  This header is expected to always be included
// AFTER unwind.h (which is why it is forcefully included above).  This ensures
// that we do not overwrite the token for the enumeration.  Subsequent uses of
// the token would be clean to rewrite with constant values.
//
// The typedef redeclaration should be safe.  Due to the protection granted to
// us by the `__ARM_EABI_UNWINDER__` above, we are guaranteed that we are in a
// header not vended by gcc.  The HP unwinder (being an itanium unwinder) does
// not support EHABI, and the GNU unwinder, derived from the HP unwinder, also
// does not support EHABI as of the introduction of this header.  As such, we
// are fairly certain that we are in the LLVM case.  Here, _Unwind_State is a
// typedef, and so we can get away with a redeclaration.
//
// Guarded redefinitions of the needed unwind state prevent the redefinition of
// those states.

#define _URC_OK 0
#define _URC_FAILURE 9

typedef uint32_t _Unwind_State;

#if !defined(_US_UNWIND_FRAME_STARTING)
#define _US_UNWIND_FRAME_STARTING ((_Unwind_State)1)
#endif

#if !defined(_US_ACTION_MASK)
#define _US_ACTION_MASK ((_Unwind_State)3)
#endif

#endif

#endif
