//===-- NativeRegisterContextFreeBSD_arm64.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__aarch64__)

#include "NativeRegisterContextFreeBSD_arm64.h"

#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/FreeBSD/NativeProcessFreeBSD.h"
#include "Plugins/Process/POSIX/ProcessPOSIXLog.h"
#include "Plugins/Process/Utility/RegisterFlagsDetector_arm64.h"
#include "Plugins/Process/Utility/RegisterInfoPOSIX_arm64.h"

// clang-format off
#include <sys/param.h>
#include <sys/ptrace.h>
#include <sys/types.h>
// clang-format on

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::process_freebsd;

// A NativeRegisterContext is constructed per thread, but all threads' registers
// will contain the same fields. Therefore this mutex prevents each instance
// competing with the other, and subsequent instances from having to detect the
// fields all over again.
static std::mutex g_register_flags_detector_mutex;
static Arm64RegisterFlagsDetector g_register_flags_detector;

NativeRegisterContextFreeBSD *
NativeRegisterContextFreeBSD::CreateHostNativeRegisterContextFreeBSD(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread) {
  std::lock_guard<std::mutex> lock(g_register_flags_detector_mutex);
  if (!g_register_flags_detector.HasDetected()) {
    NativeProcessFreeBSD &process = native_thread.GetProcess();
    g_register_flags_detector.DetectFields(
        process.GetAuxValue(AuxVector::AUXV_FREEBSD_AT_HWCAP).value_or(0),
        process.GetAuxValue(AuxVector::AUXV_AT_HWCAP2).value_or(0));
  }

  return new NativeRegisterContextFreeBSD_arm64(target_arch, native_thread);
}

NativeRegisterContextFreeBSD_arm64::NativeRegisterContextFreeBSD_arm64(
    const ArchSpec &target_arch, NativeThreadFreeBSD &native_thread)
    : NativeRegisterContextRegisterInfo(
          native_thread, new RegisterInfoPOSIX_arm64(target_arch, 0))
#ifdef LLDB_HAS_FREEBSD_WATCHPOINT
      ,
      m_read_dbreg(false)
#endif
{
  g_register_flags_detector.UpdateRegisterInfo(
      GetRegisterInfoInterface().GetRegisterInfo(),
      GetRegisterInfoInterface().GetRegisterCount());

  ::memset(&m_hwp_regs, 0, sizeof(m_hwp_regs));
  ::memset(&m_hbp_regs, 0, sizeof(m_hbp_regs));
}

RegisterInfoPOSIX_arm64 &
NativeRegisterContextFreeBSD_arm64::GetRegisterInfo() const {
  return static_cast<RegisterInfoPOSIX_arm64 &>(*m_register_info_interface_up);
}

uint32_t NativeRegisterContextFreeBSD_arm64::GetRegisterSetCount() const {
  return GetRegisterInfo().GetRegisterSetCount();
}

const RegisterSet *
NativeRegisterContextFreeBSD_arm64::GetRegisterSet(uint32_t set_index) const {
  return GetRegisterInfo().GetRegisterSet(set_index);
}

uint32_t NativeRegisterContextFreeBSD_arm64::GetUserRegisterCount() const {
  uint32_t count = 0;
  for (uint32_t set_index = 0; set_index < GetRegisterSetCount(); ++set_index)
    count += GetRegisterSet(set_index)->num_registers;
  return count;
}

Status NativeRegisterContextFreeBSD_arm64::ReadRegisterSet(uint32_t set) {
  switch (set) {
  case RegisterInfoPOSIX_arm64::GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_GETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case RegisterInfoPOSIX_arm64::FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_GETFPREGS, m_thread.GetID(),
        m_reg_data.data() + sizeof(RegisterInfoPOSIX_arm64::GPR));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_arm64::ReadRegisterSet");
}

Status NativeRegisterContextFreeBSD_arm64::WriteRegisterSet(uint32_t set) {
  switch (set) {
  case RegisterInfoPOSIX_arm64::GPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(PT_SETREGS, m_thread.GetID(),
                                               m_reg_data.data());
  case RegisterInfoPOSIX_arm64::FPRegSet:
    return NativeProcessFreeBSD::PtraceWrapper(
        PT_SETFPREGS, m_thread.GetID(),
        m_reg_data.data() + sizeof(RegisterInfoPOSIX_arm64::GPR));
  }
  llvm_unreachable("NativeRegisterContextFreeBSD_arm64::WriteRegisterSet");
}

Status
NativeRegisterContextFreeBSD_arm64::ReadRegister(const RegisterInfo *reg_info,
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

Status NativeRegisterContextFreeBSD_arm64::WriteRegister(
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

Status NativeRegisterContextFreeBSD_arm64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  error = ReadRegisterSet(RegisterInfoPOSIX_arm64::GPRegSet);
  if (error.Fail())
    return error;

  error = ReadRegisterSet(RegisterInfoPOSIX_arm64::FPRegSet);
  if (error.Fail())
    return error;

  data_sp.reset(new DataBufferHeap(m_reg_data.size(), 0));
  uint8_t *dst = data_sp->GetBytes();
  ::memcpy(dst, m_reg_data.data(), m_reg_data.size());

  return error;
}

Status NativeRegisterContextFreeBSD_arm64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_arm64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != m_reg_data.size()) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextFreeBSD_arm64::%s data_sp contained mismatched "
        "data size, expected %" PRIu64 ", actual %" PRIu64,
        __FUNCTION__, m_reg_data.size(), data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextFreeBSD_arm64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }
  ::memcpy(m_reg_data.data(), src, m_reg_data.size());

  error = WriteRegisterSet(RegisterInfoPOSIX_arm64::GPRegSet);
  if (error.Fail())
    return error;

  return WriteRegisterSet(RegisterInfoPOSIX_arm64::FPRegSet);
}

llvm::Error NativeRegisterContextFreeBSD_arm64::CopyHardwareWatchpointsFrom(
    NativeRegisterContextFreeBSD &source) {
#ifdef LLDB_HAS_FREEBSD_WATCHPOINT
  auto &r_source = static_cast<NativeRegisterContextFreeBSD_arm64 &>(source);
  llvm::Error error = r_source.ReadHardwareDebugInfo();
  if (error)
    return error;

  m_dbreg = r_source.m_dbreg;
  m_hbp_regs = r_source.m_hbp_regs;
  m_hwp_regs = r_source.m_hwp_regs;
  m_max_hbp_supported = r_source.m_max_hbp_supported;
  m_max_hwp_supported = r_source.m_max_hwp_supported;
  m_read_dbreg = true;

  // on FreeBSD this writes both breakpoints and watchpoints
  return WriteHardwareDebugRegs(eDREGTypeWATCH);
#else
  return llvm::Error::success();
#endif
}

llvm::Error NativeRegisterContextFreeBSD_arm64::ReadHardwareDebugInfo() {
#ifdef LLDB_HAS_FREEBSD_WATCHPOINT
  Log *log = GetLog(POSIXLog::Registers);

  // we're fully stateful, so no need to reread control registers ever
  if (m_read_dbreg)
    return llvm::Error::success();

  Status res = NativeProcessFreeBSD::PtraceWrapper(PT_GETDBREGS,
                                                   m_thread.GetID(), &m_dbreg);
  if (res.Fail())
    return res.ToError();

  LLDB_LOG(log, "m_dbreg read: debug_ver={0}, nbkpts={1}, nwtpts={2}",
           m_dbreg.db_debug_ver, m_dbreg.db_nbkpts, m_dbreg.db_nwtpts);
  m_max_hbp_supported = m_dbreg.db_nbkpts;
  m_max_hwp_supported = m_dbreg.db_nwtpts;
  assert(m_max_hbp_supported <= m_hbp_regs.size());
  assert(m_max_hwp_supported <= m_hwp_regs.size());

  m_read_dbreg = true;
  return llvm::Error::success();
#else
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "Hardware breakpoints/watchpoints require FreeBSD 14.0");
#endif
}

llvm::Error
NativeRegisterContextFreeBSD_arm64::WriteHardwareDebugRegs(DREGType) {
#ifdef LLDB_HAS_FREEBSD_WATCHPOINT
  assert(m_read_dbreg && "dbregs must be read before writing them back");

  // copy data from m_*_regs to m_dbreg before writing it back
  for (uint32_t i = 0; i < m_max_hbp_supported; i++) {
    m_dbreg.db_breakregs[i].dbr_addr = m_hbp_regs[i].address;
    m_dbreg.db_breakregs[i].dbr_ctrl = m_hbp_regs[i].control;
  }
  for (uint32_t i = 0; i < m_max_hwp_supported; i++) {
    m_dbreg.db_watchregs[i].dbw_addr = m_hwp_regs[i].address;
    m_dbreg.db_watchregs[i].dbw_ctrl = m_hwp_regs[i].control;
  }

  return NativeProcessFreeBSD::PtraceWrapper(PT_SETDBREGS, m_thread.GetID(),
                                             &m_dbreg)
      .ToError();
#else
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "Hardware breakpoints/watchpoints require FreeBSD 14.0");
#endif
}

#endif // defined (__aarch64__)
