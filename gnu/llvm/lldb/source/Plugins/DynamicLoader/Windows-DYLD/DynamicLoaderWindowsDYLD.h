//===-- DynamicLoaderWindowsDYLD.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_DYNAMICLOADER_WINDOWS_DYLD_DYNAMICLOADERWINDOWSDYLD_H
#define LLDB_SOURCE_PLUGINS_DYNAMICLOADER_WINDOWS_DYLD_DYNAMICLOADERWINDOWSDYLD_H

#include "lldb/Target/DynamicLoader.h"
#include "lldb/lldb-forward.h"

#include <map>

namespace lldb_private {

class DynamicLoaderWindowsDYLD : public DynamicLoader {
public:
  DynamicLoaderWindowsDYLD(Process *process);

  ~DynamicLoaderWindowsDYLD() override;

  static void Initialize();
  static void Terminate();
  static llvm::StringRef GetPluginNameStatic() { return "windows-dyld"; }
  static llvm::StringRef GetPluginDescriptionStatic();

  static DynamicLoader *CreateInstance(Process *process, bool force);

  void OnLoadModule(lldb::ModuleSP module_sp, const ModuleSpec module_spec,
                    lldb::addr_t module_addr);
  void OnUnloadModule(lldb::addr_t module_addr);

  void DidAttach() override;
  void DidLaunch() override;
  Status CanLoadImage() override;
  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

protected:
  lldb::addr_t GetLoadAddress(lldb::ModuleSP executable);

private:
  std::map<lldb::ModuleSP, lldb::addr_t> m_loaded_modules;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_DYNAMICLOADER_WINDOWS_DYLD_DYNAMICLOADERWINDOWSDYLD_H
