//===-- RegisterContextWindows_x64.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#if defined(__x86_64__) || defined(_M_X64)

#include "lldb/Host/windows/HostThreadWindows.h"
#include "lldb/Host/windows/windows.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Status.h"
#include "lldb/lldb-private-types.h"

#include "RegisterContextWindows_x64.h"
#include "Plugins/Process/Utility/RegisterContext_x86.h"
#include "TargetThreadWindows.h"
#include "Plugins/Process/Utility/lldb-x86-register-enums.h"

#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;

#define DEFINE_GPR(reg, alt, generic)                                          \
{                                                                              \
  #reg, alt, 8, 0, eEncodingUint, eFormatHexUppercase,                         \
      {dwarf_##reg##_x86_64, dwarf_##reg##_x86_64, generic,                    \
        LLDB_INVALID_REGNUM, lldb_##reg##_x86_64 },                            \
        nullptr, nullptr, nullptr,                                             \
}

#define DEFINE_GPR_BIN(reg, alt) #reg, alt, 8, 0, eEncodingUint, eFormatBinary
#define DEFINE_FPU_XMM(reg)                                                    \
  #reg, NULL, 16, 0, eEncodingUint, eFormatVectorOfUInt64,                     \
  {dwarf_##reg##_x86_64, dwarf_##reg##_x86_64, LLDB_INVALID_REGNUM,            \
   LLDB_INVALID_REGNUM, lldb_##reg##_x86_64},                                  \
  nullptr, nullptr, nullptr,

#define DEFINE_GPR_PSEUDO_32(reg)                                              \
{                                                                              \
  #reg, nullptr, 4, 0, eEncodingUint, eFormatHexUppercase,                     \
      {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,          \
        LLDB_INVALID_REGNUM, lldb_##reg##_x86_64 },                            \
        nullptr, nullptr, nullptr,                                             \
}

#define DEFINE_GPR_PSEUDO_16(reg)                                              \
{                                                                              \
  #reg, nullptr, 2, 0, eEncodingUint, eFormatHexUppercase,                     \
      {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,          \
        LLDB_INVALID_REGNUM, lldb_##reg##_x86_64 },                            \
        nullptr, nullptr, nullptr,                                             \
}

#define DEFINE_GPR_PSEUDO_8(reg)                                               \
{                                                                              \
  #reg, nullptr, 1, 0, eEncodingUint, eFormatHexUppercase,                     \
      {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,          \
        LLDB_INVALID_REGNUM, lldb_##reg##_x86_64 },                            \
        nullptr, nullptr, nullptr,                                             \
}

namespace {

// This enum defines the layout of the global RegisterInfo array.  This is
// necessary because lldb register sets are defined in terms of indices into
// the register array. As such, the order of RegisterInfos defined in global
// registers array must match the order defined here. When defining the
// register set layouts, these values can appear in an arbitrary order, and
// that determines the order that register values are displayed in a dump.
enum RegisterIndex {
  eRegisterIndexRax,
  eRegisterIndexRbx,
  eRegisterIndexRcx,
  eRegisterIndexRdx,
  eRegisterIndexRdi,
  eRegisterIndexRsi,
  eRegisterIndexRbp,
  eRegisterIndexRsp,
  eRegisterIndexR8,
  eRegisterIndexR9,
  eRegisterIndexR10,
  eRegisterIndexR11,
  eRegisterIndexR12,
  eRegisterIndexR13,
  eRegisterIndexR14,
  eRegisterIndexR15,
  eRegisterIndexRip,
  eRegisterIndexRflags,
  eRegisterIndexEax,
  eRegisterIndexEbx,
  eRegisterIndexEcx,
  eRegisterIndexEdx,
  eRegisterIndexEdi,
  eRegisterIndexEsi,
  eRegisterIndexEbp,
  eRegisterIndexEsp,
  eRegisterIndexR8d,
  eRegisterIndexR9d,
  eRegisterIndexR10d,
  eRegisterIndexR11d,
  eRegisterIndexR12d,
  eRegisterIndexR13d,
  eRegisterIndexR14d,
  eRegisterIndexR15d,
  eRegisterIndexAx,
  eRegisterIndexBx,
  eRegisterIndexCx,
  eRegisterIndexDx,
  eRegisterIndexDi,
  eRegisterIndexSi,
  eRegisterIndexBp,
  eRegisterIndexSp,
  eRegisterIndexR8w,
  eRegisterIndexR9w,
  eRegisterIndexR10w,
  eRegisterIndexR11w,
  eRegisterIndexR12w,
  eRegisterIndexR13w,
  eRegisterIndexR14w,
  eRegisterIndexR15w,
  eRegisterIndexAh,
  eRegisterIndexBh,
  eRegisterIndexCh,
  eRegisterIndexDh,
  eRegisterIndexAl,
  eRegisterIndexBl,
  eRegisterIndexCl,
  eRegisterIndexDl,
  eRegisterIndexDil,
  eRegisterIndexSil,
  eRegisterIndexBpl,
  eRegisterIndexSpl,
  eRegisterIndexR8l,
  eRegisterIndexR9l,
  eRegisterIndexR10l,
  eRegisterIndexR11l,
  eRegisterIndexR12l,
  eRegisterIndexR13l,
  eRegisterIndexR14l,
  eRegisterIndexR15l,

  eRegisterIndexXmm0,
  eRegisterIndexXmm1,
  eRegisterIndexXmm2,
  eRegisterIndexXmm3,
  eRegisterIndexXmm4,
  eRegisterIndexXmm5,
  eRegisterIndexXmm6,
  eRegisterIndexXmm7,
  eRegisterIndexXmm8,
  eRegisterIndexXmm9,
  eRegisterIndexXmm10,
  eRegisterIndexXmm11,
  eRegisterIndexXmm12,
  eRegisterIndexXmm13,
  eRegisterIndexXmm14,
  eRegisterIndexXmm15
};

// Array of all register information supported by Windows x86
RegisterInfo g_register_infos[] = {
    //  Macro auto defines most stuff     eh_frame                  DWARF
    //  GENERIC
    //  GDB                  LLDB                  VALUE REGS    INVALIDATE REGS
    //  ================================  =========================
    //  ======================  =========================
    //  ===================  =================     ==========    ===============
    DEFINE_GPR(rax, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbx, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(rcx, nullptr, LLDB_REGNUM_GENERIC_ARG1),
    DEFINE_GPR(rdx, nullptr, LLDB_REGNUM_GENERIC_ARG2),
    DEFINE_GPR(rdi, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(rsi, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbp, "fp", LLDB_REGNUM_GENERIC_FP),
    DEFINE_GPR(rsp, "sp", LLDB_REGNUM_GENERIC_SP),
    DEFINE_GPR(r8, nullptr, LLDB_REGNUM_GENERIC_ARG3),
    DEFINE_GPR(r9, nullptr, LLDB_REGNUM_GENERIC_ARG4),
    DEFINE_GPR(r10, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15, nullptr, LLDB_INVALID_REGNUM),
    DEFINE_GPR(rip, "pc", LLDB_REGNUM_GENERIC_PC),
    {
        DEFINE_GPR_BIN(eflags, "flags"),
        {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_REGNUM_GENERIC_FLAGS,
         LLDB_INVALID_REGNUM, lldb_rflags_x86_64},
        nullptr,
        nullptr,
        nullptr,
    },
    DEFINE_GPR_PSEUDO_32(eax),
    DEFINE_GPR_PSEUDO_32(ebx),
    DEFINE_GPR_PSEUDO_32(ecx),
    DEFINE_GPR_PSEUDO_32(edx),
    DEFINE_GPR_PSEUDO_32(edi),
    DEFINE_GPR_PSEUDO_32(esi),
    DEFINE_GPR_PSEUDO_32(ebp),
    DEFINE_GPR_PSEUDO_32(esp),
    DEFINE_GPR_PSEUDO_32(r8d),
    DEFINE_GPR_PSEUDO_32(r9d),
    DEFINE_GPR_PSEUDO_32(r10d),
    DEFINE_GPR_PSEUDO_32(r11d),
    DEFINE_GPR_PSEUDO_32(r12d),
    DEFINE_GPR_PSEUDO_32(r13d),
    DEFINE_GPR_PSEUDO_32(r14d),
    DEFINE_GPR_PSEUDO_32(r15d),
    DEFINE_GPR_PSEUDO_16(ax),
    DEFINE_GPR_PSEUDO_16(bx),
    DEFINE_GPR_PSEUDO_16(cx),
    DEFINE_GPR_PSEUDO_16(dx),
    DEFINE_GPR_PSEUDO_16(di),
    DEFINE_GPR_PSEUDO_16(si),
    DEFINE_GPR_PSEUDO_16(bp),
    DEFINE_GPR_PSEUDO_16(sp),
    DEFINE_GPR_PSEUDO_16(r8w),
    DEFINE_GPR_PSEUDO_16(r9w),
    DEFINE_GPR_PSEUDO_16(r10w),
    DEFINE_GPR_PSEUDO_16(r11w),
    DEFINE_GPR_PSEUDO_16(r12w),
    DEFINE_GPR_PSEUDO_16(r13w),
    DEFINE_GPR_PSEUDO_16(r14w),
    DEFINE_GPR_PSEUDO_16(r15w),
    DEFINE_GPR_PSEUDO_8(ah),
    DEFINE_GPR_PSEUDO_8(bh),
    DEFINE_GPR_PSEUDO_8(ch),
    DEFINE_GPR_PSEUDO_8(dh),
    DEFINE_GPR_PSEUDO_8(al),
    DEFINE_GPR_PSEUDO_8(bl),
    DEFINE_GPR_PSEUDO_8(cl),
    DEFINE_GPR_PSEUDO_8(dl),
    DEFINE_GPR_PSEUDO_8(dil),
    DEFINE_GPR_PSEUDO_8(sil),
    DEFINE_GPR_PSEUDO_8(bpl),
    DEFINE_GPR_PSEUDO_8(spl),
    DEFINE_GPR_PSEUDO_8(r8l),
    DEFINE_GPR_PSEUDO_8(r9l),
    DEFINE_GPR_PSEUDO_8(r10l),
    DEFINE_GPR_PSEUDO_8(r11l),
    DEFINE_GPR_PSEUDO_8(r12l),
    DEFINE_GPR_PSEUDO_8(r13l),
    DEFINE_GPR_PSEUDO_8(r14l),
    DEFINE_GPR_PSEUDO_8(r15l),
    {DEFINE_FPU_XMM(xmm0)},
    {DEFINE_FPU_XMM(xmm1)},
    {DEFINE_FPU_XMM(xmm2)},
    {DEFINE_FPU_XMM(xmm3)},
    {DEFINE_FPU_XMM(xmm4)},
    {DEFINE_FPU_XMM(xmm5)},
    {DEFINE_FPU_XMM(xmm6)},
    {DEFINE_FPU_XMM(xmm7)},
    {DEFINE_FPU_XMM(xmm8)},
    {DEFINE_FPU_XMM(xmm9)},
    {DEFINE_FPU_XMM(xmm10)},
    {DEFINE_FPU_XMM(xmm11)},
    {DEFINE_FPU_XMM(xmm12)},
    {DEFINE_FPU_XMM(xmm13)},
    {DEFINE_FPU_XMM(xmm14)},
    {DEFINE_FPU_XMM(xmm15)}};

static size_t k_num_register_infos = std::size(g_register_infos);

// Array of lldb register numbers used to define the set of all General Purpose
// Registers
uint32_t g_gpr_reg_indices[] = {
    eRegisterIndexRax,  eRegisterIndexRbx,  eRegisterIndexRcx,
    eRegisterIndexRdx,  eRegisterIndexRdi,  eRegisterIndexRsi,
    eRegisterIndexRbp,  eRegisterIndexRsp,  eRegisterIndexR8,
    eRegisterIndexR9,   eRegisterIndexR10,  eRegisterIndexR11,
    eRegisterIndexR12,  eRegisterIndexR13,  eRegisterIndexR14,
    eRegisterIndexR15,  eRegisterIndexRip,  eRegisterIndexRflags,
    eRegisterIndexEax,  eRegisterIndexEbx,  eRegisterIndexEcx,
    eRegisterIndexEdx,  eRegisterIndexEdi,  eRegisterIndexEsi,
    eRegisterIndexEbp,  eRegisterIndexEsp,  eRegisterIndexR8d,
    eRegisterIndexR9d,  eRegisterIndexR10d, eRegisterIndexR11d,
    eRegisterIndexR12d, eRegisterIndexR13d, eRegisterIndexR14d,
    eRegisterIndexR15d, eRegisterIndexAx,   eRegisterIndexBx,
    eRegisterIndexCx,   eRegisterIndexDx,   eRegisterIndexDi,
    eRegisterIndexSi,   eRegisterIndexBp,   eRegisterIndexSp,
    eRegisterIndexR8w,  eRegisterIndexR9w,  eRegisterIndexR10w,
    eRegisterIndexR11w, eRegisterIndexR12w, eRegisterIndexR13w,
    eRegisterIndexR14w, eRegisterIndexR15w, eRegisterIndexAh,
    eRegisterIndexBh,   eRegisterIndexCh,   eRegisterIndexDh,
    eRegisterIndexAl,   eRegisterIndexBl,   eRegisterIndexCl,
    eRegisterIndexDl,   eRegisterIndexDil,  eRegisterIndexSil,
    eRegisterIndexBpl,  eRegisterIndexSpl,  eRegisterIndexR8l,
    eRegisterIndexR9l,  eRegisterIndexR10l, eRegisterIndexR11l,
    eRegisterIndexR12l, eRegisterIndexR13l, eRegisterIndexR14l,
    eRegisterIndexR15l
};

uint32_t g_fpu_reg_indices[] = {
    eRegisterIndexXmm0,  eRegisterIndexXmm1,  eRegisterIndexXmm2,
    eRegisterIndexXmm3,  eRegisterIndexXmm4,  eRegisterIndexXmm5,
    eRegisterIndexXmm6,  eRegisterIndexXmm7,  eRegisterIndexXmm8,
    eRegisterIndexXmm9,  eRegisterIndexXmm10, eRegisterIndexXmm11,
    eRegisterIndexXmm12, eRegisterIndexXmm13, eRegisterIndexXmm14,
    eRegisterIndexXmm15
};

RegisterSet g_register_sets[] = {
    {"General Purpose Registers", "gpr", std::size(g_gpr_reg_indices),
     g_gpr_reg_indices},
    {"Floating Point Registers", "fpu", std::size(g_fpu_reg_indices),
     g_fpu_reg_indices}};
}

// Constructors and Destructors
RegisterContextWindows_x64::RegisterContextWindows_x64(
    Thread &thread, uint32_t concrete_frame_idx)
    : RegisterContextWindows(thread, concrete_frame_idx) {}

RegisterContextWindows_x64::~RegisterContextWindows_x64() {}

size_t RegisterContextWindows_x64::GetRegisterCount() {
  return std::size(g_register_infos);
}

const RegisterInfo *
RegisterContextWindows_x64::GetRegisterInfoAtIndex(size_t reg) {
  if (reg < k_num_register_infos)
    return &g_register_infos[reg];
  return NULL;
}

size_t RegisterContextWindows_x64::GetRegisterSetCount() {
  return std::size(g_register_sets);
}

const RegisterSet *RegisterContextWindows_x64::GetRegisterSet(size_t reg_set) {
  return &g_register_sets[reg_set];
}

bool RegisterContextWindows_x64::ReadRegister(const RegisterInfo *reg_info,
                                              RegisterValue &reg_value) {
  if (!CacheAllRegisterValues())
    return false;

  if (reg_info == nullptr)
    return false;

  const uint32_t reg = reg_info->kinds[eRegisterKindLLDB];

  switch (reg) {
#define GPR_CASE(size, reg_case, reg_val)                                      \
  case reg_case:                                                               \
    reg_value.SetUInt##size(reg_val);                                        \
    break;

  GPR_CASE(64, lldb_rax_x86_64, m_context.Rax);
  GPR_CASE(64, lldb_rbx_x86_64, m_context.Rbx);
  GPR_CASE(64, lldb_rcx_x86_64, m_context.Rcx);
  GPR_CASE(64, lldb_rdx_x86_64, m_context.Rdx);
  GPR_CASE(64, lldb_rdi_x86_64, m_context.Rdi);
  GPR_CASE(64, lldb_rsi_x86_64, m_context.Rsi);
  GPR_CASE(64, lldb_r8_x86_64, m_context.R8);
  GPR_CASE(64, lldb_r9_x86_64, m_context.R9);
  GPR_CASE(64, lldb_r10_x86_64, m_context.R10);
  GPR_CASE(64, lldb_r11_x86_64, m_context.R11);
  GPR_CASE(64, lldb_r12_x86_64, m_context.R12);
  GPR_CASE(64, lldb_r13_x86_64, m_context.R13);
  GPR_CASE(64, lldb_r14_x86_64, m_context.R14);
  GPR_CASE(64, lldb_r15_x86_64, m_context.R15);
  GPR_CASE(64, lldb_rbp_x86_64, m_context.Rbp);
  GPR_CASE(64, lldb_rsp_x86_64, m_context.Rsp);
  GPR_CASE(64, lldb_rip_x86_64, m_context.Rip);
  GPR_CASE(64, lldb_rflags_x86_64, m_context.EFlags);
  GPR_CASE(32, lldb_eax_x86_64, static_cast<uint32_t>(m_context.Rax));
  GPR_CASE(32, lldb_ebx_x86_64, static_cast<uint32_t>(m_context.Rbx));
  GPR_CASE(32, lldb_ecx_x86_64, static_cast<uint32_t>(m_context.Rcx));
  GPR_CASE(32, lldb_edx_x86_64, static_cast<uint32_t>(m_context.Rdx));
  GPR_CASE(32, lldb_edi_x86_64, static_cast<uint32_t>(m_context.Rdi));
  GPR_CASE(32, lldb_esi_x86_64, static_cast<uint32_t>(m_context.Rsi));
  GPR_CASE(32, lldb_ebp_x86_64, static_cast<uint32_t>(m_context.Rbp));
  GPR_CASE(32, lldb_esp_x86_64, static_cast<uint32_t>(m_context.Rsp));
  GPR_CASE(32, lldb_r8d_x86_64, static_cast<uint32_t>(m_context.R8));
  GPR_CASE(32, lldb_r9d_x86_64, static_cast<uint32_t>(m_context.R9));
  GPR_CASE(32, lldb_r10d_x86_64, static_cast<uint32_t>(m_context.R10));
  GPR_CASE(32, lldb_r11d_x86_64, static_cast<uint32_t>(m_context.R11));
  GPR_CASE(32, lldb_r12d_x86_64, static_cast<uint32_t>(m_context.R12));
  GPR_CASE(32, lldb_r13d_x86_64, static_cast<uint32_t>(m_context.R13));
  GPR_CASE(32, lldb_r14d_x86_64, static_cast<uint32_t>(m_context.R14));
  GPR_CASE(32, lldb_r15d_x86_64, static_cast<uint32_t>(m_context.R15));
  GPR_CASE(16, lldb_ax_x86_64, static_cast<uint16_t>(m_context.Rax));
  GPR_CASE(16, lldb_bx_x86_64, static_cast<uint16_t>(m_context.Rbx));
  GPR_CASE(16, lldb_cx_x86_64, static_cast<uint16_t>(m_context.Rcx));
  GPR_CASE(16, lldb_dx_x86_64, static_cast<uint16_t>(m_context.Rdx));
  GPR_CASE(16, lldb_di_x86_64, static_cast<uint16_t>(m_context.Rdi));
  GPR_CASE(16, lldb_si_x86_64, static_cast<uint16_t>(m_context.Rsi));
  GPR_CASE(16, lldb_bp_x86_64, static_cast<uint16_t>(m_context.Rbp));
  GPR_CASE(16, lldb_sp_x86_64, static_cast<uint16_t>(m_context.Rsp));
  GPR_CASE(16, lldb_r8w_x86_64, static_cast<uint16_t>(m_context.R8));
  GPR_CASE(16, lldb_r9w_x86_64, static_cast<uint16_t>(m_context.R9));
  GPR_CASE(16, lldb_r10w_x86_64, static_cast<uint16_t>(m_context.R10));
  GPR_CASE(16, lldb_r11w_x86_64, static_cast<uint16_t>(m_context.R11));
  GPR_CASE(16, lldb_r12w_x86_64, static_cast<uint16_t>(m_context.R12));
  GPR_CASE(16, lldb_r13w_x86_64, static_cast<uint16_t>(m_context.R13));
  GPR_CASE(16, lldb_r14w_x86_64, static_cast<uint16_t>(m_context.R14));
  GPR_CASE(16, lldb_r15w_x86_64, static_cast<uint16_t>(m_context.R15));
  GPR_CASE(8, lldb_ah_x86_64, static_cast<uint16_t>(m_context.Rax) >> 8);
  GPR_CASE(8, lldb_bh_x86_64, static_cast<uint16_t>(m_context.Rbx) >> 8);
  GPR_CASE(8, lldb_ch_x86_64, static_cast<uint16_t>(m_context.Rcx) >> 8);
  GPR_CASE(8, lldb_dh_x86_64, static_cast<uint16_t>(m_context.Rdx) >> 8);
  GPR_CASE(8, lldb_al_x86_64, static_cast<uint8_t>(m_context.Rax));
  GPR_CASE(8, lldb_bl_x86_64, static_cast<uint8_t>(m_context.Rbx));
  GPR_CASE(8, lldb_cl_x86_64, static_cast<uint8_t>(m_context.Rcx));
  GPR_CASE(8, lldb_dl_x86_64, static_cast<uint8_t>(m_context.Rdx));
  GPR_CASE(8, lldb_dil_x86_64, static_cast<uint8_t>(m_context.Rdi));
  GPR_CASE(8, lldb_sil_x86_64, static_cast<uint8_t>(m_context.Rsi));
  GPR_CASE(8, lldb_bpl_x86_64, static_cast<uint8_t>(m_context.Rbp));
  GPR_CASE(8, lldb_spl_x86_64, static_cast<uint8_t>(m_context.Rsp));
  GPR_CASE(8, lldb_r8l_x86_64, static_cast<uint8_t>(m_context.R8));
  GPR_CASE(8, lldb_r9l_x86_64, static_cast<uint8_t>(m_context.R9));
  GPR_CASE(8, lldb_r10l_x86_64, static_cast<uint8_t>(m_context.R10));
  GPR_CASE(8, lldb_r11l_x86_64, static_cast<uint8_t>(m_context.R11));
  GPR_CASE(8, lldb_r12l_x86_64, static_cast<uint8_t>(m_context.R12));
  GPR_CASE(8, lldb_r13l_x86_64, static_cast<uint8_t>(m_context.R13));
  GPR_CASE(8, lldb_r14l_x86_64, static_cast<uint8_t>(m_context.R14));
  GPR_CASE(8, lldb_r15l_x86_64, static_cast<uint8_t>(m_context.R15));

  case lldb_xmm0_x86_64:
    reg_value.SetBytes(&m_context.Xmm0,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm1_x86_64:
    reg_value.SetBytes(&m_context.Xmm1,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm2_x86_64:
    reg_value.SetBytes(&m_context.Xmm2,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm3_x86_64:
    reg_value.SetBytes(&m_context.Xmm3,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm4_x86_64:
    reg_value.SetBytes(&m_context.Xmm4,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm5_x86_64:
    reg_value.SetBytes(&m_context.Xmm5,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm6_x86_64:
    reg_value.SetBytes(&m_context.Xmm6,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm7_x86_64:
    reg_value.SetBytes(&m_context.Xmm7,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm8_x86_64:
    reg_value.SetBytes(&m_context.Xmm8,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm9_x86_64:
    reg_value.SetBytes(&m_context.Xmm9,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm10_x86_64:
    reg_value.SetBytes(&m_context.Xmm10,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm11_x86_64:
    reg_value.SetBytes(&m_context.Xmm11,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm12_x86_64:
    reg_value.SetBytes(&m_context.Xmm12,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm13_x86_64:
    reg_value.SetBytes(&m_context.Xmm13,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm14_x86_64:
    reg_value.SetBytes(&m_context.Xmm14,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  case lldb_xmm15_x86_64:
    reg_value.SetBytes(&m_context.Xmm15,
                       reg_info->byte_size, endian::InlHostByteOrder());
    break;
  }
  return true;
}

bool RegisterContextWindows_x64::WriteRegister(const RegisterInfo *reg_info,
                                               const RegisterValue &reg_value) {
  // Since we cannot only write a single register value to the inferior, we
  // need to make sure our cached copy of the register values are fresh.
  // Otherwise when writing EAX, for example, we may also overwrite some other
  // register with a stale value.
  if (!CacheAllRegisterValues())
    return false;

  switch (reg_info->kinds[eRegisterKindLLDB]) {
  case lldb_rax_x86_64:
    m_context.Rax = reg_value.GetAsUInt64();
    break;
  case lldb_rbx_x86_64:
    m_context.Rbx = reg_value.GetAsUInt64();
    break;
  case lldb_rcx_x86_64:
    m_context.Rcx = reg_value.GetAsUInt64();
    break;
  case lldb_rdx_x86_64:
    m_context.Rdx = reg_value.GetAsUInt64();
    break;
  case lldb_rdi_x86_64:
    m_context.Rdi = reg_value.GetAsUInt64();
    break;
  case lldb_rsi_x86_64:
    m_context.Rsi = reg_value.GetAsUInt64();
    break;
  case lldb_r8_x86_64:
    m_context.R8 = reg_value.GetAsUInt64();
    break;
  case lldb_r9_x86_64:
    m_context.R9 = reg_value.GetAsUInt64();
    break;
  case lldb_r10_x86_64:
    m_context.R10 = reg_value.GetAsUInt64();
    break;
  case lldb_r11_x86_64:
    m_context.R11 = reg_value.GetAsUInt64();
    break;
  case lldb_r12_x86_64:
    m_context.R12 = reg_value.GetAsUInt64();
    break;
  case lldb_r13_x86_64:
    m_context.R13 = reg_value.GetAsUInt64();
    break;
  case lldb_r14_x86_64:
    m_context.R14 = reg_value.GetAsUInt64();
    break;
  case lldb_r15_x86_64:
    m_context.R15 = reg_value.GetAsUInt64();
    break;
  case lldb_rbp_x86_64:
    m_context.Rbp = reg_value.GetAsUInt64();
    break;
  case lldb_rsp_x86_64:
    m_context.Rsp = reg_value.GetAsUInt64();
    break;
  case lldb_rip_x86_64:
    m_context.Rip = reg_value.GetAsUInt64();
    break;
  case lldb_rflags_x86_64:
    m_context.EFlags = reg_value.GetAsUInt64();
    break;
  case lldb_xmm0_x86_64:
    memcpy(&m_context.Xmm0, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm1_x86_64:
    memcpy(&m_context.Xmm1, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm2_x86_64:
    memcpy(&m_context.Xmm2, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm3_x86_64:
    memcpy(&m_context.Xmm3, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm4_x86_64:
    memcpy(&m_context.Xmm4, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm5_x86_64:
    memcpy(&m_context.Xmm5, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm6_x86_64:
    memcpy(&m_context.Xmm6, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm7_x86_64:
    memcpy(&m_context.Xmm7, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm8_x86_64:
    memcpy(&m_context.Xmm8, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm9_x86_64:
    memcpy(&m_context.Xmm9, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm10_x86_64:
    memcpy(&m_context.Xmm10, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm11_x86_64:
    memcpy(&m_context.Xmm11, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm12_x86_64:
    memcpy(&m_context.Xmm12, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm13_x86_64:
    memcpy(&m_context.Xmm13, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm14_x86_64:
    memcpy(&m_context.Xmm14, reg_value.GetBytes(), 16);
    break;
  case lldb_xmm15_x86_64:
    memcpy(&m_context.Xmm15, reg_value.GetBytes(), 16);
    break;
  }

  // Physically update the registers in the target process.
  return ApplyAllRegisterValues();
}

#endif // defined(__x86_64__) || defined(_M_X64)
