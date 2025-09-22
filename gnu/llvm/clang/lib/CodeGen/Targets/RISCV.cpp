//===- RISCV.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// RISC-V ABI Implementation
//===----------------------------------------------------------------------===//

namespace {
class RISCVABIInfo : public DefaultABIInfo {
private:
  // Size of the integer ('x') registers in bits.
  unsigned XLen;
  // Size of the floating point ('f') registers in bits. Note that the target
  // ISA might have a wider FLen than the selected ABI (e.g. an RV32IF target
  // with soft float ABI has FLen==0).
  unsigned FLen;
  const int NumArgGPRs;
  const int NumArgFPRs;
  const bool EABI;
  bool detectFPCCEligibleStructHelper(QualType Ty, CharUnits CurOff,
                                      llvm::Type *&Field1Ty,
                                      CharUnits &Field1Off,
                                      llvm::Type *&Field2Ty,
                                      CharUnits &Field2Off) const;

public:
  RISCVABIInfo(CodeGen::CodeGenTypes &CGT, unsigned XLen, unsigned FLen,
               bool EABI)
      : DefaultABIInfo(CGT), XLen(XLen), FLen(FLen), NumArgGPRs(EABI ? 6 : 8),
        NumArgFPRs(FLen != 0 ? 8 : 0), EABI(EABI) {}

  // DefaultABIInfo's classifyReturnType and classifyArgumentType are
  // non-virtual, but computeInfo is virtual, so we overload it.
  void computeInfo(CGFunctionInfo &FI) const override;

  ABIArgInfo classifyArgumentType(QualType Ty, bool IsFixed, int &ArgGPRsLeft,
                                  int &ArgFPRsLeft) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  ABIArgInfo extendType(QualType Ty) const;

  bool detectFPCCEligibleStruct(QualType Ty, llvm::Type *&Field1Ty,
                                CharUnits &Field1Off, llvm::Type *&Field2Ty,
                                CharUnits &Field2Off, int &NeededArgGPRs,
                                int &NeededArgFPRs) const;
  ABIArgInfo coerceAndExpandFPCCEligibleStruct(llvm::Type *Field1Ty,
                                               CharUnits Field1Off,
                                               llvm::Type *Field2Ty,
                                               CharUnits Field2Off) const;

  ABIArgInfo coerceVLSVector(QualType Ty) const;
};
} // end anonymous namespace

void RISCVABIInfo::computeInfo(CGFunctionInfo &FI) const {
  QualType RetTy = FI.getReturnType();
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(RetTy);

  // IsRetIndirect is true if classifyArgumentType indicated the value should
  // be passed indirect, or if the type size is a scalar greater than 2*XLen
  // and not a complex type with elements <= FLen. e.g. fp128 is passed direct
  // in LLVM IR, relying on the backend lowering code to rewrite the argument
  // list and pass indirectly on RV32.
  bool IsRetIndirect = FI.getReturnInfo().getKind() == ABIArgInfo::Indirect;
  if (!IsRetIndirect && RetTy->isScalarType() &&
      getContext().getTypeSize(RetTy) > (2 * XLen)) {
    if (RetTy->isComplexType() && FLen) {
      QualType EltTy = RetTy->castAs<ComplexType>()->getElementType();
      IsRetIndirect = getContext().getTypeSize(EltTy) > FLen;
    } else {
      // This is a normal scalar > 2*XLen, such as fp128 on RV32.
      IsRetIndirect = true;
    }
  }

  int ArgGPRsLeft = IsRetIndirect ? NumArgGPRs - 1 : NumArgGPRs;
  int ArgFPRsLeft = NumArgFPRs;
  int NumFixedArgs = FI.getNumRequiredArgs();

  int ArgNum = 0;
  for (auto &ArgInfo : FI.arguments()) {
    bool IsFixed = ArgNum < NumFixedArgs;
    ArgInfo.info =
        classifyArgumentType(ArgInfo.type, IsFixed, ArgGPRsLeft, ArgFPRsLeft);
    ArgNum++;
  }
}

// Returns true if the struct is a potential candidate for the floating point
// calling convention. If this function returns true, the caller is
// responsible for checking that if there is only a single field then that
// field is a float.
bool RISCVABIInfo::detectFPCCEligibleStructHelper(QualType Ty, CharUnits CurOff,
                                                  llvm::Type *&Field1Ty,
                                                  CharUnits &Field1Off,
                                                  llvm::Type *&Field2Ty,
                                                  CharUnits &Field2Off) const {
  bool IsInt = Ty->isIntegralOrEnumerationType();
  bool IsFloat = Ty->isRealFloatingType();

  if (IsInt || IsFloat) {
    uint64_t Size = getContext().getTypeSize(Ty);
    if (IsInt && Size > XLen)
      return false;
    // Can't be eligible if larger than the FP registers. Handling of half
    // precision values has been specified in the ABI, so don't block those.
    if (IsFloat && Size > FLen)
      return false;
    // Can't be eligible if an integer type was already found (int+int pairs
    // are not eligible).
    if (IsInt && Field1Ty && Field1Ty->isIntegerTy())
      return false;
    if (!Field1Ty) {
      Field1Ty = CGT.ConvertType(Ty);
      Field1Off = CurOff;
      return true;
    }
    if (!Field2Ty) {
      Field2Ty = CGT.ConvertType(Ty);
      Field2Off = CurOff;
      return true;
    }
    return false;
  }

  if (auto CTy = Ty->getAs<ComplexType>()) {
    if (Field1Ty)
      return false;
    QualType EltTy = CTy->getElementType();
    if (getContext().getTypeSize(EltTy) > FLen)
      return false;
    Field1Ty = CGT.ConvertType(EltTy);
    Field1Off = CurOff;
    Field2Ty = Field1Ty;
    Field2Off = Field1Off + getContext().getTypeSizeInChars(EltTy);
    return true;
  }

  if (const ConstantArrayType *ATy = getContext().getAsConstantArrayType(Ty)) {
    uint64_t ArraySize = ATy->getZExtSize();
    QualType EltTy = ATy->getElementType();
    // Non-zero-length arrays of empty records make the struct ineligible for
    // the FP calling convention in C++.
    if (const auto *RTy = EltTy->getAs<RecordType>()) {
      if (ArraySize != 0 && isa<CXXRecordDecl>(RTy->getDecl()) &&
          isEmptyRecord(getContext(), EltTy, true, true))
        return false;
    }
    CharUnits EltSize = getContext().getTypeSizeInChars(EltTy);
    for (uint64_t i = 0; i < ArraySize; ++i) {
      bool Ret = detectFPCCEligibleStructHelper(EltTy, CurOff, Field1Ty,
                                                Field1Off, Field2Ty, Field2Off);
      if (!Ret)
        return false;
      CurOff += EltSize;
    }
    return true;
  }

  if (const auto *RTy = Ty->getAs<RecordType>()) {
    // Structures with either a non-trivial destructor or a non-trivial
    // copy constructor are not eligible for the FP calling convention.
    if (getRecordArgABI(Ty, CGT.getCXXABI()))
      return false;
    if (isEmptyRecord(getContext(), Ty, true, true))
      return true;
    const RecordDecl *RD = RTy->getDecl();
    // Unions aren't eligible unless they're empty (which is caught above).
    if (RD->isUnion())
      return false;
    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        const auto *BDecl =
            cast<CXXRecordDecl>(B.getType()->castAs<RecordType>()->getDecl());
        CharUnits BaseOff = Layout.getBaseClassOffset(BDecl);
        bool Ret = detectFPCCEligibleStructHelper(B.getType(), CurOff + BaseOff,
                                                  Field1Ty, Field1Off, Field2Ty,
                                                  Field2Off);
        if (!Ret)
          return false;
      }
    }
    int ZeroWidthBitFieldCount = 0;
    for (const FieldDecl *FD : RD->fields()) {
      uint64_t FieldOffInBits = Layout.getFieldOffset(FD->getFieldIndex());
      QualType QTy = FD->getType();
      if (FD->isBitField()) {
        unsigned BitWidth = FD->getBitWidthValue(getContext());
        // Allow a bitfield with a type greater than XLen as long as the
        // bitwidth is XLen or less.
        if (getContext().getTypeSize(QTy) > XLen && BitWidth <= XLen)
          QTy = getContext().getIntTypeForBitwidth(XLen, false);
        if (BitWidth == 0) {
          ZeroWidthBitFieldCount++;
          continue;
        }
      }

      bool Ret = detectFPCCEligibleStructHelper(
          QTy, CurOff + getContext().toCharUnitsFromBits(FieldOffInBits),
          Field1Ty, Field1Off, Field2Ty, Field2Off);
      if (!Ret)
        return false;

      // As a quirk of the ABI, zero-width bitfields aren't ignored for fp+fp
      // or int+fp structs, but are ignored for a struct with an fp field and
      // any number of zero-width bitfields.
      if (Field2Ty && ZeroWidthBitFieldCount > 0)
        return false;
    }
    return Field1Ty != nullptr;
  }

  return false;
}

// Determine if a struct is eligible for passing according to the floating
// point calling convention (i.e., when flattened it contains a single fp
// value, fp+fp, or int+fp of appropriate size). If so, NeededArgFPRs and
// NeededArgGPRs are incremented appropriately.
bool RISCVABIInfo::detectFPCCEligibleStruct(QualType Ty, llvm::Type *&Field1Ty,
                                            CharUnits &Field1Off,
                                            llvm::Type *&Field2Ty,
                                            CharUnits &Field2Off,
                                            int &NeededArgGPRs,
                                            int &NeededArgFPRs) const {
  Field1Ty = nullptr;
  Field2Ty = nullptr;
  NeededArgGPRs = 0;
  NeededArgFPRs = 0;
  bool IsCandidate = detectFPCCEligibleStructHelper(
      Ty, CharUnits::Zero(), Field1Ty, Field1Off, Field2Ty, Field2Off);
  if (!Field1Ty)
    return false;
  // Not really a candidate if we have a single int but no float.
  if (Field1Ty && !Field2Ty && !Field1Ty->isFloatingPointTy())
    return false;
  if (!IsCandidate)
    return false;
  if (Field1Ty && Field1Ty->isFloatingPointTy())
    NeededArgFPRs++;
  else if (Field1Ty)
    NeededArgGPRs++;
  if (Field2Ty && Field2Ty->isFloatingPointTy())
    NeededArgFPRs++;
  else if (Field2Ty)
    NeededArgGPRs++;
  return true;
}

// Call getCoerceAndExpand for the two-element flattened struct described by
// Field1Ty, Field1Off, Field2Ty, Field2Off. This method will create an
// appropriate coerceToType and unpaddedCoerceToType.
ABIArgInfo RISCVABIInfo::coerceAndExpandFPCCEligibleStruct(
    llvm::Type *Field1Ty, CharUnits Field1Off, llvm::Type *Field2Ty,
    CharUnits Field2Off) const {
  SmallVector<llvm::Type *, 3> CoerceElts;
  SmallVector<llvm::Type *, 2> UnpaddedCoerceElts;
  if (!Field1Off.isZero())
    CoerceElts.push_back(llvm::ArrayType::get(
        llvm::Type::getInt8Ty(getVMContext()), Field1Off.getQuantity()));

  CoerceElts.push_back(Field1Ty);
  UnpaddedCoerceElts.push_back(Field1Ty);

  if (!Field2Ty) {
    return ABIArgInfo::getCoerceAndExpand(
        llvm::StructType::get(getVMContext(), CoerceElts, !Field1Off.isZero()),
        UnpaddedCoerceElts[0]);
  }

  CharUnits Field2Align =
      CharUnits::fromQuantity(getDataLayout().getABITypeAlign(Field2Ty));
  CharUnits Field1End = Field1Off +
      CharUnits::fromQuantity(getDataLayout().getTypeStoreSize(Field1Ty));
  CharUnits Field2OffNoPadNoPack = Field1End.alignTo(Field2Align);

  CharUnits Padding = CharUnits::Zero();
  if (Field2Off > Field2OffNoPadNoPack)
    Padding = Field2Off - Field2OffNoPadNoPack;
  else if (Field2Off != Field2Align && Field2Off > Field1End)
    Padding = Field2Off - Field1End;

  bool IsPacked = !Field2Off.isMultipleOf(Field2Align);

  if (!Padding.isZero())
    CoerceElts.push_back(llvm::ArrayType::get(
        llvm::Type::getInt8Ty(getVMContext()), Padding.getQuantity()));

  CoerceElts.push_back(Field2Ty);
  UnpaddedCoerceElts.push_back(Field2Ty);

  auto CoerceToType =
      llvm::StructType::get(getVMContext(), CoerceElts, IsPacked);
  auto UnpaddedCoerceToType =
      llvm::StructType::get(getVMContext(), UnpaddedCoerceElts, IsPacked);

  return ABIArgInfo::getCoerceAndExpand(CoerceToType, UnpaddedCoerceToType);
}

// Fixed-length RVV vectors are represented as scalable vectors in function
// args/return and must be coerced from fixed vectors.
ABIArgInfo RISCVABIInfo::coerceVLSVector(QualType Ty) const {
  assert(Ty->isVectorType() && "expected vector type!");

  const auto *VT = Ty->castAs<VectorType>();
  assert(VT->getElementType()->isBuiltinType() && "expected builtin type!");

  auto VScale =
      getContext().getTargetInfo().getVScaleRange(getContext().getLangOpts());

  unsigned NumElts = VT->getNumElements();
  llvm::Type *EltType;
  if (VT->getVectorKind() == VectorKind::RVVFixedLengthMask) {
    NumElts *= 8;
    EltType = llvm::Type::getInt1Ty(getVMContext());
  } else {
    assert(VT->getVectorKind() == VectorKind::RVVFixedLengthData &&
           "Unexpected vector kind");
    EltType = CGT.ConvertType(VT->getElementType());
  }

  // The MinNumElts is simplified from equation:
  // NumElts / VScale =
  //  (EltSize * NumElts / (VScale * RVVBitsPerBlock))
  //    * (RVVBitsPerBlock / EltSize)
  llvm::ScalableVectorType *ResType =
      llvm::ScalableVectorType::get(EltType, NumElts / VScale->first);
  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo RISCVABIInfo::classifyArgumentType(QualType Ty, bool IsFixed,
                                              int &ArgGPRsLeft,
                                              int &ArgFPRsLeft) const {
  assert(ArgGPRsLeft <= NumArgGPRs && "Arg GPR tracking underflow");
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always passed indirectly.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    if (ArgGPRsLeft)
      ArgGPRsLeft -= 1;
    return getNaturalAlignIndirect(Ty, /*ByVal=*/RAA ==
                                           CGCXXABI::RAA_DirectInMemory);
  }

  uint64_t Size = getContext().getTypeSize(Ty);

  // Ignore empty structs/unions whose size is zero. According to the calling
  // convention empty structs/unions are required to be sized types in C++.
  if (isEmptyRecord(getContext(), Ty, true) && Size == 0)
    return ABIArgInfo::getIgnore();

  // Pass floating point values via FPRs if possible.
  if (IsFixed && Ty->isFloatingType() && !Ty->isComplexType() &&
      FLen >= Size && ArgFPRsLeft) {
    ArgFPRsLeft--;
    return ABIArgInfo::getDirect();
  }

  // Complex types for the hard float ABI must be passed direct rather than
  // using CoerceAndExpand.
  if (IsFixed && Ty->isComplexType() && FLen && ArgFPRsLeft >= 2) {
    QualType EltTy = Ty->castAs<ComplexType>()->getElementType();
    if (getContext().getTypeSize(EltTy) <= FLen) {
      ArgFPRsLeft -= 2;
      return ABIArgInfo::getDirect();
    }
  }

  if (IsFixed && FLen && Ty->isStructureOrClassType()) {
    llvm::Type *Field1Ty = nullptr;
    llvm::Type *Field2Ty = nullptr;
    CharUnits Field1Off = CharUnits::Zero();
    CharUnits Field2Off = CharUnits::Zero();
    int NeededArgGPRs = 0;
    int NeededArgFPRs = 0;
    bool IsCandidate =
        detectFPCCEligibleStruct(Ty, Field1Ty, Field1Off, Field2Ty, Field2Off,
                                 NeededArgGPRs, NeededArgFPRs);
    if (IsCandidate && NeededArgGPRs <= ArgGPRsLeft &&
        NeededArgFPRs <= ArgFPRsLeft) {
      ArgGPRsLeft -= NeededArgGPRs;
      ArgFPRsLeft -= NeededArgFPRs;
      return coerceAndExpandFPCCEligibleStruct(Field1Ty, Field1Off, Field2Ty,
                                               Field2Off);
    }
  }

  uint64_t NeededAlign = getContext().getTypeAlign(Ty);
  // Determine the number of GPRs needed to pass the current argument
  // according to the ABI. 2*XLen-aligned varargs are passed in "aligned"
  // register pairs, so may consume 3 registers.
  // TODO: To be compatible with GCC's behaviors, we don't align registers
  // currently if we are using ILP32E calling convention. This behavior may be
  // changed when RV32E/ILP32E is ratified.
  int NeededArgGPRs = 1;
  if (!IsFixed && NeededAlign == 2 * XLen)
    NeededArgGPRs = 2 + (EABI && XLen == 32 ? 0 : (ArgGPRsLeft % 2));
  else if (Size > XLen && Size <= 2 * XLen)
    NeededArgGPRs = 2;

  if (NeededArgGPRs > ArgGPRsLeft) {
    NeededArgGPRs = ArgGPRsLeft;
  }

  ArgGPRsLeft -= NeededArgGPRs;

  if (!isAggregateTypeForABI(Ty) && !Ty->isVectorType()) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    // All integral types are promoted to XLen width
    if (Size < XLen && Ty->isIntegralOrEnumerationType()) {
      return extendType(Ty);
    }

    if (const auto *EIT = Ty->getAs<BitIntType>()) {
      if (EIT->getNumBits() < XLen)
        return extendType(Ty);
      if (EIT->getNumBits() > 128 ||
          (!getContext().getTargetInfo().hasInt128Type() &&
           EIT->getNumBits() > 64))
        return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    }

    ABIArgInfo Info = ABIArgInfo::getDirect();

    // If it is tuple type, it can't be flattened.
    if (llvm::StructType *STy = dyn_cast<llvm::StructType>(CGT.ConvertType(Ty)))
      Info.setCanBeFlattened(!STy->containsHomogeneousScalableVectorTypes());

    return Info;
  }

  if (const VectorType *VT = Ty->getAs<VectorType>())
    if (VT->getVectorKind() == VectorKind::RVVFixedLengthData ||
        VT->getVectorKind() == VectorKind::RVVFixedLengthMask)
      return coerceVLSVector(Ty);

  // Aggregates which are <= 2*XLen will be passed in registers if possible,
  // so coerce to integers.
  if (Size <= 2 * XLen) {
    unsigned Alignment = getContext().getTypeAlign(Ty);

    // Use a single XLen int if possible, 2*XLen if 2*XLen alignment is
    // required, and a 2-element XLen array if only XLen alignment is required.
    if (Size <= XLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), XLen));
    } else if (Alignment == 2 * XLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), 2 * XLen));
    } else {
      return ABIArgInfo::getDirect(llvm::ArrayType::get(
          llvm::IntegerType::get(getVMContext(), XLen), 2));
    }
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo RISCVABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  int ArgGPRsLeft = 2;
  int ArgFPRsLeft = FLen ? 2 : 0;

  // The rules for return and argument types are the same, so defer to
  // classifyArgumentType.
  return classifyArgumentType(RetTy, /*IsFixed=*/true, ArgGPRsLeft,
                              ArgFPRsLeft);
}

RValue RISCVABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                               QualType Ty, AggValueSlot Slot) const {
  CharUnits SlotSize = CharUnits::fromQuantity(XLen / 8);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true))
    return Slot.asRValue();

  auto TInfo = getContext().getTypeInfoInChars(Ty);

  // TODO: To be compatible with GCC's behaviors, we force arguments with
  // 2×XLEN-bit alignment and size at most 2×XLEN bits like `long long`,
  // `unsigned long long` and `double` to have 4-byte alignment. This
  // behavior may be changed when RV32E/ILP32E is ratified.
  if (EABI && XLen == 32)
    TInfo.Align = std::min(TInfo.Align, CharUnits::fromQuantity(4));

  // Arguments bigger than 2*Xlen bytes are passed indirectly.
  bool IsIndirect = TInfo.Width > 2 * SlotSize;

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, TInfo, SlotSize,
                          /*AllowHigherAlign=*/true, Slot);
}

ABIArgInfo RISCVABIInfo::extendType(QualType Ty) const {
  int TySize = getContext().getTypeSize(Ty);
  // RV64 ABI requires unsigned 32 bit integers to be sign extended.
  if (XLen == 64 && Ty->isUnsignedIntegerOrEnumerationType() && TySize == 32)
    return ABIArgInfo::getSignExtend(Ty);
  return ABIArgInfo::getExtend(Ty);
}

namespace {
class RISCVTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  RISCVTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, unsigned XLen,
                         unsigned FLen, bool EABI)
      : TargetCodeGenInfo(
            std::make_unique<RISCVABIInfo>(CGT, XLen, FLen, EABI)) {
    SwiftInfo =
        std::make_unique<SwiftABIInfo>(CGT, /*SwiftErrorInRegister=*/false);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD) return;

    const auto *Attr = FD->getAttr<RISCVInterruptAttr>();
    if (!Attr)
      return;

    const char *Kind;
    switch (Attr->getInterrupt()) {
    case RISCVInterruptAttr::supervisor: Kind = "supervisor"; break;
    case RISCVInterruptAttr::machine: Kind = "machine"; break;
    }

    auto *Fn = cast<llvm::Function>(GV);

    Fn->addFnAttr("interrupt", Kind);
  }
};
} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createRISCVTargetCodeGenInfo(CodeGenModule &CGM, unsigned XLen,
                                      unsigned FLen, bool EABI) {
  return std::make_unique<RISCVTargetCodeGenInfo>(CGM.getTypes(), XLen, FLen,
                                                  EABI);
}
