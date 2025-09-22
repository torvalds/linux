//===--------- EHFrameSupport.h - JITLink eh-frame utils --------*- C++ -*-===//
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

#ifndef LLVM_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORT_H
#define LLVM_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORT_H

#include "llvm/ExecutionEngine/JITLink/JITLink.h"
#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace jitlink {

/// Inspect an eh-frame CFI record.
class EHFrameCFIBlockInspector {
public:
  /// Identify CFI record type and edges based on number and order of edges
  /// in the given block only. This assumes that the block contains one CFI
  /// record that has already been split out and fixed by the
  /// DWARFRecordSplitter and EHFrameEdgeFixer passes.
  ///
  /// Zero or one outgoing edges: Record is CIE. If present, edge points to
  /// personality.
  ///
  /// Two or three outgoing edges: Record is an FDE. First edge points to CIE,
  /// second to PC-begin, third (if present) to LSDA.
  ///
  /// It is illegal to call this function on a block with four or more edges.
  static EHFrameCFIBlockInspector FromEdgeScan(Block &B);

  /// Returns true if this frame is an FDE, false for a CIE.
  bool isFDE() const { return CIEEdge != nullptr; }

  /// Returns true if this frame is a CIE, false for an FDE.
  bool isCIE() const { return CIEEdge == nullptr; }

  /// If this is a CIE record, returns the Edge pointing at the personality
  /// function, if any.
  /// It is illegal to call this method on FDE records.
  Edge *getPersonalityEdge() const {
    assert(isCIE() && "CFI record is not a CIE");
    return PersonalityEdge;
  }

  /// If this is an FDE record, returns the Edge pointing to the CIE.
  /// If this is a CIE record, returns null.
  ///
  /// The result is not valid if any modification has been made to the block
  /// after parsing.
  Edge *getCIEEdge() const { return CIEEdge; }

  /// If this is an FDE record, returns the Edge pointing at the PC-begin
  /// symbol.
  /// If this a CIE record, returns null.
  Edge *getPCBeginEdge() const { return PCBeginEdge; }

  /// If this is an FDE record, returns the Edge pointing at the LSDA, if any.
  /// It is illegal to call this method on CIE records.
  Edge *getLSDAEdge() const {
    assert(isFDE() && "CFI record is not an FDE");
    return LSDAEdge;
  }

private:
  EHFrameCFIBlockInspector(Edge *PersonalityEdge);
  EHFrameCFIBlockInspector(Edge &CIEEdge, Edge &PCBeginEdge, Edge *LSDAEdge);

  Edge *CIEEdge = nullptr;
  Edge *PCBeginEdge = nullptr;
  union {
    Edge *PersonalityEdge;
    Edge *LSDAEdge;
  };
};

/// Supports registration/deregistration of EH-frames in a target process.
class EHFrameRegistrar {
public:
  virtual ~EHFrameRegistrar();
  virtual Error registerEHFrames(orc::ExecutorAddrRange EHFrameSection) = 0;
  virtual Error deregisterEHFrames(orc::ExecutorAddrRange EHFrameSection) = 0;
};

/// Registers / Deregisters EH-frames in the current process.
class InProcessEHFrameRegistrar final : public EHFrameRegistrar {
public:
  Error registerEHFrames(orc::ExecutorAddrRange EHFrameSection) override;

  Error deregisterEHFrames(orc::ExecutorAddrRange EHFrameSection) override;
};

using StoreFrameRangeFunction = std::function<void(
    orc::ExecutorAddr EHFrameSectionAddr, size_t EHFrameSectionSize)>;

/// Creates a pass that records the address and size of the EH frame section.
/// If no eh-frame section is found then the address and size will both be given
/// as zero.
///
/// Authors of JITLinkContexts can use this function to register a post-fixup
/// pass that records the range of the eh-frame section. This range can
/// be used after finalization to register and deregister the frame.
LinkGraphPassFunction
createEHFrameRecorderPass(const Triple &TT,
                          StoreFrameRangeFunction StoreFrameRange);

} // end namespace jitlink
} // end namespace llvm

#endif // LLVM_EXECUTIONENGINE_JITLINK_EHFRAMESUPPORT_H
