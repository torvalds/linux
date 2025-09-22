//===-- UnwindAssemblyInstEmulation.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_UNWINDASSEMBLY_INSTEMULATION_UNWINDASSEMBLYINSTEMULATION_H
#define LLDB_SOURCE_PLUGINS_UNWINDASSEMBLY_INSTEMULATION_UNWINDASSEMBLYINSTEMULATION_H

#include "lldb/Core/EmulateInstruction.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/UnwindAssembly.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/lldb-private.h"

class UnwindAssemblyInstEmulation : public lldb_private::UnwindAssembly {
public:
  ~UnwindAssemblyInstEmulation() override = default;

  bool GetNonCallSiteUnwindPlanFromAssembly(
      lldb_private::AddressRange &func, lldb_private::Thread &thread,
      lldb_private::UnwindPlan &unwind_plan) override;

  bool
  GetNonCallSiteUnwindPlanFromAssembly(lldb_private::AddressRange &func,
                                       uint8_t *opcode_data, size_t opcode_size,
                                       lldb_private::UnwindPlan &unwind_plan);

  bool
  AugmentUnwindPlanFromCallSite(lldb_private::AddressRange &func,
                                lldb_private::Thread &thread,
                                lldb_private::UnwindPlan &unwind_plan) override;

  bool GetFastUnwindPlan(lldb_private::AddressRange &func,
                         lldb_private::Thread &thread,
                         lldb_private::UnwindPlan &unwind_plan) override;

  // thread may be NULL in which case we only use the Target (e.g. if this is
  // called pre-process-launch).
  bool
  FirstNonPrologueInsn(lldb_private::AddressRange &func,
                       const lldb_private::ExecutionContext &exe_ctx,
                       lldb_private::Address &first_non_prologue_insn) override;

  static lldb_private::UnwindAssembly *
  CreateInstance(const lldb_private::ArchSpec &arch);

  // PluginInterface protocol
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "inst-emulation"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

private:
  // Call CreateInstance to get an instance of this class
  UnwindAssemblyInstEmulation(const lldb_private::ArchSpec &arch,
                              lldb_private::EmulateInstruction *inst_emulator)
      : UnwindAssembly(arch), m_inst_emulator_up(inst_emulator),
        m_range_ptr(nullptr), m_unwind_plan_ptr(nullptr), m_curr_row(),
        m_initial_sp(0), m_cfa_reg_info(), m_fp_is_cfa(false),
        m_register_values(), m_pushed_regs(), m_curr_row_modified(false),
        m_forward_branch_offset(0) {
    if (m_inst_emulator_up) {
      m_inst_emulator_up->SetBaton(this);
      m_inst_emulator_up->SetCallbacks(ReadMemory, WriteMemory, ReadRegister,
                                       WriteRegister);
    }
  }

  static size_t
  ReadMemory(lldb_private::EmulateInstruction *instruction, void *baton,
             const lldb_private::EmulateInstruction::Context &context,
             lldb::addr_t addr, void *dst, size_t length);

  static size_t
  WriteMemory(lldb_private::EmulateInstruction *instruction, void *baton,
              const lldb_private::EmulateInstruction::Context &context,
              lldb::addr_t addr, const void *dst, size_t length);

  static bool ReadRegister(lldb_private::EmulateInstruction *instruction,
                           void *baton,
                           const lldb_private::RegisterInfo *reg_info,
                           lldb_private::RegisterValue &reg_value);

  static bool
  WriteRegister(lldb_private::EmulateInstruction *instruction, void *baton,
                const lldb_private::EmulateInstruction::Context &context,
                const lldb_private::RegisterInfo *reg_info,
                const lldb_private::RegisterValue &reg_value);

  //    size_t
  //    ReadMemory (lldb_private::EmulateInstruction *instruction,
  //                const lldb_private::EmulateInstruction::Context &context,
  //                lldb::addr_t addr,
  //                void *dst,
  //                size_t length);

  size_t WriteMemory(lldb_private::EmulateInstruction *instruction,
                     const lldb_private::EmulateInstruction::Context &context,
                     lldb::addr_t addr, const void *dst, size_t length);

  bool ReadRegister(lldb_private::EmulateInstruction *instruction,
                    const lldb_private::RegisterInfo *reg_info,
                    lldb_private::RegisterValue &reg_value);

  bool WriteRegister(lldb_private::EmulateInstruction *instruction,
                     const lldb_private::EmulateInstruction::Context &context,
                     const lldb_private::RegisterInfo *reg_info,
                     const lldb_private::RegisterValue &reg_value);

  static uint64_t
  MakeRegisterKindValuePair(const lldb_private::RegisterInfo &reg_info);

  void SetRegisterValue(const lldb_private::RegisterInfo &reg_info,
                        const lldb_private::RegisterValue &reg_value);

  bool GetRegisterValue(const lldb_private::RegisterInfo &reg_info,
                        lldb_private::RegisterValue &reg_value);

  std::unique_ptr<lldb_private::EmulateInstruction> m_inst_emulator_up;
  lldb_private::AddressRange *m_range_ptr;
  lldb_private::UnwindPlan *m_unwind_plan_ptr;
  lldb_private::UnwindPlan::RowSP m_curr_row;
  typedef std::map<uint64_t, uint64_t> PushedRegisterToAddrMap;
  uint64_t m_initial_sp;
  lldb_private::RegisterInfo m_cfa_reg_info;
  bool m_fp_is_cfa;
  typedef std::map<uint64_t, lldb_private::RegisterValue> RegisterValueMap;
  RegisterValueMap m_register_values;
  PushedRegisterToAddrMap m_pushed_regs;

  // While processing the instruction stream, we need to communicate some state
  // change
  // information up to the higher level loop that makes decisions about how to
  // push
  // the unwind instructions for the UnwindPlan we're constructing.

  // The instruction we're processing updated the UnwindPlan::Row contents
  bool m_curr_row_modified;
  // The instruction is branching forward with the given offset. 0 value means
  // no branching.
  uint32_t m_forward_branch_offset;
};

#endif // LLDB_SOURCE_PLUGINS_UNWINDASSEMBLY_INSTEMULATION_UNWINDASSEMBLYINSTEMULATION_H
