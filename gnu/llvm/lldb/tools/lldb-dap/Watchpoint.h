//===-- Watchpoint.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TOOLS_LLDB_DAP_WATCHPOINT_H
#define LLDB_TOOLS_LLDB_DAP_WATCHPOINT_H

#include "BreakpointBase.h"
#include "lldb/API/SBError.h"
#include "lldb/API/SBWatchpoint.h"
#include "lldb/API/SBWatchpointOptions.h"

namespace lldb_dap {

struct Watchpoint : public BreakpointBase {
  lldb::addr_t addr;
  size_t size;
  lldb::SBWatchpointOptions options;
  // The LLDB breakpoint associated wit this watchpoint.
  lldb::SBWatchpoint wp;
  lldb::SBError error;

  Watchpoint() = default;
  Watchpoint(const llvm::json::Object &obj);
  Watchpoint(lldb::SBWatchpoint wp) : wp(wp) {}

  void SetCondition() override;
  void SetHitCondition() override;
  void CreateJsonObject(llvm::json::Object &object) override;

  void SetWatchpoint();
};
} // namespace lldb_dap

#endif
