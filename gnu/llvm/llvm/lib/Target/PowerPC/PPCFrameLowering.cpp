//===-- PPCFrameLowering.cpp - PPC Frame Information ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the PPC implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "PPCFrameLowering.h"
#include "MCTargetDesc/PPCPredicates.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCReturnProtectorLowering.h"  
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/Function.h"
#include "llvm/Target/TargetOptions.h"

using namespace llvm;

#define DEBUG_TYPE "framelowering"
STATISTIC(NumPESpillVSR, "Number of spills to vector in prologue");
STATISTIC(NumPEReloadVSR, "Number of reloads from vector in epilogue");
STATISTIC(NumPrologProbed, "Number of prologues probed");

static cl::opt<bool>
EnablePEVectorSpills("ppc-enable-pe-vector-spills",
                     cl::desc("Enable spills in prologue to vector registers."),
                     cl::init(false), cl::Hidden);

static unsigned computeReturnSaveOffset(const PPCSubtarget &STI) {
  if (STI.isAIXABI())
    return STI.isPPC64() ? 16 : 8;
  // SVR4 ABI:
  return STI.isPPC64() ? 16 : 4;
}

static unsigned computeTOCSaveOffset(const PPCSubtarget &STI) {
  if (STI.isAIXABI())
    return STI.isPPC64() ? 40 : 20;
  return STI.isELFv2ABI() ? 24 : 40;
}

static unsigned computeFramePointerSaveOffset(const PPCSubtarget &STI) {
  // First slot in the general register save area.
  return STI.isPPC64() ? -8U : -4U;
}

static unsigned computeLinkageSize(const PPCSubtarget &STI) {
  if (STI.isAIXABI() || STI.isPPC64())
    return (STI.isELFv2ABI() ? 4 : 6) * (STI.isPPC64() ? 8 : 4);

  // 32-bit SVR4 ABI:
  return 8;
}

static unsigned computeBasePointerSaveOffset(const PPCSubtarget &STI) {
  // Third slot in the general purpose register save area.
  if (STI.is32BitELFABI() && STI.getTargetMachine().isPositionIndependent())
    return -12U;

  // Second slot in the general purpose register save area.
  return STI.isPPC64() ? -16U : -8U;
}

static unsigned computeCRSaveOffset(const PPCSubtarget &STI) {
  return (STI.isAIXABI() && !STI.isPPC64()) ? 4 : 8;
}

PPCFrameLowering::PPCFrameLowering(const PPCSubtarget &STI)
    : TargetFrameLowering(TargetFrameLowering::StackGrowsDown,
                          STI.getPlatformStackAlignment(), 0),
      Subtarget(STI), ReturnSaveOffset(computeReturnSaveOffset(Subtarget)),
      TOCSaveOffset(computeTOCSaveOffset(Subtarget)),
      FramePointerSaveOffset(computeFramePointerSaveOffset(Subtarget)),
      LinkageSize(computeLinkageSize(Subtarget)),
      BasePointerSaveOffset(computeBasePointerSaveOffset(Subtarget)),
      CRSaveOffset(computeCRSaveOffset(Subtarget)) {}

// With the SVR4 ABI, callee-saved registers have fixed offsets on the stack.
const PPCFrameLowering::SpillSlot *PPCFrameLowering::getCalleeSavedSpillSlots(
    unsigned &NumEntries) const {

// Floating-point register save area offsets.
#define CALLEE_SAVED_FPRS \
      {PPC::F31, -8},     \
      {PPC::F30, -16},    \
      {PPC::F29, -24},    \
      {PPC::F28, -32},    \
      {PPC::F27, -40},    \
      {PPC::F26, -48},    \
      {PPC::F25, -56},    \
      {PPC::F24, -64},    \
      {PPC::F23, -72},    \
      {PPC::F22, -80},    \
      {PPC::F21, -88},    \
      {PPC::F20, -96},    \
      {PPC::F19, -104},   \
      {PPC::F18, -112},   \
      {PPC::F17, -120},   \
      {PPC::F16, -128},   \
      {PPC::F15, -136},   \
      {PPC::F14, -144}

// 32-bit general purpose register save area offsets shared by ELF and
// AIX. AIX has an extra CSR with r13.
#define CALLEE_SAVED_GPRS32 \
      {PPC::R31, -4},       \
      {PPC::R30, -8},       \
      {PPC::R29, -12},      \
      {PPC::R28, -16},      \
      {PPC::R27, -20},      \
      {PPC::R26, -24},      \
      {PPC::R25, -28},      \
      {PPC::R24, -32},      \
      {PPC::R23, -36},      \
      {PPC::R22, -40},      \
      {PPC::R21, -44},      \
      {PPC::R20, -48},      \
      {PPC::R19, -52},      \
      {PPC::R18, -56},      \
      {PPC::R17, -60},      \
      {PPC::R16, -64},      \
      {PPC::R15, -68},      \
      {PPC::R14, -72}

// 64-bit general purpose register save area offsets.
#define CALLEE_SAVED_GPRS64 \
      {PPC::X31, -8},       \
      {PPC::X30, -16},      \
      {PPC::X29, -24},      \
      {PPC::X28, -32},      \
      {PPC::X27, -40},      \
      {PPC::X26, -48},      \
      {PPC::X25, -56},      \
      {PPC::X24, -64},      \
      {PPC::X23, -72},      \
      {PPC::X22, -80},      \
      {PPC::X21, -88},      \
      {PPC::X20, -96},      \
      {PPC::X19, -104},     \
      {PPC::X18, -112},     \
      {PPC::X17, -120},     \
      {PPC::X16, -128},     \
      {PPC::X15, -136},     \
      {PPC::X14, -144}

// Vector register save area offsets.
#define CALLEE_SAVED_VRS \
      {PPC::V31, -16},   \
      {PPC::V30, -32},   \
      {PPC::V29, -48},   \
      {PPC::V28, -64},   \
      {PPC::V27, -80},   \
      {PPC::V26, -96},   \
      {PPC::V25, -112},  \
      {PPC::V24, -128},  \
      {PPC::V23, -144},  \
      {PPC::V22, -160},  \
      {PPC::V21, -176},  \
      {PPC::V20, -192}

  // Note that the offsets here overlap, but this is fixed up in
  // processFunctionBeforeFrameFinalized.

  static const SpillSlot ELFOffsets32[] = {
      CALLEE_SAVED_FPRS,
      CALLEE_SAVED_GPRS32,

      // CR save area offset.  We map each of the nonvolatile CR fields
      // to the slot for CR2, which is the first of the nonvolatile CR
      // fields to be assigned, so that we only allocate one save slot.
      // See PPCRegisterInfo::hasReservedSpillSlot() for more information.
      {PPC::CR2, -4},

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},

      CALLEE_SAVED_VRS,

      // SPE register save area (overlaps Vector save area).
      {PPC::S31, -8},
      {PPC::S30, -16},
      {PPC::S29, -24},
      {PPC::S28, -32},
      {PPC::S27, -40},
      {PPC::S26, -48},
      {PPC::S25, -56},
      {PPC::S24, -64},
      {PPC::S23, -72},
      {PPC::S22, -80},
      {PPC::S21, -88},
      {PPC::S20, -96},
      {PPC::S19, -104},
      {PPC::S18, -112},
      {PPC::S17, -120},
      {PPC::S16, -128},
      {PPC::S15, -136},
      {PPC::S14, -144}};

  static const SpillSlot ELFOffsets64[] = {
      CALLEE_SAVED_FPRS,
      CALLEE_SAVED_GPRS64,

      // VRSAVE save area offset.
      {PPC::VRSAVE, -4},
      CALLEE_SAVED_VRS
  };

  static const SpillSlot AIXOffsets32[] = {CALLEE_SAVED_FPRS,
                                           CALLEE_SAVED_GPRS32,
                                           // Add AIX's extra CSR.
                                           {PPC::R13, -76},
                                           CALLEE_SAVED_VRS};

  static const SpillSlot AIXOffsets64[] = {
      CALLEE_SAVED_FPRS, CALLEE_SAVED_GPRS64, CALLEE_SAVED_VRS};

  if (Subtarget.is64BitELFABI()) {
    NumEntries = std::size(ELFOffsets64);
    return ELFOffsets64;
  }

  if (Subtarget.is32BitELFABI()) {
    NumEntries = std::size(ELFOffsets32);
    return ELFOffsets32;
  }

  assert(Subtarget.isAIXABI() && "Unexpected ABI.");

  if (Subtarget.isPPC64()) {
    NumEntries = std::size(AIXOffsets64);
    return AIXOffsets64;
  }

  NumEntries = std::size(AIXOffsets32);
  return AIXOffsets32;
}

static bool spillsCR(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->isCRSpilled();
}

static bool hasSpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasSpills();
}

static bool hasNonRISpills(const MachineFunction &MF) {
  const PPCFunctionInfo *FuncInfo = MF.getInfo<PPCFunctionInfo>();
  return FuncInfo->hasNonRISpills();
}

/// MustSaveLR - Return true if this function requires that we save the LR
/// register onto the stack in the prolog and restore it in the epilog of the
/// function.
static bool MustSaveLR(const MachineFunction &MF, unsigned LR) {
  const PPCFunctionInfo *MFI = MF.getInfo<PPCFunctionInfo>();

  // We need a save/restore of LR if there is any def of LR (which is
  // defined by calls, including the PIC setup sequence), or if there is
  // some use of the LR stack slot (e.g. for builtin_return_address).
  // (LR comes in 32 and 64 bit versions.)
  MachineRegisterInfo::def_iterator RI = MF.getRegInfo().def_begin(LR);
  return RI !=MF.getRegInfo().def_end() || MFI->isLRStoreRequired();
}

/// determineFrameLayoutAndUpdate - Determine the size of the frame and maximum
/// call frame size. Update the MachineFunction object with the stack size.
uint64_t
PPCFrameLowering::determineFrameLayoutAndUpdate(MachineFunction &MF,
                                                bool UseEstimate) const {
  unsigned NewMaxCallFrameSize = 0;
  uint64_t FrameSize = determineFrameLayout(MF, UseEstimate,
                                            &NewMaxCallFrameSize);
  MF.getFrameInfo().setStackSize(FrameSize);
  MF.getFrameInfo().setMaxCallFrameSize(NewMaxCallFrameSize);
  return FrameSize;
}

/// determineFrameLayout - Determine the size of the frame and maximum call
/// frame size.
uint64_t
PPCFrameLowering::determineFrameLayout(const MachineFunction &MF,
                                       bool UseEstimate,
                                       unsigned *NewMaxCallFrameSize) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();

  // Get the number of bytes to allocate from the FrameInfo
  uint64_t FrameSize =
    UseEstimate ? MFI.estimateStackSize(MF) : MFI.getStackSize();

  // Get stack alignments. The frame must be aligned to the greatest of these:
  Align TargetAlign = getStackAlign(); // alignment required per the ABI
  Align MaxAlign = MFI.getMaxAlign();  // algmt required by data in frame
  Align Alignment = std::max(TargetAlign, MaxAlign);

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  unsigned LR = RegInfo->getRARegister();
  bool DisableRedZone = MF.getFunction().hasFnAttribute(Attribute::NoRedZone);
  bool CanUseRedZone = !MFI.hasVarSizedObjects() && // No dynamic alloca.
                       !MFI.adjustsStack() &&       // No calls.
                       !MustSaveLR(MF, LR) &&       // No need to save LR.
                       !FI->mustSaveTOC() &&        // No need to save TOC.
                       !RegInfo->hasBasePointer(MF) && // No special alignment.
                       !MFI.isFrameAddressTaken();

  // Note: for PPC32 SVR4ABI, we can still generate stackless
  // code if all local vars are reg-allocated.
  bool FitsInRedZone = FrameSize <= Subtarget.getRedZoneSize();

  // Check whether we can skip adjusting the stack pointer (by using red zone)
  if (!DisableRedZone && CanUseRedZone && FitsInRedZone) {
    // No need for frame
    return 0;
  }

  // Get the maximum call frame size of all the calls.
  unsigned maxCallFrameSize = MFI.getMaxCallFrameSize();

  // Maximum call frame needs to be at least big enough for linkage area.
  unsigned minCallFrameSize = getLinkageSize();
  maxCallFrameSize = std::max(maxCallFrameSize, minCallFrameSize);

  // If we have dynamic alloca then maxCallFrameSize needs to be aligned so
  // that allocations will be aligned.
  if (MFI.hasVarSizedObjects())
    maxCallFrameSize = alignTo(maxCallFrameSize, Alignment);

  // Update the new max call frame size if the caller passes in a valid pointer.
  if (NewMaxCallFrameSize)
    *NewMaxCallFrameSize = maxCallFrameSize;

  // Include call frame size in total.
  FrameSize += maxCallFrameSize;

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, Alignment);

  return FrameSize;
}

// hasFP - Return true if the specified function actually has a dedicated frame
// pointer register.
bool PPCFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  // FIXME: This is pretty much broken by design: hasFP() might be called really
  // early, before the stack layout was calculated and thus hasFP() might return
  // true or false here depending on the time of call.
  return (MFI.getStackSize()) && needsFP(MF);
}

// needsFP - Return true if the specified function should have a dedicated frame
// pointer register.  This is true if the function has variable sized allocas or
// if frame pointer elimination is disabled.
bool PPCFrameLowering::needsFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Naked functions have no stack frame pushed, so we don't have a frame
  // pointer.
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return false;

  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         MFI.hasVarSizedObjects() || MFI.hasStackMap() || MFI.hasPatchPoint() ||
         MF.exposesReturnsTwice() ||
         (MF.getTarget().Options.GuaranteedTailCallOpt &&
          MF.getInfo<PPCFunctionInfo>()->hasFastCall());
}

void PPCFrameLowering::replaceFPWithRealFP(MachineFunction &MF) const {
  // When there is dynamic alloca in this function, we can not use the frame
  // pointer X31/R31 for the frameaddress lowering. In this case, only X1/R1
  // always points to the backchain.
  bool is31 = needsFP(MF) && !MF.getFrameInfo().hasVarSizedObjects();
  unsigned FPReg  = is31 ? PPC::R31 : PPC::R1;
  unsigned FP8Reg = is31 ? PPC::X31 : PPC::X1;

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  bool HasBP = RegInfo->hasBasePointer(MF);
  unsigned BPReg  = HasBP ? (unsigned) RegInfo->getBaseRegister(MF) : FPReg;
  unsigned BP8Reg = HasBP ? (unsigned) PPC::X30 : FP8Reg;

  for (MachineBasicBlock &MBB : MF)
    for (MachineBasicBlock::iterator MBBI = MBB.end(); MBBI != MBB.begin();) {
      --MBBI;
      for (MachineOperand &MO : MBBI->operands()) {
        if (!MO.isReg())
          continue;

        switch (MO.getReg()) {
        case PPC::FP:
          MO.setReg(FPReg);
          break;
        case PPC::FP8:
          MO.setReg(FP8Reg);
          break;
        case PPC::BP:
          MO.setReg(BPReg);
          break;
        case PPC::BP8:
          MO.setReg(BP8Reg);
          break;

        }
      }
    }
}

/*  This function will do the following:
    - If MBB is an entry or exit block, set SR1 and SR2 to R0 and R12
      respectively (defaults recommended by the ABI) and return true
    - If MBB is not an entry block, initialize the register scavenger and look
      for available registers.
    - If the defaults (R0/R12) are available, return true
    - If TwoUniqueRegsRequired is set to true, it looks for two unique
      registers. Otherwise, look for a single available register.
      - If the required registers are found, set SR1 and SR2 and return true.
      - If the required registers are not found, set SR2 or both SR1 and SR2 to
        PPC::NoRegister and return false.

    Note that if both SR1 and SR2 are valid parameters and TwoUniqueRegsRequired
    is not set, this function will attempt to find two different registers, but
    still return true if only one register is available (and set SR1 == SR2).
*/
bool
PPCFrameLowering::findScratchRegister(MachineBasicBlock *MBB,
                                      bool UseAtEnd,
                                      bool TwoUniqueRegsRequired,
                                      Register *SR1,
                                      Register *SR2) const {
  RegScavenger RS;
  Register R0 =  Subtarget.isPPC64() ? PPC::X0 : PPC::R0;
  Register R12 = Subtarget.isPPC64() ? PPC::X12 : PPC::R12;

  // Set the defaults for the two scratch registers.
  if (SR1)
    *SR1 = R0;

  if (SR2) {
    assert (SR1 && "Asking for the second scratch register but not the first?");
    *SR2 = R12;
  }

  // If MBB is an entry or exit block, use R0 and R12 as the scratch registers.
  if ((UseAtEnd && MBB->isReturnBlock()) ||
      (!UseAtEnd && (&MBB->getParent()->front() == MBB)))
    return true;

  if (UseAtEnd) {
    // The scratch register will be used before the first terminator (or at the
    // end of the block if there are no terminators).
    MachineBasicBlock::iterator MBBI = MBB->getFirstTerminator();
    if (MBBI == MBB->begin()) {
      RS.enterBasicBlock(*MBB);
    } else {
      RS.enterBasicBlockEnd(*MBB);
      RS.backward(MBBI);
    }
  } else {
    // The scratch register will be used at the start of the block.
    RS.enterBasicBlock(*MBB);
  }

  // If the two registers are available, we're all good.
  // Note that we only return here if both R0 and R12 are available because
  // although the function may not require two unique registers, it may benefit
  // from having two so we should try to provide them.
  if (!RS.isRegUsed(R0) && !RS.isRegUsed(R12))
    return true;

  // Get the list of callee-saved registers for the target.
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const MCPhysReg *CSRegs = RegInfo->getCalleeSavedRegs(MBB->getParent());

  // Get all the available registers in the block.
  BitVector BV = RS.getRegsAvailable(Subtarget.isPPC64() ? &PPC::G8RCRegClass :
                                     &PPC::GPRCRegClass);

  // We shouldn't use callee-saved registers as scratch registers as they may be
  // available when looking for a candidate block for shrink wrapping but not
  // available when the actual prologue/epilogue is being emitted because they
  // were added as live-in to the prologue block by PrologueEpilogueInserter.
  for (int i = 0; CSRegs[i]; ++i)
    BV.reset(CSRegs[i]);

  // Set the first scratch register to the first available one.
  if (SR1) {
    int FirstScratchReg = BV.find_first();
    *SR1 = FirstScratchReg == -1 ? (unsigned)PPC::NoRegister : FirstScratchReg;
  }

  // If there is another one available, set the second scratch register to that.
  // Otherwise, set it to either PPC::NoRegister if this function requires two
  // or to whatever SR1 is set to if this function doesn't require two.
  if (SR2) {
    int SecondScratchReg = BV.find_next(*SR1);
    if (SecondScratchReg != -1)
      *SR2 = SecondScratchReg;
    else
      *SR2 = TwoUniqueRegsRequired ? Register() : *SR1;
  }

  // Now that we've done our best to provide both registers, double check
  // whether we were unable to provide enough.
  if (BV.count() < (TwoUniqueRegsRequired ? 2U : 1U))
    return false;

  return true;
}

// We need a scratch register for spilling LR and for spilling CR. By default,
// we use two scratch registers to hide latency. However, if only one scratch
// register is available, we can adjust for that by not overlapping the spill
// code. However, if we need to realign the stack (i.e. have a base pointer)
// and the stack frame is large, we need two scratch registers.
// Also, stack probe requires two scratch registers, one for old sp, one for
// large frame and large probe size.
bool
PPCFrameLowering::twoUniqueScratchRegsRequired(MachineBasicBlock *MBB) const {
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  MachineFunction &MF = *(MBB->getParent());
  bool HasBP = RegInfo->hasBasePointer(MF);
  unsigned FrameSize = determineFrameLayout(MF);
  int NegFrameSize = -FrameSize;
  bool IsLargeFrame = !isInt<16>(NegFrameSize);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  Align MaxAlign = MFI.getMaxAlign();
  bool HasRedZone = Subtarget.isPPC64() || !Subtarget.isSVR4ABI();
  const PPCTargetLowering &TLI = *Subtarget.getTargetLowering();

  return ((IsLargeFrame || !HasRedZone) && HasBP && MaxAlign > 1) ||
         TLI.hasInlineStackProbe(MF);
}

bool PPCFrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);

  return findScratchRegister(TmpMBB, false,
                             twoUniqueScratchRegsRequired(TmpMBB));
}

bool PPCFrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);

  return findScratchRegister(TmpMBB, true);
}

bool PPCFrameLowering::stackUpdateCanBeMoved(MachineFunction &MF) const {
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();

  // Abort if there is no register info or function info.
  if (!RegInfo || !FI)
    return false;

  // Only move the stack update on ELFv2 ABI and PPC64.
  if (!Subtarget.isELFv2ABI() || !Subtarget.isPPC64())
    return false;

  // Check the frame size first and return false if it does not fit the
  // requirements.
  // We need a non-zero frame size as well as a frame that will fit in the red
  // zone. This is because by moving the stack pointer update we are now storing
  // to the red zone until the stack pointer is updated. If we get an interrupt
  // inside the prologue but before the stack update we now have a number of
  // stores to the red zone and those stores must all fit.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned FrameSize = MFI.getStackSize();
  if (!FrameSize || FrameSize > Subtarget.getRedZoneSize())
    return false;

  // Frame pointers and base pointers complicate matters so don't do anything
  // if we have them. For example having a frame pointer will sometimes require
  // a copy of r1 into r31 and that makes keeping track of updates to r1 more
  // difficult. Similar situation exists with setjmp.
  if (hasFP(MF) || RegInfo->hasBasePointer(MF) || MF.exposesReturnsTwice())
    return false;

  // Calls to fast_cc functions use different rules for passing parameters on
  // the stack from the ABI and using PIC base in the function imposes
  // similar restrictions to using the base pointer. It is not generally safe
  // to move the stack pointer update in these situations.
  if (FI->hasFastCall() || FI->usesPICBase())
    return false;

  // Finally we can move the stack update if we do not require register
  // scavenging. Register scavenging can introduce more spills and so
  // may make the frame size larger than we have computed.
  return !RegInfo->requiresFrameIndexScavenging(MF);
}

void PPCFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const PPCTargetLowering &TLI = *Subtarget.getTargetLowering();

  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  DebugLoc dl;
  // AIX assembler does not support cfi directives.
  const bool needsCFI = MF.needsFrameMoves() && !Subtarget.isAIXABI();

  const bool HasFastMFLR = Subtarget.hasFastMFLR();

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();
  // Get the ABI.
  bool isSVR4ABI = Subtarget.isSVR4ABI();
  bool isELFv2ABI = Subtarget.isELFv2ABI();
  assert((isSVR4ABI || Subtarget.isAIXABI()) && "Unsupported PPC ABI.");

  // Work out frame sizes.
  uint64_t FrameSize = determineFrameLayoutAndUpdate(MF);
  int64_t NegFrameSize = -FrameSize;
  if (!isPPC64 && (!isInt<32>(FrameSize) || !isInt<32>(NegFrameSize)))
    llvm_unreachable("Unhandled stack size!");

  if (MFI.isFrameAddressTaken())
    replaceFPWithRealFP(MF);

  // Check if the link register (LR) must be saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  bool MustSaveTOC = FI->mustSaveTOC();
  const SmallVectorImpl<Register> &MustSaveCRs = FI->getMustSaveCRs();
  bool MustSaveCR = !MustSaveCRs.empty();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);
  bool HasRedZone = isPPC64 || !isSVR4ABI;
  bool HasROPProtect = Subtarget.hasROPProtect();
  bool HasPrivileged = Subtarget.hasPrivileged();

  Register SPReg       = isPPC64 ? PPC::X1  : PPC::R1;
  Register BPReg = RegInfo->getBaseRegister(MF);
  Register FPReg       = isPPC64 ? PPC::X31 : PPC::R31;
  Register LRReg       = isPPC64 ? PPC::LR8 : PPC::LR;
  Register TOCReg      = isPPC64 ? PPC::X2 :  PPC::R2;
  Register ScratchReg;
  Register TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  //  ...(R12/X12 is volatile in both Darwin & SVR4, & can't be a function arg.)
  const MCInstrDesc& MFLRInst = TII.get(isPPC64 ? PPC::MFLR8
                                                : PPC::MFLR );
  const MCInstrDesc& StoreInst = TII.get(isPPC64 ? PPC::STD
                                                 : PPC::STW );
  const MCInstrDesc& StoreUpdtInst = TII.get(isPPC64 ? PPC::STDU
                                                     : PPC::STWU );
  const MCInstrDesc& StoreUpdtIdxInst = TII.get(isPPC64 ? PPC::STDUX
                                                        : PPC::STWUX);
  const MCInstrDesc& OrInst = TII.get(isPPC64 ? PPC::OR8
                                              : PPC::OR );
  const MCInstrDesc& SubtractCarryingInst = TII.get(isPPC64 ? PPC::SUBFC8
                                                            : PPC::SUBFC);
  const MCInstrDesc& SubtractImmCarryingInst = TII.get(isPPC64 ? PPC::SUBFIC8
                                                               : PPC::SUBFIC);
  const MCInstrDesc &MoveFromCondRegInst = TII.get(isPPC64 ? PPC::MFCR8
                                                           : PPC::MFCR);
  const MCInstrDesc &StoreWordInst = TII.get(isPPC64 ? PPC::STW8 : PPC::STW);
  const MCInstrDesc &HashST =
      TII.get(isPPC64 ? (HasPrivileged ? PPC::HASHSTP8 : PPC::HASHST8)
                      : (HasPrivileged ? PPC::HASHSTP : PPC::HASHST));

  // Regarding this assert: Even though LR is saved in the caller's frame (i.e.,
  // LROffset is positive), that slot is callee-owned. Because PPC32 SVR4 has no
  // Red Zone, an asynchronous event (a form of "callee") could claim a frame &
  // overwrite it, so PPC32 SVR4 must claim at least a minimal frame to save LR.
  assert((isPPC64 || !isSVR4ABI || !(!FrameSize && (MustSaveLR || HasFP))) &&
         "FrameSize must be >0 to save/restore the FP or LR for 32-bit SVR4.");

  // Using the same bool variable as below to suppress compiler warnings.
  bool SingleScratchReg = findScratchRegister(
      &MBB, false, twoUniqueScratchRegsRequired(&MBB), &ScratchReg, &TempReg);
  assert(SingleScratchReg &&
         "Required number of registers not available in this block");

  SingleScratchReg = ScratchReg == TempReg;

  int64_t LROffset = getReturnSaveOffset();

  int64_t FPOffset = 0;
  if (HasFP) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int FPIndex = FI->getFramePointerSaveIndex();
    assert(FPIndex && "No Frame Pointer Save Slot!");
    FPOffset = MFI.getObjectOffset(FPIndex);
  }

  int64_t BPOffset = 0;
  if (HasBP) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int BPIndex = FI->getBasePointerSaveIndex();
    assert(BPIndex && "No Base Pointer Save Slot!");
    BPOffset = MFI.getObjectOffset(BPIndex);
  }

  int64_t PBPOffset = 0;
  if (FI->usesPICBase()) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = MFI.getObjectOffset(PBPIndex);
  }

  // Get stack alignments.
  Align MaxAlign = MFI.getMaxAlign();
  if (HasBP && MaxAlign > 1)
    assert(Log2(MaxAlign) < 16 && "Invalid alignment!");

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple STDU/STWU/STD/STW immediate offset operand.
  bool isLargeFrame = !isInt<16>(NegFrameSize);

  // Check if we can move the stack update instruction (stdu) down the prologue
  // past the callee saves. Hopefully this will avoid the situation where the
  // saves are waiting for the update on the store with update to complete.
  MachineBasicBlock::iterator StackUpdateLoc = MBBI;
  bool MovingStackUpdateDown = false;

  // Check if we can move the stack update.
  if (stackUpdateCanBeMoved(MF)) {
    const std::vector<CalleeSavedInfo> &Info = MFI.getCalleeSavedInfo();
    for (CalleeSavedInfo CSI : Info) {
      // If the callee saved register is spilled to a register instead of the
      // stack then the spill no longer uses the stack pointer.
      // This can lead to two consequences:
      // 1) We no longer need to update the stack because the function does not
      //    spill any callee saved registers to stack.
      // 2) We have a situation where we still have to update the stack pointer
      //    even though some registers are spilled to other registers. In
      //    this case the current code moves the stack update to an incorrect
      //    position.
      // In either case we should abort moving the stack update operation.
      if (CSI.isSpilledToReg()) {
        StackUpdateLoc = MBBI;
        MovingStackUpdateDown = false;
        break;
      }

      int FrIdx = CSI.getFrameIdx();
      // If the frame index is not negative the callee saved info belongs to a
      // stack object that is not a fixed stack object. We ignore non-fixed
      // stack objects because we won't move the stack update pointer past them.
      if (FrIdx >= 0)
        continue;

      if (MFI.isFixedObjectIndex(FrIdx) && MFI.getObjectOffset(FrIdx) < 0) {
        StackUpdateLoc++;
        MovingStackUpdateDown = true;
      } else {
        // We need all of the Frame Indices to meet these conditions.
        // If they do not, abort the whole operation.
        StackUpdateLoc = MBBI;
        MovingStackUpdateDown = false;
        break;
      }
    }

    // If the operation was not aborted then update the object offset.
    if (MovingStackUpdateDown) {
      for (CalleeSavedInfo CSI : Info) {
        int FrIdx = CSI.getFrameIdx();
        if (FrIdx < 0)
          MFI.setObjectOffset(FrIdx, MFI.getObjectOffset(FrIdx) + NegFrameSize);
      }
    }
  }

  // Where in the prologue we move the CR fields depends on how many scratch
  // registers we have, and if we need to save the link register or not. This
  // lambda is to avoid duplicating the logic in 2 places.
  auto BuildMoveFromCR = [&]() {
    if (isELFv2ABI && MustSaveCRs.size() == 1) {
    // In the ELFv2 ABI, we are not required to save all CR fields.
    // If only one CR field is clobbered, it is more efficient to use
    // mfocrf to selectively save just that field, because mfocrf has short
    // latency compares to mfcr.
      assert(isPPC64 && "V2 ABI is 64-bit only.");
      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, dl, TII.get(PPC::MFOCRF8), TempReg);
      MIB.addReg(MustSaveCRs[0], RegState::Kill);
    } else {
      MachineInstrBuilder MIB =
          BuildMI(MBB, MBBI, dl, MoveFromCondRegInst, TempReg);
      for (unsigned CRfield : MustSaveCRs)
        MIB.addReg(CRfield, RegState::ImplicitKill);
    }
  };

  // If we need to spill the CR and the LR but we don't have two separate
  // registers available, we must spill them one at a time
  if (MustSaveCR && SingleScratchReg && MustSaveLR) {
    BuildMoveFromCR();
    BuildMI(MBB, MBBI, dl, StoreWordInst)
        .addReg(TempReg, getKillRegState(true))
        .addImm(CRSaveOffset)
        .addReg(SPReg);
  }

  if (MustSaveLR)
    BuildMI(MBB, MBBI, dl, MFLRInst, ScratchReg);

  if (MustSaveCR && !(SingleScratchReg && MustSaveLR))
    BuildMoveFromCR();

  if (HasRedZone) {
    if (HasFP)
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(FPReg)
        .addImm(FPOffset)
        .addReg(SPReg);
    if (FI->usesPICBase())
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(PPC::R30)
        .addImm(PBPOffset)
        .addReg(SPReg);
    if (HasBP)
      BuildMI(MBB, MBBI, dl, StoreInst)
        .addReg(BPReg)
        .addImm(BPOffset)
        .addReg(SPReg);
  }

  // Generate the instruction to store the LR. In the case where ROP protection
  // is required the register holding the LR should not be killed as it will be
  // used by the hash store instruction.
  auto SaveLR = [&](int64_t Offset) {
    assert(MustSaveLR && "LR is not required to be saved!");
    BuildMI(MBB, StackUpdateLoc, dl, StoreInst)
        .addReg(ScratchReg, getKillRegState(!HasROPProtect))
        .addImm(Offset)
        .addReg(SPReg);

    // Add the ROP protection Hash Store instruction.
    // NOTE: This is technically a violation of the ABI. The hash can be saved
    // up to 512 bytes into the Protected Zone. This can be outside of the
    // initial 288 byte volatile program storage region in the Protected Zone.
    // However, this restriction will be removed in an upcoming revision of the
    // ABI.
    if (HasROPProtect) {
      const int SaveIndex = FI->getROPProtectionHashSaveIndex();
      const int64_t ImmOffset = MFI.getObjectOffset(SaveIndex);
      assert((ImmOffset <= -8 && ImmOffset >= -512) &&
             "ROP hash save offset out of range.");
      assert(((ImmOffset & 0x7) == 0) &&
             "ROP hash save offset must be 8 byte aligned.");
      BuildMI(MBB, StackUpdateLoc, dl, HashST)
          .addReg(ScratchReg, getKillRegState(true))
          .addImm(ImmOffset)
          .addReg(SPReg);
    }
  };

  if (MustSaveLR && HasFastMFLR)
      SaveLR(LROffset);

  if (MustSaveCR &&
      !(SingleScratchReg && MustSaveLR)) {
    assert(HasRedZone && "A red zone is always available on PPC64");
    BuildMI(MBB, MBBI, dl, StoreWordInst)
      .addReg(TempReg, getKillRegState(true))
      .addImm(CRSaveOffset)
      .addReg(SPReg);
  }

  // Skip the rest if this is a leaf function & all spills fit in the Red Zone.
  if (!FrameSize) {
    if (MustSaveLR && !HasFastMFLR)
      SaveLR(LROffset);
    return;
  }

  // Adjust stack pointer: r1 += NegFrameSize.
  // If there is a preferred stack alignment, align R1 now

  if (HasBP && HasRedZone) {
    // Save a copy of r1 as the base pointer.
    BuildMI(MBB, MBBI, dl, OrInst, BPReg)
      .addReg(SPReg)
      .addReg(SPReg);
  }

  // Have we generated a STUX instruction to claim stack frame? If so,
  // the negated frame size will be placed in ScratchReg.
  bool HasSTUX =
      (TLI.hasInlineStackProbe(MF) && FrameSize > TLI.getStackProbeSize(MF)) ||
      (HasBP && MaxAlign > 1) || isLargeFrame;

  // If we use STUX to update the stack pointer, we need the two scratch
  // registers TempReg and ScratchReg, we have to save LR here which is stored
  // in ScratchReg.
  // If the offset can not be encoded into the store instruction, we also have
  // to save LR here.
  if (MustSaveLR && !HasFastMFLR &&
      (HasSTUX || !isInt<16>(FrameSize + LROffset)))
    SaveLR(LROffset);

  // If FrameSize <= TLI.getStackProbeSize(MF), as POWER ABI requires backchain
  // pointer is always stored at SP, we will get a free probe due to an essential
  // STU(X) instruction.
  if (TLI.hasInlineStackProbe(MF) && FrameSize > TLI.getStackProbeSize(MF)) {
    // To be consistent with other targets, a pseudo instruction is emitted and
    // will be later expanded in `inlineStackProbe`.
    BuildMI(MBB, MBBI, dl,
            TII.get(isPPC64 ? PPC::PROBED_STACKALLOC_64
                            : PPC::PROBED_STACKALLOC_32))
        .addDef(TempReg)
        .addDef(ScratchReg) // ScratchReg stores the old sp.
        .addImm(NegFrameSize);
    // FIXME: HasSTUX is only read if HasRedZone is not set, in such case, we
    // update the ScratchReg to meet the assumption that ScratchReg contains
    // the NegFrameSize. This solution is rather tricky.
    if (!HasRedZone) {
      BuildMI(MBB, MBBI, dl, TII.get(PPC::SUBF), ScratchReg)
          .addReg(ScratchReg)
          .addReg(SPReg);
    }
  } else {
    // This condition must be kept in sync with canUseAsPrologue.
    if (HasBP && MaxAlign > 1) {
      if (isPPC64)
        BuildMI(MBB, MBBI, dl, TII.get(PPC::RLDICL), ScratchReg)
            .addReg(SPReg)
            .addImm(0)
            .addImm(64 - Log2(MaxAlign));
      else // PPC32...
        BuildMI(MBB, MBBI, dl, TII.get(PPC::RLWINM), ScratchReg)
            .addReg(SPReg)
            .addImm(0)
            .addImm(32 - Log2(MaxAlign))
            .addImm(31);
      if (!isLargeFrame) {
        BuildMI(MBB, MBBI, dl, SubtractImmCarryingInst, ScratchReg)
            .addReg(ScratchReg, RegState::Kill)
            .addImm(NegFrameSize);
      } else {
        assert(!SingleScratchReg && "Only a single scratch reg available");
        TII.materializeImmPostRA(MBB, MBBI, dl, TempReg, NegFrameSize);
        BuildMI(MBB, MBBI, dl, SubtractCarryingInst, ScratchReg)
            .addReg(ScratchReg, RegState::Kill)
            .addReg(TempReg, RegState::Kill);
      }

      BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
          .addReg(SPReg, RegState::Kill)
          .addReg(SPReg)
          .addReg(ScratchReg);
    } else if (!isLargeFrame) {
      BuildMI(MBB, StackUpdateLoc, dl, StoreUpdtInst, SPReg)
          .addReg(SPReg)
          .addImm(NegFrameSize)
          .addReg(SPReg);
    } else {
      TII.materializeImmPostRA(MBB, MBBI, dl, ScratchReg, NegFrameSize);
      BuildMI(MBB, MBBI, dl, StoreUpdtIdxInst, SPReg)
          .addReg(SPReg, RegState::Kill)
          .addReg(SPReg)
          .addReg(ScratchReg);
    }
  }

  // Save the TOC register after the stack pointer update if a prologue TOC
  // save is required for the function.
  if (MustSaveTOC) {
    assert(isELFv2ABI && "TOC saves in the prologue only supported on ELFv2");
    BuildMI(MBB, StackUpdateLoc, dl, TII.get(PPC::STD))
      .addReg(TOCReg, getKillRegState(true))
      .addImm(TOCSaveOffset)
      .addReg(SPReg);
  }

  if (!HasRedZone) {
    assert(!isPPC64 && "A red zone is always available on PPC64");
    if (HasSTUX) {
      // The negated frame size is in ScratchReg, and the SPReg has been
      // decremented by the frame size: SPReg = old SPReg + ScratchReg.
      // Since FPOffset, PBPOffset, etc. are relative to the beginning of
      // the stack frame (i.e. the old SP), ideally, we would put the old
      // SP into a register and use it as the base for the stores. The
      // problem is that the only available register may be ScratchReg,
      // which could be R0, and R0 cannot be used as a base address.

      // First, set ScratchReg to the old SP. This may need to be modified
      // later.
      BuildMI(MBB, MBBI, dl, TII.get(PPC::SUBF), ScratchReg)
        .addReg(ScratchReg, RegState::Kill)
        .addReg(SPReg);

      if (ScratchReg == PPC::R0) {
        // R0 cannot be used as a base register, but it can be used as an
        // index in a store-indexed.
        int LastOffset = 0;
        if (HasFP) {
          // R0 += (FPOffset-LastOffset).
          // Need addic, since addi treats R0 as 0.
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(FPOffset-LastOffset);
          LastOffset = FPOffset;
          // Store FP into *R0.
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(FPReg, RegState::Kill)  // Save FP.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
        }
        if (FI->usesPICBase()) {
          // R0 += (PBPOffset-LastOffset).
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(PBPOffset-LastOffset);
          LastOffset = PBPOffset;
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(PPC::R30, RegState::Kill)  // Save PIC base pointer.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
        }
        if (HasBP) {
          // R0 += (BPOffset-LastOffset).
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), ScratchReg)
            .addReg(ScratchReg)
            .addImm(BPOffset-LastOffset);
          LastOffset = BPOffset;
          BuildMI(MBB, MBBI, dl, TII.get(PPC::STWX))
            .addReg(BPReg, RegState::Kill)  // Save BP.
            .addReg(PPC::ZERO)
            .addReg(ScratchReg);  // This will be the index (R0 is ok here).
          // BP = R0-LastOffset
          BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDIC), BPReg)
            .addReg(ScratchReg, RegState::Kill)
            .addImm(-LastOffset);
        }
      } else {
        // ScratchReg is not R0, so use it as the base register. It is
        // already set to the old SP, so we can use the offsets directly.

        // Now that the stack frame has been allocated, save all the necessary
        // registers using ScratchReg as the base address.
        if (HasFP)
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(FPReg)
            .addImm(FPOffset)
            .addReg(ScratchReg);
        if (FI->usesPICBase())
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(PPC::R30)
            .addImm(PBPOffset)
            .addReg(ScratchReg);
        if (HasBP) {
          BuildMI(MBB, MBBI, dl, StoreInst)
            .addReg(BPReg)
            .addImm(BPOffset)
            .addReg(ScratchReg);
          BuildMI(MBB, MBBI, dl, OrInst, BPReg)
            .addReg(ScratchReg, RegState::Kill)
            .addReg(ScratchReg);
        }
      }
    } else {
      // The frame size is a known 16-bit constant (fitting in the immediate
      // field of STWU). To be here we have to be compiling for PPC32.
      // Since the SPReg has been decreased by FrameSize, add it back to each
      // offset.
      if (HasFP)
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(FPReg)
          .addImm(FrameSize + FPOffset)
          .addReg(SPReg);
      if (FI->usesPICBase())
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(PPC::R30)
          .addImm(FrameSize + PBPOffset)
          .addReg(SPReg);
      if (HasBP) {
        BuildMI(MBB, MBBI, dl, StoreInst)
          .addReg(BPReg)
          .addImm(FrameSize + BPOffset)
          .addReg(SPReg);
        BuildMI(MBB, MBBI, dl, TII.get(PPC::ADDI), BPReg)
          .addReg(SPReg)
          .addImm(FrameSize);
      }
    }
  }

  // Save the LR now.
  if (!HasSTUX && MustSaveLR && !HasFastMFLR && isInt<16>(FrameSize + LROffset))
    SaveLR(LROffset + FrameSize);

  // Add Call Frame Information for the instructions we generated above.
  if (needsCFI) {
    unsigned CFIIndex;

    if (HasBP) {
      // Define CFA in terms of BP. Do this in preference to using FP/SP,
      // because if the stack needed aligning then CFA won't be at a fixed
      // offset from FP/SP.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));
    } else {
      // Adjust the definition of CFA to account for the change in SP.
      assert(NegFrameSize);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, -NegFrameSize));
    }
    BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    if (HasFP) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, FPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (FI->usesPICBase()) {
      // Describe where FP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(PPC::R30, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, PBPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (HasBP) {
      // Describe where BP was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(BPReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, BPOffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }

    if (MustSaveLR) {
      // Describe where LR was saved, at a fixed offset from CFA.
      unsigned Reg = MRI->getDwarfRegNum(LRReg, true);
      CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, Reg, LROffset));
      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  // If there is a frame pointer, copy R1 into R31
  if (HasFP) {
    BuildMI(MBB, MBBI, dl, OrInst, FPReg)
      .addReg(SPReg)
      .addReg(SPReg);

    if (!HasBP && needsCFI) {
      // Change the definition of CFA from SP+offset to FP+offset, because SP
      // will change at every alloca.
      unsigned Reg = MRI->getDwarfRegNum(FPReg, true);
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::createDefCfaRegister(nullptr, Reg));

      BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex);
    }
  }

  if (needsCFI) {
    // Describe where callee saved registers were saved, at fixed offsets from
    // CFA.
    const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
    for (const CalleeSavedInfo &I : CSI) {
      Register Reg = I.getReg();
      if (Reg == PPC::LR || Reg == PPC::LR8 || Reg == PPC::RM) continue;

      // This is a bit of a hack: CR2LT, CR2GT, CR2EQ and CR2UN are just
      // subregisters of CR2. We just need to emit a move of CR2.
      if (PPC::CRBITRCRegClass.contains(Reg))
        continue;

      if ((Reg == PPC::X2 || Reg == PPC::R2) && MustSaveTOC)
        continue;

      // For 64-bit SVR4 when we have spilled CRs, the spill location
      // is SP+8, not a frame-relative slot.
      if (isSVR4ABI && isPPC64 && (PPC::CR2 <= Reg && Reg <= PPC::CR4)) {
        // In the ELFv1 ABI, only CR2 is noted in CFI and stands in for
        // the whole CR word.  In the ELFv2 ABI, every CR that was
        // actually saved gets its own CFI record.
        Register CRReg = isELFv2ABI? Reg : PPC::CR2;
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(CRReg, true), CRSaveOffset));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
        continue;
      }

      if (I.isSpilledToReg()) {
        unsigned SpilledReg = I.getDstReg();
        unsigned CFIRegister = MF.addFrameInst(MCCFIInstruction::createRegister(
            nullptr, MRI->getDwarfRegNum(Reg, true),
            MRI->getDwarfRegNum(SpilledReg, true)));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIRegister);
      } else {
        int64_t Offset = MFI.getObjectOffset(I.getFrameIdx());
        // We have changed the object offset above but we do not want to change
        // the actual offsets in the CFI instruction so we have to undo the
        // offset change here.
        if (MovingStackUpdateDown)
          Offset -= NegFrameSize;

        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), Offset));
        BuildMI(MBB, MBBI, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex);
      }
    }
  }
}

void PPCFrameLowering::inlineStackProbe(MachineFunction &MF,
                                        MachineBasicBlock &PrologMBB) const {
  bool isPPC64 = Subtarget.isPPC64();
  const PPCTargetLowering &TLI = *Subtarget.getTargetLowering();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const MCRegisterInfo *MRI = MF.getContext().getRegisterInfo();
  // AIX assembler does not support cfi directives.
  const bool needsCFI = MF.needsFrameMoves() && !Subtarget.isAIXABI();
  auto StackAllocMIPos = llvm::find_if(PrologMBB, [](MachineInstr &MI) {
    int Opc = MI.getOpcode();
    return Opc == PPC::PROBED_STACKALLOC_64 || Opc == PPC::PROBED_STACKALLOC_32;
  });
  if (StackAllocMIPos == PrologMBB.end())
    return;
  const BasicBlock *ProbedBB = PrologMBB.getBasicBlock();
  MachineBasicBlock *CurrentMBB = &PrologMBB;
  DebugLoc DL = PrologMBB.findDebugLoc(StackAllocMIPos);
  MachineInstr &MI = *StackAllocMIPos;
  int64_t NegFrameSize = MI.getOperand(2).getImm();
  unsigned ProbeSize = TLI.getStackProbeSize(MF);
  int64_t NegProbeSize = -(int64_t)ProbeSize;
  assert(isInt<32>(NegProbeSize) && "Unhandled probe size");
  int64_t NumBlocks = NegFrameSize / NegProbeSize;
  int64_t NegResidualSize = NegFrameSize % NegProbeSize;
  Register SPReg = isPPC64 ? PPC::X1 : PPC::R1;
  Register ScratchReg = MI.getOperand(0).getReg();
  Register FPReg = MI.getOperand(1).getReg();
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  bool HasBP = RegInfo->hasBasePointer(MF);
  Register BPReg = RegInfo->getBaseRegister(MF);
  Align MaxAlign = MFI.getMaxAlign();
  bool HasRedZone = Subtarget.isPPC64() || !Subtarget.isSVR4ABI();
  const MCInstrDesc &CopyInst = TII.get(isPPC64 ? PPC::OR8 : PPC::OR);
  // Subroutines to generate .cfi_* directives.
  auto buildDefCFAReg = [&](MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register Reg) {
    unsigned RegNum = MRI->getDwarfRegNum(Reg, true);
    unsigned CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createDefCfaRegister(nullptr, RegNum));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  };
  auto buildDefCFA = [&](MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, Register Reg,
                         int Offset) {
    unsigned RegNum = MRI->getDwarfRegNum(Reg, true);
    unsigned CFIIndex = MBB.getParent()->addFrameInst(
        MCCFIInstruction::cfiDefCfa(nullptr, RegNum, Offset));
    BuildMI(MBB, MBBI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  };
  // Subroutine to determine if we can use the Imm as part of d-form.
  auto CanUseDForm = [](int64_t Imm) { return isInt<16>(Imm) && Imm % 4 == 0; };
  // Subroutine to materialize the Imm into TempReg.
  auto MaterializeImm = [&](MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, int64_t Imm,
                            Register &TempReg) {
    assert(isInt<32>(Imm) && "Unhandled imm");
    if (isInt<16>(Imm))
      BuildMI(MBB, MBBI, DL, TII.get(isPPC64 ? PPC::LI8 : PPC::LI), TempReg)
          .addImm(Imm);
    else {
      BuildMI(MBB, MBBI, DL, TII.get(isPPC64 ? PPC::LIS8 : PPC::LIS), TempReg)
          .addImm(Imm >> 16);
      BuildMI(MBB, MBBI, DL, TII.get(isPPC64 ? PPC::ORI8 : PPC::ORI), TempReg)
          .addReg(TempReg)
          .addImm(Imm & 0xFFFF);
    }
  };
  // Subroutine to store frame pointer and decrease stack pointer by probe size.
  auto allocateAndProbe = [&](MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MBBI, int64_t NegSize,
                              Register NegSizeReg, bool UseDForm,
                              Register StoreReg) {
    if (UseDForm)
      BuildMI(MBB, MBBI, DL, TII.get(isPPC64 ? PPC::STDU : PPC::STWU), SPReg)
          .addReg(StoreReg)
          .addImm(NegSize)
          .addReg(SPReg);
    else
      BuildMI(MBB, MBBI, DL, TII.get(isPPC64 ? PPC::STDUX : PPC::STWUX), SPReg)
          .addReg(StoreReg)
          .addReg(SPReg)
          .addReg(NegSizeReg);
  };
  // Used to probe stack when realignment is required.
  // Note that, according to ABI's requirement, *sp must always equals the
  // value of back-chain pointer, only st(w|d)u(x) can be used to update sp.
  // Following is pseudo code:
  // final_sp = (sp & align) + negframesize;
  // neg_gap = final_sp - sp;
  // while (neg_gap < negprobesize) {
  //   stdu fp, negprobesize(sp);
  //   neg_gap -= negprobesize;
  // }
  // stdux fp, sp, neg_gap
  //
  // When HasBP & HasRedzone, back-chain pointer is already saved in BPReg
  // before probe code, we don't need to save it, so we get one additional reg
  // that can be used to materialize the probeside if needed to use xform.
  // Otherwise, we can NOT materialize probeside, so we can only use Dform for
  // now.
  //
  // The allocations are:
  // if (HasBP && HasRedzone) {
  //   r0: materialize the probesize if needed so that we can use xform.
  //   r12: `neg_gap`
  // } else {
  //   r0: back-chain pointer
  //   r12: `neg_gap`.
  // }
  auto probeRealignedStack = [&](MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator MBBI,
                                 Register ScratchReg, Register TempReg) {
    assert(HasBP && "The function is supposed to have base pointer when its "
                    "stack is realigned.");
    assert(isPowerOf2_64(ProbeSize) && "Probe size should be power of 2");

    // FIXME: We can eliminate this limitation if we get more infomation about
    // which part of redzone are already used. Used redzone can be treated
    // probed. But there might be `holes' in redzone probed, this could
    // complicate the implementation.
    assert(ProbeSize >= Subtarget.getRedZoneSize() &&
           "Probe size should be larger or equal to the size of red-zone so "
           "that red-zone is not clobbered by probing.");

    Register &FinalStackPtr = TempReg;
    // FIXME: We only support NegProbeSize materializable by DForm currently.
    // When HasBP && HasRedzone, we can use xform if we have an additional idle
    // register.
    NegProbeSize = std::max(NegProbeSize, -((int64_t)1 << 15));
    assert(isInt<16>(NegProbeSize) &&
           "NegProbeSize should be materializable by DForm");
    Register CRReg = PPC::CR0;
    // Layout of output assembly kinda like:
    // bb.0:
    //   ...
    //   sub $scratchreg, $finalsp, r1
    //   cmpdi $scratchreg, <negprobesize>
    //   bge bb.2
    // bb.1:
    //   stdu <backchain>, <negprobesize>(r1)
    //   sub $scratchreg, $scratchreg, negprobesize
    //   cmpdi $scratchreg, <negprobesize>
    //   blt bb.1
    // bb.2:
    //   stdux <backchain>, r1, $scratchreg
    MachineFunction::iterator MBBInsertPoint = std::next(MBB.getIterator());
    MachineBasicBlock *ProbeLoopBodyMBB = MF.CreateMachineBasicBlock(ProbedBB);
    MF.insert(MBBInsertPoint, ProbeLoopBodyMBB);
    MachineBasicBlock *ProbeExitMBB = MF.CreateMachineBasicBlock(ProbedBB);
    MF.insert(MBBInsertPoint, ProbeExitMBB);
    // bb.2
    {
      Register BackChainPointer = HasRedZone ? BPReg : TempReg;
      allocateAndProbe(*ProbeExitMBB, ProbeExitMBB->end(), 0, ScratchReg, false,
                       BackChainPointer);
      if (HasRedZone)
        // PROBED_STACKALLOC_64 assumes Operand(1) stores the old sp, copy BPReg
        // to TempReg to satisfy it.
        BuildMI(*ProbeExitMBB, ProbeExitMBB->end(), DL, CopyInst, TempReg)
            .addReg(BPReg)
            .addReg(BPReg);
      ProbeExitMBB->splice(ProbeExitMBB->end(), &MBB, MBBI, MBB.end());
      ProbeExitMBB->transferSuccessorsAndUpdatePHIs(&MBB);
    }
    // bb.0
    {
      BuildMI(&MBB, DL, TII.get(isPPC64 ? PPC::SUBF8 : PPC::SUBF), ScratchReg)
          .addReg(SPReg)
          .addReg(FinalStackPtr);
      if (!HasRedZone)
        BuildMI(&MBB, DL, CopyInst, TempReg).addReg(SPReg).addReg(SPReg);
      BuildMI(&MBB, DL, TII.get(isPPC64 ? PPC::CMPDI : PPC::CMPWI), CRReg)
          .addReg(ScratchReg)
          .addImm(NegProbeSize);
      BuildMI(&MBB, DL, TII.get(PPC::BCC))
          .addImm(PPC::PRED_GE)
          .addReg(CRReg)
          .addMBB(ProbeExitMBB);
      MBB.addSuccessor(ProbeLoopBodyMBB);
      MBB.addSuccessor(ProbeExitMBB);
    }
    // bb.1
    {
      Register BackChainPointer = HasRedZone ? BPReg : TempReg;
      allocateAndProbe(*ProbeLoopBodyMBB, ProbeLoopBodyMBB->end(), NegProbeSize,
                       0, true /*UseDForm*/, BackChainPointer);
      BuildMI(ProbeLoopBodyMBB, DL, TII.get(isPPC64 ? PPC::ADDI8 : PPC::ADDI),
              ScratchReg)
          .addReg(ScratchReg)
          .addImm(-NegProbeSize);
      BuildMI(ProbeLoopBodyMBB, DL, TII.get(isPPC64 ? PPC::CMPDI : PPC::CMPWI),
              CRReg)
          .addReg(ScratchReg)
          .addImm(NegProbeSize);
      BuildMI(ProbeLoopBodyMBB, DL, TII.get(PPC::BCC))
          .addImm(PPC::PRED_LT)
          .addReg(CRReg)
          .addMBB(ProbeLoopBodyMBB);
      ProbeLoopBodyMBB->addSuccessor(ProbeExitMBB);
      ProbeLoopBodyMBB->addSuccessor(ProbeLoopBodyMBB);
    }
    // Update liveins.
    fullyRecomputeLiveIns({ProbeExitMBB, ProbeLoopBodyMBB});
    return ProbeExitMBB;
  };
  // For case HasBP && MaxAlign > 1, we have to realign the SP by performing
  // SP = SP - SP % MaxAlign, thus make the probe more like dynamic probe since
  // the offset subtracted from SP is determined by SP's runtime value.
  if (HasBP && MaxAlign > 1) {
    // Calculate final stack pointer.
    if (isPPC64)
      BuildMI(*CurrentMBB, {MI}, DL, TII.get(PPC::RLDICL), ScratchReg)
          .addReg(SPReg)
          .addImm(0)
          .addImm(64 - Log2(MaxAlign));
    else
      BuildMI(*CurrentMBB, {MI}, DL, TII.get(PPC::RLWINM), ScratchReg)
          .addReg(SPReg)
          .addImm(0)
          .addImm(32 - Log2(MaxAlign))
          .addImm(31);
    BuildMI(*CurrentMBB, {MI}, DL, TII.get(isPPC64 ? PPC::SUBF8 : PPC::SUBF),
            FPReg)
        .addReg(ScratchReg)
        .addReg(SPReg);
    MaterializeImm(*CurrentMBB, {MI}, NegFrameSize, ScratchReg);
    BuildMI(*CurrentMBB, {MI}, DL, TII.get(isPPC64 ? PPC::ADD8 : PPC::ADD4),
            FPReg)
        .addReg(ScratchReg)
        .addReg(FPReg);
    CurrentMBB = probeRealignedStack(*CurrentMBB, {MI}, ScratchReg, FPReg);
    if (needsCFI)
      buildDefCFAReg(*CurrentMBB, {MI}, FPReg);
  } else {
    // Initialize current frame pointer.
    BuildMI(*CurrentMBB, {MI}, DL, CopyInst, FPReg).addReg(SPReg).addReg(SPReg);
    // Use FPReg to calculate CFA.
    if (needsCFI)
      buildDefCFA(*CurrentMBB, {MI}, FPReg, 0);
    // Probe residual part.
    if (NegResidualSize) {
      bool ResidualUseDForm = CanUseDForm(NegResidualSize);
      if (!ResidualUseDForm)
        MaterializeImm(*CurrentMBB, {MI}, NegResidualSize, ScratchReg);
      allocateAndProbe(*CurrentMBB, {MI}, NegResidualSize, ScratchReg,
                       ResidualUseDForm, FPReg);
    }
    bool UseDForm = CanUseDForm(NegProbeSize);
    // If number of blocks is small, just probe them directly.
    if (NumBlocks < 3) {
      if (!UseDForm)
        MaterializeImm(*CurrentMBB, {MI}, NegProbeSize, ScratchReg);
      for (int i = 0; i < NumBlocks; ++i)
        allocateAndProbe(*CurrentMBB, {MI}, NegProbeSize, ScratchReg, UseDForm,
                         FPReg);
      if (needsCFI) {
        // Restore using SPReg to calculate CFA.
        buildDefCFAReg(*CurrentMBB, {MI}, SPReg);
      }
    } else {
      // Since CTR is a volatile register and current shrinkwrap implementation
      // won't choose an MBB in a loop as the PrologMBB, it's safe to synthesize a
      // CTR loop to probe.
      // Calculate trip count and stores it in CTRReg.
      MaterializeImm(*CurrentMBB, {MI}, NumBlocks, ScratchReg);
      BuildMI(*CurrentMBB, {MI}, DL, TII.get(isPPC64 ? PPC::MTCTR8 : PPC::MTCTR))
          .addReg(ScratchReg, RegState::Kill);
      if (!UseDForm)
        MaterializeImm(*CurrentMBB, {MI}, NegProbeSize, ScratchReg);
      // Create MBBs of the loop.
      MachineFunction::iterator MBBInsertPoint =
          std::next(CurrentMBB->getIterator());
      MachineBasicBlock *LoopMBB = MF.CreateMachineBasicBlock(ProbedBB);
      MF.insert(MBBInsertPoint, LoopMBB);
      MachineBasicBlock *ExitMBB = MF.CreateMachineBasicBlock(ProbedBB);
      MF.insert(MBBInsertPoint, ExitMBB);
      // Synthesize the loop body.
      allocateAndProbe(*LoopMBB, LoopMBB->end(), NegProbeSize, ScratchReg,
                       UseDForm, FPReg);
      BuildMI(LoopMBB, DL, TII.get(isPPC64 ? PPC::BDNZ8 : PPC::BDNZ))
          .addMBB(LoopMBB);
      LoopMBB->addSuccessor(ExitMBB);
      LoopMBB->addSuccessor(LoopMBB);
      // Synthesize the exit MBB.
      ExitMBB->splice(ExitMBB->end(), CurrentMBB,
                      std::next(MachineBasicBlock::iterator(MI)),
                      CurrentMBB->end());
      ExitMBB->transferSuccessorsAndUpdatePHIs(CurrentMBB);
      CurrentMBB->addSuccessor(LoopMBB);
      if (needsCFI) {
        // Restore using SPReg to calculate CFA.
        buildDefCFAReg(*ExitMBB, ExitMBB->begin(), SPReg);
      }
      // Update liveins.
      fullyRecomputeLiveIns({ExitMBB, LoopMBB});
    }
  }
  ++NumPrologProbed;
  MI.eraseFromParent();
}

void PPCFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc dl;

  if (MBBI != MBB.end())
    dl = MBBI->getDebugLoc();

  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  // Get alignment info so we know how to restore the SP.
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Get the number of bytes allocated from the FrameInfo.
  int64_t FrameSize = MFI.getStackSize();

  // Get processor type.
  bool isPPC64 = Subtarget.isPPC64();

  // Check if the link register (LR) has been saved.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  bool MustSaveLR = FI->mustSaveLR();
  const SmallVectorImpl<Register> &MustSaveCRs = FI->getMustSaveCRs();
  bool MustSaveCR = !MustSaveCRs.empty();
  // Do we have a frame pointer and/or base pointer for this function?
  bool HasFP = hasFP(MF);
  bool HasBP = RegInfo->hasBasePointer(MF);
  bool HasRedZone = Subtarget.isPPC64() || !Subtarget.isSVR4ABI();
  bool HasROPProtect = Subtarget.hasROPProtect();
  bool HasPrivileged = Subtarget.hasPrivileged();

  Register SPReg      = isPPC64 ? PPC::X1  : PPC::R1;
  Register BPReg = RegInfo->getBaseRegister(MF);
  Register FPReg      = isPPC64 ? PPC::X31 : PPC::R31;
  Register ScratchReg;
  Register TempReg     = isPPC64 ? PPC::X12 : PPC::R12; // another scratch reg
  const MCInstrDesc& MTLRInst = TII.get( isPPC64 ? PPC::MTLR8
                                                 : PPC::MTLR );
  const MCInstrDesc& LoadInst = TII.get( isPPC64 ? PPC::LD
                                                 : PPC::LWZ );
  const MCInstrDesc& LoadImmShiftedInst = TII.get( isPPC64 ? PPC::LIS8
                                                           : PPC::LIS );
  const MCInstrDesc& OrInst = TII.get(isPPC64 ? PPC::OR8
                                              : PPC::OR );
  const MCInstrDesc& OrImmInst = TII.get( isPPC64 ? PPC::ORI8
                                                  : PPC::ORI );
  const MCInstrDesc& AddImmInst = TII.get( isPPC64 ? PPC::ADDI8
                                                   : PPC::ADDI );
  const MCInstrDesc& AddInst = TII.get( isPPC64 ? PPC::ADD8
                                                : PPC::ADD4 );
  const MCInstrDesc& LoadWordInst = TII.get( isPPC64 ? PPC::LWZ8
                                                     : PPC::LWZ);
  const MCInstrDesc& MoveToCRInst = TII.get( isPPC64 ? PPC::MTOCRF8
                                                     : PPC::MTOCRF);
  const MCInstrDesc &HashChk =
      TII.get(isPPC64 ? (HasPrivileged ? PPC::HASHCHKP8 : PPC::HASHCHK8)
                      : (HasPrivileged ? PPC::HASHCHKP : PPC::HASHCHK));
  int64_t LROffset = getReturnSaveOffset();

  int64_t FPOffset = 0;

  // Using the same bool variable as below to suppress compiler warnings.
  bool SingleScratchReg = findScratchRegister(&MBB, true, false, &ScratchReg,
                                              &TempReg);
  assert(SingleScratchReg &&
         "Could not find an available scratch register");

  SingleScratchReg = ScratchReg == TempReg;

  if (HasFP) {
    int FPIndex = FI->getFramePointerSaveIndex();
    assert(FPIndex && "No Frame Pointer Save Slot!");
    FPOffset = MFI.getObjectOffset(FPIndex);
  }

  int64_t BPOffset = 0;
  if (HasBP) {
      int BPIndex = FI->getBasePointerSaveIndex();
      assert(BPIndex && "No Base Pointer Save Slot!");
      BPOffset = MFI.getObjectOffset(BPIndex);
  }

  int64_t PBPOffset = 0;
  if (FI->usesPICBase()) {
    int PBPIndex = FI->getPICBasePointerSaveIndex();
    assert(PBPIndex && "No PIC Base Pointer Save Slot!");
    PBPOffset = MFI.getObjectOffset(PBPIndex);
  }

  bool IsReturnBlock = (MBBI != MBB.end() && MBBI->isReturn());

  if (IsReturnBlock) {
    unsigned RetOpcode = MBBI->getOpcode();
    bool UsesTCRet =  RetOpcode == PPC::TCRETURNri ||
                      RetOpcode == PPC::TCRETURNdi ||
                      RetOpcode == PPC::TCRETURNai ||
                      RetOpcode == PPC::TCRETURNri8 ||
                      RetOpcode == PPC::TCRETURNdi8 ||
                      RetOpcode == PPC::TCRETURNai8;

    if (UsesTCRet) {
      int MaxTCRetDelta = FI->getTailCallSPDelta();
      MachineOperand &StackAdjust = MBBI->getOperand(1);
      assert(StackAdjust.isImm() && "Expecting immediate value.");
      // Adjust stack pointer.
      int StackAdj = StackAdjust.getImm();
      int Delta = StackAdj - MaxTCRetDelta;
      assert((Delta >= 0) && "Delta must be positive");
      if (MaxTCRetDelta>0)
        FrameSize += (StackAdj +Delta);
      else
        FrameSize += StackAdj;
    }
  }

  // Frames of 32KB & larger require special handling because they cannot be
  // indexed into with a simple LD/LWZ immediate offset operand.
  bool isLargeFrame = !isInt<16>(FrameSize);

  // On targets without red zone, the SP needs to be restored last, so that
  // all live contents of the stack frame are upwards of the SP. This means
  // that we cannot restore SP just now, since there may be more registers
  // to restore from the stack frame (e.g. R31). If the frame size is not
  // a simple immediate value, we will need a spare register to hold the
  // restored SP. If the frame size is known and small, we can simply adjust
  // the offsets of the registers to be restored, and still use SP to restore
  // them. In such case, the final update of SP will be to add the frame
  // size to it.
  // To simplify the code, set RBReg to the base register used to restore
  // values from the stack, and set SPAdd to the value that needs to be added
  // to the SP at the end. The default values are as if red zone was present.
  unsigned RBReg = SPReg;
  uint64_t SPAdd = 0;

  // Check if we can move the stack update instruction up the epilogue
  // past the callee saves. This will allow the move to LR instruction
  // to be executed before the restores of the callee saves which means
  // that the callee saves can hide the latency from the MTLR instrcution.
  MachineBasicBlock::iterator StackUpdateLoc = MBBI;
  if (stackUpdateCanBeMoved(MF)) {
    const std::vector<CalleeSavedInfo> & Info = MFI.getCalleeSavedInfo();
    for (CalleeSavedInfo CSI : Info) {
      // If the callee saved register is spilled to another register abort the
      // stack update movement.
      if (CSI.isSpilledToReg()) {
        StackUpdateLoc = MBBI;
        break;
      }
      int FrIdx = CSI.getFrameIdx();
      // If the frame index is not negative the callee saved info belongs to a
      // stack object that is not a fixed stack object. We ignore non-fixed
      // stack objects because we won't move the update of the stack pointer
      // past them.
      if (FrIdx >= 0)
        continue;

      if (MFI.isFixedObjectIndex(FrIdx) && MFI.getObjectOffset(FrIdx) < 0)
        StackUpdateLoc--;
      else {
        // Abort the operation as we can't update all CSR restores.
        StackUpdateLoc = MBBI;
        break;
      }
    }
  }

  if (FrameSize) {
    // In the prologue, the loaded (or persistent) stack pointer value is
    // offset by the STDU/STDUX/STWU/STWUX instruction. For targets with red
    // zone add this offset back now.

    // If the function has a base pointer, the stack pointer has been copied
    // to it so we can restore it by copying in the other direction.
    if (HasRedZone && HasBP) {
      BuildMI(MBB, MBBI, dl, OrInst, RBReg).
        addReg(BPReg).
        addReg(BPReg);
    }
    // If this function contained a fastcc call and GuaranteedTailCallOpt is
    // enabled (=> hasFastCall()==true) the fastcc call might contain a tail
    // call which invalidates the stack pointer value in SP(0). So we use the
    // value of R31 in this case. Similar situation exists with setjmp.
    else if (FI->hasFastCall() || MF.exposesReturnsTwice()) {
      assert(HasFP && "Expecting a valid frame pointer.");
      if (!HasRedZone)
        RBReg = FPReg;
      if (!isLargeFrame) {
        BuildMI(MBB, MBBI, dl, AddImmInst, RBReg)
          .addReg(FPReg).addImm(FrameSize);
      } else {
        TII.materializeImmPostRA(MBB, MBBI, dl, ScratchReg, FrameSize);
        BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(RBReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
      }
    } else if (!isLargeFrame && !HasBP && !MFI.hasVarSizedObjects()) {
      if (HasRedZone) {
        BuildMI(MBB, StackUpdateLoc, dl, AddImmInst, SPReg)
          .addReg(SPReg)
          .addImm(FrameSize);
      } else {
        // Make sure that adding FrameSize will not overflow the max offset
        // size.
        assert(FPOffset <= 0 && BPOffset <= 0 && PBPOffset <= 0 &&
               "Local offsets should be negative");
        SPAdd = FrameSize;
        FPOffset += FrameSize;
        BPOffset += FrameSize;
        PBPOffset += FrameSize;
      }
    } else {
      // We don't want to use ScratchReg as a base register, because it
      // could happen to be R0. Use FP instead, but make sure to preserve it.
      if (!HasRedZone) {
        // If FP is not saved, copy it to ScratchReg.
        if (!HasFP)
          BuildMI(MBB, MBBI, dl, OrInst, ScratchReg)
            .addReg(FPReg)
            .addReg(FPReg);
        RBReg = FPReg;
      }
      BuildMI(MBB, StackUpdateLoc, dl, LoadInst, RBReg)
        .addImm(0)
        .addReg(SPReg);
    }
  }
  assert(RBReg != ScratchReg && "Should have avoided ScratchReg");
  // If there is no red zone, ScratchReg may be needed for holding a useful
  // value (although not the base register). Make sure it is not overwritten
  // too early.

  // If we need to restore both the LR and the CR and we only have one
  // available scratch register, we must do them one at a time.
  if (MustSaveCR && SingleScratchReg && MustSaveLR) {
    // Here TempReg == ScratchReg, and in the absence of red zone ScratchReg
    // is live here.
    assert(HasRedZone && "Expecting red zone");
    BuildMI(MBB, MBBI, dl, LoadWordInst, TempReg)
      .addImm(CRSaveOffset)
      .addReg(SPReg);
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      BuildMI(MBB, MBBI, dl, MoveToCRInst, MustSaveCRs[i])
        .addReg(TempReg, getKillRegState(i == e-1));
  }

  // Delay restoring of the LR if ScratchReg is needed. This is ok, since
  // LR is stored in the caller's stack frame. ScratchReg will be needed
  // if RBReg is anything other than SP. We shouldn't use ScratchReg as
  // a base register anyway, because it may happen to be R0.
  bool LoadedLR = false;
  if (MustSaveLR && RBReg == SPReg && isInt<16>(LROffset+SPAdd)) {
    BuildMI(MBB, StackUpdateLoc, dl, LoadInst, ScratchReg)
      .addImm(LROffset+SPAdd)
      .addReg(RBReg);
    LoadedLR = true;
  }

  if (MustSaveCR && !(SingleScratchReg && MustSaveLR)) {
    assert(RBReg == SPReg && "Should be using SP as a base register");
    BuildMI(MBB, MBBI, dl, LoadWordInst, TempReg)
      .addImm(CRSaveOffset)
      .addReg(RBReg);
  }

  if (HasFP) {
    // If there is red zone, restore FP directly, since SP has already been
    // restored. Otherwise, restore the value of FP into ScratchReg.
    if (HasRedZone || RBReg == SPReg)
      BuildMI(MBB, MBBI, dl, LoadInst, FPReg)
        .addImm(FPOffset)
        .addReg(SPReg);
    else
      BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
        .addImm(FPOffset)
        .addReg(RBReg);
  }

  if (FI->usesPICBase())
    BuildMI(MBB, MBBI, dl, LoadInst, PPC::R30)
      .addImm(PBPOffset)
      .addReg(RBReg);

  if (HasBP)
    BuildMI(MBB, MBBI, dl, LoadInst, BPReg)
      .addImm(BPOffset)
      .addReg(RBReg);

  // There is nothing more to be loaded from the stack, so now we can
  // restore SP: SP = RBReg + SPAdd.
  if (RBReg != SPReg || SPAdd != 0) {
    assert(!HasRedZone && "This should not happen with red zone");
    // If SPAdd is 0, generate a copy.
    if (SPAdd == 0)
      BuildMI(MBB, MBBI, dl, OrInst, SPReg)
        .addReg(RBReg)
        .addReg(RBReg);
    else
      BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
        .addReg(RBReg)
        .addImm(SPAdd);

    assert(RBReg != ScratchReg && "Should be using FP or SP as base register");
    if (RBReg == FPReg)
      BuildMI(MBB, MBBI, dl, OrInst, FPReg)
        .addReg(ScratchReg)
        .addReg(ScratchReg);

    // Now load the LR from the caller's stack frame.
    if (MustSaveLR && !LoadedLR)
      BuildMI(MBB, MBBI, dl, LoadInst, ScratchReg)
        .addImm(LROffset)
        .addReg(SPReg);
  }

  if (MustSaveCR &&
      !(SingleScratchReg && MustSaveLR))
    for (unsigned i = 0, e = MustSaveCRs.size(); i != e; ++i)
      BuildMI(MBB, MBBI, dl, MoveToCRInst, MustSaveCRs[i])
        .addReg(TempReg, getKillRegState(i == e-1));

  if (MustSaveLR) {
    // If ROP protection is required, an extra instruction is added to compute a
    // hash and then compare it to the hash stored in the prologue.
    if (HasROPProtect) {
      const int SaveIndex = FI->getROPProtectionHashSaveIndex();
      const int64_t ImmOffset = MFI.getObjectOffset(SaveIndex);
      assert((ImmOffset <= -8 && ImmOffset >= -512) &&
             "ROP hash check location offset out of range.");
      assert(((ImmOffset & 0x7) == 0) &&
             "ROP hash check location offset must be 8 byte aligned.");
      BuildMI(MBB, StackUpdateLoc, dl, HashChk)
          .addReg(ScratchReg)
          .addImm(ImmOffset)
          .addReg(SPReg);
    }
    BuildMI(MBB, StackUpdateLoc, dl, MTLRInst).addReg(ScratchReg);
  }

  // Callee pop calling convention. Pop parameter/linkage area. Used for tail
  // call optimization
  if (IsReturnBlock) {
    unsigned RetOpcode = MBBI->getOpcode();
    if (MF.getTarget().Options.GuaranteedTailCallOpt &&
        (RetOpcode == PPC::BLR || RetOpcode == PPC::BLR8) &&
        MF.getFunction().getCallingConv() == CallingConv::Fast) {
      PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
      unsigned CallerAllocatedAmt = FI->getMinReservedArea();

      if (CallerAllocatedAmt && isInt<16>(CallerAllocatedAmt)) {
        BuildMI(MBB, MBBI, dl, AddImmInst, SPReg)
          .addReg(SPReg).addImm(CallerAllocatedAmt);
      } else {
        BuildMI(MBB, MBBI, dl, LoadImmShiftedInst, ScratchReg)
          .addImm(CallerAllocatedAmt >> 16);
        BuildMI(MBB, MBBI, dl, OrImmInst, ScratchReg)
          .addReg(ScratchReg, RegState::Kill)
          .addImm(CallerAllocatedAmt & 0xFFFF);
        BuildMI(MBB, MBBI, dl, AddInst)
          .addReg(SPReg)
          .addReg(FPReg)
          .addReg(ScratchReg);
      }
    } else {
      createTailCallBranchInstr(MBB);
    }
  }
}

void PPCFrameLowering::createTailCallBranchInstr(MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();

  // If we got this far a first terminator should exist.
  assert(MBBI != MBB.end() && "Failed to find the first terminator.");

  DebugLoc dl = MBBI->getDebugLoc();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();

  // Create branch instruction for pseudo tail call return instruction.
  // The TCRETURNdi variants are direct calls. Valid targets for those are
  // MO_GlobalAddress operands as well as MO_ExternalSymbol with PC-Rel
  // since we can tail call external functions with PC-Rel (i.e. we don't need
  // to worry about different TOC pointers). Some of the external functions will
  // be MO_GlobalAddress while others like memcpy for example, are going to
  // be MO_ExternalSymbol.
  unsigned RetOpcode = MBBI->getOpcode();
  if (RetOpcode == PPC::TCRETURNdi) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    if (JumpTarget.isGlobal())
      BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB)).
        addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
    else if (JumpTarget.isSymbol())
      BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB)).
        addExternalSymbol(JumpTarget.getSymbolName());
    else
      llvm_unreachable("Expecting Global or External Symbol");
  } else if (RetOpcode == PPC::TCRETURNri) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR));
  } else if (RetOpcode == PPC::TCRETURNai) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA)).addImm(JumpTarget.getImm());
  } else if (RetOpcode == PPC::TCRETURNdi8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    if (JumpTarget.isGlobal())
      BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB8)).
        addGlobalAddress(JumpTarget.getGlobal(), JumpTarget.getOffset());
    else if (JumpTarget.isSymbol())
      BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILB8)).
        addExternalSymbol(JumpTarget.getSymbolName());
    else
      llvm_unreachable("Expecting Global or External Symbol");
  } else if (RetOpcode == PPC::TCRETURNri8) {
    MBBI = MBB.getLastNonDebugInstr();
    assert(MBBI->getOperand(0).isReg() && "Expecting register operand.");
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBCTR8));
  } else if (RetOpcode == PPC::TCRETURNai8) {
    MBBI = MBB.getLastNonDebugInstr();
    MachineOperand &JumpTarget = MBBI->getOperand(0);
    BuildMI(MBB, MBBI, dl, TII.get(PPC::TAILBA8)).addImm(JumpTarget.getImm());
  }
}

void PPCFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  if (Subtarget.isAIXABI())
    updateCalleeSaves(MF, SavedRegs);

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  // Do not explicitly save the callee saved VSRp registers.
  // The individual VSR subregisters will be saved instead.
  SavedRegs.reset(PPC::VSRp26);
  SavedRegs.reset(PPC::VSRp27);
  SavedRegs.reset(PPC::VSRp28);
  SavedRegs.reset(PPC::VSRp29);
  SavedRegs.reset(PPC::VSRp30);
  SavedRegs.reset(PPC::VSRp31);

  //  Save and clear the LR state.
  PPCFunctionInfo *FI = MF.getInfo<PPCFunctionInfo>();
  unsigned LR = RegInfo->getRARegister();
  FI->setMustSaveLR(MustSaveLR(MF, LR));
  SavedRegs.reset(LR);

  //  Save R31 if necessary
  int FPSI = FI->getFramePointerSaveIndex();
  const bool isPPC64 = Subtarget.isPPC64();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // If the frame pointer save index hasn't been defined yet.
  if (!FPSI && needsFP(MF)) {
    // Find out what the fix offset of the frame pointer save area.
    int FPOffset = getFramePointerSaveOffset();
    // Allocate the frame index for frame pointer save area.
    FPSI = MFI.CreateFixedObject(isPPC64? 8 : 4, FPOffset, true);
    // Save the result.
    FI->setFramePointerSaveIndex(FPSI);
  }

  int BPSI = FI->getBasePointerSaveIndex();
  if (!BPSI && RegInfo->hasBasePointer(MF)) {
    int BPOffset = getBasePointerSaveOffset();
    // Allocate the frame index for the base pointer save area.
    BPSI = MFI.CreateFixedObject(isPPC64? 8 : 4, BPOffset, true);
    // Save the result.
    FI->setBasePointerSaveIndex(BPSI);
  }

  // Reserve stack space for the PIC Base register (R30).
  // Only used in SVR4 32-bit.
  if (FI->usesPICBase()) {
    int PBPSI = MFI.CreateFixedObject(4, -8, true);
    FI->setPICBasePointerSaveIndex(PBPSI);
  }

  // Make sure we don't explicitly spill r31, because, for example, we have
  // some inline asm which explicitly clobbers it, when we otherwise have a
  // frame pointer and are using r31's spill slot for the prologue/epilogue
  // code. Same goes for the base pointer and the PIC base register.
  if (needsFP(MF))
    SavedRegs.reset(isPPC64 ? PPC::X31 : PPC::R31);
  if (RegInfo->hasBasePointer(MF)) {
    SavedRegs.reset(RegInfo->getBaseRegister(MF));
    // On AIX, when BaseRegister(R30) is used, need to spill r31 too to match
    // AIX trackback table requirement.
    if (!needsFP(MF) && !SavedRegs.test(isPPC64 ? PPC::X31 : PPC::R31) &&
        Subtarget.isAIXABI()) {
      assert(
          (RegInfo->getBaseRegister(MF) == (isPPC64 ? PPC::X30 : PPC::R30)) &&
          "Invalid base register on AIX!");
      SavedRegs.set(isPPC64 ? PPC::X31 : PPC::R31);
    }
  }
  if (FI->usesPICBase())
    SavedRegs.reset(PPC::R30);

  // Reserve stack space to move the linkage area to in case of a tail call.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = FI->getTailCallSPDelta()) < 0) {
    MFI.CreateFixedObject(-1 * TCSPDelta, TCSPDelta, true);
  }

  // Allocate the nonvolatile CR spill slot iff the function uses CR 2, 3, or 4.
  // For 64-bit SVR4, and all flavors of AIX we create a FixedStack
  // object at the offset of the CR-save slot in the linkage area. The actual
  // save and restore of the condition register will be created as part of the
  // prologue and epilogue insertion, but the FixedStack object is needed to
  // keep the CalleSavedInfo valid.
  if ((SavedRegs.test(PPC::CR2) || SavedRegs.test(PPC::CR3) ||
       SavedRegs.test(PPC::CR4))) {
    const uint64_t SpillSize = 4; // Condition register is always 4 bytes.
    const int64_t SpillOffset =
        Subtarget.isPPC64() ? 8 : Subtarget.isAIXABI() ? 4 : -4;
    int FrameIdx =
        MFI.CreateFixedObject(SpillSize, SpillOffset,
                              /* IsImmutable */ true, /* IsAliased */ false);
    FI->setCRSpillFrameIndex(FrameIdx);
  }
}

void PPCFrameLowering::processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                                       RegScavenger *RS) const {
  // Get callee saved register information.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();

  // If the function is shrink-wrapped, and if the function has a tail call, the
  // tail call might not be in the new RestoreBlock, so real branch instruction
  // won't be generated by emitEpilogue(), because shrink-wrap has chosen new
  // RestoreBlock. So we handle this case here.
  if (MFI.getSavePoint() && MFI.hasTailCall()) {
    MachineBasicBlock *RestoreBlock = MFI.getRestorePoint();
    for (MachineBasicBlock &MBB : MF) {
      if (MBB.isReturnBlock() && (&MBB) != RestoreBlock)
        createTailCallBranchInstr(MBB);
    }
  }

  // Early exit if no callee saved registers are modified!
  if (CSI.empty() && !needsFP(MF)) {
    addScavengingSpillSlot(MF, RS);
    return;
  }

  unsigned MinGPR = PPC::R31;
  unsigned MinG8R = PPC::X31;
  unsigned MinFPR = PPC::F31;
  unsigned MinVR = Subtarget.hasSPE() ? PPC::S31 : PPC::V31;

  bool HasGPSaveArea = false;
  bool HasG8SaveArea = false;
  bool HasFPSaveArea = false;
  bool HasVRSaveArea = false;

  SmallVector<CalleeSavedInfo, 18> GPRegs;
  SmallVector<CalleeSavedInfo, 18> G8Regs;
  SmallVector<CalleeSavedInfo, 18> FPRegs;
  SmallVector<CalleeSavedInfo, 18> VRegs;

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();
    assert((!MF.getInfo<PPCFunctionInfo>()->mustSaveTOC() ||
            (Reg != PPC::X2 && Reg != PPC::R2)) &&
           "Not expecting to try to spill R2 in a function that must save TOC");
    if (PPC::GPRCRegClass.contains(Reg)) {
      HasGPSaveArea = true;

      GPRegs.push_back(I);

      if (Reg < MinGPR) {
        MinGPR = Reg;
      }
    } else if (PPC::G8RCRegClass.contains(Reg)) {
      HasG8SaveArea = true;

      G8Regs.push_back(I);

      if (Reg < MinG8R) {
        MinG8R = Reg;
      }
    } else if (PPC::F8RCRegClass.contains(Reg)) {
      HasFPSaveArea = true;

      FPRegs.push_back(I);

      if (Reg < MinFPR) {
        MinFPR = Reg;
      }
    } else if (PPC::CRBITRCRegClass.contains(Reg) ||
               PPC::CRRCRegClass.contains(Reg)) {
      ; // do nothing, as we already know whether CRs are spilled
    } else if (PPC::VRRCRegClass.contains(Reg) ||
               PPC::SPERCRegClass.contains(Reg)) {
      // Altivec and SPE are mutually exclusive, but have the same stack
      // alignment requirements, so overload the save area for both cases.
      HasVRSaveArea = true;

      VRegs.push_back(I);

      if (Reg < MinVR) {
        MinVR = Reg;
      }
    } else {
      llvm_unreachable("Unknown RegisterClass!");
    }
  }

  PPCFunctionInfo *PFI = MF.getInfo<PPCFunctionInfo>();
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();

  int64_t LowerBound = 0;

  // Take into account stack space reserved for tail calls.
  int TCSPDelta = 0;
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      (TCSPDelta = PFI->getTailCallSPDelta()) < 0) {
    LowerBound = TCSPDelta;
  }

  // The Floating-point register save area is right below the back chain word
  // of the previous stack frame.
  if (HasFPSaveArea) {
    for (unsigned i = 0, e = FPRegs.size(); i != e; ++i) {
      int FI = FPRegs[i].getFrameIdx();

      MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    }

    LowerBound -= (31 - TRI->getEncodingValue(MinFPR) + 1) * 8;
  }

  // Check whether the frame pointer register is allocated. If so, make sure it
  // is spilled to the correct offset.
  if (needsFP(MF)) {
    int FI = PFI->getFramePointerSaveIndex();
    assert(FI && "No Frame Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    // FP is R31/X31, so no need to update MinGPR/MinG8R.
    HasGPSaveArea = true;
  }

  if (PFI->usesPICBase()) {
    int FI = PFI->getPICBasePointerSaveIndex();
    assert(FI && "No PIC Base Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));

    MinGPR = std::min<unsigned>(MinGPR, PPC::R30);
    HasGPSaveArea = true;
  }

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  if (RegInfo->hasBasePointer(MF)) {
    int FI = PFI->getBasePointerSaveIndex();
    assert(FI && "No Base Pointer Save Slot!");
    MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));

    Register BP = RegInfo->getBaseRegister(MF);
    if (PPC::G8RCRegClass.contains(BP)) {
      MinG8R = std::min<unsigned>(MinG8R, BP);
      HasG8SaveArea = true;
    } else if (PPC::GPRCRegClass.contains(BP)) {
      MinGPR = std::min<unsigned>(MinGPR, BP);
      HasGPSaveArea = true;
    }
  }

  // General register save area starts right below the Floating-point
  // register save area.
  if (HasGPSaveArea || HasG8SaveArea) {
    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = GPRegs.size(); i != e; ++i) {
      if (!GPRegs[i].isSpilledToReg()) {
        int FI = GPRegs[i].getFrameIdx();
        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    // Move general register save area spill slots down, taking into account
    // the size of the Floating-point register save area.
    for (unsigned i = 0, e = G8Regs.size(); i != e; ++i) {
      if (!G8Regs[i].isSpilledToReg()) {
        int FI = G8Regs[i].getFrameIdx();
        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
      }
    }

    unsigned MinReg =
      std::min<unsigned>(TRI->getEncodingValue(MinGPR),
                         TRI->getEncodingValue(MinG8R));

    const unsigned GPRegSize = Subtarget.isPPC64() ? 8 : 4;
    LowerBound -= (31 - MinReg + 1) * GPRegSize;
  }

  // For 32-bit only, the CR save area is below the general register
  // save area.  For 64-bit SVR4, the CR save area is addressed relative
  // to the stack pointer and hence does not need an adjustment here.
  // Only CR2 (the first nonvolatile spilled) has an associated frame
  // index so that we have a single uniform save area.
  if (spillsCR(MF) && Subtarget.is32BitELFABI()) {
    // Adjust the frame index of the CR spill slot.
    for (const auto &CSInfo : CSI) {
      if (CSInfo.getReg() == PPC::CR2) {
        int FI = CSInfo.getFrameIdx();
        MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
        break;
      }
    }

    LowerBound -= 4; // The CR save area is always 4 bytes long.
  }

  // Both Altivec and SPE have the same alignment and padding requirements
  // within the stack frame.
  if (HasVRSaveArea) {
    // Insert alignment padding, we need 16-byte alignment. Note: for positive
    // number the alignment formula is : y = (x + (n-1)) & (~(n-1)). But since
    // we are using negative number here (the stack grows downward). We should
    // use formula : y = x & (~(n-1)). Where x is the size before aligning, n
    // is the alignment size ( n = 16 here) and y is the size after aligning.
    assert(LowerBound <= 0 && "Expect LowerBound have a non-positive value!");
    LowerBound &= ~(15);

    for (unsigned i = 0, e = VRegs.size(); i != e; ++i) {
      int FI = VRegs[i].getFrameIdx();

      MFI.setObjectOffset(FI, LowerBound + MFI.getObjectOffset(FI));
    }
  }

  addScavengingSpillSlot(MF, RS);
}

void
PPCFrameLowering::addScavengingSpillSlot(MachineFunction &MF,
                                         RegScavenger *RS) const {
  // Reserve a slot closest to SP or frame pointer if we have a dynalloc or
  // a large stack, which will require scavenging a register to materialize a
  // large offset.

  // We need to have a scavenger spill slot for spills if the frame size is
  // large. In case there is no free register for large-offset addressing,
  // this slot is used for the necessary emergency spill. Also, we need the
  // slot for dynamic stack allocations.

  // The scavenger might be invoked if the frame offset does not fit into
  // the 16-bit immediate in case of not SPE and 8-bit in case of SPE.
  // We don't know the complete frame size here because we've not yet computed
  // callee-saved register spills or the needed alignment padding.
  unsigned StackSize = determineFrameLayout(MF, true);
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool NeedSpills = Subtarget.hasSPE() ? !isInt<8>(StackSize) : !isInt<16>(StackSize);

  if (MFI.hasVarSizedObjects() || spillsCR(MF) || hasNonRISpills(MF) ||
      (hasSpills(MF) && NeedSpills)) {
    const TargetRegisterClass &GPRC = PPC::GPRCRegClass;
    const TargetRegisterClass &G8RC = PPC::G8RCRegClass;
    const TargetRegisterClass &RC = Subtarget.isPPC64() ? G8RC : GPRC;
    const TargetRegisterInfo &TRI = *Subtarget.getRegisterInfo();
    unsigned Size = TRI.getSpillSize(RC);
    Align Alignment = TRI.getSpillAlign(RC);
    RS->addScavengingFrameIndex(MFI.CreateStackObject(Size, Alignment, false));

    // Might we have over-aligned allocas?
    bool HasAlVars =
        MFI.hasVarSizedObjects() && MFI.getMaxAlign() > getStackAlign();

    // These kinds of spills might need two registers.
    if (spillsCR(MF) || HasAlVars)
      RS->addScavengingFrameIndex(
          MFI.CreateStackObject(Size, Alignment, false));
  }
}

// This function checks if a callee saved gpr can be spilled to a volatile
// vector register. This occurs for leaf functions when the option
// ppc-enable-pe-vector-spills is enabled. If there are any remaining registers
// which were not spilled to vectors, return false so the target independent
// code can handle them by assigning a FrameIdx to a stack slot.
bool PPCFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {

  if (CSI.empty())
    return true; // Early exit if no callee saved registers are modified!

  const PPCRegisterInfo *RegInfo = Subtarget.getRegisterInfo();
  const MCPhysReg *CSRegs = RegInfo->getCalleeSavedRegs(&MF);
  const MachineRegisterInfo &MRI = MF.getRegInfo();

  if (Subtarget.hasSPE()) {
    // In case of SPE we only have SuperRegs and CRs
    // in our CalleSaveInfo vector.

    for (auto &CalleeSaveReg : CSI) {
      MCPhysReg Reg = CalleeSaveReg.getReg();
      MCPhysReg Lower = RegInfo->getSubReg(Reg, 1);
      MCPhysReg Higher = RegInfo->getSubReg(Reg, 2);

      if ( // Check only for SuperRegs.
          Lower &&
          // Replace Reg if only lower-32 bits modified
          !MRI.isPhysRegModified(Higher))
        CalleeSaveReg = CalleeSavedInfo(Lower);
    }
  }

  // Early exit if cannot spill gprs to volatile vector registers.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!EnablePEVectorSpills || MFI.hasCalls() || !Subtarget.hasP9Vector())
    return false;

  // Build a BitVector of VSRs that can be used for spilling GPRs.
  BitVector BVAllocatable = TRI->getAllocatableSet(MF);
  BitVector BVCalleeSaved(TRI->getNumRegs());
  for (unsigned i = 0; CSRegs[i]; ++i)
    BVCalleeSaved.set(CSRegs[i]);

  for (unsigned Reg : BVAllocatable.set_bits()) {
    // Set to 0 if the register is not a volatile VSX register, or if it is
    // used in the function.
    if (BVCalleeSaved[Reg] || !PPC::VSRCRegClass.contains(Reg) ||
        MRI.isPhysRegUsed(Reg))
      BVAllocatable.reset(Reg);
  }

  bool AllSpilledToReg = true;
  unsigned LastVSRUsedForSpill = 0;
  for (auto &CS : CSI) {
    if (BVAllocatable.none())
      return false;

    Register Reg = CS.getReg();

    if (!PPC::G8RCRegClass.contains(Reg)) {
      AllSpilledToReg = false;
      continue;
    }

    // For P9, we can reuse LastVSRUsedForSpill to spill two GPRs
    // into one VSR using the mtvsrdd instruction.
    if (LastVSRUsedForSpill != 0) {
      CS.setDstReg(LastVSRUsedForSpill);
      BVAllocatable.reset(LastVSRUsedForSpill);
      LastVSRUsedForSpill = 0;
      continue;
    }

    unsigned VolatileVFReg = BVAllocatable.find_first();
    if (VolatileVFReg < BVAllocatable.size()) {
      CS.setDstReg(VolatileVFReg);
      LastVSRUsedForSpill = VolatileVFReg;
    } else {
      AllSpilledToReg = false;
    }
  }
  return AllSpilledToReg;
}

bool PPCFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  PPCFunctionInfo *FI = MF->getInfo<PPCFunctionInfo>();
  bool MustSaveTOC = FI->mustSaveTOC();
  DebugLoc DL;
  bool CRSpilled = false;
  MachineInstrBuilder CRMIB;
  BitVector Spilled(TRI->getNumRegs());

  VSRContainingGPRs.clear();

  // Map each VSR to GPRs to be spilled with into it. Single VSR can contain one
  // or two GPRs, so we need table to record information for later save/restore.
  for (const CalleeSavedInfo &Info : CSI) {
    if (Info.isSpilledToReg()) {
      auto &SpilledVSR =
          VSRContainingGPRs.FindAndConstruct(Info.getDstReg()).second;
      assert(SpilledVSR.second == 0 &&
             "Can't spill more than two GPRs into VSR!");
      if (SpilledVSR.first == 0)
        SpilledVSR.first = Info.getReg();
      else
        SpilledVSR.second = Info.getReg();
    }
  }

  for (const CalleeSavedInfo &I : CSI) {
    Register Reg = I.getReg();

    // CR2 through CR4 are the nonvolatile CR fields.
    bool IsCRField = PPC::CR2 <= Reg && Reg <= PPC::CR4;

    // Add the callee-saved register as live-in; it's killed at the spill.
    // Do not do this for callee-saved registers that are live-in to the
    // function because they will already be marked live-in and this will be
    // adding it for a second time. It is an error to add the same register
    // to the set more than once.
    const MachineRegisterInfo &MRI = MF->getRegInfo();
    bool IsLiveIn = MRI.isLiveIn(Reg);
    if (!IsLiveIn)
       MBB.addLiveIn(Reg);

    if (CRSpilled && IsCRField) {
      CRMIB.addReg(Reg, RegState::ImplicitKill);
      continue;
    }

    // The actual spill will happen in the prologue.
    if ((Reg == PPC::X2 || Reg == PPC::R2) && MustSaveTOC)
      continue;

    // Insert the spill to the stack frame.
    if (IsCRField) {
      PPCFunctionInfo *FuncInfo = MF->getInfo<PPCFunctionInfo>();
      if (!Subtarget.is32BitELFABI()) {
        // The actual spill will happen at the start of the prologue.
        FuncInfo->addMustSaveCR(Reg);
      } else {
        CRSpilled = true;
        FuncInfo->setSpillsCR();

        // 32-bit:  FP-relative.  Note that we made sure CR2-CR4 all have
        // the same frame index in PPCRegisterInfo::hasReservedSpillSlot.
        CRMIB = BuildMI(*MF, DL, TII.get(PPC::MFCR), PPC::R12)
                  .addReg(Reg, RegState::ImplicitKill);

        MBB.insert(MI, CRMIB);
        MBB.insert(MI, addFrameReference(BuildMI(*MF, DL, TII.get(PPC::STW))
                                         .addReg(PPC::R12,
                                                 getKillRegState(true)),
                                         I.getFrameIdx()));
      }
    } else {
      if (I.isSpilledToReg()) {
        unsigned Dst = I.getDstReg();

        if (Spilled[Dst])
          continue;

        if (VSRContainingGPRs[Dst].second != 0) {
          assert(Subtarget.hasP9Vector() &&
                 "mtvsrdd is unavailable on pre-P9 targets.");

          NumPESpillVSR += 2;
          BuildMI(MBB, MI, DL, TII.get(PPC::MTVSRDD), Dst)
              .addReg(VSRContainingGPRs[Dst].first, getKillRegState(true))
              .addReg(VSRContainingGPRs[Dst].second, getKillRegState(true));
        } else if (VSRContainingGPRs[Dst].second == 0) {
          assert(Subtarget.hasP8Vector() &&
                 "Can't move GPR to VSR on pre-P8 targets.");

          ++NumPESpillVSR;
          BuildMI(MBB, MI, DL, TII.get(PPC::MTVSRD),
                  TRI->getSubReg(Dst, PPC::sub_64))
              .addReg(VSRContainingGPRs[Dst].first, getKillRegState(true));
        } else {
          llvm_unreachable("More than two GPRs spilled to a VSR!");
        }
        Spilled.set(Dst);
      } else {
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
        // Use !IsLiveIn for the kill flag.
        // We do not want to kill registers that are live in this function
        // before their use because they will become undefined registers.
        // Functions without NoUnwind need to preserve the order of elements in
        // saved vector registers.
        if (Subtarget.needsSwapsForVSXMemOps() &&
            !MF->getFunction().hasFnAttribute(Attribute::NoUnwind))
          TII.storeRegToStackSlotNoUpd(MBB, MI, Reg, !IsLiveIn,
                                       I.getFrameIdx(), RC, TRI);
        else
          TII.storeRegToStackSlot(MBB, MI, Reg, !IsLiveIn, I.getFrameIdx(), RC,
                                  TRI, Register());
      }
    }
  }
  return true;
}

static void restoreCRs(bool is31, bool CR2Spilled, bool CR3Spilled,
                       bool CR4Spilled, MachineBasicBlock &MBB,
                       MachineBasicBlock::iterator MI,
                       ArrayRef<CalleeSavedInfo> CSI, unsigned CSIIndex) {

  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *MF->getSubtarget<PPCSubtarget>().getInstrInfo();
  DebugLoc DL;
  unsigned MoveReg = PPC::R12;

  // 32-bit:  FP-relative
  MBB.insert(MI,
             addFrameReference(BuildMI(*MF, DL, TII.get(PPC::LWZ), MoveReg),
                               CSI[CSIIndex].getFrameIdx()));

  unsigned RestoreOp = PPC::MTOCRF;
  if (CR2Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR2)
               .addReg(MoveReg, getKillRegState(!CR3Spilled && !CR4Spilled)));

  if (CR3Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR3)
               .addReg(MoveReg, getKillRegState(!CR4Spilled)));

  if (CR4Spilled)
    MBB.insert(MI, BuildMI(*MF, DL, TII.get(RestoreOp), PPC::CR4)
               .addReg(MoveReg, getKillRegState(true)));
}

MachineBasicBlock::iterator PPCFrameLowering::
eliminateCallFramePseudoInstr(MachineFunction &MF, MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator I) const {
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  if (MF.getTarget().Options.GuaranteedTailCallOpt &&
      I->getOpcode() == PPC::ADJCALLSTACKUP) {
    // Add (actually subtract) back the amount the callee popped on return.
    if (int CalleeAmt =  I->getOperand(1).getImm()) {
      bool is64Bit = Subtarget.isPPC64();
      CalleeAmt *= -1;
      unsigned StackReg = is64Bit ? PPC::X1 : PPC::R1;
      unsigned TmpReg = is64Bit ? PPC::X0 : PPC::R0;
      unsigned ADDIInstr = is64Bit ? PPC::ADDI8 : PPC::ADDI;
      unsigned ADDInstr = is64Bit ? PPC::ADD8 : PPC::ADD4;
      unsigned LISInstr = is64Bit ? PPC::LIS8 : PPC::LIS;
      unsigned ORIInstr = is64Bit ? PPC::ORI8 : PPC::ORI;
      const DebugLoc &dl = I->getDebugLoc();

      if (isInt<16>(CalleeAmt)) {
        BuildMI(MBB, I, dl, TII.get(ADDIInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addImm(CalleeAmt);
      } else {
        MachineBasicBlock::iterator MBBI = I;
        BuildMI(MBB, MBBI, dl, TII.get(LISInstr), TmpReg)
          .addImm(CalleeAmt >> 16);
        BuildMI(MBB, MBBI, dl, TII.get(ORIInstr), TmpReg)
          .addReg(TmpReg, RegState::Kill)
          .addImm(CalleeAmt & 0xFFFF);
        BuildMI(MBB, MBBI, dl, TII.get(ADDInstr), StackReg)
          .addReg(StackReg, RegState::Kill)
          .addReg(TmpReg);
      }
    }
  }
  // Simply discard ADJCALLSTACKDOWN, ADJCALLSTACKUP instructions.
  return MBB.erase(I);
}

static bool isCalleeSavedCR(unsigned Reg) {
  return PPC::CR2 == Reg || Reg == PPC::CR3 || Reg == PPC::CR4;
}

bool PPCFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  MachineFunction *MF = MBB.getParent();
  const PPCInstrInfo &TII = *Subtarget.getInstrInfo();
  PPCFunctionInfo *FI = MF->getInfo<PPCFunctionInfo>();
  bool MustSaveTOC = FI->mustSaveTOC();
  bool CR2Spilled = false;
  bool CR3Spilled = false;
  bool CR4Spilled = false;
  unsigned CSIIndex = 0;
  BitVector Restored(TRI->getNumRegs());

  // Initialize insertion-point logic; we will be restoring in reverse
  // order of spill.
  MachineBasicBlock::iterator I = MI, BeforeI = I;
  bool AtStart = I == MBB.begin();

  if (!AtStart)
    --BeforeI;

  for (unsigned i = 0, e = CSI.size(); i != e; ++i) {
    Register Reg = CSI[i].getReg();

    if ((Reg == PPC::X2 || Reg == PPC::R2) && MustSaveTOC)
      continue;

    // Restore of callee saved condition register field is handled during
    // epilogue insertion.
    if (isCalleeSavedCR(Reg) && !Subtarget.is32BitELFABI())
      continue;

    if (Reg == PPC::CR2) {
      CR2Spilled = true;
      // The spill slot is associated only with CR2, which is the
      // first nonvolatile spilled.  Save it here.
      CSIIndex = i;
      continue;
    } else if (Reg == PPC::CR3) {
      CR3Spilled = true;
      continue;
    } else if (Reg == PPC::CR4) {
      CR4Spilled = true;
      continue;
    } else {
      // On 32-bit ELF when we first encounter a non-CR register after seeing at
      // least one CR register, restore all spilled CRs together.
      if (CR2Spilled || CR3Spilled || CR4Spilled) {
        bool is31 = needsFP(*MF);
        restoreCRs(is31, CR2Spilled, CR3Spilled, CR4Spilled, MBB, I, CSI,
                   CSIIndex);
        CR2Spilled = CR3Spilled = CR4Spilled = false;
      }

      if (CSI[i].isSpilledToReg()) {
        DebugLoc DL;
        unsigned Dst = CSI[i].getDstReg();

        if (Restored[Dst])
          continue;

        if (VSRContainingGPRs[Dst].second != 0) {
          assert(Subtarget.hasP9Vector());
          NumPEReloadVSR += 2;
          BuildMI(MBB, I, DL, TII.get(PPC::MFVSRLD),
                  VSRContainingGPRs[Dst].second)
              .addReg(Dst);
          BuildMI(MBB, I, DL, TII.get(PPC::MFVSRD),
                  VSRContainingGPRs[Dst].first)
              .addReg(TRI->getSubReg(Dst, PPC::sub_64), getKillRegState(true));
        } else if (VSRContainingGPRs[Dst].second == 0) {
          assert(Subtarget.hasP8Vector());
          ++NumPEReloadVSR;
          BuildMI(MBB, I, DL, TII.get(PPC::MFVSRD),
                  VSRContainingGPRs[Dst].first)
              .addReg(TRI->getSubReg(Dst, PPC::sub_64), getKillRegState(true));
        } else {
          llvm_unreachable("More than two GPRs spilled to a VSR!");
        }

        Restored.set(Dst);

      } else {
        // Default behavior for non-CR saves.
        const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);

        // Functions without NoUnwind need to preserve the order of elements in
        // saved vector registers.
        if (Subtarget.needsSwapsForVSXMemOps() &&
            !MF->getFunction().hasFnAttribute(Attribute::NoUnwind))
          TII.loadRegFromStackSlotNoUpd(MBB, I, Reg, CSI[i].getFrameIdx(), RC,
                                        TRI);
        else
          TII.loadRegFromStackSlot(MBB, I, Reg, CSI[i].getFrameIdx(), RC, TRI,
                                   Register());

        assert(I != MBB.begin() &&
               "loadRegFromStackSlot didn't insert any code!");
      }
    }

    // Insert in reverse order.
    if (AtStart)
      I = MBB.begin();
    else {
      I = BeforeI;
      ++I;
    }
  }

  // If we haven't yet spilled the CRs, do so now.
  if (CR2Spilled || CR3Spilled || CR4Spilled) {
    assert(Subtarget.is32BitELFABI() &&
           "Only set CR[2|3|4]Spilled on 32-bit SVR4.");
    bool is31 = needsFP(*MF);
    restoreCRs(is31, CR2Spilled, CR3Spilled, CR4Spilled, MBB, I, CSI, CSIIndex);
  }

  return true;
}

uint64_t PPCFrameLowering::getTOCSaveOffset() const {
  return TOCSaveOffset;
}

uint64_t PPCFrameLowering::getFramePointerSaveOffset() const {
  return FramePointerSaveOffset;
}

uint64_t PPCFrameLowering::getBasePointerSaveOffset() const {
  return BasePointerSaveOffset;
}

bool PPCFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  if (MF.getInfo<PPCFunctionInfo>()->shrinkWrapDisabled())
    return false;
  return !MF.getSubtarget<PPCSubtarget>().is32BitELFABI();
}

void PPCFrameLowering::updateCalleeSaves(const MachineFunction &MF,
                                         BitVector &SavedRegs) const {
  // The AIX ABI uses traceback tables for EH which require that if callee-saved
  // register N is used, all registers N-31 must be saved/restored.
  // NOTE: The check for AIX is not actually what is relevant. Traceback tables
  // on Linux have the same requirements. It is just that AIX is the only ABI
  // for which we actually use traceback tables. If another ABI needs to be
  // supported that also uses them, we can add a check such as
  // Subtarget.usesTraceBackTables().
  assert(Subtarget.isAIXABI() &&
         "Function updateCalleeSaves should only be called for AIX.");

  // If there are no callee saves then there is nothing to do.
  if (SavedRegs.none())
    return;

  const MCPhysReg *CSRegs =
      Subtarget.getRegisterInfo()->getCalleeSavedRegs(&MF);
  MCPhysReg LowestGPR = PPC::R31;
  MCPhysReg LowestG8R = PPC::X31;
  MCPhysReg LowestFPR = PPC::F31;
  MCPhysReg LowestVR = PPC::V31;

  // Traverse the CSRs twice so as not to rely on ascending ordering of
  // registers in the array. The first pass finds the lowest numbered
  // register and the second pass marks all higher numbered registers
  // for spilling.
  for (int i = 0; CSRegs[i]; i++) {
    // Get the lowest numbered register for each class that actually needs
    // to be saved.
    MCPhysReg Cand = CSRegs[i];
    if (!SavedRegs.test(Cand))
      continue;
    if (PPC::GPRCRegClass.contains(Cand) && Cand < LowestGPR)
      LowestGPR = Cand;
    else if (PPC::G8RCRegClass.contains(Cand) && Cand < LowestG8R)
      LowestG8R = Cand;
    else if ((PPC::F4RCRegClass.contains(Cand) ||
              PPC::F8RCRegClass.contains(Cand)) &&
             Cand < LowestFPR)
      LowestFPR = Cand;
    else if (PPC::VRRCRegClass.contains(Cand) && Cand < LowestVR)
      LowestVR = Cand;
  }

  for (int i = 0; CSRegs[i]; i++) {
    MCPhysReg Cand = CSRegs[i];
    if ((PPC::GPRCRegClass.contains(Cand) && Cand > LowestGPR) ||
        (PPC::G8RCRegClass.contains(Cand) && Cand > LowestG8R) ||
        ((PPC::F4RCRegClass.contains(Cand) ||
          PPC::F8RCRegClass.contains(Cand)) &&
         Cand > LowestFPR) ||
        (PPC::VRRCRegClass.contains(Cand) && Cand > LowestVR))
      SavedRegs.set(Cand);
  }
}

uint64_t PPCFrameLowering::getStackThreshold() const {
  // On PPC64, we use `stux r1, r1, <scratch_reg>` to extend the stack;
  // use `add r1, r1, <scratch_reg>` to release the stack frame.
  // Scratch register contains a signed 64-bit number, which is negative
  // when extending the stack and is positive when releasing the stack frame.
  // To make `stux` and `add` paired, the absolute value of the number contained
  // in the scratch register should be the same. Thus the maximum stack size
  // is (2^63)-1, i.e., LONG_MAX.
  if (Subtarget.isPPC64())
    return LONG_MAX;

  return TargetFrameLowering::getStackThreshold();
}

const ReturnProtectorLowering *PPCFrameLowering::getReturnProtector() const {
  return &RPL;
}
