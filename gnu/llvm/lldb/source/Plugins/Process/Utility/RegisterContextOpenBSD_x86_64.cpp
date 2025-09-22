//===-- RegisterContextOpenBSD_x86_64.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RegisterContextOpenBSD_x86_64.h"
#include "RegisterContextPOSIX_x86.h"
#include <vector>

using namespace lldb_private;
using namespace lldb;

// /usr/include/machine/reg.h
typedef struct _GPR {
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t rax;
  uint64_t rsp;
  uint64_t rip;
  uint64_t rflags;
  uint64_t cs;
  uint64_t ss;
  uint64_t ds;
  uint64_t es;
  uint64_t fs;
  uint64_t gs;
} GPR;

struct DBG {
  uint64_t dr[16]; /* debug registers */
                   /* Index 0-3: debug address registers */
                   /* Index 4-5: reserved */
                   /* Index 6: debug status */
                   /* Index 7: debug control */
                   /* Index 8-15: reserved */
};

struct UserArea {
  GPR gpr;
  FPR fpr;
  DBG dbg;
};

#define DR_OFFSET(reg_index) (LLVM_EXTENSION offsetof(DBG, dr[reg_index]))

// Include RegisterInfos_x86_64 to declare our g_register_infos_x86_64
// structure.
#define DECLARE_REGISTER_INFOS_X86_64_STRUCT
#include "RegisterInfos_x86_64.h"
#undef DECLARE_REGISTER_INFOS_X86_64_STRUCT

static const RegisterInfo *
PrivateGetRegisterInfoPtr(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86_64:
    return g_register_infos_x86_64;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t
PrivateGetRegisterCount(const lldb_private::ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86_64:
    return static_cast<uint32_t>(sizeof(g_register_infos_x86_64) /
                                 sizeof(g_register_infos_x86_64[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterContextOpenBSD_x86_64::RegisterContextOpenBSD_x86_64(
    const ArchSpec &target_arch)
    : lldb_private::RegisterInfoInterface(target_arch),
      m_register_info_p(PrivateGetRegisterInfoPtr(target_arch)),
      m_register_count(PrivateGetRegisterCount(target_arch)) {}

size_t RegisterContextOpenBSD_x86_64::GetGPRSize() const { return sizeof(GPR); }

const RegisterInfo *RegisterContextOpenBSD_x86_64::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterContextOpenBSD_x86_64::GetRegisterCount() const {
  return m_register_count;
}
