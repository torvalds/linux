//===- Instructions.cpp - Implement the LLVM instructions -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements all of the non-inline methods for the LLVM instruction
// classes.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Instructions.h"
#include "LLVMContextImpl.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CheckedArithmetic.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/TypeSize.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

using namespace llvm;

static cl::opt<bool> DisableI2pP2iOpt(
    "disable-i2p-p2i-opt", cl::init(false),
    cl::desc("Disables inttoptr/ptrtoint roundtrip optimization"));

//===----------------------------------------------------------------------===//
//                            AllocaInst Class
//===----------------------------------------------------------------------===//

std::optional<TypeSize>
AllocaInst::getAllocationSize(const DataLayout &DL) const {
  TypeSize Size = DL.getTypeAllocSize(getAllocatedType());
  if (isArrayAllocation()) {
    auto *C = dyn_cast<ConstantInt>(getArraySize());
    if (!C)
      return std::nullopt;
    assert(!Size.isScalable() && "Array elements cannot have a scalable size");
    auto CheckedProd =
        checkedMulUnsigned(Size.getKnownMinValue(), C->getZExtValue());
    if (!CheckedProd)
      return std::nullopt;
    return TypeSize::getFixed(*CheckedProd);
  }
  return Size;
}

std::optional<TypeSize>
AllocaInst::getAllocationSizeInBits(const DataLayout &DL) const {
  std::optional<TypeSize> Size = getAllocationSize(DL);
  if (!Size)
    return std::nullopt;
  auto CheckedProd = checkedMulUnsigned(Size->getKnownMinValue(),
                                        static_cast<TypeSize::ScalarTy>(8));
  if (!CheckedProd)
    return std::nullopt;
  return TypeSize::get(*CheckedProd, Size->isScalable());
}

//===----------------------------------------------------------------------===//
//                              SelectInst Class
//===----------------------------------------------------------------------===//

/// areInvalidOperands - Return a string if the specified operands are invalid
/// for a select operation, otherwise return null.
const char *SelectInst::areInvalidOperands(Value *Op0, Value *Op1, Value *Op2) {
  if (Op1->getType() != Op2->getType())
    return "both values to select must have same type";

  if (Op1->getType()->isTokenTy())
    return "select values cannot have token type";

  if (VectorType *VT = dyn_cast<VectorType>(Op0->getType())) {
    // Vector select.
    if (VT->getElementType() != Type::getInt1Ty(Op0->getContext()))
      return "vector select condition element type must be i1";
    VectorType *ET = dyn_cast<VectorType>(Op1->getType());
    if (!ET)
      return "selected values for vector select must be vectors";
    if (ET->getElementCount() != VT->getElementCount())
      return "vector select requires selected vectors to have "
                   "the same vector length as select condition";
  } else if (Op0->getType() != Type::getInt1Ty(Op0->getContext())) {
    return "select condition must be i1 or <n x i1>";
  }
  return nullptr;
}

//===----------------------------------------------------------------------===//
//                               PHINode Class
//===----------------------------------------------------------------------===//

PHINode::PHINode(const PHINode &PN)
    : Instruction(PN.getType(), Instruction::PHI, nullptr, PN.getNumOperands()),
      ReservedSpace(PN.getNumOperands()) {
  allocHungoffUses(PN.getNumOperands());
  std::copy(PN.op_begin(), PN.op_end(), op_begin());
  copyIncomingBlocks(make_range(PN.block_begin(), PN.block_end()));
  SubclassOptionalData = PN.SubclassOptionalData;
}

// removeIncomingValue - Remove an incoming value.  This is useful if a
// predecessor basic block is deleted.
Value *PHINode::removeIncomingValue(unsigned Idx, bool DeletePHIIfEmpty) {
  Value *Removed = getIncomingValue(Idx);

  // Move everything after this operand down.
  //
  // FIXME: we could just swap with the end of the list, then erase.  However,
  // clients might not expect this to happen.  The code as it is thrashes the
  // use/def lists, which is kinda lame.
  std::copy(op_begin() + Idx + 1, op_end(), op_begin() + Idx);
  copyIncomingBlocks(drop_begin(blocks(), Idx + 1), Idx);

  // Nuke the last value.
  Op<-1>().set(nullptr);
  setNumHungOffUseOperands(getNumOperands() - 1);

  // If the PHI node is dead, because it has zero entries, nuke it now.
  if (getNumOperands() == 0 && DeletePHIIfEmpty) {
    // If anyone is using this PHI, make them use a dummy value instead...
    replaceAllUsesWith(PoisonValue::get(getType()));
    eraseFromParent();
  }
  return Removed;
}

void PHINode::removeIncomingValueIf(function_ref<bool(unsigned)> Predicate,
                                    bool DeletePHIIfEmpty) {
  SmallDenseSet<unsigned> RemoveIndices;
  for (unsigned Idx = 0; Idx < getNumIncomingValues(); ++Idx)
    if (Predicate(Idx))
      RemoveIndices.insert(Idx);

  if (RemoveIndices.empty())
    return;

  // Remove operands.
  auto NewOpEnd = remove_if(operands(), [&](Use &U) {
    return RemoveIndices.contains(U.getOperandNo());
  });
  for (Use &U : make_range(NewOpEnd, op_end()))
    U.set(nullptr);

  // Remove incoming blocks.
  (void)std::remove_if(const_cast<block_iterator>(block_begin()),
                 const_cast<block_iterator>(block_end()), [&](BasicBlock *&BB) {
                   return RemoveIndices.contains(&BB - block_begin());
                 });

  setNumHungOffUseOperands(getNumOperands() - RemoveIndices.size());

  // If the PHI node is dead, because it has zero entries, nuke it now.
  if (getNumOperands() == 0 && DeletePHIIfEmpty) {
    // If anyone is using this PHI, make them use a dummy value instead...
    replaceAllUsesWith(PoisonValue::get(getType()));
    eraseFromParent();
  }
}

/// growOperands - grow operands - This grows the operand list in response
/// to a push_back style of operation.  This grows the number of ops by 1.5
/// times.
///
void PHINode::growOperands() {
  unsigned e = getNumOperands();
  unsigned NumOps = e + e / 2;
  if (NumOps < 2) NumOps = 2;      // 2 op PHI nodes are VERY common.

  ReservedSpace = NumOps;
  growHungoffUses(ReservedSpace, /* IsPhi */ true);
}

/// hasConstantValue - If the specified PHI node always merges together the same
/// value, return the value, otherwise return null.
Value *PHINode::hasConstantValue() const {
  // Exploit the fact that phi nodes always have at least one entry.
  Value *ConstantValue = getIncomingValue(0);
  for (unsigned i = 1, e = getNumIncomingValues(); i != e; ++i)
    if (getIncomingValue(i) != ConstantValue && getIncomingValue(i) != this) {
      if (ConstantValue != this)
        return nullptr; // Incoming values not all the same.
       // The case where the first value is this PHI.
      ConstantValue = getIncomingValue(i);
    }
  if (ConstantValue == this)
    return PoisonValue::get(getType());
  return ConstantValue;
}

/// hasConstantOrUndefValue - Whether the specified PHI node always merges
/// together the same value, assuming that undefs result in the same value as
/// non-undefs.
/// Unlike \ref hasConstantValue, this does not return a value because the
/// unique non-undef incoming value need not dominate the PHI node.
bool PHINode::hasConstantOrUndefValue() const {
  Value *ConstantValue = nullptr;
  for (unsigned i = 0, e = getNumIncomingValues(); i != e; ++i) {
    Value *Incoming = getIncomingValue(i);
    if (Incoming != this && !isa<UndefValue>(Incoming)) {
      if (ConstantValue && ConstantValue != Incoming)
        return false;
      ConstantValue = Incoming;
    }
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                       LandingPadInst Implementation
//===----------------------------------------------------------------------===//

LandingPadInst::LandingPadInst(Type *RetTy, unsigned NumReservedValues,
                               const Twine &NameStr,
                               InsertPosition InsertBefore)
    : Instruction(RetTy, Instruction::LandingPad, nullptr, 0, InsertBefore) {
  init(NumReservedValues, NameStr);
}

LandingPadInst::LandingPadInst(const LandingPadInst &LP)
    : Instruction(LP.getType(), Instruction::LandingPad, nullptr,
                  LP.getNumOperands()),
      ReservedSpace(LP.getNumOperands()) {
  allocHungoffUses(LP.getNumOperands());
  Use *OL = getOperandList();
  const Use *InOL = LP.getOperandList();
  for (unsigned I = 0, E = ReservedSpace; I != E; ++I)
    OL[I] = InOL[I];

  setCleanup(LP.isCleanup());
}

LandingPadInst *LandingPadInst::Create(Type *RetTy, unsigned NumReservedClauses,
                                       const Twine &NameStr,
                                       InsertPosition InsertBefore) {
  return new LandingPadInst(RetTy, NumReservedClauses, NameStr, InsertBefore);
}

void LandingPadInst::init(unsigned NumReservedValues, const Twine &NameStr) {
  ReservedSpace = NumReservedValues;
  setNumHungOffUseOperands(0);
  allocHungoffUses(ReservedSpace);
  setName(NameStr);
  setCleanup(false);
}

/// growOperands - grow operands - This grows the operand list in response to a
/// push_back style of operation. This grows the number of ops by 2 times.
void LandingPadInst::growOperands(unsigned Size) {
  unsigned e = getNumOperands();
  if (ReservedSpace >= e + Size) return;
  ReservedSpace = (std::max(e, 1U) + Size / 2) * 2;
  growHungoffUses(ReservedSpace);
}

void LandingPadInst::addClause(Constant *Val) {
  unsigned OpNo = getNumOperands();
  growOperands(1);
  assert(OpNo < ReservedSpace && "Growing didn't work!");
  setNumHungOffUseOperands(getNumOperands() + 1);
  getOperandList()[OpNo] = Val;
}

//===----------------------------------------------------------------------===//
//                        CallBase Implementation
//===----------------------------------------------------------------------===//

CallBase *CallBase::Create(CallBase *CB, ArrayRef<OperandBundleDef> Bundles,
                           InsertPosition InsertPt) {
  switch (CB->getOpcode()) {
  case Instruction::Call:
    return CallInst::Create(cast<CallInst>(CB), Bundles, InsertPt);
  case Instruction::Invoke:
    return InvokeInst::Create(cast<InvokeInst>(CB), Bundles, InsertPt);
  case Instruction::CallBr:
    return CallBrInst::Create(cast<CallBrInst>(CB), Bundles, InsertPt);
  default:
    llvm_unreachable("Unknown CallBase sub-class!");
  }
}

CallBase *CallBase::Create(CallBase *CI, OperandBundleDef OpB,
                           InsertPosition InsertPt) {
  SmallVector<OperandBundleDef, 2> OpDefs;
  for (unsigned i = 0, e = CI->getNumOperandBundles(); i < e; ++i) {
    auto ChildOB = CI->getOperandBundleAt(i);
    if (ChildOB.getTagName() != OpB.getTag())
      OpDefs.emplace_back(ChildOB);
  }
  OpDefs.emplace_back(OpB);
  return CallBase::Create(CI, OpDefs, InsertPt);
}

Function *CallBase::getCaller() { return getParent()->getParent(); }

unsigned CallBase::getNumSubclassExtraOperandsDynamic() const {
  assert(getOpcode() == Instruction::CallBr && "Unexpected opcode!");
  return cast<CallBrInst>(this)->getNumIndirectDests() + 1;
}

bool CallBase::isIndirectCall() const {
  const Value *V = getCalledOperand();
  if (isa<Function>(V) || isa<Constant>(V))
    return false;
  return !isInlineAsm();
}

/// Tests if this call site must be tail call optimized. Only a CallInst can
/// be tail call optimized.
bool CallBase::isMustTailCall() const {
  if (auto *CI = dyn_cast<CallInst>(this))
    return CI->isMustTailCall();
  return false;
}

/// Tests if this call site is marked as a tail call.
bool CallBase::isTailCall() const {
  if (auto *CI = dyn_cast<CallInst>(this))
    return CI->isTailCall();
  return false;
}

Intrinsic::ID CallBase::getIntrinsicID() const {
  if (auto *F = getCalledFunction())
    return F->getIntrinsicID();
  return Intrinsic::not_intrinsic;
}

FPClassTest CallBase::getRetNoFPClass() const {
  FPClassTest Mask = Attrs.getRetNoFPClass();

  if (const Function *F = getCalledFunction())
    Mask |= F->getAttributes().getRetNoFPClass();
  return Mask;
}

FPClassTest CallBase::getParamNoFPClass(unsigned i) const {
  FPClassTest Mask = Attrs.getParamNoFPClass(i);

  if (const Function *F = getCalledFunction())
    Mask |= F->getAttributes().getParamNoFPClass(i);
  return Mask;
}

std::optional<ConstantRange> CallBase::getRange() const {
  const Attribute RangeAttr = getRetAttr(llvm::Attribute::Range);
  if (RangeAttr.isValid())
    return RangeAttr.getRange();
  return std::nullopt;
}

bool CallBase::isReturnNonNull() const {
  if (hasRetAttr(Attribute::NonNull))
    return true;

  if (getRetDereferenceableBytes() > 0 &&
      !NullPointerIsDefined(getCaller(), getType()->getPointerAddressSpace()))
    return true;

  return false;
}

Value *CallBase::getArgOperandWithAttribute(Attribute::AttrKind Kind) const {
  unsigned Index;

  if (Attrs.hasAttrSomewhere(Kind, &Index))
    return getArgOperand(Index - AttributeList::FirstArgIndex);
  if (const Function *F = getCalledFunction())
    if (F->getAttributes().hasAttrSomewhere(Kind, &Index))
      return getArgOperand(Index - AttributeList::FirstArgIndex);

  return nullptr;
}

/// Determine whether the argument or parameter has the given attribute.
bool CallBase::paramHasAttr(unsigned ArgNo, Attribute::AttrKind Kind) const {
  assert(ArgNo < arg_size() && "Param index out of bounds!");

  if (Attrs.hasParamAttr(ArgNo, Kind))
    return true;

  const Function *F = getCalledFunction();
  if (!F)
    return false;

  if (!F->getAttributes().hasParamAttr(ArgNo, Kind))
    return false;

  // Take into account mod/ref by operand bundles.
  switch (Kind) {
  case Attribute::ReadNone:
    return !hasReadingOperandBundles() && !hasClobberingOperandBundles();
  case Attribute::ReadOnly:
    return !hasClobberingOperandBundles();
  case Attribute::WriteOnly:
    return !hasReadingOperandBundles();
  default:
    return true;
  }
}

bool CallBase::hasFnAttrOnCalledFunction(Attribute::AttrKind Kind) const {
  if (auto *F = dyn_cast<Function>(getCalledOperand()))
    return F->getAttributes().hasFnAttr(Kind);

  return false;
}

bool CallBase::hasFnAttrOnCalledFunction(StringRef Kind) const {
  if (auto *F = dyn_cast<Function>(getCalledOperand()))
    return F->getAttributes().hasFnAttr(Kind);

  return false;
}

template <typename AK>
Attribute CallBase::getFnAttrOnCalledFunction(AK Kind) const {
  if constexpr (std::is_same_v<AK, Attribute::AttrKind>) {
    // getMemoryEffects() correctly combines memory effects from the call-site,
    // operand bundles and function.
    assert(Kind != Attribute::Memory && "Use getMemoryEffects() instead");
  }

  if (auto *F = dyn_cast<Function>(getCalledOperand()))
    return F->getAttributes().getFnAttr(Kind);

  return Attribute();
}

template Attribute
CallBase::getFnAttrOnCalledFunction(Attribute::AttrKind Kind) const;
template Attribute CallBase::getFnAttrOnCalledFunction(StringRef Kind) const;

template <typename AK>
Attribute CallBase::getParamAttrOnCalledFunction(unsigned ArgNo,
                                                 AK Kind) const {
  Value *V = getCalledOperand();

  if (auto *F = dyn_cast<Function>(V))
    return F->getAttributes().getParamAttr(ArgNo, Kind);

  return Attribute();
}
template Attribute
CallBase::getParamAttrOnCalledFunction(unsigned ArgNo,
                                       Attribute::AttrKind Kind) const;
template Attribute CallBase::getParamAttrOnCalledFunction(unsigned ArgNo,
                                                          StringRef Kind) const;

void CallBase::getOperandBundlesAsDefs(
    SmallVectorImpl<OperandBundleDef> &Defs) const {
  for (unsigned i = 0, e = getNumOperandBundles(); i != e; ++i)
    Defs.emplace_back(getOperandBundleAt(i));
}

CallBase::op_iterator
CallBase::populateBundleOperandInfos(ArrayRef<OperandBundleDef> Bundles,
                                     const unsigned BeginIndex) {
  auto It = op_begin() + BeginIndex;
  for (auto &B : Bundles)
    It = std::copy(B.input_begin(), B.input_end(), It);

  auto *ContextImpl = getContext().pImpl;
  auto BI = Bundles.begin();
  unsigned CurrentIndex = BeginIndex;

  for (auto &BOI : bundle_op_infos()) {
    assert(BI != Bundles.end() && "Incorrect allocation?");

    BOI.Tag = ContextImpl->getOrInsertBundleTag(BI->getTag());
    BOI.Begin = CurrentIndex;
    BOI.End = CurrentIndex + BI->input_size();
    CurrentIndex = BOI.End;
    BI++;
  }

  assert(BI == Bundles.end() && "Incorrect allocation?");

  return It;
}

CallBase::BundleOpInfo &CallBase::getBundleOpInfoForOperand(unsigned OpIdx) {
  /// When there isn't many bundles, we do a simple linear search.
  /// Else fallback to a binary-search that use the fact that bundles usually
  /// have similar number of argument to get faster convergence.
  if (bundle_op_info_end() - bundle_op_info_begin() < 8) {
    for (auto &BOI : bundle_op_infos())
      if (BOI.Begin <= OpIdx && OpIdx < BOI.End)
        return BOI;

    llvm_unreachable("Did not find operand bundle for operand!");
  }

  assert(OpIdx >= arg_size() && "the Idx is not in the operand bundles");
  assert(bundle_op_info_end() - bundle_op_info_begin() > 0 &&
         OpIdx < std::prev(bundle_op_info_end())->End &&
         "The Idx isn't in the operand bundle");

  /// We need a decimal number below and to prevent using floating point numbers
  /// we use an intergal value multiplied by this constant.
  constexpr unsigned NumberScaling = 1024;

  bundle_op_iterator Begin = bundle_op_info_begin();
  bundle_op_iterator End = bundle_op_info_end();
  bundle_op_iterator Current = Begin;

  while (Begin != End) {
    unsigned ScaledOperandPerBundle =
        NumberScaling * (std::prev(End)->End - Begin->Begin) / (End - Begin);
    Current = Begin + (((OpIdx - Begin->Begin) * NumberScaling) /
                       ScaledOperandPerBundle);
    if (Current >= End)
      Current = std::prev(End);
    assert(Current < End && Current >= Begin &&
           "the operand bundle doesn't cover every value in the range");
    if (OpIdx >= Current->Begin && OpIdx < Current->End)
      break;
    if (OpIdx >= Current->End)
      Begin = Current + 1;
    else
      End = Current;
  }

  assert(OpIdx >= Current->Begin && OpIdx < Current->End &&
         "the operand bundle doesn't cover every value in the range");
  return *Current;
}

CallBase *CallBase::addOperandBundle(CallBase *CB, uint32_t ID,
                                     OperandBundleDef OB,
                                     InsertPosition InsertPt) {
  if (CB->getOperandBundle(ID))
    return CB;

  SmallVector<OperandBundleDef, 1> Bundles;
  CB->getOperandBundlesAsDefs(Bundles);
  Bundles.push_back(OB);
  return Create(CB, Bundles, InsertPt);
}

CallBase *CallBase::removeOperandBundle(CallBase *CB, uint32_t ID,
                                        InsertPosition InsertPt) {
  SmallVector<OperandBundleDef, 1> Bundles;
  bool CreateNew = false;

  for (unsigned I = 0, E = CB->getNumOperandBundles(); I != E; ++I) {
    auto Bundle = CB->getOperandBundleAt(I);
    if (Bundle.getTagID() == ID) {
      CreateNew = true;
      continue;
    }
    Bundles.emplace_back(Bundle);
  }

  return CreateNew ? Create(CB, Bundles, InsertPt) : CB;
}

bool CallBase::hasReadingOperandBundles() const {
  // Implementation note: this is a conservative implementation of operand
  // bundle semantics, where *any* non-assume operand bundle (other than
  // ptrauth) forces a callsite to be at least readonly.
  return hasOperandBundlesOtherThan(
             {LLVMContext::OB_ptrauth, LLVMContext::OB_kcfi}) &&
         getIntrinsicID() != Intrinsic::assume;
}

bool CallBase::hasClobberingOperandBundles() const {
  return hasOperandBundlesOtherThan(
             {LLVMContext::OB_deopt, LLVMContext::OB_funclet,
              LLVMContext::OB_ptrauth, LLVMContext::OB_kcfi}) &&
         getIntrinsicID() != Intrinsic::assume;
}

MemoryEffects CallBase::getMemoryEffects() const {
  MemoryEffects ME = getAttributes().getMemoryEffects();
  if (auto *Fn = dyn_cast<Function>(getCalledOperand())) {
    MemoryEffects FnME = Fn->getMemoryEffects();
    if (hasOperandBundles()) {
      // TODO: Add a method to get memory effects for operand bundles instead.
      if (hasReadingOperandBundles())
        FnME |= MemoryEffects::readOnly();
      if (hasClobberingOperandBundles())
        FnME |= MemoryEffects::writeOnly();
    }
    ME &= FnME;
  }
  return ME;
}
void CallBase::setMemoryEffects(MemoryEffects ME) {
  addFnAttr(Attribute::getWithMemoryEffects(getContext(), ME));
}

/// Determine if the function does not access memory.
bool CallBase::doesNotAccessMemory() const {
  return getMemoryEffects().doesNotAccessMemory();
}
void CallBase::setDoesNotAccessMemory() {
  setMemoryEffects(MemoryEffects::none());
}

/// Determine if the function does not access or only reads memory.
bool CallBase::onlyReadsMemory() const {
  return getMemoryEffects().onlyReadsMemory();
}
void CallBase::setOnlyReadsMemory() {
  setMemoryEffects(getMemoryEffects() & MemoryEffects::readOnly());
}

/// Determine if the function does not access or only writes memory.
bool CallBase::onlyWritesMemory() const {
  return getMemoryEffects().onlyWritesMemory();
}
void CallBase::setOnlyWritesMemory() {
  setMemoryEffects(getMemoryEffects() & MemoryEffects::writeOnly());
}

/// Determine if the call can access memmory only using pointers based
/// on its arguments.
bool CallBase::onlyAccessesArgMemory() const {
  return getMemoryEffects().onlyAccessesArgPointees();
}
void CallBase::setOnlyAccessesArgMemory() {
  setMemoryEffects(getMemoryEffects() & MemoryEffects::argMemOnly());
}

/// Determine if the function may only access memory that is
///  inaccessible from the IR.
bool CallBase::onlyAccessesInaccessibleMemory() const {
  return getMemoryEffects().onlyAccessesInaccessibleMem();
}
void CallBase::setOnlyAccessesInaccessibleMemory() {
  setMemoryEffects(getMemoryEffects() & MemoryEffects::inaccessibleMemOnly());
}

/// Determine if the function may only access memory that is
///  either inaccessible from the IR or pointed to by its arguments.
bool CallBase::onlyAccessesInaccessibleMemOrArgMem() const {
  return getMemoryEffects().onlyAccessesInaccessibleOrArgMem();
}
void CallBase::setOnlyAccessesInaccessibleMemOrArgMem() {
  setMemoryEffects(getMemoryEffects() &
                   MemoryEffects::inaccessibleOrArgMemOnly());
}

//===----------------------------------------------------------------------===//
//                        CallInst Implementation
//===----------------------------------------------------------------------===//

void CallInst::init(FunctionType *FTy, Value *Func, ArrayRef<Value *> Args,
                    ArrayRef<OperandBundleDef> Bundles, const Twine &NameStr) {
  this->FTy = FTy;
  assert(getNumOperands() == Args.size() + CountBundleInputs(Bundles) + 1 &&
         "NumOperands not set up?");

#ifndef NDEBUG
  assert((Args.size() == FTy->getNumParams() ||
          (FTy->isVarArg() && Args.size() > FTy->getNumParams())) &&
         "Calling a function with bad signature!");

  for (unsigned i = 0; i != Args.size(); ++i)
    assert((i >= FTy->getNumParams() ||
            FTy->getParamType(i) == Args[i]->getType()) &&
           "Calling a function with a bad signature!");
#endif

  // Set operands in order of their index to match use-list-order
  // prediction.
  llvm::copy(Args, op_begin());
  setCalledOperand(Func);

  auto It = populateBundleOperandInfos(Bundles, Args.size());
  (void)It;
  assert(It + 1 == op_end() && "Should add up!");

  setName(NameStr);
}

void CallInst::init(FunctionType *FTy, Value *Func, const Twine &NameStr) {
  this->FTy = FTy;
  assert(getNumOperands() == 1 && "NumOperands not set up?");
  setCalledOperand(Func);

  assert(FTy->getNumParams() == 0 && "Calling a function with bad signature");

  setName(NameStr);
}

CallInst::CallInst(FunctionType *Ty, Value *Func, const Twine &Name,
                   InsertPosition InsertBefore)
    : CallBase(Ty->getReturnType(), Instruction::Call,
               OperandTraits<CallBase>::op_end(this) - 1, 1, InsertBefore) {
  init(Ty, Func, Name);
}

CallInst::CallInst(const CallInst &CI)
    : CallBase(CI.Attrs, CI.FTy, CI.getType(), Instruction::Call,
               OperandTraits<CallBase>::op_end(this) - CI.getNumOperands(),
               CI.getNumOperands()) {
  setTailCallKind(CI.getTailCallKind());
  setCallingConv(CI.getCallingConv());

  std::copy(CI.op_begin(), CI.op_end(), op_begin());
  std::copy(CI.bundle_op_info_begin(), CI.bundle_op_info_end(),
            bundle_op_info_begin());
  SubclassOptionalData = CI.SubclassOptionalData;
}

CallInst *CallInst::Create(CallInst *CI, ArrayRef<OperandBundleDef> OpB,
                           InsertPosition InsertPt) {
  std::vector<Value *> Args(CI->arg_begin(), CI->arg_end());

  auto *NewCI = CallInst::Create(CI->getFunctionType(), CI->getCalledOperand(),
                                 Args, OpB, CI->getName(), InsertPt);
  NewCI->setTailCallKind(CI->getTailCallKind());
  NewCI->setCallingConv(CI->getCallingConv());
  NewCI->SubclassOptionalData = CI->SubclassOptionalData;
  NewCI->setAttributes(CI->getAttributes());
  NewCI->setDebugLoc(CI->getDebugLoc());
  return NewCI;
}

// Update profile weight for call instruction by scaling it using the ratio
// of S/T. The meaning of "branch_weights" meta data for call instruction is
// transfered to represent call count.
void CallInst::updateProfWeight(uint64_t S, uint64_t T) {
  if (T == 0) {
    LLVM_DEBUG(dbgs() << "Attempting to update profile weights will result in "
                         "div by 0. Ignoring. Likely the function "
                      << getParent()->getParent()->getName()
                      << " has 0 entry count, and contains call instructions "
                         "with non-zero prof info.");
    return;
  }
  scaleProfData(*this, S, T);
}

//===----------------------------------------------------------------------===//
//                        InvokeInst Implementation
//===----------------------------------------------------------------------===//

void InvokeInst::init(FunctionType *FTy, Value *Fn, BasicBlock *IfNormal,
                      BasicBlock *IfException, ArrayRef<Value *> Args,
                      ArrayRef<OperandBundleDef> Bundles,
                      const Twine &NameStr) {
  this->FTy = FTy;

  assert((int)getNumOperands() ==
             ComputeNumOperands(Args.size(), CountBundleInputs(Bundles)) &&
         "NumOperands not set up?");

#ifndef NDEBUG
  assert(((Args.size() == FTy->getNumParams()) ||
          (FTy->isVarArg() && Args.size() > FTy->getNumParams())) &&
         "Invoking a function with bad signature");

  for (unsigned i = 0, e = Args.size(); i != e; i++)
    assert((i >= FTy->getNumParams() ||
            FTy->getParamType(i) == Args[i]->getType()) &&
           "Invoking a function with a bad signature!");
#endif

  // Set operands in order of their index to match use-list-order
  // prediction.
  llvm::copy(Args, op_begin());
  setNormalDest(IfNormal);
  setUnwindDest(IfException);
  setCalledOperand(Fn);

  auto It = populateBundleOperandInfos(Bundles, Args.size());
  (void)It;
  assert(It + 3 == op_end() && "Should add up!");

  setName(NameStr);
}

InvokeInst::InvokeInst(const InvokeInst &II)
    : CallBase(II.Attrs, II.FTy, II.getType(), Instruction::Invoke,
               OperandTraits<CallBase>::op_end(this) - II.getNumOperands(),
               II.getNumOperands()) {
  setCallingConv(II.getCallingConv());
  std::copy(II.op_begin(), II.op_end(), op_begin());
  std::copy(II.bundle_op_info_begin(), II.bundle_op_info_end(),
            bundle_op_info_begin());
  SubclassOptionalData = II.SubclassOptionalData;
}

InvokeInst *InvokeInst::Create(InvokeInst *II, ArrayRef<OperandBundleDef> OpB,
                               InsertPosition InsertPt) {
  std::vector<Value *> Args(II->arg_begin(), II->arg_end());

  auto *NewII = InvokeInst::Create(
      II->getFunctionType(), II->getCalledOperand(), II->getNormalDest(),
      II->getUnwindDest(), Args, OpB, II->getName(), InsertPt);
  NewII->setCallingConv(II->getCallingConv());
  NewII->SubclassOptionalData = II->SubclassOptionalData;
  NewII->setAttributes(II->getAttributes());
  NewII->setDebugLoc(II->getDebugLoc());
  return NewII;
}

LandingPadInst *InvokeInst::getLandingPadInst() const {
  return cast<LandingPadInst>(getUnwindDest()->getFirstNonPHI());
}

void InvokeInst::updateProfWeight(uint64_t S, uint64_t T) {
  if (T == 0) {
    LLVM_DEBUG(dbgs() << "Attempting to update profile weights will result in "
                         "div by 0. Ignoring. Likely the function "
                      << getParent()->getParent()->getName()
                      << " has 0 entry count, and contains call instructions "
                         "with non-zero prof info.");
    return;
  }
  scaleProfData(*this, S, T);
}

//===----------------------------------------------------------------------===//
//                        CallBrInst Implementation
//===----------------------------------------------------------------------===//

void CallBrInst::init(FunctionType *FTy, Value *Fn, BasicBlock *Fallthrough,
                      ArrayRef<BasicBlock *> IndirectDests,
                      ArrayRef<Value *> Args,
                      ArrayRef<OperandBundleDef> Bundles,
                      const Twine &NameStr) {
  this->FTy = FTy;

  assert((int)getNumOperands() ==
             ComputeNumOperands(Args.size(), IndirectDests.size(),
                                CountBundleInputs(Bundles)) &&
         "NumOperands not set up?");

#ifndef NDEBUG
  assert(((Args.size() == FTy->getNumParams()) ||
          (FTy->isVarArg() && Args.size() > FTy->getNumParams())) &&
         "Calling a function with bad signature");

  for (unsigned i = 0, e = Args.size(); i != e; i++)
    assert((i >= FTy->getNumParams() ||
            FTy->getParamType(i) == Args[i]->getType()) &&
           "Calling a function with a bad signature!");
#endif

  // Set operands in order of their index to match use-list-order
  // prediction.
  std::copy(Args.begin(), Args.end(), op_begin());
  NumIndirectDests = IndirectDests.size();
  setDefaultDest(Fallthrough);
  for (unsigned i = 0; i != NumIndirectDests; ++i)
    setIndirectDest(i, IndirectDests[i]);
  setCalledOperand(Fn);

  auto It = populateBundleOperandInfos(Bundles, Args.size());
  (void)It;
  assert(It + 2 + IndirectDests.size() == op_end() && "Should add up!");

  setName(NameStr);
}

CallBrInst::CallBrInst(const CallBrInst &CBI)
    : CallBase(CBI.Attrs, CBI.FTy, CBI.getType(), Instruction::CallBr,
               OperandTraits<CallBase>::op_end(this) - CBI.getNumOperands(),
               CBI.getNumOperands()) {
  setCallingConv(CBI.getCallingConv());
  std::copy(CBI.op_begin(), CBI.op_end(), op_begin());
  std::copy(CBI.bundle_op_info_begin(), CBI.bundle_op_info_end(),
            bundle_op_info_begin());
  SubclassOptionalData = CBI.SubclassOptionalData;
  NumIndirectDests = CBI.NumIndirectDests;
}

CallBrInst *CallBrInst::Create(CallBrInst *CBI, ArrayRef<OperandBundleDef> OpB,
                               InsertPosition InsertPt) {
  std::vector<Value *> Args(CBI->arg_begin(), CBI->arg_end());

  auto *NewCBI = CallBrInst::Create(
      CBI->getFunctionType(), CBI->getCalledOperand(), CBI->getDefaultDest(),
      CBI->getIndirectDests(), Args, OpB, CBI->getName(), InsertPt);
  NewCBI->setCallingConv(CBI->getCallingConv());
  NewCBI->SubclassOptionalData = CBI->SubclassOptionalData;
  NewCBI->setAttributes(CBI->getAttributes());
  NewCBI->setDebugLoc(CBI->getDebugLoc());
  NewCBI->NumIndirectDests = CBI->NumIndirectDests;
  return NewCBI;
}

//===----------------------------------------------------------------------===//
//                        ReturnInst Implementation
//===----------------------------------------------------------------------===//

ReturnInst::ReturnInst(const ReturnInst &RI)
    : Instruction(Type::getVoidTy(RI.getContext()), Instruction::Ret,
                  OperandTraits<ReturnInst>::op_end(this) - RI.getNumOperands(),
                  RI.getNumOperands()) {
  if (RI.getNumOperands())
    Op<0>() = RI.Op<0>();
  SubclassOptionalData = RI.SubclassOptionalData;
}

ReturnInst::ReturnInst(LLVMContext &C, Value *retVal,
                       InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(C), Instruction::Ret,
                  OperandTraits<ReturnInst>::op_end(this) - !!retVal, !!retVal,
                  InsertBefore) {
  if (retVal)
    Op<0>() = retVal;
}

//===----------------------------------------------------------------------===//
//                        ResumeInst Implementation
//===----------------------------------------------------------------------===//

ResumeInst::ResumeInst(const ResumeInst &RI)
    : Instruction(Type::getVoidTy(RI.getContext()), Instruction::Resume,
                  OperandTraits<ResumeInst>::op_begin(this), 1) {
  Op<0>() = RI.Op<0>();
}

ResumeInst::ResumeInst(Value *Exn, InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(Exn->getContext()), Instruction::Resume,
                  OperandTraits<ResumeInst>::op_begin(this), 1, InsertBefore) {
  Op<0>() = Exn;
}

//===----------------------------------------------------------------------===//
//                        CleanupReturnInst Implementation
//===----------------------------------------------------------------------===//

CleanupReturnInst::CleanupReturnInst(const CleanupReturnInst &CRI)
    : Instruction(CRI.getType(), Instruction::CleanupRet,
                  OperandTraits<CleanupReturnInst>::op_end(this) -
                      CRI.getNumOperands(),
                  CRI.getNumOperands()) {
  setSubclassData<Instruction::OpaqueField>(
      CRI.getSubclassData<Instruction::OpaqueField>());
  Op<0>() = CRI.Op<0>();
  if (CRI.hasUnwindDest())
    Op<1>() = CRI.Op<1>();
}

void CleanupReturnInst::init(Value *CleanupPad, BasicBlock *UnwindBB) {
  if (UnwindBB)
    setSubclassData<UnwindDestField>(true);

  Op<0>() = CleanupPad;
  if (UnwindBB)
    Op<1>() = UnwindBB;
}

CleanupReturnInst::CleanupReturnInst(Value *CleanupPad, BasicBlock *UnwindBB,
                                     unsigned Values,
                                     InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(CleanupPad->getContext()),
                  Instruction::CleanupRet,
                  OperandTraits<CleanupReturnInst>::op_end(this) - Values,
                  Values, InsertBefore) {
  init(CleanupPad, UnwindBB);
}

//===----------------------------------------------------------------------===//
//                        CatchReturnInst Implementation
//===----------------------------------------------------------------------===//
void CatchReturnInst::init(Value *CatchPad, BasicBlock *BB) {
  Op<0>() = CatchPad;
  Op<1>() = BB;
}

CatchReturnInst::CatchReturnInst(const CatchReturnInst &CRI)
    : Instruction(Type::getVoidTy(CRI.getContext()), Instruction::CatchRet,
                  OperandTraits<CatchReturnInst>::op_begin(this), 2) {
  Op<0>() = CRI.Op<0>();
  Op<1>() = CRI.Op<1>();
}

CatchReturnInst::CatchReturnInst(Value *CatchPad, BasicBlock *BB,
                                 InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(BB->getContext()), Instruction::CatchRet,
                  OperandTraits<CatchReturnInst>::op_begin(this), 2,
                  InsertBefore) {
  init(CatchPad, BB);
}

//===----------------------------------------------------------------------===//
//                       CatchSwitchInst Implementation
//===----------------------------------------------------------------------===//

CatchSwitchInst::CatchSwitchInst(Value *ParentPad, BasicBlock *UnwindDest,
                                 unsigned NumReservedValues,
                                 const Twine &NameStr,
                                 InsertPosition InsertBefore)
    : Instruction(ParentPad->getType(), Instruction::CatchSwitch, nullptr, 0,
                  InsertBefore) {
  if (UnwindDest)
    ++NumReservedValues;
  init(ParentPad, UnwindDest, NumReservedValues + 1);
  setName(NameStr);
}

CatchSwitchInst::CatchSwitchInst(const CatchSwitchInst &CSI)
    : Instruction(CSI.getType(), Instruction::CatchSwitch, nullptr,
                  CSI.getNumOperands()) {
  init(CSI.getParentPad(), CSI.getUnwindDest(), CSI.getNumOperands());
  setNumHungOffUseOperands(ReservedSpace);
  Use *OL = getOperandList();
  const Use *InOL = CSI.getOperandList();
  for (unsigned I = 1, E = ReservedSpace; I != E; ++I)
    OL[I] = InOL[I];
}

void CatchSwitchInst::init(Value *ParentPad, BasicBlock *UnwindDest,
                           unsigned NumReservedValues) {
  assert(ParentPad && NumReservedValues);

  ReservedSpace = NumReservedValues;
  setNumHungOffUseOperands(UnwindDest ? 2 : 1);
  allocHungoffUses(ReservedSpace);

  Op<0>() = ParentPad;
  if (UnwindDest) {
    setSubclassData<UnwindDestField>(true);
    setUnwindDest(UnwindDest);
  }
}

/// growOperands - grow operands - This grows the operand list in response to a
/// push_back style of operation. This grows the number of ops by 2 times.
void CatchSwitchInst::growOperands(unsigned Size) {
  unsigned NumOperands = getNumOperands();
  assert(NumOperands >= 1);
  if (ReservedSpace >= NumOperands + Size)
    return;
  ReservedSpace = (NumOperands + Size / 2) * 2;
  growHungoffUses(ReservedSpace);
}

void CatchSwitchInst::addHandler(BasicBlock *Handler) {
  unsigned OpNo = getNumOperands();
  growOperands(1);
  assert(OpNo < ReservedSpace && "Growing didn't work!");
  setNumHungOffUseOperands(getNumOperands() + 1);
  getOperandList()[OpNo] = Handler;
}

void CatchSwitchInst::removeHandler(handler_iterator HI) {
  // Move all subsequent handlers up one.
  Use *EndDst = op_end() - 1;
  for (Use *CurDst = HI.getCurrent(); CurDst != EndDst; ++CurDst)
    *CurDst = *(CurDst + 1);
  // Null out the last handler use.
  *EndDst = nullptr;

  setNumHungOffUseOperands(getNumOperands() - 1);
}

//===----------------------------------------------------------------------===//
//                        FuncletPadInst Implementation
//===----------------------------------------------------------------------===//
void FuncletPadInst::init(Value *ParentPad, ArrayRef<Value *> Args,
                          const Twine &NameStr) {
  assert(getNumOperands() == 1 + Args.size() && "NumOperands not set up?");
  llvm::copy(Args, op_begin());
  setParentPad(ParentPad);
  setName(NameStr);
}

FuncletPadInst::FuncletPadInst(const FuncletPadInst &FPI)
    : Instruction(FPI.getType(), FPI.getOpcode(),
                  OperandTraits<FuncletPadInst>::op_end(this) -
                      FPI.getNumOperands(),
                  FPI.getNumOperands()) {
  std::copy(FPI.op_begin(), FPI.op_end(), op_begin());
  setParentPad(FPI.getParentPad());
}

FuncletPadInst::FuncletPadInst(Instruction::FuncletPadOps Op, Value *ParentPad,
                               ArrayRef<Value *> Args, unsigned Values,
                               const Twine &NameStr,
                               InsertPosition InsertBefore)
    : Instruction(ParentPad->getType(), Op,
                  OperandTraits<FuncletPadInst>::op_end(this) - Values, Values,
                  InsertBefore) {
  init(ParentPad, Args, NameStr);
}

//===----------------------------------------------------------------------===//
//                      UnreachableInst Implementation
//===----------------------------------------------------------------------===//

UnreachableInst::UnreachableInst(LLVMContext &Context,
                                 InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(Context), Instruction::Unreachable, nullptr,
                  0, InsertBefore) {}

//===----------------------------------------------------------------------===//
//                        BranchInst Implementation
//===----------------------------------------------------------------------===//

void BranchInst::AssertOK() {
  if (isConditional())
    assert(getCondition()->getType()->isIntegerTy(1) &&
           "May only branch on boolean predicates!");
}

BranchInst::BranchInst(BasicBlock *IfTrue, InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(IfTrue->getContext()), Instruction::Br,
                  OperandTraits<BranchInst>::op_end(this) - 1, 1,
                  InsertBefore) {
  assert(IfTrue && "Branch destination may not be null!");
  Op<-1>() = IfTrue;
}

BranchInst::BranchInst(BasicBlock *IfTrue, BasicBlock *IfFalse, Value *Cond,
                       InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(IfTrue->getContext()), Instruction::Br,
                  OperandTraits<BranchInst>::op_end(this) - 3, 3,
                  InsertBefore) {
  // Assign in order of operand index to make use-list order predictable.
  Op<-3>() = Cond;
  Op<-2>() = IfFalse;
  Op<-1>() = IfTrue;
#ifndef NDEBUG
  AssertOK();
#endif
}

BranchInst::BranchInst(const BranchInst &BI)
    : Instruction(Type::getVoidTy(BI.getContext()), Instruction::Br,
                  OperandTraits<BranchInst>::op_end(this) - BI.getNumOperands(),
                  BI.getNumOperands()) {
  // Assign in order of operand index to make use-list order predictable.
  if (BI.getNumOperands() != 1) {
    assert(BI.getNumOperands() == 3 && "BR can have 1 or 3 operands!");
    Op<-3>() = BI.Op<-3>();
    Op<-2>() = BI.Op<-2>();
  }
  Op<-1>() = BI.Op<-1>();
  SubclassOptionalData = BI.SubclassOptionalData;
}

void BranchInst::swapSuccessors() {
  assert(isConditional() &&
         "Cannot swap successors of an unconditional branch");
  Op<-1>().swap(Op<-2>());

  // Update profile metadata if present and it matches our structural
  // expectations.
  swapProfMetadata();
}

//===----------------------------------------------------------------------===//
//                        AllocaInst Implementation
//===----------------------------------------------------------------------===//

static Value *getAISize(LLVMContext &Context, Value *Amt) {
  if (!Amt)
    Amt = ConstantInt::get(Type::getInt32Ty(Context), 1);
  else {
    assert(!isa<BasicBlock>(Amt) &&
           "Passed basic block into allocation size parameter! Use other ctor");
    assert(Amt->getType()->isIntegerTy() &&
           "Allocation array size is not an integer!");
  }
  return Amt;
}

static Align computeAllocaDefaultAlign(Type *Ty, InsertPosition Pos) {
  assert(Pos.isValid() &&
         "Insertion position cannot be null when alignment not provided!");
  BasicBlock *BB = Pos.getBasicBlock();
  assert(BB->getParent() &&
         "BB must be in a Function when alignment not provided!");
  const DataLayout &DL = BB->getDataLayout();
  return DL.getPrefTypeAlign(Ty);
}

AllocaInst::AllocaInst(Type *Ty, unsigned AddrSpace, const Twine &Name,
                       InsertPosition InsertBefore)
    : AllocaInst(Ty, AddrSpace, /*ArraySize=*/nullptr, Name, InsertBefore) {}

AllocaInst::AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize,
                       const Twine &Name, InsertPosition InsertBefore)
    : AllocaInst(Ty, AddrSpace, ArraySize,
                 computeAllocaDefaultAlign(Ty, InsertBefore), Name,
                 InsertBefore) {}

AllocaInst::AllocaInst(Type *Ty, unsigned AddrSpace, Value *ArraySize,
                       Align Align, const Twine &Name,
                       InsertPosition InsertBefore)
    : UnaryInstruction(PointerType::get(Ty, AddrSpace), Alloca,
                       getAISize(Ty->getContext(), ArraySize), InsertBefore),
      AllocatedType(Ty) {
  setAlignment(Align);
  assert(!Ty->isVoidTy() && "Cannot allocate void!");
  setName(Name);
}

bool AllocaInst::isArrayAllocation() const {
  if (ConstantInt *CI = dyn_cast<ConstantInt>(getOperand(0)))
    return !CI->isOne();
  return true;
}

/// isStaticAlloca - Return true if this alloca is in the entry block of the
/// function and is a constant size.  If so, the code generator will fold it
/// into the prolog/epilog code, so it is basically free.
bool AllocaInst::isStaticAlloca() const {
  // Must be constant size.
  if (!isa<ConstantInt>(getArraySize())) return false;

  // Must be in the entry block.
  const BasicBlock *Parent = getParent();
  return Parent->isEntryBlock() && !isUsedWithInAlloca();
}

//===----------------------------------------------------------------------===//
//                           LoadInst Implementation
//===----------------------------------------------------------------------===//

void LoadInst::AssertOK() {
  assert(getOperand(0)->getType()->isPointerTy() &&
         "Ptr must have pointer type.");
}

static Align computeLoadStoreDefaultAlign(Type *Ty, InsertPosition Pos) {
  assert(Pos.isValid() &&
         "Insertion position cannot be null when alignment not provided!");
  BasicBlock *BB = Pos.getBasicBlock();
  assert(BB->getParent() &&
         "BB must be in a Function when alignment not provided!");
  const DataLayout &DL = BB->getDataLayout();
  return DL.getABITypeAlign(Ty);
}

LoadInst::LoadInst(Type *Ty, Value *Ptr, const Twine &Name,
                   InsertPosition InsertBef)
    : LoadInst(Ty, Ptr, Name, /*isVolatile=*/false, InsertBef) {}

LoadInst::LoadInst(Type *Ty, Value *Ptr, const Twine &Name, bool isVolatile,
                   InsertPosition InsertBef)
    : LoadInst(Ty, Ptr, Name, isVolatile,
               computeLoadStoreDefaultAlign(Ty, InsertBef), InsertBef) {}

LoadInst::LoadInst(Type *Ty, Value *Ptr, const Twine &Name, bool isVolatile,
                   Align Align, InsertPosition InsertBef)
    : LoadInst(Ty, Ptr, Name, isVolatile, Align, AtomicOrdering::NotAtomic,
               SyncScope::System, InsertBef) {}

LoadInst::LoadInst(Type *Ty, Value *Ptr, const Twine &Name, bool isVolatile,
                   Align Align, AtomicOrdering Order, SyncScope::ID SSID,
                   InsertPosition InsertBef)
    : UnaryInstruction(Ty, Load, Ptr, InsertBef) {
  setVolatile(isVolatile);
  setAlignment(Align);
  setAtomic(Order, SSID);
  AssertOK();
  setName(Name);
}

//===----------------------------------------------------------------------===//
//                           StoreInst Implementation
//===----------------------------------------------------------------------===//

void StoreInst::AssertOK() {
  assert(getOperand(0) && getOperand(1) && "Both operands must be non-null!");
  assert(getOperand(1)->getType()->isPointerTy() &&
         "Ptr must have pointer type!");
}

StoreInst::StoreInst(Value *val, Value *addr, InsertPosition InsertBefore)
    : StoreInst(val, addr, /*isVolatile=*/false, InsertBefore) {}

StoreInst::StoreInst(Value *val, Value *addr, bool isVolatile,
                     InsertPosition InsertBefore)
    : StoreInst(val, addr, isVolatile,
                computeLoadStoreDefaultAlign(val->getType(), InsertBefore),
                InsertBefore) {}

StoreInst::StoreInst(Value *val, Value *addr, bool isVolatile, Align Align,
                     InsertPosition InsertBefore)
    : StoreInst(val, addr, isVolatile, Align, AtomicOrdering::NotAtomic,
                SyncScope::System, InsertBefore) {}

StoreInst::StoreInst(Value *val, Value *addr, bool isVolatile, Align Align,
                     AtomicOrdering Order, SyncScope::ID SSID,
                     InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(val->getContext()), Store,
                  OperandTraits<StoreInst>::op_begin(this),
                  OperandTraits<StoreInst>::operands(this), InsertBefore) {
  Op<0>() = val;
  Op<1>() = addr;
  setVolatile(isVolatile);
  setAlignment(Align);
  setAtomic(Order, SSID);
  AssertOK();
}

//===----------------------------------------------------------------------===//
//                       AtomicCmpXchgInst Implementation
//===----------------------------------------------------------------------===//

void AtomicCmpXchgInst::Init(Value *Ptr, Value *Cmp, Value *NewVal,
                             Align Alignment, AtomicOrdering SuccessOrdering,
                             AtomicOrdering FailureOrdering,
                             SyncScope::ID SSID) {
  Op<0>() = Ptr;
  Op<1>() = Cmp;
  Op<2>() = NewVal;
  setSuccessOrdering(SuccessOrdering);
  setFailureOrdering(FailureOrdering);
  setSyncScopeID(SSID);
  setAlignment(Alignment);

  assert(getOperand(0) && getOperand(1) && getOperand(2) &&
         "All operands must be non-null!");
  assert(getOperand(0)->getType()->isPointerTy() &&
         "Ptr must have pointer type!");
  assert(getOperand(1)->getType() == getOperand(2)->getType() &&
         "Cmp type and NewVal type must be same!");
}

AtomicCmpXchgInst::AtomicCmpXchgInst(Value *Ptr, Value *Cmp, Value *NewVal,
                                     Align Alignment,
                                     AtomicOrdering SuccessOrdering,
                                     AtomicOrdering FailureOrdering,
                                     SyncScope::ID SSID,
                                     InsertPosition InsertBefore)
    : Instruction(
          StructType::get(Cmp->getType(), Type::getInt1Ty(Cmp->getContext())),
          AtomicCmpXchg, OperandTraits<AtomicCmpXchgInst>::op_begin(this),
          OperandTraits<AtomicCmpXchgInst>::operands(this), InsertBefore) {
  Init(Ptr, Cmp, NewVal, Alignment, SuccessOrdering, FailureOrdering, SSID);
}

//===----------------------------------------------------------------------===//
//                       AtomicRMWInst Implementation
//===----------------------------------------------------------------------===//

void AtomicRMWInst::Init(BinOp Operation, Value *Ptr, Value *Val,
                         Align Alignment, AtomicOrdering Ordering,
                         SyncScope::ID SSID) {
  assert(Ordering != AtomicOrdering::NotAtomic &&
         "atomicrmw instructions can only be atomic.");
  assert(Ordering != AtomicOrdering::Unordered &&
         "atomicrmw instructions cannot be unordered.");
  Op<0>() = Ptr;
  Op<1>() = Val;
  setOperation(Operation);
  setOrdering(Ordering);
  setSyncScopeID(SSID);
  setAlignment(Alignment);

  assert(getOperand(0) && getOperand(1) && "All operands must be non-null!");
  assert(getOperand(0)->getType()->isPointerTy() &&
         "Ptr must have pointer type!");
  assert(Ordering != AtomicOrdering::NotAtomic &&
         "AtomicRMW instructions must be atomic!");
}

AtomicRMWInst::AtomicRMWInst(BinOp Operation, Value *Ptr, Value *Val,
                             Align Alignment, AtomicOrdering Ordering,
                             SyncScope::ID SSID, InsertPosition InsertBefore)
    : Instruction(Val->getType(), AtomicRMW,
                  OperandTraits<AtomicRMWInst>::op_begin(this),
                  OperandTraits<AtomicRMWInst>::operands(this), InsertBefore) {
  Init(Operation, Ptr, Val, Alignment, Ordering, SSID);
}

StringRef AtomicRMWInst::getOperationName(BinOp Op) {
  switch (Op) {
  case AtomicRMWInst::Xchg:
    return "xchg";
  case AtomicRMWInst::Add:
    return "add";
  case AtomicRMWInst::Sub:
    return "sub";
  case AtomicRMWInst::And:
    return "and";
  case AtomicRMWInst::Nand:
    return "nand";
  case AtomicRMWInst::Or:
    return "or";
  case AtomicRMWInst::Xor:
    return "xor";
  case AtomicRMWInst::Max:
    return "max";
  case AtomicRMWInst::Min:
    return "min";
  case AtomicRMWInst::UMax:
    return "umax";
  case AtomicRMWInst::UMin:
    return "umin";
  case AtomicRMWInst::FAdd:
    return "fadd";
  case AtomicRMWInst::FSub:
    return "fsub";
  case AtomicRMWInst::FMax:
    return "fmax";
  case AtomicRMWInst::FMin:
    return "fmin";
  case AtomicRMWInst::UIncWrap:
    return "uinc_wrap";
  case AtomicRMWInst::UDecWrap:
    return "udec_wrap";
  case AtomicRMWInst::BAD_BINOP:
    return "<invalid operation>";
  }

  llvm_unreachable("invalid atomicrmw operation");
}

//===----------------------------------------------------------------------===//
//                       FenceInst Implementation
//===----------------------------------------------------------------------===//

FenceInst::FenceInst(LLVMContext &C, AtomicOrdering Ordering,
                     SyncScope::ID SSID, InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(C), Fence, nullptr, 0, InsertBefore) {
  setOrdering(Ordering);
  setSyncScopeID(SSID);
}

//===----------------------------------------------------------------------===//
//                       GetElementPtrInst Implementation
//===----------------------------------------------------------------------===//

void GetElementPtrInst::init(Value *Ptr, ArrayRef<Value *> IdxList,
                             const Twine &Name) {
  assert(getNumOperands() == 1 + IdxList.size() &&
         "NumOperands not initialized?");
  Op<0>() = Ptr;
  llvm::copy(IdxList, op_begin() + 1);
  setName(Name);
}

GetElementPtrInst::GetElementPtrInst(const GetElementPtrInst &GEPI)
    : Instruction(GEPI.getType(), GetElementPtr,
                  OperandTraits<GetElementPtrInst>::op_end(this) -
                      GEPI.getNumOperands(),
                  GEPI.getNumOperands()),
      SourceElementType(GEPI.SourceElementType),
      ResultElementType(GEPI.ResultElementType) {
  std::copy(GEPI.op_begin(), GEPI.op_end(), op_begin());
  SubclassOptionalData = GEPI.SubclassOptionalData;
}

Type *GetElementPtrInst::getTypeAtIndex(Type *Ty, Value *Idx) {
  if (auto *Struct = dyn_cast<StructType>(Ty)) {
    if (!Struct->indexValid(Idx))
      return nullptr;
    return Struct->getTypeAtIndex(Idx);
  }
  if (!Idx->getType()->isIntOrIntVectorTy())
    return nullptr;
  if (auto *Array = dyn_cast<ArrayType>(Ty))
    return Array->getElementType();
  if (auto *Vector = dyn_cast<VectorType>(Ty))
    return Vector->getElementType();
  return nullptr;
}

Type *GetElementPtrInst::getTypeAtIndex(Type *Ty, uint64_t Idx) {
  if (auto *Struct = dyn_cast<StructType>(Ty)) {
    if (Idx >= Struct->getNumElements())
      return nullptr;
    return Struct->getElementType(Idx);
  }
  if (auto *Array = dyn_cast<ArrayType>(Ty))
    return Array->getElementType();
  if (auto *Vector = dyn_cast<VectorType>(Ty))
    return Vector->getElementType();
  return nullptr;
}

template <typename IndexTy>
static Type *getIndexedTypeInternal(Type *Ty, ArrayRef<IndexTy> IdxList) {
  if (IdxList.empty())
    return Ty;
  for (IndexTy V : IdxList.slice(1)) {
    Ty = GetElementPtrInst::getTypeAtIndex(Ty, V);
    if (!Ty)
      return Ty;
  }
  return Ty;
}

Type *GetElementPtrInst::getIndexedType(Type *Ty, ArrayRef<Value *> IdxList) {
  return getIndexedTypeInternal(Ty, IdxList);
}

Type *GetElementPtrInst::getIndexedType(Type *Ty,
                                        ArrayRef<Constant *> IdxList) {
  return getIndexedTypeInternal(Ty, IdxList);
}

Type *GetElementPtrInst::getIndexedType(Type *Ty, ArrayRef<uint64_t> IdxList) {
  return getIndexedTypeInternal(Ty, IdxList);
}

/// hasAllZeroIndices - Return true if all of the indices of this GEP are
/// zeros.  If so, the result pointer and the first operand have the same
/// value, just potentially different types.
bool GetElementPtrInst::hasAllZeroIndices() const {
  for (unsigned i = 1, e = getNumOperands(); i != e; ++i) {
    if (ConstantInt *CI = dyn_cast<ConstantInt>(getOperand(i))) {
      if (!CI->isZero()) return false;
    } else {
      return false;
    }
  }
  return true;
}

/// hasAllConstantIndices - Return true if all of the indices of this GEP are
/// constant integers.  If so, the result pointer and the first operand have
/// a constant offset between them.
bool GetElementPtrInst::hasAllConstantIndices() const {
  for (unsigned i = 1, e = getNumOperands(); i != e; ++i) {
    if (!isa<ConstantInt>(getOperand(i)))
      return false;
  }
  return true;
}

void GetElementPtrInst::setNoWrapFlags(GEPNoWrapFlags NW) {
  SubclassOptionalData = NW.getRaw();
}

void GetElementPtrInst::setIsInBounds(bool B) {
  GEPNoWrapFlags NW = cast<GEPOperator>(this)->getNoWrapFlags();
  if (B)
    NW |= GEPNoWrapFlags::inBounds();
  else
    NW = NW.withoutInBounds();
  setNoWrapFlags(NW);
}

GEPNoWrapFlags GetElementPtrInst::getNoWrapFlags() const {
  return cast<GEPOperator>(this)->getNoWrapFlags();
}

bool GetElementPtrInst::isInBounds() const {
  return cast<GEPOperator>(this)->isInBounds();
}

bool GetElementPtrInst::hasNoUnsignedSignedWrap() const {
  return cast<GEPOperator>(this)->hasNoUnsignedSignedWrap();
}

bool GetElementPtrInst::hasNoUnsignedWrap() const {
  return cast<GEPOperator>(this)->hasNoUnsignedWrap();
}

bool GetElementPtrInst::accumulateConstantOffset(const DataLayout &DL,
                                                 APInt &Offset) const {
  // Delegate to the generic GEPOperator implementation.
  return cast<GEPOperator>(this)->accumulateConstantOffset(DL, Offset);
}

bool GetElementPtrInst::collectOffset(
    const DataLayout &DL, unsigned BitWidth,
    MapVector<Value *, APInt> &VariableOffsets,
    APInt &ConstantOffset) const {
  // Delegate to the generic GEPOperator implementation.
  return cast<GEPOperator>(this)->collectOffset(DL, BitWidth, VariableOffsets,
                                                ConstantOffset);
}

//===----------------------------------------------------------------------===//
//                           ExtractElementInst Implementation
//===----------------------------------------------------------------------===//

ExtractElementInst::ExtractElementInst(Value *Val, Value *Index,
                                       const Twine &Name,
                                       InsertPosition InsertBef)
    : Instruction(
          cast<VectorType>(Val->getType())->getElementType(), ExtractElement,
          OperandTraits<ExtractElementInst>::op_begin(this), 2, InsertBef) {
  assert(isValidOperands(Val, Index) &&
         "Invalid extractelement instruction operands!");
  Op<0>() = Val;
  Op<1>() = Index;
  setName(Name);
}

bool ExtractElementInst::isValidOperands(const Value *Val, const Value *Index) {
  if (!Val->getType()->isVectorTy() || !Index->getType()->isIntegerTy())
    return false;
  return true;
}

//===----------------------------------------------------------------------===//
//                           InsertElementInst Implementation
//===----------------------------------------------------------------------===//

InsertElementInst::InsertElementInst(Value *Vec, Value *Elt, Value *Index,
                                     const Twine &Name,
                                     InsertPosition InsertBef)
    : Instruction(Vec->getType(), InsertElement,
                  OperandTraits<InsertElementInst>::op_begin(this), 3,
                  InsertBef) {
  assert(isValidOperands(Vec, Elt, Index) &&
         "Invalid insertelement instruction operands!");
  Op<0>() = Vec;
  Op<1>() = Elt;
  Op<2>() = Index;
  setName(Name);
}

bool InsertElementInst::isValidOperands(const Value *Vec, const Value *Elt,
                                        const Value *Index) {
  if (!Vec->getType()->isVectorTy())
    return false;   // First operand of insertelement must be vector type.

  if (Elt->getType() != cast<VectorType>(Vec->getType())->getElementType())
    return false;// Second operand of insertelement must be vector element type.

  if (!Index->getType()->isIntegerTy())
    return false;  // Third operand of insertelement must be i32.
  return true;
}

//===----------------------------------------------------------------------===//
//                      ShuffleVectorInst Implementation
//===----------------------------------------------------------------------===//

static Value *createPlaceholderForShuffleVector(Value *V) {
  assert(V && "Cannot create placeholder of nullptr V");
  return PoisonValue::get(V->getType());
}

ShuffleVectorInst::ShuffleVectorInst(Value *V1, Value *Mask, const Twine &Name,
                                     InsertPosition InsertBefore)
    : ShuffleVectorInst(V1, createPlaceholderForShuffleVector(V1), Mask, Name,
                        InsertBefore) {}

ShuffleVectorInst::ShuffleVectorInst(Value *V1, ArrayRef<int> Mask,
                                     const Twine &Name,
                                     InsertPosition InsertBefore)
    : ShuffleVectorInst(V1, createPlaceholderForShuffleVector(V1), Mask, Name,
                        InsertBefore) {}

ShuffleVectorInst::ShuffleVectorInst(Value *V1, Value *V2, Value *Mask,
                                     const Twine &Name,
                                     InsertPosition InsertBefore)
    : Instruction(
          VectorType::get(cast<VectorType>(V1->getType())->getElementType(),
                          cast<VectorType>(Mask->getType())->getElementCount()),
          ShuffleVector, OperandTraits<ShuffleVectorInst>::op_begin(this),
          OperandTraits<ShuffleVectorInst>::operands(this), InsertBefore) {
  assert(isValidOperands(V1, V2, Mask) &&
         "Invalid shuffle vector instruction operands!");

  Op<0>() = V1;
  Op<1>() = V2;
  SmallVector<int, 16> MaskArr;
  getShuffleMask(cast<Constant>(Mask), MaskArr);
  setShuffleMask(MaskArr);
  setName(Name);
}

ShuffleVectorInst::ShuffleVectorInst(Value *V1, Value *V2, ArrayRef<int> Mask,
                                     const Twine &Name,
                                     InsertPosition InsertBefore)
    : Instruction(
          VectorType::get(cast<VectorType>(V1->getType())->getElementType(),
                          Mask.size(), isa<ScalableVectorType>(V1->getType())),
          ShuffleVector, OperandTraits<ShuffleVectorInst>::op_begin(this),
          OperandTraits<ShuffleVectorInst>::operands(this), InsertBefore) {
  assert(isValidOperands(V1, V2, Mask) &&
         "Invalid shuffle vector instruction operands!");
  Op<0>() = V1;
  Op<1>() = V2;
  setShuffleMask(Mask);
  setName(Name);
}

void ShuffleVectorInst::commute() {
  int NumOpElts = cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
  int NumMaskElts = ShuffleMask.size();
  SmallVector<int, 16> NewMask(NumMaskElts);
  for (int i = 0; i != NumMaskElts; ++i) {
    int MaskElt = getMaskValue(i);
    if (MaskElt == PoisonMaskElem) {
      NewMask[i] = PoisonMaskElem;
      continue;
    }
    assert(MaskElt >= 0 && MaskElt < 2 * NumOpElts && "Out-of-range mask");
    MaskElt = (MaskElt < NumOpElts) ? MaskElt + NumOpElts : MaskElt - NumOpElts;
    NewMask[i] = MaskElt;
  }
  setShuffleMask(NewMask);
  Op<0>().swap(Op<1>());
}

bool ShuffleVectorInst::isValidOperands(const Value *V1, const Value *V2,
                                        ArrayRef<int> Mask) {
  // V1 and V2 must be vectors of the same type.
  if (!isa<VectorType>(V1->getType()) || V1->getType() != V2->getType())
    return false;

  // Make sure the mask elements make sense.
  int V1Size =
      cast<VectorType>(V1->getType())->getElementCount().getKnownMinValue();
  for (int Elem : Mask)
    if (Elem != PoisonMaskElem && Elem >= V1Size * 2)
      return false;

  if (isa<ScalableVectorType>(V1->getType()))
    if ((Mask[0] != 0 && Mask[0] != PoisonMaskElem) || !all_equal(Mask))
      return false;

  return true;
}

bool ShuffleVectorInst::isValidOperands(const Value *V1, const Value *V2,
                                        const Value *Mask) {
  // V1 and V2 must be vectors of the same type.
  if (!V1->getType()->isVectorTy() || V1->getType() != V2->getType())
    return false;

  // Mask must be vector of i32, and must be the same kind of vector as the
  // input vectors
  auto *MaskTy = dyn_cast<VectorType>(Mask->getType());
  if (!MaskTy || !MaskTy->getElementType()->isIntegerTy(32) ||
      isa<ScalableVectorType>(MaskTy) != isa<ScalableVectorType>(V1->getType()))
    return false;

  // Check to see if Mask is valid.
  if (isa<UndefValue>(Mask) || isa<ConstantAggregateZero>(Mask))
    return true;

  if (const auto *MV = dyn_cast<ConstantVector>(Mask)) {
    unsigned V1Size = cast<FixedVectorType>(V1->getType())->getNumElements();
    for (Value *Op : MV->operands()) {
      if (auto *CI = dyn_cast<ConstantInt>(Op)) {
        if (CI->uge(V1Size*2))
          return false;
      } else if (!isa<UndefValue>(Op)) {
        return false;
      }
    }
    return true;
  }

  if (const auto *CDS = dyn_cast<ConstantDataSequential>(Mask)) {
    unsigned V1Size = cast<FixedVectorType>(V1->getType())->getNumElements();
    for (unsigned i = 0, e = cast<FixedVectorType>(MaskTy)->getNumElements();
         i != e; ++i)
      if (CDS->getElementAsInteger(i) >= V1Size*2)
        return false;
    return true;
  }

  return false;
}

void ShuffleVectorInst::getShuffleMask(const Constant *Mask,
                                       SmallVectorImpl<int> &Result) {
  ElementCount EC = cast<VectorType>(Mask->getType())->getElementCount();

  if (isa<ConstantAggregateZero>(Mask)) {
    Result.resize(EC.getKnownMinValue(), 0);
    return;
  }

  Result.reserve(EC.getKnownMinValue());

  if (EC.isScalable()) {
    assert((isa<ConstantAggregateZero>(Mask) || isa<UndefValue>(Mask)) &&
           "Scalable vector shuffle mask must be undef or zeroinitializer");
    int MaskVal = isa<UndefValue>(Mask) ? -1 : 0;
    for (unsigned I = 0; I < EC.getKnownMinValue(); ++I)
      Result.emplace_back(MaskVal);
    return;
  }

  unsigned NumElts = EC.getKnownMinValue();

  if (auto *CDS = dyn_cast<ConstantDataSequential>(Mask)) {
    for (unsigned i = 0; i != NumElts; ++i)
      Result.push_back(CDS->getElementAsInteger(i));
    return;
  }
  for (unsigned i = 0; i != NumElts; ++i) {
    Constant *C = Mask->getAggregateElement(i);
    Result.push_back(isa<UndefValue>(C) ? -1 :
                     cast<ConstantInt>(C)->getZExtValue());
  }
}

void ShuffleVectorInst::setShuffleMask(ArrayRef<int> Mask) {
  ShuffleMask.assign(Mask.begin(), Mask.end());
  ShuffleMaskForBitcode = convertShuffleMaskForBitcode(Mask, getType());
}

Constant *ShuffleVectorInst::convertShuffleMaskForBitcode(ArrayRef<int> Mask,
                                                          Type *ResultTy) {
  Type *Int32Ty = Type::getInt32Ty(ResultTy->getContext());
  if (isa<ScalableVectorType>(ResultTy)) {
    assert(all_equal(Mask) && "Unexpected shuffle");
    Type *VecTy = VectorType::get(Int32Ty, Mask.size(), true);
    if (Mask[0] == 0)
      return Constant::getNullValue(VecTy);
    return PoisonValue::get(VecTy);
  }
  SmallVector<Constant *, 16> MaskConst;
  for (int Elem : Mask) {
    if (Elem == PoisonMaskElem)
      MaskConst.push_back(PoisonValue::get(Int32Ty));
    else
      MaskConst.push_back(ConstantInt::get(Int32Ty, Elem));
  }
  return ConstantVector::get(MaskConst);
}

static bool isSingleSourceMaskImpl(ArrayRef<int> Mask, int NumOpElts) {
  assert(!Mask.empty() && "Shuffle mask must contain elements");
  bool UsesLHS = false;
  bool UsesRHS = false;
  for (int I : Mask) {
    if (I == -1)
      continue;
    assert(I >= 0 && I < (NumOpElts * 2) &&
           "Out-of-bounds shuffle mask element");
    UsesLHS |= (I < NumOpElts);
    UsesRHS |= (I >= NumOpElts);
    if (UsesLHS && UsesRHS)
      return false;
  }
  // Allow for degenerate case: completely undef mask means neither source is used.
  return UsesLHS || UsesRHS;
}

bool ShuffleVectorInst::isSingleSourceMask(ArrayRef<int> Mask, int NumSrcElts) {
  // We don't have vector operand size information, so assume operands are the
  // same size as the mask.
  return isSingleSourceMaskImpl(Mask, NumSrcElts);
}

static bool isIdentityMaskImpl(ArrayRef<int> Mask, int NumOpElts) {
  if (!isSingleSourceMaskImpl(Mask, NumOpElts))
    return false;
  for (int i = 0, NumMaskElts = Mask.size(); i < NumMaskElts; ++i) {
    if (Mask[i] == -1)
      continue;
    if (Mask[i] != i && Mask[i] != (NumOpElts + i))
      return false;
  }
  return true;
}

bool ShuffleVectorInst::isIdentityMask(ArrayRef<int> Mask, int NumSrcElts) {
  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  // We don't have vector operand size information, so assume operands are the
  // same size as the mask.
  return isIdentityMaskImpl(Mask, NumSrcElts);
}

bool ShuffleVectorInst::isReverseMask(ArrayRef<int> Mask, int NumSrcElts) {
  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  if (!isSingleSourceMask(Mask, NumSrcElts))
    return false;

  // The number of elements in the mask must be at least 2.
  if (NumSrcElts < 2)
    return false;

  for (int I = 0, E = Mask.size(); I < E; ++I) {
    if (Mask[I] == -1)
      continue;
    if (Mask[I] != (NumSrcElts - 1 - I) &&
        Mask[I] != (NumSrcElts + NumSrcElts - 1 - I))
      return false;
  }
  return true;
}

bool ShuffleVectorInst::isZeroEltSplatMask(ArrayRef<int> Mask, int NumSrcElts) {
  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  if (!isSingleSourceMask(Mask, NumSrcElts))
    return false;
  for (int I = 0, E = Mask.size(); I < E; ++I) {
    if (Mask[I] == -1)
      continue;
    if (Mask[I] != 0 && Mask[I] != NumSrcElts)
      return false;
  }
  return true;
}

bool ShuffleVectorInst::isSelectMask(ArrayRef<int> Mask, int NumSrcElts) {
  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  // Select is differentiated from identity. It requires using both sources.
  if (isSingleSourceMask(Mask, NumSrcElts))
    return false;
  for (int I = 0, E = Mask.size(); I < E; ++I) {
    if (Mask[I] == -1)
      continue;
    if (Mask[I] != I && Mask[I] != (NumSrcElts + I))
      return false;
  }
  return true;
}

bool ShuffleVectorInst::isTransposeMask(ArrayRef<int> Mask, int NumSrcElts) {
  // Example masks that will return true:
  // v1 = <a, b, c, d>
  // v2 = <e, f, g, h>
  // trn1 = shufflevector v1, v2 <0, 4, 2, 6> = <a, e, c, g>
  // trn2 = shufflevector v1, v2 <1, 5, 3, 7> = <b, f, d, h>

  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  // 1. The number of elements in the mask must be a power-of-2 and at least 2.
  int Sz = Mask.size();
  if (Sz < 2 || !isPowerOf2_32(Sz))
    return false;

  // 2. The first element of the mask must be either a 0 or a 1.
  if (Mask[0] != 0 && Mask[0] != 1)
    return false;

  // 3. The difference between the first 2 elements must be equal to the
  // number of elements in the mask.
  if ((Mask[1] - Mask[0]) != NumSrcElts)
    return false;

  // 4. The difference between consecutive even-numbered and odd-numbered
  // elements must be equal to 2.
  for (int I = 2; I < Sz; ++I) {
    int MaskEltVal = Mask[I];
    if (MaskEltVal == -1)
      return false;
    int MaskEltPrevVal = Mask[I - 2];
    if (MaskEltVal - MaskEltPrevVal != 2)
      return false;
  }
  return true;
}

bool ShuffleVectorInst::isSpliceMask(ArrayRef<int> Mask, int NumSrcElts,
                                     int &Index) {
  if (Mask.size() != static_cast<unsigned>(NumSrcElts))
    return false;
  // Example: shufflevector <4 x n> A, <4 x n> B, <1,2,3,4>
  int StartIndex = -1;
  for (int I = 0, E = Mask.size(); I != E; ++I) {
    int MaskEltVal = Mask[I];
    if (MaskEltVal == -1)
      continue;

    if (StartIndex == -1) {
      // Don't support a StartIndex that begins in the second input, or if the
      // first non-undef index would access below the StartIndex.
      if (MaskEltVal < I || NumSrcElts <= (MaskEltVal - I))
        return false;

      StartIndex = MaskEltVal - I;
      continue;
    }

    // Splice is sequential starting from StartIndex.
    if (MaskEltVal != (StartIndex + I))
      return false;
  }

  if (StartIndex == -1)
    return false;

  // NOTE: This accepts StartIndex == 0 (COPY).
  Index = StartIndex;
  return true;
}

bool ShuffleVectorInst::isExtractSubvectorMask(ArrayRef<int> Mask,
                                               int NumSrcElts, int &Index) {
  // Must extract from a single source.
  if (!isSingleSourceMaskImpl(Mask, NumSrcElts))
    return false;

  // Must be smaller (else this is an Identity shuffle).
  if (NumSrcElts <= (int)Mask.size())
    return false;

  // Find start of extraction, accounting that we may start with an UNDEF.
  int SubIndex = -1;
  for (int i = 0, e = Mask.size(); i != e; ++i) {
    int M = Mask[i];
    if (M < 0)
      continue;
    int Offset = (M % NumSrcElts) - i;
    if (0 <= SubIndex && SubIndex != Offset)
      return false;
    SubIndex = Offset;
  }

  if (0 <= SubIndex && SubIndex + (int)Mask.size() <= NumSrcElts) {
    Index = SubIndex;
    return true;
  }
  return false;
}

bool ShuffleVectorInst::isInsertSubvectorMask(ArrayRef<int> Mask,
                                              int NumSrcElts, int &NumSubElts,
                                              int &Index) {
  int NumMaskElts = Mask.size();

  // Don't try to match if we're shuffling to a smaller size.
  if (NumMaskElts < NumSrcElts)
    return false;

  // TODO: We don't recognize self-insertion/widening.
  if (isSingleSourceMaskImpl(Mask, NumSrcElts))
    return false;

  // Determine which mask elements are attributed to which source.
  APInt UndefElts = APInt::getZero(NumMaskElts);
  APInt Src0Elts = APInt::getZero(NumMaskElts);
  APInt Src1Elts = APInt::getZero(NumMaskElts);
  bool Src0Identity = true;
  bool Src1Identity = true;

  for (int i = 0; i != NumMaskElts; ++i) {
    int M = Mask[i];
    if (M < 0) {
      UndefElts.setBit(i);
      continue;
    }
    if (M < NumSrcElts) {
      Src0Elts.setBit(i);
      Src0Identity &= (M == i);
      continue;
    }
    Src1Elts.setBit(i);
    Src1Identity &= (M == (i + NumSrcElts));
  }
  assert((Src0Elts | Src1Elts | UndefElts).isAllOnes() &&
         "unknown shuffle elements");
  assert(!Src0Elts.isZero() && !Src1Elts.isZero() &&
         "2-source shuffle not found");

  // Determine lo/hi span ranges.
  // TODO: How should we handle undefs at the start of subvector insertions?
  int Src0Lo = Src0Elts.countr_zero();
  int Src1Lo = Src1Elts.countr_zero();
  int Src0Hi = NumMaskElts - Src0Elts.countl_zero();
  int Src1Hi = NumMaskElts - Src1Elts.countl_zero();

  // If src0 is in place, see if the src1 elements is inplace within its own
  // span.
  if (Src0Identity) {
    int NumSub1Elts = Src1Hi - Src1Lo;
    ArrayRef<int> Sub1Mask = Mask.slice(Src1Lo, NumSub1Elts);
    if (isIdentityMaskImpl(Sub1Mask, NumSrcElts)) {
      NumSubElts = NumSub1Elts;
      Index = Src1Lo;
      return true;
    }
  }

  // If src1 is in place, see if the src0 elements is inplace within its own
  // span.
  if (Src1Identity) {
    int NumSub0Elts = Src0Hi - Src0Lo;
    ArrayRef<int> Sub0Mask = Mask.slice(Src0Lo, NumSub0Elts);
    if (isIdentityMaskImpl(Sub0Mask, NumSrcElts)) {
      NumSubElts = NumSub0Elts;
      Index = Src0Lo;
      return true;
    }
  }

  return false;
}

bool ShuffleVectorInst::isIdentityWithPadding() const {
  // FIXME: Not currently possible to express a shuffle mask for a scalable
  // vector for this case.
  if (isa<ScalableVectorType>(getType()))
    return false;

  int NumOpElts = cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
  int NumMaskElts = cast<FixedVectorType>(getType())->getNumElements();
  if (NumMaskElts <= NumOpElts)
    return false;

  // The first part of the mask must choose elements from exactly 1 source op.
  ArrayRef<int> Mask = getShuffleMask();
  if (!isIdentityMaskImpl(Mask, NumOpElts))
    return false;

  // All extending must be with undef elements.
  for (int i = NumOpElts; i < NumMaskElts; ++i)
    if (Mask[i] != -1)
      return false;

  return true;
}

bool ShuffleVectorInst::isIdentityWithExtract() const {
  // FIXME: Not currently possible to express a shuffle mask for a scalable
  // vector for this case.
  if (isa<ScalableVectorType>(getType()))
    return false;

  int NumOpElts = cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
  int NumMaskElts = cast<FixedVectorType>(getType())->getNumElements();
  if (NumMaskElts >= NumOpElts)
    return false;

  return isIdentityMaskImpl(getShuffleMask(), NumOpElts);
}

bool ShuffleVectorInst::isConcat() const {
  // Vector concatenation is differentiated from identity with padding.
  if (isa<UndefValue>(Op<0>()) || isa<UndefValue>(Op<1>()))
    return false;

  // FIXME: Not currently possible to express a shuffle mask for a scalable
  // vector for this case.
  if (isa<ScalableVectorType>(getType()))
    return false;

  int NumOpElts = cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
  int NumMaskElts = cast<FixedVectorType>(getType())->getNumElements();
  if (NumMaskElts != NumOpElts * 2)
    return false;

  // Use the mask length rather than the operands' vector lengths here. We
  // already know that the shuffle returns a vector twice as long as the inputs,
  // and neither of the inputs are undef vectors. If the mask picks consecutive
  // elements from both inputs, then this is a concatenation of the inputs.
  return isIdentityMaskImpl(getShuffleMask(), NumMaskElts);
}

static bool isReplicationMaskWithParams(ArrayRef<int> Mask,
                                        int ReplicationFactor, int VF) {
  assert(Mask.size() == (unsigned)ReplicationFactor * VF &&
         "Unexpected mask size.");

  for (int CurrElt : seq(VF)) {
    ArrayRef<int> CurrSubMask = Mask.take_front(ReplicationFactor);
    assert(CurrSubMask.size() == (unsigned)ReplicationFactor &&
           "Run out of mask?");
    Mask = Mask.drop_front(ReplicationFactor);
    if (!all_of(CurrSubMask, [CurrElt](int MaskElt) {
          return MaskElt == PoisonMaskElem || MaskElt == CurrElt;
        }))
      return false;
  }
  assert(Mask.empty() && "Did not consume the whole mask?");

  return true;
}

bool ShuffleVectorInst::isReplicationMask(ArrayRef<int> Mask,
                                          int &ReplicationFactor, int &VF) {
  // undef-less case is trivial.
  if (!llvm::is_contained(Mask, PoisonMaskElem)) {
    ReplicationFactor =
        Mask.take_while([](int MaskElt) { return MaskElt == 0; }).size();
    if (ReplicationFactor == 0 || Mask.size() % ReplicationFactor != 0)
      return false;
    VF = Mask.size() / ReplicationFactor;
    return isReplicationMaskWithParams(Mask, ReplicationFactor, VF);
  }

  // However, if the mask contains undef's, we have to enumerate possible tuples
  // and pick one. There are bounds on replication factor: [1, mask size]
  // (where RF=1 is an identity shuffle, RF=mask size is a broadcast shuffle)
  // Additionally, mask size is a replication factor multiplied by vector size,
  // which further significantly reduces the search space.

  // Before doing that, let's perform basic correctness checking first.
  int Largest = -1;
  for (int MaskElt : Mask) {
    if (MaskElt == PoisonMaskElem)
      continue;
    // Elements must be in non-decreasing order.
    if (MaskElt < Largest)
      return false;
    Largest = std::max(Largest, MaskElt);
  }

  // Prefer larger replication factor if all else equal.
  for (int PossibleReplicationFactor :
       reverse(seq_inclusive<unsigned>(1, Mask.size()))) {
    if (Mask.size() % PossibleReplicationFactor != 0)
      continue;
    int PossibleVF = Mask.size() / PossibleReplicationFactor;
    if (!isReplicationMaskWithParams(Mask, PossibleReplicationFactor,
                                     PossibleVF))
      continue;
    ReplicationFactor = PossibleReplicationFactor;
    VF = PossibleVF;
    return true;
  }

  return false;
}

bool ShuffleVectorInst::isReplicationMask(int &ReplicationFactor,
                                          int &VF) const {
  // Not possible to express a shuffle mask for a scalable vector for this
  // case.
  if (isa<ScalableVectorType>(getType()))
    return false;

  VF = cast<FixedVectorType>(Op<0>()->getType())->getNumElements();
  if (ShuffleMask.size() % VF != 0)
    return false;
  ReplicationFactor = ShuffleMask.size() / VF;

  return isReplicationMaskWithParams(ShuffleMask, ReplicationFactor, VF);
}

bool ShuffleVectorInst::isOneUseSingleSourceMask(ArrayRef<int> Mask, int VF) {
  if (VF <= 0 || Mask.size() < static_cast<unsigned>(VF) ||
      Mask.size() % VF != 0)
    return false;
  for (unsigned K = 0, Sz = Mask.size(); K < Sz; K += VF) {
    ArrayRef<int> SubMask = Mask.slice(K, VF);
    if (all_of(SubMask, [](int Idx) { return Idx == PoisonMaskElem; }))
      continue;
    SmallBitVector Used(VF, false);
    for (int Idx : SubMask) {
      if (Idx != PoisonMaskElem && Idx < VF)
        Used.set(Idx);
    }
    if (!Used.all())
      return false;
  }
  return true;
}

/// Return true if this shuffle mask is a replication mask.
bool ShuffleVectorInst::isOneUseSingleSourceMask(int VF) const {
  // Not possible to express a shuffle mask for a scalable vector for this
  // case.
  if (isa<ScalableVectorType>(getType()))
    return false;
  if (!isSingleSourceMask(ShuffleMask, VF))
    return false;

  return isOneUseSingleSourceMask(ShuffleMask, VF);
}

bool ShuffleVectorInst::isInterleave(unsigned Factor) {
  FixedVectorType *OpTy = dyn_cast<FixedVectorType>(getOperand(0)->getType());
  // shuffle_vector can only interleave fixed length vectors - for scalable
  // vectors, see the @llvm.vector.interleave2 intrinsic
  if (!OpTy)
    return false;
  unsigned OpNumElts = OpTy->getNumElements();

  return isInterleaveMask(ShuffleMask, Factor, OpNumElts * 2);
}

bool ShuffleVectorInst::isInterleaveMask(
    ArrayRef<int> Mask, unsigned Factor, unsigned NumInputElts,
    SmallVectorImpl<unsigned> &StartIndexes) {
  unsigned NumElts = Mask.size();
  if (NumElts % Factor)
    return false;

  unsigned LaneLen = NumElts / Factor;
  if (!isPowerOf2_32(LaneLen))
    return false;

  StartIndexes.resize(Factor);

  // Check whether each element matches the general interleaved rule.
  // Ignore undef elements, as long as the defined elements match the rule.
  // Outer loop processes all factors (x, y, z in the above example)
  unsigned I = 0, J;
  for (; I < Factor; I++) {
    unsigned SavedLaneValue;
    unsigned SavedNoUndefs = 0;

    // Inner loop processes consecutive accesses (x, x+1... in the example)
    for (J = 0; J < LaneLen - 1; J++) {
      // Lane computes x's position in the Mask
      unsigned Lane = J * Factor + I;
      unsigned NextLane = Lane + Factor;
      int LaneValue = Mask[Lane];
      int NextLaneValue = Mask[NextLane];

      // If both are defined, values must be sequential
      if (LaneValue >= 0 && NextLaneValue >= 0 &&
          LaneValue + 1 != NextLaneValue)
        break;

      // If the next value is undef, save the current one as reference
      if (LaneValue >= 0 && NextLaneValue < 0) {
        SavedLaneValue = LaneValue;
        SavedNoUndefs = 1;
      }

      // Undefs are allowed, but defined elements must still be consecutive:
      // i.e.: x,..., undef,..., x + 2,..., undef,..., undef,..., x + 5, ....
      // Verify this by storing the last non-undef followed by an undef
      // Check that following non-undef masks are incremented with the
      // corresponding distance.
      if (SavedNoUndefs > 0 && LaneValue < 0) {
        SavedNoUndefs++;
        if (NextLaneValue >= 0 &&
            SavedLaneValue + SavedNoUndefs != (unsigned)NextLaneValue)
          break;
      }
    }

    if (J < LaneLen - 1)
      return false;

    int StartMask = 0;
    if (Mask[I] >= 0) {
      // Check that the start of the I range (J=0) is greater than 0
      StartMask = Mask[I];
    } else if (Mask[(LaneLen - 1) * Factor + I] >= 0) {
      // StartMask defined by the last value in lane
      StartMask = Mask[(LaneLen - 1) * Factor + I] - J;
    } else if (SavedNoUndefs > 0) {
      // StartMask defined by some non-zero value in the j loop
      StartMask = SavedLaneValue - (LaneLen - 1 - SavedNoUndefs);
    }
    // else StartMask remains set to 0, i.e. all elements are undefs

    if (StartMask < 0)
      return false;
    // We must stay within the vectors; This case can happen with undefs.
    if (StartMask + LaneLen > NumInputElts)
      return false;

    StartIndexes[I] = StartMask;
  }

  return true;
}

/// Check if the mask is a DE-interleave mask of the given factor
/// \p Factor like:
///     <Index, Index+Factor, ..., Index+(NumElts-1)*Factor>
bool ShuffleVectorInst::isDeInterleaveMaskOfFactor(ArrayRef<int> Mask,
                                                   unsigned Factor,
                                                   unsigned &Index) {
  // Check all potential start indices from 0 to (Factor - 1).
  for (unsigned Idx = 0; Idx < Factor; Idx++) {
    unsigned I = 0;

    // Check that elements are in ascending order by Factor. Ignore undef
    // elements.
    for (; I < Mask.size(); I++)
      if (Mask[I] >= 0 && static_cast<unsigned>(Mask[I]) != Idx + I * Factor)
        break;

    if (I == Mask.size()) {
      Index = Idx;
      return true;
    }
  }

  return false;
}

/// Try to lower a vector shuffle as a bit rotation.
///
/// Look for a repeated rotation pattern in each sub group.
/// Returns an element-wise left bit rotation amount or -1 if failed.
static int matchShuffleAsBitRotate(ArrayRef<int> Mask, int NumSubElts) {
  int NumElts = Mask.size();
  assert((NumElts % NumSubElts) == 0 && "Illegal shuffle mask");

  int RotateAmt = -1;
  for (int i = 0; i != NumElts; i += NumSubElts) {
    for (int j = 0; j != NumSubElts; ++j) {
      int M = Mask[i + j];
      if (M < 0)
        continue;
      if (M < i || M >= i + NumSubElts)
        return -1;
      int Offset = (NumSubElts - (M - (i + j))) % NumSubElts;
      if (0 <= RotateAmt && Offset != RotateAmt)
        return -1;
      RotateAmt = Offset;
    }
  }
  return RotateAmt;
}

bool ShuffleVectorInst::isBitRotateMask(
    ArrayRef<int> Mask, unsigned EltSizeInBits, unsigned MinSubElts,
    unsigned MaxSubElts, unsigned &NumSubElts, unsigned &RotateAmt) {
  for (NumSubElts = MinSubElts; NumSubElts <= MaxSubElts; NumSubElts *= 2) {
    int EltRotateAmt = matchShuffleAsBitRotate(Mask, NumSubElts);
    if (EltRotateAmt < 0)
      continue;
    RotateAmt = EltRotateAmt * EltSizeInBits;
    return true;
  }

  return false;
}

//===----------------------------------------------------------------------===//
//                             InsertValueInst Class
//===----------------------------------------------------------------------===//

void InsertValueInst::init(Value *Agg, Value *Val, ArrayRef<unsigned> Idxs,
                           const Twine &Name) {
  assert(getNumOperands() == 2 && "NumOperands not initialized?");

  // There's no fundamental reason why we require at least one index
  // (other than weirdness with &*IdxBegin being invalid; see
  // getelementptr's init routine for example). But there's no
  // present need to support it.
  assert(!Idxs.empty() && "InsertValueInst must have at least one index");

  assert(ExtractValueInst::getIndexedType(Agg->getType(), Idxs) ==
         Val->getType() && "Inserted value must match indexed type!");
  Op<0>() = Agg;
  Op<1>() = Val;

  Indices.append(Idxs.begin(), Idxs.end());
  setName(Name);
}

InsertValueInst::InsertValueInst(const InsertValueInst &IVI)
  : Instruction(IVI.getType(), InsertValue,
                OperandTraits<InsertValueInst>::op_begin(this), 2),
    Indices(IVI.Indices) {
  Op<0>() = IVI.getOperand(0);
  Op<1>() = IVI.getOperand(1);
  SubclassOptionalData = IVI.SubclassOptionalData;
}

//===----------------------------------------------------------------------===//
//                             ExtractValueInst Class
//===----------------------------------------------------------------------===//

void ExtractValueInst::init(ArrayRef<unsigned> Idxs, const Twine &Name) {
  assert(getNumOperands() == 1 && "NumOperands not initialized?");

  // There's no fundamental reason why we require at least one index.
  // But there's no present need to support it.
  assert(!Idxs.empty() && "ExtractValueInst must have at least one index");

  Indices.append(Idxs.begin(), Idxs.end());
  setName(Name);
}

ExtractValueInst::ExtractValueInst(const ExtractValueInst &EVI)
  : UnaryInstruction(EVI.getType(), ExtractValue, EVI.getOperand(0)),
    Indices(EVI.Indices) {
  SubclassOptionalData = EVI.SubclassOptionalData;
}

// getIndexedType - Returns the type of the element that would be extracted
// with an extractvalue instruction with the specified parameters.
//
// A null type is returned if the indices are invalid for the specified
// pointer type.
//
Type *ExtractValueInst::getIndexedType(Type *Agg,
                                       ArrayRef<unsigned> Idxs) {
  for (unsigned Index : Idxs) {
    // We can't use CompositeType::indexValid(Index) here.
    // indexValid() always returns true for arrays because getelementptr allows
    // out-of-bounds indices. Since we don't allow those for extractvalue and
    // insertvalue we need to check array indexing manually.
    // Since the only other types we can index into are struct types it's just
    // as easy to check those manually as well.
    if (ArrayType *AT = dyn_cast<ArrayType>(Agg)) {
      if (Index >= AT->getNumElements())
        return nullptr;
      Agg = AT->getElementType();
    } else if (StructType *ST = dyn_cast<StructType>(Agg)) {
      if (Index >= ST->getNumElements())
        return nullptr;
      Agg = ST->getElementType(Index);
    } else {
      // Not a valid type to index into.
      return nullptr;
    }
  }
  return const_cast<Type*>(Agg);
}

//===----------------------------------------------------------------------===//
//                             UnaryOperator Class
//===----------------------------------------------------------------------===//

UnaryOperator::UnaryOperator(UnaryOps iType, Value *S, Type *Ty,
                             const Twine &Name, InsertPosition InsertBefore)
    : UnaryInstruction(Ty, iType, S, InsertBefore) {
  Op<0>() = S;
  setName(Name);
  AssertOK();
}

UnaryOperator *UnaryOperator::Create(UnaryOps Op, Value *S, const Twine &Name,
                                     InsertPosition InsertBefore) {
  return new UnaryOperator(Op, S, S->getType(), Name, InsertBefore);
}

void UnaryOperator::AssertOK() {
  Value *LHS = getOperand(0);
  (void)LHS; // Silence warnings.
#ifndef NDEBUG
  switch (getOpcode()) {
  case FNeg:
    assert(getType() == LHS->getType() &&
           "Unary operation should return same type as operand!");
    assert(getType()->isFPOrFPVectorTy() &&
           "Tried to create a floating-point operation on a "
           "non-floating-point type!");
    break;
  default: llvm_unreachable("Invalid opcode provided");
  }
#endif
}

//===----------------------------------------------------------------------===//
//                             BinaryOperator Class
//===----------------------------------------------------------------------===//

BinaryOperator::BinaryOperator(BinaryOps iType, Value *S1, Value *S2, Type *Ty,
                               const Twine &Name, InsertPosition InsertBefore)
    : Instruction(Ty, iType, OperandTraits<BinaryOperator>::op_begin(this),
                  OperandTraits<BinaryOperator>::operands(this), InsertBefore) {
  Op<0>() = S1;
  Op<1>() = S2;
  setName(Name);
  AssertOK();
}

void BinaryOperator::AssertOK() {
  Value *LHS = getOperand(0), *RHS = getOperand(1);
  (void)LHS; (void)RHS; // Silence warnings.
  assert(LHS->getType() == RHS->getType() &&
         "Binary operator operand types must match!");
#ifndef NDEBUG
  switch (getOpcode()) {
  case Add: case Sub:
  case Mul:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isIntOrIntVectorTy() &&
           "Tried to create an integer operation on a non-integer type!");
    break;
  case FAdd: case FSub:
  case FMul:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isFPOrFPVectorTy() &&
           "Tried to create a floating-point operation on a "
           "non-floating-point type!");
    break;
  case UDiv:
  case SDiv:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isIntOrIntVectorTy() &&
           "Incorrect operand type (not integer) for S/UDIV");
    break;
  case FDiv:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isFPOrFPVectorTy() &&
           "Incorrect operand type (not floating point) for FDIV");
    break;
  case URem:
  case SRem:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isIntOrIntVectorTy() &&
           "Incorrect operand type (not integer) for S/UREM");
    break;
  case FRem:
    assert(getType() == LHS->getType() &&
           "Arithmetic operation should return same type as operands!");
    assert(getType()->isFPOrFPVectorTy() &&
           "Incorrect operand type (not floating point) for FREM");
    break;
  case Shl:
  case LShr:
  case AShr:
    assert(getType() == LHS->getType() &&
           "Shift operation should return same type as operands!");
    assert(getType()->isIntOrIntVectorTy() &&
           "Tried to create a shift operation on a non-integral type!");
    break;
  case And: case Or:
  case Xor:
    assert(getType() == LHS->getType() &&
           "Logical operation should return same type as operands!");
    assert(getType()->isIntOrIntVectorTy() &&
           "Tried to create a logical operation on a non-integral type!");
    break;
  default: llvm_unreachable("Invalid opcode provided");
  }
#endif
}

BinaryOperator *BinaryOperator::Create(BinaryOps Op, Value *S1, Value *S2,
                                       const Twine &Name,
                                       InsertPosition InsertBefore) {
  assert(S1->getType() == S2->getType() &&
         "Cannot create binary operator with two operands of differing type!");
  return new BinaryOperator(Op, S1, S2, S1->getType(), Name, InsertBefore);
}

BinaryOperator *BinaryOperator::CreateNeg(Value *Op, const Twine &Name,
                                          InsertPosition InsertBefore) {
  Value *Zero = ConstantInt::get(Op->getType(), 0);
  return new BinaryOperator(Instruction::Sub, Zero, Op, Op->getType(), Name,
                            InsertBefore);
}

BinaryOperator *BinaryOperator::CreateNSWNeg(Value *Op, const Twine &Name,
                                             InsertPosition InsertBefore) {
  Value *Zero = ConstantInt::get(Op->getType(), 0);
  return BinaryOperator::CreateNSWSub(Zero, Op, Name, InsertBefore);
}

BinaryOperator *BinaryOperator::CreateNot(Value *Op, const Twine &Name,
                                          InsertPosition InsertBefore) {
  Constant *C = Constant::getAllOnesValue(Op->getType());
  return new BinaryOperator(Instruction::Xor, Op, C,
                            Op->getType(), Name, InsertBefore);
}

// Exchange the two operands to this instruction. This instruction is safe to
// use on any binary instruction and does not modify the semantics of the
// instruction. If the instruction is order-dependent (SetLT f.e.), the opcode
// is changed.
bool BinaryOperator::swapOperands() {
  if (!isCommutative())
    return true; // Can't commute operands
  Op<0>().swap(Op<1>());
  return false;
}

//===----------------------------------------------------------------------===//
//                             FPMathOperator Class
//===----------------------------------------------------------------------===//

float FPMathOperator::getFPAccuracy() const {
  const MDNode *MD =
      cast<Instruction>(this)->getMetadata(LLVMContext::MD_fpmath);
  if (!MD)
    return 0.0;
  ConstantFP *Accuracy = mdconst::extract<ConstantFP>(MD->getOperand(0));
  return Accuracy->getValueAPF().convertToFloat();
}

//===----------------------------------------------------------------------===//
//                                CastInst Class
//===----------------------------------------------------------------------===//

// Just determine if this cast only deals with integral->integral conversion.
bool CastInst::isIntegerCast() const {
  switch (getOpcode()) {
    default: return false;
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::Trunc:
      return true;
    case Instruction::BitCast:
      return getOperand(0)->getType()->isIntegerTy() &&
        getType()->isIntegerTy();
  }
}

/// This function determines if the CastInst does not require any bits to be
/// changed in order to effect the cast. Essentially, it identifies cases where
/// no code gen is necessary for the cast, hence the name no-op cast.  For
/// example, the following are all no-op casts:
/// # bitcast i32* %x to i8*
/// # bitcast <2 x i32> %x to <4 x i16>
/// # ptrtoint i32* %x to i32     ; on 32-bit plaforms only
/// Determine if the described cast is a no-op.
bool CastInst::isNoopCast(Instruction::CastOps Opcode,
                          Type *SrcTy,
                          Type *DestTy,
                          const DataLayout &DL) {
  assert(castIsValid(Opcode, SrcTy, DestTy) && "method precondition");
  switch (Opcode) {
    default: llvm_unreachable("Invalid CastOp");
    case Instruction::Trunc:
    case Instruction::ZExt:
    case Instruction::SExt:
    case Instruction::FPTrunc:
    case Instruction::FPExt:
    case Instruction::UIToFP:
    case Instruction::SIToFP:
    case Instruction::FPToUI:
    case Instruction::FPToSI:
    case Instruction::AddrSpaceCast:
      // TODO: Target informations may give a more accurate answer here.
      return false;
    case Instruction::BitCast:
      return true;  // BitCast never modifies bits.
    case Instruction::PtrToInt:
      return DL.getIntPtrType(SrcTy)->getScalarSizeInBits() ==
             DestTy->getScalarSizeInBits();
    case Instruction::IntToPtr:
      return DL.getIntPtrType(DestTy)->getScalarSizeInBits() ==
             SrcTy->getScalarSizeInBits();
  }
}

bool CastInst::isNoopCast(const DataLayout &DL) const {
  return isNoopCast(getOpcode(), getOperand(0)->getType(), getType(), DL);
}

/// This function determines if a pair of casts can be eliminated and what
/// opcode should be used in the elimination. This assumes that there are two
/// instructions like this:
/// *  %F = firstOpcode SrcTy %x to MidTy
/// *  %S = secondOpcode MidTy %F to DstTy
/// The function returns a resultOpcode so these two casts can be replaced with:
/// *  %Replacement = resultOpcode %SrcTy %x to DstTy
/// If no such cast is permitted, the function returns 0.
unsigned CastInst::isEliminableCastPair(
  Instruction::CastOps firstOp, Instruction::CastOps secondOp,
  Type *SrcTy, Type *MidTy, Type *DstTy, Type *SrcIntPtrTy, Type *MidIntPtrTy,
  Type *DstIntPtrTy) {
  // Define the 144 possibilities for these two cast instructions. The values
  // in this matrix determine what to do in a given situation and select the
  // case in the switch below.  The rows correspond to firstOp, the columns
  // correspond to secondOp.  In looking at the table below, keep in mind
  // the following cast properties:
  //
  //          Size Compare       Source               Destination
  // Operator  Src ? Size   Type       Sign         Type       Sign
  // -------- ------------ -------------------   ---------------------
  // TRUNC         >       Integer      Any        Integral     Any
  // ZEXT          <       Integral   Unsigned     Integer      Any
  // SEXT          <       Integral    Signed      Integer      Any
  // FPTOUI       n/a      FloatPt      n/a        Integral   Unsigned
  // FPTOSI       n/a      FloatPt      n/a        Integral    Signed
  // UITOFP       n/a      Integral   Unsigned     FloatPt      n/a
  // SITOFP       n/a      Integral    Signed      FloatPt      n/a
  // FPTRUNC       >       FloatPt      n/a        FloatPt      n/a
  // FPEXT         <       FloatPt      n/a        FloatPt      n/a
  // PTRTOINT     n/a      Pointer      n/a        Integral   Unsigned
  // INTTOPTR     n/a      Integral   Unsigned     Pointer      n/a
  // BITCAST       =       FirstClass   n/a       FirstClass    n/a
  // ADDRSPCST    n/a      Pointer      n/a        Pointer      n/a
  //
  // NOTE: some transforms are safe, but we consider them to be non-profitable.
  // For example, we could merge "fptoui double to i32" + "zext i32 to i64",
  // into "fptoui double to i64", but this loses information about the range
  // of the produced value (we no longer know the top-part is all zeros).
  // Further this conversion is often much more expensive for typical hardware,
  // and causes issues when building libgcc.  We disallow fptosi+sext for the
  // same reason.
  const unsigned numCastOps =
    Instruction::CastOpsEnd - Instruction::CastOpsBegin;
  static const uint8_t CastResults[numCastOps][numCastOps] = {
    // T        F  F  U  S  F  F  P  I  B  A  -+
    // R  Z  S  P  P  I  I  T  P  2  N  T  S   |
    // U  E  E  2  2  2  2  R  E  I  T  C  C   +- secondOp
    // N  X  X  U  S  F  F  N  X  N  2  V  V   |
    // C  T  T  I  I  P  P  C  T  T  P  T  T  -+
    {  1, 0, 0,99,99, 0, 0,99,99,99, 0, 3, 0}, // Trunc         -+
    {  8, 1, 9,99,99, 2,17,99,99,99, 2, 3, 0}, // ZExt           |
    {  8, 0, 1,99,99, 0, 2,99,99,99, 0, 3, 0}, // SExt           |
    {  0, 0, 0,99,99, 0, 0,99,99,99, 0, 3, 0}, // FPToUI         |
    {  0, 0, 0,99,99, 0, 0,99,99,99, 0, 3, 0}, // FPToSI         |
    { 99,99,99, 0, 0,99,99, 0, 0,99,99, 4, 0}, // UIToFP         +- firstOp
    { 99,99,99, 0, 0,99,99, 0, 0,99,99, 4, 0}, // SIToFP         |
    { 99,99,99, 0, 0,99,99, 0, 0,99,99, 4, 0}, // FPTrunc        |
    { 99,99,99, 2, 2,99,99, 8, 2,99,99, 4, 0}, // FPExt          |
    {  1, 0, 0,99,99, 0, 0,99,99,99, 7, 3, 0}, // PtrToInt       |
    { 99,99,99,99,99,99,99,99,99,11,99,15, 0}, // IntToPtr       |
    {  5, 5, 5, 0, 0, 5, 5, 0, 0,16, 5, 1,14}, // BitCast        |
    {  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,13,12}, // AddrSpaceCast -+
  };

  // TODO: This logic could be encoded into the table above and handled in the
  // switch below.
  // If either of the casts are a bitcast from scalar to vector, disallow the
  // merging. However, any pair of bitcasts are allowed.
  bool IsFirstBitcast  = (firstOp == Instruction::BitCast);
  bool IsSecondBitcast = (secondOp == Instruction::BitCast);
  bool AreBothBitcasts = IsFirstBitcast && IsSecondBitcast;

  // Check if any of the casts convert scalars <-> vectors.
  if ((IsFirstBitcast  && isa<VectorType>(SrcTy) != isa<VectorType>(MidTy)) ||
      (IsSecondBitcast && isa<VectorType>(MidTy) != isa<VectorType>(DstTy)))
    if (!AreBothBitcasts)
      return 0;

  int ElimCase = CastResults[firstOp-Instruction::CastOpsBegin]
                            [secondOp-Instruction::CastOpsBegin];
  switch (ElimCase) {
    case 0:
      // Categorically disallowed.
      return 0;
    case 1:
      // Allowed, use first cast's opcode.
      return firstOp;
    case 2:
      // Allowed, use second cast's opcode.
      return secondOp;
    case 3:
      // No-op cast in second op implies firstOp as long as the DestTy
      // is integer and we are not converting between a vector and a
      // non-vector type.
      if (!SrcTy->isVectorTy() && DstTy->isIntegerTy())
        return firstOp;
      return 0;
    case 4:
      // No-op cast in second op implies firstOp as long as the DestTy
      // matches MidTy.
      if (DstTy == MidTy)
        return firstOp;
      return 0;
    case 5:
      // No-op cast in first op implies secondOp as long as the SrcTy
      // is an integer.
      if (SrcTy->isIntegerTy())
        return secondOp;
      return 0;
    case 7: {
      // Disable inttoptr/ptrtoint optimization if enabled.
      if (DisableI2pP2iOpt)
        return 0;

      // Cannot simplify if address spaces are different!
      if (SrcTy->getPointerAddressSpace() != DstTy->getPointerAddressSpace())
        return 0;

      unsigned MidSize = MidTy->getScalarSizeInBits();
      // We can still fold this without knowing the actual sizes as long we
      // know that the intermediate pointer is the largest possible
      // pointer size.
      // FIXME: Is this always true?
      if (MidSize == 64)
        return Instruction::BitCast;

      // ptrtoint, inttoptr -> bitcast (ptr -> ptr) if int size is >= ptr size.
      if (!SrcIntPtrTy || DstIntPtrTy != SrcIntPtrTy)
        return 0;
      unsigned PtrSize = SrcIntPtrTy->getScalarSizeInBits();
      if (MidSize >= PtrSize)
        return Instruction::BitCast;
      return 0;
    }
    case 8: {
      // ext, trunc -> bitcast,    if the SrcTy and DstTy are the same
      // ext, trunc -> ext,        if sizeof(SrcTy) < sizeof(DstTy)
      // ext, trunc -> trunc,      if sizeof(SrcTy) > sizeof(DstTy)
      unsigned SrcSize = SrcTy->getScalarSizeInBits();
      unsigned DstSize = DstTy->getScalarSizeInBits();
      if (SrcTy == DstTy)
        return Instruction::BitCast;
      if (SrcSize < DstSize)
        return firstOp;
      if (SrcSize > DstSize)
        return secondOp;
      return 0;
    }
    case 9:
      // zext, sext -> zext, because sext can't sign extend after zext
      return Instruction::ZExt;
    case 11: {
      // inttoptr, ptrtoint -> bitcast if SrcSize<=PtrSize and SrcSize==DstSize
      if (!MidIntPtrTy)
        return 0;
      unsigned PtrSize = MidIntPtrTy->getScalarSizeInBits();
      unsigned SrcSize = SrcTy->getScalarSizeInBits();
      unsigned DstSize = DstTy->getScalarSizeInBits();
      if (SrcSize <= PtrSize && SrcSize == DstSize)
        return Instruction::BitCast;
      return 0;
    }
    case 12:
      // addrspacecast, addrspacecast -> bitcast,       if SrcAS == DstAS
      // addrspacecast, addrspacecast -> addrspacecast, if SrcAS != DstAS
      if (SrcTy->getPointerAddressSpace() != DstTy->getPointerAddressSpace())
        return Instruction::AddrSpaceCast;
      return Instruction::BitCast;
    case 13:
      // FIXME: this state can be merged with (1), but the following assert
      // is useful to check the correcteness of the sequence due to semantic
      // change of bitcast.
      assert(
        SrcTy->isPtrOrPtrVectorTy() &&
        MidTy->isPtrOrPtrVectorTy() &&
        DstTy->isPtrOrPtrVectorTy() &&
        SrcTy->getPointerAddressSpace() != MidTy->getPointerAddressSpace() &&
        MidTy->getPointerAddressSpace() == DstTy->getPointerAddressSpace() &&
        "Illegal addrspacecast, bitcast sequence!");
      // Allowed, use first cast's opcode
      return firstOp;
    case 14:
      // bitcast, addrspacecast -> addrspacecast
      return Instruction::AddrSpaceCast;
    case 15:
      // FIXME: this state can be merged with (1), but the following assert
      // is useful to check the correcteness of the sequence due to semantic
      // change of bitcast.
      assert(
        SrcTy->isIntOrIntVectorTy() &&
        MidTy->isPtrOrPtrVectorTy() &&
        DstTy->isPtrOrPtrVectorTy() &&
        MidTy->getPointerAddressSpace() == DstTy->getPointerAddressSpace() &&
        "Illegal inttoptr, bitcast sequence!");
      // Allowed, use first cast's opcode
      return firstOp;
    case 16:
      // FIXME: this state can be merged with (2), but the following assert
      // is useful to check the correcteness of the sequence due to semantic
      // change of bitcast.
      assert(
        SrcTy->isPtrOrPtrVectorTy() &&
        MidTy->isPtrOrPtrVectorTy() &&
        DstTy->isIntOrIntVectorTy() &&
        SrcTy->getPointerAddressSpace() == MidTy->getPointerAddressSpace() &&
        "Illegal bitcast, ptrtoint sequence!");
      // Allowed, use second cast's opcode
      return secondOp;
    case 17:
      // (sitofp (zext x)) -> (uitofp x)
      return Instruction::UIToFP;
    case 99:
      // Cast combination can't happen (error in input). This is for all cases
      // where the MidTy is not the same for the two cast instructions.
      llvm_unreachable("Invalid Cast Combination");
    default:
      llvm_unreachable("Error in CastResults table!!!");
  }
}

CastInst *CastInst::Create(Instruction::CastOps op, Value *S, Type *Ty,
                           const Twine &Name, InsertPosition InsertBefore) {
  assert(castIsValid(op, S, Ty) && "Invalid cast!");
  // Construct and return the appropriate CastInst subclass
  switch (op) {
  case Trunc:         return new TruncInst         (S, Ty, Name, InsertBefore);
  case ZExt:          return new ZExtInst          (S, Ty, Name, InsertBefore);
  case SExt:          return new SExtInst          (S, Ty, Name, InsertBefore);
  case FPTrunc:       return new FPTruncInst       (S, Ty, Name, InsertBefore);
  case FPExt:         return new FPExtInst         (S, Ty, Name, InsertBefore);
  case UIToFP:        return new UIToFPInst        (S, Ty, Name, InsertBefore);
  case SIToFP:        return new SIToFPInst        (S, Ty, Name, InsertBefore);
  case FPToUI:        return new FPToUIInst        (S, Ty, Name, InsertBefore);
  case FPToSI:        return new FPToSIInst        (S, Ty, Name, InsertBefore);
  case PtrToInt:      return new PtrToIntInst      (S, Ty, Name, InsertBefore);
  case IntToPtr:      return new IntToPtrInst      (S, Ty, Name, InsertBefore);
  case BitCast:
    return new BitCastInst(S, Ty, Name, InsertBefore);
  case AddrSpaceCast:
    return new AddrSpaceCastInst(S, Ty, Name, InsertBefore);
  default:
    llvm_unreachable("Invalid opcode provided");
  }
}

CastInst *CastInst::CreateZExtOrBitCast(Value *S, Type *Ty, const Twine &Name,
                                        InsertPosition InsertBefore) {
  if (S->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return Create(Instruction::BitCast, S, Ty, Name, InsertBefore);
  return Create(Instruction::ZExt, S, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreateSExtOrBitCast(Value *S, Type *Ty, const Twine &Name,
                                        InsertPosition InsertBefore) {
  if (S->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return Create(Instruction::BitCast, S, Ty, Name, InsertBefore);
  return Create(Instruction::SExt, S, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreateTruncOrBitCast(Value *S, Type *Ty, const Twine &Name,
                                         InsertPosition InsertBefore) {
  if (S->getType()->getScalarSizeInBits() == Ty->getScalarSizeInBits())
    return Create(Instruction::BitCast, S, Ty, Name, InsertBefore);
  return Create(Instruction::Trunc, S, Ty, Name, InsertBefore);
}

/// Create a BitCast or a PtrToInt cast instruction
CastInst *CastInst::CreatePointerCast(Value *S, Type *Ty, const Twine &Name,
                                      InsertPosition InsertBefore) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert((Ty->isIntOrIntVectorTy() || Ty->isPtrOrPtrVectorTy()) &&
         "Invalid cast");
  assert(Ty->isVectorTy() == S->getType()->isVectorTy() && "Invalid cast");
  assert((!Ty->isVectorTy() ||
          cast<VectorType>(Ty)->getElementCount() ==
              cast<VectorType>(S->getType())->getElementCount()) &&
         "Invalid cast");

  if (Ty->isIntOrIntVectorTy())
    return Create(Instruction::PtrToInt, S, Ty, Name, InsertBefore);

  return CreatePointerBitCastOrAddrSpaceCast(S, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreatePointerBitCastOrAddrSpaceCast(
    Value *S, Type *Ty, const Twine &Name, InsertPosition InsertBefore) {
  assert(S->getType()->isPtrOrPtrVectorTy() && "Invalid cast");
  assert(Ty->isPtrOrPtrVectorTy() && "Invalid cast");

  if (S->getType()->getPointerAddressSpace() != Ty->getPointerAddressSpace())
    return Create(Instruction::AddrSpaceCast, S, Ty, Name, InsertBefore);

  return Create(Instruction::BitCast, S, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreateBitOrPointerCast(Value *S, Type *Ty,
                                           const Twine &Name,
                                           InsertPosition InsertBefore) {
  if (S->getType()->isPointerTy() && Ty->isIntegerTy())
    return Create(Instruction::PtrToInt, S, Ty, Name, InsertBefore);
  if (S->getType()->isIntegerTy() && Ty->isPointerTy())
    return Create(Instruction::IntToPtr, S, Ty, Name, InsertBefore);

  return Create(Instruction::BitCast, S, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreateIntegerCast(Value *C, Type *Ty, bool isSigned,
                                      const Twine &Name,
                                      InsertPosition InsertBefore) {
  assert(C->getType()->isIntOrIntVectorTy() && Ty->isIntOrIntVectorTy() &&
         "Invalid integer cast");
  unsigned SrcBits = C->getType()->getScalarSizeInBits();
  unsigned DstBits = Ty->getScalarSizeInBits();
  Instruction::CastOps opcode =
    (SrcBits == DstBits ? Instruction::BitCast :
     (SrcBits > DstBits ? Instruction::Trunc :
      (isSigned ? Instruction::SExt : Instruction::ZExt)));
  return Create(opcode, C, Ty, Name, InsertBefore);
}

CastInst *CastInst::CreateFPCast(Value *C, Type *Ty, const Twine &Name,
                                 InsertPosition InsertBefore) {
  assert(C->getType()->isFPOrFPVectorTy() && Ty->isFPOrFPVectorTy() &&
         "Invalid cast");
  unsigned SrcBits = C->getType()->getScalarSizeInBits();
  unsigned DstBits = Ty->getScalarSizeInBits();
  assert((C->getType() == Ty || SrcBits != DstBits) && "Invalid cast");
  Instruction::CastOps opcode =
    (SrcBits == DstBits ? Instruction::BitCast :
     (SrcBits > DstBits ? Instruction::FPTrunc : Instruction::FPExt));
  return Create(opcode, C, Ty, Name, InsertBefore);
}

bool CastInst::isBitCastable(Type *SrcTy, Type *DestTy) {
  if (!SrcTy->isFirstClassType() || !DestTy->isFirstClassType())
    return false;

  if (SrcTy == DestTy)
    return true;

  if (VectorType *SrcVecTy = dyn_cast<VectorType>(SrcTy)) {
    if (VectorType *DestVecTy = dyn_cast<VectorType>(DestTy)) {
      if (SrcVecTy->getElementCount() == DestVecTy->getElementCount()) {
        // An element by element cast. Valid if casting the elements is valid.
        SrcTy = SrcVecTy->getElementType();
        DestTy = DestVecTy->getElementType();
      }
    }
  }

  if (PointerType *DestPtrTy = dyn_cast<PointerType>(DestTy)) {
    if (PointerType *SrcPtrTy = dyn_cast<PointerType>(SrcTy)) {
      return SrcPtrTy->getAddressSpace() == DestPtrTy->getAddressSpace();
    }
  }

  TypeSize SrcBits = SrcTy->getPrimitiveSizeInBits();   // 0 for ptr
  TypeSize DestBits = DestTy->getPrimitiveSizeInBits(); // 0 for ptr

  // Could still have vectors of pointers if the number of elements doesn't
  // match
  if (SrcBits.getKnownMinValue() == 0 || DestBits.getKnownMinValue() == 0)
    return false;

  if (SrcBits != DestBits)
    return false;

  if (DestTy->isX86_MMXTy() || SrcTy->isX86_MMXTy())
    return false;

  return true;
}

bool CastInst::isBitOrNoopPointerCastable(Type *SrcTy, Type *DestTy,
                                          const DataLayout &DL) {
  // ptrtoint and inttoptr are not allowed on non-integral pointers
  if (auto *PtrTy = dyn_cast<PointerType>(SrcTy))
    if (auto *IntTy = dyn_cast<IntegerType>(DestTy))
      return (IntTy->getBitWidth() == DL.getPointerTypeSizeInBits(PtrTy) &&
              !DL.isNonIntegralPointerType(PtrTy));
  if (auto *PtrTy = dyn_cast<PointerType>(DestTy))
    if (auto *IntTy = dyn_cast<IntegerType>(SrcTy))
      return (IntTy->getBitWidth() == DL.getPointerTypeSizeInBits(PtrTy) &&
              !DL.isNonIntegralPointerType(PtrTy));

  return isBitCastable(SrcTy, DestTy);
}

// Provide a way to get a "cast" where the cast opcode is inferred from the
// types and size of the operand. This, basically, is a parallel of the
// logic in the castIsValid function below.  This axiom should hold:
//   castIsValid( getCastOpcode(Val, Ty), Val, Ty)
// should not assert in castIsValid. In other words, this produces a "correct"
// casting opcode for the arguments passed to it.
Instruction::CastOps
CastInst::getCastOpcode(
  const Value *Src, bool SrcIsSigned, Type *DestTy, bool DestIsSigned) {
  Type *SrcTy = Src->getType();

  assert(SrcTy->isFirstClassType() && DestTy->isFirstClassType() &&
         "Only first class types are castable!");

  if (SrcTy == DestTy)
    return BitCast;

  // FIXME: Check address space sizes here
  if (VectorType *SrcVecTy = dyn_cast<VectorType>(SrcTy))
    if (VectorType *DestVecTy = dyn_cast<VectorType>(DestTy))
      if (SrcVecTy->getElementCount() == DestVecTy->getElementCount()) {
        // An element by element cast.  Find the appropriate opcode based on the
        // element types.
        SrcTy = SrcVecTy->getElementType();
        DestTy = DestVecTy->getElementType();
      }

  // Get the bit sizes, we'll need these
  unsigned SrcBits = SrcTy->getPrimitiveSizeInBits();   // 0 for ptr
  unsigned DestBits = DestTy->getPrimitiveSizeInBits(); // 0 for ptr

  // Run through the possibilities ...
  if (DestTy->isIntegerTy()) {                      // Casting to integral
    if (SrcTy->isIntegerTy()) {                     // Casting from integral
      if (DestBits < SrcBits)
        return Trunc;                               // int -> smaller int
      else if (DestBits > SrcBits) {                // its an extension
        if (SrcIsSigned)
          return SExt;                              // signed -> SEXT
        else
          return ZExt;                              // unsigned -> ZEXT
      } else {
        return BitCast;                             // Same size, No-op cast
      }
    } else if (SrcTy->isFloatingPointTy()) {        // Casting from floating pt
      if (DestIsSigned)
        return FPToSI;                              // FP -> sint
      else
        return FPToUI;                              // FP -> uint
    } else if (SrcTy->isVectorTy()) {
      assert(DestBits == SrcBits &&
             "Casting vector to integer of different width");
      return BitCast;                             // Same size, no-op cast
    } else {
      assert(SrcTy->isPointerTy() &&
             "Casting from a value that is not first-class type");
      return PtrToInt;                              // ptr -> int
    }
  } else if (DestTy->isFloatingPointTy()) {         // Casting to floating pt
    if (SrcTy->isIntegerTy()) {                     // Casting from integral
      if (SrcIsSigned)
        return SIToFP;                              // sint -> FP
      else
        return UIToFP;                              // uint -> FP
    } else if (SrcTy->isFloatingPointTy()) {        // Casting from floating pt
      if (DestBits < SrcBits) {
        return FPTrunc;                             // FP -> smaller FP
      } else if (DestBits > SrcBits) {
        return FPExt;                               // FP -> larger FP
      } else  {
        return BitCast;                             // same size, no-op cast
      }
    } else if (SrcTy->isVectorTy()) {
      assert(DestBits == SrcBits &&
             "Casting vector to floating point of different width");
      return BitCast;                             // same size, no-op cast
    }
    llvm_unreachable("Casting pointer or non-first class to float");
  } else if (DestTy->isVectorTy()) {
    assert(DestBits == SrcBits &&
           "Illegal cast to vector (wrong type or size)");
    return BitCast;
  } else if (DestTy->isPointerTy()) {
    if (SrcTy->isPointerTy()) {
      if (DestTy->getPointerAddressSpace() != SrcTy->getPointerAddressSpace())
        return AddrSpaceCast;
      return BitCast;                               // ptr -> ptr
    } else if (SrcTy->isIntegerTy()) {
      return IntToPtr;                              // int -> ptr
    }
    llvm_unreachable("Casting pointer to other than pointer or int");
  } else if (DestTy->isX86_MMXTy()) {
    if (SrcTy->isVectorTy()) {
      assert(DestBits == SrcBits && "Casting vector of wrong width to X86_MMX");
      return BitCast;                               // 64-bit vector to MMX
    }
    llvm_unreachable("Illegal cast to X86_MMX");
  }
  llvm_unreachable("Casting to type that is not first-class");
}

//===----------------------------------------------------------------------===//
//                    CastInst SubClass Constructors
//===----------------------------------------------------------------------===//

/// Check that the construction parameters for a CastInst are correct. This
/// could be broken out into the separate constructors but it is useful to have
/// it in one place and to eliminate the redundant code for getting the sizes
/// of the types involved.
bool
CastInst::castIsValid(Instruction::CastOps op, Type *SrcTy, Type *DstTy) {
  if (!SrcTy->isFirstClassType() || !DstTy->isFirstClassType() ||
      SrcTy->isAggregateType() || DstTy->isAggregateType())
    return false;

  // Get the size of the types in bits, and whether we are dealing
  // with vector types, we'll need this later.
  bool SrcIsVec = isa<VectorType>(SrcTy);
  bool DstIsVec = isa<VectorType>(DstTy);
  unsigned SrcScalarBitSize = SrcTy->getScalarSizeInBits();
  unsigned DstScalarBitSize = DstTy->getScalarSizeInBits();

  // If these are vector types, get the lengths of the vectors (using zero for
  // scalar types means that checking that vector lengths match also checks that
  // scalars are not being converted to vectors or vectors to scalars).
  ElementCount SrcEC = SrcIsVec ? cast<VectorType>(SrcTy)->getElementCount()
                                : ElementCount::getFixed(0);
  ElementCount DstEC = DstIsVec ? cast<VectorType>(DstTy)->getElementCount()
                                : ElementCount::getFixed(0);

  // Switch on the opcode provided
  switch (op) {
  default: return false; // This is an input error
  case Instruction::Trunc:
    return SrcTy->isIntOrIntVectorTy() && DstTy->isIntOrIntVectorTy() &&
           SrcEC == DstEC && SrcScalarBitSize > DstScalarBitSize;
  case Instruction::ZExt:
    return SrcTy->isIntOrIntVectorTy() && DstTy->isIntOrIntVectorTy() &&
           SrcEC == DstEC && SrcScalarBitSize < DstScalarBitSize;
  case Instruction::SExt:
    return SrcTy->isIntOrIntVectorTy() && DstTy->isIntOrIntVectorTy() &&
           SrcEC == DstEC && SrcScalarBitSize < DstScalarBitSize;
  case Instruction::FPTrunc:
    return SrcTy->isFPOrFPVectorTy() && DstTy->isFPOrFPVectorTy() &&
           SrcEC == DstEC && SrcScalarBitSize > DstScalarBitSize;
  case Instruction::FPExt:
    return SrcTy->isFPOrFPVectorTy() && DstTy->isFPOrFPVectorTy() &&
           SrcEC == DstEC && SrcScalarBitSize < DstScalarBitSize;
  case Instruction::UIToFP:
  case Instruction::SIToFP:
    return SrcTy->isIntOrIntVectorTy() && DstTy->isFPOrFPVectorTy() &&
           SrcEC == DstEC;
  case Instruction::FPToUI:
  case Instruction::FPToSI:
    return SrcTy->isFPOrFPVectorTy() && DstTy->isIntOrIntVectorTy() &&
           SrcEC == DstEC;
  case Instruction::PtrToInt:
    if (SrcEC != DstEC)
      return false;
    return SrcTy->isPtrOrPtrVectorTy() && DstTy->isIntOrIntVectorTy();
  case Instruction::IntToPtr:
    if (SrcEC != DstEC)
      return false;
    return SrcTy->isIntOrIntVectorTy() && DstTy->isPtrOrPtrVectorTy();
  case Instruction::BitCast: {
    PointerType *SrcPtrTy = dyn_cast<PointerType>(SrcTy->getScalarType());
    PointerType *DstPtrTy = dyn_cast<PointerType>(DstTy->getScalarType());

    // BitCast implies a no-op cast of type only. No bits change.
    // However, you can't cast pointers to anything but pointers.
    if (!SrcPtrTy != !DstPtrTy)
      return false;

    // For non-pointer cases, the cast is okay if the source and destination bit
    // widths are identical.
    if (!SrcPtrTy)
      return SrcTy->getPrimitiveSizeInBits() == DstTy->getPrimitiveSizeInBits();

    // If both are pointers then the address spaces must match.
    if (SrcPtrTy->getAddressSpace() != DstPtrTy->getAddressSpace())
      return false;

    // A vector of pointers must have the same number of elements.
    if (SrcIsVec && DstIsVec)
      return SrcEC == DstEC;
    if (SrcIsVec)
      return SrcEC == ElementCount::getFixed(1);
    if (DstIsVec)
      return DstEC == ElementCount::getFixed(1);

    return true;
  }
  case Instruction::AddrSpaceCast: {
    PointerType *SrcPtrTy = dyn_cast<PointerType>(SrcTy->getScalarType());
    if (!SrcPtrTy)
      return false;

    PointerType *DstPtrTy = dyn_cast<PointerType>(DstTy->getScalarType());
    if (!DstPtrTy)
      return false;

    if (SrcPtrTy->getAddressSpace() == DstPtrTy->getAddressSpace())
      return false;

    return SrcEC == DstEC;
  }
  }
}

TruncInst::TruncInst(Value *S, Type *Ty, const Twine &Name,
                     InsertPosition InsertBefore)
    : CastInst(Ty, Trunc, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal Trunc");
}

ZExtInst::ZExtInst(Value *S, Type *Ty, const Twine &Name,
                   InsertPosition InsertBefore)
    : CastInst(Ty, ZExt, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal ZExt");
}

SExtInst::SExtInst(Value *S, Type *Ty, const Twine &Name,
                   InsertPosition InsertBefore)
    : CastInst(Ty, SExt, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal SExt");
}

FPTruncInst::FPTruncInst(Value *S, Type *Ty, const Twine &Name,
                         InsertPosition InsertBefore)
    : CastInst(Ty, FPTrunc, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal FPTrunc");
}

FPExtInst::FPExtInst(Value *S, Type *Ty, const Twine &Name,
                     InsertPosition InsertBefore)
    : CastInst(Ty, FPExt, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal FPExt");
}

UIToFPInst::UIToFPInst(Value *S, Type *Ty, const Twine &Name,
                       InsertPosition InsertBefore)
    : CastInst(Ty, UIToFP, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal UIToFP");
}

SIToFPInst::SIToFPInst(Value *S, Type *Ty, const Twine &Name,
                       InsertPosition InsertBefore)
    : CastInst(Ty, SIToFP, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal SIToFP");
}

FPToUIInst::FPToUIInst(Value *S, Type *Ty, const Twine &Name,
                       InsertPosition InsertBefore)
    : CastInst(Ty, FPToUI, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal FPToUI");
}

FPToSIInst::FPToSIInst(Value *S, Type *Ty, const Twine &Name,
                       InsertPosition InsertBefore)
    : CastInst(Ty, FPToSI, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal FPToSI");
}

PtrToIntInst::PtrToIntInst(Value *S, Type *Ty, const Twine &Name,
                           InsertPosition InsertBefore)
    : CastInst(Ty, PtrToInt, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal PtrToInt");
}

IntToPtrInst::IntToPtrInst(Value *S, Type *Ty, const Twine &Name,
                           InsertPosition InsertBefore)
    : CastInst(Ty, IntToPtr, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal IntToPtr");
}

BitCastInst::BitCastInst(Value *S, Type *Ty, const Twine &Name,
                         InsertPosition InsertBefore)
    : CastInst(Ty, BitCast, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal BitCast");
}

AddrSpaceCastInst::AddrSpaceCastInst(Value *S, Type *Ty, const Twine &Name,
                                     InsertPosition InsertBefore)
    : CastInst(Ty, AddrSpaceCast, S, Name, InsertBefore) {
  assert(castIsValid(getOpcode(), S, Ty) && "Illegal AddrSpaceCast");
}

//===----------------------------------------------------------------------===//
//                               CmpInst Classes
//===----------------------------------------------------------------------===//

CmpInst::CmpInst(Type *ty, OtherOps op, Predicate predicate, Value *LHS,
                 Value *RHS, const Twine &Name, InsertPosition InsertBefore,
                 Instruction *FlagsSource)
    : Instruction(ty, op, OperandTraits<CmpInst>::op_begin(this),
                  OperandTraits<CmpInst>::operands(this), InsertBefore) {
  Op<0>() = LHS;
  Op<1>() = RHS;
  setPredicate((Predicate)predicate);
  setName(Name);
  if (FlagsSource)
    copyIRFlags(FlagsSource);
}

CmpInst *CmpInst::Create(OtherOps Op, Predicate predicate, Value *S1, Value *S2,
                         const Twine &Name, InsertPosition InsertBefore) {
  if (Op == Instruction::ICmp) {
    if (InsertBefore.isValid())
      return new ICmpInst(InsertBefore, CmpInst::Predicate(predicate),
                          S1, S2, Name);
    else
      return new ICmpInst(CmpInst::Predicate(predicate),
                          S1, S2, Name);
  }

  if (InsertBefore.isValid())
    return new FCmpInst(InsertBefore, CmpInst::Predicate(predicate),
                        S1, S2, Name);
  else
    return new FCmpInst(CmpInst::Predicate(predicate),
                        S1, S2, Name);
}

CmpInst *CmpInst::CreateWithCopiedFlags(OtherOps Op, Predicate Pred, Value *S1,
                                        Value *S2,
                                        const Instruction *FlagsSource,
                                        const Twine &Name,
                                        InsertPosition InsertBefore) {
  CmpInst *Inst = Create(Op, Pred, S1, S2, Name, InsertBefore);
  Inst->copyIRFlags(FlagsSource);
  return Inst;
}

void CmpInst::swapOperands() {
  if (ICmpInst *IC = dyn_cast<ICmpInst>(this))
    IC->swapOperands();
  else
    cast<FCmpInst>(this)->swapOperands();
}

bool CmpInst::isCommutative() const {
  if (const ICmpInst *IC = dyn_cast<ICmpInst>(this))
    return IC->isCommutative();
  return cast<FCmpInst>(this)->isCommutative();
}

bool CmpInst::isEquality(Predicate P) {
  if (ICmpInst::isIntPredicate(P))
    return ICmpInst::isEquality(P);
  if (FCmpInst::isFPPredicate(P))
    return FCmpInst::isEquality(P);
  llvm_unreachable("Unsupported predicate kind");
}

CmpInst::Predicate CmpInst::getInversePredicate(Predicate pred) {
  switch (pred) {
    default: llvm_unreachable("Unknown cmp predicate!");
    case ICMP_EQ: return ICMP_NE;
    case ICMP_NE: return ICMP_EQ;
    case ICMP_UGT: return ICMP_ULE;
    case ICMP_ULT: return ICMP_UGE;
    case ICMP_UGE: return ICMP_ULT;
    case ICMP_ULE: return ICMP_UGT;
    case ICMP_SGT: return ICMP_SLE;
    case ICMP_SLT: return ICMP_SGE;
    case ICMP_SGE: return ICMP_SLT;
    case ICMP_SLE: return ICMP_SGT;

    case FCMP_OEQ: return FCMP_UNE;
    case FCMP_ONE: return FCMP_UEQ;
    case FCMP_OGT: return FCMP_ULE;
    case FCMP_OLT: return FCMP_UGE;
    case FCMP_OGE: return FCMP_ULT;
    case FCMP_OLE: return FCMP_UGT;
    case FCMP_UEQ: return FCMP_ONE;
    case FCMP_UNE: return FCMP_OEQ;
    case FCMP_UGT: return FCMP_OLE;
    case FCMP_ULT: return FCMP_OGE;
    case FCMP_UGE: return FCMP_OLT;
    case FCMP_ULE: return FCMP_OGT;
    case FCMP_ORD: return FCMP_UNO;
    case FCMP_UNO: return FCMP_ORD;
    case FCMP_TRUE: return FCMP_FALSE;
    case FCMP_FALSE: return FCMP_TRUE;
  }
}

StringRef CmpInst::getPredicateName(Predicate Pred) {
  switch (Pred) {
  default:                   return "unknown";
  case FCmpInst::FCMP_FALSE: return "false";
  case FCmpInst::FCMP_OEQ:   return "oeq";
  case FCmpInst::FCMP_OGT:   return "ogt";
  case FCmpInst::FCMP_OGE:   return "oge";
  case FCmpInst::FCMP_OLT:   return "olt";
  case FCmpInst::FCMP_OLE:   return "ole";
  case FCmpInst::FCMP_ONE:   return "one";
  case FCmpInst::FCMP_ORD:   return "ord";
  case FCmpInst::FCMP_UNO:   return "uno";
  case FCmpInst::FCMP_UEQ:   return "ueq";
  case FCmpInst::FCMP_UGT:   return "ugt";
  case FCmpInst::FCMP_UGE:   return "uge";
  case FCmpInst::FCMP_ULT:   return "ult";
  case FCmpInst::FCMP_ULE:   return "ule";
  case FCmpInst::FCMP_UNE:   return "une";
  case FCmpInst::FCMP_TRUE:  return "true";
  case ICmpInst::ICMP_EQ:    return "eq";
  case ICmpInst::ICMP_NE:    return "ne";
  case ICmpInst::ICMP_SGT:   return "sgt";
  case ICmpInst::ICMP_SGE:   return "sge";
  case ICmpInst::ICMP_SLT:   return "slt";
  case ICmpInst::ICMP_SLE:   return "sle";
  case ICmpInst::ICMP_UGT:   return "ugt";
  case ICmpInst::ICMP_UGE:   return "uge";
  case ICmpInst::ICMP_ULT:   return "ult";
  case ICmpInst::ICMP_ULE:   return "ule";
  }
}

raw_ostream &llvm::operator<<(raw_ostream &OS, CmpInst::Predicate Pred) {
  OS << CmpInst::getPredicateName(Pred);
  return OS;
}

ICmpInst::Predicate ICmpInst::getSignedPredicate(Predicate pred) {
  switch (pred) {
    default: llvm_unreachable("Unknown icmp predicate!");
    case ICMP_EQ: case ICMP_NE:
    case ICMP_SGT: case ICMP_SLT: case ICMP_SGE: case ICMP_SLE:
       return pred;
    case ICMP_UGT: return ICMP_SGT;
    case ICMP_ULT: return ICMP_SLT;
    case ICMP_UGE: return ICMP_SGE;
    case ICMP_ULE: return ICMP_SLE;
  }
}

ICmpInst::Predicate ICmpInst::getUnsignedPredicate(Predicate pred) {
  switch (pred) {
    default: llvm_unreachable("Unknown icmp predicate!");
    case ICMP_EQ: case ICMP_NE:
    case ICMP_UGT: case ICMP_ULT: case ICMP_UGE: case ICMP_ULE:
       return pred;
    case ICMP_SGT: return ICMP_UGT;
    case ICMP_SLT: return ICMP_ULT;
    case ICMP_SGE: return ICMP_UGE;
    case ICMP_SLE: return ICMP_ULE;
  }
}

CmpInst::Predicate CmpInst::getSwappedPredicate(Predicate pred) {
  switch (pred) {
    default: llvm_unreachable("Unknown cmp predicate!");
    case ICMP_EQ: case ICMP_NE:
      return pred;
    case ICMP_SGT: return ICMP_SLT;
    case ICMP_SLT: return ICMP_SGT;
    case ICMP_SGE: return ICMP_SLE;
    case ICMP_SLE: return ICMP_SGE;
    case ICMP_UGT: return ICMP_ULT;
    case ICMP_ULT: return ICMP_UGT;
    case ICMP_UGE: return ICMP_ULE;
    case ICMP_ULE: return ICMP_UGE;

    case FCMP_FALSE: case FCMP_TRUE:
    case FCMP_OEQ: case FCMP_ONE:
    case FCMP_UEQ: case FCMP_UNE:
    case FCMP_ORD: case FCMP_UNO:
      return pred;
    case FCMP_OGT: return FCMP_OLT;
    case FCMP_OLT: return FCMP_OGT;
    case FCMP_OGE: return FCMP_OLE;
    case FCMP_OLE: return FCMP_OGE;
    case FCMP_UGT: return FCMP_ULT;
    case FCMP_ULT: return FCMP_UGT;
    case FCMP_UGE: return FCMP_ULE;
    case FCMP_ULE: return FCMP_UGE;
  }
}

bool CmpInst::isNonStrictPredicate(Predicate pred) {
  switch (pred) {
  case ICMP_SGE:
  case ICMP_SLE:
  case ICMP_UGE:
  case ICMP_ULE:
  case FCMP_OGE:
  case FCMP_OLE:
  case FCMP_UGE:
  case FCMP_ULE:
    return true;
  default:
    return false;
  }
}

bool CmpInst::isStrictPredicate(Predicate pred) {
  switch (pred) {
  case ICMP_SGT:
  case ICMP_SLT:
  case ICMP_UGT:
  case ICMP_ULT:
  case FCMP_OGT:
  case FCMP_OLT:
  case FCMP_UGT:
  case FCMP_ULT:
    return true;
  default:
    return false;
  }
}

CmpInst::Predicate CmpInst::getStrictPredicate(Predicate pred) {
  switch (pred) {
  case ICMP_SGE:
    return ICMP_SGT;
  case ICMP_SLE:
    return ICMP_SLT;
  case ICMP_UGE:
    return ICMP_UGT;
  case ICMP_ULE:
    return ICMP_ULT;
  case FCMP_OGE:
    return FCMP_OGT;
  case FCMP_OLE:
    return FCMP_OLT;
  case FCMP_UGE:
    return FCMP_UGT;
  case FCMP_ULE:
    return FCMP_ULT;
  default:
    return pred;
  }
}

CmpInst::Predicate CmpInst::getNonStrictPredicate(Predicate pred) {
  switch (pred) {
  case ICMP_SGT:
    return ICMP_SGE;
  case ICMP_SLT:
    return ICMP_SLE;
  case ICMP_UGT:
    return ICMP_UGE;
  case ICMP_ULT:
    return ICMP_ULE;
  case FCMP_OGT:
    return FCMP_OGE;
  case FCMP_OLT:
    return FCMP_OLE;
  case FCMP_UGT:
    return FCMP_UGE;
  case FCMP_ULT:
    return FCMP_ULE;
  default:
    return pred;
  }
}

CmpInst::Predicate CmpInst::getFlippedStrictnessPredicate(Predicate pred) {
  assert(CmpInst::isRelational(pred) && "Call only with relational predicate!");

  if (isStrictPredicate(pred))
    return getNonStrictPredicate(pred);
  if (isNonStrictPredicate(pred))
    return getStrictPredicate(pred);

  llvm_unreachable("Unknown predicate!");
}

CmpInst::Predicate CmpInst::getSignedPredicate(Predicate pred) {
  assert(CmpInst::isUnsigned(pred) && "Call only with unsigned predicates!");

  switch (pred) {
  default:
    llvm_unreachable("Unknown predicate!");
  case CmpInst::ICMP_ULT:
    return CmpInst::ICMP_SLT;
  case CmpInst::ICMP_ULE:
    return CmpInst::ICMP_SLE;
  case CmpInst::ICMP_UGT:
    return CmpInst::ICMP_SGT;
  case CmpInst::ICMP_UGE:
    return CmpInst::ICMP_SGE;
  }
}

CmpInst::Predicate CmpInst::getUnsignedPredicate(Predicate pred) {
  assert(CmpInst::isSigned(pred) && "Call only with signed predicates!");

  switch (pred) {
  default:
    llvm_unreachable("Unknown predicate!");
  case CmpInst::ICMP_SLT:
    return CmpInst::ICMP_ULT;
  case CmpInst::ICMP_SLE:
    return CmpInst::ICMP_ULE;
  case CmpInst::ICMP_SGT:
    return CmpInst::ICMP_UGT;
  case CmpInst::ICMP_SGE:
    return CmpInst::ICMP_UGE;
  }
}

bool CmpInst::isUnsigned(Predicate predicate) {
  switch (predicate) {
    default: return false;
    case ICmpInst::ICMP_ULT: case ICmpInst::ICMP_ULE: case ICmpInst::ICMP_UGT:
    case ICmpInst::ICMP_UGE: return true;
  }
}

bool CmpInst::isSigned(Predicate predicate) {
  switch (predicate) {
    default: return false;
    case ICmpInst::ICMP_SLT: case ICmpInst::ICMP_SLE: case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_SGE: return true;
  }
}

bool ICmpInst::compare(const APInt &LHS, const APInt &RHS,
                       ICmpInst::Predicate Pred) {
  assert(ICmpInst::isIntPredicate(Pred) && "Only for integer predicates!");
  switch (Pred) {
  case ICmpInst::Predicate::ICMP_EQ:
    return LHS.eq(RHS);
  case ICmpInst::Predicate::ICMP_NE:
    return LHS.ne(RHS);
  case ICmpInst::Predicate::ICMP_UGT:
    return LHS.ugt(RHS);
  case ICmpInst::Predicate::ICMP_UGE:
    return LHS.uge(RHS);
  case ICmpInst::Predicate::ICMP_ULT:
    return LHS.ult(RHS);
  case ICmpInst::Predicate::ICMP_ULE:
    return LHS.ule(RHS);
  case ICmpInst::Predicate::ICMP_SGT:
    return LHS.sgt(RHS);
  case ICmpInst::Predicate::ICMP_SGE:
    return LHS.sge(RHS);
  case ICmpInst::Predicate::ICMP_SLT:
    return LHS.slt(RHS);
  case ICmpInst::Predicate::ICMP_SLE:
    return LHS.sle(RHS);
  default:
    llvm_unreachable("Unexpected non-integer predicate.");
  };
}

bool FCmpInst::compare(const APFloat &LHS, const APFloat &RHS,
                       FCmpInst::Predicate Pred) {
  APFloat::cmpResult R = LHS.compare(RHS);
  switch (Pred) {
  default:
    llvm_unreachable("Invalid FCmp Predicate");
  case FCmpInst::FCMP_FALSE:
    return false;
  case FCmpInst::FCMP_TRUE:
    return true;
  case FCmpInst::FCMP_UNO:
    return R == APFloat::cmpUnordered;
  case FCmpInst::FCMP_ORD:
    return R != APFloat::cmpUnordered;
  case FCmpInst::FCMP_UEQ:
    return R == APFloat::cmpUnordered || R == APFloat::cmpEqual;
  case FCmpInst::FCMP_OEQ:
    return R == APFloat::cmpEqual;
  case FCmpInst::FCMP_UNE:
    return R != APFloat::cmpEqual;
  case FCmpInst::FCMP_ONE:
    return R == APFloat::cmpLessThan || R == APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_ULT:
    return R == APFloat::cmpUnordered || R == APFloat::cmpLessThan;
  case FCmpInst::FCMP_OLT:
    return R == APFloat::cmpLessThan;
  case FCmpInst::FCMP_UGT:
    return R == APFloat::cmpUnordered || R == APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_OGT:
    return R == APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_ULE:
    return R != APFloat::cmpGreaterThan;
  case FCmpInst::FCMP_OLE:
    return R == APFloat::cmpLessThan || R == APFloat::cmpEqual;
  case FCmpInst::FCMP_UGE:
    return R != APFloat::cmpLessThan;
  case FCmpInst::FCMP_OGE:
    return R == APFloat::cmpGreaterThan || R == APFloat::cmpEqual;
  }
}

CmpInst::Predicate CmpInst::getFlippedSignednessPredicate(Predicate pred) {
  assert(CmpInst::isRelational(pred) &&
         "Call only with non-equality predicates!");

  if (isSigned(pred))
    return getUnsignedPredicate(pred);
  if (isUnsigned(pred))
    return getSignedPredicate(pred);

  llvm_unreachable("Unknown predicate!");
}

bool CmpInst::isOrdered(Predicate predicate) {
  switch (predicate) {
    default: return false;
    case FCmpInst::FCMP_OEQ: case FCmpInst::FCMP_ONE: case FCmpInst::FCMP_OGT:
    case FCmpInst::FCMP_OLT: case FCmpInst::FCMP_OGE: case FCmpInst::FCMP_OLE:
    case FCmpInst::FCMP_ORD: return true;
  }
}

bool CmpInst::isUnordered(Predicate predicate) {
  switch (predicate) {
    default: return false;
    case FCmpInst::FCMP_UEQ: case FCmpInst::FCMP_UNE: case FCmpInst::FCMP_UGT:
    case FCmpInst::FCMP_ULT: case FCmpInst::FCMP_UGE: case FCmpInst::FCMP_ULE:
    case FCmpInst::FCMP_UNO: return true;
  }
}

bool CmpInst::isTrueWhenEqual(Predicate predicate) {
  switch(predicate) {
    default: return false;
    case ICMP_EQ:   case ICMP_UGE: case ICMP_ULE: case ICMP_SGE: case ICMP_SLE:
    case FCMP_TRUE: case FCMP_UEQ: case FCMP_UGE: case FCMP_ULE: return true;
  }
}

bool CmpInst::isFalseWhenEqual(Predicate predicate) {
  switch(predicate) {
  case ICMP_NE:    case ICMP_UGT: case ICMP_ULT: case ICMP_SGT: case ICMP_SLT:
  case FCMP_FALSE: case FCMP_ONE: case FCMP_OGT: case FCMP_OLT: return true;
  default: return false;
  }
}

bool CmpInst::isImpliedTrueByMatchingCmp(Predicate Pred1, Predicate Pred2) {
  // If the predicates match, then we know the first condition implies the
  // second is true.
  if (Pred1 == Pred2)
    return true;

  switch (Pred1) {
  default:
    break;
  case ICMP_EQ:
    // A == B implies A >=u B, A <=u B, A >=s B, and A <=s B are true.
    return Pred2 == ICMP_UGE || Pred2 == ICMP_ULE || Pred2 == ICMP_SGE ||
           Pred2 == ICMP_SLE;
  case ICMP_UGT: // A >u B implies A != B and A >=u B are true.
    return Pred2 == ICMP_NE || Pred2 == ICMP_UGE;
  case ICMP_ULT: // A <u B implies A != B and A <=u B are true.
    return Pred2 == ICMP_NE || Pred2 == ICMP_ULE;
  case ICMP_SGT: // A >s B implies A != B and A >=s B are true.
    return Pred2 == ICMP_NE || Pred2 == ICMP_SGE;
  case ICMP_SLT: // A <s B implies A != B and A <=s B are true.
    return Pred2 == ICMP_NE || Pred2 == ICMP_SLE;
  }
  return false;
}

bool CmpInst::isImpliedFalseByMatchingCmp(Predicate Pred1, Predicate Pred2) {
  return isImpliedTrueByMatchingCmp(Pred1, getInversePredicate(Pred2));
}

//===----------------------------------------------------------------------===//
//                        SwitchInst Implementation
//===----------------------------------------------------------------------===//

void SwitchInst::init(Value *Value, BasicBlock *Default, unsigned NumReserved) {
  assert(Value && Default && NumReserved);
  ReservedSpace = NumReserved;
  setNumHungOffUseOperands(2);
  allocHungoffUses(ReservedSpace);

  Op<0>() = Value;
  Op<1>() = Default;
}

/// SwitchInst ctor - Create a new switch instruction, specifying a value to
/// switch on and a default destination.  The number of additional cases can
/// be specified here to make memory allocation more efficient.  This
/// constructor can also autoinsert before another instruction.
SwitchInst::SwitchInst(Value *Value, BasicBlock *Default, unsigned NumCases,
                       InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(Value->getContext()), Instruction::Switch,
                  nullptr, 0, InsertBefore) {
  init(Value, Default, 2+NumCases*2);
}

SwitchInst::SwitchInst(const SwitchInst &SI)
    : Instruction(SI.getType(), Instruction::Switch, nullptr, 0) {
  init(SI.getCondition(), SI.getDefaultDest(), SI.getNumOperands());
  setNumHungOffUseOperands(SI.getNumOperands());
  Use *OL = getOperandList();
  const Use *InOL = SI.getOperandList();
  for (unsigned i = 2, E = SI.getNumOperands(); i != E; i += 2) {
    OL[i] = InOL[i];
    OL[i+1] = InOL[i+1];
  }
  SubclassOptionalData = SI.SubclassOptionalData;
}

/// addCase - Add an entry to the switch instruction...
///
void SwitchInst::addCase(ConstantInt *OnVal, BasicBlock *Dest) {
  unsigned NewCaseIdx = getNumCases();
  unsigned OpNo = getNumOperands();
  if (OpNo+2 > ReservedSpace)
    growOperands();  // Get more space!
  // Initialize some new operands.
  assert(OpNo+1 < ReservedSpace && "Growing didn't work!");
  setNumHungOffUseOperands(OpNo+2);
  CaseHandle Case(this, NewCaseIdx);
  Case.setValue(OnVal);
  Case.setSuccessor(Dest);
}

/// removeCase - This method removes the specified case and its successor
/// from the switch instruction.
SwitchInst::CaseIt SwitchInst::removeCase(CaseIt I) {
  unsigned idx = I->getCaseIndex();

  assert(2 + idx*2 < getNumOperands() && "Case index out of range!!!");

  unsigned NumOps = getNumOperands();
  Use *OL = getOperandList();

  // Overwrite this case with the end of the list.
  if (2 + (idx + 1) * 2 != NumOps) {
    OL[2 + idx * 2] = OL[NumOps - 2];
    OL[2 + idx * 2 + 1] = OL[NumOps - 1];
  }

  // Nuke the last value.
  OL[NumOps-2].set(nullptr);
  OL[NumOps-2+1].set(nullptr);
  setNumHungOffUseOperands(NumOps-2);

  return CaseIt(this, idx);
}

/// growOperands - grow operands - This grows the operand list in response
/// to a push_back style of operation.  This grows the number of ops by 3 times.
///
void SwitchInst::growOperands() {
  unsigned e = getNumOperands();
  unsigned NumOps = e*3;

  ReservedSpace = NumOps;
  growHungoffUses(ReservedSpace);
}

MDNode *SwitchInstProfUpdateWrapper::buildProfBranchWeightsMD() {
  assert(Changed && "called only if metadata has changed");

  if (!Weights)
    return nullptr;

  assert(SI.getNumSuccessors() == Weights->size() &&
         "num of prof branch_weights must accord with num of successors");

  bool AllZeroes = all_of(*Weights, [](uint32_t W) { return W == 0; });

  if (AllZeroes || Weights->size() < 2)
    return nullptr;

  return MDBuilder(SI.getParent()->getContext()).createBranchWeights(*Weights);
}

void SwitchInstProfUpdateWrapper::init() {
  MDNode *ProfileData = getBranchWeightMDNode(SI);
  if (!ProfileData)
    return;

  if (getNumBranchWeights(*ProfileData) != SI.getNumSuccessors()) {
    llvm_unreachable("number of prof branch_weights metadata operands does "
                     "not correspond to number of succesors");
  }

  SmallVector<uint32_t, 8> Weights;
  if (!extractBranchWeights(ProfileData, Weights))
    return;
  this->Weights = std::move(Weights);
}

SwitchInst::CaseIt
SwitchInstProfUpdateWrapper::removeCase(SwitchInst::CaseIt I) {
  if (Weights) {
    assert(SI.getNumSuccessors() == Weights->size() &&
           "num of prof branch_weights must accord with num of successors");
    Changed = true;
    // Copy the last case to the place of the removed one and shrink.
    // This is tightly coupled with the way SwitchInst::removeCase() removes
    // the cases in SwitchInst::removeCase(CaseIt).
    (*Weights)[I->getCaseIndex() + 1] = Weights->back();
    Weights->pop_back();
  }
  return SI.removeCase(I);
}

void SwitchInstProfUpdateWrapper::addCase(
    ConstantInt *OnVal, BasicBlock *Dest,
    SwitchInstProfUpdateWrapper::CaseWeightOpt W) {
  SI.addCase(OnVal, Dest);

  if (!Weights && W && *W) {
    Changed = true;
    Weights = SmallVector<uint32_t, 8>(SI.getNumSuccessors(), 0);
    (*Weights)[SI.getNumSuccessors() - 1] = *W;
  } else if (Weights) {
    Changed = true;
    Weights->push_back(W.value_or(0));
  }
  if (Weights)
    assert(SI.getNumSuccessors() == Weights->size() &&
           "num of prof branch_weights must accord with num of successors");
}

Instruction::InstListType::iterator
SwitchInstProfUpdateWrapper::eraseFromParent() {
  // Instruction is erased. Mark as unchanged to not touch it in the destructor.
  Changed = false;
  if (Weights)
    Weights->resize(0);
  return SI.eraseFromParent();
}

SwitchInstProfUpdateWrapper::CaseWeightOpt
SwitchInstProfUpdateWrapper::getSuccessorWeight(unsigned idx) {
  if (!Weights)
    return std::nullopt;
  return (*Weights)[idx];
}

void SwitchInstProfUpdateWrapper::setSuccessorWeight(
    unsigned idx, SwitchInstProfUpdateWrapper::CaseWeightOpt W) {
  if (!W)
    return;

  if (!Weights && *W)
    Weights = SmallVector<uint32_t, 8>(SI.getNumSuccessors(), 0);

  if (Weights) {
    auto &OldW = (*Weights)[idx];
    if (*W != OldW) {
      Changed = true;
      OldW = *W;
    }
  }
}

SwitchInstProfUpdateWrapper::CaseWeightOpt
SwitchInstProfUpdateWrapper::getSuccessorWeight(const SwitchInst &SI,
                                                unsigned idx) {
  if (MDNode *ProfileData = getBranchWeightMDNode(SI))
    if (ProfileData->getNumOperands() == SI.getNumSuccessors() + 1)
      return mdconst::extract<ConstantInt>(ProfileData->getOperand(idx + 1))
          ->getValue()
          .getZExtValue();

  return std::nullopt;
}

//===----------------------------------------------------------------------===//
//                        IndirectBrInst Implementation
//===----------------------------------------------------------------------===//

void IndirectBrInst::init(Value *Address, unsigned NumDests) {
  assert(Address && Address->getType()->isPointerTy() &&
         "Address of indirectbr must be a pointer");
  ReservedSpace = 1+NumDests;
  setNumHungOffUseOperands(1);
  allocHungoffUses(ReservedSpace);

  Op<0>() = Address;
}


/// growOperands - grow operands - This grows the operand list in response
/// to a push_back style of operation.  This grows the number of ops by 2 times.
///
void IndirectBrInst::growOperands() {
  unsigned e = getNumOperands();
  unsigned NumOps = e*2;

  ReservedSpace = NumOps;
  growHungoffUses(ReservedSpace);
}

IndirectBrInst::IndirectBrInst(Value *Address, unsigned NumCases,
                               InsertPosition InsertBefore)
    : Instruction(Type::getVoidTy(Address->getContext()),
                  Instruction::IndirectBr, nullptr, 0, InsertBefore) {
  init(Address, NumCases);
}

IndirectBrInst::IndirectBrInst(const IndirectBrInst &IBI)
    : Instruction(Type::getVoidTy(IBI.getContext()), Instruction::IndirectBr,
                  nullptr, IBI.getNumOperands()) {
  allocHungoffUses(IBI.getNumOperands());
  Use *OL = getOperandList();
  const Use *InOL = IBI.getOperandList();
  for (unsigned i = 0, E = IBI.getNumOperands(); i != E; ++i)
    OL[i] = InOL[i];
  SubclassOptionalData = IBI.SubclassOptionalData;
}

/// addDestination - Add a destination.
///
void IndirectBrInst::addDestination(BasicBlock *DestBB) {
  unsigned OpNo = getNumOperands();
  if (OpNo+1 > ReservedSpace)
    growOperands();  // Get more space!
  // Initialize some new operands.
  assert(OpNo < ReservedSpace && "Growing didn't work!");
  setNumHungOffUseOperands(OpNo+1);
  getOperandList()[OpNo] = DestBB;
}

/// removeDestination - This method removes the specified successor from the
/// indirectbr instruction.
void IndirectBrInst::removeDestination(unsigned idx) {
  assert(idx < getNumOperands()-1 && "Successor index out of range!");

  unsigned NumOps = getNumOperands();
  Use *OL = getOperandList();

  // Replace this value with the last one.
  OL[idx+1] = OL[NumOps-1];

  // Nuke the last value.
  OL[NumOps-1].set(nullptr);
  setNumHungOffUseOperands(NumOps-1);
}

//===----------------------------------------------------------------------===//
//                            FreezeInst Implementation
//===----------------------------------------------------------------------===//

FreezeInst::FreezeInst(Value *S, const Twine &Name, InsertPosition InsertBefore)
    : UnaryInstruction(S->getType(), Freeze, S, InsertBefore) {
  setName(Name);
}

//===----------------------------------------------------------------------===//
//                           cloneImpl() implementations
//===----------------------------------------------------------------------===//

// Define these methods here so vtables don't get emitted into every translation
// unit that uses these classes.

GetElementPtrInst *GetElementPtrInst::cloneImpl() const {
  return new (getNumOperands()) GetElementPtrInst(*this);
}

UnaryOperator *UnaryOperator::cloneImpl() const {
  return Create(getOpcode(), Op<0>());
}

BinaryOperator *BinaryOperator::cloneImpl() const {
  return Create(getOpcode(), Op<0>(), Op<1>());
}

FCmpInst *FCmpInst::cloneImpl() const {
  return new FCmpInst(getPredicate(), Op<0>(), Op<1>());
}

ICmpInst *ICmpInst::cloneImpl() const {
  return new ICmpInst(getPredicate(), Op<0>(), Op<1>());
}

ExtractValueInst *ExtractValueInst::cloneImpl() const {
  return new ExtractValueInst(*this);
}

InsertValueInst *InsertValueInst::cloneImpl() const {
  return new InsertValueInst(*this);
}

AllocaInst *AllocaInst::cloneImpl() const {
  AllocaInst *Result = new AllocaInst(getAllocatedType(), getAddressSpace(),
                                      getOperand(0), getAlign());
  Result->setUsedWithInAlloca(isUsedWithInAlloca());
  Result->setSwiftError(isSwiftError());
  return Result;
}

LoadInst *LoadInst::cloneImpl() const {
  return new LoadInst(getType(), getOperand(0), Twine(), isVolatile(),
                      getAlign(), getOrdering(), getSyncScopeID());
}

StoreInst *StoreInst::cloneImpl() const {
  return new StoreInst(getOperand(0), getOperand(1), isVolatile(), getAlign(),
                       getOrdering(), getSyncScopeID());
}

AtomicCmpXchgInst *AtomicCmpXchgInst::cloneImpl() const {
  AtomicCmpXchgInst *Result = new AtomicCmpXchgInst(
      getOperand(0), getOperand(1), getOperand(2), getAlign(),
      getSuccessOrdering(), getFailureOrdering(), getSyncScopeID());
  Result->setVolatile(isVolatile());
  Result->setWeak(isWeak());
  return Result;
}

AtomicRMWInst *AtomicRMWInst::cloneImpl() const {
  AtomicRMWInst *Result =
      new AtomicRMWInst(getOperation(), getOperand(0), getOperand(1),
                        getAlign(), getOrdering(), getSyncScopeID());
  Result->setVolatile(isVolatile());
  return Result;
}

FenceInst *FenceInst::cloneImpl() const {
  return new FenceInst(getContext(), getOrdering(), getSyncScopeID());
}

TruncInst *TruncInst::cloneImpl() const {
  return new TruncInst(getOperand(0), getType());
}

ZExtInst *ZExtInst::cloneImpl() const {
  return new ZExtInst(getOperand(0), getType());
}

SExtInst *SExtInst::cloneImpl() const {
  return new SExtInst(getOperand(0), getType());
}

FPTruncInst *FPTruncInst::cloneImpl() const {
  return new FPTruncInst(getOperand(0), getType());
}

FPExtInst *FPExtInst::cloneImpl() const {
  return new FPExtInst(getOperand(0), getType());
}

UIToFPInst *UIToFPInst::cloneImpl() const {
  return new UIToFPInst(getOperand(0), getType());
}

SIToFPInst *SIToFPInst::cloneImpl() const {
  return new SIToFPInst(getOperand(0), getType());
}

FPToUIInst *FPToUIInst::cloneImpl() const {
  return new FPToUIInst(getOperand(0), getType());
}

FPToSIInst *FPToSIInst::cloneImpl() const {
  return new FPToSIInst(getOperand(0), getType());
}

PtrToIntInst *PtrToIntInst::cloneImpl() const {
  return new PtrToIntInst(getOperand(0), getType());
}

IntToPtrInst *IntToPtrInst::cloneImpl() const {
  return new IntToPtrInst(getOperand(0), getType());
}

BitCastInst *BitCastInst::cloneImpl() const {
  return new BitCastInst(getOperand(0), getType());
}

AddrSpaceCastInst *AddrSpaceCastInst::cloneImpl() const {
  return new AddrSpaceCastInst(getOperand(0), getType());
}

CallInst *CallInst::cloneImpl() const {
  if (hasOperandBundles()) {
    unsigned DescriptorBytes = getNumOperandBundles() * sizeof(BundleOpInfo);
    return new(getNumOperands(), DescriptorBytes) CallInst(*this);
  }
  return  new(getNumOperands()) CallInst(*this);
}

SelectInst *SelectInst::cloneImpl() const {
  return SelectInst::Create(getOperand(0), getOperand(1), getOperand(2));
}

VAArgInst *VAArgInst::cloneImpl() const {
  return new VAArgInst(getOperand(0), getType());
}

ExtractElementInst *ExtractElementInst::cloneImpl() const {
  return ExtractElementInst::Create(getOperand(0), getOperand(1));
}

InsertElementInst *InsertElementInst::cloneImpl() const {
  return InsertElementInst::Create(getOperand(0), getOperand(1), getOperand(2));
}

ShuffleVectorInst *ShuffleVectorInst::cloneImpl() const {
  return new ShuffleVectorInst(getOperand(0), getOperand(1), getShuffleMask());
}

PHINode *PHINode::cloneImpl() const { return new PHINode(*this); }

LandingPadInst *LandingPadInst::cloneImpl() const {
  return new LandingPadInst(*this);
}

ReturnInst *ReturnInst::cloneImpl() const {
  return new(getNumOperands()) ReturnInst(*this);
}

BranchInst *BranchInst::cloneImpl() const {
  return new(getNumOperands()) BranchInst(*this);
}

SwitchInst *SwitchInst::cloneImpl() const { return new SwitchInst(*this); }

IndirectBrInst *IndirectBrInst::cloneImpl() const {
  return new IndirectBrInst(*this);
}

InvokeInst *InvokeInst::cloneImpl() const {
  if (hasOperandBundles()) {
    unsigned DescriptorBytes = getNumOperandBundles() * sizeof(BundleOpInfo);
    return new(getNumOperands(), DescriptorBytes) InvokeInst(*this);
  }
  return new(getNumOperands()) InvokeInst(*this);
}

CallBrInst *CallBrInst::cloneImpl() const {
  if (hasOperandBundles()) {
    unsigned DescriptorBytes = getNumOperandBundles() * sizeof(BundleOpInfo);
    return new (getNumOperands(), DescriptorBytes) CallBrInst(*this);
  }
  return new (getNumOperands()) CallBrInst(*this);
}

ResumeInst *ResumeInst::cloneImpl() const { return new (1) ResumeInst(*this); }

CleanupReturnInst *CleanupReturnInst::cloneImpl() const {
  return new (getNumOperands()) CleanupReturnInst(*this);
}

CatchReturnInst *CatchReturnInst::cloneImpl() const {
  return new (getNumOperands()) CatchReturnInst(*this);
}

CatchSwitchInst *CatchSwitchInst::cloneImpl() const {
  return new CatchSwitchInst(*this);
}

FuncletPadInst *FuncletPadInst::cloneImpl() const {
  return new (getNumOperands()) FuncletPadInst(*this);
}

UnreachableInst *UnreachableInst::cloneImpl() const {
  LLVMContext &Context = getContext();
  return new UnreachableInst(Context);
}

FreezeInst *FreezeInst::cloneImpl() const {
  return new FreezeInst(getOperand(0));
}
