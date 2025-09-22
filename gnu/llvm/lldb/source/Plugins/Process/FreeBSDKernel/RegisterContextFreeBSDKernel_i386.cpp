//===-- RegisterContextFreeBSDKernel_i386.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextFreeBSDKernel_i386.h"

#include "lldb/Target/Process.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "llvm/Support/Endian.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextFreeBSDKernel_i386::RegisterContextFreeBSDKernel_i386(
    Thread &thread, RegisterInfoInterface *register_info, lldb::addr_t pcb_addr)
    : RegisterContextPOSIX_x86(thread, 0, register_info), m_pcb_addr(pcb_addr) {
}

bool RegisterContextFreeBSDKernel_i386::ReadGPR() { return true; }

bool RegisterContextFreeBSDKernel_i386::ReadFPR() { return true; }

bool RegisterContextFreeBSDKernel_i386::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextFreeBSDKernel_i386::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextFreeBSDKernel_i386::ReadRegister(
    const RegisterInfo *reg_info, RegisterValue &value) {
  if (m_pcb_addr == LLDB_INVALID_ADDRESS)
    return false;

  struct {
    llvm::support::ulittle32_t edi;
    llvm::support::ulittle32_t esi;
    llvm::support::ulittle32_t ebp;
    llvm::support::ulittle32_t esp;
    llvm::support::ulittle32_t ebx;
    llvm::support::ulittle32_t eip;
  } pcb;

  Status error;
  size_t rd =
      m_thread.GetProcess()->ReadMemory(m_pcb_addr, &pcb, sizeof(pcb), error);
  if (rd != sizeof(pcb))
    return false;

  uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  switch (reg) {
#define REG(x)                                                                 \
  case lldb_##x##_i386:                                                      \
    value = pcb.x;                                                             \
    break;

    REG(edi);
    REG(esi);
    REG(ebp);
    REG(esp);
    REG(eip);

#undef REG

  default:
    return false;
  }

  return true;
}

bool RegisterContextFreeBSDKernel_i386::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}
