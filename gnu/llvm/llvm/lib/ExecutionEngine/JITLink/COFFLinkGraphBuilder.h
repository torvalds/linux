//===----- COFFLinkGraphBuilder.h - COFF LinkGraph builder ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic COFF LinkGraph building code.
//
//===----------------------------------------------------------------------===//

#ifndef LIB_EXECUTIONENGINE_JITLINK_COFFLINKGRAPHBUILDER_H
#define LIB_EXECUTIONENGINE_JITLINK_COFFLINKGRAPHBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Object/COFF.h"

#include "COFFDirectiveParser.h"
#include "EHFrameSupportImpl.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

#include <list>

namespace llvm {
namespace jitlink {

class COFFLinkGraphBuilder {
public:
  virtual ~COFFLinkGraphBuilder();
  Expected<std::unique_ptr<LinkGraph>> buildGraph();

protected:
  using COFFSectionIndex = int32_t;
  using COFFSymbolIndex = int32_t;

  COFFLinkGraphBuilder(const object::COFFObjectFile &Obj, Triple TT,
                       SubtargetFeatures Features,
                       LinkGraph::GetEdgeKindNameFunction GetEdgeKindName);

  LinkGraph &getGraph() const { return *G; }

  const object::COFFObjectFile &getObject() const { return Obj; }

  virtual Error addRelocations() = 0;

  Error graphifySections();
  Error graphifySymbols();

  void setGraphSymbol(COFFSectionIndex SecIndex, COFFSymbolIndex SymIndex,
                      Symbol &Sym) {
    assert(!GraphSymbols[SymIndex] && "Duplicate symbol at index");
    GraphSymbols[SymIndex] = &Sym;
    if (!COFF::isReservedSectionNumber(SecIndex))
      SymbolSets[SecIndex].insert({Sym.getOffset(), &Sym});
  }

  Symbol *getGraphSymbol(COFFSymbolIndex SymIndex) const {
    if (SymIndex < 0 ||
        SymIndex >= static_cast<COFFSymbolIndex>(GraphSymbols.size()))
      return nullptr;
    return GraphSymbols[SymIndex];
  }

  void setGraphBlock(COFFSectionIndex SecIndex, Block *B) {
    assert(!GraphBlocks[SecIndex] && "Duplicate section at index");
    assert(!COFF::isReservedSectionNumber(SecIndex) && "Invalid section index");
    GraphBlocks[SecIndex] = B;
  }

  Block *getGraphBlock(COFFSectionIndex SecIndex) const {
    if (SecIndex <= 0 ||
        SecIndex >= static_cast<COFFSectionIndex>(GraphSymbols.size()))
      return nullptr;
    return GraphBlocks[SecIndex];
  }

  object::COFFObjectFile::section_iterator_range sections() const {
    return Obj.sections();
  }

  /// Traverse all matching relocation records in the given section. The handler
  /// function Func should be callable with this signature:
  ///   Error(const object::RelocationRef&,
  ///         const object::SectionRef&, Section &)
  ///
  template <typename RelocHandlerFunction>
  Error forEachRelocation(const object::SectionRef &RelSec,
                          RelocHandlerFunction &&Func,
                          bool ProcessDebugSections = false);

  /// Traverse all matching relocation records in the given section. Convenience
  /// wrapper to allow passing a member function for the handler.
  ///
  template <typename ClassT, typename RelocHandlerMethod>
  Error forEachRelocation(const object::SectionRef &RelSec, ClassT *Instance,
                          RelocHandlerMethod &&Method,
                          bool ProcessDebugSections = false) {
    return forEachRelocation(
        RelSec,
        [Instance, Method](const auto &Rel, const auto &Target, auto &GS) {
          return (Instance->*Method)(Rel, Target, GS);
        },
        ProcessDebugSections);
  }

private:
  // Pending comdat symbol export that is initiated by the first symbol of
  // COMDAT sequence.
  struct ComdatExportRequest {
    COFFSymbolIndex SymbolIndex;
    jitlink::Linkage Linkage;
    orc::ExecutorAddrDiff Size;
  };
  std::vector<std::optional<ComdatExportRequest>> PendingComdatExports;

  // This represents a pending request to create a weak external symbol with a
  // name.
  struct WeakExternalRequest {
    COFFSymbolIndex Alias;
    COFFSymbolIndex Target;
    uint32_t Characteristics;
    StringRef SymbolName;
  };
  std::vector<WeakExternalRequest> WeakExternalRequests;

  // Per COFF section jitlink symbol set sorted by offset.
  // Used for calculating implicit size of defined symbols.
  using SymbolSet = std::set<std::pair<orc::ExecutorAddrDiff, Symbol *>>;
  std::vector<SymbolSet> SymbolSets;

  Section &getCommonSection();

  Symbol *createExternalSymbol(COFFSymbolIndex SymIndex, StringRef SymbolName,
                               object::COFFSymbolRef Symbol,
                               const object::coff_section *Section);
  Expected<Symbol *> createAliasSymbol(StringRef SymbolName, Linkage L, Scope S,
                                       Symbol &Target);
  Expected<Symbol *> createDefinedSymbol(COFFSymbolIndex SymIndex,
                                         StringRef SymbolName,
                                         object::COFFSymbolRef Symbol,
                                         const object::coff_section *Section);
  Expected<Symbol *> createCOMDATExportRequest(
      COFFSymbolIndex SymIndex, object::COFFSymbolRef Symbol,
      const object::coff_aux_section_definition *Definition);
  Expected<Symbol *> exportCOMDATSymbol(COFFSymbolIndex SymIndex,
                                        StringRef SymbolName,
                                        object::COFFSymbolRef Symbol);

  Error handleDirectiveSection(StringRef Str);
  Error flushWeakAliasRequests();
  Error handleAlternateNames();
  Error calculateImplicitSizeOfSymbols();

  static uint64_t getSectionAddress(const object::COFFObjectFile &Obj,
                                    const object::coff_section *Section);
  static uint64_t getSectionSize(const object::COFFObjectFile &Obj,
                                 const object::coff_section *Section);
  static bool isComdatSection(const object::coff_section *Section);
  static unsigned getPointerSize(const object::COFFObjectFile &Obj);
  static llvm::endianness getEndianness(const object::COFFObjectFile &Obj);
  static StringRef getDLLImportStubPrefix() { return "__imp_"; }
  static StringRef getDirectiveSectionName() { return ".drectve"; }
  StringRef getCOFFSectionName(COFFSectionIndex SectionIndex,
                               const object::coff_section *Sec,
                               object::COFFSymbolRef Sym);

  const object::COFFObjectFile &Obj;
  std::unique_ptr<LinkGraph> G;
  COFFDirectiveParser DirectiveParser;

  Section *CommonSection = nullptr;
  std::vector<Block *> GraphBlocks;
  std::vector<Symbol *> GraphSymbols;

  DenseMap<StringRef, StringRef> AlternateNames;
  DenseMap<StringRef, Symbol *> ExternalSymbols;
  DenseMap<StringRef, Symbol *> DefinedSymbols;
};

template <typename RelocHandlerFunction>
Error COFFLinkGraphBuilder::forEachRelocation(const object::SectionRef &RelSec,
                                              RelocHandlerFunction &&Func,
                                              bool ProcessDebugSections) {

  auto COFFRelSect = Obj.getCOFFSection(RelSec);

  // Target sections have names in valid COFF object files.
  Expected<StringRef> Name = Obj.getSectionName(COFFRelSect);
  if (!Name)
    return Name.takeError();

  // Skip the unhandled metadata sections.
  if (*Name == ".voltbl")
    return Error::success();
  LLVM_DEBUG(dbgs() << "  " << *Name << ":\n");

  // Lookup the link-graph node corresponding to the target section name.
  auto *BlockToFix = getGraphBlock(RelSec.getIndex() + 1);
  if (!BlockToFix)
    return make_error<StringError>(
        "Referencing a section that wasn't added to the graph: " + *Name,
        inconvertibleErrorCode());

  // Let the callee process relocation entries one by one.
  for (const auto &R : RelSec.relocations())
    if (Error Err = Func(R, RelSec, *BlockToFix))
      return Err;

  LLVM_DEBUG(dbgs() << "\n");
  return Error::success();
}

} // end namespace jitlink
} // end namespace llvm

#endif // LIB_EXECUTIONENGINE_JITLINK_COFFLINKGRAPHBUILDER_H
