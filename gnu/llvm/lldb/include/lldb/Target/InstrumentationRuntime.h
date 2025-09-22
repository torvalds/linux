//===-- InstrumentationRuntime.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_INSTRUMENTATIONRUNTIME_H
#define LLDB_TARGET_INSTRUMENTATIONRUNTIME_H

#include <map>
#include <vector>

#include "lldb/Core/PluginInterface.h"
#include "lldb/Utility/StructuredData.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-private.h"
#include "lldb/lldb-types.h"

namespace lldb_private {

typedef std::map<lldb::InstrumentationRuntimeType,
                 lldb::InstrumentationRuntimeSP>
    InstrumentationRuntimeCollection;

class InstrumentationRuntime
    : public std::enable_shared_from_this<InstrumentationRuntime>,
      public PluginInterface {
  /// The instrumented process.
  lldb::ProcessWP m_process_wp;

  /// The module containing the instrumentation runtime.
  lldb::ModuleSP m_runtime_module;

  /// The breakpoint in the instrumentation runtime.
  lldb::user_id_t m_breakpoint_id;

  /// Indicates whether or not breakpoints have been registered in the
  /// instrumentation runtime.
  bool m_is_active;

protected:
  InstrumentationRuntime(const lldb::ProcessSP &process_sp)
      : m_breakpoint_id(0), m_is_active(false) {
    if (process_sp)
      m_process_wp = process_sp;
  }

  lldb::ProcessSP GetProcessSP() { return m_process_wp.lock(); }

  lldb::ModuleSP GetRuntimeModuleSP() { return m_runtime_module; }

  void SetRuntimeModuleSP(lldb::ModuleSP module_sp) {
    m_runtime_module = std::move(module_sp);
  }

  lldb::user_id_t GetBreakpointID() const { return m_breakpoint_id; }

  void SetBreakpointID(lldb::user_id_t ID) { m_breakpoint_id = ID; }

  void SetActive(bool IsActive) { m_is_active = IsActive; }

  /// Return a regular expression which can be used to identify a valid version
  /// of the runtime library.
  virtual const RegularExpression &GetPatternForRuntimeLibrary() = 0;

  /// Check whether \p module_sp corresponds to a valid runtime library.
  virtual bool CheckIfRuntimeIsValid(const lldb::ModuleSP module_sp) = 0;

  /// Register a breakpoint in the runtime library and perform any other
  /// necessary initialization. The runtime library
  /// is guaranteed to be loaded.
  virtual void Activate() = 0;

public:
  static void ModulesDidLoad(lldb_private::ModuleList &module_list,
                             Process *process,
                             InstrumentationRuntimeCollection &runtimes);

  /// Look for the instrumentation runtime in \p module_list. Register and
  /// activate the runtime if this hasn't already
  /// been done.
  void ModulesDidLoad(lldb_private::ModuleList &module_list);

  bool IsActive() const { return m_is_active; }

  virtual lldb::ThreadCollectionSP
  GetBacktracesFromExtendedStopInfo(StructuredData::ObjectSP info);
};

} // namespace lldb_private

#endif // LLDB_TARGET_INSTRUMENTATIONRUNTIME_H
