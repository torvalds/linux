//===- CallPromotionUtils.cpp - Utilities for call promotion ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities useful for promoting indirect call sites to
// direct call sites.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CallPromotionUtils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/TypeMetadataUtils.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

#define DEBUG_TYPE "call-promotion-utils"

/// Fix-up phi nodes in an invoke instruction's normal destination.
///
/// After versioning an invoke instruction, values coming from the original
/// block will now be coming from the "merge" block. For example, in the code
/// below:
///
///   then_bb:
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   merge_bb:
///     %t2 = phi i32 [ %t0, %then_bb ], [ %t1, %else_bb ]
///     br %normal_dst
///
///   normal_dst:
///     %t3 = phi i32 [ %x, %orig_bb ], ...
///
/// "orig_bb" is no longer a predecessor of "normal_dst", so the phi nodes in
/// "normal_dst" must be fixed to refer to "merge_bb":
///
///    normal_dst:
///      %t3 = phi i32 [ %x, %merge_bb ], ...
///
static void fixupPHINodeForNormalDest(InvokeInst *Invoke, BasicBlock *OrigBlock,
                                      BasicBlock *MergeBlock) {
  for (PHINode &Phi : Invoke->getNormalDest()->phis()) {
    int Idx = Phi.getBasicBlockIndex(OrigBlock);
    if (Idx == -1)
      continue;
    Phi.setIncomingBlock(Idx, MergeBlock);
  }
}

/// Fix-up phi nodes in an invoke instruction's unwind destination.
///
/// After versioning an invoke instruction, values coming from the original
/// block will now be coming from either the "then" block or the "else" block.
/// For example, in the code below:
///
///   then_bb:
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   unwind_dst:
///     %t3 = phi i32 [ %x, %orig_bb ], ...
///
/// "orig_bb" is no longer a predecessor of "unwind_dst", so the phi nodes in
/// "unwind_dst" must be fixed to refer to "then_bb" and "else_bb":
///
///   unwind_dst:
///     %t3 = phi i32 [ %x, %then_bb ], [ %x, %else_bb ], ...
///
static void fixupPHINodeForUnwindDest(InvokeInst *Invoke, BasicBlock *OrigBlock,
                                      BasicBlock *ThenBlock,
                                      BasicBlock *ElseBlock) {
  for (PHINode &Phi : Invoke->getUnwindDest()->phis()) {
    int Idx = Phi.getBasicBlockIndex(OrigBlock);
    if (Idx == -1)
      continue;
    auto *V = Phi.getIncomingValue(Idx);
    Phi.setIncomingBlock(Idx, ThenBlock);
    Phi.addIncoming(V, ElseBlock);
  }
}

/// Create a phi node for the returned value of a call or invoke instruction.
///
/// After versioning a call or invoke instruction that returns a value, we have
/// to merge the value of the original and new instructions. We do this by
/// creating a phi node and replacing uses of the original instruction with this
/// phi node.
///
/// For example, if \p OrigInst is defined in "else_bb" and \p NewInst is
/// defined in "then_bb", we create the following phi node:
///
///   ; Uses of the original instruction are replaced by uses of the phi node.
///   %t0 = phi i32 [ %orig_inst, %else_bb ], [ %new_inst, %then_bb ],
///
static void createRetPHINode(Instruction *OrigInst, Instruction *NewInst,
                             BasicBlock *MergeBlock, IRBuilder<> &Builder) {

  if (OrigInst->getType()->isVoidTy() || OrigInst->use_empty())
    return;

  Builder.SetInsertPoint(MergeBlock, MergeBlock->begin());
  PHINode *Phi = Builder.CreatePHI(OrigInst->getType(), 0);
  SmallVector<User *, 16> UsersToUpdate(OrigInst->users());
  for (User *U : UsersToUpdate)
    U->replaceUsesOfWith(OrigInst, Phi);
  Phi->addIncoming(OrigInst, OrigInst->getParent());
  Phi->addIncoming(NewInst, NewInst->getParent());
}

/// Cast a call or invoke instruction to the given type.
///
/// When promoting a call site, the return type of the call site might not match
/// that of the callee. If this is the case, we have to cast the returned value
/// to the correct type. The location of the cast depends on if we have a call
/// or invoke instruction.
///
/// For example, if the call instruction below requires a bitcast after
/// promotion:
///
///   orig_bb:
///     %t0 = call i32 @func()
///     ...
///
/// The bitcast is placed after the call instruction:
///
///   orig_bb:
///     ; Uses of the original return value are replaced by uses of the bitcast.
///     %t0 = call i32 @func()
///     %t1 = bitcast i32 %t0 to ...
///     ...
///
/// A similar transformation is performed for invoke instructions. However,
/// since invokes are terminating, a new block is created for the bitcast. For
/// example, if the invoke instruction below requires a bitcast after promotion:
///
///   orig_bb:
///     %t0 = invoke i32 @func() to label %normal_dst unwind label %unwind_dst
///
/// The edge between the original block and the invoke's normal destination is
/// split, and the bitcast is placed there:
///
///   orig_bb:
///     %t0 = invoke i32 @func() to label %split_bb unwind label %unwind_dst
///
///   split_bb:
///     ; Uses of the original return value are replaced by uses of the bitcast.
///     %t1 = bitcast i32 %t0 to ...
///     br label %normal_dst
///
static void createRetBitCast(CallBase &CB, Type *RetTy, CastInst **RetBitCast) {

  // Save the users of the calling instruction. These uses will be changed to
  // use the bitcast after we create it.
  SmallVector<User *, 16> UsersToUpdate(CB.users());

  // Determine an appropriate location to create the bitcast for the return
  // value. The location depends on if we have a call or invoke instruction.
  BasicBlock::iterator InsertBefore;
  if (auto *Invoke = dyn_cast<InvokeInst>(&CB))
    InsertBefore =
        SplitEdge(Invoke->getParent(), Invoke->getNormalDest())->begin();
  else
    InsertBefore = std::next(CB.getIterator());

  // Bitcast the return value to the correct type.
  auto *Cast = CastInst::CreateBitOrPointerCast(&CB, RetTy, "", InsertBefore);
  if (RetBitCast)
    *RetBitCast = Cast;

  // Replace all the original uses of the calling instruction with the bitcast.
  for (User *U : UsersToUpdate)
    U->replaceUsesOfWith(&CB, Cast);
}

/// Predicate and clone the given call site.
///
/// This function creates an if-then-else structure at the location of the call
/// site. The "if" condition is specified by `Cond`.
/// The original call site is moved into the "else" block, and a clone of the
/// call site is placed in the "then" block. The cloned instruction is returned.
///
/// For example, the call instruction below:
///
///   orig_bb:
///     %t0 = call i32 %ptr()
///     ...
///
/// Is replace by the following:
///
///   orig_bb:
///     %cond = Cond
///     br i1 %cond, %then_bb, %else_bb
///
///   then_bb:
///     ; The clone of the original call instruction is placed in the "then"
///     ; block. It is not yet promoted.
///     %t1 = call i32 %ptr()
///     br merge_bb
///
///   else_bb:
///     ; The original call instruction is moved to the "else" block.
///     %t0 = call i32 %ptr()
///     br merge_bb
///
///   merge_bb:
///     ; Uses of the original call instruction are replaced by uses of the phi
///     ; node.
///     %t2 = phi i32 [ %t0, %else_bb ], [ %t1, %then_bb ]
///     ...
///
/// A similar transformation is performed for invoke instructions. However,
/// since invokes are terminating, more work is required. For example, the
/// invoke instruction below:
///
///   orig_bb:
///     %t0 = invoke %ptr() to label %normal_dst unwind label %unwind_dst
///
/// Is replace by the following:
///
///   orig_bb:
///     %cond = Cond
///     br i1 %cond, %then_bb, %else_bb
///
///   then_bb:
///     ; The clone of the original invoke instruction is placed in the "then"
///     ; block, and its normal destination is set to the "merge" block. It is
///     ; not yet promoted.
///     %t1 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   else_bb:
///     ; The original invoke instruction is moved into the "else" block, and
///     ; its normal destination is set to the "merge" block.
///     %t0 = invoke i32 %ptr() to label %merge_bb unwind label %unwind_dst
///
///   merge_bb:
///     ; Uses of the original invoke instruction are replaced by uses of the
///     ; phi node, and the merge block branches to the normal destination.
///     %t2 = phi i32 [ %t0, %else_bb ], [ %t1, %then_bb ]
///     br %normal_dst
///
/// An indirect musttail call is processed slightly differently in that:
/// 1. No merge block needed for the orginal and the cloned callsite, since
///    either one ends the flow. No phi node is needed either.
/// 2. The return statement following the original call site is duplicated too
///    and placed immediately after the cloned call site per the IR convention.
///
/// For example, the musttail call instruction below:
///
///   orig_bb:
///     %t0 = musttail call i32 %ptr()
///     ...
///
/// Is replaced by the following:
///
///   cond_bb:
///     %cond = Cond
///     br i1 %cond, %then_bb, %orig_bb
///
///   then_bb:
///     ; The clone of the original call instruction is placed in the "then"
///     ; block. It is not yet promoted.
///     %t1 = musttail call i32 %ptr()
///     ret %t1
///
///   orig_bb:
///     ; The original call instruction stays in its original block.
///     %t0 = musttail call i32 %ptr()
///     ret %t0
static CallBase &versionCallSiteWithCond(CallBase &CB, Value *Cond,
                                         MDNode *BranchWeights) {

  IRBuilder<> Builder(&CB);
  CallBase *OrigInst = &CB;
  BasicBlock *OrigBlock = OrigInst->getParent();

  if (OrigInst->isMustTailCall()) {
    // Create an if-then structure. The original instruction stays in its block,
    // and a clone of the original instruction is placed in the "then" block.
    Instruction *ThenTerm =
        SplitBlockAndInsertIfThen(Cond, &CB, false, BranchWeights);
    BasicBlock *ThenBlock = ThenTerm->getParent();
    ThenBlock->setName("if.true.direct_targ");
    CallBase *NewInst = cast<CallBase>(OrigInst->clone());
    NewInst->insertBefore(ThenTerm);

    // Place a clone of the optional bitcast after the new call site.
    Value *NewRetVal = NewInst;
    auto Next = OrigInst->getNextNode();
    if (auto *BitCast = dyn_cast_or_null<BitCastInst>(Next)) {
      assert(BitCast->getOperand(0) == OrigInst &&
             "bitcast following musttail call must use the call");
      auto NewBitCast = BitCast->clone();
      NewBitCast->replaceUsesOfWith(OrigInst, NewInst);
      NewBitCast->insertBefore(ThenTerm);
      NewRetVal = NewBitCast;
      Next = BitCast->getNextNode();
    }

    // Place a clone of the return instruction after the new call site.
    ReturnInst *Ret = dyn_cast_or_null<ReturnInst>(Next);
    assert(Ret && "musttail call must precede a ret with an optional bitcast");
    auto NewRet = Ret->clone();
    if (Ret->getReturnValue())
      NewRet->replaceUsesOfWith(Ret->getReturnValue(), NewRetVal);
    NewRet->insertBefore(ThenTerm);

    // A return instructions is terminating, so we don't need the terminator
    // instruction just created.
    ThenTerm->eraseFromParent();

    return *NewInst;
  }

  // Create an if-then-else structure. The original instruction is moved into
  // the "else" block, and a clone of the original instruction is placed in the
  // "then" block.
  Instruction *ThenTerm = nullptr;
  Instruction *ElseTerm = nullptr;
  SplitBlockAndInsertIfThenElse(Cond, &CB, &ThenTerm, &ElseTerm, BranchWeights);
  BasicBlock *ThenBlock = ThenTerm->getParent();
  BasicBlock *ElseBlock = ElseTerm->getParent();
  BasicBlock *MergeBlock = OrigInst->getParent();

  ThenBlock->setName("if.true.direct_targ");
  ElseBlock->setName("if.false.orig_indirect");
  MergeBlock->setName("if.end.icp");

  CallBase *NewInst = cast<CallBase>(OrigInst->clone());
  OrigInst->moveBefore(ElseTerm);
  NewInst->insertBefore(ThenTerm);

  // If the original call site is an invoke instruction, we have extra work to
  // do since invoke instructions are terminating. We have to fix-up phi nodes
  // in the invoke's normal and unwind destinations.
  if (auto *OrigInvoke = dyn_cast<InvokeInst>(OrigInst)) {
    auto *NewInvoke = cast<InvokeInst>(NewInst);

    // Invoke instructions are terminating, so we don't need the terminator
    // instructions that were just created.
    ThenTerm->eraseFromParent();
    ElseTerm->eraseFromParent();

    // Branch from the "merge" block to the original normal destination.
    Builder.SetInsertPoint(MergeBlock);
    Builder.CreateBr(OrigInvoke->getNormalDest());

    // Fix-up phi nodes in the original invoke's normal and unwind destinations.
    fixupPHINodeForNormalDest(OrigInvoke, OrigBlock, MergeBlock);
    fixupPHINodeForUnwindDest(OrigInvoke, MergeBlock, ThenBlock, ElseBlock);

    // Now set the normal destinations of the invoke instructions to be the
    // "merge" block.
    OrigInvoke->setNormalDest(MergeBlock);
    NewInvoke->setNormalDest(MergeBlock);
  }

  // Create a phi node for the returned value of the call site.
  createRetPHINode(OrigInst, NewInst, MergeBlock, Builder);

  return *NewInst;
}

// Predicate and clone the given call site using condition `CB.callee ==
// Callee`. See the comment `versionCallSiteWithCond` for the transformation.
CallBase &llvm::versionCallSite(CallBase &CB, Value *Callee,
                                MDNode *BranchWeights) {

  IRBuilder<> Builder(&CB);

  // Create the compare. The called value and callee must have the same type to
  // be compared.
  if (CB.getCalledOperand()->getType() != Callee->getType())
    Callee = Builder.CreateBitCast(Callee, CB.getCalledOperand()->getType());
  auto *Cond = Builder.CreateICmpEQ(CB.getCalledOperand(), Callee);

  return versionCallSiteWithCond(CB, Cond, BranchWeights);
}

bool llvm::isLegalToPromote(const CallBase &CB, Function *Callee,
                            const char **FailureReason) {
  assert(!CB.getCalledFunction() && "Only indirect call sites can be promoted");

  auto &DL = Callee->getDataLayout();

  // Check the return type. The callee's return value type must be bitcast
  // compatible with the call site's type.
  Type *CallRetTy = CB.getType();
  Type *FuncRetTy = Callee->getReturnType();
  if (CallRetTy != FuncRetTy)
    if (!CastInst::isBitOrNoopPointerCastable(FuncRetTy, CallRetTy, DL)) {
      if (FailureReason)
        *FailureReason = "Return type mismatch";
      return false;
    }

  // The number of formal arguments of the callee.
  unsigned NumParams = Callee->getFunctionType()->getNumParams();

  // The number of actual arguments in the call.
  unsigned NumArgs = CB.arg_size();

  // Check the number of arguments. The callee and call site must agree on the
  // number of arguments.
  if (NumArgs != NumParams && !Callee->isVarArg()) {
    if (FailureReason)
      *FailureReason = "The number of arguments mismatch";
    return false;
  }

  // Check the argument types. The callee's formal argument types must be
  // bitcast compatible with the corresponding actual argument types of the call
  // site.
  unsigned I = 0;
  for (; I < NumParams; ++I) {
    // Make sure that the callee and call agree on byval/inalloca. The types do
    // not have to match.
    if (Callee->hasParamAttribute(I, Attribute::ByVal) !=
        CB.getAttributes().hasParamAttr(I, Attribute::ByVal)) {
      if (FailureReason)
        *FailureReason = "byval mismatch";
      return false;
    }
    if (Callee->hasParamAttribute(I, Attribute::InAlloca) !=
        CB.getAttributes().hasParamAttr(I, Attribute::InAlloca)) {
      if (FailureReason)
        *FailureReason = "inalloca mismatch";
      return false;
    }

    Type *FormalTy = Callee->getFunctionType()->getFunctionParamType(I);
    Type *ActualTy = CB.getArgOperand(I)->getType();
    if (FormalTy == ActualTy)
      continue;
    if (!CastInst::isBitOrNoopPointerCastable(ActualTy, FormalTy, DL)) {
      if (FailureReason)
        *FailureReason = "Argument type mismatch";
      return false;
    }

    // MustTail call needs stricter type match. See
    // Verifier::verifyMustTailCall().
    if (CB.isMustTailCall()) {
      PointerType *PF = dyn_cast<PointerType>(FormalTy);
      PointerType *PA = dyn_cast<PointerType>(ActualTy);
      if (!PF || !PA || PF->getAddressSpace() != PA->getAddressSpace()) {
        if (FailureReason)
          *FailureReason = "Musttail call Argument type mismatch";
        return false;
      }
    }
  }
  for (; I < NumArgs; I++) {
    // Vararg functions can have more arguments than parameters.
    assert(Callee->isVarArg());
    if (CB.paramHasAttr(I, Attribute::StructRet)) {
      if (FailureReason)
        *FailureReason = "SRet arg to vararg function";
      return false;
    }
  }

  return true;
}

CallBase &llvm::promoteCall(CallBase &CB, Function *Callee,
                            CastInst **RetBitCast) {
  assert(!CB.getCalledFunction() && "Only indirect call sites can be promoted");

  // Set the called function of the call site to be the given callee (but don't
  // change the type).
  CB.setCalledOperand(Callee);

  // Since the call site will no longer be direct, we must clear metadata that
  // is only appropriate for indirect calls. This includes !prof and !callees
  // metadata.
  CB.setMetadata(LLVMContext::MD_prof, nullptr);
  CB.setMetadata(LLVMContext::MD_callees, nullptr);

  // If the function type of the call site matches that of the callee, no
  // additional work is required.
  if (CB.getFunctionType() == Callee->getFunctionType())
    return CB;

  // Save the return types of the call site and callee.
  Type *CallSiteRetTy = CB.getType();
  Type *CalleeRetTy = Callee->getReturnType();

  // Change the function type of the call site the match that of the callee.
  CB.mutateFunctionType(Callee->getFunctionType());

  // Inspect the arguments of the call site. If an argument's type doesn't
  // match the corresponding formal argument's type in the callee, bitcast it
  // to the correct type.
  auto CalleeType = Callee->getFunctionType();
  auto CalleeParamNum = CalleeType->getNumParams();

  LLVMContext &Ctx = Callee->getContext();
  const AttributeList &CallerPAL = CB.getAttributes();
  // The new list of argument attributes.
  SmallVector<AttributeSet, 4> NewArgAttrs;
  bool AttributeChanged = false;

  for (unsigned ArgNo = 0; ArgNo < CalleeParamNum; ++ArgNo) {
    auto *Arg = CB.getArgOperand(ArgNo);
    Type *FormalTy = CalleeType->getParamType(ArgNo);
    Type *ActualTy = Arg->getType();
    if (FormalTy != ActualTy) {
      auto *Cast =
          CastInst::CreateBitOrPointerCast(Arg, FormalTy, "", CB.getIterator());
      CB.setArgOperand(ArgNo, Cast);

      // Remove any incompatible attributes for the argument.
      AttrBuilder ArgAttrs(Ctx, CallerPAL.getParamAttrs(ArgNo));
      ArgAttrs.remove(AttributeFuncs::typeIncompatible(FormalTy));

      // We may have a different byval/inalloca type.
      if (ArgAttrs.getByValType())
        ArgAttrs.addByValAttr(Callee->getParamByValType(ArgNo));
      if (ArgAttrs.getInAllocaType())
        ArgAttrs.addInAllocaAttr(Callee->getParamInAllocaType(ArgNo));

      NewArgAttrs.push_back(AttributeSet::get(Ctx, ArgAttrs));
      AttributeChanged = true;
    } else
      NewArgAttrs.push_back(CallerPAL.getParamAttrs(ArgNo));
  }

  // If the return type of the call site doesn't match that of the callee, cast
  // the returned value to the appropriate type.
  // Remove any incompatible return value attribute.
  AttrBuilder RAttrs(Ctx, CallerPAL.getRetAttrs());
  if (!CallSiteRetTy->isVoidTy() && CallSiteRetTy != CalleeRetTy) {
    createRetBitCast(CB, CallSiteRetTy, RetBitCast);
    RAttrs.remove(AttributeFuncs::typeIncompatible(CalleeRetTy));
    AttributeChanged = true;
  }

  // Set the new callsite attribute.
  if (AttributeChanged)
    CB.setAttributes(AttributeList::get(Ctx, CallerPAL.getFnAttrs(),
                                        AttributeSet::get(Ctx, RAttrs),
                                        NewArgAttrs));

  return CB;
}

CallBase &llvm::promoteCallWithIfThenElse(CallBase &CB, Function *Callee,
                                          MDNode *BranchWeights) {

  // Version the indirect call site. If the called value is equal to the given
  // callee, 'NewInst' will be executed, otherwise the original call site will
  // be executed.
  CallBase &NewInst = versionCallSite(CB, Callee, BranchWeights);

  // Promote 'NewInst' so that it directly calls the desired function.
  return promoteCall(NewInst, Callee);
}

CallBase &llvm::promoteCallWithVTableCmp(CallBase &CB, Instruction *VPtr,
                                         Function *Callee,
                                         ArrayRef<Constant *> AddressPoints,
                                         MDNode *BranchWeights) {
  assert(!AddressPoints.empty() && "Caller should guarantee");
  IRBuilder<> Builder(&CB);
  SmallVector<Value *, 2> ICmps;
  for (auto &AddressPoint : AddressPoints)
    ICmps.push_back(Builder.CreateICmpEQ(VPtr, AddressPoint));

  // TODO: Perform tree height reduction if the number of ICmps is high.
  Value *Cond = Builder.CreateOr(ICmps);

  // Version the indirect call site. If Cond is true, 'NewInst' will be
  // executed, otherwise the original call site will be executed.
  CallBase &NewInst = versionCallSiteWithCond(CB, Cond, BranchWeights);

  // Promote 'NewInst' so that it directly calls the desired function.
  return promoteCall(NewInst, Callee);
}

bool llvm::tryPromoteCall(CallBase &CB) {
  assert(!CB.getCalledFunction());
  Module *M = CB.getCaller()->getParent();
  const DataLayout &DL = M->getDataLayout();
  Value *Callee = CB.getCalledOperand();

  LoadInst *VTableEntryLoad = dyn_cast<LoadInst>(Callee);
  if (!VTableEntryLoad)
    return false; // Not a vtable entry load.
  Value *VTableEntryPtr = VTableEntryLoad->getPointerOperand();
  APInt VTableOffset(DL.getTypeSizeInBits(VTableEntryPtr->getType()), 0);
  Value *VTableBasePtr = VTableEntryPtr->stripAndAccumulateConstantOffsets(
      DL, VTableOffset, /* AllowNonInbounds */ true);
  LoadInst *VTablePtrLoad = dyn_cast<LoadInst>(VTableBasePtr);
  if (!VTablePtrLoad)
    return false; // Not a vtable load.
  Value *Object = VTablePtrLoad->getPointerOperand();
  APInt ObjectOffset(DL.getTypeSizeInBits(Object->getType()), 0);
  Value *ObjectBase = Object->stripAndAccumulateConstantOffsets(
      DL, ObjectOffset, /* AllowNonInbounds */ true);
  if (!(isa<AllocaInst>(ObjectBase) && ObjectOffset == 0))
    // Not an Alloca or the offset isn't zero.
    return false;

  // Look for the vtable pointer store into the object by the ctor.
  BasicBlock::iterator BBI(VTablePtrLoad);
  Value *VTablePtr = FindAvailableLoadedValue(
      VTablePtrLoad, VTablePtrLoad->getParent(), BBI, 0, nullptr, nullptr);
  if (!VTablePtr)
    return false; // No vtable found.
  APInt VTableOffsetGVBase(DL.getTypeSizeInBits(VTablePtr->getType()), 0);
  Value *VTableGVBase = VTablePtr->stripAndAccumulateConstantOffsets(
      DL, VTableOffsetGVBase, /* AllowNonInbounds */ true);
  GlobalVariable *GV = dyn_cast<GlobalVariable>(VTableGVBase);
  if (!(GV && GV->isConstant() && GV->hasDefinitiveInitializer()))
    // Not in the form of a global constant variable with an initializer.
    return false;

  APInt VTableGVOffset = VTableOffsetGVBase + VTableOffset;
  if (!(VTableGVOffset.getActiveBits() <= 64))
    return false; // Out of range.

  Function *DirectCallee = nullptr;
  std::tie(DirectCallee, std::ignore) =
      getFunctionAtVTableOffset(GV, VTableGVOffset.getZExtValue(), *M);
  if (!DirectCallee)
    return false; // No function pointer found.

  if (!isLegalToPromote(CB, DirectCallee))
    return false;

  // Success.
  promoteCall(CB, DirectCallee);
  return true;
}

#undef DEBUG_TYPE
