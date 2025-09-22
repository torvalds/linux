//=--------- COFFLinkGraphBuilder.cpp - COFF LinkGraph builder ----------===//
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
#include "COFFLinkGraphBuilder.h"

#define DEBUG_TYPE "jitlink"

static const char *CommonSectionName = "__common";

namespace llvm {
namespace jitlink {

static Triple createTripleWithCOFFFormat(Triple T) {
  T.setObjectFormat(Triple::COFF);
  return T;
}

COFFLinkGraphBuilder::COFFLinkGraphBuilder(
    const object::COFFObjectFile &Obj, Triple TT, SubtargetFeatures Features,
    LinkGraph::GetEdgeKindNameFunction GetEdgeKindName)
    : Obj(Obj), G(std::make_unique<LinkGraph>(
                    Obj.getFileName().str(), createTripleWithCOFFFormat(TT),
                    std::move(Features), getPointerSize(Obj),
                    getEndianness(Obj), std::move(GetEdgeKindName))) {
  LLVM_DEBUG({
    dbgs() << "Created COFFLinkGraphBuilder for \"" << Obj.getFileName()
           << "\"\n";
  });
}

COFFLinkGraphBuilder::~COFFLinkGraphBuilder() = default;

unsigned
COFFLinkGraphBuilder::getPointerSize(const object::COFFObjectFile &Obj) {
  return Obj.getBytesInAddress();
}

llvm::endianness
COFFLinkGraphBuilder::getEndianness(const object::COFFObjectFile &Obj) {
  return Obj.isLittleEndian() ? llvm::endianness::little
                              : llvm::endianness::big;
}

uint64_t COFFLinkGraphBuilder::getSectionSize(const object::COFFObjectFile &Obj,
                                              const object::coff_section *Sec) {
  // Consider the difference between executable form and object form.
  // More information is inside COFFObjectFile::getSectionSize
  if (Obj.getDOSHeader())
    return std::min(Sec->VirtualSize, Sec->SizeOfRawData);
  return Sec->SizeOfRawData;
}

uint64_t
COFFLinkGraphBuilder::getSectionAddress(const object::COFFObjectFile &Obj,
                                        const object::coff_section *Section) {
  return Section->VirtualAddress + Obj.getImageBase();
}

bool COFFLinkGraphBuilder::isComdatSection(
    const object::coff_section *Section) {
  return Section->Characteristics & COFF::IMAGE_SCN_LNK_COMDAT;
}

Section &COFFLinkGraphBuilder::getCommonSection() {
  if (!CommonSection)
    CommonSection = &G->createSection(CommonSectionName,
                                      orc::MemProt::Read | orc::MemProt::Write);
  return *CommonSection;
}

Expected<std::unique_ptr<LinkGraph>> COFFLinkGraphBuilder::buildGraph() {
  if (!Obj.isRelocatableObject())
    return make_error<JITLinkError>("Object is not a relocatable COFF file");

  if (auto Err = graphifySections())
    return std::move(Err);

  if (auto Err = graphifySymbols())
    return std::move(Err);

  if (auto Err = addRelocations())
    return std::move(Err);

  return std::move(G);
}

StringRef
COFFLinkGraphBuilder::getCOFFSectionName(COFFSectionIndex SectionIndex,
                                         const object::coff_section *Sec,
                                         object::COFFSymbolRef Sym) {
  switch (SectionIndex) {
  case COFF::IMAGE_SYM_UNDEFINED: {
    if (Sym.getValue())
      return "(common)";
    else
      return "(external)";
  }
  case COFF::IMAGE_SYM_ABSOLUTE:
    return "(absolute)";
  case COFF::IMAGE_SYM_DEBUG: {
    // Used with .file symbol
    return "(debug)";
  }
  default: {
    // Non reserved regular section numbers
    if (Expected<StringRef> SecNameOrErr = Obj.getSectionName(Sec))
      return *SecNameOrErr;
  }
  }
  return "";
}

Error COFFLinkGraphBuilder::graphifySections() {
  LLVM_DEBUG(dbgs() << "  Creating graph sections...\n");

  GraphBlocks.resize(Obj.getNumberOfSections() + 1);
  // For each section...
  for (COFFSectionIndex SecIndex = 1;
       SecIndex <= static_cast<COFFSectionIndex>(Obj.getNumberOfSections());
       SecIndex++) {
    Expected<const object::coff_section *> Sec = Obj.getSection(SecIndex);
    if (!Sec)
      return Sec.takeError();

    StringRef SectionName;
    if (Expected<StringRef> SecNameOrErr = Obj.getSectionName(*Sec))
      SectionName = *SecNameOrErr;

    // FIXME: Skip debug info sections
    if (SectionName == ".voltbl") {
      LLVM_DEBUG({
        dbgs() << "    "
               << "Skipping section \"" << SectionName << "\"\n";
      });
      continue;
    }

    LLVM_DEBUG({
      dbgs() << "    "
             << "Creating section for \"" << SectionName << "\"\n";
    });

    // Get the section's memory protection flags.
    orc::MemProt Prot = orc::MemProt::Read;
    if ((*Sec)->Characteristics & COFF::IMAGE_SCN_MEM_EXECUTE)
      Prot |= orc::MemProt::Exec;
    if ((*Sec)->Characteristics & COFF::IMAGE_SCN_MEM_READ)
      Prot |= orc::MemProt::Read;
    if ((*Sec)->Characteristics & COFF::IMAGE_SCN_MEM_WRITE)
      Prot |= orc::MemProt::Write;

    // Look for existing sections first.
    auto *GraphSec = G->findSectionByName(SectionName);
    if (!GraphSec) {
      GraphSec = &G->createSection(SectionName, Prot);
      if ((*Sec)->Characteristics & COFF::IMAGE_SCN_LNK_REMOVE)
        GraphSec->setMemLifetime(orc::MemLifetime::NoAlloc);
    }
    if (GraphSec->getMemProt() != Prot)
      return make_error<JITLinkError>("MemProt should match");

    Block *B = nullptr;
    if ((*Sec)->Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA)
      B = &G->createZeroFillBlock(
          *GraphSec, getSectionSize(Obj, *Sec),
          orc::ExecutorAddr(getSectionAddress(Obj, *Sec)),
          (*Sec)->getAlignment(), 0);
    else {
      ArrayRef<uint8_t> Data;
      if (auto Err = Obj.getSectionContents(*Sec, Data))
        return Err;

      auto CharData = ArrayRef<char>(
          reinterpret_cast<const char *>(Data.data()), Data.size());

      if (SectionName == getDirectiveSectionName())
        if (auto Err = handleDirectiveSection(
                StringRef(CharData.data(), CharData.size())))
          return Err;

      B = &G->createContentBlock(
          *GraphSec, CharData, orc::ExecutorAddr(getSectionAddress(Obj, *Sec)),
          (*Sec)->getAlignment(), 0);
    }

    setGraphBlock(SecIndex, B);
  }

  return Error::success();
}

Error COFFLinkGraphBuilder::graphifySymbols() {
  LLVM_DEBUG(dbgs() << "  Creating graph symbols...\n");

  SymbolSets.resize(Obj.getNumberOfSections() + 1);
  PendingComdatExports.resize(Obj.getNumberOfSections() + 1);
  GraphSymbols.resize(Obj.getNumberOfSymbols());

  for (COFFSymbolIndex SymIndex = 0;
       SymIndex < static_cast<COFFSymbolIndex>(Obj.getNumberOfSymbols());
       SymIndex++) {
    Expected<object::COFFSymbolRef> Sym = Obj.getSymbol(SymIndex);
    if (!Sym)
      return Sym.takeError();

    StringRef SymbolName;
    if (Expected<StringRef> SymNameOrErr = Obj.getSymbolName(*Sym))
      SymbolName = *SymNameOrErr;

    COFFSectionIndex SectionIndex = Sym->getSectionNumber();
    const object::coff_section *Sec = nullptr;

    if (!COFF::isReservedSectionNumber(SectionIndex)) {
      auto SecOrErr = Obj.getSection(SectionIndex);
      if (!SecOrErr)
        return make_error<JITLinkError>(
            "Invalid COFF section number:" + formatv("{0:d}: ", SectionIndex) +
            " (" + toString(SecOrErr.takeError()) + ")");
      Sec = *SecOrErr;
    }

    // Create jitlink symbol
    jitlink::Symbol *GSym = nullptr;
    if (Sym->isFileRecord())
      LLVM_DEBUG({
        dbgs() << "    " << SymIndex << ": Skipping FileRecord symbol \""
               << SymbolName << "\" in "
               << getCOFFSectionName(SectionIndex, Sec, *Sym)
               << " (index: " << SectionIndex << ") \n";
      });
    else if (Sym->isUndefined()) {
      GSym = createExternalSymbol(SymIndex, SymbolName, *Sym, Sec);
    } else if (Sym->isWeakExternal()) {
      auto *WeakExternal = Sym->getAux<object::coff_aux_weak_external>();
      COFFSymbolIndex TagIndex = WeakExternal->TagIndex;
      uint32_t Characteristics = WeakExternal->Characteristics;
      WeakExternalRequests.push_back(
          {SymIndex, TagIndex, Characteristics, SymbolName});
    } else {
      Expected<jitlink::Symbol *> NewGSym =
          createDefinedSymbol(SymIndex, SymbolName, *Sym, Sec);
      if (!NewGSym)
        return NewGSym.takeError();
      GSym = *NewGSym;
      if (GSym) {
        LLVM_DEBUG({
          dbgs() << "    " << SymIndex
                 << ": Creating defined graph symbol for COFF symbol \""
                 << SymbolName << "\" in "
                 << getCOFFSectionName(SectionIndex, Sec, *Sym)
                 << " (index: " << SectionIndex << ") \n";
          dbgs() << "      " << *GSym << "\n";
        });
      }
    }

    // Register the symbol
    if (GSym)
      setGraphSymbol(SectionIndex, SymIndex, *GSym);
    SymIndex += Sym->getNumberOfAuxSymbols();
  }

  if (auto Err = flushWeakAliasRequests())
    return Err;

  if (auto Err = handleAlternateNames())
    return Err;

  if (auto Err = calculateImplicitSizeOfSymbols())
    return Err;

  return Error::success();
}

Error COFFLinkGraphBuilder::handleDirectiveSection(StringRef Str) {
  auto Parsed = DirectiveParser.parse(Str);
  if (!Parsed)
    return Parsed.takeError();
  for (auto *Arg : *Parsed) {
    StringRef S = Arg->getValue();
    switch (Arg->getOption().getID()) {
    case COFF_OPT_alternatename: {
      StringRef From, To;
      std::tie(From, To) = S.split('=');
      if (From.empty() || To.empty())
        return make_error<JITLinkError>(
            "Invalid COFF /alternatename directive");
      AlternateNames[From] = To;
      break;
    }
    case COFF_OPT_incl: {
      auto DataCopy = G->allocateContent(S);
      StringRef StrCopy(DataCopy.data(), DataCopy.size());
      ExternalSymbols[StrCopy] = &G->addExternalSymbol(StrCopy, 0, false);
      ExternalSymbols[StrCopy]->setLive(true);
      break;
    }
    case COFF_OPT_export:
      break;
    default: {
      LLVM_DEBUG({
        dbgs() << "Unknown coff directive: " << Arg->getSpelling() << "\n";
      });
      break;
    }
    }
  }
  return Error::success();
}

Error COFFLinkGraphBuilder::flushWeakAliasRequests() {
  // Export the weak external symbols and alias it
  for (auto &WeakExternal : WeakExternalRequests) {
    if (auto *Target = getGraphSymbol(WeakExternal.Target)) {
      Expected<object::COFFSymbolRef> AliasSymbol =
          Obj.getSymbol(WeakExternal.Alias);
      if (!AliasSymbol)
        return AliasSymbol.takeError();

      // FIXME: IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY and
      // IMAGE_WEAK_EXTERN_SEARCH_LIBRARY are handled in the same way.
      Scope S =
          WeakExternal.Characteristics == COFF::IMAGE_WEAK_EXTERN_SEARCH_ALIAS
              ? Scope::Default
              : Scope::Local;

      auto NewSymbol =
          createAliasSymbol(WeakExternal.SymbolName, Linkage::Weak, S, *Target);
      if (!NewSymbol)
        return NewSymbol.takeError();
      setGraphSymbol(AliasSymbol->getSectionNumber(), WeakExternal.Alias,
                     **NewSymbol);
      LLVM_DEBUG({
        dbgs() << "    " << WeakExternal.Alias
               << ": Creating weak external symbol for COFF symbol \""
               << WeakExternal.SymbolName << "\" in section "
               << AliasSymbol->getSectionNumber() << "\n";
        dbgs() << "      " << **NewSymbol << "\n";
      });
    } else
      return make_error<JITLinkError>("Weak symbol alias requested but actual "
                                      "symbol not found for symbol " +
                                      formatv("{0:d}", WeakExternal.Alias));
  }
  return Error::success();
}

Error COFFLinkGraphBuilder::handleAlternateNames() {
  for (auto &KeyValue : AlternateNames)
    if (DefinedSymbols.count(KeyValue.second) &&
        ExternalSymbols.count(KeyValue.first)) {
      auto *Target = DefinedSymbols[KeyValue.second];
      auto *Alias = ExternalSymbols[KeyValue.first];
      G->makeDefined(*Alias, Target->getBlock(), Target->getOffset(),
                     Target->getSize(), Linkage::Weak, Scope::Local, false);
    }
  return Error::success();
}

Symbol *COFFLinkGraphBuilder::createExternalSymbol(
    COFFSymbolIndex SymIndex, StringRef SymbolName,
    object::COFFSymbolRef Symbol, const object::coff_section *Section) {
  if (!ExternalSymbols.count(SymbolName))
    ExternalSymbols[SymbolName] =
        &G->addExternalSymbol(SymbolName, Symbol.getValue(), false);

  LLVM_DEBUG({
    dbgs() << "    " << SymIndex
           << ": Creating external graph symbol for COFF symbol \""
           << SymbolName << "\" in "
           << getCOFFSectionName(Symbol.getSectionNumber(), Section, Symbol)
           << " (index: " << Symbol.getSectionNumber() << ") \n";
  });
  return ExternalSymbols[SymbolName];
}

Expected<Symbol *> COFFLinkGraphBuilder::createAliasSymbol(StringRef SymbolName,
                                                           Linkage L, Scope S,
                                                           Symbol &Target) {
  if (!Target.isDefined()) {
    // FIXME: Support this when there's a way to handle this.
    return make_error<JITLinkError>("Weak external symbol with external "
                                    "symbol as alternative not supported.");
  }
  return &G->addDefinedSymbol(Target.getBlock(), Target.getOffset(), SymbolName,
                              Target.getSize(), L, S, Target.isCallable(),
                              false);
}

// In COFF, most of the defined symbols don't contain the size information.
// Hence, we calculate the "implicit" size of symbol by taking the delta of
// offsets of consecutive symbols within a block. We maintain a balanced tree
// set of symbols sorted by offset per each block in order to achieve
// logarithmic time complexity of sorted symbol insertion. Symbol is inserted to
// the set once it's processed in graphifySymbols. In this function, we iterate
// each collected symbol in sorted order and calculate the implicit size.
Error COFFLinkGraphBuilder::calculateImplicitSizeOfSymbols() {
  for (COFFSectionIndex SecIndex = 1;
       SecIndex <= static_cast<COFFSectionIndex>(Obj.getNumberOfSections());
       SecIndex++) {
    auto &SymbolSet = SymbolSets[SecIndex];
    if (SymbolSet.empty())
      continue;
    jitlink::Block *B = getGraphBlock(SecIndex);
    orc::ExecutorAddrDiff LastOffset = B->getSize();
    orc::ExecutorAddrDiff LastDifferentOffset = B->getSize();
    orc::ExecutorAddrDiff LastSize = 0;
    for (auto It = SymbolSet.rbegin(); It != SymbolSet.rend(); It++) {
      orc::ExecutorAddrDiff Offset = It->first;
      jitlink::Symbol *Symbol = It->second;
      orc::ExecutorAddrDiff CandSize;
      // Last offset can be same when aliasing happened
      if (Symbol->getOffset() == LastOffset)
        CandSize = LastSize;
      else
        CandSize = LastOffset - Offset;

      LLVM_DEBUG({
        if (Offset + Symbol->getSize() > LastDifferentOffset)
          dbgs() << "  Overlapping symbol range generated for the following "
                    "symbol:"
                 << "\n"
                 << "    " << *Symbol << "\n";
      });
      (void)LastDifferentOffset;
      if (LastOffset != Offset)
        LastDifferentOffset = Offset;
      LastSize = CandSize;
      LastOffset = Offset;
      if (Symbol->getSize()) {
        // Non empty symbol can happen in COMDAT symbol.
        // We don't consider the possibility of overlapping symbol range that
        // could be introduced by disparity between inferred symbol size and
        // defined symbol size because symbol size information is currently only
        // used by jitlink-check where we have control to not make overlapping
        // ranges.
        continue;
      }

      LLVM_DEBUG({
        if (!CandSize)
          dbgs() << "  Empty implicit symbol size generated for the following "
                    "symbol:"
                 << "\n"
                 << "    " << *Symbol << "\n";
      });

      Symbol->setSize(CandSize);
    }
  }
  return Error::success();
}

Expected<Symbol *> COFFLinkGraphBuilder::createDefinedSymbol(
    COFFSymbolIndex SymIndex, StringRef SymbolName,
    object::COFFSymbolRef Symbol, const object::coff_section *Section) {
  if (Symbol.isCommon()) {
    // FIXME: correct alignment
    return &G->addDefinedSymbol(
        G->createZeroFillBlock(getCommonSection(), Symbol.getValue(),
                               orc::ExecutorAddr(), Symbol.getValue(), 0),
        0, SymbolName, Symbol.getValue(), Linkage::Strong, Scope::Default,
        false, false);
  }
  if (Symbol.isAbsolute())
    return &G->addAbsoluteSymbol(SymbolName,
                                 orc::ExecutorAddr(Symbol.getValue()), 0,
                                 Linkage::Strong, Scope::Local, false);

  if (llvm::COFF::isReservedSectionNumber(Symbol.getSectionNumber()))
    return make_error<JITLinkError>(
        "Reserved section number used in regular symbol " +
        formatv("{0:d}", SymIndex));

  Block *B = getGraphBlock(Symbol.getSectionNumber());
  if (!B) {
    LLVM_DEBUG({
      dbgs() << "    " << SymIndex
             << ": Skipping graph symbol since section was not created for "
                "COFF symbol \""
             << SymbolName << "\" in section " << Symbol.getSectionNumber()
             << "\n";
    });
    return nullptr;
  }

  if (Symbol.isExternal()) {
    // This is not a comdat sequence, export the symbol as it is
    if (!isComdatSection(Section)) {
      auto GSym = &G->addDefinedSymbol(
          *B, Symbol.getValue(), SymbolName, 0, Linkage::Strong, Scope::Default,
          Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION, false);
      DefinedSymbols[SymbolName] = GSym;
      return GSym;
    } else {
      if (!PendingComdatExports[Symbol.getSectionNumber()])
        return make_error<JITLinkError>("No pending COMDAT export for symbol " +
                                        formatv("{0:d}", SymIndex));

      return exportCOMDATSymbol(SymIndex, SymbolName, Symbol);
    }
  }

  if (Symbol.getStorageClass() == COFF::IMAGE_SYM_CLASS_STATIC ||
      Symbol.getStorageClass() == COFF::IMAGE_SYM_CLASS_LABEL) {
    const object::coff_aux_section_definition *Definition =
        Symbol.getSectionDefinition();
    if (!Definition || !isComdatSection(Section)) {
      // Handle typical static symbol
      return &G->addDefinedSymbol(
          *B, Symbol.getValue(), SymbolName, 0, Linkage::Strong, Scope::Local,
          Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION, false);
    }
    if (Definition->Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
      auto Target = Definition->getNumber(Symbol.isBigObj());
      auto GSym = &G->addDefinedSymbol(
          *B, Symbol.getValue(), SymbolName, 0, Linkage::Strong, Scope::Local,
          Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION, false);
      getGraphBlock(Target)->addEdge(Edge::KeepAlive, 0, *GSym, 0);
      return GSym;
    }
    if (PendingComdatExports[Symbol.getSectionNumber()])
      return make_error<JITLinkError>(
          "COMDAT export request already exists before symbol " +
          formatv("{0:d}", SymIndex));
    return createCOMDATExportRequest(SymIndex, Symbol, Definition);
  }
  return make_error<JITLinkError>("Unsupported storage class " +
                                  formatv("{0:d}", Symbol.getStorageClass()) +
                                  " in symbol " + formatv("{0:d}", SymIndex));
}

// COMDAT handling:
// When IMAGE_SCN_LNK_COMDAT flag is set in the flags of a section,
// the section is called a COMDAT section. It contains two symbols
// in a sequence that specifes the behavior. First symbol is the section
// symbol which contains the size and name of the section. It also contains
// selection type that specifies how duplicate of the symbol is handled.
// Second symbol is COMDAT symbol which usually defines the external name and
// data type.
//
// Since two symbols always come in a specific order, we initiate pending COMDAT
// export request when we encounter the first symbol and actually exports it
// when we process the second symbol.
//
// Process the first symbol of COMDAT sequence.
Expected<Symbol *> COFFLinkGraphBuilder::createCOMDATExportRequest(
    COFFSymbolIndex SymIndex, object::COFFSymbolRef Symbol,
    const object::coff_aux_section_definition *Definition) {
  Linkage L = Linkage::Strong;
  switch (Definition->Selection) {
  case COFF::IMAGE_COMDAT_SELECT_NODUPLICATES: {
    L = Linkage::Strong;
    break;
  }
  case COFF::IMAGE_COMDAT_SELECT_ANY: {
    L = Linkage::Weak;
    break;
  }
  case COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH:
  case COFF::IMAGE_COMDAT_SELECT_SAME_SIZE: {
    // FIXME: Implement size/content validation when LinkGraph is able to
    // handle this.
    L = Linkage::Weak;
    break;
  }
  case COFF::IMAGE_COMDAT_SELECT_LARGEST: {
    // FIXME: Support IMAGE_COMDAT_SELECT_LARGEST properly when LinkGraph is
    // able to handle this.
    LLVM_DEBUG({
      dbgs() << "    " << SymIndex
             << ": Partially supported IMAGE_COMDAT_SELECT_LARGEST was used"
                " in section "
             << Symbol.getSectionNumber() << " (size: " << Definition->Length
             << ")\n";
    });
    L = Linkage::Weak;
    break;
  }
  case COFF::IMAGE_COMDAT_SELECT_NEWEST: {
    // Even link.exe doesn't support this selection properly.
    return make_error<JITLinkError>(
        "IMAGE_COMDAT_SELECT_NEWEST is not supported.");
  }
  default: {
    return make_error<JITLinkError>("Invalid comdat selection type: " +
                                    formatv("{0:d}", Definition->Selection));
  }
  }
  PendingComdatExports[Symbol.getSectionNumber()] = {SymIndex, L,
                                                     Definition->Length};
  return nullptr;
}

// Process the second symbol of COMDAT sequence.
Expected<Symbol *>
COFFLinkGraphBuilder::exportCOMDATSymbol(COFFSymbolIndex SymIndex,
                                         StringRef SymbolName,
                                         object::COFFSymbolRef Symbol) {
  Block *B = getGraphBlock(Symbol.getSectionNumber());
  auto &PendingComdatExport = PendingComdatExports[Symbol.getSectionNumber()];
  // NOTE: ComdatDef->Length is the size of "section" not size of symbol.
  // We use zero symbol size to not reach out of bound of block when symbol
  // offset is non-zero.
  auto GSym = &G->addDefinedSymbol(
      *B, Symbol.getValue(), SymbolName, 0, PendingComdatExport->Linkage,
      Scope::Default, Symbol.getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION,
      false);
  LLVM_DEBUG({
    dbgs() << "    " << SymIndex
           << ": Exporting COMDAT graph symbol for COFF symbol \"" << SymbolName
           << "\" in section " << Symbol.getSectionNumber() << "\n";
    dbgs() << "      " << *GSym << "\n";
  });
  setGraphSymbol(Symbol.getSectionNumber(), PendingComdatExport->SymbolIndex,
                 *GSym);
  DefinedSymbols[SymbolName] = GSym;
  PendingComdatExport = std::nullopt;
  return GSym;
}

} // namespace jitlink
} // namespace llvm
