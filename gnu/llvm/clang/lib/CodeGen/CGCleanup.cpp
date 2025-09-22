//===--- CGCleanup.cpp - Bookkeeping and code emission for cleanups -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains code dealing with the IR generation for cleanups
// and related information.
//
// A "cleanup" is a piece of code which needs to be executed whenever
// control transfers out of a particular scope.  This can be
// conditionalized to occur only on exceptional control flow, only on
// normal control flow, or both.
//
//===----------------------------------------------------------------------===//

#include "CGCleanup.h"
#include "CodeGenFunction.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;
using namespace CodeGen;

bool DominatingValue<RValue>::saved_type::needsSaving(RValue rv) {
  if (rv.isScalar())
    return DominatingLLVMValue::needsSaving(rv.getScalarVal());
  if (rv.isAggregate())
    return DominatingValue<Address>::needsSaving(rv.getAggregateAddress());
  return true;
}

DominatingValue<RValue>::saved_type
DominatingValue<RValue>::saved_type::save(CodeGenFunction &CGF, RValue rv) {
  if (rv.isScalar()) {
    llvm::Value *V = rv.getScalarVal();
    return saved_type(DominatingLLVMValue::save(CGF, V),
                      DominatingLLVMValue::needsSaving(V) ? ScalarAddress
                                                          : ScalarLiteral);
  }

  if (rv.isComplex()) {
    CodeGenFunction::ComplexPairTy V = rv.getComplexVal();
    return saved_type(DominatingLLVMValue::save(CGF, V.first),
                      DominatingLLVMValue::save(CGF, V.second));
  }

  assert(rv.isAggregate());
  Address V = rv.getAggregateAddress();
  return saved_type(DominatingValue<Address>::save(CGF, V),
                    DominatingValue<Address>::needsSaving(V)
                        ? AggregateAddress
                        : AggregateLiteral);
}

/// Given a saved r-value produced by SaveRValue, perform the code
/// necessary to restore it to usability at the current insertion
/// point.
RValue DominatingValue<RValue>::saved_type::restore(CodeGenFunction &CGF) {
  switch (K) {
  case ScalarLiteral:
  case ScalarAddress:
    return RValue::get(DominatingLLVMValue::restore(CGF, Vals.first));
  case AggregateLiteral:
  case AggregateAddress:
    return RValue::getAggregate(
        DominatingValue<Address>::restore(CGF, AggregateAddr));
  case ComplexAddress: {
    llvm::Value *real = DominatingLLVMValue::restore(CGF, Vals.first);
    llvm::Value *imag = DominatingLLVMValue::restore(CGF, Vals.second);
    return RValue::getComplex(real, imag);
  }
  }

  llvm_unreachable("bad saved r-value kind");
}

/// Push an entry of the given size onto this protected-scope stack.
char *EHScopeStack::allocate(size_t Size) {
  Size = llvm::alignTo(Size, ScopeStackAlignment);
  if (!StartOfBuffer) {
    unsigned Capacity = 1024;
    while (Capacity < Size) Capacity *= 2;
    StartOfBuffer = new char[Capacity];
    StartOfData = EndOfBuffer = StartOfBuffer + Capacity;
  } else if (static_cast<size_t>(StartOfData - StartOfBuffer) < Size) {
    unsigned CurrentCapacity = EndOfBuffer - StartOfBuffer;
    unsigned UsedCapacity = CurrentCapacity - (StartOfData - StartOfBuffer);

    unsigned NewCapacity = CurrentCapacity;
    do {
      NewCapacity *= 2;
    } while (NewCapacity < UsedCapacity + Size);

    char *NewStartOfBuffer = new char[NewCapacity];
    char *NewEndOfBuffer = NewStartOfBuffer + NewCapacity;
    char *NewStartOfData = NewEndOfBuffer - UsedCapacity;
    memcpy(NewStartOfData, StartOfData, UsedCapacity);
    delete [] StartOfBuffer;
    StartOfBuffer = NewStartOfBuffer;
    EndOfBuffer = NewEndOfBuffer;
    StartOfData = NewStartOfData;
  }

  assert(StartOfBuffer + Size <= StartOfData);
  StartOfData -= Size;
  return StartOfData;
}

void EHScopeStack::deallocate(size_t Size) {
  StartOfData += llvm::alignTo(Size, ScopeStackAlignment);
}

bool EHScopeStack::containsOnlyLifetimeMarkers(
    EHScopeStack::stable_iterator Old) const {
  for (EHScopeStack::iterator it = begin(); stabilize(it) != Old; it++) {
    EHCleanupScope *cleanup = dyn_cast<EHCleanupScope>(&*it);
    if (!cleanup || !cleanup->isLifetimeMarker())
      return false;
  }

  return true;
}

bool EHScopeStack::requiresLandingPad() const {
  for (stable_iterator si = getInnermostEHScope(); si != stable_end(); ) {
    // Skip lifetime markers.
    if (auto *cleanup = dyn_cast<EHCleanupScope>(&*find(si)))
      if (cleanup->isLifetimeMarker()) {
        si = cleanup->getEnclosingEHScope();
        continue;
      }
    return true;
  }

  return false;
}

EHScopeStack::stable_iterator
EHScopeStack::getInnermostActiveNormalCleanup() const {
  for (stable_iterator si = getInnermostNormalCleanup(), se = stable_end();
         si != se; ) {
    EHCleanupScope &cleanup = cast<EHCleanupScope>(*find(si));
    if (cleanup.isActive()) return si;
    si = cleanup.getEnclosingNormalCleanup();
  }
  return stable_end();
}


void *EHScopeStack::pushCleanup(CleanupKind Kind, size_t Size) {
  char *Buffer = allocate(EHCleanupScope::getSizeForCleanupSize(Size));
  bool IsNormalCleanup = Kind & NormalCleanup;
  bool IsEHCleanup = Kind & EHCleanup;
  bool IsLifetimeMarker = Kind & LifetimeMarker;

  // Per C++ [except.terminate], it is implementation-defined whether none,
  // some, or all cleanups are called before std::terminate. Thus, when
  // terminate is the current EH scope, we may skip adding any EH cleanup
  // scopes.
  if (InnermostEHScope != stable_end() &&
      find(InnermostEHScope)->getKind() == EHScope::Terminate)
    IsEHCleanup = false;

  EHCleanupScope *Scope =
    new (Buffer) EHCleanupScope(IsNormalCleanup,
                                IsEHCleanup,
                                Size,
                                BranchFixups.size(),
                                InnermostNormalCleanup,
                                InnermostEHScope);
  if (IsNormalCleanup)
    InnermostNormalCleanup = stable_begin();
  if (IsEHCleanup)
    InnermostEHScope = stable_begin();
  if (IsLifetimeMarker)
    Scope->setLifetimeMarker();

  // With Windows -EHa, Invoke llvm.seh.scope.begin() for EHCleanup
  // If exceptions are disabled/ignored and SEH is not in use, then there is no
  // invoke destination. SEH "works" even if exceptions are off. In practice,
  // this means that C++ destructors and other EH cleanups don't run, which is
  // consistent with MSVC's behavior, except in the presence of -EHa.
  // Check getInvokeDest() to generate llvm.seh.scope.begin() as needed.
  if (CGF->getLangOpts().EHAsynch && IsEHCleanup && !IsLifetimeMarker &&
      CGF->getTarget().getCXXABI().isMicrosoft() && CGF->getInvokeDest())
    CGF->EmitSehCppScopeBegin();

  return Scope->getCleanupBuffer();
}

void EHScopeStack::popCleanup() {
  assert(!empty() && "popping exception stack when not empty");

  assert(isa<EHCleanupScope>(*begin()));
  EHCleanupScope &Cleanup = cast<EHCleanupScope>(*begin());
  InnermostNormalCleanup = Cleanup.getEnclosingNormalCleanup();
  InnermostEHScope = Cleanup.getEnclosingEHScope();
  deallocate(Cleanup.getAllocatedSize());

  // Destroy the cleanup.
  Cleanup.Destroy();

  // Check whether we can shrink the branch-fixups stack.
  if (!BranchFixups.empty()) {
    // If we no longer have any normal cleanups, all the fixups are
    // complete.
    if (!hasNormalCleanups())
      BranchFixups.clear();

    // Otherwise we can still trim out unnecessary nulls.
    else
      popNullFixups();
  }
}

EHFilterScope *EHScopeStack::pushFilter(unsigned numFilters) {
  assert(getInnermostEHScope() == stable_end());
  char *buffer = allocate(EHFilterScope::getSizeForNumFilters(numFilters));
  EHFilterScope *filter = new (buffer) EHFilterScope(numFilters);
  InnermostEHScope = stable_begin();
  return filter;
}

void EHScopeStack::popFilter() {
  assert(!empty() && "popping exception stack when not empty");

  EHFilterScope &filter = cast<EHFilterScope>(*begin());
  deallocate(EHFilterScope::getSizeForNumFilters(filter.getNumFilters()));

  InnermostEHScope = filter.getEnclosingEHScope();
}

EHCatchScope *EHScopeStack::pushCatch(unsigned numHandlers) {
  char *buffer = allocate(EHCatchScope::getSizeForNumHandlers(numHandlers));
  EHCatchScope *scope =
    new (buffer) EHCatchScope(numHandlers, InnermostEHScope);
  InnermostEHScope = stable_begin();
  return scope;
}

void EHScopeStack::pushTerminate() {
  char *Buffer = allocate(EHTerminateScope::getSize());
  new (Buffer) EHTerminateScope(InnermostEHScope);
  InnermostEHScope = stable_begin();
}

/// Remove any 'null' fixups on the stack.  However, we can't pop more
/// fixups than the fixup depth on the innermost normal cleanup, or
/// else fixups that we try to add to that cleanup will end up in the
/// wrong place.  We *could* try to shrink fixup depths, but that's
/// actually a lot of work for little benefit.
void EHScopeStack::popNullFixups() {
  // We expect this to only be called when there's still an innermost
  // normal cleanup;  otherwise there really shouldn't be any fixups.
  assert(hasNormalCleanups());

  EHScopeStack::iterator it = find(InnermostNormalCleanup);
  unsigned MinSize = cast<EHCleanupScope>(*it).getFixupDepth();
  assert(BranchFixups.size() >= MinSize && "fixup stack out of order");

  while (BranchFixups.size() > MinSize &&
         BranchFixups.back().Destination == nullptr)
    BranchFixups.pop_back();
}

RawAddress CodeGenFunction::createCleanupActiveFlag() {
  // Create a variable to decide whether the cleanup needs to be run.
  RawAddress active = CreateTempAllocaWithoutCast(
      Builder.getInt1Ty(), CharUnits::One(), "cleanup.cond");

  // Initialize it to false at a site that's guaranteed to be run
  // before each evaluation.
  setBeforeOutermostConditional(Builder.getFalse(), active, *this);

  // Initialize it to true at the current location.
  Builder.CreateStore(Builder.getTrue(), active);

  return active;
}

void CodeGenFunction::initFullExprCleanupWithFlag(RawAddress ActiveFlag) {
  // Set that as the active flag in the cleanup.
  EHCleanupScope &cleanup = cast<EHCleanupScope>(*EHStack.begin());
  assert(!cleanup.hasActiveFlag() && "cleanup already has active flag?");
  cleanup.setActiveFlag(ActiveFlag);

  if (cleanup.isNormalCleanup()) cleanup.setTestFlagInNormalCleanup();
  if (cleanup.isEHCleanup()) cleanup.setTestFlagInEHCleanup();
}

void EHScopeStack::Cleanup::anchor() {}

static void createStoreInstBefore(llvm::Value *value, Address addr,
                                  llvm::Instruction *beforeInst,
                                  CodeGenFunction &CGF) {
  auto store = new llvm::StoreInst(value, addr.emitRawPointer(CGF), beforeInst);
  store->setAlignment(addr.getAlignment().getAsAlign());
}

static llvm::LoadInst *createLoadInstBefore(Address addr, const Twine &name,
                                            llvm::Instruction *beforeInst,
                                            CodeGenFunction &CGF) {
  return new llvm::LoadInst(addr.getElementType(), addr.emitRawPointer(CGF),
                            name, false, addr.getAlignment().getAsAlign(),
                            beforeInst);
}

/// All the branch fixups on the EH stack have propagated out past the
/// outermost normal cleanup; resolve them all by adding cases to the
/// given switch instruction.
static void ResolveAllBranchFixups(CodeGenFunction &CGF,
                                   llvm::SwitchInst *Switch,
                                   llvm::BasicBlock *CleanupEntry) {
  llvm::SmallPtrSet<llvm::BasicBlock*, 4> CasesAdded;

  for (unsigned I = 0, E = CGF.EHStack.getNumBranchFixups(); I != E; ++I) {
    // Skip this fixup if its destination isn't set.
    BranchFixup &Fixup = CGF.EHStack.getBranchFixup(I);
    if (Fixup.Destination == nullptr) continue;

    // If there isn't an OptimisticBranchBlock, then InitialBranch is
    // still pointing directly to its destination; forward it to the
    // appropriate cleanup entry.  This is required in the specific
    // case of
    //   { std::string s; goto lbl; }
    //   lbl:
    // i.e. where there's an unresolved fixup inside a single cleanup
    // entry which we're currently popping.
    if (Fixup.OptimisticBranchBlock == nullptr) {
      createStoreInstBefore(CGF.Builder.getInt32(Fixup.DestinationIndex),
                            CGF.getNormalCleanupDestSlot(), Fixup.InitialBranch,
                            CGF);
      Fixup.InitialBranch->setSuccessor(0, CleanupEntry);
    }

    // Don't add this case to the switch statement twice.
    if (!CasesAdded.insert(Fixup.Destination).second)
      continue;

    Switch->addCase(CGF.Builder.getInt32(Fixup.DestinationIndex),
                    Fixup.Destination);
  }

  CGF.EHStack.clearFixups();
}

/// Transitions the terminator of the given exit-block of a cleanup to
/// be a cleanup switch.
static llvm::SwitchInst *TransitionToCleanupSwitch(CodeGenFunction &CGF,
                                                   llvm::BasicBlock *Block) {
  // If it's a branch, turn it into a switch whose default
  // destination is its original target.
  llvm::Instruction *Term = Block->getTerminator();
  assert(Term && "can't transition block without terminator");

  if (llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Term)) {
    assert(Br->isUnconditional());
    auto Load = createLoadInstBefore(CGF.getNormalCleanupDestSlot(),
                                     "cleanup.dest", Term, CGF);
    llvm::SwitchInst *Switch =
      llvm::SwitchInst::Create(Load, Br->getSuccessor(0), 4, Block);
    Br->eraseFromParent();
    return Switch;
  } else {
    return cast<llvm::SwitchInst>(Term);
  }
}

void CodeGenFunction::ResolveBranchFixups(llvm::BasicBlock *Block) {
  assert(Block && "resolving a null target block");
  if (!EHStack.getNumBranchFixups()) return;

  assert(EHStack.hasNormalCleanups() &&
         "branch fixups exist with no normal cleanups on stack");

  llvm::SmallPtrSet<llvm::BasicBlock*, 4> ModifiedOptimisticBlocks;
  bool ResolvedAny = false;

  for (unsigned I = 0, E = EHStack.getNumBranchFixups(); I != E; ++I) {
    // Skip this fixup if its destination doesn't match.
    BranchFixup &Fixup = EHStack.getBranchFixup(I);
    if (Fixup.Destination != Block) continue;

    Fixup.Destination = nullptr;
    ResolvedAny = true;

    // If it doesn't have an optimistic branch block, LatestBranch is
    // already pointing to the right place.
    llvm::BasicBlock *BranchBB = Fixup.OptimisticBranchBlock;
    if (!BranchBB)
      continue;

    // Don't process the same optimistic branch block twice.
    if (!ModifiedOptimisticBlocks.insert(BranchBB).second)
      continue;

    llvm::SwitchInst *Switch = TransitionToCleanupSwitch(*this, BranchBB);

    // Add a case to the switch.
    Switch->addCase(Builder.getInt32(Fixup.DestinationIndex), Block);
  }

  if (ResolvedAny)
    EHStack.popNullFixups();
}

/// Pops cleanup blocks until the given savepoint is reached.
void CodeGenFunction::PopCleanupBlocks(
    EHScopeStack::stable_iterator Old,
    std::initializer_list<llvm::Value **> ValuesToReload) {
  assert(Old.isValid());

  bool HadBranches = false;
  while (EHStack.stable_begin() != Old) {
    EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.begin());
    HadBranches |= Scope.hasBranches();

    // As long as Old strictly encloses the scope's enclosing normal
    // cleanup, we're going to emit another normal cleanup which
    // fallthrough can propagate through.
    bool FallThroughIsBranchThrough =
      Old.strictlyEncloses(Scope.getEnclosingNormalCleanup());

    PopCleanupBlock(FallThroughIsBranchThrough);
  }

  // If we didn't have any branches, the insertion point before cleanups must
  // dominate the current insertion point and we don't need to reload any
  // values.
  if (!HadBranches)
    return;

  // Spill and reload all values that the caller wants to be live at the current
  // insertion point.
  for (llvm::Value **ReloadedValue : ValuesToReload) {
    auto *Inst = dyn_cast_or_null<llvm::Instruction>(*ReloadedValue);
    if (!Inst)
      continue;

    // Don't spill static allocas, they dominate all cleanups. These are created
    // by binding a reference to a local variable or temporary.
    auto *AI = dyn_cast<llvm::AllocaInst>(Inst);
    if (AI && AI->isStaticAlloca())
      continue;

    Address Tmp =
        CreateDefaultAlignTempAlloca(Inst->getType(), "tmp.exprcleanup");

    // Find an insertion point after Inst and spill it to the temporary.
    llvm::BasicBlock::iterator InsertBefore;
    if (auto *Invoke = dyn_cast<llvm::InvokeInst>(Inst))
      InsertBefore = Invoke->getNormalDest()->getFirstInsertionPt();
    else
      InsertBefore = std::next(Inst->getIterator());
    CGBuilderTy(CGM, &*InsertBefore).CreateStore(Inst, Tmp);

    // Reload the value at the current insertion point.
    *ReloadedValue = Builder.CreateLoad(Tmp);
  }
}

/// Pops cleanup blocks until the given savepoint is reached, then add the
/// cleanups from the given savepoint in the lifetime-extended cleanups stack.
void CodeGenFunction::PopCleanupBlocks(
    EHScopeStack::stable_iterator Old, size_t OldLifetimeExtendedSize,
    std::initializer_list<llvm::Value **> ValuesToReload) {
  PopCleanupBlocks(Old, ValuesToReload);

  // Move our deferred cleanups onto the EH stack.
  for (size_t I = OldLifetimeExtendedSize,
              E = LifetimeExtendedCleanupStack.size(); I != E; /**/) {
    // Alignment should be guaranteed by the vptrs in the individual cleanups.
    assert((I % alignof(LifetimeExtendedCleanupHeader) == 0) &&
           "misaligned cleanup stack entry");

    LifetimeExtendedCleanupHeader &Header =
        reinterpret_cast<LifetimeExtendedCleanupHeader&>(
            LifetimeExtendedCleanupStack[I]);
    I += sizeof(Header);

    EHStack.pushCopyOfCleanup(Header.getKind(),
                              &LifetimeExtendedCleanupStack[I],
                              Header.getSize());
    I += Header.getSize();

    if (Header.isConditional()) {
      RawAddress ActiveFlag =
          reinterpret_cast<RawAddress &>(LifetimeExtendedCleanupStack[I]);
      initFullExprCleanupWithFlag(ActiveFlag);
      I += sizeof(ActiveFlag);
    }
  }
  LifetimeExtendedCleanupStack.resize(OldLifetimeExtendedSize);
}

static llvm::BasicBlock *CreateNormalEntry(CodeGenFunction &CGF,
                                           EHCleanupScope &Scope) {
  assert(Scope.isNormalCleanup());
  llvm::BasicBlock *Entry = Scope.getNormalBlock();
  if (!Entry) {
    Entry = CGF.createBasicBlock("cleanup");
    Scope.setNormalBlock(Entry);
  }
  return Entry;
}

/// Attempts to reduce a cleanup's entry block to a fallthrough.  This
/// is basically llvm::MergeBlockIntoPredecessor, except
/// simplified/optimized for the tighter constraints on cleanup blocks.
///
/// Returns the new block, whatever it is.
static llvm::BasicBlock *SimplifyCleanupEntry(CodeGenFunction &CGF,
                                              llvm::BasicBlock *Entry) {
  llvm::BasicBlock *Pred = Entry->getSinglePredecessor();
  if (!Pred) return Entry;

  llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Pred->getTerminator());
  if (!Br || Br->isConditional()) return Entry;
  assert(Br->getSuccessor(0) == Entry);

  // If we were previously inserting at the end of the cleanup entry
  // block, we'll need to continue inserting at the end of the
  // predecessor.
  bool WasInsertBlock = CGF.Builder.GetInsertBlock() == Entry;
  assert(!WasInsertBlock || CGF.Builder.GetInsertPoint() == Entry->end());

  // Kill the branch.
  Br->eraseFromParent();

  // Replace all uses of the entry with the predecessor, in case there
  // are phis in the cleanup.
  Entry->replaceAllUsesWith(Pred);

  // Merge the blocks.
  Pred->splice(Pred->end(), Entry);

  // Kill the entry block.
  Entry->eraseFromParent();

  if (WasInsertBlock)
    CGF.Builder.SetInsertPoint(Pred);

  return Pred;
}

static void EmitCleanup(CodeGenFunction &CGF,
                        EHScopeStack::Cleanup *Fn,
                        EHScopeStack::Cleanup::Flags flags,
                        Address ActiveFlag) {
  // If there's an active flag, load it and skip the cleanup if it's
  // false.
  llvm::BasicBlock *ContBB = nullptr;
  if (ActiveFlag.isValid()) {
    ContBB = CGF.createBasicBlock("cleanup.done");
    llvm::BasicBlock *CleanupBB = CGF.createBasicBlock("cleanup.action");
    llvm::Value *IsActive
      = CGF.Builder.CreateLoad(ActiveFlag, "cleanup.is_active");
    CGF.Builder.CreateCondBr(IsActive, CleanupBB, ContBB);
    CGF.EmitBlock(CleanupBB);
  }

  // Ask the cleanup to emit itself.
  Fn->Emit(CGF, flags);
  assert(CGF.HaveInsertPoint() && "cleanup ended with no insertion point?");

  // Emit the continuation block if there was an active flag.
  if (ActiveFlag.isValid())
    CGF.EmitBlock(ContBB);
}

static void ForwardPrebranchedFallthrough(llvm::BasicBlock *Exit,
                                          llvm::BasicBlock *From,
                                          llvm::BasicBlock *To) {
  // Exit is the exit block of a cleanup, so it always terminates in
  // an unconditional branch or a switch.
  llvm::Instruction *Term = Exit->getTerminator();

  if (llvm::BranchInst *Br = dyn_cast<llvm::BranchInst>(Term)) {
    assert(Br->isUnconditional() && Br->getSuccessor(0) == From);
    Br->setSuccessor(0, To);
  } else {
    llvm::SwitchInst *Switch = cast<llvm::SwitchInst>(Term);
    for (unsigned I = 0, E = Switch->getNumSuccessors(); I != E; ++I)
      if (Switch->getSuccessor(I) == From)
        Switch->setSuccessor(I, To);
  }
}

/// We don't need a normal entry block for the given cleanup.
/// Optimistic fixup branches can cause these blocks to come into
/// existence anyway;  if so, destroy it.
///
/// The validity of this transformation is very much specific to the
/// exact ways in which we form branches to cleanup entries.
static void destroyOptimisticNormalEntry(CodeGenFunction &CGF,
                                         EHCleanupScope &scope) {
  llvm::BasicBlock *entry = scope.getNormalBlock();
  if (!entry) return;

  // Replace all the uses with unreachable.
  llvm::BasicBlock *unreachableBB = CGF.getUnreachableBlock();
  for (llvm::BasicBlock::use_iterator
         i = entry->use_begin(), e = entry->use_end(); i != e; ) {
    llvm::Use &use = *i;
    ++i;

    use.set(unreachableBB);

    // The only uses should be fixup switches.
    llvm::SwitchInst *si = cast<llvm::SwitchInst>(use.getUser());
    if (si->getNumCases() == 1 && si->getDefaultDest() == unreachableBB) {
      // Replace the switch with a branch.
      llvm::BranchInst::Create(si->case_begin()->getCaseSuccessor(), si);

      // The switch operand is a load from the cleanup-dest alloca.
      llvm::LoadInst *condition = cast<llvm::LoadInst>(si->getCondition());

      // Destroy the switch.
      si->eraseFromParent();

      // Destroy the load.
      assert(condition->getOperand(0) == CGF.NormalCleanupDest.getPointer());
      assert(condition->use_empty());
      condition->eraseFromParent();
    }
  }

  assert(entry->use_empty());
  delete entry;
}

/// Pops a cleanup block.  If the block includes a normal cleanup, the
/// current insertion point is threaded through the cleanup, as are
/// any branch fixups on the cleanup.
void CodeGenFunction::PopCleanupBlock(bool FallthroughIsBranchThrough,
                                      bool ForDeactivation) {
  assert(!EHStack.empty() && "cleanup stack is empty!");
  assert(isa<EHCleanupScope>(*EHStack.begin()) && "top not a cleanup!");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.begin());
  assert(Scope.getFixupDepth() <= EHStack.getNumBranchFixups());

  // If we are deactivating a normal cleanup, we need to pretend that the
  // fallthrough is unreachable. We restore this IP before returning.
  CGBuilderTy::InsertPoint NormalDeactivateOrigIP;
  if (ForDeactivation && (Scope.isNormalCleanup() || !getLangOpts().EHAsynch)) {
    NormalDeactivateOrigIP = Builder.saveAndClearIP();
  }
  // Remember activation information.
  bool IsActive = Scope.isActive();
  Address NormalActiveFlag =
    Scope.shouldTestFlagInNormalCleanup() ? Scope.getActiveFlag()
                                          : Address::invalid();
  Address EHActiveFlag =
    Scope.shouldTestFlagInEHCleanup() ? Scope.getActiveFlag()
                                      : Address::invalid();

  // Check whether we need an EH cleanup.  This is only true if we've
  // generated a lazy EH cleanup block.
  llvm::BasicBlock *EHEntry = Scope.getCachedEHDispatchBlock();
  assert(Scope.hasEHBranches() == (EHEntry != nullptr));
  bool RequiresEHCleanup = (EHEntry != nullptr);
  EHScopeStack::stable_iterator EHParent = Scope.getEnclosingEHScope();

  // Check the three conditions which might require a normal cleanup:

  // - whether there are branch fix-ups through this cleanup
  unsigned FixupDepth = Scope.getFixupDepth();
  bool HasFixups = EHStack.getNumBranchFixups() != FixupDepth;

  // - whether there are branch-throughs or branch-afters
  bool HasExistingBranches = Scope.hasBranches();

  // - whether there's a fallthrough
  llvm::BasicBlock *FallthroughSource = Builder.GetInsertBlock();
  bool HasFallthrough =
      FallthroughSource != nullptr && (IsActive || HasExistingBranches);

  // Branch-through fall-throughs leave the insertion point set to the
  // end of the last cleanup, which points to the current scope.  The
  // rest of IR gen doesn't need to worry about this; it only happens
  // during the execution of PopCleanupBlocks().
  bool HasPrebranchedFallthrough =
    (FallthroughSource && FallthroughSource->getTerminator());

  // If this is a normal cleanup, then having a prebranched
  // fallthrough implies that the fallthrough source unconditionally
  // jumps here.
  assert(!Scope.isNormalCleanup() || !HasPrebranchedFallthrough ||
         (Scope.getNormalBlock() &&
          FallthroughSource->getTerminator()->getSuccessor(0)
            == Scope.getNormalBlock()));

  bool RequiresNormalCleanup = false;
  if (Scope.isNormalCleanup() &&
      (HasFixups || HasExistingBranches || HasFallthrough)) {
    RequiresNormalCleanup = true;
  }

  // If we have a prebranched fallthrough into an inactive normal
  // cleanup, rewrite it so that it leads to the appropriate place.
  if (Scope.isNormalCleanup() && HasPrebranchedFallthrough &&
      !RequiresNormalCleanup) {
    // FIXME: Come up with a program which would need forwarding prebranched
    // fallthrough and add tests. Otherwise delete this and assert against it.
    assert(!IsActive);
    llvm::BasicBlock *prebranchDest;

    // If the prebranch is semantically branching through the next
    // cleanup, just forward it to the next block, leaving the
    // insertion point in the prebranched block.
    if (FallthroughIsBranchThrough) {
      EHScope &enclosing = *EHStack.find(Scope.getEnclosingNormalCleanup());
      prebranchDest = CreateNormalEntry(*this, cast<EHCleanupScope>(enclosing));

    // Otherwise, we need to make a new block.  If the normal cleanup
    // isn't being used at all, we could actually reuse the normal
    // entry block, but this is simpler, and it avoids conflicts with
    // dead optimistic fixup branches.
    } else {
      prebranchDest = createBasicBlock("forwarded-prebranch");
      EmitBlock(prebranchDest);
    }

    llvm::BasicBlock *normalEntry = Scope.getNormalBlock();
    assert(normalEntry && !normalEntry->use_empty());

    ForwardPrebranchedFallthrough(FallthroughSource,
                                  normalEntry, prebranchDest);
  }

  // If we don't need the cleanup at all, we're done.
  if (!RequiresNormalCleanup && !RequiresEHCleanup) {
    destroyOptimisticNormalEntry(*this, Scope);
    EHStack.popCleanup(); // safe because there are no fixups
    assert(EHStack.getNumBranchFixups() == 0 ||
           EHStack.hasNormalCleanups());
    if (NormalDeactivateOrigIP.isSet())
      Builder.restoreIP(NormalDeactivateOrigIP);
    return;
  }

  // Copy the cleanup emission data out.  This uses either a stack
  // array or malloc'd memory, depending on the size, which is
  // behavior that SmallVector would provide, if we could use it
  // here. Unfortunately, if you ask for a SmallVector<char>, the
  // alignment isn't sufficient.
  auto *CleanupSource = reinterpret_cast<char *>(Scope.getCleanupBuffer());
  alignas(EHScopeStack::ScopeStackAlignment) char
      CleanupBufferStack[8 * sizeof(void *)];
  std::unique_ptr<char[]> CleanupBufferHeap;
  size_t CleanupSize = Scope.getCleanupSize();
  EHScopeStack::Cleanup *Fn;

  if (CleanupSize <= sizeof(CleanupBufferStack)) {
    memcpy(CleanupBufferStack, CleanupSource, CleanupSize);
    Fn = reinterpret_cast<EHScopeStack::Cleanup *>(CleanupBufferStack);
  } else {
    CleanupBufferHeap.reset(new char[CleanupSize]);
    memcpy(CleanupBufferHeap.get(), CleanupSource, CleanupSize);
    Fn = reinterpret_cast<EHScopeStack::Cleanup *>(CleanupBufferHeap.get());
  }

  EHScopeStack::Cleanup::Flags cleanupFlags;
  if (Scope.isNormalCleanup())
    cleanupFlags.setIsNormalCleanupKind();
  if (Scope.isEHCleanup())
    cleanupFlags.setIsEHCleanupKind();

  // Under -EHa, invoke seh.scope.end() to mark scope end before dtor
  bool IsEHa = getLangOpts().EHAsynch && !Scope.isLifetimeMarker();
  const EHPersonality &Personality = EHPersonality::get(*this);
  if (!RequiresNormalCleanup) {
    // Mark CPP scope end for passed-by-value Arg temp
    //   per Windows ABI which is "normally" Cleanup in callee
    if (IsEHa && getInvokeDest()) {
      // If we are deactivating a normal cleanup then we don't have a
      // fallthrough. Restore original IP to emit CPP scope ends in the correct
      // block.
      if (NormalDeactivateOrigIP.isSet())
        Builder.restoreIP(NormalDeactivateOrigIP);
      if (Personality.isMSVCXXPersonality() && Builder.GetInsertBlock())
        EmitSehCppScopeEnd();
      if (NormalDeactivateOrigIP.isSet())
        NormalDeactivateOrigIP = Builder.saveAndClearIP();
    }
    destroyOptimisticNormalEntry(*this, Scope);
    Scope.MarkEmitted();
    EHStack.popCleanup();
  } else {
    // If we have a fallthrough and no other need for the cleanup,
    // emit it directly.
    if (HasFallthrough && !HasPrebranchedFallthrough && !HasFixups &&
        !HasExistingBranches) {

      // mark SEH scope end for fall-through flow
      if (IsEHa && getInvokeDest()) {
        if (Personality.isMSVCXXPersonality())
          EmitSehCppScopeEnd();
        else
          EmitSehTryScopeEnd();
      }

      destroyOptimisticNormalEntry(*this, Scope);
      Scope.MarkEmitted();
      EHStack.popCleanup();

      EmitCleanup(*this, Fn, cleanupFlags, NormalActiveFlag);

    // Otherwise, the best approach is to thread everything through
    // the cleanup block and then try to clean up after ourselves.
    } else {
      // Force the entry block to exist.
      llvm::BasicBlock *NormalEntry = CreateNormalEntry(*this, Scope);

      // I.  Set up the fallthrough edge in.

      CGBuilderTy::InsertPoint savedInactiveFallthroughIP;

      // If there's a fallthrough, we need to store the cleanup
      // destination index.  For fall-throughs this is always zero.
      if (HasFallthrough) {
        if (!HasPrebranchedFallthrough)
          Builder.CreateStore(Builder.getInt32(0), getNormalCleanupDestSlot());

      // Otherwise, save and clear the IP if we don't have fallthrough
      // because the cleanup is inactive.
      } else if (FallthroughSource) {
        assert(!IsActive && "source without fallthrough for active cleanup");
        savedInactiveFallthroughIP = Builder.saveAndClearIP();
      }

      // II.  Emit the entry block.  This implicitly branches to it if
      // we have fallthrough.  All the fixups and existing branches
      // should already be branched to it.
      EmitBlock(NormalEntry);

      // intercept normal cleanup to mark SEH scope end
      if (IsEHa && getInvokeDest()) {
        if (Personality.isMSVCXXPersonality())
          EmitSehCppScopeEnd();
        else
          EmitSehTryScopeEnd();
      }

      // III.  Figure out where we're going and build the cleanup
      // epilogue.

      bool HasEnclosingCleanups =
        (Scope.getEnclosingNormalCleanup() != EHStack.stable_end());

      // Compute the branch-through dest if we need it:
      //   - if there are branch-throughs threaded through the scope
      //   - if fall-through is a branch-through
      //   - if there are fixups that will be optimistically forwarded
      //     to the enclosing cleanup
      llvm::BasicBlock *BranchThroughDest = nullptr;
      if (Scope.hasBranchThroughs() ||
          (FallthroughSource && FallthroughIsBranchThrough) ||
          (HasFixups && HasEnclosingCleanups)) {
        assert(HasEnclosingCleanups);
        EHScope &S = *EHStack.find(Scope.getEnclosingNormalCleanup());
        BranchThroughDest = CreateNormalEntry(*this, cast<EHCleanupScope>(S));
      }

      llvm::BasicBlock *FallthroughDest = nullptr;
      SmallVector<llvm::Instruction*, 2> InstsToAppend;

      // If there's exactly one branch-after and no other threads,
      // we can route it without a switch.
      // Skip for SEH, since ExitSwitch is used to generate code to indicate
      // abnormal termination. (SEH: Except _leave and fall-through at
      // the end, all other exits in a _try (return/goto/continue/break)
      // are considered as abnormal terminations, using NormalCleanupDestSlot
      // to indicate abnormal termination)
      if (!Scope.hasBranchThroughs() && !HasFixups && !HasFallthrough &&
          !currentFunctionUsesSEHTry() && Scope.getNumBranchAfters() == 1) {
        assert(!BranchThroughDest || !IsActive);

        // Clean up the possibly dead store to the cleanup dest slot.
        llvm::Instruction *NormalCleanupDestSlot =
            cast<llvm::Instruction>(getNormalCleanupDestSlot().getPointer());
        if (NormalCleanupDestSlot->hasOneUse()) {
          NormalCleanupDestSlot->user_back()->eraseFromParent();
          NormalCleanupDestSlot->eraseFromParent();
          NormalCleanupDest = RawAddress::invalid();
        }

        llvm::BasicBlock *BranchAfter = Scope.getBranchAfterBlock(0);
        InstsToAppend.push_back(llvm::BranchInst::Create(BranchAfter));

      // Build a switch-out if we need it:
      //   - if there are branch-afters threaded through the scope
      //   - if fall-through is a branch-after
      //   - if there are fixups that have nowhere left to go and
      //     so must be immediately resolved
      } else if (Scope.getNumBranchAfters() ||
                 (HasFallthrough && !FallthroughIsBranchThrough) ||
                 (HasFixups && !HasEnclosingCleanups)) {

        llvm::BasicBlock *Default =
          (BranchThroughDest ? BranchThroughDest : getUnreachableBlock());

        // TODO: base this on the number of branch-afters and fixups
        const unsigned SwitchCapacity = 10;

        // pass the abnormal exit flag to Fn (SEH cleanup)
        cleanupFlags.setHasExitSwitch();

        llvm::LoadInst *Load = createLoadInstBefore(
            getNormalCleanupDestSlot(), "cleanup.dest", nullptr, *this);
        llvm::SwitchInst *Switch =
          llvm::SwitchInst::Create(Load, Default, SwitchCapacity);

        InstsToAppend.push_back(Load);
        InstsToAppend.push_back(Switch);

        // Branch-after fallthrough.
        if (FallthroughSource && !FallthroughIsBranchThrough) {
          FallthroughDest = createBasicBlock("cleanup.cont");
          if (HasFallthrough)
            Switch->addCase(Builder.getInt32(0), FallthroughDest);
        }

        for (unsigned I = 0, E = Scope.getNumBranchAfters(); I != E; ++I) {
          Switch->addCase(Scope.getBranchAfterIndex(I),
                          Scope.getBranchAfterBlock(I));
        }

        // If there aren't any enclosing cleanups, we can resolve all
        // the fixups now.
        if (HasFixups && !HasEnclosingCleanups)
          ResolveAllBranchFixups(*this, Switch, NormalEntry);
      } else {
        // We should always have a branch-through destination in this case.
        assert(BranchThroughDest);
        InstsToAppend.push_back(llvm::BranchInst::Create(BranchThroughDest));
      }

      // IV.  Pop the cleanup and emit it.
      Scope.MarkEmitted();
      EHStack.popCleanup();
      assert(EHStack.hasNormalCleanups() == HasEnclosingCleanups);

      EmitCleanup(*this, Fn, cleanupFlags, NormalActiveFlag);

      // Append the prepared cleanup prologue from above.
      llvm::BasicBlock *NormalExit = Builder.GetInsertBlock();
      for (unsigned I = 0, E = InstsToAppend.size(); I != E; ++I)
        InstsToAppend[I]->insertInto(NormalExit, NormalExit->end());

      // Optimistically hope that any fixups will continue falling through.
      for (unsigned I = FixupDepth, E = EHStack.getNumBranchFixups();
           I < E; ++I) {
        BranchFixup &Fixup = EHStack.getBranchFixup(I);
        if (!Fixup.Destination) continue;
        if (!Fixup.OptimisticBranchBlock) {
          createStoreInstBefore(Builder.getInt32(Fixup.DestinationIndex),
                                getNormalCleanupDestSlot(), Fixup.InitialBranch,
                                *this);
          Fixup.InitialBranch->setSuccessor(0, NormalEntry);
        }
        Fixup.OptimisticBranchBlock = NormalExit;
      }

      // V.  Set up the fallthrough edge out.

      // Case 1: a fallthrough source exists but doesn't branch to the
      // cleanup because the cleanup is inactive.
      if (!HasFallthrough && FallthroughSource) {
        // Prebranched fallthrough was forwarded earlier.
        // Non-prebranched fallthrough doesn't need to be forwarded.
        // Either way, all we need to do is restore the IP we cleared before.
        assert(!IsActive);
        Builder.restoreIP(savedInactiveFallthroughIP);

      // Case 2: a fallthrough source exists and should branch to the
      // cleanup, but we're not supposed to branch through to the next
      // cleanup.
      } else if (HasFallthrough && FallthroughDest) {
        assert(!FallthroughIsBranchThrough);
        EmitBlock(FallthroughDest);

      // Case 3: a fallthrough source exists and should branch to the
      // cleanup and then through to the next.
      } else if (HasFallthrough) {
        // Everything is already set up for this.

      // Case 4: no fallthrough source exists.
      } else {
        Builder.ClearInsertionPoint();
      }

      // VI.  Assorted cleaning.

      // Check whether we can merge NormalEntry into a single predecessor.
      // This might invalidate (non-IR) pointers to NormalEntry.
      llvm::BasicBlock *NewNormalEntry =
        SimplifyCleanupEntry(*this, NormalEntry);

      // If it did invalidate those pointers, and NormalEntry was the same
      // as NormalExit, go back and patch up the fixups.
      if (NewNormalEntry != NormalEntry && NormalEntry == NormalExit)
        for (unsigned I = FixupDepth, E = EHStack.getNumBranchFixups();
               I < E; ++I)
          EHStack.getBranchFixup(I).OptimisticBranchBlock = NewNormalEntry;
    }
  }

  if (NormalDeactivateOrigIP.isSet())
    Builder.restoreIP(NormalDeactivateOrigIP);
  assert(EHStack.hasNormalCleanups() || EHStack.getNumBranchFixups() == 0);

  // Emit the EH cleanup if required.
  if (RequiresEHCleanup) {
    CGBuilderTy::InsertPoint SavedIP = Builder.saveAndClearIP();

    EmitBlock(EHEntry);

    llvm::BasicBlock *NextAction = getEHDispatchBlock(EHParent);

    // Push a terminate scope or cleanupendpad scope around the potentially
    // throwing cleanups. For funclet EH personalities, the cleanupendpad models
    // program termination when cleanups throw.
    bool PushedTerminate = false;
    SaveAndRestore RestoreCurrentFuncletPad(CurrentFuncletPad);
    llvm::CleanupPadInst *CPI = nullptr;

    const EHPersonality &Personality = EHPersonality::get(*this);
    if (Personality.usesFuncletPads()) {
      llvm::Value *ParentPad = CurrentFuncletPad;
      if (!ParentPad)
        ParentPad = llvm::ConstantTokenNone::get(CGM.getLLVMContext());
      CurrentFuncletPad = CPI = Builder.CreateCleanupPad(ParentPad);
    }

    // Non-MSVC personalities need to terminate when an EH cleanup throws.
    if (!Personality.isMSVCPersonality()) {
      EHStack.pushTerminate();
      PushedTerminate = true;
    } else if (IsEHa && getInvokeDest()) {
      EmitSehCppScopeEnd();
    }

    // We only actually emit the cleanup code if the cleanup is either
    // active or was used before it was deactivated.
    if (EHActiveFlag.isValid() || IsActive) {
      cleanupFlags.setIsForEHCleanup();
      EmitCleanup(*this, Fn, cleanupFlags, EHActiveFlag);
    }

    if (CPI)
      Builder.CreateCleanupRet(CPI, NextAction);
    else
      Builder.CreateBr(NextAction);

    // Leave the terminate scope.
    if (PushedTerminate)
      EHStack.popTerminate();

    Builder.restoreIP(SavedIP);

    SimplifyCleanupEntry(*this, EHEntry);
  }
}

/// isObviouslyBranchWithoutCleanups - Return true if a branch to the
/// specified destination obviously has no cleanups to run.  'false' is always
/// a conservatively correct answer for this method.
bool CodeGenFunction::isObviouslyBranchWithoutCleanups(JumpDest Dest) const {
  assert(Dest.getScopeDepth().encloses(EHStack.stable_begin())
         && "stale jump destination");

  // Calculate the innermost active normal cleanup.
  EHScopeStack::stable_iterator TopCleanup =
    EHStack.getInnermostActiveNormalCleanup();

  // If we're not in an active normal cleanup scope, or if the
  // destination scope is within the innermost active normal cleanup
  // scope, we don't need to worry about fixups.
  if (TopCleanup == EHStack.stable_end() ||
      TopCleanup.encloses(Dest.getScopeDepth())) // works for invalid
    return true;

  // Otherwise, we might need some cleanups.
  return false;
}


/// Terminate the current block by emitting a branch which might leave
/// the current cleanup-protected scope.  The target scope may not yet
/// be known, in which case this will require a fixup.
///
/// As a side-effect, this method clears the insertion point.
void CodeGenFunction::EmitBranchThroughCleanup(JumpDest Dest) {
  assert(Dest.getScopeDepth().encloses(EHStack.stable_begin())
         && "stale jump destination");

  if (!HaveInsertPoint())
    return;

  // Create the branch.
  llvm::BranchInst *BI = Builder.CreateBr(Dest.getBlock());

  // Calculate the innermost active normal cleanup.
  EHScopeStack::stable_iterator
    TopCleanup = EHStack.getInnermostActiveNormalCleanup();

  // If we're not in an active normal cleanup scope, or if the
  // destination scope is within the innermost active normal cleanup
  // scope, we don't need to worry about fixups.
  if (TopCleanup == EHStack.stable_end() ||
      TopCleanup.encloses(Dest.getScopeDepth())) { // works for invalid
    Builder.ClearInsertionPoint();
    return;
  }

  // If we can't resolve the destination cleanup scope, just add this
  // to the current cleanup scope as a branch fixup.
  if (!Dest.getScopeDepth().isValid()) {
    BranchFixup &Fixup = EHStack.addBranchFixup();
    Fixup.Destination = Dest.getBlock();
    Fixup.DestinationIndex = Dest.getDestIndex();
    Fixup.InitialBranch = BI;
    Fixup.OptimisticBranchBlock = nullptr;

    Builder.ClearInsertionPoint();
    return;
  }

  // Otherwise, thread through all the normal cleanups in scope.

  // Store the index at the start.
  llvm::ConstantInt *Index = Builder.getInt32(Dest.getDestIndex());
  createStoreInstBefore(Index, getNormalCleanupDestSlot(), BI, *this);

  // Adjust BI to point to the first cleanup block.
  {
    EHCleanupScope &Scope =
      cast<EHCleanupScope>(*EHStack.find(TopCleanup));
    BI->setSuccessor(0, CreateNormalEntry(*this, Scope));
  }

  // Add this destination to all the scopes involved.
  EHScopeStack::stable_iterator I = TopCleanup;
  EHScopeStack::stable_iterator E = Dest.getScopeDepth();
  if (E.strictlyEncloses(I)) {
    while (true) {
      EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(I));
      assert(Scope.isNormalCleanup());
      I = Scope.getEnclosingNormalCleanup();

      // If this is the last cleanup we're propagating through, tell it
      // that there's a resolved jump moving through it.
      if (!E.strictlyEncloses(I)) {
        Scope.addBranchAfter(Index, Dest.getBlock());
        break;
      }

      // Otherwise, tell the scope that there's a jump propagating
      // through it.  If this isn't new information, all the rest of
      // the work has been done before.
      if (!Scope.addBranchThrough(Dest.getBlock()))
        break;
    }
  }

  Builder.ClearInsertionPoint();
}

static bool IsUsedAsEHCleanup(EHScopeStack &EHStack,
                              EHScopeStack::stable_iterator cleanup) {
  // If we needed an EH block for any reason, that counts.
  if (EHStack.find(cleanup)->hasEHBranches())
    return true;

  // Check whether any enclosed cleanups were needed.
  for (EHScopeStack::stable_iterator
         i = EHStack.getInnermostEHScope(); i != cleanup; ) {
    assert(cleanup.strictlyEncloses(i));

    EHScope &scope = *EHStack.find(i);
    if (scope.hasEHBranches())
      return true;

    i = scope.getEnclosingEHScope();
  }

  return false;
}

enum ForActivation_t {
  ForActivation,
  ForDeactivation
};

/// The given cleanup block is changing activation state.  Configure a
/// cleanup variable if necessary.
///
/// It would be good if we had some way of determining if there were
/// extra uses *after* the change-over point.
static void SetupCleanupBlockActivation(CodeGenFunction &CGF,
                                        EHScopeStack::stable_iterator C,
                                        ForActivation_t kind,
                                        llvm::Instruction *dominatingIP) {
  EHCleanupScope &Scope = cast<EHCleanupScope>(*CGF.EHStack.find(C));

  // We always need the flag if we're activating the cleanup in a
  // conditional context, because we have to assume that the current
  // location doesn't necessarily dominate the cleanup's code.
  bool isActivatedInConditional =
    (kind == ForActivation && CGF.isInConditionalBranch());

  bool needFlag = false;

  // Calculate whether the cleanup was used:

  //   - as a normal cleanup
  if (Scope.isNormalCleanup()) {
    Scope.setTestFlagInNormalCleanup();
    needFlag = true;
  }

  //  - as an EH cleanup
  if (Scope.isEHCleanup() &&
      (isActivatedInConditional || IsUsedAsEHCleanup(CGF.EHStack, C))) {
    Scope.setTestFlagInEHCleanup();
    needFlag = true;
  }

  // If it hasn't yet been used as either, we're done.
  if (!needFlag)
    return;

  Address var = Scope.getActiveFlag();
  if (!var.isValid()) {
    CodeGenFunction::AllocaTrackerRAII AllocaTracker(CGF);
    var = CGF.CreateTempAlloca(CGF.Builder.getInt1Ty(), CharUnits::One(),
                               "cleanup.isactive");
    Scope.setActiveFlag(var);
    Scope.AddAuxAllocas(AllocaTracker.Take());

    assert(dominatingIP && "no existing variable and no dominating IP!");

    // Initialize to true or false depending on whether it was
    // active up to this point.
    llvm::Constant *value = CGF.Builder.getInt1(kind == ForDeactivation);

    // If we're in a conditional block, ignore the dominating IP and
    // use the outermost conditional branch.
    if (CGF.isInConditionalBranch()) {
      CGF.setBeforeOutermostConditional(value, var, CGF);
    } else {
      createStoreInstBefore(value, var, dominatingIP, CGF);
    }
  }

  CGF.Builder.CreateStore(CGF.Builder.getInt1(kind == ForActivation), var);
}

/// Activate a cleanup that was created in an inactivated state.
void CodeGenFunction::ActivateCleanupBlock(EHScopeStack::stable_iterator C,
                                           llvm::Instruction *dominatingIP) {
  assert(C != EHStack.stable_end() && "activating bottom of stack?");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(C));
  assert(!Scope.isActive() && "double activation");

  SetupCleanupBlockActivation(*this, C, ForActivation, dominatingIP);

  Scope.setActive(true);
}

/// Deactive a cleanup that was created in an active state.
void CodeGenFunction::DeactivateCleanupBlock(EHScopeStack::stable_iterator C,
                                             llvm::Instruction *dominatingIP) {
  assert(C != EHStack.stable_end() && "deactivating bottom of stack?");
  EHCleanupScope &Scope = cast<EHCleanupScope>(*EHStack.find(C));
  assert(Scope.isActive() && "double deactivation");

  // If it's the top of the stack, just pop it, but do so only if it belongs
  // to the current RunCleanupsScope.
  if (C == EHStack.stable_begin() &&
      CurrentCleanupScopeDepth.strictlyEncloses(C)) {
    PopCleanupBlock(/*FallthroughIsBranchThrough=*/false,
                    /*ForDeactivation=*/true);
    return;
  }

  // Otherwise, follow the general case.
  SetupCleanupBlockActivation(*this, C, ForDeactivation, dominatingIP);

  Scope.setActive(false);
}

RawAddress CodeGenFunction::getNormalCleanupDestSlot() {
  if (!NormalCleanupDest.isValid())
    NormalCleanupDest =
      CreateDefaultAlignTempAlloca(Builder.getInt32Ty(), "cleanup.dest.slot");
  return NormalCleanupDest;
}

/// Emits all the code to cause the given temporary to be cleaned up.
void CodeGenFunction::EmitCXXTemporary(const CXXTemporary *Temporary,
                                       QualType TempType,
                                       Address Ptr) {
  pushDestroy(NormalAndEHCleanup, Ptr, TempType, destroyCXXObject,
              /*useEHCleanup*/ true);
}

// Need to set "funclet" in OperandBundle properly for noThrow
//       intrinsic (see CGCall.cpp)
static void EmitSehScope(CodeGenFunction &CGF,
                         llvm::FunctionCallee &SehCppScope) {
  llvm::BasicBlock *InvokeDest = CGF.getInvokeDest();
  assert(CGF.Builder.GetInsertBlock() && InvokeDest);
  llvm::BasicBlock *Cont = CGF.createBasicBlock("invoke.cont");
  SmallVector<llvm::OperandBundleDef, 1> BundleList =
      CGF.getBundlesForFunclet(SehCppScope.getCallee());
  if (CGF.CurrentFuncletPad)
    BundleList.emplace_back("funclet", CGF.CurrentFuncletPad);
  CGF.Builder.CreateInvoke(SehCppScope, Cont, InvokeDest, std::nullopt,
                           BundleList);
  CGF.EmitBlock(Cont);
}

// Invoke a llvm.seh.scope.begin at the beginning of a CPP scope for -EHa
void CodeGenFunction::EmitSehCppScopeBegin() {
  assert(getLangOpts().EHAsynch);
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehCppScope =
      CGM.CreateRuntimeFunction(FTy, "llvm.seh.scope.begin");
  EmitSehScope(*this, SehCppScope);
}

// Invoke a llvm.seh.scope.end at the end of a CPP scope for -EHa
//   llvm.seh.scope.end is emitted before popCleanup, so it's "invoked"
void CodeGenFunction::EmitSehCppScopeEnd() {
  assert(getLangOpts().EHAsynch);
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehCppScope =
      CGM.CreateRuntimeFunction(FTy, "llvm.seh.scope.end");
  EmitSehScope(*this, SehCppScope);
}

// Invoke a llvm.seh.try.begin at the beginning of a SEH scope for -EHa
void CodeGenFunction::EmitSehTryScopeBegin() {
  assert(getLangOpts().EHAsynch);
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehCppScope =
      CGM.CreateRuntimeFunction(FTy, "llvm.seh.try.begin");
  EmitSehScope(*this, SehCppScope);
}

// Invoke a llvm.seh.try.end at the end of a SEH scope for -EHa
void CodeGenFunction::EmitSehTryScopeEnd() {
  assert(getLangOpts().EHAsynch);
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
  llvm::FunctionCallee SehCppScope =
      CGM.CreateRuntimeFunction(FTy, "llvm.seh.try.end");
  EmitSehScope(*this, SehCppScope);
}
