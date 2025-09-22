//===-- RegisterContextLinuxCore_x86_64.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextLinuxCore_x86_64.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/RegisterValue.h"

using namespace lldb_private;

const uint32_t g_gpr_regnums_i386[] = {
    lldb_eax_i386,       lldb_ebx_i386,    lldb_ecx_i386, lldb_edx_i386,
    lldb_edi_i386,       lldb_esi_i386,    lldb_ebp_i386, lldb_esp_i386,
    lldb_eip_i386,       lldb_eflags_i386, lldb_cs_i386,  lldb_fs_i386,
    lldb_gs_i386,        lldb_ss_i386,     lldb_ds_i386,  lldb_es_i386,
    lldb_ax_i386,        lldb_bx_i386,     lldb_cx_i386,  lldb_dx_i386,
    lldb_di_i386,        lldb_si_i386,     lldb_bp_i386,  lldb_sp_i386,
    lldb_ah_i386,        lldb_bh_i386,     lldb_ch_i386,  lldb_dh_i386,
    lldb_al_i386,        lldb_bl_i386,     lldb_cl_i386,  lldb_dl_i386,
    LLDB_INVALID_REGNUM, // Register sets must be terminated with
                         // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_gpr_regnums_i386) / sizeof(g_gpr_regnums_i386[0])) -
                      1 ==
                  k_num_gpr_registers_i386,
              "g_gpr_regnums_i386 has wrong number of register infos");

const uint32_t g_lldb_regnums_i386[] = {
    lldb_fctrl_i386,    lldb_fstat_i386,     lldb_ftag_i386,  lldb_fop_i386,
    lldb_fiseg_i386,    lldb_fioff_i386,     lldb_foseg_i386, lldb_fooff_i386,
    lldb_mxcsr_i386,    lldb_mxcsrmask_i386, lldb_st0_i386,   lldb_st1_i386,
    lldb_st2_i386,      lldb_st3_i386,       lldb_st4_i386,   lldb_st5_i386,
    lldb_st6_i386,      lldb_st7_i386,       lldb_mm0_i386,   lldb_mm1_i386,
    lldb_mm2_i386,      lldb_mm3_i386,       lldb_mm4_i386,   lldb_mm5_i386,
    lldb_mm6_i386,      lldb_mm7_i386,       lldb_xmm0_i386,  lldb_xmm1_i386,
    lldb_xmm2_i386,     lldb_xmm3_i386,      lldb_xmm4_i386,  lldb_xmm5_i386,
    lldb_xmm6_i386,     lldb_xmm7_i386,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_lldb_regnums_i386) / sizeof(g_lldb_regnums_i386[0])) -
                      1 ==
                  k_num_fpr_registers_i386,
              "g_lldb_regnums_i386 has wrong number of register infos");

const uint32_t g_avx_regnums_i386[] = {
    lldb_ymm0_i386,     lldb_ymm1_i386, lldb_ymm2_i386, lldb_ymm3_i386,
    lldb_ymm4_i386,     lldb_ymm5_i386, lldb_ymm6_i386, lldb_ymm7_i386,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_avx_regnums_i386) / sizeof(g_avx_regnums_i386[0])) -
                      1 ==
                  k_num_avx_registers_i386,
              " g_avx_regnums_i386 has wrong number of register infos");

static const uint32_t g_gpr_regnums_x86_64[] = {
    x86_64_with_base::lldb_rax,
    x86_64_with_base::lldb_rbx,
    x86_64_with_base::lldb_rcx,
    x86_64_with_base::lldb_rdx,
    x86_64_with_base::lldb_rdi,
    x86_64_with_base::lldb_rsi,
    x86_64_with_base::lldb_rbp,
    x86_64_with_base::lldb_rsp,
    x86_64_with_base::lldb_r8,
    x86_64_with_base::lldb_r9,
    x86_64_with_base::lldb_r10,
    x86_64_with_base::lldb_r11,
    x86_64_with_base::lldb_r12,
    x86_64_with_base::lldb_r13,
    x86_64_with_base::lldb_r14,
    x86_64_with_base::lldb_r15,
    x86_64_with_base::lldb_rip,
    x86_64_with_base::lldb_rflags,
    x86_64_with_base::lldb_cs,
    x86_64_with_base::lldb_fs,
    x86_64_with_base::lldb_gs,
    x86_64_with_base::lldb_ss,
    x86_64_with_base::lldb_fs_base,
    x86_64_with_base::lldb_gs_base,
    x86_64_with_base::lldb_ds,
    x86_64_with_base::lldb_es,
    x86_64_with_base::lldb_eax,
    x86_64_with_base::lldb_ebx,
    x86_64_with_base::lldb_ecx,
    x86_64_with_base::lldb_edx,
    x86_64_with_base::lldb_edi,
    x86_64_with_base::lldb_esi,
    x86_64_with_base::lldb_ebp,
    x86_64_with_base::lldb_esp,
    x86_64_with_base::lldb_r8d,  // Low 32 bits or r8
    x86_64_with_base::lldb_r9d,  // Low 32 bits or r9
    x86_64_with_base::lldb_r10d, // Low 32 bits or r10
    x86_64_with_base::lldb_r11d, // Low 32 bits or r11
    x86_64_with_base::lldb_r12d, // Low 32 bits or r12
    x86_64_with_base::lldb_r13d, // Low 32 bits or r13
    x86_64_with_base::lldb_r14d, // Low 32 bits or r14
    x86_64_with_base::lldb_r15d, // Low 32 bits or r15
    x86_64_with_base::lldb_ax,
    x86_64_with_base::lldb_bx,
    x86_64_with_base::lldb_cx,
    x86_64_with_base::lldb_dx,
    x86_64_with_base::lldb_di,
    x86_64_with_base::lldb_si,
    x86_64_with_base::lldb_bp,
    x86_64_with_base::lldb_sp,
    x86_64_with_base::lldb_r8w,  // Low 16 bits or r8
    x86_64_with_base::lldb_r9w,  // Low 16 bits or r9
    x86_64_with_base::lldb_r10w, // Low 16 bits or r10
    x86_64_with_base::lldb_r11w, // Low 16 bits or r11
    x86_64_with_base::lldb_r12w, // Low 16 bits or r12
    x86_64_with_base::lldb_r13w, // Low 16 bits or r13
    x86_64_with_base::lldb_r14w, // Low 16 bits or r14
    x86_64_with_base::lldb_r15w, // Low 16 bits or r15
    x86_64_with_base::lldb_ah,
    x86_64_with_base::lldb_bh,
    x86_64_with_base::lldb_ch,
    x86_64_with_base::lldb_dh,
    x86_64_with_base::lldb_al,
    x86_64_with_base::lldb_bl,
    x86_64_with_base::lldb_cl,
    x86_64_with_base::lldb_dl,
    x86_64_with_base::lldb_dil,
    x86_64_with_base::lldb_sil,
    x86_64_with_base::lldb_bpl,
    x86_64_with_base::lldb_spl,
    x86_64_with_base::lldb_r8l,  // Low 8 bits or r8
    x86_64_with_base::lldb_r9l,  // Low 8 bits or r9
    x86_64_with_base::lldb_r10l, // Low 8 bits or r10
    x86_64_with_base::lldb_r11l, // Low 8 bits or r11
    x86_64_with_base::lldb_r12l, // Low 8 bits or r12
    x86_64_with_base::lldb_r13l, // Low 8 bits or r13
    x86_64_with_base::lldb_r14l, // Low 8 bits or r14
    x86_64_with_base::lldb_r15l, // Low 8 bits or r15
    LLDB_INVALID_REGNUM          // Register sets must be terminated with
                                 // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_gpr_regnums_x86_64) / sizeof(g_gpr_regnums_x86_64[0])) -
                      1 ==
                  x86_64_with_base::k_num_gpr_registers,
              "g_gpr_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_lldb_regnums_x86_64[] = {
    x86_64_with_base::lldb_fctrl, x86_64_with_base::lldb_fstat,
    x86_64_with_base::lldb_ftag,  x86_64_with_base::lldb_fop,
    x86_64_with_base::lldb_fiseg, x86_64_with_base::lldb_fioff,
    x86_64_with_base::lldb_fip,   x86_64_with_base::lldb_foseg,
    x86_64_with_base::lldb_fooff, x86_64_with_base::lldb_fdp,
    x86_64_with_base::lldb_mxcsr, x86_64_with_base::lldb_mxcsrmask,
    x86_64_with_base::lldb_st0,   x86_64_with_base::lldb_st1,
    x86_64_with_base::lldb_st2,   x86_64_with_base::lldb_st3,
    x86_64_with_base::lldb_st4,   x86_64_with_base::lldb_st5,
    x86_64_with_base::lldb_st6,   x86_64_with_base::lldb_st7,
    x86_64_with_base::lldb_mm0,   x86_64_with_base::lldb_mm1,
    x86_64_with_base::lldb_mm2,   x86_64_with_base::lldb_mm3,
    x86_64_with_base::lldb_mm4,   x86_64_with_base::lldb_mm5,
    x86_64_with_base::lldb_mm6,   x86_64_with_base::lldb_mm7,
    x86_64_with_base::lldb_xmm0,  x86_64_with_base::lldb_xmm1,
    x86_64_with_base::lldb_xmm2,  x86_64_with_base::lldb_xmm3,
    x86_64_with_base::lldb_xmm4,  x86_64_with_base::lldb_xmm5,
    x86_64_with_base::lldb_xmm6,  x86_64_with_base::lldb_xmm7,
    x86_64_with_base::lldb_xmm8,  x86_64_with_base::lldb_xmm9,
    x86_64_with_base::lldb_xmm10, x86_64_with_base::lldb_xmm11,
    x86_64_with_base::lldb_xmm12, x86_64_with_base::lldb_xmm13,
    x86_64_with_base::lldb_xmm14, x86_64_with_base::lldb_xmm15,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert(
    (sizeof(g_lldb_regnums_x86_64) / sizeof(g_lldb_regnums_x86_64[0])) - 1 ==
        x86_64_with_base::k_num_fpr_registers,
    "g_lldb_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_avx_regnums_x86_64[] = {
    x86_64_with_base::lldb_ymm0,  x86_64_with_base::lldb_ymm1,
    x86_64_with_base::lldb_ymm2,  x86_64_with_base::lldb_ymm3,
    x86_64_with_base::lldb_ymm4,  x86_64_with_base::lldb_ymm5,
    x86_64_with_base::lldb_ymm6,  x86_64_with_base::lldb_ymm7,
    x86_64_with_base::lldb_ymm8,  x86_64_with_base::lldb_ymm9,
    x86_64_with_base::lldb_ymm10, x86_64_with_base::lldb_ymm11,
    x86_64_with_base::lldb_ymm12, x86_64_with_base::lldb_ymm13,
    x86_64_with_base::lldb_ymm14, x86_64_with_base::lldb_ymm15,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_avx_regnums_x86_64) / sizeof(g_avx_regnums_x86_64[0])) -
                      1 ==
                  x86_64_with_base::k_num_avx_registers,
              "g_avx_regnums_x86_64 has wrong number of register infos");

static const RegisterSet g_reg_sets_i386[] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_i386,
     g_gpr_regnums_i386},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_i386,
     g_lldb_regnums_i386},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_i386,
     g_avx_regnums_i386}};

static const RegisterSet g_reg_sets_x86_64[] = {
    {"General Purpose Registers", "gpr", x86_64_with_base::k_num_gpr_registers,
     g_gpr_regnums_x86_64},
    {"Floating Point Registers", "fpu", x86_64_with_base::k_num_fpr_registers,
     g_lldb_regnums_x86_64},
    {"Advanced Vector Extensions", "avx", x86_64_with_base::k_num_avx_registers,
     g_avx_regnums_x86_64}};

RegisterContextLinuxCore_x86_64::RegisterContextLinuxCore_x86_64(
    Thread &thread, RegisterInfoInterface *register_info,
    const DataExtractor &gpregset, llvm::ArrayRef<CoreNote> notes)
    : RegisterContextCorePOSIX_x86_64(thread, register_info, gpregset, notes) {}

const RegisterSet *RegisterContextLinuxCore_x86_64::GetRegisterSet(size_t set) {
  if (IsRegisterSetAvailable(set)) {
    switch (m_register_info_up->GetTargetArchitecture().GetMachine()) {
    case llvm::Triple::x86:
      return &g_reg_sets_i386[set];
    case llvm::Triple::x86_64:
      return &g_reg_sets_x86_64[set];
    default:
      assert(false && "Unhandled target architecture.");
      return nullptr;
    }
  }
  return nullptr;
}

RegInfo &RegisterContextLinuxCore_x86_64::GetRegInfo() {
  return GetRegInfoShared(
      m_register_info_up->GetTargetArchitecture().GetMachine(),
      /*with_base=*/true);
}
