//===-- memprof_flags.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemProfiler, a memory profiler.
//
// MemProf flag parsing logic.
//===----------------------------------------------------------------------===//

#include "memprof_flags.h"
#include "memprof_interface_internal.h"
#include "memprof_stack.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"

namespace __memprof {

Flags memprof_flags_dont_use_directly; // use via flags().

static const char *MaybeUseMemprofDefaultOptionsCompileDefinition() {
#ifdef MEMPROF_DEFAULT_OPTIONS
  return SANITIZER_STRINGIFY(MEMPROF_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

void Flags::SetDefaults() {
#define MEMPROF_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "memprof_flags.inc"
#undef MEMPROF_FLAG
}

static void RegisterMemprofFlags(FlagParser *parser, Flags *f) {
#define MEMPROF_FLAG(Type, Name, DefaultValue, Description)                    \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "memprof_flags.inc"
#undef MEMPROF_FLAG
}

void InitializeFlags() {
  // Set the default values and prepare for parsing MemProf and common flags.
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetEnv("MEMPROF_SYMBOLIZER_PATH");
    cf.malloc_context_size = kDefaultMallocContextSize;
    cf.intercept_tls_get_addr = true;
    cf.exitcode = 1;
    OverrideCommonFlags(cf);
  }
  Flags *f = flags();
  f->SetDefaults();

  FlagParser memprof_parser;
  RegisterMemprofFlags(&memprof_parser, f);
  RegisterCommonFlags(&memprof_parser);

  // Override from MemProf compile definition.
  const char *memprof_compile_def =
      MaybeUseMemprofDefaultOptionsCompileDefinition();
  memprof_parser.ParseString(memprof_compile_def);

  // Override from user-specified string.
  const char *memprof_default_options = __memprof_default_options();
  memprof_parser.ParseString(memprof_default_options);

  // Override from command line.
  memprof_parser.ParseStringFromEnv("MEMPROF_OPTIONS");

  InitializeCommonFlags();

  if (Verbosity())
    ReportUnrecognizedFlags();

  if (common_flags()->help) {
    memprof_parser.PrintFlagDescriptions();
  }

  CHECK_LE((uptr)common_flags()->malloc_context_size, kStackTraceMax);
}

} // namespace __memprof

SANITIZER_INTERFACE_WEAK_DEF(const char *, __memprof_default_options, void) {
  return "";
}
