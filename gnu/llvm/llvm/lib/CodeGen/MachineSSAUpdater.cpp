//===- MachineSSAUpdater.cpp - Unstructured SSA Update Tool ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MachineSSAUpdater class. It's based on SSAUpdater
// class in lib/Transforms/Utils.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineSSAUpdater.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/SSAUpdaterImpl.h"
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "machine-ssaupdater"

using AvailableValsTy = DenseMap<MachineBasicBlock *, Register>;

static AvailableValsTy &getAvailableVals(void *AV) {
  return *static_cast<AvailableValsTy*>(AV);
}

MachineSSAUpdater::MachineSSAUpdater(MachineFunction &MF,
                                     SmallVectorImpl<MachineInstr*> *NewPHI)
  : InsertedPHIs(NewPHI), TII(MF.getSubtarget().getInstrInfo()),
    MRI(&MF.getRegInfo()) {}

MachineSSAUpdater::~MachineSSAUpdater() {
  delete static_cast<AvailableValsTy*>(AV);
}

/// Initialize - Reset this object to get ready for a new set of SSA
/// updates.
void MachineSSAUpdater::Initialize(Register V) {
  if (!AV)
    AV = new AvailableValsTy();
  else
    getAvailableVals(AV).clear();

  RegAttrs = MRI->getVRegAttrs(V);
}

/// HasValueForBlock - Return true if the MachineSSAUpdater already has a value for
/// the specified block.
bool MachineSSAUpdater::HasValueForBlock(MachineBasicBlock *BB) const {
  return getAvailableVals(AV).count(BB);
}

/// AddAvailableValue - Indicate that a rewritten value is available in the
/// specified block with the specified value.
void MachineSSAUpdater::AddAvailableValue(MachineBasicBlock *BB, Register V) {
  getAvailableVals(AV)[BB] = V;
}

/// GetValueAtEndOfBlock - Construct SSA form, materializing a value that is
/// live at the end of the specified block.
Register MachineSSAUpdater::GetValueAtEndOfBlock(MachineBasicBlock *BB) {
  return GetValueAtEndOfBlockInternal(BB);
}

static
Register LookForIdenticalPHI(MachineBasicBlock *BB,
        SmallVectorImpl<std::pair<MachineBasicBlock *, Register>> &PredValues) {
  if (BB->empty())
    return Register();

  MachineBasicBlock::iterator I = BB->begin();
  if (!I->isPHI())
    return Register();

  AvailableValsTy AVals;
  for (const auto &[SrcBB, SrcReg] : PredValues)
    AVals[SrcBB] = SrcReg;
  while (I != BB->end() && I->isPHI()) {
    bool Same = true;
    for (unsigned i = 1, e = I->getNumOperands(); i != e; i += 2) {
      Register SrcReg = I->getOperand(i).getReg();
      MachineBasicBlock *SrcBB = I->getOperand(i+1).getMBB();
      if (AVals[SrcBB] != SrcReg) {
        Same = false;
        break;
      }
    }
    if (Same)
      return I->getOperand(0).getReg();
    ++I;
  }
  return Register();
}

/// InsertNewDef - Insert an empty PHI or IMPLICIT_DEF instruction which define
/// a value of the given register class at the start of the specified basic
/// block. It returns the virtual register defined by the instruction.
static MachineInstrBuilder InsertNewDef(unsigned Opcode, MachineBasicBlock *BB,
                                        MachineBasicBlock::iterator I,
                                        MachineRegisterInfo::VRegAttrs RegAttrs,
                                        MachineRegisterInfo *MRI,
                                        const TargetInstrInfo *TII) {
  Register NewVR = MRI->createVirtualRegister(RegAttrs);
  return BuildMI(*BB, I, DebugLoc(), TII->get(Opcode), NewVR);
}

/// GetValueInMiddleOfBlock - Construct SSA form, materializing a value that
/// is live in the middle of the specified block. If ExistingValueOnly is
/// true then this will only return an existing value or $noreg; otherwise new
/// instructions may be inserted to materialize a value.
///
/// GetValueInMiddleOfBlock is the same as GetValueAtEndOfBlock except in one
/// important case: if there is a definition of the rewritten value after the
/// 'use' in BB.  Consider code like this:
///
///      X1 = ...
///   SomeBB:
///      use(X)
///      X2 = ...
///      br Cond, SomeBB, OutBB
///
/// In this case, there are two values (X1 and X2) added to the AvailableVals
/// set by the client of the rewriter, and those values are both live out of
/// their respective blocks.  However, the use of X happens in the *middle* of
/// a block.  Because of this, we need to insert a new PHI node in SomeBB to
/// merge the appropriate values, and this value isn't live out of the block.
Register MachineSSAUpdater::GetValueInMiddleOfBlock(MachineBasicBlock *BB,
                                                    bool ExistingValueOnly) {
  // If there is no definition of the renamed variable in this block, just use
  // GetValueAtEndOfBlock to do our work.
  if (!HasValueForBlock(BB))
    return GetValueAtEndOfBlockInternal(BB, ExistingValueOnly);

  // If there are no predecessors, just return undef.
  if (BB->pred_empty()) {
    // If we cannot insert new instructions, just return $noreg.
    if (ExistingValueOnly)
      return Register();
    // Insert an implicit_def to represent an undef value.
    MachineInstr *NewDef =
        InsertNewDef(TargetOpcode::IMPLICIT_DEF, BB, BB->getFirstTerminator(),
                     RegAttrs, MRI, TII);
    return NewDef->getOperand(0).getReg();
  }

  // Otherwise, we have the hard case.  Get the live-in values for each
  // predecessor.
  SmallVector<std::pair<MachineBasicBlock*, Register>, 8> PredValues;
  Register SingularValue;

  bool isFirstPred = true;
  for (MachineBasicBlock *PredBB : BB->predecessors()) {
    Register PredVal = GetValueAtEndOfBlockInternal(PredBB, ExistingValueOnly);
    PredValues.push_back(std::make_pair(PredBB, PredVal));

    // Compute SingularValue.
    if (isFirstPred) {
      SingularValue = PredVal;
      isFirstPred = false;
    } else if (PredVal != SingularValue)
      SingularValue = Register();
  }

  // Otherwise, if all the merged values are the same, just use it.
  if (SingularValue)
    return SingularValue;

  // If an identical PHI is already in BB, just reuse it.
  Register DupPHI = LookForIdenticalPHI(BB, PredValues);
  if (DupPHI)
    return DupPHI;

  // If we cannot create new instructions, return $noreg now.
  if (ExistingValueOnly)
    return Register();

  // Otherwise, we do need a PHI: insert one now.
  MachineBasicBlock::iterator Loc = BB->empty() ? BB->end() : BB->begin();
  MachineInstrBuilder InsertedPHI =
      InsertNewDef(TargetOpcode::PHI, BB, Loc, RegAttrs, MRI, TII);

  // Fill in all the predecessors of the PHI.
  for (const auto &[SrcBB, SrcReg] : PredValues)
    InsertedPHI.addReg(SrcReg).addMBB(SrcBB);

  // See if the PHI node can be merged to a single value.  This can happen in
  // loop cases when we get a PHI of itself and one other value.
  if (unsigned ConstVal = InsertedPHI->isConstantValuePHI()) {
    InsertedPHI->eraseFromParent();
    return ConstVal;
  }

  // If the client wants to know about all new instructions, tell it.
  if (InsertedPHIs) InsertedPHIs->push_back(InsertedPHI);

  LLVM_DEBUG(dbgs() << "  Inserted PHI: " << *InsertedPHI);
  return InsertedPHI.getReg(0);
}

static
MachineBasicBlock *findCorrespondingPred(const MachineInstr *MI,
                                         MachineOperand *U) {
  for (unsigned i = 1, e = MI->getNumOperands(); i != e; i += 2) {
    if (&MI->getOperand(i) == U)
      return MI->getOperand(i+1).getMBB();
  }

  llvm_unreachable("MachineOperand::getParent() failure?");
}

/// RewriteUse - Rewrite a use of the symbolic value.  This handles PHI nodes,
/// which use their value in the corresponding predecessor.
void MachineSSAUpdater::RewriteUse(MachineOperand &U) {
  MachineInstr *UseMI = U.getParent();
  Register NewVR;
  if (UseMI->isPHI()) {
    MachineBasicBlock *SourceBB = findCorrespondingPred(UseMI, &U);
    NewVR = GetValueAtEndOfBlockInternal(SourceBB);
  } else {
    NewVR = GetValueInMiddleOfBlock(UseMI->getParent());
  }

  // Insert a COPY if needed to satisfy register class constraints for the using
  // MO. Or, if possible, just constrain the class for NewVR to avoid the need
  // for a COPY.
  if (NewVR) {
    const TargetRegisterClass *UseRC =
        dyn_cast_or_null<const TargetRegisterClass *>(RegAttrs.RCOrRB);
    if (UseRC && !MRI->constrainRegClass(NewVR, UseRC)) {
      MachineBasicBlock *UseBB = UseMI->getParent();
      MachineInstr *InsertedCopy =
          InsertNewDef(TargetOpcode::COPY, UseBB, UseBB->getFirstNonPHI(),
                       RegAttrs, MRI, TII)
              .addReg(NewVR);
      NewVR = InsertedCopy->getOperand(0).getReg();
      LLVM_DEBUG(dbgs() << "  Inserted COPY: " << *InsertedCopy);
    }
  }
  U.setReg(NewVR);
}

namespace llvm {

/// SSAUpdaterTraits<MachineSSAUpdater> - Traits for the SSAUpdaterImpl
/// template, specialized for MachineSSAUpdater.
template<>
class SSAUpdaterTraits<MachineSSAUpdater> {
public:
  using BlkT = MachineBasicBlock;
  using ValT = Register;
  using PhiT = MachineInstr;
  using BlkSucc_iterator = MachineBasicBlock::succ_iterator;

  static BlkSucc_iterator BlkSucc_begin(BlkT *BB) { return BB->succ_begin(); }
  static BlkSucc_iterator BlkSucc_end(BlkT *BB) { return BB->succ_end(); }

  /// Iterator for PHI operands.
  class PHI_iterator {
  private:
    MachineInstr *PHI;
    unsigned idx;

  public:
    explicit PHI_iterator(MachineInstr *P) // begin iterator
      : PHI(P), idx(1) {}
    PHI_iterator(MachineInstr *P, bool) // end iterator
      : PHI(P), idx(PHI->getNumOperands()) {}

    PHI_iterator &operator++() { idx += 2; return *this; }
    bool operator==(const PHI_iterator& x) const { return idx == x.idx; }
    bool operator!=(const PHI_iterator& x) const { return !operator==(x); }

    unsigned getIncomingValue() { return PHI->getOperand(idx).getReg(); }

    MachineBasicBlock *getIncomingBlock() {
      return PHI->getOperand(idx+1).getMBB();
    }
  };

  static inline PHI_iterator PHI_begin(PhiT *PHI) { return PHI_iterator(PHI); }

  static inline PHI_iterator PHI_end(PhiT *PHI) {
    return PHI_iterator(PHI, true);
  }

  /// FindPredecessorBlocks - Put the predecessors of BB into the Preds
  /// vector.
  static void FindPredecessorBlocks(MachineBasicBlock *BB,
                                    SmallVectorImpl<MachineBasicBlock*> *Preds){
    append_range(*Preds, BB->predecessors());
  }

  /// GetPoisonVal - Create an IMPLICIT_DEF instruction with a new register.
  /// Add it into the specified block and return the register.
  static Register GetPoisonVal(MachineBasicBlock *BB,
                              MachineSSAUpdater *Updater) {
    // Insert an implicit_def to represent a poison value.
    MachineInstr *NewDef =
        InsertNewDef(TargetOpcode::IMPLICIT_DEF, BB, BB->getFirstNonPHI(),
                     Updater->RegAttrs, Updater->MRI, Updater->TII);
    return NewDef->getOperand(0).getReg();
  }

  /// CreateEmptyPHI - Create a PHI instruction that defines a new register.
  /// Add it into the specified block and return the register.
  static Register CreateEmptyPHI(MachineBasicBlock *BB, unsigned NumPreds,
                                 MachineSSAUpdater *Updater) {
    MachineBasicBlock::iterator Loc = BB->empty() ? BB->end() : BB->begin();
    MachineInstr *PHI =
        InsertNewDef(TargetOpcode::PHI, BB, Loc, Updater->RegAttrs,
                     Updater->MRI, Updater->TII);
    return PHI->getOperand(0).getReg();
  }

  /// AddPHIOperand - Add the specified value as an operand of the PHI for
  /// the specified predecessor block.
  static void AddPHIOperand(MachineInstr *PHI, Register Val,
                            MachineBasicBlock *Pred) {
    MachineInstrBuilder(*Pred->getParent(), PHI).addReg(Val).addMBB(Pred);
  }

  /// InstrIsPHI - Check if an instruction is a PHI.
  static MachineInstr *InstrIsPHI(MachineInstr *I) {
    if (I && I->isPHI())
      return I;
    return nullptr;
  }

  /// ValueIsPHI - Check if the instruction that defines the specified register
  /// is a PHI instruction.
  static MachineInstr *ValueIsPHI(Register Val, MachineSSAUpdater *Updater) {
    return InstrIsPHI(Updater->MRI->getVRegDef(Val));
  }

  /// ValueIsNewPHI - Like ValueIsPHI but also check if the PHI has no source
  /// operands, i.e., it was just added.
  static MachineInstr *ValueIsNewPHI(Register Val, MachineSSAUpdater *Updater) {
    MachineInstr *PHI = ValueIsPHI(Val, Updater);
    if (PHI && PHI->getNumOperands() <= 1)
      return PHI;
    return nullptr;
  }

  /// GetPHIValue - For the specified PHI instruction, return the register
  /// that it defines.
  static Register GetPHIValue(MachineInstr *PHI) {
    return PHI->getOperand(0).getReg();
  }
};

} // end namespace llvm

/// GetValueAtEndOfBlockInternal - Check to see if AvailableVals has an entry
/// for the specified BB and if so, return it.  If not, construct SSA form by
/// first calculating the required placement of PHIs and then inserting new
/// PHIs where needed.
Register
MachineSSAUpdater::GetValueAtEndOfBlockInternal(MachineBasicBlock *BB,
                                                bool ExistingValueOnly) {
  AvailableValsTy &AvailableVals = getAvailableVals(AV);
  Register ExistingVal = AvailableVals.lookup(BB);
  if (ExistingVal || ExistingValueOnly)
    return ExistingVal;

  SSAUpdaterImpl<MachineSSAUpdater> Impl(this, &AvailableVals, InsertedPHIs);
  return Impl.GetValue(BB);
}
