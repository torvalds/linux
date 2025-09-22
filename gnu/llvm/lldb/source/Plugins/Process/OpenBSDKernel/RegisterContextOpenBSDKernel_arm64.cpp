//===-- RegisterContextOpenBSDKernel_arm64.cpp ----------------------------===//
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

#include "RegisterContextOpenBSDKernel_arm64.h"
#include "Plugins/Process/Utility/lldb-arm64-register-enums.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/Support/Endian.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextOpenBSDKernel_arm64::RegisterContextOpenBSDKernel_arm64(
    Thread &thread, std::unique_ptr<RegisterInfoPOSIX_arm64> register_info_up,
    lldb::addr_t pcb_addr)
    : RegisterContextPOSIX_arm64(thread, std::move(register_info_up)),
      m_pcb_addr(pcb_addr) {}

bool RegisterContextOpenBSDKernel_arm64::ReadGPR() { return true; }

bool RegisterContextOpenBSDKernel_arm64::ReadFPR() { return true; }

bool RegisterContextOpenBSDKernel_arm64::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextOpenBSDKernel_arm64::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextOpenBSDKernel_arm64::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (m_pcb_addr == LLDB_INVALID_ADDRESS)
    return false;

#ifdef __aarch64__
  Status error;
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
  rd = m_thread.GetProcess()->ReadMemory(pcb.pcb_sp, &sf, sizeof(sf), error);
  if (rd != sizeof(sf))
    return false;

  uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  switch (reg) {
#define REG(x)					\
    case gpr_##x##_arm64:			\
      value = (u_int64_t)sf.sf_##x;		\
      return true;

    REG(x19);
    REG(x20);
    REG(x21);
    REG(x22);
    REG(x23);
    REG(x24);
    REG(x25);
    REG(x26);
    REG(x27);
    REG(x28);
  case gpr_fp_arm64:
    value = (u_int64_t)sf.sf_x29;
    return true;
  case gpr_sp_arm64:
    value = (u_int64_t)(pcb.pcb_sp + sizeof(sf));
    return true;
  case gpr_pc_arm64:
    value = (u_int64_t)sf.sf_lr;
    return true;
  }
#endif
  return false;
}

bool RegisterContextOpenBSDKernel_arm64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}
