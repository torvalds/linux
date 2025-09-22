//===-- RegisterContextPOSIXCore_mips64.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextPOSIXCore_mips64.h"

#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"

#include <memory>

using namespace lldb_private;

RegisterContextCorePOSIX_mips64::RegisterContextCorePOSIX_mips64(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContextPOSIX_mips64(thread, 0, register_info) {
  m_gpr_buffer = std::make_shared<DataBufferHeap>(gpregset.GetDataStart(),
                                                  gpregset.GetByteSize());
  m_gpr.SetData(m_gpr_buffer);
  m_gpr.SetByteOrder(gpregset.GetByteOrder());

  DataExtractor fpregset = getRegset(
      notes, register_info->GetTargetArchitecture().GetTriple(), FPR_Desc);
  m_fpr_buffer = std::make_shared<DataBufferHeap>(fpregset.GetDataStart(),
                                                  fpregset.GetByteSize());
  m_fpr.SetData(m_fpr_buffer);
  m_fpr.SetByteOrder(fpregset.GetByteOrder());
}

RegisterContextCorePOSIX_mips64::~RegisterContextCorePOSIX_mips64() = default;

bool RegisterContextCorePOSIX_mips64::ReadGPR() { return true; }

bool RegisterContextCorePOSIX_mips64::ReadFPR() { return false; }

bool RegisterContextCorePOSIX_mips64::WriteGPR() {
  assert(0);
  return false;
}

bool RegisterContextCorePOSIX_mips64::WriteFPR() {
  assert(0);
  return false;
}

bool RegisterContextCorePOSIX_mips64::ReadRegister(const RegisterInfo *reg_info,
                                                   RegisterValue &value) {
  
  lldb::offset_t offset = reg_info->byte_offset;
  lldb_private::ArchSpec arch = m_register_info_up->GetTargetArchitecture();
  uint64_t v;
  if (IsGPR(reg_info->kinds[lldb::eRegisterKindLLDB])) {
    if (reg_info->byte_size == 4 && !(arch.GetMachine() == llvm::Triple::mips64el))
      // In case of 32bit core file, the register data are placed at 4 byte
      // offset.
      offset = offset / 2;
    v = m_gpr.GetMaxU64(&offset, reg_info->byte_size);
    value = v;
    return true;
  } else if (IsFPR(reg_info->kinds[lldb::eRegisterKindLLDB])) {
    offset = offset - sizeof(GPR_linux_mips);
    v =m_fpr.GetMaxU64(&offset, reg_info->byte_size);
    value = v;
    return true;
    }
  return false;
}

bool RegisterContextCorePOSIX_mips64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_mips64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &value) {
  return false;
}

bool RegisterContextCorePOSIX_mips64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  return false;
}

bool RegisterContextCorePOSIX_mips64::HardwareSingleStep(bool enable) {
  return false;
}
