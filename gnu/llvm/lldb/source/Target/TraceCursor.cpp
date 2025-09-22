//===-- TraceCursor.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/TraceCursor.h"

#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Trace.h"

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

TraceCursor::TraceCursor(lldb::ThreadSP thread_sp)
    : m_exe_ctx_ref(ExecutionContext(thread_sp)) {}

ExecutionContextRef &TraceCursor::GetExecutionContextRef() {
  return m_exe_ctx_ref;
}

void TraceCursor::SetForwards(bool forwards) { m_forwards = forwards; }

bool TraceCursor::IsForwards() const { return m_forwards; }

bool TraceCursor::IsError() const {
  return GetItemKind() == lldb::eTraceItemKindError;
}

bool TraceCursor::IsEvent() const {
  return GetItemKind() == lldb::eTraceItemKindEvent;
}

bool TraceCursor::IsInstruction() const {
  return GetItemKind() == lldb::eTraceItemKindInstruction;
}

const char *TraceCursor::GetEventTypeAsString() const {
  return EventKindToString(GetEventType());
}

const char *TraceCursor::EventKindToString(lldb::TraceEvent event_kind) {
  switch (event_kind) {
  case lldb::eTraceEventDisabledHW:
    return "hardware disabled tracing";
  case lldb::eTraceEventDisabledSW:
    return "software disabled tracing";
  case lldb::eTraceEventCPUChanged:
    return "CPU core changed";
  case lldb::eTraceEventHWClockTick:
    return "HW clock tick";
  case lldb::eTraceEventSyncPoint:
    return "trace synchronization point";
  }
  llvm_unreachable("Fully covered switch above");
}
