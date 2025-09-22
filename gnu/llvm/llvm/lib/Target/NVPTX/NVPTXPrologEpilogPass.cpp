//===-- NVPTXPrologEpilogPass.cpp - NVPTX prolog/epilog inserter ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a copy of the generic LLVM PrologEpilogInserter pass, modified
// to remove unneeded functionality and to handle virtual registers. Most code
// here is a copy of PrologEpilogInserter.cpp.
//
//===----------------------------------------------------------------------===//

#include "NVPTX.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "nvptx-prolog-epilog"

namespace {
class NVPTXPrologEpilogPass : public MachineFunctionPass {
public:
  static char ID;
  NVPTXPrologEpilogPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  StringRef getPassName() const override { return "NVPTX Prolog Epilog Pass"; }

private:
  void calculateFrameObjectOffsets(MachineFunction &Fn);
};
}

MachineFunctionPass *llvm::createNVPTXPrologEpilogPass() {
  return new NVPTXPrologEpilogPass();
}

char NVPTXPrologEpilogPass::ID = 0;

bool NVPTXPrologEpilogPass::runOnMachineFunction(MachineFunction &MF) {
  const TargetSubtargetInfo &STI = MF.getSubtarget();
  const TargetFrameLowering &TFI = *STI.getFrameLowering();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  bool Modified = false;

  calculateFrameObjectOffsets(MF);

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        if (!MI.getOperand(i).isFI())
          continue;

        // Frame indices in debug values are encoded in a target independent
        // way with simply the frame index and offset rather than any
        // target-specific addressing mode.
        if (MI.isDebugValue()) {
          MachineOperand &Op = MI.getOperand(i);
          assert(
              MI.isDebugOperand(&Op) &&
              "Frame indices can only appear as a debug operand in a DBG_VALUE*"
              " machine instruction");
          Register Reg;
          auto Offset =
              TFI.getFrameIndexReference(MF, Op.getIndex(), Reg);
          Op.ChangeToRegister(Reg, /*isDef=*/false);
          const DIExpression *DIExpr = MI.getDebugExpression();
          if (MI.isNonListDebugValue()) {
            DIExpr = TRI.prependOffsetExpression(MI.getDebugExpression(), DIExpression::ApplyOffset, Offset);
          } else {
	    SmallVector<uint64_t, 3> Ops;
            TRI.getOffsetOpcodes(Offset, Ops);
            unsigned OpIdx = MI.getDebugOperandIndex(&Op);
            DIExpr = DIExpression::appendOpsToArg(DIExpr, Ops, OpIdx);
          }
          MI.getDebugExpressionOp().setMetadata(DIExpr);
          continue;
        }

        TRI.eliminateFrameIndex(MI, 0, i, nullptr);
        Modified = true;
      }
    }
  }

  // Add function prolog/epilog
  TFI.emitPrologue(MF, MF.front());

  for (MachineFunction::iterator I = MF.begin(), E = MF.end(); I != E; ++I) {
    // If last instruction is a return instruction, add an epilogue
    if (I->isReturnBlock())
      TFI.emitEpilogue(MF, *I);
  }

  return Modified;
}

/// AdjustStackOffset - Helper function used to adjust the stack frame offset.
static inline void AdjustStackOffset(MachineFrameInfo &MFI, int FrameIdx,
                                     bool StackGrowsDown, int64_t &Offset,
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

  if (StackGrowsDown) {
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << -Offset
                      << "]\n");
    MFI.setObjectOffset(FrameIdx, -Offset); // Set the computed offset
  } else {
    LLVM_DEBUG(dbgs() << "alloc FI(" << FrameIdx << ") at SP[" << Offset
                      << "]\n");
    MFI.setObjectOffset(FrameIdx, Offset);
    Offset += MFI.getObjectSize(FrameIdx);
  }
}

void
NVPTXPrologEpilogPass::calculateFrameObjectOffsets(MachineFunction &Fn) {
  const TargetFrameLowering &TFI = *Fn.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *RegInfo = Fn.getSubtarget().getRegisterInfo();

  bool StackGrowsDown =
    TFI.getStackGrowthDirection() == TargetFrameLowering::StackGrowsDown;

  // Loop over all of the stack objects, assigning sequential addresses...
  MachineFrameInfo &MFI = Fn.getFrameInfo();

  // Start at the beginning of the local area.
  // The Offset is the distance from the stack top in the direction
  // of stack growth -- so it's always nonnegative.
  int LocalAreaOffset = TFI.getOffsetOfLocalArea();
  if (StackGrowsDown)
    LocalAreaOffset = -LocalAreaOffset;
  assert(LocalAreaOffset >= 0
         && "Local area offset should be in direction of stack growth");
  int64_t Offset = LocalAreaOffset;

  // If there are fixed sized objects that are preallocated in the local area,
  // non-fixed objects can't be allocated right at the start of local area.
  // We currently don't support filling in holes in between fixed sized
  // objects, so we adjust 'Offset' to point to the end of last fixed sized
  // preallocated object.
  for (int i = MFI.getObjectIndexBegin(); i != 0; ++i) {
    int64_t FixedOff;
    if (StackGrowsDown) {
      // The maximum distance from the stack pointer is at lower address of
      // the object -- which is given by offset. For down growing stack
      // the offset is negative, so we negate the offset to get the distance.
      FixedOff = -MFI.getObjectOffset(i);
    } else {
      // The maximum distance from the start pointer is at the upper
      // address of the object.
      FixedOff = MFI.getObjectOffset(i) + MFI.getObjectSize(i);
    }
    if (FixedOff > Offset) Offset = FixedOff;
  }

  // NOTE: We do not have a call stack

  Align MaxAlign = MFI.getMaxAlign();

  // No scavenger

  // FIXME: Once this is working, then enable flag will change to a target
  // check for whether the frame is large enough to want to use virtual
  // frame index registers. Functions which don't want/need this optimization
  // will continue to use the existing code path.
  if (MFI.getUseLocalStackAllocationBlock()) {
    Align Alignment = MFI.getLocalFrameMaxAlign();

    // Adjust to alignment boundary.
    Offset = alignTo(Offset, Alignment);

    LLVM_DEBUG(dbgs() << "Local frame base offset: " << Offset << "\n");

    // Resolve offsets for objects in the local block.
    for (unsigned i = 0, e = MFI.getLocalFrameObjectCount(); i != e; ++i) {
      std::pair<int, int64_t> Entry = MFI.getLocalFrameObjectMap(i);
      int64_t FIOffset = (StackGrowsDown ? -Offset : Offset) + Entry.second;
      LLVM_DEBUG(dbgs() << "alloc FI(" << Entry.first << ") at SP[" << FIOffset
                        << "]\n");
      MFI.setObjectOffset(Entry.first, FIOffset);
    }
    // Allocate the local block
    Offset += MFI.getLocalFrameSize();

    MaxAlign = std::max(Alignment, MaxAlign);
  }

  // No stack protector

  // Then assign frame offsets to stack objects that are not used to spill
  // callee saved registers.
  for (unsigned i = 0, e = MFI.getObjectIndexEnd(); i != e; ++i) {
    if (MFI.isObjectPreAllocated(i) &&
        MFI.getUseLocalStackAllocationBlock())
      continue;
    if (MFI.isDeadObjectIndex(i))
      continue;

    AdjustStackOffset(MFI, i, StackGrowsDown, Offset, MaxAlign);
  }

  // No scavenger

  if (!TFI.targetHandlesStackFrameRounding()) {
    // If we have reserved argument space for call sites in the function
    // immediately on entry to the current function, count it as part of the
    // overall stack size.
    if (MFI.adjustsStack() && TFI.hasReservedCallFrame(Fn))
      Offset += MFI.getMaxCallFrameSize();

    // Round up the size to a multiple of the alignment.  If the function has
    // any calls or alloca's, align to the target's StackAlignment value to
    // ensure that the callee's frame or the alloca data is suitably aligned;
    // otherwise, for leaf functions, align to the TransientStackAlignment
    // value.
    Align StackAlign;
    if (MFI.adjustsStack() || MFI.hasVarSizedObjects() ||
        (RegInfo->hasStackRealignment(Fn) && MFI.getObjectIndexEnd() != 0))
      StackAlign = TFI.getStackAlign();
    else
      StackAlign = TFI.getTransientStackAlign();

    // If the frame pointer is eliminated, all frame offsets will be relative to
    // SP not FP. Align to MaxAlign so this works.
    Offset = alignTo(Offset, std::max(StackAlign, MaxAlign));
  }

  // Update frame info to pretend that this is part of the stack...
  int64_t StackSize = Offset - LocalAreaOffset;
  MFI.setStackSize(StackSize);
}
