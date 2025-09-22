//===----- COFF_x86_64.cpp - JIT linker implementation for COFF/x86_64 ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// COFF/x86_64 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/COFF_x86_64.h"
#include "COFFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"
#include "SEHFrameSupport.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/ExecutionEngine/JITLink/x86_64.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Endian.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {

enum EdgeKind_coff_x86_64 : Edge::Kind {
  PCRel32 = x86_64::FirstPlatformRelocation,
  Pointer32NB,
  Pointer64,
  SectionIdx16,
  SecRel32,
};

class COFFJITLinker_x86_64 : public JITLinker<COFFJITLinker_x86_64> {
  friend class JITLinker<COFFJITLinker_x86_64>;

public:
  COFFJITLinker_x86_64(std::unique_ptr<JITLinkContext> Ctx,
                       std::unique_ptr<LinkGraph> G,
                       PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {}

private:
  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return x86_64::applyFixup(G, B, E, nullptr);
  }
};

class COFFLinkGraphBuilder_x86_64 : public COFFLinkGraphBuilder {
private:
  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    for (const auto &RelSect : sections())
      if (Error Err = COFFLinkGraphBuilder::forEachRelocation(
              RelSect, this, &COFFLinkGraphBuilder_x86_64::addSingleRelocation))
        return Err;

    return Error::success();
  }

  Error addSingleRelocation(const object::RelocationRef &Rel,
                            const object::SectionRef &FixupSect,
                            Block &BlockToFix) {
    const object::coff_relocation *COFFRel = getObject().getCOFFRelocation(Rel);
    auto SymbolIt = Rel.getSymbol();
    if (SymbolIt == getObject().symbol_end()) {
      return make_error<StringError>(
          formatv("Invalid symbol index in relocation entry. "
                  "index: {0}, section: {1}",
                  COFFRel->SymbolTableIndex, FixupSect.getIndex()),
          inconvertibleErrorCode());
    }

    object::COFFSymbolRef COFFSymbol = getObject().getCOFFSymbol(*SymbolIt);
    COFFSymbolIndex SymIndex = getObject().getSymbolIndex(COFFSymbol);

    Symbol *GraphSymbol = getGraphSymbol(SymIndex);
    if (!GraphSymbol)
      return make_error<StringError>(
          formatv("Could not find symbol at given index, did you add it to "
                  "JITSymbolTable? index: {0}, section: {1}",
                  SymIndex, FixupSect.getIndex()),
          inconvertibleErrorCode());

    int64_t Addend = 0;
    orc::ExecutorAddr FixupAddress =
        orc::ExecutorAddr(FixupSect.getAddress()) + Rel.getOffset();
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();

    Edge::Kind Kind = Edge::Invalid;
    const char *FixupPtr = BlockToFix.getContent().data() + Offset;

    switch (Rel.getType()) {
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_ADDR32NB: {
      Kind = EdgeKind_coff_x86_64::Pointer32NB;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32_1: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      Addend -= 1;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32_2: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      Addend -= 2;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32_3: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      Addend -= 3;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32_4: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      Addend -= 4;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_REL32_5: {
      Kind = EdgeKind_coff_x86_64::PCRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      Addend -= 5;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_ADDR64: {
      Kind = EdgeKind_coff_x86_64::Pointer64;
      Addend = *reinterpret_cast<const support::little64_t *>(FixupPtr);
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_SECTION: {
      Kind = EdgeKind_coff_x86_64::SectionIdx16;
      Addend = *reinterpret_cast<const support::little16_t *>(FixupPtr);
      uint64_t SectionIdx = 0;
      if (COFFSymbol.isAbsolute())
        SectionIdx = getObject().getNumberOfSections() + 1;
      else
        SectionIdx = COFFSymbol.getSectionNumber();
      auto *AbsSym = &getGraph().addAbsoluteSymbol(
          "secidx", orc::ExecutorAddr(SectionIdx), 2, Linkage::Strong,
          Scope::Local, false);
      GraphSymbol = AbsSym;
      break;
    }
    case COFF::RelocationTypeAMD64::IMAGE_REL_AMD64_SECREL: {
      // FIXME: SECREL to external symbol should be handled
      if (!GraphSymbol->isDefined())
        return Error::success();
      Kind = EdgeKind_coff_x86_64::SecRel32;
      Addend = *reinterpret_cast<const support::little32_t *>(FixupPtr);
      break;
    }
    default: {
      return make_error<JITLinkError>("Unsupported x86_64 relocation:" +
                                      formatv("{0:d}", Rel.getType()));
    }
    };

    Edge GE(Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, getCOFFX86RelocationKindName(Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));

    return Error::success();
  }

public:
  COFFLinkGraphBuilder_x86_64(const object::COFFObjectFile &Obj, const Triple T,
                              const SubtargetFeatures Features)
      : COFFLinkGraphBuilder(Obj, std::move(T), std::move(Features),
                             getCOFFX86RelocationKindName) {}
};

class COFFLinkGraphLowering_x86_64 {
public:
  // Lowers COFF x86_64 specific edges to generic x86_64 edges.
  Error lowerCOFFRelocationEdges(LinkGraph &G, JITLinkContext &Ctx) {
    for (auto *B : G.blocks()) {
      for (auto &E : B->edges()) {
        switch (E.getKind()) {
        case EdgeKind_coff_x86_64::Pointer32NB: {
          auto ImageBase = getImageBaseAddress(G, Ctx);
          if (!ImageBase)
            return ImageBase.takeError();
          E.setAddend(E.getAddend() - ImageBase->getValue());
          E.setKind(x86_64::Pointer32);
          break;
        }
        case EdgeKind_coff_x86_64::PCRel32: {
          E.setKind(x86_64::PCRel32);
          break;
        }
        case EdgeKind_coff_x86_64::Pointer64: {
          E.setKind(x86_64::Pointer64);
          break;
        }
        case EdgeKind_coff_x86_64::SectionIdx16: {
          E.setKind(x86_64::Pointer16);
          break;
        }
        case EdgeKind_coff_x86_64::SecRel32: {
          E.setAddend(E.getAddend() -
                      getSectionStart(E.getTarget().getBlock().getSection())
                          .getValue());
          E.setKind(x86_64::Pointer32);
          break;
        }
        default:
          break;
        }
      }
    }
    return Error::success();
  }

private:
  static StringRef getImageBaseSymbolName() { return "__ImageBase"; }

  orc::ExecutorAddr getSectionStart(Section &Sec) {
    if (!SectionStartCache.count(&Sec)) {
      SectionRange Range(Sec);
      SectionStartCache[&Sec] = Range.getStart();
    }
    return SectionStartCache[&Sec];
  }

  Expected<orc::ExecutorAddr> getImageBaseAddress(LinkGraph &G,
                                                  JITLinkContext &Ctx) {
    if (this->ImageBase)
      return this->ImageBase;
    for (auto *S : G.defined_symbols())
      if (S->getName() == getImageBaseSymbolName()) {
        this->ImageBase = S->getAddress();
        return this->ImageBase;
      }

    JITLinkContext::LookupMap Symbols;
    Symbols[getImageBaseSymbolName()] = SymbolLookupFlags::RequiredSymbol;
    orc::ExecutorAddr ImageBase;
    Error Err = Error::success();
    Ctx.lookup(Symbols,
               createLookupContinuation([&](Expected<AsyncLookupResult> LR) {
                 ErrorAsOutParameter EAO(&Err);
                 if (!LR) {
                   Err = LR.takeError();
                   return;
                 }
                 ImageBase = LR->begin()->second.getAddress();
               }));
    if (Err)
      return std::move(Err);
    this->ImageBase = ImageBase;
    return ImageBase;
  }

  DenseMap<Section *, orc::ExecutorAddr> SectionStartCache;
  orc::ExecutorAddr ImageBase;
};

Error lowerEdges_COFF_x86_64(LinkGraph &G, JITLinkContext *Ctx) {
  LLVM_DEBUG(dbgs() << "Lowering COFF x86_64 edges:\n");
  COFFLinkGraphLowering_x86_64 GraphLowering;

  if (auto Err = GraphLowering.lowerCOFFRelocationEdges(G, *Ctx))
    return Err;

  return Error::success();
}
} // namespace

namespace llvm {
namespace jitlink {

/// Return the string name of the given COFF x86_64 edge kind.
const char *getCOFFX86RelocationKindName(Edge::Kind R) {
  switch (R) {
  case PCRel32:
    return "PCRel32";
  case Pointer32NB:
    return "Pointer32NB";
  case Pointer64:
    return "Pointer64";
  case SectionIdx16:
    return "SectionIdx16";
  case SecRel32:
    return "SecRel32";
  default:
    return x86_64::getEdgeKindName(R);
  }
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromCOFFObject_x86_64(MemoryBufferRef ObjectBuffer) {
  LLVM_DEBUG({
    dbgs() << "Building jitlink graph for new input "
           << ObjectBuffer.getBufferIdentifier() << "...\n";
  });

  auto COFFObj = object::ObjectFile::createCOFFObjectFile(ObjectBuffer);
  if (!COFFObj)
    return COFFObj.takeError();

  auto Features = (*COFFObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  return COFFLinkGraphBuilder_x86_64(**COFFObj, (*COFFObj)->makeTriple(),
                                     std::move(*Features))
      .buildGraph();
}

void link_COFF_x86_64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(TT)) {
      Config.PrePrunePasses.push_back(std::move(MarkLive));
      Config.PrePrunePasses.push_back(SEHFrameKeepAlivePass(".pdata"));
    } else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Add COFF edge lowering passes.
    JITLinkContext *CtxPtr = Ctx.get();
    Config.PreFixupPasses.push_back(
        [CtxPtr](LinkGraph &G) { return lowerEdges_COFF_x86_64(G, CtxPtr); });
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  COFFJITLinker_x86_64::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace jitlink
} // namespace llvm
