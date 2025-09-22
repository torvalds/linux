//===-- JITLoaderGDB.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_JITLOADER_GDB_JITLOADERGDB_H
#define LLDB_SOURCE_PLUGINS_JITLOADER_GDB_JITLOADERGDB_H

#include <map>

#include "lldb/Target/JITLoader.h"
#include "lldb/Target/Process.h"

class JITLoaderGDB : public lldb_private::JITLoader {
public:
  JITLoaderGDB(lldb_private::Process *process);

  ~JITLoaderGDB() override;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "gdb"; }

  static llvm::StringRef GetPluginDescriptionStatic();

  static lldb::JITLoaderSP CreateInstance(lldb_private::Process *process,
                                          bool force);

  static void DebuggerInitialize(lldb_private::Debugger &debugger);

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // JITLoader interface
  void DidAttach() override;

  void DidLaunch() override;

  void ModulesDidLoad(lldb_private::ModuleList &module_list) override;

private:
  lldb::addr_t GetSymbolAddress(lldb_private::ModuleList &module_list,
                                lldb_private::ConstString name,
                                lldb::SymbolType symbol_type) const;

  void SetJITBreakpoint(lldb_private::ModuleList &module_list);

  bool DidSetJITBreakpoint() const;

  bool ReadJITDescriptor(bool all_entries);

  template <typename ptr_t> bool ReadJITDescriptorImpl(bool all_entries);

  static bool
  JITDebugBreakpointHit(void *baton,
                        lldb_private::StoppointCallbackContext *context,
                        lldb::user_id_t break_id, lldb::user_id_t break_loc_id);

  static void ProcessStateChangedCallback(void *baton,
                                          lldb_private::Process *process,
                                          lldb::StateType state);

  // A collection of in-memory jitted object addresses and their corresponding
  // modules
  typedef std::map<lldb::addr_t, const lldb::ModuleSP> JITObjectMap;
  JITObjectMap m_jit_objects;

  lldb::user_id_t m_jit_break_id;
  lldb::addr_t m_jit_descriptor_addr;
};

#endif // LLDB_SOURCE_PLUGINS_JITLOADER_GDB_JITLOADERGDB_H
