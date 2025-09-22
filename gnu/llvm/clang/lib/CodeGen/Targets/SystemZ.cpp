//===- SystemZ.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/Basic/Builtins.h"
#include "llvm/IR/IntrinsicsS390.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// SystemZ ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class SystemZABIInfo : public ABIInfo {
  bool HasVector;
  bool IsSoftFloatABI;

public:
  SystemZABIInfo(CodeGenTypes &CGT, bool HV, bool SF)
      : ABIInfo(CGT), HasVector(HV), IsSoftFloatABI(SF) {}

  bool isPromotableIntegerTypeForABI(QualType Ty) const;
  bool isCompoundType(QualType Ty) const;
  bool isVectorArgumentType(QualType Ty) const;
  bool isFPArgumentType(QualType Ty) const;
  QualType GetSingleElementType(QualType Ty) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType ArgTy) const;

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class SystemZTargetCodeGenInfo : public TargetCodeGenInfo {
  ASTContext &Ctx;

  // These are used for speeding up the search for a visible vector ABI.
  mutable bool HasVisibleVecABIFlag = false;
  mutable std::set<const Type *> SeenTypes;

  // Returns true (the first time) if Ty is, or is found to include, a vector
  // type that exposes the vector ABI. This is any vector >=16 bytes which
  // with vector support are aligned to only 8 bytes. When IsParam is true,
  // the type belongs to a value as passed between functions. If it is a
  // vector <=16 bytes it will be passed in a vector register (if supported).
  bool isVectorTypeBased(const Type *Ty, bool IsParam) const;

public:
  SystemZTargetCodeGenInfo(CodeGenTypes &CGT, bool HasVector, bool SoftFloatABI)
      : TargetCodeGenInfo(
            std::make_unique<SystemZABIInfo>(CGT, HasVector, SoftFloatABI)),
            Ctx(CGT.getContext()) {
    SwiftInfo =
        std::make_unique<SwiftABIInfo>(CGT, /*SwiftErrorInRegister=*/false);
  }

  // The vector ABI is different when the vector facility is present and when
  // a module e.g. defines an externally visible vector variable, a flag
  // indicating a visible vector ABI is added. Eventually this will result in
  // a GNU attribute indicating the vector ABI of the module.  Ty is the type
  // of a variable or function parameter that is globally visible.
  void handleExternallyVisibleObjABI(const Type *Ty, CodeGen::CodeGenModule &M,
                                     bool IsParam) const {
    if (!HasVisibleVecABIFlag && isVectorTypeBased(Ty, IsParam)) {
      M.getModule().addModuleFlag(llvm::Module::Warning,
                                  "s390x-visible-vector-ABI", 1);
      HasVisibleVecABIFlag = true;
    }
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override {
    if (!D)
      return;

    // Check if the vector ABI becomes visible by an externally visible
    // variable or function.
    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (VD->isExternallyVisible())
        handleExternallyVisibleObjABI(VD->getType().getTypePtr(), M,
                                      /*IsParam*/false);
    }
    else if (const FunctionDecl *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isExternallyVisible())
        handleExternallyVisibleObjABI(FD->getType().getTypePtr(), M,
                                      /*IsParam*/false);
    }
  }

  llvm::Value *testFPKind(llvm::Value *V, unsigned BuiltinID,
                          CGBuilderTy &Builder,
                          CodeGenModule &CGM) const override {
    assert(V->getType()->isFloatingPointTy() && "V should have an FP type.");
    // Only use TDC in constrained FP mode.
    if (!Builder.getIsFPConstrained())
      return nullptr;

    llvm::Type *Ty = V->getType();
    if (Ty->isFloatTy() || Ty->isDoubleTy() || Ty->isFP128Ty()) {
      llvm::Module &M = CGM.getModule();
      auto &Ctx = M.getContext();
      llvm::Function *TDCFunc =
          llvm::Intrinsic::getDeclaration(&M, llvm::Intrinsic::s390_tdc, Ty);
      unsigned TDCBits = 0;
      switch (BuiltinID) {
      case Builtin::BI__builtin_isnan:
        TDCBits = 0xf;
        break;
      case Builtin::BIfinite:
      case Builtin::BI__finite:
      case Builtin::BIfinitef:
      case Builtin::BI__finitef:
      case Builtin::BIfinitel:
      case Builtin::BI__finitel:
      case Builtin::BI__builtin_isfinite:
        TDCBits = 0xfc0;
        break;
      case Builtin::BI__builtin_isinf:
        TDCBits = 0x30;
        break;
      default:
        break;
      }
      if (TDCBits)
        return Builder.CreateCall(
            TDCFunc,
            {V, llvm::ConstantInt::get(llvm::Type::getInt64Ty(Ctx), TDCBits)});
    }
    return nullptr;
  }
};
}

bool SystemZABIInfo::isPromotableIntegerTypeForABI(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Promotable integer types are required to be promoted by the ABI.
  if (ABIInfo::isPromotableIntegerTypeForABI(Ty))
    return true;

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() < 64)
      return true;

  // 32-bit values must also be promoted.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return true;
    default:
      return false;
    }
  return false;
}

bool SystemZABIInfo::isCompoundType(QualType Ty) const {
  return (Ty->isAnyComplexType() ||
          Ty->isVectorType() ||
          isAggregateTypeForABI(Ty));
}

bool SystemZABIInfo::isVectorArgumentType(QualType Ty) const {
  return (HasVector &&
          Ty->isVectorType() &&
          getContext().getTypeSize(Ty) <= 128);
}

bool SystemZABIInfo::isFPArgumentType(QualType Ty) const {
  if (IsSoftFloatABI)
    return false;

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Float:
    case BuiltinType::Double:
      return true;
    default:
      return false;
    }

  return false;
}

QualType SystemZABIInfo::GetSingleElementType(QualType Ty) const {
  const RecordType *RT = Ty->getAs<RecordType>();

  if (RT && RT->isStructureOrClassType()) {
    const RecordDecl *RD = RT->getDecl();
    QualType Found;

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      if (CXXRD->hasDefinition())
        for (const auto &I : CXXRD->bases()) {
          QualType Base = I.getType();

          // Empty bases don't affect things either way.
          if (isEmptyRecord(getContext(), Base, true))
            continue;

          if (!Found.isNull())
            return Ty;
          Found = GetSingleElementType(Base);
        }

    // Check the fields.
    for (const auto *FD : RD->fields()) {
      // Unlike isSingleElementStruct(), empty structure and array fields
      // do count.  So do anonymous bitfields that aren't zero-sized.

      // Like isSingleElementStruct(), ignore C++20 empty data members.
      if (FD->hasAttr<NoUniqueAddressAttr>() &&
          isEmptyRecord(getContext(), FD->getType(), true))
        continue;

      // Unlike isSingleElementStruct(), arrays do not count.
      // Nested structures still do though.
      if (!Found.isNull())
        return Ty;
      Found = GetSingleElementType(FD->getType());
    }

    // Unlike isSingleElementStruct(), trailing padding is allowed.
    // An 8-byte aligned struct s { float f; } is passed as a double.
    if (!Found.isNull())
      return Found;
  }

  return Ty;
}

RValue SystemZABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty, AggValueSlot Slot) const {
  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i64 __gpr;
  //   i64 __fpr;
  //   i8 *__overflow_arg_area;
  //   i8 *__reg_save_area;
  // };

  // Every non-vector argument occupies 8 bytes and is passed by preference
  // in either GPRs or FPRs.  Vector arguments occupy 8 or 16 bytes and are
  // always passed on the stack.
  const SystemZTargetCodeGenInfo &SZCGI =
      static_cast<const SystemZTargetCodeGenInfo &>(
          CGT.getCGM().getTargetCodeGenInfo());
  Ty = getContext().getCanonicalType(Ty);
  auto TyInfo = getContext().getTypeInfoInChars(Ty);
  llvm::Type *ArgTy = CGF.ConvertTypeForMem(Ty);
  llvm::Type *DirectTy = ArgTy;
  ABIArgInfo AI = classifyArgumentType(Ty);
  bool IsIndirect = AI.isIndirect();
  bool InFPRs = false;
  bool IsVector = false;
  CharUnits UnpaddedSize;
  CharUnits DirectAlign;
  SZCGI.handleExternallyVisibleObjABI(Ty.getTypePtr(), CGT.getCGM(),
                                      /*IsParam*/true);
  if (IsIndirect) {
    DirectTy = llvm::PointerType::getUnqual(DirectTy);
    UnpaddedSize = DirectAlign = CharUnits::fromQuantity(8);
  } else {
    if (AI.getCoerceToType())
      ArgTy = AI.getCoerceToType();
    InFPRs = (!IsSoftFloatABI && (ArgTy->isFloatTy() || ArgTy->isDoubleTy()));
    IsVector = ArgTy->isVectorTy();
    UnpaddedSize = TyInfo.Width;
    DirectAlign = TyInfo.Align;
  }
  CharUnits PaddedSize = CharUnits::fromQuantity(8);
  if (IsVector && UnpaddedSize > PaddedSize)
    PaddedSize = CharUnits::fromQuantity(16);
  assert((UnpaddedSize <= PaddedSize) && "Invalid argument size.");

  CharUnits Padding = (PaddedSize - UnpaddedSize);

  llvm::Type *IndexTy = CGF.Int64Ty;
  llvm::Value *PaddedSizeV =
    llvm::ConstantInt::get(IndexTy, PaddedSize.getQuantity());

  if (IsVector) {
    // Work out the address of a vector argument on the stack.
    // Vector arguments are always passed in the high bits of a
    // single (8 byte) or double (16 byte) stack slot.
    Address OverflowArgAreaPtr =
        CGF.Builder.CreateStructGEP(VAListAddr, 2, "overflow_arg_area_ptr");
    Address OverflowArgArea =
        Address(CGF.Builder.CreateLoad(OverflowArgAreaPtr, "overflow_arg_area"),
                CGF.Int8Ty, TyInfo.Align);
    Address MemAddr = OverflowArgArea.withElementType(DirectTy);

    // Update overflow_arg_area_ptr pointer
    llvm::Value *NewOverflowArgArea = CGF.Builder.CreateGEP(
        OverflowArgArea.getElementType(), OverflowArgArea.emitRawPointer(CGF),
        PaddedSizeV, "overflow_arg_area");
    CGF.Builder.CreateStore(NewOverflowArgArea, OverflowArgAreaPtr);

    return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(MemAddr, Ty), Slot);
  }

  assert(PaddedSize.getQuantity() == 8);

  unsigned MaxRegs, RegCountField, RegSaveIndex;
  CharUnits RegPadding;
  if (InFPRs) {
    MaxRegs = 4; // Maximum of 4 FPR arguments
    RegCountField = 1; // __fpr
    RegSaveIndex = 16; // save offset for f0
    RegPadding = CharUnits(); // floats are passed in the high bits of an FPR
  } else {
    MaxRegs = 5; // Maximum of 5 GPR arguments
    RegCountField = 0; // __gpr
    RegSaveIndex = 2; // save offset for r2
    RegPadding = Padding; // values are passed in the low bits of a GPR
  }

  Address RegCountPtr =
      CGF.Builder.CreateStructGEP(VAListAddr, RegCountField, "reg_count_ptr");
  llvm::Value *RegCount = CGF.Builder.CreateLoad(RegCountPtr, "reg_count");
  llvm::Value *MaxRegsV = llvm::ConstantInt::get(IndexTy, MaxRegs);
  llvm::Value *InRegs = CGF.Builder.CreateICmpULT(RegCount, MaxRegsV,
                                                 "fits_in_regs");

  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *InMemBlock = CGF.createBasicBlock("vaarg.in_mem");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");
  CGF.Builder.CreateCondBr(InRegs, InRegBlock, InMemBlock);

  // Emit code to load the value if it was passed in registers.
  CGF.EmitBlock(InRegBlock);

  // Work out the address of an argument register.
  llvm::Value *ScaledRegCount =
    CGF.Builder.CreateMul(RegCount, PaddedSizeV, "scaled_reg_count");
  llvm::Value *RegBase =
    llvm::ConstantInt::get(IndexTy, RegSaveIndex * PaddedSize.getQuantity()
                                      + RegPadding.getQuantity());
  llvm::Value *RegOffset =
    CGF.Builder.CreateAdd(ScaledRegCount, RegBase, "reg_offset");
  Address RegSaveAreaPtr =
      CGF.Builder.CreateStructGEP(VAListAddr, 3, "reg_save_area_ptr");
  llvm::Value *RegSaveArea =
      CGF.Builder.CreateLoad(RegSaveAreaPtr, "reg_save_area");
  Address RawRegAddr(
      CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea, RegOffset, "raw_reg_addr"),
      CGF.Int8Ty, PaddedSize);
  Address RegAddr = RawRegAddr.withElementType(DirectTy);

  // Update the register count
  llvm::Value *One = llvm::ConstantInt::get(IndexTy, 1);
  llvm::Value *NewRegCount =
    CGF.Builder.CreateAdd(RegCount, One, "reg_count");
  CGF.Builder.CreateStore(NewRegCount, RegCountPtr);
  CGF.EmitBranch(ContBlock);

  // Emit code to load the value if it was passed in memory.
  CGF.EmitBlock(InMemBlock);

  // Work out the address of a stack argument.
  Address OverflowArgAreaPtr =
      CGF.Builder.CreateStructGEP(VAListAddr, 2, "overflow_arg_area_ptr");
  Address OverflowArgArea =
      Address(CGF.Builder.CreateLoad(OverflowArgAreaPtr, "overflow_arg_area"),
              CGF.Int8Ty, PaddedSize);
  Address RawMemAddr =
      CGF.Builder.CreateConstByteGEP(OverflowArgArea, Padding, "raw_mem_addr");
  Address MemAddr = RawMemAddr.withElementType(DirectTy);

  // Update overflow_arg_area_ptr pointer
  llvm::Value *NewOverflowArgArea = CGF.Builder.CreateGEP(
      OverflowArgArea.getElementType(), OverflowArgArea.emitRawPointer(CGF),
      PaddedSizeV, "overflow_arg_area");
  CGF.Builder.CreateStore(NewOverflowArgArea, OverflowArgAreaPtr);
  CGF.EmitBranch(ContBlock);

  // Return the appropriate result.
  CGF.EmitBlock(ContBlock);
  Address ResAddr = emitMergePHI(CGF, RegAddr, InRegBlock, MemAddr, InMemBlock,
                                 "va_arg.addr");

  if (IsIndirect)
    ResAddr = Address(CGF.Builder.CreateLoad(ResAddr, "indirect_arg"), ArgTy,
                      TyInfo.Align);

  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(ResAddr, Ty), Slot);
}

ABIArgInfo SystemZABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();
  if (isVectorArgumentType(RetTy))
    return ABIArgInfo::getDirect();
  if (isCompoundType(RetTy) || getContext().getTypeSize(RetTy) > 64)
    return getNaturalAlignIndirect(RetTy);
  return (isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                               : ABIArgInfo::getDirect());
}

ABIArgInfo SystemZABIInfo::classifyArgumentType(QualType Ty) const {
  // Handle transparent union types.
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Handle the generic C++ ABI.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Integers and enums are extended to full register width.
  if (isPromotableIntegerTypeForABI(Ty))
    return ABIArgInfo::getExtend(Ty, CGT.ConvertType(Ty));

  // Handle vector types and vector-like structure types.  Note that
  // as opposed to float-like structure types, we do not allow any
  // padding for vector-like structures, so verify the sizes match.
  uint64_t Size = getContext().getTypeSize(Ty);
  QualType SingleElementTy = GetSingleElementType(Ty);
  if (isVectorArgumentType(SingleElementTy) &&
      getContext().getTypeSize(SingleElementTy) == Size)
    return ABIArgInfo::getDirect(CGT.ConvertType(SingleElementTy));

  // Values that are not 1, 2, 4 or 8 bytes in size are passed indirectly.
  if (Size != 8 && Size != 16 && Size != 32 && Size != 64)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  // Handle small structures.
  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    // Structures with flexible arrays have variable length, so really
    // fail the size test above.
    const RecordDecl *RD = RT->getDecl();
    if (RD->hasFlexibleArrayMember())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // The structure is passed as an unextended integer, a float, or a double.
    llvm::Type *PassTy;
    if (isFPArgumentType(SingleElementTy)) {
      assert(Size == 32 || Size == 64);
      if (Size == 32)
        PassTy = llvm::Type::getFloatTy(getVMContext());
      else
        PassTy = llvm::Type::getDoubleTy(getVMContext());
    } else
      PassTy = llvm::IntegerType::get(getVMContext(), Size);
    return ABIArgInfo::getDirect(PassTy);
  }

  // Non-structure compounds are passed indirectly.
  if (isCompoundType(Ty))
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  return ABIArgInfo::getDirect(nullptr);
}

void SystemZABIInfo::computeInfo(CGFunctionInfo &FI) const {
  const SystemZTargetCodeGenInfo &SZCGI =
      static_cast<const SystemZTargetCodeGenInfo &>(
          CGT.getCGM().getTargetCodeGenInfo());
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  unsigned Idx = 0;
  for (auto &I : FI.arguments()) {
    I.info = classifyArgumentType(I.type);
    if (FI.isVariadic() && Idx++ >= FI.getNumRequiredArgs())
      // Check if a vararg vector argument is passed, in which case the
      // vector ABI becomes visible as the va_list could be passed on to
      // other functions.
      SZCGI.handleExternallyVisibleObjABI(I.type.getTypePtr(), CGT.getCGM(),
                                          /*IsParam*/true);
  }
}

bool SystemZTargetCodeGenInfo::isVectorTypeBased(const Type *Ty,
                                                 bool IsParam) const {
  if (!SeenTypes.insert(Ty).second)
    return false;

  if (IsParam) {
    // A narrow (<16 bytes) vector will as a parameter also expose the ABI as
    // it will be passed in a vector register. A wide (>16 bytes) vector will
    // be passed via "hidden" pointer where any extra alignment is not
    // required (per GCC).
    const Type *SingleEltTy = getABIInfo<SystemZABIInfo>()
                                  .GetSingleElementType(QualType(Ty, 0))
                                  .getTypePtr();
    bool SingleVecEltStruct = SingleEltTy != Ty && SingleEltTy->isVectorType() &&
      Ctx.getTypeSize(SingleEltTy) == Ctx.getTypeSize(Ty);
    if (Ty->isVectorType() || SingleVecEltStruct)
      return Ctx.getTypeSize(Ty) / 8 <= 16;
  }

  // Assume pointers are dereferenced.
  while (Ty->isPointerType() || Ty->isArrayType())
    Ty = Ty->getPointeeOrArrayElementType();

  // Vectors >= 16 bytes expose the ABI through alignment requirements.
  if (Ty->isVectorType() && Ctx.getTypeSize(Ty) / 8 >= 16)
      return true;

  if (const auto *RecordTy = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RecordTy->getDecl();
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      if (CXXRD->hasDefinition())
        for (const auto &I : CXXRD->bases())
          if (isVectorTypeBased(I.getType().getTypePtr(), /*IsParam*/false))
            return true;
    for (const auto *FD : RD->fields())
      if (isVectorTypeBased(FD->getType().getTypePtr(), /*IsParam*/false))
        return true;
  }

  if (const auto *FT = Ty->getAs<FunctionType>())
    if (isVectorTypeBased(FT->getReturnType().getTypePtr(), /*IsParam*/true))
      return true;
  if (const FunctionProtoType *Proto = Ty->getAs<FunctionProtoType>())
    for (const auto &ParamType : Proto->getParamTypes())
      if (isVectorTypeBased(ParamType.getTypePtr(), /*IsParam*/true))
        return true;

  return false;
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createSystemZTargetCodeGenInfo(CodeGenModule &CGM, bool HasVector,
                                        bool SoftFloatABI) {
  return std::make_unique<SystemZTargetCodeGenInfo>(CGM.getTypes(), HasVector,
                                                    SoftFloatABI);
}
