//===-- MachineFrameInfo.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Implements MachineFrameInfo that manages the stack frame.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFrameInfo.h"

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

#define DEBUG_TYPE "codegen"

using namespace llvm;

void MachineFrameInfo::ensureMaxAlignment(Align Alignment) {
  if (!StackRealignable)
    assert(Alignment <= StackAlignment &&
           "For targets without stack realignment, Alignment is out of limit!");
  if (MaxAlignment < Alignment)
    MaxAlignment = Alignment;
}

/// Clamp the alignment if requested and emit a warning.
static inline Align clampStackAlignment(bool ShouldClamp, Align Alignment,
                                        Align StackAlignment) {
  if (!ShouldClamp || Alignment <= StackAlignment)
    return Alignment;
  LLVM_DEBUG(dbgs() << "Warning: requested alignment " << DebugStr(Alignment)
                    << " exceeds the stack alignment "
                    << DebugStr(StackAlignment)
                    << " when stack realignment is off" << '\n');
  return StackAlignment;
}

int MachineFrameInfo::CreateStackObject(uint64_t Size, Align Alignment,
                                        bool IsSpillSlot,
                                        const AllocaInst *Alloca,
                                        uint8_t StackID) {
  assert(Size != 0 && "Cannot allocate zero size stack objects!");
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.push_back(StackObject(Size, Alignment, 0, false, IsSpillSlot, Alloca,
                                !IsSpillSlot, StackID));
  int Index = (int)Objects.size() - NumFixedObjects - 1;
  assert(Index >= 0 && "Bad frame index!");
  if (contributesToMaxAlignment(StackID))
    ensureMaxAlignment(Alignment);
  return Index;
}

int MachineFrameInfo::CreateSpillStackObject(uint64_t Size, Align Alignment) {
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  CreateStackObject(Size, Alignment, true);
  int Index = (int)Objects.size() - NumFixedObjects - 1;
  ensureMaxAlignment(Alignment);
  return Index;
}

int MachineFrameInfo::CreateVariableSizedObject(Align Alignment,
                                                const AllocaInst *Alloca) {
  HasVarSizedObjects = true;
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.push_back(StackObject(0, Alignment, 0, false, false, Alloca, true));
  ensureMaxAlignment(Alignment);
  return (int)Objects.size()-NumFixedObjects-1;
}

int MachineFrameInfo::CreateFixedObject(uint64_t Size, int64_t SPOffset,
                                        bool IsImmutable, bool IsAliased) {
  assert(Size != 0 && "Cannot allocate zero size fixed stack objects!");
  // The alignment of the frame index can be determined from its offset from
  // the incoming frame position.  If the frame object is at offset 32 and
  // the stack is guaranteed to be 16-byte aligned, then we know that the
  // object is 16-byte aligned. Note that unlike the non-fixed case, if the
  // stack needs realignment, we can't assume that the stack will in fact be
  // aligned.
  Align Alignment =
      commonAlignment(ForcedRealign ? Align(1) : StackAlignment, SPOffset);
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.insert(Objects.begin(),
                 StackObject(Size, Alignment, SPOffset, IsImmutable,
                             /*IsSpillSlot=*/false, /*Alloca=*/nullptr,
                             IsAliased));
  return -++NumFixedObjects;
}

int MachineFrameInfo::CreateFixedSpillStackObject(uint64_t Size,
                                                  int64_t SPOffset,
                                                  bool IsImmutable) {
  Align Alignment =
      commonAlignment(ForcedRealign ? Align(1) : StackAlignment, SPOffset);
  Alignment = clampStackAlignment(!StackRealignable, Alignment, StackAlignment);
  Objects.insert(Objects.begin(),
                 StackObject(Size, Alignment, SPOffset, IsImmutable,
                             /*IsSpillSlot=*/true, /*Alloca=*/nullptr,
                             /*IsAliased=*/false));
  return -++NumFixedObjects;
}

BitVector MachineFrameInfo::getPristineRegs(const MachineFunction &MF) const {
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  BitVector BV(TRI->getNumRegs());

  // Before CSI is calculated, no registers are considered pristine. They can be
  // freely used and PEI will make sure they are saved.
  if (!isCalleeSavedInfoValid())
    return BV;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (const MCPhysReg *CSR = MRI.getCalleeSavedRegs(); CSR && *CSR;
       ++CSR)
    BV.set(*CSR);

  // Saved CSRs are not pristine.
  for (const auto &I : getCalleeSavedInfo())
    for (MCPhysReg S : TRI->subregs_inclusive(I.getReg()))
      BV.reset(S);

  return BV;
}

uint64_t MachineFrameInfo::estimateStackSize(const MachineFunction &MF) const {
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  Align MaxAlign = getMaxAlign();
  int64_t Offset = 0;

  // This code is very, very similar to PEI::calculateFrameObjectOffsets().
  // It really should be refactored to share code. Until then, changes
  // should keep in mind that there's tight coupling between the two.

  for (int i = getObjectIndexBegin(); i != 0; ++i) {
    // Only estimate stack size of default stack.
    if (getStackID(i) != TargetStackID::Default)
      continue;
    int64_t FixedOff = -getObjectOffset(i);
    if (FixedOff > Offset) Offset = FixedOff;
  }
  for (unsigned i = 0, e = getObjectIndexEnd(); i != e; ++i) {
    // Only estimate stack size of live objects on default stack.
    if (isDeadObjectIndex(i) || getStackID(i) != TargetStackID::Default)
      continue;
    Offset += getObjectSize(i);
    Align Alignment = getObjectAlign(i);
    // Adjust to alignment boundary
    Offset = alignTo(Offset, Alignment);

    MaxAlign = std::max(Alignment, MaxAlign);
  }

  if (adjustsStack() && TFI->hasReservedCallFrame(MF))
    Offset += getMaxCallFrameSize();

  // Round up the size to a multiple of the alignment.  If the function has
  // any calls or alloca's, align to the target's StackAlignment value to
  // ensure that the callee's frame or the alloca data is suitably aligned;
  // otherwise, for leaf functions, align to the TransientStackAlignment
  // value.
  Align StackAlign;
  if (adjustsStack() || hasVarSizedObjects() ||
      (RegInfo->hasStackRealignment(MF) && getObjectIndexEnd() != 0))
    StackAlign = TFI->getStackAlign();
  else
    StackAlign = TFI->getTransientStackAlign();

  // If the frame pointer is eliminated, all frame offsets will be relative to
  // SP not FP. Align to MaxAlign so this works.
  StackAlign = std::max(StackAlign, MaxAlign);
  return alignTo(Offset, StackAlign);
}

void MachineFrameInfo::computeMaxCallFrameSize(
    MachineFunction &MF, std::vector<MachineBasicBlock::iterator> *FrameSDOps) {
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  unsigned FrameSetupOpcode = TII.getCallFrameSetupOpcode();
  unsigned FrameDestroyOpcode = TII.getCallFrameDestroyOpcode();
  assert(FrameSetupOpcode != ~0u && FrameDestroyOpcode != ~0u &&
         "Can only compute MaxCallFrameSize if Setup/Destroy opcode are known");

  MaxCallFrameSize = 0;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opcode = MI.getOpcode();
      if (Opcode == FrameSetupOpcode || Opcode == FrameDestroyOpcode) {
        uint64_t Size = TII.getFrameSize(MI);
        MaxCallFrameSize = std::max(MaxCallFrameSize, Size);
        if (FrameSDOps != nullptr)
          FrameSDOps->push_back(&MI);
      }
    }
  }
}

void MachineFrameInfo::print(const MachineFunction &MF, raw_ostream &OS) const{
  if (Objects.empty()) return;

  const TargetFrameLowering *FI = MF.getSubtarget().getFrameLowering();
  int ValOffset = (FI ? FI->getOffsetOfLocalArea() : 0);

  OS << "Frame Objects:\n";

  for (unsigned i = 0, e = Objects.size(); i != e; ++i) {
    const StackObject &SO = Objects[i];
    OS << "  fi#" << (int)(i-NumFixedObjects) << ": ";

    if (SO.StackID != 0)
      OS << "id=" << static_cast<unsigned>(SO.StackID) << ' ';

    if (SO.Size == ~0ULL) {
      OS << "dead\n";
      continue;
    }
    if (SO.Size == 0)
      OS << "variable sized";
    else
      OS << "size=" << SO.Size;
    OS << ", align=" << SO.Alignment.value();

    if (i < NumFixedObjects)
      OS << ", fixed";
    if (i < NumFixedObjects || SO.SPOffset != -1) {
      int64_t Off = SO.SPOffset - ValOffset;
      OS << ", at location [SP";
      if (Off > 0)
        OS << "+" << Off;
      else if (Off < 0)
        OS << Off;
      OS << "]";
    }
    OS << "\n";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineFrameInfo::dump(const MachineFunction &MF) const {
  print(MF, dbgs());
}
#endif
