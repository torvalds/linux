//===- InstCombineCalls.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the visitCall, visitInvoke, and visitCallBr functions.
//
//===----------------------------------------------------------------------===//

#include "InstCombineInternal.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/InstructionSimplify.h"
#include "llvm/Analysis/Loads.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/InstCombine/InstCombiner.h"
#include "llvm/Transforms/Utils/AssumeBundleBuilder.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SimplifyLibCalls.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#define DEBUG_TYPE "instcombine"
#include "llvm/Transforms/Utils/InstructionWorklist.h"

using namespace llvm;
using namespace PatternMatch;

STATISTIC(NumSimplified, "Number of library calls simplified");

static cl::opt<unsigned> GuardWideningWindow(
    "instcombine-guard-widening-window",
    cl::init(3),
    cl::desc("How wide an instruction window to bypass looking for "
             "another guard"));

/// Return the specified type promoted as it would be to pass though a va_arg
/// area.
static Type *getPromotedType(Type *Ty) {
  if (IntegerType* ITy = dyn_cast<IntegerType>(Ty)) {
    if (ITy->getBitWidth() < 32)
      return Type::getInt32Ty(Ty->getContext());
  }
  return Ty;
}

/// Recognize a memcpy/memmove from a trivially otherwise unused alloca.
/// TODO: This should probably be integrated with visitAllocSites, but that
/// requires a deeper change to allow either unread or unwritten objects.
static bool hasUndefSource(AnyMemTransferInst *MI) {
  auto *Src = MI->getRawSource();
  while (isa<GetElementPtrInst>(Src) || isa<BitCastInst>(Src)) {
    if (!Src->hasOneUse())
      return false;
    Src = cast<Instruction>(Src)->getOperand(0);
  }
  return isa<AllocaInst>(Src) && Src->hasOneUse();
}

Instruction *InstCombinerImpl::SimplifyAnyMemTransfer(AnyMemTransferInst *MI) {
  Align DstAlign = getKnownAlignment(MI->getRawDest(), DL, MI, &AC, &DT);
  MaybeAlign CopyDstAlign = MI->getDestAlign();
  if (!CopyDstAlign || *CopyDstAlign < DstAlign) {
    MI->setDestAlignment(DstAlign);
    return MI;
  }

  Align SrcAlign = getKnownAlignment(MI->getRawSource(), DL, MI, &AC, &DT);
  MaybeAlign CopySrcAlign = MI->getSourceAlign();
  if (!CopySrcAlign || *CopySrcAlign < SrcAlign) {
    MI->setSourceAlignment(SrcAlign);
    return MI;
  }

  // If we have a store to a location which is known constant, we can conclude
  // that the store must be storing the constant value (else the memory
  // wouldn't be constant), and this must be a noop.
  if (!isModSet(AA->getModRefInfoMask(MI->getDest()))) {
    // Set the size of the copy to 0, it will be deleted on the next iteration.
    MI->setLength(Constant::getNullValue(MI->getLength()->getType()));
    return MI;
  }

  // If the source is provably undef, the memcpy/memmove doesn't do anything
  // (unless the transfer is volatile).
  if (hasUndefSource(MI) && !MI->isVolatile()) {
    // Set the size of the copy to 0, it will be deleted on the next iteration.
    MI->setLength(Constant::getNullValue(MI->getLength()->getType()));
    return MI;
  }

  // If MemCpyInst length is 1/2/4/8 bytes then replace memcpy with
  // load/store.
  ConstantInt *MemOpLength = dyn_cast<ConstantInt>(MI->getLength());
  if (!MemOpLength) return nullptr;

  // Source and destination pointer types are always "i8*" for intrinsic.  See
  // if the size is something we can handle with a single primitive load/store.
  // A single load+store correctly handles overlapping memory in the memmove
  // case.
  uint64_t Size = MemOpLength->getLimitedValue();
  assert(Size && "0-sized memory transferring should be removed already.");

  if (Size > 8 || (Size&(Size-1)))
    return nullptr;  // If not 1/2/4/8 bytes, exit.

  // If it is an atomic and alignment is less than the size then we will
  // introduce the unaligned memory access which will be later transformed
  // into libcall in CodeGen. This is not evident performance gain so disable
  // it now.
  if (isa<AtomicMemTransferInst>(MI))
    if (*CopyDstAlign < Size || *CopySrcAlign < Size)
      return nullptr;

  // Use an integer load+store unless we can find something better.
  IntegerType* IntType = IntegerType::get(MI->getContext(), Size<<3);

  // If the memcpy has metadata describing the members, see if we can get the
  // TBAA, scope and noalias tags describing our copy.
  AAMDNodes AACopyMD = MI->getAAMetadata().adjustForAccess(Size);

  Value *Src = MI->getArgOperand(1);
  Value *Dest = MI->getArgOperand(0);
  LoadInst *L = Builder.CreateLoad(IntType, Src);
  // Alignment from the mem intrinsic will be better, so use it.
  L->setAlignment(*CopySrcAlign);
  L->setAAMetadata(AACopyMD);
  MDNode *LoopMemParallelMD =
    MI->getMetadata(LLVMContext::MD_mem_parallel_loop_access);
  if (LoopMemParallelMD)
    L->setMetadata(LLVMContext::MD_mem_parallel_loop_access, LoopMemParallelMD);
  MDNode *AccessGroupMD = MI->getMetadata(LLVMContext::MD_access_group);
  if (AccessGroupMD)
    L->setMetadata(LLVMContext::MD_access_group, AccessGroupMD);

  StoreInst *S = Builder.CreateStore(L, Dest);
  // Alignment from the mem intrinsic will be better, so use it.
  S->setAlignment(*CopyDstAlign);
  S->setAAMetadata(AACopyMD);
  if (LoopMemParallelMD)
    S->setMetadata(LLVMContext::MD_mem_parallel_loop_access, LoopMemParallelMD);
  if (AccessGroupMD)
    S->setMetadata(LLVMContext::MD_access_group, AccessGroupMD);
  S->copyMetadata(*MI, LLVMContext::MD_DIAssignID);

  if (auto *MT = dyn_cast<MemTransferInst>(MI)) {
    // non-atomics can be volatile
    L->setVolatile(MT->isVolatile());
    S->setVolatile(MT->isVolatile());
  }
  if (isa<AtomicMemTransferInst>(MI)) {
    // atomics have to be unordered
    L->setOrdering(AtomicOrdering::Unordered);
    S->setOrdering(AtomicOrdering::Unordered);
  }

  // Set the size of the copy to 0, it will be deleted on the next iteration.
  MI->setLength(Constant::getNullValue(MemOpLength->getType()));
  return MI;
}

Instruction *InstCombinerImpl::SimplifyAnyMemSet(AnyMemSetInst *MI) {
  const Align KnownAlignment =
      getKnownAlignment(MI->getDest(), DL, MI, &AC, &DT);
  MaybeAlign MemSetAlign = MI->getDestAlign();
  if (!MemSetAlign || *MemSetAlign < KnownAlignment) {
    MI->setDestAlignment(KnownAlignment);
    return MI;
  }

  // If we have a store to a location which is known constant, we can conclude
  // that the store must be storing the constant value (else the memory
  // wouldn't be constant), and this must be a noop.
  if (!isModSet(AA->getModRefInfoMask(MI->getDest()))) {
    // Set the size of the copy to 0, it will be deleted on the next iteration.
    MI->setLength(Constant::getNullValue(MI->getLength()->getType()));
    return MI;
  }

  // Remove memset with an undef value.
  // FIXME: This is technically incorrect because it might overwrite a poison
  // value. Change to PoisonValue once #52930 is resolved.
  if (isa<UndefValue>(MI->getValue())) {
    // Set the size of the copy to 0, it will be deleted on the next iteration.
    MI->setLength(Constant::getNullValue(MI->getLength()->getType()));
    return MI;
  }

  // Extract the length and alignment and fill if they are constant.
  ConstantInt *LenC = dyn_cast<ConstantInt>(MI->getLength());
  ConstantInt *FillC = dyn_cast<ConstantInt>(MI->getValue());
  if (!LenC || !FillC || !FillC->getType()->isIntegerTy(8))
    return nullptr;
  const uint64_t Len = LenC->getLimitedValue();
  assert(Len && "0-sized memory setting should be removed already.");
  const Align Alignment = MI->getDestAlign().valueOrOne();

  // If it is an atomic and alignment is less than the size then we will
  // introduce the unaligned memory access which will be later transformed
  // into libcall in CodeGen. This is not evident performance gain so disable
  // it now.
  if (isa<AtomicMemSetInst>(MI))
    if (Alignment < Len)
      return nullptr;

  // memset(s,c,n) -> store s, c (for n=1,2,4,8)
  if (Len <= 8 && isPowerOf2_32((uint32_t)Len)) {
    Type *ITy = IntegerType::get(MI->getContext(), Len*8);  // n=1 -> i8.

    Value *Dest = MI->getDest();

    // Extract the fill value and store.
    const uint64_t Fill = FillC->getZExtValue()*0x0101010101010101ULL;
    Constant *FillVal = ConstantInt::get(ITy, Fill);
    StoreInst *S = Builder.CreateStore(FillVal, Dest, MI->isVolatile());
    S->copyMetadata(*MI, LLVMContext::MD_DIAssignID);
    auto replaceOpForAssignmentMarkers = [FillC, FillVal](auto *DbgAssign) {
      if (llvm::is_contained(DbgAssign->location_ops(), FillC))
        DbgAssign->replaceVariableLocationOp(FillC, FillVal);
    };
    for_each(at::getAssignmentMarkers(S), replaceOpForAssignmentMarkers);
    for_each(at::getDVRAssignmentMarkers(S), replaceOpForAssignmentMarkers);

    S->setAlignment(Alignment);
    if (isa<AtomicMemSetInst>(MI))
      S->setOrdering(AtomicOrdering::Unordered);

    // Set the size of the copy to 0, it will be deleted on the next iteration.
    MI->setLength(Constant::getNullValue(LenC->getType()));
    return MI;
  }

  return nullptr;
}

// TODO, Obvious Missing Transforms:
// * Narrow width by halfs excluding zero/undef lanes
Value *InstCombinerImpl::simplifyMaskedLoad(IntrinsicInst &II) {
  Value *LoadPtr = II.getArgOperand(0);
  const Align Alignment =
      cast<ConstantInt>(II.getArgOperand(1))->getAlignValue();

  // If the mask is all ones or undefs, this is a plain vector load of the 1st
  // argument.
  if (maskIsAllOneOrUndef(II.getArgOperand(2))) {
    LoadInst *L = Builder.CreateAlignedLoad(II.getType(), LoadPtr, Alignment,
                                            "unmaskedload");
    L->copyMetadata(II);
    return L;
  }

  // If we can unconditionally load from this address, replace with a
  // load/select idiom. TODO: use DT for context sensitive query
  if (isDereferenceablePointer(LoadPtr, II.getType(),
                               II.getDataLayout(), &II, &AC)) {
    LoadInst *LI = Builder.CreateAlignedLoad(II.getType(), LoadPtr, Alignment,
                                             "unmaskedload");
    LI->copyMetadata(II);
    return Builder.CreateSelect(II.getArgOperand(2), LI, II.getArgOperand(3));
  }

  return nullptr;
}

// TODO, Obvious Missing Transforms:
// * Single constant active lane -> store
// * Narrow width by halfs excluding zero/undef lanes
Instruction *InstCombinerImpl::simplifyMaskedStore(IntrinsicInst &II) {
  auto *ConstMask = dyn_cast<Constant>(II.getArgOperand(3));
  if (!ConstMask)
    return nullptr;

  // If the mask is all zeros, this instruction does nothing.
  if (ConstMask->isNullValue())
    return eraseInstFromFunction(II);

  // If the mask is all ones, this is a plain vector store of the 1st argument.
  if (ConstMask->isAllOnesValue()) {
    Value *StorePtr = II.getArgOperand(1);
    Align Alignment = cast<ConstantInt>(II.getArgOperand(2))->getAlignValue();
    StoreInst *S =
        new StoreInst(II.getArgOperand(0), StorePtr, false, Alignment);
    S->copyMetadata(II);
    return S;
  }

  if (isa<ScalableVectorType>(ConstMask->getType()))
    return nullptr;

  // Use masked off lanes to simplify operands via SimplifyDemandedVectorElts
  APInt DemandedElts = possiblyDemandedEltsInMask(ConstMask);
  APInt PoisonElts(DemandedElts.getBitWidth(), 0);
  if (Value *V = SimplifyDemandedVectorElts(II.getOperand(0), DemandedElts,
                                            PoisonElts))
    return replaceOperand(II, 0, V);

  return nullptr;
}

// TODO, Obvious Missing Transforms:
// * Single constant active lane load -> load
// * Dereferenceable address & few lanes -> scalarize speculative load/selects
// * Adjacent vector addresses -> masked.load
// * Narrow width by halfs excluding zero/undef lanes
// * Vector incrementing address -> vector masked load
Instruction *InstCombinerImpl::simplifyMaskedGather(IntrinsicInst &II) {
  auto *ConstMask = dyn_cast<Constant>(II.getArgOperand(2));
  if (!ConstMask)
    return nullptr;

  // Vector splat address w/known mask -> scalar load
  // Fold the gather to load the source vector first lane
  // because it is reloading the same value each time
  if (ConstMask->isAllOnesValue())
    if (auto *SplatPtr = getSplatValue(II.getArgOperand(0))) {
      auto *VecTy = cast<VectorType>(II.getType());
      const Align Alignment =
          cast<ConstantInt>(II.getArgOperand(1))->getAlignValue();
      LoadInst *L = Builder.CreateAlignedLoad(VecTy->getElementType(), SplatPtr,
                                              Alignment, "load.scalar");
      Value *Shuf =
          Builder.CreateVectorSplat(VecTy->getElementCount(), L, "broadcast");
      return replaceInstUsesWith(II, cast<Instruction>(Shuf));
    }

  return nullptr;
}

// TODO, Obvious Missing Transforms:
// * Single constant active lane -> store
// * Adjacent vector addresses -> masked.store
// * Narrow store width by halfs excluding zero/undef lanes
// * Vector incrementing address -> vector masked store
Instruction *InstCombinerImpl::simplifyMaskedScatter(IntrinsicInst &II) {
  auto *ConstMask = dyn_cast<Constant>(II.getArgOperand(3));
  if (!ConstMask)
    return nullptr;

  // If the mask is all zeros, a scatter does nothing.
  if (ConstMask->isNullValue())
    return eraseInstFromFunction(II);

  // Vector splat address -> scalar store
  if (auto *SplatPtr = getSplatValue(II.getArgOperand(1))) {
    // scatter(splat(value), splat(ptr), non-zero-mask) -> store value, ptr
    if (auto *SplatValue = getSplatValue(II.getArgOperand(0))) {
      if (maskContainsAllOneOrUndef(ConstMask)) {
        Align Alignment =
            cast<ConstantInt>(II.getArgOperand(2))->getAlignValue();
        StoreInst *S = new StoreInst(SplatValue, SplatPtr, /*IsVolatile=*/false,
                                     Alignment);
        S->copyMetadata(II);
        return S;
      }
    }
    // scatter(vector, splat(ptr), splat(true)) -> store extract(vector,
    // lastlane), ptr
    if (ConstMask->isAllOnesValue()) {
      Align Alignment = cast<ConstantInt>(II.getArgOperand(2))->getAlignValue();
      VectorType *WideLoadTy = cast<VectorType>(II.getArgOperand(1)->getType());
      ElementCount VF = WideLoadTy->getElementCount();
      Value *RunTimeVF = Builder.CreateElementCount(Builder.getInt32Ty(), VF);
      Value *LastLane = Builder.CreateSub(RunTimeVF, Builder.getInt32(1));
      Value *Extract =
          Builder.CreateExtractElement(II.getArgOperand(0), LastLane);
      StoreInst *S =
          new StoreInst(Extract, SplatPtr, /*IsVolatile=*/false, Alignment);
      S->copyMetadata(II);
      return S;
    }
  }
  if (isa<ScalableVectorType>(ConstMask->getType()))
    return nullptr;

  // Use masked off lanes to simplify operands via SimplifyDemandedVectorElts
  APInt DemandedElts = possiblyDemandedEltsInMask(ConstMask);
  APInt PoisonElts(DemandedElts.getBitWidth(), 0);
  if (Value *V = SimplifyDemandedVectorElts(II.getOperand(0), DemandedElts,
                                            PoisonElts))
    return replaceOperand(II, 0, V);
  if (Value *V = SimplifyDemandedVectorElts(II.getOperand(1), DemandedElts,
                                            PoisonElts))
    return replaceOperand(II, 1, V);

  return nullptr;
}

/// This function transforms launder.invariant.group and strip.invariant.group
/// like:
/// launder(launder(%x)) -> launder(%x)       (the result is not the argument)
/// launder(strip(%x)) -> launder(%x)
/// strip(strip(%x)) -> strip(%x)             (the result is not the argument)
/// strip(launder(%x)) -> strip(%x)
/// This is legal because it preserves the most recent information about
/// the presence or absence of invariant.group.
static Instruction *simplifyInvariantGroupIntrinsic(IntrinsicInst &II,
                                                    InstCombinerImpl &IC) {
  auto *Arg = II.getArgOperand(0);
  auto *StrippedArg = Arg->stripPointerCasts();
  auto *StrippedInvariantGroupsArg = StrippedArg;
  while (auto *Intr = dyn_cast<IntrinsicInst>(StrippedInvariantGroupsArg)) {
    if (Intr->getIntrinsicID() != Intrinsic::launder_invariant_group &&
        Intr->getIntrinsicID() != Intrinsic::strip_invariant_group)
      break;
    StrippedInvariantGroupsArg = Intr->getArgOperand(0)->stripPointerCasts();
  }
  if (StrippedArg == StrippedInvariantGroupsArg)
    return nullptr; // No launders/strips to remove.

  Value *Result = nullptr;

  if (II.getIntrinsicID() == Intrinsic::launder_invariant_group)
    Result = IC.Builder.CreateLaunderInvariantGroup(StrippedInvariantGroupsArg);
  else if (II.getIntrinsicID() == Intrinsic::strip_invariant_group)
    Result = IC.Builder.CreateStripInvariantGroup(StrippedInvariantGroupsArg);
  else
    llvm_unreachable(
        "simplifyInvariantGroupIntrinsic only handles launder and strip");
  if (Result->getType()->getPointerAddressSpace() !=
      II.getType()->getPointerAddressSpace())
    Result = IC.Builder.CreateAddrSpaceCast(Result, II.getType());

  return cast<Instruction>(Result);
}

static Instruction *foldCttzCtlz(IntrinsicInst &II, InstCombinerImpl &IC) {
  assert((II.getIntrinsicID() == Intrinsic::cttz ||
          II.getIntrinsicID() == Intrinsic::ctlz) &&
         "Expected cttz or ctlz intrinsic");
  bool IsTZ = II.getIntrinsicID() == Intrinsic::cttz;
  Value *Op0 = II.getArgOperand(0);
  Value *Op1 = II.getArgOperand(1);
  Value *X;
  // ctlz(bitreverse(x)) -> cttz(x)
  // cttz(bitreverse(x)) -> ctlz(x)
  if (match(Op0, m_BitReverse(m_Value(X)))) {
    Intrinsic::ID ID = IsTZ ? Intrinsic::ctlz : Intrinsic::cttz;
    Function *F = Intrinsic::getDeclaration(II.getModule(), ID, II.getType());
    return CallInst::Create(F, {X, II.getArgOperand(1)});
  }

  if (II.getType()->isIntOrIntVectorTy(1)) {
    // ctlz/cttz i1 Op0 --> not Op0
    if (match(Op1, m_Zero()))
      return BinaryOperator::CreateNot(Op0);
    // If zero is poison, then the input can be assumed to be "true", so the
    // instruction simplifies to "false".
    assert(match(Op1, m_One()) && "Expected ctlz/cttz operand to be 0 or 1");
    return IC.replaceInstUsesWith(II, ConstantInt::getNullValue(II.getType()));
  }

  // If ctlz/cttz is only used as a shift amount, set is_zero_poison to true.
  if (II.hasOneUse() && match(Op1, m_Zero()) &&
      match(II.user_back(), m_Shift(m_Value(), m_Specific(&II)))) {
    II.dropUBImplyingAttrsAndMetadata();
    return IC.replaceOperand(II, 1, IC.Builder.getTrue());
  }

  Constant *C;

  if (IsTZ) {
    // cttz(-x) -> cttz(x)
    if (match(Op0, m_Neg(m_Value(X))))
      return IC.replaceOperand(II, 0, X);

    // cttz(-x & x) -> cttz(x)
    if (match(Op0, m_c_And(m_Neg(m_Value(X)), m_Deferred(X))))
      return IC.replaceOperand(II, 0, X);

    // cttz(sext(x)) -> cttz(zext(x))
    if (match(Op0, m_OneUse(m_SExt(m_Value(X))))) {
      auto *Zext = IC.Builder.CreateZExt(X, II.getType());
      auto *CttzZext =
          IC.Builder.CreateBinaryIntrinsic(Intrinsic::cttz, Zext, Op1);
      return IC.replaceInstUsesWith(II, CttzZext);
    }

    // Zext doesn't change the number of trailing zeros, so narrow:
    // cttz(zext(x)) -> zext(cttz(x)) if the 'ZeroIsPoison' parameter is 'true'.
    if (match(Op0, m_OneUse(m_ZExt(m_Value(X)))) && match(Op1, m_One())) {
      auto *Cttz = IC.Builder.CreateBinaryIntrinsic(Intrinsic::cttz, X,
                                                    IC.Builder.getTrue());
      auto *ZextCttz = IC.Builder.CreateZExt(Cttz, II.getType());
      return IC.replaceInstUsesWith(II, ZextCttz);
    }

    // cttz(abs(x)) -> cttz(x)
    // cttz(nabs(x)) -> cttz(x)
    Value *Y;
    SelectPatternFlavor SPF = matchSelectPattern(Op0, X, Y).Flavor;
    if (SPF == SPF_ABS || SPF == SPF_NABS)
      return IC.replaceOperand(II, 0, X);

    if (match(Op0, m_Intrinsic<Intrinsic::abs>(m_Value(X))))
      return IC.replaceOperand(II, 0, X);

    // cttz(shl(%const, %val), 1) --> add(cttz(%const, 1), %val)
    if (match(Op0, m_Shl(m_ImmConstant(C), m_Value(X))) &&
        match(Op1, m_One())) {
      Value *ConstCttz =
          IC.Builder.CreateBinaryIntrinsic(Intrinsic::cttz, C, Op1);
      return BinaryOperator::CreateAdd(ConstCttz, X);
    }

    // cttz(lshr exact (%const, %val), 1) --> sub(cttz(%const, 1), %val)
    if (match(Op0, m_Exact(m_LShr(m_ImmConstant(C), m_Value(X)))) &&
        match(Op1, m_One())) {
      Value *ConstCttz =
          IC.Builder.CreateBinaryIntrinsic(Intrinsic::cttz, C, Op1);
      return BinaryOperator::CreateSub(ConstCttz, X);
    }

    // cttz(add(lshr(UINT_MAX, %val), 1)) --> sub(width, %val)
    if (match(Op0, m_Add(m_LShr(m_AllOnes(), m_Value(X)), m_One()))) {
      Value *Width =
          ConstantInt::get(II.getType(), II.getType()->getScalarSizeInBits());
      return BinaryOperator::CreateSub(Width, X);
    }
  } else {
    // ctlz(lshr(%const, %val), 1) --> add(ctlz(%const, 1), %val)
    if (match(Op0, m_LShr(m_ImmConstant(C), m_Value(X))) &&
        match(Op1, m_One())) {
      Value *ConstCtlz =
          IC.Builder.CreateBinaryIntrinsic(Intrinsic::ctlz, C, Op1);
      return BinaryOperator::CreateAdd(ConstCtlz, X);
    }

    // ctlz(shl nuw (%const, %val), 1) --> sub(ctlz(%const, 1), %val)
    if (match(Op0, m_NUWShl(m_ImmConstant(C), m_Value(X))) &&
        match(Op1, m_One())) {
      Value *ConstCtlz =
          IC.Builder.CreateBinaryIntrinsic(Intrinsic::ctlz, C, Op1);
      return BinaryOperator::CreateSub(ConstCtlz, X);
    }
  }

  KnownBits Known = IC.computeKnownBits(Op0, 0, &II);

  // Create a mask for bits above (ctlz) or below (cttz) the first known one.
  unsigned PossibleZeros = IsTZ ? Known.countMaxTrailingZeros()
                                : Known.countMaxLeadingZeros();
  unsigned DefiniteZeros = IsTZ ? Known.countMinTrailingZeros()
                                : Known.countMinLeadingZeros();

  // If all bits above (ctlz) or below (cttz) the first known one are known
  // zero, this value is constant.
  // FIXME: This should be in InstSimplify because we're replacing an
  // instruction with a constant.
  if (PossibleZeros == DefiniteZeros) {
    auto *C = ConstantInt::get(Op0->getType(), DefiniteZeros);
    return IC.replaceInstUsesWith(II, C);
  }

  // If the input to cttz/ctlz is known to be non-zero,
  // then change the 'ZeroIsPoison' parameter to 'true'
  // because we know the zero behavior can't affect the result.
  if (!Known.One.isZero() ||
      isKnownNonZero(Op0, IC.getSimplifyQuery().getWithInstruction(&II))) {
    if (!match(II.getArgOperand(1), m_One()))
      return IC.replaceOperand(II, 1, IC.Builder.getTrue());
  }

  // Add range attribute since known bits can't completely reflect what we know.
  unsigned BitWidth = Op0->getType()->getScalarSizeInBits();
  if (BitWidth != 1 && !II.hasRetAttr(Attribute::Range) &&
      !II.getMetadata(LLVMContext::MD_range)) {
    ConstantRange Range(APInt(BitWidth, DefiniteZeros),
                        APInt(BitWidth, PossibleZeros + 1));
    II.addRangeRetAttr(Range);
    return &II;
  }

  return nullptr;
}

static Instruction *foldCtpop(IntrinsicInst &II, InstCombinerImpl &IC) {
  assert(II.getIntrinsicID() == Intrinsic::ctpop &&
         "Expected ctpop intrinsic");
  Type *Ty = II.getType();
  unsigned BitWidth = Ty->getScalarSizeInBits();
  Value *Op0 = II.getArgOperand(0);
  Value *X, *Y;

  // ctpop(bitreverse(x)) -> ctpop(x)
  // ctpop(bswap(x)) -> ctpop(x)
  if (match(Op0, m_BitReverse(m_Value(X))) || match(Op0, m_BSwap(m_Value(X))))
    return IC.replaceOperand(II, 0, X);

  // ctpop(rot(x)) -> ctpop(x)
  if ((match(Op0, m_FShl(m_Value(X), m_Value(Y), m_Value())) ||
       match(Op0, m_FShr(m_Value(X), m_Value(Y), m_Value()))) &&
      X == Y)
    return IC.replaceOperand(II, 0, X);

  // ctpop(x | -x) -> bitwidth - cttz(x, false)
  if (Op0->hasOneUse() &&
      match(Op0, m_c_Or(m_Value(X), m_Neg(m_Deferred(X))))) {
    Function *F =
        Intrinsic::getDeclaration(II.getModule(), Intrinsic::cttz, Ty);
    auto *Cttz = IC.Builder.CreateCall(F, {X, IC.Builder.getFalse()});
    auto *Bw = ConstantInt::get(Ty, APInt(BitWidth, BitWidth));
    return IC.replaceInstUsesWith(II, IC.Builder.CreateSub(Bw, Cttz));
  }

  // ctpop(~x & (x - 1)) -> cttz(x, false)
  if (match(Op0,
            m_c_And(m_Not(m_Value(X)), m_Add(m_Deferred(X), m_AllOnes())))) {
    Function *F =
        Intrinsic::getDeclaration(II.getModule(), Intrinsic::cttz, Ty);
    return CallInst::Create(F, {X, IC.Builder.getFalse()});
  }

  // Zext doesn't change the number of set bits, so narrow:
  // ctpop (zext X) --> zext (ctpop X)
  if (match(Op0, m_OneUse(m_ZExt(m_Value(X))))) {
    Value *NarrowPop = IC.Builder.CreateUnaryIntrinsic(Intrinsic::ctpop, X);
    return CastInst::Create(Instruction::ZExt, NarrowPop, Ty);
  }

  KnownBits Known(BitWidth);
  IC.computeKnownBits(Op0, Known, 0, &II);

  // If all bits are zero except for exactly one fixed bit, then the result
  // must be 0 or 1, and we can get that answer by shifting to LSB:
  // ctpop (X & 32) --> (X & 32) >> 5
  // TODO: Investigate removing this as its likely unnecessary given the below
  // `isKnownToBeAPowerOfTwo` check.
  if ((~Known.Zero).isPowerOf2())
    return BinaryOperator::CreateLShr(
        Op0, ConstantInt::get(Ty, (~Known.Zero).exactLogBase2()));

  // More generally we can also handle non-constant power of 2 patterns such as
  // shl/shr(Pow2, X), (X & -X), etc... by transforming:
  // ctpop(Pow2OrZero) --> icmp ne X, 0
  if (IC.isKnownToBeAPowerOfTwo(Op0, /* OrZero */ true))
    return CastInst::Create(Instruction::ZExt,
                            IC.Builder.CreateICmp(ICmpInst::ICMP_NE, Op0,
                                                  Constant::getNullValue(Ty)),
                            Ty);

  // Add range attribute since known bits can't completely reflect what we know.
  if (BitWidth != 1 && !II.hasRetAttr(Attribute::Range) &&
      !II.getMetadata(LLVMContext::MD_range)) {
    ConstantRange Range(APInt(BitWidth, Known.countMinPopulation()),
                        APInt(BitWidth, Known.countMaxPopulation() + 1));
    II.addRangeRetAttr(Range);
    return &II;
  }

  return nullptr;
}

/// Convert a table lookup to shufflevector if the mask is constant.
/// This could benefit tbl1 if the mask is { 7,6,5,4,3,2,1,0 }, in
/// which case we could lower the shufflevector with rev64 instructions
/// as it's actually a byte reverse.
static Value *simplifyNeonTbl1(const IntrinsicInst &II,
                               InstCombiner::BuilderTy &Builder) {
  // Bail out if the mask is not a constant.
  auto *C = dyn_cast<Constant>(II.getArgOperand(1));
  if (!C)
    return nullptr;

  auto *VecTy = cast<FixedVectorType>(II.getType());
  unsigned NumElts = VecTy->getNumElements();

  // Only perform this transformation for <8 x i8> vector types.
  if (!VecTy->getElementType()->isIntegerTy(8) || NumElts != 8)
    return nullptr;

  int Indexes[8];

  for (unsigned I = 0; I < NumElts; ++I) {
    Constant *COp = C->getAggregateElement(I);

    if (!COp || !isa<ConstantInt>(COp))
      return nullptr;

    Indexes[I] = cast<ConstantInt>(COp)->getLimitedValue();

    // Make sure the mask indices are in range.
    if ((unsigned)Indexes[I] >= NumElts)
      return nullptr;
  }

  auto *V1 = II.getArgOperand(0);
  auto *V2 = Constant::getNullValue(V1->getType());
  return Builder.CreateShuffleVector(V1, V2, ArrayRef(Indexes));
}

// Returns true iff the 2 intrinsics have the same operands, limiting the
// comparison to the first NumOperands.
static bool haveSameOperands(const IntrinsicInst &I, const IntrinsicInst &E,
                             unsigned NumOperands) {
  assert(I.arg_size() >= NumOperands && "Not enough operands");
  assert(E.arg_size() >= NumOperands && "Not enough operands");
  for (unsigned i = 0; i < NumOperands; i++)
    if (I.getArgOperand(i) != E.getArgOperand(i))
      return false;
  return true;
}

// Remove trivially empty start/end intrinsic ranges, i.e. a start
// immediately followed by an end (ignoring debuginfo or other
// start/end intrinsics in between). As this handles only the most trivial
// cases, tracking the nesting level is not needed:
//
//   call @llvm.foo.start(i1 0)
//   call @llvm.foo.start(i1 0) ; This one won't be skipped: it will be removed
//   call @llvm.foo.end(i1 0)
//   call @llvm.foo.end(i1 0) ; &I
static bool
removeTriviallyEmptyRange(IntrinsicInst &EndI, InstCombinerImpl &IC,
                          std::function<bool(const IntrinsicInst &)> IsStart) {
  // We start from the end intrinsic and scan backwards, so that InstCombine
  // has already processed (and potentially removed) all the instructions
  // before the end intrinsic.
  BasicBlock::reverse_iterator BI(EndI), BE(EndI.getParent()->rend());
  for (; BI != BE; ++BI) {
    if (auto *I = dyn_cast<IntrinsicInst>(&*BI)) {
      if (I->isDebugOrPseudoInst() ||
          I->getIntrinsicID() == EndI.getIntrinsicID())
        continue;
      if (IsStart(*I)) {
        if (haveSameOperands(EndI, *I, EndI.arg_size())) {
          IC.eraseInstFromFunction(*I);
          IC.eraseInstFromFunction(EndI);
          return true;
        }
        // Skip start intrinsics that don't pair with this end intrinsic.
        continue;
      }
    }
    break;
  }

  return false;
}

Instruction *InstCombinerImpl::visitVAEndInst(VAEndInst &I) {
  removeTriviallyEmptyRange(I, *this, [](const IntrinsicInst &I) {
    return I.getIntrinsicID() == Intrinsic::vastart ||
           I.getIntrinsicID() == Intrinsic::vacopy;
  });
  return nullptr;
}

static CallInst *canonicalizeConstantArg0ToArg1(CallInst &Call) {
  assert(Call.arg_size() > 1 && "Need at least 2 args to swap");
  Value *Arg0 = Call.getArgOperand(0), *Arg1 = Call.getArgOperand(1);
  if (isa<Constant>(Arg0) && !isa<Constant>(Arg1)) {
    Call.setArgOperand(0, Arg1);
    Call.setArgOperand(1, Arg0);
    return &Call;
  }
  return nullptr;
}

/// Creates a result tuple for an overflow intrinsic \p II with a given
/// \p Result and a constant \p Overflow value.
static Instruction *createOverflowTuple(IntrinsicInst *II, Value *Result,
                                        Constant *Overflow) {
  Constant *V[] = {PoisonValue::get(Result->getType()), Overflow};
  StructType *ST = cast<StructType>(II->getType());
  Constant *Struct = ConstantStruct::get(ST, V);
  return InsertValueInst::Create(Struct, Result, 0);
}

Instruction *
InstCombinerImpl::foldIntrinsicWithOverflowCommon(IntrinsicInst *II) {
  WithOverflowInst *WO = cast<WithOverflowInst>(II);
  Value *OperationResult = nullptr;
  Constant *OverflowResult = nullptr;
  if (OptimizeOverflowCheck(WO->getBinaryOp(), WO->isSigned(), WO->getLHS(),
                            WO->getRHS(), *WO, OperationResult, OverflowResult))
    return createOverflowTuple(WO, OperationResult, OverflowResult);
  return nullptr;
}

static bool inputDenormalIsIEEE(const Function &F, const Type *Ty) {
  Ty = Ty->getScalarType();
  return F.getDenormalMode(Ty->getFltSemantics()).Input == DenormalMode::IEEE;
}

static bool inputDenormalIsDAZ(const Function &F, const Type *Ty) {
  Ty = Ty->getScalarType();
  return F.getDenormalMode(Ty->getFltSemantics()).inputsAreZero();
}

/// \returns the compare predicate type if the test performed by
/// llvm.is.fpclass(x, \p Mask) is equivalent to fcmp o__ x, 0.0 with the
/// floating-point environment assumed for \p F for type \p Ty
static FCmpInst::Predicate fpclassTestIsFCmp0(FPClassTest Mask,
                                              const Function &F, Type *Ty) {
  switch (static_cast<unsigned>(Mask)) {
  case fcZero:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_OEQ;
    break;
  case fcZero | fcSubnormal:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_OEQ;
    break;
  case fcPositive | fcNegZero:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_OGE;
    break;
  case fcPositive | fcNegZero | fcNegSubnormal:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_OGE;
    break;
  case fcPosSubnormal | fcPosNormal | fcPosInf:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_OGT;
    break;
  case fcNegative | fcPosZero:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_OLE;
    break;
  case fcNegative | fcPosZero | fcPosSubnormal:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_OLE;
    break;
  case fcNegSubnormal | fcNegNormal | fcNegInf:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_OLT;
    break;
  case fcPosNormal | fcPosInf:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_OGT;
    break;
  case fcNegNormal | fcNegInf:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_OLT;
    break;
  case ~fcZero & ~fcNan:
    if (inputDenormalIsIEEE(F, Ty))
      return FCmpInst::FCMP_ONE;
    break;
  case ~(fcZero | fcSubnormal) & ~fcNan:
    if (inputDenormalIsDAZ(F, Ty))
      return FCmpInst::FCMP_ONE;
    break;
  default:
    break;
  }

  return FCmpInst::BAD_FCMP_PREDICATE;
}

Instruction *InstCombinerImpl::foldIntrinsicIsFPClass(IntrinsicInst &II) {
  Value *Src0 = II.getArgOperand(0);
  Value *Src1 = II.getArgOperand(1);
  const ConstantInt *CMask = cast<ConstantInt>(Src1);
  FPClassTest Mask = static_cast<FPClassTest>(CMask->getZExtValue());
  const bool IsUnordered = (Mask & fcNan) == fcNan;
  const bool IsOrdered = (Mask & fcNan) == fcNone;
  const FPClassTest OrderedMask = Mask & ~fcNan;
  const FPClassTest OrderedInvertedMask = ~OrderedMask & ~fcNan;

  const bool IsStrict =
      II.getFunction()->getAttributes().hasFnAttr(Attribute::StrictFP);

  Value *FNegSrc;
  if (match(Src0, m_FNeg(m_Value(FNegSrc)))) {
    // is.fpclass (fneg x), mask -> is.fpclass x, (fneg mask)

    II.setArgOperand(1, ConstantInt::get(Src1->getType(), fneg(Mask)));
    return replaceOperand(II, 0, FNegSrc);
  }

  Value *FAbsSrc;
  if (match(Src0, m_FAbs(m_Value(FAbsSrc)))) {
    II.setArgOperand(1, ConstantInt::get(Src1->getType(), inverse_fabs(Mask)));
    return replaceOperand(II, 0, FAbsSrc);
  }

  if ((OrderedMask == fcInf || OrderedInvertedMask == fcInf) &&
      (IsOrdered || IsUnordered) && !IsStrict) {
    // is.fpclass(x, fcInf) -> fcmp oeq fabs(x), +inf
    // is.fpclass(x, ~fcInf) -> fcmp one fabs(x), +inf
    // is.fpclass(x, fcInf|fcNan) -> fcmp ueq fabs(x), +inf
    // is.fpclass(x, ~(fcInf|fcNan)) -> fcmp une fabs(x), +inf
    Constant *Inf = ConstantFP::getInfinity(Src0->getType());
    FCmpInst::Predicate Pred =
        IsUnordered ? FCmpInst::FCMP_UEQ : FCmpInst::FCMP_OEQ;
    if (OrderedInvertedMask == fcInf)
      Pred = IsUnordered ? FCmpInst::FCMP_UNE : FCmpInst::FCMP_ONE;

    Value *Fabs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, Src0);
    Value *CmpInf = Builder.CreateFCmp(Pred, Fabs, Inf);
    CmpInf->takeName(&II);
    return replaceInstUsesWith(II, CmpInf);
  }

  if ((OrderedMask == fcPosInf || OrderedMask == fcNegInf) &&
      (IsOrdered || IsUnordered) && !IsStrict) {
    // is.fpclass(x, fcPosInf) -> fcmp oeq x, +inf
    // is.fpclass(x, fcNegInf) -> fcmp oeq x, -inf
    // is.fpclass(x, fcPosInf|fcNan) -> fcmp ueq x, +inf
    // is.fpclass(x, fcNegInf|fcNan) -> fcmp ueq x, -inf
    Constant *Inf =
        ConstantFP::getInfinity(Src0->getType(), OrderedMask == fcNegInf);
    Value *EqInf = IsUnordered ? Builder.CreateFCmpUEQ(Src0, Inf)
                               : Builder.CreateFCmpOEQ(Src0, Inf);

    EqInf->takeName(&II);
    return replaceInstUsesWith(II, EqInf);
  }

  if ((OrderedInvertedMask == fcPosInf || OrderedInvertedMask == fcNegInf) &&
      (IsOrdered || IsUnordered) && !IsStrict) {
    // is.fpclass(x, ~fcPosInf) -> fcmp one x, +inf
    // is.fpclass(x, ~fcNegInf) -> fcmp one x, -inf
    // is.fpclass(x, ~fcPosInf|fcNan) -> fcmp une x, +inf
    // is.fpclass(x, ~fcNegInf|fcNan) -> fcmp une x, -inf
    Constant *Inf = ConstantFP::getInfinity(Src0->getType(),
                                            OrderedInvertedMask == fcNegInf);
    Value *NeInf = IsUnordered ? Builder.CreateFCmpUNE(Src0, Inf)
                               : Builder.CreateFCmpONE(Src0, Inf);
    NeInf->takeName(&II);
    return replaceInstUsesWith(II, NeInf);
  }

  if (Mask == fcNan && !IsStrict) {
    // Equivalent of isnan. Replace with standard fcmp if we don't care about FP
    // exceptions.
    Value *IsNan =
        Builder.CreateFCmpUNO(Src0, ConstantFP::getZero(Src0->getType()));
    IsNan->takeName(&II);
    return replaceInstUsesWith(II, IsNan);
  }

  if (Mask == (~fcNan & fcAllFlags) && !IsStrict) {
    // Equivalent of !isnan. Replace with standard fcmp.
    Value *FCmp =
        Builder.CreateFCmpORD(Src0, ConstantFP::getZero(Src0->getType()));
    FCmp->takeName(&II);
    return replaceInstUsesWith(II, FCmp);
  }

  FCmpInst::Predicate PredType = FCmpInst::BAD_FCMP_PREDICATE;

  // Try to replace with an fcmp with 0
  //
  // is.fpclass(x, fcZero) -> fcmp oeq x, 0.0
  // is.fpclass(x, fcZero | fcNan) -> fcmp ueq x, 0.0
  // is.fpclass(x, ~fcZero & ~fcNan) -> fcmp one x, 0.0
  // is.fpclass(x, ~fcZero) -> fcmp une x, 0.0
  //
  // is.fpclass(x, fcPosSubnormal | fcPosNormal | fcPosInf) -> fcmp ogt x, 0.0
  // is.fpclass(x, fcPositive | fcNegZero) -> fcmp oge x, 0.0
  //
  // is.fpclass(x, fcNegSubnormal | fcNegNormal | fcNegInf) -> fcmp olt x, 0.0
  // is.fpclass(x, fcNegative | fcPosZero) -> fcmp ole x, 0.0
  //
  if (!IsStrict && (IsOrdered || IsUnordered) &&
      (PredType = fpclassTestIsFCmp0(OrderedMask, *II.getFunction(),
                                     Src0->getType())) !=
          FCmpInst::BAD_FCMP_PREDICATE) {
    Constant *Zero = ConstantFP::getZero(Src0->getType());
    // Equivalent of == 0.
    Value *FCmp = Builder.CreateFCmp(
        IsUnordered ? FCmpInst::getUnorderedPredicate(PredType) : PredType,
        Src0, Zero);

    FCmp->takeName(&II);
    return replaceInstUsesWith(II, FCmp);
  }

  KnownFPClass Known = computeKnownFPClass(Src0, Mask, &II);

  // Clear test bits we know must be false from the source value.
  // fp_class (nnan x), qnan|snan|other -> fp_class (nnan x), other
  // fp_class (ninf x), ninf|pinf|other -> fp_class (ninf x), other
  if ((Mask & Known.KnownFPClasses) != Mask) {
    II.setArgOperand(
        1, ConstantInt::get(Src1->getType(), Mask & Known.KnownFPClasses));
    return &II;
  }

  // If none of the tests which can return false are possible, fold to true.
  // fp_class (nnan x), ~(qnan|snan) -> true
  // fp_class (ninf x), ~(ninf|pinf) -> true
  if (Mask == Known.KnownFPClasses)
    return replaceInstUsesWith(II, ConstantInt::get(II.getType(), true));

  return nullptr;
}

static std::optional<bool> getKnownSign(Value *Op, const SimplifyQuery &SQ) {
  KnownBits Known = computeKnownBits(Op, /*Depth=*/0, SQ);
  if (Known.isNonNegative())
    return false;
  if (Known.isNegative())
    return true;

  Value *X, *Y;
  if (match(Op, m_NSWSub(m_Value(X), m_Value(Y))))
    return isImpliedByDomCondition(ICmpInst::ICMP_SLT, X, Y, SQ.CxtI, SQ.DL);

  return std::nullopt;
}

static std::optional<bool> getKnownSignOrZero(Value *Op,
                                              const SimplifyQuery &SQ) {
  if (std::optional<bool> Sign = getKnownSign(Op, SQ))
    return Sign;

  Value *X, *Y;
  if (match(Op, m_NSWSub(m_Value(X), m_Value(Y))))
    return isImpliedByDomCondition(ICmpInst::ICMP_SLE, X, Y, SQ.CxtI, SQ.DL);

  return std::nullopt;
}

/// Return true if two values \p Op0 and \p Op1 are known to have the same sign.
static bool signBitMustBeTheSame(Value *Op0, Value *Op1,
                                 const SimplifyQuery &SQ) {
  std::optional<bool> Known1 = getKnownSign(Op1, SQ);
  if (!Known1)
    return false;
  std::optional<bool> Known0 = getKnownSign(Op0, SQ);
  if (!Known0)
    return false;
  return *Known0 == *Known1;
}

/// Try to canonicalize min/max(X + C0, C1) as min/max(X, C1 - C0) + C0. This
/// can trigger other combines.
static Instruction *moveAddAfterMinMax(IntrinsicInst *II,
                                       InstCombiner::BuilderTy &Builder) {
  Intrinsic::ID MinMaxID = II->getIntrinsicID();
  assert((MinMaxID == Intrinsic::smax || MinMaxID == Intrinsic::smin ||
          MinMaxID == Intrinsic::umax || MinMaxID == Intrinsic::umin) &&
         "Expected a min or max intrinsic");

  // TODO: Match vectors with undef elements, but undef may not propagate.
  Value *Op0 = II->getArgOperand(0), *Op1 = II->getArgOperand(1);
  Value *X;
  const APInt *C0, *C1;
  if (!match(Op0, m_OneUse(m_Add(m_Value(X), m_APInt(C0)))) ||
      !match(Op1, m_APInt(C1)))
    return nullptr;

  // Check for necessary no-wrap and overflow constraints.
  bool IsSigned = MinMaxID == Intrinsic::smax || MinMaxID == Intrinsic::smin;
  auto *Add = cast<BinaryOperator>(Op0);
  if ((IsSigned && !Add->hasNoSignedWrap()) ||
      (!IsSigned && !Add->hasNoUnsignedWrap()))
    return nullptr;

  // If the constant difference overflows, then instsimplify should reduce the
  // min/max to the add or C1.
  bool Overflow;
  APInt CDiff =
      IsSigned ? C1->ssub_ov(*C0, Overflow) : C1->usub_ov(*C0, Overflow);
  assert(!Overflow && "Expected simplify of min/max");

  // min/max (add X, C0), C1 --> add (min/max X, C1 - C0), C0
  // Note: the "mismatched" no-overflow setting does not propagate.
  Constant *NewMinMaxC = ConstantInt::get(II->getType(), CDiff);
  Value *NewMinMax = Builder.CreateBinaryIntrinsic(MinMaxID, X, NewMinMaxC);
  return IsSigned ? BinaryOperator::CreateNSWAdd(NewMinMax, Add->getOperand(1))
                  : BinaryOperator::CreateNUWAdd(NewMinMax, Add->getOperand(1));
}
/// Match a sadd_sat or ssub_sat which is using min/max to clamp the value.
Instruction *InstCombinerImpl::matchSAddSubSat(IntrinsicInst &MinMax1) {
  Type *Ty = MinMax1.getType();

  // We are looking for a tree of:
  // max(INT_MIN, min(INT_MAX, add(sext(A), sext(B))))
  // Where the min and max could be reversed
  Instruction *MinMax2;
  BinaryOperator *AddSub;
  const APInt *MinValue, *MaxValue;
  if (match(&MinMax1, m_SMin(m_Instruction(MinMax2), m_APInt(MaxValue)))) {
    if (!match(MinMax2, m_SMax(m_BinOp(AddSub), m_APInt(MinValue))))
      return nullptr;
  } else if (match(&MinMax1,
                   m_SMax(m_Instruction(MinMax2), m_APInt(MinValue)))) {
    if (!match(MinMax2, m_SMin(m_BinOp(AddSub), m_APInt(MaxValue))))
      return nullptr;
  } else
    return nullptr;

  // Check that the constants clamp a saturate, and that the new type would be
  // sensible to convert to.
  if (!(*MaxValue + 1).isPowerOf2() || -*MinValue != *MaxValue + 1)
    return nullptr;
  // In what bitwidth can this be treated as saturating arithmetics?
  unsigned NewBitWidth = (*MaxValue + 1).logBase2() + 1;
  // FIXME: This isn't quite right for vectors, but using the scalar type is a
  // good first approximation for what should be done there.
  if (!shouldChangeType(Ty->getScalarType()->getIntegerBitWidth(), NewBitWidth))
    return nullptr;

  // Also make sure that the inner min/max and the add/sub have one use.
  if (!MinMax2->hasOneUse() || !AddSub->hasOneUse())
    return nullptr;

  // Create the new type (which can be a vector type)
  Type *NewTy = Ty->getWithNewBitWidth(NewBitWidth);

  Intrinsic::ID IntrinsicID;
  if (AddSub->getOpcode() == Instruction::Add)
    IntrinsicID = Intrinsic::sadd_sat;
  else if (AddSub->getOpcode() == Instruction::Sub)
    IntrinsicID = Intrinsic::ssub_sat;
  else
    return nullptr;

  // The two operands of the add/sub must be nsw-truncatable to the NewTy. This
  // is usually achieved via a sext from a smaller type.
  if (ComputeMaxSignificantBits(AddSub->getOperand(0), 0, AddSub) >
          NewBitWidth ||
      ComputeMaxSignificantBits(AddSub->getOperand(1), 0, AddSub) > NewBitWidth)
    return nullptr;

  // Finally create and return the sat intrinsic, truncated to the new type
  Function *F = Intrinsic::getDeclaration(MinMax1.getModule(), IntrinsicID, NewTy);
  Value *AT = Builder.CreateTrunc(AddSub->getOperand(0), NewTy);
  Value *BT = Builder.CreateTrunc(AddSub->getOperand(1), NewTy);
  Value *Sat = Builder.CreateCall(F, {AT, BT});
  return CastInst::Create(Instruction::SExt, Sat, Ty);
}


/// If we have a clamp pattern like max (min X, 42), 41 -- where the output
/// can only be one of two possible constant values -- turn that into a select
/// of constants.
static Instruction *foldClampRangeOfTwo(IntrinsicInst *II,
                                        InstCombiner::BuilderTy &Builder) {
  Value *I0 = II->getArgOperand(0), *I1 = II->getArgOperand(1);
  Value *X;
  const APInt *C0, *C1;
  if (!match(I1, m_APInt(C1)) || !I0->hasOneUse())
    return nullptr;

  CmpInst::Predicate Pred = CmpInst::BAD_ICMP_PREDICATE;
  switch (II->getIntrinsicID()) {
  case Intrinsic::smax:
    if (match(I0, m_SMin(m_Value(X), m_APInt(C0))) && *C0 == *C1 + 1)
      Pred = ICmpInst::ICMP_SGT;
    break;
  case Intrinsic::smin:
    if (match(I0, m_SMax(m_Value(X), m_APInt(C0))) && *C1 == *C0 + 1)
      Pred = ICmpInst::ICMP_SLT;
    break;
  case Intrinsic::umax:
    if (match(I0, m_UMin(m_Value(X), m_APInt(C0))) && *C0 == *C1 + 1)
      Pred = ICmpInst::ICMP_UGT;
    break;
  case Intrinsic::umin:
    if (match(I0, m_UMax(m_Value(X), m_APInt(C0))) && *C1 == *C0 + 1)
      Pred = ICmpInst::ICMP_ULT;
    break;
  default:
    llvm_unreachable("Expected min/max intrinsic");
  }
  if (Pred == CmpInst::BAD_ICMP_PREDICATE)
    return nullptr;

  // max (min X, 42), 41 --> X > 41 ? 42 : 41
  // min (max X, 42), 43 --> X < 43 ? 42 : 43
  Value *Cmp = Builder.CreateICmp(Pred, X, I1);
  return SelectInst::Create(Cmp, ConstantInt::get(II->getType(), *C0), I1);
}

/// If this min/max has a constant operand and an operand that is a matching
/// min/max with a constant operand, constant-fold the 2 constant operands.
static Value *reassociateMinMaxWithConstants(IntrinsicInst *II,
                                             IRBuilderBase &Builder,
                                             const SimplifyQuery &SQ) {
  Intrinsic::ID MinMaxID = II->getIntrinsicID();
  auto *LHS = dyn_cast<MinMaxIntrinsic>(II->getArgOperand(0));
  if (!LHS)
    return nullptr;

  Constant *C0, *C1;
  if (!match(LHS->getArgOperand(1), m_ImmConstant(C0)) ||
      !match(II->getArgOperand(1), m_ImmConstant(C1)))
    return nullptr;

  // max (max X, C0), C1 --> max X, (max C0, C1)
  // min (min X, C0), C1 --> min X, (min C0, C1)
  // umax (smax X, nneg C0), nneg C1 --> smax X, (umax C0, C1)
  // smin (umin X, nneg C0), nneg C1 --> umin X, (smin C0, C1)
  Intrinsic::ID InnerMinMaxID = LHS->getIntrinsicID();
  if (InnerMinMaxID != MinMaxID &&
      !(((MinMaxID == Intrinsic::umax && InnerMinMaxID == Intrinsic::smax) ||
         (MinMaxID == Intrinsic::smin && InnerMinMaxID == Intrinsic::umin)) &&
        isKnownNonNegative(C0, SQ) && isKnownNonNegative(C1, SQ)))
    return nullptr;

  ICmpInst::Predicate Pred = MinMaxIntrinsic::getPredicate(MinMaxID);
  Value *CondC = Builder.CreateICmp(Pred, C0, C1);
  Value *NewC = Builder.CreateSelect(CondC, C0, C1);
  return Builder.CreateIntrinsic(InnerMinMaxID, II->getType(),
                                 {LHS->getArgOperand(0), NewC});
}

/// If this min/max has a matching min/max operand with a constant, try to push
/// the constant operand into this instruction. This can enable more folds.
static Instruction *
reassociateMinMaxWithConstantInOperand(IntrinsicInst *II,
                                       InstCombiner::BuilderTy &Builder) {
  // Match and capture a min/max operand candidate.
  Value *X, *Y;
  Constant *C;
  Instruction *Inner;
  if (!match(II, m_c_MaxOrMin(m_OneUse(m_CombineAnd(
                                  m_Instruction(Inner),
                                  m_MaxOrMin(m_Value(X), m_ImmConstant(C)))),
                              m_Value(Y))))
    return nullptr;

  // The inner op must match. Check for constants to avoid infinite loops.
  Intrinsic::ID MinMaxID = II->getIntrinsicID();
  auto *InnerMM = dyn_cast<IntrinsicInst>(Inner);
  if (!InnerMM || InnerMM->getIntrinsicID() != MinMaxID ||
      match(X, m_ImmConstant()) || match(Y, m_ImmConstant()))
    return nullptr;

  // max (max X, C), Y --> max (max X, Y), C
  Function *MinMax =
      Intrinsic::getDeclaration(II->getModule(), MinMaxID, II->getType());
  Value *NewInner = Builder.CreateBinaryIntrinsic(MinMaxID, X, Y);
  NewInner->takeName(Inner);
  return CallInst::Create(MinMax, {NewInner, C});
}

/// Reduce a sequence of min/max intrinsics with a common operand.
static Instruction *factorizeMinMaxTree(IntrinsicInst *II) {
  // Match 3 of the same min/max ops. Example: umin(umin(), umin()).
  auto *LHS = dyn_cast<IntrinsicInst>(II->getArgOperand(0));
  auto *RHS = dyn_cast<IntrinsicInst>(II->getArgOperand(1));
  Intrinsic::ID MinMaxID = II->getIntrinsicID();
  if (!LHS || !RHS || LHS->getIntrinsicID() != MinMaxID ||
      RHS->getIntrinsicID() != MinMaxID ||
      (!LHS->hasOneUse() && !RHS->hasOneUse()))
    return nullptr;

  Value *A = LHS->getArgOperand(0);
  Value *B = LHS->getArgOperand(1);
  Value *C = RHS->getArgOperand(0);
  Value *D = RHS->getArgOperand(1);

  // Look for a common operand.
  Value *MinMaxOp = nullptr;
  Value *ThirdOp = nullptr;
  if (LHS->hasOneUse()) {
    // If the LHS is only used in this chain and the RHS is used outside of it,
    // reuse the RHS min/max because that will eliminate the LHS.
    if (D == A || C == A) {
      // min(min(a, b), min(c, a)) --> min(min(c, a), b)
      // min(min(a, b), min(a, d)) --> min(min(a, d), b)
      MinMaxOp = RHS;
      ThirdOp = B;
    } else if (D == B || C == B) {
      // min(min(a, b), min(c, b)) --> min(min(c, b), a)
      // min(min(a, b), min(b, d)) --> min(min(b, d), a)
      MinMaxOp = RHS;
      ThirdOp = A;
    }
  } else {
    assert(RHS->hasOneUse() && "Expected one-use operand");
    // Reuse the LHS. This will eliminate the RHS.
    if (D == A || D == B) {
      // min(min(a, b), min(c, a)) --> min(min(a, b), c)
      // min(min(a, b), min(c, b)) --> min(min(a, b), c)
      MinMaxOp = LHS;
      ThirdOp = C;
    } else if (C == A || C == B) {
      // min(min(a, b), min(b, d)) --> min(min(a, b), d)
      // min(min(a, b), min(c, b)) --> min(min(a, b), d)
      MinMaxOp = LHS;
      ThirdOp = D;
    }
  }

  if (!MinMaxOp || !ThirdOp)
    return nullptr;

  Module *Mod = II->getModule();
  Function *MinMax = Intrinsic::getDeclaration(Mod, MinMaxID, II->getType());
  return CallInst::Create(MinMax, { MinMaxOp, ThirdOp });
}

/// If all arguments of the intrinsic are unary shuffles with the same mask,
/// try to shuffle after the intrinsic.
static Instruction *
foldShuffledIntrinsicOperands(IntrinsicInst *II,
                              InstCombiner::BuilderTy &Builder) {
  // TODO: This should be extended to handle other intrinsics like fshl, ctpop,
  //       etc. Use llvm::isTriviallyVectorizable() and related to determine
  //       which intrinsics are safe to shuffle?
  switch (II->getIntrinsicID()) {
  case Intrinsic::smax:
  case Intrinsic::smin:
  case Intrinsic::umax:
  case Intrinsic::umin:
  case Intrinsic::fma:
  case Intrinsic::fshl:
  case Intrinsic::fshr:
    break;
  default:
    return nullptr;
  }

  Value *X;
  ArrayRef<int> Mask;
  if (!match(II->getArgOperand(0),
             m_Shuffle(m_Value(X), m_Undef(), m_Mask(Mask))))
    return nullptr;

  // At least 1 operand must have 1 use because we are creating 2 instructions.
  if (none_of(II->args(), [](Value *V) { return V->hasOneUse(); }))
    return nullptr;

  // See if all arguments are shuffled with the same mask.
  SmallVector<Value *, 4> NewArgs(II->arg_size());
  NewArgs[0] = X;
  Type *SrcTy = X->getType();
  for (unsigned i = 1, e = II->arg_size(); i != e; ++i) {
    if (!match(II->getArgOperand(i),
               m_Shuffle(m_Value(X), m_Undef(), m_SpecificMask(Mask))) ||
        X->getType() != SrcTy)
      return nullptr;
    NewArgs[i] = X;
  }

  // intrinsic (shuf X, M), (shuf Y, M), ... --> shuf (intrinsic X, Y, ...), M
  Instruction *FPI = isa<FPMathOperator>(II) ? II : nullptr;
  Value *NewIntrinsic =
      Builder.CreateIntrinsic(II->getIntrinsicID(), SrcTy, NewArgs, FPI);
  return new ShuffleVectorInst(NewIntrinsic, Mask);
}

/// Fold the following cases and accepts bswap and bitreverse intrinsics:
///   bswap(logic_op(bswap(x), y)) --> logic_op(x, bswap(y))
///   bswap(logic_op(bswap(x), bswap(y))) --> logic_op(x, y) (ignores multiuse)
template <Intrinsic::ID IntrID>
static Instruction *foldBitOrderCrossLogicOp(Value *V,
                                             InstCombiner::BuilderTy &Builder) {
  static_assert(IntrID == Intrinsic::bswap || IntrID == Intrinsic::bitreverse,
                "This helper only supports BSWAP and BITREVERSE intrinsics");

  Value *X, *Y;
  // Find bitwise logic op. Check that it is a BinaryOperator explicitly so we
  // don't match ConstantExpr that aren't meaningful for this transform.
  if (match(V, m_OneUse(m_BitwiseLogic(m_Value(X), m_Value(Y)))) &&
      isa<BinaryOperator>(V)) {
    Value *OldReorderX, *OldReorderY;
    BinaryOperator::BinaryOps Op = cast<BinaryOperator>(V)->getOpcode();

    // If both X and Y are bswap/bitreverse, the transform reduces the number
    // of instructions even if there's multiuse.
    // If only one operand is bswap/bitreverse, we need to ensure the operand
    // have only one use.
    if (match(X, m_Intrinsic<IntrID>(m_Value(OldReorderX))) &&
        match(Y, m_Intrinsic<IntrID>(m_Value(OldReorderY)))) {
      return BinaryOperator::Create(Op, OldReorderX, OldReorderY);
    }

    if (match(X, m_OneUse(m_Intrinsic<IntrID>(m_Value(OldReorderX))))) {
      Value *NewReorder = Builder.CreateUnaryIntrinsic(IntrID, Y);
      return BinaryOperator::Create(Op, OldReorderX, NewReorder);
    }

    if (match(Y, m_OneUse(m_Intrinsic<IntrID>(m_Value(OldReorderY))))) {
      Value *NewReorder = Builder.CreateUnaryIntrinsic(IntrID, X);
      return BinaryOperator::Create(Op, NewReorder, OldReorderY);
    }
  }
  return nullptr;
}

static Value *simplifyReductionOperand(Value *Arg, bool CanReorderLanes) {
  if (!CanReorderLanes)
    return nullptr;

  Value *V;
  if (match(Arg, m_VecReverse(m_Value(V))))
    return V;

  ArrayRef<int> Mask;
  if (!isa<FixedVectorType>(Arg->getType()) ||
      !match(Arg, m_Shuffle(m_Value(V), m_Undef(), m_Mask(Mask))) ||
      !cast<ShuffleVectorInst>(Arg)->isSingleSource())
    return nullptr;

  int Sz = Mask.size();
  SmallBitVector UsedIndices(Sz);
  for (int Idx : Mask) {
    if (Idx == PoisonMaskElem || UsedIndices.test(Idx))
      return nullptr;
    UsedIndices.set(Idx);
  }

  // Can remove shuffle iff just shuffled elements, no repeats, undefs, or
  // other changes.
  return UsedIndices.all() ? V : nullptr;
}

/// Fold an unsigned minimum of trailing or leading zero bits counts:
///   umin(cttz(CtOp, ZeroUndef), ConstOp) --> cttz(CtOp | (1 << ConstOp))
///   umin(ctlz(CtOp, ZeroUndef), ConstOp) --> ctlz(CtOp | (SignedMin
///                                              >> ConstOp))
template <Intrinsic::ID IntrID>
static Value *
foldMinimumOverTrailingOrLeadingZeroCount(Value *I0, Value *I1,
                                          const DataLayout &DL,
                                          InstCombiner::BuilderTy &Builder) {
  static_assert(IntrID == Intrinsic::cttz || IntrID == Intrinsic::ctlz,
                "This helper only supports cttz and ctlz intrinsics");

  Value *CtOp;
  Value *ZeroUndef;
  if (!match(I0,
             m_OneUse(m_Intrinsic<IntrID>(m_Value(CtOp), m_Value(ZeroUndef)))))
    return nullptr;

  unsigned BitWidth = I1->getType()->getScalarSizeInBits();
  auto LessBitWidth = [BitWidth](auto &C) { return C.ult(BitWidth); };
  if (!match(I1, m_CheckedInt(LessBitWidth)))
    // We have a constant >= BitWidth (which can be handled by CVP)
    // or a non-splat vector with elements < and >= BitWidth
    return nullptr;

  Type *Ty = I1->getType();
  Constant *NewConst = ConstantFoldBinaryOpOperands(
      IntrID == Intrinsic::cttz ? Instruction::Shl : Instruction::LShr,
      IntrID == Intrinsic::cttz
          ? ConstantInt::get(Ty, 1)
          : ConstantInt::get(Ty, APInt::getSignedMinValue(BitWidth)),
      cast<Constant>(I1), DL);
  return Builder.CreateBinaryIntrinsic(
      IntrID, Builder.CreateOr(CtOp, NewConst),
      ConstantInt::getTrue(ZeroUndef->getType()));
}

/// CallInst simplification. This mostly only handles folding of intrinsic
/// instructions. For normal calls, it allows visitCallBase to do the heavy
/// lifting.
Instruction *InstCombinerImpl::visitCallInst(CallInst &CI) {
  // Don't try to simplify calls without uses. It will not do anything useful,
  // but will result in the following folds being skipped.
  if (!CI.use_empty()) {
    SmallVector<Value *, 4> Args;
    Args.reserve(CI.arg_size());
    for (Value *Op : CI.args())
      Args.push_back(Op);
    if (Value *V = simplifyCall(&CI, CI.getCalledOperand(), Args,
                                SQ.getWithInstruction(&CI)))
      return replaceInstUsesWith(CI, V);
  }

  if (Value *FreedOp = getFreedOperand(&CI, &TLI))
    return visitFree(CI, FreedOp);

  // If the caller function (i.e. us, the function that contains this CallInst)
  // is nounwind, mark the call as nounwind, even if the callee isn't.
  if (CI.getFunction()->doesNotThrow() && !CI.doesNotThrow()) {
    CI.setDoesNotThrow();
    return &CI;
  }

  IntrinsicInst *II = dyn_cast<IntrinsicInst>(&CI);
  if (!II) return visitCallBase(CI);

  // For atomic unordered mem intrinsics if len is not a positive or
  // not a multiple of element size then behavior is undefined.
  if (auto *AMI = dyn_cast<AtomicMemIntrinsic>(II))
    if (ConstantInt *NumBytes = dyn_cast<ConstantInt>(AMI->getLength()))
      if (NumBytes->isNegative() ||
          (NumBytes->getZExtValue() % AMI->getElementSizeInBytes() != 0)) {
        CreateNonTerminatorUnreachable(AMI);
        assert(AMI->getType()->isVoidTy() &&
               "non void atomic unordered mem intrinsic");
        return eraseInstFromFunction(*AMI);
      }

  // Intrinsics cannot occur in an invoke or a callbr, so handle them here
  // instead of in visitCallBase.
  if (auto *MI = dyn_cast<AnyMemIntrinsic>(II)) {
    bool Changed = false;

    // memmove/cpy/set of zero bytes is a noop.
    if (Constant *NumBytes = dyn_cast<Constant>(MI->getLength())) {
      if (NumBytes->isNullValue())
        return eraseInstFromFunction(CI);
    }

    // No other transformations apply to volatile transfers.
    if (auto *M = dyn_cast<MemIntrinsic>(MI))
      if (M->isVolatile())
        return nullptr;

    // If we have a memmove and the source operation is a constant global,
    // then the source and dest pointers can't alias, so we can change this
    // into a call to memcpy.
    if (auto *MMI = dyn_cast<AnyMemMoveInst>(MI)) {
      if (GlobalVariable *GVSrc = dyn_cast<GlobalVariable>(MMI->getSource()))
        if (GVSrc->isConstant()) {
          Module *M = CI.getModule();
          Intrinsic::ID MemCpyID =
              isa<AtomicMemMoveInst>(MMI)
                  ? Intrinsic::memcpy_element_unordered_atomic
                  : Intrinsic::memcpy;
          Type *Tys[3] = { CI.getArgOperand(0)->getType(),
                           CI.getArgOperand(1)->getType(),
                           CI.getArgOperand(2)->getType() };
          CI.setCalledFunction(Intrinsic::getDeclaration(M, MemCpyID, Tys));
          Changed = true;
        }
    }

    if (AnyMemTransferInst *MTI = dyn_cast<AnyMemTransferInst>(MI)) {
      // memmove(x,x,size) -> noop.
      if (MTI->getSource() == MTI->getDest())
        return eraseInstFromFunction(CI);
    }

    // If we can determine a pointer alignment that is bigger than currently
    // set, update the alignment.
    if (auto *MTI = dyn_cast<AnyMemTransferInst>(MI)) {
      if (Instruction *I = SimplifyAnyMemTransfer(MTI))
        return I;
    } else if (auto *MSI = dyn_cast<AnyMemSetInst>(MI)) {
      if (Instruction *I = SimplifyAnyMemSet(MSI))
        return I;
    }

    if (Changed) return II;
  }

  // For fixed width vector result intrinsics, use the generic demanded vector
  // support.
  if (auto *IIFVTy = dyn_cast<FixedVectorType>(II->getType())) {
    auto VWidth = IIFVTy->getNumElements();
    APInt PoisonElts(VWidth, 0);
    APInt AllOnesEltMask(APInt::getAllOnes(VWidth));
    if (Value *V = SimplifyDemandedVectorElts(II, AllOnesEltMask, PoisonElts)) {
      if (V != II)
        return replaceInstUsesWith(*II, V);
      return II;
    }
  }

  if (II->isCommutative()) {
    if (auto Pair = matchSymmetricPair(II->getOperand(0), II->getOperand(1))) {
      replaceOperand(*II, 0, Pair->first);
      replaceOperand(*II, 1, Pair->second);
      return II;
    }

    if (CallInst *NewCall = canonicalizeConstantArg0ToArg1(CI))
      return NewCall;
  }

  // Unused constrained FP intrinsic calls may have declared side effect, which
  // prevents it from being removed. In some cases however the side effect is
  // actually absent. To detect this case, call SimplifyConstrainedFPCall. If it
  // returns a replacement, the call may be removed.
  if (CI.use_empty() && isa<ConstrainedFPIntrinsic>(CI)) {
    if (simplifyConstrainedFPCall(&CI, SQ.getWithInstruction(&CI)))
      return eraseInstFromFunction(CI);
  }

  Intrinsic::ID IID = II->getIntrinsicID();
  switch (IID) {
  case Intrinsic::objectsize: {
    SmallVector<Instruction *> InsertedInstructions;
    if (Value *V = lowerObjectSizeCall(II, DL, &TLI, AA, /*MustSucceed=*/false,
                                       &InsertedInstructions)) {
      for (Instruction *Inserted : InsertedInstructions)
        Worklist.add(Inserted);
      return replaceInstUsesWith(CI, V);
    }
    return nullptr;
  }
  case Intrinsic::abs: {
    Value *IIOperand = II->getArgOperand(0);
    bool IntMinIsPoison = cast<Constant>(II->getArgOperand(1))->isOneValue();

    // abs(-x) -> abs(x)
    // TODO: Copy nsw if it was present on the neg?
    Value *X;
    if (match(IIOperand, m_Neg(m_Value(X))))
      return replaceOperand(*II, 0, X);
    if (match(IIOperand, m_Select(m_Value(), m_Value(X), m_Neg(m_Deferred(X)))))
      return replaceOperand(*II, 0, X);
    if (match(IIOperand, m_Select(m_Value(), m_Neg(m_Value(X)), m_Deferred(X))))
      return replaceOperand(*II, 0, X);

    Value *Y;
    // abs(a * abs(b)) -> abs(a * b)
    if (match(IIOperand,
              m_OneUse(m_c_Mul(m_Value(X),
                               m_Intrinsic<Intrinsic::abs>(m_Value(Y)))))) {
      bool NSW =
          cast<Instruction>(IIOperand)->hasNoSignedWrap() && IntMinIsPoison;
      auto *XY = NSW ? Builder.CreateNSWMul(X, Y) : Builder.CreateMul(X, Y);
      return replaceOperand(*II, 0, XY);
    }

    if (std::optional<bool> Known =
            getKnownSignOrZero(IIOperand, SQ.getWithInstruction(II))) {
      // abs(x) -> x if x >= 0 (include abs(x-y) --> x - y where x >= y)
      // abs(x) -> x if x > 0 (include abs(x-y) --> x - y where x > y)
      if (!*Known)
        return replaceInstUsesWith(*II, IIOperand);

      // abs(x) -> -x if x < 0
      // abs(x) -> -x if x < = 0 (include abs(x-y) --> y - x where x <= y)
      if (IntMinIsPoison)
        return BinaryOperator::CreateNSWNeg(IIOperand);
      return BinaryOperator::CreateNeg(IIOperand);
    }

    // abs (sext X) --> zext (abs X*)
    // Clear the IsIntMin (nsw) bit on the abs to allow narrowing.
    if (match(IIOperand, m_OneUse(m_SExt(m_Value(X))))) {
      Value *NarrowAbs =
          Builder.CreateBinaryIntrinsic(Intrinsic::abs, X, Builder.getFalse());
      return CastInst::Create(Instruction::ZExt, NarrowAbs, II->getType());
    }

    // Match a complicated way to check if a number is odd/even:
    // abs (srem X, 2) --> and X, 1
    const APInt *C;
    if (match(IIOperand, m_SRem(m_Value(X), m_APInt(C))) && *C == 2)
      return BinaryOperator::CreateAnd(X, ConstantInt::get(II->getType(), 1));

    break;
  }
  case Intrinsic::umin: {
    Value *I0 = II->getArgOperand(0), *I1 = II->getArgOperand(1);
    // umin(x, 1) == zext(x != 0)
    if (match(I1, m_One())) {
      assert(II->getType()->getScalarSizeInBits() != 1 &&
             "Expected simplify of umin with max constant");
      Value *Zero = Constant::getNullValue(I0->getType());
      Value *Cmp = Builder.CreateICmpNE(I0, Zero);
      return CastInst::Create(Instruction::ZExt, Cmp, II->getType());
    }
    // umin(cttz(x), const) --> cttz(x | (1 << const))
    if (Value *FoldedCttz =
            foldMinimumOverTrailingOrLeadingZeroCount<Intrinsic::cttz>(
                I0, I1, DL, Builder))
      return replaceInstUsesWith(*II, FoldedCttz);
    // umin(ctlz(x), const) --> ctlz(x | (SignedMin >> const))
    if (Value *FoldedCtlz =
            foldMinimumOverTrailingOrLeadingZeroCount<Intrinsic::ctlz>(
                I0, I1, DL, Builder))
      return replaceInstUsesWith(*II, FoldedCtlz);
    [[fallthrough]];
  }
  case Intrinsic::umax: {
    Value *I0 = II->getArgOperand(0), *I1 = II->getArgOperand(1);
    Value *X, *Y;
    if (match(I0, m_ZExt(m_Value(X))) && match(I1, m_ZExt(m_Value(Y))) &&
        (I0->hasOneUse() || I1->hasOneUse()) && X->getType() == Y->getType()) {
      Value *NarrowMaxMin = Builder.CreateBinaryIntrinsic(IID, X, Y);
      return CastInst::Create(Instruction::ZExt, NarrowMaxMin, II->getType());
    }
    Constant *C;
    if (match(I0, m_ZExt(m_Value(X))) && match(I1, m_Constant(C)) &&
        I0->hasOneUse()) {
      if (Constant *NarrowC = getLosslessUnsignedTrunc(C, X->getType())) {
        Value *NarrowMaxMin = Builder.CreateBinaryIntrinsic(IID, X, NarrowC);
        return CastInst::Create(Instruction::ZExt, NarrowMaxMin, II->getType());
      }
    }
    // If both operands of unsigned min/max are sign-extended, it is still ok
    // to narrow the operation.
    [[fallthrough]];
  }
  case Intrinsic::smax:
  case Intrinsic::smin: {
    Value *I0 = II->getArgOperand(0), *I1 = II->getArgOperand(1);
    Value *X, *Y;
    if (match(I0, m_SExt(m_Value(X))) && match(I1, m_SExt(m_Value(Y))) &&
        (I0->hasOneUse() || I1->hasOneUse()) && X->getType() == Y->getType()) {
      Value *NarrowMaxMin = Builder.CreateBinaryIntrinsic(IID, X, Y);
      return CastInst::Create(Instruction::SExt, NarrowMaxMin, II->getType());
    }

    Constant *C;
    if (match(I0, m_SExt(m_Value(X))) && match(I1, m_Constant(C)) &&
        I0->hasOneUse()) {
      if (Constant *NarrowC = getLosslessSignedTrunc(C, X->getType())) {
        Value *NarrowMaxMin = Builder.CreateBinaryIntrinsic(IID, X, NarrowC);
        return CastInst::Create(Instruction::SExt, NarrowMaxMin, II->getType());
      }
    }

    // umin(i1 X, i1 Y) -> and i1 X, Y
    // smax(i1 X, i1 Y) -> and i1 X, Y
    if ((IID == Intrinsic::umin || IID == Intrinsic::smax) &&
        II->getType()->isIntOrIntVectorTy(1)) {
      return BinaryOperator::CreateAnd(I0, I1);
    }

    // umax(i1 X, i1 Y) -> or i1 X, Y
    // smin(i1 X, i1 Y) -> or i1 X, Y
    if ((IID == Intrinsic::umax || IID == Intrinsic::smin) &&
        II->getType()->isIntOrIntVectorTy(1)) {
      return BinaryOperator::CreateOr(I0, I1);
    }

    if (IID == Intrinsic::smax || IID == Intrinsic::smin) {
      // smax (neg nsw X), (neg nsw Y) --> neg nsw (smin X, Y)
      // smin (neg nsw X), (neg nsw Y) --> neg nsw (smax X, Y)
      // TODO: Canonicalize neg after min/max if I1 is constant.
      if (match(I0, m_NSWNeg(m_Value(X))) && match(I1, m_NSWNeg(m_Value(Y))) &&
          (I0->hasOneUse() || I1->hasOneUse())) {
        Intrinsic::ID InvID = getInverseMinMaxIntrinsic(IID);
        Value *InvMaxMin = Builder.CreateBinaryIntrinsic(InvID, X, Y);
        return BinaryOperator::CreateNSWNeg(InvMaxMin);
      }
    }

    // (umax X, (xor X, Pow2))
    //      -> (or X, Pow2)
    // (umin X, (xor X, Pow2))
    //      -> (and X, ~Pow2)
    // (smax X, (xor X, Pos_Pow2))
    //      -> (or X, Pos_Pow2)
    // (smin X, (xor X, Pos_Pow2))
    //      -> (and X, ~Pos_Pow2)
    // (smax X, (xor X, Neg_Pow2))
    //      -> (and X, ~Neg_Pow2)
    // (smin X, (xor X, Neg_Pow2))
    //      -> (or X, Neg_Pow2)
    if ((match(I0, m_c_Xor(m_Specific(I1), m_Value(X))) ||
         match(I1, m_c_Xor(m_Specific(I0), m_Value(X)))) &&
        isKnownToBeAPowerOfTwo(X, /* OrZero */ true)) {
      bool UseOr = IID == Intrinsic::smax || IID == Intrinsic::umax;
      bool UseAndN = IID == Intrinsic::smin || IID == Intrinsic::umin;

      if (IID == Intrinsic::smax || IID == Intrinsic::smin) {
        auto KnownSign = getKnownSign(X, SQ.getWithInstruction(II));
        if (KnownSign == std::nullopt) {
          UseOr = false;
          UseAndN = false;
        } else if (*KnownSign /* true is Signed. */) {
          UseOr ^= true;
          UseAndN ^= true;
          Type *Ty = I0->getType();
          // Negative power of 2 must be IntMin. It's possible to be able to
          // prove negative / power of 2 without actually having known bits, so
          // just get the value by hand.
          X = Constant::getIntegerValue(
              Ty, APInt::getSignedMinValue(Ty->getScalarSizeInBits()));
        }
      }
      if (UseOr)
        return BinaryOperator::CreateOr(I0, X);
      else if (UseAndN)
        return BinaryOperator::CreateAnd(I0, Builder.CreateNot(X));
    }

    // If we can eliminate ~A and Y is free to invert:
    // max ~A, Y --> ~(min A, ~Y)
    //
    // Examples:
    // max ~A, ~Y --> ~(min A, Y)
    // max ~A, C --> ~(min A, ~C)
    // max ~A, (max ~Y, ~Z) --> ~min( A, (min Y, Z))
    auto moveNotAfterMinMax = [&](Value *X, Value *Y) -> Instruction * {
      Value *A;
      if (match(X, m_OneUse(m_Not(m_Value(A)))) &&
          !isFreeToInvert(A, A->hasOneUse())) {
        if (Value *NotY = getFreelyInverted(Y, Y->hasOneUse(), &Builder)) {
          Intrinsic::ID InvID = getInverseMinMaxIntrinsic(IID);
          Value *InvMaxMin = Builder.CreateBinaryIntrinsic(InvID, A, NotY);
          return BinaryOperator::CreateNot(InvMaxMin);
        }
      }
      return nullptr;
    };

    if (Instruction *I = moveNotAfterMinMax(I0, I1))
      return I;
    if (Instruction *I = moveNotAfterMinMax(I1, I0))
      return I;

    if (Instruction *I = moveAddAfterMinMax(II, Builder))
      return I;

    // minmax (X & NegPow2C, Y & NegPow2C) --> minmax(X, Y) & NegPow2C
    const APInt *RHSC;
    if (match(I0, m_OneUse(m_And(m_Value(X), m_NegatedPower2(RHSC)))) &&
        match(I1, m_OneUse(m_And(m_Value(Y), m_SpecificInt(*RHSC)))))
      return BinaryOperator::CreateAnd(Builder.CreateBinaryIntrinsic(IID, X, Y),
                                       ConstantInt::get(II->getType(), *RHSC));

    // smax(X, -X) --> abs(X)
    // smin(X, -X) --> -abs(X)
    // umax(X, -X) --> -abs(X)
    // umin(X, -X) --> abs(X)
    if (isKnownNegation(I0, I1)) {
      // We can choose either operand as the input to abs(), but if we can
      // eliminate the only use of a value, that's better for subsequent
      // transforms/analysis.
      if (I0->hasOneUse() && !I1->hasOneUse())
        std::swap(I0, I1);

      // This is some variant of abs(). See if we can propagate 'nsw' to the abs
      // operation and potentially its negation.
      bool IntMinIsPoison = isKnownNegation(I0, I1, /* NeedNSW */ true);
      Value *Abs = Builder.CreateBinaryIntrinsic(
          Intrinsic::abs, I0,
          ConstantInt::getBool(II->getContext(), IntMinIsPoison));

      // We don't have a "nabs" intrinsic, so negate if needed based on the
      // max/min operation.
      if (IID == Intrinsic::smin || IID == Intrinsic::umax)
        Abs = Builder.CreateNeg(Abs, "nabs", IntMinIsPoison);
      return replaceInstUsesWith(CI, Abs);
    }

    if (Instruction *Sel = foldClampRangeOfTwo(II, Builder))
      return Sel;

    if (Instruction *SAdd = matchSAddSubSat(*II))
      return SAdd;

    if (Value *NewMinMax = reassociateMinMaxWithConstants(II, Builder, SQ))
      return replaceInstUsesWith(*II, NewMinMax);

    if (Instruction *R = reassociateMinMaxWithConstantInOperand(II, Builder))
      return R;

    if (Instruction *NewMinMax = factorizeMinMaxTree(II))
       return NewMinMax;

    // Try to fold minmax with constant RHS based on range information
    if (match(I1, m_APIntAllowPoison(RHSC))) {
      ICmpInst::Predicate Pred =
          ICmpInst::getNonStrictPredicate(MinMaxIntrinsic::getPredicate(IID));
      bool IsSigned = MinMaxIntrinsic::isSigned(IID);
      ConstantRange LHS_CR = computeConstantRangeIncludingKnownBits(
          I0, IsSigned, SQ.getWithInstruction(II));
      if (!LHS_CR.isFullSet()) {
        if (LHS_CR.icmp(Pred, *RHSC))
          return replaceInstUsesWith(*II, I0);
        if (LHS_CR.icmp(ICmpInst::getSwappedPredicate(Pred), *RHSC))
          return replaceInstUsesWith(*II,
                                     ConstantInt::get(II->getType(), *RHSC));
      }
    }

    break;
  }
  case Intrinsic::bitreverse: {
    Value *IIOperand = II->getArgOperand(0);
    // bitrev (zext i1 X to ?) --> X ? SignBitC : 0
    Value *X;
    if (match(IIOperand, m_ZExt(m_Value(X))) &&
        X->getType()->isIntOrIntVectorTy(1)) {
      Type *Ty = II->getType();
      APInt SignBit = APInt::getSignMask(Ty->getScalarSizeInBits());
      return SelectInst::Create(X, ConstantInt::get(Ty, SignBit),
                                ConstantInt::getNullValue(Ty));
    }

    if (Instruction *crossLogicOpFold =
        foldBitOrderCrossLogicOp<Intrinsic::bitreverse>(IIOperand, Builder))
      return crossLogicOpFold;

    break;
  }
  case Intrinsic::bswap: {
    Value *IIOperand = II->getArgOperand(0);

    // Try to canonicalize bswap-of-logical-shift-by-8-bit-multiple as
    // inverse-shift-of-bswap:
    // bswap (shl X, Y) --> lshr (bswap X), Y
    // bswap (lshr X, Y) --> shl (bswap X), Y
    Value *X, *Y;
    if (match(IIOperand, m_OneUse(m_LogicalShift(m_Value(X), m_Value(Y))))) {
      unsigned BitWidth = IIOperand->getType()->getScalarSizeInBits();
      if (MaskedValueIsZero(Y, APInt::getLowBitsSet(BitWidth, 3))) {
        Value *NewSwap = Builder.CreateUnaryIntrinsic(Intrinsic::bswap, X);
        BinaryOperator::BinaryOps InverseShift =
            cast<BinaryOperator>(IIOperand)->getOpcode() == Instruction::Shl
                ? Instruction::LShr
                : Instruction::Shl;
        return BinaryOperator::Create(InverseShift, NewSwap, Y);
      }
    }

    KnownBits Known = computeKnownBits(IIOperand, 0, II);
    uint64_t LZ = alignDown(Known.countMinLeadingZeros(), 8);
    uint64_t TZ = alignDown(Known.countMinTrailingZeros(), 8);
    unsigned BW = Known.getBitWidth();

    // bswap(x) -> shift(x) if x has exactly one "active byte"
    if (BW - LZ - TZ == 8) {
      assert(LZ != TZ && "active byte cannot be in the middle");
      if (LZ > TZ)  // -> shl(x) if the "active byte" is in the low part of x
        return BinaryOperator::CreateNUWShl(
            IIOperand, ConstantInt::get(IIOperand->getType(), LZ - TZ));
      // -> lshr(x) if the "active byte" is in the high part of x
      return BinaryOperator::CreateExactLShr(
            IIOperand, ConstantInt::get(IIOperand->getType(), TZ - LZ));
    }

    // bswap(trunc(bswap(x))) -> trunc(lshr(x, c))
    if (match(IIOperand, m_Trunc(m_BSwap(m_Value(X))))) {
      unsigned C = X->getType()->getScalarSizeInBits() - BW;
      Value *CV = ConstantInt::get(X->getType(), C);
      Value *V = Builder.CreateLShr(X, CV);
      return new TruncInst(V, IIOperand->getType());
    }

    if (Instruction *crossLogicOpFold =
            foldBitOrderCrossLogicOp<Intrinsic::bswap>(IIOperand, Builder)) {
      return crossLogicOpFold;
    }

    // Try to fold into bitreverse if bswap is the root of the expression tree.
    if (Instruction *BitOp = matchBSwapOrBitReverse(*II, /*MatchBSwaps*/ false,
                                                    /*MatchBitReversals*/ true))
      return BitOp;
    break;
  }
  case Intrinsic::masked_load:
    if (Value *SimplifiedMaskedOp = simplifyMaskedLoad(*II))
      return replaceInstUsesWith(CI, SimplifiedMaskedOp);
    break;
  case Intrinsic::masked_store:
    return simplifyMaskedStore(*II);
  case Intrinsic::masked_gather:
    return simplifyMaskedGather(*II);
  case Intrinsic::masked_scatter:
    return simplifyMaskedScatter(*II);
  case Intrinsic::launder_invariant_group:
  case Intrinsic::strip_invariant_group:
    if (auto *SkippedBarrier = simplifyInvariantGroupIntrinsic(*II, *this))
      return replaceInstUsesWith(*II, SkippedBarrier);
    break;
  case Intrinsic::powi:
    if (ConstantInt *Power = dyn_cast<ConstantInt>(II->getArgOperand(1))) {
      // 0 and 1 are handled in instsimplify
      // powi(x, -1) -> 1/x
      if (Power->isMinusOne())
        return BinaryOperator::CreateFDivFMF(ConstantFP::get(CI.getType(), 1.0),
                                             II->getArgOperand(0), II);
      // powi(x, 2) -> x*x
      if (Power->equalsInt(2))
        return BinaryOperator::CreateFMulFMF(II->getArgOperand(0),
                                             II->getArgOperand(0), II);

      if (!Power->getValue()[0]) {
        Value *X;
        // If power is even:
        // powi(-x, p) -> powi(x, p)
        // powi(fabs(x), p) -> powi(x, p)
        // powi(copysign(x, y), p) -> powi(x, p)
        if (match(II->getArgOperand(0), m_FNeg(m_Value(X))) ||
            match(II->getArgOperand(0), m_FAbs(m_Value(X))) ||
            match(II->getArgOperand(0),
                  m_Intrinsic<Intrinsic::copysign>(m_Value(X), m_Value())))
          return replaceOperand(*II, 0, X);
      }
    }
    break;

  case Intrinsic::cttz:
  case Intrinsic::ctlz:
    if (auto *I = foldCttzCtlz(*II, *this))
      return I;
    break;

  case Intrinsic::ctpop:
    if (auto *I = foldCtpop(*II, *this))
      return I;
    break;

  case Intrinsic::fshl:
  case Intrinsic::fshr: {
    Value *Op0 = II->getArgOperand(0), *Op1 = II->getArgOperand(1);
    Type *Ty = II->getType();
    unsigned BitWidth = Ty->getScalarSizeInBits();
    Constant *ShAmtC;
    if (match(II->getArgOperand(2), m_ImmConstant(ShAmtC))) {
      // Canonicalize a shift amount constant operand to modulo the bit-width.
      Constant *WidthC = ConstantInt::get(Ty, BitWidth);
      Constant *ModuloC =
          ConstantFoldBinaryOpOperands(Instruction::URem, ShAmtC, WidthC, DL);
      if (!ModuloC)
        return nullptr;
      if (ModuloC != ShAmtC)
        return replaceOperand(*II, 2, ModuloC);

      assert(match(ConstantFoldCompareInstOperands(ICmpInst::ICMP_UGT, WidthC,
                                                   ShAmtC, DL),
                   m_One()) &&
             "Shift amount expected to be modulo bitwidth");

      // Canonicalize funnel shift right by constant to funnel shift left. This
      // is not entirely arbitrary. For historical reasons, the backend may
      // recognize rotate left patterns but miss rotate right patterns.
      if (IID == Intrinsic::fshr) {
        // fshr X, Y, C --> fshl X, Y, (BitWidth - C) if C is not zero.
        if (!isKnownNonZero(ShAmtC, SQ.getWithInstruction(II)))
          return nullptr;

        Constant *LeftShiftC = ConstantExpr::getSub(WidthC, ShAmtC);
        Module *Mod = II->getModule();
        Function *Fshl = Intrinsic::getDeclaration(Mod, Intrinsic::fshl, Ty);
        return CallInst::Create(Fshl, { Op0, Op1, LeftShiftC });
      }
      assert(IID == Intrinsic::fshl &&
             "All funnel shifts by simple constants should go left");

      // fshl(X, 0, C) --> shl X, C
      // fshl(X, undef, C) --> shl X, C
      if (match(Op1, m_ZeroInt()) || match(Op1, m_Undef()))
        return BinaryOperator::CreateShl(Op0, ShAmtC);

      // fshl(0, X, C) --> lshr X, (BW-C)
      // fshl(undef, X, C) --> lshr X, (BW-C)
      if (match(Op0, m_ZeroInt()) || match(Op0, m_Undef()))
        return BinaryOperator::CreateLShr(Op1,
                                          ConstantExpr::getSub(WidthC, ShAmtC));

      // fshl i16 X, X, 8 --> bswap i16 X (reduce to more-specific form)
      if (Op0 == Op1 && BitWidth == 16 && match(ShAmtC, m_SpecificInt(8))) {
        Module *Mod = II->getModule();
        Function *Bswap = Intrinsic::getDeclaration(Mod, Intrinsic::bswap, Ty);
        return CallInst::Create(Bswap, { Op0 });
      }
      if (Instruction *BitOp =
              matchBSwapOrBitReverse(*II, /*MatchBSwaps*/ true,
                                     /*MatchBitReversals*/ true))
        return BitOp;
    }

    // Left or right might be masked.
    if (SimplifyDemandedInstructionBits(*II))
      return &CI;

    // The shift amount (operand 2) of a funnel shift is modulo the bitwidth,
    // so only the low bits of the shift amount are demanded if the bitwidth is
    // a power-of-2.
    if (!isPowerOf2_32(BitWidth))
      break;
    APInt Op2Demanded = APInt::getLowBitsSet(BitWidth, Log2_32_Ceil(BitWidth));
    KnownBits Op2Known(BitWidth);
    if (SimplifyDemandedBits(II, 2, Op2Demanded, Op2Known))
      return &CI;
    break;
  }
  case Intrinsic::ptrmask: {
    unsigned BitWidth = DL.getPointerTypeSizeInBits(II->getType());
    KnownBits Known(BitWidth);
    if (SimplifyDemandedInstructionBits(*II, Known))
      return II;

    Value *InnerPtr, *InnerMask;
    bool Changed = false;
    // Combine:
    // (ptrmask (ptrmask p, A), B)
    //    -> (ptrmask p, (and A, B))
    if (match(II->getArgOperand(0),
              m_OneUse(m_Intrinsic<Intrinsic::ptrmask>(m_Value(InnerPtr),
                                                       m_Value(InnerMask))))) {
      assert(II->getArgOperand(1)->getType() == InnerMask->getType() &&
             "Mask types must match");
      // TODO: If InnerMask == Op1, we could copy attributes from inner
      // callsite -> outer callsite.
      Value *NewMask = Builder.CreateAnd(II->getArgOperand(1), InnerMask);
      replaceOperand(CI, 0, InnerPtr);
      replaceOperand(CI, 1, NewMask);
      Changed = true;
    }

    // See if we can deduce non-null.
    if (!CI.hasRetAttr(Attribute::NonNull) &&
        (Known.isNonZero() ||
         isKnownNonZero(II, getSimplifyQuery().getWithInstruction(II)))) {
      CI.addRetAttr(Attribute::NonNull);
      Changed = true;
    }

    unsigned NewAlignmentLog =
        std::min(Value::MaxAlignmentExponent,
                 std::min(BitWidth - 1, Known.countMinTrailingZeros()));
    // Known bits will capture if we had alignment information associated with
    // the pointer argument.
    if (NewAlignmentLog > Log2(CI.getRetAlign().valueOrOne())) {
      CI.addRetAttr(Attribute::getWithAlignment(
          CI.getContext(), Align(uint64_t(1) << NewAlignmentLog)));
      Changed = true;
    }
    if (Changed)
      return &CI;
    break;
  }
  case Intrinsic::uadd_with_overflow:
  case Intrinsic::sadd_with_overflow: {
    if (Instruction *I = foldIntrinsicWithOverflowCommon(II))
      return I;

    // Given 2 constant operands whose sum does not overflow:
    // uaddo (X +nuw C0), C1 -> uaddo X, C0 + C1
    // saddo (X +nsw C0), C1 -> saddo X, C0 + C1
    Value *X;
    const APInt *C0, *C1;
    Value *Arg0 = II->getArgOperand(0);
    Value *Arg1 = II->getArgOperand(1);
    bool IsSigned = IID == Intrinsic::sadd_with_overflow;
    bool HasNWAdd = IsSigned
                        ? match(Arg0, m_NSWAddLike(m_Value(X), m_APInt(C0)))
                        : match(Arg0, m_NUWAddLike(m_Value(X), m_APInt(C0)));
    if (HasNWAdd && match(Arg1, m_APInt(C1))) {
      bool Overflow;
      APInt NewC =
          IsSigned ? C1->sadd_ov(*C0, Overflow) : C1->uadd_ov(*C0, Overflow);
      if (!Overflow)
        return replaceInstUsesWith(
            *II, Builder.CreateBinaryIntrinsic(
                     IID, X, ConstantInt::get(Arg1->getType(), NewC)));
    }
    break;
  }

  case Intrinsic::umul_with_overflow:
  case Intrinsic::smul_with_overflow:
  case Intrinsic::usub_with_overflow:
    if (Instruction *I = foldIntrinsicWithOverflowCommon(II))
      return I;
    break;

  case Intrinsic::ssub_with_overflow: {
    if (Instruction *I = foldIntrinsicWithOverflowCommon(II))
      return I;

    Constant *C;
    Value *Arg0 = II->getArgOperand(0);
    Value *Arg1 = II->getArgOperand(1);
    // Given a constant C that is not the minimum signed value
    // for an integer of a given bit width:
    //
    // ssubo X, C -> saddo X, -C
    if (match(Arg1, m_Constant(C)) && C->isNotMinSignedValue()) {
      Value *NegVal = ConstantExpr::getNeg(C);
      // Build a saddo call that is equivalent to the discovered
      // ssubo call.
      return replaceInstUsesWith(
          *II, Builder.CreateBinaryIntrinsic(Intrinsic::sadd_with_overflow,
                                             Arg0, NegVal));
    }

    break;
  }

  case Intrinsic::uadd_sat:
  case Intrinsic::sadd_sat:
  case Intrinsic::usub_sat:
  case Intrinsic::ssub_sat: {
    SaturatingInst *SI = cast<SaturatingInst>(II);
    Type *Ty = SI->getType();
    Value *Arg0 = SI->getLHS();
    Value *Arg1 = SI->getRHS();

    // Make use of known overflow information.
    OverflowResult OR = computeOverflow(SI->getBinaryOp(), SI->isSigned(),
                                        Arg0, Arg1, SI);
    switch (OR) {
      case OverflowResult::MayOverflow:
        break;
      case OverflowResult::NeverOverflows:
        if (SI->isSigned())
          return BinaryOperator::CreateNSW(SI->getBinaryOp(), Arg0, Arg1);
        else
          return BinaryOperator::CreateNUW(SI->getBinaryOp(), Arg0, Arg1);
      case OverflowResult::AlwaysOverflowsLow: {
        unsigned BitWidth = Ty->getScalarSizeInBits();
        APInt Min = APSInt::getMinValue(BitWidth, !SI->isSigned());
        return replaceInstUsesWith(*SI, ConstantInt::get(Ty, Min));
      }
      case OverflowResult::AlwaysOverflowsHigh: {
        unsigned BitWidth = Ty->getScalarSizeInBits();
        APInt Max = APSInt::getMaxValue(BitWidth, !SI->isSigned());
        return replaceInstUsesWith(*SI, ConstantInt::get(Ty, Max));
      }
    }

    // usub_sat((sub nuw C, A), C1) -> usub_sat(usub_sat(C, C1), A)
    // which after that:
    // usub_sat((sub nuw C, A), C1) -> usub_sat(C - C1, A) if C1 u< C
    // usub_sat((sub nuw C, A), C1) -> 0 otherwise
    Constant *C, *C1;
    Value *A;
    if (IID == Intrinsic::usub_sat &&
        match(Arg0, m_NUWSub(m_ImmConstant(C), m_Value(A))) &&
        match(Arg1, m_ImmConstant(C1))) {
      auto *NewC = Builder.CreateBinaryIntrinsic(Intrinsic::usub_sat, C, C1);
      auto *NewSub =
          Builder.CreateBinaryIntrinsic(Intrinsic::usub_sat, NewC, A);
      return replaceInstUsesWith(*SI, NewSub);
    }

    // ssub.sat(X, C) -> sadd.sat(X, -C) if C != MIN
    if (IID == Intrinsic::ssub_sat && match(Arg1, m_Constant(C)) &&
        C->isNotMinSignedValue()) {
      Value *NegVal = ConstantExpr::getNeg(C);
      return replaceInstUsesWith(
          *II, Builder.CreateBinaryIntrinsic(
              Intrinsic::sadd_sat, Arg0, NegVal));
    }

    // sat(sat(X + Val2) + Val) -> sat(X + (Val+Val2))
    // sat(sat(X - Val2) - Val) -> sat(X - (Val+Val2))
    // if Val and Val2 have the same sign
    if (auto *Other = dyn_cast<IntrinsicInst>(Arg0)) {
      Value *X;
      const APInt *Val, *Val2;
      APInt NewVal;
      bool IsUnsigned =
          IID == Intrinsic::uadd_sat || IID == Intrinsic::usub_sat;
      if (Other->getIntrinsicID() == IID &&
          match(Arg1, m_APInt(Val)) &&
          match(Other->getArgOperand(0), m_Value(X)) &&
          match(Other->getArgOperand(1), m_APInt(Val2))) {
        if (IsUnsigned)
          NewVal = Val->uadd_sat(*Val2);
        else if (Val->isNonNegative() == Val2->isNonNegative()) {
          bool Overflow;
          NewVal = Val->sadd_ov(*Val2, Overflow);
          if (Overflow) {
            // Both adds together may add more than SignedMaxValue
            // without saturating the final result.
            break;
          }
        } else {
          // Cannot fold saturated addition with different signs.
          break;
        }

        return replaceInstUsesWith(
            *II, Builder.CreateBinaryIntrinsic(
                     IID, X, ConstantInt::get(II->getType(), NewVal)));
      }
    }
    break;
  }

  case Intrinsic::minnum:
  case Intrinsic::maxnum:
  case Intrinsic::minimum:
  case Intrinsic::maximum: {
    Value *Arg0 = II->getArgOperand(0);
    Value *Arg1 = II->getArgOperand(1);
    Value *X, *Y;
    if (match(Arg0, m_FNeg(m_Value(X))) && match(Arg1, m_FNeg(m_Value(Y))) &&
        (Arg0->hasOneUse() || Arg1->hasOneUse())) {
      // If both operands are negated, invert the call and negate the result:
      // min(-X, -Y) --> -(max(X, Y))
      // max(-X, -Y) --> -(min(X, Y))
      Intrinsic::ID NewIID;
      switch (IID) {
      case Intrinsic::maxnum:
        NewIID = Intrinsic::minnum;
        break;
      case Intrinsic::minnum:
        NewIID = Intrinsic::maxnum;
        break;
      case Intrinsic::maximum:
        NewIID = Intrinsic::minimum;
        break;
      case Intrinsic::minimum:
        NewIID = Intrinsic::maximum;
        break;
      default:
        llvm_unreachable("unexpected intrinsic ID");
      }
      Value *NewCall = Builder.CreateBinaryIntrinsic(NewIID, X, Y, II);
      Instruction *FNeg = UnaryOperator::CreateFNeg(NewCall);
      FNeg->copyIRFlags(II);
      return FNeg;
    }

    // m(m(X, C2), C1) -> m(X, C)
    const APFloat *C1, *C2;
    if (auto *M = dyn_cast<IntrinsicInst>(Arg0)) {
      if (M->getIntrinsicID() == IID && match(Arg1, m_APFloat(C1)) &&
          ((match(M->getArgOperand(0), m_Value(X)) &&
            match(M->getArgOperand(1), m_APFloat(C2))) ||
           (match(M->getArgOperand(1), m_Value(X)) &&
            match(M->getArgOperand(0), m_APFloat(C2))))) {
        APFloat Res(0.0);
        switch (IID) {
        case Intrinsic::maxnum:
          Res = maxnum(*C1, *C2);
          break;
        case Intrinsic::minnum:
          Res = minnum(*C1, *C2);
          break;
        case Intrinsic::maximum:
          Res = maximum(*C1, *C2);
          break;
        case Intrinsic::minimum:
          Res = minimum(*C1, *C2);
          break;
        default:
          llvm_unreachable("unexpected intrinsic ID");
        }
        Value *V = Builder.CreateBinaryIntrinsic(
            IID, X, ConstantFP::get(Arg0->getType(), Res), II);
        // TODO: Conservatively intersecting FMF. If Res == C2, the transform
        //       was a simplification (so Arg0 and its original flags could
        //       propagate?)
        if (auto *CI = dyn_cast<CallInst>(V))
          CI->andIRFlags(M);
        return replaceInstUsesWith(*II, V);
      }
    }

    // m((fpext X), (fpext Y)) -> fpext (m(X, Y))
    if (match(Arg0, m_OneUse(m_FPExt(m_Value(X)))) &&
        match(Arg1, m_OneUse(m_FPExt(m_Value(Y)))) &&
        X->getType() == Y->getType()) {
      Value *NewCall =
          Builder.CreateBinaryIntrinsic(IID, X, Y, II, II->getName());
      return new FPExtInst(NewCall, II->getType());
    }

    // max X, -X --> fabs X
    // min X, -X --> -(fabs X)
    // TODO: Remove one-use limitation? That is obviously better for max,
    // hence why we don't check for one-use for that. However,
    // it would be an extra instruction for min (fnabs), but
    // that is still likely better for analysis and codegen.
    auto IsMinMaxOrXNegX = [IID, &X](Value *Op0, Value *Op1) {
      if (match(Op0, m_FNeg(m_Value(X))) && match(Op1, m_Specific(X)))
        return Op0->hasOneUse() ||
               (IID != Intrinsic::minimum && IID != Intrinsic::minnum);
      return false;
    };

    if (IsMinMaxOrXNegX(Arg0, Arg1) || IsMinMaxOrXNegX(Arg1, Arg0)) {
      Value *R = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, II);
      if (IID == Intrinsic::minimum || IID == Intrinsic::minnum)
        R = Builder.CreateFNegFMF(R, II);
      return replaceInstUsesWith(*II, R);
    }

    break;
  }
  case Intrinsic::matrix_multiply: {
    // Optimize negation in matrix multiplication.

    // -A * -B -> A * B
    Value *A, *B;
    if (match(II->getArgOperand(0), m_FNeg(m_Value(A))) &&
        match(II->getArgOperand(1), m_FNeg(m_Value(B)))) {
      replaceOperand(*II, 0, A);
      replaceOperand(*II, 1, B);
      return II;
    }

    Value *Op0 = II->getOperand(0);
    Value *Op1 = II->getOperand(1);
    Value *OpNotNeg, *NegatedOp;
    unsigned NegatedOpArg, OtherOpArg;
    if (match(Op0, m_FNeg(m_Value(OpNotNeg)))) {
      NegatedOp = Op0;
      NegatedOpArg = 0;
      OtherOpArg = 1;
    } else if (match(Op1, m_FNeg(m_Value(OpNotNeg)))) {
      NegatedOp = Op1;
      NegatedOpArg = 1;
      OtherOpArg = 0;
    } else
      // Multiplication doesn't have a negated operand.
      break;

    // Only optimize if the negated operand has only one use.
    if (!NegatedOp->hasOneUse())
      break;

    Value *OtherOp = II->getOperand(OtherOpArg);
    VectorType *RetTy = cast<VectorType>(II->getType());
    VectorType *NegatedOpTy = cast<VectorType>(NegatedOp->getType());
    VectorType *OtherOpTy = cast<VectorType>(OtherOp->getType());
    ElementCount NegatedCount = NegatedOpTy->getElementCount();
    ElementCount OtherCount = OtherOpTy->getElementCount();
    ElementCount RetCount = RetTy->getElementCount();
    // (-A) * B -> A * (-B), if it is cheaper to negate B and vice versa.
    if (ElementCount::isKnownGT(NegatedCount, OtherCount) &&
        ElementCount::isKnownLT(OtherCount, RetCount)) {
      Value *InverseOtherOp = Builder.CreateFNeg(OtherOp);
      replaceOperand(*II, NegatedOpArg, OpNotNeg);
      replaceOperand(*II, OtherOpArg, InverseOtherOp);
      return II;
    }
    // (-A) * B -> -(A * B), if it is cheaper to negate the result
    if (ElementCount::isKnownGT(NegatedCount, RetCount)) {
      SmallVector<Value *, 5> NewArgs(II->args());
      NewArgs[NegatedOpArg] = OpNotNeg;
      Instruction *NewMul =
          Builder.CreateIntrinsic(II->getType(), IID, NewArgs, II);
      return replaceInstUsesWith(*II, Builder.CreateFNegFMF(NewMul, II));
    }
    break;
  }
  case Intrinsic::fmuladd: {
    // Try to simplify the underlying FMul.
    if (Value *V = simplifyFMulInst(II->getArgOperand(0), II->getArgOperand(1),
                                    II->getFastMathFlags(),
                                    SQ.getWithInstruction(II))) {
      auto *FAdd = BinaryOperator::CreateFAdd(V, II->getArgOperand(2));
      FAdd->copyFastMathFlags(II);
      return FAdd;
    }

    [[fallthrough]];
  }
  case Intrinsic::fma: {
    // fma fneg(x), fneg(y), z -> fma x, y, z
    Value *Src0 = II->getArgOperand(0);
    Value *Src1 = II->getArgOperand(1);
    Value *X, *Y;
    if (match(Src0, m_FNeg(m_Value(X))) && match(Src1, m_FNeg(m_Value(Y)))) {
      replaceOperand(*II, 0, X);
      replaceOperand(*II, 1, Y);
      return II;
    }

    // fma fabs(x), fabs(x), z -> fma x, x, z
    if (match(Src0, m_FAbs(m_Value(X))) &&
        match(Src1, m_FAbs(m_Specific(X)))) {
      replaceOperand(*II, 0, X);
      replaceOperand(*II, 1, X);
      return II;
    }

    // Try to simplify the underlying FMul. We can only apply simplifications
    // that do not require rounding.
    if (Value *V = simplifyFMAFMul(II->getArgOperand(0), II->getArgOperand(1),
                                   II->getFastMathFlags(),
                                   SQ.getWithInstruction(II))) {
      auto *FAdd = BinaryOperator::CreateFAdd(V, II->getArgOperand(2));
      FAdd->copyFastMathFlags(II);
      return FAdd;
    }

    // fma x, y, 0 -> fmul x, y
    // This is always valid for -0.0, but requires nsz for +0.0 as
    // -0.0 + 0.0 = 0.0, which would not be the same as the fmul on its own.
    if (match(II->getArgOperand(2), m_NegZeroFP()) ||
        (match(II->getArgOperand(2), m_PosZeroFP()) &&
         II->getFastMathFlags().noSignedZeros()))
      return BinaryOperator::CreateFMulFMF(Src0, Src1, II);

    break;
  }
  case Intrinsic::copysign: {
    Value *Mag = II->getArgOperand(0), *Sign = II->getArgOperand(1);
    if (std::optional<bool> KnownSignBit = computeKnownFPSignBit(
            Sign, /*Depth=*/0, getSimplifyQuery().getWithInstruction(II))) {
      if (*KnownSignBit) {
        // If we know that the sign argument is negative, reduce to FNABS:
        // copysign Mag, -Sign --> fneg (fabs Mag)
        Value *Fabs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, Mag, II);
        return replaceInstUsesWith(*II, Builder.CreateFNegFMF(Fabs, II));
      }

      // If we know that the sign argument is positive, reduce to FABS:
      // copysign Mag, +Sign --> fabs Mag
      Value *Fabs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, Mag, II);
      return replaceInstUsesWith(*II, Fabs);
    }

    // Propagate sign argument through nested calls:
    // copysign Mag, (copysign ?, X) --> copysign Mag, X
    Value *X;
    if (match(Sign, m_Intrinsic<Intrinsic::copysign>(m_Value(), m_Value(X))))
      return replaceOperand(*II, 1, X);

    // Clear sign-bit of constant magnitude:
    // copysign -MagC, X --> copysign MagC, X
    // TODO: Support constant folding for fabs
    const APFloat *MagC;
    if (match(Mag, m_APFloat(MagC)) && MagC->isNegative()) {
      APFloat PosMagC = *MagC;
      PosMagC.clearSign();
      return replaceOperand(*II, 0, ConstantFP::get(Mag->getType(), PosMagC));
    }

    // Peek through changes of magnitude's sign-bit. This call rewrites those:
    // copysign (fabs X), Sign --> copysign X, Sign
    // copysign (fneg X), Sign --> copysign X, Sign
    if (match(Mag, m_FAbs(m_Value(X))) || match(Mag, m_FNeg(m_Value(X))))
      return replaceOperand(*II, 0, X);

    break;
  }
  case Intrinsic::fabs: {
    Value *Cond, *TVal, *FVal;
    Value *Arg = II->getArgOperand(0);
    Value *X;
    // fabs (-X) --> fabs (X)
    if (match(Arg, m_FNeg(m_Value(X)))) {
        CallInst *Fabs = Builder.CreateUnaryIntrinsic(Intrinsic::fabs, X, II);
        return replaceInstUsesWith(CI, Fabs);
    }

    if (match(Arg, m_Select(m_Value(Cond), m_Value(TVal), m_Value(FVal)))) {
      // fabs (select Cond, TrueC, FalseC) --> select Cond, AbsT, AbsF
      if (isa<Constant>(TVal) || isa<Constant>(FVal)) {
        CallInst *AbsT = Builder.CreateCall(II->getCalledFunction(), {TVal});
        CallInst *AbsF = Builder.CreateCall(II->getCalledFunction(), {FVal});
        SelectInst *SI = SelectInst::Create(Cond, AbsT, AbsF);
        FastMathFlags FMF1 = II->getFastMathFlags();
        FastMathFlags FMF2 = cast<SelectInst>(Arg)->getFastMathFlags();
        FMF2.setNoSignedZeros(false);
        SI->setFastMathFlags(FMF1 | FMF2);
        return SI;
      }
      // fabs (select Cond, -FVal, FVal) --> fabs FVal
      if (match(TVal, m_FNeg(m_Specific(FVal))))
        return replaceOperand(*II, 0, FVal);
      // fabs (select Cond, TVal, -TVal) --> fabs TVal
      if (match(FVal, m_FNeg(m_Specific(TVal))))
        return replaceOperand(*II, 0, TVal);
    }

    Value *Magnitude, *Sign;
    if (match(II->getArgOperand(0),
              m_CopySign(m_Value(Magnitude), m_Value(Sign)))) {
      // fabs (copysign x, y) -> (fabs x)
      CallInst *AbsSign =
          Builder.CreateCall(II->getCalledFunction(), {Magnitude});
      AbsSign->copyFastMathFlags(II);
      return replaceInstUsesWith(*II, AbsSign);
    }

    [[fallthrough]];
  }
  case Intrinsic::ceil:
  case Intrinsic::floor:
  case Intrinsic::round:
  case Intrinsic::roundeven:
  case Intrinsic::nearbyint:
  case Intrinsic::rint:
  case Intrinsic::trunc: {
    Value *ExtSrc;
    if (match(II->getArgOperand(0), m_OneUse(m_FPExt(m_Value(ExtSrc))))) {
      // Narrow the call: intrinsic (fpext x) -> fpext (intrinsic x)
      Value *NarrowII = Builder.CreateUnaryIntrinsic(IID, ExtSrc, II);
      return new FPExtInst(NarrowII, II->getType());
    }
    break;
  }
  case Intrinsic::cos:
  case Intrinsic::amdgcn_cos: {
    Value *X, *Sign;
    Value *Src = II->getArgOperand(0);
    if (match(Src, m_FNeg(m_Value(X))) || match(Src, m_FAbs(m_Value(X))) ||
        match(Src, m_CopySign(m_Value(X), m_Value(Sign)))) {
      // cos(-x) --> cos(x)
      // cos(fabs(x)) --> cos(x)
      // cos(copysign(x, y)) --> cos(x)
      return replaceOperand(*II, 0, X);
    }
    break;
  }
  case Intrinsic::sin:
  case Intrinsic::amdgcn_sin: {
    Value *X;
    if (match(II->getArgOperand(0), m_OneUse(m_FNeg(m_Value(X))))) {
      // sin(-x) --> -sin(x)
      Value *NewSin = Builder.CreateUnaryIntrinsic(IID, X, II);
      return UnaryOperator::CreateFNegFMF(NewSin, II);
    }
    break;
  }
  case Intrinsic::ldexp: {
    // ldexp(ldexp(x, a), b) -> ldexp(x, a + b)
    //
    // The danger is if the first ldexp would overflow to infinity or underflow
    // to zero, but the combined exponent avoids it. We ignore this with
    // reassoc.
    //
    // It's also safe to fold if we know both exponents are >= 0 or <= 0 since
    // it would just double down on the overflow/underflow which would occur
    // anyway.
    //
    // TODO: Could do better if we had range tracking for the input value
    // exponent. Also could broaden sign check to cover == 0 case.
    Value *Src = II->getArgOperand(0);
    Value *Exp = II->getArgOperand(1);
    Value *InnerSrc;
    Value *InnerExp;
    if (match(Src, m_OneUse(m_Intrinsic<Intrinsic::ldexp>(
                       m_Value(InnerSrc), m_Value(InnerExp)))) &&
        Exp->getType() == InnerExp->getType()) {
      FastMathFlags FMF = II->getFastMathFlags();
      FastMathFlags InnerFlags = cast<FPMathOperator>(Src)->getFastMathFlags();

      if ((FMF.allowReassoc() && InnerFlags.allowReassoc()) ||
          signBitMustBeTheSame(Exp, InnerExp, SQ.getWithInstruction(II))) {
        // TODO: Add nsw/nuw probably safe if integer type exceeds exponent
        // width.
        Value *NewExp = Builder.CreateAdd(InnerExp, Exp);
        II->setArgOperand(1, NewExp);
        II->setFastMathFlags(InnerFlags); // Or the inner flags.
        return replaceOperand(*II, 0, InnerSrc);
      }
    }

    // ldexp(x, zext(i1 y)) -> fmul x, (select y, 2.0, 1.0)
    // ldexp(x, sext(i1 y)) -> fmul x, (select y, 0.5, 1.0)
    Value *ExtSrc;
    if (match(Exp, m_ZExt(m_Value(ExtSrc))) &&
        ExtSrc->getType()->getScalarSizeInBits() == 1) {
      Value *Select =
          Builder.CreateSelect(ExtSrc, ConstantFP::get(II->getType(), 2.0),
                               ConstantFP::get(II->getType(), 1.0));
      return BinaryOperator::CreateFMulFMF(Src, Select, II);
    }
    if (match(Exp, m_SExt(m_Value(ExtSrc))) &&
        ExtSrc->getType()->getScalarSizeInBits() == 1) {
      Value *Select =
          Builder.CreateSelect(ExtSrc, ConstantFP::get(II->getType(), 0.5),
                               ConstantFP::get(II->getType(), 1.0));
      return BinaryOperator::CreateFMulFMF(Src, Select, II);
    }

    // ldexp(x, c ? exp : 0) -> c ? ldexp(x, exp) : x
    // ldexp(x, c ? 0 : exp) -> c ? x : ldexp(x, exp)
    ///
    // TODO: If we cared, should insert a canonicalize for x
    Value *SelectCond, *SelectLHS, *SelectRHS;
    if (match(II->getArgOperand(1),
              m_OneUse(m_Select(m_Value(SelectCond), m_Value(SelectLHS),
                                m_Value(SelectRHS))))) {
      Value *NewLdexp = nullptr;
      Value *Select = nullptr;
      if (match(SelectRHS, m_ZeroInt())) {
        NewLdexp = Builder.CreateLdexp(Src, SelectLHS);
        Select = Builder.CreateSelect(SelectCond, NewLdexp, Src);
      } else if (match(SelectLHS, m_ZeroInt())) {
        NewLdexp = Builder.CreateLdexp(Src, SelectRHS);
        Select = Builder.CreateSelect(SelectCond, Src, NewLdexp);
      }

      if (NewLdexp) {
        Select->takeName(II);
        cast<Instruction>(NewLdexp)->copyFastMathFlags(II);
        return replaceInstUsesWith(*II, Select);
      }
    }

    break;
  }
  case Intrinsic::ptrauth_auth:
  case Intrinsic::ptrauth_resign: {
    // (sign|resign) + (auth|resign) can be folded by omitting the middle
    // sign+auth component if the key and discriminator match.
    bool NeedSign = II->getIntrinsicID() == Intrinsic::ptrauth_resign;
    Value *Ptr = II->getArgOperand(0);
    Value *Key = II->getArgOperand(1);
    Value *Disc = II->getArgOperand(2);

    // AuthKey will be the key we need to end up authenticating against in
    // whatever we replace this sequence with.
    Value *AuthKey = nullptr, *AuthDisc = nullptr, *BasePtr;
    if (const auto *CI = dyn_cast<CallBase>(Ptr)) {
      BasePtr = CI->getArgOperand(0);
      if (CI->getIntrinsicID() == Intrinsic::ptrauth_sign) {
        if (CI->getArgOperand(1) != Key || CI->getArgOperand(2) != Disc)
          break;
      } else if (CI->getIntrinsicID() == Intrinsic::ptrauth_resign) {
        if (CI->getArgOperand(3) != Key || CI->getArgOperand(4) != Disc)
          break;
        AuthKey = CI->getArgOperand(1);
        AuthDisc = CI->getArgOperand(2);
      } else
        break;
    } else if (const auto *PtrToInt = dyn_cast<PtrToIntOperator>(Ptr)) {
      // ptrauth constants are equivalent to a call to @llvm.ptrauth.sign for
      // our purposes, so check for that too.
      const auto *CPA = dyn_cast<ConstantPtrAuth>(PtrToInt->getOperand(0));
      if (!CPA || !CPA->isKnownCompatibleWith(Key, Disc, DL))
        break;

      // resign(ptrauth(p,ks,ds),ks,ds,kr,dr) -> ptrauth(p,kr,dr)
      if (NeedSign && isa<ConstantInt>(II->getArgOperand(4))) {
        auto *SignKey = cast<ConstantInt>(II->getArgOperand(3));
        auto *SignDisc = cast<ConstantInt>(II->getArgOperand(4));
        auto *SignAddrDisc = ConstantPointerNull::get(Builder.getPtrTy());
        auto *NewCPA = ConstantPtrAuth::get(CPA->getPointer(), SignKey,
                                            SignDisc, SignAddrDisc);
        replaceInstUsesWith(
            *II, ConstantExpr::getPointerCast(NewCPA, II->getType()));
        return eraseInstFromFunction(*II);
      }

      // auth(ptrauth(p,k,d),k,d) -> p
      BasePtr = Builder.CreatePtrToInt(CPA->getPointer(), II->getType());
    } else
      break;

    unsigned NewIntrin;
    if (AuthKey && NeedSign) {
      // resign(0,1) + resign(1,2) = resign(0, 2)
      NewIntrin = Intrinsic::ptrauth_resign;
    } else if (AuthKey) {
      // resign(0,1) + auth(1) = auth(0)
      NewIntrin = Intrinsic::ptrauth_auth;
    } else if (NeedSign) {
      // sign(0) + resign(0, 1) = sign(1)
      NewIntrin = Intrinsic::ptrauth_sign;
    } else {
      // sign(0) + auth(0) = nop
      replaceInstUsesWith(*II, BasePtr);
      return eraseInstFromFunction(*II);
    }

    SmallVector<Value *, 4> CallArgs;
    CallArgs.push_back(BasePtr);
    if (AuthKey) {
      CallArgs.push_back(AuthKey);
      CallArgs.push_back(AuthDisc);
    }

    if (NeedSign) {
      CallArgs.push_back(II->getArgOperand(3));
      CallArgs.push_back(II->getArgOperand(4));
    }

    Function *NewFn = Intrinsic::getDeclaration(II->getModule(), NewIntrin);
    return CallInst::Create(NewFn, CallArgs);
  }
  case Intrinsic::arm_neon_vtbl1:
  case Intrinsic::aarch64_neon_tbl1:
    if (Value *V = simplifyNeonTbl1(*II, Builder))
      return replaceInstUsesWith(*II, V);
    break;

  case Intrinsic::arm_neon_vmulls:
  case Intrinsic::arm_neon_vmullu:
  case Intrinsic::aarch64_neon_smull:
  case Intrinsic::aarch64_neon_umull: {
    Value *Arg0 = II->getArgOperand(0);
    Value *Arg1 = II->getArgOperand(1);

    // Handle mul by zero first:
    if (isa<ConstantAggregateZero>(Arg0) || isa<ConstantAggregateZero>(Arg1)) {
      return replaceInstUsesWith(CI, ConstantAggregateZero::get(II->getType()));
    }

    // Check for constant LHS & RHS - in this case we just simplify.
    bool Zext = (IID == Intrinsic::arm_neon_vmullu ||
                 IID == Intrinsic::aarch64_neon_umull);
    VectorType *NewVT = cast<VectorType>(II->getType());
    if (Constant *CV0 = dyn_cast<Constant>(Arg0)) {
      if (Constant *CV1 = dyn_cast<Constant>(Arg1)) {
        Value *V0 = Builder.CreateIntCast(CV0, NewVT, /*isSigned=*/!Zext);
        Value *V1 = Builder.CreateIntCast(CV1, NewVT, /*isSigned=*/!Zext);
        return replaceInstUsesWith(CI, Builder.CreateMul(V0, V1));
      }

      // Couldn't simplify - canonicalize constant to the RHS.
      std::swap(Arg0, Arg1);
    }

    // Handle mul by one:
    if (Constant *CV1 = dyn_cast<Constant>(Arg1))
      if (ConstantInt *Splat =
              dyn_cast_or_null<ConstantInt>(CV1->getSplatValue()))
        if (Splat->isOne())
          return CastInst::CreateIntegerCast(Arg0, II->getType(),
                                             /*isSigned=*/!Zext);

    break;
  }
  case Intrinsic::arm_neon_aesd:
  case Intrinsic::arm_neon_aese:
  case Intrinsic::aarch64_crypto_aesd:
  case Intrinsic::aarch64_crypto_aese: {
    Value *DataArg = II->getArgOperand(0);
    Value *KeyArg  = II->getArgOperand(1);

    // Try to use the builtin XOR in AESE and AESD to eliminate a prior XOR
    Value *Data, *Key;
    if (match(KeyArg, m_ZeroInt()) &&
        match(DataArg, m_Xor(m_Value(Data), m_Value(Key)))) {
      replaceOperand(*II, 0, Data);
      replaceOperand(*II, 1, Key);
      return II;
    }
    break;
  }
  case Intrinsic::hexagon_V6_vandvrt:
  case Intrinsic::hexagon_V6_vandvrt_128B: {
    // Simplify Q -> V -> Q conversion.
    if (auto Op0 = dyn_cast<IntrinsicInst>(II->getArgOperand(0))) {
      Intrinsic::ID ID0 = Op0->getIntrinsicID();
      if (ID0 != Intrinsic::hexagon_V6_vandqrt &&
          ID0 != Intrinsic::hexagon_V6_vandqrt_128B)
        break;
      Value *Bytes = Op0->getArgOperand(1), *Mask = II->getArgOperand(1);
      uint64_t Bytes1 = computeKnownBits(Bytes, 0, Op0).One.getZExtValue();
      uint64_t Mask1 = computeKnownBits(Mask, 0, II).One.getZExtValue();
      // Check if every byte has common bits in Bytes and Mask.
      uint64_t C = Bytes1 & Mask1;
      if ((C & 0xFF) && (C & 0xFF00) && (C & 0xFF0000) && (C & 0xFF000000))
        return replaceInstUsesWith(*II, Op0->getArgOperand(0));
    }
    break;
  }
  case Intrinsic::stackrestore: {
    enum class ClassifyResult {
      None,
      Alloca,
      StackRestore,
      CallWithSideEffects,
    };
    auto Classify = [](const Instruction *I) {
      if (isa<AllocaInst>(I))
        return ClassifyResult::Alloca;

      if (auto *CI = dyn_cast<CallInst>(I)) {
        if (auto *II = dyn_cast<IntrinsicInst>(CI)) {
          if (II->getIntrinsicID() == Intrinsic::stackrestore)
            return ClassifyResult::StackRestore;

          if (II->mayHaveSideEffects())
            return ClassifyResult::CallWithSideEffects;
        } else {
          // Consider all non-intrinsic calls to be side effects
          return ClassifyResult::CallWithSideEffects;
        }
      }

      return ClassifyResult::None;
    };

    // If the stacksave and the stackrestore are in the same BB, and there is
    // no intervening call, alloca, or stackrestore of a different stacksave,
    // remove the restore. This can happen when variable allocas are DCE'd.
    if (IntrinsicInst *SS = dyn_cast<IntrinsicInst>(II->getArgOperand(0))) {
      if (SS->getIntrinsicID() == Intrinsic::stacksave &&
          SS->getParent() == II->getParent()) {
        BasicBlock::iterator BI(SS);
        bool CannotRemove = false;
        for (++BI; &*BI != II; ++BI) {
          switch (Classify(&*BI)) {
          case ClassifyResult::None:
            // So far so good, look at next instructions.
            break;

          case ClassifyResult::StackRestore:
            // If we found an intervening stackrestore for a different
            // stacksave, we can't remove the stackrestore. Otherwise, continue.
            if (cast<IntrinsicInst>(*BI).getArgOperand(0) != SS)
              CannotRemove = true;
            break;

          case ClassifyResult::Alloca:
          case ClassifyResult::CallWithSideEffects:
            // If we found an alloca, a non-intrinsic call, or an intrinsic
            // call with side effects, we can't remove the stackrestore.
            CannotRemove = true;
            break;
          }
          if (CannotRemove)
            break;
        }

        if (!CannotRemove)
          return eraseInstFromFunction(CI);
      }
    }

    // Scan down this block to see if there is another stack restore in the
    // same block without an intervening call/alloca.
    BasicBlock::iterator BI(II);
    Instruction *TI = II->getParent()->getTerminator();
    bool CannotRemove = false;
    for (++BI; &*BI != TI; ++BI) {
      switch (Classify(&*BI)) {
      case ClassifyResult::None:
        // So far so good, look at next instructions.
        break;

      case ClassifyResult::StackRestore:
        // If there is a stackrestore below this one, remove this one.
        return eraseInstFromFunction(CI);

      case ClassifyResult::Alloca:
      case ClassifyResult::CallWithSideEffects:
        // If we found an alloca, a non-intrinsic call, or an intrinsic call
        // with side effects (such as llvm.stacksave and llvm.read_register),
        // we can't remove the stack restore.
        CannotRemove = true;
        break;
      }
      if (CannotRemove)
        break;
    }

    // If the stack restore is in a return, resume, or unwind block and if there
    // are no allocas or calls between the restore and the return, nuke the
    // restore.
    if (!CannotRemove && (isa<ReturnInst>(TI) || isa<ResumeInst>(TI)))
      return eraseInstFromFunction(CI);
    break;
  }
  case Intrinsic::lifetime_end:
    // Asan needs to poison memory to detect invalid access which is possible
    // even for empty lifetime range.
    if (II->getFunction()->hasFnAttribute(Attribute::SanitizeAddress) ||
        II->getFunction()->hasFnAttribute(Attribute::SanitizeMemory) ||
        II->getFunction()->hasFnAttribute(Attribute::SanitizeHWAddress))
      break;

    if (removeTriviallyEmptyRange(*II, *this, [](const IntrinsicInst &I) {
          return I.getIntrinsicID() == Intrinsic::lifetime_start;
        }))
      return nullptr;
    break;
  case Intrinsic::assume: {
    Value *IIOperand = II->getArgOperand(0);
    SmallVector<OperandBundleDef, 4> OpBundles;
    II->getOperandBundlesAsDefs(OpBundles);

    /// This will remove the boolean Condition from the assume given as
    /// argument and remove the assume if it becomes useless.
    /// always returns nullptr for use as a return values.
    auto RemoveConditionFromAssume = [&](Instruction *Assume) -> Instruction * {
      assert(isa<AssumeInst>(Assume));
      if (isAssumeWithEmptyBundle(*cast<AssumeInst>(II)))
        return eraseInstFromFunction(CI);
      replaceUse(II->getOperandUse(0), ConstantInt::getTrue(II->getContext()));
      return nullptr;
    };
    // Remove an assume if it is followed by an identical assume.
    // TODO: Do we need this? Unless there are conflicting assumptions, the
    // computeKnownBits(IIOperand) below here eliminates redundant assumes.
    Instruction *Next = II->getNextNonDebugInstruction();
    if (match(Next, m_Intrinsic<Intrinsic::assume>(m_Specific(IIOperand))))
      return RemoveConditionFromAssume(Next);

    // Canonicalize assume(a && b) -> assume(a); assume(b);
    // Note: New assumption intrinsics created here are registered by
    // the InstCombineIRInserter object.
    FunctionType *AssumeIntrinsicTy = II->getFunctionType();
    Value *AssumeIntrinsic = II->getCalledOperand();
    Value *A, *B;
    if (match(IIOperand, m_LogicalAnd(m_Value(A), m_Value(B)))) {
      Builder.CreateCall(AssumeIntrinsicTy, AssumeIntrinsic, A, OpBundles,
                         II->getName());
      Builder.CreateCall(AssumeIntrinsicTy, AssumeIntrinsic, B, II->getName());
      return eraseInstFromFunction(*II);
    }
    // assume(!(a || b)) -> assume(!a); assume(!b);
    if (match(IIOperand, m_Not(m_LogicalOr(m_Value(A), m_Value(B))))) {
      Builder.CreateCall(AssumeIntrinsicTy, AssumeIntrinsic,
                         Builder.CreateNot(A), OpBundles, II->getName());
      Builder.CreateCall(AssumeIntrinsicTy, AssumeIntrinsic,
                         Builder.CreateNot(B), II->getName());
      return eraseInstFromFunction(*II);
    }

    // assume( (load addr) != null ) -> add 'nonnull' metadata to load
    // (if assume is valid at the load)
    CmpInst::Predicate Pred;
    Instruction *LHS;
    if (match(IIOperand, m_ICmp(Pred, m_Instruction(LHS), m_Zero())) &&
        Pred == ICmpInst::ICMP_NE && LHS->getOpcode() == Instruction::Load &&
        LHS->getType()->isPointerTy() &&
        isValidAssumeForContext(II, LHS, &DT)) {
      MDNode *MD = MDNode::get(II->getContext(), std::nullopt);
      LHS->setMetadata(LLVMContext::MD_nonnull, MD);
      LHS->setMetadata(LLVMContext::MD_noundef, MD);
      return RemoveConditionFromAssume(II);

      // TODO: apply nonnull return attributes to calls and invokes
      // TODO: apply range metadata for range check patterns?
    }

    // Separate storage assumptions apply to the underlying allocations, not any
    // particular pointer within them. When evaluating the hints for AA purposes
    // we getUnderlyingObject them; by precomputing the answers here we can
    // avoid having to do so repeatedly there.
    for (unsigned Idx = 0; Idx < II->getNumOperandBundles(); Idx++) {
      OperandBundleUse OBU = II->getOperandBundleAt(Idx);
      if (OBU.getTagName() == "separate_storage") {
        assert(OBU.Inputs.size() == 2);
        auto MaybeSimplifyHint = [&](const Use &U) {
          Value *Hint = U.get();
          // Not having a limit is safe because InstCombine removes unreachable
          // code.
          Value *UnderlyingObject = getUnderlyingObject(Hint, /*MaxLookup*/ 0);
          if (Hint != UnderlyingObject)
            replaceUse(const_cast<Use &>(U), UnderlyingObject);
        };
        MaybeSimplifyHint(OBU.Inputs[0]);
        MaybeSimplifyHint(OBU.Inputs[1]);
      }
    }

    // Convert nonnull assume like:
    // %A = icmp ne i32* %PTR, null
    // call void @llvm.assume(i1 %A)
    // into
    // call void @llvm.assume(i1 true) [ "nonnull"(i32* %PTR) ]
    if (EnableKnowledgeRetention &&
        match(IIOperand, m_Cmp(Pred, m_Value(A), m_Zero())) &&
        Pred == CmpInst::ICMP_NE && A->getType()->isPointerTy()) {
      if (auto *Replacement = buildAssumeFromKnowledge(
              {RetainedKnowledge{Attribute::NonNull, 0, A}}, Next, &AC, &DT)) {

        Replacement->insertBefore(Next);
        AC.registerAssumption(Replacement);
        return RemoveConditionFromAssume(II);
      }
    }

    // Convert alignment assume like:
    // %B = ptrtoint i32* %A to i64
    // %C = and i64 %B, Constant
    // %D = icmp eq i64 %C, 0
    // call void @llvm.assume(i1 %D)
    // into
    // call void @llvm.assume(i1 true) [ "align"(i32* [[A]], i64  Constant + 1)]
    uint64_t AlignMask;
    if (EnableKnowledgeRetention &&
        match(IIOperand,
              m_Cmp(Pred, m_And(m_Value(A), m_ConstantInt(AlignMask)),
                    m_Zero())) &&
        Pred == CmpInst::ICMP_EQ) {
      if (isPowerOf2_64(AlignMask + 1)) {
        uint64_t Offset = 0;
        match(A, m_Add(m_Value(A), m_ConstantInt(Offset)));
        if (match(A, m_PtrToInt(m_Value(A)))) {
          /// Note: this doesn't preserve the offset information but merges
          /// offset and alignment.
          /// TODO: we can generate a GEP instead of merging the alignment with
          /// the offset.
          RetainedKnowledge RK{Attribute::Alignment,
                               (unsigned)MinAlign(Offset, AlignMask + 1), A};
          if (auto *Replacement =
                  buildAssumeFromKnowledge(RK, Next, &AC, &DT)) {

            Replacement->insertAfter(II);
            AC.registerAssumption(Replacement);
          }
          return RemoveConditionFromAssume(II);
        }
      }
    }

    /// Canonicalize Knowledge in operand bundles.
    if (EnableKnowledgeRetention && II->hasOperandBundles()) {
      for (unsigned Idx = 0; Idx < II->getNumOperandBundles(); Idx++) {
        auto &BOI = II->bundle_op_info_begin()[Idx];
        RetainedKnowledge RK =
          llvm::getKnowledgeFromBundle(cast<AssumeInst>(*II), BOI);
        if (BOI.End - BOI.Begin > 2)
          continue; // Prevent reducing knowledge in an align with offset since
                    // extracting a RetainedKnowledge from them looses offset
                    // information
        RetainedKnowledge CanonRK =
          llvm::simplifyRetainedKnowledge(cast<AssumeInst>(II), RK,
                                          &getAssumptionCache(),
                                          &getDominatorTree());
        if (CanonRK == RK)
          continue;
        if (!CanonRK) {
          if (BOI.End - BOI.Begin > 0) {
            Worklist.pushValue(II->op_begin()[BOI.Begin]);
            Value::dropDroppableUse(II->op_begin()[BOI.Begin]);
          }
          continue;
        }
        assert(RK.AttrKind == CanonRK.AttrKind);
        if (BOI.End - BOI.Begin > 0)
          II->op_begin()[BOI.Begin].set(CanonRK.WasOn);
        if (BOI.End - BOI.Begin > 1)
          II->op_begin()[BOI.Begin + 1].set(ConstantInt::get(
              Type::getInt64Ty(II->getContext()), CanonRK.ArgValue));
        if (RK.WasOn)
          Worklist.pushValue(RK.WasOn);
        return II;
      }
    }

    // If there is a dominating assume with the same condition as this one,
    // then this one is redundant, and should be removed.
    KnownBits Known(1);
    computeKnownBits(IIOperand, Known, 0, II);
    if (Known.isAllOnes() && isAssumeWithEmptyBundle(cast<AssumeInst>(*II)))
      return eraseInstFromFunction(*II);

    // assume(false) is unreachable.
    if (match(IIOperand, m_CombineOr(m_Zero(), m_Undef()))) {
      CreateNonTerminatorUnreachable(II);
      return eraseInstFromFunction(*II);
    }

    // Update the cache of affected values for this assumption (we might be
    // here because we just simplified the condition).
    AC.updateAffectedValues(cast<AssumeInst>(II));
    break;
  }
  case Intrinsic::experimental_guard: {
    // Is this guard followed by another guard?  We scan forward over a small
    // fixed window of instructions to handle common cases with conditions
    // computed between guards.
    Instruction *NextInst = II->getNextNonDebugInstruction();
    for (unsigned i = 0; i < GuardWideningWindow; i++) {
      // Note: Using context-free form to avoid compile time blow up
      if (!isSafeToSpeculativelyExecute(NextInst))
        break;
      NextInst = NextInst->getNextNonDebugInstruction();
    }
    Value *NextCond = nullptr;
    if (match(NextInst,
              m_Intrinsic<Intrinsic::experimental_guard>(m_Value(NextCond)))) {
      Value *CurrCond = II->getArgOperand(0);

      // Remove a guard that it is immediately preceded by an identical guard.
      // Otherwise canonicalize guard(a); guard(b) -> guard(a & b).
      if (CurrCond != NextCond) {
        Instruction *MoveI = II->getNextNonDebugInstruction();
        while (MoveI != NextInst) {
          auto *Temp = MoveI;
          MoveI = MoveI->getNextNonDebugInstruction();
          Temp->moveBefore(II);
        }
        replaceOperand(*II, 0, Builder.CreateAnd(CurrCond, NextCond));
      }
      eraseInstFromFunction(*NextInst);
      return II;
    }
    break;
  }
  case Intrinsic::vector_insert: {
    Value *Vec = II->getArgOperand(0);
    Value *SubVec = II->getArgOperand(1);
    Value *Idx = II->getArgOperand(2);
    auto *DstTy = dyn_cast<FixedVectorType>(II->getType());
    auto *VecTy = dyn_cast<FixedVectorType>(Vec->getType());
    auto *SubVecTy = dyn_cast<FixedVectorType>(SubVec->getType());

    // Only canonicalize if the destination vector, Vec, and SubVec are all
    // fixed vectors.
    if (DstTy && VecTy && SubVecTy) {
      unsigned DstNumElts = DstTy->getNumElements();
      unsigned VecNumElts = VecTy->getNumElements();
      unsigned SubVecNumElts = SubVecTy->getNumElements();
      unsigned IdxN = cast<ConstantInt>(Idx)->getZExtValue();

      // An insert that entirely overwrites Vec with SubVec is a nop.
      if (VecNumElts == SubVecNumElts)
        return replaceInstUsesWith(CI, SubVec);

      // Widen SubVec into a vector of the same width as Vec, since
      // shufflevector requires the two input vectors to be the same width.
      // Elements beyond the bounds of SubVec within the widened vector are
      // undefined.
      SmallVector<int, 8> WidenMask;
      unsigned i;
      for (i = 0; i != SubVecNumElts; ++i)
        WidenMask.push_back(i);
      for (; i != VecNumElts; ++i)
        WidenMask.push_back(PoisonMaskElem);

      Value *WidenShuffle = Builder.CreateShuffleVector(SubVec, WidenMask);

      SmallVector<int, 8> Mask;
      for (unsigned i = 0; i != IdxN; ++i)
        Mask.push_back(i);
      for (unsigned i = DstNumElts; i != DstNumElts + SubVecNumElts; ++i)
        Mask.push_back(i);
      for (unsigned i = IdxN + SubVecNumElts; i != DstNumElts; ++i)
        Mask.push_back(i);

      Value *Shuffle = Builder.CreateShuffleVector(Vec, WidenShuffle, Mask);
      return replaceInstUsesWith(CI, Shuffle);
    }
    break;
  }
  case Intrinsic::vector_extract: {
    Value *Vec = II->getArgOperand(0);
    Value *Idx = II->getArgOperand(1);

    Type *ReturnType = II->getType();
    // (extract_vector (insert_vector InsertTuple, InsertValue, InsertIdx),
    // ExtractIdx)
    unsigned ExtractIdx = cast<ConstantInt>(Idx)->getZExtValue();
    Value *InsertTuple, *InsertIdx, *InsertValue;
    if (match(Vec, m_Intrinsic<Intrinsic::vector_insert>(m_Value(InsertTuple),
                                                         m_Value(InsertValue),
                                                         m_Value(InsertIdx))) &&
        InsertValue->getType() == ReturnType) {
      unsigned Index = cast<ConstantInt>(InsertIdx)->getZExtValue();
      // Case where we get the same index right after setting it.
      // extract.vector(insert.vector(InsertTuple, InsertValue, Idx), Idx) -->
      // InsertValue
      if (ExtractIdx == Index)
        return replaceInstUsesWith(CI, InsertValue);
      // If we are getting a different index than what was set in the
      // insert.vector intrinsic. We can just set the input tuple to the one up
      // in the chain. extract.vector(insert.vector(InsertTuple, InsertValue,
      // InsertIndex), ExtractIndex)
      // --> extract.vector(InsertTuple, ExtractIndex)
      else
        return replaceOperand(CI, 0, InsertTuple);
    }

    auto *DstTy = dyn_cast<VectorType>(ReturnType);
    auto *VecTy = dyn_cast<VectorType>(Vec->getType());

    if (DstTy && VecTy) {
      auto DstEltCnt = DstTy->getElementCount();
      auto VecEltCnt = VecTy->getElementCount();
      unsigned IdxN = cast<ConstantInt>(Idx)->getZExtValue();

      // Extracting the entirety of Vec is a nop.
      if (DstEltCnt == VecTy->getElementCount()) {
        replaceInstUsesWith(CI, Vec);
        return eraseInstFromFunction(CI);
      }

      // Only canonicalize to shufflevector if the destination vector and
      // Vec are fixed vectors.
      if (VecEltCnt.isScalable() || DstEltCnt.isScalable())
        break;

      SmallVector<int, 8> Mask;
      for (unsigned i = 0; i != DstEltCnt.getKnownMinValue(); ++i)
        Mask.push_back(IdxN + i);

      Value *Shuffle = Builder.CreateShuffleVector(Vec, Mask);
      return replaceInstUsesWith(CI, Shuffle);
    }
    break;
  }
  case Intrinsic::vector_reverse: {
    Value *BO0, *BO1, *X, *Y;
    Value *Vec = II->getArgOperand(0);
    if (match(Vec, m_OneUse(m_BinOp(m_Value(BO0), m_Value(BO1))))) {
      auto *OldBinOp = cast<BinaryOperator>(Vec);
      if (match(BO0, m_VecReverse(m_Value(X)))) {
        // rev(binop rev(X), rev(Y)) --> binop X, Y
        if (match(BO1, m_VecReverse(m_Value(Y))))
          return replaceInstUsesWith(CI, BinaryOperator::CreateWithCopiedFlags(
                                             OldBinOp->getOpcode(), X, Y,
                                             OldBinOp, OldBinOp->getName(),
                                             II->getIterator()));
        // rev(binop rev(X), BO1Splat) --> binop X, BO1Splat
        if (isSplatValue(BO1))
          return replaceInstUsesWith(CI, BinaryOperator::CreateWithCopiedFlags(
                                             OldBinOp->getOpcode(), X, BO1,
                                             OldBinOp, OldBinOp->getName(),
                                             II->getIterator()));
      }
      // rev(binop BO0Splat, rev(Y)) --> binop BO0Splat, Y
      if (match(BO1, m_VecReverse(m_Value(Y))) && isSplatValue(BO0))
        return replaceInstUsesWith(CI,
                                   BinaryOperator::CreateWithCopiedFlags(
                                       OldBinOp->getOpcode(), BO0, Y, OldBinOp,
                                       OldBinOp->getName(), II->getIterator()));
    }
    // rev(unop rev(X)) --> unop X
    if (match(Vec, m_OneUse(m_UnOp(m_VecReverse(m_Value(X)))))) {
      auto *OldUnOp = cast<UnaryOperator>(Vec);
      auto *NewUnOp = UnaryOperator::CreateWithCopiedFlags(
          OldUnOp->getOpcode(), X, OldUnOp, OldUnOp->getName(),
          II->getIterator());
      return replaceInstUsesWith(CI, NewUnOp);
    }
    break;
  }
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_and: {
    // Canonicalize logical or/and reductions:
    // Or reduction for i1 is represented as:
    // %val = bitcast <ReduxWidth x i1> to iReduxWidth
    // %res = cmp ne iReduxWidth %val, 0
    // And reduction for i1 is represented as:
    // %val = bitcast <ReduxWidth x i1> to iReduxWidth
    // %res = cmp eq iReduxWidth %val, 11111
    Value *Arg = II->getArgOperand(0);
    Value *Vect;

    if (Value *NewOp =
            simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
      replaceUse(II->getOperandUse(0), NewOp);
      return II;
    }

    if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
      if (auto *FTy = dyn_cast<FixedVectorType>(Vect->getType()))
        if (FTy->getElementType() == Builder.getInt1Ty()) {
          Value *Res = Builder.CreateBitCast(
              Vect, Builder.getIntNTy(FTy->getNumElements()));
          if (IID == Intrinsic::vector_reduce_and) {
            Res = Builder.CreateICmpEQ(
                Res, ConstantInt::getAllOnesValue(Res->getType()));
          } else {
            assert(IID == Intrinsic::vector_reduce_or &&
                   "Expected or reduction.");
            Res = Builder.CreateIsNotNull(Res);
          }
          if (Arg != Vect)
            Res = Builder.CreateCast(cast<CastInst>(Arg)->getOpcode(), Res,
                                     II->getType());
          return replaceInstUsesWith(CI, Res);
        }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_add: {
    if (IID == Intrinsic::vector_reduce_add) {
      // Convert vector_reduce_add(ZExt(<n x i1>)) to
      // ZExtOrTrunc(ctpop(bitcast <n x i1> to in)).
      // Convert vector_reduce_add(SExt(<n x i1>)) to
      // -ZExtOrTrunc(ctpop(bitcast <n x i1> to in)).
      // Convert vector_reduce_add(<n x i1>) to
      // Trunc(ctpop(bitcast <n x i1> to in)).
      Value *Arg = II->getArgOperand(0);
      Value *Vect;

      if (Value *NewOp =
              simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
        replaceUse(II->getOperandUse(0), NewOp);
        return II;
      }

      if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
        if (auto *FTy = dyn_cast<FixedVectorType>(Vect->getType()))
          if (FTy->getElementType() == Builder.getInt1Ty()) {
            Value *V = Builder.CreateBitCast(
                Vect, Builder.getIntNTy(FTy->getNumElements()));
            Value *Res = Builder.CreateUnaryIntrinsic(Intrinsic::ctpop, V);
            if (Res->getType() != II->getType())
              Res = Builder.CreateZExtOrTrunc(Res, II->getType());
            if (Arg != Vect &&
                cast<Instruction>(Arg)->getOpcode() == Instruction::SExt)
              Res = Builder.CreateNeg(Res);
            return replaceInstUsesWith(CI, Res);
          }
      }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_xor: {
    if (IID == Intrinsic::vector_reduce_xor) {
      // Exclusive disjunction reduction over the vector with
      // (potentially-extended) i1 element type is actually a
      // (potentially-extended) arithmetic `add` reduction over the original
      // non-extended value:
      //   vector_reduce_xor(?ext(<n x i1>))
      //     -->
      //   ?ext(vector_reduce_add(<n x i1>))
      Value *Arg = II->getArgOperand(0);
      Value *Vect;

      if (Value *NewOp =
              simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
        replaceUse(II->getOperandUse(0), NewOp);
        return II;
      }

      if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
        if (auto *VTy = dyn_cast<VectorType>(Vect->getType()))
          if (VTy->getElementType() == Builder.getInt1Ty()) {
            Value *Res = Builder.CreateAddReduce(Vect);
            if (Arg != Vect)
              Res = Builder.CreateCast(cast<CastInst>(Arg)->getOpcode(), Res,
                                       II->getType());
            return replaceInstUsesWith(CI, Res);
          }
      }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_mul: {
    if (IID == Intrinsic::vector_reduce_mul) {
      // Multiplicative reduction over the vector with (potentially-extended)
      // i1 element type is actually a (potentially zero-extended)
      // logical `and` reduction over the original non-extended value:
      //   vector_reduce_mul(?ext(<n x i1>))
      //     -->
      //   zext(vector_reduce_and(<n x i1>))
      Value *Arg = II->getArgOperand(0);
      Value *Vect;

      if (Value *NewOp =
              simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
        replaceUse(II->getOperandUse(0), NewOp);
        return II;
      }

      if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
        if (auto *VTy = dyn_cast<VectorType>(Vect->getType()))
          if (VTy->getElementType() == Builder.getInt1Ty()) {
            Value *Res = Builder.CreateAndReduce(Vect);
            if (Res->getType() != II->getType())
              Res = Builder.CreateZExt(Res, II->getType());
            return replaceInstUsesWith(CI, Res);
          }
      }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_umin:
  case Intrinsic::vector_reduce_umax: {
    if (IID == Intrinsic::vector_reduce_umin ||
        IID == Intrinsic::vector_reduce_umax) {
      // UMin/UMax reduction over the vector with (potentially-extended)
      // i1 element type is actually a (potentially-extended)
      // logical `and`/`or` reduction over the original non-extended value:
      //   vector_reduce_u{min,max}(?ext(<n x i1>))
      //     -->
      //   ?ext(vector_reduce_{and,or}(<n x i1>))
      Value *Arg = II->getArgOperand(0);
      Value *Vect;

      if (Value *NewOp =
              simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
        replaceUse(II->getOperandUse(0), NewOp);
        return II;
      }

      if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
        if (auto *VTy = dyn_cast<VectorType>(Vect->getType()))
          if (VTy->getElementType() == Builder.getInt1Ty()) {
            Value *Res = IID == Intrinsic::vector_reduce_umin
                             ? Builder.CreateAndReduce(Vect)
                             : Builder.CreateOrReduce(Vect);
            if (Arg != Vect)
              Res = Builder.CreateCast(cast<CastInst>(Arg)->getOpcode(), Res,
                                       II->getType());
            return replaceInstUsesWith(CI, Res);
          }
      }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_smax: {
    if (IID == Intrinsic::vector_reduce_smin ||
        IID == Intrinsic::vector_reduce_smax) {
      // SMin/SMax reduction over the vector with (potentially-extended)
      // i1 element type is actually a (potentially-extended)
      // logical `and`/`or` reduction over the original non-extended value:
      //   vector_reduce_s{min,max}(<n x i1>)
      //     -->
      //   vector_reduce_{or,and}(<n x i1>)
      // and
      //   vector_reduce_s{min,max}(sext(<n x i1>))
      //     -->
      //   sext(vector_reduce_{or,and}(<n x i1>))
      // and
      //   vector_reduce_s{min,max}(zext(<n x i1>))
      //     -->
      //   zext(vector_reduce_{and,or}(<n x i1>))
      Value *Arg = II->getArgOperand(0);
      Value *Vect;

      if (Value *NewOp =
              simplifyReductionOperand(Arg, /*CanReorderLanes=*/true)) {
        replaceUse(II->getOperandUse(0), NewOp);
        return II;
      }

      if (match(Arg, m_ZExtOrSExtOrSelf(m_Value(Vect)))) {
        if (auto *VTy = dyn_cast<VectorType>(Vect->getType()))
          if (VTy->getElementType() == Builder.getInt1Ty()) {
            Instruction::CastOps ExtOpc = Instruction::CastOps::CastOpsEnd;
            if (Arg != Vect)
              ExtOpc = cast<CastInst>(Arg)->getOpcode();
            Value *Res = ((IID == Intrinsic::vector_reduce_smin) ==
                          (ExtOpc == Instruction::CastOps::ZExt))
                             ? Builder.CreateAndReduce(Vect)
                             : Builder.CreateOrReduce(Vect);
            if (Arg != Vect)
              Res = Builder.CreateCast(ExtOpc, Res, II->getType());
            return replaceInstUsesWith(CI, Res);
          }
      }
    }
    [[fallthrough]];
  }
  case Intrinsic::vector_reduce_fmax:
  case Intrinsic::vector_reduce_fmin:
  case Intrinsic::vector_reduce_fadd:
  case Intrinsic::vector_reduce_fmul: {
    bool CanReorderLanes = (IID != Intrinsic::vector_reduce_fadd &&
                            IID != Intrinsic::vector_reduce_fmul) ||
                           II->hasAllowReassoc();
    const unsigned ArgIdx = (IID == Intrinsic::vector_reduce_fadd ||
                             IID == Intrinsic::vector_reduce_fmul)
                                ? 1
                                : 0;
    Value *Arg = II->getArgOperand(ArgIdx);
    if (Value *NewOp = simplifyReductionOperand(Arg, CanReorderLanes)) {
      replaceUse(II->getOperandUse(ArgIdx), NewOp);
      return nullptr;
    }
    break;
  }
  case Intrinsic::is_fpclass: {
    if (Instruction *I = foldIntrinsicIsFPClass(*II))
      return I;
    break;
  }
  case Intrinsic::threadlocal_address: {
    Align MinAlign = getKnownAlignment(II->getArgOperand(0), DL, II, &AC, &DT);
    MaybeAlign Align = II->getRetAlign();
    if (MinAlign > Align.valueOrOne()) {
      II->addRetAttr(Attribute::getWithAlignment(II->getContext(), MinAlign));
      return II;
    }
    break;
  }
  default: {
    // Handle target specific intrinsics
    std::optional<Instruction *> V = targetInstCombineIntrinsic(*II);
    if (V)
      return *V;
    break;
  }
  }

  // Try to fold intrinsic into select operands. This is legal if:
  //  * The intrinsic is speculatable.
  //  * The select condition is not a vector, or the intrinsic does not
  //    perform cross-lane operations.
  switch (IID) {
  case Intrinsic::ctlz:
  case Intrinsic::cttz:
  case Intrinsic::ctpop:
  case Intrinsic::umin:
  case Intrinsic::umax:
  case Intrinsic::smin:
  case Intrinsic::smax:
  case Intrinsic::usub_sat:
  case Intrinsic::uadd_sat:
  case Intrinsic::ssub_sat:
  case Intrinsic::sadd_sat:
    for (Value *Op : II->args())
      if (auto *Sel = dyn_cast<SelectInst>(Op))
        if (Instruction *R = FoldOpIntoSelect(*II, Sel))
          return R;
    [[fallthrough]];
  default:
    break;
  }

  if (Instruction *Shuf = foldShuffledIntrinsicOperands(II, Builder))
    return Shuf;

  // Some intrinsics (like experimental_gc_statepoint) can be used in invoke
  // context, so it is handled in visitCallBase and we should trigger it.
  return visitCallBase(*II);
}

// Fence instruction simplification
Instruction *InstCombinerImpl::visitFenceInst(FenceInst &FI) {
  auto *NFI = dyn_cast<FenceInst>(FI.getNextNonDebugInstruction());
  // This check is solely here to handle arbitrary target-dependent syncscopes.
  // TODO: Can remove if does not matter in practice.
  if (NFI && FI.isIdenticalTo(NFI))
    return eraseInstFromFunction(FI);

  // Returns true if FI1 is identical or stronger fence than FI2.
  auto isIdenticalOrStrongerFence = [](FenceInst *FI1, FenceInst *FI2) {
    auto FI1SyncScope = FI1->getSyncScopeID();
    // Consider same scope, where scope is global or single-thread.
    if (FI1SyncScope != FI2->getSyncScopeID() ||
        (FI1SyncScope != SyncScope::System &&
         FI1SyncScope != SyncScope::SingleThread))
      return false;

    return isAtLeastOrStrongerThan(FI1->getOrdering(), FI2->getOrdering());
  };
  if (NFI && isIdenticalOrStrongerFence(NFI, &FI))
    return eraseInstFromFunction(FI);

  if (auto *PFI = dyn_cast_or_null<FenceInst>(FI.getPrevNonDebugInstruction()))
    if (isIdenticalOrStrongerFence(PFI, &FI))
      return eraseInstFromFunction(FI);
  return nullptr;
}

// InvokeInst simplification
Instruction *InstCombinerImpl::visitInvokeInst(InvokeInst &II) {
  return visitCallBase(II);
}

// CallBrInst simplification
Instruction *InstCombinerImpl::visitCallBrInst(CallBrInst &CBI) {
  return visitCallBase(CBI);
}

Instruction *InstCombinerImpl::tryOptimizeCall(CallInst *CI) {
  if (!CI->getCalledFunction()) return nullptr;

  // Skip optimizing notail and musttail calls so
  // LibCallSimplifier::optimizeCall doesn't have to preserve those invariants.
  // LibCallSimplifier::optimizeCall should try to preseve tail calls though.
  if (CI->isMustTailCall() || CI->isNoTailCall())
    return nullptr;

  auto InstCombineRAUW = [this](Instruction *From, Value *With) {
    replaceInstUsesWith(*From, With);
  };
  auto InstCombineErase = [this](Instruction *I) {
    eraseInstFromFunction(*I);
  };
  LibCallSimplifier Simplifier(DL, &TLI, &AC, ORE, BFI, PSI, InstCombineRAUW,
                               InstCombineErase);
  if (Value *With = Simplifier.optimizeCall(CI, Builder)) {
    ++NumSimplified;
    return CI->use_empty() ? CI : replaceInstUsesWith(*CI, With);
  }

  return nullptr;
}

static IntrinsicInst *findInitTrampolineFromAlloca(Value *TrampMem) {
  // Strip off at most one level of pointer casts, looking for an alloca.  This
  // is good enough in practice and simpler than handling any number of casts.
  Value *Underlying = TrampMem->stripPointerCasts();
  if (Underlying != TrampMem &&
      (!Underlying->hasOneUse() || Underlying->user_back() != TrampMem))
    return nullptr;
  if (!isa<AllocaInst>(Underlying))
    return nullptr;

  IntrinsicInst *InitTrampoline = nullptr;
  for (User *U : TrampMem->users()) {
    IntrinsicInst *II = dyn_cast<IntrinsicInst>(U);
    if (!II)
      return nullptr;
    if (II->getIntrinsicID() == Intrinsic::init_trampoline) {
      if (InitTrampoline)
        // More than one init_trampoline writes to this value.  Give up.
        return nullptr;
      InitTrampoline = II;
      continue;
    }
    if (II->getIntrinsicID() == Intrinsic::adjust_trampoline)
      // Allow any number of calls to adjust.trampoline.
      continue;
    return nullptr;
  }

  // No call to init.trampoline found.
  if (!InitTrampoline)
    return nullptr;

  // Check that the alloca is being used in the expected way.
  if (InitTrampoline->getOperand(0) != TrampMem)
    return nullptr;

  return InitTrampoline;
}

static IntrinsicInst *findInitTrampolineFromBB(IntrinsicInst *AdjustTramp,
                                               Value *TrampMem) {
  // Visit all the previous instructions in the basic block, and try to find a
  // init.trampoline which has a direct path to the adjust.trampoline.
  for (BasicBlock::iterator I = AdjustTramp->getIterator(),
                            E = AdjustTramp->getParent()->begin();
       I != E;) {
    Instruction *Inst = &*--I;
    if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(I))
      if (II->getIntrinsicID() == Intrinsic::init_trampoline &&
          II->getOperand(0) == TrampMem)
        return II;
    if (Inst->mayWriteToMemory())
      return nullptr;
  }
  return nullptr;
}

// Given a call to llvm.adjust.trampoline, find and return the corresponding
// call to llvm.init.trampoline if the call to the trampoline can be optimized
// to a direct call to a function.  Otherwise return NULL.
static IntrinsicInst *findInitTrampoline(Value *Callee) {
  Callee = Callee->stripPointerCasts();
  IntrinsicInst *AdjustTramp = dyn_cast<IntrinsicInst>(Callee);
  if (!AdjustTramp ||
      AdjustTramp->getIntrinsicID() != Intrinsic::adjust_trampoline)
    return nullptr;

  Value *TrampMem = AdjustTramp->getOperand(0);

  if (IntrinsicInst *IT = findInitTrampolineFromAlloca(TrampMem))
    return IT;
  if (IntrinsicInst *IT = findInitTrampolineFromBB(AdjustTramp, TrampMem))
    return IT;
  return nullptr;
}

bool InstCombinerImpl::annotateAnyAllocSite(CallBase &Call,
                                            const TargetLibraryInfo *TLI) {
  // Note: We only handle cases which can't be driven from generic attributes
  // here.  So, for example, nonnull and noalias (which are common properties
  // of some allocation functions) are expected to be handled via annotation
  // of the respective allocator declaration with generic attributes.
  bool Changed = false;

  if (!Call.getType()->isPointerTy())
    return Changed;

  std::optional<APInt> Size = getAllocSize(&Call, TLI);
  if (Size && *Size != 0) {
    // TODO: We really should just emit deref_or_null here and then
    // let the generic inference code combine that with nonnull.
    if (Call.hasRetAttr(Attribute::NonNull)) {
      Changed = !Call.hasRetAttr(Attribute::Dereferenceable);
      Call.addRetAttr(Attribute::getWithDereferenceableBytes(
          Call.getContext(), Size->getLimitedValue()));
    } else {
      Changed = !Call.hasRetAttr(Attribute::DereferenceableOrNull);
      Call.addRetAttr(Attribute::getWithDereferenceableOrNullBytes(
          Call.getContext(), Size->getLimitedValue()));
    }
  }

  // Add alignment attribute if alignment is a power of two constant.
  Value *Alignment = getAllocAlignment(&Call, TLI);
  if (!Alignment)
    return Changed;

  ConstantInt *AlignOpC = dyn_cast<ConstantInt>(Alignment);
  if (AlignOpC && AlignOpC->getValue().ult(llvm::Value::MaximumAlignment)) {
    uint64_t AlignmentVal = AlignOpC->getZExtValue();
    if (llvm::isPowerOf2_64(AlignmentVal)) {
      Align ExistingAlign = Call.getRetAlign().valueOrOne();
      Align NewAlign = Align(AlignmentVal);
      if (NewAlign > ExistingAlign) {
        Call.addRetAttr(
            Attribute::getWithAlignment(Call.getContext(), NewAlign));
        Changed = true;
      }
    }
  }
  return Changed;
}

/// Improvements for call, callbr and invoke instructions.
Instruction *InstCombinerImpl::visitCallBase(CallBase &Call) {
  bool Changed = annotateAnyAllocSite(Call, &TLI);

  // Mark any parameters that are known to be non-null with the nonnull
  // attribute.  This is helpful for inlining calls to functions with null
  // checks on their arguments.
  SmallVector<unsigned, 4> ArgNos;
  unsigned ArgNo = 0;

  for (Value *V : Call.args()) {
    if (V->getType()->isPointerTy() &&
        !Call.paramHasAttr(ArgNo, Attribute::NonNull) &&
        isKnownNonZero(V, getSimplifyQuery().getWithInstruction(&Call)))
      ArgNos.push_back(ArgNo);
    ArgNo++;
  }

  assert(ArgNo == Call.arg_size() && "Call arguments not processed correctly.");

  if (!ArgNos.empty()) {
    AttributeList AS = Call.getAttributes();
    LLVMContext &Ctx = Call.getContext();
    AS = AS.addParamAttribute(Ctx, ArgNos,
                              Attribute::get(Ctx, Attribute::NonNull));
    Call.setAttributes(AS);
    Changed = true;
  }

  // If the callee is a pointer to a function, attempt to move any casts to the
  // arguments of the call/callbr/invoke.
  Value *Callee = Call.getCalledOperand();
  Function *CalleeF = dyn_cast<Function>(Callee);
  if ((!CalleeF || CalleeF->getFunctionType() != Call.getFunctionType()) &&
      transformConstExprCastCall(Call))
    return nullptr;

  if (CalleeF) {
    // Remove the convergent attr on calls when the callee is not convergent.
    if (Call.isConvergent() && !CalleeF->isConvergent() &&
        !CalleeF->isIntrinsic()) {
      LLVM_DEBUG(dbgs() << "Removing convergent attr from instr " << Call
                        << "\n");
      Call.setNotConvergent();
      return &Call;
    }

    // If the call and callee calling conventions don't match, and neither one
    // of the calling conventions is compatible with C calling convention
    // this call must be unreachable, as the call is undefined.
    if ((CalleeF->getCallingConv() != Call.getCallingConv() &&
         !(CalleeF->getCallingConv() == llvm::CallingConv::C &&
           TargetLibraryInfoImpl::isCallingConvCCompatible(&Call)) &&
         !(Call.getCallingConv() == llvm::CallingConv::C &&
           TargetLibraryInfoImpl::isCallingConvCCompatible(CalleeF))) &&
        // Only do this for calls to a function with a body.  A prototype may
        // not actually end up matching the implementation's calling conv for a
        // variety of reasons (e.g. it may be written in assembly).
        !CalleeF->isDeclaration()) {
      Instruction *OldCall = &Call;
      CreateNonTerminatorUnreachable(OldCall);
      // If OldCall does not return void then replaceInstUsesWith poison.
      // This allows ValueHandlers and custom metadata to adjust itself.
      if (!OldCall->getType()->isVoidTy())
        replaceInstUsesWith(*OldCall, PoisonValue::get(OldCall->getType()));
      if (isa<CallInst>(OldCall))
        return eraseInstFromFunction(*OldCall);

      // We cannot remove an invoke or a callbr, because it would change thexi
      // CFG, just change the callee to a null pointer.
      cast<CallBase>(OldCall)->setCalledFunction(
          CalleeF->getFunctionType(),
          Constant::getNullValue(CalleeF->getType()));
      return nullptr;
    }
  }

  // Calling a null function pointer is undefined if a null address isn't
  // dereferenceable.
  if ((isa<ConstantPointerNull>(Callee) &&
       !NullPointerIsDefined(Call.getFunction())) ||
      isa<UndefValue>(Callee)) {
    // If Call does not return void then replaceInstUsesWith poison.
    // This allows ValueHandlers and custom metadata to adjust itself.
    if (!Call.getType()->isVoidTy())
      replaceInstUsesWith(Call, PoisonValue::get(Call.getType()));

    if (Call.isTerminator()) {
      // Can't remove an invoke or callbr because we cannot change the CFG.
      return nullptr;
    }

    // This instruction is not reachable, just remove it.
    CreateNonTerminatorUnreachable(&Call);
    return eraseInstFromFunction(Call);
  }

  if (IntrinsicInst *II = findInitTrampoline(Callee))
    return transformCallThroughTrampoline(Call, *II);

  if (isa<InlineAsm>(Callee) && !Call.doesNotThrow()) {
    InlineAsm *IA = cast<InlineAsm>(Callee);
    if (!IA->canThrow()) {
      // Normal inline asm calls cannot throw - mark them
      // 'nounwind'.
      Call.setDoesNotThrow();
      Changed = true;
    }
  }

  // Try to optimize the call if possible, we require DataLayout for most of
  // this.  None of these calls are seen as possibly dead so go ahead and
  // delete the instruction now.
  if (CallInst *CI = dyn_cast<CallInst>(&Call)) {
    Instruction *I = tryOptimizeCall(CI);
    // If we changed something return the result, etc. Otherwise let
    // the fallthrough check.
    if (I) return eraseInstFromFunction(*I);
  }

  if (!Call.use_empty() && !Call.isMustTailCall())
    if (Value *ReturnedArg = Call.getReturnedArgOperand()) {
      Type *CallTy = Call.getType();
      Type *RetArgTy = ReturnedArg->getType();
      if (RetArgTy->canLosslesslyBitCastTo(CallTy))
        return replaceInstUsesWith(
            Call, Builder.CreateBitOrPointerCast(ReturnedArg, CallTy));
    }

  // Drop unnecessary kcfi operand bundles from calls that were converted
  // into direct calls.
  auto Bundle = Call.getOperandBundle(LLVMContext::OB_kcfi);
  if (Bundle && !Call.isIndirectCall()) {
    DEBUG_WITH_TYPE(DEBUG_TYPE "-kcfi", {
      if (CalleeF) {
        ConstantInt *FunctionType = nullptr;
        ConstantInt *ExpectedType = cast<ConstantInt>(Bundle->Inputs[0]);

        if (MDNode *MD = CalleeF->getMetadata(LLVMContext::MD_kcfi_type))
          FunctionType = mdconst::extract<ConstantInt>(MD->getOperand(0));

        if (FunctionType &&
            FunctionType->getZExtValue() != ExpectedType->getZExtValue())
          dbgs() << Call.getModule()->getName()
                 << ": warning: kcfi: " << Call.getCaller()->getName()
                 << ": call to " << CalleeF->getName()
                 << " using a mismatching function pointer type\n";
      }
    });

    return CallBase::removeOperandBundle(&Call, LLVMContext::OB_kcfi);
  }

  if (isRemovableAlloc(&Call, &TLI))
    return visitAllocSite(Call);

  // Handle intrinsics which can be used in both call and invoke context.
  switch (Call.getIntrinsicID()) {
  case Intrinsic::experimental_gc_statepoint: {
    GCStatepointInst &GCSP = *cast<GCStatepointInst>(&Call);
    SmallPtrSet<Value *, 32> LiveGcValues;
    for (const GCRelocateInst *Reloc : GCSP.getGCRelocates()) {
      GCRelocateInst &GCR = *const_cast<GCRelocateInst *>(Reloc);

      // Remove the relocation if unused.
      if (GCR.use_empty()) {
        eraseInstFromFunction(GCR);
        continue;
      }

      Value *DerivedPtr = GCR.getDerivedPtr();
      Value *BasePtr = GCR.getBasePtr();

      // Undef is undef, even after relocation.
      if (isa<UndefValue>(DerivedPtr) || isa<UndefValue>(BasePtr)) {
        replaceInstUsesWith(GCR, UndefValue::get(GCR.getType()));
        eraseInstFromFunction(GCR);
        continue;
      }

      if (auto *PT = dyn_cast<PointerType>(GCR.getType())) {
        // The relocation of null will be null for most any collector.
        // TODO: provide a hook for this in GCStrategy.  There might be some
        // weird collector this property does not hold for.
        if (isa<ConstantPointerNull>(DerivedPtr)) {
          // Use null-pointer of gc_relocate's type to replace it.
          replaceInstUsesWith(GCR, ConstantPointerNull::get(PT));
          eraseInstFromFunction(GCR);
          continue;
        }

        // isKnownNonNull -> nonnull attribute
        if (!GCR.hasRetAttr(Attribute::NonNull) &&
            isKnownNonZero(DerivedPtr,
                           getSimplifyQuery().getWithInstruction(&Call))) {
          GCR.addRetAttr(Attribute::NonNull);
          // We discovered new fact, re-check users.
          Worklist.pushUsersToWorkList(GCR);
        }
      }

      // If we have two copies of the same pointer in the statepoint argument
      // list, canonicalize to one.  This may let us common gc.relocates.
      if (GCR.getBasePtr() == GCR.getDerivedPtr() &&
          GCR.getBasePtrIndex() != GCR.getDerivedPtrIndex()) {
        auto *OpIntTy = GCR.getOperand(2)->getType();
        GCR.setOperand(2, ConstantInt::get(OpIntTy, GCR.getBasePtrIndex()));
      }

      // TODO: bitcast(relocate(p)) -> relocate(bitcast(p))
      // Canonicalize on the type from the uses to the defs

      // TODO: relocate((gep p, C, C2, ...)) -> gep(relocate(p), C, C2, ...)
      LiveGcValues.insert(BasePtr);
      LiveGcValues.insert(DerivedPtr);
    }
    std::optional<OperandBundleUse> Bundle =
        GCSP.getOperandBundle(LLVMContext::OB_gc_live);
    unsigned NumOfGCLives = LiveGcValues.size();
    if (!Bundle || NumOfGCLives == Bundle->Inputs.size())
      break;
    // We can reduce the size of gc live bundle.
    DenseMap<Value *, unsigned> Val2Idx;
    std::vector<Value *> NewLiveGc;
    for (Value *V : Bundle->Inputs) {
      if (Val2Idx.count(V))
        continue;
      if (LiveGcValues.count(V)) {
        Val2Idx[V] = NewLiveGc.size();
        NewLiveGc.push_back(V);
      } else
        Val2Idx[V] = NumOfGCLives;
    }
    // Update all gc.relocates
    for (const GCRelocateInst *Reloc : GCSP.getGCRelocates()) {
      GCRelocateInst &GCR = *const_cast<GCRelocateInst *>(Reloc);
      Value *BasePtr = GCR.getBasePtr();
      assert(Val2Idx.count(BasePtr) && Val2Idx[BasePtr] != NumOfGCLives &&
             "Missed live gc for base pointer");
      auto *OpIntTy1 = GCR.getOperand(1)->getType();
      GCR.setOperand(1, ConstantInt::get(OpIntTy1, Val2Idx[BasePtr]));
      Value *DerivedPtr = GCR.getDerivedPtr();
      assert(Val2Idx.count(DerivedPtr) && Val2Idx[DerivedPtr] != NumOfGCLives &&
             "Missed live gc for derived pointer");
      auto *OpIntTy2 = GCR.getOperand(2)->getType();
      GCR.setOperand(2, ConstantInt::get(OpIntTy2, Val2Idx[DerivedPtr]));
    }
    // Create new statepoint instruction.
    OperandBundleDef NewBundle("gc-live", NewLiveGc);
    return CallBase::Create(&Call, NewBundle);
  }
  default: { break; }
  }

  return Changed ? &Call : nullptr;
}

/// If the callee is a constexpr cast of a function, attempt to move the cast to
/// the arguments of the call/invoke.
/// CallBrInst is not supported.
bool InstCombinerImpl::transformConstExprCastCall(CallBase &Call) {
  auto *Callee =
      dyn_cast<Function>(Call.getCalledOperand()->stripPointerCasts());
  if (!Callee)
    return false;

  assert(!isa<CallBrInst>(Call) &&
         "CallBr's don't have a single point after a def to insert at");

  // If this is a call to a thunk function, don't remove the cast. Thunks are
  // used to transparently forward all incoming parameters and outgoing return
  // values, so it's important to leave the cast in place.
  if (Callee->hasFnAttribute("thunk"))
    return false;

  // If this is a call to a naked function, the assembly might be
  // using an argument, or otherwise rely on the frame layout,
  // the function prototype will mismatch.
  if (Callee->hasFnAttribute(Attribute::Naked))
    return false;

  // If this is a musttail call, the callee's prototype must match the caller's
  // prototype with the exception of pointee types. The code below doesn't
  // implement that, so we can't do this transform.
  // TODO: Do the transform if it only requires adding pointer casts.
  if (Call.isMustTailCall())
    return false;

  Instruction *Caller = &Call;
  const AttributeList &CallerPAL = Call.getAttributes();

  // Okay, this is a cast from a function to a different type.  Unless doing so
  // would cause a type conversion of one of our arguments, change this call to
  // be a direct call with arguments casted to the appropriate types.
  FunctionType *FT = Callee->getFunctionType();
  Type *OldRetTy = Caller->getType();
  Type *NewRetTy = FT->getReturnType();

  // Check to see if we are changing the return type...
  if (OldRetTy != NewRetTy) {

    if (NewRetTy->isStructTy())
      return false; // TODO: Handle multiple return values.

    if (!CastInst::isBitOrNoopPointerCastable(NewRetTy, OldRetTy, DL)) {
      if (Callee->isDeclaration())
        return false;   // Cannot transform this return value.

      if (!Caller->use_empty() &&
          // void -> non-void is handled specially
          !NewRetTy->isVoidTy())
        return false;   // Cannot transform this return value.
    }

    if (!CallerPAL.isEmpty() && !Caller->use_empty()) {
      AttrBuilder RAttrs(FT->getContext(), CallerPAL.getRetAttrs());
      if (RAttrs.overlaps(AttributeFuncs::typeIncompatible(NewRetTy)))
        return false;   // Attribute not compatible with transformed value.
    }

    // If the callbase is an invoke instruction, and the return value is
    // used by a PHI node in a successor, we cannot change the return type of
    // the call because there is no place to put the cast instruction (without
    // breaking the critical edge).  Bail out in this case.
    if (!Caller->use_empty()) {
      BasicBlock *PhisNotSupportedBlock = nullptr;
      if (auto *II = dyn_cast<InvokeInst>(Caller))
        PhisNotSupportedBlock = II->getNormalDest();
      if (PhisNotSupportedBlock)
        for (User *U : Caller->users())
          if (PHINode *PN = dyn_cast<PHINode>(U))
            if (PN->getParent() == PhisNotSupportedBlock)
              return false;
    }
  }

  unsigned NumActualArgs = Call.arg_size();
  unsigned NumCommonArgs = std::min(FT->getNumParams(), NumActualArgs);

  // Prevent us turning:
  // declare void @takes_i32_inalloca(i32* inalloca)
  //  call void bitcast (void (i32*)* @takes_i32_inalloca to void (i32)*)(i32 0)
  //
  // into:
  //  call void @takes_i32_inalloca(i32* null)
  //
  //  Similarly, avoid folding away bitcasts of byval calls.
  if (Callee->getAttributes().hasAttrSomewhere(Attribute::InAlloca) ||
      Callee->getAttributes().hasAttrSomewhere(Attribute::Preallocated))
    return false;

  auto AI = Call.arg_begin();
  for (unsigned i = 0, e = NumCommonArgs; i != e; ++i, ++AI) {
    Type *ParamTy = FT->getParamType(i);
    Type *ActTy = (*AI)->getType();

    if (!CastInst::isBitOrNoopPointerCastable(ActTy, ParamTy, DL))
      return false;   // Cannot transform this parameter value.

    // Check if there are any incompatible attributes we cannot drop safely.
    if (AttrBuilder(FT->getContext(), CallerPAL.getParamAttrs(i))
            .overlaps(AttributeFuncs::typeIncompatible(
                ParamTy, AttributeFuncs::ASK_UNSAFE_TO_DROP)))
      return false;   // Attribute not compatible with transformed value.

    if (Call.isInAllocaArgument(i) ||
        CallerPAL.hasParamAttr(i, Attribute::Preallocated))
      return false; // Cannot transform to and from inalloca/preallocated.

    if (CallerPAL.hasParamAttr(i, Attribute::SwiftError))
      return false;

    if (CallerPAL.hasParamAttr(i, Attribute::ByVal) !=
        Callee->getAttributes().hasParamAttr(i, Attribute::ByVal))
      return false; // Cannot transform to or from byval.
  }

  if (Callee->isDeclaration()) {
    // Do not delete arguments unless we have a function body.
    if (FT->getNumParams() < NumActualArgs && !FT->isVarArg())
      return false;

    // If the callee is just a declaration, don't change the varargsness of the
    // call.  We don't want to introduce a varargs call where one doesn't
    // already exist.
    if (FT->isVarArg() != Call.getFunctionType()->isVarArg())
      return false;

    // If both the callee and the cast type are varargs, we still have to make
    // sure the number of fixed parameters are the same or we have the same
    // ABI issues as if we introduce a varargs call.
    if (FT->isVarArg() && Call.getFunctionType()->isVarArg() &&
        FT->getNumParams() != Call.getFunctionType()->getNumParams())
      return false;
  }

  if (FT->getNumParams() < NumActualArgs && FT->isVarArg() &&
      !CallerPAL.isEmpty()) {
    // In this case we have more arguments than the new function type, but we
    // won't be dropping them.  Check that these extra arguments have attributes
    // that are compatible with being a vararg call argument.
    unsigned SRetIdx;
    if (CallerPAL.hasAttrSomewhere(Attribute::StructRet, &SRetIdx) &&
        SRetIdx - AttributeList::FirstArgIndex >= FT->getNumParams())
      return false;
  }

  // Okay, we decided that this is a safe thing to do: go ahead and start
  // inserting cast instructions as necessary.
  SmallVector<Value *, 8> Args;
  SmallVector<AttributeSet, 8> ArgAttrs;
  Args.reserve(NumActualArgs);
  ArgAttrs.reserve(NumActualArgs);

  // Get any return attributes.
  AttrBuilder RAttrs(FT->getContext(), CallerPAL.getRetAttrs());

  // If the return value is not being used, the type may not be compatible
  // with the existing attributes.  Wipe out any problematic attributes.
  RAttrs.remove(AttributeFuncs::typeIncompatible(NewRetTy));

  LLVMContext &Ctx = Call.getContext();
  AI = Call.arg_begin();
  for (unsigned i = 0; i != NumCommonArgs; ++i, ++AI) {
    Type *ParamTy = FT->getParamType(i);

    Value *NewArg = *AI;
    if ((*AI)->getType() != ParamTy)
      NewArg = Builder.CreateBitOrPointerCast(*AI, ParamTy);
    Args.push_back(NewArg);

    // Add any parameter attributes except the ones incompatible with the new
    // type. Note that we made sure all incompatible ones are safe to drop.
    AttributeMask IncompatibleAttrs = AttributeFuncs::typeIncompatible(
        ParamTy, AttributeFuncs::ASK_SAFE_TO_DROP);
    ArgAttrs.push_back(
        CallerPAL.getParamAttrs(i).removeAttributes(Ctx, IncompatibleAttrs));
  }

  // If the function takes more arguments than the call was taking, add them
  // now.
  for (unsigned i = NumCommonArgs; i != FT->getNumParams(); ++i) {
    Args.push_back(Constant::getNullValue(FT->getParamType(i)));
    ArgAttrs.push_back(AttributeSet());
  }

  // If we are removing arguments to the function, emit an obnoxious warning.
  if (FT->getNumParams() < NumActualArgs) {
    // TODO: if (!FT->isVarArg()) this call may be unreachable. PR14722
    if (FT->isVarArg()) {
      // Add all of the arguments in their promoted form to the arg list.
      for (unsigned i = FT->getNumParams(); i != NumActualArgs; ++i, ++AI) {
        Type *PTy = getPromotedType((*AI)->getType());
        Value *NewArg = *AI;
        if (PTy != (*AI)->getType()) {
          // Must promote to pass through va_arg area!
          Instruction::CastOps opcode =
            CastInst::getCastOpcode(*AI, false, PTy, false);
          NewArg = Builder.CreateCast(opcode, *AI, PTy);
        }
        Args.push_back(NewArg);

        // Add any parameter attributes.
        ArgAttrs.push_back(CallerPAL.getParamAttrs(i));
      }
    }
  }

  AttributeSet FnAttrs = CallerPAL.getFnAttrs();

  if (NewRetTy->isVoidTy())
    Caller->setName("");   // Void type should not have a name.

  assert((ArgAttrs.size() == FT->getNumParams() || FT->isVarArg()) &&
         "missing argument attributes");
  AttributeList NewCallerPAL = AttributeList::get(
      Ctx, FnAttrs, AttributeSet::get(Ctx, RAttrs), ArgAttrs);

  SmallVector<OperandBundleDef, 1> OpBundles;
  Call.getOperandBundlesAsDefs(OpBundles);

  CallBase *NewCall;
  if (InvokeInst *II = dyn_cast<InvokeInst>(Caller)) {
    NewCall = Builder.CreateInvoke(Callee, II->getNormalDest(),
                                   II->getUnwindDest(), Args, OpBundles);
  } else {
    NewCall = Builder.CreateCall(Callee, Args, OpBundles);
    cast<CallInst>(NewCall)->setTailCallKind(
        cast<CallInst>(Caller)->getTailCallKind());
  }
  NewCall->takeName(Caller);
  NewCall->setCallingConv(Call.getCallingConv());
  NewCall->setAttributes(NewCallerPAL);

  // Preserve prof metadata if any.
  NewCall->copyMetadata(*Caller, {LLVMContext::MD_prof});

  // Insert a cast of the return type as necessary.
  Instruction *NC = NewCall;
  Value *NV = NC;
  if (OldRetTy != NV->getType() && !Caller->use_empty()) {
    if (!NV->getType()->isVoidTy()) {
      NV = NC = CastInst::CreateBitOrPointerCast(NC, OldRetTy);
      NC->setDebugLoc(Caller->getDebugLoc());

      auto OptInsertPt = NewCall->getInsertionPointAfterDef();
      assert(OptInsertPt && "No place to insert cast");
      InsertNewInstBefore(NC, *OptInsertPt);
      Worklist.pushUsersToWorkList(*Caller);
    } else {
      NV = PoisonValue::get(Caller->getType());
    }
  }

  if (!Caller->use_empty())
    replaceInstUsesWith(*Caller, NV);
  else if (Caller->hasValueHandle()) {
    if (OldRetTy == NV->getType())
      ValueHandleBase::ValueIsRAUWd(Caller, NV);
    else
      // We cannot call ValueIsRAUWd with a different type, and the
      // actual tracked value will disappear.
      ValueHandleBase::ValueIsDeleted(Caller);
  }

  eraseInstFromFunction(*Caller);
  return true;
}

/// Turn a call to a function created by init_trampoline / adjust_trampoline
/// intrinsic pair into a direct call to the underlying function.
Instruction *
InstCombinerImpl::transformCallThroughTrampoline(CallBase &Call,
                                                 IntrinsicInst &Tramp) {
  FunctionType *FTy = Call.getFunctionType();
  AttributeList Attrs = Call.getAttributes();

  // If the call already has the 'nest' attribute somewhere then give up -
  // otherwise 'nest' would occur twice after splicing in the chain.
  if (Attrs.hasAttrSomewhere(Attribute::Nest))
    return nullptr;

  Function *NestF = cast<Function>(Tramp.getArgOperand(1)->stripPointerCasts());
  FunctionType *NestFTy = NestF->getFunctionType();

  AttributeList NestAttrs = NestF->getAttributes();
  if (!NestAttrs.isEmpty()) {
    unsigned NestArgNo = 0;
    Type *NestTy = nullptr;
    AttributeSet NestAttr;

    // Look for a parameter marked with the 'nest' attribute.
    for (FunctionType::param_iterator I = NestFTy->param_begin(),
                                      E = NestFTy->param_end();
         I != E; ++NestArgNo, ++I) {
      AttributeSet AS = NestAttrs.getParamAttrs(NestArgNo);
      if (AS.hasAttribute(Attribute::Nest)) {
        // Record the parameter type and any other attributes.
        NestTy = *I;
        NestAttr = AS;
        break;
      }
    }

    if (NestTy) {
      std::vector<Value*> NewArgs;
      std::vector<AttributeSet> NewArgAttrs;
      NewArgs.reserve(Call.arg_size() + 1);
      NewArgAttrs.reserve(Call.arg_size());

      // Insert the nest argument into the call argument list, which may
      // mean appending it.  Likewise for attributes.

      {
        unsigned ArgNo = 0;
        auto I = Call.arg_begin(), E = Call.arg_end();
        do {
          if (ArgNo == NestArgNo) {
            // Add the chain argument and attributes.
            Value *NestVal = Tramp.getArgOperand(2);
            if (NestVal->getType() != NestTy)
              NestVal = Builder.CreateBitCast(NestVal, NestTy, "nest");
            NewArgs.push_back(NestVal);
            NewArgAttrs.push_back(NestAttr);
          }

          if (I == E)
            break;

          // Add the original argument and attributes.
          NewArgs.push_back(*I);
          NewArgAttrs.push_back(Attrs.getParamAttrs(ArgNo));

          ++ArgNo;
          ++I;
        } while (true);
      }

      // The trampoline may have been bitcast to a bogus type (FTy).
      // Handle this by synthesizing a new function type, equal to FTy
      // with the chain parameter inserted.

      std::vector<Type*> NewTypes;
      NewTypes.reserve(FTy->getNumParams()+1);

      // Insert the chain's type into the list of parameter types, which may
      // mean appending it.
      {
        unsigned ArgNo = 0;
        FunctionType::param_iterator I = FTy->param_begin(),
          E = FTy->param_end();

        do {
          if (ArgNo == NestArgNo)
            // Add the chain's type.
            NewTypes.push_back(NestTy);

          if (I == E)
            break;

          // Add the original type.
          NewTypes.push_back(*I);

          ++ArgNo;
          ++I;
        } while (true);
      }

      // Replace the trampoline call with a direct call.  Let the generic
      // code sort out any function type mismatches.
      FunctionType *NewFTy =
          FunctionType::get(FTy->getReturnType(), NewTypes, FTy->isVarArg());
      AttributeList NewPAL =
          AttributeList::get(FTy->getContext(), Attrs.getFnAttrs(),
                             Attrs.getRetAttrs(), NewArgAttrs);

      SmallVector<OperandBundleDef, 1> OpBundles;
      Call.getOperandBundlesAsDefs(OpBundles);

      Instruction *NewCaller;
      if (InvokeInst *II = dyn_cast<InvokeInst>(&Call)) {
        NewCaller = InvokeInst::Create(NewFTy, NestF, II->getNormalDest(),
                                       II->getUnwindDest(), NewArgs, OpBundles);
        cast<InvokeInst>(NewCaller)->setCallingConv(II->getCallingConv());
        cast<InvokeInst>(NewCaller)->setAttributes(NewPAL);
      } else if (CallBrInst *CBI = dyn_cast<CallBrInst>(&Call)) {
        NewCaller =
            CallBrInst::Create(NewFTy, NestF, CBI->getDefaultDest(),
                               CBI->getIndirectDests(), NewArgs, OpBundles);
        cast<CallBrInst>(NewCaller)->setCallingConv(CBI->getCallingConv());
        cast<CallBrInst>(NewCaller)->setAttributes(NewPAL);
      } else {
        NewCaller = CallInst::Create(NewFTy, NestF, NewArgs, OpBundles);
        cast<CallInst>(NewCaller)->setTailCallKind(
            cast<CallInst>(Call).getTailCallKind());
        cast<CallInst>(NewCaller)->setCallingConv(
            cast<CallInst>(Call).getCallingConv());
        cast<CallInst>(NewCaller)->setAttributes(NewPAL);
      }
      NewCaller->setDebugLoc(Call.getDebugLoc());

      return NewCaller;
    }
  }

  // Replace the trampoline call with a direct call.  Since there is no 'nest'
  // parameter, there is no need to adjust the argument list.  Let the generic
  // code sort out any function type mismatches.
  Call.setCalledFunction(FTy, NestF);
  return &Call;
}
