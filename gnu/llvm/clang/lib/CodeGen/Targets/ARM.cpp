//===- ARM.cpp ------------------------------------------------------------===//
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
// ARM ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class ARMABIInfo : public ABIInfo {
  ARMABIKind Kind;
  bool IsFloatABISoftFP;

public:
  ARMABIInfo(CodeGenTypes &CGT, ARMABIKind Kind) : ABIInfo(CGT), Kind(Kind) {
    setCCs();
    IsFloatABISoftFP = CGT.getCodeGenOpts().FloatABI == "softfp" ||
        CGT.getCodeGenOpts().FloatABI == ""; // default
  }

  bool isEABI() const {
    switch (getTarget().getTriple().getEnvironment()) {
    case llvm::Triple::Android:
    case llvm::Triple::EABI:
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABI:
    case llvm::Triple::GNUEABIT64:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABIHFT64:
    case llvm::Triple::MuslEABI:
    case llvm::Triple::MuslEABIHF:
      return true;
    default:
      return getTarget().getTriple().isOHOSFamily();
    }
  }

  bool isEABIHF() const {
    switch (getTarget().getTriple().getEnvironment()) {
    case llvm::Triple::EABIHF:
    case llvm::Triple::GNUEABIHF:
    case llvm::Triple::GNUEABIHFT64:
    case llvm::Triple::MuslEABIHF:
      return true;
    default:
      return false;
    }
  }

  ARMABIKind getABIKind() const { return Kind; }

  bool allowBFloatArgsAndRet() const override {
    return !IsFloatABISoftFP && getTarget().hasBFloat16Type();
  }

private:
  ABIArgInfo classifyReturnType(QualType RetTy, bool isVariadic,
                                unsigned functionCallConv) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, bool isVariadic,
                                  unsigned functionCallConv) const;
  ABIArgInfo classifyHomogeneousAggregate(QualType Ty, const Type *Base,
                                          uint64_t Members) const;
  ABIArgInfo coerceIllegalVector(QualType Ty) const;
  bool isIllegalVectorType(QualType Ty) const;
  bool containsAnyFP16Vectors(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;
  bool isZeroLengthBitfieldPermittedInHomogeneousAggregate() const override;

  bool isEffectivelyAAPCS_VFP(unsigned callConvention, bool acceptHalf) const;

  void computeInfo(CGFunctionInfo &FI) const override;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  llvm::CallingConv::ID getLLVMDefaultCC() const;
  llvm::CallingConv::ID getABIDefaultCC() const;
  void setCCs();
};

class ARMSwiftABIInfo : public SwiftABIInfo {
public:
  explicit ARMSwiftABIInfo(CodeGenTypes &CGT)
      : SwiftABIInfo(CGT, /*SwiftErrorInRegister=*/true) {}

  bool isLegalVectorType(CharUnits VectorSize, llvm::Type *EltTy,
                         unsigned NumElts) const override;
};

class ARMTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  ARMTargetCodeGenInfo(CodeGenTypes &CGT, ARMABIKind K)
      : TargetCodeGenInfo(std::make_unique<ARMABIInfo>(CGT, K)) {
    SwiftInfo = std::make_unique<ARMSwiftABIInfo>(CGT);
  }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 13;
  }

  StringRef getARCRetainAutoreleasedReturnValueMarker() const override {
    return "mov\tr7, r7\t\t// marker for objc_retainAutoreleaseReturnValue";
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Four8 = llvm::ConstantInt::get(CGF.Int8Ty, 4);

    // 0-15 are the 16 integer registers.
    AssignToArrayRange(CGF.Builder, Address, Four8, 0, 15);
    return false;
  }

  unsigned getSizeOfUnwindException() const override {
    if (getABIInfo<ARMABIInfo>().isEABI())
      return 88;
    return TargetCodeGenInfo::getSizeOfUnwindException();
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;
    auto *Fn = cast<llvm::Function>(GV);

    if (const auto *TA = FD->getAttr<TargetAttr>()) {
      ParsedTargetAttr Attr =
          CGM.getTarget().parseTargetAttr(TA->getFeaturesStr());
      if (!Attr.BranchProtection.empty()) {
        TargetInfo::BranchProtectionInfo BPI{};
        StringRef DiagMsg;
        StringRef Arch =
            Attr.CPU.empty() ? CGM.getTarget().getTargetOpts().CPU : Attr.CPU;
        if (!CGM.getTarget().validateBranchProtection(Attr.BranchProtection,
                                                      Arch, BPI, DiagMsg)) {
          CGM.getDiags().Report(
              D->getLocation(),
              diag::warn_target_unsupported_branch_protection_attribute)
              << Arch;
        } else
          setBranchProtectionFnAttributes(BPI, (*Fn));
      } else if (CGM.getLangOpts().BranchTargetEnforcement ||
                 CGM.getLangOpts().hasSignReturnAddress()) {
        // If the Branch Protection attribute is missing, validate the target
        // Architecture attribute against Branch Protection command line
        // settings.
        if (!CGM.getTarget().isBranchProtectionSupportedArch(Attr.CPU))
          CGM.getDiags().Report(
              D->getLocation(),
              diag::warn_target_unsupported_branch_protection_attribute)
              << Attr.CPU;
      }
    } else if (CGM.getTarget().isBranchProtectionSupportedArch(
                   CGM.getTarget().getTargetOpts().CPU)) {
      TargetInfo::BranchProtectionInfo BPI(CGM.getLangOpts());
      setBranchProtectionFnAttributes(BPI, (*Fn));
    }

    const ARMInterruptAttr *Attr = FD->getAttr<ARMInterruptAttr>();
    if (!Attr)
      return;

    const char *Kind;
    switch (Attr->getInterrupt()) {
    case ARMInterruptAttr::Generic: Kind = ""; break;
    case ARMInterruptAttr::IRQ:     Kind = "IRQ"; break;
    case ARMInterruptAttr::FIQ:     Kind = "FIQ"; break;
    case ARMInterruptAttr::SWI:     Kind = "SWI"; break;
    case ARMInterruptAttr::ABORT:   Kind = "ABORT"; break;
    case ARMInterruptAttr::UNDEF:   Kind = "UNDEF"; break;
    }

    Fn->addFnAttr("interrupt", Kind);

    ARMABIKind ABI = getABIInfo<ARMABIInfo>().getABIKind();
    if (ABI == ARMABIKind::APCS)
      return;

    // AAPCS guarantees that sp will be 8-byte aligned on any public interface,
    // however this is not necessarily true on taking any interrupt. Instruct
    // the backend to perform a realignment as part of the function prologue.
    llvm::AttrBuilder B(Fn->getContext());
    B.addStackAlignmentAttr(8);
    Fn->addFnAttrs(B);
  }
};

class WindowsARMTargetCodeGenInfo : public ARMTargetCodeGenInfo {
public:
  WindowsARMTargetCodeGenInfo(CodeGenTypes &CGT, ARMABIKind K)
      : ARMTargetCodeGenInfo(CGT, K) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:" + qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name, llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};

void WindowsARMTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  ARMTargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  addStackProbeTargetAttributes(D, GV, CGM);
}
}

void ARMABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!::classifyReturnType(getCXXABI(), FI, *this))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType(), FI.isVariadic(),
                                            FI.getCallingConvention());

  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type, FI.isVariadic(),
                                  FI.getCallingConvention());


  // Always honor user-specified calling convention.
  if (FI.getCallingConvention() != llvm::CallingConv::C)
    return;

  llvm::CallingConv::ID cc = getRuntimeCC();
  if (cc != llvm::CallingConv::C)
    FI.setEffectiveCallingConvention(cc);
}

/// Return the default calling convention that LLVM will use.
llvm::CallingConv::ID ARMABIInfo::getLLVMDefaultCC() const {
  // The default calling convention that LLVM will infer.
  if (isEABIHF() || getTarget().getTriple().isWatchABI())
    return llvm::CallingConv::ARM_AAPCS_VFP;
  else if (isEABI())
    return llvm::CallingConv::ARM_AAPCS;
  else
    return llvm::CallingConv::ARM_APCS;
}

/// Return the calling convention that our ABI would like us to use
/// as the C calling convention.
llvm::CallingConv::ID ARMABIInfo::getABIDefaultCC() const {
  switch (getABIKind()) {
  case ARMABIKind::APCS:
    return llvm::CallingConv::ARM_APCS;
  case ARMABIKind::AAPCS:
    return llvm::CallingConv::ARM_AAPCS;
  case ARMABIKind::AAPCS_VFP:
    return llvm::CallingConv::ARM_AAPCS_VFP;
  case ARMABIKind::AAPCS16_VFP:
    return llvm::CallingConv::ARM_AAPCS_VFP;
  }
  llvm_unreachable("bad ABI kind");
}

void ARMABIInfo::setCCs() {
  assert(getRuntimeCC() == llvm::CallingConv::C);

  // Don't muddy up the IR with a ton of explicit annotations if
  // they'd just match what LLVM will infer from the triple.
  llvm::CallingConv::ID abiCC = getABIDefaultCC();
  if (abiCC != getLLVMDefaultCC())
    RuntimeCC = abiCC;
}

ABIArgInfo ARMABIInfo::coerceIllegalVector(QualType Ty) const {
  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size <= 32) {
    llvm::Type *ResType =
        llvm::Type::getInt32Ty(getVMContext());
    return ABIArgInfo::getDirect(ResType);
  }
  if (Size == 64 || Size == 128) {
    auto *ResType = llvm::FixedVectorType::get(
        llvm::Type::getInt32Ty(getVMContext()), Size / 32);
    return ABIArgInfo::getDirect(ResType);
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo ARMABIInfo::classifyHomogeneousAggregate(QualType Ty,
                                                    const Type *Base,
                                                    uint64_t Members) const {
  assert(Base && "Base class should be set for homogeneous aggregate");
  // Base can be a floating-point or a vector.
  if (const VectorType *VT = Base->getAs<VectorType>()) {
    // FP16 vectors should be converted to integer vectors
    if (!getTarget().hasLegalHalfType() && containsAnyFP16Vectors(Ty)) {
      uint64_t Size = getContext().getTypeSize(VT);
      auto *NewVecTy = llvm::FixedVectorType::get(
          llvm::Type::getInt32Ty(getVMContext()), Size / 32);
      llvm::Type *Ty = llvm::ArrayType::get(NewVecTy, Members);
      return ABIArgInfo::getDirect(Ty, 0, nullptr, false);
    }
  }
  unsigned Align = 0;
  if (getABIKind() == ARMABIKind::AAPCS ||
      getABIKind() == ARMABIKind::AAPCS_VFP) {
    // For alignment adjusted HFAs, cap the argument alignment to 8, leave it
    // default otherwise.
    Align = getContext().getTypeUnadjustedAlignInChars(Ty).getQuantity();
    unsigned BaseAlign = getContext().getTypeAlignInChars(Base).getQuantity();
    Align = (Align > BaseAlign && Align >= 8) ? 8 : 0;
  }
  return ABIArgInfo::getDirect(nullptr, 0, nullptr, false, Align);
}

ABIArgInfo ARMABIInfo::classifyArgumentType(QualType Ty, bool isVariadic,
                                            unsigned functionCallConv) const {
  // 6.1.2.1 The following argument types are VFP CPRCs:
  //   A single-precision floating-point type (including promoted
  //   half-precision types); A double-precision floating-point type;
  //   A 64-bit or 128-bit containerized vector type; Homogeneous Aggregate
  //   with a Base Type of a single- or double-precision floating-point type,
  //   64-bit containerized vectors or 128-bit containerized vectors with one
  //   to four Elements.
  // Variadic functions should always marshal to the base standard.
  bool IsAAPCS_VFP =
      !isVariadic && isEffectivelyAAPCS_VFP(functionCallConv, /* AAPCS16 */ false);

  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Handle illegal vector types here.
  if (isIllegalVectorType(Ty))
    return coerceIllegalVector(Ty);

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>()) {
      Ty = EnumTy->getDecl()->getIntegerType();
    }

    if (const auto *EIT = Ty->getAs<BitIntType>())
      if (EIT->getNumBits() > 64)
        return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
  }

  // Ignore empty records.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  if (IsAAPCS_VFP) {
    // Homogeneous Aggregates need to be expanded when we can fit the aggregate
    // into VFP registers.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(Ty, Base, Members))
      return classifyHomogeneousAggregate(Ty, Base, Members);
  } else if (getABIKind() == ARMABIKind::AAPCS16_VFP) {
    // WatchOS does have homogeneous aggregates. Note that we intentionally use
    // this convention even for a variadic function: the backend will use GPRs
    // if needed.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(Ty, Base, Members)) {
      assert(Base && Members <= 4 && "unexpected homogeneous aggregate");
      llvm::Type *Ty =
        llvm::ArrayType::get(CGT.ConvertType(QualType(Base, 0)), Members);
      return ABIArgInfo::getDirect(Ty, 0, nullptr, false);
    }
  }

  if (getABIKind() == ARMABIKind::AAPCS16_VFP &&
      getContext().getTypeSizeInChars(Ty) > CharUnits::fromQuantity(16)) {
    // WatchOS is adopting the 64-bit AAPCS rule on composite types: if they're
    // bigger than 128-bits, they get placed in space allocated by the caller,
    // and a pointer is passed.
    return ABIArgInfo::getIndirect(
        CharUnits::fromQuantity(getContext().getTypeAlign(Ty) / 8), false);
  }

  // Support byval for ARM.
  // The ABI alignment for APCS is 4-byte and for AAPCS at least 4-byte and at
  // most 8-byte. We realign the indirect argument if type alignment is bigger
  // than ABI alignment.
  uint64_t ABIAlign = 4;
  uint64_t TyAlign;
  if (getABIKind() == ARMABIKind::AAPCS_VFP ||
      getABIKind() == ARMABIKind::AAPCS) {
    TyAlign = getContext().getTypeUnadjustedAlignInChars(Ty).getQuantity();
    ABIAlign = std::clamp(TyAlign, (uint64_t)4, (uint64_t)8);
  } else {
    TyAlign = getContext().getTypeAlignInChars(Ty).getQuantity();
  }
  if (getContext().getTypeSizeInChars(Ty) > CharUnits::fromQuantity(64)) {
    assert(getABIKind() != ARMABIKind::AAPCS16_VFP && "unexpected byval");
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(ABIAlign),
                                   /*ByVal=*/true,
                                   /*Realign=*/TyAlign > ABIAlign);
  }

  // On RenderScript, coerce Aggregates <= 64 bytes to an integer array of
  // same size and alignment.
  if (getTarget().isRenderScriptTarget()) {
    return coerceToIntArray(Ty, getContext(), getVMContext());
  }

  // Otherwise, pass by coercing to a structure of the appropriate size.
  llvm::Type* ElemTy;
  unsigned SizeRegs;
  // FIXME: Try to match the types of the arguments more accurately where
  // we can.
  if (TyAlign <= 4) {
    ElemTy = llvm::Type::getInt32Ty(getVMContext());
    SizeRegs = (getContext().getTypeSize(Ty) + 31) / 32;
  } else {
    ElemTy = llvm::Type::getInt64Ty(getVMContext());
    SizeRegs = (getContext().getTypeSize(Ty) + 63) / 64;
  }

  return ABIArgInfo::getDirect(llvm::ArrayType::get(ElemTy, SizeRegs));
}

static bool isIntegerLikeType(QualType Ty, ASTContext &Context,
                              llvm::LLVMContext &VMContext) {
  // APCS, C Language Calling Conventions, Non-Simple Return Values: A structure
  // is called integer-like if its size is less than or equal to one word, and
  // the offset of each of its addressable sub-fields is zero.

  uint64_t Size = Context.getTypeSize(Ty);

  // Check that the type fits in a word.
  if (Size > 32)
    return false;

  // FIXME: Handle vector types!
  if (Ty->isVectorType())
    return false;

  // Float types are never treated as "integer like".
  if (Ty->isRealFloatingType())
    return false;

  // If this is a builtin or pointer type then it is ok.
  if (Ty->getAs<BuiltinType>() || Ty->isPointerType())
    return true;

  // Small complex integer types are "integer like".
  if (const ComplexType *CT = Ty->getAs<ComplexType>())
    return isIntegerLikeType(CT->getElementType(), Context, VMContext);

  // Single element and zero sized arrays should be allowed, by the definition
  // above, but they are not.

  // Otherwise, it must be a record type.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT) return false;

  // Ignore records with flexible arrays.
  const RecordDecl *RD = RT->getDecl();
  if (RD->hasFlexibleArrayMember())
    return false;

  // Check that all sub-fields are at offset 0, and are themselves "integer
  // like".
  const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

  bool HadField = false;
  unsigned idx = 0;
  for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
       i != e; ++i, ++idx) {
    const FieldDecl *FD = *i;

    // Bit-fields are not addressable, we only need to verify they are "integer
    // like". We still have to disallow a subsequent non-bitfield, for example:
    //   struct { int : 0; int x }
    // is non-integer like according to gcc.
    if (FD->isBitField()) {
      if (!RD->isUnion())
        HadField = true;

      if (!isIntegerLikeType(FD->getType(), Context, VMContext))
        return false;

      continue;
    }

    // Check if this field is at offset 0.
    if (Layout.getFieldOffset(idx) != 0)
      return false;

    if (!isIntegerLikeType(FD->getType(), Context, VMContext))
      return false;

    // Only allow at most one field in a structure. This doesn't match the
    // wording above, but follows gcc in situations with a field following an
    // empty structure.
    if (!RD->isUnion()) {
      if (HadField)
        return false;

      HadField = true;
    }
  }

  return true;
}

ABIArgInfo ARMABIInfo::classifyReturnType(QualType RetTy, bool isVariadic,
                                          unsigned functionCallConv) const {

  // Variadic functions should always marshal to the base standard.
  bool IsAAPCS_VFP =
      !isVariadic && isEffectivelyAAPCS_VFP(functionCallConv, /* AAPCS16 */ true);

  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const VectorType *VT = RetTy->getAs<VectorType>()) {
    // Large vector types should be returned via memory.
    if (getContext().getTypeSize(RetTy) > 128)
      return getNaturalAlignIndirect(RetTy);
    // TODO: FP16/BF16 vectors should be converted to integer vectors
    // This check is similar  to isIllegalVectorType - refactor?
    if ((!getTarget().hasLegalHalfType() &&
        (VT->getElementType()->isFloat16Type() ||
         VT->getElementType()->isHalfType())) ||
        (IsFloatABISoftFP &&
         VT->getElementType()->isBFloat16Type()))
      return coerceIllegalVector(RetTy);
  }

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    if (const auto *EIT = RetTy->getAs<BitIntType>())
      if (EIT->getNumBits() > 64)
        return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);

    return isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                                : ABIArgInfo::getDirect();
  }

  // Are we following APCS?
  if (getABIKind() == ARMABIKind::APCS) {
    if (isEmptyRecord(getContext(), RetTy, false))
      return ABIArgInfo::getIgnore();

    // Complex types are all returned as packed integers.
    //
    // FIXME: Consider using 2 x vector types if the back end handles them
    // correctly.
    if (RetTy->isAnyComplexType())
      return ABIArgInfo::getDirect(llvm::IntegerType::get(
          getVMContext(), getContext().getTypeSize(RetTy)));

    // Integer like structures are returned in r0.
    if (isIntegerLikeType(RetTy, getContext(), getVMContext())) {
      // Return in the smallest viable integer type.
      uint64_t Size = getContext().getTypeSize(RetTy);
      if (Size <= 8)
        return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
      if (Size <= 16)
        return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
    }

    // Otherwise return in memory.
    return getNaturalAlignIndirect(RetTy);
  }

  // Otherwise this is an AAPCS variant.

  if (isEmptyRecord(getContext(), RetTy, true))
    return ABIArgInfo::getIgnore();

  // Check for homogeneous aggregates with AAPCS-VFP.
  if (IsAAPCS_VFP) {
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (isHomogeneousAggregate(RetTy, Base, Members))
      return classifyHomogeneousAggregate(RetTy, Base, Members);
  }

  // Aggregates <= 4 bytes are returned in r0; other aggregates
  // are returned indirectly.
  uint64_t Size = getContext().getTypeSize(RetTy);
  if (Size <= 32) {
    // On RenderScript, coerce Aggregates <= 4 bytes to an integer array of
    // same size and alignment.
    if (getTarget().isRenderScriptTarget()) {
      return coerceToIntArray(RetTy, getContext(), getVMContext());
    }
    if (getDataLayout().isBigEndian())
      // Return in 32 bit integer integer type (as if loaded by LDR, AAPCS 5.4)
      return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));

    // Return in the smallest viable integer type.
    if (Size <= 8)
      return ABIArgInfo::getDirect(llvm::Type::getInt8Ty(getVMContext()));
    if (Size <= 16)
      return ABIArgInfo::getDirect(llvm::Type::getInt16Ty(getVMContext()));
    return ABIArgInfo::getDirect(llvm::Type::getInt32Ty(getVMContext()));
  } else if (Size <= 128 && getABIKind() == ARMABIKind::AAPCS16_VFP) {
    llvm::Type *Int32Ty = llvm::Type::getInt32Ty(getVMContext());
    llvm::Type *CoerceTy =
        llvm::ArrayType::get(Int32Ty, llvm::alignTo(Size, 32) / 32);
    return ABIArgInfo::getDirect(CoerceTy);
  }

  return getNaturalAlignIndirect(RetTy);
}

/// isIllegalVector - check whether Ty is an illegal vector type.
bool ARMABIInfo::isIllegalVectorType(QualType Ty) const {
  if (const VectorType *VT = Ty->getAs<VectorType> ()) {
    // On targets that don't support half, fp16 or bfloat, they are expanded
    // into float, and we don't want the ABI to depend on whether or not they
    // are supported in hardware. Thus return false to coerce vectors of these
    // types into integer vectors.
    // We do not depend on hasLegalHalfType for bfloat as it is a
    // separate IR type.
    if ((!getTarget().hasLegalHalfType() &&
        (VT->getElementType()->isFloat16Type() ||
         VT->getElementType()->isHalfType())) ||
        (IsFloatABISoftFP &&
         VT->getElementType()->isBFloat16Type()))
      return true;
    if (isAndroid()) {
      // Android shipped using Clang 3.1, which supported a slightly different
      // vector ABI. The primary differences were that 3-element vector types
      // were legal, and so were sub 32-bit vectors (i.e. <2 x i8>). This path
      // accepts that legacy behavior for Android only.
      // Check whether VT is legal.
      unsigned NumElements = VT->getNumElements();
      // NumElements should be power of 2 or equal to 3.
      if (!llvm::isPowerOf2_32(NumElements) && NumElements != 3)
        return true;
    } else {
      // Check whether VT is legal.
      unsigned NumElements = VT->getNumElements();
      uint64_t Size = getContext().getTypeSize(VT);
      // NumElements should be power of 2.
      if (!llvm::isPowerOf2_32(NumElements))
        return true;
      // Size should be greater than 32 bits.
      return Size <= 32;
    }
  }
  return false;
}

/// Return true if a type contains any 16-bit floating point vectors
bool ARMABIInfo::containsAnyFP16Vectors(QualType Ty) const {
  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    uint64_t NElements = AT->getZExtSize();
    if (NElements == 0)
      return false;
    return containsAnyFP16Vectors(AT->getElementType());
  } else if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      if (llvm::any_of(CXXRD->bases(), [this](const CXXBaseSpecifier &B) {
            return containsAnyFP16Vectors(B.getType());
          }))
        return true;

    if (llvm::any_of(RD->fields(), [this](FieldDecl *FD) {
          return FD && containsAnyFP16Vectors(FD->getType());
        }))
      return true;

    return false;
  } else {
    if (const VectorType *VT = Ty->getAs<VectorType>())
      return (VT->getElementType()->isFloat16Type() ||
              VT->getElementType()->isBFloat16Type() ||
              VT->getElementType()->isHalfType());
    return false;
  }
}

bool ARMSwiftABIInfo::isLegalVectorType(CharUnits VectorSize, llvm::Type *EltTy,
                                        unsigned NumElts) const {
  if (!llvm::isPowerOf2_32(NumElts))
    return false;
  unsigned size = CGT.getDataLayout().getTypeStoreSizeInBits(EltTy);
  if (size > 64)
    return false;
  if (VectorSize.getQuantity() != 8 &&
      (VectorSize.getQuantity() != 16 || NumElts == 1))
    return false;
  return true;
}

bool ARMABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for AAPCS-VFP must have base types of float,
  // double, or 64-bit or 128-bit vectors.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->getKind() == BuiltinType::Float ||
        BT->getKind() == BuiltinType::Double ||
        BT->getKind() == BuiltinType::LongDouble)
      return true;
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    unsigned VecSize = getContext().getTypeSize(VT);
    if (VecSize == 64 || VecSize == 128)
      return true;
  }
  return false;
}

bool ARMABIInfo::isHomogeneousAggregateSmallEnough(const Type *Base,
                                                   uint64_t Members) const {
  return Members <= 4;
}

bool ARMABIInfo::isZeroLengthBitfieldPermittedInHomogeneousAggregate() const {
  // AAPCS32 says that the rule for whether something is a homogeneous
  // aggregate is applied to the output of the data layout decision. So
  // anything that doesn't affect the data layout also does not affect
  // homogeneity. In particular, zero-length bitfields don't stop a struct
  // being homogeneous.
  return true;
}

bool ARMABIInfo::isEffectivelyAAPCS_VFP(unsigned callConvention,
                                        bool acceptHalf) const {
  // Give precedence to user-specified calling conventions.
  if (callConvention != llvm::CallingConv::C)
    return (callConvention == llvm::CallingConv::ARM_AAPCS_VFP);
  else
    return (getABIKind() == ARMABIKind::AAPCS_VFP) ||
           (acceptHalf && (getABIKind() == ARMABIKind::AAPCS16_VFP));
}

RValue ARMABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                             QualType Ty, AggValueSlot Slot) const {
  CharUnits SlotSize = CharUnits::fromQuantity(4);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true))
    return Slot.asRValue();

  CharUnits TySize = getContext().getTypeSizeInChars(Ty);
  CharUnits TyAlignForABI = getContext().getTypeUnadjustedAlignInChars(Ty);

  // Use indirect if size of the illegal vector is bigger than 16 bytes.
  bool IsIndirect = false;
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (TySize > CharUnits::fromQuantity(16) && isIllegalVectorType(Ty)) {
    IsIndirect = true;

  // ARMv7k passes structs bigger than 16 bytes indirectly, in space
  // allocated by the caller.
  } else if (TySize > CharUnits::fromQuantity(16) &&
             getABIKind() == ARMABIKind::AAPCS16_VFP &&
             !isHomogeneousAggregate(Ty, Base, Members)) {
    IsIndirect = true;

  // Otherwise, bound the type's ABI alignment.
  // The ABI alignment for 64-bit or 128-bit vectors is 8 for AAPCS and 4 for
  // APCS. For AAPCS, the ABI alignment is at least 4-byte and at most 8-byte.
  // Our callers should be prepared to handle an under-aligned address.
  } else if (getABIKind() == ARMABIKind::AAPCS_VFP ||
             getABIKind() == ARMABIKind::AAPCS) {
    TyAlignForABI = std::max(TyAlignForABI, CharUnits::fromQuantity(4));
    TyAlignForABI = std::min(TyAlignForABI, CharUnits::fromQuantity(8));
  } else if (getABIKind() == ARMABIKind::AAPCS16_VFP) {
    // ARMv7k allows type alignment up to 16 bytes.
    TyAlignForABI = std::max(TyAlignForABI, CharUnits::fromQuantity(4));
    TyAlignForABI = std::min(TyAlignForABI, CharUnits::fromQuantity(16));
  } else {
    TyAlignForABI = CharUnits::fromQuantity(4);
  }

  TypeInfoChars TyInfo(TySize, TyAlignForABI, AlignRequirementKind::None);
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect, TyInfo, SlotSize,
                          /*AllowHigherAlign*/ true, Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createARMTargetCodeGenInfo(CodeGenModule &CGM, ARMABIKind Kind) {
  return std::make_unique<ARMTargetCodeGenInfo>(CGM.getTypes(), Kind);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createWindowsARMTargetCodeGenInfo(CodeGenModule &CGM, ARMABIKind K) {
  return std::make_unique<WindowsARMTargetCodeGenInfo>(CGM.getTypes(), K);
}
