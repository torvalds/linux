//===-- RegisterContextNetBSD_i386.cpp --------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RegisterContextNetBSD_i386.h"
#include "RegisterContextPOSIX_x86.h"

using namespace lldb_private;
using namespace lldb;

// this needs to match 'struct reg'
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

struct FPR_i386 {
  uint16_t fctrl;     // FPU Control Word (fcw)
  uint16_t fstat;     // FPU Status Word (fsw)
  uint16_t ftag;      // FPU Tag Word (ftw)
  uint16_t fop;       // Last Instruction Opcode (fop)
  union {
    struct {
      uint64_t fip; // Instruction Pointer
      uint64_t fdp; // Data Pointer
    } x86_64;
    struct {
      uint32_t fioff; // FPU IP Offset (fip)
      uint32_t fiseg; // FPU IP Selector (fcs)
      uint32_t fooff; // FPU Operand Pointer Offset (foo)
      uint32_t foseg; // FPU Operand Pointer Selector (fos)
    } i386_; // Added _ in the end to avoid error with gcc defining i386 in some
             // cases
  } ptr;
  uint32_t mxcsr;     // MXCSR Register State
  uint32_t mxcsrmask; // MXCSR Mask
  MMSReg stmm[8];     // 8*16 bytes for each FP-reg = 128 bytes
  XMMReg xmm[8];      // 8*16 bytes for each XMM-reg = 128 bytes
  uint32_t padding[56];
};

struct UserArea {
  GPR gpr;
  FPR_i386 i387;
  uint32_t u_debugreg[8]; // Debug registers (DR0 - DR7).
  uint32_t tlsbase;
};

#define DR_SIZE sizeof(((UserArea *)NULL)->u_debugreg[0])
#define DR_OFFSET(reg_index)                                                   \
  (LLVM_EXTENSION offsetof(UserArea, u_debugreg[reg_index]))

// Include RegisterInfos_i386 to declare our g_register_infos_i386 structure.
#define DECLARE_REGISTER_INFOS_I386_STRUCT
#include "RegisterInfos_i386.h"
#undef DECLARE_REGISTER_INFOS_I386_STRUCT

RegisterContextNetBSD_i386::RegisterContextNetBSD_i386(
    const ArchSpec &target_arch)
    : RegisterInfoInterface(target_arch) {}

size_t RegisterContextNetBSD_i386::GetGPRSize() const { return sizeof(GPR); }

const RegisterInfo *RegisterContextNetBSD_i386::GetRegisterInfo() const {
  switch (GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return g_register_infos_i386;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

uint32_t RegisterContextNetBSD_i386::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_i386) /
                               sizeof(g_register_infos_i386[0]));
}
