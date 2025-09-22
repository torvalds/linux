//===-- BreakpointPrecondition.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointPrecondition.h"
#include "lldb/Utility/Status.h"

using namespace lldb_private;

bool BreakpointPrecondition::EvaluatePrecondition(
    StoppointCallbackContext &context) {
  return false;
}

void BreakpointPrecondition::GetDescription(Stream &stream,
                                            lldb::DescriptionLevel level) {}

Status BreakpointPrecondition::ConfigurePrecondition(Args &args) {
  Status error;
  error.SetErrorString("Base breakpoint precondition has no options.");
  return error;
}
