//===-- ubsan_flags.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runtime flags for UndefinedBehaviorSanitizer.
//
//===----------------------------------------------------------------------===//

#include "ubsan_platform.h"
#if CAN_SANITIZE_UB
#include "ubsan_flags.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_flag_parser.h"

#include <stdlib.h>

namespace __ubsan {

static const char *GetFlag(const char *flag) {
  // We cannot call getenv() from inside a preinit array initializer
  if (SANITIZER_CAN_USE_PREINIT_ARRAY) {
    return GetEnv(flag);
  } else {
    return getenv(flag);
  }
}

Flags ubsan_flags;

void Flags::SetDefaults() {
#define UBSAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "ubsan_flags.inc"
#undef UBSAN_FLAG
}

void RegisterUbsanFlags(FlagParser *parser, Flags *f) {
#define UBSAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "ubsan_flags.inc"
#undef UBSAN_FLAG
}

void InitializeFlags() {
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetFlag("UBSAN_SYMBOLIZER_PATH");
    OverrideCommonFlags(cf);
  }

  Flags *f = flags();
  f->SetDefaults();

  FlagParser parser;
  RegisterCommonFlags(&parser);
  RegisterUbsanFlags(&parser, f);

  // Override from user-specified string.
  parser.ParseString(__ubsan_default_options());
  // Override from environment variable.
  parser.ParseStringFromEnv("UBSAN_OPTIONS");
  InitializeCommonFlags();
  if (Verbosity()) ReportUnrecognizedFlags();

  if (common_flags()->help) parser.PrintFlagDescriptions();
}

}  // namespace __ubsan

SANITIZER_INTERFACE_WEAK_DEF(const char *, __ubsan_default_options, void) {
  return "";
}

#endif  // CAN_SANITIZE_UB
