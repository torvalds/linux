//===-- sancov_flags.cpp ----------------------------------------*- C++ -*-===//
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

#include "sancov_flags.h"
#include "sanitizer_flag_parser.h"
#include "sanitizer_platform.h"

SANITIZER_INTERFACE_WEAK_DEF(const char*, __sancov_default_options, void) {
  return "";
}

using namespace __sanitizer;

namespace __sancov {

SancovFlags sancov_flags_dont_use_directly;  // use via flags();

void SancovFlags::SetDefaults() {
#define SANCOV_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "sancov_flags.inc"
#undef SANCOV_FLAG
}

static void RegisterSancovFlags(FlagParser *parser, SancovFlags *f) {
#define SANCOV_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "sancov_flags.inc"
#undef SANCOV_FLAG
}

void InitializeSancovFlags() {
  SancovFlags *f = sancov_flags();
  f->SetDefaults();

  FlagParser parser;
  RegisterSancovFlags(&parser, f);

  parser.ParseString(__sancov_default_options());
  parser.ParseStringFromEnv("SANCOV_OPTIONS");

  ReportUnrecognizedFlags();
  if (f->help) parser.PrintFlagDescriptions();
}

}  // namespace __sancov
