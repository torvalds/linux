//===-- sancov_flags.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sanitizer Coverage runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef SANCOV_FLAGS_H
#define SANCOV_FLAGS_H

#include "sanitizer_flag_parser.h"
#include "sanitizer_internal_defs.h"

namespace __sancov {

struct SancovFlags {
#define SANCOV_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "sancov_flags.inc"
#undef SANCOV_FLAG

  void SetDefaults();
};

extern SancovFlags sancov_flags_dont_use_directly;

inline SancovFlags* sancov_flags() { return &sancov_flags_dont_use_directly; }

void InitializeSancovFlags();

}  // namespace __sancov

extern "C" SANITIZER_INTERFACE_ATTRIBUTE SANITIZER_WEAK_ATTRIBUTE const char*
__sancov_default_options();

#endif
