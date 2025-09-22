//===-- GDBRemoteRegisterFallback.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "GDBRemoteRegisterFallback.h"

namespace lldb_private {
namespace process_gdb_remote {

#define REG(name, size)                                                        \
  DynamicRegisterInfo::Register {                                              \
    ConstString(#name), empty_alt_name, reg_set, size, LLDB_INVALID_INDEX32,   \
        lldb::eEncodingUint, lldb::eFormatHex, LLDB_INVALID_REGNUM,            \
        LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM, {}, {}  \
  }
#define R64(name) REG(name, 8)
#define R32(name) REG(name, 4)
#define R16(name) REG(name, 2)

static std::vector<DynamicRegisterInfo::Register> GetRegisters_aarch64() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R64(x0),  R64(x1),  R64(x2),  R64(x3),  R64(x4),  R64(x5),   R64(x6),
      R64(x7),  R64(x8),  R64(x9),  R64(x10), R64(x11), R64(x12),  R64(x13),
      R64(x14), R64(x15), R64(x16), R64(x17), R64(x18), R64(x19),  R64(x20),
      R64(x21), R64(x22), R64(x23), R64(x24), R64(x25), R64(x26),  R64(x27),
      R64(x28), R64(x29), R64(x30), R64(sp),  R64(pc),  R32(cpsr),
  };

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_msp430() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R16(pc),  R16(sp),  R16(r2),  R16(r3), R16(fp),  R16(r5),
      R16(r6),  R16(r7),  R16(r8),  R16(r9), R16(r10), R16(r11),
      R16(r12), R16(r13), R16(r14), R16(r15)};

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_x86() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R32(eax), R32(ecx), R32(edx), R32(ebx),    R32(esp), R32(ebp),
      R32(esi), R32(edi), R32(eip), R32(eflags), R32(cs),  R32(ss),
      R32(ds),  R32(es),  R32(fs),  R32(gs),
  };

  return registers;
}

static std::vector<DynamicRegisterInfo::Register> GetRegisters_x86_64() {
  ConstString empty_alt_name;
  ConstString reg_set{"general purpose registers"};

  std::vector<DynamicRegisterInfo::Register> registers{
      R64(rax), R64(rbx), R64(rcx), R64(rdx), R64(rsi), R64(rdi),
      R64(rbp), R64(rsp), R64(r8),  R64(r9),  R64(r10), R64(r11),
      R64(r12), R64(r13), R64(r14), R64(r15), R64(rip), R32(eflags),
      R32(cs),  R32(ss),  R32(ds),  R32(es),  R32(fs),  R32(gs),
  };

  return registers;
}

#undef R32
#undef R64
#undef REG

std::vector<DynamicRegisterInfo::Register>
GetFallbackRegisters(const ArchSpec &arch_to_use) {
  switch (arch_to_use.GetMachine()) {
  case llvm::Triple::aarch64:
    return GetRegisters_aarch64();
  case llvm::Triple::msp430:
    return GetRegisters_msp430();
  case llvm::Triple::x86:
    return GetRegisters_x86();
  case llvm::Triple::x86_64:
    return GetRegisters_x86_64();
  default:
    break;
  }

  return {};
}

} // namespace process_gdb_remote
} // namespace lldb_private
