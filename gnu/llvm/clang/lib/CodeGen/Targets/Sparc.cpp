//===- Sparc.cpp ----------------------------------------------------------===//
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
// SPARC v8 ABI Implementation.
// Based on the SPARC Compliance Definition version 2.4.1.
//
// Ensures that complex values are passed in registers.
//
namespace {
class SparcV8ABIInfo : public DefaultABIInfo {
public:
  SparcV8ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  void computeInfo(CGFunctionInfo &FI) const override;
};
} // end anonymous namespace


ABIArgInfo
SparcV8ABIInfo::classifyReturnType(QualType Ty) const {
  if (Ty->isAnyComplexType()) {
    return ABIArgInfo::getDirect();
  }
  else {
    return DefaultABIInfo::classifyReturnType(Ty);
  }
}

void SparcV8ABIInfo::computeInfo(CGFunctionInfo &FI) const {

  FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

namespace {
class SparcV8TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SparcV8TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<SparcV8ABIInfo>(CGT)) {}

  llvm::Value *decodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *Address) const override {
    int Offset;
    if (isAggregateTypeForABI(CGF.CurFnInfo->getReturnType()))
      Offset = 12;
    else
      Offset = 8;
    return CGF.Builder.CreateGEP(CGF.Int8Ty, Address,
                                 llvm::ConstantInt::get(CGF.Int32Ty, Offset));
  }

  llvm::Value *encodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *Address) const override {
    int Offset;
    if (isAggregateTypeForABI(CGF.CurFnInfo->getReturnType()))
      Offset = -12;
    else
      Offset = -8;
    return CGF.Builder.CreateGEP(CGF.Int8Ty, Address,
                                 llvm::ConstantInt::get(CGF.Int32Ty, Offset));
  }
};
} // end anonymous namespace

//===----------------------------------------------------------------------===//
// SPARC v9 ABI Implementation.
// Based on the SPARC Compliance Definition version 2.4.1.
//
// Function arguments a mapped to a nominal "parameter array" and promoted to
// registers depending on their type. Each argument occupies 8 or 16 bytes in
// the array, structs larger than 16 bytes are passed indirectly.
//
// One case requires special care:
//
//   struct mixed {
//     int i;
//     float f;
//   };
//
// When a struct mixed is passed by value, it only occupies 8 bytes in the
// parameter array, but the int is passed in an integer register, and the float
// is passed in a floating point register. This is represented as two arguments
// with the LLVM IR inreg attribute:
//
//   declare void f(i32 inreg %i, float inreg %f)
//
// The code generator will only allocate 4 bytes from the parameter array for
// the inreg arguments. All other arguments are allocated a multiple of 8
// bytes.
//
namespace {
class SparcV9ABIInfo : public ABIInfo {
public:
  SparcV9ABIInfo(CodeGenTypes &CGT) : ABIInfo(CGT) {}

private:
  ABIArgInfo classifyType(QualType RetTy, unsigned SizeLimit) const;
  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  // Coercion type builder for structs passed in registers. The coercion type
  // serves two purposes:
  //
  // 1. Pad structs to a multiple of 64 bits, so they are passed 'left-aligned'
  //    in registers.
  // 2. Expose aligned floating point elements as first-level elements, so the
  //    code generator knows to pass them in floating point registers.
  //
  // We also compute the InReg flag which indicates that the struct contains
  // aligned 32-bit floats.
  //
  struct CoerceBuilder {
    llvm::LLVMContext &Context;
    const llvm::DataLayout &DL;
    SmallVector<llvm::Type*, 8> Elems;
    uint64_t Size;
    bool InReg;

    CoerceBuilder(llvm::LLVMContext &c, const llvm::DataLayout &dl)
      : Context(c), DL(dl), Size(0), InReg(false) {}

    // Pad Elems with integers until Size is ToSize.
    void pad(uint64_t ToSize) {
      assert(ToSize >= Size && "Cannot remove elements");
      if (ToSize == Size)
        return;

      // Finish the current 64-bit word.
      uint64_t Aligned = llvm::alignTo(Size, 64);
      if (Aligned > Size && Aligned <= ToSize) {
        Elems.push_back(llvm::IntegerType::get(Context, Aligned - Size));
        Size = Aligned;
      }

      // Add whole 64-bit words.
      while (Size + 64 <= ToSize) {
        Elems.push_back(llvm::Type::getInt64Ty(Context));
        Size += 64;
      }

      // Final in-word padding.
      if (Size < ToSize) {
        Elems.push_back(llvm::IntegerType::get(Context, ToSize - Size));
        Size = ToSize;
      }
    }

    // Add a floating point element at Offset.
    void addFloat(uint64_t Offset, llvm::Type *Ty, unsigned Bits) {
      // Unaligned floats are treated as integers.
      if (Offset % Bits)
        return;
      // The InReg flag is only required if there are any floats < 64 bits.
      if (Bits < 64)
        InReg = true;
      pad(Offset);
      Elems.push_back(Ty);
      Size = Offset + Bits;
    }

    // Add a struct type to the coercion type, starting at Offset (in bits).
    void addStruct(uint64_t Offset, llvm::StructType *StrTy) {
      const llvm::StructLayout *Layout = DL.getStructLayout(StrTy);
      for (unsigned i = 0, e = StrTy->getNumElements(); i != e; ++i) {
        llvm::Type *ElemTy = StrTy->getElementType(i);
        uint64_t ElemOffset = Offset + Layout->getElementOffsetInBits(i);
        switch (ElemTy->getTypeID()) {
        case llvm::Type::StructTyID:
          addStruct(ElemOffset, cast<llvm::StructType>(ElemTy));
          break;
        case llvm::Type::FloatTyID:
          addFloat(ElemOffset, ElemTy, 32);
          break;
        case llvm::Type::DoubleTyID:
          addFloat(ElemOffset, ElemTy, 64);
          break;
        case llvm::Type::FP128TyID:
          addFloat(ElemOffset, ElemTy, 128);
          break;
        case llvm::Type::PointerTyID:
          if (ElemOffset % 64 == 0) {
            pad(ElemOffset);
            Elems.push_back(ElemTy);
            Size += 64;
          }
          break;
        default:
          break;
        }
      }
    }

    // Check if Ty is a usable substitute for the coercion type.
    bool isUsableType(llvm::StructType *Ty) const {
      return llvm::ArrayRef(Elems) == Ty->elements();
    }

    // Get the coercion type as a literal struct type.
    llvm::Type *getType() const {
      if (Elems.size() == 1)
        return Elems.front();
      else
        return llvm::StructType::get(Context, Elems);
    }
  };
};
} // end anonymous namespace

ABIArgInfo
SparcV9ABIInfo::classifyType(QualType Ty, unsigned SizeLimit) const {
  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();

  uint64_t Size = getContext().getTypeSize(Ty);

  // Anything too big to fit in registers is passed with an explicit indirect
  // pointer / sret pointer.
  if (Size > SizeLimit)
    return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Integer types smaller than a register are extended.
  if (Size < 64 && Ty->isIntegerType())
    return ABIArgInfo::getExtend(Ty);

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() < 64)
      return ABIArgInfo::getExtend(Ty);

  // Other non-aggregates go in registers.
  if (!isAggregateTypeForABI(Ty))
    return ABIArgInfo::getDirect();

  // If a C++ object has either a non-trivial copy constructor or a non-trivial
  // destructor, it is passed with an explicit indirect pointer / sret pointer.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // This is a small aggregate type that should be passed in registers.
  // Build a coercion type from the LLVM struct type.
  llvm::StructType *StrTy = dyn_cast<llvm::StructType>(CGT.ConvertType(Ty));
  if (!StrTy)
    return ABIArgInfo::getDirect();

  CoerceBuilder CB(getVMContext(), getDataLayout());
  CB.addStruct(0, StrTy);
  // All structs, even empty ones, should take up a register argument slot,
  // so pin the minimum struct size to one bit.
  CB.pad(llvm::alignTo(
      std::max(CB.DL.getTypeSizeInBits(StrTy).getKnownMinValue(), uint64_t(1)),
      64));

  // Try to use the original type for coercion.
  llvm::Type *CoerceTy = CB.isUsableType(StrTy) ? StrTy : CB.getType();

  if (CB.InReg)
    return ABIArgInfo::getDirectInReg(CoerceTy);
  else
    return ABIArgInfo::getDirect(CoerceTy);
}

RValue SparcV9ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                 QualType Ty, AggValueSlot Slot) const {
  ABIArgInfo AI = classifyType(Ty, 16 * 8);
  llvm::Type *ArgTy = CGT.ConvertType(Ty);
  if (AI.canHaveCoerceToType() && !AI.getCoerceToType())
    AI.setCoerceToType(ArgTy);

  CharUnits SlotSize = CharUnits::fromQuantity(8);

  CGBuilderTy &Builder = CGF.Builder;
  Address Addr = Address(Builder.CreateLoad(VAListAddr, "ap.cur"),
                         getVAListElementType(CGF), SlotSize);
  llvm::Type *ArgPtrTy = CGF.UnqualPtrTy;

  auto TypeInfo = getContext().getTypeInfoInChars(Ty);

  Address ArgAddr = Address::invalid();
  CharUnits Stride;
  switch (AI.getKind()) {
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
  case ABIArgInfo::InAlloca:
    llvm_unreachable("Unsupported ABI kind for va_arg");

  case ABIArgInfo::Extend: {
    Stride = SlotSize;
    CharUnits Offset = SlotSize - TypeInfo.Width;
    ArgAddr = Builder.CreateConstInBoundsByteGEP(Addr, Offset, "extend");
    break;
  }

  case ABIArgInfo::Direct: {
    auto AllocSize = getDataLayout().getTypeAllocSize(AI.getCoerceToType());
    Stride = CharUnits::fromQuantity(AllocSize).alignTo(SlotSize);
    ArgAddr = Addr;
    break;
  }

  case ABIArgInfo::Indirect:
  case ABIArgInfo::IndirectAliased:
    Stride = SlotSize;
    ArgAddr = Addr.withElementType(ArgPtrTy);
    ArgAddr = Address(Builder.CreateLoad(ArgAddr, "indirect.arg"), ArgTy,
                      TypeInfo.Align);
    break;

  case ABIArgInfo::Ignore:
    return Slot.asRValue();
  }

  // Update VAList.
  Address NextPtr = Builder.CreateConstInBoundsByteGEP(Addr, Stride, "ap.next");
  Builder.CreateStore(NextPtr.emitRawPointer(CGF), VAListAddr);

  return CGF.EmitLoadOfAnyValue(
      CGF.MakeAddrLValue(ArgAddr.withElementType(ArgTy), Ty), Slot);
}

void SparcV9ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  FI.getReturnInfo() = classifyType(FI.getReturnType(), 32 * 8);
  for (auto &I : FI.arguments())
    I.info = classifyType(I.type, 16 * 8);
}

namespace {
class SparcV9TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  SparcV9TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<SparcV9ABIInfo>(CGT)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 14;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;

  llvm::Value *decodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *Address) const override {
    return CGF.Builder.CreateGEP(CGF.Int8Ty, Address,
                                 llvm::ConstantInt::get(CGF.Int32Ty, 8));
  }

  llvm::Value *encodeReturnAddress(CodeGen::CodeGenFunction &CGF,
                                   llvm::Value *Address) const override {
    return CGF.Builder.CreateGEP(CGF.Int8Ty, Address,
                                 llvm::ConstantInt::get(CGF.Int32Ty, -8));
  }
};
} // end anonymous namespace

bool
SparcV9TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {
  // This is calculated from the LLVM and GCC tables and verified
  // against gcc output.  AFAIK all ABIs use the same encoding.

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::IntegerType *i8 = CGF.Int8Ty;
  llvm::Value *Four8 = llvm::ConstantInt::get(i8, 4);
  llvm::Value *Eight8 = llvm::ConstantInt::get(i8, 8);

  // 0-31: the 8-byte general-purpose registers
  AssignToArrayRange(Builder, Address, Eight8, 0, 31);

  // 32-63: f0-31, the 4-byte floating-point registers
  AssignToArrayRange(Builder, Address, Four8, 32, 63);

  //   Y   = 64
  //   PSR = 65
  //   WIM = 66
  //   TBR = 67
  //   PC  = 68
  //   NPC = 69
  //   FSR = 70
  //   CSR = 71
  AssignToArrayRange(Builder, Address, Eight8, 64, 71);

  // 72-87: d0-15, the 8-byte floating-point registers
  AssignToArrayRange(Builder, Address, Eight8, 72, 87);

  return false;
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createSparcV8TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<SparcV8TargetCodeGenInfo>(CGM.getTypes());
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createSparcV9TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<SparcV9TargetCodeGenInfo>(CGM.getTypes());
}
