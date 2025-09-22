//===--------- DefineExternalSectionStartAndEndSymbols.h --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility class for recognizing external section start and end symbols and
// transforming them into defined symbols for the start and end blocks of the
// associated Section.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_JITLINK_DEFINEEXTERNALSECTIONSTARTANDENDSYMBOLS_H
#define LLVM_EXECUTIONENGINE_JITLINK_DEFINEEXTERNALSECTIONSTARTANDENDSYMBOLS_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

struct SectionRangeSymbolDesc {
  SectionRangeSymbolDesc() = default;
  SectionRangeSymbolDesc(Section &Sec, bool IsStart)
      : Sec(&Sec), IsStart(IsStart) {}
  Section *Sec = nullptr;
  bool IsStart = false;
};

/// Pass implementation for the createDefineExternalSectionStartAndEndSymbols
/// function.
template <typename SymbolIdentifierFunction>
class DefineExternalSectionStartAndEndSymbols {
public:
  DefineExternalSectionStartAndEndSymbols(SymbolIdentifierFunction F)
      : F(std::move(F)) {}

  Error operator()(LinkGraph &G) {

    // This pass will affect the external symbols set, so copy them out into a
    // vector and iterate over that.
    std::vector<Symbol *> Externals(G.external_symbols().begin(),
                                    G.external_symbols().end());

    for (auto *Sym : Externals) {
      SectionRangeSymbolDesc D = F(G, *Sym);
      if (D.Sec) {
        auto &SR = getSectionRange(*D.Sec);
        if (D.IsStart) {
          if (SR.empty())
            G.makeAbsolute(*Sym, orc::ExecutorAddr());
          else
            G.makeDefined(*Sym, *SR.getFirstBlock(), 0, 0, Linkage::Strong,
                          Scope::Local, false);
        } else {
          if (SR.empty())
            G.makeAbsolute(*Sym, orc::ExecutorAddr());
          else
            G.makeDefined(*Sym, *SR.getLastBlock(),
                          SR.getLastBlock()->getSize(), 0, Linkage::Strong,
                          Scope::Local, false);
        }
      }
    }
    return Error::success();
  }

private:
  SectionRange &getSectionRange(Section &Sec) {
    auto I = SectionRanges.find(&Sec);
    if (I == SectionRanges.end())
      I = SectionRanges.insert(std::make_pair(&Sec, SectionRange(Sec))).first;
    return I->second;
  }

  DenseMap<Section *, SectionRange> SectionRanges;
  SymbolIdentifierFunction F;
};

/// Returns a JITLink pass (as a function class) that uses the given symbol
/// identification function to identify external section start and end symbols
/// (and their associated Section*s) and transform the identified externals
/// into defined symbols pointing to the start of the first block in the
/// section and the end of the last (start and end symbols for empty sections
/// will be transformed into absolute symbols at address 0).
///
/// The identification function should be callable as
///
///   SectionRangeSymbolDesc (LinkGraph &G, Symbol &Sym)
///
/// If Sym is not a section range start or end symbol then a default
/// constructed SectionRangeSymbolDesc should be returned. If Sym is a start
/// symbol then SectionRangeSymbolDesc(Sec, true), where Sec is a reference to
/// the target Section. If Sym is an end symbol then
/// SectionRangeSymbolDesc(Sec, false) should be returned.
///
/// This pass should be run in the PostAllocationPass pipeline, at which point
/// all blocks should have been assigned their final addresses.
template <typename SymbolIdentifierFunction>
DefineExternalSectionStartAndEndSymbols<SymbolIdentifierFunction>
createDefineExternalSectionStartAndEndSymbolsPass(
    SymbolIdentifierFunction &&F) {
  return DefineExternalSectionStartAndEndSymbols<SymbolIdentifierFunction>(
      std::forward<SymbolIdentifierFunction>(F));
}

/// ELF section start/end symbol detection.
inline SectionRangeSymbolDesc
identifyELFSectionStartAndEndSymbols(LinkGraph &G, Symbol &Sym) {
  constexpr StringRef StartSymbolPrefix = "__start_";
  constexpr StringRef EndSymbolPrefix = "__stop_";

  auto SymName = Sym.getName();
  if (SymName.starts_with(StartSymbolPrefix)) {
    if (auto *Sec =
            G.findSectionByName(SymName.drop_front(StartSymbolPrefix.size())))
      return {*Sec, true};
  } else if (SymName.starts_with(EndSymbolPrefix)) {
    if (auto *Sec =
            G.findSectionByName(SymName.drop_front(EndSymbolPrefix.size())))
      return {*Sec, false};
  }
  return {};
}

/// MachO section start/end symbol detection.
inline SectionRangeSymbolDesc
identifyMachOSectionStartAndEndSymbols(LinkGraph &G, Symbol &Sym) {
  constexpr StringRef StartSymbolPrefix = "section$start$";
  constexpr StringRef EndSymbolPrefix = "section$end$";

  auto SymName = Sym.getName();
  if (SymName.starts_with(StartSymbolPrefix)) {
    auto [SegName, SecName] =
      SymName.drop_front(StartSymbolPrefix.size()).split('$');
    std::string SectionName = (SegName + "," + SecName).str();
    if (auto *Sec = G.findSectionByName(SectionName))
      return {*Sec, true};
  } else if (SymName.starts_with(EndSymbolPrefix)) {
    auto [SegName, SecName] =
      SymName.drop_front(EndSymbolPrefix.size()).split('$');
    std::string SectionName = (SegName + "," + SecName).str();
    if (auto *Sec = G.findSectionByName(SectionName))
      return {*Sec, false};
  }
  return {};
}

} // end namespace jitlink
} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_EXECUTIONENGINE_JITLINK_DEFINEEXTERNALSECTIONSTARTANDENDSYMBOLS_H
