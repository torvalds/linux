//===------- EHFrameSupportImpl.h - JITLink eh-frame utils ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// EHFrame registration support for JITLink.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORTIMPL_H
#define LLVM_LIB_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORTIMPL_H

#include "llvm/ExecutionEngine/JITLink/EHFrameSupport.h"

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/Support/BinaryStreamReader.h"

namespace llvm {
namespace jitlink {

/// A LinkGraph pass that adds missing FDE-to-CIE, FDE-to-PC and FDE-to-LSDA
/// edges.
class EHFrameEdgeFixer {
public:
  /// Create an eh-frame edge fixer.
  /// If a given edge-kind is not supported on the target architecture then
  /// Edge::Invalid should be used.
  EHFrameEdgeFixer(StringRef EHFrameSectionName, unsigned PointerSize,
                   Edge::Kind Pointer32, Edge::Kind Pointer64,
                   Edge::Kind Delta32, Edge::Kind Delta64,
                   Edge::Kind NegDelta32);
  Error operator()(LinkGraph &G);

private:

  struct AugmentationInfo {
    bool AugmentationDataPresent = false;
    bool EHDataFieldPresent = false;
    uint8_t Fields[4] = {0x0, 0x0, 0x0, 0x0};
  };

  struct CIEInformation {
    CIEInformation() = default;
    CIEInformation(Symbol &CIESymbol) : CIESymbol(&CIESymbol) {}
    Symbol *CIESymbol = nullptr;
    bool AugmentationDataPresent = false;
    bool LSDAPresent = false;
    uint8_t LSDAEncoding = 0;
    uint8_t AddressEncoding = 0;
  };

  struct EdgeTarget {
    EdgeTarget() = default;
    EdgeTarget(const Edge &E) : Target(&E.getTarget()), Addend(E.getAddend()) {}

    Symbol *Target = nullptr;
    Edge::AddendT Addend = 0;
  };

  struct BlockEdgesInfo {
    DenseMap<Edge::OffsetT, EdgeTarget> TargetMap;
    DenseSet<Edge::OffsetT> Multiple;
  };

  using CIEInfosMap = DenseMap<orc::ExecutorAddr, CIEInformation>;

  struct ParseContext {
    ParseContext(LinkGraph &G) : G(G) {}

    Expected<CIEInformation *> findCIEInfo(orc::ExecutorAddr Address) {
      auto I = CIEInfos.find(Address);
      if (I == CIEInfos.end())
        return make_error<JITLinkError>("No CIE found at address " +
                                        formatv("{0:x16}", Address));
      return &I->second;
    }

    LinkGraph &G;
    CIEInfosMap CIEInfos;
    BlockAddressMap AddrToBlock;
    DenseMap<orc::ExecutorAddr, Symbol *> AddrToSym;
  };

  Error processBlock(ParseContext &PC, Block &B);
  Error processCIE(ParseContext &PC, Block &B, size_t CIEDeltaFieldOffset,
                   const BlockEdgesInfo &BlockEdges);
  Error processFDE(ParseContext &PC, Block &B, size_t CIEDeltaFieldOffset,
                   uint32_t CIEDelta, const BlockEdgesInfo &BlockEdges);

  Expected<AugmentationInfo>
  parseAugmentationString(BinaryStreamReader &RecordReader);

  Expected<uint8_t> readPointerEncoding(BinaryStreamReader &RecordReader,
                                        Block &InBlock, const char *FieldName);
  Error skipEncodedPointer(uint8_t PointerEncoding,
                           BinaryStreamReader &RecordReader);
  Expected<Symbol *> getOrCreateEncodedPointerEdge(
      ParseContext &PC, const BlockEdgesInfo &BlockEdges,
      uint8_t PointerEncoding, BinaryStreamReader &RecordReader,
      Block &BlockToFix, size_t PointerFieldOffset, const char *FieldName);

  Expected<Symbol &> getOrCreateSymbol(ParseContext &PC,
                                       orc::ExecutorAddr Addr);

  StringRef EHFrameSectionName;
  unsigned PointerSize;
  Edge::Kind Pointer32;
  Edge::Kind Pointer64;
  Edge::Kind Delta32;
  Edge::Kind Delta64;
  Edge::Kind NegDelta32;
};

/// Add a 32-bit null-terminator to the end of the eh-frame section.
class EHFrameNullTerminator {
public:
  EHFrameNullTerminator(StringRef EHFrameSectionName);
  Error operator()(LinkGraph &G);

private:
  static char NullTerminatorBlockContent[];
  StringRef EHFrameSectionName;
};

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_LIB_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORTIMPL_H
