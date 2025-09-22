//===-- BreakpointBase.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BreakpointBase.h"
#include "DAP.h"
#include "llvm/ADT/StringExtras.h"

using namespace lldb_dap;

BreakpointBase::BreakpointBase(const llvm::json::Object &obj)
    : condition(std::string(GetString(obj, "condition"))),
      hitCondition(std::string(GetString(obj, "hitCondition"))) {}

void BreakpointBase::UpdateBreakpoint(const BreakpointBase &request_bp) {
  if (condition != request_bp.condition) {
    condition = request_bp.condition;
    SetCondition();
  }
  if (hitCondition != request_bp.hitCondition) {
    hitCondition = request_bp.hitCondition;
    SetHitCondition();
  }
}

const char *BreakpointBase::GetBreakpointLabel() {
  // Breakpoints in LLDB can have names added to them which are kind of like
  // labels or categories. All breakpoints that are set through the IDE UI get
  // sent through the various DAP set*Breakpoint packets, and these
  // breakpoints will be labeled with this name so if breakpoint update events
  // come in for breakpoints that the IDE doesn't know about, like if a
  // breakpoint is set manually using the debugger console, we won't report any
  // updates on them and confused the IDE. This function gets called by all of
  // the breakpoint classes after they set breakpoints to mark a breakpoint as
  // a UI breakpoint. We can later check a lldb::SBBreakpoint object that comes
  // in via LLDB breakpoint changed events and check the breakpoint by calling
  // "bool lldb::SBBreakpoint::MatchesName(const char *)" to check if a
  // breakpoint in one of the UI breakpoints that we should report changes for.
  return "dap";
}
