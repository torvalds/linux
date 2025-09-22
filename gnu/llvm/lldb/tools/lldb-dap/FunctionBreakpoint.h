//===-- FunctionBreakpoint.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_FUNCTIONBREAKPOINT_H
#define LLDB_TOOLS_LLDB_DAP_FUNCTIONBREAKPOINT_H

#include "Breakpoint.h"

namespace lldb_dap {

struct FunctionBreakpoint : public Breakpoint {
  std::string functionName;

  FunctionBreakpoint() = default;
  FunctionBreakpoint(const llvm::json::Object &obj);

  // Set this breakpoint in LLDB as a new breakpoint
  void SetBreakpoint();
};

} // namespace lldb_dap

#endif
