//===-- SystemZElimCompare.cpp - Eliminate comparison instructions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass:
// (1) tries to remove compares if CC already contains the required information
// (2) fuses compares and branches into COMPARE AND BRANCH instructions
//
//===----------------------------------------------------------------------===//

#include "SystemZ.h"
#include "SystemZInstrInfo.h"
#include "SystemZTargetMachine.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/MC/MCInstrDesc.h"
#include <cassert>
#include <cstdint>

using namespace llvm;

#define DEBUG_TYPE "systemz-elim-compare"

STATISTIC(BranchOnCounts, "Number of branch-on-count instructions");
STATISTIC(LoadAndTraps, "Number of load-and-trap instructions");
STATISTIC(EliminatedComparisons, "Number of eliminated comparisons");
STATISTIC(FusedComparisons, "Number of fused compare-and-branch instructions");

namespace {

// Represents the references to a particular register in one or more
// instructions.
struct Reference {
  Reference() = default;

  Reference &operator|=(const Reference &Other) {
    Def |= Other.Def;
    Use |= Other.Use;
    return *this;
  }

  explicit operator bool() const { return Def || Use; }

  // True if the register is defined or used in some form, either directly or
  // via a sub- or super-register.
  bool Def = false;
  bool Use = false;
};

class SystemZElimCompare : public MachineFunctionPass {
public:
  static char ID;

  SystemZElimCompare() : MachineFunctionPass(ID) {
    initializeSystemZElimComparePass(*PassRegistry::getPassRegistry());
  }

  bool processBlock(MachineBasicBlock &MBB);
  bool runOnMachineFunction(MachineFunction &F) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  Reference getRegReferences(MachineInstr &MI, unsigned Reg);
  bool convertToBRCT(MachineInstr &MI, MachineInstr &Compare,
                     SmallVectorImpl<MachineInstr *> &CCUsers);
  bool convertToLoadAndTrap(MachineInstr &MI, MachineInstr &Compare,
                            SmallVectorImpl<MachineInstr *> &CCUsers);
  bool convertToLoadAndTest(MachineInstr &MI, MachineInstr &Compare,
                            SmallVectorImpl<MachineInstr *> &CCUsers);
  bool convertToLogical(MachineInstr &MI, MachineInstr &Compare,
                        SmallVectorImpl<MachineInstr *> &CCUsers);
  bool adjustCCMasksForInstr(MachineInstr &MI, MachineInstr &Compare,
                             SmallVectorImpl<MachineInstr *> &CCUsers,
                             unsigned ConvOpc = 0);
  bool optimizeCompareZero(MachineInstr &Compare,
                           SmallVectorImpl<MachineInstr *> &CCUsers);
  bool fuseCompareOperations(MachineInstr &Compare,
                             SmallVectorImpl<MachineInstr *> &CCUsers);

  const SystemZInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
};

char SystemZElimCompare::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS(SystemZElimCompare, DEBUG_TYPE,
                "SystemZ Comparison Elimination", false, false)

// Returns true if MI is an instruction whose output equals the value in Reg.
static bool preservesValueOf(MachineInstr &MI, unsigned Reg) {
  switch (MI.getOpcode()) {
  case SystemZ::LR:
  case SystemZ::LGR:
  case SystemZ::LGFR:
  case SystemZ::LTR:
  case SystemZ::LTGR:
  case SystemZ::LTGFR:
    if (MI.getOperand(1).getReg() == Reg)
      return true;
  }

  return false;
}

// Return true if any CC result of MI would (perhaps after conversion)
// reflect the value of Reg.
static bool resultTests(MachineInstr &MI, unsigned Reg) {
  if (MI.getNumOperands() > 0 && MI.getOperand(0).isReg() &&
      MI.getOperand(0).isDef() && MI.getOperand(0).getReg() == Reg)
    return true;

  return (preservesValueOf(MI, Reg));
}

// Describe the references to Reg or any of its aliases in MI.
Reference SystemZElimCompare::getRegReferences(MachineInstr &MI, unsigned Reg) {
  Reference Ref;
  if (MI.isDebugInstr())
    return Ref;

  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg()) {
      if (Register MOReg = MO.getReg()) {
        if (TRI->regsOverlap(MOReg, Reg)) {
          if (MO.isUse())
            Ref.Use = true;
          else if (MO.isDef())
            Ref.Def = true;
        }
      }
    }
  }
  return Ref;
}

// Return true if this is a load and test which can be optimized the
// same way as compare instruction.
static bool isLoadAndTestAsCmp(MachineInstr &MI) {
  // If we during isel used a load-and-test as a compare with 0, the
  // def operand is dead.
  return (MI.getOpcode() == SystemZ::LTEBR ||
          MI.getOpcode() == SystemZ::LTDBR ||
          MI.getOpcode() == SystemZ::LTXBR) &&
         MI.getOperand(0).isDead();
}

// Return the source register of Compare, which is the unknown value
// being tested.
static unsigned getCompareSourceReg(MachineInstr &Compare) {
  unsigned reg = 0;
  if (Compare.isCompare())
    reg = Compare.getOperand(0).getReg();
  else if (isLoadAndTestAsCmp(Compare))
    reg = Compare.getOperand(1).getReg();
  assert(reg);

  return reg;
}

// Compare compares the result of MI against zero.  If MI is an addition
// of -1 and if CCUsers is a single branch on nonzero, eliminate the addition
// and convert the branch to a BRCT(G) or BRCTH.  Return true on success.
bool SystemZElimCompare::convertToBRCT(
    MachineInstr &MI, MachineInstr &Compare,
    SmallVectorImpl<MachineInstr *> &CCUsers) {
  // Check whether we have an addition of -1.
  unsigned Opcode = MI.getOpcode();
  unsigned BRCT;
  if (Opcode == SystemZ::AHI)
    BRCT = SystemZ::BRCT;
  else if (Opcode == SystemZ::AGHI)
    BRCT = SystemZ::BRCTG;
  else if (Opcode == SystemZ::AIH)
    BRCT = SystemZ::BRCTH;
  else
    return false;
  if (MI.getOperand(2).getImm() != -1)
    return false;

  // Check whether we have a single JLH.
  if (CCUsers.size() != 1)
    return false;
  MachineInstr *Branch = CCUsers[0];
  if (Branch->getOpcode() != SystemZ::BRC ||
      Branch->getOperand(0).getImm() != SystemZ::CCMASK_ICMP ||
      Branch->getOperand(1).getImm() != SystemZ::CCMASK_CMP_NE)
    return false;

  // We already know that there are no references to the register between
  // MI and Compare.  Make sure that there are also no references between
  // Compare and Branch.
  unsigned SrcReg = getCompareSourceReg(Compare);
  MachineBasicBlock::iterator MBBI = Compare, MBBE = Branch;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (getRegReferences(*MBBI, SrcReg))
      return false;

  // The transformation is OK.  Rebuild Branch as a BRCT(G) or BRCTH.
  MachineOperand Target(Branch->getOperand(2));
  while (Branch->getNumOperands())
    Branch->removeOperand(0);
  Branch->setDesc(TII->get(BRCT));
  MachineInstrBuilder MIB(*Branch->getParent()->getParent(), Branch);
  MIB.add(MI.getOperand(0)).add(MI.getOperand(1)).add(Target);
  // Add a CC def to BRCT(G), since we may have to split them again if the
  // branch displacement overflows.  BRCTH has a 32-bit displacement, so
  // this is not necessary there.
  if (BRCT != SystemZ::BRCTH)
    MIB.addReg(SystemZ::CC, RegState::ImplicitDefine | RegState::Dead);
  MI.eraseFromParent();
  return true;
}

// Compare compares the result of MI against zero.  If MI is a suitable load
// instruction and if CCUsers is a single conditional trap on zero, eliminate
// the load and convert the branch to a load-and-trap.  Return true on success.
bool SystemZElimCompare::convertToLoadAndTrap(
    MachineInstr &MI, MachineInstr &Compare,
    SmallVectorImpl<MachineInstr *> &CCUsers) {
  unsigned LATOpcode = TII->getLoadAndTrap(MI.getOpcode());
  if (!LATOpcode)
    return false;

  // Check whether we have a single CondTrap that traps on zero.
  if (CCUsers.size() != 1)
    return false;
  MachineInstr *Branch = CCUsers[0];
  if (Branch->getOpcode() != SystemZ::CondTrap ||
      Branch->getOperand(0).getImm() != SystemZ::CCMASK_ICMP ||
      Branch->getOperand(1).getImm() != SystemZ::CCMASK_CMP_EQ)
    return false;

  // We already know that there are no references to the register between
  // MI and Compare.  Make sure that there are also no references between
  // Compare and Branch.
  unsigned SrcReg = getCompareSourceReg(Compare);
  MachineBasicBlock::iterator MBBI = Compare, MBBE = Branch;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (getRegReferences(*MBBI, SrcReg))
      return false;

  // The transformation is OK.  Rebuild Branch as a load-and-trap.
  while (Branch->getNumOperands())
    Branch->removeOperand(0);
  Branch->setDesc(TII->get(LATOpcode));
  MachineInstrBuilder(*Branch->getParent()->getParent(), Branch)
      .add(MI.getOperand(0))
      .add(MI.getOperand(1))
      .add(MI.getOperand(2))
      .add(MI.getOperand(3));
  MI.eraseFromParent();
  return true;
}

// If MI is a load instruction, try to convert it into a LOAD AND TEST.
// Return true on success.
bool SystemZElimCompare::convertToLoadAndTest(
    MachineInstr &MI, MachineInstr &Compare,
    SmallVectorImpl<MachineInstr *> &CCUsers) {

  // Try to adjust CC masks for the LOAD AND TEST opcode that could replace MI.
  unsigned Opcode = TII->getLoadAndTest(MI.getOpcode());
  if (!Opcode || !adjustCCMasksForInstr(MI, Compare, CCUsers, Opcode))
    return false;

  // Rebuild to get the CC operand in the right place.
  auto MIB = BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), TII->get(Opcode));
  for (const auto &MO : MI.operands())
    MIB.add(MO);
  MIB.setMemRefs(MI.memoperands());
  MI.eraseFromParent();

  // Mark instruction as not raising an FP exception if applicable.  We already
  // verified earlier that this move is valid.
  if (!Compare.mayRaiseFPException())
    MIB.setMIFlag(MachineInstr::MIFlag::NoFPExcept);

  return true;
}

// See if MI is an instruction with an equivalent "logical" opcode that can
// be used and replace MI. This is useful for EQ/NE comparisons where the
// "nsw" flag is missing since the "logical" opcode always sets CC to reflect
// the result being zero or non-zero.
bool SystemZElimCompare::convertToLogical(
    MachineInstr &MI, MachineInstr &Compare,
    SmallVectorImpl<MachineInstr *> &CCUsers) {

  unsigned ConvOpc = 0;
  switch (MI.getOpcode()) {
  case SystemZ::AR:   ConvOpc = SystemZ::ALR;   break;
  case SystemZ::ARK:  ConvOpc = SystemZ::ALRK;  break;
  case SystemZ::AGR:  ConvOpc = SystemZ::ALGR;  break;
  case SystemZ::AGRK: ConvOpc = SystemZ::ALGRK; break;
  case SystemZ::A:    ConvOpc = SystemZ::AL;    break;
  case SystemZ::AY:   ConvOpc = SystemZ::ALY;   break;
  case SystemZ::AG:   ConvOpc = SystemZ::ALG;   break;
  default: break;
  }
  if (!ConvOpc || !adjustCCMasksForInstr(MI, Compare, CCUsers, ConvOpc))
    return false;

  // Operands should be identical, so just change the opcode and remove the
  // dead flag on CC.
  MI.setDesc(TII->get(ConvOpc));
  MI.clearRegisterDeads(SystemZ::CC);
  return true;
}

#ifndef NDEBUG
static bool isAddWithImmediate(unsigned Opcode) {
  switch(Opcode) {
  case SystemZ::AHI:
  case SystemZ::AHIK:
  case SystemZ::AGHI:
  case SystemZ::AGHIK:
  case SystemZ::AFI:
  case SystemZ::AIH:
  case SystemZ::AGFI:
    return true;
  default: break;
  }
  return false;
}
#endif

// The CC users in CCUsers are testing the result of a comparison of some
// value X against zero and we know that any CC value produced by MI would
// also reflect the value of X.  ConvOpc may be used to pass the transfomed
// opcode MI will have if this succeeds.  Try to adjust CCUsers so that they
// test the result of MI directly, returning true on success.  Leave
// everything unchanged on failure.
bool SystemZElimCompare::adjustCCMasksForInstr(
    MachineInstr &MI, MachineInstr &Compare,
    SmallVectorImpl<MachineInstr *> &CCUsers,
    unsigned ConvOpc) {
  unsigned CompareFlags = Compare.getDesc().TSFlags;
  unsigned CompareCCValues = SystemZII::getCCValues(CompareFlags);
  int Opcode = (ConvOpc ? ConvOpc : MI.getOpcode());
  const MCInstrDesc &Desc = TII->get(Opcode);
  unsigned MIFlags = Desc.TSFlags;

  // If Compare may raise an FP exception, we can only eliminate it
  // if MI itself would have already raised the exception.
  if (Compare.mayRaiseFPException()) {
    // If the caller will change MI to use ConvOpc, only test whether
    // ConvOpc is suitable; it is on the caller to set the MI flag.
    if (ConvOpc && !Desc.mayRaiseFPException())
      return false;
    // If the caller will not change MI, we test the MI flag here.
    if (!ConvOpc && !MI.mayRaiseFPException())
      return false;
  }

  // See which compare-style condition codes are available.
  unsigned CCValues = SystemZII::getCCValues(MIFlags);
  unsigned ReusableCCMask = CCValues;
  // For unsigned comparisons with zero, only equality makes sense.
  if (CompareFlags & SystemZII::IsLogical)
    ReusableCCMask &= SystemZ::CCMASK_CMP_EQ;
  unsigned OFImplies = 0;
  bool LogicalMI = false;
  bool MIEquivalentToCmp = false;
  if (MI.getFlag(MachineInstr::NoSWrap) &&
      (MIFlags & SystemZII::CCIfNoSignedWrap)) {
    // If MI has the NSW flag set in combination with the
    // SystemZII::CCIfNoSignedWrap flag, all CCValues are valid.
  }
  else if ((MIFlags & SystemZII::CCIfNoSignedWrap) &&
           MI.getOperand(2).isImm()) {
    // Signed addition of immediate. If adding a positive immediate
    // overflows, the result must be less than zero. If adding a negative
    // immediate overflows, the result must be larger than zero (except in
    // the special case of adding the minimum value of the result range, in
    // which case we cannot predict whether the result is larger than or
    // equal to zero).
    assert(isAddWithImmediate(Opcode) && "Expected an add with immediate.");
    assert(!MI.mayLoadOrStore() && "Expected an immediate term.");
    int64_t RHS = MI.getOperand(2).getImm();
    if (SystemZ::GRX32BitRegClass.contains(MI.getOperand(0).getReg()) &&
        RHS == INT32_MIN)
      return false;
    OFImplies = (RHS > 0 ? SystemZ::CCMASK_CMP_LT : SystemZ::CCMASK_CMP_GT);
  }
  else if ((MIFlags & SystemZII::IsLogical) && CCValues) {
    // Use CCMASK_CMP_EQ to match with CCUsers. On success CCMask:s will be
    // converted to CCMASK_LOGICAL_ZERO or CCMASK_LOGICAL_NONZERO.
    LogicalMI = true;
    ReusableCCMask = SystemZ::CCMASK_CMP_EQ;
  }
  else {
    ReusableCCMask &= SystemZII::getCompareZeroCCMask(MIFlags);
    assert((ReusableCCMask & ~CCValues) == 0 && "Invalid CCValues");
    MIEquivalentToCmp =
      ReusableCCMask == CCValues && CCValues == CompareCCValues;
  }
  if (ReusableCCMask == 0)
    return false;

  if (!MIEquivalentToCmp) {
    // Now check whether these flags are enough for all users.
    SmallVector<MachineOperand *, 4> AlterMasks;
    for (MachineInstr *CCUserMI : CCUsers) {
      // Fail if this isn't a use of CC that we understand.
      unsigned Flags = CCUserMI->getDesc().TSFlags;
      unsigned FirstOpNum;
      if (Flags & SystemZII::CCMaskFirst)
        FirstOpNum = 0;
      else if (Flags & SystemZII::CCMaskLast)
        FirstOpNum = CCUserMI->getNumExplicitOperands() - 2;
      else
        return false;

      // Check whether the instruction predicate treats all CC values
      // outside of ReusableCCMask in the same way.  In that case it
      // doesn't matter what those CC values mean.
      unsigned CCValid = CCUserMI->getOperand(FirstOpNum).getImm();
      unsigned CCMask = CCUserMI->getOperand(FirstOpNum + 1).getImm();
      assert(CCValid == CompareCCValues && (CCMask & ~CCValid) == 0 &&
             "Corrupt CC operands of CCUser.");
      unsigned OutValid = ~ReusableCCMask & CCValid;
      unsigned OutMask = ~ReusableCCMask & CCMask;
      if (OutMask != 0 && OutMask != OutValid)
        return false;

      AlterMasks.push_back(&CCUserMI->getOperand(FirstOpNum));
      AlterMasks.push_back(&CCUserMI->getOperand(FirstOpNum + 1));
    }

    // All users are OK.  Adjust the masks for MI.
    for (unsigned I = 0, E = AlterMasks.size(); I != E; I += 2) {
      AlterMasks[I]->setImm(CCValues);
      unsigned CCMask = AlterMasks[I + 1]->getImm();
      if (LogicalMI) {
        // Translate the CCMask into its "logical" value.
        CCMask = (CCMask == SystemZ::CCMASK_CMP_EQ ?
                  SystemZ::CCMASK_LOGICAL_ZERO : SystemZ::CCMASK_LOGICAL_NONZERO);
        CCMask &= CCValues; // Logical subtracts never set CC=0.
      } else {
        if (CCMask & ~ReusableCCMask)
          CCMask = (CCMask & ReusableCCMask) | (CCValues & ~ReusableCCMask);
        CCMask |= (CCMask & OFImplies) ? SystemZ::CCMASK_ARITH_OVERFLOW : 0;
      }
      AlterMasks[I + 1]->setImm(CCMask);
    }
  }

  // CC is now live after MI.
  if (!ConvOpc)
    MI.clearRegisterDeads(SystemZ::CC);

  // Check if MI lies before Compare.
  bool BeforeCmp = false;
  MachineBasicBlock::iterator MBBI = MI, MBBE = MI.getParent()->end();
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (MBBI == Compare) {
      BeforeCmp = true;
      break;
    }

  // Clear any intervening kills of CC.
  if (BeforeCmp) {
    MachineBasicBlock::iterator MBBI = MI, MBBE = Compare;
    for (++MBBI; MBBI != MBBE; ++MBBI)
      MBBI->clearRegisterKills(SystemZ::CC, TRI);
  }

  return true;
}

// Return true if Compare is a comparison against zero.
static bool isCompareZero(MachineInstr &Compare) {
  if (isLoadAndTestAsCmp(Compare))
    return true;
  return Compare.getNumExplicitOperands() == 2 &&
    Compare.getOperand(1).isImm() && Compare.getOperand(1).getImm() == 0;
}

// Try to optimize cases where comparison instruction Compare is testing
// a value against zero.  Return true on success and if Compare should be
// deleted as dead.  CCUsers is the list of instructions that use the CC
// value produced by Compare.
bool SystemZElimCompare::optimizeCompareZero(
    MachineInstr &Compare, SmallVectorImpl<MachineInstr *> &CCUsers) {
  if (!isCompareZero(Compare))
    return false;

  // Search back for CC results that are based on the first operand.
  unsigned SrcReg = getCompareSourceReg(Compare);
  MachineBasicBlock &MBB = *Compare.getParent();
  Reference CCRefs;
  Reference SrcRefs;
  for (MachineBasicBlock::reverse_iterator MBBI =
         std::next(MachineBasicBlock::reverse_iterator(&Compare)),
         MBBE = MBB.rend(); MBBI != MBBE;) {
    MachineInstr &MI = *MBBI++;
    if (resultTests(MI, SrcReg)) {
      // Try to remove both MI and Compare by converting a branch to BRCT(G).
      // or a load-and-trap instruction.  We don't care in this case whether
      // CC is modified between MI and Compare.
      if (!CCRefs.Use && !SrcRefs) {
        if (convertToBRCT(MI, Compare, CCUsers)) {
          BranchOnCounts += 1;
          return true;
        }
        if (convertToLoadAndTrap(MI, Compare, CCUsers)) {
          LoadAndTraps += 1;
          return true;
        }
      }
      // Try to eliminate Compare by reusing a CC result from MI.
      if ((!CCRefs && convertToLoadAndTest(MI, Compare, CCUsers)) ||
          (!CCRefs.Def &&
           (adjustCCMasksForInstr(MI, Compare, CCUsers) ||
            convertToLogical(MI, Compare, CCUsers)))) {
        EliminatedComparisons += 1;
        return true;
      }
    }
    SrcRefs |= getRegReferences(MI, SrcReg);
    if (SrcRefs.Def)
      break;
    CCRefs |= getRegReferences(MI, SystemZ::CC);
    if (CCRefs.Use && CCRefs.Def)
      break;
    // Eliminating a Compare that may raise an FP exception will move
    // raising the exception to some earlier MI.  We cannot do this if
    // there is anything in between that might change exception flags.
    if (Compare.mayRaiseFPException() &&
        (MI.isCall() || MI.hasUnmodeledSideEffects()))
      break;
  }

  // Also do a forward search to handle cases where an instruction after the
  // compare can be converted, like
  // CGHI %r0d, 0; %r1d = LGR %r0d  =>  LTGR %r1d, %r0d
  auto MIRange = llvm::make_range(
      std::next(MachineBasicBlock::iterator(&Compare)), MBB.end());
  for (MachineInstr &MI : llvm::make_early_inc_range(MIRange)) {
    if (preservesValueOf(MI, SrcReg)) {
      // Try to eliminate Compare by reusing a CC result from MI.
      if (convertToLoadAndTest(MI, Compare, CCUsers)) {
        EliminatedComparisons += 1;
        return true;
      }
    }
    if (getRegReferences(MI, SrcReg).Def)
      return false;
    if (getRegReferences(MI, SystemZ::CC))
      return false;
  }

  return false;
}

// Try to fuse comparison instruction Compare into a later branch.
// Return true on success and if Compare is therefore redundant.
bool SystemZElimCompare::fuseCompareOperations(
    MachineInstr &Compare, SmallVectorImpl<MachineInstr *> &CCUsers) {
  // See whether we have a single branch with which to fuse.
  if (CCUsers.size() != 1)
    return false;
  MachineInstr *Branch = CCUsers[0];
  SystemZII::FusedCompareType Type;
  switch (Branch->getOpcode()) {
  case SystemZ::BRC:
    Type = SystemZII::CompareAndBranch;
    break;
  case SystemZ::CondReturn:
    Type = SystemZII::CompareAndReturn;
    break;
  case SystemZ::CallBCR:
    Type = SystemZII::CompareAndSibcall;
    break;
  case SystemZ::CondTrap:
    Type = SystemZII::CompareAndTrap;
    break;
  default:
    return false;
  }

  // See whether we have a comparison that can be fused.
  unsigned FusedOpcode =
      TII->getFusedCompare(Compare.getOpcode(), Type, &Compare);
  if (!FusedOpcode)
    return false;

  // Make sure that the operands are available at the branch.
  // SrcReg2 is the register if the source operand is a register,
  // 0 if the source operand is immediate, and the base register
  // if the source operand is memory (index is not supported).
  Register SrcReg = Compare.getOperand(0).getReg();
  Register SrcReg2 =
    Compare.getOperand(1).isReg() ? Compare.getOperand(1).getReg() : Register();
  MachineBasicBlock::iterator MBBI = Compare, MBBE = Branch;
  for (++MBBI; MBBI != MBBE; ++MBBI)
    if (MBBI->modifiesRegister(SrcReg, TRI) ||
        (SrcReg2 && MBBI->modifiesRegister(SrcReg2, TRI)))
      return false;

  // Read the branch mask, target (if applicable), regmask (if applicable).
  MachineOperand CCMask(MBBI->getOperand(1));
  assert((CCMask.getImm() & ~SystemZ::CCMASK_ICMP) == 0 &&
         "Invalid condition-code mask for integer comparison");
  // This is only valid for CompareAndBranch and CompareAndSibcall.
  MachineOperand Target(MBBI->getOperand(
    (Type == SystemZII::CompareAndBranch ||
     Type == SystemZII::CompareAndSibcall) ? 2 : 0));
  const uint32_t *RegMask;
  if (Type == SystemZII::CompareAndSibcall)
    RegMask = MBBI->getOperand(3).getRegMask();

  // Clear out all current operands.
  int CCUse = MBBI->findRegisterUseOperandIdx(SystemZ::CC, TRI, false);
  assert(CCUse >= 0 && "BRC/BCR must use CC");
  Branch->removeOperand(CCUse);
  // Remove regmask (sibcall).
  if (Type == SystemZII::CompareAndSibcall)
    Branch->removeOperand(3);
  // Remove target (branch or sibcall).
  if (Type == SystemZII::CompareAndBranch ||
      Type == SystemZII::CompareAndSibcall)
    Branch->removeOperand(2);
  Branch->removeOperand(1);
  Branch->removeOperand(0);

  // Rebuild Branch as a fused compare and branch.
  // SrcNOps is the number of MI operands of the compare instruction
  // that we need to copy over.
  unsigned SrcNOps = 2;
  if (FusedOpcode == SystemZ::CLT || FusedOpcode == SystemZ::CLGT)
    SrcNOps = 3;
  Branch->setDesc(TII->get(FusedOpcode));
  MachineInstrBuilder MIB(*Branch->getParent()->getParent(), Branch);
  for (unsigned I = 0; I < SrcNOps; I++)
    MIB.add(Compare.getOperand(I));
  MIB.add(CCMask);

  if (Type == SystemZII::CompareAndBranch) {
    // Only conditional branches define CC, as they may be converted back
    // to a non-fused branch because of a long displacement.  Conditional
    // returns don't have that problem.
    MIB.add(Target).addReg(SystemZ::CC,
                           RegState::ImplicitDefine | RegState::Dead);
  }

  if (Type == SystemZII::CompareAndSibcall) {
    MIB.add(Target);
    MIB.addRegMask(RegMask);
  }

  // Clear any intervening kills of SrcReg and SrcReg2.
  MBBI = Compare;
  for (++MBBI; MBBI != MBBE; ++MBBI) {
    MBBI->clearRegisterKills(SrcReg, TRI);
    if (SrcReg2)
      MBBI->clearRegisterKills(SrcReg2, TRI);
  }
  FusedComparisons += 1;
  return true;
}

// Process all comparison instructions in MBB.  Return true if something
// changed.
bool SystemZElimCompare::processBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  // Walk backwards through the block looking for comparisons, recording
  // all CC users as we go.  The subroutines can delete Compare and
  // instructions before it.
  LiveRegUnits LiveRegs(*TRI);
  LiveRegs.addLiveOuts(MBB);
  bool CompleteCCUsers = LiveRegs.available(SystemZ::CC);
  SmallVector<MachineInstr *, 4> CCUsers;
  MachineBasicBlock::iterator MBBI = MBB.end();
  while (MBBI != MBB.begin()) {
    MachineInstr &MI = *--MBBI;
    if (CompleteCCUsers && (MI.isCompare() || isLoadAndTestAsCmp(MI)) &&
        (optimizeCompareZero(MI, CCUsers) ||
         fuseCompareOperations(MI, CCUsers))) {
      ++MBBI;
      MI.eraseFromParent();
      Changed = true;
      CCUsers.clear();
      continue;
    }

    if (MI.definesRegister(SystemZ::CC, /*TRI=*/nullptr)) {
      CCUsers.clear();
      CompleteCCUsers = true;
    }
    if (MI.readsRegister(SystemZ::CC, /*TRI=*/nullptr) && CompleteCCUsers)
      CCUsers.push_back(&MI);
  }
  return Changed;
}

bool SystemZElimCompare::runOnMachineFunction(MachineFunction &F) {
  if (skipFunction(F.getFunction()))
    return false;

  TII = F.getSubtarget<SystemZSubtarget>().getInstrInfo();
  TRI = &TII->getRegisterInfo();

  bool Changed = false;
  for (auto &MBB : F)
    Changed |= processBlock(MBB);

  return Changed;
}

FunctionPass *llvm::createSystemZElimComparePass(SystemZTargetMachine &TM) {
  return new SystemZElimCompare();
}
