//===-- BreakpointPrecondition.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_BREAKPOINTPRECONDITION_H
#define LLDB_BREAKPOINT_BREAKPOINTPRECONDITION_H

#include "lldb/lldb-enumerations.h"

namespace lldb_private {

class Args;
class Status;
class StoppointCallbackContext;
class Stream;

class BreakpointPrecondition {
public:
  virtual ~BreakpointPrecondition() = default;
  virtual bool EvaluatePrecondition(StoppointCallbackContext &context);
  virtual Status ConfigurePrecondition(Args &args);
  virtual void GetDescription(Stream &stream, lldb::DescriptionLevel level);
};
} // namespace lldb_private

#endif
