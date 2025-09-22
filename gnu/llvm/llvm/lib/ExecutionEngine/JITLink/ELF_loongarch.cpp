//===--- ELF_loongarch.cpp - JIT linker implementation for ELF/loongarch --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/loongarch jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_loongarch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/loongarch.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"

#include "EHFrameSupportImpl.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;
using namespace llvm::jitlink::loongarch;

namespace {

class ELFJITLinker_loongarch : public JITLinker<ELFJITLinker_loongarch> {
  friend class JITLinker<ELFJITLinker_loongarch>;

public:
  ELFJITLinker_loongarch(std::unique_ptr<JITLinkContext> Ctx,
                         std::unique_ptr<LinkGraph> G,
                         PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {}

private:
  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return loongarch::applyFixup(G, B, E);
  }
};

template <typename ELFT>
class ELFLinkGraphBuilder_loongarch : public ELFLinkGraphBuilder<ELFT> {
private:
  static Expected<loongarch::EdgeKind_loongarch>
  getRelocationKind(const uint32_t Type) {
    using namespace loongarch;
    switch (Type) {
    case ELF::R_LARCH_64:
      return Pointer64;
    case ELF::R_LARCH_32:
      return Pointer32;
    case ELF::R_LARCH_32_PCREL:
      return Delta32;
    case ELF::R_LARCH_B26:
      return Branch26PCRel;
    case ELF::R_LARCH_PCALA_HI20:
      return Page20;
    case ELF::R_LARCH_PCALA_LO12:
      return PageOffset12;
    case ELF::R_LARCH_GOT_PC_HI20:
      return RequestGOTAndTransformToPage20;
    case ELF::R_LARCH_GOT_PC_LO12:
      return RequestGOTAndTransformToPageOffset12;
    }

    return make_error<JITLinkError>(
        "Unsupported loongarch relocation:" + formatv("{0:d}: ", Type) +
        object::getELFRelocationTypeName(ELF::EM_LOONGARCH, Type));
  }

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    using Base = ELFLinkGraphBuilder<ELFT>;
    using Self = ELFLinkGraphBuilder_loongarch<ELFT>;
    for (const auto &RelSect : Base::Sections)
      if (Error Err = Base::forEachRelaRelocation(RelSect, this,
                                                  &Self::addSingleRelocation))
        return Err;

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rela &Rel,
                            const typename ELFT::Shdr &FixupSect,
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

    uint32_t Type = Rel.getType(false);
    Expected<loongarch::EdgeKind_loongarch> Kind = getRelocationKind(Type);
    if (!Kind)
      return Kind.takeError();

    int64_t Addend = Rel.r_addend;
    auto FixupAddress = orc::ExecutorAddr(FixupSect.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();
    Edge GE(*Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, loongarch::getEdgeKindName(*Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));

    return Error::success();
  }

public:
  ELFLinkGraphBuilder_loongarch(StringRef FileName,
                                const object::ELFFile<ELFT> &Obj, Triple TT,
                                SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, loongarch::getEdgeKindName) {}
};

Error buildTables_ELF_loongarch(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  GOTTableManager GOT;
  PLTTableManager PLT(GOT);
  visitExistingEdges(G, GOT, PLT);
  return Error::success();
}

} // namespace

namespace llvm {
namespace jitlink {

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_loongarch(MemoryBufferRef ObjectBuffer) {
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

  if ((*ELFObj)->getArch() == Triple::loongarch64) {
    auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF64LE>>(**ELFObj);
    return ELFLinkGraphBuilder_loongarch<object::ELF64LE>(
               (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
               (*ELFObj)->makeTriple(), std::move(*Features))
        .buildGraph();
  }

  assert((*ELFObj)->getArch() == Triple::loongarch32 &&
         "Invalid triple for LoongArch ELF object file");
  auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF32LE>>(**ELFObj);
  return ELFLinkGraphBuilder_loongarch<object::ELF32LE>(
             (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
             (*ELFObj)->makeTriple(), std::move(*Features))
      .buildGraph();
}

void link_ELF_loongarch(std::unique_ptr<LinkGraph> G,
                        std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    // Add eh-frame passes.
    Config.PrePrunePasses.push_back(DWARFRecordSectionSplitter(".eh_frame"));
    Config.PrePrunePasses.push_back(
        EHFrameEdgeFixer(".eh_frame", G->getPointerSize(), Pointer32, Pointer64,
                         Delta32, Delta64, NegDelta32));
    Config.PrePrunePasses.push_back(EHFrameNullTerminator(".eh_frame"));

    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Add an in-place GOT/PLTStubs build pass.
    Config.PostPrunePasses.push_back(buildTables_ELF_loongarch);
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_loongarch::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace jitlink
} // namespace llvm
