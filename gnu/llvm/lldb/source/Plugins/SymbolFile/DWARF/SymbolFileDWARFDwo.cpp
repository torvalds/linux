//===-- SymbolFileDWARFDwo.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SymbolFileDWARFDwo.h"

#include "lldb/Core/Section.h"
#include "lldb/Expression/DWARFExpression.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"
#include "llvm/Support/Casting.h"

#include "DWARFCompileUnit.h"
#include "DWARFDebugInfo.h"
#include "DWARFUnit.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;

char SymbolFileDWARFDwo::ID;

SymbolFileDWARFDwo::SymbolFileDWARFDwo(SymbolFileDWARF &base_symbol_file,
                                       ObjectFileSP objfile, uint32_t id)
    : SymbolFileDWARF(objfile, objfile->GetSectionList(
                                   /*update_module_section_list*/ false)),
      m_base_symbol_file(base_symbol_file) {
  SetFileIndex(id);

  // Parsing of the dwarf unit index is not thread-safe, so we need to prime it
  // to enable subsequent concurrent lookups.
  m_context.GetAsLLVM().getCUIndex();
}

DWARFCompileUnit *SymbolFileDWARFDwo::GetDWOCompileUnitForHash(uint64_t hash) {
  if (const llvm::DWARFUnitIndex &index = m_context.GetAsLLVM().getCUIndex()) {
    if (const llvm::DWARFUnitIndex::Entry *entry = index.getFromHash(hash)) {
      if (auto *unit_contrib = entry->getContribution())
        return llvm::dyn_cast_or_null<DWARFCompileUnit>(
            DebugInfo().GetUnitAtOffset(DIERef::Section::DebugInfo,
                                        unit_contrib->getOffset()));
    }
    return nullptr;
  }

  DWARFCompileUnit *cu = FindSingleCompileUnit();
  if (!cu)
    return nullptr;
  std::optional<uint64_t> dwo_id = cu->GetDWOId();
  if (!dwo_id || hash != *dwo_id)
    return nullptr;
  return cu;
}

DWARFCompileUnit *SymbolFileDWARFDwo::FindSingleCompileUnit() {
  DWARFDebugInfo &debug_info = DebugInfo();

  // Right now we only support dwo files with one compile unit. If we don't have
  // type units, we can just check for the unit count.
  if (!debug_info.ContainsTypeUnits() && debug_info.GetNumUnits() == 1)
    return llvm::cast<DWARFCompileUnit>(debug_info.GetUnitAtIndex(0));

  // Otherwise, we have to run through all units, and find the compile unit that
  // way.
  DWARFCompileUnit *cu = nullptr;
  for (size_t i = 0; i < debug_info.GetNumUnits(); ++i) {
    if (auto *candidate =
            llvm::dyn_cast<DWARFCompileUnit>(debug_info.GetUnitAtIndex(i))) {
      if (cu)
        return nullptr; // More that one CU found.
      cu = candidate;
    }
  }
  return cu;
}

lldb::offset_t SymbolFileDWARFDwo::GetVendorDWARFOpcodeSize(
    const lldb_private::DataExtractor &data, const lldb::offset_t data_offset,
    const uint8_t op) const {
  return GetBaseSymbolFile().GetVendorDWARFOpcodeSize(data, data_offset, op);
}

uint64_t SymbolFileDWARFDwo::GetDebugInfoSize(bool load_all_debug_info) {
  // Directly get debug info from current dwo object file's section list
  // instead of asking SymbolFileCommon::GetDebugInfo() which parses from
  // owning module which is wrong.
  SectionList *section_list =
      m_objfile_sp->GetSectionList(/*update_module_section_list=*/false);
  if (section_list)
    return section_list->GetDebugInfoSize();
  return 0;
}

bool SymbolFileDWARFDwo::ParseVendorDWARFOpcode(
    uint8_t op, const lldb_private::DataExtractor &opcodes,
    lldb::offset_t &offset, std::vector<lldb_private::Value> &stack) const {
  return GetBaseSymbolFile().ParseVendorDWARFOpcode(op, opcodes, offset, stack);
}

SymbolFileDWARF::DIEToTypePtr &SymbolFileDWARFDwo::GetDIEToType() {
  return GetBaseSymbolFile().GetDIEToType();
}

SymbolFileDWARF::DIEToVariableSP &SymbolFileDWARFDwo::GetDIEToVariable() {
  return GetBaseSymbolFile().GetDIEToVariable();
}

llvm::DenseMap<lldb::opaque_compiler_type_t, DIERef> &
SymbolFileDWARFDwo::GetForwardDeclCompilerTypeToDIE() {
  return GetBaseSymbolFile().GetForwardDeclCompilerTypeToDIE();
}

void SymbolFileDWARFDwo::GetObjCMethods(
    lldb_private::ConstString class_name,
    llvm::function_ref<bool(DWARFDIE die)> callback) {
  GetBaseSymbolFile().GetObjCMethods(class_name, callback);
}

UniqueDWARFASTTypeMap &SymbolFileDWARFDwo::GetUniqueDWARFASTTypeMap() {
  return GetBaseSymbolFile().GetUniqueDWARFASTTypeMap();
}

DWARFDIE SymbolFileDWARFDwo::FindDefinitionDIE(const DWARFDIE &die) {
  return GetBaseSymbolFile().FindDefinitionDIE(die);
}

lldb::TypeSP SymbolFileDWARFDwo::FindCompleteObjCDefinitionTypeForDIE(
    const DWARFDIE &die, lldb_private::ConstString type_name,
    bool must_be_implementation) {
  return GetBaseSymbolFile().FindCompleteObjCDefinitionTypeForDIE(
      die, type_name, must_be_implementation);
}

llvm::Expected<lldb::TypeSystemSP>
SymbolFileDWARFDwo::GetTypeSystemForLanguage(LanguageType language) {
  return GetBaseSymbolFile().GetTypeSystemForLanguage(language);
}

DWARFDIE
SymbolFileDWARFDwo::GetDIE(const DIERef &die_ref) {
  if (die_ref.file_index() == GetFileIndex())
    return DebugInfo().GetDIE(die_ref.section(), die_ref.die_offset());
  return GetBaseSymbolFile().GetDIE(die_ref);
}

void SymbolFileDWARFDwo::FindGlobalVariables(
    ConstString name, const CompilerDeclContext &parent_decl_ctx,
    uint32_t max_matches, VariableList &variables) {
  GetBaseSymbolFile().FindGlobalVariables(name, parent_decl_ctx, max_matches,
                                          variables);
}

bool SymbolFileDWARFDwo::GetDebugInfoIndexWasLoadedFromCache() const {
  return GetBaseSymbolFile().GetDebugInfoIndexWasLoadedFromCache();
}
void SymbolFileDWARFDwo::SetDebugInfoIndexWasLoadedFromCache() {
  GetBaseSymbolFile().SetDebugInfoIndexWasLoadedFromCache();
}
bool SymbolFileDWARFDwo::GetDebugInfoIndexWasSavedToCache() const {
  return GetBaseSymbolFile().GetDebugInfoIndexWasSavedToCache();
}
void SymbolFileDWARFDwo::SetDebugInfoIndexWasSavedToCache() {
  GetBaseSymbolFile().SetDebugInfoIndexWasSavedToCache();
}
bool SymbolFileDWARFDwo::GetDebugInfoHadFrameVariableErrors() const {
  return GetBaseSymbolFile().GetDebugInfoHadFrameVariableErrors();
}
void SymbolFileDWARFDwo::SetDebugInfoHadFrameVariableErrors() {
  return GetBaseSymbolFile().SetDebugInfoHadFrameVariableErrors();
}

SymbolFileDWARF *
SymbolFileDWARFDwo::GetDIERefSymbolFile(const DIERef &die_ref) {
  return GetBaseSymbolFile().GetDIERefSymbolFile(die_ref);
}
