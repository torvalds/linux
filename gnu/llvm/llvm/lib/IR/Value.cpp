//===-- Value.cpp - Implement the Value class -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Value, ValueHandle, and User classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Value.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DerivedUser.h"
#include "llvm/IR/GetElementPtrTypeIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/TypedPointerType.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>

using namespace llvm;

static cl::opt<unsigned> UseDerefAtPointSemantics(
    "use-dereferenceable-at-point-semantics", cl::Hidden, cl::init(false),
    cl::desc("Deref attributes and metadata infer facts at definition only"));

//===----------------------------------------------------------------------===//
//                                Value Class
//===----------------------------------------------------------------------===//
static inline Type *checkType(Type *Ty) {
  assert(Ty && "Value defined with a null type: Error!");
  assert(!isa<TypedPointerType>(Ty->getScalarType()) &&
         "Cannot have values with typed pointer types");
  return Ty;
}

Value::Value(Type *ty, unsigned scid)
    : SubclassID(scid), HasValueHandle(0), SubclassOptionalData(0),
      SubclassData(0), NumUserOperands(0), IsUsedByMD(false), HasName(false),
      HasMetadata(false), VTy(checkType(ty)), UseList(nullptr) {
  static_assert(ConstantFirstVal == 0, "!(SubclassID < ConstantFirstVal)");
  // FIXME: Why isn't this in the subclass gunk??
  // Note, we cannot call isa<CallInst> before the CallInst has been
  // constructed.
  unsigned OpCode = 0;
  if (SubclassID >= InstructionVal)
    OpCode = SubclassID - InstructionVal;
  if (OpCode == Instruction::Call || OpCode == Instruction::Invoke ||
      OpCode == Instruction::CallBr)
    assert((VTy->isFirstClassType() || VTy->isVoidTy() || VTy->isStructTy()) &&
           "invalid CallBase type!");
  else if (SubclassID != BasicBlockVal &&
           (/*SubclassID < ConstantFirstVal ||*/ SubclassID > ConstantLastVal))
    assert((VTy->isFirstClassType() || VTy->isVoidTy()) &&
           "Cannot create non-first-class values except for constants!");
  static_assert(sizeof(Value) == 2 * sizeof(void *) + 2 * sizeof(unsigned),
                "Value too big");
}

Value::~Value() {
  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsDeleted(this);
  if (isUsedByMetadata())
    ValueAsMetadata::handleDeletion(this);

  // Remove associated metadata from context.
  if (HasMetadata)
    clearMetadata();

#ifndef NDEBUG      // Only in -g mode...
  // Check to make sure that there are no uses of this value that are still
  // around when the value is destroyed.  If there are, then we have a dangling
  // reference and something is wrong.  This code is here to print out where
  // the value is still being referenced.
  //
  // Note that use_empty() cannot be called here, as it eventually downcasts
  // 'this' to GlobalValue (derived class of Value), but GlobalValue has already
  // been destructed, so accessing it is UB.
  //
  if (!materialized_use_empty()) {
    dbgs() << "While deleting: " << *VTy << " %" << getName() << "\n";
    for (auto *U : users())
      dbgs() << "Use still stuck around after Def is destroyed:" << *U << "\n";
  }
#endif
  assert(materialized_use_empty() && "Uses remain when a value is destroyed!");

  // If this value is named, destroy the name.  This should not be in a symtab
  // at this point.
  destroyValueName();
}

void Value::deleteValue() {
  switch (getValueID()) {
#define HANDLE_VALUE(Name)                                                     \
  case Value::Name##Val:                                                       \
    delete static_cast<Name *>(this);                                          \
    break;
#define HANDLE_MEMORY_VALUE(Name)                                              \
  case Value::Name##Val:                                                       \
    static_cast<DerivedUser *>(this)->DeleteValue(                             \
        static_cast<DerivedUser *>(this));                                     \
    break;
#define HANDLE_CONSTANT(Name)                                                  \
  case Value::Name##Val:                                                       \
    llvm_unreachable("constants should be destroyed with destroyConstant");    \
    break;
#define HANDLE_INSTRUCTION(Name)  /* nothing */
#include "llvm/IR/Value.def"

#define HANDLE_INST(N, OPC, CLASS)                                             \
  case Value::InstructionVal + Instruction::OPC:                               \
    delete static_cast<CLASS *>(this);                                         \
    break;
#define HANDLE_USER_INST(N, OPC, CLASS)
#include "llvm/IR/Instruction.def"

  default:
    llvm_unreachable("attempting to delete unknown value kind");
  }
}

void Value::destroyValueName() {
  ValueName *Name = getValueName();
  if (Name) {
    MallocAllocator Allocator;
    Name->Destroy(Allocator);
  }
  setValueName(nullptr);
}

bool Value::hasNUses(unsigned N) const {
  return hasNItems(use_begin(), use_end(), N);
}

bool Value::hasNUsesOrMore(unsigned N) const {
  return hasNItemsOrMore(use_begin(), use_end(), N);
}

bool Value::hasOneUser() const {
  if (use_empty())
    return false;
  if (hasOneUse())
    return true;
  return std::equal(++user_begin(), user_end(), user_begin());
}

static bool isUnDroppableUser(const User *U) { return !U->isDroppable(); }

Use *Value::getSingleUndroppableUse() {
  Use *Result = nullptr;
  for (Use &U : uses()) {
    if (!U.getUser()->isDroppable()) {
      if (Result)
        return nullptr;
      Result = &U;
    }
  }
  return Result;
}

User *Value::getUniqueUndroppableUser() {
  User *Result = nullptr;
  for (auto *U : users()) {
    if (!U->isDroppable()) {
      if (Result && Result != U)
        return nullptr;
      Result = U;
    }
  }
  return Result;
}

bool Value::hasNUndroppableUses(unsigned int N) const {
  return hasNItems(user_begin(), user_end(), N, isUnDroppableUser);
}

bool Value::hasNUndroppableUsesOrMore(unsigned int N) const {
  return hasNItemsOrMore(user_begin(), user_end(), N, isUnDroppableUser);
}

void Value::dropDroppableUses(
    llvm::function_ref<bool(const Use *)> ShouldDrop) {
  SmallVector<Use *, 8> ToBeEdited;
  for (Use &U : uses())
    if (U.getUser()->isDroppable() && ShouldDrop(&U))
      ToBeEdited.push_back(&U);
  for (Use *U : ToBeEdited)
    dropDroppableUse(*U);
}

void Value::dropDroppableUsesIn(User &Usr) {
  assert(Usr.isDroppable() && "Expected a droppable user!");
  for (Use &UsrOp : Usr.operands()) {
    if (UsrOp.get() == this)
      dropDroppableUse(UsrOp);
  }
}

void Value::dropDroppableUse(Use &U) {
  U.removeFromList();
  if (auto *Assume = dyn_cast<AssumeInst>(U.getUser())) {
    unsigned OpNo = U.getOperandNo();
    if (OpNo == 0)
      U.set(ConstantInt::getTrue(Assume->getContext()));
    else {
      U.set(UndefValue::get(U.get()->getType()));
      CallInst::BundleOpInfo &BOI = Assume->getBundleOpInfoForOperand(OpNo);
      BOI.Tag = Assume->getContext().pImpl->getOrInsertBundleTag("ignore");
    }
    return;
  }

  llvm_unreachable("unkown droppable use");
}

bool Value::isUsedInBasicBlock(const BasicBlock *BB) const {
  // This can be computed either by scanning the instructions in BB, or by
  // scanning the use list of this Value. Both lists can be very long, but
  // usually one is quite short.
  //
  // Scan both lists simultaneously until one is exhausted. This limits the
  // search to the shorter list.
  BasicBlock::const_iterator BI = BB->begin(), BE = BB->end();
  const_user_iterator UI = user_begin(), UE = user_end();
  for (; BI != BE && UI != UE; ++BI, ++UI) {
    // Scan basic block: Check if this Value is used by the instruction at BI.
    if (is_contained(BI->operands(), this))
      return true;
    // Scan use list: Check if the use at UI is in BB.
    const auto *User = dyn_cast<Instruction>(*UI);
    if (User && User->getParent() == BB)
      return true;
  }
  return false;
}

unsigned Value::getNumUses() const {
  return (unsigned)std::distance(use_begin(), use_end());
}

static bool getSymTab(Value *V, ValueSymbolTable *&ST) {
  ST = nullptr;
  if (Instruction *I = dyn_cast<Instruction>(V)) {
    if (BasicBlock *P = I->getParent())
      if (Function *PP = P->getParent())
        ST = PP->getValueSymbolTable();
  } else if (BasicBlock *BB = dyn_cast<BasicBlock>(V)) {
    if (Function *P = BB->getParent())
      ST = P->getValueSymbolTable();
  } else if (GlobalValue *GV = dyn_cast<GlobalValue>(V)) {
    if (Module *P = GV->getParent())
      ST = &P->getValueSymbolTable();
  } else if (Argument *A = dyn_cast<Argument>(V)) {
    if (Function *P = A->getParent())
      ST = P->getValueSymbolTable();
  } else {
    assert(isa<Constant>(V) && "Unknown value type!");
    return true;  // no name is setable for this.
  }
  return false;
}

ValueName *Value::getValueName() const {
  if (!HasName) return nullptr;

  LLVMContext &Ctx = getContext();
  auto I = Ctx.pImpl->ValueNames.find(this);
  assert(I != Ctx.pImpl->ValueNames.end() &&
         "No name entry found!");

  return I->second;
}

void Value::setValueName(ValueName *VN) {
  LLVMContext &Ctx = getContext();

  assert(HasName == Ctx.pImpl->ValueNames.count(this) &&
         "HasName bit out of sync!");

  if (!VN) {
    if (HasName)
      Ctx.pImpl->ValueNames.erase(this);
    HasName = false;
    return;
  }

  HasName = true;
  Ctx.pImpl->ValueNames[this] = VN;
}

StringRef Value::getName() const {
  // Make sure the empty string is still a C string. For historical reasons,
  // some clients want to call .data() on the result and expect it to be null
  // terminated.
  if (!hasName())
    return StringRef("", 0);
  return getValueName()->getKey();
}

void Value::setNameImpl(const Twine &NewName) {
  bool NeedNewName =
      !getContext().shouldDiscardValueNames() || isa<GlobalValue>(this);

  // Fast-path: LLVMContext can be set to strip out non-GlobalValue names
  // and there is no need to delete the old name.
  if (!NeedNewName && !hasName())
    return;

  // Fast path for common IRBuilder case of setName("") when there is no name.
  if (NewName.isTriviallyEmpty() && !hasName())
    return;

  SmallString<256> NameData;
  StringRef NameRef = NeedNewName ? NewName.toStringRef(NameData) : "";
  assert(!NameRef.contains(0) && "Null bytes are not allowed in names");

  // Name isn't changing?
  if (getName() == NameRef)
    return;

  assert(!getType()->isVoidTy() && "Cannot assign a name to void values!");

  // Get the symbol table to update for this object.
  ValueSymbolTable *ST;
  if (getSymTab(this, ST))
    return;  // Cannot set a name on this value (e.g. constant).

  if (!ST) { // No symbol table to update?  Just do the change.
    // NOTE: Could optimize for the case the name is shrinking to not deallocate
    // then reallocated.
    destroyValueName();

    if (!NameRef.empty()) {
      // Create the new name.
      assert(NeedNewName);
      MallocAllocator Allocator;
      setValueName(ValueName::create(NameRef, Allocator));
      getValueName()->setValue(this);
    }
    return;
  }

  // NOTE: Could optimize for the case the name is shrinking to not deallocate
  // then reallocated.
  if (hasName()) {
    // Remove old name.
    ST->removeValueName(getValueName());
    destroyValueName();

    if (NameRef.empty())
      return;
  }

  // Name is changing to something new.
  assert(NeedNewName);
  setValueName(ST->createValueName(NameRef, this));
}

void Value::setName(const Twine &NewName) {
  setNameImpl(NewName);
  if (Function *F = dyn_cast<Function>(this))
    F->updateAfterNameChange();
}

void Value::takeName(Value *V) {
  assert(V != this && "Illegal call to this->takeName(this)!");
  ValueSymbolTable *ST = nullptr;
  // If this value has a name, drop it.
  if (hasName()) {
    // Get the symtab this is in.
    if (getSymTab(this, ST)) {
      // We can't set a name on this value, but we need to clear V's name if
      // it has one.
      if (V->hasName()) V->setName("");
      return;  // Cannot set a name on this value (e.g. constant).
    }

    // Remove old name.
    if (ST)
      ST->removeValueName(getValueName());
    destroyValueName();
  }

  // Now we know that this has no name.

  // If V has no name either, we're done.
  if (!V->hasName()) return;

  // Get this's symtab if we didn't before.
  if (!ST) {
    if (getSymTab(this, ST)) {
      // Clear V's name.
      V->setName("");
      return;  // Cannot set a name on this value (e.g. constant).
    }
  }

  // Get V's ST, this should always succeed, because V has a name.
  ValueSymbolTable *VST;
  bool Failure = getSymTab(V, VST);
  assert(!Failure && "V has a name, so it should have a ST!"); (void)Failure;

  // If these values are both in the same symtab, we can do this very fast.
  // This works even if both values have no symtab yet.
  if (ST == VST) {
    // Take the name!
    setValueName(V->getValueName());
    V->setValueName(nullptr);
    getValueName()->setValue(this);
    return;
  }

  // Otherwise, things are slightly more complex.  Remove V's name from VST and
  // then reinsert it into ST.

  if (VST)
    VST->removeValueName(V->getValueName());
  setValueName(V->getValueName());
  V->setValueName(nullptr);
  getValueName()->setValue(this);

  if (ST)
    ST->reinsertValue(this);
}

#ifndef NDEBUG
std::string Value::getNameOrAsOperand() const {
  if (!getName().empty())
    return std::string(getName());

  std::string BBName;
  raw_string_ostream OS(BBName);
  printAsOperand(OS, false);
  return OS.str();
}
#endif

void Value::assertModuleIsMaterializedImpl() const {
#ifndef NDEBUG
  const GlobalValue *GV = dyn_cast<GlobalValue>(this);
  if (!GV)
    return;
  const Module *M = GV->getParent();
  if (!M)
    return;
  assert(M->isMaterialized());
#endif
}

#ifndef NDEBUG
static bool contains(SmallPtrSetImpl<ConstantExpr *> &Cache, ConstantExpr *Expr,
                     Constant *C) {
  if (!Cache.insert(Expr).second)
    return false;

  for (auto &O : Expr->operands()) {
    if (O == C)
      return true;
    auto *CE = dyn_cast<ConstantExpr>(O);
    if (!CE)
      continue;
    if (contains(Cache, CE, C))
      return true;
  }
  return false;
}

static bool contains(Value *Expr, Value *V) {
  if (Expr == V)
    return true;

  auto *C = dyn_cast<Constant>(V);
  if (!C)
    return false;

  auto *CE = dyn_cast<ConstantExpr>(Expr);
  if (!CE)
    return false;

  SmallPtrSet<ConstantExpr *, 4> Cache;
  return contains(Cache, CE, C);
}
#endif // NDEBUG

void Value::doRAUW(Value *New, ReplaceMetadataUses ReplaceMetaUses) {
  assert(New && "Value::replaceAllUsesWith(<null>) is invalid!");
  assert(!contains(New, this) &&
         "this->replaceAllUsesWith(expr(this)) is NOT valid!");
  assert(New->getType() == getType() &&
         "replaceAllUses of value with new value of different type!");

  // Notify all ValueHandles (if present) that this value is going away.
  if (HasValueHandle)
    ValueHandleBase::ValueIsRAUWd(this, New);
  if (ReplaceMetaUses == ReplaceMetadataUses::Yes && isUsedByMetadata())
    ValueAsMetadata::handleRAUW(this, New);

  while (!materialized_use_empty()) {
    Use &U = *UseList;
    // Must handle Constants specially, we cannot call replaceUsesOfWith on a
    // constant because they are uniqued.
    if (auto *C = dyn_cast<Constant>(U.getUser())) {
      if (!isa<GlobalValue>(C)) {
        C->handleOperandChange(this, New);
        continue;
      }
    }

    U.set(New);
  }

  if (BasicBlock *BB = dyn_cast<BasicBlock>(this))
    BB->replaceSuccessorsPhiUsesWith(cast<BasicBlock>(New));
}

void Value::replaceAllUsesWith(Value *New) {
  doRAUW(New, ReplaceMetadataUses::Yes);
}

void Value::replaceNonMetadataUsesWith(Value *New) {
  doRAUW(New, ReplaceMetadataUses::No);
}

void Value::replaceUsesWithIf(Value *New,
                              llvm::function_ref<bool(Use &U)> ShouldReplace) {
  assert(New && "Value::replaceUsesWithIf(<null>) is invalid!");
  assert(New->getType() == getType() &&
         "replaceUses of value with new value of different type!");

  SmallVector<TrackingVH<Constant>, 8> Consts;
  SmallPtrSet<Constant *, 8> Visited;

  for (Use &U : llvm::make_early_inc_range(uses())) {
    if (!ShouldReplace(U))
      continue;
    // Must handle Constants specially, we cannot call replaceUsesOfWith on a
    // constant because they are uniqued.
    if (auto *C = dyn_cast<Constant>(U.getUser())) {
      if (!isa<GlobalValue>(C)) {
        if (Visited.insert(C).second)
          Consts.push_back(TrackingVH<Constant>(C));
        continue;
      }
    }
    U.set(New);
  }

  while (!Consts.empty()) {
    // FIXME: handleOperandChange() updates all the uses in a given Constant,
    //        not just the one passed to ShouldReplace
    Consts.pop_back_val()->handleOperandChange(this, New);
  }
}

/// Replace llvm.dbg.* uses of MetadataAsValue(ValueAsMetadata(V)) outside BB
/// with New.
static void replaceDbgUsesOutsideBlock(Value *V, Value *New, BasicBlock *BB) {
  SmallVector<DbgVariableIntrinsic *> DbgUsers;
  SmallVector<DbgVariableRecord *> DPUsers;
  findDbgUsers(DbgUsers, V, &DPUsers);
  for (auto *DVI : DbgUsers) {
    if (DVI->getParent() != BB)
      DVI->replaceVariableLocationOp(V, New);
  }
  for (auto *DVR : DPUsers) {
    DbgMarker *Marker = DVR->getMarker();
    if (Marker->getParent() != BB)
      DVR->replaceVariableLocationOp(V, New);
  }
}

// Like replaceAllUsesWith except it does not handle constants or basic blocks.
// This routine leaves uses within BB.
void Value::replaceUsesOutsideBlock(Value *New, BasicBlock *BB) {
  assert(New && "Value::replaceUsesOutsideBlock(<null>, BB) is invalid!");
  assert(!contains(New, this) &&
         "this->replaceUsesOutsideBlock(expr(this), BB) is NOT valid!");
  assert(New->getType() == getType() &&
         "replaceUses of value with new value of different type!");
  assert(BB && "Basic block that may contain a use of 'New' must be defined\n");

  replaceDbgUsesOutsideBlock(this, New, BB);
  replaceUsesWithIf(New, [BB](Use &U) {
    auto *I = dyn_cast<Instruction>(U.getUser());
    // Don't replace if it's an instruction in the BB basic block.
    return !I || I->getParent() != BB;
  });
}

namespace {
// Various metrics for how much to strip off of pointers.
enum PointerStripKind {
  PSK_ZeroIndices,
  PSK_ZeroIndicesAndAliases,
  PSK_ZeroIndicesSameRepresentation,
  PSK_ForAliasAnalysis,
  PSK_InBoundsConstantIndices,
  PSK_InBounds
};

template <PointerStripKind StripKind> static void NoopCallback(const Value *) {}

template <PointerStripKind StripKind>
static const Value *stripPointerCastsAndOffsets(
    const Value *V,
    function_ref<void(const Value *)> Func = NoopCallback<StripKind>) {
  if (!V->getType()->isPointerTy())
    return V;

  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<const Value *, 4> Visited;

  Visited.insert(V);
  do {
    Func(V);
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      switch (StripKind) {
      case PSK_ZeroIndices:
      case PSK_ZeroIndicesAndAliases:
      case PSK_ZeroIndicesSameRepresentation:
      case PSK_ForAliasAnalysis:
        if (!GEP->hasAllZeroIndices())
          return V;
        break;
      case PSK_InBoundsConstantIndices:
        if (!GEP->hasAllConstantIndices())
          return V;
        [[fallthrough]];
      case PSK_InBounds:
        if (!GEP->isInBounds())
          return V;
        break;
      }
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast) {
      Value *NewV = cast<Operator>(V)->getOperand(0);
      if (!NewV->getType()->isPointerTy())
        return V;
      V = NewV;
    } else if (StripKind != PSK_ZeroIndicesSameRepresentation &&
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      // TODO: If we know an address space cast will not change the
      //       representation we could look through it here as well.
      V = cast<Operator>(V)->getOperand(0);
    } else if (StripKind == PSK_ZeroIndicesAndAliases && isa<GlobalAlias>(V)) {
      V = cast<GlobalAlias>(V)->getAliasee();
    } else if (StripKind == PSK_ForAliasAnalysis && isa<PHINode>(V) &&
               cast<PHINode>(V)->getNumIncomingValues() == 1) {
      V = cast<PHINode>(V)->getIncomingValue(0);
    } else {
      if (const auto *Call = dyn_cast<CallBase>(V)) {
        if (const Value *RV = Call->getReturnedArgOperand()) {
          V = RV;
          continue;
        }
        // The result of launder.invariant.group must alias it's argument,
        // but it can't be marked with returned attribute, that's why it needs
        // special case.
        if (StripKind == PSK_ForAliasAnalysis &&
            (Call->getIntrinsicID() == Intrinsic::launder_invariant_group ||
             Call->getIntrinsicID() == Intrinsic::strip_invariant_group)) {
          V = Call->getArgOperand(0);
          continue;
        }
      }
      return V;
    }
    assert(V->getType()->isPointerTy() && "Unexpected operand type!");
  } while (Visited.insert(V).second);

  return V;
}
} // end anonymous namespace

const Value *Value::stripPointerCasts() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndices>(this);
}

const Value *Value::stripPointerCastsAndAliases() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndicesAndAliases>(this);
}

const Value *Value::stripPointerCastsSameRepresentation() const {
  return stripPointerCastsAndOffsets<PSK_ZeroIndicesSameRepresentation>(this);
}

const Value *Value::stripInBoundsConstantOffsets() const {
  return stripPointerCastsAndOffsets<PSK_InBoundsConstantIndices>(this);
}

const Value *Value::stripPointerCastsForAliasAnalysis() const {
  return stripPointerCastsAndOffsets<PSK_ForAliasAnalysis>(this);
}

const Value *Value::stripAndAccumulateConstantOffsets(
    const DataLayout &DL, APInt &Offset, bool AllowNonInbounds,
    bool AllowInvariantGroup,
    function_ref<bool(Value &, APInt &)> ExternalAnalysis) const {
  if (!getType()->isPtrOrPtrVectorTy())
    return this;

  unsigned BitWidth = Offset.getBitWidth();
  assert(BitWidth == DL.getIndexTypeSizeInBits(getType()) &&
         "The offset bit width does not match the DL specification.");

  // Even though we don't look through PHI nodes, we could be called on an
  // instruction in an unreachable block, which may be on a cycle.
  SmallPtrSet<const Value *, 4> Visited;
  Visited.insert(this);
  const Value *V = this;
  do {
    if (auto *GEP = dyn_cast<GEPOperator>(V)) {
      // If in-bounds was requested, we do not strip non-in-bounds GEPs.
      if (!AllowNonInbounds && !GEP->isInBounds())
        return V;

      // If one of the values we have visited is an addrspacecast, then
      // the pointer type of this GEP may be different from the type
      // of the Ptr parameter which was passed to this function.  This
      // means when we construct GEPOffset, we need to use the size
      // of GEP's pointer type rather than the size of the original
      // pointer type.
      APInt GEPOffset(DL.getIndexTypeSizeInBits(V->getType()), 0);
      if (!GEP->accumulateConstantOffset(DL, GEPOffset, ExternalAnalysis))
        return V;

      // Stop traversal if the pointer offset wouldn't fit in the bit-width
      // provided by the Offset argument. This can happen due to AddrSpaceCast
      // stripping.
      if (GEPOffset.getSignificantBits() > BitWidth)
        return V;

      // External Analysis can return a result higher/lower than the value
      // represents. We need to detect overflow/underflow.
      APInt GEPOffsetST = GEPOffset.sextOrTrunc(BitWidth);
      if (!ExternalAnalysis) {
        Offset += GEPOffsetST;
      } else {
        bool Overflow = false;
        APInt OldOffset = Offset;
        Offset = Offset.sadd_ov(GEPOffsetST, Overflow);
        if (Overflow) {
          Offset = OldOffset;
          return V;
        }
      }
      V = GEP->getPointerOperand();
    } else if (Operator::getOpcode(V) == Instruction::BitCast ||
               Operator::getOpcode(V) == Instruction::AddrSpaceCast) {
      V = cast<Operator>(V)->getOperand(0);
    } else if (auto *GA = dyn_cast<GlobalAlias>(V)) {
      if (!GA->isInterposable())
        V = GA->getAliasee();
    } else if (const auto *Call = dyn_cast<CallBase>(V)) {
        if (const Value *RV = Call->getReturnedArgOperand())
          V = RV;
        if (AllowInvariantGroup && Call->isLaunderOrStripInvariantGroup())
          V = Call->getArgOperand(0);
    }
    assert(V->getType()->isPtrOrPtrVectorTy() && "Unexpected operand type!");
  } while (Visited.insert(V).second);

  return V;
}

const Value *
Value::stripInBoundsOffsets(function_ref<void(const Value *)> Func) const {
  return stripPointerCastsAndOffsets<PSK_InBounds>(this, Func);
}

bool Value::canBeFreed() const {
  assert(getType()->isPointerTy());

  // Cases that can simply never be deallocated
  // *) Constants aren't allocated per se, thus not deallocated either.
  if (isa<Constant>(this))
    return false;

  // Handle byval/byref/sret/inalloca/preallocated arguments.  The storage
  // lifetime is guaranteed to be longer than the callee's lifetime.
  if (auto *A = dyn_cast<Argument>(this)) {
    if (A->hasPointeeInMemoryValueAttr())
      return false;
    // A pointer to an object in a function which neither frees, nor can arrange
    // for another thread to free on its behalf, can not be freed in the scope
    // of the function.  Note that this logic is restricted to memory
    // allocations in existance before the call; a nofree function *is* allowed
    // to free memory it allocated.
    const Function *F = A->getParent();
    if (F->doesNotFreeMemory() && F->hasNoSync())
      return false;
  }

  const Function *F = nullptr;
  if (auto *I = dyn_cast<Instruction>(this))
    F = I->getFunction();
  if (auto *A = dyn_cast<Argument>(this))
    F = A->getParent();

  if (!F)
    return true;

  // With garbage collection, deallocation typically occurs solely at or after
  // safepoints.  If we're compiling for a collector which uses the
  // gc.statepoint infrastructure, safepoints aren't explicitly present
  // in the IR until after lowering from abstract to physical machine model.
  // The collector could chose to mix explicit deallocation and gc'd objects
  // which is why we need the explicit opt in on a per collector basis.
  if (!F->hasGC())
    return true;
  
  const auto &GCName = F->getGC();
  if (GCName == "statepoint-example") {
    auto *PT = cast<PointerType>(this->getType());
    if (PT->getAddressSpace() != 1)
      // For the sake of this example GC, we arbitrarily pick addrspace(1) as
      // our GC managed heap.  This must match the same check in
      // RewriteStatepointsForGC (and probably needs better factored.)
      return true;

    // It is cheaper to scan for a declaration than to scan for a use in this
    // function.  Note that gc.statepoint is a type overloaded function so the
    // usual trick of requesting declaration of the intrinsic from the module
    // doesn't work.
    for (auto &Fn : *F->getParent())
      if (Fn.getIntrinsicID() == Intrinsic::experimental_gc_statepoint)
        return true;
    return false;
  }
  return true;
}

uint64_t Value::getPointerDereferenceableBytes(const DataLayout &DL,
                                               bool &CanBeNull,
                                               bool &CanBeFreed) const {
  assert(getType()->isPointerTy() && "must be pointer");

  uint64_t DerefBytes = 0;
  CanBeNull = false;
  CanBeFreed = UseDerefAtPointSemantics && canBeFreed();
  if (const Argument *A = dyn_cast<Argument>(this)) {
    DerefBytes = A->getDereferenceableBytes();
    if (DerefBytes == 0) {
      // Handle byval/byref/inalloca/preallocated arguments
      if (Type *ArgMemTy = A->getPointeeInMemoryValueType()) {
        if (ArgMemTy->isSized()) {
          // FIXME: Why isn't this the type alloc size?
          DerefBytes = DL.getTypeStoreSize(ArgMemTy).getKnownMinValue();
        }
      }
    }

    if (DerefBytes == 0) {
      DerefBytes = A->getDereferenceableOrNullBytes();
      CanBeNull = true;
    }
  } else if (const auto *Call = dyn_cast<CallBase>(this)) {
    DerefBytes = Call->getRetDereferenceableBytes();
    if (DerefBytes == 0) {
      DerefBytes = Call->getRetDereferenceableOrNullBytes();
      CanBeNull = true;
    }
  } else if (const LoadInst *LI = dyn_cast<LoadInst>(this)) {
    if (MDNode *MD = LI->getMetadata(LLVMContext::MD_dereferenceable)) {
      ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
      DerefBytes = CI->getLimitedValue();
    }
    if (DerefBytes == 0) {
      if (MDNode *MD =
              LI->getMetadata(LLVMContext::MD_dereferenceable_or_null)) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
        DerefBytes = CI->getLimitedValue();
      }
      CanBeNull = true;
    }
  } else if (auto *IP = dyn_cast<IntToPtrInst>(this)) {
    if (MDNode *MD = IP->getMetadata(LLVMContext::MD_dereferenceable)) {
      ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
      DerefBytes = CI->getLimitedValue();
    }
    if (DerefBytes == 0) {
      if (MDNode *MD =
              IP->getMetadata(LLVMContext::MD_dereferenceable_or_null)) {
        ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
        DerefBytes = CI->getLimitedValue();
      }
      CanBeNull = true;
    }
  } else if (auto *AI = dyn_cast<AllocaInst>(this)) {
    if (!AI->isArrayAllocation()) {
      DerefBytes =
          DL.getTypeStoreSize(AI->getAllocatedType()).getKnownMinValue();
      CanBeNull = false;
      CanBeFreed = false;
    }
  } else if (auto *GV = dyn_cast<GlobalVariable>(this)) {
    if (GV->getValueType()->isSized() && !GV->hasExternalWeakLinkage()) {
      // TODO: Don't outright reject hasExternalWeakLinkage but set the
      // CanBeNull flag.
      DerefBytes = DL.getTypeStoreSize(GV->getValueType()).getFixedValue();
      CanBeNull = false;
      CanBeFreed = false;
    }
  }
  return DerefBytes;
}

Align Value::getPointerAlignment(const DataLayout &DL) const {
  assert(getType()->isPointerTy() && "must be pointer");
  if (auto *GO = dyn_cast<GlobalObject>(this)) {
    if (isa<Function>(GO)) {
      Align FunctionPtrAlign = DL.getFunctionPtrAlign().valueOrOne();
      switch (DL.getFunctionPtrAlignType()) {
      case DataLayout::FunctionPtrAlignType::Independent:
        return FunctionPtrAlign;
      case DataLayout::FunctionPtrAlignType::MultipleOfFunctionAlign:
        return std::max(FunctionPtrAlign, GO->getAlign().valueOrOne());
      }
      llvm_unreachable("Unhandled FunctionPtrAlignType");
    }
    const MaybeAlign Alignment(GO->getAlign());
    if (!Alignment) {
      if (auto *GVar = dyn_cast<GlobalVariable>(GO)) {
        Type *ObjectType = GVar->getValueType();
        if (ObjectType->isSized()) {
          // If the object is defined in the current Module, we'll be giving
          // it the preferred alignment. Otherwise, we have to assume that it
          // may only have the minimum ABI alignment.
          if (GVar->isStrongDefinitionForLinker())
            return DL.getPreferredAlign(GVar);
          else
            return DL.getABITypeAlign(ObjectType);
        }
      }
    }
    return Alignment.valueOrOne();
  } else if (const Argument *A = dyn_cast<Argument>(this)) {
    const MaybeAlign Alignment = A->getParamAlign();
    if (!Alignment && A->hasStructRetAttr()) {
      // An sret parameter has at least the ABI alignment of the return type.
      Type *EltTy = A->getParamStructRetType();
      if (EltTy->isSized())
        return DL.getABITypeAlign(EltTy);
    }
    return Alignment.valueOrOne();
  } else if (const AllocaInst *AI = dyn_cast<AllocaInst>(this)) {
    return AI->getAlign();
  } else if (const auto *Call = dyn_cast<CallBase>(this)) {
    MaybeAlign Alignment = Call->getRetAlign();
    if (!Alignment && Call->getCalledFunction())
      Alignment = Call->getCalledFunction()->getAttributes().getRetAlignment();
    return Alignment.valueOrOne();
  } else if (const LoadInst *LI = dyn_cast<LoadInst>(this)) {
    if (MDNode *MD = LI->getMetadata(LLVMContext::MD_align)) {
      ConstantInt *CI = mdconst::extract<ConstantInt>(MD->getOperand(0));
      return Align(CI->getLimitedValue());
    }
  } else if (auto *CstPtr = dyn_cast<Constant>(this)) {
    // Strip pointer casts to avoid creating unnecessary ptrtoint expression
    // if the only "reduction" is combining a bitcast + ptrtoint.
    CstPtr = CstPtr->stripPointerCasts();
    if (auto *CstInt = dyn_cast_or_null<ConstantInt>(ConstantExpr::getPtrToInt(
            const_cast<Constant *>(CstPtr), DL.getIntPtrType(getType()),
            /*OnlyIfReduced=*/true))) {
      size_t TrailingZeros = CstInt->getValue().countr_zero();
      // While the actual alignment may be large, elsewhere we have
      // an arbitrary upper alignmet limit, so let's clamp to it.
      return Align(TrailingZeros < Value::MaxAlignmentExponent
                       ? uint64_t(1) << TrailingZeros
                       : Value::MaximumAlignment);
    }
  }
  return Align(1);
}

static std::optional<int64_t>
getOffsetFromIndex(const GEPOperator *GEP, unsigned Idx, const DataLayout &DL) {
  // Skip over the first indices.
  gep_type_iterator GTI = gep_type_begin(GEP);
  for (unsigned i = 1; i != Idx; ++i, ++GTI)
    /*skip along*/;

  // Compute the offset implied by the rest of the indices.
  int64_t Offset = 0;
  for (unsigned i = Idx, e = GEP->getNumOperands(); i != e; ++i, ++GTI) {
    ConstantInt *OpC = dyn_cast<ConstantInt>(GEP->getOperand(i));
    if (!OpC)
      return std::nullopt;
    if (OpC->isZero())
      continue; // No offset.

    // Handle struct indices, which add their field offset to the pointer.
    if (StructType *STy = GTI.getStructTypeOrNull()) {
      Offset += DL.getStructLayout(STy)->getElementOffset(OpC->getZExtValue());
      continue;
    }

    // Otherwise, we have a sequential type like an array or fixed-length
    // vector. Multiply the index by the ElementSize.
    TypeSize Size = GTI.getSequentialElementStride(DL);
    if (Size.isScalable())
      return std::nullopt;
    Offset += Size.getFixedValue() * OpC->getSExtValue();
  }

  return Offset;
}

std::optional<int64_t> Value::getPointerOffsetFrom(const Value *Other,
                                                   const DataLayout &DL) const {
  const Value *Ptr1 = Other;
  const Value *Ptr2 = this;
  APInt Offset1(DL.getIndexTypeSizeInBits(Ptr1->getType()), 0);
  APInt Offset2(DL.getIndexTypeSizeInBits(Ptr2->getType()), 0);
  Ptr1 = Ptr1->stripAndAccumulateConstantOffsets(DL, Offset1, true);
  Ptr2 = Ptr2->stripAndAccumulateConstantOffsets(DL, Offset2, true);

  // Handle the trivial case first.
  if (Ptr1 == Ptr2)
    return Offset2.getSExtValue() - Offset1.getSExtValue();

  const GEPOperator *GEP1 = dyn_cast<GEPOperator>(Ptr1);
  const GEPOperator *GEP2 = dyn_cast<GEPOperator>(Ptr2);

  // Right now we handle the case when Ptr1/Ptr2 are both GEPs with an identical
  // base.  After that base, they may have some number of common (and
  // potentially variable) indices.  After that they handle some constant
  // offset, which determines their offset from each other.  At this point, we
  // handle no other case.
  if (!GEP1 || !GEP2 || GEP1->getOperand(0) != GEP2->getOperand(0) ||
      GEP1->getSourceElementType() != GEP2->getSourceElementType())
    return std::nullopt;

  // Skip any common indices and track the GEP types.
  unsigned Idx = 1;
  for (; Idx != GEP1->getNumOperands() && Idx != GEP2->getNumOperands(); ++Idx)
    if (GEP1->getOperand(Idx) != GEP2->getOperand(Idx))
      break;

  auto IOffset1 = getOffsetFromIndex(GEP1, Idx, DL);
  auto IOffset2 = getOffsetFromIndex(GEP2, Idx, DL);
  if (!IOffset1 || !IOffset2)
    return std::nullopt;
  return *IOffset2 - *IOffset1 + Offset2.getSExtValue() -
         Offset1.getSExtValue();
}

const Value *Value::DoPHITranslation(const BasicBlock *CurBB,
                                     const BasicBlock *PredBB) const {
  auto *PN = dyn_cast<PHINode>(this);
  if (PN && PN->getParent() == CurBB)
    return PN->getIncomingValueForBlock(PredBB);
  return this;
}

LLVMContext &Value::getContext() const { return VTy->getContext(); }

void Value::reverseUseList() {
  if (!UseList || !UseList->Next)
    // No need to reverse 0 or 1 uses.
    return;

  Use *Head = UseList;
  Use *Current = UseList->Next;
  Head->Next = nullptr;
  while (Current) {
    Use *Next = Current->Next;
    Current->Next = Head;
    Head->Prev = &Current->Next;
    Head = Current;
    Current = Next;
  }
  UseList = Head;
  Head->Prev = &UseList;
}

bool Value::isSwiftError() const {
  auto *Arg = dyn_cast<Argument>(this);
  if (Arg)
    return Arg->hasSwiftErrorAttr();
  auto *Alloca = dyn_cast<AllocaInst>(this);
  if (!Alloca)
    return false;
  return Alloca->isSwiftError();
}

//===----------------------------------------------------------------------===//
//                             ValueHandleBase Class
//===----------------------------------------------------------------------===//

void ValueHandleBase::AddToExistingUseList(ValueHandleBase **List) {
  assert(List && "Handle list is null?");

  // Splice ourselves into the list.
  Next = *List;
  *List = this;
  setPrevPtr(List);
  if (Next) {
    Next->setPrevPtr(&Next);
    assert(getValPtr() == Next->getValPtr() && "Added to wrong list?");
  }
}

void ValueHandleBase::AddToExistingUseListAfter(ValueHandleBase *List) {
  assert(List && "Must insert after existing node");

  Next = List->Next;
  setPrevPtr(&List->Next);
  List->Next = this;
  if (Next)
    Next->setPrevPtr(&Next);
}

void ValueHandleBase::AddToUseList() {
  assert(getValPtr() && "Null pointer doesn't have a use list!");

  LLVMContextImpl *pImpl = getValPtr()->getContext().pImpl;

  if (getValPtr()->HasValueHandle) {
    // If this value already has a ValueHandle, then it must be in the
    // ValueHandles map already.
    ValueHandleBase *&Entry = pImpl->ValueHandles[getValPtr()];
    assert(Entry && "Value doesn't have any handles?");
    AddToExistingUseList(&Entry);
    return;
  }

  // Ok, it doesn't have any handles yet, so we must insert it into the
  // DenseMap.  However, doing this insertion could cause the DenseMap to
  // reallocate itself, which would invalidate all of the PrevP pointers that
  // point into the old table.  Handle this by checking for reallocation and
  // updating the stale pointers only if needed.
  DenseMap<Value*, ValueHandleBase*> &Handles = pImpl->ValueHandles;
  const void *OldBucketPtr = Handles.getPointerIntoBucketsArray();

  ValueHandleBase *&Entry = Handles[getValPtr()];
  assert(!Entry && "Value really did already have handles?");
  AddToExistingUseList(&Entry);
  getValPtr()->HasValueHandle = true;

  // If reallocation didn't happen or if this was the first insertion, don't
  // walk the table.
  if (Handles.isPointerIntoBucketsArray(OldBucketPtr) ||
      Handles.size() == 1) {
    return;
  }

  // Okay, reallocation did happen.  Fix the Prev Pointers.
  for (DenseMap<Value*, ValueHandleBase*>::iterator I = Handles.begin(),
       E = Handles.end(); I != E; ++I) {
    assert(I->second && I->first == I->second->getValPtr() &&
           "List invariant broken!");
    I->second->setPrevPtr(&I->second);
  }
}

void ValueHandleBase::RemoveFromUseList() {
  assert(getValPtr() && getValPtr()->HasValueHandle &&
         "Pointer doesn't have a use list!");

  // Unlink this from its use list.
  ValueHandleBase **PrevPtr = getPrevPtr();
  assert(*PrevPtr == this && "List invariant broken");

  *PrevPtr = Next;
  if (Next) {
    assert(Next->getPrevPtr() == &Next && "List invariant broken");
    Next->setPrevPtr(PrevPtr);
    return;
  }

  // If the Next pointer was null, then it is possible that this was the last
  // ValueHandle watching VP.  If so, delete its entry from the ValueHandles
  // map.
  LLVMContextImpl *pImpl = getValPtr()->getContext().pImpl;
  DenseMap<Value*, ValueHandleBase*> &Handles = pImpl->ValueHandles;
  if (Handles.isPointerIntoBucketsArray(PrevPtr)) {
    Handles.erase(getValPtr());
    getValPtr()->HasValueHandle = false;
  }
}

void ValueHandleBase::ValueIsDeleted(Value *V) {
  assert(V->HasValueHandle && "Should only be called if ValueHandles present");

  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  LLVMContextImpl *pImpl = V->getContext().pImpl;
  ValueHandleBase *Entry = pImpl->ValueHandles[V];
  assert(Entry && "Value bit set but no entries exist");

  // We use a local ValueHandleBase as an iterator so that ValueHandles can add
  // and remove themselves from the list without breaking our iteration.  This
  // is not really an AssertingVH; we just have to give ValueHandleBase a kind.
  // Note that we deliberately do not the support the case when dropping a value
  // handle results in a new value handle being permanently added to the list
  // (as might occur in theory for CallbackVH's): the new value handle will not
  // be processed and the checking code will mete out righteous punishment if
  // the handle is still present once we have finished processing all the other
  // value handles (it is fine to momentarily add then remove a value handle).
  for (ValueHandleBase Iterator(Assert, *Entry); Entry; Entry = Iterator.Next) {
    Iterator.RemoveFromUseList();
    Iterator.AddToExistingUseListAfter(Entry);
    assert(Entry->Next == &Iterator && "Loop invariant broken.");

    switch (Entry->getKind()) {
    case Assert:
      break;
    case Weak:
    case WeakTracking:
      // WeakTracking and Weak just go to null, which unlinks them
      // from the list.
      Entry->operator=(nullptr);
      break;
    case Callback:
      // Forward to the subclass's implementation.
      static_cast<CallbackVH*>(Entry)->deleted();
      break;
    }
  }

  // All callbacks, weak references, and assertingVHs should be dropped by now.
  if (V->HasValueHandle) {
#ifndef NDEBUG      // Only in +Asserts mode...
    dbgs() << "While deleting: " << *V->getType() << " %" << V->getName()
           << "\n";
    if (pImpl->ValueHandles[V]->getKind() == Assert)
      llvm_unreachable("An asserting value handle still pointed to this"
                       " value!");

#endif
    llvm_unreachable("All references to V were not removed?");
  }
}

void ValueHandleBase::ValueIsRAUWd(Value *Old, Value *New) {
  assert(Old->HasValueHandle &&"Should only be called if ValueHandles present");
  assert(Old != New && "Changing value into itself!");
  assert(Old->getType() == New->getType() &&
         "replaceAllUses of value with new value of different type!");

  // Get the linked list base, which is guaranteed to exist since the
  // HasValueHandle flag is set.
  LLVMContextImpl *pImpl = Old->getContext().pImpl;
  ValueHandleBase *Entry = pImpl->ValueHandles[Old];

  assert(Entry && "Value bit set but no entries exist");

  // We use a local ValueHandleBase as an iterator so that
  // ValueHandles can add and remove themselves from the list without
  // breaking our iteration.  This is not really an AssertingVH; we
  // just have to give ValueHandleBase some kind.
  for (ValueHandleBase Iterator(Assert, *Entry); Entry; Entry = Iterator.Next) {
    Iterator.RemoveFromUseList();
    Iterator.AddToExistingUseListAfter(Entry);
    assert(Entry->Next == &Iterator && "Loop invariant broken.");

    switch (Entry->getKind()) {
    case Assert:
    case Weak:
      // Asserting and Weak handles do not follow RAUW implicitly.
      break;
    case WeakTracking:
      // Weak goes to the new value, which will unlink it from Old's list.
      Entry->operator=(New);
      break;
    case Callback:
      // Forward to the subclass's implementation.
      static_cast<CallbackVH*>(Entry)->allUsesReplacedWith(New);
      break;
    }
  }

#ifndef NDEBUG
  // If any new weak value handles were added while processing the
  // list, then complain about it now.
  if (Old->HasValueHandle)
    for (Entry = pImpl->ValueHandles[Old]; Entry; Entry = Entry->Next)
      switch (Entry->getKind()) {
      case WeakTracking:
        dbgs() << "After RAUW from " << *Old->getType() << " %"
               << Old->getName() << " to " << *New->getType() << " %"
               << New->getName() << "\n";
        llvm_unreachable(
            "A weak tracking value handle still pointed to the old value!\n");
      default:
        break;
      }
#endif
}

// Pin the vtable to this file.
void CallbackVH::anchor() {}
