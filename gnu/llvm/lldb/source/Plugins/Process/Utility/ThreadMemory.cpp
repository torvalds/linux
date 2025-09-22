//===-- ThreadMemory.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Process/Utility/ThreadMemory.h"

#include "Plugins/Process/Utility/RegisterContextThreadMemory.h"
#include "lldb/Target/OperatingSystem.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Unwind.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

ThreadMemory::ThreadMemory(Process &process, tid_t tid,
                           const ValueObjectSP &thread_info_valobj_sp)
    : Thread(process, tid), m_backing_thread_sp(),
      m_thread_info_valobj_sp(thread_info_valobj_sp), m_name(), m_queue(),
      m_register_data_addr(LLDB_INVALID_ADDRESS) {}

ThreadMemory::ThreadMemory(Process &process, lldb::tid_t tid,
                           llvm::StringRef name, llvm::StringRef queue,
                           lldb::addr_t register_data_addr)
    : Thread(process, tid), m_backing_thread_sp(), m_thread_info_valobj_sp(),
      m_name(std::string(name)), m_queue(std::string(queue)),
      m_register_data_addr(register_data_addr) {}

ThreadMemory::~ThreadMemory() { DestroyThread(); }

void ThreadMemory::WillResume(StateType resume_state) {
  if (m_backing_thread_sp)
    m_backing_thread_sp->WillResume(resume_state);
}

void ThreadMemory::ClearStackFrames() {
  if (m_backing_thread_sp)
    m_backing_thread_sp->ClearStackFrames();
  Thread::ClearStackFrames();
}

RegisterContextSP ThreadMemory::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = std::make_shared<RegisterContextThreadMemory>(
        *this, m_register_data_addr);
  return m_reg_context_sp;
}

RegisterContextSP
ThreadMemory::CreateRegisterContextForFrame(StackFrame *frame) {
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0)
    return GetRegisterContext();
  return GetUnwinder().CreateRegisterContextForFrame(frame);
}

bool ThreadMemory::CalculateStopInfo() {
  if (m_backing_thread_sp) {
    lldb::StopInfoSP backing_stop_info_sp(
        m_backing_thread_sp->GetPrivateStopInfo());
    if (backing_stop_info_sp &&
        backing_stop_info_sp->IsValidForOperatingSystemThread(*this)) {
      backing_stop_info_sp->SetThread(shared_from_this());
      SetStopInfo(backing_stop_info_sp);
      return true;
    }
  } else {
    ProcessSP process_sp(GetProcess());

    if (process_sp) {
      OperatingSystem *os = process_sp->GetOperatingSystem();
      if (os) {
        SetStopInfo(os->CreateThreadStopReason(this));
        return true;
      }
    }
  }
  return false;
}

void ThreadMemory::RefreshStateAfterStop() {
  if (m_backing_thread_sp)
    return m_backing_thread_sp->RefreshStateAfterStop();

  if (m_reg_context_sp)
    m_reg_context_sp->InvalidateAllRegisters();
}
