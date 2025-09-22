//===------- ELF_ppc64.cpp -JIT linker implementation for ELF/ppc64 -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/ppc64 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_ppc64.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/TableManager.h"
#include "llvm/ExecutionEngine/JITLink/ppc64.h"
#include "llvm/Object/ELFObjectFile.h"

#include "EHFrameSupportImpl.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

namespace {

using namespace llvm;
using namespace llvm::jitlink;

constexpr StringRef ELFTOCSymbolName = ".TOC.";
constexpr StringRef TOCSymbolAliasIdent = "__TOC__";
constexpr uint64_t ELFTOCBaseOffset = 0x8000;
constexpr StringRef ELFTLSInfoSectionName = "$__TLSINFO";

template <llvm::endianness Endianness>
class TLSInfoTableManager_ELF_ppc64
    : public TableManager<TLSInfoTableManager_ELF_ppc64<Endianness>> {
public:
  static const uint8_t TLSInfoEntryContent[16];

  static StringRef getSectionName() { return ELFTLSInfoSectionName; }

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind K = E.getKind();
    switch (K) {
    case ppc64::RequestTLSDescInGOTAndTransformToTOCDelta16HA:
      E.setKind(ppc64::TOCDelta16HA);
      E.setTarget(this->getEntryForTarget(G, E.getTarget()));
      return true;
    case ppc64::RequestTLSDescInGOTAndTransformToTOCDelta16LO:
      E.setKind(ppc64::TOCDelta16LO);
      E.setTarget(this->getEntryForTarget(G, E.getTarget()));
      return true;
    case ppc64::RequestTLSDescInGOTAndTransformToDelta34:
      E.setKind(ppc64::Delta34);
      E.setTarget(this->getEntryForTarget(G, E.getTarget()));
      return true;
    default:
      return false;
    }
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    // The TLS Info entry's key value will be written by
    // `fixTLVSectionsAndEdges`, so create mutable content.
    auto &TLSInfoEntry = G.createMutableContentBlock(
        getTLSInfoSection(G), G.allocateContent(getTLSInfoEntryContent()),
        orc::ExecutorAddr(), 8, 0);
    TLSInfoEntry.addEdge(ppc64::Pointer64, 8, Target, 0);
    return G.addAnonymousSymbol(TLSInfoEntry, 0, 16, false, false);
  }

private:
  Section &getTLSInfoSection(LinkGraph &G) {
    if (!TLSInfoTable)
      TLSInfoTable =
          &G.createSection(ELFTLSInfoSectionName, orc::MemProt::Read);
    return *TLSInfoTable;
  }

  ArrayRef<char> getTLSInfoEntryContent() const {
    return {reinterpret_cast<const char *>(TLSInfoEntryContent),
            sizeof(TLSInfoEntryContent)};
  }

  Section *TLSInfoTable = nullptr;
};

template <>
const uint8_t TLSInfoTableManager_ELF_ppc64<
    llvm::endianness::little>::TLSInfoEntryContent[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*pthread key */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /*data address*/
};

template <>
const uint8_t TLSInfoTableManager_ELF_ppc64<
    llvm::endianness::big>::TLSInfoEntryContent[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*pthread key */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /*data address*/
};

template <llvm::endianness Endianness>
Symbol &createELFGOTHeader(LinkGraph &G,
                           ppc64::TOCTableManager<Endianness> &TOC) {
  Symbol *TOCSymbol = nullptr;

  for (Symbol *Sym : G.defined_symbols())
    if (LLVM_UNLIKELY(Sym->getName() == ELFTOCSymbolName)) {
      TOCSymbol = Sym;
      break;
    }

  if (LLVM_LIKELY(TOCSymbol == nullptr)) {
    for (Symbol *Sym : G.external_symbols())
      if (Sym->getName() == ELFTOCSymbolName) {
        TOCSymbol = Sym;
        break;
      }
  }

  if (!TOCSymbol)
    TOCSymbol = &G.addExternalSymbol(ELFTOCSymbolName, 0, false);

  return TOC.getEntryForTarget(G, *TOCSymbol);
}

// Register preexisting GOT entries with TOC table manager.
template <llvm::endianness Endianness>
inline void
registerExistingGOTEntries(LinkGraph &G,
                           ppc64::TOCTableManager<Endianness> &TOC) {
  auto isGOTEntry = [](const Edge &E) {
    return E.getKind() == ppc64::Pointer64 && E.getTarget().isExternal();
  };
  if (Section *dotTOCSection = G.findSectionByName(".toc")) {
    for (Block *B : dotTOCSection->blocks())
      for (Edge &E : B->edges())
        if (isGOTEntry(E))
          TOC.registerPreExistingEntry(E.getTarget(),
                                       G.addAnonymousSymbol(*B, E.getOffset(),
                                                            G.getPointerSize(),
                                                            false, false));
  }
}

template <llvm::endianness Endianness>
Error buildTables_ELF_ppc64(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");
  ppc64::TOCTableManager<Endianness> TOC;
  // Before visiting edges, we create a header containing the address of TOC
  // base as ELFABIv2 suggests:
  //  > The GOT consists of an 8-byte header that contains the TOC base (the
  //  first TOC base when multiple TOCs are present), followed by an array of
  //  8-byte addresses.
  createELFGOTHeader(G, TOC);

  // There might be compiler-generated GOT entries in ELF relocatable file.
  registerExistingGOTEntries(G, TOC);

  ppc64::PLTTableManager<Endianness> PLT(TOC);
  TLSInfoTableManager_ELF_ppc64<Endianness> TLSInfo;
  visitExistingEdges(G, TOC, PLT, TLSInfo);

  // After visiting edges in LinkGraph, we have GOT entries built in the
  // synthesized section.
  // Merge sections included in TOC into synthesized TOC section,
  // thus TOC is compact and reducing chances of relocation
  // overflow.
  if (Section *TOCSection = G.findSectionByName(TOC.getSectionName())) {
    // .got and .plt are not normally present in a relocatable object file
    // because they are linker generated.
    if (Section *gotSection = G.findSectionByName(".got"))
      G.mergeSections(*TOCSection, *gotSection);
    if (Section *tocSection = G.findSectionByName(".toc"))
      G.mergeSections(*TOCSection, *tocSection);
    if (Section *sdataSection = G.findSectionByName(".sdata"))
      G.mergeSections(*TOCSection, *sdataSection);
    if (Section *sbssSection = G.findSectionByName(".sbss"))
      G.mergeSections(*TOCSection, *sbssSection);
    // .tocbss no longer appears in ELFABIv2. Leave it here to be compatible
    // with rtdyld.
    if (Section *tocbssSection = G.findSectionByName(".tocbss"))
      G.mergeSections(*TOCSection, *tocbssSection);
    if (Section *pltSection = G.findSectionByName(".plt"))
      G.mergeSections(*TOCSection, *pltSection);
  }

  return Error::success();
}

} // namespace

namespace llvm::jitlink {

template <llvm::endianness Endianness>
class ELFLinkGraphBuilder_ppc64
    : public ELFLinkGraphBuilder<object::ELFType<Endianness, true>> {
private:
  using ELFT = object::ELFType<Endianness, true>;
  using Base = ELFLinkGraphBuilder<ELFT>;

  using Base::G; // Use LinkGraph pointer from base class.

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    using Self = ELFLinkGraphBuilder_ppc64<Endianness>;
    for (const auto &RelSect : Base::Sections) {
      // Validate the section to read relocation entries from.
      if (RelSect.sh_type == ELF::SHT_REL)
        return make_error<StringError>("No SHT_REL in valid " +
                                           G->getTargetTriple().getArchName() +
                                           " ELF object files",
                                       inconvertibleErrorCode());

      if (Error Err = Base::forEachRelaRelocation(RelSect, this,
                                                  &Self::addSingleRelocation))
        return Err;
    }

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rela &Rel,
                            const typename ELFT::Shdr &FixupSection,
                            Block &BlockToFix) {
    using Base = ELFLinkGraphBuilder<ELFT>;
    auto ELFReloc = Rel.getType(false);

    // R_PPC64_NONE is a no-op.
    if (LLVM_UNLIKELY(ELFReloc == ELF::R_PPC64_NONE))
      return Error::success();

    // TLS model markers. We only support global-dynamic model now.
    if (ELFReloc == ELF::R_PPC64_TLSGD)
      return Error::success();
    if (ELFReloc == ELF::R_PPC64_TLSLD)
      return make_error<StringError>("Local-dynamic TLS model is not supported",
                                     inconvertibleErrorCode());

    if (ELFReloc == ELF::R_PPC64_PCREL_OPT)
      // TODO: Support PCREL optimization, now ignore it.
      return Error::success();

    if (ELFReloc == ELF::R_PPC64_TPREL34)
      return make_error<StringError>("Local-exec TLS model is not supported",
                                     inconvertibleErrorCode());

    auto ObjSymbol = Base::Obj.getRelocationSymbol(Rel, Base::SymTabSec);
    if (!ObjSymbol)
      return ObjSymbol.takeError();

    uint32_t SymbolIndex = Rel.getSymbol(false);
    Symbol *GraphSymbol = Base::getGraphSymbol(SymbolIndex);
    if (!GraphSymbol)
      return make_error<StringError>(
          formatv("Could not find symbol at given index, did you add it to "
                  "JITSymbolTable? index: {0}, shndx: {1} Size of table: {2}",
                  SymbolIndex, (*ObjSymbol)->st_shndx,
                  Base::GraphSymbols.size()),
          inconvertibleErrorCode());

    int64_t Addend = Rel.r_addend;
    orc::ExecutorAddr FixupAddress =
        orc::ExecutorAddr(FixupSection.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();
    Edge::Kind Kind = Edge::Invalid;

    switch (ELFReloc) {
    default:
      return make_error<JITLinkError>(
          "In " + G->getName() + ": Unsupported ppc64 relocation type " +
          object::getELFRelocationTypeName(ELF::EM_PPC64, ELFReloc));
    case ELF::R_PPC64_ADDR64:
      Kind = ppc64::Pointer64;
      break;
    case ELF::R_PPC64_ADDR32:
      Kind = ppc64::Pointer32;
      break;
    case ELF::R_PPC64_ADDR16:
      Kind = ppc64::Pointer16;
      break;
    case ELF::R_PPC64_ADDR16_DS:
      Kind = ppc64::Pointer16DS;
      break;
    case ELF::R_PPC64_ADDR16_HA:
      Kind = ppc64::Pointer16HA;
      break;
    case ELF::R_PPC64_ADDR16_HI:
      Kind = ppc64::Pointer16HI;
      break;
    case ELF::R_PPC64_ADDR16_HIGH:
      Kind = ppc64::Pointer16HIGH;
      break;
    case ELF::R_PPC64_ADDR16_HIGHA:
      Kind = ppc64::Pointer16HIGHA;
      break;
    case ELF::R_PPC64_ADDR16_HIGHER:
      Kind = ppc64::Pointer16HIGHER;
      break;
    case ELF::R_PPC64_ADDR16_HIGHERA:
      Kind = ppc64::Pointer16HIGHERA;
      break;
    case ELF::R_PPC64_ADDR16_HIGHEST:
      Kind = ppc64::Pointer16HIGHEST;
      break;
    case ELF::R_PPC64_ADDR16_HIGHESTA:
      Kind = ppc64::Pointer16HIGHESTA;
      break;
    case ELF::R_PPC64_ADDR16_LO:
      Kind = ppc64::Pointer16LO;
      break;
    case ELF::R_PPC64_ADDR16_LO_DS:
      Kind = ppc64::Pointer16LODS;
      break;
    case ELF::R_PPC64_ADDR14:
      Kind = ppc64::Pointer14;
      break;
    case ELF::R_PPC64_TOC:
      Kind = ppc64::TOC;
      break;
    case ELF::R_PPC64_TOC16:
      Kind = ppc64::TOCDelta16;
      break;
    case ELF::R_PPC64_TOC16_HA:
      Kind = ppc64::TOCDelta16HA;
      break;
    case ELF::R_PPC64_TOC16_HI:
      Kind = ppc64::TOCDelta16HI;
      break;
    case ELF::R_PPC64_TOC16_DS:
      Kind = ppc64::TOCDelta16DS;
      break;
    case ELF::R_PPC64_TOC16_LO:
      Kind = ppc64::TOCDelta16LO;
      break;
    case ELF::R_PPC64_TOC16_LO_DS:
      Kind = ppc64::TOCDelta16LODS;
      break;
    case ELF::R_PPC64_REL16:
      Kind = ppc64::Delta16;
      break;
    case ELF::R_PPC64_REL16_HA:
      Kind = ppc64::Delta16HA;
      break;
    case ELF::R_PPC64_REL16_HI:
      Kind = ppc64::Delta16HI;
      break;
    case ELF::R_PPC64_REL16_LO:
      Kind = ppc64::Delta16LO;
      break;
    case ELF::R_PPC64_REL32:
      Kind = ppc64::Delta32;
      break;
    case ELF::R_PPC64_REL24_NOTOC:
      Kind = ppc64::RequestCallNoTOC;
      break;
    case ELF::R_PPC64_REL24:
      Kind = ppc64::RequestCall;
      // Determining a target is external or not is deferred in PostPrunePass.
      // We assume branching to local entry by default, since in PostPrunePass,
      // we don't have any context to determine LocalEntryOffset. If it finally
      // turns out to be an external call, we'll have a stub for the external
      // target, the target of this edge will be the stub and its addend will be
      // set 0.
      Addend += ELF::decodePPC64LocalEntryOffset((*ObjSymbol)->st_other);
      break;
    case ELF::R_PPC64_REL64:
      Kind = ppc64::Delta64;
      break;
    case ELF::R_PPC64_PCREL34:
      Kind = ppc64::Delta34;
      break;
    case ELF::R_PPC64_GOT_PCREL34:
      Kind = ppc64::RequestGOTAndTransformToDelta34;
      break;
    case ELF::R_PPC64_GOT_TLSGD16_HA:
      Kind = ppc64::RequestTLSDescInGOTAndTransformToTOCDelta16HA;
      break;
    case ELF::R_PPC64_GOT_TLSGD16_LO:
      Kind = ppc64::RequestTLSDescInGOTAndTransformToTOCDelta16LO;
      break;
    case ELF::R_PPC64_GOT_TLSGD_PCREL34:
      Kind = ppc64::RequestTLSDescInGOTAndTransformToDelta34;
      break;
    }

    Edge GE(Kind, Offset, *GraphSymbol, Addend);
    BlockToFix.addEdge(std::move(GE));
    return Error::success();
  }

public:
  ELFLinkGraphBuilder_ppc64(StringRef FileName,
                            const object::ELFFile<ELFT> &Obj, Triple TT,
                            SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, ppc64::getEdgeKindName) {}
};

template <llvm::endianness Endianness>
class ELFJITLinker_ppc64 : public JITLinker<ELFJITLinker_ppc64<Endianness>> {
  using JITLinkerBase = JITLinker<ELFJITLinker_ppc64<Endianness>>;
  friend JITLinkerBase;

public:
  ELFJITLinker_ppc64(std::unique_ptr<JITLinkContext> Ctx,
                     std::unique_ptr<LinkGraph> G, PassConfiguration PassConfig)
      : JITLinkerBase(std::move(Ctx), std::move(G), std::move(PassConfig)) {
    JITLinkerBase::getPassConfig().PostAllocationPasses.push_back(
        [this](LinkGraph &G) { return defineTOCBase(G); });
  }

private:
  Symbol *TOCSymbol = nullptr;

  Error defineTOCBase(LinkGraph &G) {
    for (Symbol *Sym : G.defined_symbols()) {
      if (LLVM_UNLIKELY(Sym->getName() == ELFTOCSymbolName)) {
        TOCSymbol = Sym;
        return Error::success();
      }
    }

    assert(TOCSymbol == nullptr &&
           "TOCSymbol should not be defined at this point");

    for (Symbol *Sym : G.external_symbols()) {
      if (Sym->getName() == ELFTOCSymbolName) {
        TOCSymbol = Sym;
        break;
      }
    }

    if (Section *TOCSection = G.findSectionByName(
            ppc64::TOCTableManager<Endianness>::getSectionName())) {
      assert(!TOCSection->empty() && "TOC section should have reserved an "
                                     "entry for containing the TOC base");

      SectionRange SR(*TOCSection);
      orc::ExecutorAddr TOCBaseAddr(SR.getFirstBlock()->getAddress() +
                                    ELFTOCBaseOffset);
      assert(TOCSymbol && TOCSymbol->isExternal() &&
             ".TOC. should be a external symbol at this point");
      G.makeAbsolute(*TOCSymbol, TOCBaseAddr);
      // Create an alias of .TOC. so that rtdyld checker can recognize.
      G.addAbsoluteSymbol(TOCSymbolAliasIdent, TOCSymbol->getAddress(),
                          TOCSymbol->getSize(), TOCSymbol->getLinkage(),
                          TOCSymbol->getScope(), TOCSymbol->isLive());
      return Error::success();
    }

    // If TOC section doesn't exist, which means no TOC relocation is found, we
    // don't need a TOCSymbol.
    return Error::success();
  }

  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return ppc64::applyFixup<Endianness>(G, B, E, TOCSymbol);
  }
};

template <llvm::endianness Endianness>
Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ppc64(MemoryBufferRef ObjectBuffer) {
  LLVM_DEBUG({
    dbgs() << "Building jitlink graph for new input "
           << ObjectBuffer.getBufferIdentifier() << "...\n";
  });

  auto ELFObj = object::ObjectFile::createELFObjectFile(ObjectBuffer);
  if (!ELFObj)
    return ELFObj.takeError();

  auto Features = (*ELFObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  using ELFT = object::ELFType<Endianness, true>;
  auto &ELFObjFile = cast<object::ELFObjectFile<ELFT>>(**ELFObj);
  return ELFLinkGraphBuilder_ppc64<Endianness>(
             (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
             (*ELFObj)->makeTriple(), std::move(*Features))
      .buildGraph();
}

template <llvm::endianness Endianness>
void link_ELF_ppc64(std::unique_ptr<LinkGraph> G,
                    std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;

  if (Ctx->shouldAddDefaultTargetPasses(G->getTargetTriple())) {
    // Construct a JITLinker and run the link function.

    // Add eh-frame passes.
    Config.PrePrunePasses.push_back(DWARFRecordSectionSplitter(".eh_frame"));
    Config.PrePrunePasses.push_back(EHFrameEdgeFixer(
        ".eh_frame", G->getPointerSize(), ppc64::Pointer32, ppc64::Pointer64,
        ppc64::Delta32, ppc64::Delta64, ppc64::NegDelta32));
    Config.PrePrunePasses.push_back(EHFrameNullTerminator(".eh_frame"));

    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(G->getTargetTriple()))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);
  }

  Config.PostPrunePasses.push_back(buildTables_ELF_ppc64<Endianness>);

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_ppc64<Endianness>::link(std::move(Ctx), std::move(G),
                                       std::move(Config));
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ppc64(MemoryBufferRef ObjectBuffer) {
  return createLinkGraphFromELFObject_ppc64<llvm::endianness::big>(
      std::move(ObjectBuffer));
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_ppc64le(MemoryBufferRef ObjectBuffer) {
  return createLinkGraphFromELFObject_ppc64<llvm::endianness::little>(
      std::move(ObjectBuffer));
}

/// jit-link the given object buffer, which must be a ELF ppc64 object file.
void link_ELF_ppc64(std::unique_ptr<LinkGraph> G,
                    std::unique_ptr<JITLinkContext> Ctx) {
  return link_ELF_ppc64<llvm::endianness::big>(std::move(G), std::move(Ctx));
}

/// jit-link the given object buffer, which must be a ELF ppc64le object file.
void link_ELF_ppc64le(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx) {
  return link_ELF_ppc64<llvm::endianness::little>(std::move(G), std::move(Ctx));
}

} // end namespace llvm::jitlink
