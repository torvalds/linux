//===-- nsan_flags.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of NumericalStabilitySanitizer.
//===----------------------------------------------------------------------===//

#ifndef NSAN_FLAGS_H
#define NSAN_FLAGS_H

namespace __nsan {

struct Flags {
#define NSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "nsan_flags.inc"
#undef NSAN_FLAG

  double cached_absolute_error_threshold = 0.0;

  void SetDefaults();
  void PopulateCache();
};

extern Flags flags_data;
inline Flags &flags() { return flags_data; }

void InitializeFlags();

} // namespace __nsan

#endif
