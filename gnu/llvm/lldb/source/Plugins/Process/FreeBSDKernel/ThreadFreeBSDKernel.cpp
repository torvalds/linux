//===-- ThreadFreeBSDKernel.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadFreeBSDKernel.h"

#include "lldb/Target/Unwind.h"
#include "lldb/Utility/Log.h"

#include "Plugins/Process/Utility/RegisterContextFreeBSD_i386.h"
#include "Plugins/Process/Utility/RegisterContextFreeBSD_x86_64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
#include "ProcessFreeBSDKernel.h"
#include "RegisterContextFreeBSDKernel_arm64.h"
#include "RegisterContextFreeBSDKernel_i386.h"
#include "RegisterContextFreeBSDKernel_x86_64.h"
#include "ThreadFreeBSDKernel.h"

using namespace lldb;
using namespace lldb_private;

ThreadFreeBSDKernel::ThreadFreeBSDKernel(Process &process, lldb::tid_t tid,
                                         lldb::addr_t pcb_addr,
                                         std::string thread_name)
    : Thread(process, tid), m_thread_name(std::move(thread_name)),
      m_pcb_addr(pcb_addr) {}

ThreadFreeBSDKernel::~ThreadFreeBSDKernel() {}

void ThreadFreeBSDKernel::RefreshStateAfterStop() {}

lldb::RegisterContextSP ThreadFreeBSDKernel::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  return m_reg_context_sp;
}

lldb::RegisterContextSP
ThreadFreeBSDKernel::CreateRegisterContextForFrame(StackFrame *frame) {
  RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    if (m_thread_reg_ctx_sp)
      return m_thread_reg_ctx_sp;

    ProcessFreeBSDKernel *process =
        static_cast<ProcessFreeBSDKernel *>(GetProcess().get());
    ArchSpec arch = process->GetTarget().GetArchitecture();

    switch (arch.GetMachine()) {
    case llvm::Triple::aarch64:
      m_thread_reg_ctx_sp =
          std::make_shared<RegisterContextFreeBSDKernel_arm64>(
              *this, std::make_unique<RegisterInfoPOSIX_arm64>(arch, 0),
              m_pcb_addr);
      break;
    case llvm::Triple::x86:
      m_thread_reg_ctx_sp = std::make_shared<RegisterContextFreeBSDKernel_i386>(
          *this, new RegisterContextFreeBSD_i386(arch), m_pcb_addr);
      break;
    case llvm::Triple::x86_64:
      m_thread_reg_ctx_sp =
          std::make_shared<RegisterContextFreeBSDKernel_x86_64>(
              *this, new RegisterContextFreeBSD_x86_64(arch), m_pcb_addr);
      break;
    default:
      assert(false && "Unsupported architecture passed to ThreadFreeBSDKernel");
      break;
    }

    reg_ctx_sp = m_thread_reg_ctx_sp;
  } else {
    reg_ctx_sp = GetUnwinder().CreateRegisterContextForFrame(frame);
  }
  return reg_ctx_sp;
}

bool ThreadFreeBSDKernel::CalculateStopInfo() { return false; }
