//===-- NativeRegisterContextFreeBSD_mips64.cpp ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__mips64__)

#include "NativeRegisterContextFreeBSD_mips64.h"

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"
#include "Plugins/Process/Utility/lldb-mips-freebsd-register-enums.h"

// clang-format off
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/types.h>
// clang-format on
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_freebsd;

NativeRegisterContextFreeBSD *
NativeRegisterContextFreeBSD::CreateHostNativeRegisterContextFreeBSD(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread) {
  return new NativeRegisterContextFreeBSD_mips64(target_arch, native_thread);
}

NativeRegisterContextFreeBSD_mips64::NativeRegisterContextFreeBSD_mips64(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, new RegisterContextFreeBSD_mips64(target_arch)) {}

RegisterContextFreeBSD_mips64 &
NativeRegisterContextFreeBSD_mips64::GetRegisterInfo() const {
  return static_cast<RegisterContextFreeBSD_mips64 &>(
      *m_register_info_interface_up);
}

uint32_t NativeRegisterContextFreeBSD_mips64::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

const RegisterSet *
NativeRegisterContextFreeBSD_mips64::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

uint32_t NativeRegisterContextFreeBSD_mips64::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

std::optional<NativeRegisterContextFreeBSD_mips64::RegSetKind>
NativeRegisterContextFreeBSD_mips64::GetSetForNativeRegNum(
    uint32_t reg_num) const {
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::mips64:
    if (reg_num >= k_first_gpr_mips64 && reg_num <= k_last_gpr_mips64)
      return GPRegSet;
    if (reg_num >= k_first_fpr_mips64 && reg_num <= k_last_fpr_mips64)
      return FPRegSet;
    break;
  default:
    llvm_unreachable("Unhandled target architecture.");
  }

  llvm_unreachable("Register does not belong to any register set");
}

Status NativeRegisterContextFreeBSD_mips64::ReadRegisterSet(RegSetKind set) {
  switch (set) {
  case GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_GETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_GETFPREGS, m_thread.GetID(),
        m_reg_data.data() + GetRegisterInfo().GetGPRSize());
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_mips64::ReadRegisterSet");
}

Status NativeRegisterContextFreeBSD_mips64::WriteRegisterSet(RegSetKind set) {
  switch (set) {
  case GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_SETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_SETFPREGS, m_thread.GetID(),
        m_reg_data.data() + GetRegisterInfo().GetGPRSize());
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_mips64::WriteRegisterSet");
}

Status
NativeRegisterContextFreeBSD_mips64::ReadRegister(const RegisterInfo *reg_info,
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

  std::optional<RegSetKind> opt_set = GetSetForNativeRegNum(reg);
  if (!opt_set) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  RegSetKind set = *opt_set;
  error = ReadRegisterSet(set);
  if (error.Fail())
    return error;

  assert(reg_info->byte_offset + reg_info->byte_size <= m_reg_data.size());
  reg_value.SetBytes(m_reg_data.data() + reg_info->byte_offset,
                     reg_info->byte_size, endian::InlHostByteOrder());
  return error;
}

Status NativeRegisterContextFreeBSD_mips64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Status error;

  if (!reg_info)
    return Status("reg_info NULL");

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];

  if (reg == LLDB_INVALID_REGNUM)
    return Status("no lldb regnum for %s", reg_info && reg_info->name
                                               ? reg_info->name
                                               : "<unknown register>");

  std::optional<RegSetKind> opt_set = GetSetForNativeRegNum(reg);
  if (!opt_set) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  RegSetKind set = *opt_set;
  error = ReadRegisterSet(set);
  if (error.Fail())
    return error;

  assert(reg_info->byte_offset + reg_info->byte_size <= m_reg_data.size());
  ::memcpy(m_reg_data.data() + reg_info->byte_offset, reg_value.GetBytes(),
           reg_info->byte_size);

  return WriteRegisterSet(set);
}

Status NativeRegisterContextFreeBSD_mips64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  error = ReadRegisterSet(GPRegSet);
  if (error.Fail())
    return error;

  error = ReadRegisterSet(FPRegSet);
  if (error.Fail())
    return error;

  data_sp.reset(new DataBufferHeap(m_reg_data.size(), 0));
  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, m_reg_data.data(), m_reg_data.size());

  return error;
}

Status NativeRegisterContextFreeBSD_mips64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_mips64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != m_reg_data.size()) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_mips64::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, m_reg_data.size(), data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextFreeBSD_mips64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  ::memcpy(m_reg_data.data(), src, m_reg_data.size());

  error = WriteRegisterSet(GPRegSet);
  if (error.Fail())
    return error;

  return WriteRegisterSet(FPRegSet);
}

llvm::Error NativeRegisterContextFreeBSD_mips64::CopyHardwareWatchpointsFrom(
    NativeRegisterContextFreeBSD &source) {
  return llvm::Error::success();
}

#endif // defined (__mips64__)
