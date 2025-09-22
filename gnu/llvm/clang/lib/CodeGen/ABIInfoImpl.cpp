//===- ABIInfoImpl.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"

using namespace clang;
using namespace clang::CodeGen;

// Pin the vtable to this file.
DefaultABIInfo::~DefaultABIInfo() = default;

ABIArgInfo DefaultABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    return getNaturalAlignIndirect(Ty);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  ASTContext &Context = getContext();
  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() >
        Context.getTypeSize(Context.getTargetInfo().hasInt128Type()
                                ? Context.Int128Ty
                                : Context.LongLongTy))
      return getNaturalAlignIndirect(Ty);

  return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                            : ABIArgInfo::getDirect());
}

ABIArgInfo DefaultABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() >
        getContext().getTypeSize(getContext().getTargetInfo().hasInt128Type()
                                     ? getContext().Int128Ty
                                     : getContext().LongLongTy))
      return getNaturalAlignIndirect(RetTy);

  return (isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                               : ABIArgInfo::getDirect());
}

void DefaultABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);
}

RValue DefaultABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty, AggValueSlot Slot) const {
  return CGF.EmitLoadOfAnyValue(
      CGF.MakeAddrLValue(
          EmitVAArgInstr(CGF, VAListAddr, Ty, classifyArgumentType(Ty)), Ty),
      Slot);
}

ABIArgInfo CodeGen::coerceToIntArray(QualType Ty, ASTContext &Context,
                                     llvm::LLVMContext &LLVMContext) {
  // Alignment and Size are measured in bits.
  const uint64_t Size = Context.getTypeSize(Ty);
  const uint64_t Alignment = Context.getTypeAlign(Ty);
  llvm::Type *IntType = llvm::Type::getIntNTy(LLVMContext, Alignment);
  const uint64_t NumElements = (Size + Alignment - 1) / Alignment;
  return ABIArgInfo::getDirect(llvm::ArrayType::get(IntType, NumElements));
}

void CodeGen::AssignToArrayRange(CodeGen::CGBuilderTy &Builder,
                                 llvm::Value *Array, llvm::Value *Value,
                                 unsigned FirstIndex, unsigned LastIndex) {
  // Alternatively, we could emit this as a loop in the source.
  for (unsigned I = FirstIndex; I <= LastIndex; ++I) {
    llvm::Value *Cell =
        Builder.CreateConstInBoundsGEP1_32(Builder.getInt8Ty(), Array, I);
    Builder.CreateAlignedStore(Value, Cell, CharUnits::One());
  }
}

bool CodeGen::isAggregateTypeForABI(QualType T) {
  return !CodeGenFunction::hasScalarEvaluationKind(T) ||
         T->isMemberFunctionPointerType();
}

llvm::Type *CodeGen::getVAListElementType(CodeGenFunction &CGF) {
  return CGF.ConvertTypeForMem(
      CGF.getContext().getBuiltinVaListType()->getPointeeType());
}

CGCXXABI::RecordArgABI CodeGen::getRecordArgABI(const RecordType *RT,
                                                CGCXXABI &CXXABI) {
  const CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(RT->getDecl());
  if (!RD) {
    if (!RT->getDecl()->canPassInRegisters())
      return CGCXXABI::RAA_Indirect;
    return CGCXXABI::RAA_Default;
  }
  return CXXABI.getRecordArgABI(RD);
}

CGCXXABI::RecordArgABI CodeGen::getRecordArgABI(QualType T, CGCXXABI &CXXABI) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return CGCXXABI::RAA_Default;
  return getRecordArgABI(RT, CXXABI);
}

bool CodeGen::classifyReturnType(const CGCXXABI &CXXABI, CGFunctionInfo &FI,
                                 const ABIInfo &Info) {
  QualType Ty = FI.getReturnType();

  if (const auto *RT = Ty->getAs<RecordType>())
    if (!isa<CXXRecordDecl>(RT->getDecl()) &&
        !RT->getDecl()->canPassInRegisters()) {
      FI.getReturnInfo() = Info.getNaturalAlignIndirect(Ty);
      return true;
    }

  return CXXABI.classifyReturnType(FI);
}

QualType CodeGen::useFirstFieldIfTransparentUnion(QualType Ty) {
  if (const RecordType *UT = Ty->getAsUnionType()) {
    const RecordDecl *UD = UT->getDecl();
    if (UD->hasAttr<TransparentUnionAttr>()) {
      assert(!UD->field_empty() && "sema created an empty transparent union");
      return UD->field_begin()->getType();
    }
  }
  return Ty;
}

llvm::Value *CodeGen::emitRoundPointerUpToAlignment(CodeGenFunction &CGF,
                                                    llvm::Value *Ptr,
                                                    CharUnits Align) {
  // OverflowArgArea = (OverflowArgArea + Align - 1) & -Align;
  llvm::Value *RoundUp = CGF.Builder.CreateConstInBoundsGEP1_32(
      CGF.Builder.getInt8Ty(), Ptr, Align.getQuantity() - 1);
  return CGF.Builder.CreateIntrinsic(
      llvm::Intrinsic::ptrmask, {Ptr->getType(), CGF.IntPtrTy},
      {RoundUp, llvm::ConstantInt::get(CGF.IntPtrTy, -Align.getQuantity())},
      nullptr, Ptr->getName() + ".aligned");
}

Address
CodeGen::emitVoidPtrDirectVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                llvm::Type *DirectTy, CharUnits DirectSize,
                                CharUnits DirectAlign, CharUnits SlotSize,
                                bool AllowHigherAlign, bool ForceRightAdjust) {
  // Cast the element type to i8* if necessary.  Some platforms define
  // va_list as a struct containing an i8* instead of just an i8*.
  if (VAListAddr.getElementType() != CGF.Int8PtrTy)
    VAListAddr = VAListAddr.withElementType(CGF.Int8PtrTy);

  llvm::Value *Ptr = CGF.Builder.CreateLoad(VAListAddr, "argp.cur");

  // If the CC aligns values higher than the slot size, do so if needed.
  Address Addr = Address::invalid();
  if (AllowHigherAlign && DirectAlign > SlotSize) {
    Addr = Address(emitRoundPointerUpToAlignment(CGF, Ptr, DirectAlign),
                   CGF.Int8Ty, DirectAlign);
  } else {
    Addr = Address(Ptr, CGF.Int8Ty, SlotSize);
  }

  // Advance the pointer past the argument, then store that back.
  CharUnits FullDirectSize = DirectSize.alignTo(SlotSize);
  Address NextPtr =
      CGF.Builder.CreateConstInBoundsByteGEP(Addr, FullDirectSize, "argp.next");
  CGF.Builder.CreateStore(NextPtr.emitRawPointer(CGF), VAListAddr);

  // If the argument is smaller than a slot, and this is a big-endian
  // target, the argument will be right-adjusted in its slot.
  if (DirectSize < SlotSize && CGF.CGM.getDataLayout().isBigEndian() &&
      (!DirectTy->isStructTy() || ForceRightAdjust)) {
    Addr = CGF.Builder.CreateConstInBoundsByteGEP(Addr, SlotSize - DirectSize);
  }

  return Addr.withElementType(DirectTy);
}

RValue CodeGen::emitVoidPtrVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType ValueTy, bool IsIndirect,
                                 TypeInfoChars ValueInfo,
                                 CharUnits SlotSizeAndAlign,
                                 bool AllowHigherAlign, AggValueSlot Slot,
                                 bool ForceRightAdjust) {
  // The size and alignment of the value that was passed directly.
  CharUnits DirectSize, DirectAlign;
  if (IsIndirect) {
    DirectSize = CGF.getPointerSize();
    DirectAlign = CGF.getPointerAlign();
  } else {
    DirectSize = ValueInfo.Width;
    DirectAlign = ValueInfo.Align;
  }

  // Cast the address we've calculated to the right type.
  llvm::Type *DirectTy = CGF.ConvertTypeForMem(ValueTy), *ElementTy = DirectTy;
  if (IsIndirect) {
    unsigned AllocaAS = CGF.CGM.getDataLayout().getAllocaAddrSpace();
    DirectTy = llvm::PointerType::get(CGF.getLLVMContext(), AllocaAS);
  }

  Address Addr = emitVoidPtrDirectVAArg(CGF, VAListAddr, DirectTy, DirectSize,
                                        DirectAlign, SlotSizeAndAlign,
                                        AllowHigherAlign, ForceRightAdjust);

  if (IsIndirect) {
    Addr = Address(CGF.Builder.CreateLoad(Addr), ElementTy, ValueInfo.Align);
  }

  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(Addr, ValueTy), Slot);
}

Address CodeGen::emitMergePHI(CodeGenFunction &CGF, Address Addr1,
                              llvm::BasicBlock *Block1, Address Addr2,
                              llvm::BasicBlock *Block2,
                              const llvm::Twine &Name) {
  assert(Addr1.getType() == Addr2.getType());
  llvm::PHINode *PHI = CGF.Builder.CreatePHI(Addr1.getType(), 2, Name);
  PHI->addIncoming(Addr1.emitRawPointer(CGF), Block1);
  PHI->addIncoming(Addr2.emitRawPointer(CGF), Block2);
  CharUnits Align = std::min(Addr1.getAlignment(), Addr2.getAlignment());
  return Address(PHI, Addr1.getElementType(), Align);
}

bool CodeGen::isEmptyField(ASTContext &Context, const FieldDecl *FD,
                           bool AllowArrays, bool AsIfNoUniqueAddr) {
  if (FD->isUnnamedBitField())
    return true;

  QualType FT = FD->getType();

  // Constant arrays of empty records count as empty, strip them off.
  // Constant arrays of zero length always count as empty.
  bool WasArray = false;
  if (AllowArrays)
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->isZeroSize())
        return true;
      FT = AT->getElementType();
      // The [[no_unique_address]] special case below does not apply to
      // arrays of C++ empty records, so we need to remember this fact.
      WasArray = true;
    }

  const RecordType *RT = FT->getAs<RecordType>();
  if (!RT)
    return false;

  // C++ record fields are never empty, at least in the Itanium ABI.
  //
  // FIXME: We should use a predicate for whether this behavior is true in the
  // current ABI.
  //
  // The exception to the above rule are fields marked with the
  // [[no_unique_address]] attribute (since C++20).  Those do count as empty
  // according to the Itanium ABI.  The exception applies only to records,
  // not arrays of records, so we must also check whether we stripped off an
  // array type above.
  if (isa<CXXRecordDecl>(RT->getDecl()) &&
      (WasArray || (!AsIfNoUniqueAddr && !FD->hasAttr<NoUniqueAddressAttr>())))
    return false;

  return isEmptyRecord(Context, FT, AllowArrays, AsIfNoUniqueAddr);
}

bool CodeGen::isEmptyRecord(ASTContext &Context, QualType T, bool AllowArrays,
                            bool AsIfNoUniqueAddr) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const auto &I : CXXRD->bases())
      if (!isEmptyRecord(Context, I.getType(), true, AsIfNoUniqueAddr))
        return false;

  for (const auto *I : RD->fields())
    if (!isEmptyField(Context, I, AllowArrays, AsIfNoUniqueAddr))
      return false;
  return true;
}

bool CodeGen::isEmptyFieldForLayout(const ASTContext &Context,
                                    const FieldDecl *FD) {
  if (FD->isZeroLengthBitField(Context))
    return true;

  if (FD->isUnnamedBitField())
    return false;

  return isEmptyRecordForLayout(Context, FD->getType());
}

bool CodeGen::isEmptyRecordForLayout(const ASTContext &Context, QualType T) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return false;

  const RecordDecl *RD = RT->getDecl();

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    if (CXXRD->isDynamicClass())
      return false;

    for (const auto &I : CXXRD->bases())
      if (!isEmptyRecordForLayout(Context, I.getType()))
        return false;
  }

  for (const auto *I : RD->fields())
    if (!isEmptyFieldForLayout(Context, I))
      return false;

  return true;
}

const Type *CodeGen::isSingleElementStruct(QualType T, ASTContext &Context) {
  const RecordType *RT = T->getAs<RecordType>();
  if (!RT)
    return nullptr;

  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return nullptr;

  const Type *Found = nullptr;

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    for (const auto &I : CXXRD->bases()) {
      // Ignore empty records.
      if (isEmptyRecord(Context, I.getType(), true))
        continue;

      // If we already found an element then this isn't a single-element struct.
      if (Found)
        return nullptr;

      // If this is non-empty and not a single element struct, the composite
      // cannot be a single element struct.
      Found = isSingleElementStruct(I.getType(), Context);
      if (!Found)
        return nullptr;
    }
  }

  // Check for single element.
  for (const auto *FD : RD->fields()) {
    QualType FT = FD->getType();

    // Ignore empty fields.
    if (isEmptyField(Context, FD, true))
      continue;

    // If we already found an element then this isn't a single-element
    // struct.
    if (Found)
      return nullptr;

    // Treat single element arrays as the element.
    while (const ConstantArrayType *AT = Context.getAsConstantArrayType(FT)) {
      if (AT->getZExtSize() != 1)
        break;
      FT = AT->getElementType();
    }

    if (!isAggregateTypeForABI(FT)) {
      Found = FT.getTypePtr();
    } else {
      Found = isSingleElementStruct(FT, Context);
      if (!Found)
        return nullptr;
    }
  }

  // We don't consider a struct a single-element struct if it has
  // padding beyond the element type.
  if (Found && Context.getTypeSize(Found) != Context.getTypeSize(T))
    return nullptr;

  return Found;
}

Address CodeGen::EmitVAArgInstr(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, const ABIArgInfo &AI) {
  // This default implementation defers to the llvm backend's va_arg
  // instruction. It can handle only passing arguments directly
  // (typically only handled in the backend for primitive types), or
  // aggregates passed indirectly by pointer (NOTE: if the "byval"
  // flag has ABI impact in the callee, this implementation cannot
  // work.)

  // Only a few cases are covered here at the moment -- those needed
  // by the default abi.
  llvm::Value *Val;

  if (AI.isIndirect()) {
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(
        !AI.getIndirectRealign() &&
        "Unexpected IndirectRealign seen in arginfo in generic VAArg emitter!");

    auto TyInfo = CGF.getContext().getTypeInfoInChars(Ty);
    CharUnits TyAlignForABI = TyInfo.Align;

    llvm::Type *ElementTy = CGF.ConvertTypeForMem(Ty);
    llvm::Type *BaseTy = llvm::PointerType::getUnqual(ElementTy);
    llvm::Value *Addr =
        CGF.Builder.CreateVAArg(VAListAddr.emitRawPointer(CGF), BaseTy);
    return Address(Addr, ElementTy, TyAlignForABI);
  } else {
    assert((AI.isDirect() || AI.isExtend()) &&
           "Unexpected ArgInfo Kind in generic VAArg emitter!");

    assert(!AI.getInReg() &&
           "Unexpected InReg seen in arginfo in generic VAArg emitter!");
    assert(!AI.getPaddingType() &&
           "Unexpected PaddingType seen in arginfo in generic VAArg emitter!");
    assert(!AI.getDirectOffset() &&
           "Unexpected DirectOffset seen in arginfo in generic VAArg emitter!");
    assert(!AI.getCoerceToType() &&
           "Unexpected CoerceToType seen in arginfo in generic VAArg emitter!");

    Address Temp = CGF.CreateMemTemp(Ty, "varet");
    Val = CGF.Builder.CreateVAArg(VAListAddr.emitRawPointer(CGF),
                                  CGF.ConvertTypeForMem(Ty));
    CGF.Builder.CreateStore(Val, Temp);
    return Temp;
  }
}

bool CodeGen::isSIMDVectorType(ASTContext &Context, QualType Ty) {
  return Ty->getAs<VectorType>() && Context.getTypeSize(Ty) == 128;
}

bool CodeGen::isRecordWithSIMDVectorType(ASTContext &Context, QualType Ty) {
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();

  // If this is a C++ record, check the bases first.
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
    for (const auto &I : CXXRD->bases())
      if (!isRecordWithSIMDVectorType(Context, I.getType()))
        return false;

  for (const auto *i : RD->fields()) {
    QualType FT = i->getType();

    if (isSIMDVectorType(Context, FT))
      return true;

    if (isRecordWithSIMDVectorType(Context, FT))
      return true;
  }

  return false;
}
