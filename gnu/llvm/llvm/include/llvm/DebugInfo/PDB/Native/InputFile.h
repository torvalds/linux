//===- InputFile.h -------------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INPUTFILE_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INPUTFILE_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace codeview {
class LazyRandomTypeCollection;
}
namespace object {
class COFFObjectFile;
} // namespace object

namespace pdb {
class InputFile;
class LinePrinter;
class PDBFile;
class NativeSession;
class SymbolGroupIterator;
class SymbolGroup;

class InputFile {
  InputFile();

  std::unique_ptr<NativeSession> PdbSession;
  object::OwningBinary<object::Binary> CoffObject;
  std::unique_ptr<MemoryBuffer> UnknownFile;
  PointerUnion<PDBFile *, object::COFFObjectFile *, MemoryBuffer *> PdbOrObj;

  using TypeCollectionPtr = std::unique_ptr<codeview::LazyRandomTypeCollection>;

  TypeCollectionPtr Types;
  TypeCollectionPtr Ids;

  enum TypeCollectionKind { kTypes, kIds };
  codeview::LazyRandomTypeCollection &
  getOrCreateTypeCollection(TypeCollectionKind Kind);

public:
  InputFile(PDBFile *Pdb) { PdbOrObj = Pdb; }
  InputFile(object::COFFObjectFile *Obj) { PdbOrObj = Obj; }
  InputFile(MemoryBuffer *Buffer) { PdbOrObj = Buffer; }
  ~InputFile();
  InputFile(InputFile &&Other) = default;

  static Expected<InputFile> open(StringRef Path,
                                  bool AllowUnknownFile = false);

  PDBFile &pdb();
  const PDBFile &pdb() const;
  object::COFFObjectFile &obj();
  const object::COFFObjectFile &obj() const;
  MemoryBuffer &unknown();
  const MemoryBuffer &unknown() const;

  StringRef getFilePath() const;

  bool hasTypes() const;
  bool hasIds() const;

  codeview::LazyRandomTypeCollection &types();
  codeview::LazyRandomTypeCollection &ids();

  iterator_range<SymbolGroupIterator> symbol_groups();
  SymbolGroupIterator symbol_groups_begin();
  SymbolGroupIterator symbol_groups_end();

  bool isPdb() const;
  bool isObj() const;
  bool isUnknown() const;
};

class SymbolGroup {
  friend class SymbolGroupIterator;

public:
  explicit SymbolGroup(InputFile *File, uint32_t GroupIndex = 0);

  Expected<StringRef> getNameFromStringTable(uint32_t Offset) const;
  Expected<StringRef> getNameFromChecksums(uint32_t Offset) const;

  void formatFromFileName(LinePrinter &Printer, StringRef File,
                          bool Append = false) const;

  void formatFromChecksumsOffset(LinePrinter &Printer, uint32_t Offset,
                                 bool Append = false) const;

  StringRef name() const;

  codeview::DebugSubsectionArray getDebugSubsections() const {
    return Subsections;
  }
  const ModuleDebugStreamRef &getPdbModuleStream() const;

  const InputFile &getFile() const { return *File; }
  InputFile &getFile() { return *File; }

  bool hasDebugStream() const { return DebugStream != nullptr; }

private:
  void initializeForPdb(uint32_t Modi);
  void updatePdbModi(uint32_t Modi);
  void updateDebugS(const codeview::DebugSubsectionArray &SS);

  void rebuildChecksumMap();
  InputFile *File = nullptr;
  StringRef Name;
  codeview::DebugSubsectionArray Subsections;
  std::shared_ptr<ModuleDebugStreamRef> DebugStream;
  codeview::StringsAndChecksumsRef SC;
  StringMap<codeview::FileChecksumEntry> ChecksumsByFile;
};

class SymbolGroupIterator
    : public iterator_facade_base<SymbolGroupIterator,
                                  std::forward_iterator_tag, SymbolGroup> {
public:
  SymbolGroupIterator();
  explicit SymbolGroupIterator(InputFile &File);
  SymbolGroupIterator(const SymbolGroupIterator &Other) = default;
  SymbolGroupIterator &operator=(const SymbolGroupIterator &R) = default;

  const SymbolGroup &operator*() const;
  SymbolGroup &operator*();

  bool operator==(const SymbolGroupIterator &R) const;
  SymbolGroupIterator &operator++();

private:
  void scanToNextDebugS();
  bool isEnd() const;

  uint32_t Index = 0;
  std::optional<object::section_iterator> SectionIter;
  SymbolGroup Value;
};

Expected<ModuleDebugStreamRef>
getModuleDebugStream(PDBFile &File, StringRef &ModuleName, uint32_t Index);
Expected<ModuleDebugStreamRef> getModuleDebugStream(PDBFile &File,
                                                    uint32_t Index);

bool shouldDumpSymbolGroup(uint32_t Idx, const SymbolGroup &Group,
                           const FilterOptions &Filters);

// TODO: Change these callbacks to be function_refs (de-templatify them).
template <typename CallbackT>
Error iterateOneModule(InputFile &File, const PrintScope &HeaderScope,
                       const SymbolGroup &SG, uint32_t Modi,
                       CallbackT Callback) {
  HeaderScope.P.formatLine(
      "Mod {0:4} | `{1}`: ",
      fmt_align(Modi, AlignStyle::Right, HeaderScope.LabelWidth), SG.name());

  AutoIndent Indent(HeaderScope);
  return Callback(Modi, SG);
}

template <typename CallbackT>
Error iterateSymbolGroups(InputFile &Input, const PrintScope &HeaderScope,
                          CallbackT Callback) {
  AutoIndent Indent(HeaderScope);

  FilterOptions Filters = HeaderScope.P.getFilters();
  if (Filters.DumpModi) {
    uint32_t Modi = *Filters.DumpModi;
    SymbolGroup SG(&Input, Modi);
    return iterateOneModule(Input, withLabelWidth(HeaderScope, NumDigits(Modi)),
                            SG, Modi, Callback);
  }

  uint32_t I = 0;

  for (const auto &SG : Input.symbol_groups()) {
    if (shouldDumpSymbolGroup(I, SG, Filters))
      if (auto Err =
              iterateOneModule(Input, withLabelWidth(HeaderScope, NumDigits(I)),
                               SG, I, Callback))
        return Err;

    ++I;
  }
  return Error::success();
}

template <typename SubsectionT>
Error iterateModuleSubsections(
    InputFile &File, const PrintScope &HeaderScope,
    llvm::function_ref<Error(uint32_t, const SymbolGroup &, SubsectionT &)>
        Callback) {

  return iterateSymbolGroups(
      File, HeaderScope, [&](uint32_t Modi, const SymbolGroup &SG) -> Error {
        for (const auto &SS : SG.getDebugSubsections()) {
          SubsectionT Subsection;

          if (SS.kind() != Subsection.kind())
            continue;

          BinaryStreamReader Reader(SS.getRecordData());
          if (auto Err = Subsection.initialize(Reader))
            continue;
          if (auto Err = Callback(Modi, SG, Subsection))
            return Err;
        }
        return Error::success();
      });
}

} // namespace pdb
} // namespace llvm

#endif
