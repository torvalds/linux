//===-- xray_basic_flags.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
// XRay Basic flag parsing logic.
//===----------------------------------------------------------------------===//

#include "xray_basic_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "xray_defs.h"

using namespace __sanitizer;

namespace __xray {

/// Use via basicFlags().
BasicFlags xray_basic_flags_dont_use_directly;

void BasicFlags::setDefaults() XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "xray_basic_flags.inc"
#undef XRAY_FLAG
}

void registerXRayBasicFlags(FlagParser *P,
                            BasicFlags *F) XRAY_NEVER_INSTRUMENT {
#define XRAY_FLAG(Type, Name, DefaultValue, Description)                       \
  RegisterFlag(P, #Name, Description, &F->Name);
#include "xray_basic_flags.inc"
#undef XRAY_FLAG
}

const char *useCompilerDefinedBasicFlags() XRAY_NEVER_INSTRUMENT {
#ifdef XRAY_BASIC_OPTIONS
  return SANITIZER_STRINGIFY(XRAY_BASIC_OPTIONS);
#else
  return "";
#endif
}

} // namespace __xray
