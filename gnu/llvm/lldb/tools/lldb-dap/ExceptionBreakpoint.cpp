//===-- ExceptionBreakpoint.cpp ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ExceptionBreakpoint.h"
#include "BreakpointBase.h"
#include "DAP.h"

namespace lldb_dap {

void ExceptionBreakpoint::SetBreakpoint() {
  if (bp.IsValid())
    return;
  bool catch_value = filter.find("_catch") != std::string::npos;
  bool throw_value = filter.find("_throw") != std::string::npos;
  bp = g_dap.target.BreakpointCreateForException(language, catch_value,
                                                 throw_value);
  // See comments in BreakpointBase::GetBreakpointLabel() for details of why
  // we add a label to our breakpoints.
  bp.AddName(BreakpointBase::GetBreakpointLabel());
}

void ExceptionBreakpoint::ClearBreakpoint() {
  if (!bp.IsValid())
    return;
  g_dap.target.BreakpointDelete(bp.GetID());
  bp = lldb::SBBreakpoint();
}

} // namespace lldb_dap
