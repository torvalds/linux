//===-- NativeRegisterContextOpenBSD_x86_64.cpp ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#if defined(__x86_64__)

#include "NativeRegisterContextOpenBSD_x86_64.h"

#include <cpuid.h>
#include <elf.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>

#include "lldb/Host/HostInfo.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"

#include "Plugins/Process/Utility/RegisterContextOpenBSD_x86_64.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"

// clang-format off
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <machine/cpu.h>
// clang-format on

using namespace lldb_private;
using namespace lldb_private::process_openbsd;

// ----------------------------------------------------------------------------
// Private namespace.
// ----------------------------------------------------------------------------

namespace {
// x86 64-bit general purpose registers.
static const uint32_t g_gpr_regnums_x86_64[] = {
    lldb_rax_x86_64,    lldb_rbx_x86_64,    lldb_rcx_x86_64,  lldb_rdx_x86_64,
    lldb_rdi_x86_64,    lldb_rsi_x86_64,    lldb_rbp_x86_64,  lldb_rsp_x86_64,
    lldb_r8_x86_64,     lldb_r9_x86_64,     lldb_r10_x86_64,  lldb_r11_x86_64,
    lldb_r12_x86_64,    lldb_r13_x86_64,    lldb_r14_x86_64,  lldb_r15_x86_64,
    lldb_rip_x86_64,    lldb_rflags_x86_64, lldb_cs_x86_64,   lldb_fs_x86_64,
    lldb_gs_x86_64,     lldb_ss_x86_64,     lldb_ds_x86_64,   lldb_es_x86_64,
    lldb_eax_x86_64,    lldb_ebx_x86_64,    lldb_ecx_x86_64,  lldb_edx_x86_64,
    lldb_edi_x86_64,    lldb_esi_x86_64,    lldb_ebp_x86_64,  lldb_esp_x86_64,
    lldb_r8d_x86_64,    lldb_r9d_x86_64,    lldb_r10d_x86_64, lldb_r11d_x86_64,
    lldb_r12d_x86_64,   lldb_r13d_x86_64,   lldb_r14d_x86_64, lldb_r15d_x86_64,
    lldb_ax_x86_64,     lldb_bx_x86_64,     lldb_cx_x86_64,   lldb_dx_x86_64,
    lldb_di_x86_64,     lldb_si_x86_64,     lldb_bp_x86_64,   lldb_sp_x86_64,
    lldb_r8w_x86_64,    lldb_r9w_x86_64,    lldb_r10w_x86_64, lldb_r11w_x86_64,
    lldb_r12w_x86_64,   lldb_r13w_x86_64,   lldb_r14w_x86_64, lldb_r15w_x86_64,
    lldb_ah_x86_64,     lldb_bh_x86_64,     lldb_ch_x86_64,   lldb_dh_x86_64,
    lldb_al_x86_64,     lldb_bl_x86_64,     lldb_cl_x86_64,   lldb_dl_x86_64,
    lldb_dil_x86_64,    lldb_sil_x86_64,    lldb_bpl_x86_64,  lldb_spl_x86_64,
    lldb_r8l_x86_64,    lldb_r9l_x86_64,    lldb_r10l_x86_64, lldb_r11l_x86_64,
    lldb_r12l_x86_64,   lldb_r13l_x86_64,   lldb_r14l_x86_64, lldb_r15l_x86_64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(
  (sizeof(g_gpr_regnums_x86_64) / sizeof(g_gpr_regnums_x86_64[0])) - 1
      == k_num_gpr_registers_x86_64,
  "g_gpr_regnums_x86_64 has wrong number of register infos");

// x86 64-bit floating point registers.
static const uint32_t g_fpu_regnums_x86_64[] = {
    lldb_fctrl_x86_64,     lldb_fstat_x86_64, lldb_ftag_x86_64,
    lldb_fop_x86_64,       lldb_fiseg_x86_64, lldb_fioff_x86_64,
    lldb_fip_x86_64,       lldb_foseg_x86_64, lldb_fooff_x86_64,
    lldb_fdp_x86_64,       lldb_mxcsr_x86_64, lldb_mxcsrmask_x86_64,
    lldb_st0_x86_64,       lldb_st1_x86_64,
    lldb_st2_x86_64,       lldb_st3_x86_64,   lldb_st4_x86_64,
    lldb_st5_x86_64,       lldb_st6_x86_64,   lldb_st7_x86_64,
    lldb_mm0_x86_64,       lldb_mm1_x86_64,   lldb_mm2_x86_64,
    lldb_mm3_x86_64,       lldb_mm4_x86_64,   lldb_mm5_x86_64,
    lldb_mm6_x86_64,       lldb_mm7_x86_64,   lldb_xmm0_x86_64,
    lldb_xmm1_x86_64,      lldb_xmm2_x86_64,  lldb_xmm3_x86_64,
    lldb_xmm4_x86_64,      lldb_xmm5_x86_64,  lldb_xmm6_x86_64,
    lldb_xmm7_x86_64,      lldb_xmm8_x86_64,  lldb_xmm9_x86_64,
    lldb_xmm10_x86_64,     lldb_xmm11_x86_64, lldb_xmm12_x86_64,
    lldb_xmm13_x86_64,     lldb_xmm14_x86_64, lldb_xmm15_x86_64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(
  (sizeof(g_fpu_regnums_x86_64) / sizeof(g_fpu_regnums_x86_64[0])) - 1
      == k_num_fpr_registers_x86_64,
  "g_fpu_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_avx_regnums_x86_64[] = {
    lldb_ymm0_x86_64,   lldb_ymm1_x86_64,  lldb_ymm2_x86_64,  lldb_ymm3_x86_64,
    lldb_ymm4_x86_64,   lldb_ymm5_x86_64,  lldb_ymm6_x86_64,  lldb_ymm7_x86_64,
    lldb_ymm8_x86_64,   lldb_ymm9_x86_64,  lldb_ymm10_x86_64, lldb_ymm11_x86_64,
    lldb_ymm12_x86_64,  lldb_ymm13_x86_64, lldb_ymm14_x86_64, lldb_ymm15_x86_64,
    LLDB_INVALID_REGNUM // register sets need to end with this flag
};
static_assert(
  (sizeof(g_avx_regnums_x86_64) / sizeof(g_avx_regnums_x86_64[0])) - 1
       == k_num_avx_registers_x86_64,
  "g_avx_regnums_x86_64 has wrong number of register infos");

// Number of register sets provided by this context.
enum { k_num_register_sets = 3 };

// Register sets for x86 64-bit.
static const RegisterSet g_reg_sets_x86_64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_x86_64,
     g_gpr_regnums_x86_64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_x86_64,
     g_fpu_regnums_x86_64},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_x86_64,
     g_avx_regnums_x86_64},
};

struct x86_fpu_addr {
  uint32_t offset;
  uint32_t selector;
};

enum {
  k_xsave_offset_legacy_region = 160,
  k_xsave_offset_invalid = UINT32_MAX,
};

} // namespace

#define REG_CONTEXT_SIZE (GetGPRSize() + GetFPRSize())

std::unique_ptr<NativeRegisterContextOpenBSD>
NativeRegisterContextOpenBSD::CreateHostNativeRegisterContextOpenBSD(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread) {
  return std::make_unique<NativeRegisterContextOpenBSD_x86_64>(target_arch, native_thread);
}

// ----------------------------------------------------------------------------
// NativeRegisterContextOpenBSD_x86_64 members.
// ----------------------------------------------------------------------------

static RegisterInfoInterface *
CreateRegisterInfoInterface(const ArchSpec &target_arch) {
  assert((HostInfo::GetArchitecture().GetAddressByteSize() == 8) &&
         "Register setting path assumes this is a 64-bit host");
  // X86_64 hosts know how to work with 64-bit and 32-bit EXEs using the x86_64
  // register context.
  return new RegisterContextOpenBSD_x86_64(target_arch);
}

NativeRegisterContextOpenBSD_x86_64::NativeRegisterContextOpenBSD_x86_64(
    const ArchSpec &target_arch, NativeThreadProtocol &native_thread)
    : NativeRegisterContextOpenBSD(native_thread,
                                  CreateRegisterInfoInterface(target_arch)),
      m_gpr(), m_fpr() {
  uint32_t a, b, c, d;

  struct ptrace_xstate_info info;
  const Status error = NativeProcessOpenBSD::PtraceWrapper(
      PT_GETXSTATE_INFO, GetProcessPid(), &info, sizeof(info));
  if (error.Success())
      m_xsave.resize(info.xsave_len);

  __get_cpuid_count(0xd, 2, &a, &b, &c, &d);
  m_xsave_offsets[YMMRegSet] = b > 0 ? b : k_xsave_offset_invalid;
}

uint32_t NativeRegisterContextOpenBSD_x86_64::GetUserRegisterCount() const {
	uint32_t count = 0;
	for (uint32_t set_index = 0; set_index < k_num_register_sets; ++set_index)
		count += g_reg_sets_x86_64[set_index].num_registers;
	return count;
}

uint32_t NativeRegisterContextOpenBSD_x86_64::GetRegisterSetCount() const {
  return k_num_register_sets;
}

const RegisterSet *
NativeRegisterContextOpenBSD_x86_64::GetRegisterSet(uint32_t set_index) const {
  switch (GetRegisterInfoInterface().GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86_64:
    return &g_reg_sets_x86_64[set_index];
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }

  return nullptr;
}

int NativeRegisterContextOpenBSD_x86_64::GetSetForNativeRegNum(
    int reg_num) const {
  if (reg_num >= k_first_gpr_x86_64 && reg_num <= k_last_gpr_x86_64)
    return GPRegSet;
  else if (reg_num >= k_first_fpr_x86_64 && reg_num <= k_last_fpr_x86_64)
    return FPRegSet;
  else if (reg_num >= k_first_avx_x86_64 && reg_num <= k_last_avx_x86_64)
    return YMMRegSet;
  else
    return -1;
}

int NativeRegisterContextOpenBSD_x86_64::ReadRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    ReadGPR();
    return 0;
  case FPRegSet:
    ReadFPR();
    return 0;
  case YMMRegSet: {
    const Status error = NativeProcessOpenBSD::PtraceWrapper(
        PT_GETXSTATE, GetProcessPid(), m_xsave.data(), m_xsave.size());
    return error.Success() ? 0 : -1;
  }
  default:
    break;
  }
  return -1;
}
int NativeRegisterContextOpenBSD_x86_64::WriteRegisterSet(uint32_t set) {
  switch (set) {
  case GPRegSet:
    WriteGPR();
    return 0;
  case FPRegSet:
    WriteFPR();
    return 0;
  case YMMRegSet: {
    const Status error = NativeProcessOpenBSD::PtraceWrapper(
        PT_SETXSTATE, GetProcessPid(), m_xsave.data(), m_xsave.size());
    return error.Success() ? 0 : -1;
  }
  default:
    break;
  }
  return -1;
}

Status
NativeRegisterContextOpenBSD_x86_64::ReadRegister(const RegisterInfo *reg_info,
                                                 RegisterValue &reg_value) {
  Status error;

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
  case lldb_rax_x86_64:
    reg_value = (uint64_t)m_gpr.r_rax;
    break;
  case lldb_rbx_x86_64:
    reg_value = (uint64_t)m_gpr.r_rbx;
    break;
  case lldb_rcx_x86_64:
    reg_value = (uint64_t)m_gpr.r_rcx;
    break;
  case lldb_rdx_x86_64:
    reg_value = (uint64_t)m_gpr.r_rdx;
    break;
  case lldb_rdi_x86_64:
    reg_value = (uint64_t)m_gpr.r_rdi;
    break;
  case lldb_rsi_x86_64:
    reg_value = (uint64_t)m_gpr.r_rsi;
    break;
  case lldb_rbp_x86_64:
    reg_value = (uint64_t)m_gpr.r_rbp;
    break;
  case lldb_rsp_x86_64:
    reg_value = (uint64_t)m_gpr.r_rsp;
    break;
  case lldb_r8_x86_64:
    reg_value = (uint64_t)m_gpr.r_r8;
    break;
  case lldb_r9_x86_64:
    reg_value = (uint64_t)m_gpr.r_r9;
    break;
  case lldb_r10_x86_64:
    reg_value = (uint64_t)m_gpr.r_r10;
    break;
  case lldb_r11_x86_64:
    reg_value = (uint64_t)m_gpr.r_r11;
    break;
  case lldb_r12_x86_64:
    reg_value = (uint64_t)m_gpr.r_r12;
    break;
  case lldb_r13_x86_64:
    reg_value = (uint64_t)m_gpr.r_r13;
    break;
  case lldb_r14_x86_64:
    reg_value = (uint64_t)m_gpr.r_r14;
    break;
  case lldb_r15_x86_64:
    reg_value = (uint64_t)m_gpr.r_r15;
    break;
  case lldb_rip_x86_64:
    reg_value = (uint64_t)m_gpr.r_rip;
    break;
  case lldb_rflags_x86_64:
    reg_value = (uint64_t)m_gpr.r_rflags;
    break;
  case lldb_cs_x86_64:
    reg_value = (uint64_t)m_gpr.r_cs;
    break;
  case lldb_fs_x86_64:
    reg_value = (uint64_t)m_gpr.r_fs;
    break;
  case lldb_gs_x86_64:
    reg_value = (uint64_t)m_gpr.r_gs;
    break;
  case lldb_ss_x86_64:
    reg_value = (uint64_t)m_gpr.r_ss;
    break;
  case lldb_ds_x86_64:
    reg_value = (uint64_t)m_gpr.r_ds;
    break;
  case lldb_es_x86_64:
    reg_value = (uint64_t)m_gpr.r_es;
    break;
  case lldb_fctrl_x86_64:
    reg_value = (uint16_t)m_fpr.fxstate.fx_fcw;
    break;
  case lldb_fstat_x86_64:
    reg_value = (uint16_t)m_fpr.fxstate.fx_fsw;
    break;
  case lldb_ftag_x86_64:
    reg_value = (uint8_t)m_fpr.fxstate.fx_ftw;
    break;
  case lldb_fop_x86_64:
    reg_value = (uint64_t)m_fpr.fxstate.fx_fop;
    break;
  case lldb_fioff_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rip;
      reg_value = fp->offset;
      break;
    }
  case lldb_fiseg_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rip;
      reg_value = fp->selector;
      break;
    }
  case lldb_fooff_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rdp;
      reg_value = fp->offset;
      break;
    }
  case lldb_foseg_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rdp;
      reg_value = fp->selector;
      break;
    }
  case lldb_mxcsr_x86_64:
    reg_value = (uint32_t)m_fpr.fxstate.fx_mxcsr;
    break;
  case lldb_mxcsrmask_x86_64:
    reg_value = (uint32_t)m_fpr.fxstate.fx_mxcsr_mask;
    break;
  case lldb_st0_x86_64:
  case lldb_st1_x86_64:
  case lldb_st2_x86_64:
  case lldb_st3_x86_64:
  case lldb_st4_x86_64:
  case lldb_st5_x86_64:
  case lldb_st6_x86_64:
  case lldb_st7_x86_64:
    reg_value.SetBytes(&m_fpr.fxstate.fx_st[reg - lldb_st0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_mm0_x86_64:
  case lldb_mm1_x86_64:
  case lldb_mm2_x86_64:
  case lldb_mm3_x86_64:
  case lldb_mm4_x86_64:
  case lldb_mm5_x86_64:
  case lldb_mm6_x86_64:
  case lldb_mm7_x86_64:
    reg_value.SetBytes(&m_fpr.fxstate.fx_st[reg - lldb_mm0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm0_x86_64:
  case lldb_xmm1_x86_64:
  case lldb_xmm2_x86_64:
  case lldb_xmm3_x86_64:
  case lldb_xmm4_x86_64:
  case lldb_xmm5_x86_64:
  case lldb_xmm6_x86_64:
  case lldb_xmm7_x86_64:
  case lldb_xmm8_x86_64:
  case lldb_xmm9_x86_64:
  case lldb_xmm10_x86_64:
  case lldb_xmm11_x86_64:
  case lldb_xmm12_x86_64:
  case lldb_xmm13_x86_64:
  case lldb_xmm14_x86_64:
  case lldb_xmm15_x86_64:
    reg_value.SetBytes(&m_fpr.fxstate.fx_xmm[reg - lldb_xmm0_x86_64],
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  }

  if (set == YMMRegSet) {
    std::optional<YMMSplitPtr> ymm_reg = GetYMMSplitReg(reg);
    if (ymm_reg) {
      YMMReg ymm = XStateToYMM(ymm_reg->xmm, ymm_reg->ymm_hi);
      reg_value.SetBytes(ymm.bytes, reg_info->byte_size,
                         endian::InlHostByteOrder());
    } else {
      error.SetErrorStringWithFormat("register \"%s\" not supported",
                                     reg_info->name);
    }
  }

  return error;
}

Status NativeRegisterContextOpenBSD_x86_64::WriteRegister(
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {

  Status error;

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
  case lldb_rax_x86_64:
    m_gpr.r_rax = reg_value.GetAsUInt64();
    break;
  case lldb_rbx_x86_64:
    m_gpr.r_rbx = reg_value.GetAsUInt64();
    break;
  case lldb_rcx_x86_64:
    m_gpr.r_rcx = reg_value.GetAsUInt64();
    break;
  case lldb_rdx_x86_64:
    m_gpr.r_rdx = reg_value.GetAsUInt64();
    break;
  case lldb_rdi_x86_64:
    m_gpr.r_rdi = reg_value.GetAsUInt64();
    break;
  case lldb_rsi_x86_64:
    m_gpr.r_rsi = reg_value.GetAsUInt64();
    break;
  case lldb_rbp_x86_64:
    m_gpr.r_rbp = reg_value.GetAsUInt64();
    break;
  case lldb_rsp_x86_64:
    m_gpr.r_rsp = reg_value.GetAsUInt64();
    break;
  case lldb_r8_x86_64:
    m_gpr.r_r8 = reg_value.GetAsUInt64();
    break;
  case lldb_r9_x86_64:
    m_gpr.r_r9 = reg_value.GetAsUInt64();
    break;
  case lldb_r10_x86_64:
    m_gpr.r_r10 = reg_value.GetAsUInt64();
    break;
  case lldb_r11_x86_64:
    m_gpr.r_r11 = reg_value.GetAsUInt64();
    break;
  case lldb_r12_x86_64:
    m_gpr.r_r12 = reg_value.GetAsUInt64();
    break;
  case lldb_r13_x86_64:
    m_gpr.r_r13 = reg_value.GetAsUInt64();
    break;
  case lldb_r14_x86_64:
    m_gpr.r_r14 = reg_value.GetAsUInt64();
    break;
  case lldb_r15_x86_64:
    m_gpr.r_r15 = reg_value.GetAsUInt64();
    break;
  case lldb_rip_x86_64:
    m_gpr.r_rip = reg_value.GetAsUInt64();
    break;
  case lldb_rflags_x86_64:
    m_gpr.r_rflags = reg_value.GetAsUInt64();
    break;
  case lldb_cs_x86_64:
    m_gpr.r_cs = reg_value.GetAsUInt64();
    break;
  case lldb_fs_x86_64:
    m_gpr.r_fs = reg_value.GetAsUInt64();
    break;
  case lldb_gs_x86_64:
    m_gpr.r_gs = reg_value.GetAsUInt64();
    break;
  case lldb_ss_x86_64:
    m_gpr.r_ss = reg_value.GetAsUInt64();
    break;
  case lldb_ds_x86_64:
    m_gpr.r_ds = reg_value.GetAsUInt64();
    break;
  case lldb_es_x86_64:
    m_gpr.r_es = reg_value.GetAsUInt64();
    break;
  case lldb_fctrl_x86_64:
    m_fpr.fxstate.fx_fcw = reg_value.GetAsUInt16();
    break;
  case lldb_fstat_x86_64:
    m_fpr.fxstate.fx_fsw = reg_value.GetAsUInt16();
    break;
  case lldb_ftag_x86_64:
    m_fpr.fxstate.fx_ftw = reg_value.GetAsUInt8();
    break;
  case lldb_fop_x86_64:
    m_fpr.fxstate.fx_fop = reg_value.GetAsUInt16();
    break;
  case lldb_fioff_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rip;
      fp->offset = reg_value.GetAsUInt32();
      break;
    }
  case lldb_fiseg_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rip;
      fp->selector = reg_value.GetAsUInt32();
      break;
    }
  case lldb_fooff_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rdp;
      fp->offset = reg_value.GetAsUInt32();
      break;
    }
  case lldb_foseg_x86_64:
    {
      struct x86_fpu_addr *fp = (struct x86_fpu_addr *)&m_fpr.fxstate.fx_rdp;
      fp->selector = reg_value.GetAsUInt32();
      break;
    }
  case lldb_mxcsr_x86_64:
    m_fpr.fxstate.fx_mxcsr = reg_value.GetAsUInt32();
    break;
  case lldb_mxcsrmask_x86_64:
    m_fpr.fxstate.fx_mxcsr_mask = reg_value.GetAsUInt32();
    break;
  case lldb_st0_x86_64:
  case lldb_st1_x86_64:
  case lldb_st2_x86_64:
  case lldb_st3_x86_64:
  case lldb_st4_x86_64:
  case lldb_st5_x86_64:
  case lldb_st6_x86_64:
  case lldb_st7_x86_64:
    ::memcpy(&m_fpr.fxstate.fx_st[reg - lldb_st0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  case lldb_mm0_x86_64:
  case lldb_mm1_x86_64:
  case lldb_mm2_x86_64:
  case lldb_mm3_x86_64:
  case lldb_mm4_x86_64:
  case lldb_mm5_x86_64:
  case lldb_mm6_x86_64:
  case lldb_mm7_x86_64:
    ::memcpy(&m_fpr.fxstate.fx_st[reg - lldb_mm0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  case lldb_xmm0_x86_64:
  case lldb_xmm1_x86_64:
  case lldb_xmm2_x86_64:
  case lldb_xmm3_x86_64:
  case lldb_xmm4_x86_64:
  case lldb_xmm5_x86_64:
  case lldb_xmm6_x86_64:
  case lldb_xmm7_x86_64:
  case lldb_xmm8_x86_64:
  case lldb_xmm9_x86_64:
  case lldb_xmm10_x86_64:
  case lldb_xmm11_x86_64:
  case lldb_xmm12_x86_64:
  case lldb_xmm13_x86_64:
  case lldb_xmm14_x86_64:
  case lldb_xmm15_x86_64:
    ::memcpy(&m_fpr.fxstate.fx_xmm[reg - lldb_xmm0_x86_64],
             reg_value.GetBytes(), reg_value.GetByteSize());
    break;
  }

  if (set == YMMRegSet) {
    std::optional<YMMSplitPtr> ymm_reg = GetYMMSplitReg(reg);
    if (!ymm_reg) {
      error.SetErrorStringWithFormat("register \"%s\" not supported",
                                     reg_info->name);
      return error;
    }
    YMMReg ymm;
    ::memcpy(ymm.bytes, reg_value.GetBytes(), reg_value.GetByteSize());
    YMMToXState(ymm, ymm_reg->xmm, ymm_reg->ymm_hi);
  }

  if (WriteRegisterSet(set) != 0)
    error.SetErrorStringWithFormat("failed to write register set");

  return error;
}

Status NativeRegisterContextOpenBSD_x86_64::ReadAllRegisterValues(
    lldb::WritableDataBufferSP &data_sp) {
  Status error;

  data_sp.reset(new DataBufferHeap(REG_CONTEXT_SIZE, 0));
  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "failed to allocate DataBufferHeap instance of size %zu",
        REG_CONTEXT_SIZE);
    return error;
  }

  uint8_t *dst = data_sp->GetBytes();
  if (dst == nullptr) {
    error.SetErrorStringWithFormat("DataBufferHeap instance of size %zu"
                                   " returned a null pointer",
                                   REG_CONTEXT_SIZE);
    return error;
  }

  error = ReadGPR();
  if (error.Fail())
    return error;
  ::memcpy(dst, &m_gpr, GetGPRSize());
  dst += GetGPRSize();

  error = ReadFPR();
  if (error.Fail())
    return error;
  ::memcpy(dst, &m_fpr, GetFPRSize());
  dst += GetFPRSize();

  return error;
}

Status NativeRegisterContextOpenBSD_x86_64::WriteAllRegisterValues(
    const lldb::DataBufferSP &data_sp) {
  Status error;

  if (!data_sp) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextOpenBSD_x86_64::%s invalid data_sp provided",
        __FUNCTION__);
    return error;
  }

  if (data_sp->GetByteSize() != REG_CONTEXT_SIZE) {
    error.SetErrorStringWithFormat(
        "NativeRegisterContextOpenBSD_x86_64::%s data_sp contained mismatched "
        "data size, expected %zu, actual %llu",
        __FUNCTION__, REG_CONTEXT_SIZE, data_sp->GetByteSize());
    return error;
  }

  const uint8_t *src = data_sp->GetBytes();
  if (src == nullptr) {
    error.SetErrorStringWithFormat("NativeRegisterContextOpenBSD_x86_64::%s "
                                   "DataBuffer::GetBytes() returned a null "
                                   "pointer",
                                   __FUNCTION__);
    return error;
  }

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

std::optional<NativeRegisterContextOpenBSD_x86_64::YMMSplitPtr>
NativeRegisterContextOpenBSD_x86_64::GetYMMSplitReg(uint32_t reg) {
  if (m_xsave_offsets[YMMRegSet] == k_xsave_offset_invalid)
    return std::nullopt;

  uint32_t reg_index = reg - lldb_ymm0_x86_64;
  auto *xmm =
      reinterpret_cast<XMMReg *>(m_xsave.data() + k_xsave_offset_legacy_region);
  auto *ymm =
      reinterpret_cast<XMMReg *>(m_xsave.data() + m_xsave_offsets[YMMRegSet]);
  return YMMSplitPtr{&xmm[reg_index], &ymm[reg_index]};
}

#endif
