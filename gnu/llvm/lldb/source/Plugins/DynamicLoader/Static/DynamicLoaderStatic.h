//===-- DynamicLoaderStatic.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_STATIC_DYNAMICLOADERSTATIC_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_STATIC_DYNAMICLOADERSTATIC_H

#include "lldb/Target/DynamicLoader.h"
#include "lldb/Target/Process.h"
#include "lldb/Utility/FileSpec.h"
#include "lldb/Utility/UUID.h"

class DynamicLoaderStatic : public lldb_private::DynamicLoader {
public:
  DynamicLoaderStatic(lldb_private::Process *process);

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "static"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb_private::DynamicLoader *
  CreateInstance(lldb_private::Process *process, bool force);

  /// Called after attaching a process.
  ///
  /// Allow DynamicLoader plug-ins to execute some code after
  /// attaching to a process.
  void DidAttach() override;

  void DidLaunch() override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(lldb_private::Thread &thread,
                                                  bool stop_others) override;

  lldb_private::Status CanLoadImage() override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

private:
  void LoadAllImagesAtFileAddresses();
};

#endif // LLDB_SOURCE_PLUGINS_DYNAMICLOADER_STATIC_DYNAMICLOADERSTATIC_H
