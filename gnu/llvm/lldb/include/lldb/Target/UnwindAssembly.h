//===-- UnwindAssembly.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_UNWINDASSEMBLY_H
#define LLDB_TARGET_UNWINDASSEMBLY_H

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/ArchSpec.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class UnwindAssembly : public std::enable_shared_from_this<UnwindAssembly>,
                       public PluginInterface {
public:
  static lldb::UnwindAssemblySP FindPlugin(const ArchSpec &arch);

  virtual bool
  GetNonCallSiteUnwindPlanFromAssembly(AddressRange &func, Thread &thread,
                                       UnwindPlan &unwind_plan) = 0;

  virtual bool AugmentUnwindPlanFromCallSite(AddressRange &func, Thread &thread,
                                             UnwindPlan &unwind_plan) = 0;

  virtual bool GetFastUnwindPlan(AddressRange &func, Thread &thread,
                                 UnwindPlan &unwind_plan) = 0;

  // thread may be NULL in which case we only use the Target (e.g. if this is
  // called pre-process-launch).
  virtual bool
  FirstNonPrologueInsn(AddressRange &func,
                       const lldb_private::ExecutionContext &exe_ctx,
                       Address &first_non_prologue_insn) = 0;

protected:
  UnwindAssembly(const ArchSpec &arch);
  ArchSpec m_arch;
};

} // namespace lldb_private

#endif // LLDB_TARGET_UNWINDASSEMBLY_H
