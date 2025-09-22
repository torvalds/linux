//===----- ELF_aarch32.cpp - JIT linker implementation for arm/thumb ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// ELF/aarch32 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/ELF_aarch32.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITLink/aarch32.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/ARMTargetParser.h"

#include "ELFLinkGraphBuilder.h"
#include "JITLinkGeneric.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm::object;

namespace llvm {
namespace jitlink {

/// Translate from ELF relocation type to JITLink-internal edge kind.
Expected<aarch32::EdgeKind_aarch32>
getJITLinkEdgeKind(uint32_t ELFType, const aarch32::ArmConfig &ArmCfg) {
  switch (ELFType) {
  case ELF::R_ARM_ABS32:
    return aarch32::Data_Pointer32;
  case ELF::R_ARM_GOT_PREL:
    return aarch32::Data_RequestGOTAndTransformToDelta32;
  case ELF::R_ARM_REL32:
    return aarch32::Data_Delta32;
  case ELF::R_ARM_CALL:
    return aarch32::Arm_Call;
  case ELF::R_ARM_JUMP24:
    return aarch32::Arm_Jump24;
  case ELF::R_ARM_MOVW_ABS_NC:
    return aarch32::Arm_MovwAbsNC;
  case ELF::R_ARM_MOVT_ABS:
    return aarch32::Arm_MovtAbs;
  case ELF::R_ARM_NONE:
    return aarch32::None;
  case ELF::R_ARM_PREL31:
    return aarch32::Data_PRel31;
  case ELF::R_ARM_TARGET1:
    return (ArmCfg.Target1Rel) ? aarch32::Data_Delta32
                               : aarch32::Data_Pointer32;
  case ELF::R_ARM_THM_CALL:
    return aarch32::Thumb_Call;
  case ELF::R_ARM_THM_JUMP24:
    return aarch32::Thumb_Jump24;
  case ELF::R_ARM_THM_MOVW_ABS_NC:
    return aarch32::Thumb_MovwAbsNC;
  case ELF::R_ARM_THM_MOVT_ABS:
    return aarch32::Thumb_MovtAbs;
  case ELF::R_ARM_THM_MOVW_PREL_NC:
    return aarch32::Thumb_MovwPrelNC;
  case ELF::R_ARM_THM_MOVT_PREL:
    return aarch32::Thumb_MovtPrel;
  }

  return make_error<JITLinkError>(
      "Unsupported aarch32 relocation " + formatv("{0:d}: ", ELFType) +
      object::getELFRelocationTypeName(ELF::EM_ARM, ELFType));
}

/// Translate from JITLink-internal edge kind back to ELF relocation type.
Expected<uint32_t> getELFRelocationType(Edge::Kind Kind) {
  switch (static_cast<aarch32::EdgeKind_aarch32>(Kind)) {
  case aarch32::Data_Delta32:
    return ELF::R_ARM_REL32;
  case aarch32::Data_Pointer32:
    return ELF::R_ARM_ABS32;
  case aarch32::Data_PRel31:
    return ELF::R_ARM_PREL31;
  case aarch32::Data_RequestGOTAndTransformToDelta32:
    return ELF::R_ARM_GOT_PREL;
  case aarch32::Arm_Call:
    return ELF::R_ARM_CALL;
  case aarch32::Arm_Jump24:
    return ELF::R_ARM_JUMP24;
  case aarch32::Arm_MovwAbsNC:
    return ELF::R_ARM_MOVW_ABS_NC;
  case aarch32::Arm_MovtAbs:
    return ELF::R_ARM_MOVT_ABS;
  case aarch32::Thumb_Call:
    return ELF::R_ARM_THM_CALL;
  case aarch32::Thumb_Jump24:
    return ELF::R_ARM_THM_JUMP24;
  case aarch32::Thumb_MovwAbsNC:
    return ELF::R_ARM_THM_MOVW_ABS_NC;
  case aarch32::Thumb_MovtAbs:
    return ELF::R_ARM_THM_MOVT_ABS;
  case aarch32::Thumb_MovwPrelNC:
    return ELF::R_ARM_THM_MOVW_PREL_NC;
  case aarch32::Thumb_MovtPrel:
    return ELF::R_ARM_THM_MOVT_PREL;
  case aarch32::None:
    return ELF::R_ARM_NONE;
  }

  return make_error<JITLinkError>(formatv("Invalid aarch32 edge {0:d}: ",
                                          Kind));
}

/// Get a human-readable name for the given ELF AArch32 edge kind.
const char *getELFAArch32EdgeKindName(Edge::Kind R) {
  // No ELF-specific edge kinds yet
  return aarch32::getEdgeKindName(R);
}

class ELFJITLinker_aarch32 : public JITLinker<ELFJITLinker_aarch32> {
  friend class JITLinker<ELFJITLinker_aarch32>;

public:
  ELFJITLinker_aarch32(std::unique_ptr<JITLinkContext> Ctx,
                       std::unique_ptr<LinkGraph> G, PassConfiguration PassCfg,
                       aarch32::ArmConfig ArmCfg)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassCfg)),
        ArmCfg(std::move(ArmCfg)) {}

private:
  aarch32::ArmConfig ArmCfg;

  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return aarch32::applyFixup(G, B, E, ArmCfg);
  }
};

template <llvm::endianness DataEndianness>
class ELFLinkGraphBuilder_aarch32
    : public ELFLinkGraphBuilder<ELFType<DataEndianness, false>> {
private:
  using ELFT = ELFType<DataEndianness, false>;
  using Base = ELFLinkGraphBuilder<ELFT>;

  Error addRelocations() override {
    LLVM_DEBUG(dbgs() << "Processing relocations:\n");
    using Self = ELFLinkGraphBuilder_aarch32<DataEndianness>;
    for (const auto &RelSect : Base::Sections) {
      if (Error Err = Base::forEachRelRelocation(RelSect, this,
                                                 &Self::addSingleRelRelocation))
        return Err;
    }
    return Error::success();
  }

  Error addSingleRelRelocation(const typename ELFT::Rel &Rel,
                               const typename ELFT::Shdr &FixupSect,
                               Block &BlockToFix) {
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
    Expected<aarch32::EdgeKind_aarch32> Kind = getJITLinkEdgeKind(Type, ArmCfg);
    if (!Kind)
      return Kind.takeError();

    auto FixupAddress = orc::ExecutorAddr(FixupSect.sh_addr) + Rel.r_offset;
    Edge::OffsetT Offset = FixupAddress - BlockToFix.getAddress();

    Expected<int64_t> Addend =
        aarch32::readAddend(*Base::G, BlockToFix, Offset, *Kind, ArmCfg);
    if (!Addend)
      return Addend.takeError();

    Edge E(*Kind, Offset, *GraphSymbol, *Addend);
    LLVM_DEBUG({
      dbgs() << "    ";
      printEdge(dbgs(), BlockToFix, E, getELFAArch32EdgeKindName(*Kind));
      dbgs() << "\n";
    });

    BlockToFix.addEdge(std::move(E));
    return Error::success();
  }

  aarch32::ArmConfig ArmCfg;

protected:
  TargetFlagsType makeTargetFlags(const typename ELFT::Sym &Sym) override {
    // Only emit target flag for callable symbols
    if (Sym.getType() != ELF::STT_FUNC)
      return TargetFlagsType{};
    if (Sym.getValue() & 0x01)
      return aarch32::ThumbSymbol;
    return TargetFlagsType{};
  }

  orc::ExecutorAddrDiff getRawOffset(const typename ELFT::Sym &Sym,
                                     TargetFlagsType Flags) override {
    assert((makeTargetFlags(Sym) & Flags) == Flags);
    static constexpr uint64_t ThumbBit = 0x01;
    if (Sym.getType() == ELF::STT_FUNC)
      return Sym.getValue() & ~ThumbBit;
    return Sym.getValue();
  }

public:
  ELFLinkGraphBuilder_aarch32(StringRef FileName,
                              const llvm::object::ELFFile<ELFT> &Obj, Triple TT,
                              SubtargetFeatures Features,
                              aarch32::ArmConfig ArmCfg)
      : ELFLinkGraphBuilder<ELFT>(Obj, std::move(TT), std::move(Features),
                                  FileName, getELFAArch32EdgeKindName),
        ArmCfg(std::move(ArmCfg)) {}
};

template <typename StubsManagerType>
Error buildTables_ELF_aarch32(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  StubsManagerType StubsManager;
  visitExistingEdges(G, StubsManager);
  aarch32::GOTBuilder GOT;
  visitExistingEdges(G, GOT);

  return Error::success();
}

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromELFObject_aarch32(MemoryBufferRef ObjectBuffer) {
  LLVM_DEBUG({
    dbgs() << "Building jitlink graph for new input "
           << ObjectBuffer.getBufferIdentifier() << "...\n";
  });

  auto ELFObj = ObjectFile::createELFObjectFile(ObjectBuffer);
  if (!ELFObj)
    return ELFObj.takeError();

  auto Features = (*ELFObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  // Find out what exact AArch32 instruction set and features we target.
  auto TT = (*ELFObj)->makeTriple();
  ARM::ArchKind AK = ARM::parseArch(TT.getArchName());
  if (AK == ARM::ArchKind::INVALID)
    return make_error<JITLinkError>(
        "Failed to build ELF link graph: Invalid ARM ArchKind");

  // Resolve our internal configuration for the target. If at some point the
  // CPUArch alone becomes too unprecise, we can find more details in the
  // Tag_CPU_arch_profile.
  auto Arch = static_cast<ARMBuildAttrs::CPUArch>(ARM::getArchAttr(AK));
  aarch32::ArmConfig ArmCfg = aarch32::getArmConfigForCPUArch(Arch);

  // Populate the link-graph.
  switch (TT.getArch()) {
  case Triple::arm:
  case Triple::thumb: {
    auto &ELFFile = cast<ELFObjectFile<ELF32LE>>(**ELFObj).getELFFile();
    return ELFLinkGraphBuilder_aarch32<llvm::endianness::little>(
               (*ELFObj)->getFileName(), ELFFile, TT, std::move(*Features),
               ArmCfg)
        .buildGraph();
  }
  case Triple::armeb:
  case Triple::thumbeb: {
    auto &ELFFile = cast<ELFObjectFile<ELF32BE>>(**ELFObj).getELFFile();
    return ELFLinkGraphBuilder_aarch32<llvm::endianness::big>(
               (*ELFObj)->getFileName(), ELFFile, TT, std::move(*Features),
               ArmCfg)
        .buildGraph();
  }
  default:
    return make_error<JITLinkError>(
        "Failed to build ELF/aarch32 link graph: Invalid target triple " +
        TT.getTriple());
  }
}

void link_ELF_aarch32(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx) {
  const Triple &TT = G->getTargetTriple();

  using namespace ARMBuildAttrs;
  ARM::ArchKind AK = ARM::parseArch(TT.getArchName());
  auto CPU = static_cast<CPUArch>(ARM::getArchAttr(AK));
  aarch32::ArmConfig ArmCfg = aarch32::getArmConfigForCPUArch(CPU);

  PassConfiguration PassCfg;
  if (Ctx->shouldAddDefaultTargetPasses(TT)) {
    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(TT))
      PassCfg.PrePrunePasses.push_back(std::move(MarkLive));
    else
      PassCfg.PrePrunePasses.push_back(markAllSymbolsLive);

    switch (ArmCfg.Stubs) {
    case aarch32::StubsFlavor::pre_v7:
      PassCfg.PostPrunePasses.push_back(
          buildTables_ELF_aarch32<aarch32::StubsManager_prev7>);
      break;
    case aarch32::StubsFlavor::v7:
      PassCfg.PostPrunePasses.push_back(
          buildTables_ELF_aarch32<aarch32::StubsManager_v7>);
      break;
    case aarch32::StubsFlavor::Undefined:
      llvm_unreachable("Check before building graph");
    }
  }

  if (auto Err = Ctx->modifyPassConfig(*G, PassCfg))
    return Ctx->notifyFailed(std::move(Err));

  ELFJITLinker_aarch32::link(std::move(Ctx), std::move(G), std::move(PassCfg),
                             std::move(ArmCfg));
}

} // namespace jitlink
} // namespace llvm
