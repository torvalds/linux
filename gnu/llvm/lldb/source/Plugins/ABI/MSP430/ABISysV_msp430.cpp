//===-- ABISysV_msp430.cpp --------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ABISysV_msp430.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/Core/ValueObjectRegister.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/RegisterValue.h"

#include "llvm/IR/DerivedTypes.h"
#include "llvm/TargetParser/Triple.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_msp430, ABIMSP430)

enum dwarf_regnums {
  dwarf_pc = 0,
  dwarf_sp,
  dwarf_r2,
  dwarf_r3,
  dwarf_fp,
  dwarf_r5,
  dwarf_r6,
  dwarf_r7,
  dwarf_r8,
  dwarf_r9,
  dwarf_r10,
  dwarf_r11,
  dwarf_r12,
  dwarf_r13,
  dwarf_r14,
  dwarf_r15,
};

static const RegisterInfo g_register_infos[] = {
    {"r0",
     "pc",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_pc, dwarf_pc, LLDB_REGNUM_GENERIC_PC, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r1",
     "sp",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_sp, dwarf_sp, LLDB_REGNUM_GENERIC_SP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r2",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r2, dwarf_r2, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r3",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r3, dwarf_r3, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r4",
     "fp",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_fp, dwarf_fp, LLDB_REGNUM_GENERIC_FP, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r5",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r5, dwarf_r5, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r6",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r6, dwarf_r6, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r7",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r7, dwarf_r7, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r8",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r8, dwarf_r8, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r9",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r9, dwarf_r9, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r10",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r10, dwarf_r10, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r11",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r11, dwarf_r11, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r12",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r12, dwarf_r12, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r13",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r13, dwarf_r13, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r14",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r14, dwarf_r14, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    },
    {"r15",
     "",
     2,
     0,
     eEncodingUint,
     eFormatHex,
     {dwarf_r15, dwarf_r15, LLDB_INVALID_REGNUM, LLDB_INVALID_REGNUM,
      LLDB_INVALID_REGNUM},
     nullptr,
     nullptr,
     nullptr,
    }};

static const uint32_t k_num_register_infos =
    sizeof(g_register_infos) / sizeof(RegisterInfo);

const lldb_private::RegisterInfo *
ABISysV_msp430::GetRegisterInfoArray(uint32_t &count) {
  // Make the C-string names and alt_names for the register infos into const
  // C-string values by having the ConstString unique the names in the global
  // constant C-string pool.
  count = k_num_register_infos;
  return g_register_infos;
}

size_t ABISysV_msp430::GetRedZoneSize() const { return 0; }

//------------------------------------------------------------------
// Static Functions
//------------------------------------------------------------------

ABISP
ABISysV_msp430::CreateInstance(lldb::ProcessSP process_sp,
                               const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::msp430) {
    return ABISP(
        new ABISysV_msp430(std::move(process_sp), MakeMCRegisterInfo(arch)));
  }
  return ABISP();
}

bool ABISysV_msp430::PrepareTrivialCall(Thread &thread, lldb::addr_t sp,
                                        lldb::addr_t pc, lldb::addr_t ra,
                                        llvm::ArrayRef<addr_t> args) const {
  // we don't use the traditional trivial call specialized for jit
  return false;
}

bool ABISysV_msp430::GetArgumentValues(Thread &thread,
                                       ValueList &values) const {
  return false;
}

Status ABISysV_msp430::SetReturnValueObject(lldb::StackFrameSP &frame_sp,
                                            lldb::ValueObjectSP &new_value_sp) {
  return Status();
}

ValueObjectSP ABISysV_msp430::GetReturnValueObjectSimple(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

ValueObjectSP ABISysV_msp430::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  return return_valobj_sp;
}

// called when we are on the first instruction of a new function
bool ABISysV_msp430::CreateFunctionEntryUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t sp_reg_num = dwarf_sp;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 2);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -2, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("msp430 at-func-entry default");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  return true;
}

bool ABISysV_msp430::CreateDefaultUnwindPlan(UnwindPlan &unwind_plan) {
  unwind_plan.Clear();
  unwind_plan.SetRegisterKind(eRegisterKindDWARF);

  uint32_t fp_reg_num = dwarf_fp;
  uint32_t sp_reg_num = dwarf_sp;
  uint32_t pc_reg_num = dwarf_pc;

  UnwindPlan::RowSP row(new UnwindPlan::Row);
  row->GetCFAValue().SetIsRegisterPlusOffset(sp_reg_num, 2);
  row->SetRegisterLocationToAtCFAPlusOffset(pc_reg_num, -2, true);
  row->SetRegisterLocationToIsCFAPlusOffset(sp_reg_num, 0, true);
  row->SetRegisterLocationToUnspecified(fp_reg_num, true);

  unwind_plan.AppendRow(row);
  unwind_plan.SetSourceName("msp430 default unwind plan");
  unwind_plan.SetSourcedFromCompiler(eLazyBoolNo);
  unwind_plan.SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  return true;
}

bool ABISysV_msp430::RegisterIsVolatile(const RegisterInfo *reg_info) {
  return !RegisterIsCalleeSaved(reg_info);
}

bool ABISysV_msp430::RegisterIsCalleeSaved(const RegisterInfo *reg_info) {
  int reg = ((reg_info->byte_offset) / 2);

  bool save = (reg >= 4) && (reg <= 10);
  return save;
}

void ABISysV_msp430::Initialize(void) {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for msp430 targets", CreateInstance);
}

void ABISysV_msp430::Terminate(void) {
  PluginManager::UnregisterPlugin(CreateInstance);
}
