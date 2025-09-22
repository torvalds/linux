//===-- NativeRegisterContextFreeBSD_powerpc.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__powerpc__)

#include "NativeRegisterContextFreeBSD_powerpc.h"

#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"
// for register enum definitions
#include "Plugins/Process/Utility/RegisterContextPOSIX_powerpc.h"

// clang-format off
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/types.h>
// clang-format on
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_freebsd;

static const uint32_t g_gpr_regnums[] = {
    gpr_r0_powerpc,  gpr_r1_powerpc,  gpr_r2_powerpc,  gpr_r3_powerpc,
    gpr_r4_powerpc,  gpr_r5_powerpc,  gpr_r6_powerpc,  gpr_r7_powerpc,
    gpr_r8_powerpc,  gpr_r9_powerpc,  gpr_r10_powerpc, gpr_r11_powerpc,
    gpr_r12_powerpc, gpr_r13_powerpc, gpr_r14_powerpc, gpr_r15_powerpc,
    gpr_r16_powerpc, gpr_r17_powerpc, gpr_r18_powerpc, gpr_r19_powerpc,
    gpr_r20_powerpc, gpr_r21_powerpc, gpr_r22_powerpc, gpr_r23_powerpc,
    gpr_r24_powerpc, gpr_r25_powerpc, gpr_r26_powerpc, gpr_r27_powerpc,
    gpr_r28_powerpc, gpr_r29_powerpc, gpr_r30_powerpc, gpr_r31_powerpc,
    gpr_lr_powerpc,  gpr_cr_powerpc,  gpr_xer_powerpc, gpr_ctr_powerpc,
    gpr_pc_powerpc,
};

static const uint32_t g_fpr_regnums[] = {
    fpr_f0_powerpc,    fpr_f1_powerpc,  fpr_f2_powerpc,  fpr_f3_powerpc,
    fpr_f4_powerpc,    fpr_f5_powerpc,  fpr_f6_powerpc,  fpr_f7_powerpc,
    fpr_f8_powerpc,    fpr_f9_powerpc,  fpr_f10_powerpc, fpr_f11_powerpc,
    fpr_f12_powerpc,   fpr_f13_powerpc, fpr_f14_powerpc, fpr_f15_powerpc,
    fpr_f16_powerpc,   fpr_f17_powerpc, fpr_f18_powerpc, fpr_f19_powerpc,
    fpr_f20_powerpc,   fpr_f21_powerpc, fpr_f22_powerpc, fpr_f23_powerpc,
    fpr_f24_powerpc,   fpr_f25_powerpc, fpr_f26_powerpc, fpr_f27_powerpc,
    fpr_f28_powerpc,   fpr_f29_powerpc, fpr_f30_powerpc, fpr_f31_powerpc,
    fpr_fpscr_powerpc,
};

// Number of register sets provided by this context.
enum { k_num_register_sets = 2 };

static const RegisterSet g_reg_sets_powerpc[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_powerpc,
     g_gpr_regnums},
    {"Floating Point Registers", "fpr", k_num_fpr_registers_powerpc,
     g_fpr_regnums},
};

NativeRegisterContextFreeBSD *
NativeRegisterContextFreeBSD::CreateHostNativeRegisterContextFreeBSD(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread) {
  return new NativeRegisterContextFreeBSD_powerpc(target_arch, native_thread);
}

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  if (HostInfo::GetArchitecture().GetAddressByteSize() == 4) {
    return new RegisterContextFreeBSD_powerpc32(target_arch);
  } else {
    assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
           "Register setting path assumes this is a 64-bit host");
    return new RegisterContextFreeBSD_powerpc64(target_arch);
  }
}

NativeRegisterContextFreeBSD_powerpc::NativeRegisterContextFreeBSD_powerpc(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, CreateRegisterInfoInterface(target_arch)) {}

RegisterContextFreeBSD_powerpc &
NativeRegisterContextFreeBSD_powerpc::GetRegisterInfo() const {
  return static_cast<RegisterContextFreeBSD_powerpc &>(
      *m_register_info_interface_up);
}

uint32_t NativeRegisterContextFreeBSD_powerpc::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterSet *
NativeRegisterContextFreeBSD_powerpc::GetRegisterSet(uint32_t set_index) const {
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::ppc:
    return &g_reg_sets_powerpc[set_index];
  default:
    llvm_unreachable("Unhandled target architecture.");
  }
}

std::optional<NativeRegisterContextFreeBSD_powerpc::RegSetKind>
NativeRegisterContextFreeBSD_powerpc::GetSetForNativeRegNum(
    uint32_t reg_num) const {
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::ppc:
    if (reg_num >= k_first_gpr_powerpc && reg_num <= k_last_gpr_powerpc)
      return GPRegSet;
    if (reg_num >= k_first_fpr && reg_num <= k_last_fpr)
      return FPRegSet;
    break;
  default:
    llvm_unreachable("Unhandled target architecture.");
  }

  llvm_unreachable("Register does not belong to any register set");
}

uint32_t NativeRegisterContextFreeBSD_powerpc::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

Status NativeRegisterContextFreeBSD_powerpc::ReadRegisterSet(RegSetKind set) {
  switch (set) {
  case GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_GETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_GETFPREGS, m_thread.GetID(),
                                               m_reg_data.data() + sizeof(reg));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_powerpc::ReadRegisterSet");
}

Status NativeRegisterContextFreeBSD_powerpc::WriteRegisterSet(RegSetKind set) {
  switch (set) {
  case GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_SETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_SETFPREGS, m_thread.GetID(),
                                               m_reg_data.data() + sizeof(reg));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_powerpc::WriteRegisterSet");
}

Status
NativeRegisterContextFreeBSD_powerpc::ReadRegister(const RegisterInfo *reg_info,
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

Status NativeRegisterContextFreeBSD_powerpc::WriteRegister(
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

Status NativeRegisterContextFreeBSD_powerpc::ReadAllRegisterValues(
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

Status NativeRegisterContextFreeBSD_powerpc::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_powerpc::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != m_reg_data.size()) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_powerpc::%s data_sp contained mismatched "
        "data size, expected %zu, actual %" PRIu64,
        __FUNCTION__, m_reg_data.size(), data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextFreeBSD_powerpc::%s "
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

llvm::Error NativeRegisterContextFreeBSD_powerpc::CopyHardwareWatchpointsFrom(
    NativeRegisterContextFreeBSD &source) {
  return llvm::Error::success();
}

#endif // defined (__powerpc__)
