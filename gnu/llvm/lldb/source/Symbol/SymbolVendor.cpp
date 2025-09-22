//===-- SymbolVendor.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/SymbolVendor.h"

#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;

// FindPlugin
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
SymbolVendor *SymbolVendor::FindPlugin(const lldb::ModuleSP &module_sp,
                                       lldb_private::Stream *feedback_strm) {
  std::unique_ptr<SymbolVendor> instance_up;
  SymbolVendorCreateInstance create_callback;

  for (size_t idx = 0;
       (create_callback = PluginManager::GetSymbolVendorCreateCallbackAtIndex(
            idx)) != nullptr;
       ++idx) {
    instance_up.reset(create_callback(module_sp, feedback_strm));

    if (instance_up) {
      return instance_up.release();
    }
  }
  // The default implementation just tries to create debug information using
  // the file representation for the module.
  ObjectFileSP sym_objfile_sp;
  FileSpec sym_spec = module_sp->GetSymbolFileFileSpec();
  if (sym_spec && sym_spec != module_sp->GetObjectFile()->GetFileSpec()) {
    DataBufferSP data_sp;
    offset_t data_offset = 0;
    sym_objfile_sp = ObjectFile::FindPlugin(
        module_sp, &sym_spec, 0, FileSystem::Instance().GetByteSize(sym_spec),
        data_sp, data_offset);
  }
  if (!sym_objfile_sp)
    sym_objfile_sp = module_sp->GetObjectFile()->shared_from_this();
  instance_up = std::make_unique<SymbolVendor>(module_sp);
  instance_up->AddSymbolFileRepresentation(sym_objfile_sp);
  return instance_up.release();
}

// SymbolVendor constructor
SymbolVendor::SymbolVendor(const lldb::ModuleSP &module_sp)
    : ModuleChild(module_sp), m_sym_file_up() {}

// Add a representation given an object file.
void SymbolVendor::AddSymbolFileRepresentation(const ObjectFileSP &objfile_sp) {
  ModuleSP module_sp(GetModule());
  if (module_sp) {
    std::lock_guard<std::recursive_mutex> guard(module_sp->GetMutex());
    if (objfile_sp)
      m_sym_file_up.reset(SymbolFile::FindPlugin(objfile_sp));
  }
}
