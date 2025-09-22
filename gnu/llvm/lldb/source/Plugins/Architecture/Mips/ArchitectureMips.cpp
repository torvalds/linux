//===-- ArchitectureMips.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/Architecture/Mips/ArchitectureMips.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

using namespace lldb_private;
using namespace lldb;

LLDB_PLUGIN_DEFINE(ArchitectureMips)

void ArchitectureMips::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "Mips-specific algorithms",
                                &ArchitectureMips::Create);
}

void ArchitectureMips::Terminate() {
  PluginManager::UnregisterPlugin(&ArchitectureMips::Create);
}

std::unique_ptr<Architecture> ArchitectureMips::Create(const ArchSpec &arch) {
  return arch.IsMIPS() ?
      std::unique_ptr<Architecture>(new ArchitectureMips(arch)) : nullptr;
}

addr_t ArchitectureMips::GetCallableLoadAddress(addr_t code_addr,
                                                AddressClass addr_class) const {
  bool is_alternate_isa = false;

  switch (addr_class) {
  case AddressClass::eData:
  case AddressClass::eDebug:
    return LLDB_INVALID_ADDRESS;
  case AddressClass::eCodeAlternateISA:
    is_alternate_isa = true;
    break;
  default: break;
  }

  if ((code_addr & 2ull) || is_alternate_isa)
    return code_addr | 1u;
  return code_addr;
}

addr_t ArchitectureMips::GetOpcodeLoadAddress(addr_t opcode_addr,
                                              AddressClass addr_class) const {
  switch (addr_class) {
  case AddressClass::eData:
  case AddressClass::eDebug:
    return LLDB_INVALID_ADDRESS;
  default: break;
  }
  return opcode_addr & ~(1ull);
}

lldb::addr_t ArchitectureMips::GetBreakableLoadAddress(lldb::addr_t addr,
                                                       Target &target) const {

  Log *log = GetLog(LLDBLog::Breakpoints);

  Address resolved_addr;

  SectionLoadList &section_load_list = target.GetSectionLoadList();
  if (section_load_list.IsEmpty())
    // No sections are loaded, so we must assume we are not running yet and
    // need to operate only on file address.
    target.ResolveFileAddress(addr, resolved_addr);
  else
    target.ResolveLoadAddress(addr, resolved_addr);

  addr_t current_offset = 0;

  // Get the function boundaries to make sure we don't scan back before the
  // beginning of the current function.
  ModuleSP temp_addr_module_sp(resolved_addr.GetModule());
  if (temp_addr_module_sp) {
    SymbolContext sc;
    SymbolContextItem resolve_scope =
        eSymbolContextFunction | eSymbolContextSymbol;
    temp_addr_module_sp->ResolveSymbolContextForAddress(resolved_addr,
      resolve_scope, sc);
    Address sym_addr;
    if (sc.function)
      sym_addr = sc.function->GetAddressRange().GetBaseAddress();
    else if (sc.symbol)
      sym_addr = sc.symbol->GetAddress();

    addr_t function_start = sym_addr.GetLoadAddress(&target);
    if (function_start == LLDB_INVALID_ADDRESS)
      function_start = sym_addr.GetFileAddress();

    if (function_start)
      current_offset = addr - function_start;
  }

  // If breakpoint address is start of function then we dont have to do
  // anything.
  if (current_offset == 0)
    return addr;

  auto insn = GetInstructionAtAddress(target, current_offset, addr);

  if (nullptr == insn || !insn->HasDelaySlot())
    return addr;

  // Adjust the breakable address
  uint64_t breakable_addr = addr - insn->GetOpcode().GetByteSize();
  LLDB_LOGF(log,
            "Target::%s Breakpoint at 0x%8.8" PRIx64
            " is adjusted to 0x%8.8" PRIx64 " due to delay slot\n",
            __FUNCTION__, addr, breakable_addr);

  return breakable_addr;
}

Instruction *ArchitectureMips::GetInstructionAtAddress(
    Target &target, const Address &resolved_addr, addr_t symbol_offset) const {

  auto loop_count = symbol_offset / 2;

  uint32_t arch_flags = m_arch.GetFlags();
  bool IsMips16 = arch_flags & ArchSpec::eMIPSAse_mips16;
  bool IsMicromips = arch_flags & ArchSpec::eMIPSAse_micromips;

  if (loop_count > 3) {
    // Scan previous 6 bytes
    if (IsMips16 | IsMicromips)
      loop_count = 3;
    // For mips-only, instructions are always 4 bytes, so scan previous 4
    // bytes only.
    else
      loop_count = 2;
  }

  // Create Disassembler Instance
  lldb::DisassemblerSP disasm_sp(
    Disassembler::FindPlugin(m_arch, nullptr, nullptr));

  InstructionList instruction_list;
  InstructionSP prev_insn;
  uint32_t inst_to_choose = 0;

  Address addr = resolved_addr;

  for (uint32_t i = 1; i <= loop_count; i++) {
    // Adjust the address to read from.
    addr.Slide(-2);
    uint32_t insn_size = 0;

    disasm_sp->ParseInstructions(target, addr,
                                 {Disassembler::Limit::Bytes, i * 2}, nullptr);

    uint32_t num_insns = disasm_sp->GetInstructionList().GetSize();
    if (num_insns) {
      prev_insn = disasm_sp->GetInstructionList().GetInstructionAtIndex(0);
      insn_size = prev_insn->GetOpcode().GetByteSize();
      if (i == 1 && insn_size == 2) {
        // This looks like a valid 2-byte instruction (but it could be a part
        // of upper 4 byte instruction).
        instruction_list.Append(prev_insn);
        inst_to_choose = 1;
      }
      else if (i == 2) {
        // Here we may get one 4-byte instruction or two 2-byte instructions.
        if (num_insns == 2) {
          // Looks like there are two 2-byte instructions above our
          // breakpoint target address. Now the upper 2-byte instruction is
          // either a valid 2-byte instruction or could be a part of it's
          // upper 4-byte instruction. In both cases we don't care because in
          // this case lower 2-byte instruction is definitely a valid
          // instruction and whatever i=1 iteration has found out is true.
          inst_to_choose = 1;
          break;
        }
        else if (insn_size == 4) {
          // This instruction claims its a valid 4-byte instruction. But it
          // could be a part of it's upper 4-byte instruction. Lets try
          // scanning upper 2 bytes to verify this.
          instruction_list.Append(prev_insn);
          inst_to_choose = 2;
        }
      }
      else if (i == 3) {
        if (insn_size == 4)
          // FIXME: We reached here that means instruction at [target - 4] has
          // already claimed to be a 4-byte instruction, and now instruction
          // at [target - 6] is also claiming that it's a 4-byte instruction.
          // This can not be true. In this case we can not decide the valid
          // previous instruction so we let lldb set the breakpoint at the
          // address given by user.
          inst_to_choose = 0;
        else
          // This is straight-forward
          inst_to_choose = 2;
        break;
      }
    }
    else {
      // Decode failed, bytes do not form a valid instruction. So whatever
      // previous iteration has found out is true.
      if (i > 1) {
        inst_to_choose = i - 1;
        break;
      }
    }
  }

  // Check if we are able to find any valid instruction.
  if (inst_to_choose) {
    if (inst_to_choose > instruction_list.GetSize())
      inst_to_choose--;
    return instruction_list.GetInstructionAtIndex(inst_to_choose - 1).get();
  }

  return nullptr;
}
