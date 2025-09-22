//===----- ELF_i386.cpp - JIT linker implementation for ELF/i386 ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/i386 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_i386.h"
#include "DefineExternalSectionStartAndEndSymbols.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/i386.h"
#include "llvm/Object/ELFObjectFile.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {
constexpr StringRef ELFGOTSymbolName = "_GLOBAL_OFFSET_TABLE_";

Error buildTables_ELF_i386(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  i386::GOTTableManager GOT;
  i386::PLTTableManager PLT(GOT);
  visitExistingEdges(G, GOT, PLT);
  return Error::success();
}
} // namespace

namespace llvm::jitlink {

class ELFJITLinker_i386 : public JITLinker<ELFJITLinker_i386> {
  friend class JITLinker<ELFJITLinker_i386>;

public:
  ELFJITLinker_i386(std::unique_ptr<JITLinkContext> Ctx,
                    std::unique_ptr<LinkGraph> G, PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {
    getPassConfig().PostAllocationPasses.push_back(
        [this](LinkGraph &G) { return getOrCreateGOTSymbol(G); });
  }

private:
  Symbol *GOTSymbol = nullptr;

  Error getOrCreateGOTSymbol(LinkGraph &G) {
    auto DefineExternalGOTSymbolIfPresent =
        createDefineExternalSectionStartAndEndSymbolsPass(
            [&](LinkGraph &LG, Symbol &Sym) -> SectionRangeSymbolDesc {
              if (Sym.getName() == ELFGOTSymbolName)
                if (auto *GOTSection = G.findSectionByName(
                        i386::GOTTableManager::getSectionName())) {
                  GOTSymbol = &Sym;
                  return {*GOTSection, true};
                }
              return {};
            });

    // Try to attach _GLOBAL_OFFSET_TABLE_ to the GOT if it's defined as an
    // external.
    if (auto Err = DefineExternalGOTSymbolIfPresent(G))
      return Err;

    // If we succeeded then we're done.
    if (GOTSymbol)
      return Error::success();

    // Otherwise look for a GOT section: If it already has a start symbol we'll
    // record it, otherwise we'll create our own.
    // If there's a GOT section but we didn't find an external GOT symbol...
    if (auto *GOTSection =
            G.findSectionByName(i386::GOTTableManager::getSectionName())) {

      // Check for an existing defined symbol.
      for (auto *Sym : GOTSection->symbols())
        if (Sym->getName() == ELFGOTSymbolName) {
          GOTSymbol = Sym;
          return Error::success();
        }

      // If there's no defined symbol then create one.
      SectionRange SR(*GOTSection);

      if (SR.empty()) {
        GOTSymbol =
            &G.addAbsoluteSymbol(ELFGOTSymbolName, orc::ExecutorAddr(), 0,
                                 Linkage::Strong, Scope::Local, true);
      } else {
        GOTSymbol =
            &G.addDefinedSymbol(*SR.getFirstBlock(), 0, ELFGOTSymbolName, 0,
                                Linkage::Strong, Scope::Local, false, true);
      }
    }

    return Error::success();
  }

  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return i386::applyFixup(G, B, E, GOTSymbol);
  }
};

template <typename ELFT>
class ELFLinkGraphBuilder_i386 : public ELFLinkGraphBuilder<ELFT> {
private:
  static Expected<i386::EdgeKind_i386> getRelocationKind(const uint32_t Type) {
    using namespace i386;
    switch (Type) {
    case ELF::R_386_NONE:
      return EdgeKind_i386::None;
    case ELF::R_386_32:
      return EdgeKind_i386::Pointer32;
    case ELF::R_386_PC32:
      return EdgeKind_i386::PCRel32;
    case ELF::R_386_16:
      return EdgeKind_i386::Pointer16;
    case ELF::R_386_PC16:
      return EdgeKind_i386::PCRel16;
    case ELF::R_386_GOT32:
      return EdgeKind_i386::RequestGOTAndTransformToDelta32FromGOT;
    case ELF::R_386_GOTPC:
      return EdgeKind_i386::Delta32;
    case ELF::R_386_GOTOFF:
      return EdgeKind_i386::Delta32FromGOT;
    case ELF::R_386_PLT32:
      return EdgeKind_i386::BranchPCRel32;
    }

    return make_error<JITLinkError>("Unsupported i386 relocation:" +
                                    formatv("{0:d}", Type));
  }

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Adding relocations\n");
    using Base = ELFLinkGraphBuilder<ELFT>;
    using Self = ELFLinkGraphBuilder_i386;

    for (const auto &RelSect : Base::Sections) {
      // Validate the section to read relocation entries from.
      if (RelSect.sh_type == ELF::SHT_RELA)
        return make_error<StringError>(
            "No SHT_RELA in valid i386 ELF object files",
            inconvertibleErrorCode());

      if (Error Err = Base::forEachRelRelocation(RelSect, this,
                                                 &Self::addSingleRelocation))
        return Err;
    }

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rel &Rel,
                            const typename ELFT::Shdr &FixupSection,
                            Block &BlockToFix) {
    using Base = ELFLinkGraphBuilder<ELFT>;

    uint32_t SymbolIndex = Rel.getSymbol(false);
    auto ObjSymbol = Base::Obj.getRelocationSymbol(Rel, Base::SymTabSec);
    if (!ObjSymbol)
      return ObjSymbol.takeError();

    Symbol *GraphSymbol = Base::getGraphSymbol(SymbolIndex);
    if (!GraphSymbol)
      return make_error<StringError>(
          formatv("Could not find symbol at given index, did you add it to "
                  "JITSymbolTable? index: {0}, shndx: {1} Size of table: {2}",
                  SymbolIndex, (*ObjSymbol)->st_shndx,
                  Base::GraphSymbols.size()),
          inconvertibleErrorCode());

    Expected<i386::EdgeKind_i386> Kind = getRelocationKind(Rel.getType(false));
    if (!Kind)
      return Kind.takeError();

    auto FixupAddress = orc::ExecutorAddr(FixupSection.sh_addr) + Rel.r_offset;
    int64_t Addend = 0;

    switch (*Kind) {
    case i386::EdgeKind_i386::Delta32: {
      const char *FixupContent = BlockToFix.getContent().data() +
                                 (FixupAddress - BlockToFix.getAddress());
      Addend = *(const support::ulittle32_t *)FixupContent;
      break;
    }
    default:
      break;
    }

    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();
    Edge GE(*Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, i386::getEdgeKindName(*Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));
    return Error::success();
  }

public:
  ELFLinkGraphBuilder_i386(StringRef FileName, const object::ELFFile<ELFT> &Obj,
                           Triple TT, SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, i386::getEdgeKindName) {}
};

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_i386(MemoryBufferRef ObjectBuffer) {
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

  assert((*ELFObj)->getArch() == Triple::x86 &&
         "Only i386 (little endian) is supported for now");

  auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF32LE>>(**ELFObj);
  return ELFLinkGraphBuilder_i386<object::ELF32LE>(
             (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
             (*ELFObj)->makeTriple(), std::move(*Features))
      .buildGraph();
}

void link_ELF_i386(std::unique_ptr<LinkGraph> G,
                   std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Add an in-place GOT and PLT build pass.
    Config.PostPrunePasses.push_back(buildTables_ELF_i386);

    // Add GOT/Stubs optimizer pass.
    Config.PreFixupPasses.push_back(i386::optimizeGOTAndStubAccesses);
  }
  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_i386::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace llvm::jitlink
