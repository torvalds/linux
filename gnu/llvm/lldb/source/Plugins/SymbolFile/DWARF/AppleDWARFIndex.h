//===-- AppleDWARFIndex.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_APPLEDWARFINDEX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_APPLEDWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "llvm/DebugInfo/DWARF/DWARFAcceleratorTable.h"

namespace lldb_private::plugin {
namespace dwarf {
class AppleDWARFIndex : public DWARFIndex {
public:
  static std::unique_ptr<AppleDWARFIndex>
  Create(Module &module, DWARFDataExtractor apple_names,
         DWARFDataExtractor apple_namespaces, DWARFDataExtractor apple_types,
         DWARFDataExtractor apple_objc, DWARFDataExtractor debug_str);

  AppleDWARFIndex(Module &module,
                  std::unique_ptr<llvm::AppleAcceleratorTable> apple_names,
                  std::unique_ptr<llvm::AppleAcceleratorTable> apple_namespaces,
                  std::unique_ptr<llvm::AppleAcceleratorTable> apple_types,
                  std::unique_ptr<llvm::AppleAcceleratorTable> apple_objc,
                  lldb::DataBufferSP apple_names_storage,
                  lldb::DataBufferSP apple_namespaces_storage,
                  lldb::DataBufferSP apple_types_storage,
                  lldb::DataBufferSP apple_objc_storage)
      : DWARFIndex(module), m_apple_names_up(std::move(apple_names)),
        m_apple_namespaces_up(std::move(apple_namespaces)),
        m_apple_types_up(std::move(apple_types)),
        m_apple_objc_up(std::move(apple_objc)),
        m_apple_names_storage(apple_names_storage),
        m_apple_namespaces_storage(apple_namespaces_storage),
        m_apple_types_storage(apple_types_storage),
        m_apple_objc_storage(apple_objc_storage) {}

  void Preload() override {}

  void
  GetGlobalVariables(ConstString basename,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(const RegularExpression &regex,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(DWARFUnit &cu,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetObjCMethods(ConstString class_name,
                      llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void GetCompleteObjCClass(
      ConstString class_name, bool must_be_implementation,
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
  std::unique_ptr<llvm::AppleAcceleratorTable> m_apple_names_up;
  std::unique_ptr<llvm::AppleAcceleratorTable> m_apple_namespaces_up;
  std::unique_ptr<llvm::AppleAcceleratorTable> m_apple_types_up;
  std::unique_ptr<llvm::AppleAcceleratorTable> m_apple_objc_up;
  /// The following storage variables hold the data that the apple accelerator
  /// tables tables above point to.
  /// {
  lldb::DataBufferSP m_apple_names_storage;
  lldb::DataBufferSP m_apple_namespaces_storage;
  lldb::DataBufferSP m_apple_types_storage;
  lldb::DataBufferSP m_apple_objc_storage;
  /// }

  /// Search for entries whose name is `name` in `table`, calling `callback` for
  /// each match. If `search_for_tag` is provided, ignore entries whose tag is
  /// not `search_for_tag`. If `search_for_qualhash` is provided, ignore entries
  /// whose qualified name hash does not match `search_for_qualhash`.
  /// If `callback` returns false for an entry, the search is interrupted.
  void SearchFor(const llvm::AppleAcceleratorTable &table, llvm::StringRef name,
                 llvm::function_ref<bool(DWARFDIE die)> callback,
                 std::optional<dw_tag_t> search_for_tag = std::nullopt,
                 std::optional<uint32_t> search_for_qualhash = std::nullopt);
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_APPLEDWARFINDEX_H
