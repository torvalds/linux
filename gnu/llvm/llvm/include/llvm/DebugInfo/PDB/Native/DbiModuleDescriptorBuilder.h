//===- DbiModuleDescriptorBuilder.h - PDB module information ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <string>
#include <vector>

namespace llvm {
class BinaryStreamWriter;
namespace codeview {
class DebugSubsection;
}

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {

// Represents merged or unmerged symbols. Merged symbols can be written to the
// output file as is, but unmerged symbols must be rewritten first. In either
// case, the size must be known up front.
struct SymbolListWrapper {
  explicit SymbolListWrapper(ArrayRef<uint8_t> Syms)
      : SymPtr(const_cast<uint8_t *>(Syms.data())), SymSize(Syms.size()),
        NeedsToBeMerged(false) {}
  explicit SymbolListWrapper(void *SymSrc, uint32_t Length)
      : SymPtr(SymSrc), SymSize(Length), NeedsToBeMerged(true) {}

  ArrayRef<uint8_t> asArray() const {
    return ArrayRef<uint8_t>(static_cast<const uint8_t *>(SymPtr), SymSize);
  }

  uint32_t size() const { return SymSize; }

  void *SymPtr = nullptr;
  uint32_t SymSize = 0;
  bool NeedsToBeMerged = false;
};

/// Represents a string table reference at some offset in the module symbol
/// stream.
struct StringTableFixup {
  uint32_t StrTabOffset = 0;
  uint32_t SymOffsetOfReference = 0;
};

class DbiModuleDescriptorBuilder {
  friend class DbiStreamBuilder;

public:
  DbiModuleDescriptorBuilder(StringRef ModuleName, uint32_t ModIndex,
                             msf::MSFBuilder &Msf);
  ~DbiModuleDescriptorBuilder();

  DbiModuleDescriptorBuilder(const DbiModuleDescriptorBuilder &) = delete;
  DbiModuleDescriptorBuilder &
  operator=(const DbiModuleDescriptorBuilder &) = delete;

  void setPdbFilePathNI(uint32_t NI);
  void setObjFileName(StringRef Name);

  // Callback to merge one source of unmerged symbols.
  using MergeSymbolsCallback = Error (*)(void *Ctx, void *Symbols,
                                         BinaryStreamWriter &Writer);

  void setMergeSymbolsCallback(void *Ctx, MergeSymbolsCallback Callback) {
    MergeSymsCtx = Ctx;
    MergeSymsCallback = Callback;
  }

  void setStringTableFixups(std::vector<StringTableFixup> &&Fixups) {
    StringTableFixups = std::move(Fixups);
  }

  void setFirstSectionContrib(const SectionContrib &SC);
  void addSymbol(codeview::CVSymbol Symbol);
  void addSymbolsInBulk(ArrayRef<uint8_t> BulkSymbols);

  // Add symbols of known size which will be merged (rewritten) when committing
  // the PDB to disk.
  void addUnmergedSymbols(void *SymSrc, uint32_t SymLength);

  void
  addDebugSubsection(std::shared_ptr<codeview::DebugSubsection> Subsection);

  void
  addDebugSubsection(const codeview::DebugSubsectionRecord &SubsectionContents);

  uint16_t getStreamIndex() const;
  StringRef getModuleName() const { return ModuleName; }
  StringRef getObjFileName() const { return ObjFileName; }

  unsigned getModuleIndex() const { return Layout.Mod; }

  ArrayRef<std::string> source_files() const { return SourceFiles; }

  uint32_t calculateSerializedLength() const;

  /// Return the offset within the module symbol stream of the next symbol
  /// record passed to addSymbol. Add four to account for the signature.
  uint32_t getNextSymbolOffset() const { return SymbolByteSize + 4; }

  void finalize();
  Error finalizeMsfLayout();

  /// Commit the DBI descriptor to the DBI stream.
  Error commit(BinaryStreamWriter &ModiWriter);

  /// Commit the accumulated symbols to the module symbol stream. Safe to call
  /// in parallel on different DbiModuleDescriptorBuilder objects. Only modifies
  /// the pre-allocated stream in question.
  Error commitSymbolStream(const msf::MSFLayout &MsfLayout,
                           WritableBinaryStreamRef MsfBuffer);

private:
  uint32_t calculateC13DebugInfoSize() const;

  void addSourceFile(StringRef Path);
  msf::MSFBuilder &MSF;

  uint32_t SymbolByteSize = 0;
  uint32_t PdbFilePathNI = 0;
  std::string ModuleName;
  std::string ObjFileName;
  std::vector<std::string> SourceFiles;
  std::vector<SymbolListWrapper> Symbols;

  void *MergeSymsCtx = nullptr;
  MergeSymbolsCallback MergeSymsCallback = nullptr;

  std::vector<StringTableFixup> StringTableFixups;

  std::vector<codeview::DebugSubsectionRecordBuilder> C13Builders;

  ModuleInfoHeader Layout;
};

} // end namespace pdb

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTORBUILDER_H
