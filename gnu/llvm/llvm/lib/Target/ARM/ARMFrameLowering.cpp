//===- ARMFrameLowering.cpp - ARM Frame Information -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//
//
// This file contains the ARM implementation of TargetFrameLowering class.
//
// On ARM, stack frames are structured as follows:
//
// The stack grows downward.
//
// All of the individual frame areas on the frame below are optional, i.e. it's
// possible to create a function so that the particular area isn't present
// in the frame.
//
// At function entry, the "frame" looks as follows:
//
// |                                   | Higher address
// |-----------------------------------|
// |                                   |
// | arguments passed on the stack     |
// |                                   |
// |-----------------------------------| <- sp
// |                                   | Lower address
//
//
// After the prologue has run, the frame has the following general structure.
// Technically the last frame area (VLAs) doesn't get created until in the
// main function body, after the prologue is run. However, it's depicted here
// for completeness.
//
// |                                   | Higher address
// |-----------------------------------|
// |                                   |
// | arguments passed on the stack     |
// |                                   |
// |-----------------------------------| <- (sp at function entry)
// |                                   |
// | varargs from registers            |
// |                                   |
// |-----------------------------------|
// |                                   |
// | prev_lr                           |
// | prev_fp                           |
// | (a.k.a. "frame record")           |
// |                                   |
// |- - - - - - - - - - - - - - - - - -| <- fp (r7 or r11)
// |                                   |
// | callee-saved gpr registers        |
// |                                   |
// |-----------------------------------|
// |                                   |
// | callee-saved fp/simd regs         |
// |                                   |
// |-----------------------------------|
// |.empty.space.to.make.part.below....|
// |.aligned.in.case.it.needs.more.than| (size of this area is unknown at
// |.the.standard.8-byte.alignment.....|  compile time; if present)
// |-----------------------------------|
// |                                   |
// | local variables of fixed size     |
// | including spill slots             |
// |-----------------------------------| <- base pointer (not defined by ABI,
// |.variable-sized.local.variables....|       LLVM chooses r6)
// |.(VLAs)............................| (size of this area is unknown at
// |...................................|  compile time)
// |-----------------------------------| <- sp
// |                                   | Lower address
//
//
// To access the data in a frame, at-compile time, a constant offset must be
// computable from one of the pointers (fp, bp, sp) to access it. The size
// of the areas with a dotted background cannot be computed at compile-time
// if they are present, making it required to have all three of fp, bp and
// sp to be set up to be able to access all contents in the frame areas,
// assuming all of the frame areas are non-empty.
//
// For most functions, some of the frame areas are empty. For those functions,
// it may not be necessary to set up fp or bp:
// * A base pointer is definitely needed when there are both VLAs and local
//   variables with more-than-default alignment requirements.
// * A frame pointer is definitely needed when there are local variables with
//   more-than-default alignment requirements.
//
// In some cases when a base pointer is not strictly needed, it is generated
// anyway when offsets from the frame pointer to access local variables become
// so large that the offset can't be encoded in the immediate fields of loads
// or stores.
//
// The frame pointer might be chosen to be r7 or r11, depending on the target
// architecture and operating system. See ARMSubtarget::getFramePointerReg for
// details.
//
// Outgoing function arguments must be at the bottom of the stack frame when
// calling another function. If we do not have variable-sized stack objects, we
// can allocate a "reserved call frame" area at the bottom of the local
// variable area, large enough for all outgoing calls. If we do have VLAs, then
// the stack pointer must be decremented and incremented around each call to
// make space for the arguments below the VLAs.
//
//===----------------------------------------------------------------------===//

#include "ARMFrameLowering.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMConstantPoolValue.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMSubtarget.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

#define DEBUG_TYPE "arm-frame-lowering"

using namespace llvm;

static cl::opt<bool>
SpillAlignedNEONRegs("align-neon-spills", cl::Hidden, cl::init(true),
                     cl::desc("Align ARM NEON spills in prolog and epilog"));

static MachineBasicBlock::iterator
skipAlignedDPRCS2Spills(MachineBasicBlock::iterator MI,
                        unsigned NumAlignedDPRCS2Regs);

ARMFrameLowering::ARMFrameLowering(const ARMSubtarget &sti)
    : TargetFrameLowering(StackGrowsDown, sti.getStackAlignment(), 0, Align(4)),
      STI(sti) {}

bool ARMFrameLowering::keepFramePointer(const MachineFunction &MF) const {
  // iOS always has a FP for backtracking, force other targets to keep their FP
  // when doing FastISel. The emitted code is currently superior, and in cases
  // like test-suite's lencod FastISel isn't quite correct when FP is eliminated.
  return MF.getSubtarget<ARMSubtarget>().useFastISel();
}

/// Returns true if the target can safely skip saving callee-saved registers
/// for noreturn nounwind functions.
bool ARMFrameLowering::enableCalleeSaveSkip(const MachineFunction &MF) const {
  assert(MF.getFunction().hasFnAttribute(Attribute::NoReturn) &&
         MF.getFunction().hasFnAttribute(Attribute::NoUnwind) &&
         !MF.getFunction().hasFnAttribute(Attribute::UWTable));

  // Frame pointer and link register are not treated as normal CSR, thus we
  // can always skip CSR saves for nonreturning functions.
  return true;
}

/// hasFP - Return true if the specified function should have a dedicated frame
/// pointer register.  This is true if the function has variable sized allocas
/// or if frame pointer elimination is disabled.
bool ARMFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // ABI-required frame pointer.
  if (MF.getTarget().Options.DisableFramePointerElim(MF))
    return true;

  // Frame pointer required for use within this function.
  return (RegInfo->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
          MFI.isFrameAddressTaken());
}

/// isFPReserved - Return true if the frame pointer register should be
/// considered a reserved register on the scope of the specified function.
bool ARMFrameLowering::isFPReserved(const MachineFunction &MF) const {
  return hasFP(MF) || MF.getTarget().Options.FramePointerIsReserved(MF);
}

/// hasReservedCallFrame - Under normal circumstances, when a frame pointer is
/// not required, we reserve argument space for call sites in the function
/// immediately on entry to the current function.  This eliminates the need for
/// add/sub sp brackets around call sites.  Returns true if the call frame is
/// included as part of the stack frame.
bool ARMFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned CFSize = MFI.getMaxCallFrameSize();
  // It's not always a good idea to include the call frame as part of the
  // stack frame. ARM (especially Thumb) has small immediate offset to
  // address the stack frame. So a large call frame can cause poor codegen
  // and may even makes it impossible to scavenge a register.
  if (CFSize >= ((1 << 12) - 1) / 2)  // Half of imm12
    return false;

  return !MFI.hasVarSizedObjects();
}

/// canSimplifyCallFramePseudos - If there is a reserved call frame, the
/// call frame pseudos can be simplified.  Unlike most targets, having a FP
/// is not sufficient here since we still may reference some objects via SP
/// even when FP is available in Thumb2 mode.
bool
ARMFrameLowering::canSimplifyCallFramePseudos(const MachineFunction &MF) const {
  return hasReservedCallFrame(MF) || MF.getFrameInfo().hasVarSizedObjects();
}

// Returns how much of the incoming argument stack area we should clean up in an
// epilogue. For the C calling convention this will be 0, for guaranteed tail
// call conventions it can be positive (a normal return or a tail call to a
// function that uses less stack space for arguments) or negative (for a tail
// call to a function that needs more stack space than us for arguments).
static int getArgumentStackToRestore(MachineFunction &MF,
                                     MachineBasicBlock &MBB) {
  MachineBasicBlock::iterator MBBI = MBB.getLastNonDebugInstr();
  bool IsTailCallReturn = false;
  if (MBB.end() != MBBI) {
    unsigned RetOpcode = MBBI->getOpcode();
    IsTailCallReturn = RetOpcode == ARM::TCRETURNdi ||
                       RetOpcode == ARM::TCRETURNri ||
                       RetOpcode == ARM::TCRETURNrinotr12;
  }
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  int ArgumentPopSize = 0;
  if (IsTailCallReturn) {
    MachineOperand &StackAdjust = MBBI->getOperand(1);

    // For a tail-call in a callee-pops-arguments environment, some or all of
    // the stack may actually be in use for the call's arguments, this is
    // calculated during LowerCall and consumed here...
    ArgumentPopSize = StackAdjust.getImm();
  } else {
    // ... otherwise the amount to pop is *all* of the argument space,
    // conveniently stored in the MachineFunctionInfo by
    // LowerFormalArguments. This will, of course, be zero for the C calling
    // convention.
    ArgumentPopSize = AFI->getArgumentStackToRestore();
  }

  return ArgumentPopSize;
}

static bool needsWinCFI(const MachineFunction &MF) {
  const Function &F = MF.getFunction();
  return MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
         F.needsUnwindTableEntry();
}

// Given a load or a store instruction, generate an appropriate unwinding SEH
// code on Windows.
static MachineBasicBlock::iterator insertSEH(MachineBasicBlock::iterator MBBI,
                                             const TargetInstrInfo &TII,
                                             unsigned Flags) {
  unsigned Opc = MBBI->getOpcode();
  MachineBasicBlock *MBB = MBBI->getParent();
  MachineFunction &MF = *MBB->getParent();
  DebugLoc DL = MBBI->getDebugLoc();
  MachineInstrBuilder MIB;
  const ARMSubtarget &Subtarget = MF.getSubtarget<ARMSubtarget>();
  const ARMBaseRegisterInfo *RegInfo = Subtarget.getRegisterInfo();

  Flags |= MachineInstr::NoMerge;

  switch (Opc) {
  default:
    report_fatal_error("No SEH Opcode for instruction " + TII.getName(Opc));
    break;
  case ARM::t2ADDri:   // add.w r11, sp, #xx
  case ARM::t2ADDri12: // add.w r11, sp, #xx
  case ARM::t2MOVTi16: // movt  r4, #xx
  case ARM::tBL:       // bl __chkstk
    // These are harmless if used for just setting up a frame pointer,
    // but that frame pointer can't be relied upon for unwinding, unless
    // set up with SEH_SaveSP.
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop))
              .addImm(/*Wide=*/1)
              .setMIFlags(Flags);
    break;

  case ARM::t2MOVi16: { // mov(w) r4, #xx
    bool Wide = MBBI->getOperand(1).getImm() >= 256;
    if (!Wide) {
      MachineInstrBuilder NewInstr =
          BuildMI(MF, DL, TII.get(ARM::tMOVi8)).setMIFlags(MBBI->getFlags());
      NewInstr.add(MBBI->getOperand(0));
      NewInstr.add(t1CondCodeOp(/*isDead=*/true));
      for (MachineOperand &MO : llvm::drop_begin(MBBI->operands()))
        NewInstr.add(MO);
      MachineBasicBlock::iterator NewMBBI = MBB->insertAfter(MBBI, NewInstr);
      MBB->erase(MBBI);
      MBBI = NewMBBI;
    }
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop)).addImm(Wide).setMIFlags(Flags);
    break;
  }

  case ARM::tBLXr: // blx r12 (__chkstk)
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop))
              .addImm(/*Wide=*/0)
              .setMIFlags(Flags);
    break;

  case ARM::t2MOVi32imm: // movw+movt
    // This pseudo instruction expands into two mov instructions. If the
    // second operand is a symbol reference, this will stay as two wide
    // instructions, movw+movt. If they're immediates, the first one can
    // end up as a narrow mov though.
    // As two SEH instructions are appended here, they won't get interleaved
    // between the two final movw/movt instructions, but it doesn't make any
    // practical difference.
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop))
              .addImm(/*Wide=*/1)
              .setMIFlags(Flags);
    MBB->insertAfter(MBBI, MIB);
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop))
              .addImm(/*Wide=*/1)
              .setMIFlags(Flags);
    break;

  case ARM::t2STR_PRE:
    if (MBBI->getOperand(0).getReg() == ARM::SP &&
        MBBI->getOperand(2).getReg() == ARM::SP &&
        MBBI->getOperand(3).getImm() == -4) {
      unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
      MIB = BuildMI(MF, DL, TII.get(ARM::SEH_SaveRegs))
                .addImm(1ULL << Reg)
                .addImm(/*Wide=*/1)
                .setMIFlags(Flags);
    } else {
      report_fatal_error("No matching SEH Opcode for t2STR_PRE");
    }
    break;

  case ARM::t2LDR_POST:
    if (MBBI->getOperand(1).getReg() == ARM::SP &&
        MBBI->getOperand(2).getReg() == ARM::SP &&
        MBBI->getOperand(3).getImm() == 4) {
      unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(0).getReg());
      MIB = BuildMI(MF, DL, TII.get(ARM::SEH_SaveRegs))
                .addImm(1ULL << Reg)
                .addImm(/*Wide=*/1)
                .setMIFlags(Flags);
    } else {
      report_fatal_error("No matching SEH Opcode for t2LDR_POST");
    }
    break;

  case ARM::t2LDMIA_RET:
  case ARM::t2LDMIA_UPD:
  case ARM::t2STMDB_UPD: {
    unsigned Mask = 0;
    bool Wide = false;
    for (unsigned i = 4, NumOps = MBBI->getNumOperands(); i != NumOps; ++i) {
      const MachineOperand &MO = MBBI->getOperand(i);
      if (!MO.isReg() || MO.isImplicit())
        continue;
      unsigned Reg = RegInfo->getSEHRegNum(MO.getReg());
      if (Reg == 15)
        Reg = 14;
      if (Reg >= 8 && Reg <= 13)
        Wide = true;
      else if (Opc == ARM::t2LDMIA_UPD && Reg == 14)
        Wide = true;
      Mask |= 1 << Reg;
    }
    if (!Wide) {
      unsigned NewOpc;
      switch (Opc) {
      case ARM::t2LDMIA_RET:
        NewOpc = ARM::tPOP_RET;
        break;
      case ARM::t2LDMIA_UPD:
        NewOpc = ARM::tPOP;
        break;
      case ARM::t2STMDB_UPD:
        NewOpc = ARM::tPUSH;
        break;
      default:
        llvm_unreachable("");
      }
      MachineInstrBuilder NewInstr =
          BuildMI(MF, DL, TII.get(NewOpc)).setMIFlags(MBBI->getFlags());
      for (unsigned i = 2, NumOps = MBBI->getNumOperands(); i != NumOps; ++i)
        NewInstr.add(MBBI->getOperand(i));
      MachineBasicBlock::iterator NewMBBI = MBB->insertAfter(MBBI, NewInstr);
      MBB->erase(MBBI);
      MBBI = NewMBBI;
    }
    unsigned SEHOpc =
        (Opc == ARM::t2LDMIA_RET) ? ARM::SEH_SaveRegs_Ret : ARM::SEH_SaveRegs;
    MIB = BuildMI(MF, DL, TII.get(SEHOpc))
              .addImm(Mask)
              .addImm(Wide ? 1 : 0)
              .setMIFlags(Flags);
    break;
  }
  case ARM::VSTMDDB_UPD:
  case ARM::VLDMDIA_UPD: {
    int First = -1, Last = 0;
    for (const MachineOperand &MO : llvm::drop_begin(MBBI->operands(), 4)) {
      unsigned Reg = RegInfo->getSEHRegNum(MO.getReg());
      if (First == -1)
        First = Reg;
      Last = Reg;
    }
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_SaveFRegs))
              .addImm(First)
              .addImm(Last)
              .setMIFlags(Flags);
    break;
  }
  case ARM::tSUBspi:
  case ARM::tADDspi:
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_StackAlloc))
              .addImm(MBBI->getOperand(2).getImm() * 4)
              .addImm(/*Wide=*/0)
              .setMIFlags(Flags);
    break;
  case ARM::t2SUBspImm:
  case ARM::t2SUBspImm12:
  case ARM::t2ADDspImm:
  case ARM::t2ADDspImm12:
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_StackAlloc))
              .addImm(MBBI->getOperand(2).getImm())
              .addImm(/*Wide=*/1)
              .setMIFlags(Flags);
    break;

  case ARM::tMOVr:
    if (MBBI->getOperand(1).getReg() == ARM::SP &&
        (Flags & MachineInstr::FrameSetup)) {
      unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(0).getReg());
      MIB = BuildMI(MF, DL, TII.get(ARM::SEH_SaveSP))
                .addImm(Reg)
                .setMIFlags(Flags);
    } else if (MBBI->getOperand(0).getReg() == ARM::SP &&
               (Flags & MachineInstr::FrameDestroy)) {
      unsigned Reg = RegInfo->getSEHRegNum(MBBI->getOperand(1).getReg());
      MIB = BuildMI(MF, DL, TII.get(ARM::SEH_SaveSP))
                .addImm(Reg)
                .setMIFlags(Flags);
    } else {
      report_fatal_error("No SEH Opcode for MOV");
    }
    break;

  case ARM::tBX_RET:
  case ARM::TCRETURNri:
  case ARM::TCRETURNrinotr12:
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop_Ret))
              .addImm(/*Wide=*/0)
              .setMIFlags(Flags);
    break;

  case ARM::TCRETURNdi:
    MIB = BuildMI(MF, DL, TII.get(ARM::SEH_Nop_Ret))
              .addImm(/*Wide=*/1)
              .setMIFlags(Flags);
    break;
  }
  return MBB->insertAfter(MBBI, MIB);
}

static MachineBasicBlock::iterator
initMBBRange(MachineBasicBlock &MBB, const MachineBasicBlock::iterator &MBBI) {
  if (MBBI == MBB.begin())
    return MachineBasicBlock::iterator();
  return std::prev(MBBI);
}

static void insertSEHRange(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator Start,
                           const MachineBasicBlock::iterator &End,
                           const ARMBaseInstrInfo &TII, unsigned MIFlags) {
  if (Start.isValid())
    Start = std::next(Start);
  else
    Start = MBB.begin();

  for (auto MI = Start; MI != End;) {
    auto Next = std::next(MI);
    // Check if this instruction already has got a SEH opcode added. In that
    // case, don't do this generic mapping.
    if (Next != End && isSEHInstruction(*Next)) {
      MI = std::next(Next);
      while (MI != End && isSEHInstruction(*MI))
        ++MI;
      continue;
    }
    insertSEH(MI, TII, MIFlags);
    MI = Next;
  }
}

static void emitRegPlusImmediate(
    bool isARM, MachineBasicBlock &MBB, MachineBasicBlock::iterator &MBBI,
    const DebugLoc &dl, const ARMBaseInstrInfo &TII, unsigned DestReg,
    unsigned SrcReg, int NumBytes, unsigned MIFlags = MachineInstr::NoFlags,
    ARMCC::CondCodes Pred = ARMCC::AL, unsigned PredReg = 0) {
  if (isARM)
    emitARMRegPlusImmediate(MBB, MBBI, dl, DestReg, SrcReg, NumBytes,
                            Pred, PredReg, TII, MIFlags);
  else
    emitT2RegPlusImmediate(MBB, MBBI, dl, DestReg, SrcReg, NumBytes,
                           Pred, PredReg, TII, MIFlags);
}

static void emitSPUpdate(bool isARM, MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator &MBBI, const DebugLoc &dl,
                         const ARMBaseInstrInfo &TII, int NumBytes,
                         unsigned MIFlags = MachineInstr::NoFlags,
                         ARMCC::CondCodes Pred = ARMCC::AL,
                         unsigned PredReg = 0) {
  emitRegPlusImmediate(isARM, MBB, MBBI, dl, TII, ARM::SP, ARM::SP, NumBytes,
                       MIFlags, Pred, PredReg);
}

static int sizeOfSPAdjustment(const MachineInstr &MI) {
  int RegSize;
  switch (MI.getOpcode()) {
  case ARM::VSTMDDB_UPD:
    RegSize = 8;
    break;
  case ARM::STMDB_UPD:
  case ARM::t2STMDB_UPD:
    RegSize = 4;
    break;
  case ARM::t2STR_PRE:
  case ARM::STR_PRE_IMM:
    return 4;
  default:
    llvm_unreachable("Unknown push or pop like instruction");
  }

  int count = 0;
  // ARM and Thumb2 push/pop insts have explicit "sp, sp" operands (+
  // pred) so the list starts at 4.
  for (int i = MI.getNumOperands() - 1; i >= 4; --i)
    count += RegSize;
  return count;
}

static bool WindowsRequiresStackProbe(const MachineFunction &MF,
                                      size_t StackSizeInBytes) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const Function &F = MF.getFunction();
  unsigned StackProbeSize = (MFI.getStackProtectorIndex() > 0) ? 4080 : 4096;

  StackProbeSize =
      F.getFnAttributeAsParsedInteger("stack-probe-size", StackProbeSize);
  return (StackSizeInBytes >= StackProbeSize) &&
         !F.hasFnAttribute("no-stack-arg-probe");
}

namespace {

struct StackAdjustingInsts {
  struct InstInfo {
    MachineBasicBlock::iterator I;
    unsigned SPAdjust;
    bool BeforeFPSet;
  };

  SmallVector<InstInfo, 4> Insts;

  void addInst(MachineBasicBlock::iterator I, unsigned SPAdjust,
               bool BeforeFPSet = false) {
    InstInfo Info = {I, SPAdjust, BeforeFPSet};
    Insts.push_back(Info);
  }

  void addExtraBytes(const MachineBasicBlock::iterator I, unsigned ExtraBytes) {
    auto Info =
        llvm::find_if(Insts, [&](InstInfo &Info) { return Info.I == I; });
    assert(Info != Insts.end() && "invalid sp adjusting instruction");
    Info->SPAdjust += ExtraBytes;
  }

  void emitDefCFAOffsets(MachineBasicBlock &MBB, const DebugLoc &dl,
                         const ARMBaseInstrInfo &TII, bool HasFP) {
    MachineFunction &MF = *MBB.getParent();
    unsigned CFAOffset = 0;
    for (auto &Info : Insts) {
      if (HasFP && !Info.BeforeFPSet)
        return;

      CFAOffset += Info.SPAdjust;
      unsigned CFIIndex = MF.addFrameInst(
          MCCFIInstruction::cfiDefCfaOffset(nullptr, CFAOffset));
      BuildMI(MBB, std::next(Info.I), dl,
              TII.get(TargetOpcode::CFI_INSTRUCTION))
              .addCFIIndex(CFIIndex)
              .setMIFlags(MachineInstr::FrameSetup);
    }
  }
};

} // end anonymous namespace

/// Emit an instruction sequence that will align the address in
/// register Reg by zero-ing out the lower bits.  For versions of the
/// architecture that support Neon, this must be done in a single
/// instruction, since skipAlignedDPRCS2Spills assumes it is done in a
/// single instruction. That function only gets called when optimizing
/// spilling of D registers on a core with the Neon instruction set
/// present.
static void emitAligningInstructions(MachineFunction &MF, ARMFunctionInfo *AFI,
                                     const TargetInstrInfo &TII,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     const DebugLoc &DL, const unsigned Reg,
                                     const Align Alignment,
                                     const bool MustBeSingleInstruction) {
  const ARMSubtarget &AST = MF.getSubtarget<ARMSubtarget>();
  const bool CanUseBFC = AST.hasV6T2Ops() || AST.hasV7Ops();
  const unsigned AlignMask = Alignment.value() - 1U;
  const unsigned NrBitsToZero = Log2(Alignment);
  assert(!AFI->isThumb1OnlyFunction() && "Thumb1 not supported");
  if (!AFI->isThumbFunction()) {
    // if the BFC instruction is available, use that to zero the lower
    // bits:
    //   bfc Reg, #0, log2(Alignment)
    // otherwise use BIC, if the mask to zero the required number of bits
    // can be encoded in the bic immediate field
    //   bic Reg, Reg, Alignment-1
    // otherwise, emit
    //   lsr Reg, Reg, log2(Alignment)
    //   lsl Reg, Reg, log2(Alignment)
    if (CanUseBFC) {
      BuildMI(MBB, MBBI, DL, TII.get(ARM::BFC), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(~AlignMask)
          .add(predOps(ARMCC::AL));
    } else if (AlignMask <= 255) {
      BuildMI(MBB, MBBI, DL, TII.get(ARM::BICri), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(AlignMask)
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    } else {
      assert(!MustBeSingleInstruction &&
             "Shouldn't call emitAligningInstructions demanding a single "
             "instruction to be emitted for large stack alignment for a target "
             "without BFC.");
      BuildMI(MBB, MBBI, DL, TII.get(ARM::MOVsi), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(ARM_AM::getSORegOpc(ARM_AM::lsr, NrBitsToZero))
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
      BuildMI(MBB, MBBI, DL, TII.get(ARM::MOVsi), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(ARM_AM::getSORegOpc(ARM_AM::lsl, NrBitsToZero))
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    }
  } else {
    // Since this is only reached for Thumb-2 targets, the BFC instruction
    // should always be available.
    assert(CanUseBFC);
    BuildMI(MBB, MBBI, DL, TII.get(ARM::t2BFC), Reg)
        .addReg(Reg, RegState::Kill)
        .addImm(~AlignMask)
        .add(predOps(ARMCC::AL));
  }
}

/// We need the offset of the frame pointer relative to other MachineFrameInfo
/// offsets which are encoded relative to SP at function begin.
/// See also emitPrologue() for how the FP is set up.
/// Unfortunately we cannot determine this value in determineCalleeSaves() yet
/// as assignCalleeSavedSpillSlots() hasn't run at this point. Instead we use
/// this to produce a conservative estimate that we check in an assert() later.
static int getMaxFPOffset(const ARMSubtarget &STI, const ARMFunctionInfo &AFI,
                          const MachineFunction &MF) {
  // For Thumb1, push.w isn't available, so the first push will always push
  // r7 and lr onto the stack first.
  if (AFI.isThumb1OnlyFunction())
    return -AFI.getArgRegsSaveSize() - (2 * 4);
  // This is a conservative estimation: Assume the frame pointer being r7 and
  // pc("r15") up to r8 getting spilled before (= 8 registers).
  int MaxRegBytes = 8 * 4;
  if (STI.splitFramePointerPush(MF)) {
    // Here, r11 can be stored below all of r4-r15 (3 registers more than
    // above), plus d8-d15.
    MaxRegBytes = 11 * 4 + 8 * 8;
  }
  int FPCXTSaveSize =
      (STI.hasV8_1MMainlineOps() && AFI.isCmseNSEntryFunction()) ? 4 : 0;
  return -FPCXTSaveSize - AFI.getArgRegsSaveSize() - MaxRegBytes;
}

void ARMFrameLowering::emitPrologue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineBasicBlock::iterator MBBI = MBB.begin();
  MachineFrameInfo  &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  MCContext &Context = MF.getContext();
  const TargetMachine &TM = MF.getTarget();
  const MCRegisterInfo *MRI = Context.getRegisterInfo();
  const ARMBaseRegisterInfo *RegInfo = STI.getRegisterInfo();
  const ARMBaseInstrInfo &TII = *STI.getInstrInfo();
  assert(!AFI->isThumb1OnlyFunction() &&
         "This emitPrologue does not support Thumb1!");
  bool isARM = !AFI->isThumbFunction();
  Align Alignment = STI.getFrameLowering()->getStackAlign();
  unsigned ArgRegsSaveSize = AFI->getArgRegsSaveSize();
  unsigned NumBytes = MFI.getStackSize();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  int FPCXTSaveSize = 0;
  bool NeedsWinCFI = needsWinCFI(MF);

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc dl;

  Register FramePtr = RegInfo->getFrameRegister(MF);

  // Determine the sizes of each callee-save spill areas and record which frame
  // belongs to which callee-save spill areas.
  unsigned GPRCS1Size = 0, GPRCS2Size = 0, DPRCSSize = 0;
  int FramePtrSpillFI = 0;
  int D8SpillFI = 0;

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  StackAdjustingInsts DefCFAOffsetCandidates;
  bool HasFP = hasFP(MF);

  if (!AFI->hasStackFrame() &&
      (!STI.isTargetWindows() || !WindowsRequiresStackProbe(MF, NumBytes))) {
    if (NumBytes != 0) {
      emitSPUpdate(isARM, MBB, MBBI, dl, TII, -NumBytes,
                   MachineInstr::FrameSetup);
      DefCFAOffsetCandidates.addInst(std::prev(MBBI), NumBytes, true);
    }
    if (!NeedsWinCFI)
      DefCFAOffsetCandidates.emitDefCFAOffsets(MBB, dl, TII, HasFP);
    if (NeedsWinCFI && MBBI != MBB.begin()) {
      insertSEHRange(MBB, {}, MBBI, TII, MachineInstr::FrameSetup);
      BuildMI(MBB, MBBI, dl, TII.get(ARM::SEH_PrologEnd))
          .setMIFlag(MachineInstr::FrameSetup);
      MF.setHasWinCFI(true);
    }
    return;
  }

  // Determine spill area sizes.
  if (STI.splitFramePointerPush(MF)) {
    for (const CalleeSavedInfo &I : CSI) {
      Register Reg = I.getReg();
      int FI = I.getFrameIdx();
      switch (Reg) {
      case ARM::R11:
      case ARM::LR:
        if (Reg == FramePtr)
          FramePtrSpillFI = FI;
        GPRCS2Size += 4;
        break;
      case ARM::R0:
      case ARM::R1:
      case ARM::R2:
      case ARM::R3:
      case ARM::R4:
      case ARM::R5:
      case ARM::R6:
      case ARM::R7:
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R12:
        GPRCS1Size += 4;
        break;
      case ARM::FPCXTNS:
        FPCXTSaveSize = 4;
        break;
      default:
        // This is a DPR. Exclude the aligned DPRCS2 spills.
        if (Reg == ARM::D8)
          D8SpillFI = FI;
        if (Reg < ARM::D8 || Reg >= ARM::D8 + AFI->getNumAlignedDPRCS2Regs())
          DPRCSSize += 8;
      }
    }
  } else {
    for (const CalleeSavedInfo &I : CSI) {
      Register Reg = I.getReg();
      int FI = I.getFrameIdx();
      switch (Reg) {
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R11:
      case ARM::R12:
        if (STI.splitFramePushPop(MF)) {
          GPRCS2Size += 4;
          break;
        }
        [[fallthrough]];
      case ARM::R0:
      case ARM::R1:
      case ARM::R2:
      case ARM::R3:
      case ARM::R4:
      case ARM::R5:
      case ARM::R6:
      case ARM::R7:
      case ARM::LR:
        if (Reg == FramePtr)
          FramePtrSpillFI = FI;
        GPRCS1Size += 4;
        break;
      case ARM::FPCXTNS:
        FPCXTSaveSize = 4;
        break;
      default:
        // This is a DPR. Exclude the aligned DPRCS2 spills.
        if (Reg == ARM::D8)
          D8SpillFI = FI;
        if (Reg < ARM::D8 || Reg >= ARM::D8 + AFI->getNumAlignedDPRCS2Regs())
          DPRCSSize += 8;
      }
    }
  }

  MachineBasicBlock::iterator LastPush = MBB.end(), GPRCS1Push, GPRCS2Push;

  // Move past the PAC computation.
  if (AFI->shouldSignReturnAddress())
    LastPush = MBBI++;

  // Move past FPCXT area.
  if (FPCXTSaveSize > 0) {
    LastPush = MBBI++;
    DefCFAOffsetCandidates.addInst(LastPush, FPCXTSaveSize, true);
  }

  // Allocate the vararg register save area.
  if (ArgRegsSaveSize) {
    emitSPUpdate(isARM, MBB, MBBI, dl, TII, -ArgRegsSaveSize,
                 MachineInstr::FrameSetup);
    LastPush = std::prev(MBBI);
    DefCFAOffsetCandidates.addInst(LastPush, ArgRegsSaveSize, true);
  }

  // Move past area 1.
  if (GPRCS1Size > 0) {
    GPRCS1Push = LastPush = MBBI++;
    DefCFAOffsetCandidates.addInst(LastPush, GPRCS1Size, true);
  }

  // Determine starting offsets of spill areas.
  unsigned FPCXTOffset = NumBytes - ArgRegsSaveSize - FPCXTSaveSize;
  unsigned GPRCS1Offset = FPCXTOffset - GPRCS1Size;
  unsigned GPRCS2Offset = GPRCS1Offset - GPRCS2Size;
  Align DPRAlign = DPRCSSize ? std::min(Align(8), Alignment) : Align(4);
  unsigned DPRGapSize = GPRCS1Size + FPCXTSaveSize + ArgRegsSaveSize;
  if (!STI.splitFramePointerPush(MF)) {
    DPRGapSize += GPRCS2Size;
  }
  DPRGapSize %= DPRAlign.value();

  unsigned DPRCSOffset;
  if (STI.splitFramePointerPush(MF)) {
    DPRCSOffset = GPRCS1Offset - DPRGapSize - DPRCSSize;
    GPRCS2Offset = DPRCSOffset - GPRCS2Size;
  } else {
    DPRCSOffset = GPRCS2Offset - DPRGapSize - DPRCSSize;
  }
  int FramePtrOffsetInPush = 0;
  if (HasFP) {
    int FPOffset = MFI.getObjectOffset(FramePtrSpillFI);
    assert(getMaxFPOffset(STI, *AFI, MF) <= FPOffset &&
           "Max FP estimation is wrong");
    FramePtrOffsetInPush = FPOffset + ArgRegsSaveSize + FPCXTSaveSize;
    AFI->setFramePtrSpillOffset(MFI.getObjectOffset(FramePtrSpillFI) +
                                NumBytes);
  }
  AFI->setGPRCalleeSavedArea1Offset(GPRCS1Offset);
  AFI->setGPRCalleeSavedArea2Offset(GPRCS2Offset);
  AFI->setDPRCalleeSavedAreaOffset(DPRCSOffset);

  // Move past area 2.
  if (GPRCS2Size > 0 && !STI.splitFramePointerPush(MF)) {
    GPRCS2Push = LastPush = MBBI++;
    DefCFAOffsetCandidates.addInst(LastPush, GPRCS2Size);
  }

  // Prolog/epilog inserter assumes we correctly align DPRs on the stack, so our
  // .cfi_offset operations will reflect that.
  if (DPRGapSize) {
    assert(DPRGapSize == 4 && "unexpected alignment requirements for DPRs");
    if (LastPush != MBB.end() &&
        tryFoldSPUpdateIntoPushPop(STI, MF, &*LastPush, DPRGapSize))
      DefCFAOffsetCandidates.addExtraBytes(LastPush, DPRGapSize);
    else {
      emitSPUpdate(isARM, MBB, MBBI, dl, TII, -DPRGapSize,
                   MachineInstr::FrameSetup);
      DefCFAOffsetCandidates.addInst(std::prev(MBBI), DPRGapSize);
    }
  }

  // Move past area 3.
  if (DPRCSSize > 0) {
    // Since vpush register list cannot have gaps, there may be multiple vpush
    // instructions in the prologue.
    while (MBBI != MBB.end() && MBBI->getOpcode() == ARM::VSTMDDB_UPD) {
      DefCFAOffsetCandidates.addInst(MBBI, sizeOfSPAdjustment(*MBBI));
      LastPush = MBBI++;
    }
  }

  // Move past the aligned DPRCS2 area.
  if (AFI->getNumAlignedDPRCS2Regs() > 0) {
    MBBI = skipAlignedDPRCS2Spills(MBBI, AFI->getNumAlignedDPRCS2Regs());
    // The code inserted by emitAlignedDPRCS2Spills realigns the stack, and
    // leaves the stack pointer pointing to the DPRCS2 area.
    //
    // Adjust NumBytes to represent the stack slots below the DPRCS2 area.
    NumBytes += MFI.getObjectOffset(D8SpillFI);
  } else
    NumBytes = DPRCSOffset;

  if (GPRCS2Size > 0 && STI.splitFramePointerPush(MF)) {
    GPRCS2Push = LastPush = MBBI++;
    DefCFAOffsetCandidates.addInst(LastPush, GPRCS2Size);
  }

  bool NeedsWinCFIStackAlloc = NeedsWinCFI;
  if (STI.splitFramePointerPush(MF) && HasFP)
    NeedsWinCFIStackAlloc = false;

  if (STI.isTargetWindows() && WindowsRequiresStackProbe(MF, NumBytes)) {
    uint32_t NumWords = NumBytes >> 2;

    if (NumWords < 65536) {
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVi16), ARM::R4)
          .addImm(NumWords)
          .setMIFlags(MachineInstr::FrameSetup)
          .add(predOps(ARMCC::AL));
    } else {
      // Split into two instructions here, instead of using t2MOVi32imm,
      // to allow inserting accurate SEH instructions (including accurate
      // instruction size for each of them).
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVi16), ARM::R4)
          .addImm(NumWords & 0xffff)
          .setMIFlags(MachineInstr::FrameSetup)
          .add(predOps(ARMCC::AL));
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVTi16), ARM::R4)
          .addReg(ARM::R4)
          .addImm(NumWords >> 16)
          .setMIFlags(MachineInstr::FrameSetup)
          .add(predOps(ARMCC::AL));
    }

    switch (TM.getCodeModel()) {
    case CodeModel::Tiny:
      llvm_unreachable("Tiny code model not available on ARM.");
    case CodeModel::Small:
    case CodeModel::Medium:
    case CodeModel::Kernel:
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tBL))
          .add(predOps(ARMCC::AL))
          .addExternalSymbol("__chkstk")
          .addReg(ARM::R4, RegState::Implicit)
          .setMIFlags(MachineInstr::FrameSetup);
      break;
    case CodeModel::Large:
      BuildMI(MBB, MBBI, dl, TII.get(ARM::t2MOVi32imm), ARM::R12)
        .addExternalSymbol("__chkstk")
        .setMIFlags(MachineInstr::FrameSetup);

      BuildMI(MBB, MBBI, dl, TII.get(ARM::tBLXr))
          .add(predOps(ARMCC::AL))
          .addReg(ARM::R12, RegState::Kill)
          .addReg(ARM::R4, RegState::Implicit)
          .setMIFlags(MachineInstr::FrameSetup);
      break;
    }

    MachineInstrBuilder Instr, SEH;
    Instr = BuildMI(MBB, MBBI, dl, TII.get(ARM::t2SUBrr), ARM::SP)
                .addReg(ARM::SP, RegState::Kill)
                .addReg(ARM::R4, RegState::Kill)
                .setMIFlags(MachineInstr::FrameSetup)
                .add(predOps(ARMCC::AL))
                .add(condCodeOp());
    if (NeedsWinCFIStackAlloc) {
      SEH = BuildMI(MF, dl, TII.get(ARM::SEH_StackAlloc))
                .addImm(NumBytes)
                .addImm(/*Wide=*/1)
                .setMIFlags(MachineInstr::FrameSetup);
      MBB.insertAfter(Instr, SEH);
    }
    NumBytes = 0;
  }

  if (NumBytes) {
    // Adjust SP after all the callee-save spills.
    if (AFI->getNumAlignedDPRCS2Regs() == 0 &&
        tryFoldSPUpdateIntoPushPop(STI, MF, &*LastPush, NumBytes))
      DefCFAOffsetCandidates.addExtraBytes(LastPush, NumBytes);
    else {
      emitSPUpdate(isARM, MBB, MBBI, dl, TII, -NumBytes,
                   MachineInstr::FrameSetup);
      DefCFAOffsetCandidates.addInst(std::prev(MBBI), NumBytes);
    }

    if (HasFP && isARM)
      // Restore from fp only in ARM mode: e.g. sub sp, r7, #24
      // Note it's not safe to do this in Thumb2 mode because it would have
      // taken two instructions:
      // mov sp, r7
      // sub sp, #24
      // If an interrupt is taken between the two instructions, then sp is in
      // an inconsistent state (pointing to the middle of callee-saved area).
      // The interrupt handler can end up clobbering the registers.
      AFI->setShouldRestoreSPFromFP(true);
  }

  // Set FP to point to the stack slot that contains the previous FP.
  // For iOS, FP is R7, which has now been stored in spill area 1.
  // Otherwise, if this is not iOS, all the callee-saved registers go
  // into spill area 1, including the FP in R11.  In either case, it
  // is in area one and the adjustment needs to take place just after
  // that push.
  // FIXME: The above is not necessary true when PACBTI is enabled.
  // AAPCS requires use of R11, and PACBTI gets in the way of regular pushes,
  // so FP ends up on area two.
  MachineBasicBlock::iterator AfterPush;
  if (HasFP) {
    AfterPush = std::next(GPRCS1Push);
    unsigned PushSize = sizeOfSPAdjustment(*GPRCS1Push);
    int FPOffset = PushSize + FramePtrOffsetInPush;
    if (STI.splitFramePointerPush(MF)) {
      AfterPush = std::next(GPRCS2Push);
      emitRegPlusImmediate(!AFI->isThumbFunction(), MBB, AfterPush, dl, TII,
                           FramePtr, ARM::SP, 0, MachineInstr::FrameSetup);
    } else {
      emitRegPlusImmediate(!AFI->isThumbFunction(), MBB, AfterPush, dl, TII,
                           FramePtr, ARM::SP, FPOffset,
                           MachineInstr::FrameSetup);
    }
    if (!NeedsWinCFI) {
      if (FramePtrOffsetInPush + PushSize != 0) {
        unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
            nullptr, MRI->getDwarfRegNum(FramePtr, true),
            FPCXTSaveSize + ArgRegsSaveSize - FramePtrOffsetInPush));
        BuildMI(MBB, AfterPush, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
      } else {
        unsigned CFIIndex =
            MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(
                nullptr, MRI->getDwarfRegNum(FramePtr, true)));
        BuildMI(MBB, AfterPush, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
      }
    }
  }

  // Emit a SEH opcode indicating the prologue end. The rest of the prologue
  // instructions below don't need to be replayed to unwind the stack.
  if (NeedsWinCFI && MBBI != MBB.begin()) {
    MachineBasicBlock::iterator End = MBBI;
    if (HasFP && STI.splitFramePointerPush(MF))
      End = AfterPush;
    insertSEHRange(MBB, {}, End, TII, MachineInstr::FrameSetup);
    BuildMI(MBB, End, dl, TII.get(ARM::SEH_PrologEnd))
        .setMIFlag(MachineInstr::FrameSetup);
    MF.setHasWinCFI(true);
  }

  // Now that the prologue's actual instructions are finalised, we can insert
  // the necessary DWARF cf instructions to describe the situation. Start by
  // recording where each register ended up:
  if (GPRCS1Size > 0 && !NeedsWinCFI) {
    MachineBasicBlock::iterator Pos = std::next(GPRCS1Push);
    int CFIIndex;
    for (const auto &Entry : CSI) {
      Register Reg = Entry.getReg();
      int FI = Entry.getFrameIdx();
      switch (Reg) {
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R11:
      case ARM::R12:
        if (STI.splitFramePushPop(MF))
          break;
        [[fallthrough]];
      case ARM::R0:
      case ARM::R1:
      case ARM::R2:
      case ARM::R3:
      case ARM::R4:
      case ARM::R5:
      case ARM::R6:
      case ARM::R7:
      case ARM::LR:
        CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
            nullptr, MRI->getDwarfRegNum(Reg, true), MFI.getObjectOffset(FI)));
        BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
        break;
      }
    }
  }

  if (GPRCS2Size > 0 && !NeedsWinCFI) {
    MachineBasicBlock::iterator Pos = std::next(GPRCS2Push);
    for (const auto &Entry : CSI) {
      Register Reg = Entry.getReg();
      int FI = Entry.getFrameIdx();
      switch (Reg) {
      case ARM::R8:
      case ARM::R9:
      case ARM::R10:
      case ARM::R11:
      case ARM::R12:
        if (STI.splitFramePushPop(MF)) {
          unsigned DwarfReg = MRI->getDwarfRegNum(
              Reg == ARM::R12 ? ARM::RA_AUTH_CODE : Reg, true);
          int64_t Offset = MFI.getObjectOffset(FI);
          unsigned CFIIndex = MF.addFrameInst(
              MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
          BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
              .addCFIIndex(CFIIndex)
              .setMIFlags(MachineInstr::FrameSetup);
        }
        break;
      }
    }
  }

  if (DPRCSSize > 0 && !NeedsWinCFI) {
    // Since vpush register list cannot have gaps, there may be multiple vpush
    // instructions in the prologue.
    MachineBasicBlock::iterator Pos = std::next(LastPush);
    for (const auto &Entry : CSI) {
      Register Reg = Entry.getReg();
      int FI = Entry.getFrameIdx();
      if ((Reg >= ARM::D0 && Reg <= ARM::D31) &&
          (Reg < ARM::D8 || Reg >= ARM::D8 + AFI->getNumAlignedDPRCS2Regs())) {
        unsigned DwarfReg = MRI->getDwarfRegNum(Reg, true);
        int64_t Offset = MFI.getObjectOffset(FI);
        unsigned CFIIndex = MF.addFrameInst(
            MCCFIInstruction::createOffset(nullptr, DwarfReg, Offset));
        BuildMI(MBB, Pos, dl, TII.get(TargetOpcode::CFI_INSTRUCTION))
            .addCFIIndex(CFIIndex)
            .setMIFlags(MachineInstr::FrameSetup);
      }
    }
  }

  // Now we can emit descriptions of where the canonical frame address was
  // throughout the process. If we have a frame pointer, it takes over the job
  // half-way through, so only the first few .cfi_def_cfa_offset instructions
  // actually get emitted.
  if (!NeedsWinCFI)
    DefCFAOffsetCandidates.emitDefCFAOffsets(MBB, dl, TII, HasFP);

  if (STI.isTargetELF() && hasFP(MF))
    MFI.setOffsetAdjustment(MFI.getOffsetAdjustment() -
                            AFI->getFramePtrSpillOffset());

  AFI->setFPCXTSaveAreaSize(FPCXTSaveSize);
  AFI->setGPRCalleeSavedArea1Size(GPRCS1Size);
  AFI->setGPRCalleeSavedArea2Size(GPRCS2Size);
  AFI->setDPRCalleeSavedGapSize(DPRGapSize);
  AFI->setDPRCalleeSavedAreaSize(DPRCSSize);

  // If we need dynamic stack realignment, do it here. Be paranoid and make
  // sure if we also have VLAs, we have a base pointer for frame access.
  // If aligned NEON registers were spilled, the stack has already been
  // realigned.
  if (!AFI->getNumAlignedDPRCS2Regs() && RegInfo->hasStackRealignment(MF)) {
    Align MaxAlign = MFI.getMaxAlign();
    assert(!AFI->isThumb1OnlyFunction());
    if (!AFI->isThumbFunction()) {
      emitAligningInstructions(MF, AFI, TII, MBB, MBBI, dl, ARM::SP, MaxAlign,
                               false);
    } else {
      // We cannot use sp as source/dest register here, thus we're using r4 to
      // perform the calculations. We're emitting the following sequence:
      // mov r4, sp
      // -- use emitAligningInstructions to produce best sequence to zero
      // -- out lower bits in r4
      // mov sp, r4
      // FIXME: It will be better just to find spare register here.
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::R4)
          .addReg(ARM::SP, RegState::Kill)
          .add(predOps(ARMCC::AL));
      emitAligningInstructions(MF, AFI, TII, MBB, MBBI, dl, ARM::R4, MaxAlign,
                               false);
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
          .addReg(ARM::R4, RegState::Kill)
          .add(predOps(ARMCC::AL));
    }

    AFI->setShouldRestoreSPFromFP(true);
  }

  // If we need a base pointer, set it up here. It's whatever the value
  // of the stack pointer is at this point. Any variable size objects
  // will be allocated after this, so we can still use the base pointer
  // to reference locals.
  // FIXME: Clarify FrameSetup flags here.
  if (RegInfo->hasBasePointer(MF)) {
    if (isARM)
      BuildMI(MBB, MBBI, dl, TII.get(ARM::MOVr), RegInfo->getBaseRegister())
          .addReg(ARM::SP)
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    else
      BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), RegInfo->getBaseRegister())
          .addReg(ARM::SP)
          .add(predOps(ARMCC::AL));
  }

  // If the frame has variable sized objects then the epilogue must restore
  // the sp from fp. We can assume there's an FP here since hasFP already
  // checks for hasVarSizedObjects.
  if (MFI.hasVarSizedObjects())
    AFI->setShouldRestoreSPFromFP(true);
}

void ARMFrameLowering::emitEpilogue(MachineFunction &MF,
                                    MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();
  const ARMBaseInstrInfo &TII =
      *static_cast<const ARMBaseInstrInfo *>(MF.getSubtarget().getInstrInfo());
  assert(!AFI->isThumb1OnlyFunction() &&
         "This emitEpilogue does not support Thumb1!");
  bool isARM = !AFI->isThumbFunction();

  // Amount of stack space we reserved next to incoming args for either
  // varargs registers or stack arguments in tail calls made by this function.
  unsigned ReservedArgStack = AFI->getArgRegsSaveSize();

  // How much of the stack used by incoming arguments this function is expected
  // to restore in this particular epilogue.
  int IncomingArgStackToRestore = getArgumentStackToRestore(MF, MBB);
  int NumBytes = (int)MFI.getStackSize();
  Register FramePtr = RegInfo->getFrameRegister(MF);

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // First put ourselves on the first (from top) terminator instructions.
  MachineBasicBlock::iterator MBBI = MBB.getFirstTerminator();
  DebugLoc dl = MBBI != MBB.end() ? MBBI->getDebugLoc() : DebugLoc();

  MachineBasicBlock::iterator RangeStart;
  if (!AFI->hasStackFrame()) {
    if (MF.hasWinCFI()) {
      BuildMI(MBB, MBBI, dl, TII.get(ARM::SEH_EpilogStart))
          .setMIFlag(MachineInstr::FrameDestroy);
      RangeStart = initMBBRange(MBB, MBBI);
    }

    if (NumBytes + IncomingArgStackToRestore != 0)
      emitSPUpdate(isARM, MBB, MBBI, dl, TII,
                   NumBytes + IncomingArgStackToRestore,
                   MachineInstr::FrameDestroy);
  } else {
    // Unwind MBBI to point to first LDR / VLDRD.
    if (MBBI != MBB.begin()) {
      do {
        --MBBI;
      } while (MBBI != MBB.begin() &&
               MBBI->getFlag(MachineInstr::FrameDestroy));
      if (!MBBI->getFlag(MachineInstr::FrameDestroy))
        ++MBBI;
    }

    if (MF.hasWinCFI()) {
      BuildMI(MBB, MBBI, dl, TII.get(ARM::SEH_EpilogStart))
          .setMIFlag(MachineInstr::FrameDestroy);
      RangeStart = initMBBRange(MBB, MBBI);
    }

    // Move SP to start of FP callee save spill area.
    NumBytes -= (ReservedArgStack +
                 AFI->getFPCXTSaveAreaSize() +
                 AFI->getGPRCalleeSavedArea1Size() +
                 AFI->getGPRCalleeSavedArea2Size() +
                 AFI->getDPRCalleeSavedGapSize() +
                 AFI->getDPRCalleeSavedAreaSize());

    // Reset SP based on frame pointer only if the stack frame extends beyond
    // frame pointer stack slot or target is ELF and the function has FP.
    if (AFI->shouldRestoreSPFromFP()) {
      NumBytes = AFI->getFramePtrSpillOffset() - NumBytes;
      if (NumBytes) {
        if (isARM)
          emitARMRegPlusImmediate(MBB, MBBI, dl, ARM::SP, FramePtr, -NumBytes,
                                  ARMCC::AL, 0, TII,
                                  MachineInstr::FrameDestroy);
        else {
          // It's not possible to restore SP from FP in a single instruction.
          // For iOS, this looks like:
          // mov sp, r7
          // sub sp, #24
          // This is bad, if an interrupt is taken after the mov, sp is in an
          // inconsistent state.
          // Use the first callee-saved register as a scratch register.
          assert(!MFI.getPristineRegs(MF).test(ARM::R4) &&
                 "No scratch register to restore SP from FP!");
          emitT2RegPlusImmediate(MBB, MBBI, dl, ARM::R4, FramePtr, -NumBytes,
                                 ARMCC::AL, 0, TII, MachineInstr::FrameDestroy);
          BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
              .addReg(ARM::R4)
              .add(predOps(ARMCC::AL))
              .setMIFlag(MachineInstr::FrameDestroy);
        }
      } else {
        // Thumb2 or ARM.
        if (isARM)
          BuildMI(MBB, MBBI, dl, TII.get(ARM::MOVr), ARM::SP)
              .addReg(FramePtr)
              .add(predOps(ARMCC::AL))
              .add(condCodeOp())
              .setMIFlag(MachineInstr::FrameDestroy);
        else
          BuildMI(MBB, MBBI, dl, TII.get(ARM::tMOVr), ARM::SP)
              .addReg(FramePtr)
              .add(predOps(ARMCC::AL))
              .setMIFlag(MachineInstr::FrameDestroy);
      }
    } else if (NumBytes &&
               !tryFoldSPUpdateIntoPushPop(STI, MF, &*MBBI, NumBytes))
      emitSPUpdate(isARM, MBB, MBBI, dl, TII, NumBytes,
                   MachineInstr::FrameDestroy);

    // Increment past our save areas.
    if (AFI->getGPRCalleeSavedArea2Size() && STI.splitFramePointerPush(MF))
      MBBI++;

    if (MBBI != MBB.end() && AFI->getDPRCalleeSavedAreaSize()) {
      MBBI++;
      // Since vpop register list cannot have gaps, there may be multiple vpop
      // instructions in the epilogue.
      while (MBBI != MBB.end() && MBBI->getOpcode() == ARM::VLDMDIA_UPD)
        MBBI++;
    }
    if (AFI->getDPRCalleeSavedGapSize()) {
      assert(AFI->getDPRCalleeSavedGapSize() == 4 &&
             "unexpected DPR alignment gap");
      emitSPUpdate(isARM, MBB, MBBI, dl, TII, AFI->getDPRCalleeSavedGapSize(),
                   MachineInstr::FrameDestroy);
    }

    if (AFI->getGPRCalleeSavedArea2Size() && !STI.splitFramePointerPush(MF))
      MBBI++;
    if (AFI->getGPRCalleeSavedArea1Size()) MBBI++;

    if (ReservedArgStack || IncomingArgStackToRestore) {
      assert((int)ReservedArgStack + IncomingArgStackToRestore >= 0 &&
             "attempting to restore negative stack amount");
      emitSPUpdate(isARM, MBB, MBBI, dl, TII,
                   ReservedArgStack + IncomingArgStackToRestore,
                   MachineInstr::FrameDestroy);
    }

    // Validate PAC, It should have been already popped into R12. For CMSE entry
    // function, the validation instruction is emitted during expansion of the
    // tBXNS_RET, since the validation must use the value of SP at function
    // entry, before saving, resp. after restoring, FPCXTNS.
    if (AFI->shouldSignReturnAddress() && !AFI->isCmseNSEntryFunction())
      BuildMI(MBB, MBBI, DebugLoc(), STI.getInstrInfo()->get(ARM::t2AUT));
  }

  if (MF.hasWinCFI()) {
    insertSEHRange(MBB, RangeStart, MBB.end(), TII, MachineInstr::FrameDestroy);
    BuildMI(MBB, MBB.end(), dl, TII.get(ARM::SEH_EpilogEnd))
        .setMIFlag(MachineInstr::FrameDestroy);
  }
}

/// getFrameIndexReference - Provide a base+offset reference to an FI slot for
/// debug info.  It's the same as what we use for resolving the code-gen
/// references for now.  FIXME: This can go wrong when references are
/// SP-relative and simple call frames aren't used.
StackOffset ARMFrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                                     int FI,
                                                     Register &FrameReg) const {
  return StackOffset::getFixed(ResolveFrameIndexReference(MF, FI, FrameReg, 0));
}

int ARMFrameLowering::ResolveFrameIndexReference(const MachineFunction &MF,
                                                 int FI, Register &FrameReg,
                                                 int SPAdj) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const ARMBaseRegisterInfo *RegInfo = static_cast<const ARMBaseRegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  int Offset = MFI.getObjectOffset(FI) + MFI.getStackSize();
  int FPOffset = Offset - AFI->getFramePtrSpillOffset();
  bool isFixed = MFI.isFixedObjectIndex(FI);

  FrameReg = ARM::SP;
  Offset += SPAdj;

  // SP can move around if there are allocas.  We may also lose track of SP
  // when emergency spilling inside a non-reserved call frame setup.
  bool hasMovingSP = !hasReservedCallFrame(MF);

  // When dynamically realigning the stack, use the frame pointer for
  // parameters, and the stack/base pointer for locals.
  if (RegInfo->hasStackRealignment(MF)) {
    assert(hasFP(MF) && "dynamic stack realignment without a FP!");
    if (isFixed) {
      FrameReg = RegInfo->getFrameRegister(MF);
      Offset = FPOffset;
    } else if (hasMovingSP) {
      assert(RegInfo->hasBasePointer(MF) &&
             "VLAs and dynamic stack alignment, but missing base pointer!");
      FrameReg = RegInfo->getBaseRegister();
      Offset -= SPAdj;
    }
    return Offset;
  }

  // If there is a frame pointer, use it when we can.
  if (hasFP(MF) && AFI->hasStackFrame()) {
    // Use frame pointer to reference fixed objects. Use it for locals if
    // there are VLAs (and thus the SP isn't reliable as a base).
    if (isFixed || (hasMovingSP && !RegInfo->hasBasePointer(MF))) {
      FrameReg = RegInfo->getFrameRegister(MF);
      return FPOffset;
    } else if (hasMovingSP) {
      assert(RegInfo->hasBasePointer(MF) && "missing base pointer!");
      if (AFI->isThumb2Function()) {
        // Try to use the frame pointer if we can, else use the base pointer
        // since it's available. This is handy for the emergency spill slot, in
        // particular.
        if (FPOffset >= -255 && FPOffset < 0) {
          FrameReg = RegInfo->getFrameRegister(MF);
          return FPOffset;
        }
      }
    } else if (AFI->isThumbFunction()) {
      // Prefer SP to base pointer, if the offset is suitably aligned and in
      // range as the effective range of the immediate offset is bigger when
      // basing off SP.
      // Use  add <rd>, sp, #<imm8>
      //      ldr <rd>, [sp, #<imm8>]
      if (Offset >= 0 && (Offset & 3) == 0 && Offset <= 1020)
        return Offset;
      // In Thumb2 mode, the negative offset is very limited. Try to avoid
      // out of range references. ldr <rt>,[<rn>, #-<imm8>]
      if (AFI->isThumb2Function() && FPOffset >= -255 && FPOffset < 0) {
        FrameReg = RegInfo->getFrameRegister(MF);
        return FPOffset;
      }
    } else if (Offset > (FPOffset < 0 ? -FPOffset : FPOffset)) {
      // Otherwise, use SP or FP, whichever is closer to the stack slot.
      FrameReg = RegInfo->getFrameRegister(MF);
      return FPOffset;
    }
  }
  // Use the base pointer if we have one.
  // FIXME: Maybe prefer sp on Thumb1 if it's legal and the offset is cheaper?
  // That can happen if we forced a base pointer for a large call frame.
  if (RegInfo->hasBasePointer(MF)) {
    FrameReg = RegInfo->getBaseRegister();
    Offset -= SPAdj;
  }
  return Offset;
}

void ARMFrameLowering::emitPushInst(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MI,
                                    ArrayRef<CalleeSavedInfo> CSI,
                                    unsigned StmOpc, unsigned StrOpc,
                                    bool NoGap, bool (*Func)(unsigned, bool),
                                    unsigned NumAlignedDPRCS2Regs,
                                    unsigned MIFlags) const {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

  DebugLoc DL;

  using RegAndKill = std::pair<unsigned, bool>;

  SmallVector<RegAndKill, 4> Regs;
  unsigned i = CSI.size();
  while (i != 0) {
    unsigned LastReg = 0;
    for (; i != 0; --i) {
      Register Reg = CSI[i-1].getReg();
      if (!(Func)(Reg, STI.splitFramePushPop(MF))) continue;

      // D-registers in the aligned area DPRCS2 are NOT spilled here.
      if (Reg >= ARM::D8 && Reg < ARM::D8 + NumAlignedDPRCS2Regs)
        continue;

      const MachineRegisterInfo &MRI = MF.getRegInfo();
      bool isLiveIn = MRI.isLiveIn(Reg);
      if (!isLiveIn && !MRI.isReserved(Reg))
        MBB.addLiveIn(Reg);
      // If NoGap is true, push consecutive registers and then leave the rest
      // for other instructions. e.g.
      // vpush {d8, d10, d11} -> vpush {d8}, vpush {d10, d11}
      if (NoGap && LastReg && LastReg != Reg-1)
        break;
      LastReg = Reg;
      // Do not set a kill flag on values that are also marked as live-in. This
      // happens with the @llvm-returnaddress intrinsic and with arguments
      // passed in callee saved registers.
      // Omitting the kill flags is conservatively correct even if the live-in
      // is not used after all.
      Regs.push_back(std::make_pair(Reg, /*isKill=*/!isLiveIn));
    }

    if (Regs.empty())
      continue;

    llvm::sort(Regs, [&](const RegAndKill &LHS, const RegAndKill &RHS) {
      return TRI.getEncodingValue(LHS.first) < TRI.getEncodingValue(RHS.first);
    });

    if (Regs.size() > 1 || StrOpc== 0) {
      MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(StmOpc), ARM::SP)
                                    .addReg(ARM::SP)
                                    .setMIFlags(MIFlags)
                                    .add(predOps(ARMCC::AL));
      for (unsigned i = 0, e = Regs.size(); i < e; ++i)
        MIB.addReg(Regs[i].first, getKillRegState(Regs[i].second));
    } else if (Regs.size() == 1) {
      BuildMI(MBB, MI, DL, TII.get(StrOpc), ARM::SP)
          .addReg(Regs[0].first, getKillRegState(Regs[0].second))
          .addReg(ARM::SP)
          .setMIFlags(MIFlags)
          .addImm(-4)
          .add(predOps(ARMCC::AL));
    }
    Regs.clear();

    // Put any subsequent vpush instructions before this one: they will refer to
    // higher register numbers so need to be pushed first in order to preserve
    // monotonicity.
    if (MI != MBB.begin())
      --MI;
  }
}

void ARMFrameLowering::emitPopInst(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   MutableArrayRef<CalleeSavedInfo> CSI,
                                   unsigned LdmOpc, unsigned LdrOpc,
                                   bool isVarArg, bool NoGap,
                                   bool (*Func)(unsigned, bool),
                                   unsigned NumAlignedDPRCS2Regs) const {
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  const TargetRegisterInfo &TRI = *STI.getRegisterInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  bool hasPAC = AFI->shouldSignReturnAddress();
  DebugLoc DL;
  bool isTailCall = false;
  bool isInterrupt = false;
  bool isTrap = false;
  bool isCmseEntry = false;
  if (MBB.end() != MI) {
    DL = MI->getDebugLoc();
    unsigned RetOpcode = MI->getOpcode();
    isTailCall =
        (RetOpcode == ARM::TCRETURNdi || RetOpcode == ARM::TCRETURNri ||
         RetOpcode == ARM::TCRETURNrinotr12);
    isInterrupt =
        RetOpcode == ARM::SUBS_PC_LR || RetOpcode == ARM::t2SUBS_PC_LR;
    isTrap =
        RetOpcode == ARM::TRAP || RetOpcode == ARM::TRAPNaCl ||
        RetOpcode == ARM::tTRAP;
    isCmseEntry = (RetOpcode == ARM::tBXNS || RetOpcode == ARM::tBXNS_RET);
  }

  SmallVector<unsigned, 4> Regs;
  unsigned i = CSI.size();
  while (i != 0) {
    unsigned LastReg = 0;
    bool DeleteRet = false;
    for (; i != 0; --i) {
      CalleeSavedInfo &Info = CSI[i-1];
      Register Reg = Info.getReg();
      if (!(Func)(Reg, STI.splitFramePushPop(MF))) continue;

      // The aligned reloads from area DPRCS2 are not inserted here.
      if (Reg >= ARM::D8 && Reg < ARM::D8 + NumAlignedDPRCS2Regs)
        continue;
      if (Reg == ARM::LR && !isTailCall && !isVarArg && !isInterrupt &&
          !isCmseEntry && !isTrap && AFI->getArgumentStackToRestore() == 0 &&
          STI.hasV5TOps() && MBB.succ_empty() && !hasPAC &&
          !STI.splitFramePointerPush(MF)) {
        Reg = ARM::PC;
        // Fold the return instruction into the LDM.
        DeleteRet = true;
        LdmOpc = AFI->isThumbFunction() ? ARM::t2LDMIA_RET : ARM::LDMIA_RET;
      }

      // If NoGap is true, pop consecutive registers and then leave the rest
      // for other instructions. e.g.
      // vpop {d8, d10, d11} -> vpop {d8}, vpop {d10, d11}
      if (NoGap && LastReg && LastReg != Reg-1)
        break;

      LastReg = Reg;
      Regs.push_back(Reg);
    }

    if (Regs.empty())
      continue;

    llvm::sort(Regs, [&](unsigned LHS, unsigned RHS) {
      return TRI.getEncodingValue(LHS) < TRI.getEncodingValue(RHS);
    });

    if (Regs.size() > 1 || LdrOpc == 0) {
      MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(LdmOpc), ARM::SP)
                                    .addReg(ARM::SP)
                                    .add(predOps(ARMCC::AL))
                                    .setMIFlags(MachineInstr::FrameDestroy);
      for (unsigned Reg : Regs)
        MIB.addReg(Reg, getDefRegState(true));
      if (DeleteRet) {
        if (MI != MBB.end()) {
          MIB.copyImplicitOps(*MI);
          MI->eraseFromParent();
        }
      }
      MI = MIB;
    } else if (Regs.size() == 1) {
      // If we adjusted the reg to PC from LR above, switch it back here. We
      // only do that for LDM.
      if (Regs[0] == ARM::PC)
        Regs[0] = ARM::LR;
      MachineInstrBuilder MIB =
        BuildMI(MBB, MI, DL, TII.get(LdrOpc), Regs[0])
          .addReg(ARM::SP, RegState::Define)
          .addReg(ARM::SP)
          .setMIFlags(MachineInstr::FrameDestroy);
      // ARM mode needs an extra reg0 here due to addrmode2. Will go away once
      // that refactoring is complete (eventually).
      if (LdrOpc == ARM::LDR_POST_REG || LdrOpc == ARM::LDR_POST_IMM) {
        MIB.addReg(0);
        MIB.addImm(ARM_AM::getAM2Opc(ARM_AM::add, 4, ARM_AM::no_shift));
      } else
        MIB.addImm(4);
      MIB.add(predOps(ARMCC::AL));
    }
    Regs.clear();

    // Put any subsequent vpop instructions after this one: they will refer to
    // higher register numbers so need to be popped afterwards.
    if (MI != MBB.end())
      ++MI;
  }
}

/// Emit aligned spill instructions for NumAlignedDPRCS2Regs D-registers
/// starting from d8.  Also insert stack realignment code and leave the stack
/// pointer pointing to the d8 spill slot.
static void emitAlignedDPRCS2Spills(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MI,
                                    unsigned NumAlignedDPRCS2Regs,
                                    ArrayRef<CalleeSavedInfo> CSI,
                                    const TargetRegisterInfo *TRI) {
  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Mark the D-register spill slots as properly aligned.  Since MFI computes
  // stack slot layout backwards, this can actually mean that the d-reg stack
  // slot offsets can be wrong. The offset for d8 will always be correct.
  for (const CalleeSavedInfo &I : CSI) {
    unsigned DNum = I.getReg() - ARM::D8;
    if (DNum > NumAlignedDPRCS2Regs - 1)
      continue;
    int FI = I.getFrameIdx();
    // The even-numbered registers will be 16-byte aligned, the odd-numbered
    // registers will be 8-byte aligned.
    MFI.setObjectAlignment(FI, DNum % 2 ? Align(8) : Align(16));

    // The stack slot for D8 needs to be maximally aligned because this is
    // actually the point where we align the stack pointer.  MachineFrameInfo
    // computes all offsets relative to the incoming stack pointer which is a
    // bit weird when realigning the stack.  Any extra padding for this
    // over-alignment is not realized because the code inserted below adjusts
    // the stack pointer by numregs * 8 before aligning the stack pointer.
    if (DNum == 0)
      MFI.setObjectAlignment(FI, MFI.getMaxAlign());
  }

  // Move the stack pointer to the d8 spill slot, and align it at the same
  // time. Leave the stack slot address in the scratch register r4.
  //
  //   sub r4, sp, #numregs * 8
  //   bic r4, r4, #align - 1
  //   mov sp, r4
  //
  bool isThumb = AFI->isThumbFunction();
  assert(!AFI->isThumb1OnlyFunction() && "Can't realign stack for thumb1");
  AFI->setShouldRestoreSPFromFP(true);

  // sub r4, sp, #numregs * 8
  // The immediate is <= 64, so it doesn't need any special encoding.
  unsigned Opc = isThumb ? ARM::t2SUBri : ARM::SUBri;
  BuildMI(MBB, MI, DL, TII.get(Opc), ARM::R4)
      .addReg(ARM::SP)
      .addImm(8 * NumAlignedDPRCS2Regs)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());

  Align MaxAlign = MF.getFrameInfo().getMaxAlign();
  // We must set parameter MustBeSingleInstruction to true, since
  // skipAlignedDPRCS2Spills expects exactly 3 instructions to perform
  // stack alignment.  Luckily, this can always be done since all ARM
  // architecture versions that support Neon also support the BFC
  // instruction.
  emitAligningInstructions(MF, AFI, TII, MBB, MI, DL, ARM::R4, MaxAlign, true);

  // mov sp, r4
  // The stack pointer must be adjusted before spilling anything, otherwise
  // the stack slots could be clobbered by an interrupt handler.
  // Leave r4 live, it is used below.
  Opc = isThumb ? ARM::tMOVr : ARM::MOVr;
  MachineInstrBuilder MIB = BuildMI(MBB, MI, DL, TII.get(Opc), ARM::SP)
                                .addReg(ARM::R4)
                                .add(predOps(ARMCC::AL));
  if (!isThumb)
    MIB.add(condCodeOp());

  // Now spill NumAlignedDPRCS2Regs registers starting from d8.
  // r4 holds the stack slot address.
  unsigned NextReg = ARM::D8;

  // 16-byte aligned vst1.64 with 4 d-regs and address writeback.
  // The writeback is only needed when emitting two vst1.64 instructions.
  if (NumAlignedDPRCS2Regs >= 6) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QQPRRegClass);
    MBB.addLiveIn(SupReg);
    BuildMI(MBB, MI, DL, TII.get(ARM::VST1d64Qwb_fixed), ARM::R4)
        .addReg(ARM::R4, RegState::Kill)
        .addImm(16)
        .addReg(NextReg)
        .addReg(SupReg, RegState::ImplicitKill)
        .add(predOps(ARMCC::AL));
    NextReg += 4;
    NumAlignedDPRCS2Regs -= 4;
  }

  // We won't modify r4 beyond this point.  It currently points to the next
  // register to be spilled.
  unsigned R4BaseReg = NextReg;

  // 16-byte aligned vst1.64 with 4 d-regs, no writeback.
  if (NumAlignedDPRCS2Regs >= 4) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QQPRRegClass);
    MBB.addLiveIn(SupReg);
    BuildMI(MBB, MI, DL, TII.get(ARM::VST1d64Q))
        .addReg(ARM::R4)
        .addImm(16)
        .addReg(NextReg)
        .addReg(SupReg, RegState::ImplicitKill)
        .add(predOps(ARMCC::AL));
    NextReg += 4;
    NumAlignedDPRCS2Regs -= 4;
  }

  // 16-byte aligned vst1.64 with 2 d-regs.
  if (NumAlignedDPRCS2Regs >= 2) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QPRRegClass);
    MBB.addLiveIn(SupReg);
    BuildMI(MBB, MI, DL, TII.get(ARM::VST1q64))
        .addReg(ARM::R4)
        .addImm(16)
        .addReg(SupReg)
        .add(predOps(ARMCC::AL));
    NextReg += 2;
    NumAlignedDPRCS2Regs -= 2;
  }

  // Finally, use a vanilla vstr.64 for the odd last register.
  if (NumAlignedDPRCS2Regs) {
    MBB.addLiveIn(NextReg);
    // vstr.64 uses addrmode5 which has an offset scale of 4.
    BuildMI(MBB, MI, DL, TII.get(ARM::VSTRD))
        .addReg(NextReg)
        .addReg(ARM::R4)
        .addImm((NextReg - R4BaseReg) * 2)
        .add(predOps(ARMCC::AL));
  }

  // The last spill instruction inserted should kill the scratch register r4.
  std::prev(MI)->addRegisterKilled(ARM::R4, TRI);
}

/// Skip past the code inserted by emitAlignedDPRCS2Spills, and return an
/// iterator to the following instruction.
static MachineBasicBlock::iterator
skipAlignedDPRCS2Spills(MachineBasicBlock::iterator MI,
                        unsigned NumAlignedDPRCS2Regs) {
  //   sub r4, sp, #numregs * 8
  //   bic r4, r4, #align - 1
  //   mov sp, r4
  ++MI; ++MI; ++MI;
  assert(MI->mayStore() && "Expecting spill instruction");

  // These switches all fall through.
  switch(NumAlignedDPRCS2Regs) {
  case 7:
    ++MI;
    assert(MI->mayStore() && "Expecting spill instruction");
    [[fallthrough]];
  default:
    ++MI;
    assert(MI->mayStore() && "Expecting spill instruction");
    [[fallthrough]];
  case 1:
  case 2:
  case 4:
    assert(MI->killsRegister(ARM::R4, /*TRI=*/nullptr) && "Missed kill flag");
    ++MI;
  }
  return MI;
}

/// Emit aligned reload instructions for NumAlignedDPRCS2Regs D-registers
/// starting from d8.  These instructions are assumed to execute while the
/// stack is still aligned, unlike the code inserted by emitPopInst.
static void emitAlignedDPRCS2Restores(MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator MI,
                                      unsigned NumAlignedDPRCS2Regs,
                                      ArrayRef<CalleeSavedInfo> CSI,
                                      const TargetRegisterInfo *TRI) {
  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  DebugLoc DL = MI != MBB.end() ? MI->getDebugLoc() : DebugLoc();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();

  // Find the frame index assigned to d8.
  int D8SpillFI = 0;
  for (const CalleeSavedInfo &I : CSI)
    if (I.getReg() == ARM::D8) {
      D8SpillFI = I.getFrameIdx();
      break;
    }

  // Materialize the address of the d8 spill slot into the scratch register r4.
  // This can be fairly complicated if the stack frame is large, so just use
  // the normal frame index elimination mechanism to do it.  This code runs as
  // the initial part of the epilog where the stack and base pointers haven't
  // been changed yet.
  bool isThumb = AFI->isThumbFunction();
  assert(!AFI->isThumb1OnlyFunction() && "Can't realign stack for thumb1");

  unsigned Opc = isThumb ? ARM::t2ADDri : ARM::ADDri;
  BuildMI(MBB, MI, DL, TII.get(Opc), ARM::R4)
      .addFrameIndex(D8SpillFI)
      .addImm(0)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());

  // Now restore NumAlignedDPRCS2Regs registers starting from d8.
  unsigned NextReg = ARM::D8;

  // 16-byte aligned vld1.64 with 4 d-regs and writeback.
  if (NumAlignedDPRCS2Regs >= 6) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QQPRRegClass);
    BuildMI(MBB, MI, DL, TII.get(ARM::VLD1d64Qwb_fixed), NextReg)
        .addReg(ARM::R4, RegState::Define)
        .addReg(ARM::R4, RegState::Kill)
        .addImm(16)
        .addReg(SupReg, RegState::ImplicitDefine)
        .add(predOps(ARMCC::AL));
    NextReg += 4;
    NumAlignedDPRCS2Regs -= 4;
  }

  // We won't modify r4 beyond this point.  It currently points to the next
  // register to be spilled.
  unsigned R4BaseReg = NextReg;

  // 16-byte aligned vld1.64 with 4 d-regs, no writeback.
  if (NumAlignedDPRCS2Regs >= 4) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QQPRRegClass);
    BuildMI(MBB, MI, DL, TII.get(ARM::VLD1d64Q), NextReg)
        .addReg(ARM::R4)
        .addImm(16)
        .addReg(SupReg, RegState::ImplicitDefine)
        .add(predOps(ARMCC::AL));
    NextReg += 4;
    NumAlignedDPRCS2Regs -= 4;
  }

  // 16-byte aligned vld1.64 with 2 d-regs.
  if (NumAlignedDPRCS2Regs >= 2) {
    unsigned SupReg = TRI->getMatchingSuperReg(NextReg, ARM::dsub_0,
                                               &ARM::QPRRegClass);
    BuildMI(MBB, MI, DL, TII.get(ARM::VLD1q64), SupReg)
        .addReg(ARM::R4)
        .addImm(16)
        .add(predOps(ARMCC::AL));
    NextReg += 2;
    NumAlignedDPRCS2Regs -= 2;
  }

  // Finally, use a vanilla vldr.64 for the remaining odd register.
  if (NumAlignedDPRCS2Regs)
    BuildMI(MBB, MI, DL, TII.get(ARM::VLDRD), NextReg)
        .addReg(ARM::R4)
        .addImm(2 * (NextReg - R4BaseReg))
        .add(predOps(ARMCC::AL));

  // Last store kills r4.
  std::prev(MI)->addRegisterKilled(ARM::R4, TRI);
}

bool ARMFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  unsigned PushOpc = AFI->isThumbFunction() ? ARM::t2STMDB_UPD : ARM::STMDB_UPD;
  unsigned PushOneOpc = AFI->isThumbFunction() ?
    ARM::t2STR_PRE : ARM::STR_PRE_IMM;
  unsigned FltOpc = ARM::VSTMDDB_UPD;
  unsigned NumAlignedDPRCS2Regs = AFI->getNumAlignedDPRCS2Regs();
  // Compute PAC in R12.
  if (AFI->shouldSignReturnAddress()) {
    BuildMI(MBB, MI, DebugLoc(), STI.getInstrInfo()->get(ARM::t2PAC))
        .setMIFlags(MachineInstr::FrameSetup);
  }
  // Save the non-secure floating point context.
  if (llvm::any_of(CSI, [](const CalleeSavedInfo &C) {
        return C.getReg() == ARM::FPCXTNS;
      })) {
    BuildMI(MBB, MI, DebugLoc(), STI.getInstrInfo()->get(ARM::VSTR_FPCXTNS_pre),
            ARM::SP)
        .addReg(ARM::SP)
        .addImm(-4)
        .add(predOps(ARMCC::AL));
  }
  if (STI.splitFramePointerPush(MF)) {
    emitPushInst(MBB, MI, CSI, PushOpc, PushOneOpc, false,
                 &isSplitFPArea1Register, 0, MachineInstr::FrameSetup);
    emitPushInst(MBB, MI, CSI, FltOpc, 0, true, &isARMArea3Register,
                 NumAlignedDPRCS2Regs, MachineInstr::FrameSetup);
    emitPushInst(MBB, MI, CSI, PushOpc, PushOneOpc, false,
                 &isSplitFPArea2Register, 0, MachineInstr::FrameSetup);
  } else {
    emitPushInst(MBB, MI, CSI, PushOpc, PushOneOpc, false, &isARMArea1Register,
                 0, MachineInstr::FrameSetup);
    emitPushInst(MBB, MI, CSI, PushOpc, PushOneOpc, false, &isARMArea2Register,
                 0, MachineInstr::FrameSetup);
    emitPushInst(MBB, MI, CSI, FltOpc, 0, true, &isARMArea3Register,
                 NumAlignedDPRCS2Regs, MachineInstr::FrameSetup);
  }

  // The code above does not insert spill code for the aligned DPRCS2 registers.
  // The stack realignment code will be inserted between the push instructions
  // and these spills.
  if (NumAlignedDPRCS2Regs)
    emitAlignedDPRCS2Spills(MBB, MI, NumAlignedDPRCS2Regs, CSI, TRI);

  return true;
}

bool ARMFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return false;

  MachineFunction &MF = *MBB.getParent();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  bool isVarArg = AFI->getArgRegsSaveSize() > 0;
  unsigned NumAlignedDPRCS2Regs = AFI->getNumAlignedDPRCS2Regs();

  // The emitPopInst calls below do not insert reloads for the aligned DPRCS2
  // registers. Do that here instead.
  if (NumAlignedDPRCS2Regs)
    emitAlignedDPRCS2Restores(MBB, MI, NumAlignedDPRCS2Regs, CSI, TRI);

  unsigned PopOpc = AFI->isThumbFunction() ? ARM::t2LDMIA_UPD : ARM::LDMIA_UPD;
  unsigned LdrOpc =
      AFI->isThumbFunction() ? ARM::t2LDR_POST : ARM::LDR_POST_IMM;
  unsigned FltOpc = ARM::VLDMDIA_UPD;
  if (STI.splitFramePointerPush(MF)) {
    emitPopInst(MBB, MI, CSI, PopOpc, LdrOpc, isVarArg, false,
                &isSplitFPArea2Register, 0);
    emitPopInst(MBB, MI, CSI, FltOpc, 0, isVarArg, true, &isARMArea3Register,
                NumAlignedDPRCS2Regs);
    emitPopInst(MBB, MI, CSI, PopOpc, LdrOpc, isVarArg, false,
                &isSplitFPArea1Register, 0);
  } else {
    emitPopInst(MBB, MI, CSI, FltOpc, 0, isVarArg, true, &isARMArea3Register,
                NumAlignedDPRCS2Regs);
    emitPopInst(MBB, MI, CSI, PopOpc, LdrOpc, isVarArg, false,
                &isARMArea2Register, 0);
    emitPopInst(MBB, MI, CSI, PopOpc, LdrOpc, isVarArg, false,
                &isARMArea1Register, 0);
  }

  return true;
}

// FIXME: Make generic?
static unsigned EstimateFunctionSizeInBytes(const MachineFunction &MF,
                                            const ARMBaseInstrInfo &TII) {
  unsigned FnSize = 0;
  for (auto &MBB : MF) {
    for (auto &MI : MBB)
      FnSize += TII.getInstSizeInBytes(MI);
  }
  if (MF.getJumpTableInfo())
    for (auto &Table: MF.getJumpTableInfo()->getJumpTables())
      FnSize += Table.MBBs.size() * 4;
  FnSize += MF.getConstantPool()->getConstants().size() * 4;
  return FnSize;
}

/// estimateRSStackSizeLimit - Look at each instruction that references stack
/// frames and return the stack size limit beyond which some of these
/// instructions will require a scratch register during their expansion later.
// FIXME: Move to TII?
static unsigned estimateRSStackSizeLimit(MachineFunction &MF,
                                         const TargetFrameLowering *TFI,
                                         bool &HasNonSPFrameIndex) {
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  const ARMBaseInstrInfo &TII =
      *static_cast<const ARMBaseInstrInfo *>(MF.getSubtarget().getInstrInfo());
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  unsigned Limit = (1 << 12) - 1;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      if (MI.isDebugInstr())
        continue;
      for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
        if (!MI.getOperand(i).isFI())
          continue;

        // When using ADDri to get the address of a stack object, 255 is the
        // largest offset guaranteed to fit in the immediate offset.
        if (MI.getOpcode() == ARM::ADDri) {
          Limit = std::min(Limit, (1U << 8) - 1);
          break;
        }
        // t2ADDri will not require an extra register, it can reuse the
        // destination.
        if (MI.getOpcode() == ARM::t2ADDri || MI.getOpcode() == ARM::t2ADDri12)
          break;

        const MCInstrDesc &MCID = MI.getDesc();
        const TargetRegisterClass *RegClass = TII.getRegClass(MCID, i, TRI, MF);
        if (RegClass && !RegClass->contains(ARM::SP))
          HasNonSPFrameIndex = true;

        // Otherwise check the addressing mode.
        switch (MI.getDesc().TSFlags & ARMII::AddrModeMask) {
        case ARMII::AddrMode_i12:
        case ARMII::AddrMode2:
          // Default 12 bit limit.
          break;
        case ARMII::AddrMode3:
        case ARMII::AddrModeT2_i8neg:
          Limit = std::min(Limit, (1U << 8) - 1);
          break;
        case ARMII::AddrMode5FP16:
          Limit = std::min(Limit, ((1U << 8) - 1) * 2);
          break;
        case ARMII::AddrMode5:
        case ARMII::AddrModeT2_i8s4:
        case ARMII::AddrModeT2_ldrex:
          Limit = std::min(Limit, ((1U << 8) - 1) * 4);
          break;
        case ARMII::AddrModeT2_i12:
          // i12 supports only positive offset so these will be converted to
          // i8 opcodes. See llvm::rewriteT2FrameIndex.
          if (TFI->hasFP(MF) && AFI->hasStackFrame())
            Limit = std::min(Limit, (1U << 8) - 1);
          break;
        case ARMII::AddrMode4:
        case ARMII::AddrMode6:
          // Addressing modes 4 & 6 (load/store) instructions can't encode an
          // immediate offset for stack references.
          return 0;
        case ARMII::AddrModeT2_i7:
          Limit = std::min(Limit, ((1U << 7) - 1) * 1);
          break;
        case ARMII::AddrModeT2_i7s2:
          Limit = std::min(Limit, ((1U << 7) - 1) * 2);
          break;
        case ARMII::AddrModeT2_i7s4:
          Limit = std::min(Limit, ((1U << 7) - 1) * 4);
          break;
        default:
          llvm_unreachable("Unhandled addressing mode in stack size limit calculation");
        }
        break; // At most one FI per instruction
      }
    }
  }

  return Limit;
}

// In functions that realign the stack, it can be an advantage to spill the
// callee-saved vector registers after realigning the stack. The vst1 and vld1
// instructions take alignment hints that can improve performance.
static void
checkNumAlignedDPRCS2Regs(MachineFunction &MF, BitVector &SavedRegs) {
  MF.getInfo<ARMFunctionInfo>()->setNumAlignedDPRCS2Regs(0);
  if (!SpillAlignedNEONRegs)
    return;

  // Naked functions don't spill callee-saved registers.
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return;

  // We are planning to use NEON instructions vst1 / vld1.
  if (!MF.getSubtarget<ARMSubtarget>().hasNEON())
    return;

  // Don't bother if the default stack alignment is sufficiently high.
  if (MF.getSubtarget().getFrameLowering()->getStackAlign() >= Align(8))
    return;

  // Aligned spills require stack realignment.
  if (!static_cast<const ARMBaseRegisterInfo *>(
           MF.getSubtarget().getRegisterInfo())->canRealignStack(MF))
    return;

  // We always spill contiguous d-registers starting from d8. Count how many
  // needs spilling.  The register allocator will almost always use the
  // callee-saved registers in order, but it can happen that there are holes in
  // the range.  Registers above the hole will be spilled to the standard DPRCS
  // area.
  unsigned NumSpills = 0;
  for (; NumSpills < 8; ++NumSpills)
    if (!SavedRegs.test(ARM::D8 + NumSpills))
      break;

  // Don't do this for just one d-register. It's not worth it.
  if (NumSpills < 2)
    return;

  // Spill the first NumSpills D-registers after realigning the stack.
  MF.getInfo<ARMFunctionInfo>()->setNumAlignedDPRCS2Regs(NumSpills);

  // A scratch register is required for the vst1 / vld1 instructions.
  SavedRegs.set(ARM::R4);
}

bool ARMFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  // For CMSE entry functions, we want to save the FPCXT_NS immediately
  // upon function entry (resp. restore it immmediately before return)
  if (STI.hasV8_1MMainlineOps() &&
      MF.getInfo<ARMFunctionInfo>()->isCmseNSEntryFunction())
    return false;

  // We are disabling shrinkwrapping for now when PAC is enabled, as
  // shrinkwrapping can cause clobbering of r12 when the PAC code is
  // generated. A follow-up patch will fix this in a more performant manner.
  if (MF.getInfo<ARMFunctionInfo>()->shouldSignReturnAddress(
          true /* SpillsLR */))
    return false;

  return true;
}

bool ARMFrameLowering::requiresAAPCSFrameRecord(
    const MachineFunction &MF) const {
  const auto &Subtarget = MF.getSubtarget<ARMSubtarget>();
  return Subtarget.createAAPCSFrameChain() && hasFP(MF);
}

// Thumb1 may require a spill when storing to a frame index through FP (or any
// access with execute-only), for cases where FP is a high register (R11). This
// scans the function for cases where this may happen.
static bool canSpillOnFrameIndexAccess(const MachineFunction &MF,
                                       const TargetFrameLowering &TFI) {
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  if (!AFI->isThumb1OnlyFunction())
    return false;

  const ARMSubtarget &STI = MF.getSubtarget<ARMSubtarget>();
  for (const auto &MBB : MF)
    for (const auto &MI : MBB)
      if (MI.getOpcode() == ARM::tSTRspi || MI.getOpcode() == ARM::tSTRi ||
          STI.genExecuteOnly())
        for (const auto &Op : MI.operands())
          if (Op.isFI()) {
            Register Reg;
            TFI.getFrameIndexReference(MF, Op.getIndex(), Reg);
            if (ARM::hGPRRegClass.contains(Reg) && Reg != ARM::SP)
              return true;
          }
  return false;
}

void ARMFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                            BitVector &SavedRegs,
                                            RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // This tells PEI to spill the FP as if it is any other callee-save register
  // to take advantage the eliminateFrameIndex machinery. This also ensures it
  // is spilled in the order specified by getCalleeSavedRegs() to make it easier
  // to combine multiple loads / stores.
  bool CanEliminateFrame = !(requiresAAPCSFrameRecord(MF) && hasFP(MF));
  bool CS1Spilled = false;
  bool LRSpilled = false;
  unsigned NumGPRSpills = 0;
  unsigned NumFPRSpills = 0;
  SmallVector<unsigned, 4> UnspilledCS1GPRs;
  SmallVector<unsigned, 4> UnspilledCS2GPRs;
  const ARMBaseRegisterInfo *RegInfo = static_cast<const ARMBaseRegisterInfo *>(
      MF.getSubtarget().getRegisterInfo());
  const ARMBaseInstrInfo &TII =
      *static_cast<const ARMBaseInstrInfo *>(MF.getSubtarget().getInstrInfo());
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  (void)TRI;  // Silence unused warning in non-assert builds.
  Register FramePtr = RegInfo->getFrameRegister(MF);

  // Spill R4 if Thumb2 function requires stack realignment - it will be used as
  // scratch register. Also spill R4 if Thumb2 function has varsized objects,
  // since it's not always possible to restore sp from fp in a single
  // instruction.
  // FIXME: It will be better just to find spare register here.
  if (AFI->isThumb2Function() &&
      (MFI.hasVarSizedObjects() || RegInfo->hasStackRealignment(MF)))
    SavedRegs.set(ARM::R4);

  // If a stack probe will be emitted, spill R4 and LR, since they are
  // clobbered by the stack probe call.
  // This estimate should be a safe, conservative estimate. The actual
  // stack probe is enabled based on the size of the local objects;
  // this estimate also includes the varargs store size.
  if (STI.isTargetWindows() &&
      WindowsRequiresStackProbe(MF, MFI.estimateStackSize(MF))) {
    SavedRegs.set(ARM::R4);
    SavedRegs.set(ARM::LR);
  }

  if (AFI->isThumb1OnlyFunction()) {
    // Spill LR if Thumb1 function uses variable length argument lists.
    if (AFI->getArgRegsSaveSize() > 0)
      SavedRegs.set(ARM::LR);

    // Spill R4 if Thumb1 epilogue has to restore SP from FP or the function
    // requires stack alignment.  We don't know for sure what the stack size
    // will be, but for this, an estimate is good enough. If there anything
    // changes it, it'll be a spill, which implies we've used all the registers
    // and so R4 is already used, so not marking it here will be OK.
    // FIXME: It will be better just to find spare register here.
    if (MFI.hasVarSizedObjects() || RegInfo->hasStackRealignment(MF) ||
        MFI.estimateStackSize(MF) > 508)
      SavedRegs.set(ARM::R4);
  }

  // See if we can spill vector registers to aligned stack.
  checkNumAlignedDPRCS2Regs(MF, SavedRegs);

  // Spill the BasePtr if it's used.
  if (RegInfo->hasBasePointer(MF))
    SavedRegs.set(RegInfo->getBaseRegister());

  // On v8.1-M.Main CMSE entry functions save/restore FPCXT.
  if (STI.hasV8_1MMainlineOps() && AFI->isCmseNSEntryFunction())
    CanEliminateFrame = false;

  // When return address signing is enabled R12 is treated as callee-saved.
  if (AFI->shouldSignReturnAddress())
    CanEliminateFrame = false;

  // Don't spill FP if the frame can be eliminated. This is determined
  // by scanning the callee-save registers to see if any is modified.
  const MCPhysReg *CSRegs = RegInfo->getCalleeSavedRegs(&MF);
  for (unsigned i = 0; CSRegs[i]; ++i) {
    unsigned Reg = CSRegs[i];
    bool Spilled = false;
    if (SavedRegs.test(Reg)) {
      Spilled = true;
      CanEliminateFrame = false;
    }

    if (!ARM::GPRRegClass.contains(Reg)) {
      if (Spilled) {
        if (ARM::SPRRegClass.contains(Reg))
          NumFPRSpills++;
        else if (ARM::DPRRegClass.contains(Reg))
          NumFPRSpills += 2;
        else if (ARM::QPRRegClass.contains(Reg))
          NumFPRSpills += 4;
      }
      continue;
    }

    if (Spilled) {
      NumGPRSpills++;

      if (!STI.splitFramePushPop(MF)) {
        if (Reg == ARM::LR)
          LRSpilled = true;
        CS1Spilled = true;
        continue;
      }

      // Keep track if LR and any of R4, R5, R6, and R7 is spilled.
      switch (Reg) {
      case ARM::LR:
        LRSpilled = true;
        [[fallthrough]];
      case ARM::R0: case ARM::R1:
      case ARM::R2: case ARM::R3:
      case ARM::R4: case ARM::R5:
      case ARM::R6: case ARM::R7:
        CS1Spilled = true;
        break;
      default:
        break;
      }
    } else {
      if (!STI.splitFramePushPop(MF)) {
        UnspilledCS1GPRs.push_back(Reg);
        continue;
      }

      switch (Reg) {
      case ARM::R0: case ARM::R1:
      case ARM::R2: case ARM::R3:
      case ARM::R4: case ARM::R5:
      case ARM::R6: case ARM::R7:
      case ARM::LR:
        UnspilledCS1GPRs.push_back(Reg);
        break;
      default:
        UnspilledCS2GPRs.push_back(Reg);
        break;
      }
    }
  }

  bool ForceLRSpill = false;
  if (!LRSpilled && AFI->isThumb1OnlyFunction()) {
    unsigned FnSize = EstimateFunctionSizeInBytes(MF, TII);
    // Force LR to be spilled if the Thumb function size is > 2048. This enables
    // use of BL to implement far jump.
    if (FnSize >= (1 << 11)) {
      CanEliminateFrame = false;
      ForceLRSpill = true;
    }
  }

  // If any of the stack slot references may be out of range of an immediate
  // offset, make sure a register (or a spill slot) is available for the
  // register scavenger. Note that if we're indexing off the frame pointer, the
  // effective stack size is 4 bytes larger since the FP points to the stack
  // slot of the previous FP. Also, if we have variable sized objects in the
  // function, stack slot references will often be negative, and some of
  // our instructions are positive-offset only, so conservatively consider
  // that case to want a spill slot (or register) as well. Similarly, if
  // the function adjusts the stack pointer during execution and the
  // adjustments aren't already part of our stack size estimate, our offset
  // calculations may be off, so be conservative.
  // FIXME: We could add logic to be more precise about negative offsets
  //        and which instructions will need a scratch register for them. Is it
  //        worth the effort and added fragility?
  unsigned EstimatedStackSize =
      MFI.estimateStackSize(MF) + 4 * (NumGPRSpills + NumFPRSpills);

  // Determine biggest (positive) SP offset in MachineFrameInfo.
  int MaxFixedOffset = 0;
  for (int I = MFI.getObjectIndexBegin(); I < 0; ++I) {
    int MaxObjectOffset = MFI.getObjectOffset(I) + MFI.getObjectSize(I);
    MaxFixedOffset = std::max(MaxFixedOffset, MaxObjectOffset);
  }

  bool HasFP = hasFP(MF);
  if (HasFP) {
    if (AFI->hasStackFrame())
      EstimatedStackSize += 4;
  } else {
    // If FP is not used, SP will be used to access arguments, so count the
    // size of arguments into the estimation.
    EstimatedStackSize += MaxFixedOffset;
  }
  EstimatedStackSize += 16; // For possible paddings.

  unsigned EstimatedRSStackSizeLimit, EstimatedRSFixedSizeLimit;
  bool HasNonSPFrameIndex = false;
  if (AFI->isThumb1OnlyFunction()) {
    // For Thumb1, don't bother to iterate over the function. The only
    // instruction that requires an emergency spill slot is a store to a
    // frame index.
    //
    // tSTRspi, which is used for sp-relative accesses, has an 8-bit unsigned
    // immediate. tSTRi, which is used for bp- and fp-relative accesses, has
    // a 5-bit unsigned immediate.
    //
    // We could try to check if the function actually contains a tSTRspi
    // that might need the spill slot, but it's not really important.
    // Functions with VLAs or extremely large call frames are rare, and
    // if a function is allocating more than 1KB of stack, an extra 4-byte
    // slot probably isn't relevant.
    //
    // A special case is the scenario where r11 is used as FP, where accesses
    // to a frame index will require its value to be moved into a low reg.
    // This is handled later on, once we are able to determine if we have any
    // fp-relative accesses.
    if (RegInfo->hasBasePointer(MF))
      EstimatedRSStackSizeLimit = (1U << 5) * 4;
    else
      EstimatedRSStackSizeLimit = (1U << 8) * 4;
    EstimatedRSFixedSizeLimit = (1U << 5) * 4;
  } else {
    EstimatedRSStackSizeLimit =
        estimateRSStackSizeLimit(MF, this, HasNonSPFrameIndex);
    EstimatedRSFixedSizeLimit = EstimatedRSStackSizeLimit;
  }
  // Final estimate of whether sp or bp-relative accesses might require
  // scavenging.
  bool HasLargeStack = EstimatedStackSize > EstimatedRSStackSizeLimit;

  // If the stack pointer moves and we don't have a base pointer, the
  // estimate logic doesn't work. The actual offsets might be larger when
  // we're constructing a call frame, or we might need to use negative
  // offsets from fp.
  bool HasMovingSP = MFI.hasVarSizedObjects() ||
    (MFI.adjustsStack() && !canSimplifyCallFramePseudos(MF));
  bool HasBPOrFixedSP = RegInfo->hasBasePointer(MF) || !HasMovingSP;

  // If we have a frame pointer, we assume arguments will be accessed
  // relative to the frame pointer. Check whether fp-relative accesses to
  // arguments require scavenging.
  //
  // We could do slightly better on Thumb1; in some cases, an sp-relative
  // offset would be legal even though an fp-relative offset is not.
  int MaxFPOffset = getMaxFPOffset(STI, *AFI, MF);
  bool HasLargeArgumentList =
      HasFP && (MaxFixedOffset - MaxFPOffset) > (int)EstimatedRSFixedSizeLimit;

  bool BigFrameOffsets = HasLargeStack || !HasBPOrFixedSP ||
                         HasLargeArgumentList || HasNonSPFrameIndex;
  LLVM_DEBUG(dbgs() << "EstimatedLimit: " << EstimatedRSStackSizeLimit
                    << "; EstimatedStack: " << EstimatedStackSize
                    << "; EstimatedFPStack: " << MaxFixedOffset - MaxFPOffset
                    << "; BigFrameOffsets: " << BigFrameOffsets << "\n");
  if (BigFrameOffsets ||
      !CanEliminateFrame || RegInfo->cannotEliminateFrame(MF)) {
    AFI->setHasStackFrame(true);

    if (HasFP) {
      SavedRegs.set(FramePtr);
      // If the frame pointer is required by the ABI, also spill LR so that we
      // emit a complete frame record.
      if ((requiresAAPCSFrameRecord(MF) ||
           MF.getTarget().Options.DisableFramePointerElim(MF)) &&
          !LRSpilled) {
        SavedRegs.set(ARM::LR);
        LRSpilled = true;
        NumGPRSpills++;
        auto LRPos = llvm::find(UnspilledCS1GPRs, ARM::LR);
        if (LRPos != UnspilledCS1GPRs.end())
          UnspilledCS1GPRs.erase(LRPos);
      }
      auto FPPos = llvm::find(UnspilledCS1GPRs, FramePtr);
      if (FPPos != UnspilledCS1GPRs.end())
        UnspilledCS1GPRs.erase(FPPos);
      NumGPRSpills++;
      if (FramePtr == ARM::R7)
        CS1Spilled = true;
    }

    // This is the number of extra spills inserted for callee-save GPRs which
    // would not otherwise be used by the function. When greater than zero it
    // guaranteees that it is possible to scavenge a register to hold the
    // address of a stack slot. On Thumb1, the register must be a valid operand
    // to tSTRi, i.e. r4-r7. For other subtargets, this is any GPR, i.e. r4-r11
    // or lr.
    //
    // If we don't insert a spill, we instead allocate an emergency spill
    // slot, which can be used by scavenging to spill an arbitrary register.
    //
    // We currently don't try to figure out whether any specific instruction
    // requires scavening an additional register.
    unsigned NumExtraCSSpill = 0;

    if (AFI->isThumb1OnlyFunction()) {
      // For Thumb1-only targets, we need some low registers when we save and
      // restore the high registers (which aren't allocatable, but could be
      // used by inline assembly) because the push/pop instructions can not
      // access high registers. If necessary, we might need to push more low
      // registers to ensure that there is at least one free that can be used
      // for the saving & restoring, and preferably we should ensure that as
      // many as are needed are available so that fewer push/pop instructions
      // are required.

      // Low registers which are not currently pushed, but could be (r4-r7).
      SmallVector<unsigned, 4> AvailableRegs;

      // Unused argument registers (r0-r3) can be clobbered in the prologue for
      // free.
      int EntryRegDeficit = 0;
      for (unsigned Reg : {ARM::R0, ARM::R1, ARM::R2, ARM::R3}) {
        if (!MF.getRegInfo().isLiveIn(Reg)) {
          --EntryRegDeficit;
          LLVM_DEBUG(dbgs()
                     << printReg(Reg, TRI)
                     << " is unused argument register, EntryRegDeficit = "
                     << EntryRegDeficit << "\n");
        }
      }

      // Unused return registers can be clobbered in the epilogue for free.
      int ExitRegDeficit = AFI->getReturnRegsCount() - 4;
      LLVM_DEBUG(dbgs() << AFI->getReturnRegsCount()
                        << " return regs used, ExitRegDeficit = "
                        << ExitRegDeficit << "\n");

      int RegDeficit = std::max(EntryRegDeficit, ExitRegDeficit);
      LLVM_DEBUG(dbgs() << "RegDeficit = " << RegDeficit << "\n");

      // r4-r6 can be used in the prologue if they are pushed by the first push
      // instruction.
      for (unsigned Reg : {ARM::R4, ARM::R5, ARM::R6}) {
        if (SavedRegs.test(Reg)) {
          --RegDeficit;
          LLVM_DEBUG(dbgs() << printReg(Reg, TRI)
                            << " is saved low register, RegDeficit = "
                            << RegDeficit << "\n");
        } else {
          AvailableRegs.push_back(Reg);
          LLVM_DEBUG(
              dbgs()
              << printReg(Reg, TRI)
              << " is non-saved low register, adding to AvailableRegs\n");
        }
      }

      // r7 can be used if it is not being used as the frame pointer.
      if (!HasFP || FramePtr != ARM::R7) {
        if (SavedRegs.test(ARM::R7)) {
          --RegDeficit;
          LLVM_DEBUG(dbgs() << "%r7 is saved low register, RegDeficit = "
                            << RegDeficit << "\n");
        } else {
          AvailableRegs.push_back(ARM::R7);
          LLVM_DEBUG(
              dbgs()
              << "%r7 is non-saved low register, adding to AvailableRegs\n");
        }
      }

      // Each of r8-r11 needs to be copied to a low register, then pushed.
      for (unsigned Reg : {ARM::R8, ARM::R9, ARM::R10, ARM::R11}) {
        if (SavedRegs.test(Reg)) {
          ++RegDeficit;
          LLVM_DEBUG(dbgs() << printReg(Reg, TRI)
                            << " is saved high register, RegDeficit = "
                            << RegDeficit << "\n");
        }
      }

      // LR can only be used by PUSH, not POP, and can't be used at all if the
      // llvm.returnaddress intrinsic is used. This is only worth doing if we
      // are more limited at function entry than exit.
      if ((EntryRegDeficit > ExitRegDeficit) &&
          !(MF.getRegInfo().isLiveIn(ARM::LR) &&
            MF.getFrameInfo().isReturnAddressTaken())) {
        if (SavedRegs.test(ARM::LR)) {
          --RegDeficit;
          LLVM_DEBUG(dbgs() << "%lr is saved register, RegDeficit = "
                            << RegDeficit << "\n");
        } else {
          AvailableRegs.push_back(ARM::LR);
          LLVM_DEBUG(dbgs() << "%lr is not saved, adding to AvailableRegs\n");
        }
      }

      // If there are more high registers that need pushing than low registers
      // available, push some more low registers so that we can use fewer push
      // instructions. This might not reduce RegDeficit all the way to zero,
      // because we can only guarantee that r4-r6 are available, but r8-r11 may
      // need saving.
      LLVM_DEBUG(dbgs() << "Final RegDeficit = " << RegDeficit << "\n");
      for (; RegDeficit > 0 && !AvailableRegs.empty(); --RegDeficit) {
        unsigned Reg = AvailableRegs.pop_back_val();
        LLVM_DEBUG(dbgs() << "Spilling " << printReg(Reg, TRI)
                          << " to make up reg deficit\n");
        SavedRegs.set(Reg);
        NumGPRSpills++;
        CS1Spilled = true;
        assert(!MRI.isReserved(Reg) && "Should not be reserved");
        if (Reg != ARM::LR && !MRI.isPhysRegUsed(Reg))
          NumExtraCSSpill++;
        UnspilledCS1GPRs.erase(llvm::find(UnspilledCS1GPRs, Reg));
        if (Reg == ARM::LR)
          LRSpilled = true;
      }
      LLVM_DEBUG(dbgs() << "After adding spills, RegDeficit = " << RegDeficit
                        << "\n");
    }

    // Avoid spilling LR in Thumb1 if there's a tail call: it's expensive to
    // restore LR in that case.
    bool ExpensiveLRRestore = AFI->isThumb1OnlyFunction() && MFI.hasTailCall();

    // If LR is not spilled, but at least one of R4, R5, R6, and R7 is spilled.
    // Spill LR as well so we can fold BX_RET to the registers restore (LDM).
    if (!LRSpilled && CS1Spilled && !ExpensiveLRRestore) {
      SavedRegs.set(ARM::LR);
      NumGPRSpills++;
      SmallVectorImpl<unsigned>::iterator LRPos;
      LRPos = llvm::find(UnspilledCS1GPRs, (unsigned)ARM::LR);
      if (LRPos != UnspilledCS1GPRs.end())
        UnspilledCS1GPRs.erase(LRPos);

      ForceLRSpill = false;
      if (!MRI.isReserved(ARM::LR) && !MRI.isPhysRegUsed(ARM::LR) &&
          !AFI->isThumb1OnlyFunction())
        NumExtraCSSpill++;
    }

    // If stack and double are 8-byte aligned and we are spilling an odd number
    // of GPRs, spill one extra callee save GPR so we won't have to pad between
    // the integer and double callee save areas.
    LLVM_DEBUG(dbgs() << "NumGPRSpills = " << NumGPRSpills << "\n");
    const Align TargetAlign = getStackAlign();
    if (TargetAlign >= Align(8) && (NumGPRSpills & 1)) {
      if (CS1Spilled && !UnspilledCS1GPRs.empty()) {
        for (unsigned Reg : UnspilledCS1GPRs) {
          // Don't spill high register if the function is thumb.  In the case of
          // Windows on ARM, accept R11 (frame pointer)
          if (!AFI->isThumbFunction() ||
              (STI.isTargetWindows() && Reg == ARM::R11) ||
              isARMLowRegister(Reg) ||
              (Reg == ARM::LR && !ExpensiveLRRestore)) {
            SavedRegs.set(Reg);
            LLVM_DEBUG(dbgs() << "Spilling " << printReg(Reg, TRI)
                              << " to make up alignment\n");
            if (!MRI.isReserved(Reg) && !MRI.isPhysRegUsed(Reg) &&
                !(Reg == ARM::LR && AFI->isThumb1OnlyFunction()))
              NumExtraCSSpill++;
            break;
          }
        }
      } else if (!UnspilledCS2GPRs.empty() && !AFI->isThumb1OnlyFunction()) {
        unsigned Reg = UnspilledCS2GPRs.front();
        SavedRegs.set(Reg);
        LLVM_DEBUG(dbgs() << "Spilling " << printReg(Reg, TRI)
                          << " to make up alignment\n");
        if (!MRI.isReserved(Reg) && !MRI.isPhysRegUsed(Reg))
          NumExtraCSSpill++;
      }
    }

    // Estimate if we might need to scavenge registers at some point in order
    // to materialize a stack offset. If so, either spill one additional
    // callee-saved register or reserve a special spill slot to facilitate
    // register scavenging. Thumb1 needs a spill slot for stack pointer
    // adjustments and for frame index accesses when FP is high register,
    // even when the frame itself is small.
    unsigned RegsNeeded = 0;
    if (BigFrameOffsets || canSpillOnFrameIndexAccess(MF, *this)) {
      RegsNeeded++;
      // With thumb1 execute-only we may need an additional register for saving
      // and restoring the CPSR.
      if (AFI->isThumb1OnlyFunction() && STI.genExecuteOnly() && !STI.useMovt())
        RegsNeeded++;
    }

    if (RegsNeeded > NumExtraCSSpill) {
      // If any non-reserved CS register isn't spilled, just spill one or two
      // extra. That should take care of it!
      unsigned NumExtras = TargetAlign.value() / 4;
      SmallVector<unsigned, 2> Extras;
      while (NumExtras && !UnspilledCS1GPRs.empty()) {
        unsigned Reg = UnspilledCS1GPRs.pop_back_val();
        if (!MRI.isReserved(Reg) &&
            (!AFI->isThumb1OnlyFunction() || isARMLowRegister(Reg))) {
          Extras.push_back(Reg);
          NumExtras--;
        }
      }
      // For non-Thumb1 functions, also check for hi-reg CS registers
      if (!AFI->isThumb1OnlyFunction()) {
        while (NumExtras && !UnspilledCS2GPRs.empty()) {
          unsigned Reg = UnspilledCS2GPRs.pop_back_val();
          if (!MRI.isReserved(Reg)) {
            Extras.push_back(Reg);
            NumExtras--;
          }
        }
      }
      if (NumExtras == 0) {
        for (unsigned Reg : Extras) {
          SavedRegs.set(Reg);
          if (!MRI.isPhysRegUsed(Reg))
            NumExtraCSSpill++;
        }
      }
      while ((RegsNeeded > NumExtraCSSpill) && RS) {
        // Reserve a slot closest to SP or frame pointer.
        LLVM_DEBUG(dbgs() << "Reserving emergency spill slot\n");
        const TargetRegisterClass &RC = ARM::GPRRegClass;
        unsigned Size = TRI->getSpillSize(RC);
        Align Alignment = TRI->getSpillAlign(RC);
        RS->addScavengingFrameIndex(
            MFI.CreateStackObject(Size, Alignment, false));
        --RegsNeeded;
      }
    }
  }

  if (ForceLRSpill)
    SavedRegs.set(ARM::LR);
  AFI->setLRIsSpilled(SavedRegs.test(ARM::LR));
}

void ARMFrameLowering::updateLRRestored(MachineFunction &MF) {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  if (!MFI.isCalleeSavedInfoValid())
    return;

  // Check if all terminators do not implicitly use LR. Then we can 'restore' LR
  // into PC so it is not live out of the return block: Clear the Restored bit
  // in that case.
  for (CalleeSavedInfo &Info : MFI.getCalleeSavedInfo()) {
    if (Info.getReg() != ARM::LR)
      continue;
    if (all_of(MF, [](const MachineBasicBlock &MBB) {
          return all_of(MBB.terminators(), [](const MachineInstr &Term) {
            return !Term.isReturn() || Term.getOpcode() == ARM::LDMIA_RET ||
                   Term.getOpcode() == ARM::t2LDMIA_RET ||
                   Term.getOpcode() == ARM::tPOP_RET;
          });
        })) {
      Info.setRestored(false);
      break;
    }
  }
}

void ARMFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  TargetFrameLowering::processFunctionBeforeFrameFinalized(MF, RS);
  updateLRRestored(MF);
}

void ARMFrameLowering::getCalleeSaves(const MachineFunction &MF,
                                      BitVector &SavedRegs) const {
  TargetFrameLowering::getCalleeSaves(MF, SavedRegs);

  // If we have the "returned" parameter attribute which guarantees that we
  // return the value which was passed in r0 unmodified (e.g. C++ 'structors),
  // record that fact for IPRA.
  const ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  if (AFI->getPreservesR0())
    SavedRegs.set(ARM::R0);
}

bool ARMFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  // For CMSE entry functions, handle floating-point context as if it was a
  // callee-saved register.
  if (STI.hasV8_1MMainlineOps() &&
      MF.getInfo<ARMFunctionInfo>()->isCmseNSEntryFunction()) {
    CSI.emplace_back(ARM::FPCXTNS);
    CSI.back().setRestored(false);
  }

  // For functions, which sign their return address, upon function entry, the
  // return address PAC is computed in R12. Treat R12 as a callee-saved register
  // in this case.
  const auto &AFI = *MF.getInfo<ARMFunctionInfo>();
  if (AFI.shouldSignReturnAddress()) {
    // The order of register must match the order we push them, because the
    // PEI assigns frame indices in that order. When compiling for return
    // address sign and authenication, we use split push, therefore the orders
    // we want are:
    // LR, R7, R6, R5, R4, <R12>, R11, R10,  R9,  R8, D15-D8
    CSI.insert(find_if(CSI,
                       [=](const auto &CS) {
                         Register Reg = CS.getReg();
                         return Reg == ARM::R10 || Reg == ARM::R11 ||
                                Reg == ARM::R8 || Reg == ARM::R9 ||
                                ARM::DPRRegClass.contains(Reg);
                       }),
               CalleeSavedInfo(ARM::R12));
  }

  return false;
}

const TargetFrameLowering::SpillSlot *
ARMFrameLowering::getCalleeSavedSpillSlots(unsigned &NumEntries) const {
  static const SpillSlot FixedSpillOffsets[] = {{ARM::FPCXTNS, -4}};
  NumEntries = std::size(FixedSpillOffsets);
  return FixedSpillOffsets;
}

MachineBasicBlock::iterator ARMFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator I) const {
  const ARMBaseInstrInfo &TII =
      *static_cast<const ARMBaseInstrInfo *>(MF.getSubtarget().getInstrInfo());
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  bool isARM = !AFI->isThumbFunction();
  DebugLoc dl = I->getDebugLoc();
  unsigned Opc = I->getOpcode();
  bool IsDestroy = Opc == TII.getCallFrameDestroyOpcode();
  unsigned CalleePopAmount = IsDestroy ? I->getOperand(1).getImm() : 0;

  assert(!AFI->isThumb1OnlyFunction() &&
         "This eliminateCallFramePseudoInstr does not support Thumb1!");

  int PIdx = I->findFirstPredOperandIdx();
  ARMCC::CondCodes Pred = (PIdx == -1)
                              ? ARMCC::AL
                              : (ARMCC::CondCodes)I->getOperand(PIdx).getImm();
  unsigned PredReg = TII.getFramePred(*I);

  if (!hasReservedCallFrame(MF)) {
    // Bail early if the callee is expected to do the adjustment.
    if (IsDestroy && CalleePopAmount != -1U)
      return MBB.erase(I);

    // If we have alloca, convert as follows:
    // ADJCALLSTACKDOWN -> sub, sp, sp, amount
    // ADJCALLSTACKUP   -> add, sp, sp, amount
    unsigned Amount = TII.getFrameSize(*I);
    if (Amount != 0) {
      // We need to keep the stack aligned properly.  To do this, we round the
      // amount of space needed for the outgoing arguments up to the next
      // alignment boundary.
      Amount = alignSPAdjust(Amount);

      if (Opc == ARM::ADJCALLSTACKDOWN || Opc == ARM::tADJCALLSTACKDOWN) {
        emitSPUpdate(isARM, MBB, I, dl, TII, -Amount, MachineInstr::NoFlags,
                     Pred, PredReg);
      } else {
        assert(Opc == ARM::ADJCALLSTACKUP || Opc == ARM::tADJCALLSTACKUP);
        emitSPUpdate(isARM, MBB, I, dl, TII, Amount, MachineInstr::NoFlags,
                     Pred, PredReg);
      }
    }
  } else if (CalleePopAmount != -1U) {
    // If the calling convention demands that the callee pops arguments from the
    // stack, we want to add it back if we have a reserved call frame.
    emitSPUpdate(isARM, MBB, I, dl, TII, -CalleePopAmount,
                 MachineInstr::NoFlags, Pred, PredReg);
  }
  return MBB.erase(I);
}

/// Get the minimum constant for ARM that is greater than or equal to the
/// argument. In ARM, constants can have any value that can be produced by
/// rotating an 8-bit value to the right by an even number of bits within a
/// 32-bit word.
static uint32_t alignToARMConstant(uint32_t Value) {
  unsigned Shifted = 0;

  if (Value == 0)
      return 0;

  while (!(Value & 0xC0000000)) {
      Value = Value << 2;
      Shifted += 2;
  }

  bool Carry = (Value & 0x00FFFFFF);
  Value = ((Value & 0xFF000000) >> 24) + Carry;

  if (Value & 0x0000100)
      Value = Value & 0x000001FC;

  if (Shifted > 24)
      Value = Value >> (Shifted - 24);
  else
      Value = Value << (24 - Shifted);

  return Value;
}

// The stack limit in the TCB is set to this many bytes above the actual
// stack limit.
static const uint64_t kSplitStackAvailable = 256;

// Adjust the function prologue to enable split stacks. This currently only
// supports android and linux.
//
// The ABI of the segmented stack prologue is a little arbitrarily chosen, but
// must be well defined in order to allow for consistent implementations of the
// __morestack helper function. The ABI is also not a normal ABI in that it
// doesn't follow the normal calling conventions because this allows the
// prologue of each function to be optimized further.
//
// Currently, the ABI looks like (when calling __morestack)
//
//  * r4 holds the minimum stack size requested for this function call
//  * r5 holds the stack size of the arguments to the function
//  * the beginning of the function is 3 instructions after the call to
//    __morestack
//
// Implementations of __morestack should use r4 to allocate a new stack, r5 to
// place the arguments on to the new stack, and the 3-instruction knowledge to
// jump directly to the body of the function when working on the new stack.
//
// An old (and possibly no longer compatible) implementation of __morestack for
// ARM can be found at [1].
//
// [1] - https://github.com/mozilla/rust/blob/86efd9/src/rt/arch/arm/morestack.S
void ARMFrameLowering::adjustForSegmentedStacks(
    MachineFunction &MF, MachineBasicBlock &PrologueMBB) const {
  unsigned Opcode;
  unsigned CFIIndex;
  const ARMSubtarget *ST = &MF.getSubtarget<ARMSubtarget>();
  bool Thumb = ST->isThumb();
  bool Thumb2 = ST->isThumb2();

  // Sadly, this currently doesn't support varargs, platforms other than
  // android/linux. Note that thumb1/thumb2 are support for android/linux.
  if (MF.getFunction().isVarArg())
    report_fatal_error("Segmented stacks do not support vararg functions.");
  if (!ST->isTargetAndroid() && !ST->isTargetLinux())
    report_fatal_error("Segmented stacks not supported on this platform.");

  MachineFrameInfo &MFI = MF.getFrameInfo();
  MCContext &Context = MF.getContext();
  const MCRegisterInfo *MRI = Context.getRegisterInfo();
  const ARMBaseInstrInfo &TII =
      *static_cast<const ARMBaseInstrInfo *>(MF.getSubtarget().getInstrInfo());
  ARMFunctionInfo *ARMFI = MF.getInfo<ARMFunctionInfo>();
  DebugLoc DL;

  if (!MFI.needsSplitStackProlog())
    return;

  uint64_t StackSize = MFI.getStackSize();

  // Use R4 and R5 as scratch registers.
  // We save R4 and R5 before use and restore them before leaving the function.
  unsigned ScratchReg0 = ARM::R4;
  unsigned ScratchReg1 = ARM::R5;
  unsigned MovOp = ST->useMovt() ? ARM::t2MOVi32imm : ARM::tMOVi32imm;
  uint64_t AlignedStackSize;

  MachineBasicBlock *PrevStackMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *PostStackMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *AllocMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *GetMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *McrMBB = MF.CreateMachineBasicBlock();

  // Grab everything that reaches PrologueMBB to update there liveness as well.
  SmallPtrSet<MachineBasicBlock *, 8> BeforePrologueRegion;
  SmallVector<MachineBasicBlock *, 2> WalkList;
  WalkList.push_back(&PrologueMBB);

  do {
    MachineBasicBlock *CurMBB = WalkList.pop_back_val();
    for (MachineBasicBlock *PredBB : CurMBB->predecessors()) {
      if (BeforePrologueRegion.insert(PredBB).second)
        WalkList.push_back(PredBB);
    }
  } while (!WalkList.empty());

  // The order in that list is important.
  // The blocks will all be inserted before PrologueMBB using that order.
  // Therefore the block that should appear first in the CFG should appear
  // first in the list.
  MachineBasicBlock *AddedBlocks[] = {PrevStackMBB, McrMBB, GetMBB, AllocMBB,
                                      PostStackMBB};

  for (MachineBasicBlock *B : AddedBlocks)
    BeforePrologueRegion.insert(B);

  for (const auto &LI : PrologueMBB.liveins()) {
    for (MachineBasicBlock *PredBB : BeforePrologueRegion)
      PredBB->addLiveIn(LI);
  }

  // Remove the newly added blocks from the list, since we know
  // we do not have to do the following updates for them.
  for (MachineBasicBlock *B : AddedBlocks) {
    BeforePrologueRegion.erase(B);
    MF.insert(PrologueMBB.getIterator(), B);
  }

  for (MachineBasicBlock *MBB : BeforePrologueRegion) {
    // Make sure the LiveIns are still sorted and unique.
    MBB->sortUniqueLiveIns();
    // Replace the edges to PrologueMBB by edges to the sequences
    // we are about to add, but only update for immediate predecessors.
    if (MBB->isSuccessor(&PrologueMBB))
      MBB->ReplaceUsesOfBlockWith(&PrologueMBB, AddedBlocks[0]);
  }

  // The required stack size that is aligned to ARM constant criterion.
  AlignedStackSize = alignToARMConstant(StackSize);

  // When the frame size is less than 256 we just compare the stack
  // boundary directly to the value of the stack pointer, per gcc.
  bool CompareStackPointer = AlignedStackSize < kSplitStackAvailable;

  // We will use two of the callee save registers as scratch registers so we
  // need to save those registers onto the stack.
  // We will use SR0 to hold stack limit and SR1 to hold the stack size
  // requested and arguments for __morestack().
  // SR0: Scratch Register #0
  // SR1: Scratch Register #1
  // push {SR0, SR1}
  if (Thumb) {
    BuildMI(PrevStackMBB, DL, TII.get(ARM::tPUSH))
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  } else {
    BuildMI(PrevStackMBB, DL, TII.get(ARM::STMDB_UPD))
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  }

  // Emit the relevant DWARF information about the change in stack pointer as
  // well as where to find both r4 and r5 (the callee-save registers)
  if (!MF.getTarget().getMCAsmInfo()->usesWindowsCFI()) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 8));
    BuildMI(PrevStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(ScratchReg1, true), -4));
    BuildMI(PrevStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(ScratchReg0, true), -8));
    BuildMI(PrevStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // mov SR1, sp
  if (Thumb) {
    BuildMI(McrMBB, DL, TII.get(ARM::tMOVr), ScratchReg1)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL));
  } else if (CompareStackPointer) {
    BuildMI(McrMBB, DL, TII.get(ARM::MOVr), ScratchReg1)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
  }

  // sub SR1, sp, #StackSize
  if (!CompareStackPointer && Thumb) {
    if (AlignedStackSize < 256) {
      BuildMI(McrMBB, DL, TII.get(ARM::tSUBi8), ScratchReg1)
          .add(condCodeOp())
          .addReg(ScratchReg1)
          .addImm(AlignedStackSize)
          .add(predOps(ARMCC::AL));
    } else {
      if (Thumb2 || ST->genExecuteOnly()) {
        BuildMI(McrMBB, DL, TII.get(MovOp), ScratchReg0)
            .addImm(AlignedStackSize);
      } else {
        auto MBBI = McrMBB->end();
        auto RegInfo = STI.getRegisterInfo();
        RegInfo->emitLoadConstPool(*McrMBB, MBBI, DL, ScratchReg0, 0,
                                   AlignedStackSize);
      }
      BuildMI(McrMBB, DL, TII.get(ARM::tSUBrr), ScratchReg1)
          .add(condCodeOp())
          .addReg(ScratchReg1)
          .addReg(ScratchReg0)
          .add(predOps(ARMCC::AL));
    }
  } else if (!CompareStackPointer) {
    if (AlignedStackSize < 256) {
      BuildMI(McrMBB, DL, TII.get(ARM::SUBri), ScratchReg1)
          .addReg(ARM::SP)
          .addImm(AlignedStackSize)
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    } else {
      auto MBBI = McrMBB->end();
      auto RegInfo = STI.getRegisterInfo();
      RegInfo->emitLoadConstPool(*McrMBB, MBBI, DL, ScratchReg0, 0,
                                 AlignedStackSize);
      BuildMI(McrMBB, DL, TII.get(ARM::SUBrr), ScratchReg1)
          .addReg(ARM::SP)
          .addReg(ScratchReg0)
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    }
  }

  if (Thumb && ST->isThumb1Only()) {
    if (ST->genExecuteOnly()) {
      BuildMI(GetMBB, DL, TII.get(MovOp), ScratchReg0)
          .addExternalSymbol("__STACK_LIMIT");
    } else {
      unsigned PCLabelId = ARMFI->createPICLabelUId();
      ARMConstantPoolValue *NewCPV = ARMConstantPoolSymbol::Create(
          MF.getFunction().getContext(), "__STACK_LIMIT", PCLabelId, 0);
      MachineConstantPool *MCP = MF.getConstantPool();
      unsigned CPI = MCP->getConstantPoolIndex(NewCPV, Align(4));

      // ldr SR0, [pc, offset(STACK_LIMIT)]
      BuildMI(GetMBB, DL, TII.get(ARM::tLDRpci), ScratchReg0)
          .addConstantPoolIndex(CPI)
          .add(predOps(ARMCC::AL));
    }

    // ldr SR0, [SR0]
    BuildMI(GetMBB, DL, TII.get(ARM::tLDRi), ScratchReg0)
        .addReg(ScratchReg0)
        .addImm(0)
        .add(predOps(ARMCC::AL));
  } else {
    // Get TLS base address from the coprocessor
    // mrc p15, #0, SR0, c13, c0, #3
    BuildMI(McrMBB, DL, TII.get(Thumb ? ARM::t2MRC : ARM::MRC),
            ScratchReg0)
        .addImm(15)
        .addImm(0)
        .addImm(13)
        .addImm(0)
        .addImm(3)
        .add(predOps(ARMCC::AL));

    // Use the last tls slot on android and a private field of the TCP on linux.
    assert(ST->isTargetAndroid() || ST->isTargetLinux());
    unsigned TlsOffset = ST->isTargetAndroid() ? 63 : 1;

    // Get the stack limit from the right offset
    // ldr SR0, [sr0, #4 * TlsOffset]
    BuildMI(GetMBB, DL, TII.get(Thumb ? ARM::t2LDRi12 : ARM::LDRi12),
            ScratchReg0)
        .addReg(ScratchReg0)
        .addImm(4 * TlsOffset)
        .add(predOps(ARMCC::AL));
  }

  // Compare stack limit with stack size requested.
  // cmp SR0, SR1
  Opcode = Thumb ? ARM::tCMPr : ARM::CMPrr;
  BuildMI(GetMBB, DL, TII.get(Opcode))
      .addReg(ScratchReg0)
      .addReg(ScratchReg1)
      .add(predOps(ARMCC::AL));

  // This jump is taken if StackLimit <= SP - stack required.
  Opcode = Thumb ? ARM::tBcc : ARM::Bcc;
  BuildMI(GetMBB, DL, TII.get(Opcode))
      .addMBB(PostStackMBB)
      .addImm(ARMCC::LS)
      .addReg(ARM::CPSR);

  // Calling __morestack(StackSize, Size of stack arguments).
  // __morestack knows that the stack size requested is in SR0(r4)
  // and amount size of stack arguments is in SR1(r5).

  // Pass first argument for the __morestack by Scratch Register #0.
  //   The amount size of stack required
  if (Thumb) {
    if (AlignedStackSize < 256) {
      BuildMI(AllocMBB, DL, TII.get(ARM::tMOVi8), ScratchReg0)
          .add(condCodeOp())
          .addImm(AlignedStackSize)
          .add(predOps(ARMCC::AL));
    } else {
      if (Thumb2 || ST->genExecuteOnly()) {
        BuildMI(AllocMBB, DL, TII.get(MovOp), ScratchReg0)
            .addImm(AlignedStackSize);
      } else {
        auto MBBI = AllocMBB->end();
        auto RegInfo = STI.getRegisterInfo();
        RegInfo->emitLoadConstPool(*AllocMBB, MBBI, DL, ScratchReg0, 0,
                                   AlignedStackSize);
      }
    }
  } else {
    if (AlignedStackSize < 256) {
      BuildMI(AllocMBB, DL, TII.get(ARM::MOVi), ScratchReg0)
          .addImm(AlignedStackSize)
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    } else {
      auto MBBI = AllocMBB->end();
      auto RegInfo = STI.getRegisterInfo();
      RegInfo->emitLoadConstPool(*AllocMBB, MBBI, DL, ScratchReg0, 0,
                                 AlignedStackSize);
    }
  }

  // Pass second argument for the __morestack by Scratch Register #1.
  //   The amount size of stack consumed to save function arguments.
  if (Thumb) {
    if (ARMFI->getArgumentStackSize() < 256) {
      BuildMI(AllocMBB, DL, TII.get(ARM::tMOVi8), ScratchReg1)
          .add(condCodeOp())
          .addImm(alignToARMConstant(ARMFI->getArgumentStackSize()))
          .add(predOps(ARMCC::AL));
    } else {
      if (Thumb2 || ST->genExecuteOnly()) {
        BuildMI(AllocMBB, DL, TII.get(MovOp), ScratchReg1)
            .addImm(alignToARMConstant(ARMFI->getArgumentStackSize()));
      } else {
        auto MBBI = AllocMBB->end();
        auto RegInfo = STI.getRegisterInfo();
        RegInfo->emitLoadConstPool(
            *AllocMBB, MBBI, DL, ScratchReg1, 0,
            alignToARMConstant(ARMFI->getArgumentStackSize()));
      }
    }
  } else {
    if (alignToARMConstant(ARMFI->getArgumentStackSize()) < 256) {
      BuildMI(AllocMBB, DL, TII.get(ARM::MOVi), ScratchReg1)
          .addImm(alignToARMConstant(ARMFI->getArgumentStackSize()))
          .add(predOps(ARMCC::AL))
          .add(condCodeOp());
    } else {
      auto MBBI = AllocMBB->end();
      auto RegInfo = STI.getRegisterInfo();
      RegInfo->emitLoadConstPool(
          *AllocMBB, MBBI, DL, ScratchReg1, 0,
          alignToARMConstant(ARMFI->getArgumentStackSize()));
    }
  }

  // push {lr} - Save return address of this function.
  if (Thumb) {
    BuildMI(AllocMBB, DL, TII.get(ARM::tPUSH))
        .add(predOps(ARMCC::AL))
        .addReg(ARM::LR);
  } else {
    BuildMI(AllocMBB, DL, TII.get(ARM::STMDB_UPD))
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addReg(ARM::LR);
  }

  // Emit the DWARF info about the change in stack as well as where to find the
  // previous link register
  if (!MF.getTarget().getMCAsmInfo()->usesWindowsCFI()) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 12));
    BuildMI(AllocMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, MRI->getDwarfRegNum(ARM::LR, true), -12));
    BuildMI(AllocMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // Call __morestack().
  if (Thumb) {
    BuildMI(AllocMBB, DL, TII.get(ARM::tBL))
        .add(predOps(ARMCC::AL))
        .addExternalSymbol("__morestack");
  } else {
    BuildMI(AllocMBB, DL, TII.get(ARM::BL))
        .addExternalSymbol("__morestack");
  }

  // pop {lr} - Restore return address of this original function.
  if (Thumb) {
    if (ST->isThumb1Only()) {
      BuildMI(AllocMBB, DL, TII.get(ARM::tPOP))
          .add(predOps(ARMCC::AL))
          .addReg(ScratchReg0);
      BuildMI(AllocMBB, DL, TII.get(ARM::tMOVr), ARM::LR)
          .addReg(ScratchReg0)
          .add(predOps(ARMCC::AL));
    } else {
      BuildMI(AllocMBB, DL, TII.get(ARM::t2LDR_POST))
          .addReg(ARM::LR, RegState::Define)
          .addReg(ARM::SP, RegState::Define)
          .addReg(ARM::SP)
          .addImm(4)
          .add(predOps(ARMCC::AL));
    }
  } else {
    BuildMI(AllocMBB, DL, TII.get(ARM::LDMIA_UPD))
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addReg(ARM::LR);
  }

  // Restore SR0 and SR1 in case of __morestack() was called.
  // __morestack() will skip PostStackMBB block so we need to restore
  // scratch registers from here.
  // pop {SR0, SR1}
  if (Thumb) {
    BuildMI(AllocMBB, DL, TII.get(ARM::tPOP))
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  } else {
    BuildMI(AllocMBB, DL, TII.get(ARM::LDMIA_UPD))
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  }

  // Update the CFA offset now that we've popped
  if (!MF.getTarget().getMCAsmInfo()->usesWindowsCFI()) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 0));
    BuildMI(AllocMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // Return from this function.
  BuildMI(AllocMBB, DL, TII.get(ST->getReturnOpcode())).add(predOps(ARMCC::AL));

  // Restore SR0 and SR1 in case of __morestack() was not called.
  // pop {SR0, SR1}
  if (Thumb) {
    BuildMI(PostStackMBB, DL, TII.get(ARM::tPOP))
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  } else {
    BuildMI(PostStackMBB, DL, TII.get(ARM::LDMIA_UPD))
        .addReg(ARM::SP, RegState::Define)
        .addReg(ARM::SP)
        .add(predOps(ARMCC::AL))
        .addReg(ScratchReg0)
        .addReg(ScratchReg1);
  }

  // Update the CFA offset now that we've popped
  if (!MF.getTarget().getMCAsmInfo()->usesWindowsCFI()) {
    CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 0));
    BuildMI(PostStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);

    // Tell debuggers that r4 and r5 are now the same as they were in the
    // previous function, that they're the "Same Value".
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createSameValue(
        nullptr, MRI->getDwarfRegNum(ScratchReg0, true)));
    BuildMI(PostStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createSameValue(
        nullptr, MRI->getDwarfRegNum(ScratchReg1, true)));
    BuildMI(PostStackMBB, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex);
  }

  // Organizing MBB lists
  PostStackMBB->addSuccessor(&PrologueMBB);

  AllocMBB->addSuccessor(PostStackMBB);

  GetMBB->addSuccessor(PostStackMBB);
  GetMBB->addSuccessor(AllocMBB);

  McrMBB->addSuccessor(GetMBB);

  PrevStackMBB->addSuccessor(McrMBB);

#ifdef EXPENSIVE_CHECKS
  MF.verify();
#endif
}
