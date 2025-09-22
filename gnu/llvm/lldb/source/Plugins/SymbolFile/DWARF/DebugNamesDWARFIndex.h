//===-- DebugNamesDWARFIndex.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DEBUGNAMESDWARFINDEX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DEBUGNAMESDWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/ManualDWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"
#include <optional>

namespace lldb_private::plugin {
namespace dwarf {
class DebugNamesDWARFIndex : public DWARFIndex {
public:
  static llvm::Expected<std::unique_ptr<DebugNamesDWARFIndex>>
  Create(Module &module, DWARFDataExtractor debug_names,
         DWARFDataExtractor debug_str, SymbolFileDWARF &dwarf);

  void Preload() override { m_fallback.Preload(); }

  void
  GetGlobalVariables(ConstString basename,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(const RegularExpression &regex,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(DWARFUnit &cu,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetObjCMethods(ConstString class_name,
                 llvm::function_ref<bool(DWARFDIE die)> callback) override {}
  void GetCompleteObjCClass(
      ConstString class_name, bool must_be_implementation,
      llvm::function_ref<bool(DWARFDIE die)> callback) override;

  /// Uses DWARF5's IDX_parent fields, when available, to speed up this query.
  void GetFullyQualifiedType(
      const DWARFDeclContext &context,
      llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetTypes(ConstString name,
                llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetTypes(const DWARFDeclContext &context,
                llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetNamespaces(ConstString name,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetFunctions(const Module::LookupInfo &lookup_info,
                    SymbolFileDWARF &dwarf,
                    const CompilerDeclContext &parent_decl_ctx,
                    llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetFunctions(const RegularExpression &regex,
                    llvm::function_ref<bool(DWARFDIE die)> callback) override;

  void Dump(Stream &s) override;

private:
  DebugNamesDWARFIndex(Module &module,
                       std::unique_ptr<llvm::DWARFDebugNames> debug_names_up,
                       DWARFDataExtractor debug_names_data,
                       DWARFDataExtractor debug_str_data,
                       SymbolFileDWARF &dwarf)
      : DWARFIndex(module), m_debug_info(dwarf.DebugInfo()),
        m_debug_names_data(debug_names_data), m_debug_str_data(debug_str_data),
        m_debug_names_up(std::move(debug_names_up)),
        m_fallback(module, dwarf, GetUnits(*m_debug_names_up),
                   GetTypeUnitSignatures(*m_debug_names_up)) {}

  DWARFDebugInfo &m_debug_info;

  // LLVM DWARFDebugNames will hold a non-owning reference to this data, so keep
  // track of the ownership here.
  DWARFDataExtractor m_debug_names_data;
  DWARFDataExtractor m_debug_str_data;

  using DebugNames = llvm::DWARFDebugNames;
  std::unique_ptr<DebugNames> m_debug_names_up;
  ManualDWARFIndex m_fallback;

  DWARFUnit *GetNonSkeletonUnit(const DebugNames::Entry &entry) const;
  DWARFDIE GetDIE(const DebugNames::Entry &entry) const;

  /// Checks if an entry is a foreign TU and fetch the type unit.
  ///
  /// This function checks if the DebugNames::Entry refers to a foreign TU and
  /// returns an optional with a value of the \a entry is a foreign type unit
  /// entry. A valid pointer will be returned if this entry is from a .dwo file
  /// or if it is from a .dwp file and it matches the type unit's originating
  /// .dwo file by verifying that the DW_TAG_type_unit DIE has a DW_AT_dwo_name
  /// that matches the DWO name from the originating skeleton compile unit.
  ///
  /// \param[in] entry
  ///   The accelerator table entry to check.
  ///
  /// \returns
  ///   A std::optional that has a value if this entry represents a foreign type
  ///   unit. If the pointer is valid, then we were able to find and match the
  ///   entry to the type unit in the .dwo or .dwp file. The returned value can
  ///   have a valid, yet contain NULL in the following cases:
  ///   - we were not able to load the .dwo file (missing or DWO ID mismatch)
  ///   - we were able to load the .dwp file, but the type units DWO name
  ///     doesn't match the originating skeleton compile unit's entry
  ///   Returns std::nullopt if this entry is not a foreign type unit entry.
  std::optional<DWARFTypeUnit *>
  GetForeignTypeUnit(const DebugNames::Entry &entry) const;

  bool ProcessEntry(const DebugNames::Entry &entry,
                    llvm::function_ref<bool(DWARFDIE die)> callback);

  /// Returns true if `parent_entries` have identical names to `parent_names`.
  bool SameParentChain(llvm::ArrayRef<llvm::StringRef> parent_names,
                       llvm::ArrayRef<DebugNames::Entry> parent_entries) const;

  static void MaybeLogLookupError(llvm::Error error,
                                  const DebugNames::NameIndex &ni,
                                  llvm::StringRef name);

  static llvm::DenseSet<dw_offset_t> GetUnits(const DebugNames &debug_names);
  static llvm::DenseSet<uint64_t>
  GetTypeUnitSignatures(const DebugNames &debug_names);
};

} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DEBUGNAMESDWARFINDEX_H
