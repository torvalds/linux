//===-- RegisterContextLinux_x86_64.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RegisterContextLinux_x86_64.h"
#include "RegisterContextLinux_i386.h"
#include "RegisterContextPOSIX_x86.h"
#include <vector>

using namespace lldb_private;
using namespace lldb;

typedef struct _GPR {
  uint64_t r15;
  uint64_t r14;
  uint64_t r13;
  uint64_t r12;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t r11;
  uint64_t r10;
  uint64_t r9;
  uint64_t r8;
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rsi;
  uint64_t rdi;
  uint64_t orig_rax;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
  uint64_t fs_base;
  uint64_t gs_base;
  uint64_t ds;
  uint64_t es;
  uint64_t fs;
  uint64_t gs;
} GPR;

struct DBG {
  uint64_t dr[8];
};

struct UserArea {
  GPR gpr;         // General purpose registers.
  int32_t fpvalid; // True if FPU is being used.
  int32_t pad0;
  FXSAVE fpr; // General purpose floating point registers (see FPR for extended
              // register sets).
  uint64_t tsize;       // Text segment size.
  uint64_t dsize;       // Data segment size.
  uint64_t ssize;       // Stack segment size.
  uint64_t start_code;  // VM address of text.
  uint64_t start_stack; // VM address of stack bottom (top in rsp).
  int64_t signal;       // Signal causing core dump.
  int32_t reserved;     // Unused.
  int32_t pad1;
  uint64_t ar0;           // Location of GPR's.
  FXSAVE *fpstate;        // Location of FPR's.
  uint64_t magic;         // Identifier for core dumps.
  char u_comm[32];        // Command causing core dump.
  DBG dbg;                // Debug registers.
  uint64_t error_code;    // CPU error code.
  uint64_t fault_address; // Control register CR3.
};

#define DR_OFFSET(reg_index)                                                   \
  (LLVM_EXTENSION offsetof(UserArea, dbg) +                                    \
   LLVM_EXTENSION offsetof(DBG, dr[reg_index]))

// Include RegisterInfos_x86_64 to declare our g_register_infos_x86_64_with_base
// structure.
#define DECLARE_REGISTER_INFOS_X86_64_STRUCT
#include "RegisterInfos_x86_64_with_base.h"
#undef DECLARE_REGISTER_INFOS_X86_64_STRUCT

static std::vector<lldb_private::RegisterInfo> &GetPrivateRegisterInfoVector() {
  static std::vector<lldb_private::RegisterInfo> g_register_infos;
  return g_register_infos;
}

static const RegisterInfo *
GetRegisterInfo_i386(const lldb_private::ArchSpec &arch) {
  std::vector<lldb_private::RegisterInfo> &g_register_infos =
      GetPrivateRegisterInfoVector();

  // Allocate RegisterInfo only once
  if (g_register_infos.empty()) {
    // Copy the register information from base class
    std::unique_ptr<RegisterContextLinux_i386> reg_interface(
        new RegisterContextLinux_i386(arch));
    const RegisterInfo *base_info = reg_interface->GetRegisterInfo();
    g_register_infos.insert(g_register_infos.end(), &base_info[0],
                            &base_info[k_num_registers_i386]);

// Include RegisterInfos_x86_64 to update the g_register_infos structure
//  with x86_64 offsets.
#define UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS
#include "RegisterInfos_x86_64_with_base.h"
#undef UPDATE_REGISTER_INFOS_I386_STRUCT_WITH_X86_64_OFFSETS
  }

  return &g_register_infos[0];
}

static const RegisterInfo *GetRegisterInfoPtr(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86:
    return GetRegisterInfo_i386(target_arch);
  case llvm::Triple::x86_64:
    return g_register_infos_x86_64_with_base;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

static uint32_t GetRegisterInfoCount(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86: {
    assert(!GetPrivateRegisterInfoVector().empty() &&
           "i386 register info not yet filled.");
    return static_cast<uint32_t>(GetPrivateRegisterInfoVector().size());
  }
  case llvm::Triple::x86_64:
    return static_cast<uint32_t>(sizeof(g_register_infos_x86_64_with_base) /
                                 sizeof(g_register_infos_x86_64_with_base[0]));
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

static uint32_t GetUserRegisterInfoCount(const ArchSpec &target_arch) {
  switch (target_arch.GetMachine()) {
  case llvm::Triple::x86:
    return static_cast<uint32_t>(k_num_user_registers_i386);
  case llvm::Triple::x86_64:
    return static_cast<uint32_t>(x86_64_with_base::k_num_user_registers);
  default:
    assert(false && "Unhandled target architecture.");
    return 0;
  }
}

RegisterContextLinux_x86_64::RegisterContextLinux_x86_64(
    const ArchSpec &target_arch)
    : lldb_private::RegisterContextLinux_x86(
          target_arch,
          {"orig_rax",
           nullptr,
           sizeof(((GPR *)nullptr)->orig_rax),
           (LLVM_EXTENSION offsetof(GPR, orig_rax)),
           eEncodingUint,
           eFormatHex,
           {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
            LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
           nullptr,
           nullptr,
           nullptr}),
      m_register_info_p(GetRegisterInfoPtr(target_arch)),
      m_register_info_count(GetRegisterInfoCount(target_arch)),
      m_user_register_count(GetUserRegisterInfoCount(target_arch)) {}

size_t RegisterContextLinux_x86_64::GetGPRSizeStatic() { return sizeof(GPR); }

const RegisterInfo *RegisterContextLinux_x86_64::GetRegisterInfo() const {
  return m_register_info_p;
}

uint32_t RegisterContextLinux_x86_64::GetRegisterCount() const {
  return m_register_info_count;
}

uint32_t RegisterContextLinux_x86_64::GetUserRegisterCount() const {
  return m_user_register_count;
}
