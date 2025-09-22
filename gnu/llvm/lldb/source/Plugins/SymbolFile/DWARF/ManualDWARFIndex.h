//===-- ManualDWARFIndex.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_MANUALDWARFINDEX_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_MANUALDWARFINDEX_H

#include "Plugins/SymbolFile/DWARF/DWARFIndex.h"
#include "Plugins/SymbolFile/DWARF/NameToDIE.h"
#include "llvm/ADT/DenseSet.h"

namespace lldb_private::plugin {
namespace dwarf {
class DWARFDebugInfo;
class SymbolFileDWARFDwo;

class ManualDWARFIndex : public DWARFIndex {
public:
  ManualDWARFIndex(Module &module, SymbolFileDWARF &dwarf,
                   llvm::DenseSet<dw_offset_t> units_to_avoid = {},
                   llvm::DenseSet<uint64_t> type_sigs_to_avoid = {})
      : DWARFIndex(module), m_dwarf(&dwarf),
        m_units_to_avoid(std::move(units_to_avoid)),
        m_type_sigs_to_avoid(std::move(type_sigs_to_avoid)) {}

  void Preload() override { Index(); }

  void
  GetGlobalVariables(ConstString basename,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(const RegularExpression &regex,
                     llvm::function_ref<bool(DWARFDIE die)> callback) override;
  void
  GetGlobalVariables(DWARFUnit &unit,
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

  // Make IndexSet public so we can unit test the encoding and decoding logic.
  struct IndexSet {
    NameToDIE function_basenames;
    NameToDIE function_fullnames;
    NameToDIE function_methods;
    NameToDIE function_selectors;
    NameToDIE objc_class_selectors;
    NameToDIE globals;
    NameToDIE types;
    NameToDIE namespaces;
    bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr);
    void Encode(DataEncoder &encoder) const;
    bool operator==(const IndexSet &rhs) const {
      return function_basenames == rhs.function_basenames &&
             function_fullnames == rhs.function_fullnames &&
             function_methods == rhs.function_methods &&
             function_selectors == rhs.function_selectors &&
             objc_class_selectors == rhs.objc_class_selectors &&
             globals == rhs.globals && types == rhs.types &&
             namespaces == rhs.namespaces;
    }
  };

private:
  void Index();

  /// Decode a serialized version of this object from data.
  ///
  /// \param data
  ///   The decoder object that references the serialized data.
  ///
  /// \param offset_ptr
  ///   A pointer that contains the offset from which the data will be decoded
  ///   from that gets updated as data gets decoded.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  bool Decode(const DataExtractor &data, lldb::offset_t *offset_ptr,
              bool &signature_mismatch);

  /// Encode this object into a data encoder object.
  ///
  /// This allows this object to be serialized to disk.
  ///
  /// \param encoder
  ///   A data encoder object that serialized bytes will be encoded into.
  ///
  /// \param strtab
  ///   All strings in cache files are put into string tables for efficiency
  ///   and cache file size reduction. Strings are stored as uint32_t string
  ///   table offsets in the cache data.
  ///
  /// \return
  ///   True if the symbol table's object file can generate a valid signature
  ///   and all data for the symbol table was encoded, false otherwise.
  bool Encode(DataEncoder &encoder) const;

  /// Get the cache key string for this symbol table.
  ///
  /// The cache key must start with the module's cache key and is followed
  /// by information that indicates this key is for caching the symbol table
  /// contents and should also include the has of the object file. A module can
  /// be represented by an ObjectFile object for the main executable, but can
  /// also have a symbol file that is from the same or a different object file.
  /// This means we might have two symbol tables cached in the index cache, one
  /// for the main executable and one for the symbol file.
  ///
  /// \return
  ///   The unique cache key used to save and retrieve data from the index
  ///   cache.
  std::string GetCacheKey();

  /// Save the symbol table data out into a cache.
  ///
  /// The symbol table will only be saved to a cache file if caching is enabled.
  ///
  /// We cache the contents of the symbol table since symbol tables in LLDB take
  /// some time to initialize. This is due to the many sources for data that are
  /// used to create a symbol table:
  /// - standard symbol table
  /// - dynamic symbol table (ELF)
  /// - compressed debug info sections
  /// - unwind information
  /// - function pointers found in runtimes for global constructor/destructors
  /// - other sources.
  /// All of the above sources are combined and one symbol table results after
  /// all sources have been considered.
  void SaveToCache();

  /// Load the symbol table from the index cache.
  ///
  /// Quickly load the finalized symbol table from the index cache. This saves
  /// time when the debugger starts up. The index cache file for the symbol
  /// table has the modification time set to the same time as the main module.
  /// If the cache file exists and the modification times match, we will load
  /// the symbol table from the serlized cache file.
  ///
  /// \return
  ///   True if the symbol table was successfully loaded from the index cache,
  ///   false if the symbol table wasn't cached or was out of date.
  bool LoadFromCache();

  void IndexUnit(DWARFUnit &unit, SymbolFileDWARFDwo *dwp, IndexSet &set);

  static void IndexUnitImpl(DWARFUnit &unit,
                            const lldb::LanguageType cu_language,
                            IndexSet &set);

  /// The DWARF file which we are indexing.
  SymbolFileDWARF *m_dwarf;
  /// Which dwarf units should we skip while building the index.
  llvm::DenseSet<dw_offset_t> m_units_to_avoid;
  llvm::DenseSet<uint64_t> m_type_sigs_to_avoid;

  IndexSet m_set;
  bool m_indexed = false;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_MANUALDWARFINDEX_H
