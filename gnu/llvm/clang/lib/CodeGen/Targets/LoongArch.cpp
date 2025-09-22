//===- LoongArch.cpp ------------------------------------------------------===//
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

// LoongArch ABI Implementation. Documented at
// https://loongson.github.io/LoongArch-Documentation/LoongArch-ELF-ABI-EN.html
//
//===----------------------------------------------------------------------===//

namespace {
class LoongArchABIInfo : public DefaultABIInfo {
private:
  // Size of the integer ('r') registers in bits.
  unsigned GRLen;
  // Size of the floating point ('f') registers in bits.
  unsigned FRLen;
  // Number of general-purpose argument registers.
  static const int NumGARs = 8;
  // Number of floating-point argument registers.
  static const int NumFARs = 8;
  bool detectFARsEligibleStructHelper(QualType Ty, CharUnits CurOff,
                                      llvm::Type *&Field1Ty,
                                      CharUnits &Field1Off,
                                      llvm::Type *&Field2Ty,
                                      CharUnits &Field2Off) const;

public:
  LoongArchABIInfo(CodeGen::CodeGenTypes &CGT, unsigned GRLen, unsigned FRLen)
      : DefaultABIInfo(CGT), GRLen(GRLen), FRLen(FRLen) {}

  void computeInfo(CGFunctionInfo &FI) const override;

  ABIArgInfo classifyArgumentType(QualType Ty, bool IsFixed, int &GARsLeft,
                                  int &FARsLeft) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  ABIArgInfo extendType(QualType Ty) const;

  bool detectFARsEligibleStruct(QualType Ty, llvm::Type *&Field1Ty,
                                CharUnits &Field1Off, llvm::Type *&Field2Ty,
                                CharUnits &Field2Off, int &NeededArgGPRs,
                                int &NeededArgFPRs) const;
  ABIArgInfo coerceAndExpandFARsEligibleStruct(llvm::Type *Field1Ty,
                                               CharUnits Field1Off,
                                               llvm::Type *Field2Ty,
                                               CharUnits Field2Off) const;
};
} // end anonymous namespace

void LoongArchABIInfo::computeInfo(CGFunctionInfo &FI) const {
  QualType RetTy = FI.getReturnType();
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(RetTy);

  // IsRetIndirect is true if classifyArgumentType indicated the value should
  // be passed indirect, or if the type size is a scalar greater than 2*GRLen
  // and not a complex type with elements <= FRLen. e.g. fp128 is passed direct
  // in LLVM IR, relying on the backend lowering code to rewrite the argument
  // list and pass indirectly on LA32.
  bool IsRetIndirect = FI.getReturnInfo().getKind() == ABIArgInfo::Indirect;
  if (!IsRetIndirect && RetTy->isScalarType() &&
      getContext().getTypeSize(RetTy) > (2 * GRLen)) {
    if (RetTy->isComplexType() && FRLen) {
      QualType EltTy = RetTy->castAs<ComplexType>()->getElementType();
      IsRetIndirect = getContext().getTypeSize(EltTy) > FRLen;
    } else {
      // This is a normal scalar > 2*GRLen, such as fp128 on LA32.
      IsRetIndirect = true;
    }
  }

  // We must track the number of GARs and FARs used in order to conform to the
  // LoongArch ABI. As GAR usage is different for variadic arguments, we must
  // also track whether we are examining a vararg or not.
  int GARsLeft = IsRetIndirect ? NumGARs - 1 : NumGARs;
  int FARsLeft = FRLen ? NumFARs : 0;
  int NumFixedArgs = FI.getNumRequiredArgs();

  int ArgNum = 0;
  for (auto &ArgInfo : FI.arguments()) {
    ArgInfo.info = classifyArgumentType(
        ArgInfo.type, /*IsFixed=*/ArgNum < NumFixedArgs, GARsLeft, FARsLeft);
    ArgNum++;
  }
}

// Returns true if the struct is a potential candidate to be passed in FARs (and
// GARs). If this function returns true, the caller is responsible for checking
// that if there is only a single field then that field is a float.
bool LoongArchABIInfo::detectFARsEligibleStructHelper(
    QualType Ty, CharUnits CurOff, llvm::Type *&Field1Ty, CharUnits &Field1Off,
    llvm::Type *&Field2Ty, CharUnits &Field2Off) const {
  bool IsInt = Ty->isIntegralOrEnumerationType();
  bool IsFloat = Ty->isRealFloatingType();

  if (IsInt || IsFloat) {
    uint64_t Size = getContext().getTypeSize(Ty);
    if (IsInt && Size > GRLen)
      return false;
    // Can't be eligible if larger than the FP registers. Half precision isn't
    // currently supported on LoongArch and the ABI hasn't been confirmed, so
    // default to the integer ABI in that case.
    if (IsFloat && (Size > FRLen || Size < 32))
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
    if (getContext().getTypeSize(EltTy) > FRLen)
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
    // Non-zero-length arrays of empty records make the struct ineligible to be
    // passed via FARs in C++.
    if (const auto *RTy = EltTy->getAs<RecordType>()) {
      if (ArraySize != 0 && isa<CXXRecordDecl>(RTy->getDecl()) &&
          isEmptyRecord(getContext(), EltTy, true, true))
        return false;
    }
    CharUnits EltSize = getContext().getTypeSizeInChars(EltTy);
    for (uint64_t i = 0; i < ArraySize; ++i) {
      if (!detectFARsEligibleStructHelper(EltTy, CurOff, Field1Ty, Field1Off,
                                          Field2Ty, Field2Off))
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
    const RecordDecl *RD = RTy->getDecl();
    if (isEmptyRecord(getContext(), Ty, true, true) &&
        (!RD->isUnion() || !isa<CXXRecordDecl>(RD)))
      return true;
    // Unions aren't eligible unless they're empty in C (which is caught above).
    if (RD->isUnion())
      return false;
    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const CXXBaseSpecifier &B : CXXRD->bases()) {
        const auto *BDecl =
            cast<CXXRecordDecl>(B.getType()->castAs<RecordType>()->getDecl());
        if (!detectFARsEligibleStructHelper(
                B.getType(), CurOff + Layout.getBaseClassOffset(BDecl),
                Field1Ty, Field1Off, Field2Ty, Field2Off))
          return false;
      }
    }
    for (const FieldDecl *FD : RD->fields()) {
      QualType QTy = FD->getType();
      if (FD->isBitField()) {
        unsigned BitWidth = FD->getBitWidthValue(getContext());
        // Zero-width bitfields are ignored.
        if (BitWidth == 0)
          continue;
        // Allow a bitfield with a type greater than GRLen as long as the
        // bitwidth is GRLen or less.
        if (getContext().getTypeSize(QTy) > GRLen && BitWidth <= GRLen) {
          QTy = getContext().getIntTypeForBitwidth(GRLen, false);
        }
      }

      if (!detectFARsEligibleStructHelper(
              QTy,
              CurOff + getContext().toCharUnitsFromBits(
                           Layout.getFieldOffset(FD->getFieldIndex())),
              Field1Ty, Field1Off, Field2Ty, Field2Off))
        return false;
    }
    return Field1Ty != nullptr;
  }

  return false;
}

// Determine if a struct is eligible to be passed in FARs (and GARs) (i.e., when
// flattened it contains a single fp value, fp+fp, or int+fp of appropriate
// size). If so, NeededFARs and NeededGARs are incremented appropriately.
bool LoongArchABIInfo::detectFARsEligibleStruct(
    QualType Ty, llvm::Type *&Field1Ty, CharUnits &Field1Off,
    llvm::Type *&Field2Ty, CharUnits &Field2Off, int &NeededGARs,
    int &NeededFARs) const {
  Field1Ty = nullptr;
  Field2Ty = nullptr;
  NeededGARs = 0;
  NeededFARs = 0;
  if (!detectFARsEligibleStructHelper(Ty, CharUnits::Zero(), Field1Ty,
                                      Field1Off, Field2Ty, Field2Off))
    return false;
  if (!Field1Ty)
    return false;
  // Not really a candidate if we have a single int but no float.
  if (Field1Ty && !Field2Ty && !Field1Ty->isFloatingPointTy())
    return false;
  if (Field1Ty && Field1Ty->isFloatingPointTy())
    NeededFARs++;
  else if (Field1Ty)
    NeededGARs++;
  if (Field2Ty && Field2Ty->isFloatingPointTy())
    NeededFARs++;
  else if (Field2Ty)
    NeededGARs++;
  return true;
}

// Call getCoerceAndExpand for the two-element flattened struct described by
// Field1Ty, Field1Off, Field2Ty, Field2Off. This method will create an
// appropriate coerceToType and unpaddedCoerceToType.
ABIArgInfo LoongArchABIInfo::coerceAndExpandFARsEligibleStruct(
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
  CharUnits Field1End =
      Field1Off +
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

  return ABIArgInfo::getCoerceAndExpand(
      llvm::StructType::get(getVMContext(), CoerceElts, IsPacked),
      llvm::StructType::get(getVMContext(), UnpaddedCoerceElts, IsPacked));
}

ABIArgInfo LoongArchABIInfo::classifyArgumentType(QualType Ty, bool IsFixed,
                                                  int &GARsLeft,
                                                  int &FARsLeft) const {
  assert(GARsLeft <= NumGARs && "GAR tracking underflow");
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always passed indirectly.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    if (GARsLeft)
      GARsLeft -= 1;
    return getNaturalAlignIndirect(Ty, /*ByVal=*/RAA ==
                                           CGCXXABI::RAA_DirectInMemory);
  }

  uint64_t Size = getContext().getTypeSize(Ty);

  // Ignore empty struct or union whose size is zero, e.g. `struct { }` in C or
  // `struct { int a[0]; }` in C++. In C++, `struct { }` is empty but it's size
  // is 1 byte and g++ doesn't ignore it; clang++ matches this behaviour.
  if (isEmptyRecord(getContext(), Ty, true) && Size == 0)
    return ABIArgInfo::getIgnore();

  // Pass floating point values via FARs if possible.
  if (IsFixed && Ty->isFloatingType() && !Ty->isComplexType() &&
      FRLen >= Size && FARsLeft) {
    FARsLeft--;
    return ABIArgInfo::getDirect();
  }

  // Complex types for the *f or *d ABI must be passed directly rather than
  // using CoerceAndExpand.
  if (IsFixed && Ty->isComplexType() && FRLen && FARsLeft >= 2) {
    QualType EltTy = Ty->castAs<ComplexType>()->getElementType();
    if (getContext().getTypeSize(EltTy) <= FRLen) {
      FARsLeft -= 2;
      return ABIArgInfo::getDirect();
    }
  }

  if (IsFixed && FRLen && Ty->isStructureOrClassType()) {
    llvm::Type *Field1Ty = nullptr;
    llvm::Type *Field2Ty = nullptr;
    CharUnits Field1Off = CharUnits::Zero();
    CharUnits Field2Off = CharUnits::Zero();
    int NeededGARs = 0;
    int NeededFARs = 0;
    bool IsCandidate = detectFARsEligibleStruct(
        Ty, Field1Ty, Field1Off, Field2Ty, Field2Off, NeededGARs, NeededFARs);
    if (IsCandidate && NeededGARs <= GARsLeft && NeededFARs <= FARsLeft) {
      GARsLeft -= NeededGARs;
      FARsLeft -= NeededFARs;
      return coerceAndExpandFARsEligibleStruct(Field1Ty, Field1Off, Field2Ty,
                                               Field2Off);
    }
  }

  uint64_t NeededAlign = getContext().getTypeAlign(Ty);
  // Determine the number of GARs needed to pass the current argument
  // according to the ABI. 2*GRLen-aligned varargs are passed in "aligned"
  // register pairs, so may consume 3 registers.
  int NeededGARs = 1;
  if (!IsFixed && NeededAlign == 2 * GRLen)
    NeededGARs = 2 + (GARsLeft % 2);
  else if (Size > GRLen && Size <= 2 * GRLen)
    NeededGARs = 2;

  if (NeededGARs > GARsLeft)
    NeededGARs = GARsLeft;

  GARsLeft -= NeededGARs;

  if (!isAggregateTypeForABI(Ty) && !Ty->isVectorType()) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    // All integral types are promoted to GRLen width.
    if (Size < GRLen && Ty->isIntegralOrEnumerationType())
      return extendType(Ty);

    if (const auto *EIT = Ty->getAs<BitIntType>()) {
      if (EIT->getNumBits() < GRLen)
        return extendType(Ty);
      if (EIT->getNumBits() > 128 ||
          (!getContext().getTargetInfo().hasInt128Type() &&
           EIT->getNumBits() > 64))
        return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    }

    return ABIArgInfo::getDirect();
  }

  // Aggregates which are <= 2*GRLen will be passed in registers if possible,
  // so coerce to integers.
  if (Size <= 2 * GRLen) {
    // Use a single GRLen int if possible, 2*GRLen if 2*GRLen alignment is
    // required, and a 2-element GRLen array if only GRLen alignment is
    // required.
    if (Size <= GRLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), GRLen));
    }
    if (getContext().getTypeAlign(Ty) == 2 * GRLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), 2 * GRLen));
    }
    return ABIArgInfo::getDirect(
        llvm::ArrayType::get(llvm::IntegerType::get(getVMContext(), GRLen), 2));
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo LoongArchABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();
  // The rules for return and argument types are the same, so defer to
  // classifyArgumentType.
  int GARsLeft = 2;
  int FARsLeft = FRLen ? 2 : 0;
  return classifyArgumentType(RetTy, /*IsFixed=*/true, GARsLeft, FARsLeft);
}

RValue LoongArchABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                   QualType Ty, AggValueSlot Slot) const {
  CharUnits SlotSize = CharUnits::fromQuantity(GRLen / 8);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true))
    return Slot.asRValue();

  auto TInfo = getContext().getTypeInfoInChars(Ty);

  // Arguments bigger than 2*GRLen bytes are passed indirectly.
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty,
                          /*IsIndirect=*/TInfo.Width > 2 * SlotSize, TInfo,
                          SlotSize,
                          /*AllowHigherAlign=*/true, Slot);
}

ABIArgInfo LoongArchABIInfo::extendType(QualType Ty) const {
  int TySize = getContext().getTypeSize(Ty);
  // LA64 ABI requires unsigned 32 bit integers to be sign extended.
  if (GRLen == 64 && Ty->isUnsignedIntegerOrEnumerationType() && TySize == 32)
    return ABIArgInfo::getSignExtend(Ty);
  return ABIArgInfo::getExtend(Ty);
}

namespace {
class LoongArchTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  LoongArchTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, unsigned GRLen,
                             unsigned FRLen)
      : TargetCodeGenInfo(
            std::make_unique<LoongArchABIInfo>(CGT, GRLen, FRLen)) {}
};
} // namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createLoongArchTargetCodeGenInfo(CodeGenModule &CGM, unsigned GRLen,
                                          unsigned FLen) {
  return std::make_unique<LoongArchTargetCodeGenInfo>(CGM.getTypes(), GRLen,
                                                      FLen);
}
