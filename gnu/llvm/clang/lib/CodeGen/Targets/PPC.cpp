//===- PPC.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/Basic/DiagnosticFrontend.h"

using namespace clang;
using namespace clang::CodeGen;

static RValue complexTempStructure(CodeGenFunction &CGF, Address VAListAddr,
                                   QualType Ty, CharUnits SlotSize,
                                   CharUnits EltSize, const ComplexType *CTy) {
  Address Addr =
      emitVoidPtrDirectVAArg(CGF, VAListAddr, CGF.Int8Ty, SlotSize * 2,
                             SlotSize, SlotSize, /*AllowHigher*/ true);

  Address RealAddr = Addr;
  Address ImagAddr = RealAddr;
  if (CGF.CGM.getDataLayout().isBigEndian()) {
    RealAddr =
        CGF.Builder.CreateConstInBoundsByteGEP(RealAddr, SlotSize - EltSize);
    ImagAddr = CGF.Builder.CreateConstInBoundsByteGEP(ImagAddr,
                                                      2 * SlotSize - EltSize);
  } else {
    ImagAddr = CGF.Builder.CreateConstInBoundsByteGEP(RealAddr, SlotSize);
  }

  llvm::Type *EltTy = CGF.ConvertTypeForMem(CTy->getElementType());
  RealAddr = RealAddr.withElementType(EltTy);
  ImagAddr = ImagAddr.withElementType(EltTy);
  llvm::Value *Real = CGF.Builder.CreateLoad(RealAddr, ".vareal");
  llvm::Value *Imag = CGF.Builder.CreateLoad(ImagAddr, ".vaimag");

  return RValue::getComplex(Real, Imag);
}

static bool PPC_initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                        llvm::Value *Address, bool Is64Bit,
                                        bool IsAIX) {
  // This is calculated from the LLVM and GCC tables and verified
  // against gcc output.  AFAIK all PPC ABIs use the same encoding.

  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::IntegerType *i8 = CGF.Int8Ty;
  llvm::Value *Four8 = llvm::ConstantInt::get(i8, 4);
  llvm::Value *Eight8 = llvm::ConstantInt::get(i8, 8);
  llvm::Value *Sixteen8 = llvm::ConstantInt::get(i8, 16);

  // 0-31: r0-31, the 4-byte or 8-byte general-purpose registers
  AssignToArrayRange(Builder, Address, Is64Bit ? Eight8 : Four8, 0, 31);

  // 32-63: fp0-31, the 8-byte floating-point registers
  AssignToArrayRange(Builder, Address, Eight8, 32, 63);

  // 64-67 are various 4-byte or 8-byte special-purpose registers:
  // 64: mq
  // 65: lr
  // 66: ctr
  // 67: ap
  AssignToArrayRange(Builder, Address, Is64Bit ? Eight8 : Four8, 64, 67);

  // 68-76 are various 4-byte special-purpose registers:
  // 68-75 cr0-7
  // 76: xer
  AssignToArrayRange(Builder, Address, Four8, 68, 76);

  // 77-108: v0-31, the 16-byte vector registers
  AssignToArrayRange(Builder, Address, Sixteen8, 77, 108);

  // 109: vrsave
  // 110: vscr
  AssignToArrayRange(Builder, Address, Is64Bit ? Eight8 : Four8, 109, 110);

  // AIX does not utilize the rest of the registers.
  if (IsAIX)
    return false;

  // 111: spe_acc
  // 112: spefscr
  // 113: sfp
  AssignToArrayRange(Builder, Address, Is64Bit ? Eight8 : Four8, 111, 113);

  if (!Is64Bit)
    return false;

  // TODO: Need to verify if these registers are used on 64 bit AIX with Power8
  // or above CPU.
  // 64-bit only registers:
  // 114: tfhar
  // 115: tfiar
  // 116: texasr
  AssignToArrayRange(Builder, Address, Eight8, 114, 116);

  return false;
}

// AIX
namespace {
/// AIXABIInfo - The AIX XCOFF ABI information.
class AIXABIInfo : public ABIInfo {
  const bool Is64Bit;
  const unsigned PtrByteSize;
  CharUnits getParamTypeAlignment(QualType Ty) const;

public:
  AIXABIInfo(CodeGen::CodeGenTypes &CGT, bool Is64Bit)
      : ABIInfo(CGT), Is64Bit(Is64Bit), PtrByteSize(Is64Bit ? 8 : 4) {}

  bool isPromotableTypeForABI(QualType Ty) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class AIXTargetCodeGenInfo : public TargetCodeGenInfo {
  const bool Is64Bit;

public:
  AIXTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, bool Is64Bit)
      : TargetCodeGenInfo(std::make_unique<AIXABIInfo>(CGT, Is64Bit)),
        Is64Bit(Is64Bit) {}
  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};
} // namespace

// Return true if the ABI requires Ty to be passed sign- or zero-
// extended to 32/64 bits.
bool AIXABIInfo::isPromotableTypeForABI(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Promotable integer types are required to be promoted by the ABI.
  if (getContext().isPromotableIntegerType(Ty))
    return true;

  if (!Is64Bit)
    return false;

  // For 64 bit mode, in addition to the usual promotable integer types, we also
  // need to extend all 32-bit types, since the ABI requires promotion to 64
  // bits.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return true;
    default:
      break;
    }

  return false;
}

ABIArgInfo AIXABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirect();

  if (RetTy->isVectorType())
    return ABIArgInfo::getDirect();

  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  return (isPromotableTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                        : ABIArgInfo::getDirect());
}

ABIArgInfo AIXABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (Ty->isAnyComplexType())
    return ABIArgInfo::getDirect();

  if (Ty->isVectorType())
    return ABIArgInfo::getDirect();

  if (isAggregateTypeForABI(Ty)) {
    // Records with non-trivial destructors/copy-constructors should not be
    // passed by value.
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    CharUnits CCAlign = getParamTypeAlignment(Ty);
    CharUnits TyAlign = getContext().getTypeAlignInChars(Ty);

    return ABIArgInfo::getIndirect(CCAlign, /*ByVal*/ true,
                                   /*Realign*/ TyAlign > CCAlign);
  }

  return (isPromotableTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                     : ABIArgInfo::getDirect());
}

CharUnits AIXABIInfo::getParamTypeAlignment(QualType Ty) const {
  // Complex types are passed just like their elements.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  if (Ty->isVectorType())
    return CharUnits::fromQuantity(16);

  // If the structure contains a vector type, the alignment is 16.
  if (isRecordWithSIMDVectorType(getContext(), Ty))
    return CharUnits::fromQuantity(16);

  return CharUnits::fromQuantity(PtrByteSize);
}

RValue AIXABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                             QualType Ty, AggValueSlot Slot) const {

  auto TypeInfo = getContext().getTypeInfoInChars(Ty);
  TypeInfo.Align = getParamTypeAlignment(Ty);

  CharUnits SlotSize = CharUnits::fromQuantity(PtrByteSize);

  // If we have a complex type and the base type is smaller than the register
  // size, the ABI calls for the real and imaginary parts to be right-adjusted
  // in separate words in 32bit mode or doublewords in 64bit mode. However,
  // Clang expects us to produce a pointer to a structure with the two parts
  // packed tightly. So generate loads of the real and imaginary parts relative
  // to the va_list pointer, and store them to a temporary structure. We do the
  // same as the PPC64ABI here.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>()) {
    CharUnits EltSize = TypeInfo.Width / 2;
    if (EltSize < SlotSize)
      return complexTempStructure(CGF, VAListAddr, Ty, SlotSize, EltSize, CTy);
  }

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect*/ false, TypeInfo,
                          SlotSize, /*AllowHigher*/ true, Slot);
}

bool AIXTargetCodeGenInfo::initDwarfEHRegSizeTable(
    CodeGen::CodeGenFunction &CGF, llvm::Value *Address) const {
  return PPC_initDwarfEHRegSizeTable(CGF, Address, Is64Bit, /*IsAIX*/ true);
}

void AIXTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (!isa<llvm::GlobalVariable>(GV))
    return;

  auto *GVar = cast<llvm::GlobalVariable>(GV);
  auto GVId = GV->getName();

  // Is this a global variable specified by the user as toc-data?
  bool UserSpecifiedTOC =
      llvm::binary_search(M.getCodeGenOpts().TocDataVarsUserSpecified, GVId);
  // Assumes the same variable cannot be in both TocVarsUserSpecified and
  // NoTocVars.
  if (UserSpecifiedTOC ||
      ((M.getCodeGenOpts().AllTocData) &&
       !llvm::binary_search(M.getCodeGenOpts().NoTocDataVars, GVId))) {
    const unsigned long PointerSize =
        GV->getParent()->getDataLayout().getPointerSizeInBits() / 8;
    auto *VarD = dyn_cast<VarDecl>(D);
    assert(VarD && "Invalid declaration of global variable.");

    ASTContext &Context = D->getASTContext();
    unsigned Alignment = Context.toBits(Context.getDeclAlign(D)) / 8;
    const auto *Ty = VarD->getType().getTypePtr();
    const RecordDecl *RDecl =
        Ty->isRecordType() ? Ty->getAs<RecordType>()->getDecl() : nullptr;

    bool EmitDiagnostic = UserSpecifiedTOC && GV->hasExternalLinkage();
    auto reportUnsupportedWarning = [&](bool ShouldEmitWarning, StringRef Msg) {
      if (ShouldEmitWarning)
        M.getDiags().Report(D->getLocation(), diag::warn_toc_unsupported_type)
            << GVId << Msg;
    };
    if (!Ty || Ty->isIncompleteType())
      reportUnsupportedWarning(EmitDiagnostic, "of incomplete type");
    else if (RDecl && RDecl->hasFlexibleArrayMember())
      reportUnsupportedWarning(EmitDiagnostic,
                               "it contains a flexible array member");
    else if (VarD->getTLSKind() != VarDecl::TLS_None)
      reportUnsupportedWarning(EmitDiagnostic, "of thread local storage");
    else if (PointerSize < Context.getTypeInfo(VarD->getType()).Width / 8)
      reportUnsupportedWarning(EmitDiagnostic,
                               "variable is larger than a pointer");
    else if (PointerSize < Alignment)
      reportUnsupportedWarning(EmitDiagnostic,
                               "variable is aligned wider than a pointer");
    else if (D->hasAttr<SectionAttr>())
      reportUnsupportedWarning(EmitDiagnostic,
                               "variable has a section attribute");
    else if (GV->hasExternalLinkage() ||
             (M.getCodeGenOpts().AllTocData && !GV->hasLocalLinkage()))
      GVar->addAttribute("toc-data");
  }
}

// PowerPC-32
namespace {
/// PPC32_SVR4_ABIInfo - The 32-bit PowerPC ELF (SVR4) ABI information.
class PPC32_SVR4_ABIInfo : public DefaultABIInfo {
  bool IsSoftFloatABI;
  bool IsRetSmallStructInRegABI;

  CharUnits getParamTypeAlignment(QualType Ty) const;

public:
  PPC32_SVR4_ABIInfo(CodeGen::CodeGenTypes &CGT, bool SoftFloatABI,
                     bool RetSmallStructInRegABI)
      : DefaultABIInfo(CGT), IsSoftFloatABI(SoftFloatABI),
        IsRetSmallStructInRegABI(RetSmallStructInRegABI) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class PPC32TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  PPC32TargetCodeGenInfo(CodeGenTypes &CGT, bool SoftFloatABI,
                         bool RetSmallStructInRegABI)
      : TargetCodeGenInfo(std::make_unique<PPC32_SVR4_ABIInfo>(
            CGT, SoftFloatABI, RetSmallStructInRegABI)) {}

  static bool isStructReturnInRegABI(const llvm::Triple &Triple,
                                     const CodeGenOptions &Opts);

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};
}

CharUnits PPC32_SVR4_ABIInfo::getParamTypeAlignment(QualType Ty) const {
  // Complex types are passed just like their elements.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  if (Ty->isVectorType())
    return CharUnits::fromQuantity(getContext().getTypeSize(Ty) == 128 ? 16
                                                                       : 4);

  // For single-element float/vector structs, we consider the whole type
  // to have the same alignment requirements as its single element.
  const Type *AlignTy = nullptr;
  if (const Type *EltType = isSingleElementStruct(Ty, getContext())) {
    const BuiltinType *BT = EltType->getAs<BuiltinType>();
    if ((EltType->isVectorType() && getContext().getTypeSize(EltType) == 128) ||
        (BT && BT->isFloatingPoint()))
      AlignTy = EltType;
  }

  if (AlignTy)
    return CharUnits::fromQuantity(AlignTy->isVectorType() ? 16 : 4);
  return CharUnits::fromQuantity(4);
}

ABIArgInfo PPC32_SVR4_ABIInfo::classifyReturnType(QualType RetTy) const {
  uint64_t Size;

  // -msvr4-struct-return puts small aggregates in GPR3 and GPR4.
  if (isAggregateTypeForABI(RetTy) && IsRetSmallStructInRegABI &&
      (Size = getContext().getTypeSize(RetTy)) <= 64) {
    // System V ABI (1995), page 3-22, specified:
    // > A structure or union whose size is less than or equal to 8 bytes
    // > shall be returned in r3 and r4, as if it were first stored in the
    // > 8-byte aligned memory area and then the low addressed word were
    // > loaded into r3 and the high-addressed word into r4.  Bits beyond
    // > the last member of the structure or union are not defined.
    //
    // GCC for big-endian PPC32 inserts the pad before the first member,
    // not "beyond the last member" of the struct.  To stay compatible
    // with GCC, we coerce the struct to an integer of the same size.
    // LLVM will extend it and return i32 in r3, or i64 in r3:r4.
    if (Size == 0)
      return ABIArgInfo::getIgnore();
    else {
      llvm::Type *CoerceTy = llvm::Type::getIntNTy(getVMContext(), Size);
      return ABIArgInfo::getDirect(CoerceTy);
    }
  }

  return DefaultABIInfo::classifyReturnType(RetTy);
}

// TODO: this implementation is now likely redundant with
// DefaultABIInfo::EmitVAArg.
RValue PPC32_SVR4_ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAList,
                                     QualType Ty, AggValueSlot Slot) const {
  if (getTarget().getTriple().isOSDarwin()) {
    auto TI = getContext().getTypeInfoInChars(Ty);
    TI.Align = getParamTypeAlignment(Ty);

    CharUnits SlotSize = CharUnits::fromQuantity(4);
    return emitVoidPtrVAArg(CGF, VAList, Ty,
                            classifyArgumentType(Ty).isIndirect(), TI, SlotSize,
                            /*AllowHigherAlign=*/true, Slot);
  }

  const unsigned OverflowLimit = 8;
  if (const ComplexType *CTy = Ty->getAs<ComplexType>()) {
    // TODO: Implement this. For now ignore.
    (void)CTy;
    return RValue::getAggregate(Address::invalid()); // FIXME?
  }

  // struct __va_list_tag {
  //   unsigned char gpr;
  //   unsigned char fpr;
  //   unsigned short reserved;
  //   void *overflow_arg_area;
  //   void *reg_save_area;
  // };

  bool isI64 = Ty->isIntegerType() && getContext().getTypeSize(Ty) == 64;
  bool isInt = !Ty->isFloatingType();
  bool isF64 = Ty->isFloatingType() && getContext().getTypeSize(Ty) == 64;

  // All aggregates are passed indirectly?  That doesn't seem consistent
  // with the argument-lowering code.
  bool isIndirect = isAggregateTypeForABI(Ty);

  CGBuilderTy &Builder = CGF.Builder;

  // The calling convention either uses 1-2 GPRs or 1 FPR.
  Address NumRegsAddr = Address::invalid();
  if (isInt || IsSoftFloatABI) {
    NumRegsAddr = Builder.CreateStructGEP(VAList, 0, "gpr");
  } else {
    NumRegsAddr = Builder.CreateStructGEP(VAList, 1, "fpr");
  }

  llvm::Value *NumRegs = Builder.CreateLoad(NumRegsAddr, "numUsedRegs");

  // "Align" the register count when TY is i64.
  if (isI64 || (isF64 && IsSoftFloatABI)) {
    NumRegs = Builder.CreateAdd(NumRegs, Builder.getInt8(1));
    NumRegs = Builder.CreateAnd(NumRegs, Builder.getInt8((uint8_t) ~1U));
  }

  llvm::Value *CC =
      Builder.CreateICmpULT(NumRegs, Builder.getInt8(OverflowLimit), "cond");

  llvm::BasicBlock *UsingRegs = CGF.createBasicBlock("using_regs");
  llvm::BasicBlock *UsingOverflow = CGF.createBasicBlock("using_overflow");
  llvm::BasicBlock *Cont = CGF.createBasicBlock("cont");

  Builder.CreateCondBr(CC, UsingRegs, UsingOverflow);

  llvm::Type *DirectTy = CGF.ConvertType(Ty), *ElementTy = DirectTy;
  if (isIndirect)
    DirectTy = CGF.UnqualPtrTy;

  // Case 1: consume registers.
  Address RegAddr = Address::invalid();
  {
    CGF.EmitBlock(UsingRegs);

    Address RegSaveAreaPtr = Builder.CreateStructGEP(VAList, 4);
    RegAddr = Address(Builder.CreateLoad(RegSaveAreaPtr), CGF.Int8Ty,
                      CharUnits::fromQuantity(8));
    assert(RegAddr.getElementType() == CGF.Int8Ty);

    // Floating-point registers start after the general-purpose registers.
    if (!(isInt || IsSoftFloatABI)) {
      RegAddr = Builder.CreateConstInBoundsByteGEP(RegAddr,
                                                   CharUnits::fromQuantity(32));
    }

    // Get the address of the saved value by scaling the number of
    // registers we've used by the number of
    CharUnits RegSize = CharUnits::fromQuantity((isInt || IsSoftFloatABI) ? 4 : 8);
    llvm::Value *RegOffset =
        Builder.CreateMul(NumRegs, Builder.getInt8(RegSize.getQuantity()));
    RegAddr = Address(Builder.CreateInBoundsGEP(
                          CGF.Int8Ty, RegAddr.emitRawPointer(CGF), RegOffset),
                      DirectTy,
                      RegAddr.getAlignment().alignmentOfArrayElement(RegSize));

    // Increase the used-register count.
    NumRegs =
      Builder.CreateAdd(NumRegs,
                        Builder.getInt8((isI64 || (isF64 && IsSoftFloatABI)) ? 2 : 1));
    Builder.CreateStore(NumRegs, NumRegsAddr);

    CGF.EmitBranch(Cont);
  }

  // Case 2: consume space in the overflow area.
  Address MemAddr = Address::invalid();
  {
    CGF.EmitBlock(UsingOverflow);

    Builder.CreateStore(Builder.getInt8(OverflowLimit), NumRegsAddr);

    // Everything in the overflow area is rounded up to a size of at least 4.
    CharUnits OverflowAreaAlign = CharUnits::fromQuantity(4);

    CharUnits Size;
    if (!isIndirect) {
      auto TypeInfo = CGF.getContext().getTypeInfoInChars(Ty);
      Size = TypeInfo.Width.alignTo(OverflowAreaAlign);
    } else {
      Size = CGF.getPointerSize();
    }

    Address OverflowAreaAddr = Builder.CreateStructGEP(VAList, 3);
    Address OverflowArea =
        Address(Builder.CreateLoad(OverflowAreaAddr, "argp.cur"), CGF.Int8Ty,
                OverflowAreaAlign);
    // Round up address of argument to alignment
    CharUnits Align = CGF.getContext().getTypeAlignInChars(Ty);
    if (Align > OverflowAreaAlign) {
      llvm::Value *Ptr = OverflowArea.emitRawPointer(CGF);
      OverflowArea = Address(emitRoundPointerUpToAlignment(CGF, Ptr, Align),
                             OverflowArea.getElementType(), Align);
    }

    MemAddr = OverflowArea.withElementType(DirectTy);

    // Increase the overflow area.
    OverflowArea = Builder.CreateConstInBoundsByteGEP(OverflowArea, Size);
    Builder.CreateStore(OverflowArea.emitRawPointer(CGF), OverflowAreaAddr);
    CGF.EmitBranch(Cont);
  }

  CGF.EmitBlock(Cont);

  // Merge the cases with a phi.
  Address Result = emitMergePHI(CGF, RegAddr, UsingRegs, MemAddr, UsingOverflow,
                                "vaarg.addr");

  // Load the pointer if the argument was passed indirectly.
  if (isIndirect) {
    Result = Address(Builder.CreateLoad(Result, "aggr"), ElementTy,
                     getContext().getTypeAlignInChars(Ty));
  }

  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(Result, Ty), Slot);
}

bool PPC32TargetCodeGenInfo::isStructReturnInRegABI(
    const llvm::Triple &Triple, const CodeGenOptions &Opts) {
  assert(Triple.isPPC32());

  switch (Opts.getStructReturnConvention()) {
  case CodeGenOptions::SRCK_Default:
    break;
  case CodeGenOptions::SRCK_OnStack: // -maix-struct-return
    return false;
  case CodeGenOptions::SRCK_InRegs: // -msvr4-struct-return
    return true;
  }

  if (Triple.isOSBinFormatELF() && !Triple.isOSLinux())
    return true;

  return false;
}

bool
PPC32TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {
  return PPC_initDwarfEHRegSizeTable(CGF, Address, /*Is64Bit*/ false,
                                     /*IsAIX*/ false);
}

// PowerPC-64

namespace {

/// PPC64_SVR4_ABIInfo - The 64-bit PowerPC ELF (SVR4) ABI information.
class PPC64_SVR4_ABIInfo : public ABIInfo {
  static const unsigned GPRBits = 64;
  PPC64_SVR4_ABIKind Kind;
  bool IsSoftFloatABI;

public:
  PPC64_SVR4_ABIInfo(CodeGen::CodeGenTypes &CGT, PPC64_SVR4_ABIKind Kind,
                     bool SoftFloatABI)
      : ABIInfo(CGT), Kind(Kind), IsSoftFloatABI(SoftFloatABI) {}

  bool isPromotableTypeForABI(QualType Ty) const;
  CharUnits getParamTypeAlignment(QualType Ty) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType Ty) const;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override;
  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t Members) const override;

  // TODO: We can add more logic to computeInfo to improve performance.
  // Example: For aggregate arguments that fit in a register, we could
  // use getDirectInReg (as is done below for structs containing a single
  // floating-point value) to avoid pushing them to memory on function
  // entry.  This would require changing the logic in PPCISelLowering
  // when lowering the parameters in the caller and args in the callee.
  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments()) {
      // We rely on the default argument classification for the most part.
      // One exception:  An aggregate containing a single floating-point
      // or vector item must be passed in a register if one is available.
      const Type *T = isSingleElementStruct(I.type, getContext());
      if (T) {
        const BuiltinType *BT = T->getAs<BuiltinType>();
        if ((T->isVectorType() && getContext().getTypeSize(T) == 128) ||
            (BT && BT->isFloatingPoint())) {
          QualType QT(T, 0);
          I.info = ABIArgInfo::getDirectInReg(CGT.ConvertType(QT));
          continue;
        }
      }
      I.info = classifyArgumentType(I.type);
    }
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class PPC64_SVR4_TargetCodeGenInfo : public TargetCodeGenInfo {

public:
  PPC64_SVR4_TargetCodeGenInfo(CodeGenTypes &CGT, PPC64_SVR4_ABIKind Kind,
                               bool SoftFloatABI)
      : TargetCodeGenInfo(
            std::make_unique<PPC64_SVR4_ABIInfo>(CGT, Kind, SoftFloatABI)) {
    SwiftInfo =
        std::make_unique<SwiftABIInfo>(CGT, /*SwiftErrorInRegister=*/false);
  }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
  void emitTargetMetadata(CodeGen::CodeGenModule &CGM,
                          const llvm::MapVector<GlobalDecl, StringRef>
                              &MangledDeclNames) const override;
};

class PPC64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  PPC64TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<DefaultABIInfo>(CGT)) {}

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &M) const override {
    // This is recovered from gcc output.
    return 1; // r1 is the dedicated stack pointer
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;
};
}

// Return true if the ABI requires Ty to be passed sign- or zero-
// extended to 64 bits.
bool
PPC64_SVR4_ABIInfo::isPromotableTypeForABI(QualType Ty) const {
  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  // Promotable integer types are required to be promoted by the ABI.
  if (isPromotableIntegerTypeForABI(Ty))
    return true;

  // In addition to the usual promotable integer types, we also need to
  // extend all 32-bit types, since the ABI requires promotion to 64 bits.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>())
    switch (BT->getKind()) {
    case BuiltinType::Int:
    case BuiltinType::UInt:
      return true;
    default:
      break;
    }

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() < 64)
      return true;

  return false;
}

/// isAlignedParamType - Determine whether a type requires 16-byte or
/// higher alignment in the parameter area.  Always returns at least 8.
CharUnits PPC64_SVR4_ABIInfo::getParamTypeAlignment(QualType Ty) const {
  // Complex types are passed just like their elements.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  auto FloatUsesVector = [this](QualType Ty){
    return Ty->isRealFloatingType() && &getContext().getFloatTypeSemantics(
                                           Ty) == &llvm::APFloat::IEEEquad();
  };

  // Only vector types of size 16 bytes need alignment (larger types are
  // passed via reference, smaller types are not aligned).
  if (Ty->isVectorType()) {
    return CharUnits::fromQuantity(getContext().getTypeSize(Ty) == 128 ? 16 : 8);
  } else if (FloatUsesVector(Ty)) {
    // According to ABI document section 'Optional Save Areas': If extended
    // precision floating-point values in IEEE BINARY 128 QUADRUPLE PRECISION
    // format are supported, map them to a single quadword, quadword aligned.
    return CharUnits::fromQuantity(16);
  }

  // For single-element float/vector structs, we consider the whole type
  // to have the same alignment requirements as its single element.
  const Type *AlignAsType = nullptr;
  const Type *EltType = isSingleElementStruct(Ty, getContext());
  if (EltType) {
    const BuiltinType *BT = EltType->getAs<BuiltinType>();
    if ((EltType->isVectorType() && getContext().getTypeSize(EltType) == 128) ||
        (BT && BT->isFloatingPoint()))
      AlignAsType = EltType;
  }

  // Likewise for ELFv2 homogeneous aggregates.
  const Type *Base = nullptr;
  uint64_t Members = 0;
  if (!AlignAsType && Kind == PPC64_SVR4_ABIKind::ELFv2 &&
      isAggregateTypeForABI(Ty) && isHomogeneousAggregate(Ty, Base, Members))
    AlignAsType = Base;

  // With special case aggregates, only vector base types need alignment.
  if (AlignAsType) {
    bool UsesVector = AlignAsType->isVectorType() ||
                      FloatUsesVector(QualType(AlignAsType, 0));
    return CharUnits::fromQuantity(UsesVector ? 16 : 8);
  }

  // Otherwise, we only need alignment for any aggregate type that
  // has an alignment requirement of >= 16 bytes.
  if (isAggregateTypeForABI(Ty) && getContext().getTypeAlign(Ty) >= 128) {
    return CharUnits::fromQuantity(16);
  }

  return CharUnits::fromQuantity(8);
}

bool PPC64_SVR4_ABIInfo::isHomogeneousAggregateBaseType(QualType Ty) const {
  // Homogeneous aggregates for ELFv2 must have base types of float,
  // double, long double, or 128-bit vectors.
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->getKind() == BuiltinType::Float ||
        BT->getKind() == BuiltinType::Double ||
        BT->getKind() == BuiltinType::LongDouble ||
        BT->getKind() == BuiltinType::Ibm128 ||
        (getContext().getTargetInfo().hasFloat128Type() &&
         (BT->getKind() == BuiltinType::Float128))) {
      if (IsSoftFloatABI)
        return false;
      return true;
    }
  }
  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    if (getContext().getTypeSize(VT) == 128)
      return true;
  }
  return false;
}

bool PPC64_SVR4_ABIInfo::isHomogeneousAggregateSmallEnough(
    const Type *Base, uint64_t Members) const {
  // Vector and fp128 types require one register, other floating point types
  // require one or two registers depending on their size.
  uint32_t NumRegs =
      ((getContext().getTargetInfo().hasFloat128Type() &&
          Base->isFloat128Type()) ||
        Base->isVectorType()) ? 1
                              : (getContext().getTypeSize(Base) + 63) / 64;

  // Homogeneous Aggregates may occupy at most 8 registers.
  return Members * NumRegs <= 8;
}

ABIArgInfo
PPC64_SVR4_ABIInfo::classifyArgumentType(QualType Ty) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  if (Ty->isAnyComplexType())
    return ABIArgInfo::getDirect();

  // Non-Altivec vector types are passed in GPRs (smaller than 16 bytes)
  // or via reference (larger than 16 bytes).
  if (Ty->isVectorType()) {
    uint64_t Size = getContext().getTypeSize(Ty);
    if (Size > 128)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
    else if (Size < 128) {
      llvm::Type *CoerceTy = llvm::IntegerType::get(getVMContext(), Size);
      return ABIArgInfo::getDirect(CoerceTy);
    }
  }

  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() > 128)
      return getNaturalAlignIndirect(Ty, /*ByVal=*/true);

  if (isAggregateTypeForABI(Ty)) {
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

    uint64_t ABIAlign = getParamTypeAlignment(Ty).getQuantity();
    uint64_t TyAlign = getContext().getTypeAlignInChars(Ty).getQuantity();

    // ELFv2 homogeneous aggregates are passed as array types.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (Kind == PPC64_SVR4_ABIKind::ELFv2 &&
        isHomogeneousAggregate(Ty, Base, Members)) {
      llvm::Type *BaseTy = CGT.ConvertType(QualType(Base, 0));
      llvm::Type *CoerceTy = llvm::ArrayType::get(BaseTy, Members);
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // If an aggregate may end up fully in registers, we do not
    // use the ByVal method, but pass the aggregate as array.
    // This is usually beneficial since we avoid forcing the
    // back-end to store the argument to memory.
    uint64_t Bits = getContext().getTypeSize(Ty);
    if (Bits > 0 && Bits <= 8 * GPRBits) {
      llvm::Type *CoerceTy;

      // Types up to 8 bytes are passed as integer type (which will be
      // properly aligned in the argument save area doubleword).
      if (Bits <= GPRBits)
        CoerceTy =
            llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));
      // Larger types are passed as arrays, with the base type selected
      // according to the required alignment in the save area.
      else {
        uint64_t RegBits = ABIAlign * 8;
        uint64_t NumRegs = llvm::alignTo(Bits, RegBits) / RegBits;
        llvm::Type *RegTy = llvm::IntegerType::get(getVMContext(), RegBits);
        CoerceTy = llvm::ArrayType::get(RegTy, NumRegs);
      }

      return ABIArgInfo::getDirect(CoerceTy);
    }

    // All other aggregates are passed ByVal.
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(ABIAlign),
                                   /*ByVal=*/true,
                                   /*Realign=*/TyAlign > ABIAlign);
  }

  return (isPromotableTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                     : ABIArgInfo::getDirect());
}

ABIArgInfo
PPC64_SVR4_ABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  if (RetTy->isAnyComplexType())
    return ABIArgInfo::getDirect();

  // Non-Altivec vector types are returned in GPRs (smaller than 16 bytes)
  // or via reference (larger than 16 bytes).
  if (RetTy->isVectorType()) {
    uint64_t Size = getContext().getTypeSize(RetTy);
    if (Size > 128)
      return getNaturalAlignIndirect(RetTy);
    else if (Size < 128) {
      llvm::Type *CoerceTy = llvm::IntegerType::get(getVMContext(), Size);
      return ABIArgInfo::getDirect(CoerceTy);
    }
  }

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() > 128)
      return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);

  if (isAggregateTypeForABI(RetTy)) {
    // ELFv2 homogeneous aggregates are returned as array types.
    const Type *Base = nullptr;
    uint64_t Members = 0;
    if (Kind == PPC64_SVR4_ABIKind::ELFv2 &&
        isHomogeneousAggregate(RetTy, Base, Members)) {
      llvm::Type *BaseTy = CGT.ConvertType(QualType(Base, 0));
      llvm::Type *CoerceTy = llvm::ArrayType::get(BaseTy, Members);
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // ELFv2 small aggregates are returned in up to two registers.
    uint64_t Bits = getContext().getTypeSize(RetTy);
    if (Kind == PPC64_SVR4_ABIKind::ELFv2 && Bits <= 2 * GPRBits) {
      if (Bits == 0)
        return ABIArgInfo::getIgnore();

      llvm::Type *CoerceTy;
      if (Bits > GPRBits) {
        CoerceTy = llvm::IntegerType::get(getVMContext(), GPRBits);
        CoerceTy = llvm::StructType::get(CoerceTy, CoerceTy);
      } else
        CoerceTy =
            llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));
      return ABIArgInfo::getDirect(CoerceTy);
    }

    // All other aggregates are returned indirectly.
    return getNaturalAlignIndirect(RetTy);
  }

  return (isPromotableTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                        : ABIArgInfo::getDirect());
}

// Based on ARMABIInfo::EmitVAArg, adjusted for 64-bit machine.
RValue PPC64_SVR4_ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                     QualType Ty, AggValueSlot Slot) const {
  auto TypeInfo = getContext().getTypeInfoInChars(Ty);
  TypeInfo.Align = getParamTypeAlignment(Ty);

  CharUnits SlotSize = CharUnits::fromQuantity(8);

  // If we have a complex type and the base type is smaller than 8 bytes,
  // the ABI calls for the real and imaginary parts to be right-adjusted
  // in separate doublewords.  However, Clang expects us to produce a
  // pointer to a structure with the two parts packed tightly.  So generate
  // loads of the real and imaginary parts relative to the va_list pointer,
  // and store them to a temporary structure.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>()) {
    CharUnits EltSize = TypeInfo.Width / 2;
    if (EltSize < SlotSize)
      return complexTempStructure(CGF, VAListAddr, Ty, SlotSize, EltSize, CTy);
  }

  // Otherwise, just use the general rule.
  //
  // The PPC64 ABI passes some arguments in integer registers, even to variadic
  // functions. To allow va_list to use the simple "void*" representation,
  // variadic calls allocate space in the argument area for the integer argument
  // registers, and variadic functions spill their integer argument registers to
  // this area in their prologues. When aggregates smaller than a register are
  // passed this way, they are passed in the least significant bits of the
  // register, which means that after spilling on big-endian targets they will
  // be right-aligned in their argument slot. This is uncommon; for a variety of
  // reasons, other big-endian targets don't end up right-aligning aggregate
  // types this way, and so right-alignment only applies to fundamental types.
  // So on PPC64, we must force the use of right-alignment even for aggregates.
  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect*/ false, TypeInfo,
                          SlotSize, /*AllowHigher*/ true, Slot,
                          /*ForceRightAdjust*/ true);
}

bool
PPC64_SVR4_TargetCodeGenInfo::initDwarfEHRegSizeTable(
  CodeGen::CodeGenFunction &CGF,
  llvm::Value *Address) const {
  return PPC_initDwarfEHRegSizeTable(CGF, Address, /*Is64Bit*/ true,
                                     /*IsAIX*/ false);
}

void PPC64_SVR4_TargetCodeGenInfo::emitTargetMetadata(
    CodeGen::CodeGenModule &CGM,
    const llvm::MapVector<GlobalDecl, StringRef> &MangledDeclNames) const {
  if (CGM.getTypes().isLongDoubleReferenced()) {
    llvm::LLVMContext &Ctx = CGM.getLLVMContext();
    const auto *flt = &CGM.getTarget().getLongDoubleFormat();
    if (flt == &llvm::APFloat::PPCDoubleDouble())
      CGM.getModule().addModuleFlag(llvm::Module::Error, "float-abi",
                                    llvm::MDString::get(Ctx, "doubledouble"));
    else if (flt == &llvm::APFloat::IEEEquad())
      CGM.getModule().addModuleFlag(llvm::Module::Error, "float-abi",
                                    llvm::MDString::get(Ctx, "ieeequad"));
    else if (flt == &llvm::APFloat::IEEEdouble())
      CGM.getModule().addModuleFlag(llvm::Module::Error, "float-abi",
                                    llvm::MDString::get(Ctx, "ieeedouble"));
  }
}

bool
PPC64TargetCodeGenInfo::initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                                                llvm::Value *Address) const {
  return PPC_initDwarfEHRegSizeTable(CGF, Address, /*Is64Bit*/ true,
                                     /*IsAIX*/ false);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createAIXTargetCodeGenInfo(CodeGenModule &CGM, bool Is64Bit) {
  return std::make_unique<AIXTargetCodeGenInfo>(CGM.getTypes(), Is64Bit);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createPPC32TargetCodeGenInfo(CodeGenModule &CGM, bool SoftFloatABI) {
  bool RetSmallStructInRegABI = PPC32TargetCodeGenInfo::isStructReturnInRegABI(
      CGM.getTriple(), CGM.getCodeGenOpts());
  return std::make_unique<PPC32TargetCodeGenInfo>(CGM.getTypes(), SoftFloatABI,
                                                  RetSmallStructInRegABI);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createPPC64TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<PPC64TargetCodeGenInfo>(CGM.getTypes());
}

std::unique_ptr<TargetCodeGenInfo> CodeGen::createPPC64_SVR4_TargetCodeGenInfo(
    CodeGenModule &CGM, PPC64_SVR4_ABIKind Kind, bool SoftFloatABI) {
  return std::make_unique<PPC64_SVR4_TargetCodeGenInfo>(CGM.getTypes(), Kind,
                                                        SoftFloatABI);
}
