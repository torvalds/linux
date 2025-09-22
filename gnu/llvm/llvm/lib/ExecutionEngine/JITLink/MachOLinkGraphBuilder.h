//===----- MachOLinkGraphBuilder.h - MachO LinkGraph builder ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic MachO LinkGraph building code.
//
//===----------------------------------------------------------------------===//

#ifndef LIB_EXECUTIONENGINE_JITLINK_MACHOLINKGRAPHBUILDER_H
#define LIB_EXECUTIONENGINE_JITLINK_MACHOLINKGRAPHBUILDER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Object/MachO.h"

#include "EHFrameSupportImpl.h"
#include "JITLinkGeneric.h"

#include <list>

namespace llvm {
namespace jitlink {

class MachOLinkGraphBuilder {
public:
  virtual ~MachOLinkGraphBuilder();
  Expected<std::unique_ptr<LinkGraph>> buildGraph();

protected:

  struct NormalizedSymbol {
    friend class MachOLinkGraphBuilder;

  private:
    NormalizedSymbol(std::optional<StringRef> Name, uint64_t Value,
                     uint8_t Type, uint8_t Sect, uint16_t Desc, Linkage L,
                     Scope S)
        : Name(Name), Value(Value), Type(Type), Sect(Sect), Desc(Desc), L(L),
          S(S) {
      assert((!Name || !Name->empty()) && "Name must be none or non-empty");
    }

  public:
    NormalizedSymbol(const NormalizedSymbol &) = delete;
    NormalizedSymbol &operator=(const NormalizedSymbol &) = delete;
    NormalizedSymbol(NormalizedSymbol &&) = delete;
    NormalizedSymbol &operator=(NormalizedSymbol &&) = delete;

    std::optional<StringRef> Name;
    uint64_t Value = 0;
    uint8_t Type = 0;
    uint8_t Sect = 0;
    uint16_t Desc = 0;
    Linkage L = Linkage::Strong;
    Scope S = Scope::Default;
    Symbol *GraphSymbol = nullptr;
  };

  // Normalized section representation. Section and segment names are guaranteed
  // to be null-terminated, hence the extra bytes on SegName and SectName.
  class NormalizedSection {
    friend class MachOLinkGraphBuilder;

  private:
    NormalizedSection() = default;

  public:
    char SectName[17];
    char SegName[17];
    orc::ExecutorAddr Address;
    uint64_t Size = 0;
    uint64_t Alignment = 0;
    uint32_t Flags = 0;
    const char *Data = nullptr;
    Section *GraphSection = nullptr;
    std::map<orc::ExecutorAddr, Symbol *> CanonicalSymbols;
  };

  using SectionParserFunction = std::function<Error(NormalizedSection &S)>;

  MachOLinkGraphBuilder(const object::MachOObjectFile &Obj, Triple TT,
                        SubtargetFeatures Features,
                        LinkGraph::GetEdgeKindNameFunction GetEdgeKindName);

  LinkGraph &getGraph() const { return *G; }

  const object::MachOObjectFile &getObject() const { return Obj; }

  void addCustomSectionParser(StringRef SectionName,
                              SectionParserFunction Parse);

  virtual Error addRelocations() = 0;

  /// Create a symbol.
  template <typename... ArgTs>
  NormalizedSymbol &createNormalizedSymbol(ArgTs &&... Args) {
    NormalizedSymbol *Sym = reinterpret_cast<NormalizedSymbol *>(
        Allocator.Allocate<NormalizedSymbol>());
    new (Sym) NormalizedSymbol(std::forward<ArgTs>(Args)...);
    return *Sym;
  }

  /// Index is zero-based (MachO section indexes are usually one-based) and
  /// assumed to be in-range. Client is responsible for checking.
  NormalizedSection &getSectionByIndex(unsigned Index) {
    auto I = IndexToSection.find(Index);
    assert(I != IndexToSection.end() && "No section recorded at index");
    return I->second;
  }

  /// Try to get the section at the given index. Will return an error if the
  /// given index is out of range, or if no section has been added for the given
  /// index.
  Expected<NormalizedSection &> findSectionByIndex(unsigned Index) {
    auto I = IndexToSection.find(Index);
    if (I == IndexToSection.end())
      return make_error<JITLinkError>("No section recorded for index " +
                                      formatv("{0:d}", Index));
    return I->second;
  }

  /// Try to get the symbol at the given index. Will return an error if the
  /// given index is out of range, or if no symbol has been added for the given
  /// index.
  Expected<NormalizedSymbol &> findSymbolByIndex(uint64_t Index) {
    auto I = IndexToSymbol.find(Index);
    if (I == IndexToSymbol.end())
      return make_error<JITLinkError>("No symbol at index " +
                                      formatv("{0:d}", Index));
    assert(I->second && "Null symbol at index");
    return *I->second;
  }

  /// Returns the symbol with the highest address not greater than the search
  /// address, or null if no such symbol exists.
  Symbol *getSymbolByAddress(NormalizedSection &NSec,
                             orc::ExecutorAddr Address) {
    auto I = NSec.CanonicalSymbols.upper_bound(Address);
    if (I == NSec.CanonicalSymbols.begin())
      return nullptr;
    return std::prev(I)->second;
  }

  /// Returns the symbol with the highest address not greater than the search
  /// address, or an error if no such symbol exists.
  Expected<Symbol &> findSymbolByAddress(NormalizedSection &NSec,
                                         orc::ExecutorAddr Address) {
    auto *Sym = getSymbolByAddress(NSec, Address);
    if (Sym)
      if (Address <= Sym->getAddress() + Sym->getSize())
        return *Sym;
    return make_error<JITLinkError>("No symbol covering address " +
                                    formatv("{0:x16}", Address));
  }

  static Linkage getLinkage(uint16_t Desc);
  static Scope getScope(StringRef Name, uint8_t Type);
  static bool isAltEntry(const NormalizedSymbol &NSym);

  static bool isDebugSection(const NormalizedSection &NSec);
  static bool isZeroFillSection(const NormalizedSection &NSec);

  MachO::relocation_info
  getRelocationInfo(const object::relocation_iterator RelItr) {
    MachO::any_relocation_info ARI =
        getObject().getRelocation(RelItr->getRawDataRefImpl());
    MachO::relocation_info RI;
    RI.r_address = ARI.r_word0;
    RI.r_symbolnum = ARI.r_word1 & 0xffffff;
    RI.r_pcrel = (ARI.r_word1 >> 24) & 1;
    RI.r_length = (ARI.r_word1 >> 25) & 3;
    RI.r_extern = (ARI.r_word1 >> 27) & 1;
    RI.r_type = (ARI.r_word1 >> 28);
    return RI;
  }

private:
  static unsigned getPointerSize(const object::MachOObjectFile &Obj);
  static llvm::endianness getEndianness(const object::MachOObjectFile &Obj);

  void setCanonicalSymbol(NormalizedSection &NSec, Symbol &Sym) {
    auto *&CanonicalSymEntry = NSec.CanonicalSymbols[Sym.getAddress()];
    // There should be no symbol at this address, or, if there is,
    // it should be a zero-sized symbol from an empty section (which
    // we can safely override).
    assert((!CanonicalSymEntry || CanonicalSymEntry->getSize() == 0) &&
           "Duplicate canonical symbol at address");
    CanonicalSymEntry = &Sym;
  }

  Section &getCommonSection();
  void addSectionStartSymAndBlock(unsigned SecIndex, Section &GraphSec,
                                  orc::ExecutorAddr Address, const char *Data,
                                  orc::ExecutorAddrDiff Size,
                                  uint32_t Alignment, bool IsLive);

  Error createNormalizedSections();
  Error createNormalizedSymbols();

  /// Create graph blocks and symbols for externals, absolutes, commons and
  /// all defined symbols in sections without custom parsers.
  Error graphifyRegularSymbols();

  /// Create and return a graph symbol for the given normalized symbol.
  ///
  /// NSym's GraphSymbol member will be updated to point at the newly created
  /// symbol.
  Symbol &createStandardGraphSymbol(NormalizedSymbol &Sym, Block &B,
                                    size_t Size, bool IsText,
                                    bool IsNoDeadStrip, bool IsCanonical);

  /// Create graph blocks and symbols for all sections.
  Error graphifySectionsWithCustomParsers();

  /// Graphify cstring section.
  Error graphifyCStringSection(NormalizedSection &NSec,
                               std::vector<NormalizedSymbol *> NSyms);

  // Put the BumpPtrAllocator first so that we don't free any of the underlying
  // memory until the Symbol/Addressable destructors have been run.
  BumpPtrAllocator Allocator;

  const object::MachOObjectFile &Obj;
  std::unique_ptr<LinkGraph> G;

  bool SubsectionsViaSymbols = false;
  DenseMap<unsigned, NormalizedSection> IndexToSection;
  Section *CommonSection = nullptr;

  DenseMap<uint32_t, NormalizedSymbol *> IndexToSymbol;
  StringMap<SectionParserFunction> CustomSectionParserFunctions;
};

/// A pass to split up __LD,__compact_unwind sections.
class CompactUnwindSplitter {
public:
  CompactUnwindSplitter(StringRef CompactUnwindSectionName)
      : CompactUnwindSectionName(CompactUnwindSectionName) {}
  Error operator()(LinkGraph &G);

private:
  StringRef CompactUnwindSectionName;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LIB_EXECUTIONENGINE_JITLINK_MACHOLINKGRAPHBUILDER_H
