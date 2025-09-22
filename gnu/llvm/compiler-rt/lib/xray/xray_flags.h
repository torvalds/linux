//===-- xray_flags.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// XRay runtime flags.
//===----------------------------------------------------------------------===//

#ifndef XRAY_FLAGS_H
#define XRAY_FLAGS_H

#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __xray {

struct Flags {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "xray_flags.inc"
#undef XRAY_FLAG

  void setDefaults();
};

extern Flags xray_flags_dont_use_directly;
extern void registerXRayFlags(FlagParser *P, Flags *F);
const char *useCompilerDefinedFlags();
inline Flags *flags() { return &xray_flags_dont_use_directly; }

void initializeFlags();

} // namespace __xray

#endif // XRAY_FLAGS_H
