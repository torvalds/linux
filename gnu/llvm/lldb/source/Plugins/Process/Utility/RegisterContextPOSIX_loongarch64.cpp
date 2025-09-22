//===-- RegisterContextPOSIX_loongarch64.cpp --------------------*- C++ -*-===//
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

#include "RegisterContextPOSIX_loongarch64.h"

using namespace lldb;
using namespace lldb_private;

RegisterContextPOSIX_loongarch64::RegisterContextPOSIX_loongarch64(
    lldb_private::Thread &thread,
    std::unique_ptr<RegisterInfoPOSIX_loongarch64> register_info)
    : lldb_private::RegisterContext(thread, 0),
      m_register_info_up(std::move(register_info)) {}

RegisterContextPOSIX_loongarch64::~RegisterContextPOSIX_loongarch64() = default;

void RegisterContextPOSIX_loongarch64::invalidate() {}

void RegisterContextPOSIX_loongarch64::InvalidateAllRegisters() {}

size_t RegisterContextPOSIX_loongarch64::GetRegisterCount() {
  return m_register_info_up->GetRegisterCount();
}

size_t RegisterContextPOSIX_loongarch64::GetGPRSize() {
  return m_register_info_up->GetGPRSize();
}

unsigned RegisterContextPOSIX_loongarch64::GetRegisterSize(unsigned int reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_size;
}

unsigned RegisterContextPOSIX_loongarch64::GetRegisterOffset(unsigned int reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_offset;
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_loongarch64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < GetRegisterCount())
    return &GetRegisterInfo()[reg];

  return nullptr;
}

size_t RegisterContextPOSIX_loongarch64::GetRegisterSetCount() {
  return m_register_info_up->GetRegisterCount();
}

const lldb_private::RegisterSet *
RegisterContextPOSIX_loongarch64::GetRegisterSet(size_t set) {
  return m_register_info_up->GetRegisterSet(set);
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_loongarch64::GetRegisterInfo() {
  return m_register_info_up->GetRegisterInfo();
}

bool RegisterContextPOSIX_loongarch64::IsGPR(unsigned int reg) {
  return m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
         RegisterInfoPOSIX_loongarch64::GPRegSet;
}

bool RegisterContextPOSIX_loongarch64::IsFPR(unsigned int reg) {
  return m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
         RegisterInfoPOSIX_loongarch64::FPRegSet;
}
