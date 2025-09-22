//===---- llvm-jitlink-elf.cpp -- ELF parsing support for llvm-jitlink ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF parsing support for llvm-jitlink.
//
//===----------------------------------------------------------------------===//

#include "llvm-jitlink.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/Path.h"

#define DEBUG_TYPE "llvm_jitlink"

using namespace llvm;
using namespace llvm::jitlink;

static bool isELFGOTSection(Section &S) { return S.getName() == "$__GOT"; }

static bool isELFStubsSection(Section &S) { return S.getName() == "$__STUBS"; }

static bool isELFAArch32StubsSection(Section &S) {
  return S.getName().starts_with("__llvm_jitlink_aarch32_STUBS_");
}

static Expected<Edge &> getFirstRelocationEdge(LinkGraph &G, Block &B) {
  auto EItr =
      llvm::find_if(B.edges(), [](Edge &E) { return E.isRelocation(); });
  if (EItr == B.edges().end())
    return make_error<StringError>("GOT entry in " + G.getName() + ", \"" +
                                       B.getSection().getName() +
                                       "\" has no relocations",
                                   inconvertibleErrorCode());
  return *EItr;
}

static Expected<Symbol &> getELFGOTTarget(LinkGraph &G, Block &B) {
  auto E = getFirstRelocationEdge(G, B);
  if (!E)
    return E.takeError();
  auto &TargetSym = E->getTarget();
  if (!TargetSym.hasName())
    return make_error<StringError>(
        "GOT entry in " + G.getName() + ", \"" +
            TargetSym.getBlock().getSection().getName() +
            "\" points to anonymous "
            "symbol",
        inconvertibleErrorCode());
  return TargetSym;
}

static Expected<Symbol &> getELFStubTarget(LinkGraph &G, Block &B) {
  auto E = getFirstRelocationEdge(G, B);
  if (!E)
    return E.takeError();
  auto &GOTSym = E->getTarget();
  if (!GOTSym.isDefined())
    return make_error<StringError>("Stubs entry in " + G.getName() +
                                       " does not point to GOT entry",
                                   inconvertibleErrorCode());
  if (!isELFGOTSection(GOTSym.getBlock().getSection()))
    return make_error<StringError>(
        "Stubs entry in " + G.getName() + ", \"" +
            GOTSym.getBlock().getSection().getName() +
            "\" does not point to GOT entry",
        inconvertibleErrorCode());
  return getELFGOTTarget(G, GOTSym.getBlock());
}

static Expected<Symbol &> getELFAArch32StubTarget(LinkGraph &G, Block &B) {
  auto E = getFirstRelocationEdge(G, B);
  if (!E)
    return E.takeError();
  return E->getTarget();
}

enum SectionType { GOT, Stubs, AArch32Stubs, Other };

static Error registerSymbol(LinkGraph &G, Symbol &Sym, Session::FileInfo &FI,
                            SectionType SecType) {
  switch (SecType) {
  case GOT:
    if (Sym.getSize() == 0)
      return Error::success(); // Skip the GOT start symbol
    return FI.registerGOTEntry(G, Sym, getELFGOTTarget);
  case Stubs:
    return FI.registerStubEntry(G, Sym, getELFStubTarget);
  case AArch32Stubs:
    return FI.registerMultiStubEntry(G, Sym, getELFAArch32StubTarget);
  case Other:
    return Error::success();
  }
  llvm_unreachable("Unhandled SectionType enum");
}

namespace llvm {

Error registerELFGraphInfo(Session &S, LinkGraph &G) {
  auto FileName = sys::path::filename(G.getName());
  if (S.FileInfos.count(FileName)) {
    return make_error<StringError>("When -check is passed, file names must be "
                                   "distinct (duplicate: \"" +
                                       FileName + "\")",
                                   inconvertibleErrorCode());
  }

  auto &FileInfo = S.FileInfos[FileName];
  LLVM_DEBUG({
    dbgs() << "Registering ELF file info for \"" << FileName << "\"\n";
  });
  for (auto &Sec : G.sections()) {
    LLVM_DEBUG({
      dbgs() << "  Section \"" << Sec.getName() << "\": "
             << (Sec.symbols().empty() ? "empty. skipping." : "processing...")
             << "\n";
    });

    // Skip empty sections.
    if (Sec.symbols().empty())
      continue;

    if (FileInfo.SectionInfos.count(Sec.getName()))
      return make_error<StringError>("Encountered duplicate section name \"" +
                                         Sec.getName() + "\" in \"" + FileName +
                                         "\"",
                                     inconvertibleErrorCode());

    SectionType SecType;
    if (isELFGOTSection(Sec)) {
      SecType = GOT;
    } else if (isELFStubsSection(Sec)) {
      SecType = Stubs;
    } else if (isELFAArch32StubsSection(Sec)) {
      SecType = AArch32Stubs;
    } else {
      SecType = Other;
    }

    bool SectionContainsContent = false;
    bool SectionContainsZeroFill = false;

    auto *FirstSym = *Sec.symbols().begin();
    auto *LastSym = FirstSym;
    for (auto *Sym : Sec.symbols()) {
      if (Sym->getAddress() < FirstSym->getAddress())
        FirstSym = Sym;
      if (Sym->getAddress() > LastSym->getAddress())
        LastSym = Sym;

      if (SecType != Other) {
        if (Error Err = registerSymbol(G, *Sym, FileInfo, SecType))
          return Err;
        SectionContainsContent = true;
      }

      if (Sym->hasName()) {
        if (Sym->isSymbolZeroFill()) {
          S.SymbolInfos[Sym->getName()] = {Sym->getSize(),
                                           Sym->getAddress().getValue()};
          SectionContainsZeroFill = true;
        } else {
          S.SymbolInfos[Sym->getName()] = {Sym->getSymbolContent(),
                                           Sym->getAddress().getValue(),
                                           Sym->getTargetFlags()};
          SectionContainsContent = true;
        }
      }
    }

    // Add symbol info for absolute symbols.
    for (auto *Sym : G.absolute_symbols())
      S.SymbolInfos[Sym->getName()] = {Sym->getSize(),
                                       Sym->getAddress().getValue()};

    auto SecAddr = FirstSym->getAddress();
    auto SecSize =
        (LastSym->getBlock().getAddress() + LastSym->getBlock().getSize()) -
        SecAddr;

    if (SectionContainsZeroFill && SectionContainsContent)
      return make_error<StringError>("Mixed zero-fill and content sections not "
                                     "supported yet",
                                     inconvertibleErrorCode());
    if (SectionContainsZeroFill)
      FileInfo.SectionInfos[Sec.getName()] = {SecSize, SecAddr.getValue()};
    else
      FileInfo.SectionInfos[Sec.getName()] = {
          ArrayRef<char>(FirstSym->getBlock().getContent().data(), SecSize),
          SecAddr.getValue(), FirstSym->getTargetFlags()};
  }

  return Error::success();
}

} // end namespace llvm
