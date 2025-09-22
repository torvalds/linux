//===-- nsan_flags.cc -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of NumericalStabilitySanitizer.
//
//===----------------------------------------------------------------------===//

#include "nsan_flags.h"

#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"

using namespace __sanitizer;
using namespace __nsan;

SANITIZER_INTERFACE_WEAK_DEF(const char *, __nsan_default_options, void) {
  return "";
}

Flags __nsan::flags_data;

void Flags::SetDefaults() {
#define NSAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "nsan_flags.inc"
#undef NSAN_FLAG
}

void Flags::PopulateCache() {
  cached_absolute_error_threshold =
      1.0 / (1ull << log2_absolute_error_threshold);
}

static void RegisterNSanFlags(FlagParser *parser, Flags *f) {
#define NSAN_FLAG(Type, Name, DefaultValue, Description)                       \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "nsan_flags.inc"
#undef NSAN_FLAG
}

static const char *MaybeCallNsanDefaultOptions() {
  return (&__nsan_default_options) ? __nsan_default_options() : "";
}

void __nsan::InitializeFlags() {
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetEnv("NSAN_SYMBOLIZER_PATH");
    OverrideCommonFlags(cf);
  }

  flags().SetDefaults();

  FlagParser parser;
  RegisterCommonFlags(&parser);
  RegisterNSanFlags(&parser, &flags());

  const char *nsan_default_options = MaybeCallNsanDefaultOptions();
  parser.ParseString(nsan_default_options);

  parser.ParseString(GetEnv("NSAN_OPTIONS"));
  InitializeCommonFlags();
  if (Verbosity())
    ReportUnrecognizedFlags();
  if (common_flags()->help)
    parser.PrintFlagDescriptions();

  flags().PopulateCache();
}
