//===-- RegisterContextPOSIX_riscv64.cpp ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_riscv64.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextPOSIX_riscv64::RegisterContextPOSIX_riscv64(
    lldb_private::Thread &thread,
    std::unique_ptr<RegisterInfoPOSIX_riscv64> register_info)
    : lldb_private::RegisterContext(thread, 0),
      m_register_info_up(std::move(register_info)) {}

RegisterContextPOSIX_riscv64::~RegisterContextPOSIX_riscv64() = default;

void RegisterContextPOSIX_riscv64::invalidate() {}

void RegisterContextPOSIX_riscv64::InvalidateAllRegisters() {}

size_t RegisterContextPOSIX_riscv64::GetRegisterCount() {
  return m_register_info_up->GetRegisterCount();
}

size_t RegisterContextPOSIX_riscv64::GetGPRSize() {
  return m_register_info_up->GetGPRSize();
}

unsigned RegisterContextPOSIX_riscv64::GetRegisterSize(unsigned int reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_size;
}

unsigned RegisterContextPOSIX_riscv64::GetRegisterOffset(unsigned int reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_offset;
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_riscv64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < GetRegisterCount())
    return &GetRegisterInfo()[reg];

  return nullptr;
}

size_t RegisterContextPOSIX_riscv64::GetRegisterSetCount() {
  return m_register_info_up->GetRegisterSetCount();
}

const lldb_private::RegisterSet *
RegisterContextPOSIX_riscv64::GetRegisterSet(size_t set) {
  return m_register_info_up->GetRegisterSet(set);
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_riscv64::GetRegisterInfo() {
  return m_register_info_up->GetRegisterInfo();
}

bool RegisterContextPOSIX_riscv64::IsGPR(unsigned int reg) {
  return m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
         RegisterInfoPOSIX_riscv64::GPRegSet;
}

bool RegisterContextPOSIX_riscv64::IsFPR(unsigned int reg) {
  return m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
         RegisterInfoPOSIX_riscv64::FPRegSet;
}
