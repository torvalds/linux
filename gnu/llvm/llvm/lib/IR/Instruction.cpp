//===-- Instruction.cpp - Implement the Instruction class -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Instruction class for the IR library.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Instruction.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
using namespace llvm;

InsertPosition::InsertPosition(Instruction *InsertBefore)
    : InsertAt(InsertBefore ? InsertBefore->getIterator()
                            : InstListType::iterator()) {}
InsertPosition::InsertPosition(BasicBlock *InsertAtEnd)
    : InsertAt(InsertAtEnd ? InsertAtEnd->end() : InstListType::iterator()) {}

Instruction::Instruction(Type *ty, unsigned it, Use *Ops, unsigned NumOps,
                         InsertPosition InsertBefore)
    : User(ty, Value::InstructionVal + it, Ops, NumOps) {
  // When called with an iterator, there must be a block to insert into.
  if (InstListType::iterator InsertIt = InsertBefore; InsertIt.isValid()) {
    BasicBlock *BB = InsertIt.getNodeParent();
    assert(BB && "Instruction to insert before is not in a basic block!");
    insertInto(BB, InsertBefore);
  }
}

Instruction::~Instruction() {
  assert(!getParent() && "Instruction still linked in the program!");

  // Replace any extant metadata uses of this instruction with undef to
  // preserve debug info accuracy. Some alternatives include:
  // - Treat Instruction like any other Value, and point its extant metadata
  //   uses to an empty ValueAsMetadata node. This makes extant dbg.value uses
  //   trivially dead (i.e. fair game for deletion in many passes), leading to
  //   stale dbg.values being in effect for too long.
  // - Call salvageDebugInfoOrMarkUndef. Not needed to make instruction removal
  //   correct. OTOH results in wasted work in some common cases (e.g. when all
  //   instructions in a BasicBlock are deleted).
  if (isUsedByMetadata())
    ValueAsMetadata::handleRAUW(this, UndefValue::get(getType()));

  // Explicitly remove DIAssignID metadata to clear up ID -> Instruction(s)
  // mapping in LLVMContext.
  setMetadata(LLVMContext::MD_DIAssignID, nullptr);
}

const Module *Instruction::getModule() const {
  return getParent()->getModule();
}

const Function *Instruction::getFunction() const {
  return getParent()->getParent();
}

const DataLayout &Instruction::getDataLayout() const {
  return getModule()->getDataLayout();
}

void Instruction::removeFromParent() {
  // Perform any debug-info maintenence required.
  handleMarkerRemoval();

  getParent()->getInstList().remove(getIterator());
}

void Instruction::handleMarkerRemoval() {
  if (!getParent()->IsNewDbgInfoFormat || !DebugMarker)
    return;

  DebugMarker->removeMarker();
}

BasicBlock::iterator Instruction::eraseFromParent() {
  handleMarkerRemoval();
  return getParent()->getInstList().erase(getIterator());
}

void Instruction::insertBefore(Instruction *InsertPos) {
  insertBefore(InsertPos->getIterator());
}

/// Insert an unlinked instruction into a basic block immediately before the
/// specified instruction.
void Instruction::insertBefore(BasicBlock::iterator InsertPos) {
  insertBefore(*InsertPos->getParent(), InsertPos);
}

/// Insert an unlinked instruction into a basic block immediately after the
/// specified instruction.
void Instruction::insertAfter(Instruction *InsertPos) {
  BasicBlock *DestParent = InsertPos->getParent();

  DestParent->getInstList().insertAfter(InsertPos->getIterator(), this);
}

BasicBlock::iterator Instruction::insertInto(BasicBlock *ParentBB,
                                             BasicBlock::iterator It) {
  assert(getParent() == nullptr && "Expected detached instruction");
  assert((It == ParentBB->end() || It->getParent() == ParentBB) &&
         "It not in ParentBB");
  insertBefore(*ParentBB, It);
  return getIterator();
}

extern cl::opt<bool> UseNewDbgInfoFormat;

void Instruction::insertBefore(BasicBlock &BB,
                               InstListType::iterator InsertPos) {
  assert(!DebugMarker);

  BB.getInstList().insert(InsertPos, this);

  if (!BB.IsNewDbgInfoFormat)
    return;

  // We've inserted "this": if InsertAtHead is set then it comes before any
  // DbgVariableRecords attached to InsertPos. But if it's not set, then any
  // DbgRecords should now come before "this".
  bool InsertAtHead = InsertPos.getHeadBit();
  if (!InsertAtHead) {
    DbgMarker *SrcMarker = BB.getMarker(InsertPos);
    if (SrcMarker && !SrcMarker->empty()) {
      // If this assertion fires, the calling code is about to insert a PHI
      // after debug-records, which would form a sequence like:
      //     %0 = PHI
      //     #dbg_value
      //     %1 = PHI
      // Which is de-normalised and undesired -- hence the assertion. To avoid
      // this, you must insert at that position using an iterator, and it must
      // be aquired by calling getFirstNonPHIIt / begin or similar methods on
      // the block. This will signal to this behind-the-scenes debug-info
      // maintenence code that you intend the PHI to be ahead of everything,
      // including any debug-info.
      assert(!isa<PHINode>(this) && "Inserting PHI after debug-records!");
      adoptDbgRecords(&BB, InsertPos, false);
    }
  }

  // If we're inserting a terminator, check if we need to flush out
  // TrailingDbgRecords. Inserting instructions at the end of an incomplete
  // block is handled by the code block above.
  if (isTerminator())
    getParent()->flushTerminatorDbgRecords();
}

/// Unlink this instruction from its current basic block and insert it into the
/// basic block that MovePos lives in, right before MovePos.
void Instruction::moveBefore(Instruction *MovePos) {
  moveBeforeImpl(*MovePos->getParent(), MovePos->getIterator(), false);
}

void Instruction::moveBeforePreserving(Instruction *MovePos) {
  moveBeforeImpl(*MovePos->getParent(), MovePos->getIterator(), true);
}

void Instruction::moveAfter(Instruction *MovePos) {
  auto NextIt = std::next(MovePos->getIterator());
  // We want this instruction to be moved to before NextIt in the instruction
  // list, but before NextIt's debug value range.
  NextIt.setHeadBit(true);
  moveBeforeImpl(*MovePos->getParent(), NextIt, false);
}

void Instruction::moveAfterPreserving(Instruction *MovePos) {
  auto NextIt = std::next(MovePos->getIterator());
  // We want this instruction and its debug range to be moved to before NextIt
  // in the instruction list, but before NextIt's debug value range.
  NextIt.setHeadBit(true);
  moveBeforeImpl(*MovePos->getParent(), NextIt, true);
}

void Instruction::moveBefore(BasicBlock &BB, InstListType::iterator I) {
  moveBeforeImpl(BB, I, false);
}

void Instruction::moveBeforePreserving(BasicBlock &BB,
                                       InstListType::iterator I) {
  moveBeforeImpl(BB, I, true);
}

void Instruction::moveBeforeImpl(BasicBlock &BB, InstListType::iterator I,
                              bool Preserve) {
  assert(I == BB.end() || I->getParent() == &BB);
  bool InsertAtHead = I.getHeadBit();

  // If we've been given the "Preserve" flag, then just move the DbgRecords with
  // the instruction, no more special handling needed.
  if (BB.IsNewDbgInfoFormat && DebugMarker && !Preserve) {
    if (I != this->getIterator() || InsertAtHead) {
      // "this" is definitely moving in the list, or it's moving ahead of its
      // attached DbgVariableRecords. Detach any existing DbgRecords.
      handleMarkerRemoval();
    }
  }

  // Move this single instruction. Use the list splice method directly, not
  // the block splicer, which will do more debug-info things.
  BB.getInstList().splice(I, getParent()->getInstList(), getIterator());

  if (BB.IsNewDbgInfoFormat && !Preserve) {
    DbgMarker *NextMarker = getParent()->getNextMarker(this);

    // If we're inserting at point I, and not in front of the DbgRecords
    // attached there, then we should absorb the DbgRecords attached to I.
    if (!InsertAtHead && NextMarker && !NextMarker->empty()) {
      adoptDbgRecords(&BB, I, false);
    }
  }

  if (isTerminator())
    getParent()->flushTerminatorDbgRecords();
}

iterator_range<DbgRecord::self_iterator> Instruction::cloneDebugInfoFrom(
    const Instruction *From, std::optional<DbgRecord::self_iterator> FromHere,
    bool InsertAtHead) {
  if (!From->DebugMarker)
    return DbgMarker::getEmptyDbgRecordRange();

  assert(getParent()->IsNewDbgInfoFormat);
  assert(getParent()->IsNewDbgInfoFormat ==
         From->getParent()->IsNewDbgInfoFormat);

  if (!DebugMarker)
    getParent()->createMarker(this);

  return DebugMarker->cloneDebugInfoFrom(From->DebugMarker, FromHere,
                                         InsertAtHead);
}

std::optional<DbgRecord::self_iterator>
Instruction::getDbgReinsertionPosition() {
  // Is there a marker on the next instruction?
  DbgMarker *NextMarker = getParent()->getNextMarker(this);
  if (!NextMarker)
    return std::nullopt;

  // Are there any DbgRecords in the next marker?
  if (NextMarker->StoredDbgRecords.empty())
    return std::nullopt;

  return NextMarker->StoredDbgRecords.begin();
}

bool Instruction::hasDbgRecords() const { return !getDbgRecordRange().empty(); }

void Instruction::adoptDbgRecords(BasicBlock *BB, BasicBlock::iterator It,
                                  bool InsertAtHead) {
  DbgMarker *SrcMarker = BB->getMarker(It);
  auto ReleaseTrailingDbgRecords = [BB, It, SrcMarker]() {
    if (BB->end() == It) {
      SrcMarker->eraseFromParent();
      BB->deleteTrailingDbgRecords();
    }
  };

  if (!SrcMarker || SrcMarker->StoredDbgRecords.empty()) {
    ReleaseTrailingDbgRecords();
    return;
  }

  // If we have DbgMarkers attached to this instruction, we have to honour the
  // ordering of DbgRecords between this and the other marker. Fall back to just
  // absorbing from the source.
  if (DebugMarker || It == BB->end()) {
    // Ensure we _do_ have a marker.
    getParent()->createMarker(this);
    DebugMarker->absorbDebugValues(*SrcMarker, InsertAtHead);

    // Having transferred everything out of SrcMarker, we _could_ clean it up
    // and free the marker now. However, that's a lot of heap-accounting for a
    // small amount of memory with a good chance of re-use. Leave it for the
    // moment. It will be released when the Instruction is freed in the worst
    // case.
    // However: if we transferred from a trailing marker off the end of the
    // block, it's important to not leave the empty marker trailing. It will
    // give a misleading impression that some debug records have been left
    // trailing.
    ReleaseTrailingDbgRecords();
  } else {
    // Optimisation: we're transferring all the DbgRecords from the source
    // marker onto this empty location: just adopt the other instructions
    // marker.
    DebugMarker = SrcMarker;
    DebugMarker->MarkedInstr = this;
    It->DebugMarker = nullptr;
  }
}

void Instruction::dropDbgRecords() {
  if (DebugMarker)
    DebugMarker->dropDbgRecords();
}

void Instruction::dropOneDbgRecord(DbgRecord *DVR) {
  DebugMarker->dropOneDbgRecord(DVR);
}

bool Instruction::comesBefore(const Instruction *Other) const {
  assert(getParent() && Other->getParent() &&
         "instructions without BB parents have no order");
  assert(getParent() == Other->getParent() &&
         "cross-BB instruction order comparison");
  if (!getParent()->isInstrOrderValid())
    const_cast<BasicBlock *>(getParent())->renumberInstructions();
  return Order < Other->Order;
}

std::optional<BasicBlock::iterator> Instruction::getInsertionPointAfterDef() {
  assert(!getType()->isVoidTy() && "Instruction must define result");
  BasicBlock *InsertBB;
  BasicBlock::iterator InsertPt;
  if (auto *PN = dyn_cast<PHINode>(this)) {
    InsertBB = PN->getParent();
    InsertPt = InsertBB->getFirstInsertionPt();
  } else if (auto *II = dyn_cast<InvokeInst>(this)) {
    InsertBB = II->getNormalDest();
    InsertPt = InsertBB->getFirstInsertionPt();
  } else if (isa<CallBrInst>(this)) {
    // Def is available in multiple successors, there's no single dominating
    // insertion point.
    return std::nullopt;
  } else {
    assert(!isTerminator() && "Only invoke/callbr terminators return value");
    InsertBB = getParent();
    InsertPt = std::next(getIterator());
    // Any instruction inserted immediately after "this" will come before any
    // debug-info records take effect -- thus, set the head bit indicating that
    // to debug-info-transfer code.
    InsertPt.setHeadBit(true);
  }

  // catchswitch blocks don't have any legal insertion point (because they
  // are both an exception pad and a terminator).
  if (InsertPt == InsertBB->end())
    return std::nullopt;
  return InsertPt;
}

bool Instruction::isOnlyUserOfAnyOperand() {
  return any_of(operands(), [](Value *V) { return V->hasOneUser(); });
}

void Instruction::setHasNoUnsignedWrap(bool b) {
  if (auto *Inst = dyn_cast<OverflowingBinaryOperator>(this))
    Inst->setHasNoUnsignedWrap(b);
  else
    cast<TruncInst>(this)->setHasNoUnsignedWrap(b);
}

void Instruction::setHasNoSignedWrap(bool b) {
  if (auto *Inst = dyn_cast<OverflowingBinaryOperator>(this))
    Inst->setHasNoSignedWrap(b);
  else
    cast<TruncInst>(this)->setHasNoSignedWrap(b);
}

void Instruction::setIsExact(bool b) {
  cast<PossiblyExactOperator>(this)->setIsExact(b);
}

void Instruction::setNonNeg(bool b) {
  assert(isa<PossiblyNonNegInst>(this) && "Must be zext/uitofp");
  SubclassOptionalData = (SubclassOptionalData & ~PossiblyNonNegInst::NonNeg) |
                         (b * PossiblyNonNegInst::NonNeg);
}

bool Instruction::hasNoUnsignedWrap() const {
  if (auto *Inst = dyn_cast<OverflowingBinaryOperator>(this))
    return Inst->hasNoUnsignedWrap();

  return cast<TruncInst>(this)->hasNoUnsignedWrap();
}

bool Instruction::hasNoSignedWrap() const {
  if (auto *Inst = dyn_cast<OverflowingBinaryOperator>(this))
    return Inst->hasNoSignedWrap();

  return cast<TruncInst>(this)->hasNoSignedWrap();
}

bool Instruction::hasNonNeg() const {
  assert(isa<PossiblyNonNegInst>(this) && "Must be zext/uitofp");
  return (SubclassOptionalData & PossiblyNonNegInst::NonNeg) != 0;
}

bool Instruction::hasPoisonGeneratingFlags() const {
  return cast<Operator>(this)->hasPoisonGeneratingFlags();
}

void Instruction::dropPoisonGeneratingFlags() {
  switch (getOpcode()) {
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::Shl:
    cast<OverflowingBinaryOperator>(this)->setHasNoUnsignedWrap(false);
    cast<OverflowingBinaryOperator>(this)->setHasNoSignedWrap(false);
    break;

  case Instruction::UDiv:
  case Instruction::SDiv:
  case Instruction::AShr:
  case Instruction::LShr:
    cast<PossiblyExactOperator>(this)->setIsExact(false);
    break;

  case Instruction::Or:
    cast<PossiblyDisjointInst>(this)->setIsDisjoint(false);
    break;

  case Instruction::GetElementPtr:
    cast<GetElementPtrInst>(this)->setNoWrapFlags(GEPNoWrapFlags::none());
    break;

  case Instruction::UIToFP:
  case Instruction::ZExt:
    setNonNeg(false);
    break;

  case Instruction::Trunc:
    cast<TruncInst>(this)->setHasNoUnsignedWrap(false);
    cast<TruncInst>(this)->setHasNoSignedWrap(false);
    break;
  }

  if (isa<FPMathOperator>(this)) {
    setHasNoNaNs(false);
    setHasNoInfs(false);
  }

  assert(!hasPoisonGeneratingFlags() && "must be kept in sync");
}

bool Instruction::hasPoisonGeneratingMetadata() const {
  return hasMetadata(LLVMContext::MD_range) ||
         hasMetadata(LLVMContext::MD_nonnull) ||
         hasMetadata(LLVMContext::MD_align);
}

void Instruction::dropPoisonGeneratingMetadata() {
  eraseMetadata(LLVMContext::MD_range);
  eraseMetadata(LLVMContext::MD_nonnull);
  eraseMetadata(LLVMContext::MD_align);
}

bool Instruction::hasPoisonGeneratingReturnAttributes() const {
  if (const auto *CB = dyn_cast<CallBase>(this)) {
    AttributeSet RetAttrs = CB->getAttributes().getRetAttrs();
    return RetAttrs.hasAttribute(Attribute::Range) ||
           RetAttrs.hasAttribute(Attribute::Alignment) ||
           RetAttrs.hasAttribute(Attribute::NonNull);
  }
  return false;
}

void Instruction::dropPoisonGeneratingReturnAttributes() {
  if (auto *CB = dyn_cast<CallBase>(this)) {
    AttributeMask AM;
    AM.addAttribute(Attribute::Range);
    AM.addAttribute(Attribute::Alignment);
    AM.addAttribute(Attribute::NonNull);
    CB->removeRetAttrs(AM);
  }
  assert(!hasPoisonGeneratingReturnAttributes() && "must be kept in sync");
}

void Instruction::dropUBImplyingAttrsAndUnknownMetadata(
    ArrayRef<unsigned> KnownIDs) {
  dropUnknownNonDebugMetadata(KnownIDs);
  auto *CB = dyn_cast<CallBase>(this);
  if (!CB)
    return;
  // For call instructions, we also need to drop parameter and return attributes
  // that are can cause UB if the call is moved to a location where the
  // attribute is not valid.
  AttributeList AL = CB->getAttributes();
  if (AL.isEmpty())
    return;
  AttributeMask UBImplyingAttributes =
      AttributeFuncs::getUBImplyingAttributes();
  for (unsigned ArgNo = 0; ArgNo < CB->arg_size(); ArgNo++)
    CB->removeParamAttrs(ArgNo, UBImplyingAttributes);
  CB->removeRetAttrs(UBImplyingAttributes);
}

void Instruction::dropUBImplyingAttrsAndMetadata() {
  // !annotation metadata does not impact semantics.
  // !range, !nonnull and !align produce poison, so they are safe to speculate.
  // !noundef and various AA metadata must be dropped, as it generally produces
  // immediate undefined behavior.
  unsigned KnownIDs[] = {LLVMContext::MD_annotation, LLVMContext::MD_range,
                         LLVMContext::MD_nonnull, LLVMContext::MD_align};
  dropUBImplyingAttrsAndUnknownMetadata(KnownIDs);
}

bool Instruction::isExact() const {
  return cast<PossiblyExactOperator>(this)->isExact();
}

void Instruction::setFast(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setFast(B);
}

void Instruction::setHasAllowReassoc(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasAllowReassoc(B);
}

void Instruction::setHasNoNaNs(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoNaNs(B);
}

void Instruction::setHasNoInfs(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoInfs(B);
}

void Instruction::setHasNoSignedZeros(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasNoSignedZeros(B);
}

void Instruction::setHasAllowReciprocal(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasAllowReciprocal(B);
}

void Instruction::setHasAllowContract(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasAllowContract(B);
}

void Instruction::setHasApproxFunc(bool B) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setHasApproxFunc(B);
}

void Instruction::setFastMathFlags(FastMathFlags FMF) {
  assert(isa<FPMathOperator>(this) && "setting fast-math flag on invalid op");
  cast<FPMathOperator>(this)->setFastMathFlags(FMF);
}

void Instruction::copyFastMathFlags(FastMathFlags FMF) {
  assert(isa<FPMathOperator>(this) && "copying fast-math flag on invalid op");
  cast<FPMathOperator>(this)->copyFastMathFlags(FMF);
}

bool Instruction::isFast() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->isFast();
}

bool Instruction::hasAllowReassoc() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowReassoc();
}

bool Instruction::hasNoNaNs() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoNaNs();
}

bool Instruction::hasNoInfs() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoInfs();
}

bool Instruction::hasNoSignedZeros() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasNoSignedZeros();
}

bool Instruction::hasAllowReciprocal() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowReciprocal();
}

bool Instruction::hasAllowContract() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasAllowContract();
}

bool Instruction::hasApproxFunc() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->hasApproxFunc();
}

FastMathFlags Instruction::getFastMathFlags() const {
  assert(isa<FPMathOperator>(this) && "getting fast-math flag on invalid op");
  return cast<FPMathOperator>(this)->getFastMathFlags();
}

void Instruction::copyFastMathFlags(const Instruction *I) {
  copyFastMathFlags(I->getFastMathFlags());
}

void Instruction::copyIRFlags(const Value *V, bool IncludeWrapFlags) {
  // Copy the wrapping flags.
  if (IncludeWrapFlags && isa<OverflowingBinaryOperator>(this)) {
    if (auto *OB = dyn_cast<OverflowingBinaryOperator>(V)) {
      setHasNoSignedWrap(OB->hasNoSignedWrap());
      setHasNoUnsignedWrap(OB->hasNoUnsignedWrap());
    }
  }

  if (auto *TI = dyn_cast<TruncInst>(V)) {
    if (isa<TruncInst>(this)) {
      setHasNoSignedWrap(TI->hasNoSignedWrap());
      setHasNoUnsignedWrap(TI->hasNoUnsignedWrap());
    }
  }

  // Copy the exact flag.
  if (auto *PE = dyn_cast<PossiblyExactOperator>(V))
    if (isa<PossiblyExactOperator>(this))
      setIsExact(PE->isExact());

  if (auto *SrcPD = dyn_cast<PossiblyDisjointInst>(V))
    if (auto *DestPD = dyn_cast<PossiblyDisjointInst>(this))
      DestPD->setIsDisjoint(SrcPD->isDisjoint());

  // Copy the fast-math flags.
  if (auto *FP = dyn_cast<FPMathOperator>(V))
    if (isa<FPMathOperator>(this))
      copyFastMathFlags(FP->getFastMathFlags());

  if (auto *SrcGEP = dyn_cast<GetElementPtrInst>(V))
    if (auto *DestGEP = dyn_cast<GetElementPtrInst>(this))
      DestGEP->setNoWrapFlags(SrcGEP->getNoWrapFlags() |
                              DestGEP->getNoWrapFlags());

  if (auto *NNI = dyn_cast<PossiblyNonNegInst>(V))
    if (isa<PossiblyNonNegInst>(this))
      setNonNeg(NNI->hasNonNeg());
}

void Instruction::andIRFlags(const Value *V) {
  if (auto *OB = dyn_cast<OverflowingBinaryOperator>(V)) {
    if (isa<OverflowingBinaryOperator>(this)) {
      setHasNoSignedWrap(hasNoSignedWrap() && OB->hasNoSignedWrap());
      setHasNoUnsignedWrap(hasNoUnsignedWrap() && OB->hasNoUnsignedWrap());
    }
  }

  if (auto *TI = dyn_cast<TruncInst>(V)) {
    if (isa<TruncInst>(this)) {
      setHasNoSignedWrap(hasNoSignedWrap() && TI->hasNoSignedWrap());
      setHasNoUnsignedWrap(hasNoUnsignedWrap() && TI->hasNoUnsignedWrap());
    }
  }

  if (auto *PE = dyn_cast<PossiblyExactOperator>(V))
    if (isa<PossiblyExactOperator>(this))
      setIsExact(isExact() && PE->isExact());

  if (auto *SrcPD = dyn_cast<PossiblyDisjointInst>(V))
    if (auto *DestPD = dyn_cast<PossiblyDisjointInst>(this))
      DestPD->setIsDisjoint(DestPD->isDisjoint() && SrcPD->isDisjoint());

  if (auto *FP = dyn_cast<FPMathOperator>(V)) {
    if (isa<FPMathOperator>(this)) {
      FastMathFlags FM = getFastMathFlags();
      FM &= FP->getFastMathFlags();
      copyFastMathFlags(FM);
    }
  }

  if (auto *SrcGEP = dyn_cast<GetElementPtrInst>(V))
    if (auto *DestGEP = dyn_cast<GetElementPtrInst>(this))
      DestGEP->setNoWrapFlags(SrcGEP->getNoWrapFlags() &
                              DestGEP->getNoWrapFlags());

  if (auto *NNI = dyn_cast<PossiblyNonNegInst>(V))
    if (isa<PossiblyNonNegInst>(this))
      setNonNeg(hasNonNeg() && NNI->hasNonNeg());
}

const char *Instruction::getOpcodeName(unsigned OpCode) {
  switch (OpCode) {
  // Terminators
  case Ret:    return "ret";
  case Br:     return "br";
  case Switch: return "switch";
  case IndirectBr: return "indirectbr";
  case Invoke: return "invoke";
  case Resume: return "resume";
  case Unreachable: return "unreachable";
  case CleanupRet: return "cleanupret";
  case CatchRet: return "catchret";
  case CatchPad: return "catchpad";
  case CatchSwitch: return "catchswitch";
  case CallBr: return "callbr";

  // Standard unary operators...
  case FNeg: return "fneg";

  // Standard binary operators...
  case Add: return "add";
  case FAdd: return "fadd";
  case Sub: return "sub";
  case FSub: return "fsub";
  case Mul: return "mul";
  case FMul: return "fmul";
  case UDiv: return "udiv";
  case SDiv: return "sdiv";
  case FDiv: return "fdiv";
  case URem: return "urem";
  case SRem: return "srem";
  case FRem: return "frem";

  // Logical operators...
  case And: return "and";
  case Or : return "or";
  case Xor: return "xor";

  // Memory instructions...
  case Alloca:        return "alloca";
  case Load:          return "load";
  case Store:         return "store";
  case AtomicCmpXchg: return "cmpxchg";
  case AtomicRMW:     return "atomicrmw";
  case Fence:         return "fence";
  case GetElementPtr: return "getelementptr";

  // Convert instructions...
  case Trunc:         return "trunc";
  case ZExt:          return "zext";
  case SExt:          return "sext";
  case FPTrunc:       return "fptrunc";
  case FPExt:         return "fpext";
  case FPToUI:        return "fptoui";
  case FPToSI:        return "fptosi";
  case UIToFP:        return "uitofp";
  case SIToFP:        return "sitofp";
  case IntToPtr:      return "inttoptr";
  case PtrToInt:      return "ptrtoint";
  case BitCast:       return "bitcast";
  case AddrSpaceCast: return "addrspacecast";

  // Other instructions...
  case ICmp:           return "icmp";
  case FCmp:           return "fcmp";
  case PHI:            return "phi";
  case Select:         return "select";
  case Call:           return "call";
  case Shl:            return "shl";
  case LShr:           return "lshr";
  case AShr:           return "ashr";
  case VAArg:          return "va_arg";
  case ExtractElement: return "extractelement";
  case InsertElement:  return "insertelement";
  case ShuffleVector:  return "shufflevector";
  case ExtractValue:   return "extractvalue";
  case InsertValue:    return "insertvalue";
  case LandingPad:     return "landingpad";
  case CleanupPad:     return "cleanuppad";
  case Freeze:         return "freeze";

  default: return "<Invalid operator> ";
  }
}

/// This must be kept in sync with FunctionComparator::cmpOperations in
/// lib/Transforms/IPO/MergeFunctions.cpp.
bool Instruction::hasSameSpecialState(const Instruction *I2,
                                      bool IgnoreAlignment) const {
  auto I1 = this;
  assert(I1->getOpcode() == I2->getOpcode() &&
         "Can not compare special state of different instructions");

  if (const AllocaInst *AI = dyn_cast<AllocaInst>(I1))
    return AI->getAllocatedType() == cast<AllocaInst>(I2)->getAllocatedType() &&
           (AI->getAlign() == cast<AllocaInst>(I2)->getAlign() ||
            IgnoreAlignment);
  if (const LoadInst *LI = dyn_cast<LoadInst>(I1))
    return LI->isVolatile() == cast<LoadInst>(I2)->isVolatile() &&
           (LI->getAlign() == cast<LoadInst>(I2)->getAlign() ||
            IgnoreAlignment) &&
           LI->getOrdering() == cast<LoadInst>(I2)->getOrdering() &&
           LI->getSyncScopeID() == cast<LoadInst>(I2)->getSyncScopeID();
  if (const StoreInst *SI = dyn_cast<StoreInst>(I1))
    return SI->isVolatile() == cast<StoreInst>(I2)->isVolatile() &&
           (SI->getAlign() == cast<StoreInst>(I2)->getAlign() ||
            IgnoreAlignment) &&
           SI->getOrdering() == cast<StoreInst>(I2)->getOrdering() &&
           SI->getSyncScopeID() == cast<StoreInst>(I2)->getSyncScopeID();
  if (const CmpInst *CI = dyn_cast<CmpInst>(I1))
    return CI->getPredicate() == cast<CmpInst>(I2)->getPredicate();
  if (const CallInst *CI = dyn_cast<CallInst>(I1))
    return CI->isTailCall() == cast<CallInst>(I2)->isTailCall() &&
           CI->getCallingConv() == cast<CallInst>(I2)->getCallingConv() &&
           CI->getAttributes() == cast<CallInst>(I2)->getAttributes() &&
           CI->hasIdenticalOperandBundleSchema(*cast<CallInst>(I2));
  if (const InvokeInst *CI = dyn_cast<InvokeInst>(I1))
    return CI->getCallingConv() == cast<InvokeInst>(I2)->getCallingConv() &&
           CI->getAttributes() == cast<InvokeInst>(I2)->getAttributes() &&
           CI->hasIdenticalOperandBundleSchema(*cast<InvokeInst>(I2));
  if (const CallBrInst *CI = dyn_cast<CallBrInst>(I1))
    return CI->getCallingConv() == cast<CallBrInst>(I2)->getCallingConv() &&
           CI->getAttributes() == cast<CallBrInst>(I2)->getAttributes() &&
           CI->hasIdenticalOperandBundleSchema(*cast<CallBrInst>(I2));
  if (const InsertValueInst *IVI = dyn_cast<InsertValueInst>(I1))
    return IVI->getIndices() == cast<InsertValueInst>(I2)->getIndices();
  if (const ExtractValueInst *EVI = dyn_cast<ExtractValueInst>(I1))
    return EVI->getIndices() == cast<ExtractValueInst>(I2)->getIndices();
  if (const FenceInst *FI = dyn_cast<FenceInst>(I1))
    return FI->getOrdering() == cast<FenceInst>(I2)->getOrdering() &&
           FI->getSyncScopeID() == cast<FenceInst>(I2)->getSyncScopeID();
  if (const AtomicCmpXchgInst *CXI = dyn_cast<AtomicCmpXchgInst>(I1))
    return CXI->isVolatile() == cast<AtomicCmpXchgInst>(I2)->isVolatile() &&
           CXI->isWeak() == cast<AtomicCmpXchgInst>(I2)->isWeak() &&
           CXI->getSuccessOrdering() ==
               cast<AtomicCmpXchgInst>(I2)->getSuccessOrdering() &&
           CXI->getFailureOrdering() ==
               cast<AtomicCmpXchgInst>(I2)->getFailureOrdering() &&
           CXI->getSyncScopeID() ==
               cast<AtomicCmpXchgInst>(I2)->getSyncScopeID();
  if (const AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I1))
    return RMWI->getOperation() == cast<AtomicRMWInst>(I2)->getOperation() &&
           RMWI->isVolatile() == cast<AtomicRMWInst>(I2)->isVolatile() &&
           RMWI->getOrdering() == cast<AtomicRMWInst>(I2)->getOrdering() &&
           RMWI->getSyncScopeID() == cast<AtomicRMWInst>(I2)->getSyncScopeID();
  if (const ShuffleVectorInst *SVI = dyn_cast<ShuffleVectorInst>(I1))
    return SVI->getShuffleMask() ==
           cast<ShuffleVectorInst>(I2)->getShuffleMask();
  if (const GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I1))
    return GEP->getSourceElementType() ==
           cast<GetElementPtrInst>(I2)->getSourceElementType();

  return true;
}

bool Instruction::isIdenticalTo(const Instruction *I) const {
  return isIdenticalToWhenDefined(I) &&
         SubclassOptionalData == I->SubclassOptionalData;
}

bool Instruction::isIdenticalToWhenDefined(const Instruction *I) const {
  if (getOpcode() != I->getOpcode() ||
      getNumOperands() != I->getNumOperands() ||
      getType() != I->getType())
    return false;

  // If both instructions have no operands, they are identical.
  if (getNumOperands() == 0 && I->getNumOperands() == 0)
    return this->hasSameSpecialState(I);

  // We have two instructions of identical opcode and #operands.  Check to see
  // if all operands are the same.
  if (!std::equal(op_begin(), op_end(), I->op_begin()))
    return false;

  // WARNING: this logic must be kept in sync with EliminateDuplicatePHINodes()!
  if (const PHINode *thisPHI = dyn_cast<PHINode>(this)) {
    const PHINode *otherPHI = cast<PHINode>(I);
    return std::equal(thisPHI->block_begin(), thisPHI->block_end(),
                      otherPHI->block_begin());
  }

  return this->hasSameSpecialState(I);
}

// Keep this in sync with FunctionComparator::cmpOperations in
// lib/Transforms/IPO/MergeFunctions.cpp.
bool Instruction::isSameOperationAs(const Instruction *I,
                                    unsigned flags) const {
  bool IgnoreAlignment = flags & CompareIgnoringAlignment;
  bool UseScalarTypes  = flags & CompareUsingScalarTypes;

  if (getOpcode() != I->getOpcode() ||
      getNumOperands() != I->getNumOperands() ||
      (UseScalarTypes ?
       getType()->getScalarType() != I->getType()->getScalarType() :
       getType() != I->getType()))
    return false;

  // We have two instructions of identical opcode and #operands.  Check to see
  // if all operands are the same type
  for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
    if (UseScalarTypes ?
        getOperand(i)->getType()->getScalarType() !=
          I->getOperand(i)->getType()->getScalarType() :
        getOperand(i)->getType() != I->getOperand(i)->getType())
      return false;

  return this->hasSameSpecialState(I, IgnoreAlignment);
}

bool Instruction::isUsedOutsideOfBlock(const BasicBlock *BB) const {
  for (const Use &U : uses()) {
    // PHI nodes uses values in the corresponding predecessor block.  For other
    // instructions, just check to see whether the parent of the use matches up.
    const Instruction *I = cast<Instruction>(U.getUser());
    const PHINode *PN = dyn_cast<PHINode>(I);
    if (!PN) {
      if (I->getParent() != BB)
        return true;
      continue;
    }

    if (PN->getIncomingBlock(U) != BB)
      return true;
  }
  return false;
}

bool Instruction::mayReadFromMemory() const {
  switch (getOpcode()) {
  default: return false;
  case Instruction::VAArg:
  case Instruction::Load:
  case Instruction::Fence: // FIXME: refine definition of mayReadFromMemory
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
    return true;
  case Instruction::Call:
  case Instruction::Invoke:
  case Instruction::CallBr:
    return !cast<CallBase>(this)->onlyWritesMemory();
  case Instruction::Store:
    return !cast<StoreInst>(this)->isUnordered();
  }
}

bool Instruction::mayWriteToMemory() const {
  switch (getOpcode()) {
  default: return false;
  case Instruction::Fence: // FIXME: refine definition of mayWriteToMemory
  case Instruction::Store:
  case Instruction::VAArg:
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::CatchPad:
  case Instruction::CatchRet:
    return true;
  case Instruction::Call:
  case Instruction::Invoke:
  case Instruction::CallBr:
    return !cast<CallBase>(this)->onlyReadsMemory();
  case Instruction::Load:
    return !cast<LoadInst>(this)->isUnordered();
  }
}

bool Instruction::isAtomic() const {
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Fence:
    return true;
  case Instruction::Load:
    return cast<LoadInst>(this)->getOrdering() != AtomicOrdering::NotAtomic;
  case Instruction::Store:
    return cast<StoreInst>(this)->getOrdering() != AtomicOrdering::NotAtomic;
  }
}

bool Instruction::hasAtomicLoad() const {
  assert(isAtomic());
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Load:
    return true;
  }
}

bool Instruction::hasAtomicStore() const {
  assert(isAtomic());
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicCmpXchg:
  case Instruction::AtomicRMW:
  case Instruction::Store:
    return true;
  }
}

bool Instruction::isVolatile() const {
  switch (getOpcode()) {
  default:
    return false;
  case Instruction::AtomicRMW:
    return cast<AtomicRMWInst>(this)->isVolatile();
  case Instruction::Store:
    return cast<StoreInst>(this)->isVolatile();
  case Instruction::Load:
    return cast<LoadInst>(this)->isVolatile();
  case Instruction::AtomicCmpXchg:
    return cast<AtomicCmpXchgInst>(this)->isVolatile();
  case Instruction::Call:
  case Instruction::Invoke:
    // There are a very limited number of intrinsics with volatile flags.
    if (auto *II = dyn_cast<IntrinsicInst>(this)) {
      if (auto *MI = dyn_cast<MemIntrinsic>(II))
        return MI->isVolatile();
      switch (II->getIntrinsicID()) {
      default: break;
      case Intrinsic::matrix_column_major_load:
        return cast<ConstantInt>(II->getArgOperand(2))->isOne();
      case Intrinsic::matrix_column_major_store:
        return cast<ConstantInt>(II->getArgOperand(3))->isOne();
      }
    }
    return false;
  }
}

Type *Instruction::getAccessType() const {
  switch (getOpcode()) {
  case Instruction::Store:
    return cast<StoreInst>(this)->getValueOperand()->getType();
  case Instruction::Load:
  case Instruction::AtomicRMW:
    return getType();
  case Instruction::AtomicCmpXchg:
    return cast<AtomicCmpXchgInst>(this)->getNewValOperand()->getType();
  case Instruction::Call:
  case Instruction::Invoke:
    if (const IntrinsicInst *II = dyn_cast<IntrinsicInst>(this)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::masked_load:
      case Intrinsic::masked_gather:
      case Intrinsic::masked_expandload:
      case Intrinsic::vp_load:
      case Intrinsic::vp_gather:
      case Intrinsic::experimental_vp_strided_load:
        return II->getType();
      case Intrinsic::masked_store:
      case Intrinsic::masked_scatter:
      case Intrinsic::masked_compressstore:
      case Intrinsic::vp_store:
      case Intrinsic::vp_scatter:
      case Intrinsic::experimental_vp_strided_store:
        return II->getOperand(0)->getType();
      default:
        break;
      }
    }
  }

  return nullptr;
}

static bool canUnwindPastLandingPad(const LandingPadInst *LP,
                                    bool IncludePhaseOneUnwind) {
  // Because phase one unwinding skips cleanup landingpads, we effectively
  // unwind past this frame, and callers need to have valid unwind info.
  if (LP->isCleanup())
    return IncludePhaseOneUnwind;

  for (unsigned I = 0; I < LP->getNumClauses(); ++I) {
    Constant *Clause = LP->getClause(I);
    // catch ptr null catches all exceptions.
    if (LP->isCatch(I) && isa<ConstantPointerNull>(Clause))
      return false;
    // filter [0 x ptr] catches all exceptions.
    if (LP->isFilter(I) && Clause->getType()->getArrayNumElements() == 0)
      return false;
  }

  // May catch only some subset of exceptions, in which case other exceptions
  // will continue unwinding.
  return true;
}

bool Instruction::mayThrow(bool IncludePhaseOneUnwind) const {
  switch (getOpcode()) {
  case Instruction::Call:
    return !cast<CallInst>(this)->doesNotThrow();
  case Instruction::CleanupRet:
    return cast<CleanupReturnInst>(this)->unwindsToCaller();
  case Instruction::CatchSwitch:
    return cast<CatchSwitchInst>(this)->unwindsToCaller();
  case Instruction::Resume:
    return true;
  case Instruction::Invoke: {
    // Landingpads themselves don't unwind -- however, an invoke of a skipped
    // landingpad may continue unwinding.
    BasicBlock *UnwindDest = cast<InvokeInst>(this)->getUnwindDest();
    Instruction *Pad = UnwindDest->getFirstNonPHI();
    if (auto *LP = dyn_cast<LandingPadInst>(Pad))
      return canUnwindPastLandingPad(LP, IncludePhaseOneUnwind);
    return false;
  }
  case Instruction::CleanupPad:
    // Treat the same as cleanup landingpad.
    return IncludePhaseOneUnwind;
  default:
    return false;
  }
}

bool Instruction::mayHaveSideEffects() const {
  return mayWriteToMemory() || mayThrow() || !willReturn();
}

bool Instruction::isSafeToRemove() const {
  return (!isa<CallInst>(this) || !this->mayHaveSideEffects()) &&
         !this->isTerminator() && !this->isEHPad();
}

bool Instruction::willReturn() const {
  // Volatile store isn't guaranteed to return; see LangRef.
  if (auto *SI = dyn_cast<StoreInst>(this))
    return !SI->isVolatile();

  if (const auto *CB = dyn_cast<CallBase>(this))
    return CB->hasFnAttr(Attribute::WillReturn);
  return true;
}

bool Instruction::isLifetimeStartOrEnd() const {
  auto *II = dyn_cast<IntrinsicInst>(this);
  if (!II)
    return false;
  Intrinsic::ID ID = II->getIntrinsicID();
  return ID == Intrinsic::lifetime_start || ID == Intrinsic::lifetime_end;
}

bool Instruction::isLaunderOrStripInvariantGroup() const {
  auto *II = dyn_cast<IntrinsicInst>(this);
  if (!II)
    return false;
  Intrinsic::ID ID = II->getIntrinsicID();
  return ID == Intrinsic::launder_invariant_group ||
         ID == Intrinsic::strip_invariant_group;
}

bool Instruction::isDebugOrPseudoInst() const {
  return isa<DbgInfoIntrinsic>(this) || isa<PseudoProbeInst>(this);
}

const Instruction *
Instruction::getNextNonDebugInstruction(bool SkipPseudoOp) const {
  for (const Instruction *I = getNextNode(); I; I = I->getNextNode())
    if (!isa<DbgInfoIntrinsic>(I) && !(SkipPseudoOp && isa<PseudoProbeInst>(I)))
      return I;
  return nullptr;
}

const Instruction *
Instruction::getPrevNonDebugInstruction(bool SkipPseudoOp) const {
  for (const Instruction *I = getPrevNode(); I; I = I->getPrevNode())
    if (!isa<DbgInfoIntrinsic>(I) && !(SkipPseudoOp && isa<PseudoProbeInst>(I)))
      return I;
  return nullptr;
}

const DebugLoc &Instruction::getStableDebugLoc() const {
  if (isa<DbgInfoIntrinsic>(this))
    if (const Instruction *Next = getNextNonDebugInstruction())
      return Next->getDebugLoc();
  return getDebugLoc();
}

bool Instruction::isAssociative() const {
  if (auto *II = dyn_cast<IntrinsicInst>(this))
    return II->isAssociative();
  unsigned Opcode = getOpcode();
  if (isAssociative(Opcode))
    return true;

  switch (Opcode) {
  case FMul:
  case FAdd:
    return cast<FPMathOperator>(this)->hasAllowReassoc() &&
           cast<FPMathOperator>(this)->hasNoSignedZeros();
  default:
    return false;
  }
}

bool Instruction::isCommutative() const {
  if (auto *II = dyn_cast<IntrinsicInst>(this))
    return II->isCommutative();
  // TODO: Should allow icmp/fcmp?
  return isCommutative(getOpcode());
}

unsigned Instruction::getNumSuccessors() const {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<const CLASS *>(this)->getNumSuccessors();
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

BasicBlock *Instruction::getSuccessor(unsigned idx) const {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<const CLASS *>(this)->getSuccessor(idx);
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

void Instruction::setSuccessor(unsigned idx, BasicBlock *B) {
  switch (getOpcode()) {
#define HANDLE_TERM_INST(N, OPC, CLASS)                                        \
  case Instruction::OPC:                                                       \
    return static_cast<CLASS *>(this)->setSuccessor(idx, B);
#include "llvm/IR/Instruction.def"
  default:
    break;
  }
  llvm_unreachable("not a terminator");
}

void Instruction::replaceSuccessorWith(BasicBlock *OldBB, BasicBlock *NewBB) {
  for (unsigned Idx = 0, NumSuccessors = Instruction::getNumSuccessors();
       Idx != NumSuccessors; ++Idx)
    if (getSuccessor(Idx) == OldBB)
      setSuccessor(Idx, NewBB);
}

Instruction *Instruction::cloneImpl() const {
  llvm_unreachable("Subclass of Instruction failed to implement cloneImpl");
}

void Instruction::swapProfMetadata() {
  MDNode *ProfileData = getBranchWeightMDNode(*this);
  if (!ProfileData)
    return;
  unsigned FirstIdx = getBranchWeightOffset(ProfileData);
  if (ProfileData->getNumOperands() != 2 + FirstIdx)
    return;

  unsigned SecondIdx = FirstIdx + 1;
  SmallVector<Metadata *, 4> Ops;
  // If there are more weights past the second, we can't swap them
  if (ProfileData->getNumOperands() > SecondIdx + 1)
    return;
  for (unsigned Idx = 0; Idx < FirstIdx; ++Idx) {
    Ops.push_back(ProfileData->getOperand(Idx));
  }
  // Switch the order of the weights
  Ops.push_back(ProfileData->getOperand(SecondIdx));
  Ops.push_back(ProfileData->getOperand(FirstIdx));
  setMetadata(LLVMContext::MD_prof,
              MDNode::get(ProfileData->getContext(), Ops));
}

void Instruction::copyMetadata(const Instruction &SrcInst,
                               ArrayRef<unsigned> WL) {
  if (!SrcInst.hasMetadata())
    return;

  SmallDenseSet<unsigned, 4> WLS(WL.begin(), WL.end());

  // Otherwise, enumerate and copy over metadata from the old instruction to the
  // new one.
  SmallVector<std::pair<unsigned, MDNode *>, 4> TheMDs;
  SrcInst.getAllMetadataOtherThanDebugLoc(TheMDs);
  for (const auto &MD : TheMDs) {
    if (WL.empty() || WLS.count(MD.first))
      setMetadata(MD.first, MD.second);
  }
  if (WL.empty() || WLS.count(LLVMContext::MD_dbg))
    setDebugLoc(SrcInst.getDebugLoc());
}

Instruction *Instruction::clone() const {
  Instruction *New = nullptr;
  switch (getOpcode()) {
  default:
    llvm_unreachable("Unhandled Opcode.");
#define HANDLE_INST(num, opc, clas)                                            \
  case Instruction::opc:                                                       \
    New = cast<clas>(this)->cloneImpl();                                       \
    break;
#include "llvm/IR/Instruction.def"
#undef HANDLE_INST
  }

  New->SubclassOptionalData = SubclassOptionalData;
  New->copyMetadata(*this);
  return New;
}
