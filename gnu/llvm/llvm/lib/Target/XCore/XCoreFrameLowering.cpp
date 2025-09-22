//===-- XCoreFrameLowering.cpp - Frame info for XCore Target --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains XCore frame information that doesn't fit anywhere else
// cleanly...
//
//===----------------------------------------------------------------------===//

#include "XCoreFrameLowering.h"
#include "XCore.h"
#include "XCoreInstrInfo.h"
#include "XCoreMachineFunctionInfo.h"
#include "XCoreSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>

using namespace llvm;

static const unsigned FramePtr = XCore::R10;
static const int MaxImmU16 = (1<<16) - 1;

// helper functions. FIXME: Eliminate.
static inline bool isImmU6(unsigned val) {
  return val < (1 << 6);
}

static inline bool isImmU16(unsigned val) {
  return val < (1 << 16);
}

// Helper structure with compare function for handling stack slots.
namespace {
struct StackSlotInfo {
  int FI;
  int Offset;
  unsigned Reg;
  StackSlotInfo(int f, int o, int r) : FI(f), Offset(o), Reg(r){};
};
}  // end anonymous namespace

static bool CompareSSIOffset(const StackSlotInfo& a, const StackSlotInfo& b) {
  return a.Offset < b.Offset;
}

static void EmitDefCfaRegister(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               const DebugLoc &dl, const TargetInstrInfo &TII,
                               MachineFunction &MF, unsigned DRegNum) {
  unsigned CFIIndex = MF.addFrameInst(
      MCCFIInstruction::createDefCfaRegister(nullptr, DRegNum));
  BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);
}

static void EmitDefCfaOffset(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             const DebugLoc &dl, const TargetInstrInfo &TII,
                             int Offset) {
  MachineFunction &MF = *MBB.getParent();
  unsigned CFIIndex =
      MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, Offset));
  BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);
}

static void EmitCfiOffset(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, const DebugLoc &dl,
                          const TargetInstrInfo &TII, unsigned DRegNum,
                          int Offset) {
  MachineFunction &MF = *MBB.getParent();
  unsigned CFIIndex = MF.addFrameInst(
      MCCFIInstruction::createOffset(nullptr, DRegNum, Offset));
  BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex);
}

/// The SP register is moved in steps of 'MaxImmU16' towards the bottom of the
/// frame. During these steps, it may be necessary to spill registers.
/// IfNeededExtSP emits the necessary EXTSP instructions to move the SP only
/// as far as to make 'OffsetFromBottom' reachable using an STWSP_lru6.
/// \param OffsetFromTop the spill offset from the top of the frame.
/// \param [in,out] Adjusted the current SP offset from the top of the frame.
static void IfNeededExtSP(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, const DebugLoc &dl,
                          const TargetInstrInfo &TII, int OffsetFromTop,
                          int &Adjusted, int FrameSize, bool emitFrameMoves) {
  while (OffsetFromTop > Adjusted) {
    assert(Adjusted < FrameSize && "OffsetFromTop is beyond FrameSize");
    int remaining = FrameSize - Adjusted;
    int OpImm = (remaining > MaxImmU16) ? MaxImmU16 : remaining;
    int Opcode = isImmU6(OpImm) ? XCore::EXTSP_u6 : XCore::EXTSP_lu6;
    BuildMI(MBB, MBBI, dl, TII.get(Opcode)).addImm(OpImm);
    Adjusted += OpImm;
    if (emitFrameMoves)
      EmitDefCfaOffset(MBB, MBBI, dl, TII, Adjusted*4);
  }
}

/// The SP register is moved in steps of 'MaxImmU16' towards the top of the
/// frame. During these steps, it may be necessary to re-load registers.
/// IfNeededLDAWSP emits the necessary LDAWSP instructions to move the SP only
/// as far as to make 'OffsetFromTop' reachable using an LDAWSP_lru6.
/// \param OffsetFromTop the spill offset from the top of the frame.
/// \param [in,out] RemainingAdj the current SP offset from the top of the
/// frame.
static void IfNeededLDAWSP(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, const DebugLoc &dl,
                           const TargetInstrInfo &TII, int OffsetFromTop,
                           int &RemainingAdj) {
  while (OffsetFromTop < RemainingAdj - MaxImmU16) {
    assert(RemainingAdj && "OffsetFromTop is beyond FrameSize");
    int OpImm = (RemainingAdj > MaxImmU16) ? MaxImmU16 : RemainingAdj;
    int Opcode = isImmU6(OpImm) ? XCore::LDAWSP_ru6 : XCore::LDAWSP_lru6;
    BuildMI(MBB, MBBI, dl, TII.get(Opcode), XCore::SP).addImm(OpImm);
    RemainingAdj -= OpImm;
  }
}

/// Creates an ordered list of registers that are spilled
/// during the emitPrologue/emitEpilogue.
/// Registers are ordered according to their frame offset.
/// As offsets are negative, the largest offsets will be first.
static void GetSpillList(SmallVectorImpl<StackSlotInfo> &SpillList,
                         MachineFrameInfo &MFI, XCoreFunctionInfo *XFI,
                         bool fetchLR, bool fetchFP) {
  if (fetchLR) {
    int Offset = MFI.getObjectOffset(XFI->getLRSpillSlot());
    SpillList.push_back(StackSlotInfo(XFI->getLRSpillSlot(),
                                      Offset,
                                      XCore::LR));
  }
  if (fetchFP) {
    int Offset = MFI.getObjectOffset(XFI->getFPSpillSlot());
    SpillList.push_back(StackSlotInfo(XFI->getFPSpillSlot(),
                                      Offset,
                                      FramePtr));
  }
  llvm::sort(SpillList, CompareSSIOffset);
}

/// Creates an ordered list of EH info register 'spills'.
/// These slots are only used by the unwinder and calls to llvm.eh.return().
/// Registers are ordered according to their frame offset.
/// As offsets are negative, the largest offsets will be first.
static void GetEHSpillList(SmallVectorImpl<StackSlotInfo> &SpillList,
                           MachineFrameInfo &MFI, XCoreFunctionInfo *XFI,
                           const Constant *PersonalityFn,
                           const TargetLowering *TL) {
  assert(XFI->hasEHSpillSlot() && "There are no EH register spill slots");
  const int *EHSlot = XFI->getEHSpillSlot();
  SpillList.push_back(
      StackSlotInfo(EHSlot[0], MFI.getObjectOffset(EHSlot[0]),
                    TL->getExceptionPointerRegister(PersonalityFn)));
  SpillList.push_back(
      StackSlotInfo(EHSlot[0], MFI.getObjectOffset(EHSlot[1]),
                    TL->getExceptionSelectorRegister(PersonalityFn)));
  llvm::sort(SpillList, CompareSSIOffset);
}

static MachineMemOperand *getFrameIndexMMO(MachineBasicBlock &MBB,
                                           int FrameIndex,
                                           MachineMemOperand::Flags flags) {
  MachineFunction *MF = MBB.getParent();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FrameIndex), flags,
      MFI.getObjectSize(FrameIndex), MFI.getObjectAlign(FrameIndex));
  return MMO;
}


/// Restore clobbered registers with their spill slot value.
/// The SP will be adjusted at the same time, thus the SpillList must be ordered
/// with the largest (negative) offsets first.
static void RestoreSpillList(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             const DebugLoc &dl, const TargetInstrInfo &TII,
                             int &RemainingAdj,
                             SmallVectorImpl<StackSlotInfo> &SpillList) {
  for (unsigned i = 0, e = SpillList.size(); i != e; ++i) {
    assert(SpillList[i].Offset % 4 == 0 && "Misaligned stack offset");
    assert(SpillList[i].Offset <= 0 && "Unexpected positive stack offset");
    int OffsetFromTop = - SpillList[i].Offset/4;
    IfNeededLDAWSP(MBB, MBBI, dl, TII, OffsetFromTop, RemainingAdj);
    int Offset = RemainingAdj - OffsetFromTop;
    int Opcode = isImmU6(Offset) ? XCore::LDWSP_ru6 : XCore::LDWSP_lru6;
    BuildMI(MBB, MBBI, dl, TII.get(Opcode), SpillList[i].Reg)
      .addImm(Offset)
      .addMemOperand(getFrameIndexMMO(MBB, SpillList[i].FI,
                                      MachineMemOperand::MOLoad));
  }
}

//===----------------------------------------------------------------------===//
// XCoreFrameLowering:
//===----------------------------------------------------------------------===//

XCoreFrameLowering::XCoreFrameLowering(const XCoreSubtarget &sti)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown, Align(4), 0) {
  // Do nothing
}

bool XCoreFrameLowering::hasFP(const MachineFunction &MF) const {
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MF.getFrameInfo().hasVarSizedObjects();
}

void XCoreFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  const XCoreInstrInfo &TII = *MF.getSubtarget<XCoreSubtarget>().getInstrInfo();
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc dl;

  if (MFI.getMaxAlign() > getStackAlign())
    report_fatal_error("emitPrologue unsupported alignment: " +
                       Twine(MFI.getMaxAlign().value()));

  const AttributeList &PAL = MF.getFunction().getAttributes();
  if (PAL.hasAttrSomewhere(Attribute::Nest))
    BuildMI(MBB, MBBI, dl, TII.get(XCore::LDWSP_ru6), XCore::R11).addImm(0);
    // FIX: Needs addMemOperand() but can't use getFixedStack() or getStack().

  // Work out frame sizes.
  // We will adjust the SP in stages towards the final FrameSize.
  assert(MFI.getStackSize()%4 == 0 && "Misaligned frame size");
  const int FrameSize = MFI.getStackSize() / 4;
  int Adjusted = 0;

  bool saveLR = XFI->hasLRSpillSlot();
  bool UseENTSP = saveLR && FrameSize
                  && (MFI.getObjectOffset(XFI->getLRSpillSlot()) == 0);
  if (UseENTSP)
    saveLR = false;
  bool FP = hasFP(MF);
  bool emitFrameMoves = XCoreRegisterInfo::needsFrameMoves(MF);

  if (UseENTSP) {
    // Allocate space on the stack at the same time as saving LR.
    Adjusted = (FrameSize > MaxImmU16) ? MaxImmU16 : FrameSize;
    int Opcode = isImmU6(Adjusted) ? XCore::ENTSP_u6 : XCore::ENTSP_lu6;
    MBB.addLiveIn(XCore::LR);
    MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(Opcode));
    MIB.addImm(Adjusted);
    MIB->addRegisterKilled(XCore::LR, MF.getSubtarget().getRegisterInfo(),
                           true);
    if (emitFrameMoves) {
      EmitDefCfaOffset(MBB, MBBI, dl, TII, Adjusted*4);
      unsigned DRegNum = MRI->getDwarfRegNum(XCore::LR, true);
      EmitCfiOffset(MBB, MBBI, dl, TII, DRegNum, 0);
    }
  }

  // If necessary, save LR and FP to the stack, as we EXTSP.
  SmallVector<StackSlotInfo,2> SpillList;
  GetSpillList(SpillList, MFI, XFI, saveLR, FP);
  // We want the nearest (negative) offsets first, so reverse list.
  std::reverse(SpillList.begin(), SpillList.end());
  for (unsigned i = 0, e = SpillList.size(); i != e; ++i) {
    assert(SpillList[i].Offset % 4 == 0 && "Misaligned stack offset");
    assert(SpillList[i].Offset <= 0 && "Unexpected positive stack offset");
    int OffsetFromTop = - SpillList[i].Offset/4;
    IfNeededExtSP(MBB, MBBI, dl, TII, OffsetFromTop, Adjusted, FrameSize,
                  emitFrameMoves);
    int Offset = Adjusted - OffsetFromTop;
    int Opcode = isImmU6(Offset) ? XCore::STWSP_ru6 : XCore::STWSP_lru6;
    MBB.addLiveIn(SpillList[i].Reg);
    BuildMI(MBB, MBBI, dl, TII.get(Opcode))
      .addReg(SpillList[i].Reg, RegState::Kill)
      .addImm(Offset)
      .addMemOperand(getFrameIndexMMO(MBB, SpillList[i].FI,
                                      MachineMemOperand::MOStore));
    if (emitFrameMoves) {
      unsigned DRegNum = MRI->getDwarfRegNum(SpillList[i].Reg, true);
      EmitCfiOffset(MBB, MBBI, dl, TII, DRegNum, SpillList[i].Offset);
    }
  }

  // Complete any remaining Stack adjustment.
  IfNeededExtSP(MBB, MBBI, dl, TII, FrameSize, Adjusted, FrameSize,
                emitFrameMoves);
  assert(Adjusted==FrameSize && "IfNeededExtSP has not completed adjustment");

  if (FP) {
    // Set the FP from the SP.
    BuildMI(MBB, MBBI, dl, TII.get(XCore::LDAWSP_ru6), FramePtr).addImm(0);
    if (emitFrameMoves)
      EmitDefCfaRegister(MBB, MBBI, dl, TII, MF,
                         MRI->getDwarfRegNum(FramePtr, true));
  }

  if (emitFrameMoves) {
    // Frame moves for callee saved.
    for (const auto &SpillLabel : XFI->getSpillLabels()) {
      MachineBasicBlock::iterator Pos = SpillLabel.first;
      ++Pos;
      const CalleeSavedInfo &CSI = SpillLabel.second;
      int Offset = MFI.getObjectOffset(CSI.getFrameIdx());
      unsigned DRegNum = MRI->getDwarfRegNum(CSI.getReg(), true);
      EmitCfiOffset(MBB, Pos, dl, TII, DRegNum, Offset);
    }
    if (XFI->hasEHSpillSlot()) {
      // The unwinder requires stack slot & CFI offsets for the exception info.
      // We do not save/spill these registers.
      const Function *Fn = &MF.getFunction();
      const Constant *PersonalityFn =
          Fn->hasPersonalityFn() ? Fn->getPersonalityFn() : nullptr;
      SmallVector<StackSlotInfo, 2> SpillList;
      GetEHSpillList(SpillList, MFI, XFI, PersonalityFn,
                     MF.getSubtarget().getTargetLowering());
      assert(SpillList.size()==2 && "Unexpected SpillList size");
      EmitCfiOffset(MBB, MBBI, dl, TII,
                    MRI->getDwarfRegNum(SpillList[0].Reg, true),
                    SpillList[0].Offset);
      EmitCfiOffset(MBB, MBBI, dl, TII,
                    MRI->getDwarfRegNum(SpillList[1].Reg, true),
                    SpillList[1].Offset);
    }
  }
}

void XCoreFrameLowering::emitEpilogue(MachineFunction &MF,
                                     MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  const XCoreInstrInfo &TII = *MF.getSubtarget<XCoreSubtarget>().getInstrInfo();
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  DebugLoc dl = MBBI->getDebugLoc();
  unsigned RetOpcode = MBBI->getOpcode();

  // Work out frame sizes.
  // We will adjust the SP in stages towards the final FrameSize.
  int RemainingAdj = MFI.getStackSize();
  assert(RemainingAdj%4 == 0 && "Misaligned frame size");
  RemainingAdj /= 4;

  if (RetOpcode == XCore::EH_RETURN) {
    // 'Restore' the exception info the unwinder has placed into the stack
    // slots.
    const Function *Fn = &MF.getFunction();
    const Constant *PersonalityFn =
        Fn->hasPersonalityFn() ? Fn->getPersonalityFn() : nullptr;
    SmallVector<StackSlotInfo, 2> SpillList;
    GetEHSpillList(SpillList, MFI, XFI, PersonalityFn,
                   MF.getSubtarget().getTargetLowering());
    RestoreSpillList(MBB, MBBI, dl, TII, RemainingAdj, SpillList);

    // Return to the landing pad.
    Register EhStackReg = MBBI->getOperand(0).getReg();
    Register EhHandlerReg = MBBI->getOperand(1).getReg();
    BuildMI(MBB, MBBI, dl, TII.get(XCore::SETSP_1r)).addReg(EhStackReg);
    BuildMI(MBB, MBBI, dl, TII.get(XCore::BAU_1r)).addReg(EhHandlerReg);
    MBB.erase(MBBI);  // Erase the previous return instruction.
    return;
  }

  bool restoreLR = XFI->hasLRSpillSlot();
  bool UseRETSP = restoreLR && RemainingAdj
                  && (MFI.getObjectOffset(XFI->getLRSpillSlot()) == 0);
  if (UseRETSP)
    restoreLR = false;
  bool FP = hasFP(MF);

  if (FP) // Restore the stack pointer.
    BuildMI(MBB, MBBI, dl, TII.get(XCore::SETSP_1r)).addReg(FramePtr);

  // If necessary, restore LR and FP from the stack, as we EXTSP.
  SmallVector<StackSlotInfo,2> SpillList;
  GetSpillList(SpillList, MFI, XFI, restoreLR, FP);
  RestoreSpillList(MBB, MBBI, dl, TII, RemainingAdj, SpillList);

  if (RemainingAdj) {
    // Complete all but one of the remaining Stack adjustments.
    IfNeededLDAWSP(MBB, MBBI, dl, TII, 0, RemainingAdj);
    if (UseRETSP) {
      // Fold prologue into return instruction
      assert(RetOpcode == XCore::RETSP_u6
             || RetOpcode == XCore::RETSP_lu6);
      int Opcode = isImmU6(RemainingAdj) ? XCore::RETSP_u6 : XCore::RETSP_lu6;
      MachineInstrBuilder MIB = BuildMI(MBB, MBBI, dl, TII.get(Opcode))
                                  .addImm(RemainingAdj);
      for (unsigned i = 3, e = MBBI->getNumOperands(); i < e; ++i)
        MIB->addOperand(MBBI->getOperand(i)); // copy any variadic operands
      MBB.erase(MBBI);  // Erase the previous return instruction.
    } else {
      int Opcode = isImmU6(RemainingAdj) ? XCore::LDAWSP_ru6 :
                                           XCore::LDAWSP_lru6;
      BuildMI(MBB, MBBI, dl, TII.get(Opcode), XCore::SP).addImm(RemainingAdj);
      // Don't erase the return instruction.
    }
  } // else Don't erase the return instruction.
}

bool XCoreFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  XCoreFunctionInfo *XFI = MF->getInfo<XCoreFunctionInfo>();
  bool emitFrameMoves = XCoreRegisterInfo::needsFrameMoves(*MF);

  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    assert(Reg != XCore::LR && !(Reg == XCore::R10 && hasFP(*MF)) &&
           "LR & FP are always handled in emitPrologue");

    // Add the callee-saved register as live-in. It's killed at the spill.
    MBB.addLiveIn(Reg);
    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.storeRegToStackSlot(MBB, MI, Reg, true, I.getFrameIdx(), RC, TRI,
                            Register());
    if (emitFrameMoves) {
      auto Store = MI;
      --Store;
      XFI->getSpillLabels().push_back(std::make_pair(Store, I));
    }
  }
  return true;
}

bool XCoreFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  bool AtStart = MI == MBB.begin();
  MachineBasicBlock::iterator BeforeI = MI;
  if (!AtStart)
    --BeforeI;
  for (const CalleeSavedInfo &CSR : CSI) {
    Register Reg = CSR.getReg();
    assert(Reg != XCore::LR && !(Reg == XCore::R10 && hasFP(*MF)) &&
           "LR & FP are always handled in emitEpilogue");

    const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
    TII.loadRegFromStackSlot(MBB, MI, Reg, CSR.getFrameIdx(), RC, TRI,
                             Register());
    assert(MI != MBB.begin() &&
           "loadRegFromStackSlot didn't insert any code!");
    // Insert in reverse order.  loadRegFromStackSlot can insert multiple
    // instructions.
    if (AtStart)
      MI = MBB.begin();
    else {
      MI = BeforeI;
      ++MI;
    }
  }
  return true;
}

// This function eliminates ADJCALLSTACKDOWN,
// ADJCALLSTACKUP pseudo instructions
MachineBasicBlock::iterator XCoreFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const XCoreInstrInfo &TII = *MF.getSubtarget<XCoreSubtarget>().getInstrInfo();
  if (!hasReservedCallFrame(MF)) {
    // Turn the adjcallstackdown instruction into 'extsp <amt>' and the
    // adjcallstackup instruction into 'ldaw sp, sp[<amt>]'
    MachineInstr &Old = *I;
    uint64_t Amount = Old.getOperand(0).getImm();
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      Amount = alignTo(Amount, getStackAlign());

      assert(Amount%4 == 0);
      Amount /= 4;

      bool isU6 = isImmU6(Amount);
      if (!isU6 && !isImmU16(Amount)) {
        // FIX could emit multiple instructions in this case.
#ifndef NDEBUG
        errs() << "eliminateCallFramePseudoInstr size too big: "
               << Amount << "\n";
#endif
        llvm_unreachable(nullptr);
      }

      MachineInstr *New;
      if (Old.getOpcode() == XCore::ADJCALLSTACKDOWN) {
        int Opcode = isU6 ? XCore::EXTSP_u6 : XCore::EXTSP_lu6;
        New = BuildMI(MF, Old.getDebugLoc(), TII.get(Opcode)).addImm(Amount);
      } else {
        assert(Old.getOpcode() == XCore::ADJCALLSTACKUP);
        int Opcode = isU6 ? XCore::LDAWSP_ru6 : XCore::LDAWSP_lru6;
        New = BuildMI(MF, Old.getDebugLoc(), TII.get(Opcode), XCore::SP)
                  .addImm(Amount);
      }

      // Replace the pseudo instruction with a new instruction...
      MBB.insert(I, New);
    }
  }

  return MBB.erase(I);
}

void XCoreFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  bool LRUsed = MRI.isPhysRegModified(XCore::LR);

  if (!LRUsed && !MF.getFunction().isVarArg() &&
      MF.getFrameInfo().estimateStackSize(MF))
    // If we need to extend the stack it is more efficient to use entsp / retsp.
    // We force the LR to be saved so these instructions are used.
    LRUsed = true;

  if (MF.callsUnwindInit() || MF.callsEHReturn()) {
    // The unwinder expects to find spill slots for the exception info regs R0
    // & R1. These are used during llvm.eh.return() to 'restore' the exception
    // info. N.B. we do not spill or restore R0, R1 during normal operation.
    XFI->createEHSpillSlot(MF);
    // As we will  have a stack, we force the LR to be saved.
    LRUsed = true;
  }

  if (LRUsed) {
    // We will handle the LR in the prologue/epilogue
    // and allocate space on the stack ourselves.
    SavedRegs.reset(XCore::LR);
    XFI->createLRSpillSlot(MF);
  }

  if (hasFP(MF))
    // A callee save register is used to hold the FP.
    // This needs saving / restoring in the epilogue / prologue.
    XFI->createFPSpillSlot(MF);
}

void XCoreFrameLowering::
processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                    RegScavenger *RS) const {
  assert(RS && "requiresRegisterScavenging failed");
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterClass &RC = XCore::GRRegsRegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  XCoreFunctionInfo *XFI = MF.getInfo<XCoreFunctionInfo>();
  // Reserve slots close to SP or frame pointer for Scavenging spills.
  // When using SP for small frames, we don't need any scratch registers.
  // When using SP for large frames, we may need 2 scratch registers.
  // When using FP, for large or small frames, we may need 1 scratch register.
  unsigned Size = TRI.getSpillSize(RC);
  Align Alignment = TRI.getSpillAlign(RC);
  if (XFI->isLargeFrame(MF) || hasFP(MF))
    RS->addScavengingFrameIndex(MFI.CreateStackObject(Size, Alignment, false));
  if (XFI->isLargeFrame(MF) && !hasFP(MF))
    RS->addScavengingFrameIndex(MFI.CreateStackObject(Size, Alignment, false));
}
