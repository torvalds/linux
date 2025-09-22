/*===- InstrProfilingInternal.c - Support library for PGO instrumentation -===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/

// Note: This is linked into the Darwin kernel, and must remain compatible
// with freestanding compilation. See `darwin_add_builtin_libraries`.

#if !defined(__Fuchsia__)

#include "InstrProfilingInternal.h"

static unsigned ProfileDumped = 0;

COMPILER_RT_VISIBILITY unsigned lprofProfileDumped(void) {
  return ProfileDumped;
}

COMPILER_RT_VISIBILITY void lprofSetProfileDumped(unsigned Value) {
  ProfileDumped = Value;
}

#endif
