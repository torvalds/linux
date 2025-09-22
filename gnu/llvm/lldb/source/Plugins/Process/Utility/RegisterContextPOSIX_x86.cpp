//===-- RegisterContextPOSIX_x86.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cerrno>
#include <cstdint>
#include <cstring>

#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"
#include "llvm/Support/Compiler.h"

#include "RegisterContextPOSIX_x86.h"
#include "RegisterContext_x86.h"

using namespace lldb_private;
using namespace lldb;

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
    lldb_rax_x86_64,    lldb_rbx_x86_64,    lldb_rcx_x86_64, lldb_rdx_x86_64,
    lldb_rdi_x86_64,    lldb_rsi_x86_64,    lldb_rbp_x86_64, lldb_rsp_x86_64,
    lldb_r8_x86_64,     lldb_r9_x86_64,     lldb_r10_x86_64, lldb_r11_x86_64,
    lldb_r12_x86_64,    lldb_r13_x86_64,    lldb_r14_x86_64, lldb_r15_x86_64,
    lldb_rip_x86_64,    lldb_rflags_x86_64, lldb_cs_x86_64,  lldb_fs_x86_64,
    lldb_gs_x86_64,     lldb_ss_x86_64,     lldb_ds_x86_64,  lldb_es_x86_64,
    lldb_eax_x86_64,    lldb_ebx_x86_64,    lldb_ecx_x86_64, lldb_edx_x86_64,
    lldb_edi_x86_64,    lldb_esi_x86_64,    lldb_ebp_x86_64, lldb_esp_x86_64,
    lldb_r8d_x86_64,  // Low 32 bits or r8
    lldb_r9d_x86_64,  // Low 32 bits or r9
    lldb_r10d_x86_64, // Low 32 bits or r10
    lldb_r11d_x86_64, // Low 32 bits or r11
    lldb_r12d_x86_64, // Low 32 bits or r12
    lldb_r13d_x86_64, // Low 32 bits or r13
    lldb_r14d_x86_64, // Low 32 bits or r14
    lldb_r15d_x86_64, // Low 32 bits or r15
    lldb_ax_x86_64,     lldb_bx_x86_64,     lldb_cx_x86_64,  lldb_dx_x86_64,
    lldb_di_x86_64,     lldb_si_x86_64,     lldb_bp_x86_64,  lldb_sp_x86_64,
    lldb_r8w_x86_64,  // Low 16 bits or r8
    lldb_r9w_x86_64,  // Low 16 bits or r9
    lldb_r10w_x86_64, // Low 16 bits or r10
    lldb_r11w_x86_64, // Low 16 bits or r11
    lldb_r12w_x86_64, // Low 16 bits or r12
    lldb_r13w_x86_64, // Low 16 bits or r13
    lldb_r14w_x86_64, // Low 16 bits or r14
    lldb_r15w_x86_64, // Low 16 bits or r15
    lldb_ah_x86_64,     lldb_bh_x86_64,     lldb_ch_x86_64,  lldb_dh_x86_64,
    lldb_al_x86_64,     lldb_bl_x86_64,     lldb_cl_x86_64,  lldb_dl_x86_64,
    lldb_dil_x86_64,    lldb_sil_x86_64,    lldb_bpl_x86_64, lldb_spl_x86_64,
    lldb_r8l_x86_64,    // Low 8 bits or r8
    lldb_r9l_x86_64,    // Low 8 bits or r9
    lldb_r10l_x86_64,   // Low 8 bits or r10
    lldb_r11l_x86_64,   // Low 8 bits or r11
    lldb_r12l_x86_64,   // Low 8 bits or r12
    lldb_r13l_x86_64,   // Low 8 bits or r13
    lldb_r14l_x86_64,   // Low 8 bits or r14
    lldb_r15l_x86_64,   // Low 8 bits or r15
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_gpr_regnums_x86_64) / sizeof(g_gpr_regnums_x86_64[0])) -
                      1 ==
                  k_num_gpr_registers_x86_64,
              "g_gpr_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_lldb_regnums_x86_64[] = {
    lldb_fctrl_x86_64,  lldb_fstat_x86_64, lldb_ftag_x86_64,
    lldb_fop_x86_64,    lldb_fiseg_x86_64, lldb_fioff_x86_64,
    lldb_fip_x86_64,    lldb_foseg_x86_64, lldb_fooff_x86_64,
    lldb_fdp_x86_64,    lldb_mxcsr_x86_64, lldb_mxcsrmask_x86_64,
    lldb_st0_x86_64,    lldb_st1_x86_64,   lldb_st2_x86_64,
    lldb_st3_x86_64,    lldb_st4_x86_64,   lldb_st5_x86_64,
    lldb_st6_x86_64,    lldb_st7_x86_64,   lldb_mm0_x86_64,
    lldb_mm1_x86_64,    lldb_mm2_x86_64,   lldb_mm3_x86_64,
    lldb_mm4_x86_64,    lldb_mm5_x86_64,   lldb_mm6_x86_64,
    lldb_mm7_x86_64,    lldb_xmm0_x86_64,  lldb_xmm1_x86_64,
    lldb_xmm2_x86_64,   lldb_xmm3_x86_64,  lldb_xmm4_x86_64,
    lldb_xmm5_x86_64,   lldb_xmm6_x86_64,  lldb_xmm7_x86_64,
    lldb_xmm8_x86_64,   lldb_xmm9_x86_64,  lldb_xmm10_x86_64,
    lldb_xmm11_x86_64,  lldb_xmm12_x86_64, lldb_xmm13_x86_64,
    lldb_xmm14_x86_64,  lldb_xmm15_x86_64,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_lldb_regnums_x86_64) /
               sizeof(g_lldb_regnums_x86_64[0])) -
                      1 ==
                  k_num_fpr_registers_x86_64,
              "g_lldb_regnums_x86_64 has wrong number of register infos");

static const uint32_t g_avx_regnums_x86_64[] = {
    lldb_ymm0_x86_64,   lldb_ymm1_x86_64,  lldb_ymm2_x86_64,  lldb_ymm3_x86_64,
    lldb_ymm4_x86_64,   lldb_ymm5_x86_64,  lldb_ymm6_x86_64,  lldb_ymm7_x86_64,
    lldb_ymm8_x86_64,   lldb_ymm9_x86_64,  lldb_ymm10_x86_64, lldb_ymm11_x86_64,
    lldb_ymm12_x86_64,  lldb_ymm13_x86_64, lldb_ymm14_x86_64, lldb_ymm15_x86_64,
    LLDB_INVALID_REGNUM // Register sets must be terminated with
                        // LLDB_INVALID_REGNUM.
};
static_assert((sizeof(g_avx_regnums_x86_64) / sizeof(g_avx_regnums_x86_64[0])) -
                      1 ==
                  k_num_avx_registers_x86_64,
              "g_avx_regnums_x86_64 has wrong number of register infos");

uint32_t RegisterContextPOSIX_x86::g_contained_eax[] = {lldb_eax_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ebx[] = {lldb_ebx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ecx[] = {lldb_ecx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_edx[] = {lldb_edx_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_edi[] = {lldb_edi_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_esi[] = {lldb_esi_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_ebp[] = {lldb_ebp_i386,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_esp[] = {lldb_esp_i386,
                                                        LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_eax[] = {
    lldb_eax_i386, lldb_ax_i386, lldb_ah_i386, lldb_al_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ebx[] = {
    lldb_ebx_i386, lldb_bx_i386, lldb_bh_i386, lldb_bl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ecx[] = {
    lldb_ecx_i386, lldb_cx_i386, lldb_ch_i386, lldb_cl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_edx[] = {
    lldb_edx_i386, lldb_dx_i386, lldb_dh_i386, lldb_dl_i386,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_edi[] = {
    lldb_edi_i386, lldb_di_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_esi[] = {
    lldb_esi_i386, lldb_si_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_ebp[] = {
    lldb_ebp_i386, lldb_bp_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_esp[] = {
    lldb_esp_i386, lldb_sp_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_contained_rax[] = {lldb_rax_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rbx[] = {lldb_rbx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rcx[] = {lldb_rcx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rdx[] = {lldb_rdx_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rdi[] = {lldb_rdi_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rsi[] = {lldb_rsi_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rbp[] = {lldb_rbp_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_rsp[] = {lldb_rsp_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r8[] = {lldb_r8_x86_64,
                                                       LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r9[] = {lldb_r9_x86_64,
                                                       LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r10[] = {lldb_r10_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r11[] = {lldb_r11_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r12[] = {lldb_r12_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r13[] = {lldb_r13_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r14[] = {lldb_r14_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_r15[] = {lldb_r15_x86_64,
                                                        LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_rax[] = {
    lldb_rax_x86_64, lldb_eax_x86_64, lldb_ax_x86_64,
    lldb_ah_x86_64,  lldb_al_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rbx[] = {
    lldb_rbx_x86_64, lldb_ebx_x86_64, lldb_bx_x86_64,
    lldb_bh_x86_64,  lldb_bl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rcx[] = {
    lldb_rcx_x86_64, lldb_ecx_x86_64, lldb_cx_x86_64,
    lldb_ch_x86_64,  lldb_cl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rdx[] = {
    lldb_rdx_x86_64, lldb_edx_x86_64, lldb_dx_x86_64,
    lldb_dh_x86_64,  lldb_dl_x86_64,  LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rdi[] = {
    lldb_rdi_x86_64, lldb_edi_x86_64, lldb_di_x86_64, lldb_dil_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rsi[] = {
    lldb_rsi_x86_64, lldb_esi_x86_64, lldb_si_x86_64, lldb_sil_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rbp[] = {
    lldb_rbp_x86_64, lldb_ebp_x86_64, lldb_bp_x86_64, lldb_bpl_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_rsp[] = {
    lldb_rsp_x86_64, lldb_esp_x86_64, lldb_sp_x86_64, lldb_spl_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r8[] = {
    lldb_r8_x86_64, lldb_r8d_x86_64, lldb_r8w_x86_64, lldb_r8l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r9[] = {
    lldb_r9_x86_64, lldb_r9d_x86_64, lldb_r9w_x86_64, lldb_r9l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r10[] = {
    lldb_r10_x86_64, lldb_r10d_x86_64, lldb_r10w_x86_64, lldb_r10l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r11[] = {
    lldb_r11_x86_64, lldb_r11d_x86_64, lldb_r11w_x86_64, lldb_r11l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r12[] = {
    lldb_r12_x86_64, lldb_r12d_x86_64, lldb_r12w_x86_64, lldb_r12l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r13[] = {
    lldb_r13_x86_64, lldb_r13d_x86_64, lldb_r13w_x86_64, lldb_r13l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r14[] = {
    lldb_r14_x86_64, lldb_r14d_x86_64, lldb_r14w_x86_64, lldb_r14l_x86_64,
    LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_r15[] = {
    lldb_r15_x86_64, lldb_r15d_x86_64, lldb_r15w_x86_64, lldb_r15l_x86_64,
    LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_contained_fip[] = {lldb_fip_x86_64,
                                                        LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_fdp[] = {lldb_fdp_x86_64,
                                                        LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_fip[] = {
    lldb_fip_x86_64, lldb_fioff_x86_64, lldb_fiseg_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_fdp[] = {
    lldb_fdp_x86_64, lldb_fooff_x86_64, lldb_foseg_x86_64, LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_contained_st0_32[] = {lldb_st0_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st1_32[] = {lldb_st1_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st2_32[] = {lldb_st2_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st3_32[] = {lldb_st3_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st4_32[] = {lldb_st4_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st5_32[] = {lldb_st5_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st6_32[] = {lldb_st6_i386,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st7_32[] = {lldb_st7_i386,
                                                           LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_st0_32[] = {
    lldb_st0_i386, lldb_mm0_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st1_32[] = {
    lldb_st1_i386, lldb_mm1_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st2_32[] = {
    lldb_st2_i386, lldb_mm2_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st3_32[] = {
    lldb_st3_i386, lldb_mm3_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st4_32[] = {
    lldb_st4_i386, lldb_mm4_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st5_32[] = {
    lldb_st5_i386, lldb_mm5_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st6_32[] = {
    lldb_st6_i386, lldb_mm6_i386, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st7_32[] = {
    lldb_st7_i386, lldb_mm7_i386, LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_contained_st0_64[] = {lldb_st0_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st1_64[] = {lldb_st1_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st2_64[] = {lldb_st2_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st3_64[] = {lldb_st3_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st4_64[] = {lldb_st4_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st5_64[] = {lldb_st5_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st6_64[] = {lldb_st6_x86_64,
                                                           LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_contained_st7_64[] = {lldb_st7_x86_64,
                                                           LLDB_INVALID_REGNUM};

uint32_t RegisterContextPOSIX_x86::g_invalidate_st0_64[] = {
    lldb_st0_x86_64, lldb_mm0_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st1_64[] = {
    lldb_st1_x86_64, lldb_mm1_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st2_64[] = {
    lldb_st2_x86_64, lldb_mm2_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st3_64[] = {
    lldb_st3_x86_64, lldb_mm3_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st4_64[] = {
    lldb_st4_x86_64, lldb_mm4_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st5_64[] = {
    lldb_st5_x86_64, lldb_mm5_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st6_64[] = {
    lldb_st6_x86_64, lldb_mm6_x86_64, LLDB_INVALID_REGNUM};
uint32_t RegisterContextPOSIX_x86::g_invalidate_st7_64[] = {
    lldb_st7_x86_64, lldb_mm7_x86_64, LLDB_INVALID_REGNUM};

// Number of register sets provided by this context.
enum { k_num_extended_register_sets = 1, k_num_register_sets = 3 };

static const RegisterSet g_reg_sets_i386[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_i386,
     g_gpr_regnums_i386},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_i386,
     g_lldb_regnums_i386},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_i386,
     g_avx_regnums_i386}};

static const RegisterSet g_reg_sets_x86_64[k_num_register_sets] = {
    {"General Purpose Registers", "gpr", k_num_gpr_registers_x86_64,
     g_gpr_regnums_x86_64},
    {"Floating Point Registers", "fpu", k_num_fpr_registers_x86_64,
     g_lldb_regnums_x86_64},
    {"Advanced Vector Extensions", "avx", k_num_avx_registers_x86_64,
     g_avx_regnums_x86_64}};

bool RegisterContextPOSIX_x86::IsGPR(unsigned reg) {
  return reg <= GetRegInfo().last_gpr; // GPR's come first.
}

bool RegisterContextPOSIX_x86::IsFPR(unsigned reg) {
  return (GetRegInfo().first_fpr <= reg && reg <= GetRegInfo().last_fpr);
}

bool RegisterContextPOSIX_x86::IsAVX(unsigned reg) {
  return (GetRegInfo().first_ymm <= reg && reg <= GetRegInfo().last_ymm);
}

bool RegisterContextPOSIX_x86::IsFPR(unsigned reg, FPRType fpr_type) {
  bool generic_fpr = IsFPR(reg);

  if (fpr_type == eXSAVE)
    return generic_fpr || IsAVX(reg);
  return generic_fpr;
}

RegisterContextPOSIX_x86::RegisterContextPOSIX_x86(
    Thread &thread, uint32_t concrete_frame_idx,
    RegisterInfoInterface *register_info)
    : RegisterContext(thread, concrete_frame_idx) {
  m_register_info_up.reset(register_info);

  ::memset(&m_fpr, 0, sizeof(FPR));
  ::memset(&m_ymm_set, 0, sizeof(YMM));

  m_fpr_type = eNotValid;
}

RegisterContextPOSIX_x86::~RegisterContextPOSIX_x86() = default;

RegisterContextPOSIX_x86::FPRType RegisterContextPOSIX_x86::GetFPRType() {
  if (m_fpr_type == eNotValid) {
    // TODO: Use assembly to call cpuid on the inferior and query ebx or ecx
    m_fpr_type = eXSAVE; // extended floating-point registers, if available
    if (!ReadFPR())
      m_fpr_type = eFXSAVE; // assume generic floating-point registers
  }
  return m_fpr_type;
}

void RegisterContextPOSIX_x86::Invalidate() {}

void RegisterContextPOSIX_x86::InvalidateAllRegisters() {}

unsigned RegisterContextPOSIX_x86::GetRegisterOffset(unsigned reg) {
  assert(reg < GetRegInfo().num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_offset;
}

RegInfo &RegisterContextPOSIX_x86::GetRegInfo() {
  return GetRegInfoShared(
      m_register_info_up->GetTargetArchitecture().GetMachine(),
      /*with_base=*/false);
}

unsigned RegisterContextPOSIX_x86::GetRegisterSize(unsigned reg) {
  assert(reg < GetRegInfo().num_registers && "Invalid register number.");
  return GetRegisterInfo()[reg].byte_size;
}

size_t RegisterContextPOSIX_x86::GetRegisterCount() {
  size_t num_registers =
      GetRegInfo().num_gpr_registers + GetRegInfo().num_fpr_registers;
  if (GetFPRType() == eXSAVE)
    return num_registers + GetRegInfo().num_avx_registers;
  return num_registers;
}

size_t RegisterContextPOSIX_x86::GetGPRSize() {
  return m_register_info_up->GetGPRSize();
}

size_t RegisterContextPOSIX_x86::GetFXSAVEOffset() {
  return GetRegisterInfo()[GetRegInfo().first_fpr].byte_offset;
}

const RegisterInfo *RegisterContextPOSIX_x86::GetRegisterInfo() {
  // Commonly, this method is overridden and g_register_infos is copied and
  // specialized. So, use GetRegisterInfo() rather than g_register_infos in
  // this scope.
  return m_register_info_up->GetRegisterInfo();
}

const RegisterInfo *
RegisterContextPOSIX_x86::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < GetRegInfo().num_registers)
    return &GetRegisterInfo()[reg];
  else
    return nullptr;
}

size_t RegisterContextPOSIX_x86::GetRegisterSetCount() {
  size_t sets = 0;
  for (size_t set = 0; set < k_num_register_sets; ++set) {
    if (IsRegisterSetAvailable(set))
      ++sets;
  }

  return sets;
}

const RegisterSet *RegisterContextPOSIX_x86::GetRegisterSet(size_t set) {
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

const char *RegisterContextPOSIX_x86::GetRegisterName(unsigned reg) {
  assert(reg < GetRegInfo().num_registers && "Invalid register offset.");
  return GetRegisterInfo()[reg].name;
}

// Parse ymm registers and into xmm.bytes and ymmh.bytes.
bool RegisterContextPOSIX_x86::CopyYMMtoXSTATE(uint32_t reg,
                                               lldb::ByteOrder byte_order) {
  if (!IsAVX(reg))
    return false;

  if (byte_order == eByteOrderLittle) {
    uint32_t reg_no = reg - GetRegInfo().first_ymm;
    YMMToXState(m_ymm_set.ymm[reg_no], m_fpr.fxsave.xmm[reg_no].bytes,
                m_fpr.xsave.ymmh[reg_no].bytes);
    return true;
  }

  return false; // unsupported or invalid byte order
}

// Concatenate xmm.bytes with ymmh.bytes
bool RegisterContextPOSIX_x86::CopyXSTATEtoYMM(uint32_t reg,
                                               lldb::ByteOrder byte_order) {
  if (!IsAVX(reg))
    return false;

  if (byte_order == eByteOrderLittle) {
    uint32_t reg_no = reg - GetRegInfo().first_ymm;
    m_ymm_set.ymm[reg_no] = XStateToYMM(m_fpr.fxsave.xmm[reg_no].bytes,
                                        m_fpr.xsave.ymmh[reg_no].bytes);
    return true;
  }

  return false; // unsupported or invalid byte order
}

bool RegisterContextPOSIX_x86::IsRegisterSetAvailable(size_t set_index) {
  // Note: Extended register sets are assumed to be at the end of g_reg_sets...
  size_t num_sets = k_num_register_sets - k_num_extended_register_sets;

  if (GetFPRType() == eXSAVE) // ...and to start with AVX registers.
    ++num_sets;
  return (set_index < num_sets);
}
