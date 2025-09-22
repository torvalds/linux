//===-- EmulationStateARM.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EmulationStateARM.h"

#include "lldb/Interpreter/OptionValueArray.h"
#include "lldb/Interpreter/OptionValueDictionary.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/Utility/Scalar.h"

#include "Utility/ARM_DWARF_Registers.h"

using namespace lldb;
using namespace lldb_private;

EmulationStateARM::EmulationStateARM() : m_vfp_regs(), m_memory() {
  ClearPseudoRegisters();
}

EmulationStateARM::~EmulationStateARM() = default;

bool EmulationStateARM::StorePseudoRegisterValue(uint32_t reg_num,
                                                 uint64_t value) {
  if (reg_num <= dwarf_cpsr)
    m_gpr[reg_num - dwarf_r0] = (uint32_t)value;
  else if ((dwarf_s0 <= reg_num) && (reg_num <= dwarf_s31)) {
    uint32_t idx = reg_num - dwarf_s0;
    m_vfp_regs.s_regs[idx] = (uint32_t)value;
  } else if ((dwarf_d0 <= reg_num) && (reg_num <= dwarf_d31)) {
    uint32_t idx = reg_num - dwarf_d0;
    if (idx < 16) {
      m_vfp_regs.s_regs[idx * 2] = (uint32_t)value;
      m_vfp_regs.s_regs[idx * 2 + 1] = (uint32_t)(value >> 32);
    } else
      m_vfp_regs.d_regs[idx - 16] = value;
  } else
    return false;

  return true;
}

uint64_t EmulationStateARM::ReadPseudoRegisterValue(uint32_t reg_num,
                                                    bool &success) {
  uint64_t value = 0;
  success = true;

  if (reg_num <= dwarf_cpsr)
    value = m_gpr[reg_num - dwarf_r0];
  else if ((dwarf_s0 <= reg_num) && (reg_num <= dwarf_s31)) {
    uint32_t idx = reg_num - dwarf_s0;
    value = m_vfp_regs.s_regs[idx];
  } else if ((dwarf_d0 <= reg_num) && (reg_num <= dwarf_d31)) {
    uint32_t idx = reg_num - dwarf_d0;
    if (idx < 16)
      value = (uint64_t)m_vfp_regs.s_regs[idx * 2] |
              ((uint64_t)m_vfp_regs.s_regs[idx * 2 + 1] << 32);
    else
      value = m_vfp_regs.d_regs[idx - 16];
  } else
    success = false;

  return value;
}

void EmulationStateARM::ClearPseudoRegisters() {
  for (int i = 0; i < 17; ++i)
    m_gpr[i] = 0;

  for (int i = 0; i < 32; ++i)
    m_vfp_regs.s_regs[i] = 0;

  for (int i = 0; i < 16; ++i)
    m_vfp_regs.d_regs[i] = 0;
}

void EmulationStateARM::ClearPseudoMemory() { m_memory.clear(); }

bool EmulationStateARM::StoreToPseudoAddress(lldb::addr_t p_address,
                                             uint32_t value) {
  m_memory[p_address] = value;
  return true;
}

uint32_t EmulationStateARM::ReadFromPseudoAddress(lldb::addr_t p_address,
                                                  bool &success) {
  std::map<lldb::addr_t, uint32_t>::iterator pos;
  uint32_t ret_val = 0;

  success = true;
  pos = m_memory.find(p_address);
  if (pos != m_memory.end())
    ret_val = pos->second;
  else
    success = false;

  return ret_val;
}

size_t EmulationStateARM::ReadPseudoMemory(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context, lldb::addr_t addr, void *dst,
    size_t length) {
  if (!baton)
    return 0;

  bool success = true;
  EmulationStateARM *pseudo_state = (EmulationStateARM *)baton;
  if (length <= 4) {
    uint32_t value = pseudo_state->ReadFromPseudoAddress(addr, success);
    if (!success)
      return 0;

    if (endian::InlHostByteOrder() == lldb::eByteOrderBig)
      value = llvm::byteswap<uint32_t>(value);
    *((uint32_t *)dst) = value;
  } else if (length == 8) {
    uint32_t value1 = pseudo_state->ReadFromPseudoAddress(addr, success);
    if (!success)
      return 0;

    uint32_t value2 = pseudo_state->ReadFromPseudoAddress(addr + 4, success);
    if (!success)
      return 0;

    if (endian::InlHostByteOrder() == lldb::eByteOrderBig) {
      value1 = llvm::byteswap<uint32_t>(value1);
      value2 = llvm::byteswap<uint32_t>(value2);
    }
    ((uint32_t *)dst)[0] = value1;
    ((uint32_t *)dst)[1] = value2;
  } else
    success = false;

  if (success)
    return length;

  return 0;
}

size_t EmulationStateARM::WritePseudoMemory(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context, lldb::addr_t addr,
    const void *dst, size_t length) {
  if (!baton)
    return 0;

  EmulationStateARM *pseudo_state = (EmulationStateARM *)baton;

  if (length <= 4) {
    uint32_t value;
    memcpy (&value, dst, sizeof (uint32_t));
    if (endian::InlHostByteOrder() == lldb::eByteOrderBig)
      value = llvm::byteswap<uint32_t>(value);

    pseudo_state->StoreToPseudoAddress(addr, value);
    return length;
  } else if (length == 8) {
    uint32_t value1;
    uint32_t value2;
    memcpy (&value1, dst, sizeof (uint32_t));
    memcpy(&value2, static_cast<const uint8_t *>(dst) + sizeof(uint32_t),
           sizeof(uint32_t));
    if (endian::InlHostByteOrder() == lldb::eByteOrderBig) {
      value1 = llvm::byteswap<uint32_t>(value1);
      value2 = llvm::byteswap<uint32_t>(value2);
    }

    pseudo_state->StoreToPseudoAddress(addr, value1);
    pseudo_state->StoreToPseudoAddress(addr + 4, value2);
    return length;
  }

  return 0;
}

bool EmulationStateARM::ReadPseudoRegister(
    EmulateInstruction *instruction, void *baton,
    const lldb_private::RegisterInfo *reg_info,
    lldb_private::RegisterValue &reg_value) {
  if (!baton || !reg_info)
    return false;

  bool success = true;
  EmulationStateARM *pseudo_state = (EmulationStateARM *)baton;
  const uint32_t dwarf_reg_num = reg_info->kinds[eRegisterKindDWARF];
  assert(dwarf_reg_num != LLDB_INVALID_REGNUM);
  uint64_t reg_uval =
      pseudo_state->ReadPseudoRegisterValue(dwarf_reg_num, success);

  if (success)
    success = reg_value.SetUInt(reg_uval, reg_info->byte_size);
  return success;
}

bool EmulationStateARM::WritePseudoRegister(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context,
    const lldb_private::RegisterInfo *reg_info,
    const lldb_private::RegisterValue &reg_value) {
  if (!baton || !reg_info)
    return false;

  EmulationStateARM *pseudo_state = (EmulationStateARM *)baton;
  const uint32_t dwarf_reg_num = reg_info->kinds[eRegisterKindDWARF];
  assert(dwarf_reg_num != LLDB_INVALID_REGNUM);
  return pseudo_state->StorePseudoRegisterValue(dwarf_reg_num,
                                                reg_value.GetAsUInt64());
}

bool EmulationStateARM::CompareState(EmulationStateARM &other_state,
                                     Stream &out_stream) {
  bool match = true;

  for (int i = 0; match && i < 17; ++i) {
    if (m_gpr[i] != other_state.m_gpr[i]) {
      match = false;
      out_stream.Printf("r%d: 0x%x != 0x%x\n", i, m_gpr[i],
                        other_state.m_gpr[i]);
    }
  }

  for (int i = 0; match && i < 32; ++i) {
    if (m_vfp_regs.s_regs[i] != other_state.m_vfp_regs.s_regs[i]) {
      match = false;
      out_stream.Printf("s%d: 0x%x != 0x%x\n", i, m_vfp_regs.s_regs[i],
                        other_state.m_vfp_regs.s_regs[i]);
    }
  }

  for (int i = 0; match && i < 16; ++i) {
    if (m_vfp_regs.d_regs[i] != other_state.m_vfp_regs.d_regs[i]) {
      match = false;
      out_stream.Printf("d%d: 0x%" PRIx64 " != 0x%" PRIx64 "\n", i + 16,
                        m_vfp_regs.d_regs[i], other_state.m_vfp_regs.d_regs[i]);
    }
  }

  // other_state is the expected state. If it has memory, check it.
  if (!other_state.m_memory.empty() && m_memory != other_state.m_memory) {
    match = false;
    out_stream.Printf("memory does not match\n");
    out_stream.Printf("got memory:\n");
    for (auto p : m_memory)
      out_stream.Printf("0x%08" PRIx64 ": 0x%08x\n", p.first, p.second);
    out_stream.Printf("expected memory:\n");
    for (auto p : other_state.m_memory)
      out_stream.Printf("0x%08" PRIx64 ": 0x%08x\n", p.first, p.second);
  }

  return match;
}

bool EmulationStateARM::LoadRegistersStateFromDictionary(
    OptionValueDictionary *reg_dict, char kind, int first_reg, int num) {
  StreamString sstr;
  for (int i = 0; i < num; ++i) {
    sstr.Clear();
    sstr.Printf("%c%d", kind, i);
    OptionValueSP value_sp = reg_dict->GetValueForKey(sstr.GetString());
    if (value_sp.get() == nullptr)
      return false;
    uint64_t reg_value = value_sp->GetValueAs<uint64_t>().value_or(0);
    StorePseudoRegisterValue(first_reg + i, reg_value);
  }

  return true;
}

bool EmulationStateARM::LoadStateFromDictionary(
    OptionValueDictionary *test_data) {
  static constexpr llvm::StringLiteral memory_key("memory");
  static constexpr llvm::StringLiteral registers_key("registers");

  if (!test_data)
    return false;

  OptionValueSP value_sp = test_data->GetValueForKey(memory_key);

  // Load memory, if present.

  if (value_sp.get() != nullptr) {
    static constexpr llvm::StringLiteral address_key("address");
    static constexpr llvm::StringLiteral data_key("data");
    uint64_t start_address = 0;

    OptionValueDictionary *mem_dict = value_sp->GetAsDictionary();
    value_sp = mem_dict->GetValueForKey(address_key);
    if (value_sp.get() == nullptr)
      return false;
    else
      start_address = value_sp->GetValueAs<uint64_t>().value_or(0);

    value_sp = mem_dict->GetValueForKey(data_key);
    OptionValueArray *mem_array = value_sp->GetAsArray();
    if (!mem_array)
      return false;

    uint32_t num_elts = mem_array->GetSize();
    uint32_t address = (uint32_t)start_address;

    for (uint32_t i = 0; i < num_elts; ++i) {
      value_sp = mem_array->GetValueAtIndex(i);
      if (value_sp.get() == nullptr)
        return false;
      uint64_t value = value_sp->GetValueAs<uint64_t>().value_or(0);
      StoreToPseudoAddress(address, value);
      address = address + 4;
    }
  }

  value_sp = test_data->GetValueForKey(registers_key);
  if (value_sp.get() == nullptr)
    return false;

  // Load General Registers

  OptionValueDictionary *reg_dict = value_sp->GetAsDictionary();
  if (!LoadRegistersStateFromDictionary(reg_dict, 'r', dwarf_r0, 16))
    return false;

  static constexpr llvm::StringLiteral cpsr_name("cpsr");
  value_sp = reg_dict->GetValueForKey(cpsr_name);
  if (value_sp.get() == nullptr)
    return false;
  StorePseudoRegisterValue(dwarf_cpsr,
                           value_sp->GetValueAs<uint64_t>().value_or(0));

  // Load s/d Registers
  // To prevent you giving both types in a state and overwriting
  // one or the other, we'll expect to get either all S registers,
  // or all D registers. Not a mix of the two.
  bool found_s_registers =
      LoadRegistersStateFromDictionary(reg_dict, 's', dwarf_s0, 32);
  bool found_d_registers =
      LoadRegistersStateFromDictionary(reg_dict, 'd', dwarf_d0, 32);

  return found_s_registers != found_d_registers;
}
