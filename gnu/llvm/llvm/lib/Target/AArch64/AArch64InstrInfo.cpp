//===- AArch64InstrInfo.cpp - AArch64 Instruction Information -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AArch64 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "AArch64InstrInfo.h"
#include "AArch64ExpandImm.h"
#include "AArch64FrameLowering.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64PointerAuth.h"
#include "AArch64Subtarget.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "AArch64GenInstrInfo.inc"

static cl::opt<unsigned> TBZDisplacementBits(
    "aarch64-tbz-offset-bits", cl::Hidden, cl::init(14),
    cl::desc("Restrict range of TB[N]Z instructions (DEBUG)"));

static cl::opt<unsigned> CBZDisplacementBits(
    "aarch64-cbz-offset-bits", cl::Hidden, cl::init(19),
    cl::desc("Restrict range of CB[N]Z instructions (DEBUG)"));

static cl::opt<unsigned>
    BCCDisplacementBits("aarch64-bcc-offset-bits", cl::Hidden, cl::init(19),
                        cl::desc("Restrict range of Bcc instructions (DEBUG)"));

static cl::opt<unsigned>
    BDisplacementBits("aarch64-b-offset-bits", cl::Hidden, cl::init(26),
                      cl::desc("Restrict range of B instructions (DEBUG)"));

AArch64InstrInfo::AArch64InstrInfo(const AArch64Subtarget &STI)
    : AArch64GenInstrInfo(AArch64::ADJCALLSTACKDOWN, AArch64::ADJCALLSTACKUP,
                          AArch64::CATCHRET),
      RI(STI.getTargetTriple()), Subtarget(STI) {}

/// GetInstSize - Return the number of bytes of code the specified
/// instruction may be.  This returns the maximum number of bytes.
unsigned AArch64InstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  const MachineBasicBlock &MBB = *MI.getParent();
  const MachineFunction *MF = MBB.getParent();
  const Function &F = MF->getFunction();
  const MCAsmInfo *MAI = MF->getTarget().getMCAsmInfo();

  {
    auto Op = MI.getOpcode();
    if (Op == AArch64::INLINEASM || Op == AArch64::INLINEASM_BR)
      return getInlineAsmLength(MI.getOperand(0).getSymbolName(), *MAI);
  }

  // Meta-instructions emit no code.
  if (MI.isMetaInstruction())
    return 0;

  // FIXME: We currently only handle pseudoinstructions that don't get expanded
  //        before the assembly printer.
  unsigned NumBytes = 0;
  const MCInstrDesc &Desc = MI.getDesc();

  // Size should be preferably set in
  // llvm/lib/Target/AArch64/AArch64InstrInfo.td (default case).
  // Specific cases handle instructions of variable sizes
  switch (Desc.getOpcode()) {
  default:
    if (Desc.getSize())
      return Desc.getSize();

    // Anything not explicitly designated otherwise (i.e. pseudo-instructions
    // with fixed constant size but not specified in .td file) is a normal
    // 4-byte insn.
    NumBytes = 4;

    if (Desc.getSize() > 0)
      NumBytes = Desc.getSize();
    break;
  case TargetOpcode::STACKMAP:
    // The upper bound for a stackmap intrinsic is the full length of its shadow
    NumBytes = StackMapOpers(&MI).getNumPatchBytes();
    assert(NumBytes % 4 == 0 && "Invalid number of NOP bytes requested!");
    break;
  case TargetOpcode::PATCHPOINT:
    // The size of the patchpoint intrinsic is the number of bytes requested
    NumBytes = PatchPointOpers(&MI).getNumPatchBytes();
    assert(NumBytes % 4 == 0 && "Invalid number of NOP bytes requested!");
    break;
  case TargetOpcode::STATEPOINT:
    NumBytes = StatepointOpers(&MI).getNumPatchBytes();
    assert(NumBytes % 4 == 0 && "Invalid number of NOP bytes requested!");
    // No patch bytes means a normal call inst is emitted
    if (NumBytes == 0)
      NumBytes = 4;
    break;
  case TargetOpcode::PATCHABLE_FUNCTION_ENTER:
    // If `patchable-function-entry` is set, PATCHABLE_FUNCTION_ENTER
    // instructions are expanded to the specified number of NOPs. Otherwise,
    // they are expanded to 36-byte XRay sleds.
    NumBytes =
        F.getFnAttributeAsParsedInteger("patchable-function-entry", 9) * 4;
    break;
  case TargetOpcode::PATCHABLE_FUNCTION_EXIT:
  case TargetOpcode::PATCHABLE_TYPED_EVENT_CALL:
    // An XRay sled can be 4 bytes of alignment plus a 32-byte block.
    NumBytes = 36;
    break;
  case TargetOpcode::PATCHABLE_EVENT_CALL:
    // EVENT_CALL XRay sleds are exactly 6 instructions long (no alignment).
    NumBytes = 24;
    break;

  case AArch64::SPACE:
    NumBytes = MI.getOperand(1).getImm();
    break;
  case TargetOpcode::BUNDLE:
    NumBytes = getInstBundleLength(MI);
    break;
  }

  return NumBytes;
}

unsigned AArch64InstrInfo::getInstBundleLength(const MachineInstr &MI) const {
  unsigned Size = 0;
  MachineBasicBlock::const_instr_iterator I = MI.getIterator();
  MachineBasicBlock::const_instr_iterator E = MI.getParent()->instr_end();
  while (++I != E && I->isInsideBundle()) {
    assert(!I->isBundle() && "No nested bundle!");
    Size += getInstSizeInBytes(*I);
  }
  return Size;
}

static void parseCondBranch(MachineInstr *LastInst, MachineBasicBlock *&Target,
                            SmallVectorImpl<MachineOperand> &Cond) {
  // Block ends with fall-through condbranch.
  switch (LastInst->getOpcode()) {
  default:
    llvm_unreachable("Unknown branch instruction?");
  case AArch64::Bcc:
    Target = LastInst->getOperand(1).getMBB();
    Cond.push_back(LastInst->getOperand(0));
    break;
  case AArch64::CBZW:
  case AArch64::CBZX:
  case AArch64::CBNZW:
  case AArch64::CBNZX:
    Target = LastInst->getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(-1));
    Cond.push_back(MachineOperand::CreateImm(LastInst->getOpcode()));
    Cond.push_back(LastInst->getOperand(0));
    break;
  case AArch64::TBZW:
  case AArch64::TBZX:
  case AArch64::TBNZW:
  case AArch64::TBNZX:
    Target = LastInst->getOperand(2).getMBB();
    Cond.push_back(MachineOperand::CreateImm(-1));
    Cond.push_back(MachineOperand::CreateImm(LastInst->getOpcode()));
    Cond.push_back(LastInst->getOperand(0));
    Cond.push_back(LastInst->getOperand(1));
  }
}

static unsigned getBranchDisplacementBits(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("unexpected opcode!");
  case AArch64::B:
    return BDisplacementBits;
  case AArch64::TBNZW:
  case AArch64::TBZW:
  case AArch64::TBNZX:
  case AArch64::TBZX:
    return TBZDisplacementBits;
  case AArch64::CBNZW:
  case AArch64::CBZW:
  case AArch64::CBNZX:
  case AArch64::CBZX:
    return CBZDisplacementBits;
  case AArch64::Bcc:
    return BCCDisplacementBits;
  }
}

bool AArch64InstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                             int64_t BrOffset) const {
  unsigned Bits = getBranchDisplacementBits(BranchOp);
  assert(Bits >= 3 && "max branch displacement must be enough to jump"
                      "over conditional branch expansion");
  return isIntN(Bits, BrOffset / 4);
}

MachineBasicBlock *
AArch64InstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unexpected opcode!");
  case AArch64::B:
    return MI.getOperand(0).getMBB();
  case AArch64::TBZW:
  case AArch64::TBNZW:
  case AArch64::TBZX:
  case AArch64::TBNZX:
    return MI.getOperand(2).getMBB();
  case AArch64::CBZW:
  case AArch64::CBNZW:
  case AArch64::CBZX:
  case AArch64::CBNZX:
  case AArch64::Bcc:
    return MI.getOperand(1).getMBB();
  }
}

void AArch64InstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                            MachineBasicBlock &NewDestBB,
                                            MachineBasicBlock &RestoreBB,
                                            const DebugLoc &DL,
                                            int64_t BrOffset,
                                            RegScavenger *RS) const {
  assert(RS && "RegScavenger required for long branching");
  assert(MBB.empty() &&
         "new block should be inserted for expanding unconditional branch");
  assert(MBB.pred_size() == 1);
  assert(RestoreBB.empty() &&
         "restore block should be inserted for restoring clobbered registers");

  auto buildIndirectBranch = [&](Register Reg, MachineBasicBlock &DestBB) {
    // Offsets outside of the signed 33-bit range are not supported for ADRP +
    // ADD.
    if (!isInt<33>(BrOffset))
      report_fatal_error(
          "Branch offsets outside of the signed 33-bit range not supported");

    BuildMI(MBB, MBB.end(), DL, get(AArch64::ADRP), Reg)
        .addSym(DestBB.getSymbol(), AArch64II::MO_PAGE);
    BuildMI(MBB, MBB.end(), DL, get(AArch64::ADDXri), Reg)
        .addReg(Reg)
        .addSym(DestBB.getSymbol(), AArch64II::MO_PAGEOFF | AArch64II::MO_NC)
        .addImm(0);
    BuildMI(MBB, MBB.end(), DL, get(AArch64::BR)).addReg(Reg);
  };

  RS->enterBasicBlockEnd(MBB);
  // If X16 is unused, we can rely on the linker to insert a range extension
  // thunk if NewDestBB is out of range of a single B instruction.
  constexpr Register Reg = AArch64::X16;
  if (!RS->isRegUsed(Reg)) {
    insertUnconditionalBranch(MBB, &NewDestBB, DL);
    RS->setRegUsed(Reg);
    return;
  }

  // If there's a free register and it's worth inflating the code size,
  // manually insert the indirect branch.
  Register Scavenged = RS->FindUnusedReg(&AArch64::GPR64RegClass);
  if (Scavenged != AArch64::NoRegister &&
      MBB.getSectionID() == MBBSectionID::ColdSectionID) {
    buildIndirectBranch(Scavenged, NewDestBB);
    RS->setRegUsed(Scavenged);
    return;
  }

  // Note: Spilling X16 briefly moves the stack pointer, making it incompatible
  // with red zones.
  AArch64FunctionInfo *AFI = MBB.getParent()->getInfo<AArch64FunctionInfo>();
  if (!AFI || AFI->hasRedZone().value_or(true))
    report_fatal_error(
        "Unable to insert indirect branch inside function that has red zone");

  // Otherwise, spill X16 and defer range extension to the linker.
  BuildMI(MBB, MBB.end(), DL, get(AArch64::STRXpre))
      .addReg(AArch64::SP, RegState::Define)
      .addReg(Reg)
      .addReg(AArch64::SP)
      .addImm(-16);

  BuildMI(MBB, MBB.end(), DL, get(AArch64::B)).addMBB(&RestoreBB);

  BuildMI(RestoreBB, RestoreBB.end(), DL, get(AArch64::LDRXpost))
      .addReg(AArch64::SP, RegState::Define)
      .addReg(Reg, RegState::Define)
      .addReg(AArch64::SP)
      .addImm(16);
}

// Branch analysis.
bool AArch64InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                     MachineBasicBlock *&TBB,
                                     MachineBasicBlock *&FBB,
                                     SmallVectorImpl<MachineOperand> &Cond,
                                     bool AllowModify) const {
  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  // Skip over SpeculationBarrierEndBB terminators
  if (I->getOpcode() == AArch64::SpeculationBarrierISBDSBEndBB ||
      I->getOpcode() == AArch64::SpeculationBarrierSBEndBB) {
    --I;
  }

  if (!isUnpredicatedTerminator(*I))
    return false;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;

  // If there is only one terminator instruction, process it.
  unsigned LastOpc = LastInst->getOpcode();
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (isUncondBranchOpcode(LastOpc)) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }
    if (isCondBranchOpcode(LastOpc)) {
      // Block ends with fall-through condbranch.
      parseCondBranch(LastInst, TBB, Cond);
      return false;
    }
    return true; // Can't handle indirect branch.
  }

  // Get the instruction before it if it is a terminator.
  MachineInstr *SecondLastInst = &*I;
  unsigned SecondLastOpc = SecondLastInst->getOpcode();

  // If AllowModify is true and the block ends with two or more unconditional
  // branches, delete all but the first unconditional branch.
  if (AllowModify && isUncondBranchOpcode(LastOpc)) {
    while (isUncondBranchOpcode(SecondLastOpc)) {
      LastInst->eraseFromParent();
      LastInst = SecondLastInst;
      LastOpc = LastInst->getOpcode();
      if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
        // Return now the only terminator is an unconditional branch.
        TBB = LastInst->getOperand(0).getMBB();
        return false;
      }
      SecondLastInst = &*I;
      SecondLastOpc = SecondLastInst->getOpcode();
    }
  }

  // If we're allowed to modify and the block ends in a unconditional branch
  // which could simply fallthrough, remove the branch.  (Note: This case only
  // matters when we can't understand the whole sequence, otherwise it's also
  // handled by BranchFolding.cpp.)
  if (AllowModify && isUncondBranchOpcode(LastOpc) &&
      MBB.isLayoutSuccessor(getBranchDestBlock(*LastInst))) {
    LastInst->eraseFromParent();
    LastInst = SecondLastInst;
    LastOpc = LastInst->getOpcode();
    if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
      assert(!isUncondBranchOpcode(LastOpc) &&
             "unreachable unconditional branches removed above");

      if (isCondBranchOpcode(LastOpc)) {
        // Block ends with fall-through condbranch.
        parseCondBranch(LastInst, TBB, Cond);
        return false;
      }
      return true; // Can't handle indirect branch.
    }
    SecondLastInst = &*I;
    SecondLastOpc = SecondLastInst->getOpcode();
  }

  // If there are three terminators, we don't know what sort of block this is.
  if (SecondLastInst && I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  // If the block ends with a B and a Bcc, handle it.
  if (isCondBranchOpcode(SecondLastOpc) && isUncondBranchOpcode(LastOpc)) {
    parseCondBranch(SecondLastInst, TBB, Cond);
    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two unconditional branches, handle it.  The second
  // one is not executed, so remove it.
  if (isUncondBranchOpcode(SecondLastOpc) && isUncondBranchOpcode(LastOpc)) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // ...likewise if it ends with an indirect branch followed by an unconditional
  // branch.
  if (isIndirectBranchOpcode(SecondLastOpc) && isUncondBranchOpcode(LastOpc)) {
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return true;
  }

  // Otherwise, can't handle this.
  return true;
}

bool AArch64InstrInfo::analyzeBranchPredicate(MachineBasicBlock &MBB,
                                              MachineBranchPredicate &MBP,
                                              bool AllowModify) const {
  // For the moment, handle only a block which ends with a cb(n)zx followed by
  // a fallthrough.  Why this?  Because it is a common form.
  // TODO: Should we handle b.cc?

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return true;

  // Skip over SpeculationBarrierEndBB terminators
  if (I->getOpcode() == AArch64::SpeculationBarrierISBDSBEndBB ||
      I->getOpcode() == AArch64::SpeculationBarrierSBEndBB) {
    --I;
  }

  if (!isUnpredicatedTerminator(*I))
    return true;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;
  unsigned LastOpc = LastInst->getOpcode();
  if (!isCondBranchOpcode(LastOpc))
    return true;

  switch (LastOpc) {
  default:
    return true;
  case AArch64::CBZW:
  case AArch64::CBZX:
  case AArch64::CBNZW:
  case AArch64::CBNZX:
    break;
  };

  MBP.TrueDest = LastInst->getOperand(1).getMBB();
  assert(MBP.TrueDest && "expected!");
  MBP.FalseDest = MBB.getNextNode();

  MBP.ConditionDef = nullptr;
  MBP.SingleUseCondition = false;

  MBP.LHS = LastInst->getOperand(0);
  MBP.RHS = MachineOperand::CreateImm(0);
  MBP.Predicate = LastOpc == AArch64::CBNZX ? MachineBranchPredicate::PRED_NE
                                            : MachineBranchPredicate::PRED_EQ;
  return false;
}

bool AArch64InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  if (Cond[0].getImm() != -1) {
    // Regular Bcc
    AArch64CC::CondCode CC = (AArch64CC::CondCode)(int)Cond[0].getImm();
    Cond[0].setImm(AArch64CC::getInvertedCondCode(CC));
  } else {
    // Folded compare-and-branch
    switch (Cond[1].getImm()) {
    default:
      llvm_unreachable("Unknown conditional branch!");
    case AArch64::CBZW:
      Cond[1].setImm(AArch64::CBNZW);
      break;
    case AArch64::CBNZW:
      Cond[1].setImm(AArch64::CBZW);
      break;
    case AArch64::CBZX:
      Cond[1].setImm(AArch64::CBNZX);
      break;
    case AArch64::CBNZX:
      Cond[1].setImm(AArch64::CBZX);
      break;
    case AArch64::TBZW:
      Cond[1].setImm(AArch64::TBNZW);
      break;
    case AArch64::TBNZW:
      Cond[1].setImm(AArch64::TBZW);
      break;
    case AArch64::TBZX:
      Cond[1].setImm(AArch64::TBNZX);
      break;
    case AArch64::TBNZX:
      Cond[1].setImm(AArch64::TBZX);
      break;
    }
  }

  return false;
}

unsigned AArch64InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                        int *BytesRemoved) const {
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (!isUncondBranchOpcode(I->getOpcode()) &&
      !isCondBranchOpcode(I->getOpcode()))
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) {
    if (BytesRemoved)
      *BytesRemoved = 4;
    return 1;
  }
  --I;
  if (!isCondBranchOpcode(I->getOpcode())) {
    if (BytesRemoved)
      *BytesRemoved = 4;
    return 1;
  }

  // Remove the branch.
  I->eraseFromParent();
  if (BytesRemoved)
    *BytesRemoved = 8;

  return 2;
}

void AArch64InstrInfo::instantiateCondBranch(
    MachineBasicBlock &MBB, const DebugLoc &DL, MachineBasicBlock *TBB,
    ArrayRef<MachineOperand> Cond) const {
  if (Cond[0].getImm() != -1) {
    // Regular Bcc
    BuildMI(&MBB, DL, get(AArch64::Bcc)).addImm(Cond[0].getImm()).addMBB(TBB);
  } else {
    // Folded compare-and-branch
    // Note that we use addOperand instead of addReg to keep the flags.
    const MachineInstrBuilder MIB =
        BuildMI(&MBB, DL, get(Cond[1].getImm())).add(Cond[2]);
    if (Cond.size() > 3)
      MIB.addImm(Cond[3].getImm());
    MIB.addMBB(TBB);
  }
}

unsigned AArch64InstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");

  if (!FBB) {
    if (Cond.empty()) // Unconditional branch?
      BuildMI(&MBB, DL, get(AArch64::B)).addMBB(TBB);
    else
      instantiateCondBranch(MBB, DL, TBB, Cond);

    if (BytesAdded)
      *BytesAdded = 4;

    return 1;
  }

  // Two-way conditional branch.
  instantiateCondBranch(MBB, DL, TBB, Cond);
  BuildMI(&MBB, DL, get(AArch64::B)).addMBB(FBB);

  if (BytesAdded)
    *BytesAdded = 8;

  return 2;
}

// Find the original register that VReg is copied from.
static unsigned removeCopies(const MachineRegisterInfo &MRI, unsigned VReg) {
  while (Register::isVirtualRegister(VReg)) {
    const MachineInstr *DefMI = MRI.getVRegDef(VReg);
    if (!DefMI->isFullCopy())
      return VReg;
    VReg = DefMI->getOperand(1).getReg();
  }
  return VReg;
}

// Determine if VReg is defined by an instruction that can be folded into a
// csel instruction. If so, return the folded opcode, and the replacement
// register.
static unsigned canFoldIntoCSel(const MachineRegisterInfo &MRI, unsigned VReg,
                                unsigned *NewVReg = nullptr) {
  VReg = removeCopies(MRI, VReg);
  if (!Register::isVirtualRegister(VReg))
    return 0;

  bool Is64Bit = AArch64::GPR64allRegClass.hasSubClassEq(MRI.getRegClass(VReg));
  const MachineInstr *DefMI = MRI.getVRegDef(VReg);
  unsigned Opc = 0;
  unsigned SrcOpNum = 0;
  switch (DefMI->getOpcode()) {
  case AArch64::ADDSXri:
  case AArch64::ADDSWri:
    // if NZCV is used, do not fold.
    if (DefMI->findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr,
                                         true) == -1)
      return 0;
    // fall-through to ADDXri and ADDWri.
    [[fallthrough]];
  case AArch64::ADDXri:
  case AArch64::ADDWri:
    // add x, 1 -> csinc.
    if (!DefMI->getOperand(2).isImm() || DefMI->getOperand(2).getImm() != 1 ||
        DefMI->getOperand(3).getImm() != 0)
      return 0;
    SrcOpNum = 1;
    Opc = Is64Bit ? AArch64::CSINCXr : AArch64::CSINCWr;
    break;

  case AArch64::ORNXrr:
  case AArch64::ORNWrr: {
    // not x -> csinv, represented as orn dst, xzr, src.
    unsigned ZReg = removeCopies(MRI, DefMI->getOperand(1).getReg());
    if (ZReg != AArch64::XZR && ZReg != AArch64::WZR)
      return 0;
    SrcOpNum = 2;
    Opc = Is64Bit ? AArch64::CSINVXr : AArch64::CSINVWr;
    break;
  }

  case AArch64::SUBSXrr:
  case AArch64::SUBSWrr:
    // if NZCV is used, do not fold.
    if (DefMI->findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr,
                                         true) == -1)
      return 0;
    // fall-through to SUBXrr and SUBWrr.
    [[fallthrough]];
  case AArch64::SUBXrr:
  case AArch64::SUBWrr: {
    // neg x -> csneg, represented as sub dst, xzr, src.
    unsigned ZReg = removeCopies(MRI, DefMI->getOperand(1).getReg());
    if (ZReg != AArch64::XZR && ZReg != AArch64::WZR)
      return 0;
    SrcOpNum = 2;
    Opc = Is64Bit ? AArch64::CSNEGXr : AArch64::CSNEGWr;
    break;
  }
  default:
    return 0;
  }
  assert(Opc && SrcOpNum && "Missing parameters");

  if (NewVReg)
    *NewVReg = DefMI->getOperand(SrcOpNum).getReg();
  return Opc;
}

bool AArch64InstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                       ArrayRef<MachineOperand> Cond,
                                       Register DstReg, Register TrueReg,
                                       Register FalseReg, int &CondCycles,
                                       int &TrueCycles,
                                       int &FalseCycles) const {
  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
      RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // Also need to check the dest regclass, in case we're trying to optimize
  // something like:
  // %1(gpr) = PHI %2(fpr), bb1, %(fpr), bb2
  if (!RI.getCommonSubClass(RC, MRI.getRegClass(DstReg)))
    return false;

  // Expanding cbz/tbz requires an extra cycle of latency on the condition.
  unsigned ExtraCondLat = Cond.size() != 1;

  // GPRs are handled by csel.
  // FIXME: Fold in x+1, -x, and ~x when applicable.
  if (AArch64::GPR64allRegClass.hasSubClassEq(RC) ||
      AArch64::GPR32allRegClass.hasSubClassEq(RC)) {
    // Single-cycle csel, csinc, csinv, and csneg.
    CondCycles = 1 + ExtraCondLat;
    TrueCycles = FalseCycles = 1;
    if (canFoldIntoCSel(MRI, TrueReg))
      TrueCycles = 0;
    else if (canFoldIntoCSel(MRI, FalseReg))
      FalseCycles = 0;
    return true;
  }

  // Scalar floating point is handled by fcsel.
  // FIXME: Form fabs, fmin, and fmax when applicable.
  if (AArch64::FPR64RegClass.hasSubClassEq(RC) ||
      AArch64::FPR32RegClass.hasSubClassEq(RC)) {
    CondCycles = 5 + ExtraCondLat;
    TrueCycles = FalseCycles = 2;
    return true;
  }

  // Can't do vectors.
  return false;
}

void AArch64InstrInfo::insertSelect(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL, Register DstReg,
                                    ArrayRef<MachineOperand> Cond,
                                    Register TrueReg, Register FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  // Parse the condition code, see parseCondBranch() above.
  AArch64CC::CondCode CC;
  switch (Cond.size()) {
  default:
    llvm_unreachable("Unknown condition opcode in Cond");
  case 1: // b.cc
    CC = AArch64CC::CondCode(Cond[0].getImm());
    break;
  case 3: { // cbz/cbnz
    // We must insert a compare against 0.
    bool Is64Bit;
    switch (Cond[1].getImm()) {
    default:
      llvm_unreachable("Unknown branch opcode in Cond");
    case AArch64::CBZW:
      Is64Bit = false;
      CC = AArch64CC::EQ;
      break;
    case AArch64::CBZX:
      Is64Bit = true;
      CC = AArch64CC::EQ;
      break;
    case AArch64::CBNZW:
      Is64Bit = false;
      CC = AArch64CC::NE;
      break;
    case AArch64::CBNZX:
      Is64Bit = true;
      CC = AArch64CC::NE;
      break;
    }
    Register SrcReg = Cond[2].getReg();
    if (Is64Bit) {
      // cmp reg, #0 is actually subs xzr, reg, #0.
      MRI.constrainRegClass(SrcReg, &AArch64::GPR64spRegClass);
      BuildMI(MBB, I, DL, get(AArch64::SUBSXri), AArch64::XZR)
          .addReg(SrcReg)
          .addImm(0)
          .addImm(0);
    } else {
      MRI.constrainRegClass(SrcReg, &AArch64::GPR32spRegClass);
      BuildMI(MBB, I, DL, get(AArch64::SUBSWri), AArch64::WZR)
          .addReg(SrcReg)
          .addImm(0)
          .addImm(0);
    }
    break;
  }
  case 4: { // tbz/tbnz
    // We must insert a tst instruction.
    switch (Cond[1].getImm()) {
    default:
      llvm_unreachable("Unknown branch opcode in Cond");
    case AArch64::TBZW:
    case AArch64::TBZX:
      CC = AArch64CC::EQ;
      break;
    case AArch64::TBNZW:
    case AArch64::TBNZX:
      CC = AArch64CC::NE;
      break;
    }
    // cmp reg, #foo is actually ands xzr, reg, #1<<foo.
    if (Cond[1].getImm() == AArch64::TBZW || Cond[1].getImm() == AArch64::TBNZW)
      BuildMI(MBB, I, DL, get(AArch64::ANDSWri), AArch64::WZR)
          .addReg(Cond[2].getReg())
          .addImm(
              AArch64_AM::encodeLogicalImmediate(1ull << Cond[3].getImm(), 32));
    else
      BuildMI(MBB, I, DL, get(AArch64::ANDSXri), AArch64::XZR)
          .addReg(Cond[2].getReg())
          .addImm(
              AArch64_AM::encodeLogicalImmediate(1ull << Cond[3].getImm(), 64));
    break;
  }
  }

  unsigned Opc = 0;
  const TargetRegisterClass *RC = nullptr;
  bool TryFold = false;
  if (MRI.constrainRegClass(DstReg, &AArch64::GPR64RegClass)) {
    RC = &AArch64::GPR64RegClass;
    Opc = AArch64::CSELXr;
    TryFold = true;
  } else if (MRI.constrainRegClass(DstReg, &AArch64::GPR32RegClass)) {
    RC = &AArch64::GPR32RegClass;
    Opc = AArch64::CSELWr;
    TryFold = true;
  } else if (MRI.constrainRegClass(DstReg, &AArch64::FPR64RegClass)) {
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FCSELDrrr;
  } else if (MRI.constrainRegClass(DstReg, &AArch64::FPR32RegClass)) {
    RC = &AArch64::FPR32RegClass;
    Opc = AArch64::FCSELSrrr;
  }
  assert(RC && "Unsupported regclass");

  // Try folding simple instructions into the csel.
  if (TryFold) {
    unsigned NewVReg = 0;
    unsigned FoldedOpc = canFoldIntoCSel(MRI, TrueReg, &NewVReg);
    if (FoldedOpc) {
      // The folded opcodes csinc, csinc and csneg apply the operation to
      // FalseReg, so we need to invert the condition.
      CC = AArch64CC::getInvertedCondCode(CC);
      TrueReg = FalseReg;
    } else
      FoldedOpc = canFoldIntoCSel(MRI, FalseReg, &NewVReg);

    // Fold the operation. Leave any dead instructions for DCE to clean up.
    if (FoldedOpc) {
      FalseReg = NewVReg;
      Opc = FoldedOpc;
      // The extends the live range of NewVReg.
      MRI.clearKillFlags(NewVReg);
    }
  }

  // Pull all virtual register into the appropriate class.
  MRI.constrainRegClass(TrueReg, RC);
  MRI.constrainRegClass(FalseReg, RC);

  // Insert the csel.
  BuildMI(MBB, I, DL, get(Opc), DstReg)
      .addReg(TrueReg)
      .addReg(FalseReg)
      .addImm(CC);
}

// Return true if Imm can be loaded into a register by a "cheap" sequence of
// instructions. For now, "cheap" means at most two instructions.
static bool isCheapImmediate(const MachineInstr &MI, unsigned BitSize) {
  if (BitSize == 32)
    return true;

  assert(BitSize == 64 && "Only bit sizes of 32 or 64 allowed");
  uint64_t Imm = static_cast<uint64_t>(MI.getOperand(1).getImm());
  SmallVector<AArch64_IMM::ImmInsnModel, 4> Is;
  AArch64_IMM::expandMOVImm(Imm, BitSize, Is);

  return Is.size() <= 2;
}

// FIXME: this implementation should be micro-architecture dependent, so a
// micro-architecture target hook should be introduced here in future.
bool AArch64InstrInfo::isAsCheapAsAMove(const MachineInstr &MI) const {
  if (Subtarget.hasExynosCheapAsMoveHandling()) {
    if (isExynosCheapAsMove(MI))
      return true;
    return MI.isAsCheapAsAMove();
  }

  switch (MI.getOpcode()) {
  default:
    return MI.isAsCheapAsAMove();

  case AArch64::ADDWrs:
  case AArch64::ADDXrs:
  case AArch64::SUBWrs:
  case AArch64::SUBXrs:
    return Subtarget.hasALULSLFast() && MI.getOperand(3).getImm() <= 4;

  // If MOVi32imm or MOVi64imm can be expanded into ORRWri or
  // ORRXri, it is as cheap as MOV.
  // Likewise if it can be expanded to MOVZ/MOVN/MOVK.
  case AArch64::MOVi32imm:
    return isCheapImmediate(MI, 32);
  case AArch64::MOVi64imm:
    return isCheapImmediate(MI, 64);
  }
}

bool AArch64InstrInfo::isFalkorShiftExtFast(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;

  case AArch64::ADDWrs:
  case AArch64::ADDXrs:
  case AArch64::ADDSWrs:
  case AArch64::ADDSXrs: {
    unsigned Imm = MI.getOperand(3).getImm();
    unsigned ShiftVal = AArch64_AM::getShiftValue(Imm);
    if (ShiftVal == 0)
      return true;
    return AArch64_AM::getShiftType(Imm) == AArch64_AM::LSL && ShiftVal <= 5;
  }

  case AArch64::ADDWrx:
  case AArch64::ADDXrx:
  case AArch64::ADDXrx64:
  case AArch64::ADDSWrx:
  case AArch64::ADDSXrx:
  case AArch64::ADDSXrx64: {
    unsigned Imm = MI.getOperand(3).getImm();
    switch (AArch64_AM::getArithExtendType(Imm)) {
    default:
      return false;
    case AArch64_AM::UXTB:
    case AArch64_AM::UXTH:
    case AArch64_AM::UXTW:
    case AArch64_AM::UXTX:
      return AArch64_AM::getArithShiftValue(Imm) <= 4;
    }
  }

  case AArch64::SUBWrs:
  case AArch64::SUBSWrs: {
    unsigned Imm = MI.getOperand(3).getImm();
    unsigned ShiftVal = AArch64_AM::getShiftValue(Imm);
    return ShiftVal == 0 ||
           (AArch64_AM::getShiftType(Imm) == AArch64_AM::ASR && ShiftVal == 31);
  }

  case AArch64::SUBXrs:
  case AArch64::SUBSXrs: {
    unsigned Imm = MI.getOperand(3).getImm();
    unsigned ShiftVal = AArch64_AM::getShiftValue(Imm);
    return ShiftVal == 0 ||
           (AArch64_AM::getShiftType(Imm) == AArch64_AM::ASR && ShiftVal == 63);
  }

  case AArch64::SUBWrx:
  case AArch64::SUBXrx:
  case AArch64::SUBXrx64:
  case AArch64::SUBSWrx:
  case AArch64::SUBSXrx:
  case AArch64::SUBSXrx64: {
    unsigned Imm = MI.getOperand(3).getImm();
    switch (AArch64_AM::getArithExtendType(Imm)) {
    default:
      return false;
    case AArch64_AM::UXTB:
    case AArch64_AM::UXTH:
    case AArch64_AM::UXTW:
    case AArch64_AM::UXTX:
      return AArch64_AM::getArithShiftValue(Imm) == 0;
    }
  }

  case AArch64::LDRBBroW:
  case AArch64::LDRBBroX:
  case AArch64::LDRBroW:
  case AArch64::LDRBroX:
  case AArch64::LDRDroW:
  case AArch64::LDRDroX:
  case AArch64::LDRHHroW:
  case AArch64::LDRHHroX:
  case AArch64::LDRHroW:
  case AArch64::LDRHroX:
  case AArch64::LDRQroW:
  case AArch64::LDRQroX:
  case AArch64::LDRSBWroW:
  case AArch64::LDRSBWroX:
  case AArch64::LDRSBXroW:
  case AArch64::LDRSBXroX:
  case AArch64::LDRSHWroW:
  case AArch64::LDRSHWroX:
  case AArch64::LDRSHXroW:
  case AArch64::LDRSHXroX:
  case AArch64::LDRSWroW:
  case AArch64::LDRSWroX:
  case AArch64::LDRSroW:
  case AArch64::LDRSroX:
  case AArch64::LDRWroW:
  case AArch64::LDRWroX:
  case AArch64::LDRXroW:
  case AArch64::LDRXroX:
  case AArch64::PRFMroW:
  case AArch64::PRFMroX:
  case AArch64::STRBBroW:
  case AArch64::STRBBroX:
  case AArch64::STRBroW:
  case AArch64::STRBroX:
  case AArch64::STRDroW:
  case AArch64::STRDroX:
  case AArch64::STRHHroW:
  case AArch64::STRHHroX:
  case AArch64::STRHroW:
  case AArch64::STRHroX:
  case AArch64::STRQroW:
  case AArch64::STRQroX:
  case AArch64::STRSroW:
  case AArch64::STRSroX:
  case AArch64::STRWroW:
  case AArch64::STRWroX:
  case AArch64::STRXroW:
  case AArch64::STRXroX: {
    unsigned IsSigned = MI.getOperand(3).getImm();
    return !IsSigned;
  }
  }
}

bool AArch64InstrInfo::isSEHInstruction(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    default:
      return false;
    case AArch64::SEH_StackAlloc:
    case AArch64::SEH_SaveFPLR:
    case AArch64::SEH_SaveFPLR_X:
    case AArch64::SEH_SaveReg:
    case AArch64::SEH_SaveReg_X:
    case AArch64::SEH_SaveRegP:
    case AArch64::SEH_SaveRegP_X:
    case AArch64::SEH_SaveFReg:
    case AArch64::SEH_SaveFReg_X:
    case AArch64::SEH_SaveFRegP:
    case AArch64::SEH_SaveFRegP_X:
    case AArch64::SEH_SetFP:
    case AArch64::SEH_AddFP:
    case AArch64::SEH_Nop:
    case AArch64::SEH_PrologEnd:
    case AArch64::SEH_EpilogStart:
    case AArch64::SEH_EpilogEnd:
    case AArch64::SEH_PACSignLR:
    case AArch64::SEH_SaveAnyRegQP:
    case AArch64::SEH_SaveAnyRegQPX:
      return true;
  }
}

bool AArch64InstrInfo::isCoalescableExtInstr(const MachineInstr &MI,
                                             Register &SrcReg, Register &DstReg,
                                             unsigned &SubIdx) const {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::SBFMXri: // aka sxtw
  case AArch64::UBFMXri: // aka uxtw
    // Check for the 32 -> 64 bit extension case, these instructions can do
    // much more.
    if (MI.getOperand(2).getImm() != 0 || MI.getOperand(3).getImm() != 31)
      return false;
    // This is a signed or unsigned 32 -> 64 bit extension.
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    SubIdx = AArch64::sub_32;
    return true;
  }
}

bool AArch64InstrInfo::areMemAccessesTriviallyDisjoint(
    const MachineInstr &MIa, const MachineInstr &MIb) const {
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  const MachineOperand *BaseOpA = nullptr, *BaseOpB = nullptr;
  int64_t OffsetA = 0, OffsetB = 0;
  TypeSize WidthA(0, false), WidthB(0, false);
  bool OffsetAIsScalable = false, OffsetBIsScalable = false;

  assert(MIa.mayLoadOrStore() && "MIa must be a load or store.");
  assert(MIb.mayLoadOrStore() && "MIb must be a load or store.");

  if (MIa.hasUnmodeledSideEffects() || MIb.hasUnmodeledSideEffects() ||
      MIa.hasOrderedMemoryRef() || MIb.hasOrderedMemoryRef())
    return false;

  // Retrieve the base, offset from the base and width. Width
  // is the size of memory that is being loaded/stored (e.g. 1, 2, 4, 8).  If
  // base are identical, and the offset of a lower memory access +
  // the width doesn't overlap the offset of a higher memory access,
  // then the memory accesses are different.
  // If OffsetAIsScalable and OffsetBIsScalable are both true, they
  // are assumed to have the same scale (vscale).
  if (getMemOperandWithOffsetWidth(MIa, BaseOpA, OffsetA, OffsetAIsScalable,
                                   WidthA, TRI) &&
      getMemOperandWithOffsetWidth(MIb, BaseOpB, OffsetB, OffsetBIsScalable,
                                   WidthB, TRI)) {
    if (BaseOpA->isIdenticalTo(*BaseOpB) &&
        OffsetAIsScalable == OffsetBIsScalable) {
      int LowOffset = OffsetA < OffsetB ? OffsetA : OffsetB;
      int HighOffset = OffsetA < OffsetB ? OffsetB : OffsetA;
      TypeSize LowWidth = (LowOffset == OffsetA) ? WidthA : WidthB;
      if (LowWidth.isScalable() == OffsetAIsScalable &&
          LowOffset + (int)LowWidth.getKnownMinValue() <= HighOffset)
        return true;
    }
  }
  return false;
}

bool AArch64InstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                            const MachineBasicBlock *MBB,
                                            const MachineFunction &MF) const {
  if (TargetInstrInfo::isSchedulingBoundary(MI, MBB, MF))
    return true;

  // Do not move an instruction that can be recognized as a branch target.
  if (hasBTISemantics(MI))
    return true;

  switch (MI.getOpcode()) {
  case AArch64::HINT:
    // CSDB hints are scheduling barriers.
    if (MI.getOperand(0).getImm() == 0x14)
      return true;
    break;
  case AArch64::DSB:
  case AArch64::ISB:
    // DSB and ISB also are scheduling barriers.
    return true;
  case AArch64::MSRpstatesvcrImm1:
    // SMSTART and SMSTOP are also scheduling barriers.
    return true;
  default:;
  }
  if (isSEHInstruction(MI))
    return true;
  auto Next = std::next(MI.getIterator());
  return Next != MBB->end() && Next->isCFIInstruction();
}

/// analyzeCompare - For a comparison instruction, return the source registers
/// in SrcReg and SrcReg2, and the value it compares against in CmpValue.
/// Return true if the comparison instruction can be analyzed.
bool AArch64InstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                      Register &SrcReg2, int64_t &CmpMask,
                                      int64_t &CmpValue) const {
  // The first operand can be a frame index where we'd normally expect a
  // register.
  assert(MI.getNumOperands() >= 2 && "All AArch64 cmps should have 2 operands");
  if (!MI.getOperand(1).isReg())
    return false;

  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::PTEST_PP:
  case AArch64::PTEST_PP_ANY:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = MI.getOperand(1).getReg();
    // Not sure about the mask and value for now...
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  case AArch64::SUBSWrr:
  case AArch64::SUBSWrs:
  case AArch64::SUBSWrx:
  case AArch64::SUBSXrr:
  case AArch64::SUBSXrs:
  case AArch64::SUBSXrx:
  case AArch64::ADDSWrr:
  case AArch64::ADDSWrs:
  case AArch64::ADDSWrx:
  case AArch64::ADDSXrr:
  case AArch64::ADDSXrs:
  case AArch64::ADDSXrx:
    // Replace SUBSWrr with SUBWrr if NZCV is not used.
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = MI.getOperand(2).getReg();
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  case AArch64::SUBSWri:
  case AArch64::ADDSWri:
  case AArch64::SUBSXri:
  case AArch64::ADDSXri:
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = MI.getOperand(2).getImm();
    return true;
  case AArch64::ANDSWri:
  case AArch64::ANDSXri:
    // ANDS does not use the same encoding scheme as the others xxxS
    // instructions.
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = AArch64_AM::decodeLogicalImmediate(
                   MI.getOperand(2).getImm(),
                   MI.getOpcode() == AArch64::ANDSWri ? 32 : 64);
    return true;
  }

  return false;
}

static bool UpdateOperandRegClass(MachineInstr &Instr) {
  MachineBasicBlock *MBB = Instr.getParent();
  assert(MBB && "Can't get MachineBasicBlock here");
  MachineFunction *MF = MBB->getParent();
  assert(MF && "Can't get MachineFunction here");
  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  MachineRegisterInfo *MRI = &MF->getRegInfo();

  for (unsigned OpIdx = 0, EndIdx = Instr.getNumOperands(); OpIdx < EndIdx;
       ++OpIdx) {
    MachineOperand &MO = Instr.getOperand(OpIdx);
    const TargetRegisterClass *OpRegCstraints =
        Instr.getRegClassConstraint(OpIdx, TII, TRI);

    // If there's no constraint, there's nothing to do.
    if (!OpRegCstraints)
      continue;
    // If the operand is a frame index, there's nothing to do here.
    // A frame index operand will resolve correctly during PEI.
    if (MO.isFI())
      continue;

    assert(MO.isReg() &&
           "Operand has register constraints without being a register!");

    Register Reg = MO.getReg();
    if (Reg.isPhysical()) {
      if (!OpRegCstraints->contains(Reg))
        return false;
    } else if (!OpRegCstraints->hasSubClassEq(MRI->getRegClass(Reg)) &&
               !MRI->constrainRegClass(Reg, OpRegCstraints))
      return false;
  }

  return true;
}

/// Return the opcode that does not set flags when possible - otherwise
/// return the original opcode. The caller is responsible to do the actual
/// substitution and legality checking.
static unsigned convertToNonFlagSettingOpc(const MachineInstr &MI) {
  // Don't convert all compare instructions, because for some the zero register
  // encoding becomes the sp register.
  bool MIDefinesZeroReg = false;
  if (MI.definesRegister(AArch64::WZR, /*TRI=*/nullptr) ||
      MI.definesRegister(AArch64::XZR, /*TRI=*/nullptr))
    MIDefinesZeroReg = true;

  switch (MI.getOpcode()) {
  default:
    return MI.getOpcode();
  case AArch64::ADDSWrr:
    return AArch64::ADDWrr;
  case AArch64::ADDSWri:
    return MIDefinesZeroReg ? AArch64::ADDSWri : AArch64::ADDWri;
  case AArch64::ADDSWrs:
    return MIDefinesZeroReg ? AArch64::ADDSWrs : AArch64::ADDWrs;
  case AArch64::ADDSWrx:
    return AArch64::ADDWrx;
  case AArch64::ADDSXrr:
    return AArch64::ADDXrr;
  case AArch64::ADDSXri:
    return MIDefinesZeroReg ? AArch64::ADDSXri : AArch64::ADDXri;
  case AArch64::ADDSXrs:
    return MIDefinesZeroReg ? AArch64::ADDSXrs : AArch64::ADDXrs;
  case AArch64::ADDSXrx:
    return AArch64::ADDXrx;
  case AArch64::SUBSWrr:
    return AArch64::SUBWrr;
  case AArch64::SUBSWri:
    return MIDefinesZeroReg ? AArch64::SUBSWri : AArch64::SUBWri;
  case AArch64::SUBSWrs:
    return MIDefinesZeroReg ? AArch64::SUBSWrs : AArch64::SUBWrs;
  case AArch64::SUBSWrx:
    return AArch64::SUBWrx;
  case AArch64::SUBSXrr:
    return AArch64::SUBXrr;
  case AArch64::SUBSXri:
    return MIDefinesZeroReg ? AArch64::SUBSXri : AArch64::SUBXri;
  case AArch64::SUBSXrs:
    return MIDefinesZeroReg ? AArch64::SUBSXrs : AArch64::SUBXrs;
  case AArch64::SUBSXrx:
    return AArch64::SUBXrx;
  }
}

enum AccessKind { AK_Write = 0x01, AK_Read = 0x10, AK_All = 0x11 };

/// True when condition flags are accessed (either by writing or reading)
/// on the instruction trace starting at From and ending at To.
///
/// Note: If From and To are from different blocks it's assumed CC are accessed
///       on the path.
static bool areCFlagsAccessedBetweenInstrs(
    MachineBasicBlock::iterator From, MachineBasicBlock::iterator To,
    const TargetRegisterInfo *TRI, const AccessKind AccessToCheck = AK_All) {
  // Early exit if To is at the beginning of the BB.
  if (To == To->getParent()->begin())
    return true;

  // Check whether the instructions are in the same basic block
  // If not, assume the condition flags might get modified somewhere.
  if (To->getParent() != From->getParent())
    return true;

  // From must be above To.
  assert(std::any_of(
      ++To.getReverse(), To->getParent()->rend(),
      [From](MachineInstr &MI) { return MI.getIterator() == From; }));

  // We iterate backward starting at \p To until we hit \p From.
  for (const MachineInstr &Instr :
       instructionsWithoutDebug(++To.getReverse(), From.getReverse())) {
    if (((AccessToCheck & AK_Write) &&
         Instr.modifiesRegister(AArch64::NZCV, TRI)) ||
        ((AccessToCheck & AK_Read) && Instr.readsRegister(AArch64::NZCV, TRI)))
      return true;
  }
  return false;
}

std::optional<unsigned>
AArch64InstrInfo::canRemovePTestInstr(MachineInstr *PTest, MachineInstr *Mask,
                                      MachineInstr *Pred,
                                      const MachineRegisterInfo *MRI) const {
  unsigned MaskOpcode = Mask->getOpcode();
  unsigned PredOpcode = Pred->getOpcode();
  bool PredIsPTestLike = isPTestLikeOpcode(PredOpcode);
  bool PredIsWhileLike = isWhileOpcode(PredOpcode);

  if (PredIsWhileLike) {
    // For PTEST(PG, PG), PTEST is redundant when PG is the result of a WHILEcc
    // instruction and the condition is "any" since WHILcc does an implicit
    // PTEST(ALL, PG) check and PG is always a subset of ALL.
    if ((Mask == Pred) && PTest->getOpcode() == AArch64::PTEST_PP_ANY)
      return PredOpcode;

    // For PTEST(PTRUE_ALL, WHILE), if the element size matches, the PTEST is
    // redundant since WHILE performs an implicit PTEST with an all active
    // mask.
    if (isPTrueOpcode(MaskOpcode) && Mask->getOperand(1).getImm() == 31 &&
        getElementSizeForOpcode(MaskOpcode) ==
            getElementSizeForOpcode(PredOpcode))
      return PredOpcode;

    return {};
  }

  if (PredIsPTestLike) {
    // For PTEST(PG, PG), PTEST is redundant when PG is the result of an
    // instruction that sets the flags as PTEST would and the condition is
    // "any" since PG is always a subset of the governing predicate of the
    // ptest-like instruction.
    if ((Mask == Pred) && PTest->getOpcode() == AArch64::PTEST_PP_ANY)
      return PredOpcode;

    // For PTEST(PTRUE_ALL, PTEST_LIKE), the PTEST is redundant if the
    // the element size matches and either the PTEST_LIKE instruction uses
    // the same all active mask or the condition is "any".
    if (isPTrueOpcode(MaskOpcode) && Mask->getOperand(1).getImm() == 31 &&
        getElementSizeForOpcode(MaskOpcode) ==
            getElementSizeForOpcode(PredOpcode)) {
      auto PTestLikeMask = MRI->getUniqueVRegDef(Pred->getOperand(1).getReg());
      if (Mask == PTestLikeMask || PTest->getOpcode() == AArch64::PTEST_PP_ANY)
        return PredOpcode;
    }

    // For PTEST(PG, PTEST_LIKE(PG, ...)), the PTEST is redundant since the
    // flags are set based on the same mask 'PG', but PTEST_LIKE must operate
    // on 8-bit predicates like the PTEST.  Otherwise, for instructions like
    // compare that also support 16/32/64-bit predicates, the implicit PTEST
    // performed by the compare could consider fewer lanes for these element
    // sizes.
    //
    // For example, consider
    //
    //   ptrue p0.b                    ; P0=1111-1111-1111-1111
    //   index z0.s, #0, #1            ; Z0=<0,1,2,3>
    //   index z1.s, #1, #1            ; Z1=<1,2,3,4>
    //   cmphi p1.s, p0/z, z1.s, z0.s  ; P1=0001-0001-0001-0001
    //                                 ;       ^ last active
    //   ptest p0, p1.b                ; P1=0001-0001-0001-0001
    //                                 ;     ^ last active
    //
    // where the compare generates a canonical all active 32-bit predicate
    // (equivalent to 'ptrue p1.s, all'). The implicit PTEST sets the last
    // active flag, whereas the PTEST instruction with the same mask doesn't.
    // For PTEST_ANY this doesn't apply as the flags in this case would be
    // identical regardless of element size.
    auto PTestLikeMask = MRI->getUniqueVRegDef(Pred->getOperand(1).getReg());
    uint64_t PredElementSize = getElementSizeForOpcode(PredOpcode);
    if (Mask == PTestLikeMask && (PredElementSize == AArch64::ElementSizeB ||
                                  PTest->getOpcode() == AArch64::PTEST_PP_ANY))
      return PredOpcode;

    return {};
  }

  // If OP in PTEST(PG, OP(PG, ...)) has a flag-setting variant change the
  // opcode so the PTEST becomes redundant.
  switch (PredOpcode) {
  case AArch64::AND_PPzPP:
  case AArch64::BIC_PPzPP:
  case AArch64::EOR_PPzPP:
  case AArch64::NAND_PPzPP:
  case AArch64::NOR_PPzPP:
  case AArch64::ORN_PPzPP:
  case AArch64::ORR_PPzPP:
  case AArch64::BRKA_PPzP:
  case AArch64::BRKPA_PPzPP:
  case AArch64::BRKB_PPzP:
  case AArch64::BRKPB_PPzPP:
  case AArch64::RDFFR_PPz: {
    // Check to see if our mask is the same. If not the resulting flag bits
    // may be different and we can't remove the ptest.
    auto *PredMask = MRI->getUniqueVRegDef(Pred->getOperand(1).getReg());
    if (Mask != PredMask)
      return {};
    break;
  }
  case AArch64::BRKN_PPzP: {
    // BRKN uses an all active implicit mask to set flags unlike the other
    // flag-setting instructions.
    // PTEST(PTRUE_B(31), BRKN(PG, A, B)) -> BRKNS(PG, A, B).
    if ((MaskOpcode != AArch64::PTRUE_B) ||
        (Mask->getOperand(1).getImm() != 31))
      return {};
    break;
  }
  case AArch64::PTRUE_B:
    // PTEST(OP=PTRUE_B(A), OP) -> PTRUES_B(A)
    break;
  default:
    // Bail out if we don't recognize the input
    return {};
  }

  return convertToFlagSettingOpc(PredOpcode);
}

/// optimizePTestInstr - Attempt to remove a ptest of a predicate-generating
/// operation which could set the flags in an identical manner
bool AArch64InstrInfo::optimizePTestInstr(
    MachineInstr *PTest, unsigned MaskReg, unsigned PredReg,
    const MachineRegisterInfo *MRI) const {
  auto *Mask = MRI->getUniqueVRegDef(MaskReg);
  auto *Pred = MRI->getUniqueVRegDef(PredReg);
  unsigned PredOpcode = Pred->getOpcode();
  auto NewOp = canRemovePTestInstr(PTest, Mask, Pred, MRI);
  if (!NewOp)
    return false;

  const TargetRegisterInfo *TRI = &getRegisterInfo();

  // If another instruction between Pred and PTest accesses flags, don't remove
  // the ptest or update the earlier instruction to modify them.
  if (areCFlagsAccessedBetweenInstrs(Pred, PTest, TRI))
    return false;

  // If we pass all the checks, it's safe to remove the PTEST and use the flags
  // as they are prior to PTEST. Sometimes this requires the tested PTEST
  // operand to be replaced with an equivalent instruction that also sets the
  // flags.
  PTest->eraseFromParent();
  if (*NewOp != PredOpcode) {
    Pred->setDesc(get(*NewOp));
    bool succeeded = UpdateOperandRegClass(*Pred);
    (void)succeeded;
    assert(succeeded && "Operands have incompatible register classes!");
    Pred->addRegisterDefined(AArch64::NZCV, TRI);
  }

  // Ensure that the flags def is live.
  if (Pred->registerDefIsDead(AArch64::NZCV, TRI)) {
    unsigned i = 0, e = Pred->getNumOperands();
    for (; i != e; ++i) {
      MachineOperand &MO = Pred->getOperand(i);
      if (MO.isReg() && MO.isDef() && MO.getReg() == AArch64::NZCV) {
        MO.setIsDead(false);
        break;
      }
    }
  }
  return true;
}

/// Try to optimize a compare instruction. A compare instruction is an
/// instruction which produces AArch64::NZCV. It can be truly compare
/// instruction
/// when there are no uses of its destination register.
///
/// The following steps are tried in order:
/// 1. Convert CmpInstr into an unconditional version.
/// 2. Remove CmpInstr if above there is an instruction producing a needed
///    condition code or an instruction which can be converted into such an
///    instruction.
///    Only comparison with zero is supported.
bool AArch64InstrInfo::optimizeCompareInstr(
    MachineInstr &CmpInstr, Register SrcReg, Register SrcReg2, int64_t CmpMask,
    int64_t CmpValue, const MachineRegisterInfo *MRI) const {
  assert(CmpInstr.getParent());
  assert(MRI);

  // Replace SUBSWrr with SUBWrr if NZCV is not used.
  int DeadNZCVIdx =
      CmpInstr.findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr, true);
  if (DeadNZCVIdx != -1) {
    if (CmpInstr.definesRegister(AArch64::WZR, /*TRI=*/nullptr) ||
        CmpInstr.definesRegister(AArch64::XZR, /*TRI=*/nullptr)) {
      CmpInstr.eraseFromParent();
      return true;
    }
    unsigned Opc = CmpInstr.getOpcode();
    unsigned NewOpc = convertToNonFlagSettingOpc(CmpInstr);
    if (NewOpc == Opc)
      return false;
    const MCInstrDesc &MCID = get(NewOpc);
    CmpInstr.setDesc(MCID);
    CmpInstr.removeOperand(DeadNZCVIdx);
    bool succeeded = UpdateOperandRegClass(CmpInstr);
    (void)succeeded;
    assert(succeeded && "Some operands reg class are incompatible!");
    return true;
  }

  if (CmpInstr.getOpcode() == AArch64::PTEST_PP ||
      CmpInstr.getOpcode() == AArch64::PTEST_PP_ANY)
    return optimizePTestInstr(&CmpInstr, SrcReg, SrcReg2, MRI);

  if (SrcReg2 != 0)
    return false;

  // CmpInstr is a Compare instruction if destination register is not used.
  if (!MRI->use_nodbg_empty(CmpInstr.getOperand(0).getReg()))
    return false;

  if (CmpValue == 0 && substituteCmpToZero(CmpInstr, SrcReg, *MRI))
    return true;
  return (CmpValue == 0 || CmpValue == 1) &&
         removeCmpToZeroOrOne(CmpInstr, SrcReg, CmpValue, *MRI);
}

/// Get opcode of S version of Instr.
/// If Instr is S version its opcode is returned.
/// AArch64::INSTRUCTION_LIST_END is returned if Instr does not have S version
/// or we are not interested in it.
static unsigned sForm(MachineInstr &Instr) {
  switch (Instr.getOpcode()) {
  default:
    return AArch64::INSTRUCTION_LIST_END;

  case AArch64::ADDSWrr:
  case AArch64::ADDSWri:
  case AArch64::ADDSXrr:
  case AArch64::ADDSXri:
  case AArch64::SUBSWrr:
  case AArch64::SUBSWri:
  case AArch64::SUBSXrr:
  case AArch64::SUBSXri:
    return Instr.getOpcode();

  case AArch64::ADDWrr:
    return AArch64::ADDSWrr;
  case AArch64::ADDWri:
    return AArch64::ADDSWri;
  case AArch64::ADDXrr:
    return AArch64::ADDSXrr;
  case AArch64::ADDXri:
    return AArch64::ADDSXri;
  case AArch64::ADCWr:
    return AArch64::ADCSWr;
  case AArch64::ADCXr:
    return AArch64::ADCSXr;
  case AArch64::SUBWrr:
    return AArch64::SUBSWrr;
  case AArch64::SUBWri:
    return AArch64::SUBSWri;
  case AArch64::SUBXrr:
    return AArch64::SUBSXrr;
  case AArch64::SUBXri:
    return AArch64::SUBSXri;
  case AArch64::SBCWr:
    return AArch64::SBCSWr;
  case AArch64::SBCXr:
    return AArch64::SBCSXr;
  case AArch64::ANDWri:
    return AArch64::ANDSWri;
  case AArch64::ANDXri:
    return AArch64::ANDSXri;
  }
}

/// Check if AArch64::NZCV should be alive in successors of MBB.
static bool areCFlagsAliveInSuccessors(const MachineBasicBlock *MBB) {
  for (auto *BB : MBB->successors())
    if (BB->isLiveIn(AArch64::NZCV))
      return true;
  return false;
}

/// \returns The condition code operand index for \p Instr if it is a branch
/// or select and -1 otherwise.
static int
findCondCodeUseOperandIdxForBranchOrSelect(const MachineInstr &Instr) {
  switch (Instr.getOpcode()) {
  default:
    return -1;

  case AArch64::Bcc: {
    int Idx = Instr.findRegisterUseOperandIdx(AArch64::NZCV, /*TRI=*/nullptr);
    assert(Idx >= 2);
    return Idx - 2;
  }

  case AArch64::CSINVWr:
  case AArch64::CSINVXr:
  case AArch64::CSINCWr:
  case AArch64::CSINCXr:
  case AArch64::CSELWr:
  case AArch64::CSELXr:
  case AArch64::CSNEGWr:
  case AArch64::CSNEGXr:
  case AArch64::FCSELSrrr:
  case AArch64::FCSELDrrr: {
    int Idx = Instr.findRegisterUseOperandIdx(AArch64::NZCV, /*TRI=*/nullptr);
    assert(Idx >= 1);
    return Idx - 1;
  }
  }
}

/// Find a condition code used by the instruction.
/// Returns AArch64CC::Invalid if either the instruction does not use condition
/// codes or we don't optimize CmpInstr in the presence of such instructions.
static AArch64CC::CondCode findCondCodeUsedByInstr(const MachineInstr &Instr) {
  int CCIdx = findCondCodeUseOperandIdxForBranchOrSelect(Instr);
  return CCIdx >= 0 ? static_cast<AArch64CC::CondCode>(
                          Instr.getOperand(CCIdx).getImm())
                    : AArch64CC::Invalid;
}

static UsedNZCV getUsedNZCV(AArch64CC::CondCode CC) {
  assert(CC != AArch64CC::Invalid);
  UsedNZCV UsedFlags;
  switch (CC) {
  default:
    break;

  case AArch64CC::EQ: // Z set
  case AArch64CC::NE: // Z clear
    UsedFlags.Z = true;
    break;

  case AArch64CC::HI: // Z clear and C set
  case AArch64CC::LS: // Z set   or  C clear
    UsedFlags.Z = true;
    [[fallthrough]];
  case AArch64CC::HS: // C set
  case AArch64CC::LO: // C clear
    UsedFlags.C = true;
    break;

  case AArch64CC::MI: // N set
  case AArch64CC::PL: // N clear
    UsedFlags.N = true;
    break;

  case AArch64CC::VS: // V set
  case AArch64CC::VC: // V clear
    UsedFlags.V = true;
    break;

  case AArch64CC::GT: // Z clear, N and V the same
  case AArch64CC::LE: // Z set,   N and V differ
    UsedFlags.Z = true;
    [[fallthrough]];
  case AArch64CC::GE: // N and V the same
  case AArch64CC::LT: // N and V differ
    UsedFlags.N = true;
    UsedFlags.V = true;
    break;
  }
  return UsedFlags;
}

/// \returns Conditions flags used after \p CmpInstr in its MachineBB if NZCV
/// flags are not alive in successors of the same \p CmpInstr and \p MI parent.
/// \returns std::nullopt otherwise.
///
/// Collect instructions using that flags in \p CCUseInstrs if provided.
std::optional<UsedNZCV>
llvm::examineCFlagsUse(MachineInstr &MI, MachineInstr &CmpInstr,
                       const TargetRegisterInfo &TRI,
                       SmallVectorImpl<MachineInstr *> *CCUseInstrs) {
  MachineBasicBlock *CmpParent = CmpInstr.getParent();
  if (MI.getParent() != CmpParent)
    return std::nullopt;

  if (areCFlagsAliveInSuccessors(CmpParent))
    return std::nullopt;

  UsedNZCV NZCVUsedAfterCmp;
  for (MachineInstr &Instr : instructionsWithoutDebug(
           std::next(CmpInstr.getIterator()), CmpParent->instr_end())) {
    if (Instr.readsRegister(AArch64::NZCV, &TRI)) {
      AArch64CC::CondCode CC = findCondCodeUsedByInstr(Instr);
      if (CC == AArch64CC::Invalid) // Unsupported conditional instruction
        return std::nullopt;
      NZCVUsedAfterCmp |= getUsedNZCV(CC);
      if (CCUseInstrs)
        CCUseInstrs->push_back(&Instr);
    }
    if (Instr.modifiesRegister(AArch64::NZCV, &TRI))
      break;
  }
  return NZCVUsedAfterCmp;
}

static bool isADDSRegImm(unsigned Opcode) {
  return Opcode == AArch64::ADDSWri || Opcode == AArch64::ADDSXri;
}

static bool isSUBSRegImm(unsigned Opcode) {
  return Opcode == AArch64::SUBSWri || Opcode == AArch64::SUBSXri;
}

/// Check if CmpInstr can be substituted by MI.
///
/// CmpInstr can be substituted:
/// - CmpInstr is either 'ADDS %vreg, 0' or 'SUBS %vreg, 0'
/// - and, MI and CmpInstr are from the same MachineBB
/// - and, condition flags are not alive in successors of the CmpInstr parent
/// - and, if MI opcode is the S form there must be no defs of flags between
///        MI and CmpInstr
///        or if MI opcode is not the S form there must be neither defs of flags
///        nor uses of flags between MI and CmpInstr.
/// - and, if C/V flags are not used after CmpInstr
///        or if N flag is used but MI produces poison value if signed overflow
///        occurs.
static bool canInstrSubstituteCmpInstr(MachineInstr &MI, MachineInstr &CmpInstr,
                                       const TargetRegisterInfo &TRI) {
  // NOTE this assertion guarantees that MI.getOpcode() is add or subtraction
  // that may or may not set flags.
  assert(sForm(MI) != AArch64::INSTRUCTION_LIST_END);

  const unsigned CmpOpcode = CmpInstr.getOpcode();
  if (!isADDSRegImm(CmpOpcode) && !isSUBSRegImm(CmpOpcode))
    return false;

  assert((CmpInstr.getOperand(2).isImm() &&
          CmpInstr.getOperand(2).getImm() == 0) &&
         "Caller guarantees that CmpInstr compares with constant 0");

  std::optional<UsedNZCV> NZVCUsed = examineCFlagsUse(MI, CmpInstr, TRI);
  if (!NZVCUsed || NZVCUsed->C)
    return false;

  // CmpInstr is either 'ADDS %vreg, 0' or 'SUBS %vreg, 0', and MI is either
  // '%vreg = add ...' or '%vreg = sub ...'.
  // Condition flag V is used to indicate signed overflow.
  // 1) MI and CmpInstr set N and V to the same value.
  // 2) If MI is add/sub with no-signed-wrap, it produces a poison value when
  //    signed overflow occurs, so CmpInstr could still be simplified away.
  if (NZVCUsed->V && !MI.getFlag(MachineInstr::NoSWrap))
    return false;

  AccessKind AccessToCheck = AK_Write;
  if (sForm(MI) != MI.getOpcode())
    AccessToCheck = AK_All;
  return !areCFlagsAccessedBetweenInstrs(&MI, &CmpInstr, &TRI, AccessToCheck);
}

/// Substitute an instruction comparing to zero with another instruction
/// which produces needed condition flags.
///
/// Return true on success.
bool AArch64InstrInfo::substituteCmpToZero(
    MachineInstr &CmpInstr, unsigned SrcReg,
    const MachineRegisterInfo &MRI) const {
  // Get the unique definition of SrcReg.
  MachineInstr *MI = MRI.getUniqueVRegDef(SrcReg);
  if (!MI)
    return false;

  const TargetRegisterInfo &TRI = getRegisterInfo();

  unsigned NewOpc = sForm(*MI);
  if (NewOpc == AArch64::INSTRUCTION_LIST_END)
    return false;

  if (!canInstrSubstituteCmpInstr(*MI, CmpInstr, TRI))
    return false;

  // Update the instruction to set NZCV.
  MI->setDesc(get(NewOpc));
  CmpInstr.eraseFromParent();
  bool succeeded = UpdateOperandRegClass(*MI);
  (void)succeeded;
  assert(succeeded && "Some operands reg class are incompatible!");
  MI->addRegisterDefined(AArch64::NZCV, &TRI);
  return true;
}

/// \returns True if \p CmpInstr can be removed.
///
/// \p IsInvertCC is true if, after removing \p CmpInstr, condition
/// codes used in \p CCUseInstrs must be inverted.
static bool canCmpInstrBeRemoved(MachineInstr &MI, MachineInstr &CmpInstr,
                                 int CmpValue, const TargetRegisterInfo &TRI,
                                 SmallVectorImpl<MachineInstr *> &CCUseInstrs,
                                 bool &IsInvertCC) {
  assert((CmpValue == 0 || CmpValue == 1) &&
         "Only comparisons to 0 or 1 considered for removal!");

  // MI is 'CSINCWr %vreg, wzr, wzr, <cc>' or 'CSINCXr %vreg, xzr, xzr, <cc>'
  unsigned MIOpc = MI.getOpcode();
  if (MIOpc == AArch64::CSINCWr) {
    if (MI.getOperand(1).getReg() != AArch64::WZR ||
        MI.getOperand(2).getReg() != AArch64::WZR)
      return false;
  } else if (MIOpc == AArch64::CSINCXr) {
    if (MI.getOperand(1).getReg() != AArch64::XZR ||
        MI.getOperand(2).getReg() != AArch64::XZR)
      return false;
  } else {
    return false;
  }
  AArch64CC::CondCode MICC = findCondCodeUsedByInstr(MI);
  if (MICC == AArch64CC::Invalid)
    return false;

  // NZCV needs to be defined
  if (MI.findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr, true) != -1)
    return false;

  // CmpInstr is 'ADDS %vreg, 0' or 'SUBS %vreg, 0' or 'SUBS %vreg, 1'
  const unsigned CmpOpcode = CmpInstr.getOpcode();
  bool IsSubsRegImm = isSUBSRegImm(CmpOpcode);
  if (CmpValue && !IsSubsRegImm)
    return false;
  if (!CmpValue && !IsSubsRegImm && !isADDSRegImm(CmpOpcode))
    return false;

  // MI conditions allowed: eq, ne, mi, pl
  UsedNZCV MIUsedNZCV = getUsedNZCV(MICC);
  if (MIUsedNZCV.C || MIUsedNZCV.V)
    return false;

  std::optional<UsedNZCV> NZCVUsedAfterCmp =
      examineCFlagsUse(MI, CmpInstr, TRI, &CCUseInstrs);
  // Condition flags are not used in CmpInstr basic block successors and only
  // Z or N flags allowed to be used after CmpInstr within its basic block
  if (!NZCVUsedAfterCmp || NZCVUsedAfterCmp->C || NZCVUsedAfterCmp->V)
    return false;
  // Z or N flag used after CmpInstr must correspond to the flag used in MI
  if ((MIUsedNZCV.Z && NZCVUsedAfterCmp->N) ||
      (MIUsedNZCV.N && NZCVUsedAfterCmp->Z))
    return false;
  // If CmpInstr is comparison to zero MI conditions are limited to eq, ne
  if (MIUsedNZCV.N && !CmpValue)
    return false;

  // There must be no defs of flags between MI and CmpInstr
  if (areCFlagsAccessedBetweenInstrs(&MI, &CmpInstr, &TRI, AK_Write))
    return false;

  // Condition code is inverted in the following cases:
  // 1. MI condition is ne; CmpInstr is 'ADDS %vreg, 0' or 'SUBS %vreg, 0'
  // 2. MI condition is eq, pl; CmpInstr is 'SUBS %vreg, 1'
  IsInvertCC = (CmpValue && (MICC == AArch64CC::EQ || MICC == AArch64CC::PL)) ||
               (!CmpValue && MICC == AArch64CC::NE);
  return true;
}

/// Remove comparison in csinc-cmp sequence
///
/// Examples:
/// 1. \code
///   csinc w9, wzr, wzr, ne
///   cmp   w9, #0
///   b.eq
///    \endcode
/// to
///    \code
///   csinc w9, wzr, wzr, ne
///   b.ne
///    \endcode
///
/// 2. \code
///   csinc x2, xzr, xzr, mi
///   cmp   x2, #1
///   b.pl
///    \endcode
/// to
///    \code
///   csinc x2, xzr, xzr, mi
///   b.pl
///    \endcode
///
/// \param  CmpInstr comparison instruction
/// \return True when comparison removed
bool AArch64InstrInfo::removeCmpToZeroOrOne(
    MachineInstr &CmpInstr, unsigned SrcReg, int CmpValue,
    const MachineRegisterInfo &MRI) const {
  MachineInstr *MI = MRI.getUniqueVRegDef(SrcReg);
  if (!MI)
    return false;
  const TargetRegisterInfo &TRI = getRegisterInfo();
  SmallVector<MachineInstr *, 4> CCUseInstrs;
  bool IsInvertCC = false;
  if (!canCmpInstrBeRemoved(*MI, CmpInstr, CmpValue, TRI, CCUseInstrs,
                            IsInvertCC))
    return false;
  // Make transformation
  CmpInstr.eraseFromParent();
  if (IsInvertCC) {
    // Invert condition codes in CmpInstr CC users
    for (MachineInstr *CCUseInstr : CCUseInstrs) {
      int Idx = findCondCodeUseOperandIdxForBranchOrSelect(*CCUseInstr);
      assert(Idx >= 0 && "Unexpected instruction using CC.");
      MachineOperand &CCOperand = CCUseInstr->getOperand(Idx);
      AArch64CC::CondCode CCUse = AArch64CC::getInvertedCondCode(
          static_cast<AArch64CC::CondCode>(CCOperand.getImm()));
      CCOperand.setImm(CCUse);
    }
  }
  return true;
}

bool AArch64InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() != TargetOpcode::LOAD_STACK_GUARD &&
      MI.getOpcode() != AArch64::CATCHRET)
    return false;

  MachineBasicBlock &MBB = *MI.getParent();
  auto &Subtarget = MBB.getParent()->getSubtarget<AArch64Subtarget>();
  auto TRI = Subtarget.getRegisterInfo();
  DebugLoc DL = MI.getDebugLoc();

  if (MI.getOpcode() == AArch64::CATCHRET) {
    // Skip to the first instruction before the epilog.
    const TargetInstrInfo *TII =
      MBB.getParent()->getSubtarget().getInstrInfo();
    MachineBasicBlock *TargetMBB = MI.getOperand(0).getMBB();
    auto MBBI = MachineBasicBlock::iterator(MI);
    MachineBasicBlock::iterator FirstEpilogSEH = std::prev(MBBI);
    while (FirstEpilogSEH->getFlag(MachineInstr::FrameDestroy) &&
           FirstEpilogSEH != MBB.begin())
      FirstEpilogSEH = std::prev(FirstEpilogSEH);
    if (FirstEpilogSEH != MBB.begin())
      FirstEpilogSEH = std::next(FirstEpilogSEH);
    BuildMI(MBB, FirstEpilogSEH, DL, TII->get(AArch64::ADRP))
        .addReg(AArch64::X0, RegState::Define)
        .addMBB(TargetMBB);
    BuildMI(MBB, FirstEpilogSEH, DL, TII->get(AArch64::ADDXri))
        .addReg(AArch64::X0, RegState::Define)
        .addReg(AArch64::X0)
        .addMBB(TargetMBB)
        .addImm(0);
    return true;
  }

  Register Reg = MI.getOperand(0).getReg();
  Module &M = *MBB.getParent()->getFunction().getParent();
  if (M.getStackProtectorGuard() == "sysreg") {
    const AArch64SysReg::SysReg *SrcReg =
        AArch64SysReg::lookupSysRegByName(M.getStackProtectorGuardReg());
    if (!SrcReg)
      report_fatal_error("Unknown SysReg for Stack Protector Guard Register");

    // mrs xN, sysreg
    BuildMI(MBB, MI, DL, get(AArch64::MRS))
        .addDef(Reg, RegState::Renamable)
        .addImm(SrcReg->Encoding);
    int Offset = M.getStackProtectorGuardOffset();
    if (Offset >= 0 && Offset <= 32760 && Offset % 8 == 0) {
      // ldr xN, [xN, #offset]
      BuildMI(MBB, MI, DL, get(AArch64::LDRXui))
          .addDef(Reg)
          .addUse(Reg, RegState::Kill)
          .addImm(Offset / 8);
    } else if (Offset >= -256 && Offset <= 255) {
      // ldur xN, [xN, #offset]
      BuildMI(MBB, MI, DL, get(AArch64::LDURXi))
          .addDef(Reg)
          .addUse(Reg, RegState::Kill)
          .addImm(Offset);
    } else if (Offset >= -4095 && Offset <= 4095) {
      if (Offset > 0) {
        // add xN, xN, #offset
        BuildMI(MBB, MI, DL, get(AArch64::ADDXri))
            .addDef(Reg)
            .addUse(Reg, RegState::Kill)
            .addImm(Offset)
            .addImm(0);
      } else {
        // sub xN, xN, #offset
        BuildMI(MBB, MI, DL, get(AArch64::SUBXri))
            .addDef(Reg)
            .addUse(Reg, RegState::Kill)
            .addImm(-Offset)
            .addImm(0);
      }
      // ldr xN, [xN]
      BuildMI(MBB, MI, DL, get(AArch64::LDRXui))
          .addDef(Reg)
          .addUse(Reg, RegState::Kill)
          .addImm(0);
    } else {
      // Cases that are larger than +/- 4095 and not a multiple of 8, or larger
      // than 23760.
      // It might be nice to use AArch64::MOVi32imm here, which would get
      // expanded in PreSched2 after PostRA, but our lone scratch Reg already
      // contains the MRS result. findScratchNonCalleeSaveRegister() in
      // AArch64FrameLowering might help us find such a scratch register
      // though. If we failed to find a scratch register, we could emit a
      // stream of add instructions to build up the immediate. Or, we could try
      // to insert a AArch64::MOVi32imm before register allocation so that we
      // didn't need to scavenge for a scratch register.
      report_fatal_error("Unable to encode Stack Protector Guard Offset");
    }
    MBB.erase(MI);
    return true;
  }

  const GlobalValue *GV =
      cast<GlobalValue>((*MI.memoperands_begin())->getValue());
  const TargetMachine &TM = MBB.getParent()->getTarget();
  unsigned OpFlags = Subtarget.ClassifyGlobalReference(GV, TM);
  const unsigned char MO_NC = AArch64II::MO_NC;

  if ((OpFlags & AArch64II::MO_GOT) != 0) {
    BuildMI(MBB, MI, DL, get(AArch64::LOADgot), Reg)
        .addGlobalAddress(GV, 0, OpFlags);
    if (Subtarget.isTargetILP32()) {
      unsigned Reg32 = TRI->getSubReg(Reg, AArch64::sub_32);
      BuildMI(MBB, MI, DL, get(AArch64::LDRWui))
          .addDef(Reg32, RegState::Dead)
          .addUse(Reg, RegState::Kill)
          .addImm(0)
          .addMemOperand(*MI.memoperands_begin())
          .addDef(Reg, RegState::Implicit);
    } else {
      BuildMI(MBB, MI, DL, get(AArch64::LDRXui), Reg)
          .addReg(Reg, RegState::Kill)
          .addImm(0)
          .addMemOperand(*MI.memoperands_begin());
    }
  } else if (TM.getCodeModel() == CodeModel::Large) {
    assert(!Subtarget.isTargetILP32() && "how can large exist in ILP32?");
    BuildMI(MBB, MI, DL, get(AArch64::MOVZXi), Reg)
        .addGlobalAddress(GV, 0, AArch64II::MO_G0 | MO_NC)
        .addImm(0);
    BuildMI(MBB, MI, DL, get(AArch64::MOVKXi), Reg)
        .addReg(Reg, RegState::Kill)
        .addGlobalAddress(GV, 0, AArch64II::MO_G1 | MO_NC)
        .addImm(16);
    BuildMI(MBB, MI, DL, get(AArch64::MOVKXi), Reg)
        .addReg(Reg, RegState::Kill)
        .addGlobalAddress(GV, 0, AArch64II::MO_G2 | MO_NC)
        .addImm(32);
    BuildMI(MBB, MI, DL, get(AArch64::MOVKXi), Reg)
        .addReg(Reg, RegState::Kill)
        .addGlobalAddress(GV, 0, AArch64II::MO_G3)
        .addImm(48);
    BuildMI(MBB, MI, DL, get(AArch64::LDRXui), Reg)
        .addReg(Reg, RegState::Kill)
        .addImm(0)
        .addMemOperand(*MI.memoperands_begin());
  } else if (TM.getCodeModel() == CodeModel::Tiny) {
    BuildMI(MBB, MI, DL, get(AArch64::ADR), Reg)
        .addGlobalAddress(GV, 0, OpFlags);
  } else {
    BuildMI(MBB, MI, DL, get(AArch64::ADRP), Reg)
        .addGlobalAddress(GV, 0, OpFlags | AArch64II::MO_PAGE);
    unsigned char LoFlags = OpFlags | AArch64II::MO_PAGEOFF | MO_NC;
    if (Subtarget.isTargetILP32()) {
      unsigned Reg32 = TRI->getSubReg(Reg, AArch64::sub_32);
      BuildMI(MBB, MI, DL, get(AArch64::LDRWui))
          .addDef(Reg32, RegState::Dead)
          .addUse(Reg, RegState::Kill)
          .addGlobalAddress(GV, 0, LoFlags)
          .addMemOperand(*MI.memoperands_begin())
          .addDef(Reg, RegState::Implicit);
    } else {
      BuildMI(MBB, MI, DL, get(AArch64::LDRXui), Reg)
          .addReg(Reg, RegState::Kill)
          .addGlobalAddress(GV, 0, LoFlags)
          .addMemOperand(*MI.memoperands_begin());
    }
  }

  MBB.erase(MI);

  return true;
}

// Return true if this instruction simply sets its single destination register
// to zero. This is equivalent to a register rename of the zero-register.
bool AArch64InstrInfo::isGPRZero(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::MOVZWi:
  case AArch64::MOVZXi: // movz Rd, #0 (LSL #0)
    if (MI.getOperand(1).isImm() && MI.getOperand(1).getImm() == 0) {
      assert(MI.getDesc().getNumOperands() == 3 &&
             MI.getOperand(2).getImm() == 0 && "invalid MOVZi operands");
      return true;
    }
    break;
  case AArch64::ANDWri: // and Rd, Rzr, #imm
    return MI.getOperand(1).getReg() == AArch64::WZR;
  case AArch64::ANDXri:
    return MI.getOperand(1).getReg() == AArch64::XZR;
  case TargetOpcode::COPY:
    return MI.getOperand(1).getReg() == AArch64::WZR;
  }
  return false;
}

// Return true if this instruction simply renames a general register without
// modifying bits.
bool AArch64InstrInfo::isGPRCopy(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    break;
  case TargetOpcode::COPY: {
    // GPR32 copies will by lowered to ORRXrs
    Register DstReg = MI.getOperand(0).getReg();
    return (AArch64::GPR32RegClass.contains(DstReg) ||
            AArch64::GPR64RegClass.contains(DstReg));
  }
  case AArch64::ORRXrs: // orr Xd, Xzr, Xm (LSL #0)
    if (MI.getOperand(1).getReg() == AArch64::XZR) {
      assert(MI.getDesc().getNumOperands() == 4 &&
             MI.getOperand(3).getImm() == 0 && "invalid ORRrs operands");
      return true;
    }
    break;
  case AArch64::ADDXri: // add Xd, Xn, #0 (LSL #0)
    if (MI.getOperand(2).getImm() == 0) {
      assert(MI.getDesc().getNumOperands() == 4 &&
             MI.getOperand(3).getImm() == 0 && "invalid ADDXri operands");
      return true;
    }
    break;
  }
  return false;
}

// Return true if this instruction simply renames a general register without
// modifying bits.
bool AArch64InstrInfo::isFPRCopy(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    break;
  case TargetOpcode::COPY: {
    Register DstReg = MI.getOperand(0).getReg();
    return AArch64::FPR128RegClass.contains(DstReg);
  }
  case AArch64::ORRv16i8:
    if (MI.getOperand(1).getReg() == MI.getOperand(2).getReg()) {
      assert(MI.getDesc().getNumOperands() == 3 && MI.getOperand(0).isReg() &&
             "invalid ORRv16i8 operands");
      return true;
    }
    break;
  }
  return false;
}

Register AArch64InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                               int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::LDRWui:
  case AArch64::LDRXui:
  case AArch64::LDRBui:
  case AArch64::LDRHui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:
  case AArch64::LDR_PXI:
    if (MI.getOperand(0).getSubReg() == 0 && MI.getOperand(1).isFI() &&
        MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }

  return 0;
}

Register AArch64InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                              int &FrameIndex) const {
  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::STRWui:
  case AArch64::STRXui:
  case AArch64::STRBui:
  case AArch64::STRHui:
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STRQui:
  case AArch64::STR_PXI:
    if (MI.getOperand(0).getSubReg() == 0 && MI.getOperand(1).isFI() &&
        MI.getOperand(2).isImm() && MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

/// Check all MachineMemOperands for a hint to suppress pairing.
bool AArch64InstrInfo::isLdStPairSuppressed(const MachineInstr &MI) {
  return llvm::any_of(MI.memoperands(), [](MachineMemOperand *MMO) {
    return MMO->getFlags() & MOSuppressPair;
  });
}

/// Set a flag on the first MachineMemOperand to suppress pairing.
void AArch64InstrInfo::suppressLdStPair(MachineInstr &MI) {
  if (MI.memoperands_empty())
    return;
  (*MI.memoperands_begin())->setFlags(MOSuppressPair);
}

/// Check all MachineMemOperands for a hint that the load/store is strided.
bool AArch64InstrInfo::isStridedAccess(const MachineInstr &MI) {
  return llvm::any_of(MI.memoperands(), [](MachineMemOperand *MMO) {
    return MMO->getFlags() & MOStridedAccess;
  });
}

bool AArch64InstrInfo::hasUnscaledLdStOffset(unsigned Opc) {
  switch (Opc) {
  default:
    return false;
  case AArch64::STURSi:
  case AArch64::STRSpre:
  case AArch64::STURDi:
  case AArch64::STRDpre:
  case AArch64::STURQi:
  case AArch64::STRQpre:
  case AArch64::STURBBi:
  case AArch64::STURHHi:
  case AArch64::STURWi:
  case AArch64::STRWpre:
  case AArch64::STURXi:
  case AArch64::STRXpre:
  case AArch64::LDURSi:
  case AArch64::LDRSpre:
  case AArch64::LDURDi:
  case AArch64::LDRDpre:
  case AArch64::LDURQi:
  case AArch64::LDRQpre:
  case AArch64::LDURWi:
  case AArch64::LDRWpre:
  case AArch64::LDURXi:
  case AArch64::LDRXpre:
  case AArch64::LDRSWpre:
  case AArch64::LDURSWi:
  case AArch64::LDURHHi:
  case AArch64::LDURBBi:
  case AArch64::LDURSBWi:
  case AArch64::LDURSHWi:
    return true;
  }
}

std::optional<unsigned> AArch64InstrInfo::getUnscaledLdSt(unsigned Opc) {
  switch (Opc) {
  default: return {};
  case AArch64::PRFMui: return AArch64::PRFUMi;
  case AArch64::LDRXui: return AArch64::LDURXi;
  case AArch64::LDRWui: return AArch64::LDURWi;
  case AArch64::LDRBui: return AArch64::LDURBi;
  case AArch64::LDRHui: return AArch64::LDURHi;
  case AArch64::LDRSui: return AArch64::LDURSi;
  case AArch64::LDRDui: return AArch64::LDURDi;
  case AArch64::LDRQui: return AArch64::LDURQi;
  case AArch64::LDRBBui: return AArch64::LDURBBi;
  case AArch64::LDRHHui: return AArch64::LDURHHi;
  case AArch64::LDRSBXui: return AArch64::LDURSBXi;
  case AArch64::LDRSBWui: return AArch64::LDURSBWi;
  case AArch64::LDRSHXui: return AArch64::LDURSHXi;
  case AArch64::LDRSHWui: return AArch64::LDURSHWi;
  case AArch64::LDRSWui: return AArch64::LDURSWi;
  case AArch64::STRXui: return AArch64::STURXi;
  case AArch64::STRWui: return AArch64::STURWi;
  case AArch64::STRBui: return AArch64::STURBi;
  case AArch64::STRHui: return AArch64::STURHi;
  case AArch64::STRSui: return AArch64::STURSi;
  case AArch64::STRDui: return AArch64::STURDi;
  case AArch64::STRQui: return AArch64::STURQi;
  case AArch64::STRBBui: return AArch64::STURBBi;
  case AArch64::STRHHui: return AArch64::STURHHi;
  }
}

unsigned AArch64InstrInfo::getLoadStoreImmIdx(unsigned Opc) {
  switch (Opc) {
  default:
    return 2;
  case AArch64::LDPXi:
  case AArch64::LDPDi:
  case AArch64::STPXi:
  case AArch64::STPDi:
  case AArch64::LDNPXi:
  case AArch64::LDNPDi:
  case AArch64::STNPXi:
  case AArch64::STNPDi:
  case AArch64::LDPQi:
  case AArch64::STPQi:
  case AArch64::LDNPQi:
  case AArch64::STNPQi:
  case AArch64::LDPWi:
  case AArch64::LDPSi:
  case AArch64::STPWi:
  case AArch64::STPSi:
  case AArch64::LDNPWi:
  case AArch64::LDNPSi:
  case AArch64::STNPWi:
  case AArch64::STNPSi:
  case AArch64::LDG:
  case AArch64::STGPi:

  case AArch64::LD1B_IMM:
  case AArch64::LD1B_H_IMM:
  case AArch64::LD1B_S_IMM:
  case AArch64::LD1B_D_IMM:
  case AArch64::LD1SB_H_IMM:
  case AArch64::LD1SB_S_IMM:
  case AArch64::LD1SB_D_IMM:
  case AArch64::LD1H_IMM:
  case AArch64::LD1H_S_IMM:
  case AArch64::LD1H_D_IMM:
  case AArch64::LD1SH_S_IMM:
  case AArch64::LD1SH_D_IMM:
  case AArch64::LD1W_IMM:
  case AArch64::LD1W_D_IMM:
  case AArch64::LD1SW_D_IMM:
  case AArch64::LD1D_IMM:

  case AArch64::LD2B_IMM:
  case AArch64::LD2H_IMM:
  case AArch64::LD2W_IMM:
  case AArch64::LD2D_IMM:
  case AArch64::LD3B_IMM:
  case AArch64::LD3H_IMM:
  case AArch64::LD3W_IMM:
  case AArch64::LD3D_IMM:
  case AArch64::LD4B_IMM:
  case AArch64::LD4H_IMM:
  case AArch64::LD4W_IMM:
  case AArch64::LD4D_IMM:

  case AArch64::ST1B_IMM:
  case AArch64::ST1B_H_IMM:
  case AArch64::ST1B_S_IMM:
  case AArch64::ST1B_D_IMM:
  case AArch64::ST1H_IMM:
  case AArch64::ST1H_S_IMM:
  case AArch64::ST1H_D_IMM:
  case AArch64::ST1W_IMM:
  case AArch64::ST1W_D_IMM:
  case AArch64::ST1D_IMM:

  case AArch64::ST2B_IMM:
  case AArch64::ST2H_IMM:
  case AArch64::ST2W_IMM:
  case AArch64::ST2D_IMM:
  case AArch64::ST3B_IMM:
  case AArch64::ST3H_IMM:
  case AArch64::ST3W_IMM:
  case AArch64::ST3D_IMM:
  case AArch64::ST4B_IMM:
  case AArch64::ST4H_IMM:
  case AArch64::ST4W_IMM:
  case AArch64::ST4D_IMM:

  case AArch64::LD1RB_IMM:
  case AArch64::LD1RB_H_IMM:
  case AArch64::LD1RB_S_IMM:
  case AArch64::LD1RB_D_IMM:
  case AArch64::LD1RSB_H_IMM:
  case AArch64::LD1RSB_S_IMM:
  case AArch64::LD1RSB_D_IMM:
  case AArch64::LD1RH_IMM:
  case AArch64::LD1RH_S_IMM:
  case AArch64::LD1RH_D_IMM:
  case AArch64::LD1RSH_S_IMM:
  case AArch64::LD1RSH_D_IMM:
  case AArch64::LD1RW_IMM:
  case AArch64::LD1RW_D_IMM:
  case AArch64::LD1RSW_IMM:
  case AArch64::LD1RD_IMM:

  case AArch64::LDNT1B_ZRI:
  case AArch64::LDNT1H_ZRI:
  case AArch64::LDNT1W_ZRI:
  case AArch64::LDNT1D_ZRI:
  case AArch64::STNT1B_ZRI:
  case AArch64::STNT1H_ZRI:
  case AArch64::STNT1W_ZRI:
  case AArch64::STNT1D_ZRI:

  case AArch64::LDNF1B_IMM:
  case AArch64::LDNF1B_H_IMM:
  case AArch64::LDNF1B_S_IMM:
  case AArch64::LDNF1B_D_IMM:
  case AArch64::LDNF1SB_H_IMM:
  case AArch64::LDNF1SB_S_IMM:
  case AArch64::LDNF1SB_D_IMM:
  case AArch64::LDNF1H_IMM:
  case AArch64::LDNF1H_S_IMM:
  case AArch64::LDNF1H_D_IMM:
  case AArch64::LDNF1SH_S_IMM:
  case AArch64::LDNF1SH_D_IMM:
  case AArch64::LDNF1W_IMM:
  case AArch64::LDNF1W_D_IMM:
  case AArch64::LDNF1SW_D_IMM:
  case AArch64::LDNF1D_IMM:
    return 3;
  case AArch64::ADDG:
  case AArch64::STGi:
  case AArch64::LDR_PXI:
  case AArch64::STR_PXI:
    return 2;
  }
}

bool AArch64InstrInfo::isPairableLdStInst(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  // Scaled instructions.
  case AArch64::STRSui:
  case AArch64::STRDui:
  case AArch64::STRQui:
  case AArch64::STRXui:
  case AArch64::STRWui:
  case AArch64::LDRSui:
  case AArch64::LDRDui:
  case AArch64::LDRQui:
  case AArch64::LDRXui:
  case AArch64::LDRWui:
  case AArch64::LDRSWui:
  // Unscaled instructions.
  case AArch64::STURSi:
  case AArch64::STRSpre:
  case AArch64::STURDi:
  case AArch64::STRDpre:
  case AArch64::STURQi:
  case AArch64::STRQpre:
  case AArch64::STURWi:
  case AArch64::STRWpre:
  case AArch64::STURXi:
  case AArch64::STRXpre:
  case AArch64::LDURSi:
  case AArch64::LDRSpre:
  case AArch64::LDURDi:
  case AArch64::LDRDpre:
  case AArch64::LDURQi:
  case AArch64::LDRQpre:
  case AArch64::LDURWi:
  case AArch64::LDRWpre:
  case AArch64::LDURXi:
  case AArch64::LDRXpre:
  case AArch64::LDURSWi:
  case AArch64::LDRSWpre:
    return true;
  }
}

bool AArch64InstrInfo::isTailCallReturnInst(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    assert((!MI.isCall() || !MI.isReturn()) &&
           "Unexpected instruction - was a new tail call opcode introduced?");
    return false;
  case AArch64::TCRETURNdi:
  case AArch64::TCRETURNri:
  case AArch64::TCRETURNrix16x17:
  case AArch64::TCRETURNrix17:
  case AArch64::TCRETURNrinotx16:
  case AArch64::TCRETURNriALL:
  case AArch64::AUTH_TCRETURN:
  case AArch64::AUTH_TCRETURN_BTI:
    return true;
  }
}

unsigned AArch64InstrInfo::convertToFlagSettingOpc(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has no flag setting equivalent!");
  // 32-bit cases:
  case AArch64::ADDWri:
    return AArch64::ADDSWri;
  case AArch64::ADDWrr:
    return AArch64::ADDSWrr;
  case AArch64::ADDWrs:
    return AArch64::ADDSWrs;
  case AArch64::ADDWrx:
    return AArch64::ADDSWrx;
  case AArch64::ANDWri:
    return AArch64::ANDSWri;
  case AArch64::ANDWrr:
    return AArch64::ANDSWrr;
  case AArch64::ANDWrs:
    return AArch64::ANDSWrs;
  case AArch64::BICWrr:
    return AArch64::BICSWrr;
  case AArch64::BICWrs:
    return AArch64::BICSWrs;
  case AArch64::SUBWri:
    return AArch64::SUBSWri;
  case AArch64::SUBWrr:
    return AArch64::SUBSWrr;
  case AArch64::SUBWrs:
    return AArch64::SUBSWrs;
  case AArch64::SUBWrx:
    return AArch64::SUBSWrx;
  // 64-bit cases:
  case AArch64::ADDXri:
    return AArch64::ADDSXri;
  case AArch64::ADDXrr:
    return AArch64::ADDSXrr;
  case AArch64::ADDXrs:
    return AArch64::ADDSXrs;
  case AArch64::ADDXrx:
    return AArch64::ADDSXrx;
  case AArch64::ANDXri:
    return AArch64::ANDSXri;
  case AArch64::ANDXrr:
    return AArch64::ANDSXrr;
  case AArch64::ANDXrs:
    return AArch64::ANDSXrs;
  case AArch64::BICXrr:
    return AArch64::BICSXrr;
  case AArch64::BICXrs:
    return AArch64::BICSXrs;
  case AArch64::SUBXri:
    return AArch64::SUBSXri;
  case AArch64::SUBXrr:
    return AArch64::SUBSXrr;
  case AArch64::SUBXrs:
    return AArch64::SUBSXrs;
  case AArch64::SUBXrx:
    return AArch64::SUBSXrx;
  // SVE instructions:
  case AArch64::AND_PPzPP:
    return AArch64::ANDS_PPzPP;
  case AArch64::BIC_PPzPP:
    return AArch64::BICS_PPzPP;
  case AArch64::EOR_PPzPP:
    return AArch64::EORS_PPzPP;
  case AArch64::NAND_PPzPP:
    return AArch64::NANDS_PPzPP;
  case AArch64::NOR_PPzPP:
    return AArch64::NORS_PPzPP;
  case AArch64::ORN_PPzPP:
    return AArch64::ORNS_PPzPP;
  case AArch64::ORR_PPzPP:
    return AArch64::ORRS_PPzPP;
  case AArch64::BRKA_PPzP:
    return AArch64::BRKAS_PPzP;
  case AArch64::BRKPA_PPzPP:
    return AArch64::BRKPAS_PPzPP;
  case AArch64::BRKB_PPzP:
    return AArch64::BRKBS_PPzP;
  case AArch64::BRKPB_PPzPP:
    return AArch64::BRKPBS_PPzPP;
  case AArch64::BRKN_PPzP:
    return AArch64::BRKNS_PPzP;
  case AArch64::RDFFR_PPz:
    return AArch64::RDFFRS_PPz;
  case AArch64::PTRUE_B:
    return AArch64::PTRUES_B;
  }
}

// Is this a candidate for ld/st merging or pairing?  For example, we don't
// touch volatiles or load/stores that have a hint to avoid pair formation.
bool AArch64InstrInfo::isCandidateToMergeOrPair(const MachineInstr &MI) const {

  bool IsPreLdSt = isPreLdSt(MI);

  // If this is a volatile load/store, don't mess with it.
  if (MI.hasOrderedMemoryRef())
    return false;

  // Make sure this is a reg/fi+imm (as opposed to an address reloc).
  // For Pre-inc LD/ST, the operand is shifted by one.
  assert((MI.getOperand(IsPreLdSt ? 2 : 1).isReg() ||
          MI.getOperand(IsPreLdSt ? 2 : 1).isFI()) &&
         "Expected a reg or frame index operand.");

  // For Pre-indexed addressing quadword instructions, the third operand is the
  // immediate value.
  bool IsImmPreLdSt = IsPreLdSt && MI.getOperand(3).isImm();

  if (!MI.getOperand(2).isImm() && !IsImmPreLdSt)
    return false;

  // Can't merge/pair if the instruction modifies the base register.
  // e.g., ldr x0, [x0]
  // This case will never occur with an FI base.
  // However, if the instruction is an LDR<S,D,Q,W,X,SW>pre or
  // STR<S,D,Q,W,X>pre, it can be merged.
  // For example:
  //   ldr q0, [x11, #32]!
  //   ldr q1, [x11, #16]
  //   to
  //   ldp q0, q1, [x11, #32]!
  if (MI.getOperand(1).isReg() && !IsPreLdSt) {
    Register BaseReg = MI.getOperand(1).getReg();
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    if (MI.modifiesRegister(BaseReg, TRI))
      return false;
  }

  // Check if this load/store has a hint to avoid pair formation.
  // MachineMemOperands hints are set by the AArch64StorePairSuppress pass.
  if (isLdStPairSuppressed(MI))
    return false;

  // Do not pair any callee-save store/reload instructions in the
  // prologue/epilogue if the CFI information encoded the operations as separate
  // instructions, as that will cause the size of the actual prologue to mismatch
  // with the prologue size recorded in the Windows CFI.
  const MCAsmInfo *MAI = MI.getMF()->getTarget().getMCAsmInfo();
  bool NeedsWinCFI = MAI->usesWindowsCFI() &&
                     MI.getMF()->getFunction().needsUnwindTableEntry();
  if (NeedsWinCFI && (MI.getFlag(MachineInstr::FrameSetup) ||
                      MI.getFlag(MachineInstr::FrameDestroy)))
    return false;

  // On some CPUs quad load/store pairs are slower than two single load/stores.
  if (Subtarget.isPaired128Slow()) {
    switch (MI.getOpcode()) {
    default:
      break;
    case AArch64::LDURQi:
    case AArch64::STURQi:
    case AArch64::LDRQui:
    case AArch64::STRQui:
      return false;
    }
  }

  return true;
}

bool AArch64InstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &LdSt, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *TRI) const {
  if (!LdSt.mayLoadOrStore())
    return false;

  const MachineOperand *BaseOp;
  TypeSize WidthN(0, false);
  if (!getMemOperandWithOffsetWidth(LdSt, BaseOp, Offset, OffsetIsScalable,
                                    WidthN, TRI))
    return false;
  // The maximum vscale is 16 under AArch64, return the maximal extent for the
  // vector.
  Width = LocationSize::precise(WidthN);
  BaseOps.push_back(BaseOp);
  return true;
}

std::optional<ExtAddrMode>
AArch64InstrInfo::getAddrModeFromMemoryOp(const MachineInstr &MemI,
                                          const TargetRegisterInfo *TRI) const {
  const MachineOperand *Base; // Filled with the base operand of MI.
  int64_t Offset;             // Filled with the offset of MI.
  bool OffsetIsScalable;
  if (!getMemOperandWithOffset(MemI, Base, Offset, OffsetIsScalable, TRI))
    return std::nullopt;

  if (!Base->isReg())
    return std::nullopt;
  ExtAddrMode AM;
  AM.BaseReg = Base->getReg();
  AM.Displacement = Offset;
  AM.ScaledReg = 0;
  AM.Scale = 0;
  return AM;
}

bool AArch64InstrInfo::canFoldIntoAddrMode(const MachineInstr &MemI,
                                           Register Reg,
                                           const MachineInstr &AddrI,
                                           ExtAddrMode &AM) const {
  // Filter out instructions into which we cannot fold.
  unsigned NumBytes;
  int64_t OffsetScale = 1;
  switch (MemI.getOpcode()) {
  default:
    return false;

  case AArch64::LDURQi:
  case AArch64::STURQi:
    NumBytes = 16;
    break;

  case AArch64::LDURDi:
  case AArch64::STURDi:
  case AArch64::LDURXi:
  case AArch64::STURXi:
    NumBytes = 8;
    break;

  case AArch64::LDURWi:
  case AArch64::LDURSWi:
  case AArch64::STURWi:
    NumBytes = 4;
    break;

  case AArch64::LDURHi:
  case AArch64::STURHi:
  case AArch64::LDURHHi:
  case AArch64::STURHHi:
  case AArch64::LDURSHXi:
  case AArch64::LDURSHWi:
    NumBytes = 2;
    break;

  case AArch64::LDRBroX:
  case AArch64::LDRBBroX:
  case AArch64::LDRSBXroX:
  case AArch64::LDRSBWroX:
  case AArch64::STRBroX:
  case AArch64::STRBBroX:
  case AArch64::LDURBi:
  case AArch64::LDURBBi:
  case AArch64::LDURSBXi:
  case AArch64::LDURSBWi:
  case AArch64::STURBi:
  case AArch64::STURBBi:
  case AArch64::LDRBui:
  case AArch64::LDRBBui:
  case AArch64::LDRSBXui:
  case AArch64::LDRSBWui:
  case AArch64::STRBui:
  case AArch64::STRBBui:
    NumBytes = 1;
    break;

  case AArch64::LDRQroX:
  case AArch64::STRQroX:
  case AArch64::LDRQui:
  case AArch64::STRQui:
    NumBytes = 16;
    OffsetScale = 16;
    break;

  case AArch64::LDRDroX:
  case AArch64::STRDroX:
  case AArch64::LDRXroX:
  case AArch64::STRXroX:
  case AArch64::LDRDui:
  case AArch64::STRDui:
  case AArch64::LDRXui:
  case AArch64::STRXui:
    NumBytes = 8;
    OffsetScale = 8;
    break;

  case AArch64::LDRWroX:
  case AArch64::LDRSWroX:
  case AArch64::STRWroX:
  case AArch64::LDRWui:
  case AArch64::LDRSWui:
  case AArch64::STRWui:
    NumBytes = 4;
    OffsetScale = 4;
    break;

  case AArch64::LDRHroX:
  case AArch64::STRHroX:
  case AArch64::LDRHHroX:
  case AArch64::STRHHroX:
  case AArch64::LDRSHXroX:
  case AArch64::LDRSHWroX:
  case AArch64::LDRHui:
  case AArch64::STRHui:
  case AArch64::LDRHHui:
  case AArch64::STRHHui:
  case AArch64::LDRSHXui:
  case AArch64::LDRSHWui:
    NumBytes = 2;
    OffsetScale = 2;
    break;
  }

  // Check the fold operand is not the loaded/stored value.
  const MachineOperand &BaseRegOp = MemI.getOperand(0);
  if (BaseRegOp.isReg() && BaseRegOp.getReg() == Reg)
    return false;

  // Handle memory instructions with a [Reg, Reg] addressing mode.
  if (MemI.getOperand(2).isReg()) {
    // Bail if the addressing mode already includes extension of the offset
    // register.
    if (MemI.getOperand(3).getImm())
      return false;

    // Check if we actually have a scaled offset.
    if (MemI.getOperand(4).getImm() == 0)
      OffsetScale = 1;

    // If the address instructions is folded into the base register, then the
    // addressing mode must not have a scale. Then we can swap the base and the
    // scaled registers.
    if (MemI.getOperand(1).getReg() == Reg && OffsetScale != 1)
      return false;

    switch (AddrI.getOpcode()) {
    default:
      return false;

    case AArch64::SBFMXri:
      // sxtw Xa, Wm
      // ldr Xd, [Xn, Xa, lsl #N]
      // ->
      // ldr Xd, [Xn, Wm, sxtw #N]
      if (AddrI.getOperand(2).getImm() != 0 ||
          AddrI.getOperand(3).getImm() != 31)
        return false;

      AM.BaseReg = MemI.getOperand(1).getReg();
      if (AM.BaseReg == Reg)
        AM.BaseReg = MemI.getOperand(2).getReg();
      AM.ScaledReg = AddrI.getOperand(1).getReg();
      AM.Scale = OffsetScale;
      AM.Displacement = 0;
      AM.Form = ExtAddrMode::Formula::SExtScaledReg;
      return true;

    case TargetOpcode::SUBREG_TO_REG: {
      // mov Wa, Wm
      // ldr Xd, [Xn, Xa, lsl #N]
      // ->
      // ldr Xd, [Xn, Wm, uxtw #N]

      // Zero-extension looks like an ORRWrs followed by a SUBREG_TO_REG.
      if (AddrI.getOperand(1).getImm() != 0 ||
          AddrI.getOperand(3).getImm() != AArch64::sub_32)
        return false;

      const MachineRegisterInfo &MRI = AddrI.getMF()->getRegInfo();
      Register OffsetReg = AddrI.getOperand(2).getReg();
      if (!OffsetReg.isVirtual() || !MRI.hasOneNonDBGUse(OffsetReg))
        return false;

      const MachineInstr &DefMI = *MRI.getVRegDef(OffsetReg);
      if (DefMI.getOpcode() != AArch64::ORRWrs ||
          DefMI.getOperand(1).getReg() != AArch64::WZR ||
          DefMI.getOperand(3).getImm() != 0)
        return false;

      AM.BaseReg = MemI.getOperand(1).getReg();
      if (AM.BaseReg == Reg)
        AM.BaseReg = MemI.getOperand(2).getReg();
      AM.ScaledReg = DefMI.getOperand(2).getReg();
      AM.Scale = OffsetScale;
      AM.Displacement = 0;
      AM.Form = ExtAddrMode::Formula::ZExtScaledReg;
      return true;
    }
    }
  }

  // Handle memory instructions with a [Reg, #Imm] addressing mode.

  // Check we are not breaking a potential conversion to an LDP.
  auto validateOffsetForLDP = [](unsigned NumBytes, int64_t OldOffset,
                                 int64_t NewOffset) -> bool {
    int64_t MinOffset, MaxOffset;
    switch (NumBytes) {
    default:
      return true;
    case 4:
      MinOffset = -256;
      MaxOffset = 252;
      break;
    case 8:
      MinOffset = -512;
      MaxOffset = 504;
      break;
    case 16:
      MinOffset = -1024;
      MaxOffset = 1008;
      break;
    }
    return OldOffset < MinOffset || OldOffset > MaxOffset ||
           (NewOffset >= MinOffset && NewOffset <= MaxOffset);
  };
  auto canFoldAddSubImmIntoAddrMode = [&](int64_t Disp) -> bool {
    int64_t OldOffset = MemI.getOperand(2).getImm() * OffsetScale;
    int64_t NewOffset = OldOffset + Disp;
    if (!isLegalAddressingMode(NumBytes, NewOffset, /* Scale */ 0))
      return false;
    // If the old offset would fit into an LDP, but the new offset wouldn't,
    // bail out.
    if (!validateOffsetForLDP(NumBytes, OldOffset, NewOffset))
      return false;
    AM.BaseReg = AddrI.getOperand(1).getReg();
    AM.ScaledReg = 0;
    AM.Scale = 0;
    AM.Displacement = NewOffset;
    AM.Form = ExtAddrMode::Formula::Basic;
    return true;
  };

  auto canFoldAddRegIntoAddrMode =
      [&](int64_t Scale,
          ExtAddrMode::Formula Form = ExtAddrMode::Formula::Basic) -> bool {
    if (MemI.getOperand(2).getImm() != 0)
      return false;
    if (!isLegalAddressingMode(NumBytes, /* Offset */ 0, Scale))
      return false;
    AM.BaseReg = AddrI.getOperand(1).getReg();
    AM.ScaledReg = AddrI.getOperand(2).getReg();
    AM.Scale = Scale;
    AM.Displacement = 0;
    AM.Form = Form;
    return true;
  };

  auto avoidSlowSTRQ = [&](const MachineInstr &MemI) {
    unsigned Opcode = MemI.getOpcode();
    return (Opcode == AArch64::STURQi || Opcode == AArch64::STRQui) &&
           Subtarget.isSTRQroSlow();
  };

  int64_t Disp = 0;
  const bool OptSize = MemI.getMF()->getFunction().hasOptSize();
  switch (AddrI.getOpcode()) {
  default:
    return false;

  case AArch64::ADDXri:
    // add Xa, Xn, #N
    // ldr Xd, [Xa, #M]
    // ->
    // ldr Xd, [Xn, #N'+M]
    Disp = AddrI.getOperand(2).getImm() << AddrI.getOperand(3).getImm();
    return canFoldAddSubImmIntoAddrMode(Disp);

  case AArch64::SUBXri:
    // sub Xa, Xn, #N
    // ldr Xd, [Xa, #M]
    // ->
    // ldr Xd, [Xn, #N'+M]
    Disp = AddrI.getOperand(2).getImm() << AddrI.getOperand(3).getImm();
    return canFoldAddSubImmIntoAddrMode(-Disp);

  case AArch64::ADDXrs: {
    // add Xa, Xn, Xm, lsl #N
    // ldr Xd, [Xa]
    // ->
    // ldr Xd, [Xn, Xm, lsl #N]

    // Don't fold the add if the result would be slower, unless optimising for
    // size.
    unsigned Shift = static_cast<unsigned>(AddrI.getOperand(3).getImm());
    if (AArch64_AM::getShiftType(Shift) != AArch64_AM::ShiftExtendType::LSL)
      return false;
    Shift = AArch64_AM::getShiftValue(Shift);
    if (!OptSize) {
      if (Shift != 2 && Shift != 3 && Subtarget.hasAddrLSLSlow14())
        return false;
      if (avoidSlowSTRQ(MemI))
        return false;
    }
    return canFoldAddRegIntoAddrMode(1ULL << Shift);
  }

  case AArch64::ADDXrr:
    // add Xa, Xn, Xm
    // ldr Xd, [Xa]
    // ->
    // ldr Xd, [Xn, Xm, lsl #0]

    // Don't fold the add if the result would be slower, unless optimising for
    // size.
    if (!OptSize && avoidSlowSTRQ(MemI))
      return false;
    return canFoldAddRegIntoAddrMode(1);

  case AArch64::ADDXrx:
    // add Xa, Xn, Wm, {s,u}xtw #N
    // ldr Xd, [Xa]
    // ->
    // ldr Xd, [Xn, Wm, {s,u}xtw #N]

    // Don't fold the add if the result would be slower, unless optimising for
    // size.
    if (!OptSize && avoidSlowSTRQ(MemI))
      return false;

    // Can fold only sign-/zero-extend of a word.
    unsigned Imm = static_cast<unsigned>(AddrI.getOperand(3).getImm());
    AArch64_AM::ShiftExtendType Extend = AArch64_AM::getArithExtendType(Imm);
    if (Extend != AArch64_AM::UXTW && Extend != AArch64_AM::SXTW)
      return false;

    return canFoldAddRegIntoAddrMode(
        1ULL << AArch64_AM::getArithShiftValue(Imm),
        (Extend == AArch64_AM::SXTW) ? ExtAddrMode::Formula::SExtScaledReg
                                     : ExtAddrMode::Formula::ZExtScaledReg);
  }
}

// Given an opcode for an instruction with a [Reg, #Imm] addressing mode,
// return the opcode of an instruction performing the same operation, but using
// the [Reg, Reg] addressing mode.
static unsigned regOffsetOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Address folding not implemented for instruction");

  case AArch64::LDURQi:
  case AArch64::LDRQui:
    return AArch64::LDRQroX;
  case AArch64::STURQi:
  case AArch64::STRQui:
    return AArch64::STRQroX;
  case AArch64::LDURDi:
  case AArch64::LDRDui:
    return AArch64::LDRDroX;
  case AArch64::STURDi:
  case AArch64::STRDui:
    return AArch64::STRDroX;
  case AArch64::LDURXi:
  case AArch64::LDRXui:
    return AArch64::LDRXroX;
  case AArch64::STURXi:
  case AArch64::STRXui:
    return AArch64::STRXroX;
  case AArch64::LDURWi:
  case AArch64::LDRWui:
    return AArch64::LDRWroX;
  case AArch64::LDURSWi:
  case AArch64::LDRSWui:
    return AArch64::LDRSWroX;
  case AArch64::STURWi:
  case AArch64::STRWui:
    return AArch64::STRWroX;
  case AArch64::LDURHi:
  case AArch64::LDRHui:
    return AArch64::LDRHroX;
  case AArch64::STURHi:
  case AArch64::STRHui:
    return AArch64::STRHroX;
  case AArch64::LDURHHi:
  case AArch64::LDRHHui:
    return AArch64::LDRHHroX;
  case AArch64::STURHHi:
  case AArch64::STRHHui:
    return AArch64::STRHHroX;
  case AArch64::LDURSHXi:
  case AArch64::LDRSHXui:
    return AArch64::LDRSHXroX;
  case AArch64::LDURSHWi:
  case AArch64::LDRSHWui:
    return AArch64::LDRSHWroX;
  case AArch64::LDURBi:
  case AArch64::LDRBui:
    return AArch64::LDRBroX;
  case AArch64::LDURBBi:
  case AArch64::LDRBBui:
    return AArch64::LDRBBroX;
  case AArch64::LDURSBXi:
  case AArch64::LDRSBXui:
    return AArch64::LDRSBXroX;
  case AArch64::LDURSBWi:
  case AArch64::LDRSBWui:
    return AArch64::LDRSBWroX;
  case AArch64::STURBi:
  case AArch64::STRBui:
    return AArch64::STRBroX;
  case AArch64::STURBBi:
  case AArch64::STRBBui:
    return AArch64::STRBBroX;
  }
}

// Given an opcode for an instruction with a [Reg, #Imm] addressing mode, return
// the opcode of an instruction performing the same operation, but using the
// [Reg, #Imm] addressing mode with scaled offset.
unsigned scaledOffsetOpcode(unsigned Opcode, unsigned &Scale) {
  switch (Opcode) {
  default:
    llvm_unreachable("Address folding not implemented for instruction");

  case AArch64::LDURQi:
    Scale = 16;
    return AArch64::LDRQui;
  case AArch64::STURQi:
    Scale = 16;
    return AArch64::STRQui;
  case AArch64::LDURDi:
    Scale = 8;
    return AArch64::LDRDui;
  case AArch64::STURDi:
    Scale = 8;
    return AArch64::STRDui;
  case AArch64::LDURXi:
    Scale = 8;
    return AArch64::LDRXui;
  case AArch64::STURXi:
    Scale = 8;
    return AArch64::STRXui;
  case AArch64::LDURWi:
    Scale = 4;
    return AArch64::LDRWui;
  case AArch64::LDURSWi:
    Scale = 4;
    return AArch64::LDRSWui;
  case AArch64::STURWi:
    Scale = 4;
    return AArch64::STRWui;
  case AArch64::LDURHi:
    Scale = 2;
    return AArch64::LDRHui;
  case AArch64::STURHi:
    Scale = 2;
    return AArch64::STRHui;
  case AArch64::LDURHHi:
    Scale = 2;
    return AArch64::LDRHHui;
  case AArch64::STURHHi:
    Scale = 2;
    return AArch64::STRHHui;
  case AArch64::LDURSHXi:
    Scale = 2;
    return AArch64::LDRSHXui;
  case AArch64::LDURSHWi:
    Scale = 2;
    return AArch64::LDRSHWui;
  case AArch64::LDURBi:
    Scale = 1;
    return AArch64::LDRBui;
  case AArch64::LDURBBi:
    Scale = 1;
    return AArch64::LDRBBui;
  case AArch64::LDURSBXi:
    Scale = 1;
    return AArch64::LDRSBXui;
  case AArch64::LDURSBWi:
    Scale = 1;
    return AArch64::LDRSBWui;
  case AArch64::STURBi:
    Scale = 1;
    return AArch64::STRBui;
  case AArch64::STURBBi:
    Scale = 1;
    return AArch64::STRBBui;
  case AArch64::LDRQui:
  case AArch64::STRQui:
    Scale = 16;
    return Opcode;
  case AArch64::LDRDui:
  case AArch64::STRDui:
  case AArch64::LDRXui:
  case AArch64::STRXui:
    Scale = 8;
    return Opcode;
  case AArch64::LDRWui:
  case AArch64::LDRSWui:
  case AArch64::STRWui:
    Scale = 4;
    return Opcode;
  case AArch64::LDRHui:
  case AArch64::STRHui:
  case AArch64::LDRHHui:
  case AArch64::STRHHui:
  case AArch64::LDRSHXui:
  case AArch64::LDRSHWui:
    Scale = 2;
    return Opcode;
  case AArch64::LDRBui:
  case AArch64::LDRBBui:
  case AArch64::LDRSBXui:
  case AArch64::LDRSBWui:
  case AArch64::STRBui:
  case AArch64::STRBBui:
    Scale = 1;
    return Opcode;
  }
}

// Given an opcode for an instruction with a [Reg, #Imm] addressing mode, return
// the opcode of an instruction performing the same operation, but using the
// [Reg, #Imm] addressing mode with unscaled offset.
unsigned unscaledOffsetOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Address folding not implemented for instruction");

  case AArch64::LDURQi:
  case AArch64::STURQi:
  case AArch64::LDURDi:
  case AArch64::STURDi:
  case AArch64::LDURXi:
  case AArch64::STURXi:
  case AArch64::LDURWi:
  case AArch64::LDURSWi:
  case AArch64::STURWi:
  case AArch64::LDURHi:
  case AArch64::STURHi:
  case AArch64::LDURHHi:
  case AArch64::STURHHi:
  case AArch64::LDURSHXi:
  case AArch64::LDURSHWi:
  case AArch64::LDURBi:
  case AArch64::STURBi:
  case AArch64::LDURBBi:
  case AArch64::STURBBi:
  case AArch64::LDURSBWi:
  case AArch64::LDURSBXi:
    return Opcode;
  case AArch64::LDRQui:
    return AArch64::LDURQi;
  case AArch64::STRQui:
    return AArch64::STURQi;
  case AArch64::LDRDui:
    return AArch64::LDURDi;
  case AArch64::STRDui:
    return AArch64::STURDi;
  case AArch64::LDRXui:
    return AArch64::LDURXi;
  case AArch64::STRXui:
    return AArch64::STURXi;
  case AArch64::LDRWui:
    return AArch64::LDURWi;
  case AArch64::LDRSWui:
    return AArch64::LDURSWi;
  case AArch64::STRWui:
    return AArch64::STURWi;
  case AArch64::LDRHui:
    return AArch64::LDURHi;
  case AArch64::STRHui:
    return AArch64::STURHi;
  case AArch64::LDRHHui:
    return AArch64::LDURHHi;
  case AArch64::STRHHui:
    return AArch64::STURHHi;
  case AArch64::LDRSHXui:
    return AArch64::LDURSHXi;
  case AArch64::LDRSHWui:
    return AArch64::LDURSHWi;
  case AArch64::LDRBBui:
    return AArch64::LDURBBi;
  case AArch64::LDRBui:
    return AArch64::LDURBi;
  case AArch64::STRBBui:
    return AArch64::STURBBi;
  case AArch64::STRBui:
    return AArch64::STURBi;
  case AArch64::LDRSBWui:
    return AArch64::LDURSBWi;
  case AArch64::LDRSBXui:
    return AArch64::LDURSBXi;
  }
}

// Given the opcode of a memory load/store instruction, return the opcode of an
// instruction performing the same operation, but using
// the [Reg, Reg, {s,u}xtw #N] addressing mode with sign-/zero-extend of the
// offset register.
static unsigned offsetExtendOpcode(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Address folding not implemented for instruction");

  case AArch64::LDRQroX:
  case AArch64::LDURQi:
  case AArch64::LDRQui:
    return AArch64::LDRQroW;
  case AArch64::STRQroX:
  case AArch64::STURQi:
  case AArch64::STRQui:
    return AArch64::STRQroW;
  case AArch64::LDRDroX:
  case AArch64::LDURDi:
  case AArch64::LDRDui:
    return AArch64::LDRDroW;
  case AArch64::STRDroX:
  case AArch64::STURDi:
  case AArch64::STRDui:
    return AArch64::STRDroW;
  case AArch64::LDRXroX:
  case AArch64::LDURXi:
  case AArch64::LDRXui:
    return AArch64::LDRXroW;
  case AArch64::STRXroX:
  case AArch64::STURXi:
  case AArch64::STRXui:
    return AArch64::STRXroW;
  case AArch64::LDRWroX:
  case AArch64::LDURWi:
  case AArch64::LDRWui:
    return AArch64::LDRWroW;
  case AArch64::LDRSWroX:
  case AArch64::LDURSWi:
  case AArch64::LDRSWui:
    return AArch64::LDRSWroW;
  case AArch64::STRWroX:
  case AArch64::STURWi:
  case AArch64::STRWui:
    return AArch64::STRWroW;
  case AArch64::LDRHroX:
  case AArch64::LDURHi:
  case AArch64::LDRHui:
    return AArch64::LDRHroW;
  case AArch64::STRHroX:
  case AArch64::STURHi:
  case AArch64::STRHui:
    return AArch64::STRHroW;
  case AArch64::LDRHHroX:
  case AArch64::LDURHHi:
  case AArch64::LDRHHui:
    return AArch64::LDRHHroW;
  case AArch64::STRHHroX:
  case AArch64::STURHHi:
  case AArch64::STRHHui:
    return AArch64::STRHHroW;
  case AArch64::LDRSHXroX:
  case AArch64::LDURSHXi:
  case AArch64::LDRSHXui:
    return AArch64::LDRSHXroW;
  case AArch64::LDRSHWroX:
  case AArch64::LDURSHWi:
  case AArch64::LDRSHWui:
    return AArch64::LDRSHWroW;
  case AArch64::LDRBroX:
  case AArch64::LDURBi:
  case AArch64::LDRBui:
    return AArch64::LDRBroW;
  case AArch64::LDRBBroX:
  case AArch64::LDURBBi:
  case AArch64::LDRBBui:
    return AArch64::LDRBBroW;
  case AArch64::LDRSBXroX:
  case AArch64::LDURSBXi:
  case AArch64::LDRSBXui:
    return AArch64::LDRSBXroW;
  case AArch64::LDRSBWroX:
  case AArch64::LDURSBWi:
  case AArch64::LDRSBWui:
    return AArch64::LDRSBWroW;
  case AArch64::STRBroX:
  case AArch64::STURBi:
  case AArch64::STRBui:
    return AArch64::STRBroW;
  case AArch64::STRBBroX:
  case AArch64::STURBBi:
  case AArch64::STRBBui:
    return AArch64::STRBBroW;
  }
}

MachineInstr *AArch64InstrInfo::emitLdStWithAddr(MachineInstr &MemI,
                                                 const ExtAddrMode &AM) const {

  const DebugLoc &DL = MemI.getDebugLoc();
  MachineBasicBlock &MBB = *MemI.getParent();
  MachineRegisterInfo &MRI = MemI.getMF()->getRegInfo();

  if (AM.Form == ExtAddrMode::Formula::Basic) {
    if (AM.ScaledReg) {
      // The new instruction will be in the form `ldr Rt, [Xn, Xm, lsl #imm]`.
      unsigned Opcode = regOffsetOpcode(MemI.getOpcode());
      MRI.constrainRegClass(AM.BaseReg, &AArch64::GPR64spRegClass);
      auto B = BuildMI(MBB, MemI, DL, get(Opcode))
                   .addReg(MemI.getOperand(0).getReg(),
                           MemI.mayLoad() ? RegState::Define : 0)
                   .addReg(AM.BaseReg)
                   .addReg(AM.ScaledReg)
                   .addImm(0)
                   .addImm(AM.Scale > 1)
                   .setMemRefs(MemI.memoperands())
                   .setMIFlags(MemI.getFlags());
      return B.getInstr();
    }

    assert(AM.ScaledReg == 0 && AM.Scale == 0 &&
           "Addressing mode not supported for folding");

    // The new instruction will be in the form `ld[u]r Rt, [Xn, #imm]`.
    unsigned Scale = 1;
    unsigned Opcode = MemI.getOpcode();
    if (isInt<9>(AM.Displacement))
      Opcode = unscaledOffsetOpcode(Opcode);
    else
      Opcode = scaledOffsetOpcode(Opcode, Scale);

    auto B = BuildMI(MBB, MemI, DL, get(Opcode))
                 .addReg(MemI.getOperand(0).getReg(),
                         MemI.mayLoad() ? RegState::Define : 0)
                 .addReg(AM.BaseReg)
                 .addImm(AM.Displacement / Scale)
                 .setMemRefs(MemI.memoperands())
                 .setMIFlags(MemI.getFlags());
    return B.getInstr();
  }

  if (AM.Form == ExtAddrMode::Formula::SExtScaledReg ||
      AM.Form == ExtAddrMode::Formula::ZExtScaledReg) {
    // The new instruction will be in the form `ldr Rt, [Xn, Wm, {s,u}xtw #N]`.
    assert(AM.ScaledReg && !AM.Displacement &&
           "Address offset can be a register or an immediate, but not both");
    unsigned Opcode = offsetExtendOpcode(MemI.getOpcode());
    MRI.constrainRegClass(AM.BaseReg, &AArch64::GPR64spRegClass);
    // Make sure the offset register is in the correct register class.
    Register OffsetReg = AM.ScaledReg;
    const TargetRegisterClass *RC = MRI.getRegClass(OffsetReg);
    if (RC->hasSuperClassEq(&AArch64::GPR64RegClass)) {
      OffsetReg = MRI.createVirtualRegister(&AArch64::GPR32RegClass);
      BuildMI(MBB, MemI, DL, get(TargetOpcode::COPY), OffsetReg)
          .addReg(AM.ScaledReg, 0, AArch64::sub_32);
    }
    auto B = BuildMI(MBB, MemI, DL, get(Opcode))
                 .addReg(MemI.getOperand(0).getReg(),
                         MemI.mayLoad() ? RegState::Define : 0)
                 .addReg(AM.BaseReg)
                 .addReg(OffsetReg)
                 .addImm(AM.Form == ExtAddrMode::Formula::SExtScaledReg)
                 .addImm(AM.Scale != 1)
                 .setMemRefs(MemI.memoperands())
                 .setMIFlags(MemI.getFlags());

    return B.getInstr();
  }

  llvm_unreachable(
      "Function must not be called with an addressing mode it can't handle");
}

bool AArch64InstrInfo::getMemOperandWithOffsetWidth(
    const MachineInstr &LdSt, const MachineOperand *&BaseOp, int64_t &Offset,
    bool &OffsetIsScalable, TypeSize &Width,
    const TargetRegisterInfo *TRI) const {
  assert(LdSt.mayLoadOrStore() && "Expected a memory operation.");
  // Handle only loads/stores with base register followed by immediate offset.
  if (LdSt.getNumExplicitOperands() == 3) {
    // Non-paired instruction (e.g., ldr x1, [x0, #8]).
    if ((!LdSt.getOperand(1).isReg() && !LdSt.getOperand(1).isFI()) ||
        !LdSt.getOperand(2).isImm())
      return false;
  } else if (LdSt.getNumExplicitOperands() == 4) {
    // Paired instruction (e.g., ldp x1, x2, [x0, #8]).
    if (!LdSt.getOperand(1).isReg() ||
        (!LdSt.getOperand(2).isReg() && !LdSt.getOperand(2).isFI()) ||
        !LdSt.getOperand(3).isImm())
      return false;
  } else
    return false;

  // Get the scaling factor for the instruction and set the width for the
  // instruction.
  TypeSize Scale(0U, false);
  int64_t Dummy1, Dummy2;

  // If this returns false, then it's an instruction we don't want to handle.
  if (!getMemOpInfo(LdSt.getOpcode(), Scale, Width, Dummy1, Dummy2))
    return false;

  // Compute the offset. Offset is calculated as the immediate operand
  // multiplied by the scaling factor. Unscaled instructions have scaling factor
  // set to 1.
  if (LdSt.getNumExplicitOperands() == 3) {
    BaseOp = &LdSt.getOperand(1);
    Offset = LdSt.getOperand(2).getImm() * Scale.getKnownMinValue();
  } else {
    assert(LdSt.getNumExplicitOperands() == 4 && "invalid number of operands");
    BaseOp = &LdSt.getOperand(2);
    Offset = LdSt.getOperand(3).getImm() * Scale.getKnownMinValue();
  }
  OffsetIsScalable = Scale.isScalable();

  if (!BaseOp->isReg() && !BaseOp->isFI())
    return false;

  return true;
}

MachineOperand &
AArch64InstrInfo::getMemOpBaseRegImmOfsOffsetOperand(MachineInstr &LdSt) const {
  assert(LdSt.mayLoadOrStore() && "Expected a memory operation.");
  MachineOperand &OfsOp = LdSt.getOperand(LdSt.getNumExplicitOperands() - 1);
  assert(OfsOp.isImm() && "Offset operand wasn't immediate.");
  return OfsOp;
}

bool AArch64InstrInfo::getMemOpInfo(unsigned Opcode, TypeSize &Scale,
                                    TypeSize &Width, int64_t &MinOffset,
                                    int64_t &MaxOffset) {
  switch (Opcode) {
  // Not a memory operation or something we want to handle.
  default:
    Scale = TypeSize::getFixed(0);
    Width = TypeSize::getFixed(0);
    MinOffset = MaxOffset = 0;
    return false;
  // LDR / STR
  case AArch64::LDRQui:
  case AArch64::STRQui:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(16);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  case AArch64::LDRXui:
  case AArch64::LDRDui:
  case AArch64::STRXui:
  case AArch64::STRDui:
  case AArch64::PRFMui:
    Scale = TypeSize::getFixed(8);
    Width = TypeSize::getFixed(8);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  case AArch64::LDRWui:
  case AArch64::LDRSui:
  case AArch64::LDRSWui:
  case AArch64::STRWui:
  case AArch64::STRSui:
    Scale = TypeSize::getFixed(4);
    Width = TypeSize::getFixed(4);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  case AArch64::LDRHui:
  case AArch64::LDRHHui:
  case AArch64::LDRSHWui:
  case AArch64::LDRSHXui:
  case AArch64::STRHui:
  case AArch64::STRHHui:
    Scale = TypeSize::getFixed(2);
    Width = TypeSize::getFixed(2);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  case AArch64::LDRBui:
  case AArch64::LDRBBui:
  case AArch64::LDRSBWui:
  case AArch64::LDRSBXui:
  case AArch64::STRBui:
  case AArch64::STRBBui:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(1);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  // post/pre inc
  case AArch64::STRQpre:
  case AArch64::LDRQpost:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(16);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::STRXpre:
  case AArch64::STRDpre:
  case AArch64::LDRXpost:
  case AArch64::LDRDpost:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(8);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::STRWpost:
  case AArch64::LDRWpost:
    Scale = TypeSize::getFixed(4);
    Width = TypeSize::getFixed(32);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  // Unscaled
  case AArch64::LDURQi:
  case AArch64::STURQi:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(16);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LDURXi:
  case AArch64::LDURDi:
  case AArch64::LDAPURXi:
  case AArch64::STURXi:
  case AArch64::STURDi:
  case AArch64::STLURXi:
  case AArch64::PRFUMi:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(8);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LDURWi:
  case AArch64::LDURSi:
  case AArch64::LDURSWi:
  case AArch64::LDAPURi:
  case AArch64::LDAPURSWi:
  case AArch64::STURWi:
  case AArch64::STURSi:
  case AArch64::STLURWi:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(4);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LDURHi:
  case AArch64::LDURHHi:
  case AArch64::LDURSHXi:
  case AArch64::LDURSHWi:
  case AArch64::LDAPURHi:
  case AArch64::LDAPURSHWi:
  case AArch64::LDAPURSHXi:
  case AArch64::STURHi:
  case AArch64::STURHHi:
  case AArch64::STLURHi:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(2);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LDURBi:
  case AArch64::LDURBBi:
  case AArch64::LDURSBXi:
  case AArch64::LDURSBWi:
  case AArch64::LDAPURBi:
  case AArch64::LDAPURSBWi:
  case AArch64::LDAPURSBXi:
  case AArch64::STURBi:
  case AArch64::STURBBi:
  case AArch64::STLURBi:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(1);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  // LDP / STP
  case AArch64::LDPQi:
  case AArch64::LDNPQi:
  case AArch64::STPQi:
  case AArch64::STNPQi:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(32);
    MinOffset = -64;
    MaxOffset = 63;
    break;
  case AArch64::LDPXi:
  case AArch64::LDPDi:
  case AArch64::LDNPXi:
  case AArch64::LDNPDi:
  case AArch64::STPXi:
  case AArch64::STPDi:
  case AArch64::STNPXi:
  case AArch64::STNPDi:
    Scale = TypeSize::getFixed(8);
    Width = TypeSize::getFixed(16);
    MinOffset = -64;
    MaxOffset = 63;
    break;
  case AArch64::LDPWi:
  case AArch64::LDPSi:
  case AArch64::LDNPWi:
  case AArch64::LDNPSi:
  case AArch64::STPWi:
  case AArch64::STPSi:
  case AArch64::STNPWi:
  case AArch64::STNPSi:
    Scale = TypeSize::getFixed(4);
    Width = TypeSize::getFixed(8);
    MinOffset = -64;
    MaxOffset = 63;
    break;
  // pre/post inc
  case AArch64::STPQpre:
  case AArch64::LDPQpost:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(16);
    MinOffset = -1024;
    MaxOffset = 1008;
    break;
  case AArch64::STPXpre:
  case AArch64::LDPXpost:
  case AArch64::STPDpre:
  case AArch64::LDPDpost:
    Scale = TypeSize::getFixed(8);
    Width = TypeSize::getFixed(8);
    MinOffset = -512;
    MaxOffset = 504;
    break;
  case AArch64::StoreSwiftAsyncContext:
    // Store is an STRXui, but there might be an ADDXri in the expansion too.
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(8);
    MinOffset = 0;
    MaxOffset = 4095;
    break;
  case AArch64::ADDG:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(0);
    MinOffset = 0;
    MaxOffset = 63;
    break;
  case AArch64::TAGPstack:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(0);
    // TAGP with a negative offset turns into SUBP, which has a maximum offset
    // of 63 (not 64!).
    MinOffset = -63;
    MaxOffset = 63;
    break;
  case AArch64::LDG:
  case AArch64::STGi:
  case AArch64::STZGi:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(16);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  // SVE
  case AArch64::STR_ZZZZXI:
  case AArch64::LDR_ZZZZXI:
    Scale = TypeSize::getScalable(16);
    Width = TypeSize::getScalable(16 * 4);
    MinOffset = -256;
    MaxOffset = 252;
    break;
  case AArch64::STR_ZZZXI:
  case AArch64::LDR_ZZZXI:
    Scale = TypeSize::getScalable(16);
    Width = TypeSize::getScalable(16 * 3);
    MinOffset = -256;
    MaxOffset = 253;
    break;
  case AArch64::STR_ZZXI:
  case AArch64::LDR_ZZXI:
    Scale = TypeSize::getScalable(16);
    Width = TypeSize::getScalable(16 * 2);
    MinOffset = -256;
    MaxOffset = 254;
    break;
  case AArch64::LDR_PXI:
  case AArch64::STR_PXI:
    Scale = TypeSize::getScalable(2);
    Width = TypeSize::getScalable(2);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LDR_PPXI:
  case AArch64::STR_PPXI:
    Scale = TypeSize::getScalable(2);
    Width = TypeSize::getScalable(2 * 2);
    MinOffset = -256;
    MaxOffset = 254;
    break;
  case AArch64::LDR_ZXI:
  case AArch64::STR_ZXI:
    Scale = TypeSize::getScalable(16);
    Width = TypeSize::getScalable(16);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::LD1B_IMM:
  case AArch64::LD1H_IMM:
  case AArch64::LD1W_IMM:
  case AArch64::LD1D_IMM:
  case AArch64::LDNT1B_ZRI:
  case AArch64::LDNT1H_ZRI:
  case AArch64::LDNT1W_ZRI:
  case AArch64::LDNT1D_ZRI:
  case AArch64::ST1B_IMM:
  case AArch64::ST1H_IMM:
  case AArch64::ST1W_IMM:
  case AArch64::ST1D_IMM:
  case AArch64::STNT1B_ZRI:
  case AArch64::STNT1H_ZRI:
  case AArch64::STNT1W_ZRI:
  case AArch64::STNT1D_ZRI:
  case AArch64::LDNF1B_IMM:
  case AArch64::LDNF1H_IMM:
  case AArch64::LDNF1W_IMM:
  case AArch64::LDNF1D_IMM:
    // A full vectors worth of data
    // Width = mbytes * elements
    Scale = TypeSize::getScalable(16);
    Width = TypeSize::getScalable(16);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD2B_IMM:
  case AArch64::LD2H_IMM:
  case AArch64::LD2W_IMM:
  case AArch64::LD2D_IMM:
  case AArch64::ST2B_IMM:
  case AArch64::ST2H_IMM:
  case AArch64::ST2W_IMM:
  case AArch64::ST2D_IMM:
    Scale = TypeSize::getScalable(32);
    Width = TypeSize::getScalable(16 * 2);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD3B_IMM:
  case AArch64::LD3H_IMM:
  case AArch64::LD3W_IMM:
  case AArch64::LD3D_IMM:
  case AArch64::ST3B_IMM:
  case AArch64::ST3H_IMM:
  case AArch64::ST3W_IMM:
  case AArch64::ST3D_IMM:
    Scale = TypeSize::getScalable(48);
    Width = TypeSize::getScalable(16 * 3);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD4B_IMM:
  case AArch64::LD4H_IMM:
  case AArch64::LD4W_IMM:
  case AArch64::LD4D_IMM:
  case AArch64::ST4B_IMM:
  case AArch64::ST4H_IMM:
  case AArch64::ST4W_IMM:
  case AArch64::ST4D_IMM:
    Scale = TypeSize::getScalable(64);
    Width = TypeSize::getScalable(16 * 4);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD1B_H_IMM:
  case AArch64::LD1SB_H_IMM:
  case AArch64::LD1H_S_IMM:
  case AArch64::LD1SH_S_IMM:
  case AArch64::LD1W_D_IMM:
  case AArch64::LD1SW_D_IMM:
  case AArch64::ST1B_H_IMM:
  case AArch64::ST1H_S_IMM:
  case AArch64::ST1W_D_IMM:
  case AArch64::LDNF1B_H_IMM:
  case AArch64::LDNF1SB_H_IMM:
  case AArch64::LDNF1H_S_IMM:
  case AArch64::LDNF1SH_S_IMM:
  case AArch64::LDNF1W_D_IMM:
  case AArch64::LDNF1SW_D_IMM:
    // A half vector worth of data
    // Width = mbytes * elements
    Scale = TypeSize::getScalable(8);
    Width = TypeSize::getScalable(8);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD1B_S_IMM:
  case AArch64::LD1SB_S_IMM:
  case AArch64::LD1H_D_IMM:
  case AArch64::LD1SH_D_IMM:
  case AArch64::ST1B_S_IMM:
  case AArch64::ST1H_D_IMM:
  case AArch64::LDNF1B_S_IMM:
  case AArch64::LDNF1SB_S_IMM:
  case AArch64::LDNF1H_D_IMM:
  case AArch64::LDNF1SH_D_IMM:
    // A quarter vector worth of data
    // Width = mbytes * elements
    Scale = TypeSize::getScalable(4);
    Width = TypeSize::getScalable(4);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::LD1B_D_IMM:
  case AArch64::LD1SB_D_IMM:
  case AArch64::ST1B_D_IMM:
  case AArch64::LDNF1B_D_IMM:
  case AArch64::LDNF1SB_D_IMM:
    // A eighth vector worth of data
    // Width = mbytes * elements
    Scale = TypeSize::getScalable(2);
    Width = TypeSize::getScalable(2);
    MinOffset = -8;
    MaxOffset = 7;
    break;
  case AArch64::ST2Gi:
  case AArch64::STZ2Gi:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(32);
    MinOffset = -256;
    MaxOffset = 255;
    break;
  case AArch64::STGPi:
    Scale = TypeSize::getFixed(16);
    Width = TypeSize::getFixed(16);
    MinOffset = -64;
    MaxOffset = 63;
    break;
  case AArch64::LD1RB_IMM:
  case AArch64::LD1RB_H_IMM:
  case AArch64::LD1RB_S_IMM:
  case AArch64::LD1RB_D_IMM:
  case AArch64::LD1RSB_H_IMM:
  case AArch64::LD1RSB_S_IMM:
  case AArch64::LD1RSB_D_IMM:
    Scale = TypeSize::getFixed(1);
    Width = TypeSize::getFixed(1);
    MinOffset = 0;
    MaxOffset = 63;
    break;
  case AArch64::LD1RH_IMM:
  case AArch64::LD1RH_S_IMM:
  case AArch64::LD1RH_D_IMM:
  case AArch64::LD1RSH_S_IMM:
  case AArch64::LD1RSH_D_IMM:
    Scale = TypeSize::getFixed(2);
    Width = TypeSize::getFixed(2);
    MinOffset = 0;
    MaxOffset = 63;
    break;
  case AArch64::LD1RW_IMM:
  case AArch64::LD1RW_D_IMM:
  case AArch64::LD1RSW_IMM:
    Scale = TypeSize::getFixed(4);
    Width = TypeSize::getFixed(4);
    MinOffset = 0;
    MaxOffset = 63;
    break;
  case AArch64::LD1RD_IMM:
    Scale = TypeSize::getFixed(8);
    Width = TypeSize::getFixed(8);
    MinOffset = 0;
    MaxOffset = 63;
    break;
  }

  return true;
}

// Scaling factor for unscaled load or store.
int AArch64InstrInfo::getMemScale(unsigned Opc) {
  switch (Opc) {
  default:
    llvm_unreachable("Opcode has unknown scale!");
  case AArch64::LDRBBui:
  case AArch64::LDURBBi:
  case AArch64::LDRSBWui:
  case AArch64::LDURSBWi:
  case AArch64::STRBBui:
  case AArch64::STURBBi:
    return 1;
  case AArch64::LDRHHui:
  case AArch64::LDURHHi:
  case AArch64::LDRSHWui:
  case AArch64::LDURSHWi:
  case AArch64::STRHHui:
  case AArch64::STURHHi:
    return 2;
  case AArch64::LDRSui:
  case AArch64::LDURSi:
  case AArch64::LDRSpre:
  case AArch64::LDRSWui:
  case AArch64::LDURSWi:
  case AArch64::LDRSWpre:
  case AArch64::LDRWpre:
  case AArch64::LDRWui:
  case AArch64::LDURWi:
  case AArch64::STRSui:
  case AArch64::STURSi:
  case AArch64::STRSpre:
  case AArch64::STRWui:
  case AArch64::STURWi:
  case AArch64::STRWpre:
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPWi:
  case AArch64::STPSi:
  case AArch64::STPWi:
    return 4;
  case AArch64::LDRDui:
  case AArch64::LDURDi:
  case AArch64::LDRDpre:
  case AArch64::LDRXui:
  case AArch64::LDURXi:
  case AArch64::LDRXpre:
  case AArch64::STRDui:
  case AArch64::STURDi:
  case AArch64::STRDpre:
  case AArch64::STRXui:
  case AArch64::STURXi:
  case AArch64::STRXpre:
  case AArch64::LDPDi:
  case AArch64::LDPXi:
  case AArch64::STPDi:
  case AArch64::STPXi:
    return 8;
  case AArch64::LDRQui:
  case AArch64::LDURQi:
  case AArch64::STRQui:
  case AArch64::STURQi:
  case AArch64::STRQpre:
  case AArch64::LDPQi:
  case AArch64::LDRQpre:
  case AArch64::STPQi:
  case AArch64::STGi:
  case AArch64::STZGi:
  case AArch64::ST2Gi:
  case AArch64::STZ2Gi:
  case AArch64::STGPi:
    return 16;
  }
}

bool AArch64InstrInfo::isPreLd(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::LDRWpre:
  case AArch64::LDRXpre:
  case AArch64::LDRSWpre:
  case AArch64::LDRSpre:
  case AArch64::LDRDpre:
  case AArch64::LDRQpre:
    return true;
  }
}

bool AArch64InstrInfo::isPreSt(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::STRWpre:
  case AArch64::STRXpre:
  case AArch64::STRSpre:
  case AArch64::STRDpre:
  case AArch64::STRQpre:
    return true;
  }
}

bool AArch64InstrInfo::isPreLdSt(const MachineInstr &MI) {
  return isPreLd(MI) || isPreSt(MI);
}

bool AArch64InstrInfo::isPairedLdSt(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return false;
  case AArch64::LDPSi:
  case AArch64::LDPSWi:
  case AArch64::LDPDi:
  case AArch64::LDPQi:
  case AArch64::LDPWi:
  case AArch64::LDPXi:
  case AArch64::STPSi:
  case AArch64::STPDi:
  case AArch64::STPQi:
  case AArch64::STPWi:
  case AArch64::STPXi:
  case AArch64::STGPi:
    return true;
  }
}

const MachineOperand &AArch64InstrInfo::getLdStBaseOp(const MachineInstr &MI) {
  unsigned Idx =
      AArch64InstrInfo::isPairedLdSt(MI) || AArch64InstrInfo::isPreLdSt(MI) ? 2
                                                                            : 1;
  return MI.getOperand(Idx);
}

const MachineOperand &
AArch64InstrInfo::getLdStOffsetOp(const MachineInstr &MI) {
  unsigned Idx =
      AArch64InstrInfo::isPairedLdSt(MI) || AArch64InstrInfo::isPreLdSt(MI) ? 3
                                                                            : 2;
  return MI.getOperand(Idx);
}

static const TargetRegisterClass *getRegClass(const MachineInstr &MI,
                                              Register Reg) {
  if (MI.getParent() == nullptr)
    return nullptr;
  const MachineFunction *MF = MI.getParent()->getParent();
  return MF ? MF->getRegInfo().getRegClassOrNull(Reg) : nullptr;
}

bool AArch64InstrInfo::isHForm(const MachineInstr &MI) {
  auto IsHFPR = [&](const MachineOperand &Op) {
    if (!Op.isReg())
      return false;
    auto Reg = Op.getReg();
    if (Reg.isPhysical())
      return AArch64::FPR16RegClass.contains(Reg);
    const TargetRegisterClass *TRC = ::getRegClass(MI, Reg);
    return TRC == &AArch64::FPR16RegClass ||
           TRC == &AArch64::FPR16_loRegClass;
  };
  return llvm::any_of(MI.operands(), IsHFPR);
}

bool AArch64InstrInfo::isQForm(const MachineInstr &MI) {
  auto IsQFPR = [&](const MachineOperand &Op) {
    if (!Op.isReg())
      return false;
    auto Reg = Op.getReg();
    if (Reg.isPhysical())
      return AArch64::FPR128RegClass.contains(Reg);
    const TargetRegisterClass *TRC = ::getRegClass(MI, Reg);
    return TRC == &AArch64::FPR128RegClass ||
           TRC == &AArch64::FPR128_loRegClass;
  };
  return llvm::any_of(MI.operands(), IsQFPR);
}

bool AArch64InstrInfo::hasBTISemantics(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  case AArch64::BRK:
  case AArch64::HLT:
  case AArch64::PACIASP:
  case AArch64::PACIBSP:
    // Implicit BTI behavior.
    return true;
  case AArch64::PAUTH_PROLOGUE:
    // PAUTH_PROLOGUE expands to PACI(A|B)SP.
    return true;
  case AArch64::HINT: {
    unsigned Imm = MI.getOperand(0).getImm();
    // Explicit BTI instruction.
    if (Imm == 32 || Imm == 34 || Imm == 36 || Imm == 38)
      return true;
    // PACI(A|B)SP instructions.
    if (Imm == 25 || Imm == 27)
      return true;
    return false;
  }
  default:
    return false;
  }
}

bool AArch64InstrInfo::isFpOrNEON(Register Reg) {
  if (Reg == 0)
    return false;
  assert(Reg.isPhysical() && "Expected physical register in isFpOrNEON");
  return AArch64::FPR128RegClass.contains(Reg) ||
         AArch64::FPR64RegClass.contains(Reg) ||
         AArch64::FPR32RegClass.contains(Reg) ||
         AArch64::FPR16RegClass.contains(Reg) ||
         AArch64::FPR8RegClass.contains(Reg);
}

bool AArch64InstrInfo::isFpOrNEON(const MachineInstr &MI) {
  auto IsFPR = [&](const MachineOperand &Op) {
    if (!Op.isReg())
      return false;
    auto Reg = Op.getReg();
    if (Reg.isPhysical())
      return isFpOrNEON(Reg);

    const TargetRegisterClass *TRC = ::getRegClass(MI, Reg);
    return TRC == &AArch64::FPR128RegClass ||
           TRC == &AArch64::FPR128_loRegClass ||
           TRC == &AArch64::FPR64RegClass ||
           TRC == &AArch64::FPR64_loRegClass ||
           TRC == &AArch64::FPR32RegClass || TRC == &AArch64::FPR16RegClass ||
           TRC == &AArch64::FPR8RegClass;
  };
  return llvm::any_of(MI.operands(), IsFPR);
}

// Scale the unscaled offsets.  Returns false if the unscaled offset can't be
// scaled.
static bool scaleOffset(unsigned Opc, int64_t &Offset) {
  int Scale = AArch64InstrInfo::getMemScale(Opc);

  // If the byte-offset isn't a multiple of the stride, we can't scale this
  // offset.
  if (Offset % Scale != 0)
    return false;

  // Convert the byte-offset used by unscaled into an "element" offset used
  // by the scaled pair load/store instructions.
  Offset /= Scale;
  return true;
}

static bool canPairLdStOpc(unsigned FirstOpc, unsigned SecondOpc) {
  if (FirstOpc == SecondOpc)
    return true;
  // We can also pair sign-ext and zero-ext instructions.
  switch (FirstOpc) {
  default:
    return false;
  case AArch64::STRSui:
  case AArch64::STURSi:
    return SecondOpc == AArch64::STRSui || SecondOpc == AArch64::STURSi;
  case AArch64::STRDui:
  case AArch64::STURDi:
    return SecondOpc == AArch64::STRDui || SecondOpc == AArch64::STURDi;
  case AArch64::STRQui:
  case AArch64::STURQi:
    return SecondOpc == AArch64::STRQui || SecondOpc == AArch64::STURQi;
  case AArch64::STRWui:
  case AArch64::STURWi:
    return SecondOpc == AArch64::STRWui || SecondOpc == AArch64::STURWi;
  case AArch64::STRXui:
  case AArch64::STURXi:
    return SecondOpc == AArch64::STRXui || SecondOpc == AArch64::STURXi;
  case AArch64::LDRSui:
  case AArch64::LDURSi:
    return SecondOpc == AArch64::LDRSui || SecondOpc == AArch64::LDURSi;
  case AArch64::LDRDui:
  case AArch64::LDURDi:
    return SecondOpc == AArch64::LDRDui || SecondOpc == AArch64::LDURDi;
  case AArch64::LDRQui:
  case AArch64::LDURQi:
    return SecondOpc == AArch64::LDRQui || SecondOpc == AArch64::LDURQi;
  case AArch64::LDRWui:
  case AArch64::LDURWi:
    return SecondOpc == AArch64::LDRSWui || SecondOpc == AArch64::LDURSWi;
  case AArch64::LDRSWui:
  case AArch64::LDURSWi:
    return SecondOpc == AArch64::LDRWui || SecondOpc == AArch64::LDURWi;
  case AArch64::LDRXui:
  case AArch64::LDURXi:
    return SecondOpc == AArch64::LDRXui || SecondOpc == AArch64::LDURXi;
  }
  // These instructions can't be paired based on their opcodes.
  return false;
}

static bool shouldClusterFI(const MachineFrameInfo &MFI, int FI1,
                            int64_t Offset1, unsigned Opcode1, int FI2,
                            int64_t Offset2, unsigned Opcode2) {
  // Accesses through fixed stack object frame indices may access a different
  // fixed stack slot. Check that the object offsets + offsets match.
  if (MFI.isFixedObjectIndex(FI1) && MFI.isFixedObjectIndex(FI2)) {
    int64_t ObjectOffset1 = MFI.getObjectOffset(FI1);
    int64_t ObjectOffset2 = MFI.getObjectOffset(FI2);
    assert(ObjectOffset1 <= ObjectOffset2 && "Object offsets are not ordered.");
    // Convert to scaled object offsets.
    int Scale1 = AArch64InstrInfo::getMemScale(Opcode1);
    if (ObjectOffset1 % Scale1 != 0)
      return false;
    ObjectOffset1 /= Scale1;
    int Scale2 = AArch64InstrInfo::getMemScale(Opcode2);
    if (ObjectOffset2 % Scale2 != 0)
      return false;
    ObjectOffset2 /= Scale2;
    ObjectOffset1 += Offset1;
    ObjectOffset2 += Offset2;
    return ObjectOffset1 + 1 == ObjectOffset2;
  }

  return FI1 == FI2;
}

/// Detect opportunities for ldp/stp formation.
///
/// Only called for LdSt for which getMemOperandWithOffset returns true.
bool AArch64InstrInfo::shouldClusterMemOps(
    ArrayRef<const MachineOperand *> BaseOps1, int64_t OpOffset1,
    bool OffsetIsScalable1, ArrayRef<const MachineOperand *> BaseOps2,
    int64_t OpOffset2, bool OffsetIsScalable2, unsigned ClusterSize,
    unsigned NumBytes) const {
  assert(BaseOps1.size() == 1 && BaseOps2.size() == 1);
  const MachineOperand &BaseOp1 = *BaseOps1.front();
  const MachineOperand &BaseOp2 = *BaseOps2.front();
  const MachineInstr &FirstLdSt = *BaseOp1.getParent();
  const MachineInstr &SecondLdSt = *BaseOp2.getParent();
  if (BaseOp1.getType() != BaseOp2.getType())
    return false;

  assert((BaseOp1.isReg() || BaseOp1.isFI()) &&
         "Only base registers and frame indices are supported.");

  // Check for both base regs and base FI.
  if (BaseOp1.isReg() && BaseOp1.getReg() != BaseOp2.getReg())
    return false;

  // Only cluster up to a single pair.
  if (ClusterSize > 2)
    return false;

  if (!isPairableLdStInst(FirstLdSt) || !isPairableLdStInst(SecondLdSt))
    return false;

  // Can we pair these instructions based on their opcodes?
  unsigned FirstOpc = FirstLdSt.getOpcode();
  unsigned SecondOpc = SecondLdSt.getOpcode();
  if (!canPairLdStOpc(FirstOpc, SecondOpc))
    return false;

  // Can't merge volatiles or load/stores that have a hint to avoid pair
  // formation, for example.
  if (!isCandidateToMergeOrPair(FirstLdSt) ||
      !isCandidateToMergeOrPair(SecondLdSt))
    return false;

  // isCandidateToMergeOrPair guarantees that operand 2 is an immediate.
  int64_t Offset1 = FirstLdSt.getOperand(2).getImm();
  if (hasUnscaledLdStOffset(FirstOpc) && !scaleOffset(FirstOpc, Offset1))
    return false;

  int64_t Offset2 = SecondLdSt.getOperand(2).getImm();
  if (hasUnscaledLdStOffset(SecondOpc) && !scaleOffset(SecondOpc, Offset2))
    return false;

  // Pairwise instructions have a 7-bit signed offset field.
  if (Offset1 > 63 || Offset1 < -64)
    return false;

  // The caller should already have ordered First/SecondLdSt by offset.
  // Note: except for non-equal frame index bases
  if (BaseOp1.isFI()) {
    assert((!BaseOp1.isIdenticalTo(BaseOp2) || Offset1 <= Offset2) &&
           "Caller should have ordered offsets.");

    const MachineFrameInfo &MFI =
        FirstLdSt.getParent()->getParent()->getFrameInfo();
    return shouldClusterFI(MFI, BaseOp1.getIndex(), Offset1, FirstOpc,
                           BaseOp2.getIndex(), Offset2, SecondOpc);
  }

  assert(Offset1 <= Offset2 && "Caller should have ordered offsets.");

  return Offset1 + 1 == Offset2;
}

static const MachineInstrBuilder &AddSubReg(const MachineInstrBuilder &MIB,
                                            unsigned Reg, unsigned SubIdx,
                                            unsigned State,
                                            const TargetRegisterInfo *TRI) {
  if (!SubIdx)
    return MIB.addReg(Reg, State);

  if (Register::isPhysicalRegister(Reg))
    return MIB.addReg(TRI->getSubReg(Reg, SubIdx), State);
  return MIB.addReg(Reg, State, SubIdx);
}

static bool forwardCopyWillClobberTuple(unsigned DestReg, unsigned SrcReg,
                                        unsigned NumRegs) {
  // We really want the positive remainder mod 32 here, that happens to be
  // easily obtainable with a mask.
  return ((DestReg - SrcReg) & 0x1f) < NumRegs;
}

void AArch64InstrInfo::copyPhysRegTuple(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator I,
                                        const DebugLoc &DL, MCRegister DestReg,
                                        MCRegister SrcReg, bool KillSrc,
                                        unsigned Opcode,
                                        ArrayRef<unsigned> Indices) const {
  assert(Subtarget.hasNEON() && "Unexpected register copy without NEON");
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  uint16_t DestEncoding = TRI->getEncodingValue(DestReg);
  uint16_t SrcEncoding = TRI->getEncodingValue(SrcReg);
  unsigned NumRegs = Indices.size();

  int SubReg = 0, End = NumRegs, Incr = 1;
  if (forwardCopyWillClobberTuple(DestEncoding, SrcEncoding, NumRegs)) {
    SubReg = NumRegs - 1;
    End = -1;
    Incr = -1;
  }

  for (; SubReg != End; SubReg += Incr) {
    const MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opcode));
    AddSubReg(MIB, DestReg, Indices[SubReg], RegState::Define, TRI);
    AddSubReg(MIB, SrcReg, Indices[SubReg], 0, TRI);
    AddSubReg(MIB, SrcReg, Indices[SubReg], getKillRegState(KillSrc), TRI);
  }
}

void AArch64InstrInfo::copyGPRRegTuple(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       DebugLoc DL, unsigned DestReg,
                                       unsigned SrcReg, bool KillSrc,
                                       unsigned Opcode, unsigned ZeroReg,
                                       llvm::ArrayRef<unsigned> Indices) const {
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  unsigned NumRegs = Indices.size();

#ifndef NDEBUG
  uint16_t DestEncoding = TRI->getEncodingValue(DestReg);
  uint16_t SrcEncoding = TRI->getEncodingValue(SrcReg);
  assert(DestEncoding % NumRegs == 0 && SrcEncoding % NumRegs == 0 &&
         "GPR reg sequences should not be able to overlap");
#endif

  for (unsigned SubReg = 0; SubReg != NumRegs; ++SubReg) {
    const MachineInstrBuilder MIB = BuildMI(MBB, I, DL, get(Opcode));
    AddSubReg(MIB, DestReg, Indices[SubReg], RegState::Define, TRI);
    MIB.addReg(ZeroReg);
    AddSubReg(MIB, SrcReg, Indices[SubReg], getKillRegState(KillSrc), TRI);
    MIB.addImm(0);
  }
}

void AArch64InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I,
                                   const DebugLoc &DL, MCRegister DestReg,
                                   MCRegister SrcReg, bool KillSrc) const {
  if (AArch64::GPR32spRegClass.contains(DestReg) &&
      (AArch64::GPR32spRegClass.contains(SrcReg) || SrcReg == AArch64::WZR)) {
    const TargetRegisterInfo *TRI = &getRegisterInfo();

    if (DestReg == AArch64::WSP || SrcReg == AArch64::WSP) {
      // If either operand is WSP, expand to ADD #0.
      if (Subtarget.hasZeroCycleRegMove()) {
        // Cyclone recognizes "ADD Xd, Xn, #0" as a zero-cycle register move.
        MCRegister DestRegX = TRI->getMatchingSuperReg(
            DestReg, AArch64::sub_32, &AArch64::GPR64spRegClass);
        MCRegister SrcRegX = TRI->getMatchingSuperReg(
            SrcReg, AArch64::sub_32, &AArch64::GPR64spRegClass);
        // This instruction is reading and writing X registers.  This may upset
        // the register scavenger and machine verifier, so we need to indicate
        // that we are reading an undefined value from SrcRegX, but a proper
        // value from SrcReg.
        BuildMI(MBB, I, DL, get(AArch64::ADDXri), DestRegX)
            .addReg(SrcRegX, RegState::Undef)
            .addImm(0)
            .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0))
            .addReg(SrcReg, RegState::Implicit | getKillRegState(KillSrc));
      } else {
        BuildMI(MBB, I, DL, get(AArch64::ADDWri), DestReg)
            .addReg(SrcReg, getKillRegState(KillSrc))
            .addImm(0)
            .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0));
      }
    } else if (SrcReg == AArch64::WZR && Subtarget.hasZeroCycleZeroingGP()) {
      BuildMI(MBB, I, DL, get(AArch64::MOVZWi), DestReg)
          .addImm(0)
          .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0));
    } else {
      if (Subtarget.hasZeroCycleRegMove()) {
        // Cyclone recognizes "ORR Xd, XZR, Xm" as a zero-cycle register move.
        MCRegister DestRegX = TRI->getMatchingSuperReg(
            DestReg, AArch64::sub_32, &AArch64::GPR64spRegClass);
        MCRegister SrcRegX = TRI->getMatchingSuperReg(
            SrcReg, AArch64::sub_32, &AArch64::GPR64spRegClass);
        // This instruction is reading and writing X registers.  This may upset
        // the register scavenger and machine verifier, so we need to indicate
        // that we are reading an undefined value from SrcRegX, but a proper
        // value from SrcReg.
        BuildMI(MBB, I, DL, get(AArch64::ORRXrr), DestRegX)
            .addReg(AArch64::XZR)
            .addReg(SrcRegX, RegState::Undef)
            .addReg(SrcReg, RegState::Implicit | getKillRegState(KillSrc));
      } else {
        // Otherwise, expand to ORR WZR.
        BuildMI(MBB, I, DL, get(AArch64::ORRWrr), DestReg)
            .addReg(AArch64::WZR)
            .addReg(SrcReg, getKillRegState(KillSrc));
      }
    }
    return;
  }

  // Copy a Predicate register by ORRing with itself.
  if (AArch64::PPRRegClass.contains(DestReg) &&
      AArch64::PPRRegClass.contains(SrcReg)) {
    assert(Subtarget.isSVEorStreamingSVEAvailable() &&
           "Unexpected SVE register.");
    BuildMI(MBB, I, DL, get(AArch64::ORR_PPzPP), DestReg)
      .addReg(SrcReg) // Pg
      .addReg(SrcReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Copy a predicate-as-counter register by ORRing with itself as if it
  // were a regular predicate (mask) register.
  bool DestIsPNR = AArch64::PNRRegClass.contains(DestReg);
  bool SrcIsPNR = AArch64::PNRRegClass.contains(SrcReg);
  if (DestIsPNR || SrcIsPNR) {
    auto ToPPR = [](MCRegister R) -> MCRegister {
      return (R - AArch64::PN0) + AArch64::P0;
    };
    MCRegister PPRSrcReg = SrcIsPNR ? ToPPR(SrcReg) : SrcReg;
    MCRegister PPRDestReg = DestIsPNR ? ToPPR(DestReg) : DestReg;

    if (PPRSrcReg != PPRDestReg) {
      auto NewMI = BuildMI(MBB, I, DL, get(AArch64::ORR_PPzPP), PPRDestReg)
                       .addReg(PPRSrcReg) // Pg
                       .addReg(PPRSrcReg)
                       .addReg(PPRSrcReg, getKillRegState(KillSrc));
      if (DestIsPNR)
        NewMI.addDef(DestReg, RegState::Implicit);
    }
    return;
  }

  // Copy a Z register by ORRing with itself.
  if (AArch64::ZPRRegClass.contains(DestReg) &&
      AArch64::ZPRRegClass.contains(SrcReg)) {
    assert(Subtarget.isSVEorStreamingSVEAvailable() &&
           "Unexpected SVE register.");
    BuildMI(MBB, I, DL, get(AArch64::ORR_ZZZ), DestReg)
      .addReg(SrcReg)
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Copy a Z register pair by copying the individual sub-registers.
  if ((AArch64::ZPR2RegClass.contains(DestReg) ||
       AArch64::ZPR2StridedOrContiguousRegClass.contains(DestReg)) &&
      (AArch64::ZPR2RegClass.contains(SrcReg) ||
       AArch64::ZPR2StridedOrContiguousRegClass.contains(SrcReg))) {
    assert(Subtarget.isSVEorStreamingSVEAvailable() &&
           "Unexpected SVE register.");
    static const unsigned Indices[] = {AArch64::zsub0, AArch64::zsub1};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORR_ZZZ,
                     Indices);
    return;
  }

  // Copy a Z register triple by copying the individual sub-registers.
  if (AArch64::ZPR3RegClass.contains(DestReg) &&
      AArch64::ZPR3RegClass.contains(SrcReg)) {
    assert(Subtarget.isSVEorStreamingSVEAvailable() &&
           "Unexpected SVE register.");
    static const unsigned Indices[] = {AArch64::zsub0, AArch64::zsub1,
                                       AArch64::zsub2};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORR_ZZZ,
                     Indices);
    return;
  }

  // Copy a Z register quad by copying the individual sub-registers.
  if ((AArch64::ZPR4RegClass.contains(DestReg) ||
       AArch64::ZPR4StridedOrContiguousRegClass.contains(DestReg)) &&
      (AArch64::ZPR4RegClass.contains(SrcReg) ||
       AArch64::ZPR4StridedOrContiguousRegClass.contains(SrcReg))) {
    assert(Subtarget.isSVEorStreamingSVEAvailable() &&
           "Unexpected SVE register.");
    static const unsigned Indices[] = {AArch64::zsub0, AArch64::zsub1,
                                       AArch64::zsub2, AArch64::zsub3};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORR_ZZZ,
                     Indices);
    return;
  }

  if (AArch64::GPR64spRegClass.contains(DestReg) &&
      (AArch64::GPR64spRegClass.contains(SrcReg) || SrcReg == AArch64::XZR)) {
    if (DestReg == AArch64::SP || SrcReg == AArch64::SP) {
      // If either operand is SP, expand to ADD #0.
      BuildMI(MBB, I, DL, get(AArch64::ADDXri), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addImm(0)
          .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0));
    } else if (SrcReg == AArch64::XZR && Subtarget.hasZeroCycleZeroingGP()) {
      BuildMI(MBB, I, DL, get(AArch64::MOVZXi), DestReg)
          .addImm(0)
          .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0));
    } else {
      // Otherwise, expand to ORR XZR.
      BuildMI(MBB, I, DL, get(AArch64::ORRXrr), DestReg)
          .addReg(AArch64::XZR)
          .addReg(SrcReg, getKillRegState(KillSrc));
    }
    return;
  }

  // Copy a DDDD register quad by copying the individual sub-registers.
  if (AArch64::DDDDRegClass.contains(DestReg) &&
      AArch64::DDDDRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::dsub0, AArch64::dsub1,
                                       AArch64::dsub2, AArch64::dsub3};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv8i8,
                     Indices);
    return;
  }

  // Copy a DDD register triple by copying the individual sub-registers.
  if (AArch64::DDDRegClass.contains(DestReg) &&
      AArch64::DDDRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::dsub0, AArch64::dsub1,
                                       AArch64::dsub2};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv8i8,
                     Indices);
    return;
  }

  // Copy a DD register pair by copying the individual sub-registers.
  if (AArch64::DDRegClass.contains(DestReg) &&
      AArch64::DDRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::dsub0, AArch64::dsub1};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv8i8,
                     Indices);
    return;
  }

  // Copy a QQQQ register quad by copying the individual sub-registers.
  if (AArch64::QQQQRegClass.contains(DestReg) &&
      AArch64::QQQQRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::qsub0, AArch64::qsub1,
                                       AArch64::qsub2, AArch64::qsub3};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv16i8,
                     Indices);
    return;
  }

  // Copy a QQQ register triple by copying the individual sub-registers.
  if (AArch64::QQQRegClass.contains(DestReg) &&
      AArch64::QQQRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::qsub0, AArch64::qsub1,
                                       AArch64::qsub2};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv16i8,
                     Indices);
    return;
  }

  // Copy a QQ register pair by copying the individual sub-registers.
  if (AArch64::QQRegClass.contains(DestReg) &&
      AArch64::QQRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::qsub0, AArch64::qsub1};
    copyPhysRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRv16i8,
                     Indices);
    return;
  }

  if (AArch64::XSeqPairsClassRegClass.contains(DestReg) &&
      AArch64::XSeqPairsClassRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::sube64, AArch64::subo64};
    copyGPRRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRXrs,
                    AArch64::XZR, Indices);
    return;
  }

  if (AArch64::WSeqPairsClassRegClass.contains(DestReg) &&
      AArch64::WSeqPairsClassRegClass.contains(SrcReg)) {
    static const unsigned Indices[] = {AArch64::sube32, AArch64::subo32};
    copyGPRRegTuple(MBB, I, DL, DestReg, SrcReg, KillSrc, AArch64::ORRWrs,
                    AArch64::WZR, Indices);
    return;
  }

  if (AArch64::FPR128RegClass.contains(DestReg) &&
      AArch64::FPR128RegClass.contains(SrcReg)) {
    if (Subtarget.isSVEorStreamingSVEAvailable() &&
        !Subtarget.isNeonAvailable())
      BuildMI(MBB, I, DL, get(AArch64::ORR_ZZZ))
          .addReg(AArch64::Z0 + (DestReg - AArch64::Q0), RegState::Define)
          .addReg(AArch64::Z0 + (SrcReg - AArch64::Q0))
          .addReg(AArch64::Z0 + (SrcReg - AArch64::Q0));
    else if (Subtarget.isNeonAvailable())
      BuildMI(MBB, I, DL, get(AArch64::ORRv16i8), DestReg)
          .addReg(SrcReg)
          .addReg(SrcReg, getKillRegState(KillSrc));
    else {
      BuildMI(MBB, I, DL, get(AArch64::STRQpre))
          .addReg(AArch64::SP, RegState::Define)
          .addReg(SrcReg, getKillRegState(KillSrc))
          .addReg(AArch64::SP)
          .addImm(-16);
      BuildMI(MBB, I, DL, get(AArch64::LDRQpost))
          .addReg(AArch64::SP, RegState::Define)
          .addReg(DestReg, RegState::Define)
          .addReg(AArch64::SP)
          .addImm(16);
    }
    return;
  }

  if (AArch64::FPR64RegClass.contains(DestReg) &&
      AArch64::FPR64RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVDr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (AArch64::FPR32RegClass.contains(DestReg) &&
      AArch64::FPR32RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVSr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (AArch64::FPR16RegClass.contains(DestReg) &&
      AArch64::FPR16RegClass.contains(SrcReg)) {
    DestReg =
        RI.getMatchingSuperReg(DestReg, AArch64::hsub, &AArch64::FPR32RegClass);
    SrcReg =
        RI.getMatchingSuperReg(SrcReg, AArch64::hsub, &AArch64::FPR32RegClass);
    BuildMI(MBB, I, DL, get(AArch64::FMOVSr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (AArch64::FPR8RegClass.contains(DestReg) &&
      AArch64::FPR8RegClass.contains(SrcReg)) {
    DestReg =
        RI.getMatchingSuperReg(DestReg, AArch64::bsub, &AArch64::FPR32RegClass);
    SrcReg =
        RI.getMatchingSuperReg(SrcReg, AArch64::bsub, &AArch64::FPR32RegClass);
    BuildMI(MBB, I, DL, get(AArch64::FMOVSr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  // Copies between GPR64 and FPR64.
  if (AArch64::FPR64RegClass.contains(DestReg) &&
      AArch64::GPR64RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVXDr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (AArch64::GPR64RegClass.contains(DestReg) &&
      AArch64::FPR64RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVDXr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  // Copies between GPR32 and FPR32.
  if (AArch64::FPR32RegClass.contains(DestReg) &&
      AArch64::GPR32RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVWSr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  if (AArch64::GPR32RegClass.contains(DestReg) &&
      AArch64::FPR32RegClass.contains(SrcReg)) {
    BuildMI(MBB, I, DL, get(AArch64::FMOVSWr), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (DestReg == AArch64::NZCV) {
    assert(AArch64::GPR64RegClass.contains(SrcReg) && "Invalid NZCV copy");
    BuildMI(MBB, I, DL, get(AArch64::MSR))
        .addImm(AArch64SysReg::NZCV)
        .addReg(SrcReg, getKillRegState(KillSrc))
        .addReg(AArch64::NZCV, RegState::Implicit | RegState::Define);
    return;
  }

  if (SrcReg == AArch64::NZCV) {
    assert(AArch64::GPR64RegClass.contains(DestReg) && "Invalid NZCV copy");
    BuildMI(MBB, I, DL, get(AArch64::MRS), DestReg)
        .addImm(AArch64SysReg::NZCV)
        .addReg(AArch64::NZCV, RegState::Implicit | getKillRegState(KillSrc));
    return;
  }

#ifndef NDEBUG
  const TargetRegisterInfo &TRI = getRegisterInfo();
  errs() << TRI.getRegAsmName(DestReg) << " = COPY "
         << TRI.getRegAsmName(SrcReg) << "\n";
#endif
  llvm_unreachable("unimplemented reg-to-reg copy");
}

static void storeRegPairToStackSlot(const TargetRegisterInfo &TRI,
                                    MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator InsertBefore,
                                    const MCInstrDesc &MCID,
                                    Register SrcReg, bool IsKill,
                                    unsigned SubIdx0, unsigned SubIdx1, int FI,
                                    MachineMemOperand *MMO) {
  Register SrcReg0 = SrcReg;
  Register SrcReg1 = SrcReg;
  if (SrcReg.isPhysical()) {
    SrcReg0 = TRI.getSubReg(SrcReg, SubIdx0);
    SubIdx0 = 0;
    SrcReg1 = TRI.getSubReg(SrcReg, SubIdx1);
    SubIdx1 = 0;
  }
  BuildMI(MBB, InsertBefore, DebugLoc(), MCID)
      .addReg(SrcReg0, getKillRegState(IsKill), SubIdx0)
      .addReg(SrcReg1, getKillRegState(IsKill), SubIdx1)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO);
}

void AArch64InstrInfo::storeRegToStackSlot(MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           Register SrcReg, bool isKill, int FI,
                                           const TargetRegisterClass *RC,
                                           const TargetRegisterInfo *TRI,
                                           Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMO =
      MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOStore,
                              MFI.getObjectSize(FI), MFI.getObjectAlign(FI));
  unsigned Opc = 0;
  bool Offset = true;
  MCRegister PNRReg = MCRegister::NoRegister;
  unsigned StackID = TargetStackID::Default;
  switch (TRI->getSpillSize(*RC)) {
  case 1:
    if (AArch64::FPR8RegClass.hasSubClassEq(RC))
      Opc = AArch64::STRBui;
    break;
  case 2: {
    if (AArch64::FPR16RegClass.hasSubClassEq(RC))
      Opc = AArch64::STRHui;
    else if (AArch64::PNRRegClass.hasSubClassEq(RC) ||
             AArch64::PPRRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register store without SVE store instructions");
      Opc = AArch64::STR_PXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  }
  case 4:
    if (AArch64::GPR32allRegClass.hasSubClassEq(RC)) {
      Opc = AArch64::STRWui;
      if (SrcReg.isVirtual())
        MF.getRegInfo().constrainRegClass(SrcReg, &AArch64::GPR32RegClass);
      else
        assert(SrcReg != AArch64::WSP);
    } else if (AArch64::FPR32RegClass.hasSubClassEq(RC))
      Opc = AArch64::STRSui;
    else if (AArch64::PPR2RegClass.hasSubClassEq(RC)) {
      Opc = AArch64::STR_PPXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 8:
    if (AArch64::GPR64allRegClass.hasSubClassEq(RC)) {
      Opc = AArch64::STRXui;
      if (SrcReg.isVirtual())
        MF.getRegInfo().constrainRegClass(SrcReg, &AArch64::GPR64RegClass);
      else
        assert(SrcReg != AArch64::SP);
    } else if (AArch64::FPR64RegClass.hasSubClassEq(RC)) {
      Opc = AArch64::STRDui;
    } else if (AArch64::WSeqPairsClassRegClass.hasSubClassEq(RC)) {
      storeRegPairToStackSlot(getRegisterInfo(), MBB, MBBI,
                              get(AArch64::STPWi), SrcReg, isKill,
                              AArch64::sube32, AArch64::subo32, FI, MMO);
      return;
    }
    break;
  case 16:
    if (AArch64::FPR128RegClass.hasSubClassEq(RC))
      Opc = AArch64::STRQui;
    else if (AArch64::DDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Twov1d;
      Offset = false;
    } else if (AArch64::XSeqPairsClassRegClass.hasSubClassEq(RC)) {
      storeRegPairToStackSlot(getRegisterInfo(), MBB, MBBI,
                              get(AArch64::STPXi), SrcReg, isKill,
                              AArch64::sube64, AArch64::subo64, FI, MMO);
      return;
    } else if (AArch64::ZPRRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register store without SVE store instructions");
      Opc = AArch64::STR_ZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 24:
    if (AArch64::DDDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Threev1d;
      Offset = false;
    }
    break;
  case 32:
    if (AArch64::DDDDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Fourv1d;
      Offset = false;
    } else if (AArch64::QQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Twov2d;
      Offset = false;
    } else if (AArch64::ZPR2RegClass.hasSubClassEq(RC) ||
               AArch64::ZPR2StridedOrContiguousRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register store without SVE store instructions");
      Opc = AArch64::STR_ZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 48:
    if (AArch64::QQQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Threev2d;
      Offset = false;
    } else if (AArch64::ZPR3RegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register store without SVE store instructions");
      Opc = AArch64::STR_ZZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 64:
    if (AArch64::QQQQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register store without NEON");
      Opc = AArch64::ST1Fourv2d;
      Offset = false;
    } else if (AArch64::ZPR4RegClass.hasSubClassEq(RC) ||
               AArch64::ZPR4StridedOrContiguousRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register store without SVE store instructions");
      Opc = AArch64::STR_ZZZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  }
  assert(Opc && "Unknown register class");
  MFI.setStackID(FI, StackID);

  const MachineInstrBuilder MI = BuildMI(MBB, MBBI, DebugLoc(), get(Opc))
                                     .addReg(SrcReg, getKillRegState(isKill))
                                     .addFrameIndex(FI);

  if (Offset)
    MI.addImm(0);
  if (PNRReg.isValid())
    MI.addDef(PNRReg, RegState::Implicit);
  MI.addMemOperand(MMO);
}

static void loadRegPairFromStackSlot(const TargetRegisterInfo &TRI,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator InsertBefore,
                                     const MCInstrDesc &MCID,
                                     Register DestReg, unsigned SubIdx0,
                                     unsigned SubIdx1, int FI,
                                     MachineMemOperand *MMO) {
  Register DestReg0 = DestReg;
  Register DestReg1 = DestReg;
  bool IsUndef = true;
  if (DestReg.isPhysical()) {
    DestReg0 = TRI.getSubReg(DestReg, SubIdx0);
    SubIdx0 = 0;
    DestReg1 = TRI.getSubReg(DestReg, SubIdx1);
    SubIdx1 = 0;
    IsUndef = false;
  }
  BuildMI(MBB, InsertBefore, DebugLoc(), MCID)
      .addReg(DestReg0, RegState::Define | getUndefRegState(IsUndef), SubIdx0)
      .addReg(DestReg1, RegState::Define | getUndefRegState(IsUndef), SubIdx1)
      .addFrameIndex(FI)
      .addImm(0)
      .addMemOperand(MMO);
}

void AArch64InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                            MachineBasicBlock::iterator MBBI,
                                            Register DestReg, int FI,
                                            const TargetRegisterClass *RC,
                                            const TargetRegisterInfo *TRI,
                                            Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMO =
      MF.getMachineMemOperand(PtrInfo, MachineMemOperand::MOLoad,
                              MFI.getObjectSize(FI), MFI.getObjectAlign(FI));

  unsigned Opc = 0;
  bool Offset = true;
  unsigned StackID = TargetStackID::Default;
  Register PNRReg = MCRegister::NoRegister;
  switch (TRI->getSpillSize(*RC)) {
  case 1:
    if (AArch64::FPR8RegClass.hasSubClassEq(RC))
      Opc = AArch64::LDRBui;
    break;
  case 2: {
    bool IsPNR = AArch64::PNRRegClass.hasSubClassEq(RC);
    if (AArch64::FPR16RegClass.hasSubClassEq(RC))
      Opc = AArch64::LDRHui;
    else if (IsPNR || AArch64::PPRRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register load without SVE load instructions");
      if (IsPNR)
        PNRReg = DestReg;
      Opc = AArch64::LDR_PXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  }
  case 4:
    if (AArch64::GPR32allRegClass.hasSubClassEq(RC)) {
      Opc = AArch64::LDRWui;
      if (DestReg.isVirtual())
        MF.getRegInfo().constrainRegClass(DestReg, &AArch64::GPR32RegClass);
      else
        assert(DestReg != AArch64::WSP);
    } else if (AArch64::FPR32RegClass.hasSubClassEq(RC))
      Opc = AArch64::LDRSui;
    else if (AArch64::PPR2RegClass.hasSubClassEq(RC)) {
      Opc = AArch64::LDR_PPXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 8:
    if (AArch64::GPR64allRegClass.hasSubClassEq(RC)) {
      Opc = AArch64::LDRXui;
      if (DestReg.isVirtual())
        MF.getRegInfo().constrainRegClass(DestReg, &AArch64::GPR64RegClass);
      else
        assert(DestReg != AArch64::SP);
    } else if (AArch64::FPR64RegClass.hasSubClassEq(RC)) {
      Opc = AArch64::LDRDui;
    } else if (AArch64::WSeqPairsClassRegClass.hasSubClassEq(RC)) {
      loadRegPairFromStackSlot(getRegisterInfo(), MBB, MBBI,
                               get(AArch64::LDPWi), DestReg, AArch64::sube32,
                               AArch64::subo32, FI, MMO);
      return;
    }
    break;
  case 16:
    if (AArch64::FPR128RegClass.hasSubClassEq(RC))
      Opc = AArch64::LDRQui;
    else if (AArch64::DDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Twov1d;
      Offset = false;
    } else if (AArch64::XSeqPairsClassRegClass.hasSubClassEq(RC)) {
      loadRegPairFromStackSlot(getRegisterInfo(), MBB, MBBI,
                               get(AArch64::LDPXi), DestReg, AArch64::sube64,
                               AArch64::subo64, FI, MMO);
      return;
    } else if (AArch64::ZPRRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register load without SVE load instructions");
      Opc = AArch64::LDR_ZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 24:
    if (AArch64::DDDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Threev1d;
      Offset = false;
    }
    break;
  case 32:
    if (AArch64::DDDDRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Fourv1d;
      Offset = false;
    } else if (AArch64::QQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Twov2d;
      Offset = false;
    } else if (AArch64::ZPR2RegClass.hasSubClassEq(RC) ||
               AArch64::ZPR2StridedOrContiguousRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register load without SVE load instructions");
      Opc = AArch64::LDR_ZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 48:
    if (AArch64::QQQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Threev2d;
      Offset = false;
    } else if (AArch64::ZPR3RegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register load without SVE load instructions");
      Opc = AArch64::LDR_ZZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  case 64:
    if (AArch64::QQQQRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.hasNEON() && "Unexpected register load without NEON");
      Opc = AArch64::LD1Fourv2d;
      Offset = false;
    } else if (AArch64::ZPR4RegClass.hasSubClassEq(RC) ||
               AArch64::ZPR4StridedOrContiguousRegClass.hasSubClassEq(RC)) {
      assert(Subtarget.isSVEorStreamingSVEAvailable() &&
             "Unexpected register load without SVE load instructions");
      Opc = AArch64::LDR_ZZZZXI;
      StackID = TargetStackID::ScalableVector;
    }
    break;
  }

  assert(Opc && "Unknown register class");
  MFI.setStackID(FI, StackID);

  const MachineInstrBuilder MI = BuildMI(MBB, MBBI, DebugLoc(), get(Opc))
                                     .addReg(DestReg, getDefRegState(true))
                                     .addFrameIndex(FI);
  if (Offset)
    MI.addImm(0);
  if (PNRReg.isValid() && !PNRReg.isVirtual())
    MI.addDef(PNRReg, RegState::Implicit);
  MI.addMemOperand(MMO);
}

bool llvm::isNZCVTouchedInInstructionRange(const MachineInstr &DefMI,
                                           const MachineInstr &UseMI,
                                           const TargetRegisterInfo *TRI) {
  return any_of(instructionsWithoutDebug(std::next(DefMI.getIterator()),
                                         UseMI.getIterator()),
                [TRI](const MachineInstr &I) {
                  return I.modifiesRegister(AArch64::NZCV, TRI) ||
                         I.readsRegister(AArch64::NZCV, TRI);
                });
}

void AArch64InstrInfo::decomposeStackOffsetForDwarfOffsets(
    const StackOffset &Offset, int64_t &ByteSized, int64_t &VGSized) {
  // The smallest scalable element supported by scaled SVE addressing
  // modes are predicates, which are 2 scalable bytes in size. So the scalable
  // byte offset must always be a multiple of 2.
  assert(Offset.getScalable() % 2 == 0 && "Invalid frame offset");

  // VGSized offsets are divided by '2', because the VG register is the
  // the number of 64bit granules as opposed to 128bit vector chunks,
  // which is how the 'n' in e.g. MVT::nxv1i8 is modelled.
  // So, for a stack offset of 16 MVT::nxv1i8's, the size is n x 16 bytes.
  // VG = n * 2 and the dwarf offset must be VG * 8 bytes.
  ByteSized = Offset.getFixed();
  VGSized = Offset.getScalable() / 2;
}

/// Returns the offset in parts to which this frame offset can be
/// decomposed for the purpose of describing a frame offset.
/// For non-scalable offsets this is simply its byte size.
void AArch64InstrInfo::decomposeStackOffsetForFrameOffsets(
    const StackOffset &Offset, int64_t &NumBytes, int64_t &NumPredicateVectors,
    int64_t &NumDataVectors) {
  // The smallest scalable element supported by scaled SVE addressing
  // modes are predicates, which are 2 scalable bytes in size. So the scalable
  // byte offset must always be a multiple of 2.
  assert(Offset.getScalable() % 2 == 0 && "Invalid frame offset");

  NumBytes = Offset.getFixed();
  NumDataVectors = 0;
  NumPredicateVectors = Offset.getScalable() / 2;
  // This method is used to get the offsets to adjust the frame offset.
  // If the function requires ADDPL to be used and needs more than two ADDPL
  // instructions, part of the offset is folded into NumDataVectors so that it
  // uses ADDVL for part of it, reducing the number of ADDPL instructions.
  if (NumPredicateVectors % 8 == 0 || NumPredicateVectors < -64 ||
      NumPredicateVectors > 62) {
    NumDataVectors = NumPredicateVectors / 8;
    NumPredicateVectors -= NumDataVectors * 8;
  }
}

// Convenience function to create a DWARF expression for
//   Expr + NumBytes + NumVGScaledBytes * AArch64::VG
static void appendVGScaledOffsetExpr(SmallVectorImpl<char> &Expr, int NumBytes,
                                     int NumVGScaledBytes, unsigned VG,
                                     llvm::raw_string_ostream &Comment) {
  uint8_t buffer[16];

  if (NumBytes) {
    Expr.push_back(dwarf::DW_OP_consts);
    Expr.append(buffer, buffer + encodeSLEB128(NumBytes, buffer));
    Expr.push_back((uint8_t)dwarf::DW_OP_plus);
    Comment << (NumBytes < 0 ? " - " : " + ") << std::abs(NumBytes);
  }

  if (NumVGScaledBytes) {
    Expr.push_back((uint8_t)dwarf::DW_OP_consts);
    Expr.append(buffer, buffer + encodeSLEB128(NumVGScaledBytes, buffer));

    Expr.push_back((uint8_t)dwarf::DW_OP_bregx);
    Expr.append(buffer, buffer + encodeULEB128(VG, buffer));
    Expr.push_back(0);

    Expr.push_back((uint8_t)dwarf::DW_OP_mul);
    Expr.push_back((uint8_t)dwarf::DW_OP_plus);

    Comment << (NumVGScaledBytes < 0 ? " - " : " + ")
            << std::abs(NumVGScaledBytes) << " * VG";
  }
}

// Creates an MCCFIInstruction:
//    { DW_CFA_def_cfa_expression, ULEB128 (sizeof expr), expr }
static MCCFIInstruction createDefCFAExpression(const TargetRegisterInfo &TRI,
                                               unsigned Reg,
                                               const StackOffset &Offset) {
  int64_t NumBytes, NumVGScaledBytes;
  AArch64InstrInfo::decomposeStackOffsetForDwarfOffsets(Offset, NumBytes,
                                                        NumVGScaledBytes);
  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);

  if (Reg == AArch64::SP)
    Comment << "sp";
  else if (Reg == AArch64::FP)
    Comment << "fp";
  else
    Comment << printReg(Reg, &TRI);

  // Build up the expression (Reg + NumBytes + NumVGScaledBytes * AArch64::VG)
  SmallString<64> Expr;
  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  Expr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfReg));
  Expr.push_back(0);
  appendVGScaledOffsetExpr(Expr, NumBytes, NumVGScaledBytes,
                           TRI.getDwarfRegNum(AArch64::VG, true), Comment);

  // Wrap this into DW_CFA_def_cfa.
  SmallString<64> DefCfaExpr;
  DefCfaExpr.push_back(dwarf::DW_CFA_def_cfa_expression);
  uint8_t buffer[16];
  DefCfaExpr.append(buffer, buffer + encodeULEB128(Expr.size(), buffer));
  DefCfaExpr.append(Expr.str());
  return MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str(), SMLoc(),
                                        Comment.str());
}

MCCFIInstruction llvm::createDefCFA(const TargetRegisterInfo &TRI,
                                    unsigned FrameReg, unsigned Reg,
                                    const StackOffset &Offset,
                                    bool LastAdjustmentWasScalable) {
  if (Offset.getScalable())
    return createDefCFAExpression(TRI, Reg, Offset);

  if (FrameReg == Reg && !LastAdjustmentWasScalable)
    return MCCFIInstruction::cfiDefCfaOffset(nullptr, int(Offset.getFixed()));

  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  return MCCFIInstruction::cfiDefCfa(nullptr, DwarfReg, (int)Offset.getFixed());
}

MCCFIInstruction llvm::createCFAOffset(const TargetRegisterInfo &TRI,
                                       unsigned Reg,
                                       const StackOffset &OffsetFromDefCFA) {
  int64_t NumBytes, NumVGScaledBytes;
  AArch64InstrInfo::decomposeStackOffsetForDwarfOffsets(
      OffsetFromDefCFA, NumBytes, NumVGScaledBytes);

  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);

  // Non-scalable offsets can use DW_CFA_offset directly.
  if (!NumVGScaledBytes)
    return MCCFIInstruction::createOffset(nullptr, DwarfReg, NumBytes);

  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);
  Comment << printReg(Reg, &TRI) << "  @ cfa";

  // Build up expression (NumBytes + NumVGScaledBytes * AArch64::VG)
  SmallString<64> OffsetExpr;
  appendVGScaledOffsetExpr(OffsetExpr, NumBytes, NumVGScaledBytes,
                           TRI.getDwarfRegNum(AArch64::VG, true), Comment);

  // Wrap this into DW_CFA_expression
  SmallString<64> CfaExpr;
  CfaExpr.push_back(dwarf::DW_CFA_expression);
  uint8_t buffer[16];
  CfaExpr.append(buffer, buffer + encodeULEB128(DwarfReg, buffer));
  CfaExpr.append(buffer, buffer + encodeULEB128(OffsetExpr.size(), buffer));
  CfaExpr.append(OffsetExpr.str());

  return MCCFIInstruction::createEscape(nullptr, CfaExpr.str(), SMLoc(),
                                        Comment.str());
}

// Helper function to emit a frame offset adjustment from a given
// pointer (SrcReg), stored into DestReg. This function is explicit
// in that it requires the opcode.
static void emitFrameOffsetAdj(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MBBI,
                               const DebugLoc &DL, unsigned DestReg,
                               unsigned SrcReg, int64_t Offset, unsigned Opc,
                               const TargetInstrInfo *TII,
                               MachineInstr::MIFlag Flag, bool NeedsWinCFI,
                               bool *HasWinCFI, bool EmitCFAOffset,
                               StackOffset CFAOffset, unsigned FrameReg) {
  int Sign = 1;
  unsigned MaxEncoding, ShiftSize;
  switch (Opc) {
  case AArch64::ADDXri:
  case AArch64::ADDSXri:
  case AArch64::SUBXri:
  case AArch64::SUBSXri:
    MaxEncoding = 0xfff;
    ShiftSize = 12;
    break;
  case AArch64::ADDVL_XXI:
  case AArch64::ADDPL_XXI:
  case AArch64::ADDSVL_XXI:
  case AArch64::ADDSPL_XXI:
    MaxEncoding = 31;
    ShiftSize = 0;
    if (Offset < 0) {
      MaxEncoding = 32;
      Sign = -1;
      Offset = -Offset;
    }
    break;
  default:
    llvm_unreachable("Unsupported opcode");
  }

  // `Offset` can be in bytes or in "scalable bytes".
  int VScale = 1;
  if (Opc == AArch64::ADDVL_XXI || Opc == AArch64::ADDSVL_XXI)
    VScale = 16;
  else if (Opc == AArch64::ADDPL_XXI || Opc == AArch64::ADDSPL_XXI)
    VScale = 2;

  // FIXME: If the offset won't fit in 24-bits, compute the offset into a
  // scratch register.  If DestReg is a virtual register, use it as the
  // scratch register; otherwise, create a new virtual register (to be
  // replaced by the scavenger at the end of PEI).  That case can be optimized
  // slightly if DestReg is SP which is always 16-byte aligned, so the scratch
  // register can be loaded with offset%8 and the add/sub can use an extending
  // instruction with LSL#3.
  // Currently the function handles any offsets but generates a poor sequence
  // of code.
  //  assert(Offset < (1 << 24) && "unimplemented reg plus immediate");

  const unsigned MaxEncodableValue = MaxEncoding << ShiftSize;
  Register TmpReg = DestReg;
  if (TmpReg == AArch64::XZR)
    TmpReg = MBB.getParent()->getRegInfo().createVirtualRegister(
        &AArch64::GPR64RegClass);
  do {
    uint64_t ThisVal = std::min<uint64_t>(Offset, MaxEncodableValue);
    unsigned LocalShiftSize = 0;
    if (ThisVal > MaxEncoding) {
      ThisVal = ThisVal >> ShiftSize;
      LocalShiftSize = ShiftSize;
    }
    assert((ThisVal >> ShiftSize) <= MaxEncoding &&
           "Encoding cannot handle value that big");

    Offset -= ThisVal << LocalShiftSize;
    if (Offset == 0)
      TmpReg = DestReg;
    auto MBI = BuildMI(MBB, MBBI, DL, TII->get(Opc), TmpReg)
                   .addReg(SrcReg)
                   .addImm(Sign * (int)ThisVal);
    if (ShiftSize)
      MBI = MBI.addImm(
          AArch64_AM::getShifterImm(AArch64_AM::LSL, LocalShiftSize));
    MBI = MBI.setMIFlag(Flag);

    auto Change =
        VScale == 1
            ? StackOffset::getFixed(ThisVal << LocalShiftSize)
            : StackOffset::getScalable(VScale * (ThisVal << LocalShiftSize));
    if (Sign == -1 || Opc == AArch64::SUBXri || Opc == AArch64::SUBSXri)
      CFAOffset += Change;
    else
      CFAOffset -= Change;
    if (EmitCFAOffset && DestReg == TmpReg) {
      MachineFunction &MF = *MBB.getParent();
      const TargetSubtargetInfo &STI = MF.getSubtarget();
      const TargetRegisterInfo &TRI = *STI.getRegisterInfo();

      unsigned CFIIndex = MF.addFrameInst(
          createDefCFA(TRI, FrameReg, DestReg, CFAOffset, VScale != 1));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlags(Flag);
    }

    if (NeedsWinCFI) {
      assert(Sign == 1 && "SEH directives should always have a positive sign");
      int Imm = (int)(ThisVal << LocalShiftSize);
      if ((DestReg == AArch64::FP && SrcReg == AArch64::SP) ||
          (SrcReg == AArch64::FP && DestReg == AArch64::SP)) {
        if (HasWinCFI)
          *HasWinCFI = true;
        if (Imm == 0)
          BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_SetFP)).setMIFlag(Flag);
        else
          BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_AddFP))
              .addImm(Imm)
              .setMIFlag(Flag);
        assert(Offset == 0 && "Expected remaining offset to be zero to "
                              "emit a single SEH directive");
      } else if (DestReg == AArch64::SP) {
        if (HasWinCFI)
          *HasWinCFI = true;
        assert(SrcReg == AArch64::SP && "Unexpected SrcReg for SEH_StackAlloc");
        BuildMI(MBB, MBBI, DL, TII->get(AArch64::SEH_StackAlloc))
            .addImm(Imm)
            .setMIFlag(Flag);
      }
    }

    SrcReg = TmpReg;
  } while (Offset);
}

void llvm::emitFrameOffset(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                           unsigned DestReg, unsigned SrcReg,
                           StackOffset Offset, const TargetInstrInfo *TII,
                           MachineInstr::MIFlag Flag, bool SetNZCV,
                           bool NeedsWinCFI, bool *HasWinCFI,
                           bool EmitCFAOffset, StackOffset CFAOffset,
                           unsigned FrameReg) {
  // If a function is marked as arm_locally_streaming, then the runtime value of
  // vscale in the prologue/epilogue is different the runtime value of vscale
  // in the function's body. To avoid having to consider multiple vscales,
  // we can use `addsvl` to allocate any scalable stack-slots, which under
  // most circumstances will be only locals, not callee-save slots.
  const Function &F = MBB.getParent()->getFunction();
  bool UseSVL = F.hasFnAttribute("aarch64_pstate_sm_body");

  int64_t Bytes, NumPredicateVectors, NumDataVectors;
  AArch64InstrInfo::decomposeStackOffsetForFrameOffsets(
      Offset, Bytes, NumPredicateVectors, NumDataVectors);

  // First emit non-scalable frame offsets, or a simple 'mov'.
  if (Bytes || (!Offset && SrcReg != DestReg)) {
    assert((DestReg != AArch64::SP || Bytes % 8 == 0) &&
           "SP increment/decrement not 8-byte aligned");
    unsigned Opc = SetNZCV ? AArch64::ADDSXri : AArch64::ADDXri;
    if (Bytes < 0) {
      Bytes = -Bytes;
      Opc = SetNZCV ? AArch64::SUBSXri : AArch64::SUBXri;
    }
    emitFrameOffsetAdj(MBB, MBBI, DL, DestReg, SrcReg, Bytes, Opc, TII, Flag,
                       NeedsWinCFI, HasWinCFI, EmitCFAOffset, CFAOffset,
                       FrameReg);
    CFAOffset += (Opc == AArch64::ADDXri || Opc == AArch64::ADDSXri)
                     ? StackOffset::getFixed(-Bytes)
                     : StackOffset::getFixed(Bytes);
    SrcReg = DestReg;
    FrameReg = DestReg;
  }

  assert(!(SetNZCV && (NumPredicateVectors || NumDataVectors)) &&
         "SetNZCV not supported with SVE vectors");
  assert(!(NeedsWinCFI && (NumPredicateVectors || NumDataVectors)) &&
         "WinCFI not supported with SVE vectors");

  if (NumDataVectors) {
    emitFrameOffsetAdj(MBB, MBBI, DL, DestReg, SrcReg, NumDataVectors,
                       UseSVL ? AArch64::ADDSVL_XXI : AArch64::ADDVL_XXI,
                       TII, Flag, NeedsWinCFI, nullptr, EmitCFAOffset,
                       CFAOffset, FrameReg);
    CFAOffset += StackOffset::getScalable(-NumDataVectors * 16);
    SrcReg = DestReg;
  }

  if (NumPredicateVectors) {
    assert(DestReg != AArch64::SP && "Unaligned access to SP");
    emitFrameOffsetAdj(MBB, MBBI, DL, DestReg, SrcReg, NumPredicateVectors,
                       UseSVL ? AArch64::ADDSPL_XXI : AArch64::ADDPL_XXI,
                       TII, Flag, NeedsWinCFI, nullptr, EmitCFAOffset,
                       CFAOffset, FrameReg);
  }
}

MachineInstr *AArch64InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, int FrameIndex,
    LiveIntervals *LIS, VirtRegMap *VRM) const {
  // This is a bit of a hack. Consider this instruction:
  //
  //   %0 = COPY %sp; GPR64all:%0
  //
  // We explicitly chose GPR64all for the virtual register so such a copy might
  // be eliminated by RegisterCoalescer. However, that may not be possible, and
  // %0 may even spill. We can't spill %sp, and since it is in the GPR64all
  // register class, TargetInstrInfo::foldMemoryOperand() is going to try.
  //
  // To prevent that, we are going to constrain the %0 register class here.
  if (MI.isFullCopy()) {
    Register DstReg = MI.getOperand(0).getReg();
    Register SrcReg = MI.getOperand(1).getReg();
    if (SrcReg == AArch64::SP && DstReg.isVirtual()) {
      MF.getRegInfo().constrainRegClass(DstReg, &AArch64::GPR64RegClass);
      return nullptr;
    }
    if (DstReg == AArch64::SP && SrcReg.isVirtual()) {
      MF.getRegInfo().constrainRegClass(SrcReg, &AArch64::GPR64RegClass);
      return nullptr;
    }
    // Nothing can folded with copy from/to NZCV.
    if (SrcReg == AArch64::NZCV || DstReg == AArch64::NZCV)
      return nullptr;
  }

  // Handle the case where a copy is being spilled or filled but the source
  // and destination register class don't match.  For example:
  //
  //   %0 = COPY %xzr; GPR64common:%0
  //
  // In this case we can still safely fold away the COPY and generate the
  // following spill code:
  //
  //   STRXui %xzr, %stack.0
  //
  // This also eliminates spilled cross register class COPYs (e.g. between x and
  // d regs) of the same size.  For example:
  //
  //   %0 = COPY %1; GPR64:%0, FPR64:%1
  //
  // will be filled as
  //
  //   LDRDui %0, fi<#0>
  //
  // instead of
  //
  //   LDRXui %Temp, fi<#0>
  //   %0 = FMOV %Temp
  //
  if (MI.isCopy() && Ops.size() == 1 &&
      // Make sure we're only folding the explicit COPY defs/uses.
      (Ops[0] == 0 || Ops[0] == 1)) {
    bool IsSpill = Ops[0] == 0;
    bool IsFill = !IsSpill;
    const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    MachineBasicBlock &MBB = *MI.getParent();
    const MachineOperand &DstMO = MI.getOperand(0);
    const MachineOperand &SrcMO = MI.getOperand(1);
    Register DstReg = DstMO.getReg();
    Register SrcReg = SrcMO.getReg();
    // This is slightly expensive to compute for physical regs since
    // getMinimalPhysRegClass is slow.
    auto getRegClass = [&](unsigned Reg) {
      return Register::isVirtualRegister(Reg) ? MRI.getRegClass(Reg)
                                              : TRI.getMinimalPhysRegClass(Reg);
    };

    if (DstMO.getSubReg() == 0 && SrcMO.getSubReg() == 0) {
      assert(TRI.getRegSizeInBits(*getRegClass(DstReg)) ==
                 TRI.getRegSizeInBits(*getRegClass(SrcReg)) &&
             "Mismatched register size in non subreg COPY");
      if (IsSpill)
        storeRegToStackSlot(MBB, InsertPt, SrcReg, SrcMO.isKill(), FrameIndex,
                            getRegClass(SrcReg), &TRI, Register());
      else
        loadRegFromStackSlot(MBB, InsertPt, DstReg, FrameIndex,
                             getRegClass(DstReg), &TRI, Register());
      return &*--InsertPt;
    }

    // Handle cases like spilling def of:
    //
    //   %0:sub_32<def,read-undef> = COPY %wzr; GPR64common:%0
    //
    // where the physical register source can be widened and stored to the full
    // virtual reg destination stack slot, in this case producing:
    //
    //   STRXui %xzr, %stack.0
    //
    if (IsSpill && DstMO.isUndef() && SrcReg == AArch64::WZR &&
        TRI.getRegSizeInBits(*getRegClass(DstReg)) == 64) {
      assert(SrcMO.getSubReg() == 0 &&
             "Unexpected subreg on physical register");
      storeRegToStackSlot(MBB, InsertPt, AArch64::XZR, SrcMO.isKill(),
                          FrameIndex, &AArch64::GPR64RegClass, &TRI,
                          Register());
      return &*--InsertPt;
    }

    // Handle cases like filling use of:
    //
    //   %0:sub_32<def,read-undef> = COPY %1; GPR64:%0, GPR32:%1
    //
    // where we can load the full virtual reg source stack slot, into the subreg
    // destination, in this case producing:
    //
    //   LDRWui %0:sub_32<def,read-undef>, %stack.0
    //
    if (IsFill && SrcMO.getSubReg() == 0 && DstMO.isUndef()) {
      const TargetRegisterClass *FillRC;
      switch (DstMO.getSubReg()) {
      default:
        FillRC = nullptr;
        break;
      case AArch64::sub_32:
        FillRC = &AArch64::GPR32RegClass;
        break;
      case AArch64::ssub:
        FillRC = &AArch64::FPR32RegClass;
        break;
      case AArch64::dsub:
        FillRC = &AArch64::FPR64RegClass;
        break;
      }

      if (FillRC) {
        assert(TRI.getRegSizeInBits(*getRegClass(SrcReg)) ==
                   TRI.getRegSizeInBits(*FillRC) &&
               "Mismatched regclass size on folded subreg COPY");
        loadRegFromStackSlot(MBB, InsertPt, DstReg, FrameIndex, FillRC, &TRI,
                             Register());
        MachineInstr &LoadMI = *--InsertPt;
        MachineOperand &LoadDst = LoadMI.getOperand(0);
        assert(LoadDst.getSubReg() == 0 && "unexpected subreg on fill load");
        LoadDst.setSubReg(DstMO.getSubReg());
        LoadDst.setIsUndef();
        return &LoadMI;
      }
    }
  }

  // Cannot fold.
  return nullptr;
}

int llvm::isAArch64FrameOffsetLegal(const MachineInstr &MI,
                                    StackOffset &SOffset,
                                    bool *OutUseUnscaledOp,
                                    unsigned *OutUnscaledOp,
                                    int64_t *EmittableOffset) {
  // Set output values in case of early exit.
  if (EmittableOffset)
    *EmittableOffset = 0;
  if (OutUseUnscaledOp)
    *OutUseUnscaledOp = false;
  if (OutUnscaledOp)
    *OutUnscaledOp = 0;

  // Exit early for structured vector spills/fills as they can't take an
  // immediate offset.
  switch (MI.getOpcode()) {
  default:
    break;
  case AArch64::LD1Rv1d:
  case AArch64::LD1Rv2s:
  case AArch64::LD1Rv2d:
  case AArch64::LD1Rv4h:
  case AArch64::LD1Rv4s:
  case AArch64::LD1Rv8b:
  case AArch64::LD1Rv8h:
  case AArch64::LD1Rv16b:
  case AArch64::LD1Twov2d:
  case AArch64::LD1Threev2d:
  case AArch64::LD1Fourv2d:
  case AArch64::LD1Twov1d:
  case AArch64::LD1Threev1d:
  case AArch64::LD1Fourv1d:
  case AArch64::ST1Twov2d:
  case AArch64::ST1Threev2d:
  case AArch64::ST1Fourv2d:
  case AArch64::ST1Twov1d:
  case AArch64::ST1Threev1d:
  case AArch64::ST1Fourv1d:
  case AArch64::ST1i8:
  case AArch64::ST1i16:
  case AArch64::ST1i32:
  case AArch64::ST1i64:
  case AArch64::IRG:
  case AArch64::IRGstack:
  case AArch64::STGloop:
  case AArch64::STZGloop:
    return AArch64FrameOffsetCannotUpdate;
  }

  // Get the min/max offset and the scale.
  TypeSize ScaleValue(0U, false), Width(0U, false);
  int64_t MinOff, MaxOff;
  if (!AArch64InstrInfo::getMemOpInfo(MI.getOpcode(), ScaleValue, Width, MinOff,
                                      MaxOff))
    llvm_unreachable("unhandled opcode in isAArch64FrameOffsetLegal");

  // Construct the complete offset.
  bool IsMulVL = ScaleValue.isScalable();
  unsigned Scale = ScaleValue.getKnownMinValue();
  int64_t Offset = IsMulVL ? SOffset.getScalable() : SOffset.getFixed();

  const MachineOperand &ImmOpnd =
      MI.getOperand(AArch64InstrInfo::getLoadStoreImmIdx(MI.getOpcode()));
  Offset += ImmOpnd.getImm() * Scale;

  // If the offset doesn't match the scale, we rewrite the instruction to
  // use the unscaled instruction instead. Likewise, if we have a negative
  // offset and there is an unscaled op to use.
  std::optional<unsigned> UnscaledOp =
      AArch64InstrInfo::getUnscaledLdSt(MI.getOpcode());
  bool useUnscaledOp = UnscaledOp && (Offset % Scale || Offset < 0);
  if (useUnscaledOp &&
      !AArch64InstrInfo::getMemOpInfo(*UnscaledOp, ScaleValue, Width, MinOff,
                                      MaxOff))
    llvm_unreachable("unhandled opcode in isAArch64FrameOffsetLegal");

  Scale = ScaleValue.getKnownMinValue();
  assert(IsMulVL == ScaleValue.isScalable() &&
         "Unscaled opcode has different value for scalable");

  int64_t Remainder = Offset % Scale;
  assert(!(Remainder && useUnscaledOp) &&
         "Cannot have remainder when using unscaled op");

  assert(MinOff < MaxOff && "Unexpected Min/Max offsets");
  int64_t NewOffset = Offset / Scale;
  if (MinOff <= NewOffset && NewOffset <= MaxOff)
    Offset = Remainder;
  else {
    NewOffset = NewOffset < 0 ? MinOff : MaxOff;
    Offset = Offset - (NewOffset * Scale);
  }

  if (EmittableOffset)
    *EmittableOffset = NewOffset;
  if (OutUseUnscaledOp)
    *OutUseUnscaledOp = useUnscaledOp;
  if (OutUnscaledOp && UnscaledOp)
    *OutUnscaledOp = *UnscaledOp;

  if (IsMulVL)
    SOffset = StackOffset::get(SOffset.getFixed(), Offset);
  else
    SOffset = StackOffset::get(Offset, SOffset.getScalable());
  return AArch64FrameOffsetCanUpdate |
         (SOffset ? 0 : AArch64FrameOffsetIsLegal);
}

bool llvm::rewriteAArch64FrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                                    unsigned FrameReg, StackOffset &Offset,
                                    const AArch64InstrInfo *TII) {
  unsigned Opcode = MI.getOpcode();
  unsigned ImmIdx = FrameRegIdx + 1;

  if (Opcode == AArch64::ADDSXri || Opcode == AArch64::ADDXri) {
    Offset += StackOffset::getFixed(MI.getOperand(ImmIdx).getImm());
    emitFrameOffset(*MI.getParent(), MI, MI.getDebugLoc(),
                    MI.getOperand(0).getReg(), FrameReg, Offset, TII,
                    MachineInstr::NoFlags, (Opcode == AArch64::ADDSXri));
    MI.eraseFromParent();
    Offset = StackOffset();
    return true;
  }

  int64_t NewOffset;
  unsigned UnscaledOp;
  bool UseUnscaledOp;
  int Status = isAArch64FrameOffsetLegal(MI, Offset, &UseUnscaledOp,
                                         &UnscaledOp, &NewOffset);
  if (Status & AArch64FrameOffsetCanUpdate) {
    if (Status & AArch64FrameOffsetIsLegal)
      // Replace the FrameIndex with FrameReg.
      MI.getOperand(FrameRegIdx).ChangeToRegister(FrameReg, false);
    if (UseUnscaledOp)
      MI.setDesc(TII->get(UnscaledOp));

    MI.getOperand(ImmIdx).ChangeToImmediate(NewOffset);
    return !Offset;
  }

  return false;
}

void AArch64InstrInfo::insertNoop(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(AArch64::HINT)).addImm(0);
}

MCInst AArch64InstrInfo::getNop() const {
  return MCInstBuilder(AArch64::HINT).addImm(0);
}

// AArch64 supports MachineCombiner.
bool AArch64InstrInfo::useMachineCombiner() const { return true; }

// True when Opc sets flag
static bool isCombineInstrSettingFlag(unsigned Opc) {
  switch (Opc) {
  case AArch64::ADDSWrr:
  case AArch64::ADDSWri:
  case AArch64::ADDSXrr:
  case AArch64::ADDSXri:
  case AArch64::SUBSWrr:
  case AArch64::SUBSXrr:
  // Note: MSUB Wd,Wn,Wm,Wi -> Wd = Wi - WnxWm, not Wd=WnxWm - Wi.
  case AArch64::SUBSWri:
  case AArch64::SUBSXri:
    return true;
  default:
    break;
  }
  return false;
}

// 32b Opcodes that can be combined with a MUL
static bool isCombineInstrCandidate32(unsigned Opc) {
  switch (Opc) {
  case AArch64::ADDWrr:
  case AArch64::ADDWri:
  case AArch64::SUBWrr:
  case AArch64::ADDSWrr:
  case AArch64::ADDSWri:
  case AArch64::SUBSWrr:
  // Note: MSUB Wd,Wn,Wm,Wi -> Wd = Wi - WnxWm, not Wd=WnxWm - Wi.
  case AArch64::SUBWri:
  case AArch64::SUBSWri:
    return true;
  default:
    break;
  }
  return false;
}

// 64b Opcodes that can be combined with a MUL
static bool isCombineInstrCandidate64(unsigned Opc) {
  switch (Opc) {
  case AArch64::ADDXrr:
  case AArch64::ADDXri:
  case AArch64::SUBXrr:
  case AArch64::ADDSXrr:
  case AArch64::ADDSXri:
  case AArch64::SUBSXrr:
  // Note: MSUB Wd,Wn,Wm,Wi -> Wd = Wi - WnxWm, not Wd=WnxWm - Wi.
  case AArch64::SUBXri:
  case AArch64::SUBSXri:
  case AArch64::ADDv8i8:
  case AArch64::ADDv16i8:
  case AArch64::ADDv4i16:
  case AArch64::ADDv8i16:
  case AArch64::ADDv2i32:
  case AArch64::ADDv4i32:
  case AArch64::SUBv8i8:
  case AArch64::SUBv16i8:
  case AArch64::SUBv4i16:
  case AArch64::SUBv8i16:
  case AArch64::SUBv2i32:
  case AArch64::SUBv4i32:
    return true;
  default:
    break;
  }
  return false;
}

// FP Opcodes that can be combined with a FMUL.
static bool isCombineInstrCandidateFP(const MachineInstr &Inst) {
  switch (Inst.getOpcode()) {
  default:
    break;
  case AArch64::FADDHrr:
  case AArch64::FADDSrr:
  case AArch64::FADDDrr:
  case AArch64::FADDv4f16:
  case AArch64::FADDv8f16:
  case AArch64::FADDv2f32:
  case AArch64::FADDv2f64:
  case AArch64::FADDv4f32:
  case AArch64::FSUBHrr:
  case AArch64::FSUBSrr:
  case AArch64::FSUBDrr:
  case AArch64::FSUBv4f16:
  case AArch64::FSUBv8f16:
  case AArch64::FSUBv2f32:
  case AArch64::FSUBv2f64:
  case AArch64::FSUBv4f32:
    TargetOptions Options = Inst.getParent()->getParent()->getTarget().Options;
    // We can fuse FADD/FSUB with FMUL, if fusion is either allowed globally by
    // the target options or if FADD/FSUB has the contract fast-math flag.
    return Options.UnsafeFPMath ||
           Options.AllowFPOpFusion == FPOpFusion::Fast ||
           Inst.getFlag(MachineInstr::FmContract);
    return true;
  }
  return false;
}

// Opcodes that can be combined with a MUL
static bool isCombineInstrCandidate(unsigned Opc) {
  return (isCombineInstrCandidate32(Opc) || isCombineInstrCandidate64(Opc));
}

//
// Utility routine that checks if \param MO is defined by an
// \param CombineOpc instruction in the basic block \param MBB
static bool canCombine(MachineBasicBlock &MBB, MachineOperand &MO,
                       unsigned CombineOpc, unsigned ZeroReg = 0,
                       bool CheckZeroReg = false) {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineInstr *MI = nullptr;

  if (MO.isReg() && MO.getReg().isVirtual())
    MI = MRI.getUniqueVRegDef(MO.getReg());
  // And it needs to be in the trace (otherwise, it won't have a depth).
  if (!MI || MI->getParent() != &MBB || (unsigned)MI->getOpcode() != CombineOpc)
    return false;
  // Must only used by the user we combine with.
  if (!MRI.hasOneNonDBGUse(MI->getOperand(0).getReg()))
    return false;

  if (CheckZeroReg) {
    assert(MI->getNumOperands() >= 4 && MI->getOperand(0).isReg() &&
           MI->getOperand(1).isReg() && MI->getOperand(2).isReg() &&
           MI->getOperand(3).isReg() && "MAdd/MSub must have a least 4 regs");
    // The third input reg must be zero.
    if (MI->getOperand(3).getReg() != ZeroReg)
      return false;
  }

  if (isCombineInstrSettingFlag(CombineOpc) &&
      MI->findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr, true) == -1)
    return false;

  return true;
}

//
// Is \param MO defined by an integer multiply and can be combined?
static bool canCombineWithMUL(MachineBasicBlock &MBB, MachineOperand &MO,
                              unsigned MulOpc, unsigned ZeroReg) {
  return canCombine(MBB, MO, MulOpc, ZeroReg, true);
}

//
// Is \param MO defined by a floating-point multiply and can be combined?
static bool canCombineWithFMUL(MachineBasicBlock &MBB, MachineOperand &MO,
                               unsigned MulOpc) {
  return canCombine(MBB, MO, MulOpc);
}

// TODO: There are many more machine instruction opcodes to match:
//       1. Other data types (integer, vectors)
//       2. Other math / logic operations (xor, or)
//       3. Other forms of the same operation (intrinsics and other variants)
bool AArch64InstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst,
                                                   bool Invert) const {
  if (Invert)
    return false;
  switch (Inst.getOpcode()) {
  // == Floating-point types ==
  // -- Floating-point instructions --
  case AArch64::FADDHrr:
  case AArch64::FADDSrr:
  case AArch64::FADDDrr:
  case AArch64::FMULHrr:
  case AArch64::FMULSrr:
  case AArch64::FMULDrr:
  case AArch64::FMULX16:
  case AArch64::FMULX32:
  case AArch64::FMULX64:
  // -- Advanced SIMD instructions --
  case AArch64::FADDv4f16:
  case AArch64::FADDv8f16:
  case AArch64::FADDv2f32:
  case AArch64::FADDv4f32:
  case AArch64::FADDv2f64:
  case AArch64::FMULv4f16:
  case AArch64::FMULv8f16:
  case AArch64::FMULv2f32:
  case AArch64::FMULv4f32:
  case AArch64::FMULv2f64:
  case AArch64::FMULXv4f16:
  case AArch64::FMULXv8f16:
  case AArch64::FMULXv2f32:
  case AArch64::FMULXv4f32:
  case AArch64::FMULXv2f64:
  // -- SVE instructions --
  // Opcodes FMULX_ZZZ_? don't exist because there is no unpredicated FMULX
  // in the SVE instruction set (though there are predicated ones).
  case AArch64::FADD_ZZZ_H:
  case AArch64::FADD_ZZZ_S:
  case AArch64::FADD_ZZZ_D:
  case AArch64::FMUL_ZZZ_H:
  case AArch64::FMUL_ZZZ_S:
  case AArch64::FMUL_ZZZ_D:
    return Inst.getParent()->getParent()->getTarget().Options.UnsafeFPMath ||
           (Inst.getFlag(MachineInstr::MIFlag::FmReassoc) &&
            Inst.getFlag(MachineInstr::MIFlag::FmNsz));

  // == Integer types ==
  // -- Base instructions --
  // Opcodes MULWrr and MULXrr don't exist because
  // `MUL <Wd>, <Wn>, <Wm>` and `MUL <Xd>, <Xn>, <Xm>` are aliases of
  // `MADD <Wd>, <Wn>, <Wm>, WZR` and `MADD <Xd>, <Xn>, <Xm>, XZR` respectively.
  // The machine-combiner does not support three-source-operands machine
  // instruction. So we cannot reassociate MULs.
  case AArch64::ADDWrr:
  case AArch64::ADDXrr:
  case AArch64::ANDWrr:
  case AArch64::ANDXrr:
  case AArch64::ORRWrr:
  case AArch64::ORRXrr:
  case AArch64::EORWrr:
  case AArch64::EORXrr:
  case AArch64::EONWrr:
  case AArch64::EONXrr:
  // -- Advanced SIMD instructions --
  // Opcodes MULv1i64 and MULv2i64 don't exist because there is no 64-bit MUL
  // in the Advanced SIMD instruction set.
  case AArch64::ADDv8i8:
  case AArch64::ADDv16i8:
  case AArch64::ADDv4i16:
  case AArch64::ADDv8i16:
  case AArch64::ADDv2i32:
  case AArch64::ADDv4i32:
  case AArch64::ADDv1i64:
  case AArch64::ADDv2i64:
  case AArch64::MULv8i8:
  case AArch64::MULv16i8:
  case AArch64::MULv4i16:
  case AArch64::MULv8i16:
  case AArch64::MULv2i32:
  case AArch64::MULv4i32:
  case AArch64::ANDv8i8:
  case AArch64::ANDv16i8:
  case AArch64::ORRv8i8:
  case AArch64::ORRv16i8:
  case AArch64::EORv8i8:
  case AArch64::EORv16i8:
  // -- SVE instructions --
  case AArch64::ADD_ZZZ_B:
  case AArch64::ADD_ZZZ_H:
  case AArch64::ADD_ZZZ_S:
  case AArch64::ADD_ZZZ_D:
  case AArch64::MUL_ZZZ_B:
  case AArch64::MUL_ZZZ_H:
  case AArch64::MUL_ZZZ_S:
  case AArch64::MUL_ZZZ_D:
  case AArch64::AND_ZZZ:
  case AArch64::ORR_ZZZ:
  case AArch64::EOR_ZZZ:
    return true;

  default:
    return false;
  }
}

/// Find instructions that can be turned into madd.
static bool getMaddPatterns(MachineInstr &Root,
                            SmallVectorImpl<unsigned> &Patterns) {
  unsigned Opc = Root.getOpcode();
  MachineBasicBlock &MBB = *Root.getParent();
  bool Found = false;

  if (!isCombineInstrCandidate(Opc))
    return false;
  if (isCombineInstrSettingFlag(Opc)) {
    int Cmp_NZCV =
        Root.findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr, true);
    // When NZCV is live bail out.
    if (Cmp_NZCV == -1)
      return false;
    unsigned NewOpc = convertToNonFlagSettingOpc(Root);
    // When opcode can't change bail out.
    // CHECKME: do we miss any cases for opcode conversion?
    if (NewOpc == Opc)
      return false;
    Opc = NewOpc;
  }

  auto setFound = [&](int Opcode, int Operand, unsigned ZeroReg,
                      unsigned Pattern) {
    if (canCombineWithMUL(MBB, Root.getOperand(Operand), Opcode, ZeroReg)) {
      Patterns.push_back(Pattern);
      Found = true;
    }
  };

  auto setVFound = [&](int Opcode, int Operand, unsigned Pattern) {
    if (canCombine(MBB, Root.getOperand(Operand), Opcode)) {
      Patterns.push_back(Pattern);
      Found = true;
    }
  };

  typedef AArch64MachineCombinerPattern MCP;

  switch (Opc) {
  default:
    break;
  case AArch64::ADDWrr:
    assert(Root.getOperand(1).isReg() && Root.getOperand(2).isReg() &&
           "ADDWrr does not have register operands");
    setFound(AArch64::MADDWrrr, 1, AArch64::WZR, MCP::MULADDW_OP1);
    setFound(AArch64::MADDWrrr, 2, AArch64::WZR, MCP::MULADDW_OP2);
    break;
  case AArch64::ADDXrr:
    setFound(AArch64::MADDXrrr, 1, AArch64::XZR, MCP::MULADDX_OP1);
    setFound(AArch64::MADDXrrr, 2, AArch64::XZR, MCP::MULADDX_OP2);
    break;
  case AArch64::SUBWrr:
    setFound(AArch64::MADDWrrr, 2, AArch64::WZR, MCP::MULSUBW_OP2);
    setFound(AArch64::MADDWrrr, 1, AArch64::WZR, MCP::MULSUBW_OP1);
    break;
  case AArch64::SUBXrr:
    setFound(AArch64::MADDXrrr, 2, AArch64::XZR, MCP::MULSUBX_OP2);
    setFound(AArch64::MADDXrrr, 1, AArch64::XZR, MCP::MULSUBX_OP1);
    break;
  case AArch64::ADDWri:
    setFound(AArch64::MADDWrrr, 1, AArch64::WZR, MCP::MULADDWI_OP1);
    break;
  case AArch64::ADDXri:
    setFound(AArch64::MADDXrrr, 1, AArch64::XZR, MCP::MULADDXI_OP1);
    break;
  case AArch64::SUBWri:
    setFound(AArch64::MADDWrrr, 1, AArch64::WZR, MCP::MULSUBWI_OP1);
    break;
  case AArch64::SUBXri:
    setFound(AArch64::MADDXrrr, 1, AArch64::XZR, MCP::MULSUBXI_OP1);
    break;
  case AArch64::ADDv8i8:
    setVFound(AArch64::MULv8i8, 1, MCP::MULADDv8i8_OP1);
    setVFound(AArch64::MULv8i8, 2, MCP::MULADDv8i8_OP2);
    break;
  case AArch64::ADDv16i8:
    setVFound(AArch64::MULv16i8, 1, MCP::MULADDv16i8_OP1);
    setVFound(AArch64::MULv16i8, 2, MCP::MULADDv16i8_OP2);
    break;
  case AArch64::ADDv4i16:
    setVFound(AArch64::MULv4i16, 1, MCP::MULADDv4i16_OP1);
    setVFound(AArch64::MULv4i16, 2, MCP::MULADDv4i16_OP2);
    setVFound(AArch64::MULv4i16_indexed, 1, MCP::MULADDv4i16_indexed_OP1);
    setVFound(AArch64::MULv4i16_indexed, 2, MCP::MULADDv4i16_indexed_OP2);
    break;
  case AArch64::ADDv8i16:
    setVFound(AArch64::MULv8i16, 1, MCP::MULADDv8i16_OP1);
    setVFound(AArch64::MULv8i16, 2, MCP::MULADDv8i16_OP2);
    setVFound(AArch64::MULv8i16_indexed, 1, MCP::MULADDv8i16_indexed_OP1);
    setVFound(AArch64::MULv8i16_indexed, 2, MCP::MULADDv8i16_indexed_OP2);
    break;
  case AArch64::ADDv2i32:
    setVFound(AArch64::MULv2i32, 1, MCP::MULADDv2i32_OP1);
    setVFound(AArch64::MULv2i32, 2, MCP::MULADDv2i32_OP2);
    setVFound(AArch64::MULv2i32_indexed, 1, MCP::MULADDv2i32_indexed_OP1);
    setVFound(AArch64::MULv2i32_indexed, 2, MCP::MULADDv2i32_indexed_OP2);
    break;
  case AArch64::ADDv4i32:
    setVFound(AArch64::MULv4i32, 1, MCP::MULADDv4i32_OP1);
    setVFound(AArch64::MULv4i32, 2, MCP::MULADDv4i32_OP2);
    setVFound(AArch64::MULv4i32_indexed, 1, MCP::MULADDv4i32_indexed_OP1);
    setVFound(AArch64::MULv4i32_indexed, 2, MCP::MULADDv4i32_indexed_OP2);
    break;
  case AArch64::SUBv8i8:
    setVFound(AArch64::MULv8i8, 1, MCP::MULSUBv8i8_OP1);
    setVFound(AArch64::MULv8i8, 2, MCP::MULSUBv8i8_OP2);
    break;
  case AArch64::SUBv16i8:
    setVFound(AArch64::MULv16i8, 1, MCP::MULSUBv16i8_OP1);
    setVFound(AArch64::MULv16i8, 2, MCP::MULSUBv16i8_OP2);
    break;
  case AArch64::SUBv4i16:
    setVFound(AArch64::MULv4i16, 1, MCP::MULSUBv4i16_OP1);
    setVFound(AArch64::MULv4i16, 2, MCP::MULSUBv4i16_OP2);
    setVFound(AArch64::MULv4i16_indexed, 1, MCP::MULSUBv4i16_indexed_OP1);
    setVFound(AArch64::MULv4i16_indexed, 2, MCP::MULSUBv4i16_indexed_OP2);
    break;
  case AArch64::SUBv8i16:
    setVFound(AArch64::MULv8i16, 1, MCP::MULSUBv8i16_OP1);
    setVFound(AArch64::MULv8i16, 2, MCP::MULSUBv8i16_OP2);
    setVFound(AArch64::MULv8i16_indexed, 1, MCP::MULSUBv8i16_indexed_OP1);
    setVFound(AArch64::MULv8i16_indexed, 2, MCP::MULSUBv8i16_indexed_OP2);
    break;
  case AArch64::SUBv2i32:
    setVFound(AArch64::MULv2i32, 1, MCP::MULSUBv2i32_OP1);
    setVFound(AArch64::MULv2i32, 2, MCP::MULSUBv2i32_OP2);
    setVFound(AArch64::MULv2i32_indexed, 1, MCP::MULSUBv2i32_indexed_OP1);
    setVFound(AArch64::MULv2i32_indexed, 2, MCP::MULSUBv2i32_indexed_OP2);
    break;
  case AArch64::SUBv4i32:
    setVFound(AArch64::MULv4i32, 1, MCP::MULSUBv4i32_OP1);
    setVFound(AArch64::MULv4i32, 2, MCP::MULSUBv4i32_OP2);
    setVFound(AArch64::MULv4i32_indexed, 1, MCP::MULSUBv4i32_indexed_OP1);
    setVFound(AArch64::MULv4i32_indexed, 2, MCP::MULSUBv4i32_indexed_OP2);
    break;
  }
  return Found;
}
/// Floating-Point Support

/// Find instructions that can be turned into madd.
static bool getFMAPatterns(MachineInstr &Root,
                           SmallVectorImpl<unsigned> &Patterns) {

  if (!isCombineInstrCandidateFP(Root))
    return false;

  MachineBasicBlock &MBB = *Root.getParent();
  bool Found = false;

  auto Match = [&](int Opcode, int Operand, unsigned Pattern) -> bool {
    if (canCombineWithFMUL(MBB, Root.getOperand(Operand), Opcode)) {
      Patterns.push_back(Pattern);
      return true;
    }
    return false;
  };

  typedef AArch64MachineCombinerPattern MCP;

  switch (Root.getOpcode()) {
  default:
    assert(false && "Unsupported FP instruction in combiner\n");
    break;
  case AArch64::FADDHrr:
    assert(Root.getOperand(1).isReg() && Root.getOperand(2).isReg() &&
           "FADDHrr does not have register operands");

    Found  = Match(AArch64::FMULHrr, 1, MCP::FMULADDH_OP1);
    Found |= Match(AArch64::FMULHrr, 2, MCP::FMULADDH_OP2);
    break;
  case AArch64::FADDSrr:
    assert(Root.getOperand(1).isReg() && Root.getOperand(2).isReg() &&
           "FADDSrr does not have register operands");

    Found |= Match(AArch64::FMULSrr, 1, MCP::FMULADDS_OP1) ||
             Match(AArch64::FMULv1i32_indexed, 1, MCP::FMLAv1i32_indexed_OP1);

    Found |= Match(AArch64::FMULSrr, 2, MCP::FMULADDS_OP2) ||
             Match(AArch64::FMULv1i32_indexed, 2, MCP::FMLAv1i32_indexed_OP2);
    break;
  case AArch64::FADDDrr:
    Found |= Match(AArch64::FMULDrr, 1, MCP::FMULADDD_OP1) ||
             Match(AArch64::FMULv1i64_indexed, 1, MCP::FMLAv1i64_indexed_OP1);

    Found |= Match(AArch64::FMULDrr, 2, MCP::FMULADDD_OP2) ||
             Match(AArch64::FMULv1i64_indexed, 2, MCP::FMLAv1i64_indexed_OP2);
    break;
  case AArch64::FADDv4f16:
    Found |= Match(AArch64::FMULv4i16_indexed, 1, MCP::FMLAv4i16_indexed_OP1) ||
             Match(AArch64::FMULv4f16, 1, MCP::FMLAv4f16_OP1);

    Found |= Match(AArch64::FMULv4i16_indexed, 2, MCP::FMLAv4i16_indexed_OP2) ||
             Match(AArch64::FMULv4f16, 2, MCP::FMLAv4f16_OP2);
    break;
  case AArch64::FADDv8f16:
    Found |= Match(AArch64::FMULv8i16_indexed, 1, MCP::FMLAv8i16_indexed_OP1) ||
             Match(AArch64::FMULv8f16, 1, MCP::FMLAv8f16_OP1);

    Found |= Match(AArch64::FMULv8i16_indexed, 2, MCP::FMLAv8i16_indexed_OP2) ||
             Match(AArch64::FMULv8f16, 2, MCP::FMLAv8f16_OP2);
    break;
  case AArch64::FADDv2f32:
    Found |= Match(AArch64::FMULv2i32_indexed, 1, MCP::FMLAv2i32_indexed_OP1) ||
             Match(AArch64::FMULv2f32, 1, MCP::FMLAv2f32_OP1);

    Found |= Match(AArch64::FMULv2i32_indexed, 2, MCP::FMLAv2i32_indexed_OP2) ||
             Match(AArch64::FMULv2f32, 2, MCP::FMLAv2f32_OP2);
    break;
  case AArch64::FADDv2f64:
    Found |= Match(AArch64::FMULv2i64_indexed, 1, MCP::FMLAv2i64_indexed_OP1) ||
             Match(AArch64::FMULv2f64, 1, MCP::FMLAv2f64_OP1);

    Found |= Match(AArch64::FMULv2i64_indexed, 2, MCP::FMLAv2i64_indexed_OP2) ||
             Match(AArch64::FMULv2f64, 2, MCP::FMLAv2f64_OP2);
    break;
  case AArch64::FADDv4f32:
    Found |= Match(AArch64::FMULv4i32_indexed, 1, MCP::FMLAv4i32_indexed_OP1) ||
             Match(AArch64::FMULv4f32, 1, MCP::FMLAv4f32_OP1);

    Found |= Match(AArch64::FMULv4i32_indexed, 2, MCP::FMLAv4i32_indexed_OP2) ||
             Match(AArch64::FMULv4f32, 2, MCP::FMLAv4f32_OP2);
    break;
  case AArch64::FSUBHrr:
    Found  = Match(AArch64::FMULHrr, 1, MCP::FMULSUBH_OP1);
    Found |= Match(AArch64::FMULHrr, 2, MCP::FMULSUBH_OP2);
    Found |= Match(AArch64::FNMULHrr, 1, MCP::FNMULSUBH_OP1);
    break;
  case AArch64::FSUBSrr:
    Found = Match(AArch64::FMULSrr, 1, MCP::FMULSUBS_OP1);

    Found |= Match(AArch64::FMULSrr, 2, MCP::FMULSUBS_OP2) ||
             Match(AArch64::FMULv1i32_indexed, 2, MCP::FMLSv1i32_indexed_OP2);

    Found |= Match(AArch64::FNMULSrr, 1, MCP::FNMULSUBS_OP1);
    break;
  case AArch64::FSUBDrr:
    Found = Match(AArch64::FMULDrr, 1, MCP::FMULSUBD_OP1);

    Found |= Match(AArch64::FMULDrr, 2, MCP::FMULSUBD_OP2) ||
             Match(AArch64::FMULv1i64_indexed, 2, MCP::FMLSv1i64_indexed_OP2);

    Found |= Match(AArch64::FNMULDrr, 1, MCP::FNMULSUBD_OP1);
    break;
  case AArch64::FSUBv4f16:
    Found |= Match(AArch64::FMULv4i16_indexed, 2, MCP::FMLSv4i16_indexed_OP2) ||
             Match(AArch64::FMULv4f16, 2, MCP::FMLSv4f16_OP2);

    Found |= Match(AArch64::FMULv4i16_indexed, 1, MCP::FMLSv4i16_indexed_OP1) ||
             Match(AArch64::FMULv4f16, 1, MCP::FMLSv4f16_OP1);
    break;
  case AArch64::FSUBv8f16:
    Found |= Match(AArch64::FMULv8i16_indexed, 2, MCP::FMLSv8i16_indexed_OP2) ||
             Match(AArch64::FMULv8f16, 2, MCP::FMLSv8f16_OP2);

    Found |= Match(AArch64::FMULv8i16_indexed, 1, MCP::FMLSv8i16_indexed_OP1) ||
             Match(AArch64::FMULv8f16, 1, MCP::FMLSv8f16_OP1);
    break;
  case AArch64::FSUBv2f32:
    Found |= Match(AArch64::FMULv2i32_indexed, 2, MCP::FMLSv2i32_indexed_OP2) ||
             Match(AArch64::FMULv2f32, 2, MCP::FMLSv2f32_OP2);

    Found |= Match(AArch64::FMULv2i32_indexed, 1, MCP::FMLSv2i32_indexed_OP1) ||
             Match(AArch64::FMULv2f32, 1, MCP::FMLSv2f32_OP1);
    break;
  case AArch64::FSUBv2f64:
    Found |= Match(AArch64::FMULv2i64_indexed, 2, MCP::FMLSv2i64_indexed_OP2) ||
             Match(AArch64::FMULv2f64, 2, MCP::FMLSv2f64_OP2);

    Found |= Match(AArch64::FMULv2i64_indexed, 1, MCP::FMLSv2i64_indexed_OP1) ||
             Match(AArch64::FMULv2f64, 1, MCP::FMLSv2f64_OP1);
    break;
  case AArch64::FSUBv4f32:
    Found |= Match(AArch64::FMULv4i32_indexed, 2, MCP::FMLSv4i32_indexed_OP2) ||
             Match(AArch64::FMULv4f32, 2, MCP::FMLSv4f32_OP2);

    Found |= Match(AArch64::FMULv4i32_indexed, 1, MCP::FMLSv4i32_indexed_OP1) ||
             Match(AArch64::FMULv4f32, 1, MCP::FMLSv4f32_OP1);
    break;
  }
  return Found;
}

static bool getFMULPatterns(MachineInstr &Root,
                            SmallVectorImpl<unsigned> &Patterns) {
  MachineBasicBlock &MBB = *Root.getParent();
  bool Found = false;

  auto Match = [&](unsigned Opcode, int Operand, unsigned Pattern) -> bool {
    MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
    MachineOperand &MO = Root.getOperand(Operand);
    MachineInstr *MI = nullptr;
    if (MO.isReg() && MO.getReg().isVirtual())
      MI = MRI.getUniqueVRegDef(MO.getReg());
    // Ignore No-op COPYs in FMUL(COPY(DUP(..)))
    if (MI && MI->getOpcode() == TargetOpcode::COPY &&
        MI->getOperand(1).getReg().isVirtual())
      MI = MRI.getUniqueVRegDef(MI->getOperand(1).getReg());
    if (MI && MI->getOpcode() == Opcode) {
      Patterns.push_back(Pattern);
      return true;
    }
    return false;
  };

  typedef AArch64MachineCombinerPattern MCP;

  switch (Root.getOpcode()) {
  default:
    return false;
  case AArch64::FMULv2f32:
    Found = Match(AArch64::DUPv2i32lane, 1, MCP::FMULv2i32_indexed_OP1);
    Found |= Match(AArch64::DUPv2i32lane, 2, MCP::FMULv2i32_indexed_OP2);
    break;
  case AArch64::FMULv2f64:
    Found = Match(AArch64::DUPv2i64lane, 1, MCP::FMULv2i64_indexed_OP1);
    Found |= Match(AArch64::DUPv2i64lane, 2, MCP::FMULv2i64_indexed_OP2);
    break;
  case AArch64::FMULv4f16:
    Found = Match(AArch64::DUPv4i16lane, 1, MCP::FMULv4i16_indexed_OP1);
    Found |= Match(AArch64::DUPv4i16lane, 2, MCP::FMULv4i16_indexed_OP2);
    break;
  case AArch64::FMULv4f32:
    Found = Match(AArch64::DUPv4i32lane, 1, MCP::FMULv4i32_indexed_OP1);
    Found |= Match(AArch64::DUPv4i32lane, 2, MCP::FMULv4i32_indexed_OP2);
    break;
  case AArch64::FMULv8f16:
    Found = Match(AArch64::DUPv8i16lane, 1, MCP::FMULv8i16_indexed_OP1);
    Found |= Match(AArch64::DUPv8i16lane, 2, MCP::FMULv8i16_indexed_OP2);
    break;
  }

  return Found;
}

static bool getFNEGPatterns(MachineInstr &Root,
                            SmallVectorImpl<unsigned> &Patterns) {
  unsigned Opc = Root.getOpcode();
  MachineBasicBlock &MBB = *Root.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();

  auto Match = [&](unsigned Opcode, unsigned Pattern) -> bool {
    MachineOperand &MO = Root.getOperand(1);
    MachineInstr *MI = MRI.getUniqueVRegDef(MO.getReg());
    if (MI != nullptr && (MI->getOpcode() == Opcode) &&
        MRI.hasOneNonDBGUse(MI->getOperand(0).getReg()) &&
        Root.getFlag(MachineInstr::MIFlag::FmContract) &&
        Root.getFlag(MachineInstr::MIFlag::FmNsz) &&
        MI->getFlag(MachineInstr::MIFlag::FmContract) &&
        MI->getFlag(MachineInstr::MIFlag::FmNsz)) {
      Patterns.push_back(Pattern);
      return true;
    }
    return false;
  };

  switch (Opc) {
  default:
    break;
  case AArch64::FNEGDr:
    return Match(AArch64::FMADDDrrr, AArch64MachineCombinerPattern::FNMADD);
  case AArch64::FNEGSr:
    return Match(AArch64::FMADDSrrr, AArch64MachineCombinerPattern::FNMADD);
  }

  return false;
}

/// Return true when a code sequence can improve throughput. It
/// should be called only for instructions in loops.
/// \param Pattern - combiner pattern
bool AArch64InstrInfo::isThroughputPattern(unsigned Pattern) const {
  switch (Pattern) {
  default:
    break;
  case AArch64MachineCombinerPattern::FMULADDH_OP1:
  case AArch64MachineCombinerPattern::FMULADDH_OP2:
  case AArch64MachineCombinerPattern::FMULSUBH_OP1:
  case AArch64MachineCombinerPattern::FMULSUBH_OP2:
  case AArch64MachineCombinerPattern::FMULADDS_OP1:
  case AArch64MachineCombinerPattern::FMULADDS_OP2:
  case AArch64MachineCombinerPattern::FMULSUBS_OP1:
  case AArch64MachineCombinerPattern::FMULSUBS_OP2:
  case AArch64MachineCombinerPattern::FMULADDD_OP1:
  case AArch64MachineCombinerPattern::FMULADDD_OP2:
  case AArch64MachineCombinerPattern::FMULSUBD_OP1:
  case AArch64MachineCombinerPattern::FMULSUBD_OP2:
  case AArch64MachineCombinerPattern::FNMULSUBH_OP1:
  case AArch64MachineCombinerPattern::FNMULSUBS_OP1:
  case AArch64MachineCombinerPattern::FNMULSUBD_OP1:
  case AArch64MachineCombinerPattern::FMLAv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv4i16_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv8i16_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv1i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv1i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv1i64_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv1i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv4f16_OP2:
  case AArch64MachineCombinerPattern::FMLAv4f16_OP1:
  case AArch64MachineCombinerPattern::FMLAv8f16_OP1:
  case AArch64MachineCombinerPattern::FMLAv8f16_OP2:
  case AArch64MachineCombinerPattern::FMLAv2f32_OP2:
  case AArch64MachineCombinerPattern::FMLAv2f32_OP1:
  case AArch64MachineCombinerPattern::FMLAv2f64_OP1:
  case AArch64MachineCombinerPattern::FMLAv2f64_OP2:
  case AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv4f32_OP1:
  case AArch64MachineCombinerPattern::FMLAv4f32_OP2:
  case AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLSv4i16_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLSv8i16_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv1i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv1i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv2i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv4f16_OP1:
  case AArch64MachineCombinerPattern::FMLSv4f16_OP2:
  case AArch64MachineCombinerPattern::FMLSv8f16_OP1:
  case AArch64MachineCombinerPattern::FMLSv8f16_OP2:
  case AArch64MachineCombinerPattern::FMLSv2f32_OP2:
  case AArch64MachineCombinerPattern::FMLSv2f64_OP2:
  case AArch64MachineCombinerPattern::FMLSv4i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLSv4f32_OP2:
  case AArch64MachineCombinerPattern::FMULv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMULv2i64_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv2i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMULv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv4i16_indexed_OP2:
  case AArch64MachineCombinerPattern::FMULv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv4i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMULv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv8i16_indexed_OP2:
  case AArch64MachineCombinerPattern::MULADDv8i8_OP1:
  case AArch64MachineCombinerPattern::MULADDv8i8_OP2:
  case AArch64MachineCombinerPattern::MULADDv16i8_OP1:
  case AArch64MachineCombinerPattern::MULADDv16i8_OP2:
  case AArch64MachineCombinerPattern::MULADDv4i16_OP1:
  case AArch64MachineCombinerPattern::MULADDv4i16_OP2:
  case AArch64MachineCombinerPattern::MULADDv8i16_OP1:
  case AArch64MachineCombinerPattern::MULADDv8i16_OP2:
  case AArch64MachineCombinerPattern::MULADDv2i32_OP1:
  case AArch64MachineCombinerPattern::MULADDv2i32_OP2:
  case AArch64MachineCombinerPattern::MULADDv4i32_OP1:
  case AArch64MachineCombinerPattern::MULADDv4i32_OP2:
  case AArch64MachineCombinerPattern::MULSUBv8i8_OP1:
  case AArch64MachineCombinerPattern::MULSUBv8i8_OP2:
  case AArch64MachineCombinerPattern::MULSUBv16i8_OP1:
  case AArch64MachineCombinerPattern::MULSUBv16i8_OP2:
  case AArch64MachineCombinerPattern::MULSUBv4i16_OP1:
  case AArch64MachineCombinerPattern::MULSUBv4i16_OP2:
  case AArch64MachineCombinerPattern::MULSUBv8i16_OP1:
  case AArch64MachineCombinerPattern::MULSUBv8i16_OP2:
  case AArch64MachineCombinerPattern::MULSUBv2i32_OP1:
  case AArch64MachineCombinerPattern::MULSUBv2i32_OP2:
  case AArch64MachineCombinerPattern::MULSUBv4i32_OP1:
  case AArch64MachineCombinerPattern::MULSUBv4i32_OP2:
  case AArch64MachineCombinerPattern::MULADDv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::MULADDv4i16_indexed_OP2:
  case AArch64MachineCombinerPattern::MULADDv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::MULADDv8i16_indexed_OP2:
  case AArch64MachineCombinerPattern::MULADDv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::MULADDv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::MULADDv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::MULADDv4i32_indexed_OP2:
  case AArch64MachineCombinerPattern::MULSUBv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::MULSUBv4i16_indexed_OP2:
  case AArch64MachineCombinerPattern::MULSUBv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::MULSUBv8i16_indexed_OP2:
  case AArch64MachineCombinerPattern::MULSUBv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::MULSUBv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::MULSUBv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::MULSUBv4i32_indexed_OP2:
    return true;
  } // end switch (Pattern)
  return false;
}

/// Find other MI combine patterns.
static bool getMiscPatterns(MachineInstr &Root,
                            SmallVectorImpl<unsigned> &Patterns) {
  // A - (B + C)  ==>   (A - B) - C  or  (A - C) - B
  unsigned Opc = Root.getOpcode();
  MachineBasicBlock &MBB = *Root.getParent();

  switch (Opc) {
  case AArch64::SUBWrr:
  case AArch64::SUBSWrr:
  case AArch64::SUBXrr:
  case AArch64::SUBSXrr:
    // Found candidate root.
    break;
  default:
    return false;
  }

  if (isCombineInstrSettingFlag(Opc) &&
      Root.findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr, true) ==
          -1)
    return false;

  if (canCombine(MBB, Root.getOperand(2), AArch64::ADDWrr) ||
      canCombine(MBB, Root.getOperand(2), AArch64::ADDSWrr) ||
      canCombine(MBB, Root.getOperand(2), AArch64::ADDXrr) ||
      canCombine(MBB, Root.getOperand(2), AArch64::ADDSXrr)) {
    Patterns.push_back(AArch64MachineCombinerPattern::SUBADD_OP1);
    Patterns.push_back(AArch64MachineCombinerPattern::SUBADD_OP2);
    return true;
  }

  return false;
}

CombinerObjective
AArch64InstrInfo::getCombinerObjective(unsigned Pattern) const {
  switch (Pattern) {
  case AArch64MachineCombinerPattern::SUBADD_OP1:
  case AArch64MachineCombinerPattern::SUBADD_OP2:
    return CombinerObjective::MustReduceDepth;
  default:
    return TargetInstrInfo::getCombinerObjective(Pattern);
  }
}

/// Return true when there is potentially a faster code sequence for an
/// instruction chain ending in \p Root. All potential patterns are listed in
/// the \p Pattern vector. Pattern should be sorted in priority order since the
/// pattern evaluator stops checking as soon as it finds a faster sequence.

bool AArch64InstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root, SmallVectorImpl<unsigned> &Patterns,
    bool DoRegPressureReduce) const {
  // Integer patterns
  if (getMaddPatterns(Root, Patterns))
    return true;
  // Floating point patterns
  if (getFMULPatterns(Root, Patterns))
    return true;
  if (getFMAPatterns(Root, Patterns))
    return true;
  if (getFNEGPatterns(Root, Patterns))
    return true;

  // Other patterns
  if (getMiscPatterns(Root, Patterns))
    return true;

  return TargetInstrInfo::getMachineCombinerPatterns(Root, Patterns,
                                                     DoRegPressureReduce);
}

enum class FMAInstKind { Default, Indexed, Accumulator };
/// genFusedMultiply - Generate fused multiply instructions.
/// This function supports both integer and floating point instructions.
/// A typical example:
///  F|MUL I=A,B,0
///  F|ADD R,I,C
///  ==> F|MADD R,A,B,C
/// \param MF Containing MachineFunction
/// \param MRI Register information
/// \param TII Target information
/// \param Root is the F|ADD instruction
/// \param [out] InsInstrs is a vector of machine instructions and will
/// contain the generated madd instruction
/// \param IdxMulOpd is index of operand in Root that is the result of
/// the F|MUL. In the example above IdxMulOpd is 1.
/// \param MaddOpc the opcode fo the f|madd instruction
/// \param RC Register class of operands
/// \param kind of fma instruction (addressing mode) to be generated
/// \param ReplacedAddend is the result register from the instruction
/// replacing the non-combined operand, if any.
static MachineInstr *
genFusedMultiply(MachineFunction &MF, MachineRegisterInfo &MRI,
                 const TargetInstrInfo *TII, MachineInstr &Root,
                 SmallVectorImpl<MachineInstr *> &InsInstrs, unsigned IdxMulOpd,
                 unsigned MaddOpc, const TargetRegisterClass *RC,
                 FMAInstKind kind = FMAInstKind::Default,
                 const Register *ReplacedAddend = nullptr) {
  assert(IdxMulOpd == 1 || IdxMulOpd == 2);

  unsigned IdxOtherOpd = IdxMulOpd == 1 ? 2 : 1;
  MachineInstr *MUL = MRI.getUniqueVRegDef(Root.getOperand(IdxMulOpd).getReg());
  Register ResultReg = Root.getOperand(0).getReg();
  Register SrcReg0 = MUL->getOperand(1).getReg();
  bool Src0IsKill = MUL->getOperand(1).isKill();
  Register SrcReg1 = MUL->getOperand(2).getReg();
  bool Src1IsKill = MUL->getOperand(2).isKill();

  Register SrcReg2;
  bool Src2IsKill;
  if (ReplacedAddend) {
    // If we just generated a new addend, we must be it's only use.
    SrcReg2 = *ReplacedAddend;
    Src2IsKill = true;
  } else {
    SrcReg2 = Root.getOperand(IdxOtherOpd).getReg();
    Src2IsKill = Root.getOperand(IdxOtherOpd).isKill();
  }

  if (ResultReg.isVirtual())
    MRI.constrainRegClass(ResultReg, RC);
  if (SrcReg0.isVirtual())
    MRI.constrainRegClass(SrcReg0, RC);
  if (SrcReg1.isVirtual())
    MRI.constrainRegClass(SrcReg1, RC);
  if (SrcReg2.isVirtual())
    MRI.constrainRegClass(SrcReg2, RC);

  MachineInstrBuilder MIB;
  if (kind == FMAInstKind::Default)
    MIB = BuildMI(MF, MIMetadata(Root), TII->get(MaddOpc), ResultReg)
              .addReg(SrcReg0, getKillRegState(Src0IsKill))
              .addReg(SrcReg1, getKillRegState(Src1IsKill))
              .addReg(SrcReg2, getKillRegState(Src2IsKill));
  else if (kind == FMAInstKind::Indexed)
    MIB = BuildMI(MF, MIMetadata(Root), TII->get(MaddOpc), ResultReg)
              .addReg(SrcReg2, getKillRegState(Src2IsKill))
              .addReg(SrcReg0, getKillRegState(Src0IsKill))
              .addReg(SrcReg1, getKillRegState(Src1IsKill))
              .addImm(MUL->getOperand(3).getImm());
  else if (kind == FMAInstKind::Accumulator)
    MIB = BuildMI(MF, MIMetadata(Root), TII->get(MaddOpc), ResultReg)
              .addReg(SrcReg2, getKillRegState(Src2IsKill))
              .addReg(SrcReg0, getKillRegState(Src0IsKill))
              .addReg(SrcReg1, getKillRegState(Src1IsKill));
  else
    assert(false && "Invalid FMA instruction kind \n");
  // Insert the MADD (MADD, FMA, FMS, FMLA, FMSL)
  InsInstrs.push_back(MIB);
  return MUL;
}

static MachineInstr *
genFNegatedMAD(MachineFunction &MF, MachineRegisterInfo &MRI,
               const TargetInstrInfo *TII, MachineInstr &Root,
               SmallVectorImpl<MachineInstr *> &InsInstrs) {
  MachineInstr *MAD = MRI.getUniqueVRegDef(Root.getOperand(1).getReg());

  unsigned Opc = 0;
  const TargetRegisterClass *RC = MRI.getRegClass(MAD->getOperand(0).getReg());
  if (AArch64::FPR32RegClass.hasSubClassEq(RC))
    Opc = AArch64::FNMADDSrrr;
  else if (AArch64::FPR64RegClass.hasSubClassEq(RC))
    Opc = AArch64::FNMADDDrrr;
  else
    return nullptr;

  Register ResultReg = Root.getOperand(0).getReg();
  Register SrcReg0 = MAD->getOperand(1).getReg();
  Register SrcReg1 = MAD->getOperand(2).getReg();
  Register SrcReg2 = MAD->getOperand(3).getReg();
  bool Src0IsKill = MAD->getOperand(1).isKill();
  bool Src1IsKill = MAD->getOperand(2).isKill();
  bool Src2IsKill = MAD->getOperand(3).isKill();
  if (ResultReg.isVirtual())
    MRI.constrainRegClass(ResultReg, RC);
  if (SrcReg0.isVirtual())
    MRI.constrainRegClass(SrcReg0, RC);
  if (SrcReg1.isVirtual())
    MRI.constrainRegClass(SrcReg1, RC);
  if (SrcReg2.isVirtual())
    MRI.constrainRegClass(SrcReg2, RC);

  MachineInstrBuilder MIB =
      BuildMI(MF, MIMetadata(Root), TII->get(Opc), ResultReg)
          .addReg(SrcReg0, getKillRegState(Src0IsKill))
          .addReg(SrcReg1, getKillRegState(Src1IsKill))
          .addReg(SrcReg2, getKillRegState(Src2IsKill));
  InsInstrs.push_back(MIB);

  return MAD;
}

/// Fold (FMUL x (DUP y lane)) into (FMUL_indexed x y lane)
static MachineInstr *
genIndexedMultiply(MachineInstr &Root,
                   SmallVectorImpl<MachineInstr *> &InsInstrs,
                   unsigned IdxDupOp, unsigned MulOpc,
                   const TargetRegisterClass *RC, MachineRegisterInfo &MRI) {
  assert(((IdxDupOp == 1) || (IdxDupOp == 2)) &&
         "Invalid index of FMUL operand");

  MachineFunction &MF = *Root.getMF();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  MachineInstr *Dup =
      MF.getRegInfo().getUniqueVRegDef(Root.getOperand(IdxDupOp).getReg());

  if (Dup->getOpcode() == TargetOpcode::COPY)
    Dup = MRI.getUniqueVRegDef(Dup->getOperand(1).getReg());

  Register DupSrcReg = Dup->getOperand(1).getReg();
  MRI.clearKillFlags(DupSrcReg);
  MRI.constrainRegClass(DupSrcReg, RC);

  unsigned DupSrcLane = Dup->getOperand(2).getImm();

  unsigned IdxMulOp = IdxDupOp == 1 ? 2 : 1;
  MachineOperand &MulOp = Root.getOperand(IdxMulOp);

  Register ResultReg = Root.getOperand(0).getReg();

  MachineInstrBuilder MIB;
  MIB = BuildMI(MF, MIMetadata(Root), TII->get(MulOpc), ResultReg)
            .add(MulOp)
            .addReg(DupSrcReg)
            .addImm(DupSrcLane);

  InsInstrs.push_back(MIB);
  return &Root;
}

/// genFusedMultiplyAcc - Helper to generate fused multiply accumulate
/// instructions.
///
/// \see genFusedMultiply
static MachineInstr *genFusedMultiplyAcc(
    MachineFunction &MF, MachineRegisterInfo &MRI, const TargetInstrInfo *TII,
    MachineInstr &Root, SmallVectorImpl<MachineInstr *> &InsInstrs,
    unsigned IdxMulOpd, unsigned MaddOpc, const TargetRegisterClass *RC) {
  return genFusedMultiply(MF, MRI, TII, Root, InsInstrs, IdxMulOpd, MaddOpc, RC,
                          FMAInstKind::Accumulator);
}

/// genNeg - Helper to generate an intermediate negation of the second operand
/// of Root
static Register genNeg(MachineFunction &MF, MachineRegisterInfo &MRI,
                       const TargetInstrInfo *TII, MachineInstr &Root,
                       SmallVectorImpl<MachineInstr *> &InsInstrs,
                       DenseMap<unsigned, unsigned> &InstrIdxForVirtReg,
                       unsigned MnegOpc, const TargetRegisterClass *RC) {
  Register NewVR = MRI.createVirtualRegister(RC);
  MachineInstrBuilder MIB =
      BuildMI(MF, MIMetadata(Root), TII->get(MnegOpc), NewVR)
          .add(Root.getOperand(2));
  InsInstrs.push_back(MIB);

  assert(InstrIdxForVirtReg.empty());
  InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));

  return NewVR;
}

/// genFusedMultiplyAccNeg - Helper to generate fused multiply accumulate
/// instructions with an additional negation of the accumulator
static MachineInstr *genFusedMultiplyAccNeg(
    MachineFunction &MF, MachineRegisterInfo &MRI, const TargetInstrInfo *TII,
    MachineInstr &Root, SmallVectorImpl<MachineInstr *> &InsInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg, unsigned IdxMulOpd,
    unsigned MaddOpc, unsigned MnegOpc, const TargetRegisterClass *RC) {
  assert(IdxMulOpd == 1);

  Register NewVR =
      genNeg(MF, MRI, TII, Root, InsInstrs, InstrIdxForVirtReg, MnegOpc, RC);
  return genFusedMultiply(MF, MRI, TII, Root, InsInstrs, IdxMulOpd, MaddOpc, RC,
                          FMAInstKind::Accumulator, &NewVR);
}

/// genFusedMultiplyIdx - Helper to generate fused multiply accumulate
/// instructions.
///
/// \see genFusedMultiply
static MachineInstr *genFusedMultiplyIdx(
    MachineFunction &MF, MachineRegisterInfo &MRI, const TargetInstrInfo *TII,
    MachineInstr &Root, SmallVectorImpl<MachineInstr *> &InsInstrs,
    unsigned IdxMulOpd, unsigned MaddOpc, const TargetRegisterClass *RC) {
  return genFusedMultiply(MF, MRI, TII, Root, InsInstrs, IdxMulOpd, MaddOpc, RC,
                          FMAInstKind::Indexed);
}

/// genFusedMultiplyAccNeg - Helper to generate fused multiply accumulate
/// instructions with an additional negation of the accumulator
static MachineInstr *genFusedMultiplyIdxNeg(
    MachineFunction &MF, MachineRegisterInfo &MRI, const TargetInstrInfo *TII,
    MachineInstr &Root, SmallVectorImpl<MachineInstr *> &InsInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg, unsigned IdxMulOpd,
    unsigned MaddOpc, unsigned MnegOpc, const TargetRegisterClass *RC) {
  assert(IdxMulOpd == 1);

  Register NewVR =
      genNeg(MF, MRI, TII, Root, InsInstrs, InstrIdxForVirtReg, MnegOpc, RC);

  return genFusedMultiply(MF, MRI, TII, Root, InsInstrs, IdxMulOpd, MaddOpc, RC,
                          FMAInstKind::Indexed, &NewVR);
}

/// genMaddR - Generate madd instruction and combine mul and add using
/// an extra virtual register
/// Example - an ADD intermediate needs to be stored in a register:
///   MUL I=A,B,0
///   ADD R,I,Imm
///   ==> ORR  V, ZR, Imm
///   ==> MADD R,A,B,V
/// \param MF Containing MachineFunction
/// \param MRI Register information
/// \param TII Target information
/// \param Root is the ADD instruction
/// \param [out] InsInstrs is a vector of machine instructions and will
/// contain the generated madd instruction
/// \param IdxMulOpd is index of operand in Root that is the result of
/// the MUL. In the example above IdxMulOpd is 1.
/// \param MaddOpc the opcode fo the madd instruction
/// \param VR is a virtual register that holds the value of an ADD operand
/// (V in the example above).
/// \param RC Register class of operands
static MachineInstr *genMaddR(MachineFunction &MF, MachineRegisterInfo &MRI,
                              const TargetInstrInfo *TII, MachineInstr &Root,
                              SmallVectorImpl<MachineInstr *> &InsInstrs,
                              unsigned IdxMulOpd, unsigned MaddOpc, unsigned VR,
                              const TargetRegisterClass *RC) {
  assert(IdxMulOpd == 1 || IdxMulOpd == 2);

  MachineInstr *MUL = MRI.getUniqueVRegDef(Root.getOperand(IdxMulOpd).getReg());
  Register ResultReg = Root.getOperand(0).getReg();
  Register SrcReg0 = MUL->getOperand(1).getReg();
  bool Src0IsKill = MUL->getOperand(1).isKill();
  Register SrcReg1 = MUL->getOperand(2).getReg();
  bool Src1IsKill = MUL->getOperand(2).isKill();

  if (ResultReg.isVirtual())
    MRI.constrainRegClass(ResultReg, RC);
  if (SrcReg0.isVirtual())
    MRI.constrainRegClass(SrcReg0, RC);
  if (SrcReg1.isVirtual())
    MRI.constrainRegClass(SrcReg1, RC);
  if (Register::isVirtualRegister(VR))
    MRI.constrainRegClass(VR, RC);

  MachineInstrBuilder MIB =
      BuildMI(MF, MIMetadata(Root), TII->get(MaddOpc), ResultReg)
          .addReg(SrcReg0, getKillRegState(Src0IsKill))
          .addReg(SrcReg1, getKillRegState(Src1IsKill))
          .addReg(VR);
  // Insert the MADD
  InsInstrs.push_back(MIB);
  return MUL;
}

/// Do the following transformation
/// A - (B + C)  ==>   (A - B) - C
/// A - (B + C)  ==>   (A - C) - B
static void
genSubAdd2SubSub(MachineFunction &MF, MachineRegisterInfo &MRI,
                 const TargetInstrInfo *TII, MachineInstr &Root,
                 SmallVectorImpl<MachineInstr *> &InsInstrs,
                 SmallVectorImpl<MachineInstr *> &DelInstrs,
                 unsigned IdxOpd1,
                 DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) {
  assert(IdxOpd1 == 1 || IdxOpd1 == 2);
  unsigned IdxOtherOpd = IdxOpd1 == 1 ? 2 : 1;
  MachineInstr *AddMI = MRI.getUniqueVRegDef(Root.getOperand(2).getReg());

  Register ResultReg = Root.getOperand(0).getReg();
  Register RegA = Root.getOperand(1).getReg();
  bool RegAIsKill = Root.getOperand(1).isKill();
  Register RegB = AddMI->getOperand(IdxOpd1).getReg();
  bool RegBIsKill = AddMI->getOperand(IdxOpd1).isKill();
  Register RegC = AddMI->getOperand(IdxOtherOpd).getReg();
  bool RegCIsKill = AddMI->getOperand(IdxOtherOpd).isKill();
  Register NewVR = MRI.createVirtualRegister(MRI.getRegClass(RegA));

  unsigned Opcode = Root.getOpcode();
  if (Opcode == AArch64::SUBSWrr)
    Opcode = AArch64::SUBWrr;
  else if (Opcode == AArch64::SUBSXrr)
    Opcode = AArch64::SUBXrr;
  else
    assert((Opcode == AArch64::SUBWrr || Opcode == AArch64::SUBXrr) &&
           "Unexpected instruction opcode.");

  uint32_t Flags = Root.mergeFlagsWith(*AddMI);
  Flags &= ~MachineInstr::NoSWrap;
  Flags &= ~MachineInstr::NoUWrap;

  MachineInstrBuilder MIB1 =
      BuildMI(MF, MIMetadata(Root), TII->get(Opcode), NewVR)
          .addReg(RegA, getKillRegState(RegAIsKill))
          .addReg(RegB, getKillRegState(RegBIsKill))
          .setMIFlags(Flags);
  MachineInstrBuilder MIB2 =
      BuildMI(MF, MIMetadata(Root), TII->get(Opcode), ResultReg)
          .addReg(NewVR, getKillRegState(true))
          .addReg(RegC, getKillRegState(RegCIsKill))
          .setMIFlags(Flags);

  InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
  InsInstrs.push_back(MIB1);
  InsInstrs.push_back(MIB2);
  DelInstrs.push_back(AddMI);
  DelInstrs.push_back(&Root);
}

/// When getMachineCombinerPatterns() finds potential patterns,
/// this function generates the instructions that could replace the
/// original code sequence
void AArch64InstrInfo::genAlternativeCodeSequence(
    MachineInstr &Root, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  MachineBasicBlock &MBB = *Root.getParent();
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  MachineFunction &MF = *MBB.getParent();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  MachineInstr *MUL = nullptr;
  const TargetRegisterClass *RC;
  unsigned Opc;
  switch (Pattern) {
  default:
    // Reassociate instructions.
    TargetInstrInfo::genAlternativeCodeSequence(Root, Pattern, InsInstrs,
                                                DelInstrs, InstrIdxForVirtReg);
    return;
  case AArch64MachineCombinerPattern::SUBADD_OP1:
    // A - (B + C)
    // ==> (A - B) - C
    genSubAdd2SubSub(MF, MRI, TII, Root, InsInstrs, DelInstrs, 1,
                     InstrIdxForVirtReg);
    return;
  case AArch64MachineCombinerPattern::SUBADD_OP2:
    // A - (B + C)
    // ==> (A - C) - B
    genSubAdd2SubSub(MF, MRI, TII, Root, InsInstrs, DelInstrs, 2,
                     InstrIdxForVirtReg);
    return;
  case AArch64MachineCombinerPattern::MULADDW_OP1:
  case AArch64MachineCombinerPattern::MULADDX_OP1:
    // MUL I=A,B,0
    // ADD R,I,C
    // ==> MADD R,A,B,C
    // --- Create(MADD);
    if (Pattern == AArch64MachineCombinerPattern::MULADDW_OP1) {
      Opc = AArch64::MADDWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      Opc = AArch64::MADDXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDW_OP2:
  case AArch64MachineCombinerPattern::MULADDX_OP2:
    // MUL I=A,B,0
    // ADD R,C,I
    // ==> MADD R,A,B,C
    // --- Create(MADD);
    if (Pattern == AArch64MachineCombinerPattern::MULADDW_OP2) {
      Opc = AArch64::MADDWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      Opc = AArch64::MADDXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDWI_OP1:
  case AArch64MachineCombinerPattern::MULADDXI_OP1: {
    // MUL I=A,B,0
    // ADD R,I,Imm
    // ==> MOV V, Imm
    // ==> MADD R,A,B,V
    // --- Create(MADD);
    const TargetRegisterClass *OrrRC;
    unsigned BitSize, OrrOpc, ZeroReg;
    if (Pattern == AArch64MachineCombinerPattern::MULADDWI_OP1) {
      OrrOpc = AArch64::ORRWri;
      OrrRC = &AArch64::GPR32spRegClass;
      BitSize = 32;
      ZeroReg = AArch64::WZR;
      Opc = AArch64::MADDWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      OrrOpc = AArch64::ORRXri;
      OrrRC = &AArch64::GPR64spRegClass;
      BitSize = 64;
      ZeroReg = AArch64::XZR;
      Opc = AArch64::MADDXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    Register NewVR = MRI.createVirtualRegister(OrrRC);
    uint64_t Imm = Root.getOperand(2).getImm();

    if (Root.getOperand(3).isImm()) {
      unsigned Val = Root.getOperand(3).getImm();
      Imm = Imm << Val;
    }
    uint64_t UImm = SignExtend64(Imm, BitSize);
    // The immediate can be composed via a single instruction.
    SmallVector<AArch64_IMM::ImmInsnModel, 4> Insn;
    AArch64_IMM::expandMOVImm(UImm, BitSize, Insn);
    if (Insn.size() != 1)
      return;
    auto MovI = Insn.begin();
    MachineInstrBuilder MIB1;
    // MOV is an alias for one of three instructions: movz, movn, and orr.
    if (MovI->Opcode == OrrOpc)
      MIB1 = BuildMI(MF, MIMetadata(Root), TII->get(OrrOpc), NewVR)
                 .addReg(ZeroReg)
                 .addImm(MovI->Op2);
    else {
      if (BitSize == 32)
        assert((MovI->Opcode == AArch64::MOVNWi ||
                MovI->Opcode == AArch64::MOVZWi) &&
               "Expected opcode");
      else
        assert((MovI->Opcode == AArch64::MOVNXi ||
                MovI->Opcode == AArch64::MOVZXi) &&
               "Expected opcode");
      MIB1 = BuildMI(MF, MIMetadata(Root), TII->get(MovI->Opcode), NewVR)
                 .addImm(MovI->Op1)
                 .addImm(MovI->Op2);
    }
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    MUL = genMaddR(MF, MRI, TII, Root, InsInstrs, 1, Opc, NewVR, RC);
    break;
  }
  case AArch64MachineCombinerPattern::MULSUBW_OP1:
  case AArch64MachineCombinerPattern::MULSUBX_OP1: {
    // MUL I=A,B,0
    // SUB R,I, C
    // ==> SUB  V, 0, C
    // ==> MADD R,A,B,V // = -C + A*B
    // --- Create(MADD);
    const TargetRegisterClass *SubRC;
    unsigned SubOpc, ZeroReg;
    if (Pattern == AArch64MachineCombinerPattern::MULSUBW_OP1) {
      SubOpc = AArch64::SUBWrr;
      SubRC = &AArch64::GPR32spRegClass;
      ZeroReg = AArch64::WZR;
      Opc = AArch64::MADDWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      SubOpc = AArch64::SUBXrr;
      SubRC = &AArch64::GPR64spRegClass;
      ZeroReg = AArch64::XZR;
      Opc = AArch64::MADDXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    Register NewVR = MRI.createVirtualRegister(SubRC);
    // SUB NewVR, 0, C
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(SubOpc), NewVR)
            .addReg(ZeroReg)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    MUL = genMaddR(MF, MRI, TII, Root, InsInstrs, 1, Opc, NewVR, RC);
    break;
  }
  case AArch64MachineCombinerPattern::MULSUBW_OP2:
  case AArch64MachineCombinerPattern::MULSUBX_OP2:
    // MUL I=A,B,0
    // SUB R,C,I
    // ==> MSUB R,A,B,C (computes C - A*B)
    // --- Create(MSUB);
    if (Pattern == AArch64MachineCombinerPattern::MULSUBW_OP2) {
      Opc = AArch64::MSUBWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      Opc = AArch64::MSUBXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBWI_OP1:
  case AArch64MachineCombinerPattern::MULSUBXI_OP1: {
    // MUL I=A,B,0
    // SUB R,I, Imm
    // ==> MOV  V, -Imm
    // ==> MADD R,A,B,V // = -Imm + A*B
    // --- Create(MADD);
    const TargetRegisterClass *OrrRC;
    unsigned BitSize, OrrOpc, ZeroReg;
    if (Pattern == AArch64MachineCombinerPattern::MULSUBWI_OP1) {
      OrrOpc = AArch64::ORRWri;
      OrrRC = &AArch64::GPR32spRegClass;
      BitSize = 32;
      ZeroReg = AArch64::WZR;
      Opc = AArch64::MADDWrrr;
      RC = &AArch64::GPR32RegClass;
    } else {
      OrrOpc = AArch64::ORRXri;
      OrrRC = &AArch64::GPR64spRegClass;
      BitSize = 64;
      ZeroReg = AArch64::XZR;
      Opc = AArch64::MADDXrrr;
      RC = &AArch64::GPR64RegClass;
    }
    Register NewVR = MRI.createVirtualRegister(OrrRC);
    uint64_t Imm = Root.getOperand(2).getImm();
    if (Root.getOperand(3).isImm()) {
      unsigned Val = Root.getOperand(3).getImm();
      Imm = Imm << Val;
    }
    uint64_t UImm = SignExtend64(-Imm, BitSize);
    // The immediate can be composed via a single instruction.
    SmallVector<AArch64_IMM::ImmInsnModel, 4> Insn;
    AArch64_IMM::expandMOVImm(UImm, BitSize, Insn);
    if (Insn.size() != 1)
      return;
    auto MovI = Insn.begin();
    MachineInstrBuilder MIB1;
    // MOV is an alias for one of three instructions: movz, movn, and orr.
    if (MovI->Opcode == OrrOpc)
      MIB1 = BuildMI(MF, MIMetadata(Root), TII->get(OrrOpc), NewVR)
                 .addReg(ZeroReg)
                 .addImm(MovI->Op2);
    else {
      if (BitSize == 32)
        assert((MovI->Opcode == AArch64::MOVNWi ||
                MovI->Opcode == AArch64::MOVZWi) &&
               "Expected opcode");
      else
        assert((MovI->Opcode == AArch64::MOVNXi ||
                MovI->Opcode == AArch64::MOVZXi) &&
               "Expected opcode");
      MIB1 = BuildMI(MF, MIMetadata(Root), TII->get(MovI->Opcode), NewVR)
                 .addImm(MovI->Op1)
                 .addImm(MovI->Op2);
    }
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    MUL = genMaddR(MF, MRI, TII, Root, InsInstrs, 1, Opc, NewVR, RC);
    break;
  }

  case AArch64MachineCombinerPattern::MULADDv8i8_OP1:
    Opc = AArch64::MLAv8i8;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv8i8_OP2:
    Opc = AArch64::MLAv8i8;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv16i8_OP1:
    Opc = AArch64::MLAv16i8;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv16i8_OP2:
    Opc = AArch64::MLAv16i8;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i16_OP1:
    Opc = AArch64::MLAv4i16;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i16_OP2:
    Opc = AArch64::MLAv4i16;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv8i16_OP1:
    Opc = AArch64::MLAv8i16;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv8i16_OP2:
    Opc = AArch64::MLAv8i16;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv2i32_OP1:
    Opc = AArch64::MLAv2i32;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv2i32_OP2:
    Opc = AArch64::MLAv2i32;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i32_OP1:
    Opc = AArch64::MLAv4i32;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i32_OP2:
    Opc = AArch64::MLAv4i32;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::MULSUBv8i8_OP1:
    Opc = AArch64::MLAv8i8;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv8i8,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv8i8_OP2:
    Opc = AArch64::MLSv8i8;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv16i8_OP1:
    Opc = AArch64::MLAv16i8;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv16i8,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv16i8_OP2:
    Opc = AArch64::MLSv16i8;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i16_OP1:
    Opc = AArch64::MLAv4i16;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv4i16,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i16_OP2:
    Opc = AArch64::MLSv4i16;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv8i16_OP1:
    Opc = AArch64::MLAv8i16;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv8i16,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv8i16_OP2:
    Opc = AArch64::MLSv8i16;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv2i32_OP1:
    Opc = AArch64::MLAv2i32;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv2i32,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv2i32_OP2:
    Opc = AArch64::MLSv2i32;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i32_OP1:
    Opc = AArch64::MLAv4i32;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAccNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv4i32,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i32_OP2:
    Opc = AArch64::MLSv4i32;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyAcc(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::MULADDv4i16_indexed_OP1:
    Opc = AArch64::MLAv4i16_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i16_indexed_OP2:
    Opc = AArch64::MLAv4i16_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv8i16_indexed_OP1:
    Opc = AArch64::MLAv8i16_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv8i16_indexed_OP2:
    Opc = AArch64::MLAv8i16_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv2i32_indexed_OP1:
    Opc = AArch64::MLAv2i32_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv2i32_indexed_OP2:
    Opc = AArch64::MLAv2i32_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i32_indexed_OP1:
    Opc = AArch64::MLAv4i32_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULADDv4i32_indexed_OP2:
    Opc = AArch64::MLAv4i32_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::MULSUBv4i16_indexed_OP1:
    Opc = AArch64::MLAv4i16_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdxNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv4i16,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i16_indexed_OP2:
    Opc = AArch64::MLSv4i16_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv8i16_indexed_OP1:
    Opc = AArch64::MLAv8i16_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdxNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv8i16,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv8i16_indexed_OP2:
    Opc = AArch64::MLSv8i16_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv2i32_indexed_OP1:
    Opc = AArch64::MLAv2i32_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdxNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv2i32,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv2i32_indexed_OP2:
    Opc = AArch64::MLSv2i32_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i32_indexed_OP1:
    Opc = AArch64::MLAv4i32_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdxNeg(MF, MRI, TII, Root, InsInstrs,
                                 InstrIdxForVirtReg, 1, Opc, AArch64::NEGv4i32,
                                 RC);
    break;
  case AArch64MachineCombinerPattern::MULSUBv4i32_indexed_OP2:
    Opc = AArch64::MLSv4i32_indexed;
    RC = &AArch64::FPR128RegClass;
    MUL = genFusedMultiplyIdx(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  // Floating Point Support
  case AArch64MachineCombinerPattern::FMULADDH_OP1:
    Opc = AArch64::FMADDHrrr;
    RC = &AArch64::FPR16RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULADDS_OP1:
    Opc = AArch64::FMADDSrrr;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULADDD_OP1:
    Opc = AArch64::FMADDDrrr;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::FMULADDH_OP2:
    Opc = AArch64::FMADDHrrr;
    RC = &AArch64::FPR16RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULADDS_OP2:
    Opc = AArch64::FMADDSrrr;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULADDD_OP2:
    Opc = AArch64::FMADDDrrr;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::FMLAv1i32_indexed_OP1:
    Opc = AArch64::FMLAv1i32_indexed;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv1i32_indexed_OP2:
    Opc = AArch64::FMLAv1i32_indexed;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLAv1i64_indexed_OP1:
    Opc = AArch64::FMLAv1i64_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv1i64_indexed_OP2:
    Opc = AArch64::FMLAv1i64_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLAv4i16_indexed_OP1:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLAv4i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv4f16_OP1:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLAv4f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Accumulator);
    break;
  case AArch64MachineCombinerPattern::FMLAv4i16_indexed_OP2:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLAv4i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv4f16_OP2:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLAv4f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Accumulator);
    break;

  case AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv2f32_OP1:
    RC = &AArch64::FPR64RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP1) {
      Opc = AArch64::FMLAv2i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv2f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;
  case AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv2f32_OP2:
    RC = &AArch64::FPR64RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv2i32_indexed_OP2) {
      Opc = AArch64::FMLAv2i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv2f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMLAv8i16_indexed_OP1:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLAv8i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv8f16_OP1:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLAv8f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                           FMAInstKind::Accumulator);
    break;
  case AArch64MachineCombinerPattern::FMLAv8i16_indexed_OP2:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLAv8i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;
  case AArch64MachineCombinerPattern::FMLAv8f16_OP2:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLAv8f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Accumulator);
    break;

  case AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv2f64_OP1:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP1) {
      Opc = AArch64::FMLAv2i64_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv2f64;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;
  case AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv2f64_OP2:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv2i64_indexed_OP2) {
      Opc = AArch64::FMLAv2i64_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv2f64;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMLAv4f32_OP1:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP1) {
      Opc = AArch64::FMLAv4i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv4f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP2:
  case AArch64MachineCombinerPattern::FMLAv4f32_OP2:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLAv4i32_indexed_OP2) {
      Opc = AArch64::FMLAv4i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLAv4f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMULSUBH_OP1:
    Opc = AArch64::FNMSUBHrrr;
    RC = &AArch64::FPR16RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULSUBS_OP1:
    Opc = AArch64::FNMSUBSrrr;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULSUBD_OP1:
    Opc = AArch64::FNMSUBDrrr;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::FNMULSUBH_OP1:
    Opc = AArch64::FNMADDHrrr;
    RC = &AArch64::FPR16RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FNMULSUBS_OP1:
    Opc = AArch64::FNMADDSrrr;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FNMULSUBD_OP1:
    Opc = AArch64::FNMADDDrrr;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::FMULSUBH_OP2:
    Opc = AArch64::FMSUBHrrr;
    RC = &AArch64::FPR16RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULSUBS_OP2:
    Opc = AArch64::FMSUBSrrr;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;
  case AArch64MachineCombinerPattern::FMULSUBD_OP2:
    Opc = AArch64::FMSUBDrrr;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC);
    break;

  case AArch64MachineCombinerPattern::FMLSv1i32_indexed_OP2:
    Opc = AArch64::FMLSv1i32_indexed;
    RC = &AArch64::FPR32RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLSv1i64_indexed_OP2:
    Opc = AArch64::FMLSv1i64_indexed;
    RC = &AArch64::FPR64RegClass;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLSv4f16_OP1:
  case AArch64MachineCombinerPattern::FMLSv4i16_indexed_OP1: {
    RC = &AArch64::FPR64RegClass;
    Register NewVR = MRI.createVirtualRegister(RC);
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(AArch64::FNEGv4f16), NewVR)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    if (Pattern == AArch64MachineCombinerPattern::FMLSv4f16_OP1) {
      Opc = AArch64::FMLAv4f16;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator, &NewVR);
    } else {
      Opc = AArch64::FMLAv4i16_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed, &NewVR);
    }
    break;
  }
  case AArch64MachineCombinerPattern::FMLSv4f16_OP2:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLSv4f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Accumulator);
    break;
  case AArch64MachineCombinerPattern::FMLSv4i16_indexed_OP2:
    RC = &AArch64::FPR64RegClass;
    Opc = AArch64::FMLSv4i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLSv2f32_OP2:
  case AArch64MachineCombinerPattern::FMLSv2i32_indexed_OP2:
    RC = &AArch64::FPR64RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLSv2i32_indexed_OP2) {
      Opc = AArch64::FMLSv2i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLSv2f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMLSv8f16_OP1:
  case AArch64MachineCombinerPattern::FMLSv8i16_indexed_OP1: {
    RC = &AArch64::FPR128RegClass;
    Register NewVR = MRI.createVirtualRegister(RC);
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(AArch64::FNEGv8f16), NewVR)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    if (Pattern == AArch64MachineCombinerPattern::FMLSv8f16_OP1) {
      Opc = AArch64::FMLAv8f16;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator, &NewVR);
    } else {
      Opc = AArch64::FMLAv8i16_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed, &NewVR);
    }
    break;
  }
  case AArch64MachineCombinerPattern::FMLSv8f16_OP2:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLSv8f16;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Accumulator);
    break;
  case AArch64MachineCombinerPattern::FMLSv8i16_indexed_OP2:
    RC = &AArch64::FPR128RegClass;
    Opc = AArch64::FMLSv8i16_indexed;
    MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                           FMAInstKind::Indexed);
    break;

  case AArch64MachineCombinerPattern::FMLSv2f64_OP2:
  case AArch64MachineCombinerPattern::FMLSv2i64_indexed_OP2:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLSv2i64_indexed_OP2) {
      Opc = AArch64::FMLSv2i64_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLSv2f64;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;

  case AArch64MachineCombinerPattern::FMLSv4f32_OP2:
  case AArch64MachineCombinerPattern::FMLSv4i32_indexed_OP2:
    RC = &AArch64::FPR128RegClass;
    if (Pattern == AArch64MachineCombinerPattern::FMLSv4i32_indexed_OP2) {
      Opc = AArch64::FMLSv4i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Indexed);
    } else {
      Opc = AArch64::FMLSv4f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 2, Opc, RC,
                             FMAInstKind::Accumulator);
    }
    break;
  case AArch64MachineCombinerPattern::FMLSv2f32_OP1:
  case AArch64MachineCombinerPattern::FMLSv2i32_indexed_OP1: {
    RC = &AArch64::FPR64RegClass;
    Register NewVR = MRI.createVirtualRegister(RC);
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(AArch64::FNEGv2f32), NewVR)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    if (Pattern == AArch64MachineCombinerPattern::FMLSv2i32_indexed_OP1) {
      Opc = AArch64::FMLAv2i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed, &NewVR);
    } else {
      Opc = AArch64::FMLAv2f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator, &NewVR);
    }
    break;
  }
  case AArch64MachineCombinerPattern::FMLSv4f32_OP1:
  case AArch64MachineCombinerPattern::FMLSv4i32_indexed_OP1: {
    RC = &AArch64::FPR128RegClass;
    Register NewVR = MRI.createVirtualRegister(RC);
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(AArch64::FNEGv4f32), NewVR)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    if (Pattern == AArch64MachineCombinerPattern::FMLSv4i32_indexed_OP1) {
      Opc = AArch64::FMLAv4i32_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed, &NewVR);
    } else {
      Opc = AArch64::FMLAv4f32;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator, &NewVR);
    }
    break;
  }
  case AArch64MachineCombinerPattern::FMLSv2f64_OP1:
  case AArch64MachineCombinerPattern::FMLSv2i64_indexed_OP1: {
    RC = &AArch64::FPR128RegClass;
    Register NewVR = MRI.createVirtualRegister(RC);
    MachineInstrBuilder MIB1 =
        BuildMI(MF, MIMetadata(Root), TII->get(AArch64::FNEGv2f64), NewVR)
            .add(Root.getOperand(2));
    InsInstrs.push_back(MIB1);
    InstrIdxForVirtReg.insert(std::make_pair(NewVR, 0));
    if (Pattern == AArch64MachineCombinerPattern::FMLSv2i64_indexed_OP1) {
      Opc = AArch64::FMLAv2i64_indexed;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Indexed, &NewVR);
    } else {
      Opc = AArch64::FMLAv2f64;
      MUL = genFusedMultiply(MF, MRI, TII, Root, InsInstrs, 1, Opc, RC,
                             FMAInstKind::Accumulator, &NewVR);
    }
    break;
  }
  case AArch64MachineCombinerPattern::FMULv2i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv2i32_indexed_OP2: {
    unsigned IdxDupOp =
        (Pattern == AArch64MachineCombinerPattern::FMULv2i32_indexed_OP1) ? 1
                                                                          : 2;
    genIndexedMultiply(Root, InsInstrs, IdxDupOp, AArch64::FMULv2i32_indexed,
                       &AArch64::FPR128RegClass, MRI);
    break;
  }
  case AArch64MachineCombinerPattern::FMULv2i64_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv2i64_indexed_OP2: {
    unsigned IdxDupOp =
        (Pattern == AArch64MachineCombinerPattern::FMULv2i64_indexed_OP1) ? 1
                                                                          : 2;
    genIndexedMultiply(Root, InsInstrs, IdxDupOp, AArch64::FMULv2i64_indexed,
                       &AArch64::FPR128RegClass, MRI);
    break;
  }
  case AArch64MachineCombinerPattern::FMULv4i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv4i16_indexed_OP2: {
    unsigned IdxDupOp =
        (Pattern == AArch64MachineCombinerPattern::FMULv4i16_indexed_OP1) ? 1
                                                                          : 2;
    genIndexedMultiply(Root, InsInstrs, IdxDupOp, AArch64::FMULv4i16_indexed,
                       &AArch64::FPR128_loRegClass, MRI);
    break;
  }
  case AArch64MachineCombinerPattern::FMULv4i32_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv4i32_indexed_OP2: {
    unsigned IdxDupOp =
        (Pattern == AArch64MachineCombinerPattern::FMULv4i32_indexed_OP1) ? 1
                                                                          : 2;
    genIndexedMultiply(Root, InsInstrs, IdxDupOp, AArch64::FMULv4i32_indexed,
                       &AArch64::FPR128RegClass, MRI);
    break;
  }
  case AArch64MachineCombinerPattern::FMULv8i16_indexed_OP1:
  case AArch64MachineCombinerPattern::FMULv8i16_indexed_OP2: {
    unsigned IdxDupOp =
        (Pattern == AArch64MachineCombinerPattern::FMULv8i16_indexed_OP1) ? 1
                                                                          : 2;
    genIndexedMultiply(Root, InsInstrs, IdxDupOp, AArch64::FMULv8i16_indexed,
                       &AArch64::FPR128_loRegClass, MRI);
    break;
  }
  case AArch64MachineCombinerPattern::FNMADD: {
    MUL = genFNegatedMAD(MF, MRI, TII, Root, InsInstrs);
    break;
  }

  } // end switch (Pattern)
  // Record MUL and ADD/SUB for deletion
  if (MUL)
    DelInstrs.push_back(MUL);
  DelInstrs.push_back(&Root);

  // Set the flags on the inserted instructions to be the merged flags of the
  // instructions that we have combined.
  uint32_t Flags = Root.getFlags();
  if (MUL)
    Flags = Root.mergeFlagsWith(*MUL);
  for (auto *MI : InsInstrs)
    MI->setFlags(Flags);
}

/// Replace csincr-branch sequence by simple conditional branch
///
/// Examples:
/// 1. \code
///   csinc  w9, wzr, wzr, <condition code>
///   tbnz   w9, #0, 0x44
///    \endcode
/// to
///    \code
///   b.<inverted condition code>
///    \endcode
///
/// 2. \code
///   csinc w9, wzr, wzr, <condition code>
///   tbz   w9, #0, 0x44
///    \endcode
/// to
///    \code
///   b.<condition code>
///    \endcode
///
/// Replace compare and branch sequence by TBZ/TBNZ instruction when the
/// compare's constant operand is power of 2.
///
/// Examples:
///    \code
///   and  w8, w8, #0x400
///   cbnz w8, L1
///    \endcode
/// to
///    \code
///   tbnz w8, #10, L1
///    \endcode
///
/// \param  MI Conditional Branch
/// \return True when the simple conditional branch is generated
///
bool AArch64InstrInfo::optimizeCondBranch(MachineInstr &MI) const {
  bool IsNegativeBranch = false;
  bool IsTestAndBranch = false;
  unsigned TargetBBInMI = 0;
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("Unknown branch instruction?");
  case AArch64::Bcc:
    return false;
  case AArch64::CBZW:
  case AArch64::CBZX:
    TargetBBInMI = 1;
    break;
  case AArch64::CBNZW:
  case AArch64::CBNZX:
    TargetBBInMI = 1;
    IsNegativeBranch = true;
    break;
  case AArch64::TBZW:
  case AArch64::TBZX:
    TargetBBInMI = 2;
    IsTestAndBranch = true;
    break;
  case AArch64::TBNZW:
  case AArch64::TBNZX:
    TargetBBInMI = 2;
    IsNegativeBranch = true;
    IsTestAndBranch = true;
    break;
  }
  // So we increment a zero register and test for bits other
  // than bit 0? Conservatively bail out in case the verifier
  // missed this case.
  if (IsTestAndBranch && MI.getOperand(1).getImm())
    return false;

  // Find Definition.
  assert(MI.getParent() && "Incomplete machine instruciton\n");
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  Register VReg = MI.getOperand(0).getReg();
  if (!VReg.isVirtual())
    return false;

  MachineInstr *DefMI = MRI->getVRegDef(VReg);

  // Look through COPY instructions to find definition.
  while (DefMI->isCopy()) {
    Register CopyVReg = DefMI->getOperand(1).getReg();
    if (!MRI->hasOneNonDBGUse(CopyVReg))
      return false;
    if (!MRI->hasOneDef(CopyVReg))
      return false;
    DefMI = MRI->getVRegDef(CopyVReg);
  }

  switch (DefMI->getOpcode()) {
  default:
    return false;
  // Fold AND into a TBZ/TBNZ if constant operand is power of 2.
  case AArch64::ANDWri:
  case AArch64::ANDXri: {
    if (IsTestAndBranch)
      return false;
    if (DefMI->getParent() != MBB)
      return false;
    if (!MRI->hasOneNonDBGUse(VReg))
      return false;

    bool Is32Bit = (DefMI->getOpcode() == AArch64::ANDWri);
    uint64_t Mask = AArch64_AM::decodeLogicalImmediate(
        DefMI->getOperand(2).getImm(), Is32Bit ? 32 : 64);
    if (!isPowerOf2_64(Mask))
      return false;

    MachineOperand &MO = DefMI->getOperand(1);
    Register NewReg = MO.getReg();
    if (!NewReg.isVirtual())
      return false;

    assert(!MRI->def_empty(NewReg) && "Register must be defined.");

    MachineBasicBlock &RefToMBB = *MBB;
    MachineBasicBlock *TBB = MI.getOperand(1).getMBB();
    DebugLoc DL = MI.getDebugLoc();
    unsigned Imm = Log2_64(Mask);
    unsigned Opc = (Imm < 32)
                       ? (IsNegativeBranch ? AArch64::TBNZW : AArch64::TBZW)
                       : (IsNegativeBranch ? AArch64::TBNZX : AArch64::TBZX);
    MachineInstr *NewMI = BuildMI(RefToMBB, MI, DL, get(Opc))
                              .addReg(NewReg)
                              .addImm(Imm)
                              .addMBB(TBB);
    // Register lives on to the CBZ now.
    MO.setIsKill(false);

    // For immediate smaller than 32, we need to use the 32-bit
    // variant (W) in all cases. Indeed the 64-bit variant does not
    // allow to encode them.
    // Therefore, if the input register is 64-bit, we need to take the
    // 32-bit sub-part.
    if (!Is32Bit && Imm < 32)
      NewMI->getOperand(0).setSubReg(AArch64::sub_32);
    MI.eraseFromParent();
    return true;
  }
  // Look for CSINC
  case AArch64::CSINCWr:
  case AArch64::CSINCXr: {
    if (!(DefMI->getOperand(1).getReg() == AArch64::WZR &&
          DefMI->getOperand(2).getReg() == AArch64::WZR) &&
        !(DefMI->getOperand(1).getReg() == AArch64::XZR &&
          DefMI->getOperand(2).getReg() == AArch64::XZR))
      return false;

    if (DefMI->findRegisterDefOperandIdx(AArch64::NZCV, /*TRI=*/nullptr,
                                         true) != -1)
      return false;

    AArch64CC::CondCode CC = (AArch64CC::CondCode)DefMI->getOperand(3).getImm();
    // Convert only when the condition code is not modified between
    // the CSINC and the branch. The CC may be used by other
    // instructions in between.
    if (areCFlagsAccessedBetweenInstrs(DefMI, MI, &getRegisterInfo(), AK_Write))
      return false;
    MachineBasicBlock &RefToMBB = *MBB;
    MachineBasicBlock *TBB = MI.getOperand(TargetBBInMI).getMBB();
    DebugLoc DL = MI.getDebugLoc();
    if (IsNegativeBranch)
      CC = AArch64CC::getInvertedCondCode(CC);
    BuildMI(RefToMBB, MI, DL, get(AArch64::Bcc)).addImm(CC).addMBB(TBB);
    MI.eraseFromParent();
    return true;
  }
  }
}

std::pair<unsigned, unsigned>
AArch64InstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  const unsigned Mask = AArch64II::MO_FRAGMENT;
  return std::make_pair(TF & Mask, TF & ~Mask);
}

ArrayRef<std::pair<unsigned, const char *>>
AArch64InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace AArch64II;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_PAGE, "aarch64-page"}, {MO_PAGEOFF, "aarch64-pageoff"},
      {MO_G3, "aarch64-g3"},     {MO_G2, "aarch64-g2"},
      {MO_G1, "aarch64-g1"},     {MO_G0, "aarch64-g0"},
      {MO_HI12, "aarch64-hi12"}};
  return ArrayRef(TargetFlags);
}

ArrayRef<std::pair<unsigned, const char *>>
AArch64InstrInfo::getSerializableBitmaskMachineOperandTargetFlags() const {
  using namespace AArch64II;

  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_COFFSTUB, "aarch64-coffstub"},
      {MO_GOT, "aarch64-got"},
      {MO_NC, "aarch64-nc"},
      {MO_S, "aarch64-s"},
      {MO_TLS, "aarch64-tls"},
      {MO_DLLIMPORT, "aarch64-dllimport"},
      {MO_PREL, "aarch64-prel"},
      {MO_TAGGED, "aarch64-tagged"},
      {MO_ARM64EC_CALLMANGLE, "aarch64-arm64ec-callmangle"},
  };
  return ArrayRef(TargetFlags);
}

ArrayRef<std::pair<MachineMemOperand::Flags, const char *>>
AArch64InstrInfo::getSerializableMachineMemOperandTargetFlags() const {
  static const std::pair<MachineMemOperand::Flags, const char *> TargetFlags[] =
      {{MOSuppressPair, "aarch64-suppress-pair"},
       {MOStridedAccess, "aarch64-strided-access"}};
  return ArrayRef(TargetFlags);
}

/// Constants defining how certain sequences should be outlined.
/// This encompasses how an outlined function should be called, and what kind of
/// frame should be emitted for that outlined function.
///
/// \p MachineOutlinerDefault implies that the function should be called with
/// a save and restore of LR to the stack.
///
/// That is,
///
/// I1     Save LR                    OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3     Restore LR                 I2
///                                   I3
///                                   RET
///
/// * Call construction overhead: 3 (save + BL + restore)
/// * Frame construction overhead: 1 (ret)
/// * Requires stack fixups? Yes
///
/// \p MachineOutlinerTailCall implies that the function is being created from
/// a sequence of instructions ending in a return.
///
/// That is,
///
/// I1                             OUTLINED_FUNCTION:
/// I2 --> B OUTLINED_FUNCTION     I1
/// RET                            I2
///                                RET
///
/// * Call construction overhead: 1 (B)
/// * Frame construction overhead: 0 (Return included in sequence)
/// * Requires stack fixups? No
///
/// \p MachineOutlinerNoLRSave implies that the function should be called using
/// a BL instruction, but doesn't require LR to be saved and restored. This
/// happens when LR is known to be dead.
///
/// That is,
///
/// I1                                OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3                                I2
///                                   I3
///                                   RET
///
/// * Call construction overhead: 1 (BL)
/// * Frame construction overhead: 1 (RET)
/// * Requires stack fixups? No
///
/// \p MachineOutlinerThunk implies that the function is being created from
/// a sequence of instructions ending in a call. The outlined function is
/// called with a BL instruction, and the outlined function tail-calls the
/// original call destination.
///
/// That is,
///
/// I1                                OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// BL f                              I2
///                                   B f
/// * Call construction overhead: 1 (BL)
/// * Frame construction overhead: 0
/// * Requires stack fixups? No
///
/// \p MachineOutlinerRegSave implies that the function should be called with a
/// save and restore of LR to an available register. This allows us to avoid
/// stack fixups. Note that this outlining variant is compatible with the
/// NoLRSave case.
///
/// That is,
///
/// I1     Save LR                    OUTLINED_FUNCTION:
/// I2 --> BL OUTLINED_FUNCTION       I1
/// I3     Restore LR                 I2
///                                   I3
///                                   RET
///
/// * Call construction overhead: 3 (save + BL + restore)
/// * Frame construction overhead: 1 (ret)
/// * Requires stack fixups? No
enum MachineOutlinerClass {
  MachineOutlinerDefault,  /// Emit a save, restore, call, and return.
  MachineOutlinerTailCall, /// Only emit a branch.
  MachineOutlinerNoLRSave, /// Emit a call and return.
  MachineOutlinerThunk,    /// Emit a call and tail-call.
  MachineOutlinerRegSave   /// Same as default, but save to a register.
};

enum MachineOutlinerMBBFlags {
  LRUnavailableSomewhere = 0x2,
  HasCalls = 0x4,
  UnsafeRegsDead = 0x8
};

Register
AArch64InstrInfo::findRegisterToSaveLRTo(outliner::Candidate &C) const {
  MachineFunction *MF = C.getMF();
  const TargetRegisterInfo &TRI = *MF->getSubtarget().getRegisterInfo();
  const AArch64RegisterInfo *ARI =
      static_cast<const AArch64RegisterInfo *>(&TRI);
  // Check if there is an available register across the sequence that we can
  // use.
  for (unsigned Reg : AArch64::GPR64RegClass) {
    if (!ARI->isReservedReg(*MF, Reg) &&
        Reg != AArch64::LR &&  // LR is not reserved, but don't use it.
        Reg != AArch64::X16 && // X16 is not guaranteed to be preserved.
        Reg != AArch64::X17 && // Ditto for X17.
        C.isAvailableAcrossAndOutOfSeq(Reg, TRI) &&
        C.isAvailableInsideSeq(Reg, TRI))
      return Reg;
  }
  return Register();
}

static bool
outliningCandidatesSigningScopeConsensus(const outliner::Candidate &a,
                                         const outliner::Candidate &b) {
  const auto &MFIa = a.getMF()->getInfo<AArch64FunctionInfo>();
  const auto &MFIb = b.getMF()->getInfo<AArch64FunctionInfo>();

  return MFIa->shouldSignReturnAddress(false) == MFIb->shouldSignReturnAddress(false) &&
         MFIa->shouldSignReturnAddress(true) == MFIb->shouldSignReturnAddress(true);
}

static bool
outliningCandidatesSigningKeyConsensus(const outliner::Candidate &a,
                                       const outliner::Candidate &b) {
  const auto &MFIa = a.getMF()->getInfo<AArch64FunctionInfo>();
  const auto &MFIb = b.getMF()->getInfo<AArch64FunctionInfo>();

  return MFIa->shouldSignWithBKey() == MFIb->shouldSignWithBKey();
}

static bool outliningCandidatesV8_3OpsConsensus(const outliner::Candidate &a,
                                                const outliner::Candidate &b) {
  const AArch64Subtarget &SubtargetA =
      a.getMF()->getSubtarget<AArch64Subtarget>();
  const AArch64Subtarget &SubtargetB =
      b.getMF()->getSubtarget<AArch64Subtarget>();
  return SubtargetA.hasV8_3aOps() == SubtargetB.hasV8_3aOps();
}

std::optional<outliner::OutlinedFunction>
AArch64InstrInfo::getOutliningCandidateInfo(
    std::vector<outliner::Candidate> &RepeatedSequenceLocs) const {
  unsigned SequenceSize = 0;
  for (auto &MI : RepeatedSequenceLocs[0])
    SequenceSize += getInstSizeInBytes(MI);

  unsigned NumBytesToCreateFrame = 0;

  // We only allow outlining for functions having exactly matching return
  // address signing attributes, i.e., all share the same value for the
  // attribute "sign-return-address" and all share the same type of key they
  // are signed with.
  // Additionally we require all functions to simultaniously either support
  // v8.3a features or not. Otherwise an outlined function could get signed
  // using dedicated v8.3 instructions and a call from a function that doesn't
  // support v8.3 instructions would therefore be invalid.
  if (std::adjacent_find(
          RepeatedSequenceLocs.begin(), RepeatedSequenceLocs.end(),
          [](const outliner::Candidate &a, const outliner::Candidate &b) {
            // Return true if a and b are non-equal w.r.t. return address
            // signing or support of v8.3a features
            if (outliningCandidatesSigningScopeConsensus(a, b) &&
                outliningCandidatesSigningKeyConsensus(a, b) &&
                outliningCandidatesV8_3OpsConsensus(a, b)) {
              return false;
            }
            return true;
          }) != RepeatedSequenceLocs.end()) {
    return std::nullopt;
  }

  // Since at this point all candidates agree on their return address signing
  // picking just one is fine. If the candidate functions potentially sign their
  // return addresses, the outlined function should do the same. Note that in
  // the case of "sign-return-address"="non-leaf" this is an assumption: It is
  // not certainly true that the outlined function will have to sign its return
  // address but this decision is made later, when the decision to outline
  // has already been made.
  // The same holds for the number of additional instructions we need: On
  // v8.3a RET can be replaced by RETAA/RETAB and no AUT instruction is
  // necessary. However, at this point we don't know if the outlined function
  // will have a RET instruction so we assume the worst.
  const TargetRegisterInfo &TRI = getRegisterInfo();
  // Performing a tail call may require extra checks when PAuth is enabled.
  // If PAuth is disabled, set it to zero for uniformity.
  unsigned NumBytesToCheckLRInTCEpilogue = 0;
  if (RepeatedSequenceLocs[0]
          .getMF()
          ->getInfo<AArch64FunctionInfo>()
          ->shouldSignReturnAddress(true)) {
    // One PAC and one AUT instructions
    NumBytesToCreateFrame += 8;

    // PAuth is enabled - set extra tail call cost, if any.
    auto LRCheckMethod = Subtarget.getAuthenticatedLRCheckMethod(
        *RepeatedSequenceLocs[0].getMF());
    NumBytesToCheckLRInTCEpilogue =
        AArch64PAuth::getCheckerSizeInBytes(LRCheckMethod);
    // Checking the authenticated LR value may significantly impact
    // SequenceSize, so account for it for more precise results.
    if (isTailCallReturnInst(RepeatedSequenceLocs[0].back()))
      SequenceSize += NumBytesToCheckLRInTCEpilogue;

    // We have to check if sp modifying instructions would get outlined.
    // If so we only allow outlining if sp is unchanged overall, so matching
    // sub and add instructions are okay to outline, all other sp modifications
    // are not
    auto hasIllegalSPModification = [&TRI](outliner::Candidate &C) {
      int SPValue = 0;
      for (auto &MI : C) {
        if (MI.modifiesRegister(AArch64::SP, &TRI)) {
          switch (MI.getOpcode()) {
          case AArch64::ADDXri:
          case AArch64::ADDWri:
            assert(MI.getNumOperands() == 4 && "Wrong number of operands");
            assert(MI.getOperand(2).isImm() &&
                   "Expected operand to be immediate");
            assert(MI.getOperand(1).isReg() &&
                   "Expected operand to be a register");
            // Check if the add just increments sp. If so, we search for
            // matching sub instructions that decrement sp. If not, the
            // modification is illegal
            if (MI.getOperand(1).getReg() == AArch64::SP)
              SPValue += MI.getOperand(2).getImm();
            else
              return true;
            break;
          case AArch64::SUBXri:
          case AArch64::SUBWri:
            assert(MI.getNumOperands() == 4 && "Wrong number of operands");
            assert(MI.getOperand(2).isImm() &&
                   "Expected operand to be immediate");
            assert(MI.getOperand(1).isReg() &&
                   "Expected operand to be a register");
            // Check if the sub just decrements sp. If so, we search for
            // matching add instructions that increment sp. If not, the
            // modification is illegal
            if (MI.getOperand(1).getReg() == AArch64::SP)
              SPValue -= MI.getOperand(2).getImm();
            else
              return true;
            break;
          default:
            return true;
          }
        }
      }
      if (SPValue)
        return true;
      return false;
    };
    // Remove candidates with illegal stack modifying instructions
    llvm::erase_if(RepeatedSequenceLocs, hasIllegalSPModification);

    // If the sequence doesn't have enough candidates left, then we're done.
    if (RepeatedSequenceLocs.size() < 2)
      return std::nullopt;
  }

  // Properties about candidate MBBs that hold for all of them.
  unsigned FlagsSetInAll = 0xF;

  // Compute liveness information for each candidate, and set FlagsSetInAll.
  for (outliner::Candidate &C : RepeatedSequenceLocs)
    FlagsSetInAll &= C.Flags;

  unsigned LastInstrOpcode = RepeatedSequenceLocs[0].back().getOpcode();

  // Helper lambda which sets call information for every candidate.
  auto SetCandidateCallInfo =
      [&RepeatedSequenceLocs](unsigned CallID, unsigned NumBytesForCall) {
        for (outliner::Candidate &C : RepeatedSequenceLocs)
          C.setCallInfo(CallID, NumBytesForCall);
      };

  unsigned FrameID = MachineOutlinerDefault;
  NumBytesToCreateFrame += 4;

  bool HasBTI = any_of(RepeatedSequenceLocs, [](outliner::Candidate &C) {
    return C.getMF()->getInfo<AArch64FunctionInfo>()->branchTargetEnforcement();
  });

  // We check to see if CFI Instructions are present, and if they are
  // we find the number of CFI Instructions in the candidates.
  unsigned CFICount = 0;
  for (auto &I : RepeatedSequenceLocs[0]) {
    if (I.isCFIInstruction())
      CFICount++;
  }

  // We compare the number of found CFI Instructions to  the number of CFI
  // instructions in the parent function for each candidate.  We must check this
  // since if we outline one of the CFI instructions in a function, we have to
  // outline them all for correctness. If we do not, the address offsets will be
  // incorrect between the two sections of the program.
  for (outliner::Candidate &C : RepeatedSequenceLocs) {
    std::vector<MCCFIInstruction> CFIInstructions =
        C.getMF()->getFrameInstructions();

    if (CFICount > 0 && CFICount != CFIInstructions.size())
      return std::nullopt;
  }

  // Returns true if an instructions is safe to fix up, false otherwise.
  auto IsSafeToFixup = [this, &TRI](MachineInstr &MI) {
    if (MI.isCall())
      return true;

    if (!MI.modifiesRegister(AArch64::SP, &TRI) &&
        !MI.readsRegister(AArch64::SP, &TRI))
      return true;

    // Any modification of SP will break our code to save/restore LR.
    // FIXME: We could handle some instructions which add a constant
    // offset to SP, with a bit more work.
    if (MI.modifiesRegister(AArch64::SP, &TRI))
      return false;

    // At this point, we have a stack instruction that we might need to
    // fix up. We'll handle it if it's a load or store.
    if (MI.mayLoadOrStore()) {
      const MachineOperand *Base; // Filled with the base operand of MI.
      int64_t Offset;             // Filled with the offset of MI.
      bool OffsetIsScalable;

      // Does it allow us to offset the base operand and is the base the
      // register SP?
      if (!getMemOperandWithOffset(MI, Base, Offset, OffsetIsScalable, &TRI) ||
          !Base->isReg() || Base->getReg() != AArch64::SP)
        return false;

      // Fixe-up code below assumes bytes.
      if (OffsetIsScalable)
        return false;

      // Find the minimum/maximum offset for this instruction and check
      // if fixing it up would be in range.
      int64_t MinOffset,
          MaxOffset;  // Unscaled offsets for the instruction.
      // The scale to multiply the offsets by.
      TypeSize Scale(0U, false), DummyWidth(0U, false);
      getMemOpInfo(MI.getOpcode(), Scale, DummyWidth, MinOffset, MaxOffset);

      Offset += 16; // Update the offset to what it would be if we outlined.
      if (Offset < MinOffset * (int64_t)Scale.getFixedValue() ||
          Offset > MaxOffset * (int64_t)Scale.getFixedValue())
        return false;

      // It's in range, so we can outline it.
      return true;
    }

    // FIXME: Add handling for instructions like "add x0, sp, #8".

    // We can't fix it up, so don't outline it.
    return false;
  };

  // True if it's possible to fix up each stack instruction in this sequence.
  // Important for frames/call variants that modify the stack.
  bool AllStackInstrsSafe =
      llvm::all_of(RepeatedSequenceLocs[0], IsSafeToFixup);

  // If the last instruction in any candidate is a terminator, then we should
  // tail call all of the candidates.
  if (RepeatedSequenceLocs[0].back().isTerminator()) {
    FrameID = MachineOutlinerTailCall;
    NumBytesToCreateFrame = 0;
    unsigned NumBytesForCall = 4 + NumBytesToCheckLRInTCEpilogue;
    SetCandidateCallInfo(MachineOutlinerTailCall, NumBytesForCall);
  }

  else if (LastInstrOpcode == AArch64::BL ||
           ((LastInstrOpcode == AArch64::BLR ||
             LastInstrOpcode == AArch64::BLRNoIP) &&
            !HasBTI)) {
    // FIXME: Do we need to check if the code after this uses the value of LR?
    FrameID = MachineOutlinerThunk;
    NumBytesToCreateFrame = NumBytesToCheckLRInTCEpilogue;
    SetCandidateCallInfo(MachineOutlinerThunk, 4);
  }

  else {
    // We need to decide how to emit calls + frames. We can always emit the same
    // frame if we don't need to save to the stack. If we have to save to the
    // stack, then we need a different frame.
    unsigned NumBytesNoStackCalls = 0;
    std::vector<outliner::Candidate> CandidatesWithoutStackFixups;

    // Check if we have to save LR.
    for (outliner::Candidate &C : RepeatedSequenceLocs) {
      bool LRAvailable =
          (C.Flags & MachineOutlinerMBBFlags::LRUnavailableSomewhere)
              ? C.isAvailableAcrossAndOutOfSeq(AArch64::LR, TRI)
              : true;
      // If we have a noreturn caller, then we're going to be conservative and
      // say that we have to save LR. If we don't have a ret at the end of the
      // block, then we can't reason about liveness accurately.
      //
      // FIXME: We can probably do better than always disabling this in
      // noreturn functions by fixing up the liveness info.
      bool IsNoReturn =
          C.getMF()->getFunction().hasFnAttribute(Attribute::NoReturn);

      // Is LR available? If so, we don't need a save.
      if (LRAvailable && !IsNoReturn) {
        NumBytesNoStackCalls += 4;
        C.setCallInfo(MachineOutlinerNoLRSave, 4);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // Is an unused register available? If so, we won't modify the stack, so
      // we can outline with the same frame type as those that don't save LR.
      else if (findRegisterToSaveLRTo(C)) {
        NumBytesNoStackCalls += 12;
        C.setCallInfo(MachineOutlinerRegSave, 12);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // Is SP used in the sequence at all? If not, we don't have to modify
      // the stack, so we are guaranteed to get the same frame.
      else if (C.isAvailableInsideSeq(AArch64::SP, TRI)) {
        NumBytesNoStackCalls += 12;
        C.setCallInfo(MachineOutlinerDefault, 12);
        CandidatesWithoutStackFixups.push_back(C);
      }

      // If we outline this, we need to modify the stack. Pretend we don't
      // outline this by saving all of its bytes.
      else {
        NumBytesNoStackCalls += SequenceSize;
      }
    }

    // If there are no places where we have to save LR, then note that we
    // don't have to update the stack. Otherwise, give every candidate the
    // default call type, as long as it's safe to do so.
    if (!AllStackInstrsSafe ||
        NumBytesNoStackCalls <= RepeatedSequenceLocs.size() * 12) {
      RepeatedSequenceLocs = CandidatesWithoutStackFixups;
      FrameID = MachineOutlinerNoLRSave;
      if (RepeatedSequenceLocs.size() < 2)
        return std::nullopt;
    } else {
      SetCandidateCallInfo(MachineOutlinerDefault, 12);

      // Bugzilla ID: 46767
      // TODO: Check if fixing up the stack more than once is safe so we can
      // outline these.
      //
      // An outline resulting in a caller that requires stack fixups at the
      // callsite to a callee that also requires stack fixups can happen when
      // there are no available registers at the candidate callsite for a
      // candidate that itself also has calls.
      //
      // In other words if function_containing_sequence in the following pseudo
      // assembly requires that we save LR at the point of the call, but there
      // are no available registers: in this case we save using SP and as a
      // result the SP offsets requires stack fixups by multiples of 16.
      //
      // function_containing_sequence:
      //   ...
      //   save LR to SP <- Requires stack instr fixups in OUTLINED_FUNCTION_N
      //   call OUTLINED_FUNCTION_N
      //   restore LR from SP
      //   ...
      //
      // OUTLINED_FUNCTION_N:
      //   save LR to SP <- Requires stack instr fixups in OUTLINED_FUNCTION_N
      //   ...
      //   bl foo
      //   restore LR from SP
      //   ret
      //
      // Because the code to handle more than one stack fixup does not
      // currently have the proper checks for legality, these cases will assert
      // in the AArch64 MachineOutliner. This is because the code to do this
      // needs more hardening, testing, better checks that generated code is
      // legal, etc and because it is only verified to handle a single pass of
      // stack fixup.
      //
      // The assert happens in AArch64InstrInfo::buildOutlinedFrame to catch
      // these cases until they are known to be handled. Bugzilla 46767 is
      // referenced in comments at the assert site.
      //
      // To avoid asserting (or generating non-legal code on noassert builds)
      // we remove all candidates which would need more than one stack fixup by
      // pruning the cases where the candidate has calls while also having no
      // available LR and having no available general purpose registers to copy
      // LR to (ie one extra stack save/restore).
      //
      if (FlagsSetInAll & MachineOutlinerMBBFlags::HasCalls) {
        erase_if(RepeatedSequenceLocs, [this, &TRI](outliner::Candidate &C) {
          auto IsCall = [](const MachineInstr &MI) { return MI.isCall(); };
          return (llvm::any_of(C, IsCall)) &&
                 (!C.isAvailableAcrossAndOutOfSeq(AArch64::LR, TRI) ||
                  !findRegisterToSaveLRTo(C));
        });
      }
    }

    // If we dropped all of the candidates, bail out here.
    if (RepeatedSequenceLocs.size() < 2) {
      RepeatedSequenceLocs.clear();
      return std::nullopt;
    }
  }

  // Does every candidate's MBB contain a call? If so, then we might have a call
  // in the range.
  if (FlagsSetInAll & MachineOutlinerMBBFlags::HasCalls) {
    // Check if the range contains a call. These require a save + restore of the
    // link register.
    outliner::Candidate &FirstCand = RepeatedSequenceLocs[0];
    bool ModStackToSaveLR = false;
    if (std::any_of(FirstCand.begin(), std::prev(FirstCand.end()),
                    [](const MachineInstr &MI) { return MI.isCall(); }))
      ModStackToSaveLR = true;

    // Handle the last instruction separately. If this is a tail call, then the
    // last instruction is a call. We don't want to save + restore in this case.
    // However, it could be possible that the last instruction is a call without
    // it being valid to tail call this sequence. We should consider this as
    // well.
    else if (FrameID != MachineOutlinerThunk &&
             FrameID != MachineOutlinerTailCall && FirstCand.back().isCall())
      ModStackToSaveLR = true;

    if (ModStackToSaveLR) {
      // We can't fix up the stack. Bail out.
      if (!AllStackInstrsSafe) {
        RepeatedSequenceLocs.clear();
        return std::nullopt;
      }

      // Save + restore LR.
      NumBytesToCreateFrame += 8;
    }
  }

  // If we have CFI instructions, we can only outline if the outlined section
  // can be a tail call
  if (FrameID != MachineOutlinerTailCall && CFICount > 0)
    return std::nullopt;

  return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize,
                                    NumBytesToCreateFrame, FrameID);
}

void AArch64InstrInfo::mergeOutliningCandidateAttributes(
    Function &F, std::vector<outliner::Candidate> &Candidates) const {
  // If a bunch of candidates reach this point they must agree on their return
  // address signing. It is therefore enough to just consider the signing
  // behaviour of one of them
  const auto &CFn = Candidates.front().getMF()->getFunction();

  if (CFn.hasFnAttribute("ptrauth-returns"))
    F.addFnAttr(CFn.getFnAttribute("ptrauth-returns"));
  if (CFn.hasFnAttribute("ptrauth-auth-traps"))
    F.addFnAttr(CFn.getFnAttribute("ptrauth-auth-traps"));
  // Since all candidates belong to the same module, just copy the
  // function-level attributes of an arbitrary function.
  if (CFn.hasFnAttribute("sign-return-address"))
    F.addFnAttr(CFn.getFnAttribute("sign-return-address"));
  if (CFn.hasFnAttribute("sign-return-address-key"))
    F.addFnAttr(CFn.getFnAttribute("sign-return-address-key"));

  AArch64GenInstrInfo::mergeOutliningCandidateAttributes(F, Candidates);
}

bool AArch64InstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();

  // Can F be deduplicated by the linker? If it can, don't outline from it.
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
    return false;

  // Don't outline from functions with section markings; the program could
  // expect that all the code is in the named section.
  // FIXME: Allow outlining from multiple functions with the same section
  // marking.
  if (F.hasSection())
    return false;

  // Outlining from functions with redzones is unsafe since the outliner may
  // modify the stack. Check if hasRedZone is true or unknown; if yes, don't
  // outline from it.
  AArch64FunctionInfo *AFI = MF.getInfo<AArch64FunctionInfo>();
  if (!AFI || AFI->hasRedZone().value_or(true))
    return false;

  // FIXME: Determine whether it is safe to outline from functions which contain
  // streaming-mode changes. We may need to ensure any smstart/smstop pairs are
  // outlined together and ensure it is safe to outline with async unwind info,
  // required for saving & restoring VG around calls.
  if (AFI->hasStreamingModeChanges())
    return false;

  // FIXME: Teach the outliner to generate/handle Windows unwind info.
  if (MF.getTarget().getMCAsmInfo()->usesWindowsCFI())
    return false;

  // It's safe to outline from MF.
  return true;
}

SmallVector<std::pair<MachineBasicBlock::iterator, MachineBasicBlock::iterator>>
AArch64InstrInfo::getOutlinableRanges(MachineBasicBlock &MBB,
                                      unsigned &Flags) const {
  assert(MBB.getParent()->getRegInfo().tracksLiveness() &&
         "Must track liveness!");
  SmallVector<
      std::pair<MachineBasicBlock::iterator, MachineBasicBlock::iterator>>
      Ranges;
  // According to the AArch64 Procedure Call Standard, the following are
  // undefined on entry/exit from a function call:
  //
  // * Registers x16, x17, (and thus w16, w17)
  // * Condition codes (and thus the NZCV register)
  //
  // If any of these registers are used inside or live across an outlined
  // function, then they may be modified later, either by the compiler or
  // some other tool (like the linker).
  //
  // To avoid outlining in these situations, partition each block into ranges
  // where these registers are dead. We will only outline from those ranges.
  LiveRegUnits LRU(getRegisterInfo());
  auto AreAllUnsafeRegsDead = [&LRU]() {
    return LRU.available(AArch64::W16) && LRU.available(AArch64::W17) &&
           LRU.available(AArch64::NZCV);
  };

  // We need to know if LR is live across an outlining boundary later on in
  // order to decide how we'll create the outlined call, frame, etc.
  //
  // It's pretty expensive to check this for *every candidate* within a block.
  // That's some potentially n^2 behaviour, since in the worst case, we'd need
  // to compute liveness from the end of the block for O(n) candidates within
  // the block.
  //
  // So, to improve the average case, let's keep track of liveness from the end
  // of the block to the beginning of *every outlinable range*. If we know that
  // LR is available in every range we could outline from, then we know that
  // we don't need to check liveness for any candidate within that range.
  bool LRAvailableEverywhere = true;
  // Compute liveness bottom-up.
  LRU.addLiveOuts(MBB);
  // Update flags that require info about the entire MBB.
  auto UpdateWholeMBBFlags = [&Flags](const MachineInstr &MI) {
    if (MI.isCall() && !MI.isTerminator())
      Flags |= MachineOutlinerMBBFlags::HasCalls;
  };
  // Range: [RangeBegin, RangeEnd)
  MachineBasicBlock::instr_iterator RangeBegin, RangeEnd;
  unsigned RangeLen;
  auto CreateNewRangeStartingAt =
      [&RangeBegin, &RangeEnd,
       &RangeLen](MachineBasicBlock::instr_iterator NewBegin) {
        RangeBegin = NewBegin;
        RangeEnd = std::next(RangeBegin);
        RangeLen = 0;
      };
  auto SaveRangeIfNonEmpty = [&RangeLen, &Ranges, &RangeBegin, &RangeEnd]() {
    // At least one unsafe register is not dead. We do not want to outline at
    // this point. If it is long enough to outline from, save the range
    // [RangeBegin, RangeEnd).
    if (RangeLen > 1)
      Ranges.push_back(std::make_pair(RangeBegin, RangeEnd));
  };
  // Find the first point where all unsafe registers are dead.
  // FIND: <safe instr> <-- end of first potential range
  // SKIP: <unsafe def>
  // SKIP: ... everything between ...
  // SKIP: <unsafe use>
  auto FirstPossibleEndPt = MBB.instr_rbegin();
  for (; FirstPossibleEndPt != MBB.instr_rend(); ++FirstPossibleEndPt) {
    LRU.stepBackward(*FirstPossibleEndPt);
    // Update flags that impact how we outline across the entire block,
    // regardless of safety.
    UpdateWholeMBBFlags(*FirstPossibleEndPt);
    if (AreAllUnsafeRegsDead())
      break;
  }
  // If we exhausted the entire block, we have no safe ranges to outline.
  if (FirstPossibleEndPt == MBB.instr_rend())
    return Ranges;
  // Current range.
  CreateNewRangeStartingAt(FirstPossibleEndPt->getIterator());
  // StartPt points to the first place where all unsafe registers
  // are dead (if there is any such point). Begin partitioning the MBB into
  // ranges.
  for (auto &MI : make_range(FirstPossibleEndPt, MBB.instr_rend())) {
    LRU.stepBackward(MI);
    UpdateWholeMBBFlags(MI);
    if (!AreAllUnsafeRegsDead()) {
      SaveRangeIfNonEmpty();
      CreateNewRangeStartingAt(MI.getIterator());
      continue;
    }
    LRAvailableEverywhere &= LRU.available(AArch64::LR);
    RangeBegin = MI.getIterator();
    ++RangeLen;
  }
  // Above loop misses the last (or only) range. If we are still safe, then
  // let's save the range.
  if (AreAllUnsafeRegsDead())
    SaveRangeIfNonEmpty();
  if (Ranges.empty())
    return Ranges;
  // We found the ranges bottom-up. Mapping expects the top-down. Reverse
  // the order.
  std::reverse(Ranges.begin(), Ranges.end());
  // If there is at least one outlinable range where LR is unavailable
  // somewhere, remember that.
  if (!LRAvailableEverywhere)
    Flags |= MachineOutlinerMBBFlags::LRUnavailableSomewhere;
  return Ranges;
}

outliner::InstrType
AArch64InstrInfo::getOutliningTypeImpl(MachineBasicBlock::iterator &MIT,
                                   unsigned Flags) const {
  MachineInstr &MI = *MIT;
  MachineBasicBlock *MBB = MI.getParent();
  MachineFunction *MF = MBB->getParent();
  AArch64FunctionInfo *FuncInfo = MF->getInfo<AArch64FunctionInfo>();

  // Don't outline anything used for return address signing. The outlined
  // function will get signed later if needed
  switch (MI.getOpcode()) {
  case AArch64::PACM:
  case AArch64::PACIASP:
  case AArch64::PACIBSP:
  case AArch64::PACIASPPC:
  case AArch64::PACIBSPPC:
  case AArch64::AUTIASP:
  case AArch64::AUTIBSP:
  case AArch64::AUTIASPPCi:
  case AArch64::AUTIASPPCr:
  case AArch64::AUTIBSPPCi:
  case AArch64::AUTIBSPPCr:
  case AArch64::RETAA:
  case AArch64::RETAB:
  case AArch64::RETAASPPCi:
  case AArch64::RETAASPPCr:
  case AArch64::RETABSPPCi:
  case AArch64::RETABSPPCr:
  case AArch64::EMITBKEY:
  case AArch64::PAUTH_PROLOGUE:
  case AArch64::PAUTH_EPILOGUE:
    return outliner::InstrType::Illegal;
  }

  // Don't outline LOHs.
  if (FuncInfo->getLOHRelated().count(&MI))
    return outliner::InstrType::Illegal;

  // We can only outline these if we will tail call the outlined function, or
  // fix up the CFI offsets. Currently, CFI instructions are outlined only if
  // in a tail call.
  //
  // FIXME: If the proper fixups for the offset are implemented, this should be
  // possible.
  if (MI.isCFIInstruction())
    return outliner::InstrType::Legal;

  // Is this a terminator for a basic block?
  if (MI.isTerminator())
    // TargetInstrInfo::getOutliningType has already filtered out anything
    // that would break this, so we can allow it here.
    return outliner::InstrType::Legal;

  // Make sure none of the operands are un-outlinable.
  for (const MachineOperand &MOP : MI.operands()) {
    // A check preventing CFI indices was here before, but only CFI
    // instructions should have those.
    assert(!MOP.isCFIIndex());

    // If it uses LR or W30 explicitly, then don't touch it.
    if (MOP.isReg() && !MOP.isImplicit() &&
        (MOP.getReg() == AArch64::LR || MOP.getReg() == AArch64::W30))
      return outliner::InstrType::Illegal;
  }

  // Special cases for instructions that can always be outlined, but will fail
  // the later tests. e.g, ADRPs, which are PC-relative use LR, but can always
  // be outlined because they don't require a *specific* value to be in LR.
  if (MI.getOpcode() == AArch64::ADRP)
    return outliner::InstrType::Legal;

  // If MI is a call we might be able to outline it. We don't want to outline
  // any calls that rely on the position of items on the stack. When we outline
  // something containing a call, we have to emit a save and restore of LR in
  // the outlined function. Currently, this always happens by saving LR to the
  // stack. Thus, if we outline, say, half the parameters for a function call
  // plus the call, then we'll break the callee's expectations for the layout
  // of the stack.
  //
  // FIXME: Allow calls to functions which construct a stack frame, as long
  // as they don't access arguments on the stack.
  // FIXME: Figure out some way to analyze functions defined in other modules.
  // We should be able to compute the memory usage based on the IR calling
  // convention, even if we can't see the definition.
  if (MI.isCall()) {
    // Get the function associated with the call. Look at each operand and find
    // the one that represents the callee and get its name.
    const Function *Callee = nullptr;
    for (const MachineOperand &MOP : MI.operands()) {
      if (MOP.isGlobal()) {
        Callee = dyn_cast<Function>(MOP.getGlobal());
        break;
      }
    }

    // Never outline calls to mcount.  There isn't any rule that would require
    // this, but the Linux kernel's "ftrace" feature depends on it.
    if (Callee && Callee->getName() == "\01_mcount")
      return outliner::InstrType::Illegal;

    // If we don't know anything about the callee, assume it depends on the
    // stack layout of the caller. In that case, it's only legal to outline
    // as a tail-call. Explicitly list the call instructions we know about so we
    // don't get unexpected results with call pseudo-instructions.
    auto UnknownCallOutlineType = outliner::InstrType::Illegal;
    if (MI.getOpcode() == AArch64::BLR ||
        MI.getOpcode() == AArch64::BLRNoIP || MI.getOpcode() == AArch64::BL)
      UnknownCallOutlineType = outliner::InstrType::LegalTerminator;

    if (!Callee)
      return UnknownCallOutlineType;

    // We have a function we have information about. Check it if it's something
    // can safely outline.
    MachineFunction *CalleeMF = MF->getMMI().getMachineFunction(*Callee);

    // We don't know what's going on with the callee at all. Don't touch it.
    if (!CalleeMF)
      return UnknownCallOutlineType;

    // Check if we know anything about the callee saves on the function. If we
    // don't, then don't touch it, since that implies that we haven't
    // computed anything about its stack frame yet.
    MachineFrameInfo &MFI = CalleeMF->getFrameInfo();
    if (!MFI.isCalleeSavedInfoValid() || MFI.getStackSize() > 0 ||
        MFI.getNumObjects() > 0)
      return UnknownCallOutlineType;

    // At this point, we can say that CalleeMF ought to not pass anything on the
    // stack. Therefore, we can outline it.
    return outliner::InstrType::Legal;
  }

  // Don't touch the link register or W30.
  if (MI.readsRegister(AArch64::W30, &getRegisterInfo()) ||
      MI.modifiesRegister(AArch64::W30, &getRegisterInfo()))
    return outliner::InstrType::Illegal;

  // Don't outline BTI instructions, because that will prevent the outlining
  // site from being indirectly callable.
  if (hasBTISemantics(MI))
    return outliner::InstrType::Illegal;

  return outliner::InstrType::Legal;
}

void AArch64InstrInfo::fixupPostOutline(MachineBasicBlock &MBB) const {
  for (MachineInstr &MI : MBB) {
    const MachineOperand *Base;
    TypeSize Width(0, false);
    int64_t Offset;
    bool OffsetIsScalable;

    // Is this a load or store with an immediate offset with SP as the base?
    if (!MI.mayLoadOrStore() ||
        !getMemOperandWithOffsetWidth(MI, Base, Offset, OffsetIsScalable, Width,
                                      &RI) ||
        (Base->isReg() && Base->getReg() != AArch64::SP))
      continue;

    // It is, so we have to fix it up.
    TypeSize Scale(0U, false);
    int64_t Dummy1, Dummy2;

    MachineOperand &StackOffsetOperand = getMemOpBaseRegImmOfsOffsetOperand(MI);
    assert(StackOffsetOperand.isImm() && "Stack offset wasn't immediate!");
    getMemOpInfo(MI.getOpcode(), Scale, Width, Dummy1, Dummy2);
    assert(Scale != 0 && "Unexpected opcode!");
    assert(!OffsetIsScalable && "Expected offset to be a byte offset");

    // We've pushed the return address to the stack, so add 16 to the offset.
    // This is safe, since we already checked if it would overflow when we
    // checked if this instruction was legal to outline.
    int64_t NewImm = (Offset + 16) / (int64_t)Scale.getFixedValue();
    StackOffsetOperand.setImm(NewImm);
  }
}

static void signOutlinedFunction(MachineFunction &MF, MachineBasicBlock &MBB,
                                 const AArch64InstrInfo *TII,
                                 bool ShouldSignReturnAddr) {
  if (!ShouldSignReturnAddr)
    return;

  BuildMI(MBB, MBB.begin(), DebugLoc(), TII->get(AArch64::PAUTH_PROLOGUE))
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MBB.getFirstInstrTerminator(), DebugLoc(),
          TII->get(AArch64::PAUTH_EPILOGUE))
      .setMIFlag(MachineInstr::FrameDestroy);
}

void AArch64InstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {

  AArch64FunctionInfo *FI = MF.getInfo<AArch64FunctionInfo>();

  if (OF.FrameConstructionID == MachineOutlinerTailCall)
    FI->setOutliningStyle("Tail Call");
  else if (OF.FrameConstructionID == MachineOutlinerThunk) {
    // For thunk outlining, rewrite the last instruction from a call to a
    // tail-call.
    MachineInstr *Call = &*--MBB.instr_end();
    unsigned TailOpcode;
    if (Call->getOpcode() == AArch64::BL) {
      TailOpcode = AArch64::TCRETURNdi;
    } else {
      assert(Call->getOpcode() == AArch64::BLR ||
             Call->getOpcode() == AArch64::BLRNoIP);
      TailOpcode = AArch64::TCRETURNriALL;
    }
    MachineInstr *TC = BuildMI(MF, DebugLoc(), get(TailOpcode))
                           .add(Call->getOperand(0))
                           .addImm(0);
    MBB.insert(MBB.end(), TC);
    Call->eraseFromParent();

    FI->setOutliningStyle("Thunk");
  }

  bool IsLeafFunction = true;

  // Is there a call in the outlined range?
  auto IsNonTailCall = [](const MachineInstr &MI) {
    return MI.isCall() && !MI.isReturn();
  };

  if (llvm::any_of(MBB.instrs(), IsNonTailCall)) {
    // Fix up the instructions in the range, since we're going to modify the
    // stack.

    // Bugzilla ID: 46767
    // TODO: Check if fixing up twice is safe so we can outline these.
    assert(OF.FrameConstructionID != MachineOutlinerDefault &&
           "Can only fix up stack references once");
    fixupPostOutline(MBB);

    IsLeafFunction = false;

    // LR has to be a live in so that we can save it.
    if (!MBB.isLiveIn(AArch64::LR))
      MBB.addLiveIn(AArch64::LR);

    MachineBasicBlock::iterator It = MBB.begin();
    MachineBasicBlock::iterator Et = MBB.end();

    if (OF.FrameConstructionID == MachineOutlinerTailCall ||
        OF.FrameConstructionID == MachineOutlinerThunk)
      Et = std::prev(MBB.end());

    // Insert a save before the outlined region
    MachineInstr *STRXpre = BuildMI(MF, DebugLoc(), get(AArch64::STRXpre))
                                .addReg(AArch64::SP, RegState::Define)
                                .addReg(AArch64::LR)
                                .addReg(AArch64::SP)
                                .addImm(-16);
    It = MBB.insert(It, STRXpre);

    if (MF.getInfo<AArch64FunctionInfo>()->needsDwarfUnwindInfo(MF)) {
      const TargetSubtargetInfo &STI = MF.getSubtarget();
      const MCRegisterInfo *MRI = STI.getRegisterInfo();
      unsigned DwarfReg = MRI->getDwarfRegNum(AArch64::LR, true);

      // Add a CFI saying the stack was moved 16 B down.
      int64_t StackPosEntry =
          MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, 16));
      BuildMI(MBB, It, DebugLoc(), get(AArch64::CFI_INSTRUCTION))
          .addCFIIndex(StackPosEntry)
          .setMIFlags(MachineInstr::FrameSetup);

      // Add a CFI saying that the LR that we want to find is now 16 B higher
      // than before.
      int64_t LRPosEntry = MF.addFrameInst(
          MCCFIInstruction::createOffset(nullptr, DwarfReg, -16));
      BuildMI(MBB, It, DebugLoc(), get(AArch64::CFI_INSTRUCTION))
          .addCFIIndex(LRPosEntry)
          .setMIFlags(MachineInstr::FrameSetup);
    }

    // Insert a restore before the terminator for the function.
    MachineInstr *LDRXpost = BuildMI(MF, DebugLoc(), get(AArch64::LDRXpost))
                                 .addReg(AArch64::SP, RegState::Define)
                                 .addReg(AArch64::LR, RegState::Define)
                                 .addReg(AArch64::SP)
                                 .addImm(16);
    Et = MBB.insert(Et, LDRXpost);
  }

  bool ShouldSignReturnAddr = FI->shouldSignReturnAddress(!IsLeafFunction);

  // If this is a tail call outlined function, then there's already a return.
  if (OF.FrameConstructionID == MachineOutlinerTailCall ||
      OF.FrameConstructionID == MachineOutlinerThunk) {
    signOutlinedFunction(MF, MBB, this, ShouldSignReturnAddr);
    return;
  }

  // It's not a tail call, so we have to insert the return ourselves.

  // LR has to be a live in so that we can return to it.
  if (!MBB.isLiveIn(AArch64::LR))
    MBB.addLiveIn(AArch64::LR);

  MachineInstr *ret = BuildMI(MF, DebugLoc(), get(AArch64::RET))
                          .addReg(AArch64::LR);
  MBB.insert(MBB.end(), ret);

  signOutlinedFunction(MF, MBB, this, ShouldSignReturnAddr);

  FI->setOutliningStyle("Function");

  // Did we have to modify the stack by saving the link register?
  if (OF.FrameConstructionID != MachineOutlinerDefault)
    return;

  // We modified the stack.
  // Walk over the basic block and fix up all the stack accesses.
  fixupPostOutline(MBB);
}

MachineBasicBlock::iterator AArch64InstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {

  // Are we tail calling?
  if (C.CallConstructionID == MachineOutlinerTailCall) {
    // If yes, then we can just branch to the label.
    It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(AArch64::TCRETURNdi))
                            .addGlobalAddress(M.getNamedValue(MF.getName()))
                            .addImm(0));
    return It;
  }

  // Are we saving the link register?
  if (C.CallConstructionID == MachineOutlinerNoLRSave ||
      C.CallConstructionID == MachineOutlinerThunk) {
    // No, so just insert the call.
    It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(AArch64::BL))
                            .addGlobalAddress(M.getNamedValue(MF.getName())));
    return It;
  }

  // We want to return the spot where we inserted the call.
  MachineBasicBlock::iterator CallPt;

  // Instructions for saving and restoring LR around the call instruction we're
  // going to insert.
  MachineInstr *Save;
  MachineInstr *Restore;
  // Can we save to a register?
  if (C.CallConstructionID == MachineOutlinerRegSave) {
    // FIXME: This logic should be sunk into a target-specific interface so that
    // we don't have to recompute the register.
    Register Reg = findRegisterToSaveLRTo(C);
    assert(Reg && "No callee-saved register available?");

    // LR has to be a live in so that we can save it.
    if (!MBB.isLiveIn(AArch64::LR))
      MBB.addLiveIn(AArch64::LR);

    // Save and restore LR from Reg.
    Save = BuildMI(MF, DebugLoc(), get(AArch64::ORRXrs), Reg)
               .addReg(AArch64::XZR)
               .addReg(AArch64::LR)
               .addImm(0);
    Restore = BuildMI(MF, DebugLoc(), get(AArch64::ORRXrs), AArch64::LR)
                .addReg(AArch64::XZR)
                .addReg(Reg)
                .addImm(0);
  } else {
    // We have the default case. Save and restore from SP.
    Save = BuildMI(MF, DebugLoc(), get(AArch64::STRXpre))
               .addReg(AArch64::SP, RegState::Define)
               .addReg(AArch64::LR)
               .addReg(AArch64::SP)
               .addImm(-16);
    Restore = BuildMI(MF, DebugLoc(), get(AArch64::LDRXpost))
                  .addReg(AArch64::SP, RegState::Define)
                  .addReg(AArch64::LR, RegState::Define)
                  .addReg(AArch64::SP)
                  .addImm(16);
  }

  It = MBB.insert(It, Save);
  It++;

  // Insert the call.
  It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(AArch64::BL))
                          .addGlobalAddress(M.getNamedValue(MF.getName())));
  CallPt = It;
  It++;

  It = MBB.insert(It, Restore);
  return CallPt;
}

bool AArch64InstrInfo::shouldOutlineFromFunctionByDefault(
  MachineFunction &MF) const {
  return MF.getFunction().hasMinSize();
}

void AArch64InstrInfo::buildClearRegister(Register Reg, MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator Iter,
                                          DebugLoc &DL,
                                          bool AllowSideEffects) const {
  const MachineFunction &MF = *MBB.getParent();
  const AArch64Subtarget &STI = MF.getSubtarget<AArch64Subtarget>();
  const AArch64RegisterInfo &TRI = *STI.getRegisterInfo();

  if (TRI.isGeneralPurposeRegister(MF, Reg)) {
    BuildMI(MBB, Iter, DL, get(AArch64::MOVZXi), Reg).addImm(0).addImm(0);
  } else if (STI.hasSVE()) {
    BuildMI(MBB, Iter, DL, get(AArch64::DUP_ZI_D), Reg)
      .addImm(0)
      .addImm(0);
  } else {
    BuildMI(MBB, Iter, DL, get(AArch64::MOVIv2d_ns), Reg)
      .addImm(0);
  }
}

std::optional<DestSourcePair>
AArch64InstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {

  // AArch64::ORRWrs and AArch64::ORRXrs with WZR/XZR reg
  // and zero immediate operands used as an alias for mov instruction.
  if (MI.getOpcode() == AArch64::ORRWrs &&
      MI.getOperand(1).getReg() == AArch64::WZR &&
      MI.getOperand(3).getImm() == 0x0 &&
      // Check that the w->w move is not a zero-extending w->x mov.
      (!MI.getOperand(0).getReg().isVirtual() ||
       MI.getOperand(0).getSubReg() == 0) &&
      (!MI.getOperand(0).getReg().isPhysical() ||
       MI.findRegisterDefOperandIdx(MI.getOperand(0).getReg() - AArch64::W0 +
                                        AArch64::X0,
                                    /*TRI=*/nullptr) == -1))
    return DestSourcePair{MI.getOperand(0), MI.getOperand(2)};

  if (MI.getOpcode() == AArch64::ORRXrs &&
      MI.getOperand(1).getReg() == AArch64::XZR &&
      MI.getOperand(3).getImm() == 0x0)
    return DestSourcePair{MI.getOperand(0), MI.getOperand(2)};

  return std::nullopt;
}

std::optional<DestSourcePair>
AArch64InstrInfo::isCopyLikeInstrImpl(const MachineInstr &MI) const {
  if (MI.getOpcode() == AArch64::ORRWrs &&
      MI.getOperand(1).getReg() == AArch64::WZR &&
      MI.getOperand(3).getImm() == 0x0)
    return DestSourcePair{MI.getOperand(0), MI.getOperand(2)};
  return std::nullopt;
}

std::optional<RegImmPair>
AArch64InstrInfo::isAddImmediate(const MachineInstr &MI, Register Reg) const {
  int Sign = 1;
  int64_t Offset = 0;

  // TODO: Handle cases where Reg is a super- or sub-register of the
  // destination register.
  const MachineOperand &Op0 = MI.getOperand(0);
  if (!Op0.isReg() || Reg != Op0.getReg())
    return std::nullopt;

  switch (MI.getOpcode()) {
  default:
    return std::nullopt;
  case AArch64::SUBWri:
  case AArch64::SUBXri:
  case AArch64::SUBSWri:
  case AArch64::SUBSXri:
    Sign *= -1;
    [[fallthrough]];
  case AArch64::ADDSWri:
  case AArch64::ADDSXri:
  case AArch64::ADDWri:
  case AArch64::ADDXri: {
    // TODO: Third operand can be global address (usually some string).
    if (!MI.getOperand(0).isReg() || !MI.getOperand(1).isReg() ||
        !MI.getOperand(2).isImm())
      return std::nullopt;
    int Shift = MI.getOperand(3).getImm();
    assert((Shift == 0 || Shift == 12) && "Shift can be either 0 or 12");
    Offset = Sign * (MI.getOperand(2).getImm() << Shift);
  }
  }
  return RegImmPair{MI.getOperand(1).getReg(), Offset};
}

/// If the given ORR instruction is a copy, and \p DescribedReg overlaps with
/// the destination register then, if possible, describe the value in terms of
/// the source register.
static std::optional<ParamLoadedValue>
describeORRLoadedValue(const MachineInstr &MI, Register DescribedReg,
                       const TargetInstrInfo *TII,
                       const TargetRegisterInfo *TRI) {
  auto DestSrc = TII->isCopyLikeInstr(MI);
  if (!DestSrc)
    return std::nullopt;

  Register DestReg = DestSrc->Destination->getReg();
  Register SrcReg = DestSrc->Source->getReg();

  auto Expr = DIExpression::get(MI.getMF()->getFunction().getContext(), {});

  // If the described register is the destination, just return the source.
  if (DestReg == DescribedReg)
    return ParamLoadedValue(MachineOperand::CreateReg(SrcReg, false), Expr);

  // ORRWrs zero-extends to 64-bits, so we need to consider such cases.
  if (MI.getOpcode() == AArch64::ORRWrs &&
      TRI->isSuperRegister(DestReg, DescribedReg))
    return ParamLoadedValue(MachineOperand::CreateReg(SrcReg, false), Expr);

  // We may need to describe the lower part of a ORRXrs move.
  if (MI.getOpcode() == AArch64::ORRXrs &&
      TRI->isSubRegister(DestReg, DescribedReg)) {
    Register SrcSubReg = TRI->getSubReg(SrcReg, AArch64::sub_32);
    return ParamLoadedValue(MachineOperand::CreateReg(SrcSubReg, false), Expr);
  }

  assert(!TRI->isSuperOrSubRegisterEq(DestReg, DescribedReg) &&
         "Unhandled ORR[XW]rs copy case");

  return std::nullopt;
}

bool AArch64InstrInfo::isFunctionSafeToSplit(const MachineFunction &MF) const {
  // Functions cannot be split to different sections on AArch64 if they have
  // a red zone. This is because relaxing a cross-section branch may require
  // incrementing the stack pointer to spill a register, which would overwrite
  // the red zone.
  if (MF.getInfo<AArch64FunctionInfo>()->hasRedZone().value_or(true))
    return false;

  return TargetInstrInfo::isFunctionSafeToSplit(MF);
}

bool AArch64InstrInfo::isMBBSafeToSplitToCold(
    const MachineBasicBlock &MBB) const {
  // Asm Goto blocks can contain conditional branches to goto labels, which can
  // get moved out of range of the branch instruction.
  auto isAsmGoto = [](const MachineInstr &MI) {
    return MI.getOpcode() == AArch64::INLINEASM_BR;
  };
  if (llvm::any_of(MBB, isAsmGoto) || MBB.isInlineAsmBrIndirectTarget())
    return false;

  // Because jump tables are label-relative instead of table-relative, they all
  // must be in the same section or relocation fixup handling will fail.

  // Check if MBB is a jump table target
  const MachineJumpTableInfo *MJTI = MBB.getParent()->getJumpTableInfo();
  auto containsMBB = [&MBB](const MachineJumpTableEntry &JTE) {
    return llvm::is_contained(JTE.MBBs, &MBB);
  };
  if (MJTI != nullptr && llvm::any_of(MJTI->getJumpTables(), containsMBB))
    return false;

  // Check if MBB contains a jump table lookup
  for (const MachineInstr &MI : MBB) {
    switch (MI.getOpcode()) {
    case TargetOpcode::G_BRJT:
    case AArch64::JumpTableDest32:
    case AArch64::JumpTableDest16:
    case AArch64::JumpTableDest8:
      return false;
    default:
      continue;
    }
  }

  // MBB isn't a special case, so it's safe to be split to the cold section.
  return true;
}

std::optional<ParamLoadedValue>
AArch64InstrInfo::describeLoadedValue(const MachineInstr &MI,
                                      Register Reg) const {
  const MachineFunction *MF = MI.getMF();
  const TargetRegisterInfo *TRI = MF->getSubtarget().getRegisterInfo();
  switch (MI.getOpcode()) {
  case AArch64::MOVZWi:
  case AArch64::MOVZXi: {
    // MOVZWi may be used for producing zero-extended 32-bit immediates in
    // 64-bit parameters, so we need to consider super-registers.
    if (!TRI->isSuperRegisterEq(MI.getOperand(0).getReg(), Reg))
      return std::nullopt;

    if (!MI.getOperand(1).isImm())
      return std::nullopt;
    int64_t Immediate = MI.getOperand(1).getImm();
    int Shift = MI.getOperand(2).getImm();
    return ParamLoadedValue(MachineOperand::CreateImm(Immediate << Shift),
                            nullptr);
  }
  case AArch64::ORRWrs:
  case AArch64::ORRXrs:
    return describeORRLoadedValue(MI, Reg, this, TRI);
  }

  return TargetInstrInfo::describeLoadedValue(MI, Reg);
}

bool AArch64InstrInfo::isExtendLikelyToBeFolded(
    MachineInstr &ExtMI, MachineRegisterInfo &MRI) const {
  assert(ExtMI.getOpcode() == TargetOpcode::G_SEXT ||
         ExtMI.getOpcode() == TargetOpcode::G_ZEXT ||
         ExtMI.getOpcode() == TargetOpcode::G_ANYEXT);

  // Anyexts are nops.
  if (ExtMI.getOpcode() == TargetOpcode::G_ANYEXT)
    return true;

  Register DefReg = ExtMI.getOperand(0).getReg();
  if (!MRI.hasOneNonDBGUse(DefReg))
    return false;

  // It's likely that a sext/zext as a G_PTR_ADD offset will be folded into an
  // addressing mode.
  auto *UserMI = &*MRI.use_instr_nodbg_begin(DefReg);
  return UserMI->getOpcode() == TargetOpcode::G_PTR_ADD;
}

uint64_t AArch64InstrInfo::getElementSizeForOpcode(unsigned Opc) const {
  return get(Opc).TSFlags & AArch64::ElementSizeMask;
}

bool AArch64InstrInfo::isPTestLikeOpcode(unsigned Opc) const {
  return get(Opc).TSFlags & AArch64::InstrFlagIsPTestLike;
}

bool AArch64InstrInfo::isWhileOpcode(unsigned Opc) const {
  return get(Opc).TSFlags & AArch64::InstrFlagIsWhile;
}

unsigned int
AArch64InstrInfo::getTailDuplicateSize(CodeGenOptLevel OptLevel) const {
  return OptLevel >= CodeGenOptLevel::Aggressive ? 6 : 2;
}

bool AArch64InstrInfo::isLegalAddressingMode(unsigned NumBytes, int64_t Offset,
                                             unsigned Scale) const {
  if (Offset && Scale)
    return false;

  // Check Reg + Imm
  if (!Scale) {
    // 9-bit signed offset
    if (isInt<9>(Offset))
      return true;

    // 12-bit unsigned offset
    unsigned Shift = Log2_64(NumBytes);
    if (NumBytes && Offset > 0 && (Offset / NumBytes) <= (1LL << 12) - 1 &&
        // Must be a multiple of NumBytes (NumBytes is a power of 2)
        (Offset >> Shift) << Shift == Offset)
      return true;
    return false;
  }

  // Check reg1 + SIZE_IN_BYTES * reg2 and reg1 + reg2
  return Scale == 1 || (Scale > 0 && Scale == NumBytes);
}

unsigned llvm::getBLRCallOpcode(const MachineFunction &MF) {
  if (MF.getSubtarget<AArch64Subtarget>().hardenSlsBlr())
    return AArch64::BLRNoIP;
  else
    return AArch64::BLR;
}

MachineBasicBlock::iterator
AArch64InstrInfo::probedStackAlloc(MachineBasicBlock::iterator MBBI,
                                   Register TargetReg, bool FrameSetup) const {
  assert(TargetReg != AArch64::SP && "New top of stack cannot aleady be in SP");

  MachineBasicBlock &MBB = *MBBI->getParent();
  MachineFunction &MF = *MBB.getParent();
  const AArch64InstrInfo *TII =
      MF.getSubtarget<AArch64Subtarget>().getInstrInfo();
  int64_t ProbeSize = MF.getInfo<AArch64FunctionInfo>()->getStackProbeSize();
  DebugLoc DL = MBB.findDebugLoc(MBBI);

  MachineFunction::iterator MBBInsertPoint = std::next(MBB.getIterator());
  MachineBasicBlock *LoopTestMBB =
      MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.insert(MBBInsertPoint, LoopTestMBB);
  MachineBasicBlock *LoopBodyMBB =
      MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.insert(MBBInsertPoint, LoopBodyMBB);
  MachineBasicBlock *ExitMBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.insert(MBBInsertPoint, ExitMBB);
  MachineInstr::MIFlag Flags =
      FrameSetup ? MachineInstr::FrameSetup : MachineInstr::NoFlags;

  // LoopTest:
  //   SUB SP, SP, #ProbeSize
  emitFrameOffset(*LoopTestMBB, LoopTestMBB->end(), DL, AArch64::SP,
                  AArch64::SP, StackOffset::getFixed(-ProbeSize), TII, Flags);

  //   CMP SP, TargetReg
  BuildMI(*LoopTestMBB, LoopTestMBB->end(), DL, TII->get(AArch64::SUBSXrx64),
          AArch64::XZR)
      .addReg(AArch64::SP)
      .addReg(TargetReg)
      .addImm(AArch64_AM::getArithExtendImm(AArch64_AM::UXTX, 0))
      .setMIFlags(Flags);

  //   B.<Cond> LoopExit
  BuildMI(*LoopTestMBB, LoopTestMBB->end(), DL, TII->get(AArch64::Bcc))
      .addImm(AArch64CC::LE)
      .addMBB(ExitMBB)
      .setMIFlags(Flags);

  //   STR XZR, [SP]
  BuildMI(*LoopBodyMBB, LoopBodyMBB->end(), DL, TII->get(AArch64::STRXui))
      .addReg(AArch64::XZR)
      .addReg(AArch64::SP)
      .addImm(0)
      .setMIFlags(Flags);

  //   B loop
  BuildMI(*LoopBodyMBB, LoopBodyMBB->end(), DL, TII->get(AArch64::B))
      .addMBB(LoopTestMBB)
      .setMIFlags(Flags);

  // LoopExit:
  //   MOV SP, TargetReg
  BuildMI(*ExitMBB, ExitMBB->end(), DL, TII->get(AArch64::ADDXri), AArch64::SP)
      .addReg(TargetReg)
      .addImm(0)
      .addImm(AArch64_AM::getShifterImm(AArch64_AM::LSL, 0))
      .setMIFlags(Flags);

  //   LDR XZR, [SP]
  BuildMI(*ExitMBB, ExitMBB->end(), DL, TII->get(AArch64::LDRXui))
      .addReg(AArch64::XZR, RegState::Define)
      .addReg(AArch64::SP)
      .addImm(0)
      .setMIFlags(Flags);

  ExitMBB->splice(ExitMBB->end(), &MBB, std::next(MBBI), MBB.end());
  ExitMBB->transferSuccessorsAndUpdatePHIs(&MBB);

  LoopTestMBB->addSuccessor(ExitMBB);
  LoopTestMBB->addSuccessor(LoopBodyMBB);
  LoopBodyMBB->addSuccessor(LoopTestMBB);
  MBB.addSuccessor(LoopTestMBB);

  // Update liveins.
  if (MF.getRegInfo().reservedRegsFrozen())
    fullyRecomputeLiveIns({ExitMBB, LoopBodyMBB, LoopTestMBB});

  return ExitMBB->begin();
}

namespace {
class AArch64PipelinerLoopInfo : public TargetInstrInfo::PipelinerLoopInfo {
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  MachineRegisterInfo &MRI;

  /// The block of the loop
  MachineBasicBlock *LoopBB;
  /// The conditional branch of the loop
  MachineInstr *CondBranch;
  /// The compare instruction for loop control
  MachineInstr *Comp;
  /// The number of the operand of the loop counter value in Comp
  unsigned CompCounterOprNum;
  /// The instruction that updates the loop counter value
  MachineInstr *Update;
  /// The number of the operand of the loop counter value in Update
  unsigned UpdateCounterOprNum;
  /// The initial value of the loop counter
  Register Init;
  /// True iff Update is a predecessor of Comp
  bool IsUpdatePriorComp;

  /// The normalized condition used by createTripCountGreaterCondition()
  SmallVector<MachineOperand, 4> Cond;

public:
  AArch64PipelinerLoopInfo(MachineBasicBlock *LoopBB, MachineInstr *CondBranch,
                           MachineInstr *Comp, unsigned CompCounterOprNum,
                           MachineInstr *Update, unsigned UpdateCounterOprNum,
                           Register Init, bool IsUpdatePriorComp,
                           const SmallVectorImpl<MachineOperand> &Cond)
      : MF(Comp->getParent()->getParent()),
        TII(MF->getSubtarget().getInstrInfo()),
        TRI(MF->getSubtarget().getRegisterInfo()), MRI(MF->getRegInfo()),
        LoopBB(LoopBB), CondBranch(CondBranch), Comp(Comp),
        CompCounterOprNum(CompCounterOprNum), Update(Update),
        UpdateCounterOprNum(UpdateCounterOprNum), Init(Init),
        IsUpdatePriorComp(IsUpdatePriorComp), Cond(Cond.begin(), Cond.end()) {}

  bool shouldIgnoreForPipelining(const MachineInstr *MI) const override {
    // Make the instructions for loop control be placed in stage 0.
    // The predecessors of Comp are considered by the caller.
    return MI == Comp;
  }

  std::optional<bool> createTripCountGreaterCondition(
      int TC, MachineBasicBlock &MBB,
      SmallVectorImpl<MachineOperand> &CondParam) override {
    // A branch instruction will be inserted as "if (Cond) goto epilogue".
    // Cond is normalized for such use.
    // The predecessors of the branch are assumed to have already been inserted.
    CondParam = Cond;
    return {};
  }

  void createRemainingIterationsGreaterCondition(
      int TC, MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &Cond,
      DenseMap<MachineInstr *, MachineInstr *> &LastStage0Insts) override;

  void setPreheader(MachineBasicBlock *NewPreheader) override {}

  void adjustTripCount(int TripCountAdjust) override {}

  void disposed() override {}
  bool isMVEExpanderSupported() override { return true; }
};
} // namespace

/// Clone an instruction from MI. The register of ReplaceOprNum-th operand
/// is replaced by ReplaceReg. The output register is newly created.
/// The other operands are unchanged from MI.
static Register cloneInstr(const MachineInstr *MI, unsigned ReplaceOprNum,
                           Register ReplaceReg, MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator InsertTo) {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetInstrInfo *TII = MBB.getParent()->getSubtarget().getInstrInfo();
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();
  MachineInstr *NewMI = MBB.getParent()->CloneMachineInstr(MI);
  Register Result = 0;
  for (unsigned I = 0; I < NewMI->getNumOperands(); ++I) {
    if (I == 0 && NewMI->getOperand(0).getReg().isVirtual()) {
      Result = MRI.createVirtualRegister(
          MRI.getRegClass(NewMI->getOperand(0).getReg()));
      NewMI->getOperand(I).setReg(Result);
    } else if (I == ReplaceOprNum) {
      MRI.constrainRegClass(
          ReplaceReg,
          TII->getRegClass(NewMI->getDesc(), I, TRI, *MBB.getParent()));
      NewMI->getOperand(I).setReg(ReplaceReg);
    }
  }
  MBB.insert(InsertTo, NewMI);
  return Result;
}

void AArch64PipelinerLoopInfo::createRemainingIterationsGreaterCondition(
    int TC, MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &Cond,
    DenseMap<MachineInstr *, MachineInstr *> &LastStage0Insts) {
  // Create and accumulate conditions for next TC iterations.
  // Example:
  //   SUBSXrr N, counter, implicit-def $nzcv # compare instruction for the last
  //                                          # iteration of the kernel
  //
  //   # insert the following instructions
  //   cond = CSINCXr 0, 0, C, implicit $nzcv
  //   counter = ADDXri counter, 1            # clone from this->Update
  //   SUBSXrr n, counter, implicit-def $nzcv # clone from this->Comp
  //   cond = CSINCXr cond, cond, C, implicit $nzcv
  //   ... (repeat TC times)
  //   SUBSXri cond, 0, implicit-def $nzcv

  assert(CondBranch->getOpcode() == AArch64::Bcc);
  // CondCode to exit the loop
  AArch64CC::CondCode CC =
      (AArch64CC::CondCode)CondBranch->getOperand(0).getImm();
  if (CondBranch->getOperand(1).getMBB() == LoopBB)
    CC = AArch64CC::getInvertedCondCode(CC);

  // Accumulate conditions to exit the loop
  Register AccCond = AArch64::XZR;

  // If CC holds, CurCond+1 is returned; otherwise CurCond is returned.
  auto AccumulateCond = [&](Register CurCond,
                            AArch64CC::CondCode CC) -> Register {
    Register NewCond = MRI.createVirtualRegister(&AArch64::GPR64commonRegClass);
    BuildMI(MBB, MBB.end(), Comp->getDebugLoc(), TII->get(AArch64::CSINCXr))
        .addReg(NewCond, RegState::Define)
        .addReg(CurCond)
        .addReg(CurCond)
        .addImm(AArch64CC::getInvertedCondCode(CC));
    return NewCond;
  };

  if (!LastStage0Insts.empty() && LastStage0Insts[Comp]->getParent() == &MBB) {
    // Update and Comp for I==0 are already exists in MBB
    // (MBB is an unrolled kernel)
    Register Counter;
    for (int I = 0; I <= TC; ++I) {
      Register NextCounter;
      if (I != 0)
        NextCounter =
            cloneInstr(Comp, CompCounterOprNum, Counter, MBB, MBB.end());

      AccCond = AccumulateCond(AccCond, CC);

      if (I != TC) {
        if (I == 0) {
          if (Update != Comp && IsUpdatePriorComp) {
            Counter =
                LastStage0Insts[Comp]->getOperand(CompCounterOprNum).getReg();
            NextCounter = cloneInstr(Update, UpdateCounterOprNum, Counter, MBB,
                                     MBB.end());
          } else {
            // can use already calculated value
            NextCounter = LastStage0Insts[Update]->getOperand(0).getReg();
          }
        } else if (Update != Comp) {
          NextCounter =
              cloneInstr(Update, UpdateCounterOprNum, Counter, MBB, MBB.end());
        }
      }
      Counter = NextCounter;
    }
  } else {
    Register Counter;
    if (LastStage0Insts.empty()) {
      // use initial counter value (testing if the trip count is sufficient to
      // be executed by pipelined code)
      Counter = Init;
      if (IsUpdatePriorComp)
        Counter =
            cloneInstr(Update, UpdateCounterOprNum, Counter, MBB, MBB.end());
    } else {
      // MBB is an epilogue block. LastStage0Insts[Comp] is in the kernel block.
      Counter = LastStage0Insts[Comp]->getOperand(CompCounterOprNum).getReg();
    }

    for (int I = 0; I <= TC; ++I) {
      Register NextCounter;
      NextCounter =
          cloneInstr(Comp, CompCounterOprNum, Counter, MBB, MBB.end());
      AccCond = AccumulateCond(AccCond, CC);
      if (I != TC && Update != Comp)
        NextCounter =
            cloneInstr(Update, UpdateCounterOprNum, Counter, MBB, MBB.end());
      Counter = NextCounter;
    }
  }

  // If AccCond == 0, the remainder is greater than TC.
  BuildMI(MBB, MBB.end(), Comp->getDebugLoc(), TII->get(AArch64::SUBSXri))
      .addReg(AArch64::XZR, RegState::Define | RegState::Dead)
      .addReg(AccCond)
      .addImm(0)
      .addImm(0);
  Cond.clear();
  Cond.push_back(MachineOperand::CreateImm(AArch64CC::EQ));
}

static void extractPhiReg(const MachineInstr &Phi, const MachineBasicBlock *MBB,
                          Register &RegMBB, Register &RegOther) {
  assert(Phi.getNumOperands() == 5);
  if (Phi.getOperand(2).getMBB() == MBB) {
    RegMBB = Phi.getOperand(1).getReg();
    RegOther = Phi.getOperand(3).getReg();
  } else {
    assert(Phi.getOperand(4).getMBB() == MBB);
    RegMBB = Phi.getOperand(3).getReg();
    RegOther = Phi.getOperand(1).getReg();
  }
}

static bool isDefinedOutside(Register Reg, const MachineBasicBlock *BB) {
  if (!Reg.isVirtual())
    return false;
  const MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
  return MRI.getVRegDef(Reg)->getParent() != BB;
}

/// If Reg is an induction variable, return true and set some parameters
static bool getIndVarInfo(Register Reg, const MachineBasicBlock *LoopBB,
                          MachineInstr *&UpdateInst,
                          unsigned &UpdateCounterOprNum, Register &InitReg,
                          bool &IsUpdatePriorComp) {
  // Example:
  //
  // Preheader:
  //   InitReg = ...
  // LoopBB:
  //   Reg0 = PHI (InitReg, Preheader), (Reg1, LoopBB)
  //   Reg = COPY Reg0 ; COPY is ignored.
  //   Reg1 = ADD Reg, #1; UpdateInst. Incremented by a loop invariant value.
  //                     ; Reg is the value calculated in the previous
  //                     ; iteration, so IsUpdatePriorComp == false.

  if (LoopBB->pred_size() != 2)
    return false;
  if (!Reg.isVirtual())
    return false;
  const MachineRegisterInfo &MRI = LoopBB->getParent()->getRegInfo();
  UpdateInst = nullptr;
  UpdateCounterOprNum = 0;
  InitReg = 0;
  IsUpdatePriorComp = true;
  Register CurReg = Reg;
  while (true) {
    MachineInstr *Def = MRI.getVRegDef(CurReg);
    if (Def->getParent() != LoopBB)
      return false;
    if (Def->isCopy()) {
      // Ignore copy instructions unless they contain subregisters
      if (Def->getOperand(0).getSubReg() || Def->getOperand(1).getSubReg())
        return false;
      CurReg = Def->getOperand(1).getReg();
    } else if (Def->isPHI()) {
      if (InitReg != 0)
        return false;
      if (!UpdateInst)
        IsUpdatePriorComp = false;
      extractPhiReg(*Def, LoopBB, CurReg, InitReg);
    } else {
      if (UpdateInst)
        return false;
      switch (Def->getOpcode()) {
      case AArch64::ADDSXri:
      case AArch64::ADDSWri:
      case AArch64::SUBSXri:
      case AArch64::SUBSWri:
      case AArch64::ADDXri:
      case AArch64::ADDWri:
      case AArch64::SUBXri:
      case AArch64::SUBWri:
        UpdateInst = Def;
        UpdateCounterOprNum = 1;
        break;
      case AArch64::ADDSXrr:
      case AArch64::ADDSWrr:
      case AArch64::SUBSXrr:
      case AArch64::SUBSWrr:
      case AArch64::ADDXrr:
      case AArch64::ADDWrr:
      case AArch64::SUBXrr:
      case AArch64::SUBWrr:
        UpdateInst = Def;
        if (isDefinedOutside(Def->getOperand(2).getReg(), LoopBB))
          UpdateCounterOprNum = 1;
        else if (isDefinedOutside(Def->getOperand(1).getReg(), LoopBB))
          UpdateCounterOprNum = 2;
        else
          return false;
        break;
      default:
        return false;
      }
      CurReg = Def->getOperand(UpdateCounterOprNum).getReg();
    }

    if (!CurReg.isVirtual())
      return false;
    if (Reg == CurReg)
      break;
  }

  if (!UpdateInst)
    return false;

  return true;
}

std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
AArch64InstrInfo::analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const {
  // Accept loops that meet the following conditions
  // * The conditional branch is BCC
  // * The compare instruction is ADDS/SUBS/WHILEXX
  // * One operand of the compare is an induction variable and the other is a
  //   loop invariant value
  // * The induction variable is incremented/decremented by a single instruction
  // * Does not contain CALL or instructions which have unmodeled side effects

  for (MachineInstr &MI : *LoopBB)
    if (MI.isCall() || MI.hasUnmodeledSideEffects())
      // This instruction may use NZCV, which interferes with the instruction to
      // be inserted for loop control.
      return nullptr;

  MachineBasicBlock *TBB = nullptr, *FBB = nullptr;
  SmallVector<MachineOperand, 4> Cond;
  if (analyzeBranch(*LoopBB, TBB, FBB, Cond))
    return nullptr;

  // Infinite loops are not supported
  if (TBB == LoopBB && FBB == LoopBB)
    return nullptr;

  // Must be conditional branch
  if (TBB != LoopBB && FBB == nullptr)
    return nullptr;

  assert((TBB == LoopBB || FBB == LoopBB) &&
         "The Loop must be a single-basic-block loop");

  MachineInstr *CondBranch = &*LoopBB->getFirstTerminator();
  const TargetRegisterInfo &TRI = getRegisterInfo();

  if (CondBranch->getOpcode() != AArch64::Bcc)
    return nullptr;

  // Normalization for createTripCountGreaterCondition()
  if (TBB == LoopBB)
    reverseBranchCondition(Cond);

  MachineInstr *Comp = nullptr;
  unsigned CompCounterOprNum = 0;
  for (MachineInstr &MI : reverse(*LoopBB)) {
    if (MI.modifiesRegister(AArch64::NZCV, &TRI)) {
      // Guarantee that the compare is SUBS/ADDS/WHILEXX and that one of the
      // operands is a loop invariant value

      switch (MI.getOpcode()) {
      case AArch64::SUBSXri:
      case AArch64::SUBSWri:
      case AArch64::ADDSXri:
      case AArch64::ADDSWri:
        Comp = &MI;
        CompCounterOprNum = 1;
        break;
      case AArch64::ADDSWrr:
      case AArch64::ADDSXrr:
      case AArch64::SUBSWrr:
      case AArch64::SUBSXrr:
        Comp = &MI;
        break;
      default:
        if (isWhileOpcode(MI.getOpcode())) {
          Comp = &MI;
          break;
        }
        return nullptr;
      }

      if (CompCounterOprNum == 0) {
        if (isDefinedOutside(Comp->getOperand(1).getReg(), LoopBB))
          CompCounterOprNum = 2;
        else if (isDefinedOutside(Comp->getOperand(2).getReg(), LoopBB))
          CompCounterOprNum = 1;
        else
          return nullptr;
      }
      break;
    }
  }
  if (!Comp)
    return nullptr;

  MachineInstr *Update = nullptr;
  Register Init;
  bool IsUpdatePriorComp;
  unsigned UpdateCounterOprNum;
  if (!getIndVarInfo(Comp->getOperand(CompCounterOprNum).getReg(), LoopBB,
                     Update, UpdateCounterOprNum, Init, IsUpdatePriorComp))
    return nullptr;

  return std::make_unique<AArch64PipelinerLoopInfo>(
      LoopBB, CondBranch, Comp, CompCounterOprNum, Update, UpdateCounterOprNum,
      Init, IsUpdatePriorComp, Cond);
}

#define GET_INSTRINFO_HELPERS
#define GET_INSTRMAP_INFO
#include "AArch64GenInstrInfo.inc"
