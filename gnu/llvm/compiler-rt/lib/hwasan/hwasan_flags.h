//===-- hwasan_flags.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of HWAddressSanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef HWASAN_FLAGS_H
#define HWASAN_FLAGS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __hwasan {

struct Flags {
#define HWASAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "hwasan_flags.inc"
#undef HWASAN_FLAG

  void SetDefaults();
};

Flags *flags();

}  // namespace __hwasan

#endif  // HWASAN_FLAGS_H
