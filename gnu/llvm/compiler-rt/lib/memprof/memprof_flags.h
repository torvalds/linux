//===-- memprof_flags.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf runtime flags.
//===----------------------------------------------------------------------===//

#ifndef MEMPROF_FLAGS_H
#define MEMPROF_FLAGS_H

#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

// MemProf flag values can be defined in four ways:
// 1) initialized with default values at startup.
// 2) overriden during compilation of MemProf runtime by providing
//    compile definition MEMPROF_DEFAULT_OPTIONS.
// 3) overriden from string returned by user-specified function
//    __memprof_default_options().
// 4) overriden from env variable MEMPROF_OPTIONS.

namespace __memprof {

struct Flags {
#define MEMPROF_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "memprof_flags.inc"
#undef MEMPROF_FLAG

  void SetDefaults();
};

extern Flags memprof_flags_dont_use_directly;
inline Flags *flags() { return &memprof_flags_dont_use_directly; }

void InitializeFlags();

} // namespace __memprof

#endif // MEMPROF_FLAGS_H
