//===-- ubsan_flags.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runtime flags for UndefinedBehaviorSanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef UBSAN_FLAGS_H
#define UBSAN_FLAGS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __sanitizer {
class FlagParser;
}

namespace __ubsan {

struct Flags {
#define UBSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "ubsan_flags.inc"
#undef UBSAN_FLAG

  void SetDefaults();
};

extern Flags ubsan_flags;
inline Flags *flags() { return &ubsan_flags; }

void InitializeFlags();
void RegisterUbsanFlags(FlagParser *parser, Flags *f);

}  // namespace __ubsan

extern "C" {
// Users may provide their own implementation of __ubsan_default_options to
// override the default flag values.
SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE
const char *__ubsan_default_options();
}  // extern "C"

#endif  // UBSAN_FLAGS_H
