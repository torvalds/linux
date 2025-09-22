//===-- RegisterInfos_x86_64_with_base_shared.cpp--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterInfos_x86_64_with_base_shared.h"

#include "lldb/lldb-defines.h"
#include <mutex>

using namespace lldb;

namespace lldb_private {

uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_eax[] = {
    lldb_eax_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_ebx[] = {
    lldb_ebx_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_ecx[] = {
    lldb_ecx_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_edx[] = {
    lldb_edx_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_edi[] = {
    lldb_edi_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_esi[] = {
    lldb_esi_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_ebp[] = {
    lldb_ebp_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_esp[] = {
    lldb_esp_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_eax[] = {
    lldb_eax_i386, lldb_ax_i386, lldb_ah_i386, lldb_al_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_ebx[] = {
    lldb_ebx_i386, lldb_bx_i386, lldb_bh_i386, lldb_bl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_ecx[] = {
    lldb_ecx_i386, lldb_cx_i386, lldb_ch_i386, lldb_cl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_edx[] = {
    lldb_edx_i386, lldb_dx_i386, lldb_dh_i386, lldb_dl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_edi[] = {
    lldb_edi_i386, lldb_di_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_esi[] = {
    lldb_esi_i386, lldb_si_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_ebp[] = {
    lldb_ebp_i386, lldb_bp_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_esp[] = {
    lldb_esp_i386, lldb_sp_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rax[] = {
    x86_64_with_base::lldb_rax, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rbx[] = {
    x86_64_with_base::lldb_rbx, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rcx[] = {
    x86_64_with_base::lldb_rcx, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rdx[] = {
    x86_64_with_base::lldb_rdx, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rdi[] = {
    x86_64_with_base::lldb_rdi, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rsi[] = {
    x86_64_with_base::lldb_rsi, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rbp[] = {
    x86_64_with_base::lldb_rbp, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_rsp[] = {
    x86_64_with_base::lldb_rsp, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r8[] = {
    x86_64_with_base::lldb_r8, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r9[] = {
    x86_64_with_base::lldb_r9, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r10[] = {
    x86_64_with_base::lldb_r10, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r11[] = {
    x86_64_with_base::lldb_r11, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r12[] = {
    x86_64_with_base::lldb_r12, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r13[] = {
    x86_64_with_base::lldb_r13, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r14[] = {
    x86_64_with_base::lldb_r14, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_r15[] = {
    x86_64_with_base::lldb_r15, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rax[] = {
    x86_64_with_base::lldb_rax, x86_64_with_base::lldb_eax,
    x86_64_with_base::lldb_ax,  x86_64_with_base::lldb_ah,
    x86_64_with_base::lldb_al,  LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rbx[] = {
    x86_64_with_base::lldb_rbx, x86_64_with_base::lldb_ebx,
    x86_64_with_base::lldb_bx,  x86_64_with_base::lldb_bh,
    x86_64_with_base::lldb_bl,  LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rcx[] = {
    x86_64_with_base::lldb_rcx, x86_64_with_base::lldb_ecx,
    x86_64_with_base::lldb_cx,  x86_64_with_base::lldb_ch,
    x86_64_with_base::lldb_cl,  LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rdx[] = {
    x86_64_with_base::lldb_rdx, x86_64_with_base::lldb_edx,
    x86_64_with_base::lldb_dx,  x86_64_with_base::lldb_dh,
    x86_64_with_base::lldb_dl,  LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rdi[] = {
    x86_64_with_base::lldb_rdi, x86_64_with_base::lldb_edi,
    x86_64_with_base::lldb_di, x86_64_with_base::lldb_dil, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rsi[] = {
    x86_64_with_base::lldb_rsi, x86_64_with_base::lldb_esi,
    x86_64_with_base::lldb_si, x86_64_with_base::lldb_sil, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rbp[] = {
    x86_64_with_base::lldb_rbp, x86_64_with_base::lldb_ebp,
    x86_64_with_base::lldb_bp, x86_64_with_base::lldb_bpl, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_rsp[] = {
    x86_64_with_base::lldb_rsp, x86_64_with_base::lldb_esp,
    x86_64_with_base::lldb_sp, x86_64_with_base::lldb_spl, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r8[] = {
    x86_64_with_base::lldb_r8, x86_64_with_base::lldb_r8d,
    x86_64_with_base::lldb_r8w, x86_64_with_base::lldb_r8l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r9[] = {
    x86_64_with_base::lldb_r9, x86_64_with_base::lldb_r9d,
    x86_64_with_base::lldb_r9w, x86_64_with_base::lldb_r9l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r10[] = {
    x86_64_with_base::lldb_r10, x86_64_with_base::lldb_r10d,
    x86_64_with_base::lldb_r10w, x86_64_with_base::lldb_r10l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r11[] = {
    x86_64_with_base::lldb_r11, x86_64_with_base::lldb_r11d,
    x86_64_with_base::lldb_r11w, x86_64_with_base::lldb_r11l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r12[] = {
    x86_64_with_base::lldb_r12, x86_64_with_base::lldb_r12d,
    x86_64_with_base::lldb_r12w, x86_64_with_base::lldb_r12l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r13[] = {
    x86_64_with_base::lldb_r13, x86_64_with_base::lldb_r13d,
    x86_64_with_base::lldb_r13w, x86_64_with_base::lldb_r13l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r14[] = {
    x86_64_with_base::lldb_r14, x86_64_with_base::lldb_r14d,
    x86_64_with_base::lldb_r14w, x86_64_with_base::lldb_r14l,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_r15[] = {
    x86_64_with_base::lldb_r15, x86_64_with_base::lldb_r15d,
    x86_64_with_base::lldb_r15w, x86_64_with_base::lldb_r15l,
    LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_fip[] = {
    x86_64_with_base::lldb_fip, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_fdp[] = {
    x86_64_with_base::lldb_fdp, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_fip[] = {
    x86_64_with_base::lldb_fip, x86_64_with_base::lldb_fioff,
    x86_64_with_base::lldb_fiseg, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_fdp[] = {
    x86_64_with_base::lldb_fdp, x86_64_with_base::lldb_fooff,
    x86_64_with_base::lldb_foseg, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st0_32[] = {
    lldb_st0_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st1_32[] = {
    lldb_st1_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st2_32[] = {
    lldb_st2_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st3_32[] = {
    lldb_st3_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st4_32[] = {
    lldb_st4_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st5_32[] = {
    lldb_st5_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st6_32[] = {
    lldb_st6_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st7_32[] = {
    lldb_st7_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st0_32[] = {
    lldb_st0_i386, lldb_mm0_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st1_32[] = {
    lldb_st1_i386, lldb_mm1_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st2_32[] = {
    lldb_st2_i386, lldb_mm2_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st3_32[] = {
    lldb_st3_i386, lldb_mm3_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st4_32[] = {
    lldb_st4_i386, lldb_mm4_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st5_32[] = {
    lldb_st5_i386, lldb_mm5_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st6_32[] = {
    lldb_st6_i386, lldb_mm6_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st7_32[] = {
    lldb_st7_i386, lldb_mm7_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st0_64[] = {
    x86_64_with_base::lldb_st0, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st1_64[] = {
    x86_64_with_base::lldb_st1, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st2_64[] = {
    x86_64_with_base::lldb_st2, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st3_64[] = {
    x86_64_with_base::lldb_st3, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st4_64[] = {
    x86_64_with_base::lldb_st4, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st5_64[] = {
    x86_64_with_base::lldb_st5, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st6_64[] = {
    x86_64_with_base::lldb_st6, LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_contained_st7_64[] = {
    x86_64_with_base::lldb_st7, LLDB_INVALID_REGNUM};

uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st0_64[] = {
    x86_64_with_base::lldb_st0, x86_64_with_base::lldb_mm0,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st1_64[] = {
    x86_64_with_base::lldb_st1, x86_64_with_base::lldb_mm1,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st2_64[] = {
    x86_64_with_base::lldb_st2, x86_64_with_base::lldb_mm2,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st3_64[] = {
    x86_64_with_base::lldb_st3, x86_64_with_base::lldb_mm3,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st4_64[] = {
    x86_64_with_base::lldb_st4, x86_64_with_base::lldb_mm4,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st5_64[] = {
    x86_64_with_base::lldb_st5, x86_64_with_base::lldb_mm5,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st6_64[] = {
    x86_64_with_base::lldb_st6, x86_64_with_base::lldb_mm6,
    LLDB_INVALID_REGNUM};
uint32_t RegisterInfos_x86_64_with_base_shared::g_invalidate_st7_64[] = {
    x86_64_with_base::lldb_st7, x86_64_with_base::lldb_mm7,
    LLDB_INVALID_REGNUM};

RegInfo &GetRegInfoShared(llvm::Triple::ArchType arch_type, bool with_base) {
  static std::once_flag once_flag_x86, once_flag_x86_64,
      once_flag_x86_64_with_base;
  static RegInfo reg_info_x86, reg_info_x86_64, reg_info_x86_64_with_base, reg_info_invalid;

  switch (arch_type) {
  case llvm::Triple::x86:
    std::call_once(once_flag_x86, []() {
      reg_info_x86.num_registers = k_num_registers_i386;
      reg_info_x86.num_gpr_registers = k_num_gpr_registers_i386;
      reg_info_x86.num_fpr_registers = k_num_fpr_registers_i386;
      reg_info_x86.num_avx_registers = k_num_avx_registers_i386;
      reg_info_x86.last_gpr = k_last_gpr_i386;
      reg_info_x86.first_fpr = k_first_fpr_i386;
      reg_info_x86.last_fpr = k_last_fpr_i386;
      reg_info_x86.first_st = lldb_st0_i386;
      reg_info_x86.last_st = lldb_st7_i386;
      reg_info_x86.first_mm = lldb_mm0_i386;
      reg_info_x86.last_mm = lldb_mm7_i386;
      reg_info_x86.first_xmm = lldb_xmm0_i386;
      reg_info_x86.last_xmm = lldb_xmm7_i386;
      reg_info_x86.first_ymm = lldb_ymm0_i386;
      reg_info_x86.last_ymm = lldb_ymm7_i386;
      reg_info_x86.first_dr = lldb_dr0_i386;
      reg_info_x86.gpr_flags = lldb_eflags_i386;
    });

    return reg_info_x86;
  case llvm::Triple::x86_64:
    if (with_base) {
      std::call_once(once_flag_x86_64_with_base, []() {
        reg_info_x86_64_with_base.num_registers =
            x86_64_with_base::k_num_registers;
        reg_info_x86_64_with_base.num_gpr_registers =
            x86_64_with_base::k_num_gpr_registers;
        reg_info_x86_64_with_base.num_fpr_registers =
            x86_64_with_base::k_num_fpr_registers;
        reg_info_x86_64_with_base.num_avx_registers =
            x86_64_with_base::k_num_avx_registers;
        reg_info_x86_64_with_base.last_gpr = x86_64_with_base::k_last_gpr;
        reg_info_x86_64_with_base.first_fpr = x86_64_with_base::k_first_fpr;
        reg_info_x86_64_with_base.last_fpr = x86_64_with_base::k_last_fpr;
        reg_info_x86_64_with_base.first_st = x86_64_with_base::lldb_st0;
        reg_info_x86_64_with_base.last_st = x86_64_with_base::lldb_st7;
        reg_info_x86_64_with_base.first_mm = x86_64_with_base::lldb_mm0;
        reg_info_x86_64_with_base.last_mm = x86_64_with_base::lldb_mm7;
        reg_info_x86_64_with_base.first_xmm = x86_64_with_base::lldb_xmm0;
        reg_info_x86_64_with_base.last_xmm = x86_64_with_base::lldb_xmm15;
        reg_info_x86_64_with_base.first_ymm = x86_64_with_base::lldb_ymm0;
        reg_info_x86_64_with_base.last_ymm = x86_64_with_base::lldb_ymm15;
        reg_info_x86_64_with_base.first_dr = x86_64_with_base::lldb_dr0;
        reg_info_x86_64_with_base.gpr_flags = x86_64_with_base::lldb_rflags;
      });

      return reg_info_x86_64_with_base;
    } else {
      std::call_once(once_flag_x86_64, []() {
        reg_info_x86_64.num_registers = k_num_registers_x86_64;
        reg_info_x86_64.num_gpr_registers = k_num_gpr_registers_x86_64;
        reg_info_x86_64.num_fpr_registers = k_num_fpr_registers_x86_64;
        reg_info_x86_64.num_avx_registers = k_num_avx_registers_x86_64;
        reg_info_x86_64.last_gpr = k_last_gpr_x86_64;
        reg_info_x86_64.first_fpr = k_first_fpr_x86_64;
        reg_info_x86_64.last_fpr = k_last_fpr_x86_64;
        reg_info_x86_64.first_st = lldb_st0_x86_64;
        reg_info_x86_64.last_st = lldb_st7_x86_64;
        reg_info_x86_64.first_mm = lldb_mm0_x86_64;
        reg_info_x86_64.last_mm = lldb_mm7_x86_64;
        reg_info_x86_64.first_xmm = lldb_xmm0_x86_64;
        reg_info_x86_64.last_xmm = lldb_xmm15_x86_64;
        reg_info_x86_64.first_ymm = lldb_ymm0_x86_64;
        reg_info_x86_64.last_ymm = lldb_ymm15_x86_64;
        reg_info_x86_64.first_dr = lldb_dr0_x86_64;
        reg_info_x86_64.gpr_flags = lldb_rflags_x86_64;
      });
      return reg_info_x86_64;
    }
  default:
    assert(false && "Unhandled target architecture.");
    return reg_info_invalid;
  }
}

} // namespace lldb_private
