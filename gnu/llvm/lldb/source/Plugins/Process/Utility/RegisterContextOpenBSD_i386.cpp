//===-- RegisterContextOpenBSD_i386.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RegisterContextOpenBSD_i386.h"
#include "RegisterContextPOSIX_x86.h"

using namespace lldb_private;
using namespace lldb;

// /usr/include/machine/reg.h
struct GPR {
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint32_t eip;
  uint32_t eflags;
  uint32_t cs;
  uint32_t ss;
  uint32_t ds;
  uint32_t es;
  uint32_t fs;
  uint32_t gs;
};

struct dbreg {
  uint32_t dr[8]; /* debug registers */
                  /* Index 0-3: debug address registers */
                  /* Index 4-5: reserved */
                  /* Index 6: debug status */
                  /* Index 7: debug control */
};

using FPR_i386 = FXSAVE;

struct UserArea {
  GPR gpr;
  FPR_i386 i387;
};

#define DR_SIZE sizeof(uint32_t)
#define DR_OFFSET(reg_index) (LLVM_EXTENSION offsetof(dbreg, dr[reg_index]))

// Include RegisterInfos_i386 to declare our g_register_infos_i386 structure.
#define DECLARE_REGISTER_INFOS_I386_STRUCT
#include "RegisterInfos_i386.h"
#undef DECLARE_REGISTER_INFOS_I386_STRUCT

RegisterContextOpenBSD_i386::RegisterContextOpenBSD_i386(
    const ArchSpec &target_arch)
    : RegisterInfoInterface(target_arch) {}

size_t RegisterContextOpenBSD_i386::GetGPRSize() const { return sizeof(GPR); }

const RegisterInfo *RegisterContextOpenBSD_i386::GetRegisterInfo() const {
  switch (GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
    return g_register_infos_i386;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

uint32_t RegisterContextOpenBSD_i386::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_i386) /
                               sizeof(g_register_infos_i386[0]));
}
