//==- SymbolCache.h - Cache of native symbols and ids ------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_SYMBOLCACHE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/Line.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSourceFile.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"

#include <memory>
#include <vector>

namespace llvm {
namespace codeview {
class InlineSiteSym;
struct FileChecksumEntry;
} // namespace codeview
namespace pdb {
class IPDBSourceFile;
class NativeSession;
class PDBSymbol;
class PDBSymbolCompiland;
class DbiStream;

class SymbolCache {
  NativeSession &Session;
  DbiStream *Dbi = nullptr;

  /// Cache of all stable symbols, indexed by SymIndexId.  Just because a
  /// symbol has been parsed does not imply that it will be stable and have
  /// an Id.  Id allocation is an implementation, with the only guarantee
  /// being that once an Id is allocated, the symbol can be assumed to be
  /// cached.
  mutable std::vector<std::unique_ptr<NativeRawSymbol>> Cache;

  /// For type records from the TPI stream which have been paresd and cached,
  /// stores a mapping to SymIndexId of the cached symbol.
  mutable DenseMap<codeview::TypeIndex, SymIndexId> TypeIndexToSymbolId;

  /// For field list members which have been parsed and cached, stores a mapping
  /// from (IndexOfClass, MemberIndex) to the corresponding SymIndexId of the
  /// cached symbol.
  mutable DenseMap<std::pair<codeview::TypeIndex, uint32_t>, SymIndexId>
      FieldListMembersToSymbolId;

  /// List of SymIndexIds for each compiland, indexed by compiland index as they
  /// appear in the PDB file.
  mutable std::vector<SymIndexId> Compilands;

  /// List of source files, indexed by unique source file index.
  mutable std::vector<std::unique_ptr<NativeSourceFile>> SourceFiles;

  /// Map from string table offset to source file Id.
  mutable DenseMap<uint32_t, SymIndexId> FileNameOffsetToId;

  /// Map from global symbol offset to SymIndexId.
  mutable DenseMap<uint32_t, SymIndexId> GlobalOffsetToSymbolId;

  /// Map from segment and code offset to function symbols.
  mutable DenseMap<std::pair<uint32_t, uint32_t>, SymIndexId> AddressToSymbolId;
  /// Map from segment and code offset to public symbols.
  mutable DenseMap<std::pair<uint32_t, uint32_t>, SymIndexId>
      AddressToPublicSymId;

  /// Map from module index and symbol table offset to SymIndexId.
  mutable DenseMap<std::pair<uint16_t, uint32_t>, SymIndexId>
      SymTabOffsetToSymbolId;

  struct LineTableEntry {
    uint64_t Addr;
    codeview::LineInfo Line;
    uint32_t ColumnNumber;
    uint32_t FileNameIndex;
    bool IsTerminalEntry;
  };

  std::vector<LineTableEntry> findLineTable(uint16_t Modi) const;
  mutable DenseMap<uint16_t, std::vector<LineTableEntry>> LineTable;

  SymIndexId createSymbolPlaceholder() const {
    SymIndexId Id = Cache.size();
    Cache.push_back(nullptr);
    return Id;
  }

  template <typename ConcreteSymbolT, typename CVRecordT, typename... Args>
  SymIndexId createSymbolForType(codeview::TypeIndex TI, codeview::CVType CVT,
                                 Args &&...ConstructorArgs) const {
    CVRecordT Record;
    if (auto EC =
            codeview::TypeDeserializer::deserializeAs<CVRecordT>(CVT, Record)) {
      consumeError(std::move(EC));
      return 0;
    }

    return createSymbol<ConcreteSymbolT>(
        TI, std::move(Record), std::forward<Args>(ConstructorArgs)...);
  }

  SymIndexId createSymbolForModifiedType(codeview::TypeIndex ModifierTI,
                                         codeview::CVType CVT) const;

  SymIndexId createSimpleType(codeview::TypeIndex TI,
                              codeview::ModifierOptions Mods) const;

  std::unique_ptr<PDBSymbol> findFunctionSymbolBySectOffset(uint32_t Sect,
                                                            uint32_t Offset);
  std::unique_ptr<PDBSymbol> findPublicSymbolBySectOffset(uint32_t Sect,
                                                          uint32_t Offset);

public:
  SymbolCache(NativeSession &Session, DbiStream *Dbi);

  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId createSymbol(Args &&...ConstructorArgs) const {
    SymIndexId Id = Cache.size();

    // Initial construction must not access the cache, since it must be done
    // atomically.
    auto Result = std::make_unique<ConcreteSymbolT>(
        Session, Id, std::forward<Args>(ConstructorArgs)...);
    Result->SymbolId = Id;

    NativeRawSymbol *NRS = static_cast<NativeRawSymbol *>(Result.get());
    Cache.push_back(std::move(Result));

    // After the item is in the cache, we can do further initialization which
    // is then allowed to access the cache.
    NRS->initialize();
    return Id;
  }

  std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(codeview::TypeLeafKind Kind);

  std::unique_ptr<IPDBEnumSymbols>
  createTypeEnumerator(std::vector<codeview::TypeLeafKind> Kinds);

  std::unique_ptr<IPDBEnumSymbols>
  createGlobalsEnumerator(codeview::SymbolKind Kind);

  SymIndexId findSymbolByTypeIndex(codeview::TypeIndex TI) const;

  template <typename ConcreteSymbolT, typename... Args>
  SymIndexId getOrCreateFieldListMember(codeview::TypeIndex FieldListTI,
                                        uint32_t Index,
                                        Args &&... ConstructorArgs) {
    SymIndexId SymId = Cache.size();
    std::pair<codeview::TypeIndex, uint32_t> Key{FieldListTI, Index};
    auto Result = FieldListMembersToSymbolId.try_emplace(Key, SymId);
    if (Result.second)
      SymId =
          createSymbol<ConcreteSymbolT>(std::forward<Args>(ConstructorArgs)...);
    else
      SymId = Result.first->second;
    return SymId;
  }

  SymIndexId getOrCreateGlobalSymbolByOffset(uint32_t Offset);
  SymIndexId getOrCreateInlineSymbol(codeview::InlineSiteSym Sym,
                                     uint64_t ParentAddr, uint16_t Modi,
                                     uint32_t RecordOffset) const;

  std::unique_ptr<PDBSymbol>
  findSymbolBySectOffset(uint32_t Sect, uint32_t Offset, PDB_SymType Type);

  std::unique_ptr<IPDBEnumLineNumbers>
  findLineNumbersByVA(uint64_t VA, uint32_t Length) const;

  std::unique_ptr<PDBSymbolCompiland> getOrCreateCompiland(uint32_t Index);
  uint32_t getNumCompilands() const;

  std::unique_ptr<PDBSymbol> getSymbolById(SymIndexId SymbolId) const;

  NativeRawSymbol &getNativeSymbolById(SymIndexId SymbolId) const;

  template <typename ConcreteT>
  ConcreteT &getNativeSymbolById(SymIndexId SymbolId) const {
    return static_cast<ConcreteT &>(getNativeSymbolById(SymbolId));
  }

  std::unique_ptr<IPDBSourceFile> getSourceFileById(SymIndexId FileId) const;
  SymIndexId
  getOrCreateSourceFile(const codeview::FileChecksumEntry &Checksum) const;
};

} // namespace pdb
} // namespace llvm

#endif
