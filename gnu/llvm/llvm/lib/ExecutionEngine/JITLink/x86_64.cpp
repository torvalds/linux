//===----- x86_64.cpp - Generic JITLink x86-64 edge kinds, utilities ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic utilities for graphs representing x86-64 objects.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/JITLink/x86_64.h"

#define DEBUG_TYPE "jitlink"

namespace llvm {
namespace jitlink {
namespace x86_64 {

const char *getEdgeKindName(Edge::Kind K) {
  switch (K) {
  case Pointer64:
    return "Pointer64";
  case Pointer32:
    return "Pointer32";
  case Pointer32Signed:
    return "Pointer32Signed";
  case Pointer16:
    return "Pointer16";
  case Pointer8:
    return "Pointer8";
  case Delta64:
    return "Delta64";
  case Delta32:
    return "Delta32";
  case Delta8:
    return "Delta8";
  case NegDelta64:
    return "NegDelta64";
  case NegDelta32:
    return "NegDelta32";
  case Delta64FromGOT:
    return "Delta64FromGOT";
  case PCRel32:
    return "PCRel32";
  case BranchPCRel32:
    return "BranchPCRel32";
  case BranchPCRel32ToPtrJumpStub:
    return "BranchPCRel32ToPtrJumpStub";
  case BranchPCRel32ToPtrJumpStubBypassable:
    return "BranchPCRel32ToPtrJumpStubBypassable";
  case RequestGOTAndTransformToDelta32:
    return "RequestGOTAndTransformToDelta32";
  case RequestGOTAndTransformToDelta64:
    return "RequestGOTAndTransformToDelta64";
  case RequestGOTAndTransformToDelta64FromGOT:
    return "RequestGOTAndTransformToDelta64FromGOT";
  case PCRel32GOTLoadREXRelaxable:
    return "PCRel32GOTLoadREXRelaxable";
  case RequestGOTAndTransformToPCRel32GOTLoadREXRelaxable:
    return "RequestGOTAndTransformToPCRel32GOTLoadREXRelaxable";
  case PCRel32GOTLoadRelaxable:
    return "PCRel32GOTLoadRelaxable";
  case RequestGOTAndTransformToPCRel32GOTLoadRelaxable:
    return "RequestGOTAndTransformToPCRel32GOTLoadRelaxable";
  case PCRel32TLVPLoadREXRelaxable:
    return "PCRel32TLVPLoadREXRelaxable";
  case RequestTLVPAndTransformToPCRel32TLVPLoadREXRelaxable:
    return "RequestTLVPAndTransformToPCRel32TLVPLoadREXRelaxable";
  default:
    return getGenericEdgeKindName(static_cast<Edge::Kind>(K));
  }
}

const char NullPointerContent[PointerSize] = {0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00};

const char PointerJumpStubContent[6] = {
    static_cast<char>(0xFFu), 0x25, 0x00, 0x00, 0x00, 0x00};

Error optimizeGOTAndStubAccesses(LinkGraph &G) {
  LLVM_DEBUG(dbgs() << "Optimizing GOT entries and stubs:\n");

  for (auto *B : G.blocks())
    for (auto &E : B->edges()) {
      if (E.getKind() == x86_64::PCRel32GOTLoadRelaxable ||
          E.getKind() == x86_64::PCRel32GOTLoadREXRelaxable) {
#ifndef NDEBUG
        bool REXPrefix = E.getKind() == x86_64::PCRel32GOTLoadREXRelaxable;
        assert(E.getOffset() >= (REXPrefix ? 3u : 2u) &&
               "GOT edge occurs too early in block");
#endif
        auto *FixupData = reinterpret_cast<uint8_t *>(
                              const_cast<char *>(B->getContent().data())) +
                          E.getOffset();
        const uint8_t Op = FixupData[-2];
        const uint8_t ModRM = FixupData[-1];

        auto &GOTEntryBlock = E.getTarget().getBlock();
        assert(GOTEntryBlock.getSize() == G.getPointerSize() &&
               "GOT entry block should be pointer sized");
        assert(GOTEntryBlock.edges_size() == 1 &&
               "GOT entry should only have one outgoing edge");
        auto &GOTTarget = GOTEntryBlock.edges().begin()->getTarget();
        orc::ExecutorAddr TargetAddr = GOTTarget.getAddress();
        orc::ExecutorAddr EdgeAddr = B->getFixupAddress(E);
        int64_t Displacement = TargetAddr - EdgeAddr + 4;
        bool TargetInRangeForImmU32 = isUInt<32>(TargetAddr.getValue());
        bool DisplacementInRangeForImmS32 = isInt<32>(Displacement);

        // If both of the Target and displacement is out of range, then
        // there isn't optimization chance.
        if (!(TargetInRangeForImmU32 || DisplacementInRangeForImmS32))
          continue;

        // Transform "mov foo@GOTPCREL(%rip),%reg" to "lea foo(%rip),%reg".
        if (Op == 0x8b && DisplacementInRangeForImmS32) {
          FixupData[-2] = 0x8d;
          E.setKind(x86_64::Delta32);
          E.setTarget(GOTTarget);
          E.setAddend(E.getAddend() - 4);
          LLVM_DEBUG({
            dbgs() << "  Replaced GOT load wih LEA:\n    ";
            printEdge(dbgs(), *B, E, getEdgeKindName(E.getKind()));
            dbgs() << "\n";
          });
          continue;
        }

        // Transform call/jmp instructions
        if (Op == 0xff && TargetInRangeForImmU32) {
          if (ModRM == 0x15) {
            // ABI says we can convert "call *foo@GOTPCREL(%rip)" to "nop; call
            // foo" But lld convert it to "addr32 call foo, because that makes
            // result expression to be a single instruction.
            FixupData[-2] = 0x67;
            FixupData[-1] = 0xe8;
            LLVM_DEBUG({
              dbgs() << "  replaced call instruction's memory operand wih imm "
                        "operand:\n    ";
              printEdge(dbgs(), *B, E, getEdgeKindName(E.getKind()));
              dbgs() << "\n";
            });
          } else {
            // Transform "jmp *foo@GOTPCREL(%rip)" to "jmp foo; nop"
            assert(ModRM == 0x25 && "Invalid ModRm for call/jmp instructions");
            FixupData[-2] = 0xe9;
            FixupData[3] = 0x90;
            E.setOffset(E.getOffset() - 1);
            LLVM_DEBUG({
              dbgs() << "  replaced jmp instruction's memory operand wih imm "
                        "operand:\n    ";
              printEdge(dbgs(), *B, E, getEdgeKindName(E.getKind()));
              dbgs() << "\n";
            });
          }
          E.setKind(x86_64::Pointer32);
          E.setTarget(GOTTarget);
          continue;
        }
      } else if (E.getKind() == x86_64::BranchPCRel32ToPtrJumpStubBypassable) {
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
          E.setKind(x86_64::BranchPCRel32);
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

} // end namespace x86_64
} // end namespace jitlink
} // end namespace llvm
