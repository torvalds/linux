//===-- RegisterContextOpenBSDKernel_x86_64.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__OpenBSD__)
#include <sys/types.h>
#include <sys/time.h>
#define _KERNEL
#include <machine/cpu.h>
#undef _KERNEL
#include <machine/pcb.h>
#include <frame.h>
#endif

#include "RegisterContextOpenBSDKernel_x86_64.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/Support/Endian.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextOpenBSDKernel_x86_64::RegisterContextOpenBSDKernel_x86_64(
    Thread &thread, RegisterInfoInterface *register_info,
    lldb::addr_t pcb)
  : RegisterContextPOSIX_x86(thread, 0, register_info),
    m_pcb_addr(pcb) {
}

bool RegisterContextOpenBSDKernel_x86_64::ReadGPR() { return true; }

bool RegisterContextOpenBSDKernel_x86_64::ReadFPR() { return true; }

bool RegisterContextOpenBSDKernel_x86_64::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextOpenBSDKernel_x86_64::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextOpenBSDKernel_x86_64::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  Status error;

  if (m_pcb_addr == LLDB_INVALID_ADDRESS)
    return false;

#ifdef __amd64__
  struct pcb pcb;
  size_t rd = m_thread.GetProcess()->ReadMemory(m_pcb_addr, &pcb, sizeof(pcb),
						error);
  if (rd != sizeof(pcb))
    return false;

  /*
    Usually pcb is written in `cpu_switchto` function. This function writes
    registers as same as the structure of  `swichframe`  in the stack.
    We read the frame if it is.
   */
  struct switchframe sf;
  rd = m_thread.GetProcess()->ReadMemory(pcb.pcb_rsp, &sf, sizeof(sf), error);
  if (rd != sizeof(sf))
    return false;

  uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (pcb.pcb_rbp == (u_int64_t)sf.sf_rbp) {
#define SFREG(x)				\
    case lldb_##x##_x86_64:			\
      value = (u_int64_t)sf.sf_##x;		\
      return true;
#define PCBREG(x, offset)			\
    case lldb_##x##_x86_64:			\
      value = pcb.pcb_##x + (offset);		\
      return true;
    switch (reg) {
      SFREG(r15);
      SFREG(r14);
      SFREG(r13);
      SFREG(r12);
      SFREG(rbp);
      SFREG(rbx);
      SFREG(rip);
      PCBREG(rsp, sizeof(sf));
    }
  } else {
    switch (reg) {
      PCBREG(rbp, 0);
      PCBREG(rsp, 8);
    case lldb_rip_x86_64:
      value = m_thread.GetProcess()->ReadPointerFromMemory(pcb.pcb_rbp, error);
      return true;
    }
  }
#endif
  return false;
}

bool RegisterContextOpenBSDKernel_x86_64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}
