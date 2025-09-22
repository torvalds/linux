//===----------------------- MipsBranchExpansion.cpp ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This pass do two things:
/// - it expands a branch or jump instruction into a long branch if its offset
///   is too large to fit into its immediate field,
/// - it inserts nops to prevent forbidden slot hazards.
///
/// The reason why this pass combines these two tasks is that one of these two
/// tasks can break the result of the previous one.
///
/// Example of that is a situation where at first, no branch should be expanded,
/// but after adding at least one nop somewhere in the code to prevent a
/// forbidden slot hazard, offset of some branches may go out of range. In that
/// case it is necessary to check again if there is some branch that needs
/// expansion. On the other hand, expanding some branch may cause a control
/// transfer instruction to appear in the forbidden slot, which is a hazard that
/// should be fixed. This pass alternates between this two tasks untill no
/// changes are made. Only then we can be sure that all branches are expanded
/// properly, and no hazard situations exist.
///
/// Regarding branch expanding:
///
/// When branch instruction like beqzc or bnezc has offset that is too large
/// to fit into its immediate field, it has to be expanded to another
/// instruction or series of instructions.
///
/// FIXME: Fix pc-region jump instructions which cross 256MB segment boundaries.
/// TODO: Handle out of range bc, b (pseudo) instructions.
///
/// Regarding compact branch hazard prevention:
///
/// Hazards handled: forbidden slots for MIPSR6, FPU slots for MIPS3 and below,
/// load delay slots for MIPS1.
///
/// A forbidden slot hazard occurs when a compact branch instruction is executed
/// and the adjacent instruction in memory is a control transfer instruction
/// such as a branch or jump, ERET, ERETNC, DERET, WAIT and PAUSE.
///
/// For example:
///
/// 0x8004      bnec    a1,v0,<P+0x18>
/// 0x8008      beqc    a1,a2,<P+0x54>
///
/// In such cases, the processor is required to signal a Reserved Instruction
/// exception.
///
/// Here, if the instruction at 0x8004 is executed, the processor will raise an
/// exception as there is a control transfer instruction at 0x8008.
///
/// There are two sources of forbidden slot hazards:
///
/// A) A previous pass has created a compact branch directly.
/// B) Transforming a delay slot branch into compact branch. This case can be
///    difficult to process as lookahead for hazards is insufficient, as
///    backwards delay slot fillling can also produce hazards in previously
///    processed instuctions.
///
/// In future this pass can be extended (or new pass can be created) to handle
/// other pipeline hazards, such as various MIPS1 hazards, processor errata that
/// require instruction reorganization, etc.
///
/// This pass has to run after the delay slot filler as that pass can introduce
/// pipeline hazards such as compact branch hazard, hence the existing hazard
/// recognizer is not suitable.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MipsABIInfo.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "MCTargetDesc/MipsMCNaCl.h"
#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "MipsInstrInfo.h"
#include "MipsMachineFunction.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "mips-branch-expansion"

STATISTIC(NumInsertedNops, "Number of nops inserted");
STATISTIC(LongBranches, "Number of long branches.");

static cl::opt<bool>
    SkipLongBranch("skip-mips-long-branch", cl::init(false),
                   cl::desc("MIPS: Skip branch expansion pass."), cl::Hidden);

static cl::opt<bool>
    ForceLongBranch("force-mips-long-branch", cl::init(false),
                    cl::desc("MIPS: Expand all branches to long format."),
                    cl::Hidden);

namespace {

using Iter = MachineBasicBlock::iterator;
using ReverseIter = MachineBasicBlock::reverse_iterator;

struct MBBInfo {
  uint64_t Size = 0;
  bool HasLongBranch = false;
  MachineInstr *Br = nullptr;
  uint64_t Offset = 0;
  MBBInfo() = default;
};

class MipsBranchExpansion : public MachineFunctionPass {
public:
  static char ID;

  MipsBranchExpansion() : MachineFunctionPass(ID), ABI(MipsABIInfo::Unknown()) {
    initializeMipsBranchExpansionPass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Mips Branch Expansion Pass";
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  void splitMBB(MachineBasicBlock *MBB);
  void initMBBInfo();
  int64_t computeOffset(const MachineInstr *Br);
  uint64_t computeOffsetFromTheBeginning(int MBB);
  void replaceBranch(MachineBasicBlock &MBB, Iter Br, const DebugLoc &DL,
                     MachineBasicBlock *MBBOpnd);
  bool buildProperJumpMI(MachineBasicBlock *MBB,
                         MachineBasicBlock::iterator Pos, DebugLoc DL);
  void expandToLongBranch(MBBInfo &Info);
  template <typename Pred, typename Safe>
  bool handleSlot(Pred Predicate, Safe SafeInSlot);
  bool handleForbiddenSlot();
  bool handleFPUDelaySlot();
  bool handleLoadDelaySlot();
  bool handlePossibleLongBranch();

  const MipsSubtarget *STI;
  const MipsInstrInfo *TII;

  MachineFunction *MFp;
  SmallVector<MBBInfo, 16> MBBInfos;
  bool IsPIC;
  MipsABIInfo ABI;
  bool ForceLongBranchFirstPass = false;
};

} // end of anonymous namespace

char MipsBranchExpansion::ID = 0;

INITIALIZE_PASS(MipsBranchExpansion, DEBUG_TYPE,
                "Expand out of range branch instructions and fix forbidden"
                " slot hazards",
                false, false)

/// Returns a pass that clears pipeline hazards.
FunctionPass *llvm::createMipsBranchExpansion() {
  return new MipsBranchExpansion();
}

// Find the next real instruction from the current position in current basic
// block.
static Iter getNextMachineInstrInBB(Iter Position) {
  Iter I = Position, E = Position->getParent()->end();
  I = std::find_if_not(I, E,
                       [](const Iter &Insn) { return Insn->isTransient(); });

  return I;
}

// Find the next real instruction from the current position, looking through
// basic block boundaries.
static std::pair<Iter, bool> getNextMachineInstr(Iter Position,
                                                 MachineBasicBlock *Parent) {
  if (Position == Parent->end()) {
    do {
      MachineBasicBlock *Succ = Parent->getNextNode();
      if (Succ != nullptr && Parent->isSuccessor(Succ)) {
        Position = Succ->begin();
        Parent = Succ;
      } else {
        return std::make_pair(Position, true);
      }
    } while (Parent->empty());
  }

  Iter Instr = getNextMachineInstrInBB(Position);
  if (Instr == Parent->end()) {
    return getNextMachineInstr(Instr, Parent);
  }
  return std::make_pair(Instr, false);
}

/// Iterate over list of Br's operands and search for a MachineBasicBlock
/// operand.
static MachineBasicBlock *getTargetMBB(const MachineInstr &Br) {
  for (unsigned I = 0, E = Br.getDesc().getNumOperands(); I < E; ++I) {
    const MachineOperand &MO = Br.getOperand(I);

    if (MO.isMBB())
      return MO.getMBB();
  }

  llvm_unreachable("This instruction does not have an MBB operand.");
}

// Traverse the list of instructions backwards until a non-debug instruction is
// found or it reaches E.
static ReverseIter getNonDebugInstr(ReverseIter B, const ReverseIter &E) {
  for (; B != E; ++B)
    if (!B->isDebugInstr())
      return B;

  return E;
}

// Split MBB if it has two direct jumps/branches.
void MipsBranchExpansion::splitMBB(MachineBasicBlock *MBB) {
  ReverseIter End = MBB->rend();
  ReverseIter LastBr = getNonDebugInstr(MBB->rbegin(), End);

  // Return if MBB has no branch instructions.
  if ((LastBr == End) ||
      (!LastBr->isConditionalBranch() && !LastBr->isUnconditionalBranch()))
    return;

  ReverseIter FirstBr = getNonDebugInstr(std::next(LastBr), End);

  // MBB has only one branch instruction if FirstBr is not a branch
  // instruction.
  if ((FirstBr == End) ||
      (!FirstBr->isConditionalBranch() && !FirstBr->isUnconditionalBranch()))
    return;

  assert(!FirstBr->isIndirectBranch() && "Unexpected indirect branch found.");

  // Create a new MBB. Move instructions in MBB to the newly created MBB.
  MachineBasicBlock *NewMBB =
      MFp->CreateMachineBasicBlock(MBB->getBasicBlock());

  // Insert NewMBB and fix control flow.
  MachineBasicBlock *Tgt = getTargetMBB(*FirstBr);
  NewMBB->transferSuccessors(MBB);
  if (Tgt != getTargetMBB(*LastBr))
    NewMBB->removeSuccessor(Tgt, true);
  MBB->addSuccessor(NewMBB);
  MBB->addSuccessor(Tgt);
  MFp->insert(std::next(MachineFunction::iterator(MBB)), NewMBB);

  NewMBB->splice(NewMBB->end(), MBB, LastBr.getReverse(), MBB->end());
}

// Fill MBBInfos.
void MipsBranchExpansion::initMBBInfo() {
  // Split the MBBs if they have two branches. Each basic block should have at
  // most one branch after this loop is executed.
  for (auto &MBB : *MFp)
    splitMBB(&MBB);

  MFp->RenumberBlocks();
  MBBInfos.clear();
  MBBInfos.resize(MFp->size());

  for (unsigned I = 0, E = MBBInfos.size(); I < E; ++I) {
    MachineBasicBlock *MBB = MFp->getBlockNumbered(I);

    // Compute size of MBB.
    for (MachineInstr &MI : MBB->instrs())
      MBBInfos[I].Size += TII->getInstSizeInBytes(MI);
  }
}

// Compute offset of branch in number of bytes.
int64_t MipsBranchExpansion::computeOffset(const MachineInstr *Br) {
  int64_t Offset = 0;
  int ThisMBB = Br->getParent()->getNumber();
  int TargetMBB = getTargetMBB(*Br)->getNumber();

  // Compute offset of a forward branch.
  if (ThisMBB < TargetMBB) {
    for (int N = ThisMBB + 1; N < TargetMBB; ++N)
      Offset += MBBInfos[N].Size;

    return Offset + 4;
  }

  // Compute offset of a backward branch.
  for (int N = ThisMBB; N >= TargetMBB; --N)
    Offset += MBBInfos[N].Size;

  return -Offset + 4;
}

// Returns the distance in bytes up until MBB
uint64_t MipsBranchExpansion::computeOffsetFromTheBeginning(int MBB) {
  uint64_t Offset = 0;
  for (int N = 0; N < MBB; ++N)
    Offset += MBBInfos[N].Size;
  return Offset;
}

// Replace Br with a branch which has the opposite condition code and a
// MachineBasicBlock operand MBBOpnd.
void MipsBranchExpansion::replaceBranch(MachineBasicBlock &MBB, Iter Br,
                                        const DebugLoc &DL,
                                        MachineBasicBlock *MBBOpnd) {
  unsigned NewOpc = TII->getOppositeBranchOpc(Br->getOpcode());
  const MCInstrDesc &NewDesc = TII->get(NewOpc);

  MachineInstrBuilder MIB = BuildMI(MBB, Br, DL, NewDesc);

  for (unsigned I = 0, E = Br->getDesc().getNumOperands(); I < E; ++I) {
    MachineOperand &MO = Br->getOperand(I);

    switch (MO.getType()) {
    case MachineOperand::MO_Register:
      MIB.addReg(MO.getReg());
      break;
    case MachineOperand::MO_Immediate:
      // Octeon BBIT family of branch has an immediate operand
      // (e.g. BBIT0 $v0, 3, %bb.1).
      if (!TII->isBranchWithImm(Br->getOpcode()))
        llvm_unreachable("Unexpected immediate in branch instruction");
      MIB.addImm(MO.getImm());
      break;
    case MachineOperand::MO_MachineBasicBlock:
      MIB.addMBB(MBBOpnd);
      break;
    default:
      llvm_unreachable("Unexpected operand type in branch instruction");
    }
  }

  if (Br->hasDelaySlot()) {
    // Bundle the instruction in the delay slot to the newly created branch
    // and erase the original branch.
    assert(Br->isBundledWithSucc());
    MachineBasicBlock::instr_iterator II = Br.getInstrIterator();
    MIBundleBuilder(&*MIB).append((++II)->removeFromBundle());
  }
  Br->eraseFromParent();
}

bool MipsBranchExpansion::buildProperJumpMI(MachineBasicBlock *MBB,
                                            MachineBasicBlock::iterator Pos,
                                            DebugLoc DL) {
  bool HasR6 = ABI.IsN64() ? STI->hasMips64r6() : STI->hasMips32r6();
  bool AddImm = HasR6 && !STI->useIndirectJumpsHazard();

  unsigned JR = ABI.IsN64() ? Mips::JR64 : Mips::JR;
  unsigned JIC = ABI.IsN64() ? Mips::JIC64 : Mips::JIC;
  unsigned JR_HB = ABI.IsN64() ? Mips::JR_HB64 : Mips::JR_HB;
  unsigned JR_HB_R6 = ABI.IsN64() ? Mips::JR_HB64_R6 : Mips::JR_HB_R6;

  unsigned JumpOp;
  if (STI->useIndirectJumpsHazard())
    JumpOp = HasR6 ? JR_HB_R6 : JR_HB;
  else
    JumpOp = HasR6 ? JIC : JR;

  if (JumpOp == Mips::JIC && STI->inMicroMipsMode())
    JumpOp = Mips::JIC_MMR6;

  unsigned ATReg = ABI.IsN64() ? Mips::AT_64 : Mips::AT;
  MachineInstrBuilder Instr =
      BuildMI(*MBB, Pos, DL, TII->get(JumpOp)).addReg(ATReg);
  if (AddImm)
    Instr.addImm(0);

  return !AddImm;
}

// Expand branch instructions to long branches.
// TODO: This function has to be fixed for beqz16 and bnez16, because it
// currently assumes that all branches have 16-bit offsets, and will produce
// wrong code if branches whose allowed offsets are [-128, -126, ..., 126]
// are present.
void MipsBranchExpansion::expandToLongBranch(MBBInfo &I) {
  MachineBasicBlock::iterator Pos;
  MachineBasicBlock *MBB = I.Br->getParent(), *TgtMBB = getTargetMBB(*I.Br);
  DebugLoc DL = I.Br->getDebugLoc();
  const BasicBlock *BB = MBB->getBasicBlock();
  MachineFunction::iterator FallThroughMBB = ++MachineFunction::iterator(MBB);
  MachineBasicBlock *LongBrMBB = MFp->CreateMachineBasicBlock(BB);

  MFp->insert(FallThroughMBB, LongBrMBB);
  MBB->replaceSuccessor(TgtMBB, LongBrMBB);

  if (IsPIC) {
    MachineBasicBlock *BalTgtMBB = MFp->CreateMachineBasicBlock(BB);
    MFp->insert(FallThroughMBB, BalTgtMBB);
    LongBrMBB->addSuccessor(BalTgtMBB);
    BalTgtMBB->addSuccessor(TgtMBB);

    // We must select between the MIPS32r6/MIPS64r6 BALC (which is a normal
    // instruction) and the pre-MIPS32r6/MIPS64r6 definition (which is an
    // pseudo-instruction wrapping BGEZAL).
    const unsigned BalOp =
        STI->hasMips32r6()
            ? STI->inMicroMipsMode() ? Mips::BALC_MMR6 : Mips::BALC
            : STI->inMicroMipsMode() ? Mips::BAL_BR_MM : Mips::BAL_BR;

    if (!ABI.IsN64()) {
      // Pre R6:
      // $longbr:
      //  addiu $sp, $sp, -8
      //  sw $ra, 0($sp)
      //  lui $at, %hi($tgt - $baltgt)
      //  bal $baltgt
      //  addiu $at, $at, %lo($tgt - $baltgt)
      // $baltgt:
      //  addu $at, $ra, $at
      //  lw $ra, 0($sp)
      //  jr $at
      //  addiu $sp, $sp, 8
      // $fallthrough:
      //

      // R6:
      // $longbr:
      //  addiu $sp, $sp, -8
      //  sw $ra, 0($sp)
      //  lui $at, %hi($tgt - $baltgt)
      //  addiu $at, $at, %lo($tgt - $baltgt)
      //  balc $baltgt
      // $baltgt:
      //  addu $at, $ra, $at
      //  lw $ra, 0($sp)
      //  addiu $sp, $sp, 8
      //  jic $at, 0
      // $fallthrough:

      Pos = LongBrMBB->begin();

      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::ADDiu), Mips::SP)
          .addReg(Mips::SP)
          .addImm(-8);
      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::SW))
          .addReg(Mips::RA)
          .addReg(Mips::SP)
          .addImm(0);

      // LUi and ADDiu instructions create 32-bit offset of the target basic
      // block from the target of BAL(C) instruction.  We cannot use immediate
      // value for this offset because it cannot be determined accurately when
      // the program has inline assembly statements.  We therefore use the
      // relocation expressions %hi($tgt-$baltgt) and %lo($tgt-$baltgt) which
      // are resolved during the fixup, so the values will always be correct.
      //
      // Since we cannot create %hi($tgt-$baltgt) and %lo($tgt-$baltgt)
      // expressions at this point (it is possible only at the MC layer),
      // we replace LUi and ADDiu with pseudo instructions
      // LONG_BRANCH_LUi and LONG_BRANCH_ADDiu, and add both basic
      // blocks as operands to these instructions.  When lowering these pseudo
      // instructions to LUi and ADDiu in the MC layer, we will create
      // %hi($tgt-$baltgt) and %lo($tgt-$baltgt) expressions and add them as
      // operands to lowered instructions.

      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_LUi), Mips::AT)
          .addMBB(TgtMBB, MipsII::MO_ABS_HI)
          .addMBB(BalTgtMBB);

      MachineInstrBuilder BalInstr =
          BuildMI(*MFp, DL, TII->get(BalOp)).addMBB(BalTgtMBB);
      MachineInstrBuilder ADDiuInstr =
          BuildMI(*MFp, DL, TII->get(Mips::LONG_BRANCH_ADDiu), Mips::AT)
              .addReg(Mips::AT)
              .addMBB(TgtMBB, MipsII::MO_ABS_LO)
              .addMBB(BalTgtMBB);
      if (STI->hasMips32r6()) {
        LongBrMBB->insert(Pos, ADDiuInstr);
        LongBrMBB->insert(Pos, BalInstr);
      } else {
        LongBrMBB->insert(Pos, BalInstr);
        LongBrMBB->insert(Pos, ADDiuInstr);
        LongBrMBB->rbegin()->bundleWithPred();
      }

      Pos = BalTgtMBB->begin();

      BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::ADDu), Mips::AT)
          .addReg(Mips::RA)
          .addReg(Mips::AT);
      BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::LW), Mips::RA)
          .addReg(Mips::SP)
          .addImm(0);
      if (STI->isTargetNaCl())
        // Bundle-align the target of indirect branch JR.
        TgtMBB->setAlignment(MIPS_NACL_BUNDLE_ALIGN);

      // In NaCl, modifying the sp is not allowed in branch delay slot.
      // For MIPS32R6, we can skip using a delay slot branch.
      bool hasDelaySlot = buildProperJumpMI(BalTgtMBB, Pos, DL);

      if (STI->isTargetNaCl() || !hasDelaySlot) {
        BuildMI(*BalTgtMBB, std::prev(Pos), DL, TII->get(Mips::ADDiu), Mips::SP)
            .addReg(Mips::SP)
            .addImm(8);
      }
      if (hasDelaySlot) {
        if (STI->isTargetNaCl()) {
          TII->insertNop(*BalTgtMBB, Pos, DL);
        } else {
          BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::ADDiu), Mips::SP)
              .addReg(Mips::SP)
              .addImm(8);
        }
        BalTgtMBB->rbegin()->bundleWithPred();
      }
    } else {
      // Pre R6:
      // $longbr:
      //  daddiu $sp, $sp, -16
      //  sd $ra, 0($sp)
      //  daddiu $at, $zero, %hi($tgt - $baltgt)
      //  dsll $at, $at, 16
      //  bal $baltgt
      //  daddiu $at, $at, %lo($tgt - $baltgt)
      // $baltgt:
      //  daddu $at, $ra, $at
      //  ld $ra, 0($sp)
      //  jr64 $at
      //  daddiu $sp, $sp, 16
      // $fallthrough:

      // R6:
      // $longbr:
      //  daddiu $sp, $sp, -16
      //  sd $ra, 0($sp)
      //  daddiu $at, $zero, %hi($tgt - $baltgt)
      //  dsll $at, $at, 16
      //  daddiu $at, $at, %lo($tgt - $baltgt)
      //  balc $baltgt
      // $baltgt:
      //  daddu $at, $ra, $at
      //  ld $ra, 0($sp)
      //  daddiu $sp, $sp, 16
      //  jic $at, 0
      // $fallthrough:

      // We assume the branch is within-function, and that offset is within
      // +/- 2GB.  High 32 bits will therefore always be zero.

      // Note that this will work even if the offset is negative, because
      // of the +1 modification that's added in that case.  For example, if the
      // offset is -1MB (0xFFFFFFFFFFF00000), the computation for %higher is
      //
      // 0xFFFFFFFFFFF00000 + 0x80008000 = 0x000000007FF08000
      //
      // and the bits [47:32] are zero.  For %highest
      //
      // 0xFFFFFFFFFFF00000 + 0x800080008000 = 0x000080007FF08000
      //
      // and the bits [63:48] are zero.

      Pos = LongBrMBB->begin();

      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::DADDiu), Mips::SP_64)
          .addReg(Mips::SP_64)
          .addImm(-16);
      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::SD))
          .addReg(Mips::RA_64)
          .addReg(Mips::SP_64)
          .addImm(0);
      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_DADDiu),
              Mips::AT_64)
          .addReg(Mips::ZERO_64)
          .addMBB(TgtMBB, MipsII::MO_ABS_HI)
          .addMBB(BalTgtMBB);
      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::DSLL), Mips::AT_64)
          .addReg(Mips::AT_64)
          .addImm(16);

      MachineInstrBuilder BalInstr =
          BuildMI(*MFp, DL, TII->get(BalOp)).addMBB(BalTgtMBB);
      MachineInstrBuilder DADDiuInstr =
          BuildMI(*MFp, DL, TII->get(Mips::LONG_BRANCH_DADDiu), Mips::AT_64)
              .addReg(Mips::AT_64)
              .addMBB(TgtMBB, MipsII::MO_ABS_LO)
              .addMBB(BalTgtMBB);
      if (STI->hasMips32r6()) {
        LongBrMBB->insert(Pos, DADDiuInstr);
        LongBrMBB->insert(Pos, BalInstr);
      } else {
        LongBrMBB->insert(Pos, BalInstr);
        LongBrMBB->insert(Pos, DADDiuInstr);
        LongBrMBB->rbegin()->bundleWithPred();
      }

      Pos = BalTgtMBB->begin();

      BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::DADDu), Mips::AT_64)
          .addReg(Mips::RA_64)
          .addReg(Mips::AT_64);
      BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::LD), Mips::RA_64)
          .addReg(Mips::SP_64)
          .addImm(0);

      bool hasDelaySlot = buildProperJumpMI(BalTgtMBB, Pos, DL);
      // If there is no delay slot, Insert stack adjustment before
      if (!hasDelaySlot) {
        BuildMI(*BalTgtMBB, std::prev(Pos), DL, TII->get(Mips::DADDiu),
                Mips::SP_64)
            .addReg(Mips::SP_64)
            .addImm(16);
      } else {
        BuildMI(*BalTgtMBB, Pos, DL, TII->get(Mips::DADDiu), Mips::SP_64)
            .addReg(Mips::SP_64)
            .addImm(16);
        BalTgtMBB->rbegin()->bundleWithPred();
      }
    }
  } else { // Not PIC
    Pos = LongBrMBB->begin();
    LongBrMBB->addSuccessor(TgtMBB);

    // Compute the position of the potentiall jump instruction (basic blocks
    // before + 4 for the instruction)
    uint64_t JOffset = computeOffsetFromTheBeginning(MBB->getNumber()) +
                       MBBInfos[MBB->getNumber()].Size + 4;
    uint64_t TgtMBBOffset = computeOffsetFromTheBeginning(TgtMBB->getNumber());
    // If it's a forward jump, then TgtMBBOffset will be shifted by two
    // instructions
    if (JOffset < TgtMBBOffset)
      TgtMBBOffset += 2 * 4;
    // Compare 4 upper bits to check if it's the same segment
    bool SameSegmentJump = JOffset >> 28 == TgtMBBOffset >> 28;

    if (STI->hasMips32r6() && TII->isBranchOffsetInRange(Mips::BC, I.Offset)) {
      // R6:
      // $longbr:
      //  bc $tgt
      // $fallthrough:
      //
      BuildMI(*LongBrMBB, Pos, DL,
              TII->get(STI->inMicroMipsMode() ? Mips::BC_MMR6 : Mips::BC))
          .addMBB(TgtMBB);
    } else if (SameSegmentJump) {
      // Pre R6:
      // $longbr:
      //  j $tgt
      //  nop
      // $fallthrough:
      //
      BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::J)).addMBB(TgtMBB);
      TII->insertNop(*LongBrMBB, Pos, DL)->bundleWithPred();
    } else {
      // At this point, offset where we need to branch does not fit into
      // immediate field of the branch instruction and is not in the same
      // segment as jump instruction. Therefore we will break it into couple
      // instructions, where we first load the offset into register, and then we
      // do branch register.
      if (ABI.IsN64()) {
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_LUi2Op_64),
                Mips::AT_64)
            .addMBB(TgtMBB, MipsII::MO_HIGHEST);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_DADDiu2Op),
                Mips::AT_64)
            .addReg(Mips::AT_64)
            .addMBB(TgtMBB, MipsII::MO_HIGHER);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::DSLL), Mips::AT_64)
            .addReg(Mips::AT_64)
            .addImm(16);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_DADDiu2Op),
                Mips::AT_64)
            .addReg(Mips::AT_64)
            .addMBB(TgtMBB, MipsII::MO_ABS_HI);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::DSLL), Mips::AT_64)
            .addReg(Mips::AT_64)
            .addImm(16);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_DADDiu2Op),
                Mips::AT_64)
            .addReg(Mips::AT_64)
            .addMBB(TgtMBB, MipsII::MO_ABS_LO);
      } else {
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_LUi2Op),
                Mips::AT)
            .addMBB(TgtMBB, MipsII::MO_ABS_HI);
        BuildMI(*LongBrMBB, Pos, DL, TII->get(Mips::LONG_BRANCH_ADDiu2Op),
                Mips::AT)
            .addReg(Mips::AT)
            .addMBB(TgtMBB, MipsII::MO_ABS_LO);
      }
      buildProperJumpMI(LongBrMBB, Pos, DL);
    }
  }

  if (I.Br->isUnconditionalBranch()) {
    // Change branch destination.
    assert(I.Br->getDesc().getNumOperands() == 1);
    I.Br->removeOperand(0);
    I.Br->addOperand(MachineOperand::CreateMBB(LongBrMBB));
  } else
    // Change branch destination and reverse condition.
    replaceBranch(*MBB, I.Br, DL, &*FallThroughMBB);
}

static void emitGPDisp(MachineFunction &F, const MipsInstrInfo *TII) {
  MachineBasicBlock &MBB = F.front();
  MachineBasicBlock::iterator I = MBB.begin();
  DebugLoc DL = MBB.findDebugLoc(MBB.begin());
  BuildMI(MBB, I, DL, TII->get(Mips::LUi), Mips::V0)
      .addExternalSymbol("_gp_disp", MipsII::MO_ABS_HI);
  BuildMI(MBB, I, DL, TII->get(Mips::ADDiu), Mips::V0)
      .addReg(Mips::V0)
      .addExternalSymbol("_gp_disp", MipsII::MO_ABS_LO);
  MBB.removeLiveIn(Mips::V0);
}

template <typename Pred, typename Safe>
bool MipsBranchExpansion::handleSlot(Pred Predicate, Safe SafeInSlot) {
  bool Changed = false;

  for (MachineFunction::iterator FI = MFp->begin(); FI != MFp->end(); ++FI) {
    for (Iter I = FI->begin(); I != FI->end(); ++I) {

      // Delay slot hazard handling. Use lookahead over state.
      if (!Predicate(*I))
        continue;

      Iter IInSlot;
      bool LastInstInFunction =
          std::next(I) == FI->end() && std::next(FI) == MFp->end();
      if (!LastInstInFunction) {
        std::pair<Iter, bool> Res = getNextMachineInstr(std::next(I), &*FI);
        LastInstInFunction |= Res.second;
        IInSlot = Res.first;
      }

      if (LastInstInFunction || !SafeInSlot(*IInSlot, *I)) {
        MachineBasicBlock::instr_iterator Iit = I->getIterator();
        if (std::next(Iit) == FI->end() ||
            std::next(Iit)->getOpcode() != Mips::NOP) {
          Changed = true;
          TII->insertNop(*(I->getParent()), std::next(I), I->getDebugLoc())
              ->bundleWithPred();
          NumInsertedNops++;
        }
      }
    }
  }

  return Changed;
}

bool MipsBranchExpansion::handleForbiddenSlot() {
  // Forbidden slot hazards are only defined for MIPSR6 but not microMIPSR6.
  if (!STI->hasMips32r6() || STI->inMicroMipsMode())
    return false;

  return handleSlot(
      [this](auto &I) -> bool { return TII->HasForbiddenSlot(I); },
      [this](auto &IInSlot, auto &I) -> bool {
        return TII->SafeInForbiddenSlot(IInSlot);
      });
}

bool MipsBranchExpansion::handleFPUDelaySlot() {
  // FPU delay slots are only defined for MIPS3 and below.
  if (STI->hasMips32() || STI->hasMips4())
    return false;

  return handleSlot([this](auto &I) -> bool { return TII->HasFPUDelaySlot(I); },
                    [this](auto &IInSlot, auto &I) -> bool {
                      return TII->SafeInFPUDelaySlot(IInSlot, I);
                    });
}

bool MipsBranchExpansion::handleLoadDelaySlot() {
  // Load delay slot hazards are only for MIPS1.
  if (STI->hasMips2())
    return false;

  return handleSlot(
      [this](auto &I) -> bool { return TII->HasLoadDelaySlot(I); },
      [this](auto &IInSlot, auto &I) -> bool {
        return TII->SafeInLoadDelaySlot(IInSlot, I);
      });
}

bool MipsBranchExpansion::handlePossibleLongBranch() {
  if (STI->inMips16Mode() || !STI->enableLongBranchPass())
    return false;

  if (SkipLongBranch)
    return false;

  bool EverMadeChange = false, MadeChange = true;

  while (MadeChange) {
    MadeChange = false;

    initMBBInfo();

    for (unsigned I = 0, E = MBBInfos.size(); I < E; ++I) {
      MachineBasicBlock *MBB = MFp->getBlockNumbered(I);
      // Search for MBB's branch instruction.
      ReverseIter End = MBB->rend();
      ReverseIter Br = getNonDebugInstr(MBB->rbegin(), End);

      if ((Br != End) && Br->isBranch() && !Br->isIndirectBranch() &&
          (Br->isConditionalBranch() ||
           (Br->isUnconditionalBranch() && IsPIC))) {
        int64_t Offset = computeOffset(&*Br);

        if (STI->isTargetNaCl()) {
          // The offset calculation does not include sandboxing instructions
          // that will be added later in the MC layer.  Since at this point we
          // don't know the exact amount of code that "sandboxing" will add, we
          // conservatively estimate that code will not grow more than 100%.
          Offset *= 2;
        }

        if (ForceLongBranchFirstPass ||
            !TII->isBranchOffsetInRange(Br->getOpcode(), Offset)) {
          MBBInfos[I].Offset = Offset;
          MBBInfos[I].Br = &*Br;
        }
      }
    } // End for

    ForceLongBranchFirstPass = false;

    SmallVectorImpl<MBBInfo>::iterator I, E = MBBInfos.end();

    for (I = MBBInfos.begin(); I != E; ++I) {
      // Skip if this MBB doesn't have a branch or the branch has already been
      // converted to a long branch.
      if (!I->Br)
        continue;

      expandToLongBranch(*I);
      ++LongBranches;
      EverMadeChange = MadeChange = true;
    }

    MFp->RenumberBlocks();
  }

  return EverMadeChange;
}

bool MipsBranchExpansion::runOnMachineFunction(MachineFunction &MF) {
  const TargetMachine &TM = MF.getTarget();
  IsPIC = TM.isPositionIndependent();
  ABI = static_cast<const MipsTargetMachine &>(TM).getABI();
  STI = &MF.getSubtarget<MipsSubtarget>();
  TII = static_cast<const MipsInstrInfo *>(STI->getInstrInfo());

  if (IsPIC && ABI.IsO32() &&
      MF.getInfo<MipsFunctionInfo>()->globalBaseRegSet())
    emitGPDisp(MF, TII);

  MFp = &MF;

  ForceLongBranchFirstPass = ForceLongBranch;
  // Run these at least once.
  bool longBranchChanged = handlePossibleLongBranch();
  bool forbiddenSlotChanged = handleForbiddenSlot();
  bool fpuDelaySlotChanged = handleFPUDelaySlot();
  bool loadDelaySlotChanged = handleLoadDelaySlot();

  bool Changed = longBranchChanged || forbiddenSlotChanged ||
                 fpuDelaySlotChanged || loadDelaySlotChanged;

  // Then run them alternatively while there are changes.
  while (forbiddenSlotChanged) {
    longBranchChanged = handlePossibleLongBranch();
    fpuDelaySlotChanged = handleFPUDelaySlot();
    loadDelaySlotChanged = handleLoadDelaySlot();
    if (!longBranchChanged && !fpuDelaySlotChanged && !loadDelaySlotChanged)
      break;
    forbiddenSlotChanged = handleForbiddenSlot();
  }

  return Changed;
}
