//===-- SILowerSGPRSPills.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Handle SGPR spills. This pass takes the place of PrologEpilogInserter for all
// SGPR spills, so must insert CSR SGPR spills as well as expand them.
//
// This pass must never create new SGPR virtual registers.
//
// FIXME: Must stop RegScavenger spills in later passes.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/InitializePasses.h"

using namespace llvm;

#define DEBUG_TYPE "si-lower-sgpr-spills"

using MBBVector = SmallVector<MachineBasicBlock *, 4>;

namespace {

class SILowerSGPRSpills : public MachineFunctionPass {
private:
  const SIRegisterInfo *TRI = nullptr;
  const SIInstrInfo *TII = nullptr;
  LiveIntervals *LIS = nullptr;
  SlotIndexes *Indexes = nullptr;

  // Save and Restore blocks of the current function. Typically there is a
  // single save block, unless Windows EH funclets are involved.
  MBBVector SaveBlocks;
  MBBVector RestoreBlocks;

public:
  static char ID;

  SILowerSGPRSpills() : MachineFunctionPass(ID) {}

  void calculateSaveRestoreBlocks(MachineFunction &MF);
  bool spillCalleeSavedRegs(MachineFunction &MF,
                            SmallVectorImpl<int> &CalleeSavedFIs);
  void extendWWMVirtRegLiveness(MachineFunction &MF, LiveIntervals *LIS);

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getClearedProperties() const override {
    // SILowerSGPRSpills introduces new Virtual VGPRs for spilling SGPRs.
    return MachineFunctionProperties()
        .set(MachineFunctionProperties::Property::IsSSA)
        .set(MachineFunctionProperties::Property::NoVRegs);
  }
};

} // end anonymous namespace

char SILowerSGPRSpills::ID = 0;

INITIALIZE_PASS_BEGIN(SILowerSGPRSpills, DEBUG_TYPE,
                      "SI lower SGPR spill instructions", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(VirtRegMap)
INITIALIZE_PASS_END(SILowerSGPRSpills, DEBUG_TYPE,
                    "SI lower SGPR spill instructions", false, false)

char &llvm::SILowerSGPRSpillsID = SILowerSGPRSpills::ID;

/// Insert spill code for the callee-saved registers used in the function.
static void insertCSRSaves(MachineBasicBlock &SaveBlock,
                           ArrayRef<CalleeSavedInfo> CSI, SlotIndexes *Indexes,
                           LiveIntervals *LIS) {
  MachineFunction &MF = *SaveBlock.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *RI = ST.getRegisterInfo();

  MachineBasicBlock::iterator I = SaveBlock.begin();
  if (!TFI->spillCalleeSavedRegisters(SaveBlock, I, CSI, TRI)) {
    const MachineRegisterInfo &MRI = MF.getRegInfo();

    for (const CalleeSavedInfo &CS : CSI) {
      // Insert the spill to the stack frame.
      MCRegister Reg = CS.getReg();

      MachineInstrSpan MIS(I, &SaveBlock);
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(
          Reg, Reg == RI->getReturnAddressReg(MF) ? MVT::i64 : MVT::i32);

      // If this value was already livein, we probably have a direct use of the
      // incoming register value, so don't kill at the spill point. This happens
      // since we pass some special inputs (workgroup IDs) in the callee saved
      // range.
      const bool IsLiveIn = MRI.isLiveIn(Reg);
      TII.storeRegToStackSlot(SaveBlock, I, Reg, !IsLiveIn, CS.getFrameIdx(),
                              RC, TRI, Register());

      if (Indexes) {
        assert(std::distance(MIS.begin(), I) == 1);
        MachineInstr &Inst = *std::prev(I);
        Indexes->insertMachineInstrInMaps(Inst);
      }

      if (LIS)
        LIS->removeAllRegUnitsForPhysReg(Reg);
    }
  }
}

/// Insert restore code for the callee-saved registers used in the function.
static void insertCSRRestores(MachineBasicBlock &RestoreBlock,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              SlotIndexes *Indexes, LiveIntervals *LIS) {
  MachineFunction &MF = *RestoreBlock.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *RI = ST.getRegisterInfo();
  // Restore all registers immediately before the return and any
  // terminators that precede it.
  MachineBasicBlock::iterator I = RestoreBlock.getFirstTerminator();

  // FIXME: Just emit the readlane/writelane directly
  if (!TFI->restoreCalleeSavedRegisters(RestoreBlock, I, CSI, TRI)) {
    for (const CalleeSavedInfo &CI : reverse(CSI)) {
      Register Reg = CI.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(
          Reg, Reg == RI->getReturnAddressReg(MF) ? MVT::i64 : MVT::i32);

      TII.loadRegFromStackSlot(RestoreBlock, I, Reg, CI.getFrameIdx(), RC, TRI,
                               Register());
      assert(I != RestoreBlock.begin() &&
             "loadRegFromStackSlot didn't insert any code!");
      // Insert in reverse order.  loadRegFromStackSlot can insert
      // multiple instructions.

      if (Indexes) {
        MachineInstr &Inst = *std::prev(I);
        Indexes->insertMachineInstrInMaps(Inst);
      }

      if (LIS)
        LIS->removeAllRegUnitsForPhysReg(Reg);
    }
  }
}

/// Compute the sets of entry and return blocks for saving and restoring
/// callee-saved registers, and placing prolog and epilog code.
void SILowerSGPRSpills::calculateSaveRestoreBlocks(MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Even when we do not change any CSR, we still want to insert the
  // prologue and epilogue of the function.
  // So set the save points for those.

  // Use the points found by shrink-wrapping, if any.
  if (MFI.getSavePoint()) {
    SaveBlocks.push_back(MFI.getSavePoint());
    assert(MFI.getRestorePoint() && "Both restore and save must be set");
    MachineBasicBlock *RestoreBlock = MFI.getRestorePoint();
    // If RestoreBlock does not have any successor and is not a return block
    // then the end point is unreachable and we do not need to insert any
    // epilogue.
    if (!RestoreBlock->succ_empty() || RestoreBlock->isReturnBlock())
      RestoreBlocks.push_back(RestoreBlock);
    return;
  }

  // Save refs to entry and return blocks.
  SaveBlocks.push_back(&MF.front());
  for (MachineBasicBlock &MBB : MF) {
    if (MBB.isEHFuncletEntry())
      SaveBlocks.push_back(&MBB);
    if (MBB.isReturnBlock())
      RestoreBlocks.push_back(&MBB);
  }
}

// TODO: To support shrink wrapping, this would need to copy
// PrologEpilogInserter's updateLiveness.
static void updateLiveness(MachineFunction &MF, ArrayRef<CalleeSavedInfo> CSI) {
  MachineBasicBlock &EntryBB = MF.front();

  for (const CalleeSavedInfo &CSIReg : CSI)
    EntryBB.addLiveIn(CSIReg.getReg());
  EntryBB.sortUniqueLiveIns();
}

bool SILowerSGPRSpills::spillCalleeSavedRegs(
    MachineFunction &MF, SmallVectorImpl<int> &CalleeSavedFIs) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIFrameLowering *TFI = ST.getFrameLowering();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  RegScavenger *RS = nullptr;

  // Determine which of the registers in the callee save list should be saved.
  BitVector SavedRegs;
  TFI->determineCalleeSavesSGPR(MF, SavedRegs, RS);

  // Add the code to save and restore the callee saved registers.
  if (!F.hasFnAttribute(Attribute::Naked)) {
    // FIXME: This is a lie. The CalleeSavedInfo is incomplete, but this is
    // necessary for verifier liveness checks.
    MFI.setCalleeSavedInfoValid(true);

    std::vector<CalleeSavedInfo> CSI;
    const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();

    for (unsigned I = 0; CSRegs[I]; ++I) {
      MCRegister Reg = CSRegs[I];

      if (SavedRegs.test(Reg)) {
        const TargetRegisterClass *RC =
          TRI->getMinimalPhysRegClass(Reg, MVT::i32);
        int JunkFI = MFI.CreateStackObject(TRI->getSpillSize(*RC),
                                           TRI->getSpillAlign(*RC), true);

        CSI.emplace_back(Reg, JunkFI);
        CalleeSavedFIs.push_back(JunkFI);
      }
    }

    if (!CSI.empty()) {
      for (MachineBasicBlock *SaveBlock : SaveBlocks)
        insertCSRSaves(*SaveBlock, CSI, Indexes, LIS);

      // Add live ins to save blocks.
      assert(SaveBlocks.size() == 1 && "shrink wrapping not fully implemented");
      updateLiveness(MF, CSI);

      for (MachineBasicBlock *RestoreBlock : RestoreBlocks)
        insertCSRRestores(*RestoreBlock, CSI, Indexes, LIS);
      return true;
    }
  }

  return false;
}

void SILowerSGPRSpills::extendWWMVirtRegLiveness(MachineFunction &MF,
                                                 LiveIntervals *LIS) {
  // TODO: This is a workaround to avoid the unmodelled liveness computed with
  // whole-wave virtual registers when allocated together with the regular VGPR
  // virtual registers. Presently, the liveness computed during the regalloc is
  // only uniform (or single lane aware) and it doesn't take account of the
  // divergent control flow that exists for our GPUs. Since the WWM registers
  // can modify inactive lanes, the wave-aware liveness should be computed for
  // the virtual registers to accurately plot their interferences. Without
  // having the divergent CFG for the function, it is difficult to implement the
  // wave-aware liveness info. Until then, we conservatively extend the liveness
  // of the wwm registers into the entire function so that they won't be reused
  // without first spilling/splitting their liveranges.
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // Insert the IMPLICIT_DEF for the wwm-registers in the entry blocks.
  for (auto Reg : MFI->getSGPRSpillVGPRs()) {
    for (MachineBasicBlock *SaveBlock : SaveBlocks) {
      MachineBasicBlock::iterator InsertBefore = SaveBlock->begin();
      DebugLoc DL = SaveBlock->findDebugLoc(InsertBefore);
      auto MIB = BuildMI(*SaveBlock, InsertBefore, DL,
                         TII->get(AMDGPU::IMPLICIT_DEF), Reg);
      MFI->setFlag(Reg, AMDGPU::VirtRegFlag::WWM_REG);
      // Set SGPR_SPILL asm printer flag
      MIB->setAsmPrinterFlag(AMDGPU::SGPR_SPILL);
      if (LIS) {
        LIS->InsertMachineInstrInMaps(*MIB);
      }
    }
  }

  // Insert the KILL in the return blocks to extend their liveness untill the
  // end of function. Insert a separate KILL for each VGPR.
  for (MachineBasicBlock *RestoreBlock : RestoreBlocks) {
    MachineBasicBlock::iterator InsertBefore =
        RestoreBlock->getFirstTerminator();
    DebugLoc DL = RestoreBlock->findDebugLoc(InsertBefore);
    for (auto Reg : MFI->getSGPRSpillVGPRs()) {
      auto MIB = BuildMI(*RestoreBlock, InsertBefore, DL,
                         TII->get(TargetOpcode::KILL));
      MIB.addReg(Reg);
      if (LIS)
        LIS->InsertMachineInstrInMaps(*MIB);
    }
  }
}

bool SILowerSGPRSpills::runOnMachineFunction(MachineFunction &MF) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  TII = ST.getInstrInfo();
  TRI = &TII->getRegisterInfo();

  auto *LISWrapper = getAnalysisIfAvailable<LiveIntervalsWrapperPass>();
  LIS = LISWrapper ? &LISWrapper->getLIS() : nullptr;
  auto *SIWrapper = getAnalysisIfAvailable<SlotIndexesWrapperPass>();
  Indexes = SIWrapper ? &SIWrapper->getSI() : nullptr;

  assert(SaveBlocks.empty() && RestoreBlocks.empty());

  // First, expose any CSR SGPR spills. This is mostly the same as what PEI
  // does, but somewhat simpler.
  calculateSaveRestoreBlocks(MF);
  SmallVector<int> CalleeSavedFIs;
  bool HasCSRs = spillCalleeSavedRegs(MF, CalleeSavedFIs);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();

  if (!MFI.hasStackObjects() && !HasCSRs) {
    SaveBlocks.clear();
    RestoreBlocks.clear();
    return false;
  }

  bool MadeChange = false;
  bool SpilledToVirtVGPRLanes = false;

  // TODO: CSR VGPRs will never be spilled to AGPRs. These can probably be
  // handled as SpilledToReg in regular PrologEpilogInserter.
  const bool HasSGPRSpillToVGPR = TRI->spillSGPRToVGPR() &&
                                  (HasCSRs || FuncInfo->hasSpilledSGPRs());
  if (HasSGPRSpillToVGPR) {
    // Process all SGPR spills before frame offsets are finalized. Ideally SGPRs
    // are spilled to VGPRs, in which case we can eliminate the stack usage.
    //
    // This operates under the assumption that only other SGPR spills are users
    // of the frame index.

    // To track the spill frame indices handled in this pass.
    BitVector SpillFIs(MFI.getObjectIndexEnd(), false);

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
        if (!TII->isSGPRSpill(MI))
          continue;

        int FI = TII->getNamedOperand(MI, AMDGPU::OpName::addr)->getIndex();
        assert(MFI.getStackID(FI) == TargetStackID::SGPRSpill);

        bool IsCalleeSaveSGPRSpill = llvm::is_contained(CalleeSavedFIs, FI);
        if (IsCalleeSaveSGPRSpill) {
          // Spill callee-saved SGPRs into physical VGPR lanes.

          // TODO: This is to ensure the CFIs are static for efficient frame
          // unwinding in the debugger. Spilling them into virtual VGPR lanes
          // involve regalloc to allocate the physical VGPRs and that might
          // cause intermediate spill/split of such liveranges for successful
          // allocation. This would result in broken CFI encoding unless the
          // regalloc aware CFI generation to insert new CFIs along with the
          // intermediate spills is implemented. There is no such support
          // currently exist in the LLVM compiler.
          if (FuncInfo->allocateSGPRSpillToVGPRLane(
                  MF, FI, /*SpillToPhysVGPRLane=*/true)) {
            bool Spilled = TRI->eliminateSGPRToVGPRSpillFrameIndex(
                MI, FI, nullptr, Indexes, LIS, true);
            if (!Spilled)
              llvm_unreachable(
                  "failed to spill SGPR to physical VGPR lane when allocated");
          }
        } else {
          if (FuncInfo->allocateSGPRSpillToVGPRLane(MF, FI)) {
            bool Spilled = TRI->eliminateSGPRToVGPRSpillFrameIndex(
                MI, FI, nullptr, Indexes, LIS);
            if (!Spilled)
              llvm_unreachable(
                  "failed to spill SGPR to virtual VGPR lane when allocated");
            SpillFIs.set(FI);
            SpilledToVirtVGPRLanes = true;
          }
        }
      }
    }

    if (SpilledToVirtVGPRLanes) {
      extendWWMVirtRegLiveness(MF, LIS);
      if (LIS) {
        // Compute the LiveInterval for the newly created virtual registers.
        for (auto Reg : FuncInfo->getSGPRSpillVGPRs())
          LIS->createAndComputeVirtRegInterval(Reg);
      }
    }

    for (MachineBasicBlock &MBB : MF) {
      // FIXME: The dead frame indices are replaced with a null register from
      // the debug value instructions. We should instead, update it with the
      // correct register value. But not sure the register value alone is
      // adequate to lower the DIExpression. It should be worked out later.
      for (MachineInstr &MI : MBB) {
        if (MI.isDebugValue() && MI.getOperand(0).isFI() &&
            !MFI.isFixedObjectIndex(MI.getOperand(0).getIndex()) &&
            SpillFIs[MI.getOperand(0).getIndex()]) {
          MI.getOperand(0).ChangeToRegister(Register(), false /*isDef*/);
        }
      }
    }

    // All those frame indices which are dead by now should be removed from the
    // function frame. Otherwise, there is a side effect such as re-mapping of
    // free frame index ids by the later pass(es) like "stack slot coloring"
    // which in turn could mess-up with the book keeping of "frame index to VGPR
    // lane".
    FuncInfo->removeDeadFrameIndices(MFI, /*ResetSGPRSpillStackIDs*/ false);

    MadeChange = true;
  }

  if (SpilledToVirtVGPRLanes) {
    const TargetRegisterClass *RC = TRI->getWaveMaskRegClass();
    // Shift back the reserved SGPR for EXEC copy into the lowest range.
    // This SGPR is reserved to handle the whole-wave spill/copy operations
    // that might get inserted during vgpr regalloc.
    Register UnusedLowSGPR = TRI->findUnusedRegister(MRI, RC, MF);
    if (UnusedLowSGPR && TRI->getHWRegIndex(UnusedLowSGPR) <
                             TRI->getHWRegIndex(FuncInfo->getSGPRForEXECCopy()))
      FuncInfo->setSGPRForEXECCopy(UnusedLowSGPR);
  } else {
    // No SGPR spills to virtual VGPR lanes and hence there won't be any WWM
    // spills/copies. Reset the SGPR reserved for EXEC copy.
    FuncInfo->setSGPRForEXECCopy(AMDGPU::NoRegister);
  }

  SaveBlocks.clear();
  RestoreBlocks.clear();

  return MadeChange;
}
