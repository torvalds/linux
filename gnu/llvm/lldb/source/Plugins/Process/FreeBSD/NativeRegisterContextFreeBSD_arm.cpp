//===-- NativeRegisterContextFreeBSD_arm.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__arm__)

#include "NativeRegisterContextFreeBSD_arm.h"

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm.h"

// clang-format off
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/types.h>
// clang-format on

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_freebsd;

NativeRegisterContextFreeBSD *
NativeRegisterContextFreeBSD::CreateHostNativeRegisterContextFreeBSD(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread) {
  return new NativeRegisterContextFreeBSD_arm(target_arch, native_thread);
}

NativeRegisterContextFreeBSD_arm::NativeRegisterContextFreeBSD_arm(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, new RegisterInfoPOSIX_arm(target_arch)) {}

RegisterInfoPOSIX_arm &
NativeRegisterContextFreeBSD_arm::GetRegisterInfo() const {
  return static_cast<RegisterInfoPOSIX_arm &>(*m_register_info_interface_up);
}

uint32_t NativeRegisterContextFreeBSD_arm::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

const RegisterSet *
NativeRegisterContextFreeBSD_arm::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

uint32_t NativeRegisterContextFreeBSD_arm::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

Status NativeRegisterContextFreeBSD_arm::ReadRegisterSet(uint32_t set) {
  switch (set) {
  case RegisterInfoPOSIX_arm::GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_GETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case RegisterInfoPOSIX_arm::FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_GETVFPREGS, m_thread.GetID(),
        m_reg_data.data() + sizeof(RegisterInfoPOSIX_arm::GPR));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_arm::ReadRegisterSet");
}

Status NativeRegisterContextFreeBSD_arm::WriteRegisterSet(uint32_t set) {
  switch (set) {
  case RegisterInfoPOSIX_arm::GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_SETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case RegisterInfoPOSIX_arm::FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_SETVFPREGS, m_thread.GetID(),
        m_reg_data.data() + sizeof(RegisterInfoPOSIX_arm::GPR));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_arm::WriteRegisterSet");
}

Status
NativeRegisterContextFreeBSD_arm::ReadRegister(const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  Status error;

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (reg == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  uint32_t set = GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg);
  error = ReadRegisterSet(set);
  if (error.Fail())
    return error;

  assert(reg_info->byte_offset + reg_info->byte_size <= m_reg_data.size());
  reg_value.SetBytes(m_reg_data.data() + reg_info->byte_offset,
                     reg_info->byte_size, endian::InlHostByteOrder());
  return error;
}

Status NativeRegisterContextFreeBSD_arm::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Status error;

  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (reg == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  uint32_t set = GetRegisterInfo().GetRegisterSetFromRegisterIndex(reg);
  error = ReadRegisterSet(set);
  if (error.Fail())
    return error;

  assert(reg_info->byte_offset + reg_info->byte_size <= m_reg_data.size());
  ::memcpy(m_reg_data.data() + reg_info->byte_offset, reg_value.GetBytes(),
           reg_info->byte_size);

  return WriteRegisterSet(set);
}

Status NativeRegisterContextFreeBSD_arm::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  error = ReadRegisterSet(RegisterInfoPOSIX_arm::GPRegSet);
  if (error.Fail())
    return error;

  error = ReadRegisterSet(RegisterInfoPOSIX_arm::FPRegSet);
  if (error.Fail())
    return error;

  data_sp.reset(new DataBufferHeap(m_reg_data.size(), 0));
  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, m_reg_data.data(), m_reg_data.size());

  return error;
}

Status NativeRegisterContextFreeBSD_arm::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_arm::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != m_reg_data.size()) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_arm::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, m_reg_data.size(), data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextFreeBSD_arm::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  ::memcpy(m_reg_data.data(), src, m_reg_data.size());

  error = WriteRegisterSet(RegisterInfoPOSIX_arm::GPRegSet);
  if (error.Fail())
    return error;

  return WriteRegisterSet(RegisterInfoPOSIX_arm::FPRegSet);
}

llvm::Error NativeRegisterContextFreeBSD_arm::CopyHardwareWatchpointsFrom(
    NativeRegisterContextFreeBSD &source) {
  return llvm::Error::success();
}

#endif // defined (__arm__)
