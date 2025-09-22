//===-- AArch64PBQPRegAlloc.cpp - AArch64 specific PBQP constraints -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file contains the AArch64 / Cortex-A57 specific register allocation
// constraints for use by the PBQP register allocator.
//
// It is essentially a transcription of what is contained in
// AArch64A57FPLoadBalancing, which tries to use a balanced
// mix of odd and even D-registers when performing a critical sequence of
// independent, non-quadword FP/ASIMD floating-point multiply-accumulates.
//===----------------------------------------------------------------------===//

#include "AArch64PBQPRegAlloc.h"
#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64RegisterInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegAllocPBQP.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "aarch64-pbqp"

using namespace llvm;

namespace {

bool isOdd(unsigned reg) {
  switch (reg) {
  default:
    llvm_unreachable("Register is not from the expected class !");
  case AArch64::S1:
  case AArch64::S3:
  case AArch64::S5:
  case AArch64::S7:
  case AArch64::S9:
  case AArch64::S11:
  case AArch64::S13:
  case AArch64::S15:
  case AArch64::S17:
  case AArch64::S19:
  case AArch64::S21:
  case AArch64::S23:
  case AArch64::S25:
  case AArch64::S27:
  case AArch64::S29:
  case AArch64::S31:
  case AArch64::D1:
  case AArch64::D3:
  case AArch64::D5:
  case AArch64::D7:
  case AArch64::D9:
  case AArch64::D11:
  case AArch64::D13:
  case AArch64::D15:
  case AArch64::D17:
  case AArch64::D19:
  case AArch64::D21:
  case AArch64::D23:
  case AArch64::D25:
  case AArch64::D27:
  case AArch64::D29:
  case AArch64::D31:
  case AArch64::Q1:
  case AArch64::Q3:
  case AArch64::Q5:
  case AArch64::Q7:
  case AArch64::Q9:
  case AArch64::Q11:
  case AArch64::Q13:
  case AArch64::Q15:
  case AArch64::Q17:
  case AArch64::Q19:
  case AArch64::Q21:
  case AArch64::Q23:
  case AArch64::Q25:
  case AArch64::Q27:
  case AArch64::Q29:
  case AArch64::Q31:
    return true;
  case AArch64::S0:
  case AArch64::S2:
  case AArch64::S4:
  case AArch64::S6:
  case AArch64::S8:
  case AArch64::S10:
  case AArch64::S12:
  case AArch64::S14:
  case AArch64::S16:
  case AArch64::S18:
  case AArch64::S20:
  case AArch64::S22:
  case AArch64::S24:
  case AArch64::S26:
  case AArch64::S28:
  case AArch64::S30:
  case AArch64::D0:
  case AArch64::D2:
  case AArch64::D4:
  case AArch64::D6:
  case AArch64::D8:
  case AArch64::D10:
  case AArch64::D12:
  case AArch64::D14:
  case AArch64::D16:
  case AArch64::D18:
  case AArch64::D20:
  case AArch64::D22:
  case AArch64::D24:
  case AArch64::D26:
  case AArch64::D28:
  case AArch64::D30:
  case AArch64::Q0:
  case AArch64::Q2:
  case AArch64::Q4:
  case AArch64::Q6:
  case AArch64::Q8:
  case AArch64::Q10:
  case AArch64::Q12:
  case AArch64::Q14:
  case AArch64::Q16:
  case AArch64::Q18:
  case AArch64::Q20:
  case AArch64::Q22:
  case AArch64::Q24:
  case AArch64::Q26:
  case AArch64::Q28:
  case AArch64::Q30:
    return false;

  }
}

bool haveSameParity(unsigned reg1, unsigned reg2) {
  assert(AArch64InstrInfo::isFpOrNEON(reg1) &&
         "Expecting an FP register for reg1");
  assert(AArch64InstrInfo::isFpOrNEON(reg2) &&
         "Expecting an FP register for reg2");

  return isOdd(reg1) == isOdd(reg2);
}

}

bool A57ChainingConstraint::addIntraChainConstraint(PBQPRAGraph &G, unsigned Rd,
                                                 unsigned Ra) {
  if (Rd == Ra)
    return false;

  LiveIntervals &LIs = G.getMetadata().LIS;

  if (Register::isPhysicalRegister(Rd) || Register::isPhysicalRegister(Ra)) {
    LLVM_DEBUG(dbgs() << "Rd is a physical reg:"
                      << Register::isPhysicalRegister(Rd) << '\n');
    LLVM_DEBUG(dbgs() << "Ra is a physical reg:"
                      << Register::isPhysicalRegister(Ra) << '\n');
    return false;
  }

  PBQPRAGraph::NodeId node1 = G.getMetadata().getNodeIdForVReg(Rd);
  PBQPRAGraph::NodeId node2 = G.getMetadata().getNodeIdForVReg(Ra);

  const PBQPRAGraph::NodeMetadata::AllowedRegVector *vRdAllowed =
    &G.getNodeMetadata(node1).getAllowedRegs();
  const PBQPRAGraph::NodeMetadata::AllowedRegVector *vRaAllowed =
    &G.getNodeMetadata(node2).getAllowedRegs();

  PBQPRAGraph::EdgeId edge = G.findEdge(node1, node2);

  // The edge does not exist. Create one with the appropriate interference
  // costs.
  if (edge == G.invalidEdgeId()) {
    const LiveInterval &ld = LIs.getInterval(Rd);
    const LiveInterval &la = LIs.getInterval(Ra);
    bool livesOverlap = ld.overlaps(la);

    PBQPRAGraph::RawMatrix costs(vRdAllowed->size() + 1,
                                 vRaAllowed->size() + 1, 0);
    for (unsigned i = 0, ie = vRdAllowed->size(); i != ie; ++i) {
      unsigned pRd = (*vRdAllowed)[i];
      for (unsigned j = 0, je = vRaAllowed->size(); j != je; ++j) {
        unsigned pRa = (*vRaAllowed)[j];
        if (livesOverlap && TRI->regsOverlap(pRd, pRa))
          costs[i + 1][j + 1] = std::numeric_limits<PBQP::PBQPNum>::infinity();
        else
          costs[i + 1][j + 1] = haveSameParity(pRd, pRa) ? 0.0 : 1.0;
      }
    }
    G.addEdge(node1, node2, std::move(costs));
    return true;
  }

  if (G.getEdgeNode1Id(edge) == node2) {
    std::swap(node1, node2);
    std::swap(vRdAllowed, vRaAllowed);
  }

  // Enforce minCost(sameParity(RaClass)) > maxCost(otherParity(RdClass))
  PBQPRAGraph::RawMatrix costs(G.getEdgeCosts(edge));
  for (unsigned i = 0, ie = vRdAllowed->size(); i != ie; ++i) {
    unsigned pRd = (*vRdAllowed)[i];

    // Get the maximum cost (excluding unallocatable reg) for same parity
    // registers
    PBQP::PBQPNum sameParityMax = std::numeric_limits<PBQP::PBQPNum>::min();
    for (unsigned j = 0, je = vRaAllowed->size(); j != je; ++j) {
      unsigned pRa = (*vRaAllowed)[j];
      if (haveSameParity(pRd, pRa))
        if (costs[i + 1][j + 1] !=
                std::numeric_limits<PBQP::PBQPNum>::infinity() &&
            costs[i + 1][j + 1] > sameParityMax)
          sameParityMax = costs[i + 1][j + 1];
    }

    // Ensure all registers with a different parity have a higher cost
    // than sameParityMax
    for (unsigned j = 0, je = vRaAllowed->size(); j != je; ++j) {
      unsigned pRa = (*vRaAllowed)[j];
      if (!haveSameParity(pRd, pRa))
        if (sameParityMax > costs[i + 1][j + 1])
          costs[i + 1][j + 1] = sameParityMax + 1.0;
    }
  }
  G.updateEdgeCosts(edge, std::move(costs));

  return true;
}

void A57ChainingConstraint::addInterChainConstraint(PBQPRAGraph &G, unsigned Rd,
                                                 unsigned Ra) {
  LiveIntervals &LIs = G.getMetadata().LIS;

  // Do some Chain management
  if (Chains.count(Ra)) {
    if (Rd != Ra) {
      LLVM_DEBUG(dbgs() << "Moving acc chain from " << printReg(Ra, TRI)
                        << " to " << printReg(Rd, TRI) << '\n';);
      Chains.remove(Ra);
      Chains.insert(Rd);
    }
  } else {
    LLVM_DEBUG(dbgs() << "Creating new acc chain for " << printReg(Rd, TRI)
                      << '\n';);
    Chains.insert(Rd);
  }

  PBQPRAGraph::NodeId node1 = G.getMetadata().getNodeIdForVReg(Rd);

  const LiveInterval &ld = LIs.getInterval(Rd);
  for (auto r : Chains) {
    // Skip self
    if (r == Rd)
      continue;

    const LiveInterval &lr = LIs.getInterval(r);
    if (ld.overlaps(lr)) {
      const PBQPRAGraph::NodeMetadata::AllowedRegVector *vRdAllowed =
        &G.getNodeMetadata(node1).getAllowedRegs();

      PBQPRAGraph::NodeId node2 = G.getMetadata().getNodeIdForVReg(r);
      const PBQPRAGraph::NodeMetadata::AllowedRegVector *vRrAllowed =
        &G.getNodeMetadata(node2).getAllowedRegs();

      PBQPRAGraph::EdgeId edge = G.findEdge(node1, node2);
      assert(edge != G.invalidEdgeId() &&
             "PBQP error ! The edge should exist !");

      LLVM_DEBUG(dbgs() << "Refining constraint !\n";);

      if (G.getEdgeNode1Id(edge) == node2) {
        std::swap(node1, node2);
        std::swap(vRdAllowed, vRrAllowed);
      }

      // Enforce that cost is higher with all other Chains of the same parity
      PBQP::Matrix costs(G.getEdgeCosts(edge));
      for (unsigned i = 0, ie = vRdAllowed->size(); i != ie; ++i) {
        unsigned pRd = (*vRdAllowed)[i];

        // Get the maximum cost (excluding unallocatable reg) for all other
        // parity registers
        PBQP::PBQPNum sameParityMax = std::numeric_limits<PBQP::PBQPNum>::min();
        for (unsigned j = 0, je = vRrAllowed->size(); j != je; ++j) {
          unsigned pRa = (*vRrAllowed)[j];
          if (!haveSameParity(pRd, pRa))
            if (costs[i + 1][j + 1] !=
                    std::numeric_limits<PBQP::PBQPNum>::infinity() &&
                costs[i + 1][j + 1] > sameParityMax)
              sameParityMax = costs[i + 1][j + 1];
        }

        // Ensure all registers with same parity have a higher cost
        // than sameParityMax
        for (unsigned j = 0, je = vRrAllowed->size(); j != je; ++j) {
          unsigned pRa = (*vRrAllowed)[j];
          if (haveSameParity(pRd, pRa))
            if (sameParityMax > costs[i + 1][j + 1])
              costs[i + 1][j + 1] = sameParityMax + 1.0;
        }
      }
      G.updateEdgeCosts(edge, std::move(costs));
    }
  }
}

static bool regJustKilledBefore(const LiveIntervals &LIs, unsigned reg,
                                const MachineInstr &MI) {
  const LiveInterval &LI = LIs.getInterval(reg);
  SlotIndex SI = LIs.getInstructionIndex(MI);
  return LI.expiredAt(SI);
}

void A57ChainingConstraint::apply(PBQPRAGraph &G) {
  const MachineFunction &MF = G.getMetadata().MF;
  LiveIntervals &LIs = G.getMetadata().LIS;

  TRI = MF.getSubtarget().getRegisterInfo();
  LLVM_DEBUG(MF.dump());

  for (const auto &MBB: MF) {
    Chains.clear(); // FIXME: really needed ? Could not work at MF level ?

    for (const auto &MI: MBB) {

      // Forget Chains which have expired
      for (auto r : Chains) {
        SmallVector<unsigned, 8> toDel;
        if(regJustKilledBefore(LIs, r, MI)) {
          LLVM_DEBUG(dbgs() << "Killing chain " << printReg(r, TRI) << " at ";
                     MI.print(dbgs()););
          toDel.push_back(r);
        }

        while (!toDel.empty()) {
          Chains.remove(toDel.back());
          toDel.pop_back();
        }
      }

      switch (MI.getOpcode()) {
      case AArch64::FMSUBSrrr:
      case AArch64::FMADDSrrr:
      case AArch64::FNMSUBSrrr:
      case AArch64::FNMADDSrrr:
      case AArch64::FMSUBDrrr:
      case AArch64::FMADDDrrr:
      case AArch64::FNMSUBDrrr:
      case AArch64::FNMADDDrrr: {
        Register Rd = MI.getOperand(0).getReg();
        Register Ra = MI.getOperand(3).getReg();

        if (addIntraChainConstraint(G, Rd, Ra))
          addInterChainConstraint(G, Rd, Ra);
        break;
      }

      case AArch64::FMLAv2f32:
      case AArch64::FMLSv2f32: {
        Register Rd = MI.getOperand(0).getReg();
        addInterChainConstraint(G, Rd, Rd);
        break;
      }

      default:
        break;
      }
    }
  }
}
