//===- IRBuilder.cpp - Builder for LLVM Instrs ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IRBuilder class, which is used as a convenient way
// to create LLVM instructions with a consistent and simplified interface.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/IRBuilder.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/NoFolder.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

using namespace llvm;

/// CreateGlobalString - Make a new global variable with an initializer that
/// has array of i8 type filled in with the nul terminated string value
/// specified.  If Name is specified, it is the name of the global variable
/// created.
GlobalVariable *IRBuilderBase::CreateGlobalString(StringRef Str,
                                                  const Twine &Name,
                                                  unsigned AddressSpace,
                                                  Module *M, bool AddNull) {
  Constant *StrConstant = ConstantDataArray::getString(Context, Str, AddNull);
  if (!M)
    M = BB->getParent()->getParent();
  auto *GV = new GlobalVariable(
      *M, StrConstant->getType(), true, GlobalValue::PrivateLinkage,
      StrConstant, Name, nullptr, GlobalVariable::NotThreadLocal, AddressSpace);
  GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
  GV->setAlignment(Align(1));
  return GV;
}

Type *IRBuilderBase::getCurrentFunctionReturnType() const {
  assert(BB && BB->getParent() && "No current function!");
  return BB->getParent()->getReturnType();
}

DebugLoc IRBuilderBase::getCurrentDebugLocation() const {
  for (auto &KV : MetadataToCopy)
    if (KV.first == LLVMContext::MD_dbg)
      return {cast<DILocation>(KV.second)};

  return {};
}
void IRBuilderBase::SetInstDebugLocation(Instruction *I) const {
  for (const auto &KV : MetadataToCopy)
    if (KV.first == LLVMContext::MD_dbg) {
      I->setDebugLoc(DebugLoc(KV.second));
      return;
    }
}

CallInst *
IRBuilderBase::createCallHelper(Function *Callee, ArrayRef<Value *> Ops,
                                const Twine &Name, Instruction *FMFSource,
                                ArrayRef<OperandBundleDef> OpBundles) {
  CallInst *CI = CreateCall(Callee, Ops, OpBundles, Name);
  if (FMFSource)
    CI->copyFastMathFlags(FMFSource);
  return CI;
}

Value *IRBuilderBase::CreateVScale(Constant *Scaling, const Twine &Name) {
  assert(isa<ConstantInt>(Scaling) && "Expected constant integer");
  if (cast<ConstantInt>(Scaling)->isZero())
    return Scaling;
  Module *M = GetInsertBlock()->getParent()->getParent();
  Function *TheFn =
      Intrinsic::getDeclaration(M, Intrinsic::vscale, {Scaling->getType()});
  CallInst *CI = CreateCall(TheFn, {}, {}, Name);
  return cast<ConstantInt>(Scaling)->isOne() ? CI : CreateMul(CI, Scaling);
}

Value *IRBuilderBase::CreateElementCount(Type *DstType, ElementCount EC) {
  Constant *MinEC = ConstantInt::get(DstType, EC.getKnownMinValue());
  return EC.isScalable() ? CreateVScale(MinEC) : MinEC;
}

Value *IRBuilderBase::CreateTypeSize(Type *DstType, TypeSize Size) {
  Constant *MinSize = ConstantInt::get(DstType, Size.getKnownMinValue());
  return Size.isScalable() ? CreateVScale(MinSize) : MinSize;
}

Value *IRBuilderBase::CreateStepVector(Type *DstType, const Twine &Name) {
  Type *STy = DstType->getScalarType();
  if (isa<ScalableVectorType>(DstType)) {
    Type *StepVecType = DstType;
    // TODO: We expect this special case (element type < 8 bits) to be
    // temporary - once the intrinsic properly supports < 8 bits this code
    // can be removed.
    if (STy->getScalarSizeInBits() < 8)
      StepVecType =
          VectorType::get(getInt8Ty(), cast<ScalableVectorType>(DstType));
    Value *Res = CreateIntrinsic(Intrinsic::experimental_stepvector,
                                 {StepVecType}, {}, nullptr, Name);
    if (StepVecType != DstType)
      Res = CreateTrunc(Res, DstType);
    return Res;
  }

  unsigned NumEls = cast<FixedVectorType>(DstType)->getNumElements();

  // Create a vector of consecutive numbers from zero to VF.
  SmallVector<Constant *, 8> Indices;
  for (unsigned i = 0; i < NumEls; ++i)
    Indices.push_back(ConstantInt::get(STy, i));

  // Add the consecutive indices to the vector value.
  return ConstantVector::get(Indices);
}

CallInst *IRBuilderBase::CreateMemSet(Value *Ptr, Value *Val, Value *Size,
                                      MaybeAlign Align, bool isVolatile,
                                      MDNode *TBAATag, MDNode *ScopeTag,
                                      MDNode *NoAliasTag) {
  Value *Ops[] = {Ptr, Val, Size, getInt1(isVolatile)};
  Type *Tys[] = { Ptr->getType(), Size->getType() };
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(M, Intrinsic::memset, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  if (Align)
    cast<MemSetInst>(CI)->setDestAlignment(*Align);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateMemSetInline(Value *Dst, MaybeAlign DstAlign,
                                            Value *Val, Value *Size,
                                            bool IsVolatile, MDNode *TBAATag,
                                            MDNode *ScopeTag,
                                            MDNode *NoAliasTag) {
  Value *Ops[] = {Dst, Val, Size, getInt1(IsVolatile)};
  Type *Tys[] = {Dst->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(M, Intrinsic::memset_inline, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  if (DstAlign)
    cast<MemSetInlineInst>(CI)->setDestAlignment(*DstAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemSet(
    Value *Ptr, Value *Val, Value *Size, Align Alignment, uint32_t ElementSize,
    MDNode *TBAATag, MDNode *ScopeTag, MDNode *NoAliasTag) {

  Value *Ops[] = {Ptr, Val, Size, getInt32(ElementSize)};
  Type *Tys[] = {Ptr->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memset_element_unordered_atomic, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  cast<AtomicMemSetInst>(CI)->setDestAlignment(Alignment);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateMemTransferInst(
    Intrinsic::ID IntrID, Value *Dst, MaybeAlign DstAlign, Value *Src,
    MaybeAlign SrcAlign, Value *Size, bool isVolatile, MDNode *TBAATag,
    MDNode *TBAAStructTag, MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert((IntrID == Intrinsic::memcpy || IntrID == Intrinsic::memcpy_inline ||
          IntrID == Intrinsic::memmove) &&
         "Unexpected intrinsic ID");
  Value *Ops[] = {Dst, Src, Size, getInt1(isVolatile)};
  Type *Tys[] = { Dst->getType(), Src->getType(), Size->getType() };
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(M, IntrID, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  auto* MCI = cast<MemTransferInst>(CI);
  if (DstAlign)
    MCI->setDestAlignment(*DstAlign);
  if (SrcAlign)
    MCI->setSourceAlignment(*SrcAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemCpy(
    Value *Dst, Align DstAlign, Value *Src, Align SrcAlign, Value *Size,
    uint32_t ElementSize, MDNode *TBAATag, MDNode *TBAAStructTag,
    MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert(DstAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  assert(SrcAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  Value *Ops[] = {Dst, Src, Size, getInt32(ElementSize)};
  Type *Tys[] = {Dst->getType(), Src->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memcpy_element_unordered_atomic, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  // Set the alignment of the pointer args.
  auto *AMCI = cast<AtomicMemCpyInst>(CI);
  AMCI->setDestAlignment(DstAlign);
  AMCI->setSourceAlignment(SrcAlign);

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

/// isConstantOne - Return true only if val is constant int 1
static bool isConstantOne(const Value *Val) {
  assert(Val && "isConstantOne does not work with nullptr Val");
  const ConstantInt *CVal = dyn_cast<ConstantInt>(Val);
  return CVal && CVal->isOne();
}

CallInst *IRBuilderBase::CreateMalloc(Type *IntPtrTy, Type *AllocTy,
                                      Value *AllocSize, Value *ArraySize,
                                      ArrayRef<OperandBundleDef> OpB,
                                      Function *MallocF, const Twine &Name) {
  // malloc(type) becomes:
  //       i8* malloc(typeSize)
  // malloc(type, arraySize) becomes:
  //       i8* malloc(typeSize*arraySize)
  if (!ArraySize)
    ArraySize = ConstantInt::get(IntPtrTy, 1);
  else if (ArraySize->getType() != IntPtrTy)
    ArraySize = CreateIntCast(ArraySize, IntPtrTy, false);

  if (!isConstantOne(ArraySize)) {
    if (isConstantOne(AllocSize)) {
      AllocSize = ArraySize; // Operand * 1 = Operand
    } else {
      // Multiply type size by the array size...
      AllocSize = CreateMul(ArraySize, AllocSize, "mallocsize");
    }
  }

  assert(AllocSize->getType() == IntPtrTy && "malloc arg is wrong size");
  // Create the call to Malloc.
  Module *M = BB->getParent()->getParent();
  Type *BPTy = PointerType::getUnqual(Context);
  FunctionCallee MallocFunc = MallocF;
  if (!MallocFunc)
    // prototype malloc as "void *malloc(size_t)"
    MallocFunc = M->getOrInsertFunction("malloc", BPTy, IntPtrTy);
  CallInst *MCall = CreateCall(MallocFunc, AllocSize, OpB, Name);

  MCall->setTailCall();
  if (Function *F = dyn_cast<Function>(MallocFunc.getCallee())) {
    MCall->setCallingConv(F->getCallingConv());
    F->setReturnDoesNotAlias();
  }

  assert(!MCall->getType()->isVoidTy() && "Malloc has void return type");

  return MCall;
}

CallInst *IRBuilderBase::CreateMalloc(Type *IntPtrTy, Type *AllocTy,
                                      Value *AllocSize, Value *ArraySize,
                                      Function *MallocF, const Twine &Name) {

  return CreateMalloc(IntPtrTy, AllocTy, AllocSize, ArraySize, std::nullopt,
                      MallocF, Name);
}

/// CreateFree - Generate the IR for a call to the builtin free function.
CallInst *IRBuilderBase::CreateFree(Value *Source,
                                    ArrayRef<OperandBundleDef> Bundles) {
  assert(Source->getType()->isPointerTy() &&
         "Can not free something of nonpointer type!");

  Module *M = BB->getParent()->getParent();

  Type *VoidTy = Type::getVoidTy(M->getContext());
  Type *VoidPtrTy = PointerType::getUnqual(M->getContext());
  // prototype free as "void free(void*)"
  FunctionCallee FreeFunc = M->getOrInsertFunction("free", VoidTy, VoidPtrTy);
  CallInst *Result = CreateCall(FreeFunc, Source, Bundles, "");
  Result->setTailCall();
  if (Function *F = dyn_cast<Function>(FreeFunc.getCallee()))
    Result->setCallingConv(F->getCallingConv());

  return Result;
}

CallInst *IRBuilderBase::CreateElementUnorderedAtomicMemMove(
    Value *Dst, Align DstAlign, Value *Src, Align SrcAlign, Value *Size,
    uint32_t ElementSize, MDNode *TBAATag, MDNode *TBAAStructTag,
    MDNode *ScopeTag, MDNode *NoAliasTag) {
  assert(DstAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  assert(SrcAlign >= ElementSize &&
         "Pointer alignment must be at least element size");
  Value *Ops[] = {Dst, Src, Size, getInt32(ElementSize)};
  Type *Tys[] = {Dst->getType(), Src->getType(), Size->getType()};
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(
      M, Intrinsic::memmove_element_unordered_atomic, Tys);

  CallInst *CI = CreateCall(TheFn, Ops);

  // Set the alignment of the pointer args.
  CI->addParamAttr(0, Attribute::getWithAlignment(CI->getContext(), DstAlign));
  CI->addParamAttr(1, Attribute::getWithAlignment(CI->getContext(), SrcAlign));

  // Set the TBAA info if present.
  if (TBAATag)
    CI->setMetadata(LLVMContext::MD_tbaa, TBAATag);

  // Set the TBAA Struct info if present.
  if (TBAAStructTag)
    CI->setMetadata(LLVMContext::MD_tbaa_struct, TBAAStructTag);

  if (ScopeTag)
    CI->setMetadata(LLVMContext::MD_alias_scope, ScopeTag);

  if (NoAliasTag)
    CI->setMetadata(LLVMContext::MD_noalias, NoAliasTag);

  return CI;
}

CallInst *IRBuilderBase::getReductionIntrinsic(Intrinsic::ID ID, Value *Src) {
  Module *M = GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Src};
  Type *Tys[] = { Src->getType() };
  auto Decl = Intrinsic::getDeclaration(M, ID, Tys);
  return CreateCall(Decl, Ops);
}

CallInst *IRBuilderBase::CreateFAddReduce(Value *Acc, Value *Src) {
  Module *M = GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Acc, Src};
  auto Decl = Intrinsic::getDeclaration(M, Intrinsic::vector_reduce_fadd,
                                        {Src->getType()});
  return CreateCall(Decl, Ops);
}

CallInst *IRBuilderBase::CreateFMulReduce(Value *Acc, Value *Src) {
  Module *M = GetInsertBlock()->getParent()->getParent();
  Value *Ops[] = {Acc, Src};
  auto Decl = Intrinsic::getDeclaration(M, Intrinsic::vector_reduce_fmul,
                                        {Src->getType()});
  return CreateCall(Decl, Ops);
}

CallInst *IRBuilderBase::CreateAddReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_add, Src);
}

CallInst *IRBuilderBase::CreateMulReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_mul, Src);
}

CallInst *IRBuilderBase::CreateAndReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_and, Src);
}

CallInst *IRBuilderBase::CreateOrReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_or, Src);
}

CallInst *IRBuilderBase::CreateXorReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_xor, Src);
}

CallInst *IRBuilderBase::CreateIntMaxReduce(Value *Src, bool IsSigned) {
  auto ID =
      IsSigned ? Intrinsic::vector_reduce_smax : Intrinsic::vector_reduce_umax;
  return getReductionIntrinsic(ID, Src);
}

CallInst *IRBuilderBase::CreateIntMinReduce(Value *Src, bool IsSigned) {
  auto ID =
      IsSigned ? Intrinsic::vector_reduce_smin : Intrinsic::vector_reduce_umin;
  return getReductionIntrinsic(ID, Src);
}

CallInst *IRBuilderBase::CreateFPMaxReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_fmax, Src);
}

CallInst *IRBuilderBase::CreateFPMinReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_fmin, Src);
}

CallInst *IRBuilderBase::CreateFPMaximumReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_fmaximum, Src);
}

CallInst *IRBuilderBase::CreateFPMinimumReduce(Value *Src) {
  return getReductionIntrinsic(Intrinsic::vector_reduce_fminimum, Src);
}

CallInst *IRBuilderBase::CreateLifetimeStart(Value *Ptr, ConstantInt *Size) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "lifetime.start only applies to pointers.");
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "lifetime.start requires the size to be an i64");
  Value *Ops[] = { Size, Ptr };
  Module *M = BB->getParent()->getParent();
  Function *TheFn =
      Intrinsic::getDeclaration(M, Intrinsic::lifetime_start, {Ptr->getType()});
  return CreateCall(TheFn, Ops);
}

CallInst *IRBuilderBase::CreateLifetimeEnd(Value *Ptr, ConstantInt *Size) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "lifetime.end only applies to pointers.");
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "lifetime.end requires the size to be an i64");
  Value *Ops[] = { Size, Ptr };
  Module *M = BB->getParent()->getParent();
  Function *TheFn =
      Intrinsic::getDeclaration(M, Intrinsic::lifetime_end, {Ptr->getType()});
  return CreateCall(TheFn, Ops);
}

CallInst *IRBuilderBase::CreateInvariantStart(Value *Ptr, ConstantInt *Size) {

  assert(isa<PointerType>(Ptr->getType()) &&
         "invariant.start only applies to pointers.");
  if (!Size)
    Size = getInt64(-1);
  else
    assert(Size->getType() == getInt64Ty() &&
           "invariant.start requires the size to be an i64");

  Value *Ops[] = {Size, Ptr};
  // Fill in the single overloaded type: memory object type.
  Type *ObjectPtr[1] = {Ptr->getType()};
  Module *M = BB->getParent()->getParent();
  Function *TheFn =
      Intrinsic::getDeclaration(M, Intrinsic::invariant_start, ObjectPtr);
  return CreateCall(TheFn, Ops);
}

static MaybeAlign getAlign(Value *Ptr) {
  if (auto *O = dyn_cast<GlobalObject>(Ptr))
    return O->getAlign();
  if (auto *A = dyn_cast<GlobalAlias>(Ptr))
    return A->getAliaseeObject()->getAlign();
  return {};
}

CallInst *IRBuilderBase::CreateThreadLocalAddress(Value *Ptr) {
  assert(isa<GlobalValue>(Ptr) && cast<GlobalValue>(Ptr)->isThreadLocal() &&
         "threadlocal_address only applies to thread local variables.");
  CallInst *CI = CreateIntrinsic(llvm::Intrinsic::threadlocal_address,
                                 {Ptr->getType()}, {Ptr});
  if (MaybeAlign A = getAlign(Ptr)) {
    CI->addParamAttr(0, Attribute::getWithAlignment(CI->getContext(), *A));
    CI->addRetAttr(Attribute::getWithAlignment(CI->getContext(), *A));
  }
  return CI;
}

CallInst *
IRBuilderBase::CreateAssumption(Value *Cond,
                                ArrayRef<OperandBundleDef> OpBundles) {
  assert(Cond->getType() == getInt1Ty() &&
         "an assumption condition must be of type i1");

  Value *Ops[] = { Cond };
  Module *M = BB->getParent()->getParent();
  Function *FnAssume = Intrinsic::getDeclaration(M, Intrinsic::assume);
  return CreateCall(FnAssume, Ops, OpBundles);
}

Instruction *IRBuilderBase::CreateNoAliasScopeDeclaration(Value *Scope) {
  Module *M = BB->getModule();
  auto *FnIntrinsic = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_noalias_scope_decl, {});
  return CreateCall(FnIntrinsic, {Scope});
}

/// Create a call to a Masked Load intrinsic.
/// \p Ty        - vector type to load
/// \p Ptr       - base pointer for the load
/// \p Alignment - alignment of the source location
/// \p Mask      - vector of booleans which indicates what vector lanes should
///                be accessed in memory
/// \p PassThru  - pass-through value that is used to fill the masked-off lanes
///                of the result
/// \p Name      - name of the result variable
CallInst *IRBuilderBase::CreateMaskedLoad(Type *Ty, Value *Ptr, Align Alignment,
                                          Value *Mask, Value *PassThru,
                                          const Twine &Name) {
  auto *PtrTy = cast<PointerType>(Ptr->getType());
  assert(Ty->isVectorTy() && "Type should be vector");
  assert(Mask && "Mask should not be all-ones (null)");
  if (!PassThru)
    PassThru = PoisonValue::get(Ty);
  Type *OverloadedTypes[] = { Ty, PtrTy };
  Value *Ops[] = {Ptr, getInt32(Alignment.value()), Mask, PassThru};
  return CreateMaskedIntrinsic(Intrinsic::masked_load, Ops,
                               OverloadedTypes, Name);
}

/// Create a call to a Masked Store intrinsic.
/// \p Val       - data to be stored,
/// \p Ptr       - base pointer for the store
/// \p Alignment - alignment of the destination location
/// \p Mask      - vector of booleans which indicates what vector lanes should
///                be accessed in memory
CallInst *IRBuilderBase::CreateMaskedStore(Value *Val, Value *Ptr,
                                           Align Alignment, Value *Mask) {
  auto *PtrTy = cast<PointerType>(Ptr->getType());
  Type *DataTy = Val->getType();
  assert(DataTy->isVectorTy() && "Val should be a vector");
  assert(Mask && "Mask should not be all-ones (null)");
  Type *OverloadedTypes[] = { DataTy, PtrTy };
  Value *Ops[] = {Val, Ptr, getInt32(Alignment.value()), Mask};
  return CreateMaskedIntrinsic(Intrinsic::masked_store, Ops, OverloadedTypes);
}

/// Create a call to a Masked intrinsic, with given intrinsic Id,
/// an array of operands - Ops, and an array of overloaded types -
/// OverloadedTypes.
CallInst *IRBuilderBase::CreateMaskedIntrinsic(Intrinsic::ID Id,
                                               ArrayRef<Value *> Ops,
                                               ArrayRef<Type *> OverloadedTypes,
                                               const Twine &Name) {
  Module *M = BB->getParent()->getParent();
  Function *TheFn = Intrinsic::getDeclaration(M, Id, OverloadedTypes);
  return CreateCall(TheFn, Ops, {}, Name);
}

/// Create a call to a Masked Gather intrinsic.
/// \p Ty       - vector type to gather
/// \p Ptrs     - vector of pointers for loading
/// \p Align    - alignment for one element
/// \p Mask     - vector of booleans which indicates what vector lanes should
///               be accessed in memory
/// \p PassThru - pass-through value that is used to fill the masked-off lanes
///               of the result
/// \p Name     - name of the result variable
CallInst *IRBuilderBase::CreateMaskedGather(Type *Ty, Value *Ptrs,
                                            Align Alignment, Value *Mask,
                                            Value *PassThru,
                                            const Twine &Name) {
  auto *VecTy = cast<VectorType>(Ty);
  ElementCount NumElts = VecTy->getElementCount();
  auto *PtrsTy = cast<VectorType>(Ptrs->getType());
  assert(NumElts == PtrsTy->getElementCount() && "Element count mismatch");

  if (!Mask)
    Mask = getAllOnesMask(NumElts);

  if (!PassThru)
    PassThru = PoisonValue::get(Ty);

  Type *OverloadedTypes[] = {Ty, PtrsTy};
  Value *Ops[] = {Ptrs, getInt32(Alignment.value()), Mask, PassThru};

  // We specify only one type when we create this intrinsic. Types of other
  // arguments are derived from this type.
  return CreateMaskedIntrinsic(Intrinsic::masked_gather, Ops, OverloadedTypes,
                               Name);
}

/// Create a call to a Masked Scatter intrinsic.
/// \p Data  - data to be stored,
/// \p Ptrs  - the vector of pointers, where the \p Data elements should be
///            stored
/// \p Align - alignment for one element
/// \p Mask  - vector of booleans which indicates what vector lanes should
///            be accessed in memory
CallInst *IRBuilderBase::CreateMaskedScatter(Value *Data, Value *Ptrs,
                                             Align Alignment, Value *Mask) {
  auto *PtrsTy = cast<VectorType>(Ptrs->getType());
  auto *DataTy = cast<VectorType>(Data->getType());
  ElementCount NumElts = PtrsTy->getElementCount();

  if (!Mask)
    Mask = getAllOnesMask(NumElts);

  Type *OverloadedTypes[] = {DataTy, PtrsTy};
  Value *Ops[] = {Data, Ptrs, getInt32(Alignment.value()), Mask};

  // We specify only one type when we create this intrinsic. Types of other
  // arguments are derived from this type.
  return CreateMaskedIntrinsic(Intrinsic::masked_scatter, Ops, OverloadedTypes);
}

/// Create a call to Masked Expand Load intrinsic
/// \p Ty        - vector type to load
/// \p Ptr       - base pointer for the load
/// \p Mask      - vector of booleans which indicates what vector lanes should
///                be accessed in memory
/// \p PassThru  - pass-through value that is used to fill the masked-off lanes
///                of the result
/// \p Name      - name of the result variable
CallInst *IRBuilderBase::CreateMaskedExpandLoad(Type *Ty, Value *Ptr,
                                                Value *Mask, Value *PassThru,
                                                const Twine &Name) {
  assert(Ty->isVectorTy() && "Type should be vector");
  assert(Mask && "Mask should not be all-ones (null)");
  if (!PassThru)
    PassThru = PoisonValue::get(Ty);
  Type *OverloadedTypes[] = {Ty};
  Value *Ops[] = {Ptr, Mask, PassThru};
  return CreateMaskedIntrinsic(Intrinsic::masked_expandload, Ops,
                               OverloadedTypes, Name);
}

/// Create a call to Masked Compress Store intrinsic
/// \p Val       - data to be stored,
/// \p Ptr       - base pointer for the store
/// \p Mask      - vector of booleans which indicates what vector lanes should
///                be accessed in memory
CallInst *IRBuilderBase::CreateMaskedCompressStore(Value *Val, Value *Ptr,
                                                   Value *Mask) {
  Type *DataTy = Val->getType();
  assert(DataTy->isVectorTy() && "Val should be a vector");
  assert(Mask && "Mask should not be all-ones (null)");
  Type *OverloadedTypes[] = {DataTy};
  Value *Ops[] = {Val, Ptr, Mask};
  return CreateMaskedIntrinsic(Intrinsic::masked_compressstore, Ops,
                               OverloadedTypes);
}

template <typename T0>
static std::vector<Value *>
getStatepointArgs(IRBuilderBase &B, uint64_t ID, uint32_t NumPatchBytes,
                  Value *ActualCallee, uint32_t Flags, ArrayRef<T0> CallArgs) {
  std::vector<Value *> Args;
  Args.push_back(B.getInt64(ID));
  Args.push_back(B.getInt32(NumPatchBytes));
  Args.push_back(ActualCallee);
  Args.push_back(B.getInt32(CallArgs.size()));
  Args.push_back(B.getInt32(Flags));
  llvm::append_range(Args, CallArgs);
  // GC Transition and Deopt args are now always handled via operand bundle.
  // They will be removed from the signature of gc.statepoint shortly.
  Args.push_back(B.getInt32(0));
  Args.push_back(B.getInt32(0));
  // GC args are now encoded in the gc-live operand bundle
  return Args;
}

template<typename T1, typename T2, typename T3>
static std::vector<OperandBundleDef>
getStatepointBundles(std::optional<ArrayRef<T1>> TransitionArgs,
                     std::optional<ArrayRef<T2>> DeoptArgs,
                     ArrayRef<T3> GCArgs) {
  std::vector<OperandBundleDef> Rval;
  if (DeoptArgs) {
    SmallVector<Value*, 16> DeoptValues;
    llvm::append_range(DeoptValues, *DeoptArgs);
    Rval.emplace_back("deopt", DeoptValues);
  }
  if (TransitionArgs) {
    SmallVector<Value*, 16> TransitionValues;
    llvm::append_range(TransitionValues, *TransitionArgs);
    Rval.emplace_back("gc-transition", TransitionValues);
  }
  if (GCArgs.size()) {
    SmallVector<Value*, 16> LiveValues;
    llvm::append_range(LiveValues, GCArgs);
    Rval.emplace_back("gc-live", LiveValues);
  }
  return Rval;
}

template <typename T0, typename T1, typename T2, typename T3>
static CallInst *CreateGCStatepointCallCommon(
    IRBuilderBase *Builder, uint64_t ID, uint32_t NumPatchBytes,
    FunctionCallee ActualCallee, uint32_t Flags, ArrayRef<T0> CallArgs,
    std::optional<ArrayRef<T1>> TransitionArgs,
    std::optional<ArrayRef<T2>> DeoptArgs, ArrayRef<T3> GCArgs,
    const Twine &Name) {
  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  // Fill in the one generic type'd argument (the function is also vararg)
  Function *FnStatepoint =
      Intrinsic::getDeclaration(M, Intrinsic::experimental_gc_statepoint,
                                {ActualCallee.getCallee()->getType()});

  std::vector<Value *> Args = getStatepointArgs(
      *Builder, ID, NumPatchBytes, ActualCallee.getCallee(), Flags, CallArgs);

  CallInst *CI = Builder->CreateCall(
      FnStatepoint, Args,
      getStatepointBundles(TransitionArgs, DeoptArgs, GCArgs), Name);
  CI->addParamAttr(2,
                   Attribute::get(Builder->getContext(), Attribute::ElementType,
                                  ActualCallee.getFunctionType()));
  return CI;
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualCallee,
    ArrayRef<Value *> CallArgs, std::optional<ArrayRef<Value *>> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointCallCommon<Value *, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualCallee, uint32_t(StatepointFlags::None),
      CallArgs, std::nullopt /* No Transition Args */, DeoptArgs, GCArgs, Name);
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualCallee,
    uint32_t Flags, ArrayRef<Value *> CallArgs,
    std::optional<ArrayRef<Use>> TransitionArgs,
    std::optional<ArrayRef<Use>> DeoptArgs, ArrayRef<Value *> GCArgs,
    const Twine &Name) {
  return CreateGCStatepointCallCommon<Value *, Use, Use, Value *>(
      this, ID, NumPatchBytes, ActualCallee, Flags, CallArgs, TransitionArgs,
      DeoptArgs, GCArgs, Name);
}

CallInst *IRBuilderBase::CreateGCStatepointCall(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualCallee,
    ArrayRef<Use> CallArgs, std::optional<ArrayRef<Value *>> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointCallCommon<Use, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualCallee, uint32_t(StatepointFlags::None),
      CallArgs, std::nullopt, DeoptArgs, GCArgs, Name);
}

template <typename T0, typename T1, typename T2, typename T3>
static InvokeInst *CreateGCStatepointInvokeCommon(
    IRBuilderBase *Builder, uint64_t ID, uint32_t NumPatchBytes,
    FunctionCallee ActualInvokee, BasicBlock *NormalDest,
    BasicBlock *UnwindDest, uint32_t Flags, ArrayRef<T0> InvokeArgs,
    std::optional<ArrayRef<T1>> TransitionArgs,
    std::optional<ArrayRef<T2>> DeoptArgs, ArrayRef<T3> GCArgs,
    const Twine &Name) {
  Module *M = Builder->GetInsertBlock()->getParent()->getParent();
  // Fill in the one generic type'd argument (the function is also vararg)
  Function *FnStatepoint =
      Intrinsic::getDeclaration(M, Intrinsic::experimental_gc_statepoint,
                                {ActualInvokee.getCallee()->getType()});

  std::vector<Value *> Args =
      getStatepointArgs(*Builder, ID, NumPatchBytes, ActualInvokee.getCallee(),
                        Flags, InvokeArgs);

  InvokeInst *II = Builder->CreateInvoke(
      FnStatepoint, NormalDest, UnwindDest, Args,
      getStatepointBundles(TransitionArgs, DeoptArgs, GCArgs), Name);
  II->addParamAttr(2,
                   Attribute::get(Builder->getContext(), Attribute::ElementType,
                                  ActualInvokee.getFunctionType()));
  return II;
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest,
    ArrayRef<Value *> InvokeArgs, std::optional<ArrayRef<Value *>> DeoptArgs,
    ArrayRef<Value *> GCArgs, const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Value *, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest,
      uint32_t(StatepointFlags::None), InvokeArgs,
      std::nullopt /* No Transition Args*/, DeoptArgs, GCArgs, Name);
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest, uint32_t Flags,
    ArrayRef<Value *> InvokeArgs, std::optional<ArrayRef<Use>> TransitionArgs,
    std::optional<ArrayRef<Use>> DeoptArgs, ArrayRef<Value *> GCArgs,
    const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Value *, Use, Use, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest, Flags,
      InvokeArgs, TransitionArgs, DeoptArgs, GCArgs, Name);
}

InvokeInst *IRBuilderBase::CreateGCStatepointInvoke(
    uint64_t ID, uint32_t NumPatchBytes, FunctionCallee ActualInvokee,
    BasicBlock *NormalDest, BasicBlock *UnwindDest, ArrayRef<Use> InvokeArgs,
    std::optional<ArrayRef<Value *>> DeoptArgs, ArrayRef<Value *> GCArgs,
    const Twine &Name) {
  return CreateGCStatepointInvokeCommon<Use, Value *, Value *, Value *>(
      this, ID, NumPatchBytes, ActualInvokee, NormalDest, UnwindDest,
      uint32_t(StatepointFlags::None), InvokeArgs, std::nullopt, DeoptArgs,
      GCArgs, Name);
}

CallInst *IRBuilderBase::CreateGCResult(Instruction *Statepoint,
                                        Type *ResultType, const Twine &Name) {
  Intrinsic::ID ID = Intrinsic::experimental_gc_result;
  Module *M = BB->getParent()->getParent();
  Type *Types[] = {ResultType};
  Function *FnGCResult = Intrinsic::getDeclaration(M, ID, Types);

  Value *Args[] = {Statepoint};
  return CreateCall(FnGCResult, Args, {}, Name);
}

CallInst *IRBuilderBase::CreateGCRelocate(Instruction *Statepoint,
                                          int BaseOffset, int DerivedOffset,
                                          Type *ResultType, const Twine &Name) {
  Module *M = BB->getParent()->getParent();
  Type *Types[] = {ResultType};
  Function *FnGCRelocate =
      Intrinsic::getDeclaration(M, Intrinsic::experimental_gc_relocate, Types);

  Value *Args[] = {Statepoint, getInt32(BaseOffset), getInt32(DerivedOffset)};
  return CreateCall(FnGCRelocate, Args, {}, Name);
}

CallInst *IRBuilderBase::CreateGCGetPointerBase(Value *DerivedPtr,
                                                const Twine &Name) {
  Module *M = BB->getParent()->getParent();
  Type *PtrTy = DerivedPtr->getType();
  Function *FnGCFindBase = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_gc_get_pointer_base, {PtrTy, PtrTy});
  return CreateCall(FnGCFindBase, {DerivedPtr}, {}, Name);
}

CallInst *IRBuilderBase::CreateGCGetPointerOffset(Value *DerivedPtr,
                                                  const Twine &Name) {
  Module *M = BB->getParent()->getParent();
  Type *PtrTy = DerivedPtr->getType();
  Function *FnGCGetOffset = Intrinsic::getDeclaration(
      M, Intrinsic::experimental_gc_get_pointer_offset, {PtrTy});
  return CreateCall(FnGCGetOffset, {DerivedPtr}, {}, Name);
}

CallInst *IRBuilderBase::CreateUnaryIntrinsic(Intrinsic::ID ID, Value *V,
                                              Instruction *FMFSource,
                                              const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, {V->getType()});
  return createCallHelper(Fn, {V}, Name, FMFSource);
}

Value *IRBuilderBase::CreateBinaryIntrinsic(Intrinsic::ID ID, Value *LHS,
                                            Value *RHS, Instruction *FMFSource,
                                            const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, { LHS->getType() });
  if (Value *V = Folder.FoldBinaryIntrinsic(ID, LHS, RHS, Fn->getReturnType(),
                                            FMFSource))
    return V;
  return createCallHelper(Fn, {LHS, RHS}, Name, FMFSource);
}

CallInst *IRBuilderBase::CreateIntrinsic(Intrinsic::ID ID,
                                         ArrayRef<Type *> Types,
                                         ArrayRef<Value *> Args,
                                         Instruction *FMFSource,
                                         const Twine &Name) {
  Module *M = BB->getModule();
  Function *Fn = Intrinsic::getDeclaration(M, ID, Types);
  return createCallHelper(Fn, Args, Name, FMFSource);
}

CallInst *IRBuilderBase::CreateIntrinsic(Type *RetTy, Intrinsic::ID ID,
                                         ArrayRef<Value *> Args,
                                         Instruction *FMFSource,
                                         const Twine &Name) {
  Module *M = BB->getModule();

  SmallVector<Intrinsic::IITDescriptor> Table;
  Intrinsic::getIntrinsicInfoTableEntries(ID, Table);
  ArrayRef<Intrinsic::IITDescriptor> TableRef(Table);

  SmallVector<Type *> ArgTys;
  ArgTys.reserve(Args.size());
  for (auto &I : Args)
    ArgTys.push_back(I->getType());
  FunctionType *FTy = FunctionType::get(RetTy, ArgTys, false);
  SmallVector<Type *> OverloadTys;
  Intrinsic::MatchIntrinsicTypesResult Res =
      matchIntrinsicSignature(FTy, TableRef, OverloadTys);
  (void)Res;
  assert(Res == Intrinsic::MatchIntrinsicTypes_Match && TableRef.empty() &&
         "Wrong types for intrinsic!");
  // TODO: Handle varargs intrinsics.

  Function *Fn = Intrinsic::getDeclaration(M, ID, OverloadTys);
  return createCallHelper(Fn, Args, Name, FMFSource);
}

CallInst *IRBuilderBase::CreateConstrainedFPBinOp(
    Intrinsic::ID ID, Value *L, Value *R, Instruction *FMFSource,
    const Twine &Name, MDNode *FPMathTag,
    std::optional<RoundingMode> Rounding,
    std::optional<fp::ExceptionBehavior> Except) {
  Value *RoundingV = getConstrainedFPRounding(Rounding);
  Value *ExceptV = getConstrainedFPExcept(Except);

  FastMathFlags UseFMF = FMF;
  if (FMFSource)
    UseFMF = FMFSource->getFastMathFlags();

  CallInst *C = CreateIntrinsic(ID, {L->getType()},
                                {L, R, RoundingV, ExceptV}, nullptr, Name);
  setConstrainedFPCallAttr(C);
  setFPAttrs(C, FPMathTag, UseFMF);
  return C;
}

CallInst *IRBuilderBase::CreateConstrainedFPUnroundedBinOp(
    Intrinsic::ID ID, Value *L, Value *R, Instruction *FMFSource,
    const Twine &Name, MDNode *FPMathTag,
    std::optional<fp::ExceptionBehavior> Except) {
  Value *ExceptV = getConstrainedFPExcept(Except);

  FastMathFlags UseFMF = FMF;
  if (FMFSource)
    UseFMF = FMFSource->getFastMathFlags();

  CallInst *C =
      CreateIntrinsic(ID, {L->getType()}, {L, R, ExceptV}, nullptr, Name);
  setConstrainedFPCallAttr(C);
  setFPAttrs(C, FPMathTag, UseFMF);
  return C;
}

Value *IRBuilderBase::CreateNAryOp(unsigned Opc, ArrayRef<Value *> Ops,
                                   const Twine &Name, MDNode *FPMathTag) {
  if (Instruction::isBinaryOp(Opc)) {
    assert(Ops.size() == 2 && "Invalid number of operands!");
    return CreateBinOp(static_cast<Instruction::BinaryOps>(Opc),
                       Ops[0], Ops[1], Name, FPMathTag);
  }
  if (Instruction::isUnaryOp(Opc)) {
    assert(Ops.size() == 1 && "Invalid number of operands!");
    return CreateUnOp(static_cast<Instruction::UnaryOps>(Opc),
                      Ops[0], Name, FPMathTag);
  }
  llvm_unreachable("Unexpected opcode!");
}

CallInst *IRBuilderBase::CreateConstrainedFPCast(
    Intrinsic::ID ID, Value *V, Type *DestTy,
    Instruction *FMFSource, const Twine &Name, MDNode *FPMathTag,
    std::optional<RoundingMode> Rounding,
    std::optional<fp::ExceptionBehavior> Except) {
  Value *ExceptV = getConstrainedFPExcept(Except);

  FastMathFlags UseFMF = FMF;
  if (FMFSource)
    UseFMF = FMFSource->getFastMathFlags();

  CallInst *C;
  if (Intrinsic::hasConstrainedFPRoundingModeOperand(ID)) {
    Value *RoundingV = getConstrainedFPRounding(Rounding);
    C = CreateIntrinsic(ID, {DestTy, V->getType()}, {V, RoundingV, ExceptV},
                        nullptr, Name);
  } else
    C = CreateIntrinsic(ID, {DestTy, V->getType()}, {V, ExceptV}, nullptr,
                        Name);

  setConstrainedFPCallAttr(C);

  if (isa<FPMathOperator>(C))
    setFPAttrs(C, FPMathTag, UseFMF);
  return C;
}

Value *IRBuilderBase::CreateFCmpHelper(
    CmpInst::Predicate P, Value *LHS, Value *RHS, const Twine &Name,
    MDNode *FPMathTag, bool IsSignaling) {
  if (IsFPConstrained) {
    auto ID = IsSignaling ? Intrinsic::experimental_constrained_fcmps
                          : Intrinsic::experimental_constrained_fcmp;
    return CreateConstrainedFPCmp(ID, P, LHS, RHS, Name);
  }

  if (auto *V = Folder.FoldCmp(P, LHS, RHS))
    return V;
  return Insert(setFPAttrs(new FCmpInst(P, LHS, RHS), FPMathTag, FMF), Name);
}

CallInst *IRBuilderBase::CreateConstrainedFPCmp(
    Intrinsic::ID ID, CmpInst::Predicate P, Value *L, Value *R,
    const Twine &Name, std::optional<fp::ExceptionBehavior> Except) {
  Value *PredicateV = getConstrainedFPPredicate(P);
  Value *ExceptV = getConstrainedFPExcept(Except);

  CallInst *C = CreateIntrinsic(ID, {L->getType()},
                                {L, R, PredicateV, ExceptV}, nullptr, Name);
  setConstrainedFPCallAttr(C);
  return C;
}

CallInst *IRBuilderBase::CreateConstrainedFPCall(
    Function *Callee, ArrayRef<Value *> Args, const Twine &Name,
    std::optional<RoundingMode> Rounding,
    std::optional<fp::ExceptionBehavior> Except) {
  llvm::SmallVector<Value *, 6> UseArgs;

  append_range(UseArgs, Args);

  if (Intrinsic::hasConstrainedFPRoundingModeOperand(Callee->getIntrinsicID()))
    UseArgs.push_back(getConstrainedFPRounding(Rounding));
  UseArgs.push_back(getConstrainedFPExcept(Except));

  CallInst *C = CreateCall(Callee, UseArgs, Name);
  setConstrainedFPCallAttr(C);
  return C;
}

Value *IRBuilderBase::CreateSelect(Value *C, Value *True, Value *False,
                                   const Twine &Name, Instruction *MDFrom) {
  if (auto *V = Folder.FoldSelect(C, True, False))
    return V;

  SelectInst *Sel = SelectInst::Create(C, True, False);
  if (MDFrom) {
    MDNode *Prof = MDFrom->getMetadata(LLVMContext::MD_prof);
    MDNode *Unpred = MDFrom->getMetadata(LLVMContext::MD_unpredictable);
    Sel = addBranchMetadata(Sel, Prof, Unpred);
  }
  if (isa<FPMathOperator>(Sel))
    setFPAttrs(Sel, nullptr /* MDNode* */, FMF);
  return Insert(Sel, Name);
}

Value *IRBuilderBase::CreatePtrDiff(Type *ElemTy, Value *LHS, Value *RHS,
                                    const Twine &Name) {
  assert(LHS->getType() == RHS->getType() &&
         "Pointer subtraction operand types must match!");
  Value *LHS_int = CreatePtrToInt(LHS, Type::getInt64Ty(Context));
  Value *RHS_int = CreatePtrToInt(RHS, Type::getInt64Ty(Context));
  Value *Difference = CreateSub(LHS_int, RHS_int);
  return CreateExactSDiv(Difference, ConstantExpr::getSizeOf(ElemTy),
                         Name);
}

Value *IRBuilderBase::CreateLaunderInvariantGroup(Value *Ptr) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "launder.invariant.group only applies to pointers.");
  auto *PtrType = Ptr->getType();
  Module *M = BB->getParent()->getParent();
  Function *FnLaunderInvariantGroup = Intrinsic::getDeclaration(
      M, Intrinsic::launder_invariant_group, {PtrType});

  assert(FnLaunderInvariantGroup->getReturnType() == PtrType &&
         FnLaunderInvariantGroup->getFunctionType()->getParamType(0) ==
             PtrType &&
         "LaunderInvariantGroup should take and return the same type");

  return CreateCall(FnLaunderInvariantGroup, {Ptr});
}

Value *IRBuilderBase::CreateStripInvariantGroup(Value *Ptr) {
  assert(isa<PointerType>(Ptr->getType()) &&
         "strip.invariant.group only applies to pointers.");

  auto *PtrType = Ptr->getType();
  Module *M = BB->getParent()->getParent();
  Function *FnStripInvariantGroup = Intrinsic::getDeclaration(
      M, Intrinsic::strip_invariant_group, {PtrType});

  assert(FnStripInvariantGroup->getReturnType() == PtrType &&
         FnStripInvariantGroup->getFunctionType()->getParamType(0) ==
             PtrType &&
         "StripInvariantGroup should take and return the same type");

  return CreateCall(FnStripInvariantGroup, {Ptr});
}

Value *IRBuilderBase::CreateVectorReverse(Value *V, const Twine &Name) {
  auto *Ty = cast<VectorType>(V->getType());
  if (isa<ScalableVectorType>(Ty)) {
    Module *M = BB->getParent()->getParent();
    Function *F = Intrinsic::getDeclaration(M, Intrinsic::vector_reverse, Ty);
    return Insert(CallInst::Create(F, V), Name);
  }
  // Keep the original behaviour for fixed vector
  SmallVector<int, 8> ShuffleMask;
  int NumElts = Ty->getElementCount().getKnownMinValue();
  for (int i = 0; i < NumElts; ++i)
    ShuffleMask.push_back(NumElts - i - 1);
  return CreateShuffleVector(V, ShuffleMask, Name);
}

Value *IRBuilderBase::CreateVectorSplice(Value *V1, Value *V2, int64_t Imm,
                                         const Twine &Name) {
  assert(isa<VectorType>(V1->getType()) && "Unexpected type");
  assert(V1->getType() == V2->getType() &&
         "Splice expects matching operand types!");

  if (auto *VTy = dyn_cast<ScalableVectorType>(V1->getType())) {
    Module *M = BB->getParent()->getParent();
    Function *F = Intrinsic::getDeclaration(M, Intrinsic::vector_splice, VTy);

    Value *Ops[] = {V1, V2, getInt32(Imm)};
    return Insert(CallInst::Create(F, Ops), Name);
  }

  unsigned NumElts = cast<FixedVectorType>(V1->getType())->getNumElements();
  assert(((-Imm <= NumElts) || (Imm < NumElts)) &&
         "Invalid immediate for vector splice!");

  // Keep the original behaviour for fixed vector
  unsigned Idx = (NumElts + Imm) % NumElts;
  SmallVector<int, 8> Mask;
  for (unsigned I = 0; I < NumElts; ++I)
    Mask.push_back(Idx + I);

  return CreateShuffleVector(V1, V2, Mask);
}

Value *IRBuilderBase::CreateVectorSplat(unsigned NumElts, Value *V,
                                        const Twine &Name) {
  auto EC = ElementCount::getFixed(NumElts);
  return CreateVectorSplat(EC, V, Name);
}

Value *IRBuilderBase::CreateVectorSplat(ElementCount EC, Value *V,
                                        const Twine &Name) {
  assert(EC.isNonZero() && "Cannot splat to an empty vector!");

  // First insert it into a poison vector so we can shuffle it.
  Value *Poison = PoisonValue::get(VectorType::get(V->getType(), EC));
  V = CreateInsertElement(Poison, V, getInt64(0), Name + ".splatinsert");

  // Shuffle the value across the desired number of elements.
  SmallVector<int, 16> Zeros;
  Zeros.resize(EC.getKnownMinValue());
  return CreateShuffleVector(V, Zeros, Name + ".splat");
}

Value *IRBuilderBase::CreatePreserveArrayAccessIndex(
    Type *ElTy, Value *Base, unsigned Dimension, unsigned LastIndex,
    MDNode *DbgInfo) {
  auto *BaseType = Base->getType();
  assert(isa<PointerType>(BaseType) &&
         "Invalid Base ptr type for preserve.array.access.index.");

  Value *LastIndexV = getInt32(LastIndex);
  Constant *Zero = ConstantInt::get(Type::getInt32Ty(Context), 0);
  SmallVector<Value *, 4> IdxList(Dimension, Zero);
  IdxList.push_back(LastIndexV);

  Type *ResultType = GetElementPtrInst::getGEPReturnType(Base, IdxList);

  Module *M = BB->getParent()->getParent();
  Function *FnPreserveArrayAccessIndex = Intrinsic::getDeclaration(
      M, Intrinsic::preserve_array_access_index, {ResultType, BaseType});

  Value *DimV = getInt32(Dimension);
  CallInst *Fn =
      CreateCall(FnPreserveArrayAccessIndex, {Base, DimV, LastIndexV});
  Fn->addParamAttr(
      0, Attribute::get(Fn->getContext(), Attribute::ElementType, ElTy));
  if (DbgInfo)
    Fn->setMetadata(LLVMContext::MD_preserve_access_index, DbgInfo);

  return Fn;
}

Value *IRBuilderBase::CreatePreserveUnionAccessIndex(
    Value *Base, unsigned FieldIndex, MDNode *DbgInfo) {
  assert(isa<PointerType>(Base->getType()) &&
         "Invalid Base ptr type for preserve.union.access.index.");
  auto *BaseType = Base->getType();

  Module *M = BB->getParent()->getParent();
  Function *FnPreserveUnionAccessIndex = Intrinsic::getDeclaration(
      M, Intrinsic::preserve_union_access_index, {BaseType, BaseType});

  Value *DIIndex = getInt32(FieldIndex);
  CallInst *Fn =
      CreateCall(FnPreserveUnionAccessIndex, {Base, DIIndex});
  if (DbgInfo)
    Fn->setMetadata(LLVMContext::MD_preserve_access_index, DbgInfo);

  return Fn;
}

Value *IRBuilderBase::CreatePreserveStructAccessIndex(
    Type *ElTy, Value *Base, unsigned Index, unsigned FieldIndex,
    MDNode *DbgInfo) {
  auto *BaseType = Base->getType();
  assert(isa<PointerType>(BaseType) &&
         "Invalid Base ptr type for preserve.struct.access.index.");

  Value *GEPIndex = getInt32(Index);
  Constant *Zero = ConstantInt::get(Type::getInt32Ty(Context), 0);
  Type *ResultType =
      GetElementPtrInst::getGEPReturnType(Base, {Zero, GEPIndex});

  Module *M = BB->getParent()->getParent();
  Function *FnPreserveStructAccessIndex = Intrinsic::getDeclaration(
      M, Intrinsic::preserve_struct_access_index, {ResultType, BaseType});

  Value *DIIndex = getInt32(FieldIndex);
  CallInst *Fn = CreateCall(FnPreserveStructAccessIndex,
                            {Base, GEPIndex, DIIndex});
  Fn->addParamAttr(
      0, Attribute::get(Fn->getContext(), Attribute::ElementType, ElTy));
  if (DbgInfo)
    Fn->setMetadata(LLVMContext::MD_preserve_access_index, DbgInfo);

  return Fn;
}

Value *IRBuilderBase::createIsFPClass(Value *FPNum, unsigned Test) {
  ConstantInt *TestV = getInt32(Test);
  Module *M = BB->getParent()->getParent();
  Function *FnIsFPClass =
      Intrinsic::getDeclaration(M, Intrinsic::is_fpclass, {FPNum->getType()});
  return CreateCall(FnIsFPClass, {FPNum, TestV});
}

CallInst *IRBuilderBase::CreateAlignmentAssumptionHelper(const DataLayout &DL,
                                                         Value *PtrValue,
                                                         Value *AlignValue,
                                                         Value *OffsetValue) {
  SmallVector<Value *, 4> Vals({PtrValue, AlignValue});
  if (OffsetValue)
    Vals.push_back(OffsetValue);
  OperandBundleDefT<Value *> AlignOpB("align", Vals);
  return CreateAssumption(ConstantInt::getTrue(getContext()), {AlignOpB});
}

CallInst *IRBuilderBase::CreateAlignmentAssumption(const DataLayout &DL,
                                                   Value *PtrValue,
                                                   unsigned Alignment,
                                                   Value *OffsetValue) {
  assert(isa<PointerType>(PtrValue->getType()) &&
         "trying to create an alignment assumption on a non-pointer?");
  assert(Alignment != 0 && "Invalid Alignment");
  auto *PtrTy = cast<PointerType>(PtrValue->getType());
  Type *IntPtrTy = getIntPtrTy(DL, PtrTy->getAddressSpace());
  Value *AlignValue = ConstantInt::get(IntPtrTy, Alignment);
  return CreateAlignmentAssumptionHelper(DL, PtrValue, AlignValue, OffsetValue);
}

CallInst *IRBuilderBase::CreateAlignmentAssumption(const DataLayout &DL,
                                                   Value *PtrValue,
                                                   Value *Alignment,
                                                   Value *OffsetValue) {
  assert(isa<PointerType>(PtrValue->getType()) &&
         "trying to create an alignment assumption on a non-pointer?");
  return CreateAlignmentAssumptionHelper(DL, PtrValue, Alignment, OffsetValue);
}

IRBuilderDefaultInserter::~IRBuilderDefaultInserter() = default;
IRBuilderCallbackInserter::~IRBuilderCallbackInserter() = default;
IRBuilderFolder::~IRBuilderFolder() = default;
void ConstantFolder::anchor() {}
void NoFolder::anchor() {}
