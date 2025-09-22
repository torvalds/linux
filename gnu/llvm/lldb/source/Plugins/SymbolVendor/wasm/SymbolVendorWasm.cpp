//===-- SymbolVendorWasm.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolVendorWasm.h"

#include <cstring>
#include <optional>

#include "Plugins/ObjectFile/wasm/ObjectFileWasm.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleSpec.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Host/Host.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/StreamString.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::wasm;

LLDB_PLUGIN_DEFINE(SymbolVendorWasm)

// SymbolVendorWasm constructor
SymbolVendorWasm::SymbolVendorWasm(const lldb::ModuleSP &module_sp)
    : SymbolVendor(module_sp) {}

void SymbolVendorWasm::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                GetPluginDescriptionStatic(), CreateInstance);
}

void SymbolVendorWasm::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

llvm::StringRef SymbolVendorWasm::GetPluginDescriptionStatic() {
  return "Symbol vendor for WASM that looks for dwo files that match "
         "executables.";
}

// CreateInstance
//
// Platforms can register a callback to use when creating symbol vendors to
// allow for complex debug information file setups, and to also allow for
// finding separate debug information files.
SymbolVendor *
SymbolVendorWasm::CreateInstance(const lldb::ModuleSP &module_sp,
                                 lldb_private::Stream *feedback_strm) {
  if (!module_sp)
    return nullptr;

  ObjectFileWasm *obj_file =
      llvm::dyn_cast_or_null<ObjectFileWasm>(module_sp->GetObjectFile());
  if (!obj_file)
    return nullptr;

  // If the main object file already contains debug info, then we are done.
  if (obj_file->GetSectionList()->FindSectionByType(
          lldb::eSectionTypeDWARFDebugInfo, true))
    return nullptr;

  LLDB_SCOPED_TIMERF("SymbolVendorWasm::CreateInstance (module = %s)",
                     module_sp->GetFileSpec().GetPath().c_str());

  ModuleSpec module_spec;
  module_spec.GetFileSpec() = obj_file->GetFileSpec();
  FileSystem::Instance().Resolve(module_spec.GetFileSpec());
  module_spec.GetUUID() = obj_file->GetUUID();

  // A Wasm module may have a custom section named "external_debug_info" whose
  // content is the absolute or relative path of the Wasm module that contains
  // debug symbols for this module.
  std::optional<FileSpec> symbol_file_spec =
      obj_file->GetExternalDebugInfoFileSpec();
  if (!symbol_file_spec)
    return nullptr;
  module_spec.GetSymbolFileSpec() = *symbol_file_spec;

  FileSpecList search_paths = Target::GetDefaultDebugFileSearchPaths();
  FileSpec sym_fspec =
      PluginManager::LocateExecutableSymbolFile(module_spec, search_paths);
  if (!sym_fspec)
    return nullptr;

  DataBufferSP sym_file_data_sp;
  lldb::offset_t sym_file_data_offset = 0;
  ObjectFileSP sym_objfile_sp = ObjectFile::FindPlugin(
      module_sp, &sym_fspec, 0, FileSystem::Instance().GetByteSize(sym_fspec),
      sym_file_data_sp, sym_file_data_offset);
  if (!sym_objfile_sp)
    return nullptr;

  // This objfile is for debugging purposes.
  sym_objfile_sp->SetType(ObjectFile::eTypeDebugInfo);

  SymbolVendorWasm *symbol_vendor = new SymbolVendorWasm(module_sp);

  // Get the module unified section list and add our debug sections to
  // that.
  SectionList *module_section_list = module_sp->GetSectionList();
  SectionList *objfile_section_list = sym_objfile_sp->GetSectionList();

  if (!module_section_list || !objfile_section_list)
    return nullptr;

  static const SectionType g_sections[] = {
      eSectionTypeDWARFDebugAbbrev,   eSectionTypeDWARFDebugAddr,
      eSectionTypeDWARFDebugAranges,  eSectionTypeDWARFDebugCuIndex,
      eSectionTypeDWARFDebugFrame,    eSectionTypeDWARFDebugInfo,
      eSectionTypeDWARFDebugLine,     eSectionTypeDWARFDebugLineStr,
      eSectionTypeDWARFDebugLoc,      eSectionTypeDWARFDebugLocLists,
      eSectionTypeDWARFDebugMacInfo,  eSectionTypeDWARFDebugMacro,
      eSectionTypeDWARFDebugPubNames, eSectionTypeDWARFDebugPubTypes,
      eSectionTypeDWARFDebugRanges,   eSectionTypeDWARFDebugRngLists,
      eSectionTypeDWARFDebugStr,      eSectionTypeDWARFDebugStrOffsets,
      eSectionTypeDWARFDebugTypes};
  for (SectionType section_type : g_sections) {
    if (SectionSP section_sp =
            objfile_section_list->FindSectionByType(section_type, true)) {
      if (SectionSP module_section_sp =
              module_section_list->FindSectionByType(section_type, true))
        module_section_list->ReplaceSection(module_section_sp->GetID(),
                                            section_sp);
      else
        module_section_list->AddSection(section_sp);
    }
  }

  symbol_vendor->AddSymbolFileRepresentation(sym_objfile_sp);
  return symbol_vendor;
}
