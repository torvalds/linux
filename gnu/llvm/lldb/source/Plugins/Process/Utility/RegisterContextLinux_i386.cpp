//===-- RegisterContextLinux_i386.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//

#include "RegisterContextLinux_i386.h"
#include "RegisterContextPOSIX_x86.h"

using namespace lldb_private;
using namespace lldb;

struct GPR {
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esi;
  uint32_t edi;
  uint32_t ebp;
  uint32_t eax;
  uint32_t ds;
  uint32_t es;
  uint32_t fs;
  uint32_t gs;
  uint32_t orig_eax;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t esp;
  uint32_t ss;
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
  GPR regs;               // General purpose registers.
  int32_t fpvalid;        // True if FPU is being used.
  FPR_i386 i387;          // FPU registers.
  uint32_t tsize;         // Text segment size.
  uint32_t dsize;         // Data segment size.
  uint32_t ssize;         // Stack segment size.
  uint32_t start_code;    // VM address of text.
  uint32_t start_stack;   // VM address of stack bottom (top in rsp).
  int32_t signal;         // Signal causing core dump.
  int32_t reserved;       // Unused.
  uint32_t ar0;           // Location of GPR's.
  uint32_t fpstate;       // Location of FPR's. Should be a FXSTATE *, but this
                          //  has to be 32-bits even on 64-bit systems.
  uint32_t magic;         // Identifier for core dumps.
  char u_comm[32];        // Command causing core dump.
  uint32_t u_debugreg[8]; // Debug registers (DR0 - DR7).
};

#define DR_SIZE sizeof(((UserArea *)NULL)->u_debugreg[0])
#define DR_0_OFFSET 0xFC
#define DR_OFFSET(reg_index) (DR_0_OFFSET + (reg_index * 4))
#define FPR_SIZE(reg) sizeof(((FPR_i386 *)NULL)->reg)

// Include RegisterInfos_i386 to declare our g_register_infos_i386 structure.
#define DECLARE_REGISTER_INFOS_I386_STRUCT
#include "RegisterInfos_i386.h"
#undef DECLARE_REGISTER_INFOS_I386_STRUCT

RegisterContextLinux_i386::RegisterContextLinux_i386(
    const ArchSpec &target_arch)
    : RegisterContextLinux_x86(
          target_arch,
          {"orig_eax",
           nullptr,
           sizeof(((GPR *)nullptr)->orig_eax),
           (LLVM_EXTENSION offsetof(GPR, orig_eax)),
           eEncodingUint,
           eFormatHex,
           {LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
            LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM},
           nullptr,
           nullptr,
           nullptr}) {}

size_t RegisterContextLinux_i386::GetGPRSizeStatic() { return sizeof(GPR); }

const RegisterInfo *RegisterContextLinux_i386::GetRegisterInfo() const {
  switch (GetTargetArchitecture().GetMachine()) {
  case llvm::Triple::x86:
  case llvm::Triple::x86_64:
    return g_register_infos_i386;
  default:
    assert(false && "Unhandled target architecture.");
    return nullptr;
  }
}

uint32_t RegisterContextLinux_i386::GetRegisterCount() const {
  return static_cast<uint32_t>(sizeof(g_register_infos_i386) /
                               sizeof(g_register_infos_i386[0]));
}

uint32_t RegisterContextLinux_i386::GetUserRegisterCount() const {
  return static_cast<uint32_t>(k_num_user_registers_i386);
}
