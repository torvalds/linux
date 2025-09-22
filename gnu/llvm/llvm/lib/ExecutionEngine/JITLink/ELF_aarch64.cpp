//===----- ELF_aarch64.cpp - JIT linker implementation for ELF/aarch64 ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/aarch64 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_aarch64.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/aarch64.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Endian.h"

#include "DefineExternalSectionStartAndEndSymbols.h"
#include "EHFrameSupportImpl.h"
#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {

class ELFJITLinker_aarch64 : public JITLinker<ELFJITLinker_aarch64> {
  friend class JITLinker<ELFJITLinker_aarch64>;

public:
  ELFJITLinker_aarch64(std::unique_ptr<JITLinkContext> Ctx,
                       std::unique_ptr<LinkGraph> G,
                       PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {}

private:
  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return aarch64::applyFixup(G, B, E);
  }
};

template <typename ELFT>
class ELFLinkGraphBuilder_aarch64 : public ELFLinkGraphBuilder<ELFT> {
private:
  enum ELFAArch64RelocationKind : Edge::Kind {
    ELFCall26 = Edge::FirstRelocation,
    ELFLdrLo19,
    ELFAdrLo21,
    ELFAdrPage21,
    ELFAddAbs12,
    ELFLdSt8Abs12,
    ELFLdSt16Abs12,
    ELFLdSt32Abs12,
    ELFLdSt64Abs12,
    ELFLdSt128Abs12,
    ELFMovwAbsG0,
    ELFMovwAbsG1,
    ELFMovwAbsG2,
    ELFMovwAbsG3,
    ELFTstBr14,
    ELFCondBr19,
    ELFAbs32,
    ELFAbs64,
    ELFPrel32,
    ELFPrel64,
    ELFAdrGOTPage21,
    ELFLd64GOTLo12,
    ELFTLSDescAdrPage21,
    ELFTLSDescAddLo12,
    ELFTLSDescLd64Lo12,
    ELFTLSDescCall,
  };

  static Expected<ELFAArch64RelocationKind>
  getRelocationKind(const uint32_t Type) {
    using namespace aarch64;
    switch (Type) {
    case ELF::R_AARCH64_CALL26:
    case ELF::R_AARCH64_JUMP26:
      return ELFCall26;
    case ELF::R_AARCH64_LD_PREL_LO19:
      return ELFLdrLo19;
    case ELF::R_AARCH64_ADR_PREL_LO21:
      return ELFAdrLo21;
    case ELF::R_AARCH64_ADR_PREL_PG_HI21:
      return ELFAdrPage21;
    case ELF::R_AARCH64_ADD_ABS_LO12_NC:
      return ELFAddAbs12;
    case ELF::R_AARCH64_LDST8_ABS_LO12_NC:
      return ELFLdSt8Abs12;
    case ELF::R_AARCH64_LDST16_ABS_LO12_NC:
      return ELFLdSt16Abs12;
    case ELF::R_AARCH64_LDST32_ABS_LO12_NC:
      return ELFLdSt32Abs12;
    case ELF::R_AARCH64_LDST64_ABS_LO12_NC:
      return ELFLdSt64Abs12;
    case ELF::R_AARCH64_LDST128_ABS_LO12_NC:
      return ELFLdSt128Abs12;
    case ELF::R_AARCH64_MOVW_UABS_G0_NC:
      return ELFMovwAbsG0;
    case ELF::R_AARCH64_MOVW_UABS_G1_NC:
      return ELFMovwAbsG1;
    case ELF::R_AARCH64_MOVW_UABS_G2_NC:
      return ELFMovwAbsG2;
    case ELF::R_AARCH64_MOVW_UABS_G3:
      return ELFMovwAbsG3;
    case ELF::R_AARCH64_TSTBR14:
      return ELFTstBr14;
    case ELF::R_AARCH64_CONDBR19:
      return ELFCondBr19;
    case ELF::R_AARCH64_ABS32:
      return ELFAbs32;
    case ELF::R_AARCH64_ABS64:
      return ELFAbs64;
    case ELF::R_AARCH64_PREL32:
      return ELFPrel32;
    case ELF::R_AARCH64_PREL64:
      return ELFPrel64;
    case ELF::R_AARCH64_ADR_GOT_PAGE:
      return ELFAdrGOTPage21;
    case ELF::R_AARCH64_LD64_GOT_LO12_NC:
      return ELFLd64GOTLo12;
    case ELF::R_AARCH64_TLSDESC_ADR_PAGE21:
      return ELFTLSDescAdrPage21;
    case ELF::R_AARCH64_TLSDESC_ADD_LO12:
      return ELFTLSDescAddLo12;
    case ELF::R_AARCH64_TLSDESC_LD64_LO12:
      return ELFTLSDescLd64Lo12;
    case ELF::R_AARCH64_TLSDESC_CALL:
      return ELFTLSDescCall;
    }

    return make_error<JITLinkError>(
        "Unsupported aarch64 relocation:" + formatv("{0:d}: ", Type) +
        object::getELFRelocationTypeName(ELF::EM_AARCH64, Type));
  }

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    using Base = ELFLinkGraphBuilder<ELFT>;
    using Self = ELFLinkGraphBuilder_aarch64<ELFT>;
    for (const auto &RelSect : Base::Sections)
      if (Error Err = Base::forEachRelaRelocation(RelSect, this,
                                                  &Self::addSingleRelocation))
        return Err;

    return Error::success();
  }

  Error addSingleRelocation(const typename ELFT::Rela &Rel,
                            const typename ELFT::Shdr &FixupSect,
                            Block &BlockToFix) {
    using support::ulittle32_t;
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
    Expected<ELFAArch64RelocationKind> RelocKind = getRelocationKind(Type);
    if (!RelocKind)
      return RelocKind.takeError();

    int64_t Addend = Rel.r_addend;
    orc::ExecutorAddr FixupAddress =
        orc::ExecutorAddr(FixupSect.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();

    // Get a pointer to the fixup content.
    const void *FixupContent = BlockToFix.getContent().data() +
                               (FixupAddress - BlockToFix.getAddress());

    Edge::Kind Kind = Edge::Invalid;

    switch (*RelocKind) {
    case ELFCall26: {
      Kind = aarch64::Branch26PCRel;
      break;
    }
    case ELFLdrLo19: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLDRLiteral(Instr))
        return make_error<JITLinkError>(
            "R_AARCH64_LDR_PREL_LO19 target is not an LDR Literal instruction");

      Kind = aarch64::LDRLiteral19;
      break;
    }
    case ELFAdrLo21: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isADR(Instr))
        return make_error<JITLinkError>(
            "R_AARCH64_ADR_PREL_LO21 target is not an ADR instruction");

      Kind = aarch64::ADRLiteral21;
      break;
    }
    case ELFAdrPage21: {
      Kind = aarch64::Page21;
      break;
    }
    case ELFAddAbs12: {
      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFLdSt8Abs12: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLoadStoreImm12(Instr) ||
          aarch64::getPageOffset12Shift(Instr) != 0)
        return make_error<JITLinkError>(
            "R_AARCH64_LDST8_ABS_LO12_NC target is not a "
            "LDRB/STRB (imm12) instruction");

      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFLdSt16Abs12: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLoadStoreImm12(Instr) ||
          aarch64::getPageOffset12Shift(Instr) != 1)
        return make_error<JITLinkError>(
            "R_AARCH64_LDST16_ABS_LO12_NC target is not a "
            "LDRH/STRH (imm12) instruction");

      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFLdSt32Abs12: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLoadStoreImm12(Instr) ||
          aarch64::getPageOffset12Shift(Instr) != 2)
        return make_error<JITLinkError>(
            "R_AARCH64_LDST32_ABS_LO12_NC target is not a "
            "LDR/STR (imm12, 32 bit) instruction");

      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFLdSt64Abs12: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLoadStoreImm12(Instr) ||
          aarch64::getPageOffset12Shift(Instr) != 3)
        return make_error<JITLinkError>(
            "R_AARCH64_LDST64_ABS_LO12_NC target is not a "
            "LDR/STR (imm12, 64 bit) instruction");

      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFLdSt128Abs12: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isLoadStoreImm12(Instr) ||
          aarch64::getPageOffset12Shift(Instr) != 4)
        return make_error<JITLinkError>(
            "R_AARCH64_LDST128_ABS_LO12_NC target is not a "
            "LDR/STR (imm12, 128 bit) instruction");

      Kind = aarch64::PageOffset12;
      break;
    }
    case ELFMovwAbsG0: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isMoveWideImm16(Instr) ||
          aarch64::getMoveWide16Shift(Instr) != 0)
        return make_error<JITLinkError>(
            "R_AARCH64_MOVW_UABS_G0_NC target is not a "
            "MOVK/MOVZ (imm16, LSL #0) instruction");

      Kind = aarch64::MoveWide16;
      break;
    }
    case ELFMovwAbsG1: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isMoveWideImm16(Instr) ||
          aarch64::getMoveWide16Shift(Instr) != 16)
        return make_error<JITLinkError>(
            "R_AARCH64_MOVW_UABS_G1_NC target is not a "
            "MOVK/MOVZ (imm16, LSL #16) instruction");

      Kind = aarch64::MoveWide16;
      break;
    }
    case ELFMovwAbsG2: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isMoveWideImm16(Instr) ||
          aarch64::getMoveWide16Shift(Instr) != 32)
        return make_error<JITLinkError>(
            "R_AARCH64_MOVW_UABS_G2_NC target is not a "
            "MOVK/MOVZ (imm16, LSL #32) instruction");

      Kind = aarch64::MoveWide16;
      break;
    }
    case ELFMovwAbsG3: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isMoveWideImm16(Instr) ||
          aarch64::getMoveWide16Shift(Instr) != 48)
        return make_error<JITLinkError>(
            "R_AARCH64_MOVW_UABS_G3 target is not a "
            "MOVK/MOVZ (imm16, LSL #48) instruction");

      Kind = aarch64::MoveWide16;
      break;
    }
    case ELFTstBr14: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isTestAndBranchImm14(Instr))
        return make_error<JITLinkError>("R_AARCH64_TSTBR14 target is not a "
                                        "test and branch instruction");

      Kind = aarch64::TestAndBranch14PCRel;
      break;
    }
    case ELFCondBr19: {
      uint32_t Instr = *(const ulittle32_t *)FixupContent;
      if (!aarch64::isCondBranchImm19(Instr) &&
          !aarch64::isCompAndBranchImm19(Instr))
        return make_error<JITLinkError>("R_AARCH64_CONDBR19 target is not a "
                                        "conditional branch instruction");

      Kind = aarch64::CondBranch19PCRel;
      break;
    }
    case ELFAbs32: {
      Kind = aarch64::Pointer32;
      break;
    }
    case ELFAbs64: {
      Kind = aarch64::Pointer64;
      break;
    }
    case ELFPrel32: {
      Kind = aarch64::Delta32;
      break;
    }
    case ELFPrel64: {
      Kind = aarch64::Delta64;
      break;
    }
    case ELFAdrGOTPage21: {
      Kind = aarch64::RequestGOTAndTransformToPage21;
      break;
    }
    case ELFLd64GOTLo12: {
      Kind = aarch64::RequestGOTAndTransformToPageOffset12;
      break;
    }
    case ELFTLSDescAdrPage21: {
      Kind = aarch64::RequestTLSDescEntryAndTransformToPage21;
      break;
    }
    case ELFTLSDescAddLo12:
    case ELFTLSDescLd64Lo12: {
      Kind = aarch64::RequestTLSDescEntryAndTransformToPageOffset12;
      break;
    }
    case ELFTLSDescCall: {
      return Error::success();
    }
    };

    Edge GE(Kind, Offset, *GraphSymbol, Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, GE, aarch64::getEdgeKindName(Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(GE));

    return Error::success();
  }

  /// Return the string name of the given ELF aarch64 edge kind.
  const char *getELFAArch64RelocationKindName(Edge::Kind R) {
    switch (R) {
    case ELFCall26:
      return "ELFCall26";
    case ELFAdrPage21:
      return "ELFAdrPage21";
    case ELFAddAbs12:
      return "ELFAddAbs12";
    case ELFLdSt8Abs12:
      return "ELFLdSt8Abs12";
    case ELFLdSt16Abs12:
      return "ELFLdSt16Abs12";
    case ELFLdSt32Abs12:
      return "ELFLdSt32Abs12";
    case ELFLdSt64Abs12:
      return "ELFLdSt64Abs12";
    case ELFLdSt128Abs12:
      return "ELFLdSt128Abs12";
    case ELFMovwAbsG0:
      return "ELFMovwAbsG0";
    case ELFMovwAbsG1:
      return "ELFMovwAbsG1";
    case ELFMovwAbsG2:
      return "ELFMovwAbsG2";
    case ELFMovwAbsG3:
      return "ELFMovwAbsG3";
    case ELFAbs32:
      return "ELFAbs32";
    case ELFAbs64:
      return "ELFAbs64";
    case ELFPrel32:
      return "ELFPrel32";
    case ELFPrel64:
      return "ELFPrel64";
    case ELFAdrGOTPage21:
      return "ELFAdrGOTPage21";
    case ELFLd64GOTLo12:
      return "ELFLd64GOTLo12";
    case ELFTLSDescAdrPage21:
      return "ELFTLSDescAdrPage21";
    case ELFTLSDescAddLo12:
      return "ELFTLSDescAddLo12";
    case ELFTLSDescLd64Lo12:
      return "ELFTLSDescLd64Lo12";
    case ELFTLSDescCall:
      return "ELFTLSDescCall";
    default:
      return getGenericEdgeKindName(static_cast<Edge::Kind>(R));
    }
  }

public:
  ELFLinkGraphBuilder_aarch64(StringRef FileName,
                              const object::ELFFile<ELFT> &Obj, Triple TT,
                              SubtargetFeatures Features)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, aarch64::getEdgeKindName) {}
};

// TLS Info Builder.
class TLSInfoTableManager_ELF_aarch64
    : public TableManager<TLSInfoTableManager_ELF_aarch64> {
public:
  static StringRef getSectionName() { return "$__TLSINFO"; }

  static const uint8_t TLSInfoEntryContent[16];

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) { return false; }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    // the TLS Info entry's key value will be written by the fixTLVSectionByName
    // pass, so create mutable content.
    auto &TLSInfoEntry = G.createMutableContentBlock(
        getTLSInfoSection(G), G.allocateContent(getTLSInfoEntryContent()),
        orc::ExecutorAddr(), 8, 0);
    TLSInfoEntry.addEdge(aarch64::Pointer64, 8, Target, 0);
    return G.addAnonymousSymbol(TLSInfoEntry, 0, 16, false, false);
  }

private:
  Section &getTLSInfoSection(LinkGraph &G) {
    if (!TLSInfoTable)
      TLSInfoTable = &G.createSection(getSectionName(), orc::MemProt::Read);
    return *TLSInfoTable;
  }

  ArrayRef<char> getTLSInfoEntryContent() const {
    return {reinterpret_cast<const char *>(TLSInfoEntryContent),
            sizeof(TLSInfoEntryContent)};
  }

  Section *TLSInfoTable = nullptr;
};

const uint8_t TLSInfoTableManager_ELF_aarch64::TLSInfoEntryContent[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /*pthread key */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00  /*data address*/
};

// TLS Descriptor Builder.
class TLSDescTableManager_ELF_aarch64
    : public TableManager<TLSDescTableManager_ELF_aarch64> {
public:
  TLSDescTableManager_ELF_aarch64(
      TLSInfoTableManager_ELF_aarch64 &TLSInfoTableManager)
      : TLSInfoTableManager(TLSInfoTableManager) {}

  static StringRef getSectionName() { return "$__TLSDESC"; }

  static const uint8_t TLSDescEntryContent[16];

  bool visitEdge(LinkGraph &G, Block *B, Edge &E) {
    Edge::Kind KindToSet = Edge::Invalid;
    switch (E.getKind()) {
    case aarch64::RequestTLSDescEntryAndTransformToPage21: {
      KindToSet = aarch64::Page21;
      break;
    }
    case aarch64::RequestTLSDescEntryAndTransformToPageOffset12: {
      KindToSet = aarch64::PageOffset12;
      break;
    }
    default:
      return false;
    }
    assert(KindToSet != Edge::Invalid &&
           "Fell through switch, but no new kind to set");
    DEBUG_WITH_TYPE("jitlink", {
      dbgs() << "  Fixing " << G.getEdgeKindName(E.getKind()) << " edge at "
             << B->getFixupAddress(E) << " (" << B->getAddress() << " + "
             << formatv("{0:x}", E.getOffset()) << ")\n";
    });
    E.setKind(KindToSet);
    E.setTarget(getEntryForTarget(G, E.getTarget()));
    return true;
  }

  Symbol &createEntry(LinkGraph &G, Symbol &Target) {
    auto &EntryBlock =
        G.createContentBlock(getTLSDescSection(G), getTLSDescBlockContent(),
                             orc::ExecutorAddr(), 8, 0);
    EntryBlock.addEdge(aarch64::Pointer64, 0, getTLSDescResolver(G), 0);
    EntryBlock.addEdge(aarch64::Pointer64, 8,
                       TLSInfoTableManager.getEntryForTarget(G, Target), 0);
    return G.addAnonymousSymbol(EntryBlock, 0, 8, false, false);
  }

private:
  Section &getTLSDescSection(LinkGraph &G) {
    if (!GOTSection)
      GOTSection = &G.createSection(getSectionName(), orc::MemProt::Read);
    return *GOTSection;
  }

  Symbol &getTLSDescResolver(LinkGraph &G) {
    if (!TLSDescResolver)
      TLSDescResolver = &G.addExternalSymbol("__tlsdesc_resolver", 8, false);
    return *TLSDescResolver;
  }

  ArrayRef<char> getTLSDescBlockContent() {
    return {reinterpret_cast<const char *>(TLSDescEntryContent),
            sizeof(TLSDescEntryContent)};
  }

  Section *GOTSection = nullptr;
  Symbol *TLSDescResolver = nullptr;
  TLSInfoTableManager_ELF_aarch64 &TLSInfoTableManager;
};

const uint8_t TLSDescTableManager_ELF_aarch64::TLSDescEntryContent[16] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, /*resolver function pointer*/
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00 /*pointer to tls info*/
};

Error buildTables_ELF_aarch64(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  aarch64::GOTTableManager GOT;
  aarch64::PLTTableManager PLT(GOT);
  TLSInfoTableManager_ELF_aarch64 TLSInfo;
  TLSDescTableManager_ELF_aarch64 TLSDesc(TLSInfo);
  visitExistingEdges(G, GOT, PLT, TLSDesc, TLSInfo);
  return Error::success();
}

} // namespace

namespace llvm {
namespace jitlink {

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_aarch64(MemoryBufferRef ObjectBuffer) {
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

  assert((*ELFObj)->getArch() == Triple::aarch64 &&
         "Only AArch64 (little endian) is supported for now");

  auto &ELFObjFile = cast<object::ELFObjectFile<object::ELF64LE>>(**ELFObj);
  return ELFLinkGraphBuilder_aarch64<object::ELF64LE>(
             (*ELFObj)->getFileName(), ELFObjFile.getELFFile(),
             (*ELFObj)->makeTriple(), std::move(*Features))
      .buildGraph();
}

void link_ELF_aarch64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx) {
  PassConfiguration Config;
  const Triple &TT = G->getTargetTriple();
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    // Add eh-frame passes.
    Config.PrePrunePasses.push_back(DWARFRecordSectionSplitter(".eh_frame"));
    Config.PrePrunePasses.push_back(EHFrameEdgeFixer(
        ".eh_frame", 8, aarch64::Pointer32, aarch64::Pointer64,
        aarch64::Delta32, aarch64::Delta64, aarch64::NegDelta32));
    Config.PrePrunePasses.push_back(EHFrameNullTerminator(".eh_frame"));

    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Resolve any external section start / end symbols.
    Config.PostAllocationPasses.push_back(
        createDefineExternalSectionStartAndEndSymbolsPass(
            identifyELFSectionStartAndEndSymbols));

    // Add an in-place GOT/TLS/Stubs build pass.
    Config.PostPrunePasses.push_back(buildTables_ELF_aarch64);
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_aarch64::link(std::move(Ctx), std::move(G), std::move(Config));
}

} // namespace jitlink
} // namespace llvm
