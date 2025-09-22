//===---- MachO_x86_64.cpp -JIT linker implementation for MachO/x86-64 ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachO/x86-64 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/MachO_x86_64.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/x86_64.h"

#include "DefineExternalSectionStartAndEndSymbols.h"
#include "MachOLinkGraphBuilder.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {

class MachOLinkGraphBuilder_x86_64 : public MachOLinkGraphBuilder {
public:
  MachOLinkGraphBuilder_x86_64(const object::MachOObjectFile &Obj,
                               SubtargetFeatures Features)
      : MachOLinkGraphBuilder(Obj, Triple("x86_64-apple-darwin"),
                              std::move(Features), x86_64::getEdgeKindName) {}

private:
  enum MachONormalizedRelocationType : unsigned {
    MachOBranch32,
    MachOPointer32,
    MachOPointer64,
    MachOPointer64Anon,
    MachOPCRel32,
    MachOPCRel32Minus1,
    MachOPCRel32Minus2,
    MachOPCRel32Minus4,
    MachOPCRel32Anon,
    MachOPCRel32Minus1Anon,
    MachOPCRel32Minus2Anon,
    MachOPCRel32Minus4Anon,
    MachOPCRel32GOTLoad,
    MachOPCRel32GOT,
    MachOPCRel32TLV,
    MachOSubtractor32,
    MachOSubtractor64,
  };

  static Expected<MachONormalizedRelocationType>
  getRelocKind(const MachO::relocation_info &RI) {
    switch (RI.r_type) {
    case MachO::X86_64_RELOC_UNSIGNED:
      if (!RI.r_pcrel) {
        if (RI.r_length == 3)
          return RI.r_extern ? MachOPointer64 : MachOPointer64Anon;
        else if (RI.r_extern && RI.r_length == 2)
          return MachOPointer32;
      }
      break;
    case MachO::X86_64_RELOC_SIGNED:
      if (RI.r_pcrel && RI.r_length == 2)
        return RI.r_extern ? MachOPCRel32 : MachOPCRel32Anon;
      break;
    case MachO::X86_64_RELOC_BRANCH:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOBranch32;
      break;
    case MachO::X86_64_RELOC_GOT_LOAD:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPCRel32GOTLoad;
      break;
    case MachO::X86_64_RELOC_GOT:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPCRel32GOT;
      break;
    case MachO::X86_64_RELOC_SUBTRACTOR:
      if (!RI.r_pcrel && RI.r_extern) {
        if (RI.r_length == 2)
          return MachOSubtractor32;
        else if (RI.r_length == 3)
          return MachOSubtractor64;
      }
      break;
    case MachO::X86_64_RELOC_SIGNED_1:
      if (RI.r_pcrel && RI.r_length == 2)
        return RI.r_extern ? MachOPCRel32Minus1 : MachOPCRel32Minus1Anon;
      break;
    case MachO::X86_64_RELOC_SIGNED_2:
      if (RI.r_pcrel && RI.r_length == 2)
        return RI.r_extern ? MachOPCRel32Minus2 : MachOPCRel32Minus2Anon;
      break;
    case MachO::X86_64_RELOC_SIGNED_4:
      if (RI.r_pcrel && RI.r_length == 2)
        return RI.r_extern ? MachOPCRel32Minus4 : MachOPCRel32Minus4Anon;
      break;
    case MachO::X86_64_RELOC_TLV:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPCRel32TLV;
      break;
    }

    return make_error<JITLinkError>(
        "Unsupported x86-64 relocation: address=" +
        formatv("{0:x8}", RI.r_address) +
        ", symbolnum=" + formatv("{0:x6}", RI.r_symbolnum) +
        ", kind=" + formatv("{0:x1}", RI.r_type) +
        ", pc_rel=" + (RI.r_pcrel ? "true" : "false") +
        ", extern=" + (RI.r_extern ? "true" : "false") +
        ", length=" + formatv("{0:d}", RI.r_length));
  }

  using PairRelocInfo = std::tuple<Edge::Kind, Symbol *, uint64_t>;

  // Parses paired SUBTRACTOR/UNSIGNED relocations and, on success,
  // returns the edge kind and addend to be used.
  Expected<PairRelocInfo> parsePairRelocation(
      Block &BlockToFix, MachONormalizedRelocationType SubtractorKind,
      const MachO::relocation_info &SubRI, orc::ExecutorAddr FixupAddress,
      const char *FixupContent, object::relocation_iterator &UnsignedRelItr,
      object::relocation_iterator &RelEnd) {
    using namespace support;

    assert(((SubtractorKind == MachOSubtractor32 && SubRI.r_length == 2) ||
            (SubtractorKind == MachOSubtractor64 && SubRI.r_length == 3)) &&
           "Subtractor kind should match length");
    assert(SubRI.r_extern && "SUBTRACTOR reloc symbol should be extern");
    assert(!SubRI.r_pcrel && "SUBTRACTOR reloc should not be PCRel");

    if (UnsignedRelItr == RelEnd)
      return make_error<JITLinkError>("x86_64 SUBTRACTOR without paired "
                                      "UNSIGNED relocation");

    auto UnsignedRI = getRelocationInfo(UnsignedRelItr);

    if (SubRI.r_address != UnsignedRI.r_address)
      return make_error<JITLinkError>("x86_64 SUBTRACTOR and paired UNSIGNED "
                                      "point to different addresses");

    if (SubRI.r_length != UnsignedRI.r_length)
      return make_error<JITLinkError>("length of x86_64 SUBTRACTOR and paired "
                                      "UNSIGNED reloc must match");

    Symbol *FromSymbol;
    if (auto FromSymbolOrErr = findSymbolByIndex(SubRI.r_symbolnum))
      FromSymbol = FromSymbolOrErr->GraphSymbol;
    else
      return FromSymbolOrErr.takeError();

    // Read the current fixup value.
    uint64_t FixupValue = 0;
    if (SubRI.r_length == 3)
      FixupValue = *(const little64_t *)FixupContent;
    else
      FixupValue = *(const little32_t *)FixupContent;

    // Find 'ToSymbol' using symbol number or address, depending on whether the
    // paired UNSIGNED relocation is extern.
    Symbol *ToSymbol = nullptr;
    if (UnsignedRI.r_extern) {
      // Find target symbol by symbol index.
      if (auto ToSymbolOrErr = findSymbolByIndex(UnsignedRI.r_symbolnum))
        ToSymbol = ToSymbolOrErr->GraphSymbol;
      else
        return ToSymbolOrErr.takeError();
    } else {
      auto ToSymbolSec = findSectionByIndex(UnsignedRI.r_symbolnum - 1);
      if (!ToSymbolSec)
        return ToSymbolSec.takeError();
      ToSymbol = getSymbolByAddress(*ToSymbolSec, ToSymbolSec->Address);
      assert(ToSymbol && "No symbol for section");
      FixupValue -= ToSymbol->getAddress().getValue();
    }

    Edge::Kind DeltaKind;
    Symbol *TargetSymbol;
    uint64_t Addend;

    bool FixingFromSymbol = true;
    if (&BlockToFix == &FromSymbol->getAddressable()) {
      if (LLVM_UNLIKELY(&BlockToFix == &ToSymbol->getAddressable())) {
        // From and To are symbols in the same block. Decide direction by offset
        // instead.
        if (ToSymbol->getAddress() > FixupAddress)
          FixingFromSymbol = true;
        else if (FromSymbol->getAddress() > FixupAddress)
          FixingFromSymbol = false;
        else
          FixingFromSymbol = FromSymbol->getAddress() >= ToSymbol->getAddress();
      } else
        FixingFromSymbol = true;
    } else {
      if (&BlockToFix == &ToSymbol->getAddressable())
        FixingFromSymbol = false;
      else {
        // BlockToFix was neither FromSymbol nor ToSymbol.
        return make_error<JITLinkError>("SUBTRACTOR relocation must fix up "
                                        "either 'A' or 'B' (or a symbol in one "
                                        "of their alt-entry groups)");
      }
    }

    if (FixingFromSymbol) {
      TargetSymbol = ToSymbol;
      DeltaKind = (SubRI.r_length == 3) ? x86_64::Delta64 : x86_64::Delta32;
      Addend = FixupValue + (FixupAddress - FromSymbol->getAddress());
      // FIXME: handle extern 'from'.
    } else {
      TargetSymbol = FromSymbol;
      DeltaKind =
          (SubRI.r_length == 3) ? x86_64::NegDelta64 : x86_64::NegDelta32;
      Addend = FixupValue - (FixupAddress - ToSymbol->getAddress());
    }

    return PairRelocInfo(DeltaKind, TargetSymbol, Addend);
  }

  Error addRelocations() override {
    using namespace support;
    auto &Obj = getObject();

    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    for (const auto &S : Obj.sections()) {

      orc::ExecutorAddr SectionAddress(S.getAddress());

      // Skip relocations virtual sections.
      if (S.isVirtual()) {
        if (S.relocation_begin() != S.relocation_end())
          return make_error<JITLinkError>("Virtual section contains "
                                          "relocations");
        continue;
      }

      auto NSec =
          findSectionByIndex(Obj.getSectionIndex(S.getRawDataRefImpl()));
      if (!NSec)
        return NSec.takeError();

      // Skip relocations for MachO sections without corresponding graph
      // sections.
      {
        if (!NSec->GraphSection) {
          LLVM_DEBUG({
            dbgs() << "  Skipping relocations for MachO section "
                   << NSec->SegName << "/" << NSec->SectName
                   << " which has no associated graph section\n";
          });
          continue;
        }
      }

      // Add relocations for section.
      for (auto RelItr = S.relocation_begin(), RelEnd = S.relocation_end();
           RelItr != RelEnd; ++RelItr) {

        MachO::relocation_info RI = getRelocationInfo(RelItr);

        // Find the address of the value to fix up.
        auto FixupAddress = SectionAddress + (uint32_t)RI.r_address;

        LLVM_DEBUG({
          dbgs() << "  " << NSec->SectName << " + "
                 << formatv("{0:x8}", RI.r_address) << ":\n";
        });

        // Find the block that the fixup points to.
        Block *BlockToFix = nullptr;
        {
          auto SymbolToFixOrErr = findSymbolByAddress(*NSec, FixupAddress);
          if (!SymbolToFixOrErr)
            return SymbolToFixOrErr.takeError();
          BlockToFix = &SymbolToFixOrErr->getBlock();
        }

        if (FixupAddress + orc::ExecutorAddrDiff(1ULL << RI.r_length) >
            BlockToFix->getAddress() + BlockToFix->getContent().size())
          return make_error<JITLinkError>(
              "Relocation extends past end of fixup block");

        // Get a pointer to the fixup content.
        const char *FixupContent = BlockToFix->getContent().data() +
                                   (FixupAddress - BlockToFix->getAddress());

        size_t FixupOffset = FixupAddress - BlockToFix->getAddress();

        // The target symbol and addend will be populated by the switch below.
        Symbol *TargetSymbol = nullptr;
        uint64_t Addend = 0;

        // Validate the relocation kind.
        auto MachORelocKind = getRelocKind(RI);
        if (!MachORelocKind)
          return MachORelocKind.takeError();

        Edge::Kind Kind = Edge::Invalid;

        switch (*MachORelocKind) {
        case MachOBranch32:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent;
          Kind = x86_64::BranchPCRel32;
          break;
        case MachOPCRel32:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent - 4;
          Kind = x86_64::Delta32;
          break;
        case MachOPCRel32GOTLoad:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent;
          Kind = x86_64::RequestGOTAndTransformToPCRel32GOTLoadREXRelaxable;
          if (FixupOffset < 3)
            return make_error<JITLinkError>("GOTLD at invalid offset " +
                                            formatv("{0}", FixupOffset));
          break;
        case MachOPCRel32GOT:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent - 4;
          Kind = x86_64::RequestGOTAndTransformToDelta32;
          break;
        case MachOPCRel32TLV:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent;
          Kind = x86_64::RequestTLVPAndTransformToPCRel32TLVPLoadREXRelaxable;
          if (FixupOffset < 3)
            return make_error<JITLinkError>("TLV at invalid offset " +
                                            formatv("{0}", FixupOffset));
          break;
        case MachOPointer32:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const ulittle32_t *)FixupContent;
          Kind = x86_64::Pointer32;
          break;
        case MachOPointer64:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const ulittle64_t *)FixupContent;
          Kind = x86_64::Pointer64;
          break;
        case MachOPointer64Anon: {
          orc::ExecutorAddr TargetAddress(*(const ulittle64_t *)FixupContent);
          auto TargetNSec = findSectionByIndex(RI.r_symbolnum - 1);
          if (!TargetNSec)
            return TargetNSec.takeError();
          if (auto TargetSymbolOrErr =
                  findSymbolByAddress(*TargetNSec, TargetAddress))
            TargetSymbol = &*TargetSymbolOrErr;
          else
            return TargetSymbolOrErr.takeError();
          Addend = TargetAddress - TargetSymbol->getAddress();
          Kind = x86_64::Pointer64;
          break;
        }
        case MachOPCRel32Minus1:
        case MachOPCRel32Minus2:
        case MachOPCRel32Minus4:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const little32_t *)FixupContent - 4;
          Kind = x86_64::Delta32;
          break;
        case MachOPCRel32Anon: {
          orc::ExecutorAddr TargetAddress(FixupAddress + 4 +
                                          *(const little32_t *)FixupContent);
          auto TargetNSec = findSectionByIndex(RI.r_symbolnum - 1);
          if (!TargetNSec)
            return TargetNSec.takeError();
          if (auto TargetSymbolOrErr =
                  findSymbolByAddress(*TargetNSec, TargetAddress))
            TargetSymbol = &*TargetSymbolOrErr;
          else
            return TargetSymbolOrErr.takeError();
          Addend = TargetAddress - TargetSymbol->getAddress() - 4;
          Kind = x86_64::Delta32;
          break;
        }
        case MachOPCRel32Minus1Anon:
        case MachOPCRel32Minus2Anon:
        case MachOPCRel32Minus4Anon: {
          orc::ExecutorAddrDiff Delta =
              4 + orc::ExecutorAddrDiff(
                      1ULL << (*MachORelocKind - MachOPCRel32Minus1Anon));
          orc::ExecutorAddr TargetAddress =
              FixupAddress + Delta + *(const little32_t *)FixupContent;
          auto TargetNSec = findSectionByIndex(RI.r_symbolnum - 1);
          if (!TargetNSec)
            return TargetNSec.takeError();
          if (auto TargetSymbolOrErr =
                  findSymbolByAddress(*TargetNSec, TargetAddress))
            TargetSymbol = &*TargetSymbolOrErr;
          else
            return TargetSymbolOrErr.takeError();
          Addend = TargetAddress - TargetSymbol->getAddress() - Delta;
          Kind = x86_64::Delta32;
          break;
        }
        case MachOSubtractor32:
        case MachOSubtractor64: {
          // We use Delta32/Delta64 to represent SUBTRACTOR relocations.
          // parsePairRelocation handles the paired reloc, and returns the
          // edge kind to be used (either Delta32/Delta64, or
          // NegDelta32/NegDelta64, depending on the direction of the
          // subtraction) along with the addend.
          auto PairInfo =
              parsePairRelocation(*BlockToFix, *MachORelocKind, RI,
                                  FixupAddress, FixupContent, ++RelItr, RelEnd);
          if (!PairInfo)
            return PairInfo.takeError();
          std::tie(Kind, TargetSymbol, Addend) = *PairInfo;
          assert(TargetSymbol && "No target symbol from parsePairRelocation?");
          break;
        }
        }

        LLVM_DEBUG({
          dbgs() << "    ";
          Edge GE(Kind, FixupAddress - BlockToFix->getAddress(), *TargetSymbol,
                  Addend);
          printEdge(dbgs(), *BlockToFix, GE, x86_64::getEdgeKindName(Kind));
          dbgs() << "\n";
        });
        BlockToFix->addEdge(Kind, FixupAddress - BlockToFix->getAddress(),
                            *TargetSymbol, Addend);
      }
    }
    return Error::success();
  }
};

Error buildGOTAndStubs_MachO_x86_64(LinkGraph &G) {
  x86_64::GOTTableManager GOT;
  x86_64::PLTTableManager PLT(GOT);
  visitExistingEdges(G, GOT, PLT);
  return Error::success();
}

} // namespace

namespace llvm {
namespace jitlink {

class MachOJITLinker_x86_64 : public JITLinker<MachOJITLinker_x86_64> {
  friend class JITLinker<MachOJITLinker_x86_64>;

public:
  MachOJITLinker_x86_64(std::unique_ptr<JITLinkContext> Ctx,
                        std::unique_ptr<LinkGraph> G,
                        PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {}

private:
  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return x86_64::applyFixup(G, B, E, nullptr);
  }
};

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromMachOObject_x86_64(MemoryBufferRef ObjectBuffer) {
  auto MachOObj = object::ObjectFile::createMachOObjectFile(ObjectBuffer);
  if (!MachOObj)
    return MachOObj.takeError();

  auto Features = (*MachOObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  return MachOLinkGraphBuilder_x86_64(**MachOObj, std::move(*Features))
      .buildGraph();
}

void link_MachO_x86_64(std::unique_ptr<LinkGraph> G,
                       std::unique_ptr<JITLinkContext> Ctx) {

  PassConfiguration Config;

  if (Ctx->shouldAddDefaultTargetPasses(G->getTargetTriple())) {
    // Add eh-frame passes.
    Config.PrePrunePasses.push_back(createEHFrameSplitterPass_MachO_x86_64());
    Config.PrePrunePasses.push_back(createEHFrameEdgeFixerPass_MachO_x86_64());

    // Add compact unwind splitter pass.
    Config.PrePrunePasses.push_back(
        CompactUnwindSplitter("__LD,__compact_unwind"));

    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(G->getTargetTriple()))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Resolve any external section start / end symbols.
    Config.PostAllocationPasses.push_back(
        createDefineExternalSectionStartAndEndSymbolsPass(
            identifyMachOSectionStartAndEndSymbols));

    // Add an in-place GOT/Stubs pass.
    Config.PostPrunePasses.push_back(buildGOTAndStubs_MachO_x86_64);

    // Add GOT/Stubs optimizer pass.
    Config.PreFixupPasses.push_back(x86_64::optimizeGOTAndStubAccesses);
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  // Construct a JITLinker and run the link function.
  MachOJITLinker_x86_64::link(std::move(Ctx), std::move(G), std::move(Config));
}

LinkGraphPassFunction createEHFrameSplitterPass_MachO_x86_64() {
  return DWARFRecordSectionSplitter("__TEXT,__eh_frame");
}

LinkGraphPassFunction createEHFrameEdgeFixerPass_MachO_x86_64() {
  return EHFrameEdgeFixer("__TEXT,__eh_frame", x86_64::PointerSize,
                          x86_64::Pointer32, x86_64::Pointer64, x86_64::Delta32,
                          x86_64::Delta64, x86_64::NegDelta32);
}

} // end namespace jitlink
} // end namespace llvm
