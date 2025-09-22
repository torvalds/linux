//===-- msan_flags.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
//===----------------------------------------------------------------------===//
#ifndef MSAN_FLAGS_H
#define MSAN_FLAGS_H

namespace __msan {

struct Flags {
#define MSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "msan_flags.inc"
#undef MSAN_FLAG

  void SetDefaults();
};

Flags *flags();

}  // namespace __msan

#endif  // MSAN_FLAGS_H
