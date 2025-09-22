//===-- DynamicLoaderWasmDYLD.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_DynamicLoaderWasmDYLD_h_
#define liblldb_Plugins_DynamicLoaderWasmDYLD_h_

#include "lldb/Target/DynamicLoader.h"

namespace lldb_private {
namespace wasm {

class DynamicLoaderWasmDYLD : public DynamicLoader {
public:
  DynamicLoaderWasmDYLD(Process *process);

  static void Initialize();
  static void Terminate() {}

  static llvm::StringRef GetPluginNameStatic() { return "wasm-dyld"; }
  static llvm::StringRef GetPluginDescriptionStatic();

  static DynamicLoader *CreateInstance(Process *process, bool force);

  /// DynamicLoader
  /// \{
  void DidAttach() override;
  void DidLaunch() override {}
  Status CanLoadImage() override { return Status(); }
  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop) override;
  lldb::ModuleSP LoadModuleAtAddress(const lldb_private::FileSpec &file,
                                     lldb::addr_t link_map_addr,
                                     lldb::addr_t base_addr,
                                     bool base_addr_is_offset) override;

  /// \}

  /// PluginInterface protocol.
  /// \{
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }
  /// \}
};

} // namespace wasm
} // namespace lldb_private

#endif // liblldb_Plugins_DynamicLoaderWasmDYLD_h_
