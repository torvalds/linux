//===-- NativeProcessSoftwareSingleStep.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "NativeProcessSoftwareSingleStep.h"

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Host/common/NativeRegisterContext.h"
#include "lldb/Utility/RegisterValue.h"

#include <unordered_map>

using namespace lldb;
using namespace lldb_private;

namespace {

struct EmulatorBaton {
  NativeProcessProtocol &m_process;
  NativeRegisterContext &m_reg_context;

  // eRegisterKindDWARF -> RegsiterValue
  std::unordered_map<uint32_t, RegisterValue> m_register_values;

  EmulatorBaton(NativeProcessProtocol &process,
                NativeRegisterContext &reg_context)
      : m_process(process), m_reg_context(reg_context) {}
};

} // anonymous namespace

static size_t ReadMemoryCallback(EmulateInstruction *instruction, void *baton,
                                 const EmulateInstruction::Context &context,
                                 lldb::addr_t addr, void *dst, size_t length) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);

  size_t bytes_read;
  emulator_baton->m_process.ReadMemory(addr, dst, length, bytes_read);
  return bytes_read;
}

static bool ReadRegisterCallback(EmulateInstruction *instruction, void *baton,
                                 const RegisterInfo *reg_info,
                                 RegisterValue &reg_value) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);

  auto it = emulator_baton->m_register_values.find(
      reg_info->kinds[eRegisterKindDWARF]);
  if (it != emulator_baton->m_register_values.end()) {
    reg_value = it->second;
    return true;
  }

  // The emulator only fill in the dwarf regsiter numbers (and in some case the
  // generic register numbers). Get the full register info from the register
  // context based on the dwarf register numbers.
  const RegisterInfo *full_reg_info =
      emulator_baton->m_reg_context.GetRegisterInfo(
          eRegisterKindDWARF, reg_info->kinds[eRegisterKindDWARF]);

  Status error =
      emulator_baton->m_reg_context.ReadRegister(full_reg_info, reg_value);
  if (error.Success())
    return true;

  return false;
}

static bool WriteRegisterCallback(EmulateInstruction *instruction, void *baton,
                                  const EmulateInstruction::Context &context,
                                  const RegisterInfo *reg_info,
                                  const RegisterValue &reg_value) {
  EmulatorBaton *emulator_baton = static_cast<EmulatorBaton *>(baton);
  emulator_baton->m_register_values[reg_info->kinds[eRegisterKindDWARF]] =
      reg_value;
  return true;
}

static size_t WriteMemoryCallback(EmulateInstruction *instruction, void *baton,
                                  const EmulateInstruction::Context &context,
                                  lldb::addr_t addr, const void *dst,
                                  size_t length) {
  return length;
}

static lldb::addr_t ReadFlags(NativeRegisterContext &regsiter_context) {
  const RegisterInfo *flags_info = regsiter_context.GetRegisterInfo(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS);
  return regsiter_context.ReadRegisterAsUnsigned(flags_info,
                                                 LLDB_INVALID_ADDRESS);
}

static int GetSoftwareBreakpointSize(const ArchSpec &arch,
                                     lldb::addr_t next_flags) {
  if (arch.GetMachine() == llvm::Triple::arm) {
    if (next_flags & 0x20)
      // Thumb mode
      return 2;
    // Arm mode
    return 4;
  }
  if (arch.IsMIPS() || arch.GetTriple().isPPC64() ||
      arch.GetTriple().isRISCV() || arch.GetTriple().isLoongArch())
    return 4;
  return 0;
}

static Status SetSoftwareBreakpointOnPC(const ArchSpec &arch, lldb::addr_t pc,
                                        lldb::addr_t next_flags,
                                        NativeProcessProtocol &process) {
  int size_hint = GetSoftwareBreakpointSize(arch, next_flags);
  Status error;
  error = process.SetBreakpoint(pc, size_hint, /*hardware=*/false);

  // If setting the breakpoint fails because pc is out of the address
  // space, ignore it and let the debugee segfault.
  if (error.GetError() == EIO || error.GetError() == EFAULT)
    return Status();
  if (error.Fail())
    return error;

  return Status();
}

Status NativeProcessSoftwareSingleStep::SetupSoftwareSingleStepping(
    NativeThreadProtocol &thread) {
  Status error;
  NativeProcessProtocol &process = thread.GetProcess();
  NativeRegisterContext &register_context = thread.GetRegisterContext();
  const ArchSpec &arch = process.GetArchitecture();

  std::unique_ptr<EmulateInstruction> emulator_up(
      EmulateInstruction::FindPlugin(arch, eInstructionTypePCModifying,
                                     nullptr));

  if (emulator_up == nullptr)
    return Status("Instruction emulator not found!");

  EmulatorBaton baton(process, register_context);
  emulator_up->SetBaton(&baton);
  emulator_up->SetReadMemCallback(&ReadMemoryCallback);
  emulator_up->SetReadRegCallback(&ReadRegisterCallback);
  emulator_up->SetWriteMemCallback(&WriteMemoryCallback);
  emulator_up->SetWriteRegCallback(&WriteRegisterCallback);

  if (!emulator_up->ReadInstruction()) {
    // try to get at least the size of next instruction to set breakpoint.
    auto instr_size = emulator_up->GetLastInstrSize();
    if (!instr_size)
      return Status("Read instruction failed!");
    bool success = false;
    auto pc = emulator_up->ReadRegisterUnsigned(eRegisterKindGeneric,
                                                LLDB_REGNUM_GENERIC_PC,
                                                LLDB_INVALID_ADDRESS, &success);
    if (!success)
      return Status("Reading pc failed!");
    lldb::addr_t next_pc = pc + *instr_size;
    auto result =
        SetSoftwareBreakpointOnPC(arch, next_pc, /* next_flags */ 0x0, process);
    m_threads_stepping_with_breakpoint.insert({thread.GetID(), next_pc});
    return result;
  }

  bool emulation_result =
      emulator_up->EvaluateInstruction(eEmulateInstructionOptionAutoAdvancePC);

  const RegisterInfo *reg_info_pc = register_context.GetRegisterInfo(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  const RegisterInfo *reg_info_flags = register_context.GetRegisterInfo(
      eRegisterKindGeneric, LLDB_REGNUM_GENERIC_FLAGS);

  auto pc_it =
      baton.m_register_values.find(reg_info_pc->kinds[eRegisterKindDWARF]);
  auto flags_it = reg_info_flags == nullptr
                      ? baton.m_register_values.end()
                      : baton.m_register_values.find(
                            reg_info_flags->kinds[eRegisterKindDWARF]);

  lldb::addr_t next_pc;
  lldb::addr_t next_flags;
  if (emulation_result) {
    assert(pc_it != baton.m_register_values.end() &&
           "Emulation was successfull but PC wasn't updated");
    next_pc = pc_it->second.GetAsUInt64();

    if (flags_it != baton.m_register_values.end())
      next_flags = flags_it->second.GetAsUInt64();
    else
      next_flags = ReadFlags(register_context);
  } else if (pc_it == baton.m_register_values.end()) {
    // Emulate instruction failed and it haven't changed PC. Advance PC with
    // the size of the current opcode because the emulation of all
    // PC modifying instruction should be successful. The failure most
    // likely caused by a not supported instruction which don't modify PC.
    next_pc = register_context.GetPC() + emulator_up->GetOpcode().GetByteSize();
    next_flags = ReadFlags(register_context);
  } else {
    // The instruction emulation failed after it modified the PC. It is an
    // unknown error where we can't continue because the next instruction is
    // modifying the PC but we don't  know how.
    return Status("Instruction emulation failed unexpectedly.");
  }
  auto result = SetSoftwareBreakpointOnPC(arch, next_pc, next_flags, process);
  m_threads_stepping_with_breakpoint.insert({thread.GetID(), next_pc});
  return result;
}
