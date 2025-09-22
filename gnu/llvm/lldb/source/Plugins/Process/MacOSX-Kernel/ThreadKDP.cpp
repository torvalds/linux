//===-- ThreadKDP.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadKDP.h"

#include "lldb/Host/SafeMachO.h"

#include "lldb/Breakpoint/Watchpoint.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Unwind.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "Plugins/Process/Utility/StopInfoMachException.h"
#include "ProcessKDP.h"
#include "ProcessKDPLog.h"
#include "RegisterContextKDP_arm.h"
#include "RegisterContextKDP_arm64.h"
#include "RegisterContextKDP_i386.h"
#include "RegisterContextKDP_x86_64.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

// Thread Registers

ThreadKDP::ThreadKDP(Process &process, lldb::tid_t tid)
    : Thread(process, tid), m_thread_name(), m_dispatch_queue_name(),
      m_thread_dispatch_qaddr(LLDB_INVALID_ADDRESS) {
  Log *log = GetLog(KDPLog::Thread);
  LLDB_LOG(log, "this = {0}, tid = {1:x}", this, GetID());
}

ThreadKDP::~ThreadKDP() {
  Log *log = GetLog(KDPLog::Thread);
  LLDB_LOG(log, "this = {0}, tid = {1:x}", this, GetID());
  DestroyThread();
}

const char *ThreadKDP::GetName() {
  if (m_thread_name.empty())
    return nullptr;
  return m_thread_name.c_str();
}

const char *ThreadKDP::GetQueueName() { return nullptr; }

void ThreadKDP::RefreshStateAfterStop() {
  // Invalidate all registers in our register context. We don't set "force" to
  // true because the stop reply packet might have had some register values
  // that were expedited and these will already be copied into the register
  // context by the time this function gets called. The KDPRegisterContext
  // class has been made smart enough to detect when it needs to invalidate
  // which registers are valid by putting hooks in the register read and
  // register supply functions where they check the process stop ID and do the
  // right thing.
  const bool force = false;
  lldb::RegisterContextSP reg_ctx_sp(GetRegisterContext());
  if (reg_ctx_sp)
    reg_ctx_sp->InvalidateIfNeeded(force);
}

bool ThreadKDP::ThreadIDIsValid(lldb::tid_t thread) { return thread != 0; }

void ThreadKDP::Dump(Log *log, uint32_t index) {}

bool ThreadKDP::ShouldStop(bool &step_more) { return true; }
lldb::RegisterContextSP ThreadKDP::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  return m_reg_context_sp;
}

lldb::RegisterContextSP
ThreadKDP::CreateRegisterContextForFrame(StackFrame *frame) {
  lldb::RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    ProcessSP process_sp(CalculateProcess());
    if (process_sp) {
      switch (static_cast<ProcessKDP *>(process_sp.get())
                  ->GetCommunication()
                  .GetCPUType()) {
      case llvm::MachO::CPU_TYPE_ARM:
        reg_ctx_sp =
            std::make_shared<RegisterContextKDP_arm>(*this, concrete_frame_idx);
        break;
      case llvm::MachO::CPU_TYPE_ARM64:
        reg_ctx_sp = std::make_shared<RegisterContextKDP_arm64>(
            *this, concrete_frame_idx);
        break;
      case llvm::MachO::CPU_TYPE_I386:
        reg_ctx_sp = std::make_shared<RegisterContextKDP_i386>(
            *this, concrete_frame_idx);
        break;
      case llvm::MachO::CPU_TYPE_X86_64:
        reg_ctx_sp = std::make_shared<RegisterContextKDP_x86_64>(
            *this, concrete_frame_idx);
        break;
      default:
        llvm_unreachable("Add CPU type support in KDP");
      }
    }
  } else {
    reg_ctx_sp = GetUnwinder().CreateRegisterContextForFrame(frame);
  }
  return reg_ctx_sp;
}

bool ThreadKDP::CalculateStopInfo() {
  ProcessSP process_sp(GetProcess());
  if (process_sp) {
    if (m_cached_stop_info_sp) {
      SetStopInfo(m_cached_stop_info_sp);
    } else {
      SetStopInfo(StopInfo::CreateStopReasonWithSignal(*this, SIGSTOP));
    }
    return true;
  }
  return false;
}

void ThreadKDP::SetStopInfoFrom_KDP_EXCEPTION(
    const DataExtractor &exc_reply_packet) {
  lldb::offset_t offset = 0;
  uint8_t reply_command = exc_reply_packet.GetU8(&offset);
  if (reply_command == CommunicationKDP::KDP_EXCEPTION) {
    offset = 8;
    const uint32_t count = exc_reply_packet.GetU32(&offset);
    if (count >= 1) {
      // const uint32_t cpu = exc_reply_packet.GetU32 (&offset);
      offset += 4; // Skip the useless CPU field
      const uint32_t exc_type = exc_reply_packet.GetU32(&offset);
      const uint32_t exc_code = exc_reply_packet.GetU32(&offset);
      const uint32_t exc_subcode = exc_reply_packet.GetU32(&offset);
      // We have to make a copy of the stop info because the thread list will
      // iterate through the threads and clear all stop infos..

      // Let the StopInfoMachException::CreateStopReasonWithMachException()
      // function update the PC if needed as we might hit a software breakpoint
      // and need to decrement the PC (i386 and x86_64 need this) and KDP
      // doesn't do this for us.
      const bool pc_already_adjusted = false;
      const bool adjust_pc_if_needed = true;

      m_cached_stop_info_sp =
          StopInfoMachException::CreateStopReasonWithMachException(
              *this, exc_type, 2, exc_code, exc_subcode, 0, pc_already_adjusted,
              adjust_pc_if_needed);
    }
  }
}
