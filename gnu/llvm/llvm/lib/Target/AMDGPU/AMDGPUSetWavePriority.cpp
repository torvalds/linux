//===- AMDGPUSetWavePriority.cpp - Set wave priority ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Pass to temporarily raise the wave priority beginning the start of
/// the shader function until its last VMEM instructions to allow younger
/// waves to issue their VMEM instructions as well.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Allocator.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-set-wave-priority"

static cl::opt<unsigned> DefaultVALUInstsThreshold(
    "amdgpu-set-wave-priority-valu-insts-threshold",
    cl::desc("VALU instruction count threshold for adjusting wave priority"),
    cl::init(100), cl::Hidden);

namespace {

struct MBBInfo {
  MBBInfo() = default;
  unsigned NumVALUInstsAtStart = 0;
  bool MayReachVMEMLoad = false;
  MachineInstr *LastVMEMLoad = nullptr;
};

using MBBInfoSet = DenseMap<const MachineBasicBlock *, MBBInfo>;

class AMDGPUSetWavePriority : public MachineFunctionPass {
public:
  static char ID;

  AMDGPUSetWavePriority() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "Set wave priority"; }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  MachineInstr *BuildSetprioMI(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               unsigned priority) const;

  const SIInstrInfo *TII;
};

} // End anonymous namespace.

INITIALIZE_PASS(AMDGPUSetWavePriority, DEBUG_TYPE, "Set wave priority", false,
                false)

char AMDGPUSetWavePriority::ID = 0;

FunctionPass *llvm::createAMDGPUSetWavePriorityPass() {
  return new AMDGPUSetWavePriority();
}

MachineInstr *
AMDGPUSetWavePriority::BuildSetprioMI(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator I,
                                      unsigned priority) const {
  return BuildMI(MBB, I, DebugLoc(), TII->get(AMDGPU::S_SETPRIO))
      .addImm(priority);
}

// Checks that for every predecessor Pred that can reach a VMEM load,
// none of Pred's successors can reach a VMEM load.
static bool CanLowerPriorityDirectlyInPredecessors(const MachineBasicBlock &MBB,
                                                   MBBInfoSet &MBBInfos) {
  for (const MachineBasicBlock *Pred : MBB.predecessors()) {
    if (!MBBInfos[Pred].MayReachVMEMLoad)
      continue;
    for (const MachineBasicBlock *Succ : Pred->successors()) {
      if (MBBInfos[Succ].MayReachVMEMLoad)
        return false;
    }
  }
  return true;
}

static bool isVMEMLoad(const MachineInstr &MI) {
  return SIInstrInfo::isVMEM(MI) && MI.mayLoad();
}

bool AMDGPUSetWavePriority::runOnMachineFunction(MachineFunction &MF) {
  const unsigned HighPriority = 3;
  const unsigned LowPriority = 0;

  Function &F = MF.getFunction();
  if (skipFunction(F) || !AMDGPU::isEntryFunctionCC(F.getCallingConv()))
    return false;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();

  unsigned VALUInstsThreshold = DefaultVALUInstsThreshold;
  Attribute A = F.getFnAttribute("amdgpu-wave-priority-threshold");
  if (A.isValid())
    A.getValueAsString().getAsInteger(0, VALUInstsThreshold);

  // Find VMEM loads that may be executed before long-enough sequences of
  // VALU instructions. We currently assume that backedges/loops, branch
  // probabilities and other details can be ignored, so we essentially
  // determine the largest number of VALU instructions along every
  // possible path from the start of the function that may potentially be
  // executed provided no backedge is ever taken.
  MBBInfoSet MBBInfos;
  for (MachineBasicBlock *MBB : post_order(&MF)) {
    bool AtStart = true;
    unsigned MaxNumVALUInstsInMiddle = 0;
    unsigned NumVALUInstsAtEnd = 0;
    for (MachineInstr &MI : *MBB) {
      if (isVMEMLoad(MI)) {
        AtStart = false;
        MBBInfo &Info = MBBInfos[MBB];
        Info.NumVALUInstsAtStart = 0;
        MaxNumVALUInstsInMiddle = 0;
        NumVALUInstsAtEnd = 0;
        Info.LastVMEMLoad = &MI;
      } else if (SIInstrInfo::isDS(MI)) {
        AtStart = false;
        MaxNumVALUInstsInMiddle =
            std::max(MaxNumVALUInstsInMiddle, NumVALUInstsAtEnd);
        NumVALUInstsAtEnd = 0;
      } else if (SIInstrInfo::isVALU(MI)) {
        if (AtStart)
          ++MBBInfos[MBB].NumVALUInstsAtStart;
        ++NumVALUInstsAtEnd;
      }
    }

    bool SuccsMayReachVMEMLoad = false;
    unsigned NumFollowingVALUInsts = 0;
    for (const MachineBasicBlock *Succ : MBB->successors()) {
      SuccsMayReachVMEMLoad |= MBBInfos[Succ].MayReachVMEMLoad;
      NumFollowingVALUInsts =
          std::max(NumFollowingVALUInsts, MBBInfos[Succ].NumVALUInstsAtStart);
    }
    MBBInfo &Info = MBBInfos[MBB];
    if (AtStart)
      Info.NumVALUInstsAtStart += NumFollowingVALUInsts;
    NumVALUInstsAtEnd += NumFollowingVALUInsts;

    unsigned MaxNumVALUInsts =
        std::max(MaxNumVALUInstsInMiddle, NumVALUInstsAtEnd);
    Info.MayReachVMEMLoad =
        SuccsMayReachVMEMLoad ||
        (Info.LastVMEMLoad && MaxNumVALUInsts >= VALUInstsThreshold);
  }

  MachineBasicBlock &Entry = MF.front();
  if (!MBBInfos[&Entry].MayReachVMEMLoad)
    return false;

  // Raise the priority at the beginning of the shader.
  MachineBasicBlock::iterator I = Entry.begin(), E = Entry.end();
  while (I != E && !SIInstrInfo::isVALU(*I) && !I->isTerminator())
    ++I;
  BuildSetprioMI(Entry, I, HighPriority);

  // Lower the priority on edges where control leaves blocks from which
  // the VMEM loads are reachable.
  SmallSet<MachineBasicBlock *, 16> PriorityLoweringBlocks;
  for (MachineBasicBlock &MBB : MF) {
    if (MBBInfos[&MBB].MayReachVMEMLoad) {
      if (MBB.succ_empty())
        PriorityLoweringBlocks.insert(&MBB);
      continue;
    }

    if (CanLowerPriorityDirectlyInPredecessors(MBB, MBBInfos)) {
      for (MachineBasicBlock *Pred : MBB.predecessors()) {
        if (MBBInfos[Pred].MayReachVMEMLoad)
          PriorityLoweringBlocks.insert(Pred);
      }
      continue;
    }

    // Where lowering the priority in predecessors is not possible, the
    // block receiving control either was not part of a loop in the first
    // place or the loop simplification/canonicalization pass should have
    // already tried to split the edge and insert a preheader, and if for
    // whatever reason it failed to do so, then this leaves us with the
    // only option of lowering the priority within the loop.
    PriorityLoweringBlocks.insert(&MBB);
  }

  for (MachineBasicBlock *MBB : PriorityLoweringBlocks) {
    BuildSetprioMI(
        *MBB,
        MBBInfos[MBB].LastVMEMLoad
            ? std::next(MachineBasicBlock::iterator(MBBInfos[MBB].LastVMEMLoad))
            : MBB->begin(),
        LowPriority);
  }

  return true;
}
