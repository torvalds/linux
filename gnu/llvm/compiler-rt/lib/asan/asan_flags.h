//===-- asan_flags.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan runtime flags.
//===----------------------------------------------------------------------===//

#ifndef ASAN_FLAGS_H
#define ASAN_FLAGS_H

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_flag_parser.h"

// ASan flag values can be defined in four ways:
// 1) initialized with default values at startup.
// 2) overriden during compilation of ASan runtime by providing
//    compile definition ASAN_DEFAULT_OPTIONS.
// 3) overriden from string returned by user-specified function
//    __asan_default_options().
// 4) overriden from env variable ASAN_OPTIONS.
// 5) overriden during ASan activation (for now used on Android only).

namespace __asan {

struct Flags {
#define ASAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "asan_flags.inc"
#undef ASAN_FLAG

  void SetDefaults();
};

extern Flags asan_flags_dont_use_directly;
inline Flags *flags() {
  return &asan_flags_dont_use_directly;
}

void InitializeFlags();

}  // namespace __asan

#endif  // ASAN_FLAGS_H
