//===-- dfsan_flags.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of DataFlowSanitizer.
//
// DFSan flags.
//===----------------------------------------------------------------------===//

#ifndef DFSAN_FLAGS_H
#define DFSAN_FLAGS_H

namespace __dfsan {

struct Flags {
#define DFSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "dfsan_flags.inc"
#undef DFSAN_FLAG

  void SetDefaults();
};

extern Flags flags_data;
inline Flags &flags() { return flags_data; }

}  // namespace __dfsan

#endif  // DFSAN_FLAGS_H
