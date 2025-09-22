//===-- ThreadPostMortemTrace.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadPostMortemTrace.h"

#include <memory>
#include <optional>

#include "Plugins/Process/Utility/RegisterContextHistory.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

void ThreadPostMortemTrace::RefreshStateAfterStop() {}

RegisterContextSP ThreadPostMortemTrace::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);

  return m_reg_context_sp;
}

RegisterContextSP
ThreadPostMortemTrace::CreateRegisterContextForFrame(StackFrame *frame) {
  // Eventually this will calculate the register context based on the current
  // trace position.
  return std::make_shared<RegisterContextHistory>(
      *this, 0, GetProcess()->GetAddressByteSize(), LLDB_INVALID_ADDRESS);
}

bool ThreadPostMortemTrace::CalculateStopInfo() { return false; }

const std::optional<FileSpec> &ThreadPostMortemTrace::GetTraceFile() const {
  return m_trace_file;
}
