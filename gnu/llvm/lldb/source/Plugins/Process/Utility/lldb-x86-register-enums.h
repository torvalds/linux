//===-- lldb-x86-register-enums.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_X86_REGISTER_ENUMS_H
#define LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_X86_REGISTER_ENUMS_H

namespace lldb_private {
// LLDB register codes (e.g. RegisterKind == eRegisterKindLLDB)

// Internal codes for all i386 registers.
enum {
  k_first_gpr_i386,
  lldb_eax_i386 = k_first_gpr_i386,
  lldb_ebx_i386,
  lldb_ecx_i386,
  lldb_edx_i386,
  lldb_edi_i386,
  lldb_esi_i386,
  lldb_ebp_i386,
  lldb_esp_i386,
  lldb_eip_i386,
  lldb_eflags_i386,
  lldb_cs_i386,
  lldb_fs_i386,
  lldb_gs_i386,
  lldb_ss_i386,
  lldb_ds_i386,
  lldb_es_i386,

  k_first_alias_i386,
  lldb_ax_i386 = k_first_alias_i386,
  lldb_bx_i386,
  lldb_cx_i386,
  lldb_dx_i386,
  lldb_di_i386,
  lldb_si_i386,
  lldb_bp_i386,
  lldb_sp_i386,
  lldb_ah_i386,
  lldb_bh_i386,
  lldb_ch_i386,
  lldb_dh_i386,
  lldb_al_i386,
  lldb_bl_i386,
  lldb_cl_i386,
  lldb_dl_i386,
  k_last_alias_i386 = lldb_dl_i386,

  k_last_gpr_i386 = k_last_alias_i386,

  k_first_fpr_i386,
  lldb_fctrl_i386 = k_first_fpr_i386,
  lldb_fstat_i386,
  lldb_ftag_i386,
  lldb_fop_i386,
  lldb_fiseg_i386,
  lldb_fioff_i386,
  lldb_foseg_i386,
  lldb_fooff_i386,
  lldb_mxcsr_i386,
  lldb_mxcsrmask_i386,
  lldb_st0_i386,
  lldb_st1_i386,
  lldb_st2_i386,
  lldb_st3_i386,
  lldb_st4_i386,
  lldb_st5_i386,
  lldb_st6_i386,
  lldb_st7_i386,
  lldb_mm0_i386,
  lldb_mm1_i386,
  lldb_mm2_i386,
  lldb_mm3_i386,
  lldb_mm4_i386,
  lldb_mm5_i386,
  lldb_mm6_i386,
  lldb_mm7_i386,
  lldb_xmm0_i386,
  lldb_xmm1_i386,
  lldb_xmm2_i386,
  lldb_xmm3_i386,
  lldb_xmm4_i386,
  lldb_xmm5_i386,
  lldb_xmm6_i386,
  lldb_xmm7_i386,
  k_last_fpr_i386 = lldb_xmm7_i386,

  k_first_avx_i386,
  lldb_ymm0_i386 = k_first_avx_i386,
  lldb_ymm1_i386,
  lldb_ymm2_i386,
  lldb_ymm3_i386,
  lldb_ymm4_i386,
  lldb_ymm5_i386,
  lldb_ymm6_i386,
  lldb_ymm7_i386,
  k_last_avx_i386 = lldb_ymm7_i386,

  k_first_mpxr_i386,
  lldb_bnd0_i386 = k_first_mpxr_i386,
  lldb_bnd1_i386,
  lldb_bnd2_i386,
  lldb_bnd3_i386,
  k_last_mpxr_i386 = lldb_bnd3_i386,

  k_first_mpxc_i386,
  lldb_bndcfgu_i386 = k_first_mpxc_i386,
  lldb_bndstatus_i386,
  k_last_mpxc_i386 = lldb_bndstatus_i386,

  k_first_dbr_i386,
  lldb_dr0_i386 = k_first_dbr_i386,
  lldb_dr1_i386,
  lldb_dr2_i386,
  lldb_dr3_i386,
  lldb_dr4_i386,
  lldb_dr5_i386,
  lldb_dr6_i386,
  lldb_dr7_i386,
  k_last_dbr_i386 = lldb_dr7_i386,

  k_num_registers_i386,
  k_num_gpr_registers_i386 = k_last_gpr_i386 - k_first_gpr_i386 + 1,
  k_num_fpr_registers_i386 = k_last_fpr_i386 - k_first_fpr_i386 + 1,
  k_num_avx_registers_i386 = k_last_avx_i386 - k_first_avx_i386 + 1,
  k_num_mpx_registers_i386 = k_last_mpxc_i386 - k_first_mpxr_i386 + 1,
  k_num_user_registers_i386 = k_num_gpr_registers_i386 +
                              k_num_fpr_registers_i386 +
                              k_num_avx_registers_i386 +
                              k_num_mpx_registers_i386,
  k_num_dbr_registers_i386 = k_last_dbr_i386 - k_first_dbr_i386 + 1,
};

// Internal codes for all x86_64 registers.
enum {
  k_first_gpr_x86_64,
  lldb_rax_x86_64 = k_first_gpr_x86_64,
  lldb_rbx_x86_64,
  lldb_rcx_x86_64,
  lldb_rdx_x86_64,
  lldb_rdi_x86_64,
  lldb_rsi_x86_64,
  lldb_rbp_x86_64,
  lldb_rsp_x86_64,
  lldb_r8_x86_64,
  lldb_r9_x86_64,
  lldb_r10_x86_64,
  lldb_r11_x86_64,
  lldb_r12_x86_64,
  lldb_r13_x86_64,
  lldb_r14_x86_64,
  lldb_r15_x86_64,
  lldb_rip_x86_64,
  lldb_rflags_x86_64,
  lldb_cs_x86_64,
  lldb_fs_x86_64,
  lldb_gs_x86_64,
  lldb_ss_x86_64,
  lldb_ds_x86_64,
  lldb_es_x86_64,

  k_first_alias_x86_64,
  lldb_eax_x86_64 = k_first_alias_x86_64,
  lldb_ebx_x86_64,
  lldb_ecx_x86_64,
  lldb_edx_x86_64,
  lldb_edi_x86_64,
  lldb_esi_x86_64,
  lldb_ebp_x86_64,
  lldb_esp_x86_64,
  lldb_r8d_x86_64,  // Low 32 bits of r8
  lldb_r9d_x86_64,  // Low 32 bits of r9
  lldb_r10d_x86_64, // Low 32 bits of r10
  lldb_r11d_x86_64, // Low 32 bits of r11
  lldb_r12d_x86_64, // Low 32 bits of r12
  lldb_r13d_x86_64, // Low 32 bits of r13
  lldb_r14d_x86_64, // Low 32 bits of r14
  lldb_r15d_x86_64, // Low 32 bits of r15
  lldb_ax_x86_64,
  lldb_bx_x86_64,
  lldb_cx_x86_64,
  lldb_dx_x86_64,
  lldb_di_x86_64,
  lldb_si_x86_64,
  lldb_bp_x86_64,
  lldb_sp_x86_64,
  lldb_r8w_x86_64,  // Low 16 bits of r8
  lldb_r9w_x86_64,  // Low 16 bits of r9
  lldb_r10w_x86_64, // Low 16 bits of r10
  lldb_r11w_x86_64, // Low 16 bits of r11
  lldb_r12w_x86_64, // Low 16 bits of r12
  lldb_r13w_x86_64, // Low 16 bits of r13
  lldb_r14w_x86_64, // Low 16 bits of r14
  lldb_r15w_x86_64, // Low 16 bits of r15
  lldb_ah_x86_64,
  lldb_bh_x86_64,
  lldb_ch_x86_64,
  lldb_dh_x86_64,
  lldb_al_x86_64,
  lldb_bl_x86_64,
  lldb_cl_x86_64,
  lldb_dl_x86_64,
  lldb_dil_x86_64,
  lldb_sil_x86_64,
  lldb_bpl_x86_64,
  lldb_spl_x86_64,
  lldb_r8l_x86_64,  // Low 8 bits of r8
  lldb_r9l_x86_64,  // Low 8 bits of r9
  lldb_r10l_x86_64, // Low 8 bits of r10
  lldb_r11l_x86_64, // Low 8 bits of r11
  lldb_r12l_x86_64, // Low 8 bits of r12
  lldb_r13l_x86_64, // Low 8 bits of r13
  lldb_r14l_x86_64, // Low 8 bits of r14
  lldb_r15l_x86_64, // Low 8 bits of r15
  k_last_alias_x86_64 = lldb_r15l_x86_64,

  k_last_gpr_x86_64 = k_last_alias_x86_64,

  k_first_fpr_x86_64,
  lldb_fctrl_x86_64 = k_first_fpr_x86_64,
  lldb_fstat_x86_64,
  lldb_ftag_x86_64,
  lldb_fop_x86_64,
  lldb_fiseg_x86_64,
  lldb_fioff_x86_64,
  lldb_fip_x86_64,
  lldb_foseg_x86_64,
  lldb_fooff_x86_64,
  lldb_fdp_x86_64,
  lldb_mxcsr_x86_64,
  lldb_mxcsrmask_x86_64,
  lldb_st0_x86_64,
  lldb_st1_x86_64,
  lldb_st2_x86_64,
  lldb_st3_x86_64,
  lldb_st4_x86_64,
  lldb_st5_x86_64,
  lldb_st6_x86_64,
  lldb_st7_x86_64,
  lldb_mm0_x86_64,
  lldb_mm1_x86_64,
  lldb_mm2_x86_64,
  lldb_mm3_x86_64,
  lldb_mm4_x86_64,
  lldb_mm5_x86_64,
  lldb_mm6_x86_64,
  lldb_mm7_x86_64,
  lldb_xmm0_x86_64,
  lldb_xmm1_x86_64,
  lldb_xmm2_x86_64,
  lldb_xmm3_x86_64,
  lldb_xmm4_x86_64,
  lldb_xmm5_x86_64,
  lldb_xmm6_x86_64,
  lldb_xmm7_x86_64,
  lldb_xmm8_x86_64,
  lldb_xmm9_x86_64,
  lldb_xmm10_x86_64,
  lldb_xmm11_x86_64,
  lldb_xmm12_x86_64,
  lldb_xmm13_x86_64,
  lldb_xmm14_x86_64,
  lldb_xmm15_x86_64,
  k_last_fpr_x86_64 = lldb_xmm15_x86_64,

  k_first_avx_x86_64,
  lldb_ymm0_x86_64 = k_first_avx_x86_64,
  lldb_ymm1_x86_64,
  lldb_ymm2_x86_64,
  lldb_ymm3_x86_64,
  lldb_ymm4_x86_64,
  lldb_ymm5_x86_64,
  lldb_ymm6_x86_64,
  lldb_ymm7_x86_64,
  lldb_ymm8_x86_64,
  lldb_ymm9_x86_64,
  lldb_ymm10_x86_64,
  lldb_ymm11_x86_64,
  lldb_ymm12_x86_64,
  lldb_ymm13_x86_64,
  lldb_ymm14_x86_64,
  lldb_ymm15_x86_64,
  k_last_avx_x86_64 = lldb_ymm15_x86_64,

  k_first_mpxr_x86_64,
  lldb_bnd0_x86_64 = k_first_mpxr_x86_64,
  lldb_bnd1_x86_64,
  lldb_bnd2_x86_64,
  lldb_bnd3_x86_64,
  k_last_mpxr_x86_64 = lldb_bnd3_x86_64,

  k_first_mpxc_x86_64,
  lldb_bndcfgu_x86_64 = k_first_mpxc_x86_64,
  lldb_bndstatus_x86_64,
  k_last_mpxc_x86_64 = lldb_bndstatus_x86_64,

  k_first_dbr_x86_64,
  lldb_dr0_x86_64 = k_first_dbr_x86_64,
  lldb_dr1_x86_64,
  lldb_dr2_x86_64,
  lldb_dr3_x86_64,
  lldb_dr4_x86_64,
  lldb_dr5_x86_64,
  lldb_dr6_x86_64,
  lldb_dr7_x86_64,
  k_last_dbr_x86_64 = lldb_dr7_x86_64,

  k_num_registers_x86_64,
  k_num_gpr_registers_x86_64 = k_last_gpr_x86_64 - k_first_gpr_x86_64 + 1,
  k_num_fpr_registers_x86_64 = k_last_fpr_x86_64 - k_first_fpr_x86_64 + 1,
  k_num_avx_registers_x86_64 = k_last_avx_x86_64 - k_first_avx_x86_64 + 1,
  k_num_mpx_registers_x86_64 = k_last_mpxc_x86_64 - k_first_mpxr_x86_64 + 1,
  k_num_user_registers_x86_64 = k_num_gpr_registers_x86_64 +
                                k_num_fpr_registers_x86_64 +
                                k_num_avx_registers_x86_64 +
                                k_num_mpx_registers_x86_64,
  k_num_dbr_registers_x86_64 = k_last_dbr_x86_64 - k_first_dbr_x86_64 + 1,
};

// For platform that supports fs_base/gs_base registers.
namespace x86_64_with_base {
enum {
  k_first_gpr,
  lldb_rax = k_first_gpr,
  lldb_rbx,
  lldb_rcx,
  lldb_rdx,
  lldb_rdi,
  lldb_rsi,
  lldb_rbp,
  lldb_rsp,
  lldb_r8,
  lldb_r9,
  lldb_r10,
  lldb_r11,
  lldb_r12,
  lldb_r13,
  lldb_r14,
  lldb_r15,
  lldb_rip,
  lldb_rflags,
  lldb_cs,
  lldb_fs,
  lldb_gs,
  lldb_ss,
  lldb_fs_base,
  lldb_gs_base,
  lldb_ds,
  lldb_es,

  k_first_alias,
  lldb_eax = k_first_alias,
  lldb_ebx,
  lldb_ecx,
  lldb_edx,
  lldb_edi,
  lldb_esi,
  lldb_ebp,
  lldb_esp,
  lldb_r8d,  // Low 32 bits of r8
  lldb_r9d,  // Low 32 bits of r9
  lldb_r10d, // Low 32 bits of r10
  lldb_r11d, // Low 32 bits of r11
  lldb_r12d, // Low 32 bits of r12
  lldb_r13d, // Low 32 bits of r13
  lldb_r14d, // Low 32 bits of r14
  lldb_r15d, // Low 32 bits of r15
  lldb_ax,
  lldb_bx,
  lldb_cx,
  lldb_dx,
  lldb_di,
  lldb_si,
  lldb_bp,
  lldb_sp,
  lldb_r8w,  // Low 16 bits of r8
  lldb_r9w,  // Low 16 bits of r9
  lldb_r10w, // Low 16 bits of r10
  lldb_r11w, // Low 16 bits of r11
  lldb_r12w, // Low 16 bits of r12
  lldb_r13w, // Low 16 bits of r13
  lldb_r14w, // Low 16 bits of r14
  lldb_r15w, // Low 16 bits of r15
  lldb_ah,
  lldb_bh,
  lldb_ch,
  lldb_dh,
  lldb_al,
  lldb_bl,
  lldb_cl,
  lldb_dl,
  lldb_dil,
  lldb_sil,
  lldb_bpl,
  lldb_spl,
  lldb_r8l,  // Low 8 bits of r8
  lldb_r9l,  // Low 8 bits of r9
  lldb_r10l, // Low 8 bits of r10
  lldb_r11l, // Low 8 bits of r11
  lldb_r12l, // Low 8 bits of r12
  lldb_r13l, // Low 8 bits of r13
  lldb_r14l, // Low 8 bits of r14
  lldb_r15l, // Low 8 bits of r15
  k_last_alias = lldb_r15l,

  k_last_gpr = k_last_alias,

  k_first_fpr,
  lldb_fctrl = k_first_fpr,
  lldb_fstat,
  lldb_ftag,
  lldb_fop,
  lldb_fiseg,
  lldb_fioff,
  lldb_fip,
  lldb_foseg,
  lldb_fooff,
  lldb_fdp,
  lldb_mxcsr,
  lldb_mxcsrmask,
  lldb_st0,
  lldb_st1,
  lldb_st2,
  lldb_st3,
  lldb_st4,
  lldb_st5,
  lldb_st6,
  lldb_st7,
  lldb_mm0,
  lldb_mm1,
  lldb_mm2,
  lldb_mm3,
  lldb_mm4,
  lldb_mm5,
  lldb_mm6,
  lldb_mm7,
  lldb_xmm0,
  lldb_xmm1,
  lldb_xmm2,
  lldb_xmm3,
  lldb_xmm4,
  lldb_xmm5,
  lldb_xmm6,
  lldb_xmm7,
  lldb_xmm8,
  lldb_xmm9,
  lldb_xmm10,
  lldb_xmm11,
  lldb_xmm12,
  lldb_xmm13,
  lldb_xmm14,
  lldb_xmm15,
  k_last_fpr = lldb_xmm15,

  k_first_avx,
  lldb_ymm0 = k_first_avx,
  lldb_ymm1,
  lldb_ymm2,
  lldb_ymm3,
  lldb_ymm4,
  lldb_ymm5,
  lldb_ymm6,
  lldb_ymm7,
  lldb_ymm8,
  lldb_ymm9,
  lldb_ymm10,
  lldb_ymm11,
  lldb_ymm12,
  lldb_ymm13,
  lldb_ymm14,
  lldb_ymm15,
  k_last_avx = lldb_ymm15,

  k_first_mpxr,
  lldb_bnd0 = k_first_mpxr,
  lldb_bnd1,
  lldb_bnd2,
  lldb_bnd3,
  k_last_mpxr = lldb_bnd3,

  k_first_mpxc,
  lldb_bndcfgu = k_first_mpxc,
  lldb_bndstatus,
  k_last_mpxc = lldb_bndstatus,

  k_first_dbr,
  lldb_dr0 = k_first_dbr,
  lldb_dr1,
  lldb_dr2,
  lldb_dr3,
  lldb_dr4,
  lldb_dr5,
  lldb_dr6,
  lldb_dr7,
  k_last_dbr = lldb_dr7,

  k_num_registers,
  k_num_gpr_registers = k_last_gpr - k_first_gpr + 1,
  k_num_fpr_registers = k_last_fpr - k_first_fpr + 1,
  k_num_avx_registers = k_last_avx - k_first_avx + 1,
  k_num_mpx_registers = k_last_mpxc - k_first_mpxr + 1,
  k_num_user_registers = k_num_gpr_registers +
                                k_num_fpr_registers +
                                k_num_avx_registers +
                                k_num_mpx_registers,
  k_num_dbr_registers = k_last_dbr - k_first_dbr + 1,
};
} // namespace x86_64_with_base

}

#endif // LLDB_SOURCE_PLUGINS_PROCESS_UTILITY_LLDB_X86_REGISTER_ENUMS_H
