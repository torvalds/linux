//===-- FuncUnwinders.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/FuncUnwinders.h"
#include "lldb/Core/Address.h"
#include "lldb/Core/AddressRange.h"
#include "lldb/Symbol/ArmUnwindInfo.h"
#include "lldb/Symbol/CallFrameInfo.h"
#include "lldb/Symbol/CompactUnwindInfo.h"
#include "lldb/Symbol/DWARFCallFrameInfo.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Symbol/UnwindTable.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/RegisterNumber.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/UnwindAssembly.h"

#include <memory>

using namespace lldb;
using namespace lldb_private;

/// constructor

FuncUnwinders::FuncUnwinders(UnwindTable &unwind_table, AddressRange range)
    : m_unwind_table(unwind_table), m_range(range), m_mutex(),
      m_unwind_plan_assembly_sp(), m_unwind_plan_eh_frame_sp(),
      m_unwind_plan_eh_frame_augmented_sp(), m_unwind_plan_compact_unwind(),
      m_unwind_plan_arm_unwind_sp(), m_unwind_plan_fast_sp(),
      m_unwind_plan_arch_default_sp(),
      m_unwind_plan_arch_default_at_func_entry_sp(),
      m_tried_unwind_plan_assembly(false), m_tried_unwind_plan_eh_frame(false),
      m_tried_unwind_plan_object_file(false),
      m_tried_unwind_plan_debug_frame(false),
      m_tried_unwind_plan_object_file_augmented(false),
      m_tried_unwind_plan_eh_frame_augmented(false),
      m_tried_unwind_plan_debug_frame_augmented(false),
      m_tried_unwind_plan_compact_unwind(false),
      m_tried_unwind_plan_arm_unwind(false),
      m_tried_unwind_plan_symbol_file(false), m_tried_unwind_fast(false),
      m_tried_unwind_arch_default(false),
      m_tried_unwind_arch_default_at_func_entry(false),
      m_first_non_prologue_insn() {}

/// destructor

FuncUnwinders::~FuncUnwinders() = default;

UnwindPlanSP FuncUnwinders::GetUnwindPlanAtCallSite(Target &target,
                                                    Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);

  if (UnwindPlanSP plan_sp = GetObjectFileUnwindPlan(target))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetSymbolFileUnwindPlan(thread))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetDebugFrameUnwindPlan(target))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetEHFrameUnwindPlan(target))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetCompactUnwindUnwindPlan(target))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetArmUnwindUnwindPlan(target))
    return plan_sp;

  return nullptr;
}

UnwindPlanSP FuncUnwinders::GetCompactUnwindUnwindPlan(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_compact_unwind.size() > 0)
    return m_unwind_plan_compact_unwind[0]; // FIXME support multiple compact
                                            // unwind plans for one func
  if (m_tried_unwind_plan_compact_unwind)
    return UnwindPlanSP();

  m_tried_unwind_plan_compact_unwind = true;
  if (m_range.GetBaseAddress().IsValid()) {
    Address current_pc(m_range.GetBaseAddress());
    CompactUnwindInfo *compact_unwind = m_unwind_table.GetCompactUnwindInfo();
    if (compact_unwind) {
      UnwindPlanSP unwind_plan_sp(new UnwindPlan(lldb::eRegisterKindGeneric));
      if (compact_unwind->GetUnwindPlan(target, current_pc, *unwind_plan_sp)) {
        m_unwind_plan_compact_unwind.push_back(unwind_plan_sp);
        return m_unwind_plan_compact_unwind[0]; // FIXME support multiple
                                                // compact unwind plans for one
                                                // func
      }
    }
  }
  return UnwindPlanSP();
}

lldb::UnwindPlanSP FuncUnwinders::GetObjectFileUnwindPlan(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_object_file_sp.get() ||
      m_tried_unwind_plan_object_file)
    return m_unwind_plan_object_file_sp;

  m_tried_unwind_plan_object_file = true;
  if (m_range.GetBaseAddress().IsValid()) {
    CallFrameInfo *object_file_frame = m_unwind_table.GetObjectFileUnwindInfo();
    if (object_file_frame) {
      m_unwind_plan_object_file_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!object_file_frame->GetUnwindPlan(m_range,
                                            *m_unwind_plan_object_file_sp))
        m_unwind_plan_object_file_sp.reset();
    }
  }
  return m_unwind_plan_object_file_sp;
}

UnwindPlanSP FuncUnwinders::GetEHFrameUnwindPlan(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_eh_frame_sp.get() || m_tried_unwind_plan_eh_frame)
    return m_unwind_plan_eh_frame_sp;

  m_tried_unwind_plan_eh_frame = true;
  if (m_range.GetBaseAddress().IsValid()) {
    DWARFCallFrameInfo *eh_frame = m_unwind_table.GetEHFrameInfo();
    if (eh_frame) {
      m_unwind_plan_eh_frame_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!eh_frame->GetUnwindPlan(m_range, *m_unwind_plan_eh_frame_sp))
        m_unwind_plan_eh_frame_sp.reset();
    }
  }
  return m_unwind_plan_eh_frame_sp;
}

UnwindPlanSP FuncUnwinders::GetDebugFrameUnwindPlan(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_debug_frame_sp || m_tried_unwind_plan_debug_frame)
    return m_unwind_plan_debug_frame_sp;

  m_tried_unwind_plan_debug_frame = true;
  if (m_range.GetBaseAddress().IsValid()) {
    DWARFCallFrameInfo *debug_frame = m_unwind_table.GetDebugFrameInfo();
    if (debug_frame) {
      m_unwind_plan_debug_frame_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!debug_frame->GetUnwindPlan(m_range, *m_unwind_plan_debug_frame_sp))
        m_unwind_plan_debug_frame_sp.reset();
    }
  }
  return m_unwind_plan_debug_frame_sp;
}

UnwindPlanSP FuncUnwinders::GetArmUnwindUnwindPlan(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_arm_unwind_sp.get() || m_tried_unwind_plan_arm_unwind)
    return m_unwind_plan_arm_unwind_sp;

  m_tried_unwind_plan_arm_unwind = true;
  if (m_range.GetBaseAddress().IsValid()) {
    Address current_pc(m_range.GetBaseAddress());
    ArmUnwindInfo *arm_unwind_info = m_unwind_table.GetArmUnwindInfo();
    if (arm_unwind_info) {
      m_unwind_plan_arm_unwind_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!arm_unwind_info->GetUnwindPlan(target, current_pc,
                                          *m_unwind_plan_arm_unwind_sp))
        m_unwind_plan_arm_unwind_sp.reset();
    }
  }
  return m_unwind_plan_arm_unwind_sp;
}

namespace {
class RegisterContextToInfo: public SymbolFile::RegisterInfoResolver {
public:
  RegisterContextToInfo(RegisterContext &ctx) : m_ctx(ctx) {}

  const RegisterInfo *ResolveName(llvm::StringRef name) const override {
    return m_ctx.GetRegisterInfoByName(name);
  }
  const RegisterInfo *ResolveNumber(lldb::RegisterKind kind,
                                    uint32_t number) const override {
    return m_ctx.GetRegisterInfo(kind, number);
  }

private:
  RegisterContext &m_ctx;
};
} // namespace

UnwindPlanSP FuncUnwinders::GetSymbolFileUnwindPlan(Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_symbol_file_sp.get() || m_tried_unwind_plan_symbol_file)
    return m_unwind_plan_symbol_file_sp;

  m_tried_unwind_plan_symbol_file = true;
  if (SymbolFile *symfile = m_unwind_table.GetSymbolFile()) {
    m_unwind_plan_symbol_file_sp = symfile->GetUnwindPlan(
        m_range.GetBaseAddress(),
        RegisterContextToInfo(*thread.GetRegisterContext()));
  }
  return m_unwind_plan_symbol_file_sp;
}

UnwindPlanSP
FuncUnwinders::GetObjectFileAugmentedUnwindPlan(Target &target,
                                                     Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_object_file_augmented_sp.get() ||
      m_tried_unwind_plan_object_file_augmented)
    return m_unwind_plan_object_file_augmented_sp;

  m_tried_unwind_plan_object_file_augmented = true;

  UnwindPlanSP object_file_unwind_plan = GetObjectFileUnwindPlan(target);
  if (!object_file_unwind_plan)
    return m_unwind_plan_object_file_augmented_sp;

  m_unwind_plan_object_file_augmented_sp =
      std::make_shared<UnwindPlan>(*object_file_unwind_plan);

  // Augment the instructions with epilogue descriptions if necessary
  // so the UnwindPlan can be used at any instruction in the function.

  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp) {
    if (!assembly_profiler_sp->AugmentUnwindPlanFromCallSite(
            m_range, thread, *m_unwind_plan_object_file_augmented_sp)) {
      m_unwind_plan_object_file_augmented_sp.reset();
    }
  } else {
    m_unwind_plan_object_file_augmented_sp.reset();
  }
  return m_unwind_plan_object_file_augmented_sp;
}

UnwindPlanSP FuncUnwinders::GetEHFrameAugmentedUnwindPlan(Target &target,
                                                          Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_eh_frame_augmented_sp.get() ||
      m_tried_unwind_plan_eh_frame_augmented)
    return m_unwind_plan_eh_frame_augmented_sp;

  // Only supported on x86 architectures where we get eh_frame from the
  // compiler that describes the prologue instructions perfectly, and sometimes
  // the epilogue instructions too.
  if (target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_32_i386 &&
      target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_64_x86_64 &&
      target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_64_x86_64h) {
    m_tried_unwind_plan_eh_frame_augmented = true;
    return m_unwind_plan_eh_frame_augmented_sp;
  }

  m_tried_unwind_plan_eh_frame_augmented = true;

  UnwindPlanSP eh_frame_plan = GetEHFrameUnwindPlan(target);
  if (!eh_frame_plan)
    return m_unwind_plan_eh_frame_augmented_sp;

  m_unwind_plan_eh_frame_augmented_sp =
      std::make_shared<UnwindPlan>(*eh_frame_plan);

  // Augment the eh_frame instructions with epilogue descriptions if necessary
  // so the UnwindPlan can be used at any instruction in the function.

  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp) {
    if (!assembly_profiler_sp->AugmentUnwindPlanFromCallSite(
            m_range, thread, *m_unwind_plan_eh_frame_augmented_sp)) {
      m_unwind_plan_eh_frame_augmented_sp.reset();
    }
  } else {
    m_unwind_plan_eh_frame_augmented_sp.reset();
  }
  return m_unwind_plan_eh_frame_augmented_sp;
}

UnwindPlanSP FuncUnwinders::GetDebugFrameAugmentedUnwindPlan(Target &target,
                                                             Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_debug_frame_augmented_sp.get() ||
      m_tried_unwind_plan_debug_frame_augmented)
    return m_unwind_plan_debug_frame_augmented_sp;

  // Only supported on x86 architectures where we get debug_frame from the
  // compiler that describes the prologue instructions perfectly, and sometimes
  // the epilogue instructions too.
  if (target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_32_i386 &&
      target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_64_x86_64 &&
      target.GetArchitecture().GetCore() != ArchSpec::eCore_x86_64_x86_64h) {
    m_tried_unwind_plan_debug_frame_augmented = true;
    return m_unwind_plan_debug_frame_augmented_sp;
  }

  m_tried_unwind_plan_debug_frame_augmented = true;

  UnwindPlanSP debug_frame_plan = GetDebugFrameUnwindPlan(target);
  if (!debug_frame_plan)
    return m_unwind_plan_debug_frame_augmented_sp;

  m_unwind_plan_debug_frame_augmented_sp =
      std::make_shared<UnwindPlan>(*debug_frame_plan);

  // Augment the debug_frame instructions with epilogue descriptions if
  // necessary so the UnwindPlan can be used at any instruction in the
  // function.

  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp) {
    if (!assembly_profiler_sp->AugmentUnwindPlanFromCallSite(
            m_range, thread, *m_unwind_plan_debug_frame_augmented_sp)) {
      m_unwind_plan_debug_frame_augmented_sp.reset();
    }
  } else
    m_unwind_plan_debug_frame_augmented_sp.reset();
  return m_unwind_plan_debug_frame_augmented_sp;
}

UnwindPlanSP FuncUnwinders::GetAssemblyUnwindPlan(Target &target,
                                                  Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_assembly_sp.get() || m_tried_unwind_plan_assembly ||
      !m_unwind_table.GetAllowAssemblyEmulationUnwindPlans()) {
    return m_unwind_plan_assembly_sp;
  }

  m_tried_unwind_plan_assembly = true;

  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp) {
    m_unwind_plan_assembly_sp =
        std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
    if (!assembly_profiler_sp->GetNonCallSiteUnwindPlanFromAssembly(
            m_range, thread, *m_unwind_plan_assembly_sp)) {
      m_unwind_plan_assembly_sp.reset();
    }
  }
  return m_unwind_plan_assembly_sp;
}

// This method compares the pc unwind rule in the first row of two UnwindPlans.
// If they have the same way of getting the pc value (e.g. "CFA - 8" + "CFA is
// sp"), then it will return LazyBoolTrue.
LazyBool FuncUnwinders::CompareUnwindPlansForIdenticalInitialPCLocation(
    Thread &thread, const UnwindPlanSP &a, const UnwindPlanSP &b) {
  LazyBool plans_are_identical = eLazyBoolCalculate;

  RegisterNumber pc_reg(thread, eRegisterKindGeneric, LLDB_REGNUM_GENERIC_PC);
  uint32_t pc_reg_lldb_regnum = pc_reg.GetAsKind(eRegisterKindLLDB);

  if (a.get() && b.get()) {
    UnwindPlan::RowSP a_first_row = a->GetRowAtIndex(0);
    UnwindPlan::RowSP b_first_row = b->GetRowAtIndex(0);

    if (a_first_row.get() && b_first_row.get()) {
      UnwindPlan::Row::RegisterLocation a_pc_regloc;
      UnwindPlan::Row::RegisterLocation b_pc_regloc;

      a_first_row->GetRegisterInfo(pc_reg_lldb_regnum, a_pc_regloc);
      b_first_row->GetRegisterInfo(pc_reg_lldb_regnum, b_pc_regloc);

      plans_are_identical = eLazyBoolYes;

      if (a_first_row->GetCFAValue() != b_first_row->GetCFAValue()) {
        plans_are_identical = eLazyBoolNo;
      }
      if (a_pc_regloc != b_pc_regloc) {
        plans_are_identical = eLazyBoolNo;
      }
    }
  }
  return plans_are_identical;
}

UnwindPlanSP FuncUnwinders::GetUnwindPlanAtNonCallSite(Target &target,
                                                       Thread &thread) {
  UnwindPlanSP eh_frame_sp = GetEHFrameUnwindPlan(target);
  if (!eh_frame_sp)
    eh_frame_sp = GetDebugFrameUnwindPlan(target);
  if (!eh_frame_sp)
    eh_frame_sp = GetObjectFileUnwindPlan(target);
  UnwindPlanSP arch_default_at_entry_sp =
      GetUnwindPlanArchitectureDefaultAtFunctionEntry(thread);
  UnwindPlanSP arch_default_sp = GetUnwindPlanArchitectureDefault(thread);
  UnwindPlanSP assembly_sp = GetAssemblyUnwindPlan(target, thread);

  // This point of this code is to detect when a function is using a non-
  // standard ABI, and the eh_frame correctly describes that alternate ABI.
  // This is addressing a specific situation on x86_64 linux systems where one
  // function in a library pushes a value on the stack and jumps to another
  // function.  So using an assembly instruction based unwind will not work
  // when you're in the second function - the stack has been modified in a non-
  // ABI way.  But we have eh_frame that correctly describes how to unwind from
  // this location.  So we're looking to see if the initial pc register save
  // location from the eh_frame is different from the assembly unwind, the arch
  // default unwind, and the arch default at initial function entry.
  //
  // We may have eh_frame that describes the entire function -- or we may have
  // eh_frame that only describes the unwind after the prologue has executed --
  // so we need to check both the arch default (once the prologue has executed)
  // and the arch default at initial function entry.  And we may be running on
  // a target where we have only some of the assembly/arch default unwind plans
  // available.

  if (CompareUnwindPlansForIdenticalInitialPCLocation(
          thread, eh_frame_sp, arch_default_at_entry_sp) == eLazyBoolNo &&
      CompareUnwindPlansForIdenticalInitialPCLocation(
          thread, eh_frame_sp, arch_default_sp) == eLazyBoolNo &&
      CompareUnwindPlansForIdenticalInitialPCLocation(
          thread, assembly_sp, arch_default_sp) == eLazyBoolNo) {
    return eh_frame_sp;
  }

  if (UnwindPlanSP plan_sp = GetSymbolFileUnwindPlan(thread))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetDebugFrameAugmentedUnwindPlan(target, thread))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetEHFrameAugmentedUnwindPlan(target, thread))
    return plan_sp;
  if (UnwindPlanSP plan_sp = GetObjectFileAugmentedUnwindPlan(target, thread))
    return plan_sp;

  return assembly_sp;
}

UnwindPlanSP FuncUnwinders::GetUnwindPlanFastUnwind(Target &target,
                                                    Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_fast_sp.get() || m_tried_unwind_fast)
    return m_unwind_plan_fast_sp;

  m_tried_unwind_fast = true;

  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp) {
    m_unwind_plan_fast_sp =
        std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
    if (!assembly_profiler_sp->GetFastUnwindPlan(m_range, thread,
                                                 *m_unwind_plan_fast_sp)) {
      m_unwind_plan_fast_sp.reset();
    }
  }
  return m_unwind_plan_fast_sp;
}

UnwindPlanSP FuncUnwinders::GetUnwindPlanArchitectureDefault(Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_arch_default_sp.get() || m_tried_unwind_arch_default)
    return m_unwind_plan_arch_default_sp;

  m_tried_unwind_arch_default = true;

  Address current_pc;
  ProcessSP process_sp(thread.CalculateProcess());
  if (process_sp) {
    ABI *abi = process_sp->GetABI().get();
    if (abi) {
      m_unwind_plan_arch_default_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!abi->CreateDefaultUnwindPlan(*m_unwind_plan_arch_default_sp)) {
        m_unwind_plan_arch_default_sp.reset();
      }
    }
  }

  return m_unwind_plan_arch_default_sp;
}

UnwindPlanSP
FuncUnwinders::GetUnwindPlanArchitectureDefaultAtFunctionEntry(Thread &thread) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_unwind_plan_arch_default_at_func_entry_sp.get() ||
      m_tried_unwind_arch_default_at_func_entry)
    return m_unwind_plan_arch_default_at_func_entry_sp;

  m_tried_unwind_arch_default_at_func_entry = true;

  Address current_pc;
  ProcessSP process_sp(thread.CalculateProcess());
  if (process_sp) {
    ABI *abi = process_sp->GetABI().get();
    if (abi) {
      m_unwind_plan_arch_default_at_func_entry_sp =
          std::make_shared<UnwindPlan>(lldb::eRegisterKindGeneric);
      if (!abi->CreateFunctionEntryUnwindPlan(
              *m_unwind_plan_arch_default_at_func_entry_sp)) {
        m_unwind_plan_arch_default_at_func_entry_sp.reset();
      }
    }
  }

  return m_unwind_plan_arch_default_at_func_entry_sp;
}

Address &FuncUnwinders::GetFirstNonPrologueInsn(Target &target) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if (m_first_non_prologue_insn.IsValid())
    return m_first_non_prologue_insn;

  ExecutionContext exe_ctx(target.shared_from_this(), false);
  UnwindAssemblySP assembly_profiler_sp(GetUnwindAssemblyProfiler(target));
  if (assembly_profiler_sp)
    assembly_profiler_sp->FirstNonPrologueInsn(m_range, exe_ctx,
                                               m_first_non_prologue_insn);
  return m_first_non_prologue_insn;
}

const Address &FuncUnwinders::GetFunctionStartAddress() const {
  return m_range.GetBaseAddress();
}

lldb::UnwindAssemblySP
FuncUnwinders::GetUnwindAssemblyProfiler(Target &target) {
  UnwindAssemblySP assembly_profiler_sp;
  if (ArchSpec arch = m_unwind_table.GetArchitecture()) {
    arch.MergeFrom(target.GetArchitecture());
    assembly_profiler_sp = UnwindAssembly::FindPlugin(arch);
  }
  return assembly_profiler_sp;
}

Address FuncUnwinders::GetLSDAAddress(Target &target) {
  Address lsda_addr;

  UnwindPlanSP unwind_plan_sp = GetEHFrameUnwindPlan(target);
  if (unwind_plan_sp.get() == nullptr) {
    unwind_plan_sp = GetCompactUnwindUnwindPlan(target);
  }
  if (unwind_plan_sp.get() == nullptr) {
    unwind_plan_sp = GetObjectFileUnwindPlan(target);
  }
  if (unwind_plan_sp.get() && unwind_plan_sp->GetLSDAAddress().IsValid()) {
    lsda_addr = unwind_plan_sp->GetLSDAAddress();
  }
  return lsda_addr;
}

Address FuncUnwinders::GetPersonalityRoutinePtrAddress(Target &target) {
  Address personality_addr;

  UnwindPlanSP unwind_plan_sp = GetEHFrameUnwindPlan(target);
  if (unwind_plan_sp.get() == nullptr) {
    unwind_plan_sp = GetCompactUnwindUnwindPlan(target);
  }
  if (unwind_plan_sp.get() == nullptr) {
    unwind_plan_sp = GetObjectFileUnwindPlan(target);
  }
  if (unwind_plan_sp.get() &&
      unwind_plan_sp->GetPersonalityFunctionPtr().IsValid()) {
    personality_addr = unwind_plan_sp->GetPersonalityFunctionPtr();
  }

  return personality_addr;
}
