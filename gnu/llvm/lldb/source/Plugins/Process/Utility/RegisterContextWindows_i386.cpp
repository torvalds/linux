//===-- RegisterContextWindows_i386.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextWindows_i386.h"
#include "RegisterContext_x86.h"
#include "lldb-x86-register-enums.h"

using namespace lldb_private;
using namespace lldb;

namespace {
// Declare our g_register_infos structure.
typedef struct _GPR {
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t edi;
  uint32_t esi;
  uint32_t ebp;
  uint32_t esp;
  uint32_t eip;
  uint32_t eflags;
  uint32_t cs;
  uint32_t fs;
  uint32_t gs;
  uint32_t ss;
  uint32_t ds;
  uint32_t es;
} GPR;

#define GPR_OFFSET(regname) (LLVM_EXTENSION offsetof(GPR, regname))

#define DEFINE_GPR(reg, alt, kind1, kind2, kind3, kind4)                       \
  {                                                                            \
#reg, alt, sizeof(((GPR *)nullptr)->reg), GPR_OFFSET(reg), eEncodingUint,  \
        eFormatHex,                                                            \
        {kind1, kind2, kind3, kind4, lldb_##reg##_i386 }, nullptr, nullptr,    \
        nullptr,                                                               \
  }

// clang-format off
static RegisterInfo g_register_infos_i386[] = {
// General purpose registers     EH_Frame              DWARF                 Generic                     Process Plugin
//  ===========================  ==================    ================      =========================   ====================
    DEFINE_GPR(eax,   nullptr,   ehframe_eax_i386,     dwarf_eax_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ebx,   nullptr,   ehframe_ebx_i386,     dwarf_ebx_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ecx,   nullptr,   ehframe_ecx_i386,     dwarf_ecx_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(edx,   nullptr,   ehframe_edx_i386,     dwarf_edx_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(edi,   nullptr,   ehframe_edi_i386,     dwarf_edi_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(esi,   nullptr,   ehframe_esi_i386,     dwarf_esi_i386,       LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ebp,   "fp",      ehframe_ebp_i386,     dwarf_ebp_i386,       LLDB_REGNUM_GENERIC_FP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(esp,   "sp",      ehframe_esp_i386,     dwarf_esp_i386,       LLDB_REGNUM_GENERIC_SP,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(eip,   "pc",      ehframe_eip_i386,     dwarf_eip_i386,       LLDB_REGNUM_GENERIC_PC,     LLDB_INVALID_REGNUM),
    DEFINE_GPR(eflags, "flags",  ehframe_eflags_i386,  dwarf_eflags_i386,    LLDB_REGNUM_GENERIC_FLAGS,  LLDB_INVALID_REGNUM),
    DEFINE_GPR(cs,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_cs_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(fs,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_fs_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(gs,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_gs_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ss,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_ss_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(ds,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_ds_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
    DEFINE_GPR(es,     nullptr,  LLDB_INVALID_REGNUM,  dwarf_es_i386,        LLDB_INVALID_REGNUM,        LLDB_INVALID_REGNUM),
};
// clang-format on
} // namespace

RegisterContextWindows_i386::RegisterContextWindows_i386(
    const ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch) {
  assert(target_arch.GetMachine() == llvm::Triple::x86);
}

const RegisterInfo *RegisterContextWindows_i386::GetRegisterInfo() const {
  return g_register_infos_i386;
}

uint32_t RegisterContextWindows_i386::GetRegisterCount() const {
  return std::size(g_register_infos_i386);
}

uint32_t RegisterContextWindows_i386::GetUserRegisterCount() const {
  return std::size(g_register_infos_i386);
}

size_t RegisterContextWindows_i386::GetGPRSize() const { return sizeof(GPR); }
