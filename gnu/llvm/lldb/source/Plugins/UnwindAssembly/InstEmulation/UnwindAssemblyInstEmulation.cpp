//===-- UnwindAssemblyInstEmulation.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "UnwindAssemblyInstEmulation.h"

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/DumpRegisterValue.h"
#include "lldb/Core/FormatEntity.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(UnwindAssemblyInstEmulation)

//  UnwindAssemblyInstEmulation method definitions

bool UnwindAssemblyInstEmulation::GetNonCallSiteUnwindPlanFromAssembly(
    AddressRange &range, Thread &thread, UnwindPlan &unwind_plan) {
  std::vector<uint8_t> function_text(range.GetByteSize());
  ProcessSP process_sp(thread.GetProcess());
  if (process_sp) {
    Status error;
    const bool force_live_memory = true;
    if (process_sp->GetTarget().ReadMemory(
            range.GetBaseAddress(), function_text.data(), range.GetByteSize(),
            error, force_live_memory) != range.GetByteSize()) {
      return false;
    }
  }
  return GetNonCallSiteUnwindPlanFromAssembly(
      range, function_text.data(), function_text.size(), unwind_plan);
}

bool UnwindAssemblyInstEmulation::GetNonCallSiteUnwindPlanFromAssembly(
    AddressRange &range, uint8_t *opcode_data, size_t opcode_size,
    UnwindPlan &unwind_plan) {
  if (opcode_data == nullptr || opcode_size == 0)
    return false;

  if (range.GetByteSize() > 0 && range.GetBaseAddress().IsValid() &&
      m_inst_emulator_up.get()) {

    // The instruction emulation subclass setup the unwind plan for the first
    // instruction.
    m_inst_emulator_up->CreateFunctionEntryUnwind(unwind_plan);

    // CreateFunctionEntryUnwind should have created the first row. If it
    // doesn't, then we are done.
    if (unwind_plan.GetRowCount() == 0)
      return false;

    const bool prefer_file_cache = true;
    DisassemblerSP disasm_sp(Disassembler::DisassembleBytes(
        m_arch, nullptr, nullptr, range.GetBaseAddress(), opcode_data,
        opcode_size, 99999, prefer_file_cache));

    Log *log = GetLog(LLDBLog::Unwind);

    if (disasm_sp) {

      m_range_ptr = &range;
      m_unwind_plan_ptr = &unwind_plan;

      const uint32_t addr_byte_size = m_arch.GetAddressByteSize();
      const bool show_address = true;
      const bool show_bytes = true;
      const bool show_control_flow_kind = false;
      m_cfa_reg_info = *m_inst_emulator_up->GetRegisterInfo(
          unwind_plan.GetRegisterKind(), unwind_plan.GetInitialCFARegister());
      m_fp_is_cfa = false;
      m_register_values.clear();
      m_pushed_regs.clear();

      // Initialize the CFA with a known value. In the 32 bit case it will be
      // 0x80000000, and in the 64 bit case 0x8000000000000000. We use the
      // address byte size to be safe for any future address sizes
      m_initial_sp = (1ull << ((addr_byte_size * 8) - 1));
      RegisterValue cfa_reg_value;
      cfa_reg_value.SetUInt(m_initial_sp, m_cfa_reg_info.byte_size);
      SetRegisterValue(m_cfa_reg_info, cfa_reg_value);

      const InstructionList &inst_list = disasm_sp->GetInstructionList();
      const size_t num_instructions = inst_list.GetSize();

      if (num_instructions > 0) {
        Instruction *inst = inst_list.GetInstructionAtIndex(0).get();
        const lldb::addr_t base_addr = inst->GetAddress().GetFileAddress();

        // Map for storing the unwind plan row and the value of the registers
        // at a given offset. When we see a forward branch we add a new entry
        // to this map with the actual unwind plan row and register context for
        // the target address of the branch as the current data have to be
        // valid for the target address of the branch too if we are in the same
        // function.
        std::map<lldb::addr_t, std::pair<UnwindPlan::RowSP, RegisterValueMap>>
            saved_unwind_states;

        // Make a copy of the current instruction Row and save it in m_curr_row
        // so we can add updates as we process the instructions.
        UnwindPlan::RowSP last_row = unwind_plan.GetLastRow();
        UnwindPlan::Row *newrow = new UnwindPlan::Row;
        if (last_row.get())
          *newrow = *last_row.get();
        m_curr_row.reset(newrow);

        // Add the initial state to the save list with offset 0.
        saved_unwind_states.insert({0, {last_row, m_register_values}});

        // cache the stack pointer register number (in whatever register
        // numbering this UnwindPlan uses) for quick reference during
        // instruction parsing.
        RegisterInfo sp_reg_info = *m_inst_emulator_up->GetRegisterInfo(
            eRegisterKindGeneric, LLDB_REGNUM_GENERIC_SP);

        // The architecture dependent condition code of the last processed
        // instruction.
        EmulateInstruction::InstructionCondition last_condition =
            EmulateInstruction::UnconditionalCondition;
        lldb::addr_t condition_block_start_offset = 0;

        for (size_t idx = 0; idx < num_instructions; ++idx) {
          m_curr_row_modified = false;
          m_forward_branch_offset = 0;

          inst = inst_list.GetInstructionAtIndex(idx).get();
          if (inst) {
            lldb::addr_t current_offset =
                inst->GetAddress().GetFileAddress() - base_addr;
            auto it = saved_unwind_states.upper_bound(current_offset);
            assert(it != saved_unwind_states.begin() &&
                   "Unwind row for the function entry missing");
            --it; // Move it to the row corresponding to the current offset

            // If the offset of m_curr_row don't match with the offset we see
            // in saved_unwind_states then we have to update m_curr_row and
            // m_register_values based on the saved values. It is happening
            // after we processed an epilogue and a return to caller
            // instruction.
            if (it->second.first->GetOffset() != m_curr_row->GetOffset()) {
              UnwindPlan::Row *newrow = new UnwindPlan::Row;
              *newrow = *it->second.first;
              m_curr_row.reset(newrow);
              m_register_values = it->second.second;
              // re-set the CFA register ivars to match the
              // new m_curr_row.
              if (sp_reg_info.name &&
                  m_curr_row->GetCFAValue().IsRegisterPlusOffset()) {
                uint32_t row_cfa_regnum =
                    m_curr_row->GetCFAValue().GetRegisterNumber();
                lldb::RegisterKind row_kind =
                    m_unwind_plan_ptr->GetRegisterKind();
                // set m_cfa_reg_info to the row's CFA reg.
                m_cfa_reg_info = *m_inst_emulator_up->GetRegisterInfo(
                    row_kind, row_cfa_regnum);
                // set m_fp_is_cfa.
                if (sp_reg_info.kinds[row_kind] == row_cfa_regnum)
                  m_fp_is_cfa = false;
                else
                  m_fp_is_cfa = true;
              }
            }

            m_inst_emulator_up->SetInstruction(inst->GetOpcode(),
                                               inst->GetAddress(), nullptr);

            if (last_condition !=
                m_inst_emulator_up->GetInstructionCondition()) {
              if (m_inst_emulator_up->GetInstructionCondition() !=
                      EmulateInstruction::UnconditionalCondition &&
                  saved_unwind_states.count(current_offset) == 0) {
                // If we don't have a saved row for the current offset then
                // save our current state because we will have to restore it
                // after the conditional block.
                auto new_row =
                    std::make_shared<UnwindPlan::Row>(*m_curr_row.get());
                saved_unwind_states.insert(
                    {current_offset, {new_row, m_register_values}});
              }

              // If the last instruction was conditional with a different
              // condition then the then current condition then restore the
              // condition.
              if (last_condition !=
                  EmulateInstruction::UnconditionalCondition) {
                const auto &saved_state =
                    saved_unwind_states.at(condition_block_start_offset);
                m_curr_row =
                    std::make_shared<UnwindPlan::Row>(*saved_state.first);
                m_curr_row->SetOffset(current_offset);
                m_register_values = saved_state.second;
                // re-set the CFA register ivars to match the
                // new m_curr_row.
                if (sp_reg_info.name &&
                    m_curr_row->GetCFAValue().IsRegisterPlusOffset()) {
                  uint32_t row_cfa_regnum =
                      m_curr_row->GetCFAValue().GetRegisterNumber();
                  lldb::RegisterKind row_kind =
                      m_unwind_plan_ptr->GetRegisterKind();
                  // set m_cfa_reg_info to the row's CFA reg.
                  m_cfa_reg_info = *m_inst_emulator_up->GetRegisterInfo(
                      row_kind, row_cfa_regnum);
                  // set m_fp_is_cfa.
                  if (sp_reg_info.kinds[row_kind] == row_cfa_regnum)
                    m_fp_is_cfa = false;
                  else
                    m_fp_is_cfa = true;
                }
                bool replace_existing =
                    true; // The last instruction might already
                          // created a row for this offset and
                          // we want to overwrite it.
                unwind_plan.InsertRow(
                    std::make_shared<UnwindPlan::Row>(*m_curr_row),
                    replace_existing);
              }

              // We are starting a new conditional block at the actual offset
              condition_block_start_offset = current_offset;
            }

            if (log && log->GetVerbose()) {
              StreamString strm;
              lldb_private::FormatEntity::Entry format;
              FormatEntity::Parse("${frame.pc}: ", format);
              inst->Dump(&strm, inst_list.GetMaxOpcocdeByteSize(), show_address,
                         show_bytes, show_control_flow_kind, nullptr, nullptr,
                         nullptr, &format, 0);
              log->PutString(strm.GetString());
            }

            last_condition = m_inst_emulator_up->GetInstructionCondition();

            m_inst_emulator_up->EvaluateInstruction(
                eEmulateInstructionOptionIgnoreConditions);

            // If the current instruction is a branch forward then save the
            // current CFI information for the offset where we are branching.
            if (m_forward_branch_offset != 0 &&
                range.ContainsFileAddress(inst->GetAddress().GetFileAddress() +
                                          m_forward_branch_offset)) {
              auto newrow =
                  std::make_shared<UnwindPlan::Row>(*m_curr_row.get());
              newrow->SetOffset(current_offset + m_forward_branch_offset);
              saved_unwind_states.insert(
                  {current_offset + m_forward_branch_offset,
                   {newrow, m_register_values}});
              unwind_plan.InsertRow(newrow);
            }

            // Were there any changes to the CFI while evaluating this
            // instruction?
            if (m_curr_row_modified) {
              // Save the modified row if we don't already have a CFI row in
              // the current address
              if (saved_unwind_states.count(
                      current_offset + inst->GetOpcode().GetByteSize()) == 0) {
                m_curr_row->SetOffset(current_offset +
                                      inst->GetOpcode().GetByteSize());
                unwind_plan.InsertRow(m_curr_row);
                saved_unwind_states.insert(
                    {current_offset + inst->GetOpcode().GetByteSize(),
                     {m_curr_row, m_register_values}});

                // Allocate a new Row for m_curr_row, copy the current state
                // into it
                UnwindPlan::Row *newrow = new UnwindPlan::Row;
                *newrow = *m_curr_row.get();
                m_curr_row.reset(newrow);
              }
            }
          }
        }
      }
    }

    if (log && log->GetVerbose()) {
      StreamString strm;
      lldb::addr_t base_addr = range.GetBaseAddress().GetFileAddress();
      strm.Printf("Resulting unwind rows for [0x%" PRIx64 " - 0x%" PRIx64 "):",
                  base_addr, base_addr + range.GetByteSize());
      unwind_plan.Dump(strm, nullptr, base_addr);
      log->PutString(strm.GetString());
    }
    return unwind_plan.GetRowCount() > 0;
  }
  return false;
}

bool UnwindAssemblyInstEmulation::AugmentUnwindPlanFromCallSite(
    AddressRange &func, Thread &thread, UnwindPlan &unwind_plan) {
  return false;
}

bool UnwindAssemblyInstEmulation::GetFastUnwindPlan(AddressRange &func,
                                                    Thread &thread,
                                                    UnwindPlan &unwind_plan) {
  return false;
}

bool UnwindAssemblyInstEmulation::FirstNonPrologueInsn(
    AddressRange &func, const ExecutionContext &exe_ctx,
    Address &first_non_prologue_insn) {
  return false;
}

UnwindAssembly *
UnwindAssemblyInstEmulation::CreateInstance(const ArchSpec &arch) {
  std::unique_ptr<EmulateInstruction> inst_emulator_up(
      EmulateInstruction::FindPlugin(arch, eInstructionTypePrologueEpilogue,
                                     nullptr));
  // Make sure that all prologue instructions are handled
  if (inst_emulator_up)
    return new UnwindAssemblyInstEmulation(arch, inst_emulator_up.release());
  return nullptr;
}

void UnwindAssemblyInstEmulation::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void UnwindAssemblyInstEmulation::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef UnwindAssemblyInstEmulation::GetPluginDescriptionStatic() {
  return "Instruction emulation based unwind information.";
}

uint64_t UnwindAssemblyInstEmulation::MakeRegisterKindValuePair(
    const RegisterInfo &reg_info) {
  lldb::RegisterKind reg_kind;
  uint32_t reg_num;
  if (EmulateInstruction::GetBestRegisterKindAndNumber(&reg_info, reg_kind,
                                                       reg_num))
    return (uint64_t)reg_kind << 24 | reg_num;
  return 0ull;
}

void UnwindAssemblyInstEmulation::SetRegisterValue(
    const RegisterInfo &reg_info, const RegisterValue &reg_value) {
  m_register_values[MakeRegisterKindValuePair(reg_info)] = reg_value;
}

bool UnwindAssemblyInstEmulation::GetRegisterValue(const RegisterInfo &reg_info,
                                                   RegisterValue &reg_value) {
  const uint64_t reg_id = MakeRegisterKindValuePair(reg_info);
  RegisterValueMap::const_iterator pos = m_register_values.find(reg_id);
  if (pos != m_register_values.end()) {
    reg_value = pos->second;
    return true; // We had a real value that comes from an opcode that wrote
                 // to it...
  }
  // We are making up a value that is recognizable...
  reg_value.SetUInt(reg_id, reg_info.byte_size);
  return false;
}

size_t UnwindAssemblyInstEmulation::ReadMemory(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context, lldb::addr_t addr, void *dst,
    size_t dst_len) {
  Log *log = GetLog(LLDBLog::Unwind);

  if (log && log->GetVerbose()) {
    StreamString strm;
    strm.Printf(
        "UnwindAssemblyInstEmulation::ReadMemory    (addr = 0x%16.16" PRIx64
        ", dst = %p, dst_len = %" PRIu64 ", context = ",
        addr, dst, (uint64_t)dst_len);
    context.Dump(strm, instruction);
    log->PutString(strm.GetString());
  }
  memset(dst, 0, dst_len);
  return dst_len;
}

size_t UnwindAssemblyInstEmulation::WriteMemory(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context, lldb::addr_t addr,
    const void *dst, size_t dst_len) {
  if (baton && dst && dst_len)
    return ((UnwindAssemblyInstEmulation *)baton)
        ->WriteMemory(instruction, context, addr, dst, dst_len);
  return 0;
}

size_t UnwindAssemblyInstEmulation::WriteMemory(
    EmulateInstruction *instruction, const EmulateInstruction::Context &context,
    lldb::addr_t addr, const void *dst, size_t dst_len) {
  DataExtractor data(dst, dst_len,
                     instruction->GetArchitecture().GetByteOrder(),
                     instruction->GetArchitecture().GetAddressByteSize());

  Log *log = GetLog(LLDBLog::Unwind);

  if (log && log->GetVerbose()) {
    StreamString strm;

    strm.PutCString("UnwindAssemblyInstEmulation::WriteMemory   (");
    DumpDataExtractor(data, &strm, 0, eFormatBytes, 1, dst_len, UINT32_MAX,
                      addr, 0, 0);
    strm.PutCString(", context = ");
    context.Dump(strm, instruction);
    log->PutString(strm.GetString());
  }

  switch (context.type) {
  default:
  case EmulateInstruction::eContextInvalid:
  case EmulateInstruction::eContextReadOpcode:
  case EmulateInstruction::eContextImmediate:
  case EmulateInstruction::eContextAdjustBaseRegister:
  case EmulateInstruction::eContextRegisterPlusOffset:
  case EmulateInstruction::eContextAdjustPC:
  case EmulateInstruction::eContextRegisterStore:
  case EmulateInstruction::eContextRegisterLoad:
  case EmulateInstruction::eContextRelativeBranchImmediate:
  case EmulateInstruction::eContextAbsoluteBranchRegister:
  case EmulateInstruction::eContextSupervisorCall:
  case EmulateInstruction::eContextTableBranchReadMemory:
  case EmulateInstruction::eContextWriteRegisterRandomBits:
  case EmulateInstruction::eContextWriteMemoryRandomBits:
  case EmulateInstruction::eContextArithmetic:
  case EmulateInstruction::eContextAdvancePC:
  case EmulateInstruction::eContextReturnFromException:
  case EmulateInstruction::eContextPopRegisterOffStack:
  case EmulateInstruction::eContextAdjustStackPointer:
    break;

  case EmulateInstruction::eContextPushRegisterOnStack: {
    uint32_t reg_num = LLDB_INVALID_REGNUM;
    uint32_t generic_regnum = LLDB_INVALID_REGNUM;
    assert(context.GetInfoType() ==
               EmulateInstruction::eInfoTypeRegisterToRegisterPlusOffset &&
           "unhandled case, add code to handle this!");
    const uint32_t unwind_reg_kind = m_unwind_plan_ptr->GetRegisterKind();
    reg_num = context.info.RegisterToRegisterPlusOffset.data_reg
                  .kinds[unwind_reg_kind];
    generic_regnum = context.info.RegisterToRegisterPlusOffset.data_reg
                         .kinds[eRegisterKindGeneric];

    if (reg_num != LLDB_INVALID_REGNUM &&
        generic_regnum != LLDB_REGNUM_GENERIC_SP) {
      if (m_pushed_regs.find(reg_num) == m_pushed_regs.end()) {
        m_pushed_regs[reg_num] = addr;
        const int32_t offset = addr - m_initial_sp;
        m_curr_row->SetRegisterLocationToAtCFAPlusOffset(reg_num, offset,
                                                         /*can_replace=*/true);
        m_curr_row_modified = true;
      }
    }
  } break;
  }

  return dst_len;
}

bool UnwindAssemblyInstEmulation::ReadRegister(EmulateInstruction *instruction,
                                               void *baton,
                                               const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {

  if (baton && reg_info)
    return ((UnwindAssemblyInstEmulation *)baton)
        ->ReadRegister(instruction, reg_info, reg_value);
  return false;
}
bool UnwindAssemblyInstEmulation::ReadRegister(EmulateInstruction *instruction,
                                               const RegisterInfo *reg_info,
                                               RegisterValue &reg_value) {
  bool synthetic = GetRegisterValue(*reg_info, reg_value);

  Log *log = GetLog(LLDBLog::Unwind);

  if (log && log->GetVerbose()) {

    StreamString strm;
    strm.Printf("UnwindAssemblyInstEmulation::ReadRegister  (name = \"%s\") => "
                "synthetic_value = %i, value = ",
                reg_info->name, synthetic);
    DumpRegisterValue(reg_value, strm, *reg_info, false, false, eFormatDefault);
    log->PutString(strm.GetString());
  }
  return true;
}

bool UnwindAssemblyInstEmulation::WriteRegister(
    EmulateInstruction *instruction, void *baton,
    const EmulateInstruction::Context &context, const RegisterInfo *reg_info,
    const RegisterValue &reg_value) {
  if (baton && reg_info)
    return ((UnwindAssemblyInstEmulation *)baton)
        ->WriteRegister(instruction, context, reg_info, reg_value);
  return false;
}
bool UnwindAssemblyInstEmulation::WriteRegister(
    EmulateInstruction *instruction, const EmulateInstruction::Context &context,
    const RegisterInfo *reg_info, const RegisterValue &reg_value) {
  Log *log = GetLog(LLDBLog::Unwind);

  if (log && log->GetVerbose()) {

    StreamString strm;
    strm.Printf(
        "UnwindAssemblyInstEmulation::WriteRegister (name = \"%s\", value = ",
        reg_info->name);
    DumpRegisterValue(reg_value, strm, *reg_info, false, false, eFormatDefault);
    strm.PutCString(", context = ");
    context.Dump(strm, instruction);
    log->PutString(strm.GetString());
  }

  SetRegisterValue(*reg_info, reg_value);

  switch (context.type) {
  case EmulateInstruction::eContextInvalid:
  case EmulateInstruction::eContextReadOpcode:
  case EmulateInstruction::eContextImmediate:
  case EmulateInstruction::eContextAdjustBaseRegister:
  case EmulateInstruction::eContextRegisterPlusOffset:
  case EmulateInstruction::eContextAdjustPC:
  case EmulateInstruction::eContextRegisterStore:
  case EmulateInstruction::eContextSupervisorCall:
  case EmulateInstruction::eContextTableBranchReadMemory:
  case EmulateInstruction::eContextWriteRegisterRandomBits:
  case EmulateInstruction::eContextWriteMemoryRandomBits:
  case EmulateInstruction::eContextAdvancePC:
  case EmulateInstruction::eContextReturnFromException:
  case EmulateInstruction::eContextPushRegisterOnStack:
  case EmulateInstruction::eContextRegisterLoad:
    //            {
    //                const uint32_t reg_num =
    //                reg_info->kinds[m_unwind_plan_ptr->GetRegisterKind()];
    //                if (reg_num != LLDB_INVALID_REGNUM)
    //                {
    //                    const bool can_replace_only_if_unspecified = true;
    //
    //                    m_curr_row.SetRegisterLocationToUndefined (reg_num,
    //                                                               can_replace_only_if_unspecified,
    //                                                               can_replace_only_if_unspecified);
    //                    m_curr_row_modified = true;
    //                }
    //            }
    break;

  case EmulateInstruction::eContextArithmetic: {
    // If we adjusted the current frame pointer by a constant then adjust the
    // CFA offset
    // with the same amount.
    lldb::RegisterKind kind = m_unwind_plan_ptr->GetRegisterKind();
    if (m_fp_is_cfa && reg_info->kinds[kind] == m_cfa_reg_info.kinds[kind] &&
        context.GetInfoType() ==
            EmulateInstruction::eInfoTypeRegisterPlusOffset &&
        context.info.RegisterPlusOffset.reg.kinds[kind] ==
            m_cfa_reg_info.kinds[kind]) {
      const int64_t offset = context.info.RegisterPlusOffset.signed_offset;
      m_curr_row->GetCFAValue().IncOffset(-1 * offset);
      m_curr_row_modified = true;
    }
  } break;

  case EmulateInstruction::eContextAbsoluteBranchRegister:
  case EmulateInstruction::eContextRelativeBranchImmediate: {
    if (context.GetInfoType() == EmulateInstruction::eInfoTypeISAAndImmediate &&
        context.info.ISAAndImmediate.unsigned_data32 > 0) {
      m_forward_branch_offset =
          context.info.ISAAndImmediateSigned.signed_data32;
    } else if (context.GetInfoType() ==
                   EmulateInstruction::eInfoTypeISAAndImmediateSigned &&
               context.info.ISAAndImmediateSigned.signed_data32 > 0) {
      m_forward_branch_offset = context.info.ISAAndImmediate.unsigned_data32;
    } else if (context.GetInfoType() ==
                   EmulateInstruction::eInfoTypeImmediate &&
               context.info.unsigned_immediate > 0) {
      m_forward_branch_offset = context.info.unsigned_immediate;
    } else if (context.GetInfoType() ==
                   EmulateInstruction::eInfoTypeImmediateSigned &&
               context.info.signed_immediate > 0) {
      m_forward_branch_offset = context.info.signed_immediate;
    }
  } break;

  case EmulateInstruction::eContextPopRegisterOffStack: {
    const uint32_t reg_num =
        reg_info->kinds[m_unwind_plan_ptr->GetRegisterKind()];
    const uint32_t generic_regnum = reg_info->kinds[eRegisterKindGeneric];
    if (reg_num != LLDB_INVALID_REGNUM &&
        generic_regnum != LLDB_REGNUM_GENERIC_SP) {
      switch (context.GetInfoType()) {
      case EmulateInstruction::eInfoTypeAddress:
        if (m_pushed_regs.find(reg_num) != m_pushed_regs.end() &&
            context.info.address == m_pushed_regs[reg_num]) {
          m_curr_row->SetRegisterLocationToSame(reg_num,
                                                false /*must_replace*/);
          m_curr_row_modified = true;

          // FP has been restored to its original value, we are back
          // to using SP to calculate the CFA.
          if (m_fp_is_cfa) {
            m_fp_is_cfa = false;
            lldb::RegisterKind sp_reg_kind = eRegisterKindGeneric;
            uint32_t sp_reg_num = LLDB_REGNUM_GENERIC_SP;
            RegisterInfo sp_reg_info =
                *m_inst_emulator_up->GetRegisterInfo(sp_reg_kind, sp_reg_num);
            RegisterValue sp_reg_val;
            if (GetRegisterValue(sp_reg_info, sp_reg_val)) {
              m_cfa_reg_info = sp_reg_info;
              const uint32_t cfa_reg_num =
                  sp_reg_info.kinds[m_unwind_plan_ptr->GetRegisterKind()];
              assert(cfa_reg_num != LLDB_INVALID_REGNUM);
              m_curr_row->GetCFAValue().SetIsRegisterPlusOffset(
                  cfa_reg_num, m_initial_sp - sp_reg_val.GetAsUInt64());
            }
          }
        }
        break;
      case EmulateInstruction::eInfoTypeISA:
        assert(
            (generic_regnum == LLDB_REGNUM_GENERIC_PC ||
             generic_regnum == LLDB_REGNUM_GENERIC_FLAGS) &&
            "eInfoTypeISA used for popping a register other the PC/FLAGS");
        if (generic_regnum != LLDB_REGNUM_GENERIC_FLAGS) {
          m_curr_row->SetRegisterLocationToSame(reg_num,
                                                false /*must_replace*/);
          m_curr_row_modified = true;
        }
        break;
      default:
        assert(false && "unhandled case, add code to handle this!");
        break;
      }
    }
  } break;

  case EmulateInstruction::eContextSetFramePointer:
    if (!m_fp_is_cfa) {
      m_fp_is_cfa = true;
      m_cfa_reg_info = *reg_info;
      const uint32_t cfa_reg_num =
          reg_info->kinds[m_unwind_plan_ptr->GetRegisterKind()];
      assert(cfa_reg_num != LLDB_INVALID_REGNUM);
      m_curr_row->GetCFAValue().SetIsRegisterPlusOffset(
          cfa_reg_num, m_initial_sp - reg_value.GetAsUInt64());
      m_curr_row_modified = true;
    }
    break;

  case EmulateInstruction::eContextRestoreStackPointer:
    if (m_fp_is_cfa) {
      m_fp_is_cfa = false;
      m_cfa_reg_info = *reg_info;
      const uint32_t cfa_reg_num =
          reg_info->kinds[m_unwind_plan_ptr->GetRegisterKind()];
      assert(cfa_reg_num != LLDB_INVALID_REGNUM);
      m_curr_row->GetCFAValue().SetIsRegisterPlusOffset(
          cfa_reg_num, m_initial_sp - reg_value.GetAsUInt64());
      m_curr_row_modified = true;
    }
    break;

  case EmulateInstruction::eContextAdjustStackPointer:
    // If we have created a frame using the frame pointer, don't follow
    // subsequent adjustments to the stack pointer.
    if (!m_fp_is_cfa) {
      m_curr_row->GetCFAValue().SetIsRegisterPlusOffset(
          m_curr_row->GetCFAValue().GetRegisterNumber(),
          m_initial_sp - reg_value.GetAsUInt64());
      m_curr_row_modified = true;
    }
    break;
  }
  return true;
}
