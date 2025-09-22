//===-- Watchpoint.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Watchpoint.h"
#include "DAP.h"
#include "JSONUtils.h"
#include "llvm/ADT/StringExtras.h"

namespace lldb_dap {
Watchpoint::Watchpoint(const llvm::json::Object &obj) : BreakpointBase(obj) {
  llvm::StringRef dataId = GetString(obj, "dataId");
  std::string accessType = GetString(obj, "accessType").str();
  auto [addr_str, size_str] = dataId.split('/');
  llvm::to_integer(addr_str, addr, 16);
  llvm::to_integer(size_str, size);
  options.SetWatchpointTypeRead(accessType != "write");
  if (accessType != "read")
    options.SetWatchpointTypeWrite(lldb::eWatchpointWriteTypeOnModify);
}

void Watchpoint::SetCondition() { wp.SetCondition(condition.c_str()); }

void Watchpoint::SetHitCondition() {
  uint64_t hitCount = 0;
  if (llvm::to_integer(hitCondition, hitCount))
    wp.SetIgnoreCount(hitCount - 1);
}

void Watchpoint::CreateJsonObject(llvm::json::Object &object) {
  if (!error.IsValid() || error.Fail()) {
    object.try_emplace("verified", false);
    if (error.Fail())
      EmplaceSafeString(object, "message", error.GetCString());
  } else {
    object.try_emplace("verified", true);
  }
}

void Watchpoint::SetWatchpoint() {
  wp = g_dap.target.WatchpointCreateByAddress(addr, size, options, error);
  if (!condition.empty())
    SetCondition();
  if (!hitCondition.empty())
    SetHitCondition();
}
} // namespace lldb_dap
