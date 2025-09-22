//===-------- JITLink_EHFrameSupport.cpp - JITLink eh-frame utils ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "EHFrameSupportImpl.h"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/Config/config.h"
#include "llvm/ExecutionEngine/JITLink/DWARFRecordSectionSplitter.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/Support/DynamicLibrary.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {

EHFrameEdgeFixer::EHFrameEdgeFixer(StringRef EHFrameSectionName,
                                   unsigned PointerSize, Edge::Kind Pointer32,
                                   Edge::Kind Pointer64, Edge::Kind Delta32,
                                   Edge::Kind Delta64, Edge::Kind NegDelta32)
    : EHFrameSectionName(EHFrameSectionName), PointerSize(PointerSize),
      Pointer32(Pointer32), Pointer64(Pointer64), Delta32(Delta32),
      Delta64(Delta64), NegDelta32(NegDelta32) {}

Error EHFrameEdgeFixer::operator()(LinkGraph &G) {
  auto *EHFrame = G.findSectionByName(EHFrameSectionName);

  if (!EHFrame) {
    LLVM_DEBUG({
      dbgs() << "EHFrameEdgeFixer: No " << EHFrameSectionName
             << " section in \"" << G.getName() << "\". Nothing to do.\n";
    });
    return Error::success();
  }

  // Check that we support the graph's pointer size.
  if (G.getPointerSize() != 4 && G.getPointerSize() != 8)
    return make_error<JITLinkError>(
        "EHFrameEdgeFixer only supports 32 and 64 bit targets");

  LLVM_DEBUG({
    dbgs() << "EHFrameEdgeFixer: Processing " << EHFrameSectionName << " in \""
           << G.getName() << "\"...\n";
  });

  ParseContext PC(G);

  // Build a map of all blocks and symbols in the text sections. We will use
  // these for finding / building edge targets when processing FDEs.
  for (auto &Sec : G.sections()) {
    // Just record the most-canonical symbol (for eh-frame purposes) at each
    // address.
    for (auto *Sym : Sec.symbols()) {
      auto &CurSym = PC.AddrToSym[Sym->getAddress()];
      if (!CurSym || (std::make_tuple(Sym->getLinkage(), Sym->getScope(),
                                      !Sym->hasName(), Sym->getName()) <
                      std::make_tuple(CurSym->getLinkage(), CurSym->getScope(),
                                      !CurSym->hasName(), CurSym->getName())))
        CurSym = Sym;
    }
    if (auto Err = PC.AddrToBlock.addBlocks(Sec.blocks(),
                                            BlockAddressMap::includeNonNull))
      return Err;
  }

  // Sort eh-frame blocks into address order to ensure we visit CIEs before
  // their child FDEs.
  std::vector<Block *> EHFrameBlocks;
  for (auto *B : EHFrame->blocks())
    EHFrameBlocks.push_back(B);
  llvm::sort(EHFrameBlocks, [](const Block *LHS, const Block *RHS) {
    return LHS->getAddress() < RHS->getAddress();
  });

  // Loop over the blocks in address order.
  for (auto *B : EHFrameBlocks)
    if (auto Err = processBlock(PC, *B))
      return Err;

  return Error::success();
}

static Expected<size_t> readCFIRecordLength(const Block &B,
                                            BinaryStreamReader &R) {
  uint32_t Length;
  if (auto Err = R.readInteger(Length))
    return std::move(Err);

  // If Length < 0xffffffff then use the regular length field, otherwise
  // read the extended length field.
  if (Length != 0xffffffff)
    return Length;

  uint64_t ExtendedLength;
  if (auto Err = R.readInteger(ExtendedLength))
    return std::move(Err);

  if (ExtendedLength > std::numeric_limits<size_t>::max())
    return make_error<JITLinkError>(
        "In CFI record at " +
        formatv("{0:x}", B.getAddress() + R.getOffset() - 12) +
        ", extended length of " + formatv("{0:x}", ExtendedLength) +
        " exceeds address-range max (" +
        formatv("{0:x}", std::numeric_limits<size_t>::max()));

  return ExtendedLength;
}

Error EHFrameEdgeFixer::processBlock(ParseContext &PC, Block &B) {

  LLVM_DEBUG(dbgs() << "  Processing block at " << B.getAddress() << "\n");

  // eh-frame should not contain zero-fill blocks.
  if (B.isZeroFill())
    return make_error<JITLinkError>("Unexpected zero-fill block in " +
                                    EHFrameSectionName + " section");

  if (B.getSize() == 0) {
    LLVM_DEBUG(dbgs() << "    Block is empty. Skipping.\n");
    return Error::success();
  }

  // Find the offsets of any existing edges from this block.
  BlockEdgesInfo BlockEdges;
  for (auto &E : B.edges())
    if (E.isRelocation()) {
      // Check if we already saw more than one relocation at this offset.
      if (BlockEdges.Multiple.contains(E.getOffset()))
        continue;

      // Otherwise check if we previously had exactly one relocation at this
      // offset. If so, we now have a second one and move it from the TargetMap
      // into the Multiple set.
      auto It = BlockEdges.TargetMap.find(E.getOffset());
      if (It != BlockEdges.TargetMap.end()) {
        BlockEdges.TargetMap.erase(It);
        BlockEdges.Multiple.insert(E.getOffset());
      } else {
        BlockEdges.TargetMap[E.getOffset()] = EdgeTarget(E);
      }
    }

  BinaryStreamReader BlockReader(
      StringRef(B.getContent().data(), B.getContent().size()),
      PC.G.getEndianness());

  // Get the record length.
  Expected<size_t> RecordRemaining = readCFIRecordLength(B, BlockReader);
  if (!RecordRemaining)
    return RecordRemaining.takeError();

  // We expect DWARFRecordSectionSplitter to split each CFI record into its own
  // block.
  if (BlockReader.bytesRemaining() != *RecordRemaining)
    return make_error<JITLinkError>("Incomplete CFI record at " +
                                    formatv("{0:x16}", B.getAddress()));

  // Read the CIE delta for this record.
  uint64_t CIEDeltaFieldOffset = BlockReader.getOffset();
  uint32_t CIEDelta;
  if (auto Err = BlockReader.readInteger(CIEDelta))
    return Err;

  if (CIEDelta == 0) {
    if (auto Err = processCIE(PC, B, CIEDeltaFieldOffset, BlockEdges))
      return Err;
  } else {
    if (auto Err = processFDE(PC, B, CIEDeltaFieldOffset, CIEDelta, BlockEdges))
      return Err;
  }

  return Error::success();
}

Error EHFrameEdgeFixer::processCIE(ParseContext &PC, Block &B,
                                   size_t CIEDeltaFieldOffset,
                                   const BlockEdgesInfo &BlockEdges) {

  LLVM_DEBUG(dbgs() << "    Record is CIE\n");

  BinaryStreamReader RecordReader(
      StringRef(B.getContent().data(), B.getContent().size()),
      PC.G.getEndianness());

  // Skip past the CIE delta field: we've already processed this far.
  RecordReader.setOffset(CIEDeltaFieldOffset + 4);

  auto &CIESymbol = PC.G.addAnonymousSymbol(B, 0, B.getSize(), false, false);
  CIEInformation CIEInfo(CIESymbol);

  uint8_t Version = 0;
  if (auto Err = RecordReader.readInteger(Version))
    return Err;

  if (Version != 0x01)
    return make_error<JITLinkError>("Bad CIE version " + Twine(Version) +
                                    " (should be 0x01) in eh-frame");

  auto AugInfo = parseAugmentationString(RecordReader);
  if (!AugInfo)
    return AugInfo.takeError();

  // Skip the EH Data field if present.
  if (AugInfo->EHDataFieldPresent)
    if (auto Err = RecordReader.skip(PC.G.getPointerSize()))
      return Err;

  // Read and validate the code alignment factor.
  {
    uint64_t CodeAlignmentFactor = 0;
    if (auto Err = RecordReader.readULEB128(CodeAlignmentFactor))
      return Err;
  }

  // Read and validate the data alignment factor.
  {
    int64_t DataAlignmentFactor = 0;
    if (auto Err = RecordReader.readSLEB128(DataAlignmentFactor))
      return Err;
  }

  // Skip the return address register field.
  if (auto Err = RecordReader.skip(1))
    return Err;

  if (AugInfo->AugmentationDataPresent) {

    CIEInfo.AugmentationDataPresent = true;

    uint64_t AugmentationDataLength = 0;
    if (auto Err = RecordReader.readULEB128(AugmentationDataLength))
      return Err;

    uint32_t AugmentationDataStartOffset = RecordReader.getOffset();

    uint8_t *NextField = &AugInfo->Fields[0];
    while (uint8_t Field = *NextField++) {
      switch (Field) {
      case 'L':
        CIEInfo.LSDAPresent = true;
        if (auto PE = readPointerEncoding(RecordReader, B, "LSDA"))
          CIEInfo.LSDAEncoding = *PE;
        else
          return PE.takeError();
        break;
      case 'P': {
        auto PersonalityPointerEncoding =
            readPointerEncoding(RecordReader, B, "personality");
        if (!PersonalityPointerEncoding)
          return PersonalityPointerEncoding.takeError();
        if (auto Err =
                getOrCreateEncodedPointerEdge(
                    PC, BlockEdges, *PersonalityPointerEncoding, RecordReader,
                    B, RecordReader.getOffset(), "personality")
                    .takeError())
          return Err;
        break;
      }
      case 'R':
        if (auto PE = readPointerEncoding(RecordReader, B, "address")) {
          CIEInfo.AddressEncoding = *PE;
          if (CIEInfo.AddressEncoding == dwarf::DW_EH_PE_omit)
            return make_error<JITLinkError>(
                "Invalid address encoding DW_EH_PE_omit in CIE at " +
                formatv("{0:x}", B.getAddress().getValue()));
        } else
          return PE.takeError();
        break;
      default:
        llvm_unreachable("Invalid augmentation string field");
      }
    }

    if (RecordReader.getOffset() - AugmentationDataStartOffset >
        AugmentationDataLength)
      return make_error<JITLinkError>("Read past the end of the augmentation "
                                      "data while parsing fields");
  }

  assert(!PC.CIEInfos.count(CIESymbol.getAddress()) &&
         "Multiple CIEs recorded at the same address?");
  PC.CIEInfos[CIESymbol.getAddress()] = std::move(CIEInfo);

  return Error::success();
}

Error EHFrameEdgeFixer::processFDE(ParseContext &PC, Block &B,
                                   size_t CIEDeltaFieldOffset,
                                   uint32_t CIEDelta,
                                   const BlockEdgesInfo &BlockEdges) {
  LLVM_DEBUG(dbgs() << "    Record is FDE\n");

  orc::ExecutorAddr RecordAddress = B.getAddress();

  BinaryStreamReader RecordReader(
      StringRef(B.getContent().data(), B.getContent().size()),
      PC.G.getEndianness());

  // Skip past the CIE delta field: we've already read this far.
  RecordReader.setOffset(CIEDeltaFieldOffset + 4);

  auto &FDESymbol = PC.G.addAnonymousSymbol(B, 0, B.getSize(), false, false);

  CIEInformation *CIEInfo = nullptr;

  {
    // Process the CIE pointer field.
    if (BlockEdges.Multiple.contains(CIEDeltaFieldOffset))
      return make_error<JITLinkError>(
          "CIE pointer field already has multiple edges at " +
          formatv("{0:x16}", RecordAddress + CIEDeltaFieldOffset));

    auto CIEEdgeItr = BlockEdges.TargetMap.find(CIEDeltaFieldOffset);

    orc::ExecutorAddr CIEAddress =
        RecordAddress + orc::ExecutorAddrDiff(CIEDeltaFieldOffset) -
        orc::ExecutorAddrDiff(CIEDelta);
    if (CIEEdgeItr == BlockEdges.TargetMap.end()) {
      LLVM_DEBUG({
        dbgs() << "        Adding edge at "
               << (RecordAddress + CIEDeltaFieldOffset)
               << " to CIE at: " << CIEAddress << "\n";
      });
      if (auto CIEInfoOrErr = PC.findCIEInfo(CIEAddress))
        CIEInfo = *CIEInfoOrErr;
      else
        return CIEInfoOrErr.takeError();
      assert(CIEInfo->CIESymbol && "CIEInfo has no CIE symbol set");
      B.addEdge(NegDelta32, CIEDeltaFieldOffset, *CIEInfo->CIESymbol, 0);
    } else {
      LLVM_DEBUG({
        dbgs() << "        Already has edge at "
               << (RecordAddress + CIEDeltaFieldOffset) << " to CIE at "
               << CIEAddress << "\n";
      });
      auto &EI = CIEEdgeItr->second;
      if (EI.Addend)
        return make_error<JITLinkError>(
            "CIE edge at " +
            formatv("{0:x16}", RecordAddress + CIEDeltaFieldOffset) +
            " has non-zero addend");
      if (auto CIEInfoOrErr = PC.findCIEInfo(EI.Target->getAddress()))
        CIEInfo = *CIEInfoOrErr;
      else
        return CIEInfoOrErr.takeError();
    }
  }

  // Process the PC-Begin field.
  LLVM_DEBUG({
    dbgs() << "      Processing PC-begin at "
           << (RecordAddress + RecordReader.getOffset()) << "\n";
  });
  if (auto PCBegin = getOrCreateEncodedPointerEdge(
          PC, BlockEdges, CIEInfo->AddressEncoding, RecordReader, B,
          RecordReader.getOffset(), "PC begin")) {
    assert(*PCBegin && "PC-begin symbol not set");
    if ((*PCBegin)->isDefined()) {
      // Add a keep-alive edge from the FDE target to the FDE to ensure that the
      // FDE is kept alive if its target is.
      LLVM_DEBUG({
        dbgs() << "      Adding keep-alive edge from target at "
               << (*PCBegin)->getBlock().getAddress() << " to FDE at "
               << RecordAddress << "\n";
      });
      (*PCBegin)->getBlock().addEdge(Edge::KeepAlive, 0, FDESymbol, 0);
    } else {
      LLVM_DEBUG({
        dbgs() << "      WARNING: Not adding keep-alive edge to FDE at "
               << RecordAddress << ", which points to "
               << ((*PCBegin)->isExternal() ? "external" : "absolute")
               << " symbol \"" << (*PCBegin)->getName()
               << "\" -- FDE must be kept alive manually or it will be "
               << "dead stripped.\n";
      });
    }
  } else
    return PCBegin.takeError();

  // Skip over the PC range size field.
  if (auto Err = skipEncodedPointer(CIEInfo->AddressEncoding, RecordReader))
    return Err;

  if (CIEInfo->AugmentationDataPresent) {
    uint64_t AugmentationDataSize;
    if (auto Err = RecordReader.readULEB128(AugmentationDataSize))
      return Err;

    if (CIEInfo->LSDAPresent)
      if (auto Err = getOrCreateEncodedPointerEdge(
                         PC, BlockEdges, CIEInfo->LSDAEncoding, RecordReader, B,
                         RecordReader.getOffset(), "LSDA")
                         .takeError())
        return Err;
  } else {
    LLVM_DEBUG(dbgs() << "      Record does not have LSDA field.\n");
  }

  return Error::success();
}

Expected<EHFrameEdgeFixer::AugmentationInfo>
EHFrameEdgeFixer::parseAugmentationString(BinaryStreamReader &RecordReader) {
  AugmentationInfo AugInfo;
  uint8_t NextChar;
  uint8_t *NextField = &AugInfo.Fields[0];

  if (auto Err = RecordReader.readInteger(NextChar))
    return std::move(Err);

  while (NextChar != 0) {
    switch (NextChar) {
    case 'z':
      AugInfo.AugmentationDataPresent = true;
      break;
    case 'e':
      if (auto Err = RecordReader.readInteger(NextChar))
        return std::move(Err);
      if (NextChar != 'h')
        return make_error<JITLinkError>("Unrecognized substring e" +
                                        Twine(NextChar) +
                                        " in augmentation string");
      AugInfo.EHDataFieldPresent = true;
      break;
    case 'L':
    case 'P':
    case 'R':
      *NextField++ = NextChar;
      break;
    default:
      return make_error<JITLinkError>("Unrecognized character " +
                                      Twine(NextChar) +
                                      " in augmentation string");
    }

    if (auto Err = RecordReader.readInteger(NextChar))
      return std::move(Err);
  }

  return std::move(AugInfo);
}

Expected<uint8_t> EHFrameEdgeFixer::readPointerEncoding(BinaryStreamReader &R,
                                                        Block &InBlock,
                                                        const char *FieldName) {
  using namespace dwarf;

  uint8_t PointerEncoding;
  if (auto Err = R.readInteger(PointerEncoding))
    return std::move(Err);

  bool Supported = true;
  switch (PointerEncoding & 0xf) {
  case DW_EH_PE_uleb128:
  case DW_EH_PE_udata2:
  case DW_EH_PE_sleb128:
  case DW_EH_PE_sdata2:
    Supported = false;
    break;
  }
  if (Supported) {
    switch (PointerEncoding & 0x70) {
    case DW_EH_PE_textrel:
    case DW_EH_PE_datarel:
    case DW_EH_PE_funcrel:
    case DW_EH_PE_aligned:
      Supported = false;
      break;
    }
  }

  if (Supported)
    return PointerEncoding;

  return make_error<JITLinkError>("Unsupported pointer encoding " +
                                  formatv("{0:x2}", PointerEncoding) + " for " +
                                  FieldName + "in CFI record at " +
                                  formatv("{0:x16}", InBlock.getAddress()));
}

Error EHFrameEdgeFixer::skipEncodedPointer(uint8_t PointerEncoding,
                                           BinaryStreamReader &RecordReader) {
  using namespace dwarf;

  // Switch absptr to corresponding udata encoding.
  if ((PointerEncoding & 0xf) == DW_EH_PE_absptr)
    PointerEncoding |= (PointerSize == 8) ? DW_EH_PE_udata8 : DW_EH_PE_udata4;

  switch (PointerEncoding & 0xf) {
  case DW_EH_PE_udata4:
  case DW_EH_PE_sdata4:
    if (auto Err = RecordReader.skip(4))
      return Err;
    break;
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    if (auto Err = RecordReader.skip(8))
      return Err;
    break;
  default:
    llvm_unreachable("Unrecognized encoding");
  }
  return Error::success();
}

Expected<Symbol *> EHFrameEdgeFixer::getOrCreateEncodedPointerEdge(
    ParseContext &PC, const BlockEdgesInfo &BlockEdges, uint8_t PointerEncoding,
    BinaryStreamReader &RecordReader, Block &BlockToFix,
    size_t PointerFieldOffset, const char *FieldName) {
  using namespace dwarf;

  if (PointerEncoding == DW_EH_PE_omit)
    return nullptr;

  // If there's already an edge here then just skip the encoded pointer and
  // return the edge's target.
  {
    auto EdgeI = BlockEdges.TargetMap.find(PointerFieldOffset);
    if (EdgeI != BlockEdges.TargetMap.end()) {
      LLVM_DEBUG({
        dbgs() << "      Existing edge at "
               << (BlockToFix.getAddress() + PointerFieldOffset) << " to "
               << FieldName << " at " << EdgeI->second.Target->getAddress();
        if (EdgeI->second.Target->hasName())
          dbgs() << " (" << EdgeI->second.Target->getName() << ")";
        dbgs() << "\n";
      });
      if (auto Err = skipEncodedPointer(PointerEncoding, RecordReader))
        return std::move(Err);
      return EdgeI->second.Target;
    }

    if (BlockEdges.Multiple.contains(PointerFieldOffset))
      return make_error<JITLinkError>("Multiple relocations at offset " +
                                      formatv("{0:x16}", PointerFieldOffset));
  }

  // Switch absptr to corresponding udata encoding.
  if ((PointerEncoding & 0xf) == DW_EH_PE_absptr)
    PointerEncoding |= (PointerSize == 8) ? DW_EH_PE_udata8 : DW_EH_PE_udata4;

  // We need to create an edge. Start by reading the field value.
  uint64_t FieldValue;
  bool Is64Bit = false;
  switch (PointerEncoding & 0xf) {
  case DW_EH_PE_udata4: {
    uint32_t Val;
    if (auto Err = RecordReader.readInteger(Val))
      return std::move(Err);
    FieldValue = Val;
    break;
  }
  case DW_EH_PE_sdata4: {
    uint32_t Val;
    if (auto Err = RecordReader.readInteger(Val))
      return std::move(Err);
    FieldValue = Val;
    break;
  }
  case DW_EH_PE_udata8:
  case DW_EH_PE_sdata8:
    Is64Bit = true;
    if (auto Err = RecordReader.readInteger(FieldValue))
      return std::move(Err);
    break;
  default:
    llvm_unreachable("Unsupported encoding");
  }

  // Find the edge target and edge kind to use.
  orc::ExecutorAddr Target;
  Edge::Kind PtrEdgeKind = Edge::Invalid;
  if ((PointerEncoding & 0x70) == DW_EH_PE_pcrel) {
    Target = BlockToFix.getAddress() + PointerFieldOffset;
    PtrEdgeKind = Is64Bit ? Delta64 : Delta32;
  } else
    PtrEdgeKind = Is64Bit ? Pointer64 : Pointer32;
  Target += FieldValue;

  // Find or create a symbol to point the edge at.
  auto TargetSym = getOrCreateSymbol(PC, Target);
  if (!TargetSym)
    return TargetSym.takeError();
  BlockToFix.addEdge(PtrEdgeKind, PointerFieldOffset, *TargetSym, 0);

  LLVM_DEBUG({
    dbgs() << "      Adding edge at "
           << (BlockToFix.getAddress() + PointerFieldOffset) << " to "
           << FieldName << " at " << TargetSym->getAddress();
    if (TargetSym->hasName())
      dbgs() << " (" << TargetSym->getName() << ")";
    dbgs() << "\n";
  });

  return &*TargetSym;
}

Expected<Symbol &> EHFrameEdgeFixer::getOrCreateSymbol(ParseContext &PC,
                                                       orc::ExecutorAddr Addr) {
  // See whether we have a canonical symbol for the given address already.
  auto CanonicalSymI = PC.AddrToSym.find(Addr);
  if (CanonicalSymI != PC.AddrToSym.end())
    return *CanonicalSymI->second;

  // Otherwise search for a block covering the address and create a new symbol.
  auto *B = PC.AddrToBlock.getBlockCovering(Addr);
  if (!B)
    return make_error<JITLinkError>("No symbol or block covering address " +
                                    formatv("{0:x16}", Addr));

  auto &S =
      PC.G.addAnonymousSymbol(*B, Addr - B->getAddress(), 0, false, false);
  PC.AddrToSym[S.getAddress()] = &S;
  return S;
}

char EHFrameNullTerminator::NullTerminatorBlockContent[4] = {0, 0, 0, 0};

EHFrameNullTerminator::EHFrameNullTerminator(StringRef EHFrameSectionName)
    : EHFrameSectionName(EHFrameSectionName) {}

Error EHFrameNullTerminator::operator()(LinkGraph &G) {
  auto *EHFrame = G.findSectionByName(EHFrameSectionName);

  if (!EHFrame)
    return Error::success();

  LLVM_DEBUG({
    dbgs() << "EHFrameNullTerminator adding null terminator to "
           << EHFrameSectionName << "\n";
  });

  auto &NullTerminatorBlock =
      G.createContentBlock(*EHFrame, NullTerminatorBlockContent,
                           orc::ExecutorAddr(~uint64_t(4)), 1, 0);
  G.addAnonymousSymbol(NullTerminatorBlock, 0, 4, false, true);
  return Error::success();
}

EHFrameRegistrar::~EHFrameRegistrar() = default;

Error InProcessEHFrameRegistrar::registerEHFrames(
    orc::ExecutorAddrRange EHFrameSection) {
  return orc::registerEHFrameSection(EHFrameSection.Start.toPtr<void *>(),
                                     EHFrameSection.size());
}

Error InProcessEHFrameRegistrar::deregisterEHFrames(
    orc::ExecutorAddrRange EHFrameSection) {
  return orc::deregisterEHFrameSection(EHFrameSection.Start.toPtr<void *>(),
                                       EHFrameSection.size());
}

EHFrameCFIBlockInspector EHFrameCFIBlockInspector::FromEdgeScan(Block &B) {
  if (B.edges_empty())
    return EHFrameCFIBlockInspector(nullptr);
  if (B.edges_size() == 1)
    return EHFrameCFIBlockInspector(&*B.edges().begin());
  SmallVector<Edge *, 3> Es;
  for (auto &E : B.edges())
    Es.push_back(&E);
  assert(Es.size() >= 2 && Es.size() <= 3 && "Unexpected number of edges");
  llvm::sort(Es, [](const Edge *LHS, const Edge *RHS) {
    return LHS->getOffset() < RHS->getOffset();
  });
  return EHFrameCFIBlockInspector(*Es[0], *Es[1],
                                  Es.size() == 3 ? Es[2] : nullptr);
  return EHFrameCFIBlockInspector(nullptr);
}

EHFrameCFIBlockInspector::EHFrameCFIBlockInspector(Edge *PersonalityEdge)
    : PersonalityEdge(PersonalityEdge) {}

EHFrameCFIBlockInspector::EHFrameCFIBlockInspector(Edge &CIEEdge,
                                                   Edge &PCBeginEdge,
                                                   Edge *LSDAEdge)
    : CIEEdge(&CIEEdge), PCBeginEdge(&PCBeginEdge), LSDAEdge(LSDAEdge) {}

LinkGraphPassFunction
createEHFrameRecorderPass(const Triple &TT,
                          StoreFrameRangeFunction StoreRangeAddress) {
  const char *EHFrameSectionName = nullptr;
  if (TT.getObjectFormat() == Triple::MachO)
    EHFrameSectionName = "__TEXT,__eh_frame";
  else
    EHFrameSectionName = ".eh_frame";

  auto RecordEHFrame =
      [EHFrameSectionName,
       StoreFrameRange = std::move(StoreRangeAddress)](LinkGraph &G) -> Error {
    // Search for a non-empty eh-frame and record the address of the first
    // symbol in it.
    orc::ExecutorAddr Addr;
    size_t Size = 0;
    if (auto *S = G.findSectionByName(EHFrameSectionName)) {
      auto R = SectionRange(*S);
      Addr = R.getStart();
      Size = R.getSize();
    }
    if (!Addr && Size != 0)
      return make_error<JITLinkError>(
          StringRef(EHFrameSectionName) +
          " section can not have zero address with non-zero size");
    StoreFrameRange(Addr, Size);
    return Error::success();
  };

  return RecordEHFrame;
}

} // end namespace jitlink
} // end namespace llvm
