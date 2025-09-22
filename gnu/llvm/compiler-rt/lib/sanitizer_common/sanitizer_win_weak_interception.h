//===-- sanitizer_win_weak_interception.h ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This header provide helper macros to delegate calls of weak functions to the
// implementation in the main executable when a strong definition is present.
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_WIN_WEAK_INTERCEPTION_H
#define SANITIZER_WIN_WEAK_INTERCEPTION_H
#include "sanitizer_internal_defs.h"

namespace __sanitizer {
int interceptWhenPossible(uptr dll_function, const char *real_function);
}

// ----------------- Function interception helper macros -------------------- //
// Weak functions, could be redefined in the main executable, but that is not
// necessary, so we shouldn't die if we can not find a reference.
#define INTERCEPT_WEAK(Name) interceptWhenPossible((uptr) Name, #Name);

#define INTERCEPT_SANITIZER_WEAK_FUNCTION(Name)                                \
  static int intercept_##Name() {                                              \
    return __sanitizer::interceptWhenPossible((__sanitizer::uptr) Name, #Name);\
  }                                                                            \
  __pragma(section(".WEAK$M", long, read))                                     \
  __declspec(allocate(".WEAK$M")) int (*__weak_intercept_##Name)() =           \
      intercept_##Name;

#endif // SANITIZER_WIN_WEAK_INTERCEPTION_H
