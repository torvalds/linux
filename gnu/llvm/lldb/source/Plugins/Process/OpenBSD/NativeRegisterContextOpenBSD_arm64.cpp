//===-- NativeRegisterContextOpenBSD_arm64.cpp ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if defined(__arm64__) || defined(__aarch64__)

#include <elf.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>

#include "NativeRegisterContextOpenBSD_arm64.h"

#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "llvm/ADT/APInt.h"

#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <machine/cpu.h>
// clang-format on

using namespace lldb_private;
using namespace lldb_private::process_openbsd;

#define REG_CONTEXT_SIZE (GetGPRSize() + GetFPRSize())

std::unique_ptr<NativeRegisterContextOpenBSD>
NativeRegisterContextOpenBSD::CreateHostNativeRegisterContextOpenBSD(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread) {
  return std::make_unique<NativeRegisterContextOpenBSD_arm64>(target_arch, native_thread);
}

// ----------------------------------------------------------------------------
// NativeRegisterContextOpenBSD_arm64 members.
// ----------------------------------------------------------------------------

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
         "Register setting path assumes this is a 64-bit host");

  Flags opt_regsets = RegisterInfoPOSIX_arm64::eRegsetMaskPAuth;
  return new RegisterInfoPOSIX_arm64(target_arch, opt_regsets);
}

static llvm::APInt uint128ToAPInt(__uint128_t in) {
  uint64_t *pair = (uint64_t *)&in;
  llvm::APInt out(128, 2, pair);
  return out;
}

static __uint128_t APIntTouint128(llvm::APInt in) {
  assert(in.getBitWidth() == 128);
  __uint128_t out = 0;
  const uint64_t *data = in.getRawData();
  ::memcpy((uint64_t *)&out, data, sizeof(__uint128_t));
  return out;
}

NativeRegisterContextOpenBSD_arm64::NativeRegisterContextOpenBSD_arm64(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextOpenBSD(native_thread,
                                  CreateRegisterInfoInterface(target_arch)),
      m_gpr(), m_fpr() {}

RegisterInfoPOSIX_arm64 &
NativeRegisterContextOpenBSD_arm64::GetRegisterInfo() const {
  return static_cast<RegisterInfoPOSIX_arm64 &>(*m_register_info_interface_up);
}

uint32_t NativeRegisterContextOpenBSD_arm64::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

const RegisterSet *
NativeRegisterContextOpenBSD_arm64::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

uint32_t NativeRegisterContextOpenBSD_arm64::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

Status
NativeRegisterContextOpenBSD_arm64::ReadRegister(const RegisterInfo *reg_info,
                                                 RegisterValue &reg_value) {
  Status error;
  Log *log = GetLog(POSIXLog::Registers);

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot read directly",
                                   reg_info->name);
    return error;
  }

  int set = GetSetForNativeRegNum(reg);
  if (set == -1) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  if (ReadRegisterSet(set) != 0) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat(
        "reading register set for register \"%s\" failed", reg_info->name);
    return error;
  }

  if (GetRegisterInfo().IsPAuthReg(reg)) {
    uint32_t offset;

    offset = reg_info->byte_offset - GetRegisterInfo().GetPAuthOffset();
    reg_value = (uint64_t)m_pacmask[offset > 0];
    if (reg_value.GetByteSize() > reg_info->byte_size) {
      reg_value.SetType(*reg_info);
    }

    return error;
  }

  switch (reg) {
  case gpr_x0_arm64:
  case gpr_x1_arm64:
  case gpr_x2_arm64:
  case gpr_x3_arm64:
  case gpr_x4_arm64:
  case gpr_x5_arm64:
  case gpr_x6_arm64:
  case gpr_x7_arm64:
  case gpr_x8_arm64:
  case gpr_x9_arm64:
  case gpr_x10_arm64:
  case gpr_x11_arm64:
  case gpr_x12_arm64:
  case gpr_x13_arm64:
  case gpr_x14_arm64:
  case gpr_x15_arm64:
  case gpr_x16_arm64:
  case gpr_x17_arm64:
  case gpr_x18_arm64:
  case gpr_x19_arm64:
  case gpr_x20_arm64:
  case gpr_x21_arm64:
  case gpr_x22_arm64:
  case gpr_x23_arm64:
  case gpr_x24_arm64:
  case gpr_x25_arm64:
  case gpr_x26_arm64:
  case gpr_x27_arm64:
  case gpr_x28_arm64:
    reg_value = (uint64_t)m_gpr.r_reg[reg - gpr_x0_arm64];
    break;
  case gpr_fp_arm64:
    reg_value = (uint64_t)m_gpr.r_reg[29];
    break;
  case gpr_lr_arm64:
    reg_value = (uint64_t)m_gpr.r_lr;
    break;
  case gpr_sp_arm64:
    reg_value = (uint64_t)m_gpr.r_sp;
    break;
  case gpr_pc_arm64:
    reg_value = (uint64_t)m_gpr.r_pc;
    break;
  case gpr_cpsr_arm64:
    reg_value = (uint64_t)m_gpr.r_spsr;
    break;
  case fpu_v0_arm64:
  case fpu_v1_arm64:
  case fpu_v2_arm64:
  case fpu_v3_arm64:
  case fpu_v4_arm64:
  case fpu_v5_arm64:
  case fpu_v6_arm64:
  case fpu_v7_arm64:
  case fpu_v8_arm64:
  case fpu_v9_arm64:
  case fpu_v10_arm64:
  case fpu_v11_arm64:
  case fpu_v12_arm64:
  case fpu_v13_arm64:
  case fpu_v14_arm64:
  case fpu_v15_arm64:
  case fpu_v16_arm64:
  case fpu_v17_arm64:
  case fpu_v18_arm64:
  case fpu_v19_arm64:
  case fpu_v20_arm64:
  case fpu_v21_arm64:
  case fpu_v22_arm64:
  case fpu_v23_arm64:
  case fpu_v24_arm64:
  case fpu_v25_arm64:
  case fpu_v26_arm64:
  case fpu_v27_arm64:
  case fpu_v28_arm64:
  case fpu_v29_arm64:
  case fpu_v30_arm64:
  case fpu_v31_arm64:
    reg_value = uint128ToAPInt(m_fpr.fp_reg[reg - fpu_v0_arm64]);
    break;
  case fpu_fpsr_arm64:
    reg_value = (uint32_t)m_fpr.fp_sr;
    break;
  case fpu_fpcr_arm64:
    reg_value = (uint32_t)m_fpr.fp_cr;
    break;
  default:
    log->Printf("Requested read to unhandled reg: %u", reg);
    break;
  }

  if (reg_value.GetByteSize() > reg_info->byte_size) {
    reg_value.SetType(*reg_info);
  }

  return error;
}

Status NativeRegisterContextOpenBSD_arm64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {

  Status error;
  Log *log = GetLog(POSIXLog::Registers);

  if (!reg_info) {
    error.SetErrorString("reg_info NULL");
    return error;
  }

  const uint32_t reg = reg_info->kinds[lldb::eRegisterKindLLDB];
  if (reg == LLDB_INVALID_REGNUM) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is an internal-only lldb "
                                   "register, cannot read directly",
                                   reg_info->name);
    return error;
  }

  int set = GetSetForNativeRegNum(reg);
  if (set == -1) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat("register \"%s\" is in unrecognized set",
                                   reg_info->name);
    return error;
  }

  if (ReadRegisterSet(set) != 0) {
    // This is likely an internal register for lldb use only and should not be
    // directly queried.
    error.SetErrorStringWithFormat(
        "reading register set for register \"%s\" failed", reg_info->name);
    return error;
  }

  switch (reg) {
  case gpr_x0_arm64:
  case gpr_x1_arm64:
  case gpr_x2_arm64:
  case gpr_x3_arm64:
  case gpr_x4_arm64:
  case gpr_x5_arm64:
  case gpr_x6_arm64:
  case gpr_x7_arm64:
  case gpr_x8_arm64:
  case gpr_x9_arm64:
  case gpr_x10_arm64:
  case gpr_x11_arm64:
  case gpr_x12_arm64:
  case gpr_x13_arm64:
  case gpr_x14_arm64:
  case gpr_x15_arm64:
  case gpr_x16_arm64:
  case gpr_x17_arm64:
  case gpr_x18_arm64:
  case gpr_x19_arm64:
  case gpr_x20_arm64:
  case gpr_x21_arm64:
  case gpr_x22_arm64:
  case gpr_x23_arm64:
  case gpr_x24_arm64:
  case gpr_x25_arm64:
  case gpr_x26_arm64:
  case gpr_x27_arm64:
  case gpr_x28_arm64:
    m_gpr.r_reg[reg - gpr_x0_arm64] = reg_value.GetAsUInt64();
    break;
  case gpr_fp_arm64:
    m_gpr.r_reg[29] = reg_value.GetAsUInt64();
    break;
  case gpr_lr_arm64:
    m_gpr.r_lr = reg_value.GetAsUInt64();
    break;
  case gpr_sp_arm64:
    m_gpr.r_sp = reg_value.GetAsUInt64();
    break;
  case gpr_pc_arm64:
    m_gpr.r_pc = reg_value.GetAsUInt64();
    break;
  case gpr_cpsr_arm64:
    m_gpr.r_spsr = reg_value.GetAsUInt64();
    break;
  case fpu_v0_arm64:
  case fpu_v1_arm64:
  case fpu_v2_arm64:
  case fpu_v3_arm64:
  case fpu_v4_arm64:
  case fpu_v5_arm64:
  case fpu_v6_arm64:
  case fpu_v7_arm64:
  case fpu_v8_arm64:
  case fpu_v9_arm64:
  case fpu_v10_arm64:
  case fpu_v11_arm64:
  case fpu_v12_arm64:
  case fpu_v13_arm64:
  case fpu_v14_arm64:
  case fpu_v15_arm64:
  case fpu_v16_arm64:
  case fpu_v17_arm64:
  case fpu_v18_arm64:
  case fpu_v19_arm64:
  case fpu_v20_arm64:
  case fpu_v21_arm64:
  case fpu_v22_arm64:
  case fpu_v23_arm64:
  case fpu_v24_arm64:
  case fpu_v25_arm64:
  case fpu_v26_arm64:
  case fpu_v27_arm64:
  case fpu_v28_arm64:
  case fpu_v29_arm64:
  case fpu_v30_arm64:
  case fpu_v31_arm64:
    m_fpr.fp_reg[reg - fpu_v0_arm64] =
      APIntTouint128(reg_value.GetAsUInt128(llvm::APInt(128, 0, false)));
    break;
  case fpu_fpsr_arm64:
    m_fpr.fp_sr = reg_value.GetAsUInt32();
    break;
  case fpu_fpcr_arm64:
    m_fpr.fp_cr = reg_value.GetAsUInt32();
    break;
  default:
    log->Printf("Requested write of unhandled reg: %u", reg);
    break;
  }

  if (WriteRegisterSet(set) != 0)
    error.SetErrorStringWithFormat("failed to write register set");

  return error;
}

Status NativeRegisterContextOpenBSD_arm64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "failed to allocate DataBufferHeap instance of size %zu",
        REG_CONTEXT_SIZE);
    return error;
  }

  error = ReadGPR();
  if (error.Fail())
    return error;

  error = ReadFPR();
  if (error.Fail())
    return error;

  uint8_t *dst = data_sp->GetBytes();
  if (dst == nullptr) {
    error.SetErrorStringWithFormat("DataBufferHeap instance of size %zu"
                                   " returned a null pointer",
                                   REG_CONTEXT_SIZE);
    return error;
  }

  ::memcpy(dst, &m_gpr, GetGPRSize());
  dst += GetGPRSize();

  ::memcpy(dst, &m_fpr, GetFPRSize());
  dst += GetFPRSize();

  return error;
}

Status NativeRegisterContextOpenBSD_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextOpenBSD_arm64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextOpenBSD_arm64::%s data_sp contained mismatched "
        "data size, expected %zu, actual %llu",
        __FUNCTION__, REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextOpenBSD_arm64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  // TODO ?
  // Do we need to make a custom RegisterInfoOpenBSD_arm64
  // because the RegisterInfoPOSIX_arm64 doesn't quite match
  // our machine/reg.h?
  ::memcpy(&m_gpr, src, GetGPRSize());
  error = WriteGPR();
  if (error.Fail())
    return error;
  src += GetGPRSize();

  ::memcpy(&m_fpr, src, GetFPRSize());
  error = WriteFPR();
  if (error.Fail())
    return error;
  src += GetFPRSize();

  return error;
}

int NativeRegisterContextOpenBSD_arm64::GetSetForNativeRegNum(
    int reg_num) const {
  if (reg_num >= k_first_gpr_arm64 && reg_num <= k_last_gpr_arm64)
    return GPRegSet;
  else if (reg_num >= k_first_fpr_arm64 && reg_num <= k_last_fpr_arm64)
    return FPRegSet;
  else if (GetRegisterInfo().IsPAuthReg(reg_num))
    return PACMaskRegSet;
  else
    return -1;
}

int NativeRegisterContextOpenBSD_arm64::ReadRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    ReadGPR();
    return 0;
  case FPRegSet:
    ReadFPR();
    return 0;
  case PACMaskRegSet:
    ReadPACMask();
    return 0;
  default:
    break;
  }
  return -1;
}

int NativeRegisterContextOpenBSD_arm64::WriteRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    WriteGPR();
    return 0;
  case FPRegSet:
    WriteFPR();
    return 0;
  default:
    break;
  }
  return -1;
}

Status NativeRegisterContextOpenBSD_arm64::ReadPACMask() {
#ifdef PT_PACMASK
  return NativeProcessOpenBSD::PtraceWrapper(PT_PACMASK, GetProcessPid(),
					     &m_pacmask, sizeof(m_pacmask));
#else
  Status error;
  ::memset(&m_pacmask, 0, sizeof(m_pacmask));
  return error;
#endif
}

#endif
