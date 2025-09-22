//===- Hexagon.cpp --------------------------------------------------------===//
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
// Hexagon ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class HexagonABIInfo : public DefaultABIInfo {
public:
  HexagonABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, unsigned *RegsLeft) const;

  void computeInfo(CGFunctionInfo &FI) const override;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
  Address EmitVAArgFromMemory(CodeGenFunction &CFG, Address VAListAddr,
                              QualType Ty) const;
  Address EmitVAArgForHexagon(CodeGenFunction &CFG, Address VAListAddr,
                              QualType Ty) const;
  Address EmitVAArgForHexagonLinux(CodeGenFunction &CFG, Address VAListAddr,
                                   QualType Ty) const;
};

class HexagonTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  HexagonTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<HexagonABIInfo>(CGT)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 29;
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &GCM) const override {
    if (GV->isDeclaration())
      return;
    const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD)
      return;
  }
};

} // namespace

void HexagonABIInfo::computeInfo(CGFunctionInfo &FI) const {
  unsigned RegsLeft = 6;
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type, &RegsLeft);
}

static bool HexagonAdjustRegsLeft(uint64_t Size, unsigned *RegsLeft) {
  assert(Size <= 64 && "Not expecting to pass arguments larger than 64 bits"
                       " through registers");

  if (*RegsLeft == 0)
    return false;

  if (Size <= 32) {
    (*RegsLeft)--;
    return true;
  }

  if (2 <= (*RegsLeft & (~1U))) {
    *RegsLeft = (*RegsLeft & (~1U)) - 2;
    return true;
  }

  // Next available register was r5 but candidate was greater than 32-bits so it
  // has to go on the stack. However we still consume r5
  if (*RegsLeft == 1)
    *RegsLeft = 0;

  return false;
}

ABIArgInfo HexagonABIInfo::classifyArgumentType(QualType Ty,
                                                unsigned *RegsLeft) const {
  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size <= 64)
      HexagonAdjustRegsLeft(Size, RegsLeft);

    if (Size > 64 && Ty->isBitIntType())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

    return isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                             : ABIArgInfo::getDirect();
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Ignore empty records.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(Ty);
  unsigned Align = getContext().getTypeAlign(Ty);

  if (Size > 64)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

  if (HexagonAdjustRegsLeft(Size, RegsLeft))
    Align = Size <= 32 ? 32 : 64;
  if (Size <= Align) {
    // Pass in the smallest viable integer type.
    Size = llvm::bit_ceil(Size);
    return ABIArgInfo::getDirect(llvm::Type::getIntNTy(getVMContext(), Size));
  }
  return DefaultABIInfo::classifyArgumentType(Ty);
}

ABIArgInfo HexagonABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  const TargetInfo &T = CGT.getTarget();
  uint64_t Size = getContext().getTypeSize(RetTy);

  if (RetTy->getAs<VectorType>()) {
    // HVX vectors are returned in vector registers or register pairs.
    if (T.hasFeature("hvx")) {
      assert(T.hasFeature("hvx-length64b") || T.hasFeature("hvx-length128b"));
      uint64_t VecSize = T.hasFeature("hvx-length64b") ? 64*8 : 128*8;
      if (Size == VecSize || Size == 2*VecSize)
        return ABIArgInfo::getDirectInReg();
    }
    // Large vector types should be returned via memory.
    if (Size > 64)
      return getNaturalAlignIndirect(RetTy);
  }

  if (!isAggregateTypeForABI(RetTy)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    if (Size > 64 && RetTy->isBitIntType())
      return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);

    return isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                                : ABIArgInfo::getDirect();
  }

  if (isEmptyRecord(getContext(), RetTy, true))
    return ABIArgInfo::getIgnore();

  // Aggregates <= 8 bytes are returned in registers, other aggregates
  // are returned indirectly.
  if (Size <= 64) {
    // Return in the smallest viable integer type.
    Size = llvm::bit_ceil(Size);
    return ABIArgInfo::getDirect(llvm::Type::getIntNTy(getVMContext(), Size));
  }
  return getNaturalAlignIndirect(RetTy, /*ByVal=*/true);
}

Address HexagonABIInfo::EmitVAArgFromMemory(CodeGenFunction &CGF,
                                            Address VAListAddr,
                                            QualType Ty) const {
  // Load the overflow area pointer.
  Address __overflow_area_pointer_p =
      CGF.Builder.CreateStructGEP(VAListAddr, 2, "__overflow_area_pointer_p");
  llvm::Value *__overflow_area_pointer = CGF.Builder.CreateLoad(
      __overflow_area_pointer_p, "__overflow_area_pointer");

  uint64_t Align = CGF.getContext().getTypeAlign(Ty) / 8;
  if (Align > 4) {
    // Alignment should be a power of 2.
    assert((Align & (Align - 1)) == 0 && "Alignment is not power of 2!");

    // overflow_arg_area = (overflow_arg_area + align - 1) & -align;
    llvm::Value *Offset = llvm::ConstantInt::get(CGF.Int64Ty, Align - 1);

    // Add offset to the current pointer to access the argument.
    __overflow_area_pointer =
        CGF.Builder.CreateGEP(CGF.Int8Ty, __overflow_area_pointer, Offset);
    llvm::Value *AsInt =
        CGF.Builder.CreatePtrToInt(__overflow_area_pointer, CGF.Int32Ty);

    // Create a mask which should be "AND"ed
    // with (overflow_arg_area + align - 1)
    llvm::Value *Mask = llvm::ConstantInt::get(CGF.Int32Ty, -(int)Align);
    __overflow_area_pointer = CGF.Builder.CreateIntToPtr(
        CGF.Builder.CreateAnd(AsInt, Mask), __overflow_area_pointer->getType(),
        "__overflow_area_pointer.align");
  }

  // Get the type of the argument from memory and bitcast
  // overflow area pointer to the argument type.
  llvm::Type *PTy = CGF.ConvertTypeForMem(Ty);
  Address AddrTyped =
      Address(__overflow_area_pointer, PTy, CharUnits::fromQuantity(Align));

  // Round up to the minimum stack alignment for varargs which is 4 bytes.
  uint64_t Offset = llvm::alignTo(CGF.getContext().getTypeSize(Ty) / 8, 4);

  __overflow_area_pointer = CGF.Builder.CreateGEP(
      CGF.Int8Ty, __overflow_area_pointer,
      llvm::ConstantInt::get(CGF.Int32Ty, Offset),
      "__overflow_area_pointer.next");
  CGF.Builder.CreateStore(__overflow_area_pointer, __overflow_area_pointer_p);

  return AddrTyped;
}

Address HexagonABIInfo::EmitVAArgForHexagon(CodeGenFunction &CGF,
                                            Address VAListAddr,
                                            QualType Ty) const {
  // FIXME: Need to handle alignment
  llvm::Type *BP = CGF.Int8PtrTy;
  CGBuilderTy &Builder = CGF.Builder;
  Address VAListAddrAsBPP = VAListAddr.withElementType(BP);
  llvm::Value *Addr = Builder.CreateLoad(VAListAddrAsBPP, "ap.cur");
  // Handle address alignment for type alignment > 32 bits
  uint64_t TyAlign = CGF.getContext().getTypeAlign(Ty) / 8;
  if (TyAlign > 4) {
    assert((TyAlign & (TyAlign - 1)) == 0 && "Alignment is not power of 2!");
    llvm::Value *AddrAsInt = Builder.CreatePtrToInt(Addr, CGF.Int32Ty);
    AddrAsInt = Builder.CreateAdd(AddrAsInt, Builder.getInt32(TyAlign - 1));
    AddrAsInt = Builder.CreateAnd(AddrAsInt, Builder.getInt32(~(TyAlign - 1)));
    Addr = Builder.CreateIntToPtr(AddrAsInt, BP);
  }
  Address AddrTyped =
      Address(Addr, CGF.ConvertType(Ty), CharUnits::fromQuantity(TyAlign));

  uint64_t Offset = llvm::alignTo(CGF.getContext().getTypeSize(Ty) / 8, 4);
  llvm::Value *NextAddr = Builder.CreateGEP(
      CGF.Int8Ty, Addr, llvm::ConstantInt::get(CGF.Int32Ty, Offset), "ap.next");
  Builder.CreateStore(NextAddr, VAListAddrAsBPP);

  return AddrTyped;
}

Address HexagonABIInfo::EmitVAArgForHexagonLinux(CodeGenFunction &CGF,
                                                 Address VAListAddr,
                                                 QualType Ty) const {
  int ArgSize = CGF.getContext().getTypeSize(Ty) / 8;

  if (ArgSize > 8)
    return EmitVAArgFromMemory(CGF, VAListAddr, Ty);

  // Here we have check if the argument is in register area or
  // in overflow area.
  // If the saved register area pointer + argsize rounded up to alignment >
  // saved register area end pointer, argument is in overflow area.
  unsigned RegsLeft = 6;
  Ty = CGF.getContext().getCanonicalType(Ty);
  (void)classifyArgumentType(Ty, &RegsLeft);

  llvm::BasicBlock *MaybeRegBlock = CGF.createBasicBlock("vaarg.maybe_reg");
  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *OnStackBlock = CGF.createBasicBlock("vaarg.on_stack");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");

  // Get rounded size of the argument.GCC does not allow vararg of
  // size < 4 bytes. We follow the same logic here.
  ArgSize = (CGF.getContext().getTypeSize(Ty) <= 32) ? 4 : 8;
  int ArgAlign = (CGF.getContext().getTypeSize(Ty) <= 32) ? 4 : 8;

  // Argument may be in saved register area
  CGF.EmitBlock(MaybeRegBlock);

  // Load the current saved register area pointer.
  Address __current_saved_reg_area_pointer_p = CGF.Builder.CreateStructGEP(
      VAListAddr, 0, "__current_saved_reg_area_pointer_p");
  llvm::Value *__current_saved_reg_area_pointer = CGF.Builder.CreateLoad(
      __current_saved_reg_area_pointer_p, "__current_saved_reg_area_pointer");

  // Load the saved register area end pointer.
  Address __saved_reg_area_end_pointer_p = CGF.Builder.CreateStructGEP(
      VAListAddr, 1, "__saved_reg_area_end_pointer_p");
  llvm::Value *__saved_reg_area_end_pointer = CGF.Builder.CreateLoad(
      __saved_reg_area_end_pointer_p, "__saved_reg_area_end_pointer");

  // If the size of argument is > 4 bytes, check if the stack
  // location is aligned to 8 bytes
  if (ArgAlign > 4) {

    llvm::Value *__current_saved_reg_area_pointer_int =
        CGF.Builder.CreatePtrToInt(__current_saved_reg_area_pointer,
                                   CGF.Int32Ty);

    __current_saved_reg_area_pointer_int = CGF.Builder.CreateAdd(
        __current_saved_reg_area_pointer_int,
        llvm::ConstantInt::get(CGF.Int32Ty, (ArgAlign - 1)),
        "align_current_saved_reg_area_pointer");

    __current_saved_reg_area_pointer_int =
        CGF.Builder.CreateAnd(__current_saved_reg_area_pointer_int,
                              llvm::ConstantInt::get(CGF.Int32Ty, -ArgAlign),
                              "align_current_saved_reg_area_pointer");

    __current_saved_reg_area_pointer =
        CGF.Builder.CreateIntToPtr(__current_saved_reg_area_pointer_int,
                                   __current_saved_reg_area_pointer->getType(),
                                   "align_current_saved_reg_area_pointer");
  }

  llvm::Value *__new_saved_reg_area_pointer =
      CGF.Builder.CreateGEP(CGF.Int8Ty, __current_saved_reg_area_pointer,
                            llvm::ConstantInt::get(CGF.Int32Ty, ArgSize),
                            "__new_saved_reg_area_pointer");

  llvm::Value *UsingStack = nullptr;
  UsingStack = CGF.Builder.CreateICmpSGT(__new_saved_reg_area_pointer,
                                         __saved_reg_area_end_pointer);

  CGF.Builder.CreateCondBr(UsingStack, OnStackBlock, InRegBlock);

  // Argument in saved register area
  // Implement the block where argument is in register saved area
  CGF.EmitBlock(InRegBlock);

  llvm::Type *PTy = CGF.ConvertType(Ty);
  llvm::Value *__saved_reg_area_p = CGF.Builder.CreateBitCast(
      __current_saved_reg_area_pointer, llvm::PointerType::getUnqual(PTy));

  CGF.Builder.CreateStore(__new_saved_reg_area_pointer,
                          __current_saved_reg_area_pointer_p);

  CGF.EmitBranch(ContBlock);

  // Argument in overflow area
  // Implement the block where the argument is in overflow area.
  CGF.EmitBlock(OnStackBlock);

  // Load the overflow area pointer
  Address __overflow_area_pointer_p =
      CGF.Builder.CreateStructGEP(VAListAddr, 2, "__overflow_area_pointer_p");
  llvm::Value *__overflow_area_pointer = CGF.Builder.CreateLoad(
      __overflow_area_pointer_p, "__overflow_area_pointer");

  // Align the overflow area pointer according to the alignment of the argument
  if (ArgAlign > 4) {
    llvm::Value *__overflow_area_pointer_int =
        CGF.Builder.CreatePtrToInt(__overflow_area_pointer, CGF.Int32Ty);

    __overflow_area_pointer_int =
        CGF.Builder.CreateAdd(__overflow_area_pointer_int,
                              llvm::ConstantInt::get(CGF.Int32Ty, ArgAlign - 1),
                              "align_overflow_area_pointer");

    __overflow_area_pointer_int =
        CGF.Builder.CreateAnd(__overflow_area_pointer_int,
                              llvm::ConstantInt::get(CGF.Int32Ty, -ArgAlign),
                              "align_overflow_area_pointer");

    __overflow_area_pointer = CGF.Builder.CreateIntToPtr(
        __overflow_area_pointer_int, __overflow_area_pointer->getType(),
        "align_overflow_area_pointer");
  }

  // Get the pointer for next argument in overflow area and store it
  // to overflow area pointer.
  llvm::Value *__new_overflow_area_pointer = CGF.Builder.CreateGEP(
      CGF.Int8Ty, __overflow_area_pointer,
      llvm::ConstantInt::get(CGF.Int32Ty, ArgSize),
      "__overflow_area_pointer.next");

  CGF.Builder.CreateStore(__new_overflow_area_pointer,
                          __overflow_area_pointer_p);

  CGF.Builder.CreateStore(__new_overflow_area_pointer,
                          __current_saved_reg_area_pointer_p);

  // Bitcast the overflow area pointer to the type of argument.
  llvm::Type *OverflowPTy = CGF.ConvertTypeForMem(Ty);
  llvm::Value *__overflow_area_p = CGF.Builder.CreateBitCast(
      __overflow_area_pointer, llvm::PointerType::getUnqual(OverflowPTy));

  CGF.EmitBranch(ContBlock);

  // Get the correct pointer to load the variable argument
  // Implement the ContBlock
  CGF.EmitBlock(ContBlock);

  llvm::Type *MemTy = CGF.ConvertTypeForMem(Ty);
  llvm::Type *MemPTy = llvm::PointerType::getUnqual(MemTy);
  llvm::PHINode *ArgAddr = CGF.Builder.CreatePHI(MemPTy, 2, "vaarg.addr");
  ArgAddr->addIncoming(__saved_reg_area_p, InRegBlock);
  ArgAddr->addIncoming(__overflow_area_p, OnStackBlock);

  return Address(ArgAddr, MemTy, CharUnits::fromQuantity(ArgAlign));
}

RValue HexagonABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty, AggValueSlot Slot) const {

  if (getTarget().getTriple().isMusl())
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(EmitVAArgForHexagonLinux(CGF, VAListAddr, Ty), Ty),
        Slot);

  return CGF.EmitLoadOfAnyValue(
      CGF.MakeAddrLValue(EmitVAArgForHexagon(CGF, VAListAddr, Ty), Ty), Slot);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createHexagonTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<HexagonTargetCodeGenInfo>(CGM.getTypes());
}
