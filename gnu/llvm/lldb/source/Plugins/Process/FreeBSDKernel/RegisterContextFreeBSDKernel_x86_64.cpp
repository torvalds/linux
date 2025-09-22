//===-- RegisterContextFreeBSDKernel_x86_64.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextFreeBSDKernel_x86_64.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/Support/Endian.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextFreeBSDKernel_x86_64::RegisterContextFreeBSDKernel_x86_64(
    Thread &thread, RegisterInfoInterface *register_info, lldb::addr_t pcb_addr)
    : RegisterContextPOSIX_x86(thread, 0, register_info), m_pcb_addr(pcb_addr) {
}

bool RegisterContextFreeBSDKernel_x86_64::ReadGPR() { return true; }

bool RegisterContextFreeBSDKernel_x86_64::ReadFPR() { return true; }

bool RegisterContextFreeBSDKernel_x86_64::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextFreeBSDKernel_x86_64::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextFreeBSDKernel_x86_64::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (m_pcb_addr == LLDB_INVALID_ADDRESS)
    return false;

  struct {
    llvm::support::ulittle64_t r15;
    llvm::support::ulittle64_t r14;
    llvm::support::ulittle64_t r13;
    llvm::support::ulittle64_t r12;
    llvm::support::ulittle64_t rbp;
    llvm::support::ulittle64_t rsp;
    llvm::support::ulittle64_t rbx;
    llvm::support::ulittle64_t rip;
  } pcb;

  Status error;
  size_t rd =
      m_thread.GetProcess()->ReadMemory(m_pcb_addr, &pcb, sizeof(pcb), error);
  if (rd != sizeof(pcb))
    return false;

  uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  switch (reg) {
#define REG(x)                                                                 \
  case lldb_##x##_x86_64:                                                      \
    value = pcb.x;                                                             \
    break;

    REG(r15);
    REG(r14);
    REG(r13);
    REG(r12);
    REG(rbp);
    REG(rsp);
    REG(rbx);
    REG(rip);

#undef REG

  default:
    return false;
  }

  return true;
}

bool RegisterContextFreeBSDKernel_x86_64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}
