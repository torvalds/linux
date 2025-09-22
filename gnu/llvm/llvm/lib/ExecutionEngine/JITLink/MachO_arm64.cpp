//===---- MachO_arm64.cpp - JIT linker implementation for MachO/arm64 -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachO/arm64 jit-link implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/MachO_arm64.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/JITLink/aarch64.h"

#include "DefineExternalSectionStartAndEndSymbols.h"
#include "MachOLinkGraphBuilder.h"

#define DEBUG_TYPE "jitlink"

using namespace llvm;
using namespace llvm::jitlink;

namespace {

class MachOLinkGraphBuilder_arm64 : public MachOLinkGraphBuilder {
public:
  MachOLinkGraphBuilder_arm64(const object::MachOObjectFile &Obj,
                              SubtargetFeatures Features)
      : MachOLinkGraphBuilder(Obj, Triple("arm64-apple-darwin"),
                              std::move(Features), aarch64::getEdgeKindName),
        NumSymbols(Obj.getSymtabLoadCommand().nsyms) {}

private:
  enum MachOARM64RelocationKind : Edge::Kind {
    MachOBranch26 = Edge::FirstRelocation,
    MachOPointer32,
    MachOPointer64,
    MachOPointer64Anon,
    MachOPage21,
    MachOPageOffset12,
    MachOGOTPage21,
    MachOGOTPageOffset12,
    MachOTLVPage21,
    MachOTLVPageOffset12,
    MachOPointerToGOT,
    MachOPairedAddend,
    MachOLDRLiteral19,
    MachODelta32,
    MachODelta64,
    MachONegDelta32,
    MachONegDelta64,
  };

  static Expected<MachOARM64RelocationKind>
  getRelocationKind(const MachO::relocation_info &RI) {
    switch (RI.r_type) {
    case MachO::ARM64_RELOC_UNSIGNED:
      if (!RI.r_pcrel) {
        if (RI.r_length == 3)
          return RI.r_extern ? MachOPointer64 : MachOPointer64Anon;
        else if (RI.r_length == 2)
          return MachOPointer32;
      }
      break;
    case MachO::ARM64_RELOC_SUBTRACTOR:
      // SUBTRACTOR must be non-pc-rel, extern, with length 2 or 3.
      // Initially represent SUBTRACTOR relocations with 'Delta<W>'.
      // They may be turned into NegDelta<W> by parsePairRelocation.
      if (!RI.r_pcrel && RI.r_extern) {
        if (RI.r_length == 2)
          return MachODelta32;
        else if (RI.r_length == 3)
          return MachODelta64;
      }
      break;
    case MachO::ARM64_RELOC_BRANCH26:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOBranch26;
      break;
    case MachO::ARM64_RELOC_PAGE21:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPage21;
      break;
    case MachO::ARM64_RELOC_PAGEOFF12:
      if (!RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPageOffset12;
      break;
    case MachO::ARM64_RELOC_GOT_LOAD_PAGE21:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOGOTPage21;
      break;
    case MachO::ARM64_RELOC_GOT_LOAD_PAGEOFF12:
      if (!RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOGOTPageOffset12;
      break;
    case MachO::ARM64_RELOC_POINTER_TO_GOT:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOPointerToGOT;
      break;
    case MachO::ARM64_RELOC_ADDEND:
      if (!RI.r_pcrel && !RI.r_extern && RI.r_length == 2)
        return MachOPairedAddend;
      break;
    case MachO::ARM64_RELOC_TLVP_LOAD_PAGE21:
      if (RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOTLVPage21;
      break;
    case MachO::ARM64_RELOC_TLVP_LOAD_PAGEOFF12:
      if (!RI.r_pcrel && RI.r_extern && RI.r_length == 2)
        return MachOTLVPageOffset12;
      break;
    }

    return make_error<JITLinkError>(
        "Unsupported arm64 relocation: address=" +
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
  Expected<PairRelocInfo>
  parsePairRelocation(Block &BlockToFix, Edge::Kind SubtractorKind,
                      const MachO::relocation_info &SubRI,
                      orc::ExecutorAddr FixupAddress, const char *FixupContent,
                      object::relocation_iterator &UnsignedRelItr,
                      object::relocation_iterator &RelEnd) {
    using namespace support;

    assert(((SubtractorKind == MachODelta32 && SubRI.r_length == 2) ||
            (SubtractorKind == MachODelta64 && SubRI.r_length == 3)) &&
           "Subtractor kind should match length");
    assert(SubRI.r_extern && "SUBTRACTOR reloc symbol should be extern");
    assert(!SubRI.r_pcrel && "SUBTRACTOR reloc should not be PCRel");

    if (UnsignedRelItr == RelEnd)
      return make_error<JITLinkError>("arm64 SUBTRACTOR without paired "
                                      "UNSIGNED relocation");

    auto UnsignedRI = getRelocationInfo(UnsignedRelItr);

    if (SubRI.r_address != UnsignedRI.r_address)
      return make_error<JITLinkError>("arm64 SUBTRACTOR and paired UNSIGNED "
                                      "point to different addresses");

    if (SubRI.r_length != UnsignedRI.r_length)
      return make_error<JITLinkError>("length of arm64 SUBTRACTOR and paired "
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
      DeltaKind = (SubRI.r_length == 3) ? aarch64::Delta64 : aarch64::Delta32;
      Addend = FixupValue + (FixupAddress - FromSymbol->getAddress());
      // FIXME: handle extern 'from'.
    } else {
      TargetSymbol = &*FromSymbol;
      DeltaKind =
          (SubRI.r_length == 3) ? aarch64::NegDelta64 : aarch64::NegDelta32;
      Addend = FixupValue - (FixupAddress - ToSymbol->getAddress());
    }

    return PairRelocInfo(DeltaKind, TargetSymbol, Addend);
  }

  Error addRelocations() override {
    using namespace support;
    auto &Obj = getObject();

    LLVM_DEBUG(dbgs() << "Processing relocations:\n");

    for (auto &S : Obj.sections()) {

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

      for (auto RelItr = S.relocation_begin(), RelEnd = S.relocation_end();
           RelItr != RelEnd; ++RelItr) {

        MachO::relocation_info RI = getRelocationInfo(RelItr);

        // Validate the relocation kind.
        auto MachORelocKind = getRelocationKind(RI);
        if (!MachORelocKind)
          return MachORelocKind.takeError();

        // Find the address of the value to fix up.
        orc::ExecutorAddr FixupAddress =
            SectionAddress + (uint32_t)RI.r_address;
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
              "Relocation content extends past end of fixup block");

        Edge::Kind Kind = Edge::Invalid;

        // Get a pointer to the fixup content.
        const char *FixupContent = BlockToFix->getContent().data() +
                                   (FixupAddress - BlockToFix->getAddress());

        // The target symbol and addend will be populated by the switch below.
        Symbol *TargetSymbol = nullptr;
        uint64_t Addend = 0;

        if (*MachORelocKind == MachOPairedAddend) {
          // If this is an Addend relocation then process it and move to the
          // paired reloc.

          Addend = SignExtend64(RI.r_symbolnum, 24);

          ++RelItr;
          if (RelItr == RelEnd)
            return make_error<JITLinkError>("Unpaired Addend reloc at " +
                                            formatv("{0:x16}", FixupAddress));
          RI = getRelocationInfo(RelItr);

          MachORelocKind = getRelocationKind(RI);
          if (!MachORelocKind)
            return MachORelocKind.takeError();

          if (*MachORelocKind != MachOBranch26 &&
              *MachORelocKind != MachOPage21 &&
              *MachORelocKind != MachOPageOffset12)
            return make_error<JITLinkError>(
                "Invalid relocation pair: Addend + " +
                StringRef(getMachOARM64RelocationKindName(*MachORelocKind)));

          LLVM_DEBUG({
            dbgs() << "    Addend: value = " << formatv("{0:x6}", Addend)
                   << ", pair is "
                   << getMachOARM64RelocationKindName(*MachORelocKind) << "\n";
          });

          // Find the address of the value to fix up.
          orc::ExecutorAddr PairedFixupAddress =
              SectionAddress + (uint32_t)RI.r_address;
          if (PairedFixupAddress != FixupAddress)
            return make_error<JITLinkError>("Paired relocation points at "
                                            "different target");
        }

        switch (*MachORelocKind) {
        case MachOBranch26: {
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          uint32_t Instr = *(const ulittle32_t *)FixupContent;
          if ((Instr & 0x7fffffff) != 0x14000000)
            return make_error<JITLinkError>("BRANCH26 target is not a B or BL "
                                            "instruction with a zero addend");
          Kind = aarch64::Branch26PCRel;
          break;
        }
        case MachOPointer32:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const ulittle32_t *)FixupContent;
          Kind = aarch64::Pointer32;
          break;
        case MachOPointer64:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          Addend = *(const ulittle64_t *)FixupContent;
          Kind = aarch64::Pointer64;
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
          Kind = aarch64::Pointer64;
          break;
        }
        case MachOPage21:
        case MachOGOTPage21:
        case MachOTLVPage21: {
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          uint32_t Instr = *(const ulittle32_t *)FixupContent;
          if ((Instr & 0xffffffe0) != 0x90000000)
            return make_error<JITLinkError>("PAGE21/GOTPAGE21 target is not an "
                                            "ADRP instruction with a zero "
                                            "addend");

          if (*MachORelocKind == MachOPage21) {
            Kind = aarch64::Page21;
          } else if (*MachORelocKind == MachOGOTPage21) {
            Kind = aarch64::RequestGOTAndTransformToPage21;
          } else if (*MachORelocKind == MachOTLVPage21) {
            Kind = aarch64::RequestTLVPAndTransformToPage21;
          }
          break;
        }
        case MachOPageOffset12: {
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          uint32_t Instr = *(const ulittle32_t *)FixupContent;
          uint32_t EncodedAddend = (Instr & 0x003FFC00) >> 10;
          if (EncodedAddend != 0)
            return make_error<JITLinkError>("GOTPAGEOFF12 target has non-zero "
                                            "encoded addend");
          Kind = aarch64::PageOffset12;
          break;
        }
        case MachOGOTPageOffset12:
        case MachOTLVPageOffset12: {
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();
          uint32_t Instr = *(const ulittle32_t *)FixupContent;
          if ((Instr & 0xfffffc00) != 0xf9400000)
            return make_error<JITLinkError>("GOTPAGEOFF12 target is not an LDR "
                                            "immediate instruction with a zero "
                                            "addend");

          if (*MachORelocKind == MachOGOTPageOffset12) {
            Kind = aarch64::RequestGOTAndTransformToPageOffset12;
          } else if (*MachORelocKind == MachOTLVPageOffset12) {
            Kind = aarch64::RequestTLVPAndTransformToPageOffset12;
          }
          break;
        }
        case MachOPointerToGOT:
          if (auto TargetSymbolOrErr = findSymbolByIndex(RI.r_symbolnum))
            TargetSymbol = TargetSymbolOrErr->GraphSymbol;
          else
            return TargetSymbolOrErr.takeError();

          Kind = aarch64::RequestGOTAndTransformToDelta32;
          break;
        case MachODelta32:
        case MachODelta64: {
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
        default:
          llvm_unreachable("Special relocation kind should not appear in "
                           "mach-o file");
        }

        LLVM_DEBUG({
          dbgs() << "    ";
          Edge GE(Kind, FixupAddress - BlockToFix->getAddress(), *TargetSymbol,
                  Addend);
          printEdge(dbgs(), *BlockToFix, GE, aarch64::getEdgeKindName(Kind));
          dbgs() << "\n";
        });
        BlockToFix->addEdge(Kind, FixupAddress - BlockToFix->getAddress(),
                            *TargetSymbol, Addend);
      }
    }
    return Error::success();
  }

  /// Return the string name of the given MachO arm64 edge kind.
  const char *getMachOARM64RelocationKindName(Edge::Kind R) {
    switch (R) {
    case MachOBranch26:
      return "MachOBranch26";
    case MachOPointer64:
      return "MachOPointer64";
    case MachOPointer64Anon:
      return "MachOPointer64Anon";
    case MachOPage21:
      return "MachOPage21";
    case MachOPageOffset12:
      return "MachOPageOffset12";
    case MachOGOTPage21:
      return "MachOGOTPage21";
    case MachOGOTPageOffset12:
      return "MachOGOTPageOffset12";
    case MachOTLVPage21:
      return "MachOTLVPage21";
    case MachOTLVPageOffset12:
      return "MachOTLVPageOffset12";
    case MachOPointerToGOT:
      return "MachOPointerToGOT";
    case MachOPairedAddend:
      return "MachOPairedAddend";
    case MachOLDRLiteral19:
      return "MachOLDRLiteral19";
    case MachODelta32:
      return "MachODelta32";
    case MachODelta64:
      return "MachODelta64";
    case MachONegDelta32:
      return "MachONegDelta32";
    case MachONegDelta64:
      return "MachONegDelta64";
    default:
      return getGenericEdgeKindName(static_cast<Edge::Kind>(R));
    }
  }

  unsigned NumSymbols = 0;
};

} // namespace

namespace llvm {
namespace jitlink {

Error buildTables_MachO_arm64(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Visiting edges in graph:\n");

  aarch64::GOTTableManager GOT;
  aarch64::PLTTableManager PLT(GOT);
  visitExistingEdges(G, GOT, PLT);
  return Error::success();
}

class MachOJITLinker_arm64 : public JITLinker<MachOJITLinker_arm64> {
  friend class JITLinker<MachOJITLinker_arm64>;

public:
  MachOJITLinker_arm64(std::unique_ptr<JITLinkContext> Ctx,
                       std::unique_ptr<LinkGraph> G,
                       PassConfiguration PassConfig)
      : JITLinker(std::move(Ctx), std::move(G), std::move(PassConfig)) {}

private:
  Error applyFixup(LinkGraph &G, Block &B, const Edge &E) const {
    return aarch64::applyFixup(G, B, E);
  }

  uint64_t NullValue = 0;
};

Expected<std::unique_ptr<LinkGraph>>
createLinkGraphFromMachOObject_arm64(MemoryBufferRef ObjectBuffer) {
  auto MachOObj = object::ObjectFile::createMachOObjectFile(ObjectBuffer);
  if (!MachOObj)
    return MachOObj.takeError();

  auto Features = (*MachOObj)->getFeatures();
  if (!Features)
    return Features.takeError();

  return MachOLinkGraphBuilder_arm64(**MachOObj, std::move(*Features))
      .buildGraph();
}

void link_MachO_arm64(std::unique_ptr<LinkGraph> G,
                      std::unique_ptr<JITLinkContext> Ctx) {

  PassConfiguration Config;

  if (Ctx->shouldAddDefaultTargetPasses(G->getTargetTriple())) {
    // Add a mark-live pass.
    if (auto MarkLive = Ctx->getMarkLivePass(G->getTargetTriple()))
      Config.PrePrunePasses.push_back(std::move(MarkLive));
    else
      Config.PrePrunePasses.push_back(markAllSymbolsLive);

    // Add compact unwind splitter pass.
    Config.PrePrunePasses.push_back(
        CompactUnwindSplitter("__LD,__compact_unwind"));

    // Add eh-frame passes.
    // FIXME: Prune eh-frames for which compact-unwind is available once
    // we support compact-unwind registration with libunwind.
    Config.PrePrunePasses.push_back(createEHFrameSplitterPass_MachO_arm64());
    Config.PrePrunePasses.push_back(createEHFrameEdgeFixerPass_MachO_arm64());

    // Resolve any external section start / end symbols.
    Config.PostAllocationPasses.push_back(
        createDefineExternalSectionStartAndEndSymbolsPass(
            identifyMachOSectionStartAndEndSymbols));

    // Add an in-place GOT/Stubs pass.
    Config.PostPrunePasses.push_back(buildTables_MachO_arm64);
  }

  if (auto Err = Ctx->modifyPassConfig(*G, Config))
    return Ctx->notifyFailed(std::move(Err));

  // Construct a JITLinker and run the link function.
  MachOJITLinker_arm64::link(std::move(Ctx), std::move(G), std::move(Config));
}

LinkGraphPassFunction createEHFrameSplitterPass_MachO_arm64() {
  return DWARFRecordSectionSplitter("__TEXT,__eh_frame");
}

LinkGraphPassFunction createEHFrameEdgeFixerPass_MachO_arm64() {
  return EHFrameEdgeFixer("__TEXT,__eh_frame", aarch64::PointerSize,
                          aarch64::Pointer32, aarch64::Pointer64,
                          aarch64::Delta32, aarch64::Delta64,
                          aarch64::NegDelta32);
}

} // end namespace jitlink
} // end namespace llvm
