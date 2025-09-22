//===-- HistoryThread.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/lldb-private.h"

#include "Plugins/Process/Utility/HistoryThread.h"

#include "Plugins/Process/Utility/HistoryUnwind.h"
#include "Plugins/Process/Utility/RegisterContextHistory.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

//  Constructor

HistoryThread::HistoryThread(lldb_private::Process &process, lldb::tid_t tid,
                             std::vector<lldb::addr_t> pcs,
                             bool pcs_are_call_addresses)
    : Thread(process, tid, true), m_framelist_mutex(), m_framelist(),
      m_pcs(pcs), m_extended_unwind_token(LLDB_INVALID_ADDRESS), m_queue_name(),
      m_thread_name(), m_originating_unique_thread_id(tid),
      m_queue_id(LLDB_INVALID_QUEUE_ID) {
  m_unwinder_up =
      std::make_unique<HistoryUnwind>(*this, pcs, pcs_are_call_addresses);
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p HistoryThread::HistoryThread", static_cast<void *>(this));
}

//  Destructor

HistoryThread::~HistoryThread() {
  Log *log = GetLog(LLDBLog::Object);
  LLDB_LOGF(log, "%p HistoryThread::~HistoryThread (tid=0x%" PRIx64 ")",
            static_cast<void *>(this), GetID());
  DestroyThread();
}

lldb::RegisterContextSP HistoryThread::GetRegisterContext() {
  RegisterContextSP rctx;
  if (m_pcs.size() > 0) {
    rctx = std::make_shared<RegisterContextHistory>(
        *this, 0, GetProcess()->GetAddressByteSize(), m_pcs[0]);
  }
  return rctx;
}

lldb::RegisterContextSP
HistoryThread::CreateRegisterContextForFrame(StackFrame *frame) {
  return m_unwinder_up->CreateRegisterContextForFrame(frame);
}

lldb::StackFrameListSP HistoryThread::GetStackFrameList() {
  // FIXME do not throw away the lock after we acquire it..
  std::unique_lock<std::mutex> lock(m_framelist_mutex);
  lock.unlock();
  if (m_framelist.get() == nullptr) {
    m_framelist =
        std::make_shared<StackFrameList>(*this, StackFrameListSP(), true);
  }

  return m_framelist;
}

uint32_t HistoryThread::GetExtendedBacktraceOriginatingIndexID() {
  if (m_originating_unique_thread_id != LLDB_INVALID_THREAD_ID) {
    if (GetProcess()->HasAssignedIndexIDToThread(
            m_originating_unique_thread_id)) {
      return GetProcess()->AssignIndexIDToThread(
          m_originating_unique_thread_id);
    }
  }
  return LLDB_INVALID_THREAD_ID;
}
