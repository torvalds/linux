//===--- CGAtomic.cpp - Emit LLVM IR for atomic operations ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the code for emitting atomic operations.
//
//===----------------------------------------------------------------------===//

#include "CGCall.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Operator.h"

using namespace clang;
using namespace CodeGen;

namespace {
  class AtomicInfo {
    CodeGenFunction &CGF;
    QualType AtomicTy;
    QualType ValueTy;
    uint64_t AtomicSizeInBits;
    uint64_t ValueSizeInBits;
    CharUnits AtomicAlign;
    CharUnits ValueAlign;
    TypeEvaluationKind EvaluationKind;
    bool UseLibcall;
    LValue LVal;
    CGBitFieldInfo BFI;
  public:
    AtomicInfo(CodeGenFunction &CGF, LValue &lvalue)
        : CGF(CGF), AtomicSizeInBits(0), ValueSizeInBits(0),
          EvaluationKind(TEK_Scalar), UseLibcall(true) {
      assert(!lvalue.isGlobalReg());
      ASTContext &C = CGF.getContext();
      if (lvalue.isSimple()) {
        AtomicTy = lvalue.getType();
        if (auto *ATy = AtomicTy->getAs<AtomicType>())
          ValueTy = ATy->getValueType();
        else
          ValueTy = AtomicTy;
        EvaluationKind = CGF.getEvaluationKind(ValueTy);

        uint64_t ValueAlignInBits;
        uint64_t AtomicAlignInBits;
        TypeInfo ValueTI = C.getTypeInfo(ValueTy);
        ValueSizeInBits = ValueTI.Width;
        ValueAlignInBits = ValueTI.Align;

        TypeInfo AtomicTI = C.getTypeInfo(AtomicTy);
        AtomicSizeInBits = AtomicTI.Width;
        AtomicAlignInBits = AtomicTI.Align;

        assert(ValueSizeInBits <= AtomicSizeInBits);
        assert(ValueAlignInBits <= AtomicAlignInBits);

        AtomicAlign = C.toCharUnitsFromBits(AtomicAlignInBits);
        ValueAlign = C.toCharUnitsFromBits(ValueAlignInBits);
        if (lvalue.getAlignment().isZero())
          lvalue.setAlignment(AtomicAlign);

        LVal = lvalue;
      } else if (lvalue.isBitField()) {
        ValueTy = lvalue.getType();
        ValueSizeInBits = C.getTypeSize(ValueTy);
        auto &OrigBFI = lvalue.getBitFieldInfo();
        auto Offset = OrigBFI.Offset % C.toBits(lvalue.getAlignment());
        AtomicSizeInBits = C.toBits(
            C.toCharUnitsFromBits(Offset + OrigBFI.Size + C.getCharWidth() - 1)
                .alignTo(lvalue.getAlignment()));
        llvm::Value *BitFieldPtr = lvalue.getRawBitFieldPointer(CGF);
        auto OffsetInChars =
            (C.toCharUnitsFromBits(OrigBFI.Offset) / lvalue.getAlignment()) *
            lvalue.getAlignment();
        llvm::Value *StoragePtr = CGF.Builder.CreateConstGEP1_64(
            CGF.Int8Ty, BitFieldPtr, OffsetInChars.getQuantity());
        StoragePtr = CGF.Builder.CreateAddrSpaceCast(
            StoragePtr, CGF.UnqualPtrTy, "atomic_bitfield_base");
        BFI = OrigBFI;
        BFI.Offset = Offset;
        BFI.StorageSize = AtomicSizeInBits;
        BFI.StorageOffset += OffsetInChars;
        llvm::Type *StorageTy = CGF.Builder.getIntNTy(AtomicSizeInBits);
        LVal = LValue::MakeBitfield(
            Address(StoragePtr, StorageTy, lvalue.getAlignment()), BFI,
            lvalue.getType(), lvalue.getBaseInfo(), lvalue.getTBAAInfo());
        AtomicTy = C.getIntTypeForBitwidth(AtomicSizeInBits, OrigBFI.IsSigned);
        if (AtomicTy.isNull()) {
          llvm::APInt Size(
              /*numBits=*/32,
              C.toCharUnitsFromBits(AtomicSizeInBits).getQuantity());
          AtomicTy = C.getConstantArrayType(C.CharTy, Size, nullptr,
                                            ArraySizeModifier::Normal,
                                            /*IndexTypeQuals=*/0);
        }
        AtomicAlign = ValueAlign = lvalue.getAlignment();
      } else if (lvalue.isVectorElt()) {
        ValueTy = lvalue.getType()->castAs<VectorType>()->getElementType();
        ValueSizeInBits = C.getTypeSize(ValueTy);
        AtomicTy = lvalue.getType();
        AtomicSizeInBits = C.getTypeSize(AtomicTy);
        AtomicAlign = ValueAlign = lvalue.getAlignment();
        LVal = lvalue;
      } else {
        assert(lvalue.isExtVectorElt());
        ValueTy = lvalue.getType();
        ValueSizeInBits = C.getTypeSize(ValueTy);
        AtomicTy = ValueTy = CGF.getContext().getExtVectorType(
            lvalue.getType(), cast<llvm::FixedVectorType>(
                                  lvalue.getExtVectorAddress().getElementType())
                                  ->getNumElements());
        AtomicSizeInBits = C.getTypeSize(AtomicTy);
        AtomicAlign = ValueAlign = lvalue.getAlignment();
        LVal = lvalue;
      }
      UseLibcall = !C.getTargetInfo().hasBuiltinAtomic(
          AtomicSizeInBits, C.toBits(lvalue.getAlignment()));
    }

    QualType getAtomicType() const { return AtomicTy; }
    QualType getValueType() const { return ValueTy; }
    CharUnits getAtomicAlignment() const { return AtomicAlign; }
    uint64_t getAtomicSizeInBits() const { return AtomicSizeInBits; }
    uint64_t getValueSizeInBits() const { return ValueSizeInBits; }
    TypeEvaluationKind getEvaluationKind() const { return EvaluationKind; }
    bool shouldUseLibcall() const { return UseLibcall; }
    const LValue &getAtomicLValue() const { return LVal; }
    llvm::Value *getAtomicPointer() const {
      if (LVal.isSimple())
        return LVal.emitRawPointer(CGF);
      else if (LVal.isBitField())
        return LVal.getRawBitFieldPointer(CGF);
      else if (LVal.isVectorElt())
        return LVal.getRawVectorPointer(CGF);
      assert(LVal.isExtVectorElt());
      return LVal.getRawExtVectorPointer(CGF);
    }
    Address getAtomicAddress() const {
      llvm::Type *ElTy;
      if (LVal.isSimple())
        ElTy = LVal.getAddress().getElementType();
      else if (LVal.isBitField())
        ElTy = LVal.getBitFieldAddress().getElementType();
      else if (LVal.isVectorElt())
        ElTy = LVal.getVectorAddress().getElementType();
      else
        ElTy = LVal.getExtVectorAddress().getElementType();
      return Address(getAtomicPointer(), ElTy, getAtomicAlignment());
    }

    Address getAtomicAddressAsAtomicIntPointer() const {
      return castToAtomicIntPointer(getAtomicAddress());
    }

    /// Is the atomic size larger than the underlying value type?
    ///
    /// Note that the absence of padding does not mean that atomic
    /// objects are completely interchangeable with non-atomic
    /// objects: we might have promoted the alignment of a type
    /// without making it bigger.
    bool hasPadding() const {
      return (ValueSizeInBits != AtomicSizeInBits);
    }

    bool emitMemSetZeroIfNecessary() const;

    llvm::Value *getAtomicSizeValue() const {
      CharUnits size = CGF.getContext().toCharUnitsFromBits(AtomicSizeInBits);
      return CGF.CGM.getSize(size);
    }

    /// Cast the given pointer to an integer pointer suitable for atomic
    /// operations if the source.
    Address castToAtomicIntPointer(Address Addr) const;

    /// If Addr is compatible with the iN that will be used for an atomic
    /// operation, bitcast it. Otherwise, create a temporary that is suitable
    /// and copy the value across.
    Address convertToAtomicIntPointer(Address Addr) const;

    /// Turn an atomic-layout object into an r-value.
    RValue convertAtomicTempToRValue(Address addr, AggValueSlot resultSlot,
                                     SourceLocation loc, bool AsValue) const;

    llvm::Value *getScalarRValValueOrNull(RValue RVal) const;

    /// Converts an rvalue to integer value if needed.
    llvm::Value *convertRValueToInt(RValue RVal, bool CmpXchg = false) const;

    RValue ConvertToValueOrAtomic(llvm::Value *IntVal, AggValueSlot ResultSlot,
                                  SourceLocation Loc, bool AsValue,
                                  bool CmpXchg = false) const;

    /// Copy an atomic r-value into atomic-layout memory.
    void emitCopyIntoMemory(RValue rvalue) const;

    /// Project an l-value down to the value field.
    LValue projectValue() const {
      assert(LVal.isSimple());
      Address addr = getAtomicAddress();
      if (hasPadding())
        addr = CGF.Builder.CreateStructGEP(addr, 0);

      return LValue::MakeAddr(addr, getValueType(), CGF.getContext(),
                              LVal.getBaseInfo(), LVal.getTBAAInfo());
    }

    /// Emits atomic load.
    /// \returns Loaded value.
    RValue EmitAtomicLoad(AggValueSlot ResultSlot, SourceLocation Loc,
                          bool AsValue, llvm::AtomicOrdering AO,
                          bool IsVolatile);

    /// Emits atomic compare-and-exchange sequence.
    /// \param Expected Expected value.
    /// \param Desired Desired value.
    /// \param Success Atomic ordering for success operation.
    /// \param Failure Atomic ordering for failed operation.
    /// \param IsWeak true if atomic operation is weak, false otherwise.
    /// \returns Pair of values: previous value from storage (value type) and
    /// boolean flag (i1 type) with true if success and false otherwise.
    std::pair<RValue, llvm::Value *>
    EmitAtomicCompareExchange(RValue Expected, RValue Desired,
                              llvm::AtomicOrdering Success =
                                  llvm::AtomicOrdering::SequentiallyConsistent,
                              llvm::AtomicOrdering Failure =
                                  llvm::AtomicOrdering::SequentiallyConsistent,
                              bool IsWeak = false);

    /// Emits atomic update.
    /// \param AO Atomic ordering.
    /// \param UpdateOp Update operation for the current lvalue.
    void EmitAtomicUpdate(llvm::AtomicOrdering AO,
                          const llvm::function_ref<RValue(RValue)> &UpdateOp,
                          bool IsVolatile);
    /// Emits atomic update.
    /// \param AO Atomic ordering.
    void EmitAtomicUpdate(llvm::AtomicOrdering AO, RValue UpdateRVal,
                          bool IsVolatile);

    /// Materialize an atomic r-value in atomic-layout memory.
    Address materializeRValue(RValue rvalue) const;

    /// Creates temp alloca for intermediate operations on atomic value.
    Address CreateTempAlloca() const;
  private:
    bool requiresMemSetZero(llvm::Type *type) const;


    /// Emits atomic load as a libcall.
    void EmitAtomicLoadLibcall(llvm::Value *AddForLoaded,
                               llvm::AtomicOrdering AO, bool IsVolatile);
    /// Emits atomic load as LLVM instruction.
    llvm::Value *EmitAtomicLoadOp(llvm::AtomicOrdering AO, bool IsVolatile,
                                  bool CmpXchg = false);
    /// Emits atomic compare-and-exchange op as a libcall.
    llvm::Value *EmitAtomicCompareExchangeLibcall(
        llvm::Value *ExpectedAddr, llvm::Value *DesiredAddr,
        llvm::AtomicOrdering Success =
            llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering Failure =
            llvm::AtomicOrdering::SequentiallyConsistent);
    /// Emits atomic compare-and-exchange op as LLVM instruction.
    std::pair<llvm::Value *, llvm::Value *> EmitAtomicCompareExchangeOp(
        llvm::Value *ExpectedVal, llvm::Value *DesiredVal,
        llvm::AtomicOrdering Success =
            llvm::AtomicOrdering::SequentiallyConsistent,
        llvm::AtomicOrdering Failure =
            llvm::AtomicOrdering::SequentiallyConsistent,
        bool IsWeak = false);
    /// Emit atomic update as libcalls.
    void
    EmitAtomicUpdateLibcall(llvm::AtomicOrdering AO,
                            const llvm::function_ref<RValue(RValue)> &UpdateOp,
                            bool IsVolatile);
    /// Emit atomic update as LLVM instructions.
    void EmitAtomicUpdateOp(llvm::AtomicOrdering AO,
                            const llvm::function_ref<RValue(RValue)> &UpdateOp,
                            bool IsVolatile);
    /// Emit atomic update as libcalls.
    void EmitAtomicUpdateLibcall(llvm::AtomicOrdering AO, RValue UpdateRVal,
                                 bool IsVolatile);
    /// Emit atomic update as LLVM instructions.
    void EmitAtomicUpdateOp(llvm::AtomicOrdering AO, RValue UpdateRal,
                            bool IsVolatile);
  };
}

Address AtomicInfo::CreateTempAlloca() const {
  Address TempAlloca = CGF.CreateMemTemp(
      (LVal.isBitField() && ValueSizeInBits > AtomicSizeInBits) ? ValueTy
                                                                : AtomicTy,
      getAtomicAlignment(),
      "atomic-temp");
  // Cast to pointer to value type for bitfields.
  if (LVal.isBitField())
    return CGF.Builder.CreatePointerBitCastOrAddrSpaceCast(
        TempAlloca, getAtomicAddress().getType(),
        getAtomicAddress().getElementType());
  return TempAlloca;
}

static RValue emitAtomicLibcall(CodeGenFunction &CGF,
                                StringRef fnName,
                                QualType resultType,
                                CallArgList &args) {
  const CGFunctionInfo &fnInfo =
    CGF.CGM.getTypes().arrangeBuiltinFunctionCall(resultType, args);
  llvm::FunctionType *fnTy = CGF.CGM.getTypes().GetFunctionType(fnInfo);
  llvm::AttrBuilder fnAttrB(CGF.getLLVMContext());
  fnAttrB.addAttribute(llvm::Attribute::NoUnwind);
  fnAttrB.addAttribute(llvm::Attribute::WillReturn);
  llvm::AttributeList fnAttrs = llvm::AttributeList::get(
      CGF.getLLVMContext(), llvm::AttributeList::FunctionIndex, fnAttrB);

  llvm::FunctionCallee fn =
      CGF.CGM.CreateRuntimeFunction(fnTy, fnName, fnAttrs);
  auto callee = CGCallee::forDirect(fn);
  return CGF.EmitCall(fnInfo, callee, ReturnValueSlot(), args);
}

/// Does a store of the given IR type modify the full expected width?
static bool isFullSizeType(CodeGenModule &CGM, llvm::Type *type,
                           uint64_t expectedSize) {
  return (CGM.getDataLayout().getTypeStoreSize(type) * 8 == expectedSize);
}

/// Does the atomic type require memsetting to zero before initialization?
///
/// The IR type is provided as a way of making certain queries faster.
bool AtomicInfo::requiresMemSetZero(llvm::Type *type) const {
  // If the atomic type has size padding, we definitely need a memset.
  if (hasPadding()) return true;

  // Otherwise, do some simple heuristics to try to avoid it:
  switch (getEvaluationKind()) {
  // For scalars and complexes, check whether the store size of the
  // type uses the full size.
  case TEK_Scalar:
    return !isFullSizeType(CGF.CGM, type, AtomicSizeInBits);
  case TEK_Complex:
    return !isFullSizeType(CGF.CGM, type->getStructElementType(0),
                           AtomicSizeInBits / 2);

  // Padding in structs has an undefined bit pattern.  User beware.
  case TEK_Aggregate:
    return false;
  }
  llvm_unreachable("bad evaluation kind");
}

bool AtomicInfo::emitMemSetZeroIfNecessary() const {
  assert(LVal.isSimple());
  Address addr = LVal.getAddress();
  if (!requiresMemSetZero(addr.getElementType()))
    return false;

  CGF.Builder.CreateMemSet(
      addr.emitRawPointer(CGF), llvm::ConstantInt::get(CGF.Int8Ty, 0),
      CGF.getContext().toCharUnitsFromBits(AtomicSizeInBits).getQuantity(),
      LVal.getAlignment().getAsAlign());
  return true;
}

static void emitAtomicCmpXchg(CodeGenFunction &CGF, AtomicExpr *E, bool IsWeak,
                              Address Dest, Address Ptr,
                              Address Val1, Address Val2,
                              uint64_t Size,
                              llvm::AtomicOrdering SuccessOrder,
                              llvm::AtomicOrdering FailureOrder,
                              llvm::SyncScope::ID Scope) {
  // Note that cmpxchg doesn't support weak cmpxchg, at least at the moment.
  llvm::Value *Expected = CGF.Builder.CreateLoad(Val1);
  llvm::Value *Desired = CGF.Builder.CreateLoad(Val2);

  llvm::AtomicCmpXchgInst *Pair = CGF.Builder.CreateAtomicCmpXchg(
      Ptr, Expected, Desired, SuccessOrder, FailureOrder, Scope);
  Pair->setVolatile(E->isVolatile());
  Pair->setWeak(IsWeak);

  // Cmp holds the result of the compare-exchange operation: true on success,
  // false on failure.
  llvm::Value *Old = CGF.Builder.CreateExtractValue(Pair, 0);
  llvm::Value *Cmp = CGF.Builder.CreateExtractValue(Pair, 1);

  // This basic block is used to hold the store instruction if the operation
  // failed.
  llvm::BasicBlock *StoreExpectedBB =
      CGF.createBasicBlock("cmpxchg.store_expected", CGF.CurFn);

  // This basic block is the exit point of the operation, we should end up
  // here regardless of whether or not the operation succeeded.
  llvm::BasicBlock *ContinueBB =
      CGF.createBasicBlock("cmpxchg.continue", CGF.CurFn);

  // Update Expected if Expected isn't equal to Old, otherwise branch to the
  // exit point.
  CGF.Builder.CreateCondBr(Cmp, ContinueBB, StoreExpectedBB);

  CGF.Builder.SetInsertPoint(StoreExpectedBB);
  // Update the memory at Expected with Old's value.
  CGF.Builder.CreateStore(Old, Val1);
  // Finally, branch to the exit point.
  CGF.Builder.CreateBr(ContinueBB);

  CGF.Builder.SetInsertPoint(ContinueBB);
  // Update the memory at Dest with Cmp's value.
  CGF.EmitStoreOfScalar(Cmp, CGF.MakeAddrLValue(Dest, E->getType()));
}

/// Given an ordering required on success, emit all possible cmpxchg
/// instructions to cope with the provided (but possibly only dynamically known)
/// FailureOrder.
static void emitAtomicCmpXchgFailureSet(CodeGenFunction &CGF, AtomicExpr *E,
                                        bool IsWeak, Address Dest, Address Ptr,
                                        Address Val1, Address Val2,
                                        llvm::Value *FailureOrderVal,
                                        uint64_t Size,
                                        llvm::AtomicOrdering SuccessOrder,
                                        llvm::SyncScope::ID Scope) {
  llvm::AtomicOrdering FailureOrder;
  if (llvm::ConstantInt *FO = dyn_cast<llvm::ConstantInt>(FailureOrderVal)) {
    auto FOS = FO->getSExtValue();
    if (!llvm::isValidAtomicOrderingCABI(FOS))
      FailureOrder = llvm::AtomicOrdering::Monotonic;
    else
      switch ((llvm::AtomicOrderingCABI)FOS) {
      case llvm::AtomicOrderingCABI::relaxed:
      // 31.7.2.18: "The failure argument shall not be memory_order_release
      // nor memory_order_acq_rel". Fallback to monotonic.
      case llvm::AtomicOrderingCABI::release:
      case llvm::AtomicOrderingCABI::acq_rel:
        FailureOrder = llvm::AtomicOrdering::Monotonic;
        break;
      case llvm::AtomicOrderingCABI::consume:
      case llvm::AtomicOrderingCABI::acquire:
        FailureOrder = llvm::AtomicOrdering::Acquire;
        break;
      case llvm::AtomicOrderingCABI::seq_cst:
        FailureOrder = llvm::AtomicOrdering::SequentiallyConsistent;
        break;
      }
    // Prior to c++17, "the failure argument shall be no stronger than the
    // success argument". This condition has been lifted and the only
    // precondition is 31.7.2.18. Effectively treat this as a DR and skip
    // language version checks.
    emitAtomicCmpXchg(CGF, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                      FailureOrder, Scope);
    return;
  }

  // Create all the relevant BB's
  auto *MonotonicBB = CGF.createBasicBlock("monotonic_fail", CGF.CurFn);
  auto *AcquireBB = CGF.createBasicBlock("acquire_fail", CGF.CurFn);
  auto *SeqCstBB = CGF.createBasicBlock("seqcst_fail", CGF.CurFn);
  auto *ContBB = CGF.createBasicBlock("atomic.continue", CGF.CurFn);

  // MonotonicBB is arbitrarily chosen as the default case; in practice, this
  // doesn't matter unless someone is crazy enough to use something that
  // doesn't fold to a constant for the ordering.
  llvm::SwitchInst *SI = CGF.Builder.CreateSwitch(FailureOrderVal, MonotonicBB);
  // Implemented as acquire, since it's the closest in LLVM.
  SI->addCase(CGF.Builder.getInt32((int)llvm::AtomicOrderingCABI::consume),
              AcquireBB);
  SI->addCase(CGF.Builder.getInt32((int)llvm::AtomicOrderingCABI::acquire),
              AcquireBB);
  SI->addCase(CGF.Builder.getInt32((int)llvm::AtomicOrderingCABI::seq_cst),
              SeqCstBB);

  // Emit all the different atomics
  CGF.Builder.SetInsertPoint(MonotonicBB);
  emitAtomicCmpXchg(CGF, E, IsWeak, Dest, Ptr, Val1, Val2,
                    Size, SuccessOrder, llvm::AtomicOrdering::Monotonic, Scope);
  CGF.Builder.CreateBr(ContBB);

  CGF.Builder.SetInsertPoint(AcquireBB);
  emitAtomicCmpXchg(CGF, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                    llvm::AtomicOrdering::Acquire, Scope);
  CGF.Builder.CreateBr(ContBB);

  CGF.Builder.SetInsertPoint(SeqCstBB);
  emitAtomicCmpXchg(CGF, E, IsWeak, Dest, Ptr, Val1, Val2, Size, SuccessOrder,
                    llvm::AtomicOrdering::SequentiallyConsistent, Scope);
  CGF.Builder.CreateBr(ContBB);

  CGF.Builder.SetInsertPoint(ContBB);
}

/// Duplicate the atomic min/max operation in conventional IR for the builtin
/// variants that return the new rather than the original value.
static llvm::Value *EmitPostAtomicMinMax(CGBuilderTy &Builder,
                                         AtomicExpr::AtomicOp Op,
                                         bool IsSigned,
                                         llvm::Value *OldVal,
                                         llvm::Value *RHS) {
  llvm::CmpInst::Predicate Pred;
  switch (Op) {
  default:
    llvm_unreachable("Unexpected min/max operation");
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
    Pred = IsSigned ? llvm::CmpInst::ICMP_SGT : llvm::CmpInst::ICMP_UGT;
    break;
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
    Pred = IsSigned ? llvm::CmpInst::ICMP_SLT : llvm::CmpInst::ICMP_ULT;
    break;
  }
  llvm::Value *Cmp = Builder.CreateICmp(Pred, OldVal, RHS, "tst");
  return Builder.CreateSelect(Cmp, OldVal, RHS, "newval");
}

static void EmitAtomicOp(CodeGenFunction &CGF, AtomicExpr *E, Address Dest,
                         Address Ptr, Address Val1, Address Val2,
                         llvm::Value *IsWeak, llvm::Value *FailureOrder,
                         uint64_t Size, llvm::AtomicOrdering Order,
                         llvm::SyncScope::ID Scope) {
  llvm::AtomicRMWInst::BinOp Op = llvm::AtomicRMWInst::Add;
  bool PostOpMinMax = false;
  unsigned PostOp = 0;

  switch (E->getOp()) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    llvm_unreachable("Already handled!");

  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__hip_atomic_compare_exchange_strong:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_strong:
    emitAtomicCmpXchgFailureSet(CGF, E, false, Dest, Ptr, Val1, Val2,
                                FailureOrder, Size, Order, Scope);
    return;
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_weak:
  case AtomicExpr::AO__hip_atomic_compare_exchange_weak:
    emitAtomicCmpXchgFailureSet(CGF, E, true, Dest, Ptr, Val1, Val2,
                                FailureOrder, Size, Order, Scope);
    return;
  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n: {
    if (llvm::ConstantInt *IsWeakC = dyn_cast<llvm::ConstantInt>(IsWeak)) {
      emitAtomicCmpXchgFailureSet(CGF, E, IsWeakC->getZExtValue(), Dest, Ptr,
                                  Val1, Val2, FailureOrder, Size, Order, Scope);
    } else {
      // Create all the relevant BB's
      llvm::BasicBlock *StrongBB =
          CGF.createBasicBlock("cmpxchg.strong", CGF.CurFn);
      llvm::BasicBlock *WeakBB = CGF.createBasicBlock("cmxchg.weak", CGF.CurFn);
      llvm::BasicBlock *ContBB =
          CGF.createBasicBlock("cmpxchg.continue", CGF.CurFn);

      llvm::SwitchInst *SI = CGF.Builder.CreateSwitch(IsWeak, WeakBB);
      SI->addCase(CGF.Builder.getInt1(false), StrongBB);

      CGF.Builder.SetInsertPoint(StrongBB);
      emitAtomicCmpXchgFailureSet(CGF, E, false, Dest, Ptr, Val1, Val2,
                                  FailureOrder, Size, Order, Scope);
      CGF.Builder.CreateBr(ContBB);

      CGF.Builder.SetInsertPoint(WeakBB);
      emitAtomicCmpXchgFailureSet(CGF, E, true, Dest, Ptr, Val1, Val2,
                                  FailureOrder, Size, Order, Scope);
      CGF.Builder.CreateBr(ContBB);

      CGF.Builder.SetInsertPoint(ContBB);
    }
    return;
  }
  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__hip_atomic_load:
  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load: {
    llvm::LoadInst *Load = CGF.Builder.CreateLoad(Ptr);
    Load->setAtomic(Order, Scope);
    Load->setVolatile(E->isVolatile());
    CGF.Builder.CreateStore(Load, Dest);
    return;
  }

  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__hip_atomic_store:
  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_store:
  case AtomicExpr::AO__scoped_atomic_store_n: {
    llvm::Value *LoadVal1 = CGF.Builder.CreateLoad(Val1);
    llvm::StoreInst *Store = CGF.Builder.CreateStore(LoadVal1, Ptr);
    Store->setAtomic(Order, Scope);
    Store->setVolatile(E->isVolatile());
    return;
  }

  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__hip_atomic_exchange:
  case AtomicExpr::AO__opencl_atomic_exchange:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Op = llvm::AtomicRMWInst::Xchg;
    break;

  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
    PostOp = E->getValueType()->isFloatingType() ? llvm::Instruction::FAdd
                                                 : llvm::Instruction::Add;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__hip_atomic_fetch_add:
  case AtomicExpr::AO__opencl_atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
    Op = E->getValueType()->isFloatingType() ? llvm::AtomicRMWInst::FAdd
                                             : llvm::AtomicRMWInst::Add;
    break;

  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
    PostOp = E->getValueType()->isFloatingType() ? llvm::Instruction::FSub
                                                 : llvm::Instruction::Sub;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_sub:
  case AtomicExpr::AO__hip_atomic_fetch_sub:
  case AtomicExpr::AO__opencl_atomic_fetch_sub:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
    Op = E->getValueType()->isFloatingType() ? llvm::AtomicRMWInst::FSub
                                             : llvm::AtomicRMWInst::Sub;
    break;

  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
    PostOpMinMax = true;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_min:
  case AtomicExpr::AO__hip_atomic_fetch_min:
  case AtomicExpr::AO__opencl_atomic_fetch_min:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
    Op = E->getValueType()->isFloatingType()
             ? llvm::AtomicRMWInst::FMin
             : (E->getValueType()->isSignedIntegerType()
                    ? llvm::AtomicRMWInst::Min
                    : llvm::AtomicRMWInst::UMin);
    break;

  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
    PostOpMinMax = true;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__hip_atomic_fetch_max:
  case AtomicExpr::AO__opencl_atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
    Op = E->getValueType()->isFloatingType()
             ? llvm::AtomicRMWInst::FMax
             : (E->getValueType()->isSignedIntegerType()
                    ? llvm::AtomicRMWInst::Max
                    : llvm::AtomicRMWInst::UMax);
    break;

  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
    PostOp = llvm::Instruction::And;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__hip_atomic_fetch_and:
  case AtomicExpr::AO__opencl_atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
    Op = llvm::AtomicRMWInst::And;
    break;

  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
    PostOp = llvm::Instruction::Or;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__hip_atomic_fetch_or:
  case AtomicExpr::AO__opencl_atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
    Op = llvm::AtomicRMWInst::Or;
    break;

  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
    PostOp = llvm::Instruction::Xor;
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__hip_atomic_fetch_xor:
  case AtomicExpr::AO__opencl_atomic_fetch_xor:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
    Op = llvm::AtomicRMWInst::Xor;
    break;

  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
    PostOp = llvm::Instruction::And; // the NOT is special cased below
    [[fallthrough]];
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
    Op = llvm::AtomicRMWInst::Nand;
    break;
  }

  llvm::Value *LoadVal1 = CGF.Builder.CreateLoad(Val1);
  llvm::AtomicRMWInst *RMWI =
      CGF.Builder.CreateAtomicRMW(Op, Ptr, LoadVal1, Order, Scope);
  RMWI->setVolatile(E->isVolatile());

  // For __atomic_*_fetch operations, perform the operation again to
  // determine the value which was written.
  llvm::Value *Result = RMWI;
  if (PostOpMinMax)
    Result = EmitPostAtomicMinMax(CGF.Builder, E->getOp(),
                                  E->getValueType()->isSignedIntegerType(),
                                  RMWI, LoadVal1);
  else if (PostOp)
    Result = CGF.Builder.CreateBinOp((llvm::Instruction::BinaryOps)PostOp, RMWI,
                                     LoadVal1);
  if (E->getOp() == AtomicExpr::AO__atomic_nand_fetch ||
      E->getOp() == AtomicExpr::AO__scoped_atomic_nand_fetch)
    Result = CGF.Builder.CreateNot(Result);
  CGF.Builder.CreateStore(Result, Dest);
}

// This function emits any expression (scalar, complex, or aggregate)
// into a temporary alloca.
static Address
EmitValToTemp(CodeGenFunction &CGF, Expr *E) {
  Address DeclPtr = CGF.CreateMemTemp(E->getType(), ".atomictmp");
  CGF.EmitAnyExprToMem(E, DeclPtr, E->getType().getQualifiers(),
                       /*Init*/ true);
  return DeclPtr;
}

static void EmitAtomicOp(CodeGenFunction &CGF, AtomicExpr *Expr, Address Dest,
                         Address Ptr, Address Val1, Address Val2,
                         llvm::Value *IsWeak, llvm::Value *FailureOrder,
                         uint64_t Size, llvm::AtomicOrdering Order,
                         llvm::Value *Scope) {
  auto ScopeModel = Expr->getScopeModel();

  // LLVM atomic instructions always have synch scope. If clang atomic
  // expression has no scope operand, use default LLVM synch scope.
  if (!ScopeModel) {
    EmitAtomicOp(CGF, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                 Order, CGF.CGM.getLLVMContext().getOrInsertSyncScopeID(""));
    return;
  }

  // Handle constant scope.
  if (auto SC = dyn_cast<llvm::ConstantInt>(Scope)) {
    auto SCID = CGF.getTargetHooks().getLLVMSyncScopeID(
        CGF.CGM.getLangOpts(), ScopeModel->map(SC->getZExtValue()),
        Order, CGF.CGM.getLLVMContext());
    EmitAtomicOp(CGF, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                 Order, SCID);
    return;
  }

  // Handle non-constant scope.
  auto &Builder = CGF.Builder;
  auto Scopes = ScopeModel->getRuntimeValues();
  llvm::DenseMap<unsigned, llvm::BasicBlock *> BB;
  for (auto S : Scopes)
    BB[S] = CGF.createBasicBlock(getAsString(ScopeModel->map(S)), CGF.CurFn);

  llvm::BasicBlock *ContBB =
      CGF.createBasicBlock("atomic.scope.continue", CGF.CurFn);

  auto *SC = Builder.CreateIntCast(Scope, Builder.getInt32Ty(), false);
  // If unsupported synch scope is encountered at run time, assume a fallback
  // synch scope value.
  auto FallBack = ScopeModel->getFallBackValue();
  llvm::SwitchInst *SI = Builder.CreateSwitch(SC, BB[FallBack]);
  for (auto S : Scopes) {
    auto *B = BB[S];
    if (S != FallBack)
      SI->addCase(Builder.getInt32(S), B);

    Builder.SetInsertPoint(B);
    EmitAtomicOp(CGF, Expr, Dest, Ptr, Val1, Val2, IsWeak, FailureOrder, Size,
                 Order,
                 CGF.getTargetHooks().getLLVMSyncScopeID(CGF.CGM.getLangOpts(),
                                                         ScopeModel->map(S),
                                                         Order,
                                                         CGF.getLLVMContext()));
    Builder.CreateBr(ContBB);
  }

  Builder.SetInsertPoint(ContBB);
}

RValue CodeGenFunction::EmitAtomicExpr(AtomicExpr *E) {
  QualType AtomicTy = E->getPtr()->getType()->getPointeeType();
  QualType MemTy = AtomicTy;
  if (const AtomicType *AT = AtomicTy->getAs<AtomicType>())
    MemTy = AT->getValueType();
  llvm::Value *IsWeak = nullptr, *OrderFail = nullptr;

  Address Val1 = Address::invalid();
  Address Val2 = Address::invalid();
  Address Dest = Address::invalid();
  Address Ptr = EmitPointerWithAlignment(E->getPtr());

  if (E->getOp() == AtomicExpr::AO__c11_atomic_init ||
      E->getOp() == AtomicExpr::AO__opencl_atomic_init) {
    LValue lvalue = MakeAddrLValue(Ptr, AtomicTy);
    EmitAtomicInit(E->getVal1(), lvalue);
    return RValue::get(nullptr);
  }

  auto TInfo = getContext().getTypeInfoInChars(AtomicTy);
  uint64_t Size = TInfo.Width.getQuantity();
  unsigned MaxInlineWidthInBits = getTarget().getMaxAtomicInlineWidth();

  CharUnits MaxInlineWidth =
      getContext().toCharUnitsFromBits(MaxInlineWidthInBits);
  DiagnosticsEngine &Diags = CGM.getDiags();
  bool Misaligned = (Ptr.getAlignment() % TInfo.Width) != 0;
  bool Oversized = getContext().toBits(TInfo.Width) > MaxInlineWidthInBits;
  if (Misaligned) {
    Diags.Report(E->getBeginLoc(), diag::warn_atomic_op_misaligned)
        << (int)TInfo.Width.getQuantity()
        << (int)Ptr.getAlignment().getQuantity();
  }
  if (Oversized) {
    Diags.Report(E->getBeginLoc(), diag::warn_atomic_op_oversized)
        << (int)TInfo.Width.getQuantity() << (int)MaxInlineWidth.getQuantity();
  }

  llvm::Value *Order = EmitScalarExpr(E->getOrder());
  llvm::Value *Scope =
      E->getScopeModel() ? EmitScalarExpr(E->getScope()) : nullptr;
  bool ShouldCastToIntPtrTy = true;

  switch (E->getOp()) {
  case AtomicExpr::AO__c11_atomic_init:
  case AtomicExpr::AO__opencl_atomic_init:
    llvm_unreachable("Already handled above with EmitAtomicInit!");

  case AtomicExpr::AO__atomic_load_n:
  case AtomicExpr::AO__scoped_atomic_load_n:
  case AtomicExpr::AO__c11_atomic_load:
  case AtomicExpr::AO__opencl_atomic_load:
  case AtomicExpr::AO__hip_atomic_load:
    break;

  case AtomicExpr::AO__atomic_load:
  case AtomicExpr::AO__scoped_atomic_load:
    Dest = EmitPointerWithAlignment(E->getVal1());
    break;

  case AtomicExpr::AO__atomic_store:
  case AtomicExpr::AO__scoped_atomic_store:
    Val1 = EmitPointerWithAlignment(E->getVal1());
    break;

  case AtomicExpr::AO__atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_exchange:
    Val1 = EmitPointerWithAlignment(E->getVal1());
    Dest = EmitPointerWithAlignment(E->getVal2());
    break;

  case AtomicExpr::AO__atomic_compare_exchange:
  case AtomicExpr::AO__atomic_compare_exchange_n:
  case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
  case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
  case AtomicExpr::AO__hip_atomic_compare_exchange_weak:
  case AtomicExpr::AO__hip_atomic_compare_exchange_strong:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_weak:
  case AtomicExpr::AO__opencl_atomic_compare_exchange_strong:
  case AtomicExpr::AO__scoped_atomic_compare_exchange:
  case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
    Val1 = EmitPointerWithAlignment(E->getVal1());
    if (E->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange)
      Val2 = EmitPointerWithAlignment(E->getVal2());
    else
      Val2 = EmitValToTemp(*this, E->getVal2());
    OrderFail = EmitScalarExpr(E->getOrderFail());
    if (E->getOp() == AtomicExpr::AO__atomic_compare_exchange_n ||
        E->getOp() == AtomicExpr::AO__atomic_compare_exchange ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange_n ||
        E->getOp() == AtomicExpr::AO__scoped_atomic_compare_exchange)
      IsWeak = EmitScalarExpr(E->getWeak());
    break;

  case AtomicExpr::AO__c11_atomic_fetch_add:
  case AtomicExpr::AO__c11_atomic_fetch_sub:
  case AtomicExpr::AO__hip_atomic_fetch_add:
  case AtomicExpr::AO__hip_atomic_fetch_sub:
  case AtomicExpr::AO__opencl_atomic_fetch_add:
  case AtomicExpr::AO__opencl_atomic_fetch_sub:
    if (MemTy->isPointerType()) {
      // For pointer arithmetic, we're required to do a bit of math:
      // adding 1 to an int* is not the same as adding 1 to a uintptr_t.
      // ... but only for the C11 builtins. The GNU builtins expect the
      // user to multiply by sizeof(T).
      QualType Val1Ty = E->getVal1()->getType();
      llvm::Value *Val1Scalar = EmitScalarExpr(E->getVal1());
      CharUnits PointeeIncAmt =
          getContext().getTypeSizeInChars(MemTy->getPointeeType());
      Val1Scalar = Builder.CreateMul(Val1Scalar, CGM.getSize(PointeeIncAmt));
      auto Temp = CreateMemTemp(Val1Ty, ".atomictmp");
      Val1 = Temp;
      EmitStoreOfScalar(Val1Scalar, MakeAddrLValue(Temp, Val1Ty));
      break;
    }
    [[fallthrough]];
  case AtomicExpr::AO__atomic_fetch_add:
  case AtomicExpr::AO__atomic_fetch_max:
  case AtomicExpr::AO__atomic_fetch_min:
  case AtomicExpr::AO__atomic_fetch_sub:
  case AtomicExpr::AO__atomic_add_fetch:
  case AtomicExpr::AO__atomic_max_fetch:
  case AtomicExpr::AO__atomic_min_fetch:
  case AtomicExpr::AO__atomic_sub_fetch:
  case AtomicExpr::AO__c11_atomic_fetch_max:
  case AtomicExpr::AO__c11_atomic_fetch_min:
  case AtomicExpr::AO__opencl_atomic_fetch_max:
  case AtomicExpr::AO__opencl_atomic_fetch_min:
  case AtomicExpr::AO__hip_atomic_fetch_max:
  case AtomicExpr::AO__hip_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_add:
  case AtomicExpr::AO__scoped_atomic_fetch_max:
  case AtomicExpr::AO__scoped_atomic_fetch_min:
  case AtomicExpr::AO__scoped_atomic_fetch_sub:
  case AtomicExpr::AO__scoped_atomic_add_fetch:
  case AtomicExpr::AO__scoped_atomic_max_fetch:
  case AtomicExpr::AO__scoped_atomic_min_fetch:
  case AtomicExpr::AO__scoped_atomic_sub_fetch:
    ShouldCastToIntPtrTy = !MemTy->isFloatingType();
    [[fallthrough]];

  case AtomicExpr::AO__atomic_fetch_and:
  case AtomicExpr::AO__atomic_fetch_nand:
  case AtomicExpr::AO__atomic_fetch_or:
  case AtomicExpr::AO__atomic_fetch_xor:
  case AtomicExpr::AO__atomic_and_fetch:
  case AtomicExpr::AO__atomic_nand_fetch:
  case AtomicExpr::AO__atomic_or_fetch:
  case AtomicExpr::AO__atomic_xor_fetch:
  case AtomicExpr::AO__atomic_store_n:
  case AtomicExpr::AO__atomic_exchange_n:
  case AtomicExpr::AO__c11_atomic_fetch_and:
  case AtomicExpr::AO__c11_atomic_fetch_nand:
  case AtomicExpr::AO__c11_atomic_fetch_or:
  case AtomicExpr::AO__c11_atomic_fetch_xor:
  case AtomicExpr::AO__c11_atomic_store:
  case AtomicExpr::AO__c11_atomic_exchange:
  case AtomicExpr::AO__hip_atomic_fetch_and:
  case AtomicExpr::AO__hip_atomic_fetch_or:
  case AtomicExpr::AO__hip_atomic_fetch_xor:
  case AtomicExpr::AO__hip_atomic_store:
  case AtomicExpr::AO__hip_atomic_exchange:
  case AtomicExpr::AO__opencl_atomic_fetch_and:
  case AtomicExpr::AO__opencl_atomic_fetch_or:
  case AtomicExpr::AO__opencl_atomic_fetch_xor:
  case AtomicExpr::AO__opencl_atomic_store:
  case AtomicExpr::AO__opencl_atomic_exchange:
  case AtomicExpr::AO__scoped_atomic_fetch_and:
  case AtomicExpr::AO__scoped_atomic_fetch_nand:
  case AtomicExpr::AO__scoped_atomic_fetch_or:
  case AtomicExpr::AO__scoped_atomic_fetch_xor:
  case AtomicExpr::AO__scoped_atomic_and_fetch:
  case AtomicExpr::AO__scoped_atomic_nand_fetch:
  case AtomicExpr::AO__scoped_atomic_or_fetch:
  case AtomicExpr::AO__scoped_atomic_xor_fetch:
  case AtomicExpr::AO__scoped_atomic_store_n:
  case AtomicExpr::AO__scoped_atomic_exchange_n:
    Val1 = EmitValToTemp(*this, E->getVal1());
    break;
  }

  QualType RValTy = E->getType().getUnqualifiedType();

  // The inlined atomics only function on iN types, where N is a power of 2. We
  // need to make sure (via temporaries if necessary) that all incoming values
  // are compatible.
  LValue AtomicVal = MakeAddrLValue(Ptr, AtomicTy);
  AtomicInfo Atomics(*this, AtomicVal);

  if (ShouldCastToIntPtrTy) {
    Ptr = Atomics.castToAtomicIntPointer(Ptr);
    if (Val1.isValid())
      Val1 = Atomics.convertToAtomicIntPointer(Val1);
    if (Val2.isValid())
      Val2 = Atomics.convertToAtomicIntPointer(Val2);
  }
  if (Dest.isValid()) {
    if (ShouldCastToIntPtrTy)
      Dest = Atomics.castToAtomicIntPointer(Dest);
  } else if (E->isCmpXChg())
    Dest = CreateMemTemp(RValTy, "cmpxchg.bool");
  else if (!RValTy->isVoidType()) {
    Dest = Atomics.CreateTempAlloca();
    if (ShouldCastToIntPtrTy)
      Dest = Atomics.castToAtomicIntPointer(Dest);
  }

  bool PowerOf2Size = (Size & (Size - 1)) == 0;
  bool UseLibcall = !PowerOf2Size || (Size > 16);

  // For atomics larger than 16 bytes, emit a libcall from the frontend. This
  // avoids the overhead of dealing with excessively-large value types in IR.
  // Non-power-of-2 values also lower to libcall here, as they are not currently
  // permitted in IR instructions (although that constraint could be relaxed in
  // the future). For other cases where a libcall is required on a given
  // platform, we let the backend handle it (this includes handling for all of
  // the size-optimized libcall variants, which are only valid up to 16 bytes.)
  //
  // See: https://llvm.org/docs/Atomics.html#libcalls-atomic
  if (UseLibcall) {
    CallArgList Args;
    // For non-optimized library calls, the size is the first parameter.
    Args.add(RValue::get(llvm::ConstantInt::get(SizeTy, Size)),
             getContext().getSizeType());

    // The atomic address is the second parameter.
    // The OpenCL atomic library functions only accept pointer arguments to
    // generic address space.
    auto CastToGenericAddrSpace = [&](llvm::Value *V, QualType PT) {
      if (!E->isOpenCL())
        return V;
      auto AS = PT->castAs<PointerType>()->getPointeeType().getAddressSpace();
      if (AS == LangAS::opencl_generic)
        return V;
      auto DestAS = getContext().getTargetAddressSpace(LangAS::opencl_generic);
      auto *DestType = llvm::PointerType::get(getLLVMContext(), DestAS);

      return getTargetHooks().performAddrSpaceCast(
          *this, V, AS, LangAS::opencl_generic, DestType, false);
    };

    Args.add(RValue::get(CastToGenericAddrSpace(Ptr.emitRawPointer(*this),
                                                E->getPtr()->getType())),
             getContext().VoidPtrTy);

    // The next 1-3 parameters are op-dependent.
    std::string LibCallName;
    QualType RetTy;
    bool HaveRetTy = false;
    switch (E->getOp()) {
    case AtomicExpr::AO__c11_atomic_init:
    case AtomicExpr::AO__opencl_atomic_init:
      llvm_unreachable("Already handled!");

    // There is only one libcall for compare an exchange, because there is no
    // optimisation benefit possible from a libcall version of a weak compare
    // and exchange.
    // bool __atomic_compare_exchange(size_t size, void *mem, void *expected,
    //                                void *desired, int success, int failure)
    case AtomicExpr::AO__atomic_compare_exchange:
    case AtomicExpr::AO__atomic_compare_exchange_n:
    case AtomicExpr::AO__c11_atomic_compare_exchange_weak:
    case AtomicExpr::AO__c11_atomic_compare_exchange_strong:
    case AtomicExpr::AO__hip_atomic_compare_exchange_weak:
    case AtomicExpr::AO__hip_atomic_compare_exchange_strong:
    case AtomicExpr::AO__opencl_atomic_compare_exchange_weak:
    case AtomicExpr::AO__opencl_atomic_compare_exchange_strong:
    case AtomicExpr::AO__scoped_atomic_compare_exchange:
    case AtomicExpr::AO__scoped_atomic_compare_exchange_n:
      LibCallName = "__atomic_compare_exchange";
      RetTy = getContext().BoolTy;
      HaveRetTy = true;
      Args.add(RValue::get(CastToGenericAddrSpace(Val1.emitRawPointer(*this),
                                                  E->getVal1()->getType())),
               getContext().VoidPtrTy);
      Args.add(RValue::get(CastToGenericAddrSpace(Val2.emitRawPointer(*this),
                                                  E->getVal2()->getType())),
               getContext().VoidPtrTy);
      Args.add(RValue::get(Order), getContext().IntTy);
      Order = OrderFail;
      break;
    // void __atomic_exchange(size_t size, void *mem, void *val, void *return,
    //                        int order)
    case AtomicExpr::AO__atomic_exchange:
    case AtomicExpr::AO__atomic_exchange_n:
    case AtomicExpr::AO__c11_atomic_exchange:
    case AtomicExpr::AO__hip_atomic_exchange:
    case AtomicExpr::AO__opencl_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_exchange:
    case AtomicExpr::AO__scoped_atomic_exchange_n:
      LibCallName = "__atomic_exchange";
      Args.add(RValue::get(CastToGenericAddrSpace(Val1.emitRawPointer(*this),
                                                  E->getVal1()->getType())),
               getContext().VoidPtrTy);
      break;
    // void __atomic_store(size_t size, void *mem, void *val, int order)
    case AtomicExpr::AO__atomic_store:
    case AtomicExpr::AO__atomic_store_n:
    case AtomicExpr::AO__c11_atomic_store:
    case AtomicExpr::AO__hip_atomic_store:
    case AtomicExpr::AO__opencl_atomic_store:
    case AtomicExpr::AO__scoped_atomic_store:
    case AtomicExpr::AO__scoped_atomic_store_n:
      LibCallName = "__atomic_store";
      RetTy = getContext().VoidTy;
      HaveRetTy = true;
      Args.add(RValue::get(CastToGenericAddrSpace(Val1.emitRawPointer(*this),
                                                  E->getVal1()->getType())),
               getContext().VoidPtrTy);
      break;
    // void __atomic_load(size_t size, void *mem, void *return, int order)
    case AtomicExpr::AO__atomic_load:
    case AtomicExpr::AO__atomic_load_n:
    case AtomicExpr::AO__c11_atomic_load:
    case AtomicExpr::AO__hip_atomic_load:
    case AtomicExpr::AO__opencl_atomic_load:
    case AtomicExpr::AO__scoped_atomic_load:
    case AtomicExpr::AO__scoped_atomic_load_n:
      LibCallName = "__atomic_load";
      break;
    case AtomicExpr::AO__atomic_add_fetch:
    case AtomicExpr::AO__scoped_atomic_add_fetch:
    case AtomicExpr::AO__atomic_fetch_add:
    case AtomicExpr::AO__c11_atomic_fetch_add:
    case AtomicExpr::AO__hip_atomic_fetch_add:
    case AtomicExpr::AO__opencl_atomic_fetch_add:
    case AtomicExpr::AO__scoped_atomic_fetch_add:
    case AtomicExpr::AO__atomic_and_fetch:
    case AtomicExpr::AO__scoped_atomic_and_fetch:
    case AtomicExpr::AO__atomic_fetch_and:
    case AtomicExpr::AO__c11_atomic_fetch_and:
    case AtomicExpr::AO__hip_atomic_fetch_and:
    case AtomicExpr::AO__opencl_atomic_fetch_and:
    case AtomicExpr::AO__scoped_atomic_fetch_and:
    case AtomicExpr::AO__atomic_or_fetch:
    case AtomicExpr::AO__scoped_atomic_or_fetch:
    case AtomicExpr::AO__atomic_fetch_or:
    case AtomicExpr::AO__c11_atomic_fetch_or:
    case AtomicExpr::AO__hip_atomic_fetch_or:
    case AtomicExpr::AO__opencl_atomic_fetch_or:
    case AtomicExpr::AO__scoped_atomic_fetch_or:
    case AtomicExpr::AO__atomic_sub_fetch:
    case AtomicExpr::AO__scoped_atomic_sub_fetch:
    case AtomicExpr::AO__atomic_fetch_sub:
    case AtomicExpr::AO__c11_atomic_fetch_sub:
    case AtomicExpr::AO__hip_atomic_fetch_sub:
    case AtomicExpr::AO__opencl_atomic_fetch_sub:
    case AtomicExpr::AO__scoped_atomic_fetch_sub:
    case AtomicExpr::AO__atomic_xor_fetch:
    case AtomicExpr::AO__scoped_atomic_xor_fetch:
    case AtomicExpr::AO__atomic_fetch_xor:
    case AtomicExpr::AO__c11_atomic_fetch_xor:
    case AtomicExpr::AO__hip_atomic_fetch_xor:
    case AtomicExpr::AO__opencl_atomic_fetch_xor:
    case AtomicExpr::AO__scoped_atomic_fetch_xor:
    case AtomicExpr::AO__atomic_nand_fetch:
    case AtomicExpr::AO__atomic_fetch_nand:
    case AtomicExpr::AO__c11_atomic_fetch_nand:
    case AtomicExpr::AO__scoped_atomic_fetch_nand:
    case AtomicExpr::AO__scoped_atomic_nand_fetch:
    case AtomicExpr::AO__atomic_min_fetch:
    case AtomicExpr::AO__atomic_fetch_min:
    case AtomicExpr::AO__c11_atomic_fetch_min:
    case AtomicExpr::AO__hip_atomic_fetch_min:
    case AtomicExpr::AO__opencl_atomic_fetch_min:
    case AtomicExpr::AO__scoped_atomic_fetch_min:
    case AtomicExpr::AO__scoped_atomic_min_fetch:
    case AtomicExpr::AO__atomic_max_fetch:
    case AtomicExpr::AO__atomic_fetch_max:
    case AtomicExpr::AO__c11_atomic_fetch_max:
    case AtomicExpr::AO__hip_atomic_fetch_max:
    case AtomicExpr::AO__opencl_atomic_fetch_max:
    case AtomicExpr::AO__scoped_atomic_fetch_max:
    case AtomicExpr::AO__scoped_atomic_max_fetch:
      llvm_unreachable("Integral atomic operations always become atomicrmw!");
    }

    if (E->isOpenCL()) {
      LibCallName =
          std::string("__opencl") + StringRef(LibCallName).drop_front(1).str();
    }
    // By default, assume we return a value of the atomic type.
    if (!HaveRetTy) {
      // Value is returned through parameter before the order.
      RetTy = getContext().VoidTy;
      Args.add(RValue::get(
                   CastToGenericAddrSpace(Dest.emitRawPointer(*this), RetTy)),
               getContext().VoidPtrTy);
    }
    // Order is always the last parameter.
    Args.add(RValue::get(Order),
             getContext().IntTy);
    if (E->isOpenCL())
      Args.add(RValue::get(Scope), getContext().IntTy);

    RValue Res = emitAtomicLibcall(*this, LibCallName, RetTy, Args);
    // The value is returned directly from the libcall.
    if (E->isCmpXChg())
      return Res;

    if (RValTy->isVoidType())
      return RValue::get(nullptr);

    return convertTempToRValue(Dest.withElementType(ConvertTypeForMem(RValTy)),
                               RValTy, E->getExprLoc());
  }

  bool IsStore = E->getOp() == AtomicExpr::AO__c11_atomic_store ||
                 E->getOp() == AtomicExpr::AO__opencl_atomic_store ||
                 E->getOp() == AtomicExpr::AO__hip_atomic_store ||
                 E->getOp() == AtomicExpr::AO__atomic_store ||
                 E->getOp() == AtomicExpr::AO__atomic_store_n ||
                 E->getOp() == AtomicExpr::AO__scoped_atomic_store ||
                 E->getOp() == AtomicExpr::AO__scoped_atomic_store_n;
  bool IsLoad = E->getOp() == AtomicExpr::AO__c11_atomic_load ||
                E->getOp() == AtomicExpr::AO__opencl_atomic_load ||
                E->getOp() == AtomicExpr::AO__hip_atomic_load ||
                E->getOp() == AtomicExpr::AO__atomic_load ||
                E->getOp() == AtomicExpr::AO__atomic_load_n ||
                E->getOp() == AtomicExpr::AO__scoped_atomic_load ||
                E->getOp() == AtomicExpr::AO__scoped_atomic_load_n;

  if (isa<llvm::ConstantInt>(Order)) {
    auto ord = cast<llvm::ConstantInt>(Order)->getZExtValue();
    // We should not ever get to a case where the ordering isn't a valid C ABI
    // value, but it's hard to enforce that in general.
    if (llvm::isValidAtomicOrderingCABI(ord))
      switch ((llvm::AtomicOrderingCABI)ord) {
      case llvm::AtomicOrderingCABI::relaxed:
        EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                     llvm::AtomicOrdering::Monotonic, Scope);
        break;
      case llvm::AtomicOrderingCABI::consume:
      case llvm::AtomicOrderingCABI::acquire:
        if (IsStore)
          break; // Avoid crashing on code with undefined behavior
        EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                     llvm::AtomicOrdering::Acquire, Scope);
        break;
      case llvm::AtomicOrderingCABI::release:
        if (IsLoad)
          break; // Avoid crashing on code with undefined behavior
        EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                     llvm::AtomicOrdering::Release, Scope);
        break;
      case llvm::AtomicOrderingCABI::acq_rel:
        if (IsLoad || IsStore)
          break; // Avoid crashing on code with undefined behavior
        EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                     llvm::AtomicOrdering::AcquireRelease, Scope);
        break;
      case llvm::AtomicOrderingCABI::seq_cst:
        EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                     llvm::AtomicOrdering::SequentiallyConsistent, Scope);
        break;
      }
    if (RValTy->isVoidType())
      return RValue::get(nullptr);

    return convertTempToRValue(Dest.withElementType(ConvertTypeForMem(RValTy)),
                               RValTy, E->getExprLoc());
  }

  // Long case, when Order isn't obviously constant.

  // Create all the relevant BB's
  llvm::BasicBlock *MonotonicBB = nullptr, *AcquireBB = nullptr,
                   *ReleaseBB = nullptr, *AcqRelBB = nullptr,
                   *SeqCstBB = nullptr;
  MonotonicBB = createBasicBlock("monotonic", CurFn);
  if (!IsStore)
    AcquireBB = createBasicBlock("acquire", CurFn);
  if (!IsLoad)
    ReleaseBB = createBasicBlock("release", CurFn);
  if (!IsLoad && !IsStore)
    AcqRelBB = createBasicBlock("acqrel", CurFn);
  SeqCstBB = createBasicBlock("seqcst", CurFn);
  llvm::BasicBlock *ContBB = createBasicBlock("atomic.continue", CurFn);

  // Create the switch for the split
  // MonotonicBB is arbitrarily chosen as the default case; in practice, this
  // doesn't matter unless someone is crazy enough to use something that
  // doesn't fold to a constant for the ordering.
  Order = Builder.CreateIntCast(Order, Builder.getInt32Ty(), false);
  llvm::SwitchInst *SI = Builder.CreateSwitch(Order, MonotonicBB);

  // Emit all the different atomics
  Builder.SetInsertPoint(MonotonicBB);
  EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
               llvm::AtomicOrdering::Monotonic, Scope);
  Builder.CreateBr(ContBB);
  if (!IsStore) {
    Builder.SetInsertPoint(AcquireBB);
    EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                 llvm::AtomicOrdering::Acquire, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::consume),
                AcquireBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::acquire),
                AcquireBB);
  }
  if (!IsLoad) {
    Builder.SetInsertPoint(ReleaseBB);
    EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                 llvm::AtomicOrdering::Release, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::release),
                ReleaseBB);
  }
  if (!IsLoad && !IsStore) {
    Builder.SetInsertPoint(AcqRelBB);
    EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
                 llvm::AtomicOrdering::AcquireRelease, Scope);
    Builder.CreateBr(ContBB);
    SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::acq_rel),
                AcqRelBB);
  }
  Builder.SetInsertPoint(SeqCstBB);
  EmitAtomicOp(*this, E, Dest, Ptr, Val1, Val2, IsWeak, OrderFail, Size,
               llvm::AtomicOrdering::SequentiallyConsistent, Scope);
  Builder.CreateBr(ContBB);
  SI->addCase(Builder.getInt32((int)llvm::AtomicOrderingCABI::seq_cst),
              SeqCstBB);

  // Cleanup and return
  Builder.SetInsertPoint(ContBB);
  if (RValTy->isVoidType())
    return RValue::get(nullptr);

  assert(Atomics.getValueSizeInBits() <= Atomics.getAtomicSizeInBits());
  return convertTempToRValue(Dest.withElementType(ConvertTypeForMem(RValTy)),
                             RValTy, E->getExprLoc());
}

Address AtomicInfo::castToAtomicIntPointer(Address addr) const {
  llvm::IntegerType *ty =
    llvm::IntegerType::get(CGF.getLLVMContext(), AtomicSizeInBits);
  return addr.withElementType(ty);
}

Address AtomicInfo::convertToAtomicIntPointer(Address Addr) const {
  llvm::Type *Ty = Addr.getElementType();
  uint64_t SourceSizeInBits = CGF.CGM.getDataLayout().getTypeSizeInBits(Ty);
  if (SourceSizeInBits != AtomicSizeInBits) {
    Address Tmp = CreateTempAlloca();
    CGF.Builder.CreateMemCpy(Tmp, Addr,
                             std::min(AtomicSizeInBits, SourceSizeInBits) / 8);
    Addr = Tmp;
  }

  return castToAtomicIntPointer(Addr);
}

RValue AtomicInfo::convertAtomicTempToRValue(Address addr,
                                             AggValueSlot resultSlot,
                                             SourceLocation loc,
                                             bool asValue) const {
  if (LVal.isSimple()) {
    if (EvaluationKind == TEK_Aggregate)
      return resultSlot.asRValue();

    // Drill into the padding structure if we have one.
    if (hasPadding())
      addr = CGF.Builder.CreateStructGEP(addr, 0);

    // Otherwise, just convert the temporary to an r-value using the
    // normal conversion routine.
    return CGF.convertTempToRValue(addr, getValueType(), loc);
  }
  if (!asValue)
    // Get RValue from temp memory as atomic for non-simple lvalues
    return RValue::get(CGF.Builder.CreateLoad(addr));
  if (LVal.isBitField())
    return CGF.EmitLoadOfBitfieldLValue(
        LValue::MakeBitfield(addr, LVal.getBitFieldInfo(), LVal.getType(),
                             LVal.getBaseInfo(), TBAAAccessInfo()), loc);
  if (LVal.isVectorElt())
    return CGF.EmitLoadOfLValue(
        LValue::MakeVectorElt(addr, LVal.getVectorIdx(), LVal.getType(),
                              LVal.getBaseInfo(), TBAAAccessInfo()), loc);
  assert(LVal.isExtVectorElt());
  return CGF.EmitLoadOfExtVectorElementLValue(LValue::MakeExtVectorElt(
      addr, LVal.getExtVectorElts(), LVal.getType(),
      LVal.getBaseInfo(), TBAAAccessInfo()));
}

/// Return true if \param ValTy is a type that should be casted to integer
/// around the atomic memory operation. If \param CmpXchg is true, then the
/// cast of a floating point type is made as that instruction can not have
/// floating point operands.  TODO: Allow compare-and-exchange and FP - see
/// comment in AtomicExpandPass.cpp.
static bool shouldCastToInt(llvm::Type *ValTy, bool CmpXchg) {
  if (ValTy->isFloatingPointTy())
    return ValTy->isX86_FP80Ty() || CmpXchg;
  return !ValTy->isIntegerTy() && !ValTy->isPointerTy();
}

RValue AtomicInfo::ConvertToValueOrAtomic(llvm::Value *Val,
                                          AggValueSlot ResultSlot,
                                          SourceLocation Loc, bool AsValue,
                                          bool CmpXchg) const {
  // Try not to in some easy cases.
  assert((Val->getType()->isIntegerTy() || Val->getType()->isPointerTy() ||
          Val->getType()->isIEEELikeFPTy()) &&
         "Expected integer, pointer or floating point value when converting "
         "result.");
  if (getEvaluationKind() == TEK_Scalar &&
      (((!LVal.isBitField() ||
         LVal.getBitFieldInfo().Size == ValueSizeInBits) &&
        !hasPadding()) ||
       !AsValue)) {
    auto *ValTy = AsValue
                      ? CGF.ConvertTypeForMem(ValueTy)
                      : getAtomicAddress().getElementType();
    if (!shouldCastToInt(ValTy, CmpXchg)) {
      assert((!ValTy->isIntegerTy() || Val->getType() == ValTy) &&
             "Different integer types.");
      return RValue::get(CGF.EmitFromMemory(Val, ValueTy));
    }
    if (llvm::CastInst::isBitCastable(Val->getType(), ValTy))
      return RValue::get(CGF.Builder.CreateBitCast(Val, ValTy));
  }

  // Create a temporary.  This needs to be big enough to hold the
  // atomic integer.
  Address Temp = Address::invalid();
  bool TempIsVolatile = false;
  if (AsValue && getEvaluationKind() == TEK_Aggregate) {
    assert(!ResultSlot.isIgnored());
    Temp = ResultSlot.getAddress();
    TempIsVolatile = ResultSlot.isVolatile();
  } else {
    Temp = CreateTempAlloca();
  }

  // Slam the integer into the temporary.
  Address CastTemp = castToAtomicIntPointer(Temp);
  CGF.Builder.CreateStore(Val, CastTemp)->setVolatile(TempIsVolatile);

  return convertAtomicTempToRValue(Temp, ResultSlot, Loc, AsValue);
}

void AtomicInfo::EmitAtomicLoadLibcall(llvm::Value *AddForLoaded,
                                       llvm::AtomicOrdering AO, bool) {
  // void __atomic_load(size_t size, void *mem, void *return, int order);
  CallArgList Args;
  Args.add(RValue::get(getAtomicSizeValue()), CGF.getContext().getSizeType());
  Args.add(RValue::get(getAtomicPointer()), CGF.getContext().VoidPtrTy);
  Args.add(RValue::get(AddForLoaded), CGF.getContext().VoidPtrTy);
  Args.add(
      RValue::get(llvm::ConstantInt::get(CGF.IntTy, (int)llvm::toCABI(AO))),
      CGF.getContext().IntTy);
  emitAtomicLibcall(CGF, "__atomic_load", CGF.getContext().VoidTy, Args);
}

llvm::Value *AtomicInfo::EmitAtomicLoadOp(llvm::AtomicOrdering AO,
                                          bool IsVolatile, bool CmpXchg) {
  // Okay, we're doing this natively.
  Address Addr = getAtomicAddress();
  if (shouldCastToInt(Addr.getElementType(), CmpXchg))
    Addr = castToAtomicIntPointer(Addr);
  llvm::LoadInst *Load = CGF.Builder.CreateLoad(Addr, "atomic-load");
  Load->setAtomic(AO);

  // Other decoration.
  if (IsVolatile)
    Load->setVolatile(true);
  CGF.CGM.DecorateInstructionWithTBAA(Load, LVal.getTBAAInfo());
  return Load;
}

/// An LValue is a candidate for having its loads and stores be made atomic if
/// we are operating under /volatile:ms *and* the LValue itself is volatile and
/// performing such an operation can be performed without a libcall.
bool CodeGenFunction::LValueIsSuitableForInlineAtomic(LValue LV) {
  if (!CGM.getLangOpts().MSVolatile) return false;
  AtomicInfo AI(*this, LV);
  bool IsVolatile = LV.isVolatile() || hasVolatileMember(LV.getType());
  // An atomic is inline if we don't need to use a libcall.
  bool AtomicIsInline = !AI.shouldUseLibcall();
  // MSVC doesn't seem to do this for types wider than a pointer.
  if (getContext().getTypeSize(LV.getType()) >
      getContext().getTypeSize(getContext().getIntPtrType()))
    return false;
  return IsVolatile && AtomicIsInline;
}

RValue CodeGenFunction::EmitAtomicLoad(LValue LV, SourceLocation SL,
                                       AggValueSlot Slot) {
  llvm::AtomicOrdering AO;
  bool IsVolatile = LV.isVolatileQualified();
  if (LV.getType()->isAtomicType()) {
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
  } else {
    AO = llvm::AtomicOrdering::Acquire;
    IsVolatile = true;
  }
  return EmitAtomicLoad(LV, SL, AO, IsVolatile, Slot);
}

RValue AtomicInfo::EmitAtomicLoad(AggValueSlot ResultSlot, SourceLocation Loc,
                                  bool AsValue, llvm::AtomicOrdering AO,
                                  bool IsVolatile) {
  // Check whether we should use a library call.
  if (shouldUseLibcall()) {
    Address TempAddr = Address::invalid();
    if (LVal.isSimple() && !ResultSlot.isIgnored()) {
      assert(getEvaluationKind() == TEK_Aggregate);
      TempAddr = ResultSlot.getAddress();
    } else
      TempAddr = CreateTempAlloca();

    EmitAtomicLoadLibcall(TempAddr.emitRawPointer(CGF), AO, IsVolatile);

    // Okay, turn that back into the original value or whole atomic (for
    // non-simple lvalues) type.
    return convertAtomicTempToRValue(TempAddr, ResultSlot, Loc, AsValue);
  }

  // Okay, we're doing this natively.
  auto *Load = EmitAtomicLoadOp(AO, IsVolatile);

  // If we're ignoring an aggregate return, don't do anything.
  if (getEvaluationKind() == TEK_Aggregate && ResultSlot.isIgnored())
    return RValue::getAggregate(Address::invalid(), false);

  // Okay, turn that back into the original value or atomic (for non-simple
  // lvalues) type.
  return ConvertToValueOrAtomic(Load, ResultSlot, Loc, AsValue);
}

/// Emit a load from an l-value of atomic type.  Note that the r-value
/// we produce is an r-value of the atomic *value* type.
RValue CodeGenFunction::EmitAtomicLoad(LValue src, SourceLocation loc,
                                       llvm::AtomicOrdering AO, bool IsVolatile,
                                       AggValueSlot resultSlot) {
  AtomicInfo Atomics(*this, src);
  return Atomics.EmitAtomicLoad(resultSlot, loc, /*AsValue=*/true, AO,
                                IsVolatile);
}

/// Copy an r-value into memory as part of storing to an atomic type.
/// This needs to create a bit-pattern suitable for atomic operations.
void AtomicInfo::emitCopyIntoMemory(RValue rvalue) const {
  assert(LVal.isSimple());
  // If we have an r-value, the rvalue should be of the atomic type,
  // which means that the caller is responsible for having zeroed
  // any padding.  Just do an aggregate copy of that type.
  if (rvalue.isAggregate()) {
    LValue Dest = CGF.MakeAddrLValue(getAtomicAddress(), getAtomicType());
    LValue Src = CGF.MakeAddrLValue(rvalue.getAggregateAddress(),
                                    getAtomicType());
    bool IsVolatile = rvalue.isVolatileQualified() ||
                      LVal.isVolatileQualified();
    CGF.EmitAggregateCopy(Dest, Src, getAtomicType(),
                          AggValueSlot::DoesNotOverlap, IsVolatile);
    return;
  }

  // Okay, otherwise we're copying stuff.

  // Zero out the buffer if necessary.
  emitMemSetZeroIfNecessary();

  // Drill past the padding if present.
  LValue TempLVal = projectValue();

  // Okay, store the rvalue in.
  if (rvalue.isScalar()) {
    CGF.EmitStoreOfScalar(rvalue.getScalarVal(), TempLVal, /*init*/ true);
  } else {
    CGF.EmitStoreOfComplex(rvalue.getComplexVal(), TempLVal, /*init*/ true);
  }
}


/// Materialize an r-value into memory for the purposes of storing it
/// to an atomic type.
Address AtomicInfo::materializeRValue(RValue rvalue) const {
  // Aggregate r-values are already in memory, and EmitAtomicStore
  // requires them to be values of the atomic type.
  if (rvalue.isAggregate())
    return rvalue.getAggregateAddress();

  // Otherwise, make a temporary and materialize into it.
  LValue TempLV = CGF.MakeAddrLValue(CreateTempAlloca(), getAtomicType());
  AtomicInfo Atomics(CGF, TempLV);
  Atomics.emitCopyIntoMemory(rvalue);
  return TempLV.getAddress();
}

llvm::Value *AtomicInfo::getScalarRValValueOrNull(RValue RVal) const {
  if (RVal.isScalar() && (!hasPadding() || !LVal.isSimple()))
    return RVal.getScalarVal();
  return nullptr;
}

llvm::Value *AtomicInfo::convertRValueToInt(RValue RVal, bool CmpXchg) const {
  // If we've got a scalar value of the right size, try to avoid going
  // through memory. Floats get casted if needed by AtomicExpandPass.
  if (llvm::Value *Value = getScalarRValValueOrNull(RVal)) {
    if (!shouldCastToInt(Value->getType(), CmpXchg))
      return CGF.EmitToMemory(Value, ValueTy);
    else {
      llvm::IntegerType *InputIntTy = llvm::IntegerType::get(
          CGF.getLLVMContext(),
          LVal.isSimple() ? getValueSizeInBits() : getAtomicSizeInBits());
      if (llvm::BitCastInst::isBitCastable(Value->getType(), InputIntTy))
        return CGF.Builder.CreateBitCast(Value, InputIntTy);
    }
  }
  // Otherwise, we need to go through memory.
  // Put the r-value in memory.
  Address Addr = materializeRValue(RVal);

  // Cast the temporary to the atomic int type and pull a value out.
  Addr = castToAtomicIntPointer(Addr);
  return CGF.Builder.CreateLoad(Addr);
}

std::pair<llvm::Value *, llvm::Value *> AtomicInfo::EmitAtomicCompareExchangeOp(
    llvm::Value *ExpectedVal, llvm::Value *DesiredVal,
    llvm::AtomicOrdering Success, llvm::AtomicOrdering Failure, bool IsWeak) {
  // Do the atomic store.
  Address Addr = getAtomicAddressAsAtomicIntPointer();
  auto *Inst = CGF.Builder.CreateAtomicCmpXchg(Addr, ExpectedVal, DesiredVal,
                                               Success, Failure);
  // Other decoration.
  Inst->setVolatile(LVal.isVolatileQualified());
  Inst->setWeak(IsWeak);

  // Okay, turn that back into the original value type.
  auto *PreviousVal = CGF.Builder.CreateExtractValue(Inst, /*Idxs=*/0);
  auto *SuccessFailureVal = CGF.Builder.CreateExtractValue(Inst, /*Idxs=*/1);
  return std::make_pair(PreviousVal, SuccessFailureVal);
}

llvm::Value *
AtomicInfo::EmitAtomicCompareExchangeLibcall(llvm::Value *ExpectedAddr,
                                             llvm::Value *DesiredAddr,
                                             llvm::AtomicOrdering Success,
                                             llvm::AtomicOrdering Failure) {
  // bool __atomic_compare_exchange(size_t size, void *obj, void *expected,
  // void *desired, int success, int failure);
  CallArgList Args;
  Args.add(RValue::get(getAtomicSizeValue()), CGF.getContext().getSizeType());
  Args.add(RValue::get(getAtomicPointer()), CGF.getContext().VoidPtrTy);
  Args.add(RValue::get(ExpectedAddr), CGF.getContext().VoidPtrTy);
  Args.add(RValue::get(DesiredAddr), CGF.getContext().VoidPtrTy);
  Args.add(RValue::get(
               llvm::ConstantInt::get(CGF.IntTy, (int)llvm::toCABI(Success))),
           CGF.getContext().IntTy);
  Args.add(RValue::get(
               llvm::ConstantInt::get(CGF.IntTy, (int)llvm::toCABI(Failure))),
           CGF.getContext().IntTy);
  auto SuccessFailureRVal = emitAtomicLibcall(CGF, "__atomic_compare_exchange",
                                              CGF.getContext().BoolTy, Args);

  return SuccessFailureRVal.getScalarVal();
}

std::pair<RValue, llvm::Value *> AtomicInfo::EmitAtomicCompareExchange(
    RValue Expected, RValue Desired, llvm::AtomicOrdering Success,
    llvm::AtomicOrdering Failure, bool IsWeak) {
  // Check whether we should use a library call.
  if (shouldUseLibcall()) {
    // Produce a source address.
    Address ExpectedAddr = materializeRValue(Expected);
    llvm::Value *ExpectedPtr = ExpectedAddr.emitRawPointer(CGF);
    llvm::Value *DesiredPtr = materializeRValue(Desired).emitRawPointer(CGF);
    auto *Res = EmitAtomicCompareExchangeLibcall(ExpectedPtr, DesiredPtr,
                                                 Success, Failure);
    return std::make_pair(
        convertAtomicTempToRValue(ExpectedAddr, AggValueSlot::ignored(),
                                  SourceLocation(), /*AsValue=*/false),
        Res);
  }

  // If we've got a scalar value of the right size, try to avoid going
  // through memory.
  auto *ExpectedVal = convertRValueToInt(Expected, /*CmpXchg=*/true);
  auto *DesiredVal = convertRValueToInt(Desired, /*CmpXchg=*/true);
  auto Res = EmitAtomicCompareExchangeOp(ExpectedVal, DesiredVal, Success,
                                         Failure, IsWeak);
  return std::make_pair(
      ConvertToValueOrAtomic(Res.first, AggValueSlot::ignored(),
                             SourceLocation(), /*AsValue=*/false,
                             /*CmpXchg=*/true),
      Res.second);
}

static void
EmitAtomicUpdateValue(CodeGenFunction &CGF, AtomicInfo &Atomics, RValue OldRVal,
                      const llvm::function_ref<RValue(RValue)> &UpdateOp,
                      Address DesiredAddr) {
  RValue UpRVal;
  LValue AtomicLVal = Atomics.getAtomicLValue();
  LValue DesiredLVal;
  if (AtomicLVal.isSimple()) {
    UpRVal = OldRVal;
    DesiredLVal = CGF.MakeAddrLValue(DesiredAddr, AtomicLVal.getType());
  } else {
    // Build new lvalue for temp address.
    Address Ptr = Atomics.materializeRValue(OldRVal);
    LValue UpdateLVal;
    if (AtomicLVal.isBitField()) {
      UpdateLVal =
          LValue::MakeBitfield(Ptr, AtomicLVal.getBitFieldInfo(),
                               AtomicLVal.getType(),
                               AtomicLVal.getBaseInfo(),
                               AtomicLVal.getTBAAInfo());
      DesiredLVal =
          LValue::MakeBitfield(DesiredAddr, AtomicLVal.getBitFieldInfo(),
                               AtomicLVal.getType(), AtomicLVal.getBaseInfo(),
                               AtomicLVal.getTBAAInfo());
    } else if (AtomicLVal.isVectorElt()) {
      UpdateLVal = LValue::MakeVectorElt(Ptr, AtomicLVal.getVectorIdx(),
                                         AtomicLVal.getType(),
                                         AtomicLVal.getBaseInfo(),
                                         AtomicLVal.getTBAAInfo());
      DesiredLVal = LValue::MakeVectorElt(
          DesiredAddr, AtomicLVal.getVectorIdx(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
    } else {
      assert(AtomicLVal.isExtVectorElt());
      UpdateLVal = LValue::MakeExtVectorElt(Ptr, AtomicLVal.getExtVectorElts(),
                                            AtomicLVal.getType(),
                                            AtomicLVal.getBaseInfo(),
                                            AtomicLVal.getTBAAInfo());
      DesiredLVal = LValue::MakeExtVectorElt(
          DesiredAddr, AtomicLVal.getExtVectorElts(), AtomicLVal.getType(),
          AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
    }
    UpRVal = CGF.EmitLoadOfLValue(UpdateLVal, SourceLocation());
  }
  // Store new value in the corresponding memory area.
  RValue NewRVal = UpdateOp(UpRVal);
  if (NewRVal.isScalar()) {
    CGF.EmitStoreThroughLValue(NewRVal, DesiredLVal);
  } else {
    assert(NewRVal.isComplex());
    CGF.EmitStoreOfComplex(NewRVal.getComplexVal(), DesiredLVal,
                           /*isInit=*/false);
  }
}

void AtomicInfo::EmitAtomicUpdateLibcall(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  Address ExpectedAddr = CreateTempAlloca();

  EmitAtomicLoadLibcall(ExpectedAddr.emitRawPointer(CGF), AO, IsVolatile);
  auto *ContBB = CGF.createBasicBlock("atomic_cont");
  auto *ExitBB = CGF.createBasicBlock("atomic_exit");
  CGF.EmitBlock(ContBB);
  Address DesiredAddr = CreateTempAlloca();
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    auto *OldVal = CGF.Builder.CreateLoad(ExpectedAddr);
    CGF.Builder.CreateStore(OldVal, DesiredAddr);
  }
  auto OldRVal = convertAtomicTempToRValue(ExpectedAddr,
                                           AggValueSlot::ignored(),
                                           SourceLocation(), /*AsValue=*/false);
  EmitAtomicUpdateValue(CGF, *this, OldRVal, UpdateOp, DesiredAddr);
  llvm::Value *ExpectedPtr = ExpectedAddr.emitRawPointer(CGF);
  llvm::Value *DesiredPtr = DesiredAddr.emitRawPointer(CGF);
  auto *Res =
      EmitAtomicCompareExchangeLibcall(ExpectedPtr, DesiredPtr, AO, Failure);
  CGF.Builder.CreateCondBr(Res, ExitBB, ContBB);
  CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::EmitAtomicUpdateOp(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  // Do the atomic load.
  auto *OldVal = EmitAtomicLoadOp(Failure, IsVolatile, /*CmpXchg=*/true);
  // For non-simple lvalues perform compare-and-swap procedure.
  auto *ContBB = CGF.createBasicBlock("atomic_cont");
  auto *ExitBB = CGF.createBasicBlock("atomic_exit");
  auto *CurBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(ContBB);
  llvm::PHINode *PHI = CGF.Builder.CreatePHI(OldVal->getType(),
                                             /*NumReservedValues=*/2);
  PHI->addIncoming(OldVal, CurBB);
  Address NewAtomicAddr = CreateTempAlloca();
  Address NewAtomicIntAddr =
      shouldCastToInt(NewAtomicAddr.getElementType(), /*CmpXchg=*/true)
          ? castToAtomicIntPointer(NewAtomicAddr)
          : NewAtomicAddr;

  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    CGF.Builder.CreateStore(PHI, NewAtomicIntAddr);
  }
  auto OldRVal = ConvertToValueOrAtomic(PHI, AggValueSlot::ignored(),
                                        SourceLocation(), /*AsValue=*/false,
                                        /*CmpXchg=*/true);
  EmitAtomicUpdateValue(CGF, *this, OldRVal, UpdateOp, NewAtomicAddr);
  auto *DesiredVal = CGF.Builder.CreateLoad(NewAtomicIntAddr);
  // Try to write new value using cmpxchg operation.
  auto Res = EmitAtomicCompareExchangeOp(PHI, DesiredVal, AO, Failure);
  PHI->addIncoming(Res.first, CGF.Builder.GetInsertBlock());
  CGF.Builder.CreateCondBr(Res.second, ExitBB, ContBB);
  CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
}

static void EmitAtomicUpdateValue(CodeGenFunction &CGF, AtomicInfo &Atomics,
                                  RValue UpdateRVal, Address DesiredAddr) {
  LValue AtomicLVal = Atomics.getAtomicLValue();
  LValue DesiredLVal;
  // Build new lvalue for temp address.
  if (AtomicLVal.isBitField()) {
    DesiredLVal =
        LValue::MakeBitfield(DesiredAddr, AtomicLVal.getBitFieldInfo(),
                             AtomicLVal.getType(), AtomicLVal.getBaseInfo(),
                             AtomicLVal.getTBAAInfo());
  } else if (AtomicLVal.isVectorElt()) {
    DesiredLVal =
        LValue::MakeVectorElt(DesiredAddr, AtomicLVal.getVectorIdx(),
                              AtomicLVal.getType(), AtomicLVal.getBaseInfo(),
                              AtomicLVal.getTBAAInfo());
  } else {
    assert(AtomicLVal.isExtVectorElt());
    DesiredLVal = LValue::MakeExtVectorElt(
        DesiredAddr, AtomicLVal.getExtVectorElts(), AtomicLVal.getType(),
        AtomicLVal.getBaseInfo(), AtomicLVal.getTBAAInfo());
  }
  // Store new value in the corresponding memory area.
  assert(UpdateRVal.isScalar());
  CGF.EmitStoreThroughLValue(UpdateRVal, DesiredLVal);
}

void AtomicInfo::EmitAtomicUpdateLibcall(llvm::AtomicOrdering AO,
                                         RValue UpdateRVal, bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  Address ExpectedAddr = CreateTempAlloca();

  EmitAtomicLoadLibcall(ExpectedAddr.emitRawPointer(CGF), AO, IsVolatile);
  auto *ContBB = CGF.createBasicBlock("atomic_cont");
  auto *ExitBB = CGF.createBasicBlock("atomic_exit");
  CGF.EmitBlock(ContBB);
  Address DesiredAddr = CreateTempAlloca();
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    auto *OldVal = CGF.Builder.CreateLoad(ExpectedAddr);
    CGF.Builder.CreateStore(OldVal, DesiredAddr);
  }
  EmitAtomicUpdateValue(CGF, *this, UpdateRVal, DesiredAddr);
  llvm::Value *ExpectedPtr = ExpectedAddr.emitRawPointer(CGF);
  llvm::Value *DesiredPtr = DesiredAddr.emitRawPointer(CGF);
  auto *Res =
      EmitAtomicCompareExchangeLibcall(ExpectedPtr, DesiredPtr, AO, Failure);
  CGF.Builder.CreateCondBr(Res, ExitBB, ContBB);
  CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::EmitAtomicUpdateOp(llvm::AtomicOrdering AO, RValue UpdateRVal,
                                    bool IsVolatile) {
  auto Failure = llvm::AtomicCmpXchgInst::getStrongestFailureOrdering(AO);

  // Do the atomic load.
  auto *OldVal = EmitAtomicLoadOp(Failure, IsVolatile, /*CmpXchg=*/true);
  // For non-simple lvalues perform compare-and-swap procedure.
  auto *ContBB = CGF.createBasicBlock("atomic_cont");
  auto *ExitBB = CGF.createBasicBlock("atomic_exit");
  auto *CurBB = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(ContBB);
  llvm::PHINode *PHI = CGF.Builder.CreatePHI(OldVal->getType(),
                                             /*NumReservedValues=*/2);
  PHI->addIncoming(OldVal, CurBB);
  Address NewAtomicAddr = CreateTempAlloca();
  Address NewAtomicIntAddr = castToAtomicIntPointer(NewAtomicAddr);
  if ((LVal.isBitField() && BFI.Size != ValueSizeInBits) ||
      requiresMemSetZero(getAtomicAddress().getElementType())) {
    CGF.Builder.CreateStore(PHI, NewAtomicIntAddr);
  }
  EmitAtomicUpdateValue(CGF, *this, UpdateRVal, NewAtomicAddr);
  auto *DesiredVal = CGF.Builder.CreateLoad(NewAtomicIntAddr);
  // Try to write new value using cmpxchg operation.
  auto Res = EmitAtomicCompareExchangeOp(PHI, DesiredVal, AO, Failure);
  PHI->addIncoming(Res.first, CGF.Builder.GetInsertBlock());
  CGF.Builder.CreateCondBr(Res.second, ExitBB, ContBB);
  CGF.EmitBlock(ExitBB, /*IsFinished=*/true);
}

void AtomicInfo::EmitAtomicUpdate(
    llvm::AtomicOrdering AO, const llvm::function_ref<RValue(RValue)> &UpdateOp,
    bool IsVolatile) {
  if (shouldUseLibcall()) {
    EmitAtomicUpdateLibcall(AO, UpdateOp, IsVolatile);
  } else {
    EmitAtomicUpdateOp(AO, UpdateOp, IsVolatile);
  }
}

void AtomicInfo::EmitAtomicUpdate(llvm::AtomicOrdering AO, RValue UpdateRVal,
                                  bool IsVolatile) {
  if (shouldUseLibcall()) {
    EmitAtomicUpdateLibcall(AO, UpdateRVal, IsVolatile);
  } else {
    EmitAtomicUpdateOp(AO, UpdateRVal, IsVolatile);
  }
}

void CodeGenFunction::EmitAtomicStore(RValue rvalue, LValue lvalue,
                                      bool isInit) {
  bool IsVolatile = lvalue.isVolatileQualified();
  llvm::AtomicOrdering AO;
  if (lvalue.getType()->isAtomicType()) {
    AO = llvm::AtomicOrdering::SequentiallyConsistent;
  } else {
    AO = llvm::AtomicOrdering::Release;
    IsVolatile = true;
  }
  return EmitAtomicStore(rvalue, lvalue, AO, IsVolatile, isInit);
}

/// Emit a store to an l-value of atomic type.
///
/// Note that the r-value is expected to be an r-value *of the atomic
/// type*; this means that for aggregate r-values, it should include
/// storage for any padding that was necessary.
void CodeGenFunction::EmitAtomicStore(RValue rvalue, LValue dest,
                                      llvm::AtomicOrdering AO, bool IsVolatile,
                                      bool isInit) {
  // If this is an aggregate r-value, it should agree in type except
  // maybe for address-space qualification.
  assert(!rvalue.isAggregate() ||
         rvalue.getAggregateAddress().getElementType() ==
             dest.getAddress().getElementType());

  AtomicInfo atomics(*this, dest);
  LValue LVal = atomics.getAtomicLValue();

  // If this is an initialization, just put the value there normally.
  if (LVal.isSimple()) {
    if (isInit) {
      atomics.emitCopyIntoMemory(rvalue);
      return;
    }

    // Check whether we should use a library call.
    if (atomics.shouldUseLibcall()) {
      // Produce a source address.
      Address srcAddr = atomics.materializeRValue(rvalue);

      // void __atomic_store(size_t size, void *mem, void *val, int order)
      CallArgList args;
      args.add(RValue::get(atomics.getAtomicSizeValue()),
               getContext().getSizeType());
      args.add(RValue::get(atomics.getAtomicPointer()), getContext().VoidPtrTy);
      args.add(RValue::get(srcAddr.emitRawPointer(*this)),
               getContext().VoidPtrTy);
      args.add(
          RValue::get(llvm::ConstantInt::get(IntTy, (int)llvm::toCABI(AO))),
          getContext().IntTy);
      emitAtomicLibcall(*this, "__atomic_store", getContext().VoidTy, args);
      return;
    }

    // Okay, we're doing this natively.
    llvm::Value *ValToStore = atomics.convertRValueToInt(rvalue);

    // Do the atomic store.
    Address Addr = atomics.getAtomicAddress();
    if (llvm::Value *Value = atomics.getScalarRValValueOrNull(rvalue))
      if (shouldCastToInt(Value->getType(), /*CmpXchg=*/false)) {
        Addr = atomics.castToAtomicIntPointer(Addr);
        ValToStore = Builder.CreateIntCast(ValToStore, Addr.getElementType(),
                                           /*isSigned=*/false);
      }
    llvm::StoreInst *store = Builder.CreateStore(ValToStore, Addr);

    if (AO == llvm::AtomicOrdering::Acquire)
      AO = llvm::AtomicOrdering::Monotonic;
    else if (AO == llvm::AtomicOrdering::AcquireRelease)
      AO = llvm::AtomicOrdering::Release;
    // Initializations don't need to be atomic.
    if (!isInit)
      store->setAtomic(AO);

    // Other decoration.
    if (IsVolatile)
      store->setVolatile(true);
    CGM.DecorateInstructionWithTBAA(store, dest.getTBAAInfo());
    return;
  }

  // Emit simple atomic update operation.
  atomics.EmitAtomicUpdate(AO, rvalue, IsVolatile);
}

/// Emit a compare-and-exchange op for atomic type.
///
std::pair<RValue, llvm::Value *> CodeGenFunction::EmitAtomicCompareExchange(
    LValue Obj, RValue Expected, RValue Desired, SourceLocation Loc,
    llvm::AtomicOrdering Success, llvm::AtomicOrdering Failure, bool IsWeak,
    AggValueSlot Slot) {
  // If this is an aggregate r-value, it should agree in type except
  // maybe for address-space qualification.
  assert(!Expected.isAggregate() ||
         Expected.getAggregateAddress().getElementType() ==
             Obj.getAddress().getElementType());
  assert(!Desired.isAggregate() ||
         Desired.getAggregateAddress().getElementType() ==
             Obj.getAddress().getElementType());
  AtomicInfo Atomics(*this, Obj);

  return Atomics.EmitAtomicCompareExchange(Expected, Desired, Success, Failure,
                                           IsWeak);
}

void CodeGenFunction::EmitAtomicUpdate(
    LValue LVal, llvm::AtomicOrdering AO,
    const llvm::function_ref<RValue(RValue)> &UpdateOp, bool IsVolatile) {
  AtomicInfo Atomics(*this, LVal);
  Atomics.EmitAtomicUpdate(AO, UpdateOp, IsVolatile);
}

void CodeGenFunction::EmitAtomicInit(Expr *init, LValue dest) {
  AtomicInfo atomics(*this, dest);

  switch (atomics.getEvaluationKind()) {
  case TEK_Scalar: {
    llvm::Value *value = EmitScalarExpr(init);
    atomics.emitCopyIntoMemory(RValue::get(value));
    return;
  }

  case TEK_Complex: {
    ComplexPairTy value = EmitComplexExpr(init);
    atomics.emitCopyIntoMemory(RValue::getComplex(value));
    return;
  }

  case TEK_Aggregate: {
    // Fix up the destination if the initializer isn't an expression
    // of atomic type.
    bool Zeroed = false;
    if (!init->getType()->isAtomicType()) {
      Zeroed = atomics.emitMemSetZeroIfNecessary();
      dest = atomics.projectValue();
    }

    // Evaluate the expression directly into the destination.
    AggValueSlot slot = AggValueSlot::forLValue(
        dest, AggValueSlot::IsNotDestructed,
        AggValueSlot::DoesNotNeedGCBarriers, AggValueSlot::IsNotAliased,
        AggValueSlot::DoesNotOverlap,
        Zeroed ? AggValueSlot::IsZeroed : AggValueSlot::IsNotZeroed);

    EmitAggExpr(init, slot);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}
