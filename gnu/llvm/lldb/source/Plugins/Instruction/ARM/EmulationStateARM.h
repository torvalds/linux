//===-- EmulationStateARM.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATIONSTATEARM_H
#define LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATIONSTATEARM_H

#include <map>

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Core/Opcode.h"

class EmulationStateARM {
public:
  EmulationStateARM();

  virtual ~EmulationStateARM();

  bool StorePseudoRegisterValue(uint32_t reg_num, uint64_t value);

  uint64_t ReadPseudoRegisterValue(uint32_t reg_num, bool &success);

  bool StoreToPseudoAddress(lldb::addr_t p_address, uint32_t value);

  uint32_t ReadFromPseudoAddress(lldb::addr_t p_address, bool &success);

  void ClearPseudoRegisters();

  void ClearPseudoMemory();

  bool LoadStateFromDictionary(lldb_private::OptionValueDictionary *test_data);

  bool CompareState(EmulationStateARM &other_state,
                    lldb_private::Stream &out_stream);

  static size_t
  ReadPseudoMemory(lldb_private::EmulateInstruction *instruction, void *baton,
                   const lldb_private::EmulateInstruction::Context &context,
                   lldb::addr_t addr, void *dst, size_t length);

  static size_t
  WritePseudoMemory(lldb_private::EmulateInstruction *instruction, void *baton,
                    const lldb_private::EmulateInstruction::Context &context,
                    lldb::addr_t addr, const void *dst, size_t length);

  static bool ReadPseudoRegister(lldb_private::EmulateInstruction *instruction,
                                 void *baton,
                                 const lldb_private::RegisterInfo *reg_info,
                                 lldb_private::RegisterValue &reg_value);

  static bool
  WritePseudoRegister(lldb_private::EmulateInstruction *instruction,
                      void *baton,
                      const lldb_private::EmulateInstruction::Context &context,
                      const lldb_private::RegisterInfo *reg_info,
                      const lldb_private::RegisterValue &reg_value);

private:
  bool LoadRegistersStateFromDictionary(
      lldb_private::OptionValueDictionary *reg_dict, char kind, int first_reg,
      int num);

  uint32_t m_gpr[17] = {0};
  struct _sd_regs {
    uint32_t s_regs[32]; // sregs 0 - 31 & dregs 0 - 15

    uint64_t d_regs[16]; // dregs 16-31

  } m_vfp_regs;

  std::map<lldb::addr_t, uint32_t> m_memory; // Eventually will want to change
                                             // uint32_t to a data buffer heap
                                             // type.

  EmulationStateARM(const EmulationStateARM &) = delete;
  const EmulationStateARM &operator=(const EmulationStateARM &) = delete;
};

#endif // LLDB_SOURCE_PLUGINS_INSTRUCTION_ARM_EMULATIONSTATEARM_H
