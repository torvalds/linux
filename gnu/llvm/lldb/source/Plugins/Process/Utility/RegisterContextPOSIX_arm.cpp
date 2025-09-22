//===-- RegisterContextPOSIX_arm.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_arm.h"

using namespace lldb;
using namespace lldb_private;

bool RegisterContextPOSIX_arm::IsGPR(unsigned reg) {
  if (m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm::GPRegSet)
    return true;
  return false;
}

bool RegisterContextPOSIX_arm::IsFPR(unsigned reg) {
  if (m_register_info_up->GetRegisterSetFromRegisterIndex(reg) ==
      RegisterInfoPOSIX_arm::FPRegSet)
    return true;
  return false;
}

RegisterContextPOSIX_arm::RegisterContextPOSIX_arm(
    lldb_private::Thread &thread,
    std::unique_ptr<RegisterInfoPOSIX_arm> register_info)
    : lldb_private::RegisterContext(thread, 0),
      m_register_info_up(std::move(register_info)) {}

RegisterContextPOSIX_arm::~RegisterContextPOSIX_arm() = default;

void RegisterContextPOSIX_arm::Invalidate() {}

void RegisterContextPOSIX_arm::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_arm::GetRegisterOffset(unsigned reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_offset;
}

unsigned RegisterContextPOSIX_arm::GetRegisterSize(unsigned reg) {
  return m_register_info_up->GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_arm::GetRegisterCount() {
  return m_register_info_up->GetRegisterCount();
}

size_t RegisterContextPOSIX_arm::GetGPRSize() {
  return m_register_info_up->GetGPRSize();
}

const lldb_private::RegisterInfo *RegisterContextPOSIX_arm::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_up->GetRegisterInfo();
}

const lldb_private::RegisterInfo *
RegisterContextPOSIX_arm::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < GetRegisterCount())
    return &GetRegisterInfo()[reg];

  return nullptr;
}

size_t RegisterContextPOSIX_arm::GetRegisterSetCount() {
  return m_register_info_up->GetRegisterSetCount();
}

const lldb_private::RegisterSet *
RegisterContextPOSIX_arm::GetRegisterSet(size_t set) {
  return m_register_info_up->GetRegisterSet(set);
}

const char *RegisterContextPOSIX_arm::GetRegisterName(unsigned reg) {
  if (reg < GetRegisterCount())
    return GetRegisterInfo()[reg].name;
  return nullptr;
}
