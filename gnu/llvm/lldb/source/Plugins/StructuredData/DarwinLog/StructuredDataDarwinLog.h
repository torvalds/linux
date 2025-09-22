//===-- StructuredDataDarwinLog.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_STRUCTUREDDATA_DARWINLOG_STRUCTUREDDATADARWINLOG_H
#define LLDB_SOURCE_PLUGINS_STRUCTUREDDATA_DARWINLOG_STRUCTUREDDATADARWINLOG_H

#include "lldb/Target/StructuredDataPlugin.h"

#include <mutex>

// Forward declarations
namespace sddarwinlog_private {
class EnableCommand;
}

namespace lldb_private {

class StructuredDataDarwinLog : public StructuredDataPlugin {
  friend sddarwinlog_private::EnableCommand;

public:
  // Public static API

  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetStaticPluginName() { return "darwin-log"; }

  /// Return whether the DarwinLog functionality is enabled.
  ///
  /// The DarwinLog functionality is enabled if the user explicitly enabled
  /// it with the enable command, or if the user has the setting set
  /// that controls if we always enable it for newly created/attached
  /// processes.
  ///
  /// \return
  ///      True if DarwinLog support is/will be enabled for existing or
  ///      newly launched/attached processes.
  static bool IsEnabled();

  // PluginInterface API

  llvm::StringRef GetPluginName() override { return GetStaticPluginName(); }

  // StructuredDataPlugin API

  bool SupportsStructuredDataType(llvm::StringRef type_name) override;

  void HandleArrivalOfStructuredData(
      Process &process, llvm::StringRef type_name,
      const StructuredData::ObjectSP &object_sp) override;

  Status GetDescription(const StructuredData::ObjectSP &object_sp,
                        lldb_private::Stream &stream) override;

  bool GetEnabled(llvm::StringRef type_name) const override;

  void ModulesDidLoad(Process &process, ModuleList &module_list) override;

  ~StructuredDataDarwinLog() override;

private:
  // Private constructors

  StructuredDataDarwinLog(const lldb::ProcessWP &process_wp);

  // Private static methods

  static lldb::StructuredDataPluginSP CreateInstance(Process &process);

  static void DebuggerInitialize(Debugger &debugger);

  static bool InitCompletionHookCallback(void *baton,
                                         StoppointCallbackContext *context,
                                         lldb::user_id_t break_id,
                                         lldb::user_id_t break_loc_id);

  static Status FilterLaunchInfo(ProcessLaunchInfo &launch_info,
                                 Target *target);

  // Internal helper methods used by friend classes
  void SetEnabled(bool enabled);

  void AddInitCompletionHook(Process &process);

  // Private methods

  void DumpTimestamp(Stream &stream, uint64_t timestamp);

  size_t DumpHeader(Stream &stream, const StructuredData::Dictionary &event);

  size_t HandleDisplayOfEvent(const StructuredData::Dictionary &event,
                              Stream &stream);

  /// Call the enable command again, using whatever settings were initially
  /// made.

  void EnableNow();

  // Private data
  bool m_recorded_first_timestamp;
  uint64_t m_first_timestamp_seen;
  bool m_is_enabled;
  std::mutex m_added_breakpoint_mutex;
  bool m_added_breakpoint;
  lldb::user_id_t m_breakpoint_id;
};
}

#endif // LLDB_SOURCE_PLUGINS_STRUCTUREDDATA_DARWINLOG_STRUCTUREDDATADARWINLOG_H
