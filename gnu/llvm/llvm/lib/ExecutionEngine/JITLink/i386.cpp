//===---- i386.cpp - Generic JITLink i386 edge kinds, utilities -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing i386 objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/i386.h"

#define DEBUG_TYPE "jitlink"

namespace llvm::jitlink::i386 {

const char *getEdgeKindName(Edge::Kind K) {
  switch (K) {
  case None:
    return "None";
  case Pointer32:
    return "Pointer32";
  case PCRel32:
    return "PCRel32";
  case Pointer16:
    return "Pointer16";
  case PCRel16:
    return "PCRel16";
  case Delta32:
    return "Delta32";
  case Delta32FromGOT:
    return "Delta32FromGOT";
  case RequestGOTAndTransformToDelta32FromGOT:
    return "RequestGOTAndTransformToDelta32FromGOT";
  case BranchPCRel32:
    return "BranchPCRel32";
  case BranchPCRel32ToPtrJumpStub:
    return "BranchPCRel32ToPtrJumpStub";
  case BranchPCRel32ToPtrJumpStubBypassable:
    return "BranchPCRel32ToPtrJumpStubBypassable";
  }

  return getGenericEdgeKindName(K);
}

const char NullPointerContent[PointerSize] = {0x00, 0x00, 0x00, 0x00};

const char PointerJumpStubContent[6] = {
    static_cast<char>(0xFFu), 0x25, 0x00, 0x00, 0x00, 0x00};

Error optimizeGOTAndStubAccesses(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Optimizing GOT entries and stubs:\n");

  for (auto *B : G.blocks())
    for (auto &E : B->edges()) {
      if (E.getKind() == i386::BranchPCRel32ToPtrJumpStubBypassable) {
        auto &StubBlock = E.getTarget().getBlock();
        assert(StubBlock.getSize() == sizeof(PointerJumpStubContent) &&
               "Stub block should be stub sized");
        assert(StubBlock.edges_size() == 1 &&
               "Stub block should only have one outgoing edge");

        auto &GOTBlock = StubBlock.edges().begin()->getTarget().getBlock();
        assert(GOTBlock.getSize() == G.getPointerSize() &&
               "GOT block should be pointer sized");
        assert(GOTBlock.edges_size() == 1 &&
               "GOT block should only have one outgoing edge");

        auto &GOTTarget = GOTBlock.edges().begin()->getTarget();
        orc::ExecutorAddr EdgeAddr = B->getAddress() + E.getOffset();
        orc::ExecutorAddr TargetAddr = GOTTarget.getAddress();

        int64_t Displacement = TargetAddr - EdgeAddr + 4;
        if (isInt<32>(Displacement)) {
          E.setKind(i386::BranchPCRel32);
          E.setTarget(GOTTarget);
          LLVM_DEBUG({
            dbgs() << "  Replaced stub branch with direct branch:\n    ";
            printEdge(dbgs(), *B, E, getEdgeKindName(E.getKind()));
            dbgs() << "\n";
          });
        }
      }
    }

  return Error::success();
}

} // namespace llvm::jitlink::i386
