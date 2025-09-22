//===-- RegisterContextWindows_x86_64.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextWindows_x86_64.h"
#include "RegisterContext_x86.h"
#include "lldb-x86-register-enums.h"

#include <vector>

using namespace lldb_private;
using namespace lldb;

namespace {
typedef struct _GPR {
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rbx;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
  uint16_t cs;
  uint16_t fs;
  uint16_t gs;
  uint16_t ss;
  uint16_t ds;
  uint16_t es;
} GPR;

#define GPR_OFFSET(regname) (LLVM_EXTENSION offsetof(GPR, regname))
#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
#reg, alt, sizeof(((GPR *)nullptr)->reg), GPR_OFFSET(reg), eEncodingUint,  \
        eFormatHex,                                                            \
        {kind1, kind2, kind3, kind4, lldb_##reg##_x86_64 }, nullptr, nullptr,  \
        nullptr,                                                               \
  }

typedef struct _FPReg {
  XMMReg xmm0;
  XMMReg xmm1;
  XMMReg xmm2;
  XMMReg xmm3;
  XMMReg xmm4;
  XMMReg xmm5;
  XMMReg xmm6;
  XMMReg xmm7;
  XMMReg xmm8;
  XMMReg xmm9;
  XMMReg xmm10;
  XMMReg xmm11;
  XMMReg xmm12;
  XMMReg xmm13;
  XMMReg xmm14;
  XMMReg xmm15;
} FPReg;

#define FPR_OFFSET(regname)                                                    \
  (sizeof(GPR) + LLVM_EXTENSION offsetof(FPReg, regname))

#define DEFINE_XMM(reg)                                                        \
  {                                                                            \
#reg, NULL, sizeof(((FPReg *)nullptr)->reg), FPR_OFFSET(reg),              \
        eEncodingUint, eFormatVectorOfUInt64,                                  \
        {dwarf_##reg##_x86_64, dwarf_##reg##_x86_64, LLDB_INVALID_REGNUM,      \
         LLDB_INVALID_REGNUM, lldb_##reg##_x86_64 },                           \
         nullptr, nullptr, nullptr,                                            \
  }

// clang-format off
static RegisterInfo g_register_infos_x86_64[] = {
// General purpose registers     EH_Frame              DWARF                 Generic                     Process Plugin
//  ===========================  ==================    ================      =========================   ====================
    DEFINE_GPR(rax,    nullptr,  dwarf_rax_x86_64,     dwarf_rax_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbx,    nullptr,  dwarf_rbx_x86_64,     dwarf_rbx_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rcx,    nullptr,  dwarf_rcx_x86_64,     dwarf_rcx_x86_64,     LLDB_REGNUM_GENERIC_ARG4,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rdx,    nullptr,  dwarf_rdx_x86_64,     dwarf_rdx_x86_64,     LLDB_REGNUM_GENERIC_ARG3,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rdi,    nullptr,  dwarf_rdi_x86_64,     dwarf_rdi_x86_64,     LLDB_REGNUM_GENERIC_ARG1,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rsi,    nullptr,  dwarf_rsi_x86_64,     dwarf_rsi_x86_64,     LLDB_REGNUM_GENERIC_ARG2,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(rbp,    nullptr,  dwarf_rbp_x86_64,     dwarf_rbp_x86_64,     LLDB_REGNUM_GENERIC_FP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(rsp,    nullptr,  dwarf_rsp_x86_64,     dwarf_rsp_x86_64,     LLDB_REGNUM_GENERIC_SP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(r8,     nullptr,  dwarf_r8_x86_64,      dwarf_r8_x86_64,      LLDB_REGNUM_GENERIC_ARG5,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(r9,     nullptr,  dwarf_r9_x86_64,      dwarf_r9_x86_64,      LLDB_REGNUM_GENERIC_ARG6,   LLDB_INVALID_REGNUM),
    DEFINE_GPR(r10,    nullptr,  dwarf_r10_x86_64,     dwarf_r10_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r11,    nullptr,  dwarf_r11_x86_64,     dwarf_r11_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r12,    nullptr,  dwarf_r12_x86_64,     dwarf_r12_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r13,    nullptr,  dwarf_r13_x86_64,     dwarf_r13_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r14,    nullptr,  dwarf_r14_x86_64,     dwarf_r14_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(r15,    nullptr,  dwarf_r15_x86_64,     dwarf_r15_x86_64,     LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(rip,    nullptr,  dwarf_rip_x86_64,     dwarf_rip_x86_64,     LLDB_REGNUM_GENERIC_PC,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(rflags, nullptr,  dwarf_rflags_x86_64,  dwarf_rflags_x86_64,  LLDB_REGNUM_GENERIC_FLAGS,  LLDB_INVALID_REGNUM),
    DEFINE_GPR(cs,     nullptr,  dwarf_cs_x86_64,      dwarf_cs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(fs,     nullptr,  dwarf_fs_x86_64,      dwarf_fs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(gs,     nullptr,  dwarf_gs_x86_64,      dwarf_gs_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ss,     nullptr,  dwarf_ss_x86_64,      dwarf_ss_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ds,     nullptr,  dwarf_ds_x86_64,      dwarf_ds_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(es,     nullptr,  dwarf_es_x86_64,      dwarf_es_x86_64,      LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_XMM(xmm0),
    DEFINE_XMM(xmm1),
    DEFINE_XMM(xmm2),
    DEFINE_XMM(xmm3),
    DEFINE_XMM(xmm4),
    DEFINE_XMM(xmm5),
    DEFINE_XMM(xmm6),
    DEFINE_XMM(xmm7),
    DEFINE_XMM(xmm8),
    DEFINE_XMM(xmm9),
    DEFINE_XMM(xmm10),
    DEFINE_XMM(xmm11),
    DEFINE_XMM(xmm12),
    DEFINE_XMM(xmm13),
    DEFINE_XMM(xmm14),
    DEFINE_XMM(xmm15)
};
// clang-format on
} // namespace

RegisterContextWindows_x86_64::RegisterContextWindows_x86_64(
    const ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch) {
  assert(target_arch.GetMachine() == llvm::Triple::x86_64);
}

const RegisterInfo *RegisterContextWindows_x86_64::GetRegisterInfo() const {
  return g_register_infos_x86_64;
}

uint32_t RegisterContextWindows_x86_64::GetRegisterCount() const {
  return std::size(g_register_infos_x86_64);
}

uint32_t RegisterContextWindows_x86_64::GetUserRegisterCount() const {
  return std::size(g_register_infos_x86_64);
}

size_t RegisterContextWindows_x86_64::GetGPRSize() const { return sizeof(GPR); }
