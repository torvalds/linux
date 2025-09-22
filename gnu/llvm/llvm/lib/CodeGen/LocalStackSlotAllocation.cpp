//===- LocalStackSlotAllocation.cpp - Pre-allocate locals to stack slots --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass assigns local frame indices to stack slots relative to one another
// and allocates additional base registers to access them when the target
// estimates they are likely to be out of range of stack pointer and frame
// pointer relative addressing.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/LocalStackSlotAllocation.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <tuple>

using namespace llvm;

#define DEBUG_TYPE "localstackalloc"

STATISTIC(NumAllocations, "Number of frame indices allocated into local block");
STATISTIC(NumBaseRegisters, "Number of virtual frame base registers allocated");
STATISTIC(NumReplacements, "Number of frame indices references replaced");

namespace {

  class FrameRef {
    MachineBasicBlock::iterator MI; // Instr referencing the frame
    int64_t LocalOffset;            // Local offset of the frame idx referenced
    int FrameIdx;                   // The frame index

    // Order reference instruction appears in program. Used to ensure
    // deterministic order when multiple instructions may reference the same
    // location.
    unsigned Order;

  public:
    FrameRef(MachineInstr *I, int64_t Offset, int Idx, unsigned Ord) :
      MI(I), LocalOffset(Offset), FrameIdx(Idx), Order(Ord) {}

    bool operator<(const FrameRef &RHS) const {
      return std::tie(LocalOffset, FrameIdx, Order) <
             std::tie(RHS.LocalOffset, RHS.FrameIdx, RHS.Order);
    }

    MachineBasicBlock::iterator getMachineInstr() const { return MI; }
    int64_t getLocalOffset() const { return LocalOffset; }
    int getFrameIndex() const { return FrameIdx; }
  };

  class LocalStackSlotImpl {
    SmallVector<int64_t, 16> LocalOffsets;

    /// StackObjSet - A set of stack object indexes
    using StackObjSet = SmallSetVector<int, 8>;

    void AdjustStackOffset(MachineFrameInfo &MFI, int FrameIdx, int64_t &Offset,
                           bool StackGrowsDown, Align &MaxAlign);
    void AssignProtectedObjSet(const StackObjSet &UnassignedObjs,
                               SmallSet<int, 16> &ProtectedObjs,
                               MachineFrameInfo &MFI, bool StackGrowsDown,
                               int64_t &Offset, Align &MaxAlign);
    void calculateFrameObjectOffsets(MachineFunction &Fn);
    bool insertFrameReferenceRegisters(MachineFunction &Fn);

  public:
    bool runOnMachineFunction(MachineFunction &MF);
  };

  class LocalStackSlotPass : public MachineFunctionPass {
  public:
    static char ID; // Pass identification, replacement for typeid

    explicit LocalStackSlotPass() : MachineFunctionPass(ID) {
      initializeLocalStackSlotPassPass(*PassRegistry::getPassRegistry());
    }

    bool runOnMachineFunction(MachineFunction &MF) override {
      return LocalStackSlotImpl().runOnMachineFunction(MF);
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesCFG();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };

} // end anonymous namespace

PreservedAnalyses
LocalStackSlotAllocationPass::run(MachineFunction &MF,
                                  MachineFunctionAnalysisManager &) {
  bool Changed = LocalStackSlotImpl().runOnMachineFunction(MF);
  if (!Changed)
    return PreservedAnalyses::all();
  auto PA = getMachineFunctionPassPreservedAnalyses();
  PA.preserveSet<CFGAnalyses>();
  return PA;
}

char LocalStackSlotPass::ID = 0;

char &llvm::LocalStackSlotAllocationID = LocalStackSlotPass::ID;
INITIALIZE_PASS(LocalStackSlotPass, DEBUG_TYPE,
                "Local Stack Slot Allocation", false, false)

bool LocalStackSlotImpl::runOnMachineFunction(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  unsigned LocalObjectCount = MFI.getObjectIndexEnd();

  // If the target doesn't want/need this pass, or if there are no locals
  // to consider, early exit.
  if (LocalObjectCount == 0 || !TRI->requiresVirtualBaseRegisters(MF))
    return false;

  // Make sure we have enough space to store the local offsets.
  LocalOffsets.resize(MFI.getObjectIndexEnd());

  // Lay out the local blob.
  calculateFrameObjectOffsets(MF);

  // Insert virtual base registers to resolve frame index references.
  bool UsedBaseRegs = insertFrameReferenceRegisters(MF);

  // Tell MFI whether any base registers were allocated. PEI will only
  // want to use the local block allocations from this pass if there were any.
  // Otherwise, PEI can do a bit better job of getting the alignment right
  // without a hole at the start since it knows the alignment of the stack
  // at the start of local allocation, and this pass doesn't.
  MFI.setUseLocalStackAllocationBlock(UsedBaseRegs);

  return true;
}

/// AdjustStackOffset - Helper function used to adjust the stack frame offset.
void LocalStackSlotImpl::AdjustStackOffset(MachineFrameInfo &MFI, int FrameIdx,
                                           int64_t &Offset, bool StackGrowsDown,
                                           Align &MaxAlign) {
  // If the stack grows down, add the object size to find the lowest address.
  if (StackGrowsDown)
    Offset += MFI.getObjectSize(FrameIdx);

  Align Alignment = MFI.getObjectAlign(FrameIdx);

  // If the alignment of this object is greater than that of the stack, then
  // increase the stack alignment to match.
  MaxAlign = std::max(MaxAlign, Alignment);

  // Adjust to alignment boundary.
  Offset = alignTo(Offset, Alignment);

  int64_t LocalOffset = StackGrowsDown ? -Offset : Offset;
  LLVM_DEBUG(dbgs() << "Allocate FI(" << FrameIdx << ") to local offset "
                    << LocalOffset << "\n");
  // Keep the offset available for base register allocation
  LocalOffsets[FrameIdx] = LocalOffset;
  // And tell MFI about it for PEI to use later
  MFI.mapLocalFrameObject(FrameIdx, LocalOffset);

  if (!StackGrowsDown)
    Offset += MFI.getObjectSize(FrameIdx);

  ++NumAllocations;
}

/// AssignProtectedObjSet - Helper function to assign large stack objects (i.e.,
/// those required to be close to the Stack Protector) to stack offsets.
void LocalStackSlotImpl::AssignProtectedObjSet(
    const StackObjSet &UnassignedObjs, SmallSet<int, 16> &ProtectedObjs,
    MachineFrameInfo &MFI, bool StackGrowsDown, int64_t &Offset,
    Align &MaxAlign) {
  for (int i : UnassignedObjs) {
    AdjustStackOffset(MFI, i, Offset, StackGrowsDown, MaxAlign);
    ProtectedObjs.insert(i);
  }
}

/// calculateFrameObjectOffsets - Calculate actual frame offsets for all of the
/// abstract stack objects.
void LocalStackSlotImpl::calculateFrameObjectOffsets(MachineFunction &Fn) {
  // Loop over all of the stack objects, assigning sequential addresses...
  MachineFrameInfo &MFI = Fn.getFrameInfo();
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  bool StackGrowsDown =
    TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;
  int64_t Offset = 0;
  Align MaxAlign;

  // Make sure that the stack protector comes before the local variables on the
  // stack.
  SmallSet<int, 16> ProtectedObjs;
  if (MFI.hasStackProtectorIndex()) {
    int StackProtectorFI = MFI.getStackProtectorIndex();

    // We need to make sure we didn't pre-allocate the stack protector when
    // doing this.
    // If we already have a stack protector, this will re-assign it to a slot
    // that is **not** covering the protected objects.
    assert(!MFI.isObjectPreAllocated(StackProtectorFI) &&
           "Stack protector pre-allocated in LocalStackSlotAllocation");

    StackObjSet LargeArrayObjs;
    StackObjSet SmallArrayObjs;
    StackObjSet AddrOfObjs;

    // Only place the stack protector in the local stack area if the target
    // allows it.
    if (TFI.isStackIdSafeForLocalArea(MFI.getStackID(StackProtectorFI)))
      AdjustStackOffset(MFI, StackProtectorFI, Offset, StackGrowsDown,
                        MaxAlign);

    // Assign large stack objects first.
    for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
      if (MFI.isDeadObjectIndex(i))
        continue;
      if (StackProtectorFI == (int)i)
        continue;
      if (!TFI.isStackIdSafeForLocalArea(MFI.getStackID(i)))
        continue;

      switch (MFI.getObjectSSPLayout(i)) {
      case MachineFrameInfo::SSPLK_None:
        continue;
      case MachineFrameInfo::SSPLK_SmallArray:
        SmallArrayObjs.insert(i);
        continue;
      case MachineFrameInfo::SSPLK_AddrOf:
        AddrOfObjs.insert(i);
        continue;
      case MachineFrameInfo::SSPLK_LargeArray:
        LargeArrayObjs.insert(i);
        continue;
      }
      llvm_unreachable("Unexpected SSPLayoutKind.");
    }

    AssignProtectedObjSet(LargeArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
    AssignProtectedObjSet(SmallArrayObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
    AssignProtectedObjSet(AddrOfObjs, ProtectedObjs, MFI, StackGrowsDown,
                          Offset, MaxAlign);
  }

  // Then assign frame offsets to stack objects that are not used to spill
  // callee saved registers.
  for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
    if (MFI.isDeadObjectIndex(i))
      continue;
    if (MFI.getStackProtectorIndex() == (int)i)
      continue;
    if (ProtectedObjs.count(i))
      continue;
    if (!TFI.isStackIdSafeForLocalArea(MFI.getStackID(i)))
      continue;

    AdjustStackOffset(MFI, i, Offset, StackGrowsDown, MaxAlign);
  }

  // Remember how big this blob of stack space is
  MFI.setLocalFrameSize(Offset);
  MFI.setLocalFrameMaxAlign(MaxAlign);
}

static inline bool
lookupCandidateBaseReg(unsigned BaseReg,
                       int64_t BaseOffset,
                       int64_t FrameSizeAdjust,
                       int64_t LocalFrameOffset,
                       const MachineInstr &MI,
                       const TargetRegisterInfo *TRI) {
  // Check if the relative offset from the where the base register references
  // to the target address is in range for the instruction.
  int64_t Offset = FrameSizeAdjust + LocalFrameOffset - BaseOffset;
  return TRI->isFrameOffsetLegal(&MI, BaseReg, Offset);
}

bool LocalStackSlotImpl::insertFrameReferenceRegisters(MachineFunction &Fn) {
  // Scan the function's instructions looking for frame index references.
  // For each, ask the target if it wants a virtual base register for it
  // based on what we can tell it about where the local will end up in the
  // stack frame. If it wants one, re-use a suitable one we've previously
  // allocated, or if there isn't one that fits the bill, allocate a new one
  // and ask the target to create a defining instruction for it.

  MachineFrameInfo &MFI = Fn.getFrameInfo();
  const TargetRegisterInfo *TRI = Fn.getSubtarget().getRegisterInfo();
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  bool StackGrowsDown =
    TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  // Collect all of the instructions in the block that reference
  // a frame index. Also store the frame index referenced to ease later
  // lookup. (For any insn that has more than one FI reference, we arbitrarily
  // choose the first one).
  SmallVector<FrameRef, 64> FrameReferenceInsns;

  unsigned Order = 0;

  for (MachineBasicBlock &BB : Fn) {
    for (MachineInstr &MI : BB) {
      // Debug value, stackmap and patchpoint instructions can't be out of
      // range, so they don't need any updates.
      if (MI.isDebugInstr() || MI.getOpcode() == TargetOpcode::STATEPOINT ||
          MI.getOpcode() == TargetOpcode::STACKMAP ||
          MI.getOpcode() == TargetOpcode::PATCHPOINT)
        continue;

      // For now, allocate the base register(s) within the basic block
      // where they're used, and don't try to keep them around outside
      // of that. It may be beneficial to try sharing them more broadly
      // than that, but the increased register pressure makes that a
      // tricky thing to balance. Investigate if re-materializing these
      // becomes an issue.
      for (const MachineOperand &MO : MI.operands()) {
        // Consider replacing all frame index operands that reference
        // an object allocated in the local block.
        if (MO.isFI()) {
          // Don't try this with values not in the local block.
          if (!MFI.isObjectPreAllocated(MO.getIndex()))
            break;
          int Idx = MO.getIndex();
          int64_t LocalOffset = LocalOffsets[Idx];
          if (!TRI->needsFrameBaseReg(&MI, LocalOffset))
            break;
          FrameReferenceInsns.push_back(FrameRef(&MI, LocalOffset, Idx, Order++));
          break;
        }
      }
    }
  }

  // Sort the frame references by local offset.
  // Use frame index as a tie-breaker in case MI's have the same offset.
  llvm::sort(FrameReferenceInsns);

  MachineBasicBlock *Entry = &Fn.front();

  Register BaseReg;
  int64_t BaseOffset = 0;

  // Loop through the frame references and allocate for them as necessary.
  for (int ref = 0, e = FrameReferenceInsns.size(); ref < e ; ++ref) {
    FrameRef &FR = FrameReferenceInsns[ref];
    MachineInstr &MI = *FR.getMachineInstr();
    int64_t LocalOffset = FR.getLocalOffset();
    int FrameIdx = FR.getFrameIndex();
    assert(MFI.isObjectPreAllocated(FrameIdx) &&
           "Only pre-allocated locals expected!");

    // We need to keep the references to the stack protector slot through frame
    // index operands so that it gets resolved by PEI rather than this pass.
    // This avoids accesses to the stack protector though virtual base
    // registers, and forces PEI to address it using fp/sp/bp.
    if (MFI.hasStackProtectorIndex() &&
        FrameIdx == MFI.getStackProtectorIndex())
      continue;

    LLVM_DEBUG(dbgs() << "Considering: " << MI);

    unsigned idx = 0;
    for (unsigned f = MI.getNumOperands(); idx != f; ++idx) {
      if (!MI.getOperand(idx).isFI())
        continue;

      if (FrameIdx == MI.getOperand(idx).getIndex())
        break;
    }

    assert(idx < MI.getNumOperands() && "Cannot find FI operand");

    int64_t Offset = 0;
    int64_t FrameSizeAdjust = StackGrowsDown ? MFI.getLocalFrameSize() : 0;

    LLVM_DEBUG(dbgs() << "  Replacing FI in: " << MI);

    // If we have a suitable base register available, use it; otherwise
    // create a new one. Note that any offset encoded in the
    // instruction itself will be taken into account by the target,
    // so we don't have to adjust for it here when reusing a base
    // register.
    if (BaseReg.isValid() &&
        lookupCandidateBaseReg(BaseReg, BaseOffset, FrameSizeAdjust,
                               LocalOffset, MI, TRI)) {
      LLVM_DEBUG(dbgs() << "  Reusing base register " << BaseReg << "\n");
      // We found a register to reuse.
      Offset = FrameSizeAdjust + LocalOffset - BaseOffset;
    } else {
      // No previously defined register was in range, so create a new one.
      int64_t InstrOffset = TRI->getFrameIndexInstrOffset(&MI, idx);

      int64_t CandBaseOffset = FrameSizeAdjust + LocalOffset + InstrOffset;

      // We'd like to avoid creating single-use virtual base registers.
      // Because the FrameRefs are in sorted order, and we've already
      // processed all FrameRefs before this one, just check whether or not
      // the next FrameRef will be able to reuse this new register. If not,
      // then don't bother creating it.
      if (ref + 1 >= e ||
          !lookupCandidateBaseReg(
              BaseReg, CandBaseOffset, FrameSizeAdjust,
              FrameReferenceInsns[ref + 1].getLocalOffset(),
              *FrameReferenceInsns[ref + 1].getMachineInstr(), TRI))
        continue;

      // Save the base offset.
      BaseOffset = CandBaseOffset;

      // Tell the target to insert the instruction to initialize
      // the base register.
      //            MachineBasicBlock::iterator InsertionPt = Entry->begin();
      BaseReg = TRI->materializeFrameBaseRegister(Entry, FrameIdx, InstrOffset);

      LLVM_DEBUG(dbgs() << "  Materialized base register at frame local offset "
                        << LocalOffset + InstrOffset
                        << " into " << printReg(BaseReg, TRI) << '\n');

      // The base register already includes any offset specified
      // by the instruction, so account for that so it doesn't get
      // applied twice.
      Offset = -InstrOffset;

      ++NumBaseRegisters;
    }
    assert(BaseReg && "Unable to allocate virtual base register!");

    // Modify the instruction to use the new base register rather
    // than the frame index operand.
    TRI->resolveFrameIndex(MI, BaseReg, Offset);
    LLVM_DEBUG(dbgs() << "Resolved: " << MI);

    ++NumReplacements;
  }

  return BaseReg.isValid();
}
