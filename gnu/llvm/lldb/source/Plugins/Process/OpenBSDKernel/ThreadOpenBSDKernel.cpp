//===-- ThreadOpenBSDKernel.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ThreadOpenBSDKernel.h"

#include "lldb/Target/Unwind.h"
#include "lldb/Utility/Log.h"

#include "Plugins/Process/Utility/RegisterContextOpenBSD_i386.h"
#include "Plugins/Process/Utility/RegisterContextOpenBSD_x86_64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
#include "ProcessOpenBSDKernel.h"
#include "RegisterContextOpenBSDKernel_arm64.h"
#include "RegisterContextOpenBSDKernel_i386.h"
#include "RegisterContextOpenBSDKernel_x86_64.h"
#include "ThreadOpenBSDKernel.h"

using namespace lldb;
using namespace lldb_private;

ThreadOpenBSDKernel::ThreadOpenBSDKernel(Process &process, lldb::tid_t tid,
					 lldb::addr_t pcb,
					 std::string thread_name)
    : Thread(process, tid), m_thread_name(std::move(thread_name)),
      m_pcb(pcb) {}

ThreadOpenBSDKernel::~ThreadOpenBSDKernel() {}

void ThreadOpenBSDKernel::RefreshStateAfterStop() {}

lldb::RegisterContextSP ThreadOpenBSDKernel::GetRegisterContext() {
  if (!m_reg_context_sp)
    m_reg_context_sp = CreateRegisterContextForFrame(nullptr);
  return m_reg_context_sp;
}

lldb::RegisterContextSP
ThreadOpenBSDKernel::CreateRegisterContextForFrame(StackFrame *frame) {
  RegisterContextSP reg_ctx_sp;
  uint32_t concrete_frame_idx = 0;

  if (frame)
    concrete_frame_idx = frame->GetConcreteFrameIndex();

  if (concrete_frame_idx == 0) {
    if (m_thread_reg_ctx_sp)
      return m_thread_reg_ctx_sp;

    ProcessOpenBSDKernel *process =
	static_cast<ProcessOpenBSDKernel *>(GetProcess().get());
    ArchSpec arch = process->GetTarget().GetArchitecture();

    switch (arch.GetMachine()) {
    case llvm::Triple::aarch64:
      m_thread_reg_ctx_sp =
	  std::make_shared<RegisterContextOpenBSDKernel_arm64>(
	      *this, std::make_unique<RegisterInfoPOSIX_arm64>(arch, 0),
	      m_pcb);
      break;
    case llvm::Triple::x86:
      m_thread_reg_ctx_sp = std::make_shared<RegisterContextOpenBSDKernel_i386>(
	  *this, new RegisterContextOpenBSD_i386(arch), m_pcb);
      break;
    case llvm::Triple::x86_64:
      m_thread_reg_ctx_sp =
	  std::make_shared<RegisterContextOpenBSDKernel_x86_64>(
		  *this, new RegisterContextOpenBSD_x86_64(arch), m_pcb);
      break;
    default:
      assert(false && "Unsupported architecture passed to ThreadOpenBSDKernel");
      break;
    }

    reg_ctx_sp = m_thread_reg_ctx_sp;
  } else {
    reg_ctx_sp = GetUnwinder().CreateRegisterContextForFrame(frame);
  }
  return reg_ctx_sp;
}

bool ThreadOpenBSDKernel::CalculateStopInfo() { return false; }
