//===-- FunctionBreakpoint.cpp ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FunctionBreakpoint.h"
#include "DAP.h"

namespace lldb_dap {

FunctionBreakpoint::FunctionBreakpoint(const llvm::json::Object &obj)
    : Breakpoint(obj), functionName(std::string(GetString(obj, "name"))) {}

void FunctionBreakpoint::SetBreakpoint() {
  if (functionName.empty())
    return;
  bp = g_dap.target.BreakpointCreateByName(functionName.c_str());
  Breakpoint::SetBreakpoint();
}

} // namespace lldb_dap
