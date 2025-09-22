#ifndef LLDB_SYMBOL_FUNCUNWINDERS_H
#define LLDB_SYMBOL_FUNCUNWINDERS_H

#include "lldb/Core/AddressRange.h"
#include "lldb/lldb-private-enumerations.h"
#include <mutex>
#include <vector>

namespace lldb_private {

class UnwindTable;

class FuncUnwinders {
public:
  // FuncUnwinders objects are used to track UnwindPlans for a function (named
  // or not - really just an address range)

  // We'll record four different UnwindPlans for each address range:
  //
  //   1. Unwinding from a call site (a valid exception throw location)
  //      This is often sourced from the eh_frame exception handling info
  //   2. Unwinding from a non-call site (any location in the function)
  //      This is often done by analyzing the function prologue assembly
  //      language instructions
  //   3. A fast unwind method for this function which only retrieves a
  //      limited set of registers necessary to walk the stack
  //   4. An architectural default unwind plan when none of the above are
  //      available for some reason.

  // Additionally, FuncUnwinds object can be asked where the prologue
  // instructions are finished for migrating breakpoints past the stack frame
  // setup instructions when we don't have line table information.

  FuncUnwinders(lldb_private::UnwindTable &unwind_table, AddressRange range);

  ~FuncUnwinders();

  lldb::UnwindPlanSP GetUnwindPlanAtCallSite(Target &target, Thread &thread);

  lldb::UnwindPlanSP GetUnwindPlanAtNonCallSite(Target &target,
                                                lldb_private::Thread &thread);

  lldb::UnwindPlanSP GetUnwindPlanFastUnwind(Target &target,
                                             lldb_private::Thread &thread);

  lldb::UnwindPlanSP
  GetUnwindPlanArchitectureDefault(lldb_private::Thread &thread);

  lldb::UnwindPlanSP
  GetUnwindPlanArchitectureDefaultAtFunctionEntry(lldb_private::Thread &thread);

  Address &GetFirstNonPrologueInsn(Target &target);

  const Address &GetFunctionStartAddress() const;

  bool ContainsAddress(const Address &addr) const {
    return m_range.ContainsFileAddress(addr);
  }

  // A function may have a Language Specific Data Area specified -- a block of
  // data in
  // the object file which is used in the processing of an exception throw /
  // catch. If any of the UnwindPlans have the address of the LSDA region for
  // this function, this will return it.
  Address GetLSDAAddress(Target &target);

  // A function may have a Personality Routine associated with it -- used in the
  // processing of throwing an exception.  If any of the UnwindPlans have the
  // address of the personality routine, this will return it.  Read the target-
  // pointer at this address to get the personality function address.
  Address GetPersonalityRoutinePtrAddress(Target &target);

  // The following methods to retrieve specific unwind plans should rarely be
  // used. Instead, clients should ask for the *behavior* they are looking for,
  // using one of the above UnwindPlan retrieval methods.

  lldb::UnwindPlanSP GetAssemblyUnwindPlan(Target &target, Thread &thread);

  lldb::UnwindPlanSP GetObjectFileUnwindPlan(Target &target);

  lldb::UnwindPlanSP GetObjectFileAugmentedUnwindPlan(Target &target,
                                                      Thread &thread);

  lldb::UnwindPlanSP GetEHFrameUnwindPlan(Target &target);

  lldb::UnwindPlanSP GetEHFrameAugmentedUnwindPlan(Target &target,
                                                   Thread &thread);

  lldb::UnwindPlanSP GetDebugFrameUnwindPlan(Target &target);

  lldb::UnwindPlanSP GetDebugFrameAugmentedUnwindPlan(Target &target,
                                                      Thread &thread);

  lldb::UnwindPlanSP GetCompactUnwindUnwindPlan(Target &target);

  lldb::UnwindPlanSP GetArmUnwindUnwindPlan(Target &target);

  lldb::UnwindPlanSP GetSymbolFileUnwindPlan(Thread &thread);

  lldb::UnwindPlanSP GetArchDefaultUnwindPlan(Thread &thread);

  lldb::UnwindPlanSP GetArchDefaultAtFuncEntryUnwindPlan(Thread &thread);

private:
  lldb::UnwindAssemblySP GetUnwindAssemblyProfiler(Target &target);

  // Do a simplistic comparison for the register restore rule for getting the
  // caller's pc value on two UnwindPlans -- returns LazyBoolYes if they have
  // the same unwind rule for the pc, LazyBoolNo if they do not have the same
  // unwind rule for the pc, and LazyBoolCalculate if it was unable to
  // determine this for some reason.
  lldb_private::LazyBool CompareUnwindPlansForIdenticalInitialPCLocation(
      Thread &thread, const lldb::UnwindPlanSP &a, const lldb::UnwindPlanSP &b);

  UnwindTable &m_unwind_table;
  AddressRange m_range;

  std::recursive_mutex m_mutex;

  lldb::UnwindPlanSP m_unwind_plan_assembly_sp;
  lldb::UnwindPlanSP m_unwind_plan_object_file_sp;
  lldb::UnwindPlanSP m_unwind_plan_eh_frame_sp;
  lldb::UnwindPlanSP m_unwind_plan_debug_frame_sp;

  // augmented by assembly inspection so it's valid everywhere
  lldb::UnwindPlanSP m_unwind_plan_object_file_augmented_sp;
  lldb::UnwindPlanSP m_unwind_plan_eh_frame_augmented_sp;
  lldb::UnwindPlanSP m_unwind_plan_debug_frame_augmented_sp;

  std::vector<lldb::UnwindPlanSP> m_unwind_plan_compact_unwind;
  lldb::UnwindPlanSP m_unwind_plan_arm_unwind_sp;
  lldb::UnwindPlanSP m_unwind_plan_symbol_file_sp;
  lldb::UnwindPlanSP m_unwind_plan_fast_sp;
  lldb::UnwindPlanSP m_unwind_plan_arch_default_sp;
  lldb::UnwindPlanSP m_unwind_plan_arch_default_at_func_entry_sp;

  // Fetching the UnwindPlans can be expensive - if we've already attempted to
  // get one & failed, don't try again.
  bool m_tried_unwind_plan_assembly : 1, m_tried_unwind_plan_eh_frame : 1,
      m_tried_unwind_plan_object_file : 1,
      m_tried_unwind_plan_debug_frame : 1,
      m_tried_unwind_plan_object_file_augmented : 1,
      m_tried_unwind_plan_eh_frame_augmented : 1,
      m_tried_unwind_plan_debug_frame_augmented : 1,
      m_tried_unwind_plan_compact_unwind : 1,
      m_tried_unwind_plan_arm_unwind : 1, m_tried_unwind_plan_symbol_file : 1,
      m_tried_unwind_fast : 1, m_tried_unwind_arch_default : 1,
      m_tried_unwind_arch_default_at_func_entry : 1;

  Address m_first_non_prologue_insn;

  FuncUnwinders(const FuncUnwinders &) = delete;
  const FuncUnwinders &operator=(const FuncUnwinders &) = delete;

}; // class FuncUnwinders

} // namespace lldb_private

#endif // LLDB_SYMBOL_FUNCUNWINDERS_H
