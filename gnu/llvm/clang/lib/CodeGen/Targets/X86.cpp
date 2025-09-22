//===- X86.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "llvm/ADT/SmallBitVector.h"

using namespace clang;
using namespace clang::CodeGen;

namespace {

/// IsX86_MMXType - Return true if this is an MMX type.
bool IsX86_MMXType(llvm::Type *IRType) {
  // Return true if the type is an MMX type <2 x i32>, <4 x i16>, or <8 x i8>.
  return IRType->isVectorTy() && IRType->getPrimitiveSizeInBits() == 64 &&
    cast<llvm::VectorType>(IRType)->getElementType()->isIntegerTy() &&
    IRType->getScalarSizeInBits() != 64;
}

static llvm::Type* X86AdjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                          StringRef Constraint,
                                          llvm::Type* Ty) {
  bool IsMMXCons = llvm::StringSwitch<bool>(Constraint)
                     .Cases("y", "&y", "^Ym", true)
                     .Default(false);
  if (IsMMXCons && Ty->isVectorTy()) {
    if (cast<llvm::VectorType>(Ty)->getPrimitiveSizeInBits().getFixedValue() !=
        64) {
      // Invalid MMX constraint
      return nullptr;
    }

    return llvm::Type::getX86_MMXTy(CGF.getLLVMContext());
  }

  if (Constraint == "k") {
    llvm::Type *Int1Ty = llvm::Type::getInt1Ty(CGF.getLLVMContext());
    return llvm::FixedVectorType::get(Int1Ty, Ty->getScalarSizeInBits());
  }

  // No operation needed
  return Ty;
}

/// Returns true if this type can be passed in SSE registers with the
/// X86_VectorCall calling convention. Shared between x86_32 and x86_64.
static bool isX86VectorTypeForVectorCall(ASTContext &Context, QualType Ty) {
  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    if (BT->isFloatingPoint() && BT->getKind() != BuiltinType::Half) {
      if (BT->getKind() == BuiltinType::LongDouble) {
        if (&Context.getTargetInfo().getLongDoubleFormat() ==
            &llvm::APFloat::x87DoubleExtended())
          return false;
      }
      return true;
    }
  } else if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // vectorcall can pass XMM, YMM, and ZMM vectors. We don't pass SSE1 MMX
    // registers specially.
    unsigned VecSize = Context.getTypeSize(VT);
    if (VecSize == 128 || VecSize == 256 || VecSize == 512)
      return true;
  }
  return false;
}

/// Returns true if this aggregate is small enough to be passed in SSE registers
/// in the X86_VectorCall calling convention. Shared between x86_32 and x86_64.
static bool isX86VectorCallAggregateSmallEnough(uint64_t NumMembers) {
  return NumMembers <= 4;
}

/// Returns a Homogeneous Vector Aggregate ABIArgInfo, used in X86.
static ABIArgInfo getDirectX86Hva(llvm::Type* T = nullptr) {
  auto AI = ABIArgInfo::getDirect(T);
  AI.setInReg(true);
  AI.setCanBeFlattened(false);
  return AI;
}

//===----------------------------------------------------------------------===//
// X86-32 ABI Implementation
//===----------------------------------------------------------------------===//

/// Similar to llvm::CCState, but for Clang.
struct CCState {
  CCState(CGFunctionInfo &FI)
      : IsPreassigned(FI.arg_size()), CC(FI.getCallingConvention()),
	Required(FI.getRequiredArgs()), IsDelegateCall(FI.isDelegateCall()) {}

  llvm::SmallBitVector IsPreassigned;
  unsigned CC = CallingConv::CC_C;
  unsigned FreeRegs = 0;
  unsigned FreeSSERegs = 0;
  RequiredArgs Required;
  bool IsDelegateCall = false;
};

/// X86_32ABIInfo - The X86-32 ABI information.
class X86_32ABIInfo : public ABIInfo {
  enum Class {
    Integer,
    Float
  };

  static const unsigned MinABIStackAlignInBytes = 4;

  bool IsDarwinVectorABI;
  bool IsRetSmallStructInRegABI;
  bool IsWin32StructABI;
  bool IsSoftFloatABI;
  bool IsMCUABI;
  bool IsLinuxABI;
  unsigned DefaultNumRegisterParameters;

  static bool isRegisterSize(unsigned Size) {
    return (Size == 8 || Size == 16 || Size == 32 || Size == 64);
  }

  bool isHomogeneousAggregateBaseType(QualType Ty) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorTypeForVectorCall(getContext(), Ty);
  }

  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t NumMembers) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorCallAggregateSmallEnough(NumMembers);
  }

  bool shouldReturnTypeInRegister(QualType Ty, ASTContext &Context) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be passed in memory.
  ABIArgInfo getIndirectResult(QualType Ty, bool ByVal, CCState &State) const;

  ABIArgInfo getIndirectReturnResult(QualType Ty, CCState &State) const;

  /// Return the alignment to use for the given type on the stack.
  unsigned getTypeStackAlignInBytes(QualType Ty, unsigned Align) const;

  Class classify(QualType Ty) const;
  ABIArgInfo classifyReturnType(QualType RetTy, CCState &State) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, CCState &State,
                                  unsigned ArgIndex) const;

  /// Updates the number of available free registers, returns
  /// true if any registers were allocated.
  bool updateFreeRegs(QualType Ty, CCState &State) const;

  bool shouldAggregateUseDirect(QualType Ty, CCState &State, bool &InReg,
                                bool &NeedsPadding) const;
  bool shouldPrimitiveUseInReg(QualType Ty, CCState &State) const;

  bool canExpandIndirectArgument(QualType Ty) const;

  /// Rewrite the function info so that all memory arguments use
  /// inalloca.
  void rewriteWithInAlloca(CGFunctionInfo &FI) const;

  void addFieldToArgStruct(SmallVector<llvm::Type *, 6> &FrameFields,
                           CharUnits &StackOffset, ABIArgInfo &Info,
                           QualType Type) const;
  void runVectorCallFirstPass(CGFunctionInfo &FI, CCState &State) const;

public:

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  X86_32ABIInfo(CodeGen::CodeGenTypes &CGT, bool DarwinVectorABI,
                bool RetSmallStructInRegABI, bool Win32StructABI,
                unsigned NumRegisterParameters, bool SoftFloatABI)
      : ABIInfo(CGT), IsDarwinVectorABI(DarwinVectorABI),
        IsRetSmallStructInRegABI(RetSmallStructInRegABI),
        IsWin32StructABI(Win32StructABI), IsSoftFloatABI(SoftFloatABI),
        IsMCUABI(CGT.getTarget().getTriple().isOSIAMCU()),
        IsLinuxABI(CGT.getTarget().getTriple().isOSLinux() ||
                   CGT.getTarget().getTriple().isOSCygMing()),
        DefaultNumRegisterParameters(NumRegisterParameters) {}
};

class X86_32SwiftABIInfo : public SwiftABIInfo {
public:
  explicit X86_32SwiftABIInfo(CodeGenTypes &CGT)
      : SwiftABIInfo(CGT, /*SwiftErrorInRegister=*/false) {}

  bool shouldPassIndirectly(ArrayRef<llvm::Type *> ComponentTys,
                            bool AsReturnValue) const override {
    // LLVM's x86-32 lowering currently only assigns up to three
    // integer registers and three fp registers.  Oddly, it'll use up to
    // four vector registers for vectors, but those can overlap with the
    // scalar registers.
    return occupiesMoreThan(ComponentTys, /*total=*/3);
  }
};

class X86_32TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  X86_32TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, bool DarwinVectorABI,
                          bool RetSmallStructInRegABI, bool Win32StructABI,
                          unsigned NumRegisterParameters, bool SoftFloatABI)
      : TargetCodeGenInfo(std::make_unique<X86_32ABIInfo>(
            CGT, DarwinVectorABI, RetSmallStructInRegABI, Win32StructABI,
            NumRegisterParameters, SoftFloatABI)) {
    SwiftInfo = std::make_unique<X86_32SwiftABIInfo>(CGT);
  }

  static bool isStructReturnInRegABI(
      const llvm::Triple &Triple, const CodeGenOptions &Opts);

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    // Darwin uses different dwarf register numbers for EH.
    if (CGM.getTarget().getTriple().isOSDarwin()) return 5;
    return 4;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override;

  llvm::Type* adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                  StringRef Constraint,
                                  llvm::Type* Ty) const override {
    return X86AdjustInlineAsmType(CGF, Constraint, Ty);
  }

  void addReturnRegisterOutputs(CodeGenFunction &CGF, LValue ReturnValue,
                                std::string &Constraints,
                                std::vector<llvm::Type *> &ResultRegTypes,
                                std::vector<llvm::Type *> &ResultTruncRegTypes,
                                std::vector<LValue> &ResultRegDests,
                                std::string &AsmString,
                                unsigned NumOutputs) const override;

  StringRef getARCRetainAutoreleasedReturnValueMarker() const override {
    return "movl\t%ebp, %ebp"
           "\t\t// marker for objc_retainAutoreleaseReturnValue";
  }
};

}

/// Rewrite input constraint references after adding some output constraints.
/// In the case where there is one output and one input and we add one output,
/// we need to replace all operand references greater than or equal to 1:
///     mov $0, $1
///     mov eax, $1
/// The result will be:
///     mov $0, $2
///     mov eax, $2
static void rewriteInputConstraintReferences(unsigned FirstIn,
                                             unsigned NumNewOuts,
                                             std::string &AsmString) {
  std::string Buf;
  llvm::raw_string_ostream OS(Buf);
  size_t Pos = 0;
  while (Pos < AsmString.size()) {
    size_t DollarStart = AsmString.find('$', Pos);
    if (DollarStart == std::string::npos)
      DollarStart = AsmString.size();
    size_t DollarEnd = AsmString.find_first_not_of('$', DollarStart);
    if (DollarEnd == std::string::npos)
      DollarEnd = AsmString.size();
    OS << StringRef(&AsmString[Pos], DollarEnd - Pos);
    Pos = DollarEnd;
    size_t NumDollars = DollarEnd - DollarStart;
    if (NumDollars % 2 != 0 && Pos < AsmString.size()) {
      // We have an operand reference.
      size_t DigitStart = Pos;
      if (AsmString[DigitStart] == '{') {
        OS << '{';
        ++DigitStart;
      }
      size_t DigitEnd = AsmString.find_first_not_of("0123456789", DigitStart);
      if (DigitEnd == std::string::npos)
        DigitEnd = AsmString.size();
      StringRef OperandStr(&AsmString[DigitStart], DigitEnd - DigitStart);
      unsigned OperandIndex;
      if (!OperandStr.getAsInteger(10, OperandIndex)) {
        if (OperandIndex >= FirstIn)
          OperandIndex += NumNewOuts;
        OS << OperandIndex;
      } else {
        OS << OperandStr;
      }
      Pos = DigitEnd;
    }
  }
  AsmString = std::move(OS.str());
}

/// Add output constraints for EAX:EDX because they are return registers.
void X86_32TargetCodeGenInfo::addReturnRegisterOutputs(
    CodeGenFunction &CGF, LValue ReturnSlot, std::string &Constraints,
    std::vector<llvm::Type *> &ResultRegTypes,
    std::vector<llvm::Type *> &ResultTruncRegTypes,
    std::vector<LValue> &ResultRegDests, std::string &AsmString,
    unsigned NumOutputs) const {
  uint64_t RetWidth = CGF.getContext().getTypeSize(ReturnSlot.getType());

  // Use the EAX constraint if the width is 32 or smaller and EAX:EDX if it is
  // larger.
  if (!Constraints.empty())
    Constraints += ',';
  if (RetWidth <= 32) {
    Constraints += "={eax}";
    ResultRegTypes.push_back(CGF.Int32Ty);
  } else {
    // Use the 'A' constraint for EAX:EDX.
    Constraints += "=A";
    ResultRegTypes.push_back(CGF.Int64Ty);
  }

  // Truncate EAX or EAX:EDX to an integer of the appropriate size.
  llvm::Type *CoerceTy = llvm::IntegerType::get(CGF.getLLVMContext(), RetWidth);
  ResultTruncRegTypes.push_back(CoerceTy);

  // Coerce the integer by bitcasting the return slot pointer.
  ReturnSlot.setAddress(ReturnSlot.getAddress().withElementType(CoerceTy));
  ResultRegDests.push_back(ReturnSlot);

  rewriteInputConstraintReferences(NumOutputs, 1, AsmString);
}

/// shouldReturnTypeInRegister - Determine if the given type should be
/// returned in a register (for the Darwin and MCU ABI).
bool X86_32ABIInfo::shouldReturnTypeInRegister(QualType Ty,
                                               ASTContext &Context) const {
  uint64_t Size = Context.getTypeSize(Ty);

  // For i386, type must be register sized.
  // For the MCU ABI, it only needs to be <= 8-byte
  if ((IsMCUABI && Size > 64) || (!IsMCUABI && !isRegisterSize(Size)))
   return false;

  if (Ty->isVectorType()) {
    // 64- and 128- bit vectors inside structures are not returned in
    // registers.
    if (Size == 64 || Size == 128)
      return false;

    return true;
  }

  // If this is a builtin, pointer, enum, complex type, member pointer, or
  // member function pointer it is ok.
  if (Ty->getAs<BuiltinType>() || Ty->hasPointerRepresentation() ||
      Ty->isAnyComplexType() || Ty->isEnumeralType() ||
      Ty->isBlockPointerType() || Ty->isMemberPointerType())
    return true;

  // Arrays are treated like records.
  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty))
    return shouldReturnTypeInRegister(AT->getElementType(), Context);

  // Otherwise, it must be a record type.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT) return false;

  // FIXME: Traverse bases here too.

  // Structure types are passed in register if all fields would be
  // passed in a register.
  for (const auto *FD : RT->getDecl()->fields()) {
    // Empty fields are ignored.
    if (isEmptyField(Context, FD, true))
      continue;

    // Check fields recursively.
    if (!shouldReturnTypeInRegister(FD->getType(), Context))
      return false;
  }
  return true;
}

static bool is32Or64BitBasicType(QualType Ty, ASTContext &Context) {
  // Treat complex types as the element type.
  if (const ComplexType *CTy = Ty->getAs<ComplexType>())
    Ty = CTy->getElementType();

  // Check for a type which we know has a simple scalar argument-passing
  // convention without any padding.  (We're specifically looking for 32
  // and 64-bit integer and integer-equivalents, float, and double.)
  if (!Ty->getAs<BuiltinType>() && !Ty->hasPointerRepresentation() &&
      !Ty->isEnumeralType() && !Ty->isBlockPointerType())
    return false;

  uint64_t Size = Context.getTypeSize(Ty);
  return Size == 32 || Size == 64;
}

static bool addFieldSizes(ASTContext &Context, const RecordDecl *RD,
                          uint64_t &Size) {
  for (const auto *FD : RD->fields()) {
    // Scalar arguments on the stack get 4 byte alignment on x86. If the
    // argument is smaller than 32-bits, expanding the struct will create
    // alignment padding.
    if (!is32Or64BitBasicType(FD->getType(), Context))
      return false;

    // FIXME: Reject bit-fields wholesale; there are two problems, we don't know
    // how to expand them yet, and the predicate for telling if a bitfield still
    // counts as "basic" is more complicated than what we were doing previously.
    if (FD->isBitField())
      return false;

    Size += Context.getTypeSize(FD->getType());
  }
  return true;
}

static bool addBaseAndFieldSizes(ASTContext &Context, const CXXRecordDecl *RD,
                                 uint64_t &Size) {
  // Don't do this if there are any non-empty bases.
  for (const CXXBaseSpecifier &Base : RD->bases()) {
    if (!addBaseAndFieldSizes(Context, Base.getType()->getAsCXXRecordDecl(),
                              Size))
      return false;
  }
  if (!addFieldSizes(Context, RD, Size))
    return false;
  return true;
}

/// Test whether an argument type which is to be passed indirectly (on the
/// stack) would have the equivalent layout if it was expanded into separate
/// arguments. If so, we prefer to do the latter to avoid inhibiting
/// optimizations.
bool X86_32ABIInfo::canExpandIndirectArgument(QualType Ty) const {
  // We can only expand structure types.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (!RT)
    return false;
  const RecordDecl *RD = RT->getDecl();
  uint64_t Size = 0;
  if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
    if (!IsWin32StructABI) {
      // On non-Windows, we have to conservatively match our old bitcode
      // prototypes in order to be ABI-compatible at the bitcode level.
      if (!CXXRD->isCLike())
        return false;
    } else {
      // Don't do this for dynamic classes.
      if (CXXRD->isDynamicClass())
        return false;
    }
    if (!addBaseAndFieldSizes(getContext(), CXXRD, Size))
      return false;
  } else {
    if (!addFieldSizes(getContext(), RD, Size))
      return false;
  }

  // We can do this if there was no alignment padding.
  return Size == getContext().getTypeSize(Ty);
}

ABIArgInfo X86_32ABIInfo::getIndirectReturnResult(QualType RetTy, CCState &State) const {
  // If the return value is indirect, then the hidden argument is consuming one
  // integer register.
  if (State.CC != llvm::CallingConv::X86_FastCall &&
      State.CC != llvm::CallingConv::X86_VectorCall && State.FreeRegs) {
    --State.FreeRegs;
    if (!IsMCUABI)
      return getNaturalAlignIndirectInReg(RetTy);
  }
  return getNaturalAlignIndirect(RetTy, /*ByVal=*/false);
}

ABIArgInfo X86_32ABIInfo::classifyReturnType(QualType RetTy,
                                             CCState &State) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  if ((State.CC == llvm::CallingConv::X86_VectorCall ||
       State.CC == llvm::CallingConv::X86_RegCall) &&
      isHomogeneousAggregate(RetTy, Base, NumElts)) {
    // The LLVM struct type for such an aggregate should lower properly.
    return ABIArgInfo::getDirect();
  }

  if (const VectorType *VT = RetTy->getAs<VectorType>()) {
    // On Darwin, some vectors are returned in registers.
    if (IsDarwinVectorABI) {
      uint64_t Size = getContext().getTypeSize(RetTy);

      // 128-bit vectors are a special case; they are returned in
      // registers and we need to make sure to pick a type the LLVM
      // backend will like.
      if (Size == 128)
        return ABIArgInfo::getDirect(llvm::FixedVectorType::get(
            llvm::Type::getInt64Ty(getVMContext()), 2));

      // Always return in register if it fits in a general purpose
      // register, or if it is 64 bits and has a single element.
      if ((Size == 8 || Size == 16 || Size == 32) ||
          (Size == 64 && VT->getNumElements() == 1))
        return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),
                                                            Size));

      return getIndirectReturnResult(RetTy, State);
    }

    return ABIArgInfo::getDirect();
  }

  if (isAggregateTypeForABI(RetTy)) {
    if (const RecordType *RT = RetTy->getAs<RecordType>()) {
      // Structures with flexible arrays are always indirect.
      if (RT->getDecl()->hasFlexibleArrayMember())
        return getIndirectReturnResult(RetTy, State);
    }

    // If specified, structs and unions are always indirect.
    if (!IsRetSmallStructInRegABI && !RetTy->isAnyComplexType())
      return getIndirectReturnResult(RetTy, State);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), RetTy, true))
      return ABIArgInfo::getIgnore();

    // Return complex of _Float16 as <2 x half> so the backend will use xmm0.
    if (const ComplexType *CT = RetTy->getAs<ComplexType>()) {
      QualType ET = getContext().getCanonicalType(CT->getElementType());
      if (ET->isFloat16Type())
        return ABIArgInfo::getDirect(llvm::FixedVectorType::get(
            llvm::Type::getHalfTy(getVMContext()), 2));
    }

    // Small structures which are register sized are generally returned
    // in a register.
    if (shouldReturnTypeInRegister(RetTy, getContext())) {
      uint64_t Size = getContext().getTypeSize(RetTy);

      // As a special-case, if the struct is a "single-element" struct, and
      // the field is of type "float" or "double", return it in a
      // floating-point register. (MSVC does not apply this special case.)
      // We apply a similar transformation for pointer types to improve the
      // quality of the generated IR.
      if (const Type *SeltTy = isSingleElementStruct(RetTy, getContext()))
        if ((!IsWin32StructABI && SeltTy->isRealFloatingType())
            || SeltTy->hasPointerRepresentation())
          return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

      // FIXME: We should be able to narrow this integer in cases with dead
      // padding.
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),Size));
    }

    return getIndirectReturnResult(RetTy, State);
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  if (const auto *EIT = RetTy->getAs<BitIntType>())
    if (EIT->getNumBits() > 64)
      return getIndirectReturnResult(RetTy, State);

  return (isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                               : ABIArgInfo::getDirect());
}

unsigned X86_32ABIInfo::getTypeStackAlignInBytes(QualType Ty,
                                                 unsigned Align) const {
  // Otherwise, if the alignment is less than or equal to the minimum ABI
  // alignment, just use the default; the backend will handle this.
  if (Align <= MinABIStackAlignInBytes)
    return 0; // Use default alignment.

  if (IsLinuxABI) {
    // Exclude other System V OS (e.g Darwin, PS4 and FreeBSD) since we don't
    // want to spend any effort dealing with the ramifications of ABI breaks.
    //
    // If the vector type is __m128/__m256/__m512, return the default alignment.
    if (Ty->isVectorType() && (Align == 16 || Align == 32 || Align == 64))
      return Align;
  }
  // On non-Darwin, the stack type alignment is always 4.
  if (!IsDarwinVectorABI) {
    // Set explicit alignment, since we may need to realign the top.
    return MinABIStackAlignInBytes;
  }

  // Otherwise, if the type contains an SSE vector type, the alignment is 16.
  if (Align >= 16 && (isSIMDVectorType(getContext(), Ty) ||
                      isRecordWithSIMDVectorType(getContext(), Ty)))
    return 16;

  return MinABIStackAlignInBytes;
}

ABIArgInfo X86_32ABIInfo::getIndirectResult(QualType Ty, bool ByVal,
                                            CCState &State) const {
  if (!ByVal) {
    if (State.FreeRegs) {
      --State.FreeRegs; // Non-byval indirects just use one pointer.
      if (!IsMCUABI)
        return getNaturalAlignIndirectInReg(Ty);
    }
    return getNaturalAlignIndirect(Ty, false);
  }

  // Compute the byval alignment.
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  unsigned StackAlign = getTypeStackAlignInBytes(Ty, TypeAlign);
  if (StackAlign == 0)
    return ABIArgInfo::getIndirect(CharUnits::fromQuantity(4), /*ByVal=*/true);

  // If the stack alignment is less than the type alignment, realign the
  // argument.
  bool Realign = TypeAlign > StackAlign;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(StackAlign),
                                 /*ByVal=*/true, Realign);
}

X86_32ABIInfo::Class X86_32ABIInfo::classify(QualType Ty) const {
  const Type *T = isSingleElementStruct(Ty, getContext());
  if (!T)
    T = Ty.getTypePtr();

  if (const BuiltinType *BT = T->getAs<BuiltinType>()) {
    BuiltinType::Kind K = BT->getKind();
    if (K == BuiltinType::Float || K == BuiltinType::Double)
      return Float;
  }
  return Integer;
}

bool X86_32ABIInfo::updateFreeRegs(QualType Ty, CCState &State) const {
  if (!IsSoftFloatABI) {
    Class C = classify(Ty);
    if (C == Float)
      return false;
  }

  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = (Size + 31) / 32;

  if (SizeInRegs == 0)
    return false;

  if (!IsMCUABI) {
    if (SizeInRegs > State.FreeRegs) {
      State.FreeRegs = 0;
      return false;
    }
  } else {
    // The MCU psABI allows passing parameters in-reg even if there are
    // earlier parameters that are passed on the stack. Also,
    // it does not allow passing >8-byte structs in-register,
    // even if there are 3 free registers available.
    if (SizeInRegs > State.FreeRegs || SizeInRegs > 2)
      return false;
  }

  State.FreeRegs -= SizeInRegs;
  return true;
}

bool X86_32ABIInfo::shouldAggregateUseDirect(QualType Ty, CCState &State,
                                             bool &InReg,
                                             bool &NeedsPadding) const {
  // On Windows, aggregates other than HFAs are never passed in registers, and
  // they do not consume register slots. Homogenous floating-point aggregates
  // (HFAs) have already been dealt with at this point.
  if (IsWin32StructABI && isAggregateTypeForABI(Ty))
    return false;

  NeedsPadding = false;
  InReg = !IsMCUABI;

  if (!updateFreeRegs(Ty, State))
    return false;

  if (IsMCUABI)
    return true;

  if (State.CC == llvm::CallingConv::X86_FastCall ||
      State.CC == llvm::CallingConv::X86_VectorCall ||
      State.CC == llvm::CallingConv::X86_RegCall) {
    if (getContext().getTypeSize(Ty) <= 32 && State.FreeRegs)
      NeedsPadding = true;

    return false;
  }

  return true;
}

bool X86_32ABIInfo::shouldPrimitiveUseInReg(QualType Ty, CCState &State) const {
  bool IsPtrOrInt = (getContext().getTypeSize(Ty) <= 32) &&
                    (Ty->isIntegralOrEnumerationType() || Ty->isPointerType() ||
                     Ty->isReferenceType());

  if (!IsPtrOrInt && (State.CC == llvm::CallingConv::X86_FastCall ||
                      State.CC == llvm::CallingConv::X86_VectorCall))
    return false;

  if (!updateFreeRegs(Ty, State))
    return false;

  if (!IsPtrOrInt && State.CC == llvm::CallingConv::X86_RegCall)
    return false;

  // Return true to apply inreg to all legal parameters except for MCU targets.
  return !IsMCUABI;
}

void X86_32ABIInfo::runVectorCallFirstPass(CGFunctionInfo &FI, CCState &State) const {
  // Vectorcall x86 works subtly different than in x64, so the format is
  // a bit different than the x64 version.  First, all vector types (not HVAs)
  // are assigned, with the first 6 ending up in the [XYZ]MM0-5 registers.
  // This differs from the x64 implementation, where the first 6 by INDEX get
  // registers.
  // In the second pass over the arguments, HVAs are passed in the remaining
  // vector registers if possible, or indirectly by address. The address will be
  // passed in ECX/EDX if available. Any other arguments are passed according to
  // the usual fastcall rules.
  MutableArrayRef<CGFunctionInfoArgInfo> Args = FI.arguments();
  for (int I = 0, E = Args.size(); I < E; ++I) {
    const Type *Base = nullptr;
    uint64_t NumElts = 0;
    const QualType &Ty = Args[I].type;
    if ((Ty->isVectorType() || Ty->isBuiltinType()) &&
        isHomogeneousAggregate(Ty, Base, NumElts)) {
      if (State.FreeSSERegs >= NumElts) {
        State.FreeSSERegs -= NumElts;
        Args[I].info = ABIArgInfo::getDirectInReg();
        State.IsPreassigned.set(I);
      }
    }
  }
}

ABIArgInfo X86_32ABIInfo::classifyArgumentType(QualType Ty, CCState &State,
                                               unsigned ArgIndex) const {
  // FIXME: Set alignment on indirect arguments.
  bool IsFastCall = State.CC == llvm::CallingConv::X86_FastCall;
  bool IsRegCall = State.CC == llvm::CallingConv::X86_RegCall;
  bool IsVectorCall = State.CC == llvm::CallingConv::X86_VectorCall;

  Ty = useFirstFieldIfTransparentUnion(Ty);
  TypeInfo TI = getContext().getTypeInfo(Ty);

  // Check with the C++ ABI first.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect) {
      return getIndirectResult(Ty, false, State);
    } else if (State.IsDelegateCall) {
      // Avoid having different alignments on delegate call args by always
      // setting the alignment to 4, which is what we do for inallocas.
      ABIArgInfo Res = getIndirectResult(Ty, false, State);
      Res.setIndirectAlign(CharUnits::fromQuantity(4));
      return Res;
    } else if (RAA == CGCXXABI::RAA_DirectInMemory) {
      // The field index doesn't matter, we'll fix it up later.
      return ABIArgInfo::getInAlloca(/*FieldIndex=*/0);
    }
  }

  // Regcall uses the concept of a homogenous vector aggregate, similar
  // to other targets.
  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  if ((IsRegCall || IsVectorCall) &&
      isHomogeneousAggregate(Ty, Base, NumElts)) {
    if (State.FreeSSERegs >= NumElts) {
      State.FreeSSERegs -= NumElts;

      // Vectorcall passes HVAs directly and does not flatten them, but regcall
      // does.
      if (IsVectorCall)
        return getDirectX86Hva();

      if (Ty->isBuiltinType() || Ty->isVectorType())
        return ABIArgInfo::getDirect();
      return ABIArgInfo::getExpand();
    }
    if (IsVectorCall && Ty->isBuiltinType())
      return ABIArgInfo::getDirect();
    return getIndirectResult(Ty, /*ByVal=*/false, State);
  }

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    // FIXME: This should not be byval!
    if (RT && RT->getDecl()->hasFlexibleArrayMember())
      return getIndirectResult(Ty, true, State);

    // Ignore empty structs/unions on non-Windows.
    if (!IsWin32StructABI && isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    llvm::LLVMContext &LLVMContext = getVMContext();
    llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(LLVMContext);
    bool NeedsPadding = false;
    bool InReg;
    if (shouldAggregateUseDirect(Ty, State, InReg, NeedsPadding)) {
      unsigned SizeInRegs = (TI.Width + 31) / 32;
      SmallVector<llvm::Type*, 3> Elements(SizeInRegs, Int32);
      llvm::Type *Result = llvm::StructType::get(LLVMContext, Elements);
      if (InReg)
        return ABIArgInfo::getDirectInReg(Result);
      else
        return ABIArgInfo::getDirect(Result);
    }
    llvm::IntegerType *PaddingType = NeedsPadding ? Int32 : nullptr;

    // Pass over-aligned aggregates to non-variadic functions on Windows
    // indirectly. This behavior was added in MSVC 2015. Use the required
    // alignment from the record layout, since that may be less than the
    // regular type alignment, and types with required alignment of less than 4
    // bytes are not passed indirectly.
    if (IsWin32StructABI && State.Required.isRequiredArg(ArgIndex)) {
      unsigned AlignInBits = 0;
      if (RT) {
        const ASTRecordLayout &Layout =
          getContext().getASTRecordLayout(RT->getDecl());
        AlignInBits = getContext().toBits(Layout.getRequiredAlignment());
      } else if (TI.isAlignRequired()) {
        AlignInBits = TI.Align;
      }
      if (AlignInBits > 32)
        return getIndirectResult(Ty, /*ByVal=*/false, State);
    }

    // Expand small (<= 128-bit) record types when we know that the stack layout
    // of those arguments will match the struct. This is important because the
    // LLVM backend isn't smart enough to remove byval, which inhibits many
    // optimizations.
    // Don't do this for the MCU if there are still free integer registers
    // (see X86_64 ABI for full explanation).
    if (TI.Width <= 4 * 32 && (!IsMCUABI || State.FreeRegs == 0) &&
        canExpandIndirectArgument(Ty))
      return ABIArgInfo::getExpandWithPadding(
          IsFastCall || IsVectorCall || IsRegCall, PaddingType);

    return getIndirectResult(Ty, true, State);
  }

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    // On Windows, vectors are passed directly if registers are available, or
    // indirectly if not. This avoids the need to align argument memory. Pass
    // user-defined vector types larger than 512 bits indirectly for simplicity.
    if (IsWin32StructABI) {
      if (TI.Width <= 512 && State.FreeSSERegs > 0) {
        --State.FreeSSERegs;
        return ABIArgInfo::getDirectInReg();
      }
      return getIndirectResult(Ty, /*ByVal=*/false, State);
    }

    // On Darwin, some vectors are passed in memory, we handle this by passing
    // it as an i8/i16/i32/i64.
    if (IsDarwinVectorABI) {
      if ((TI.Width == 8 || TI.Width == 16 || TI.Width == 32) ||
          (TI.Width == 64 && VT->getNumElements() == 1))
        return ABIArgInfo::getDirect(
            llvm::IntegerType::get(getVMContext(), TI.Width));
    }

    if (IsX86_MMXType(CGT.ConvertType(Ty)))
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), 64));

    return ABIArgInfo::getDirect();
  }


  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  bool InReg = shouldPrimitiveUseInReg(Ty, State);

  if (isPromotableIntegerTypeForABI(Ty)) {
    if (InReg)
      return ABIArgInfo::getExtendInReg(Ty);
    return ABIArgInfo::getExtend(Ty);
  }

  if (const auto *EIT = Ty->getAs<BitIntType>()) {
    if (EIT->getNumBits() <= 64) {
      if (InReg)
        return ABIArgInfo::getDirectInReg();
      return ABIArgInfo::getDirect();
    }
    return getIndirectResult(Ty, /*ByVal=*/false, State);
  }

  if (InReg)
    return ABIArgInfo::getDirectInReg();
  return ABIArgInfo::getDirect();
}

void X86_32ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  CCState State(FI);
  if (IsMCUABI)
    State.FreeRegs = 3;
  else if (State.CC == llvm::CallingConv::X86_FastCall) {
    State.FreeRegs = 2;
    State.FreeSSERegs = 3;
  } else if (State.CC == llvm::CallingConv::X86_VectorCall) {
    State.FreeRegs = 2;
    State.FreeSSERegs = 6;
  } else if (FI.getHasRegParm())
    State.FreeRegs = FI.getRegParm();
  else if (State.CC == llvm::CallingConv::X86_RegCall) {
    State.FreeRegs = 5;
    State.FreeSSERegs = 8;
  } else if (IsWin32StructABI) {
    // Since MSVC 2015, the first three SSE vectors have been passed in
    // registers. The rest are passed indirectly.
    State.FreeRegs = DefaultNumRegisterParameters;
    State.FreeSSERegs = 3;
  } else
    State.FreeRegs = DefaultNumRegisterParameters;

  if (!::classifyReturnType(getCXXABI(), FI, *this)) {
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType(), State);
  } else if (FI.getReturnInfo().isIndirect()) {
    // The C++ ABI is not aware of register usage, so we have to check if the
    // return value was sret and put it in a register ourselves if appropriate.
    if (State.FreeRegs) {
      --State.FreeRegs;  // The sret parameter consumes a register.
      if (!IsMCUABI)
        FI.getReturnInfo().setInReg(true);
    }
  }

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++State.FreeRegs;

  // For vectorcall, do a first pass over the arguments, assigning FP and vector
  // arguments to XMM registers as available.
  if (State.CC == llvm::CallingConv::X86_VectorCall)
    runVectorCallFirstPass(FI, State);

  bool UsedInAlloca = false;
  MutableArrayRef<CGFunctionInfoArgInfo> Args = FI.arguments();
  for (unsigned I = 0, E = Args.size(); I < E; ++I) {
    // Skip arguments that have already been assigned.
    if (State.IsPreassigned.test(I))
      continue;

    Args[I].info =
        classifyArgumentType(Args[I].type, State, I);
    UsedInAlloca |= (Args[I].info.getKind() == ABIArgInfo::InAlloca);
  }

  // If we needed to use inalloca for any argument, do a second pass and rewrite
  // all the memory arguments to use inalloca.
  if (UsedInAlloca)
    rewriteWithInAlloca(FI);
}

void
X86_32ABIInfo::addFieldToArgStruct(SmallVector<llvm::Type *, 6> &FrameFields,
                                   CharUnits &StackOffset, ABIArgInfo &Info,
                                   QualType Type) const {
  // Arguments are always 4-byte-aligned.
  CharUnits WordSize = CharUnits::fromQuantity(4);
  assert(StackOffset.isMultipleOf(WordSize) && "unaligned inalloca struct");

  // sret pointers and indirect things will require an extra pointer
  // indirection, unless they are byval. Most things are byval, and will not
  // require this indirection.
  bool IsIndirect = false;
  if (Info.isIndirect() && !Info.getIndirectByVal())
    IsIndirect = true;
  Info = ABIArgInfo::getInAlloca(FrameFields.size(), IsIndirect);
  llvm::Type *LLTy = CGT.ConvertTypeForMem(Type);
  if (IsIndirect)
    LLTy = llvm::PointerType::getUnqual(getVMContext());
  FrameFields.push_back(LLTy);
  StackOffset += IsIndirect ? WordSize : getContext().getTypeSizeInChars(Type);

  // Insert padding bytes to respect alignment.
  CharUnits FieldEnd = StackOffset;
  StackOffset = FieldEnd.alignTo(WordSize);
  if (StackOffset != FieldEnd) {
    CharUnits NumBytes = StackOffset - FieldEnd;
    llvm::Type *Ty = llvm::Type::getInt8Ty(getVMContext());
    Ty = llvm::ArrayType::get(Ty, NumBytes.getQuantity());
    FrameFields.push_back(Ty);
  }
}

static bool isArgInAlloca(const ABIArgInfo &Info) {
  // Leave ignored and inreg arguments alone.
  switch (Info.getKind()) {
  case ABIArgInfo::InAlloca:
    return true;
  case ABIArgInfo::Ignore:
  case ABIArgInfo::IndirectAliased:
    return false;
  case ABIArgInfo::Indirect:
  case ABIArgInfo::Direct:
  case ABIArgInfo::Extend:
    return !Info.getInReg();
  case ABIArgInfo::Expand:
  case ABIArgInfo::CoerceAndExpand:
    // These are aggregate types which are never passed in registers when
    // inalloca is involved.
    return true;
  }
  llvm_unreachable("invalid enum");
}

void X86_32ABIInfo::rewriteWithInAlloca(CGFunctionInfo &FI) const {
  assert(IsWin32StructABI && "inalloca only supported on win32");

  // Build a packed struct type for all of the arguments in memory.
  SmallVector<llvm::Type *, 6> FrameFields;

  // The stack alignment is always 4.
  CharUnits StackAlign = CharUnits::fromQuantity(4);

  CharUnits StackOffset;
  CGFunctionInfo::arg_iterator I = FI.arg_begin(), E = FI.arg_end();

  // Put 'this' into the struct before 'sret', if necessary.
  bool IsThisCall =
      FI.getCallingConvention() == llvm::CallingConv::X86_ThisCall;
  ABIArgInfo &Ret = FI.getReturnInfo();
  if (Ret.isIndirect() && Ret.isSRetAfterThis() && !IsThisCall &&
      isArgInAlloca(I->info)) {
    addFieldToArgStruct(FrameFields, StackOffset, I->info, I->type);
    ++I;
  }

  // Put the sret parameter into the inalloca struct if it's in memory.
  if (Ret.isIndirect() && !Ret.getInReg()) {
    addFieldToArgStruct(FrameFields, StackOffset, Ret, FI.getReturnType());
    // On Windows, the hidden sret parameter is always returned in eax.
    Ret.setInAllocaSRet(IsWin32StructABI);
  }

  // Skip the 'this' parameter in ecx.
  if (IsThisCall)
    ++I;

  // Put arguments passed in memory into the struct.
  for (; I != E; ++I) {
    if (isArgInAlloca(I->info))
      addFieldToArgStruct(FrameFields, StackOffset, I->info, I->type);
  }

  FI.setArgStruct(llvm::StructType::get(getVMContext(), FrameFields,
                                        /*isPacked=*/true),
                  StackAlign);
}

RValue X86_32ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, AggValueSlot Slot) const {

  auto TypeInfo = getContext().getTypeInfoInChars(Ty);

  CCState State(*const_cast<CGFunctionInfo *>(CGF.CurFnInfo));
  ABIArgInfo AI = classifyArgumentType(Ty, State, /*ArgIndex*/ 0);
  // Empty records are ignored for parameter passing purposes.
  if (AI.isIgnore())
    return Slot.asRValue();

  // x86-32 changes the alignment of certain arguments on the stack.
  //
  // Just messing with TypeInfo like this works because we never pass
  // anything indirectly.
  TypeInfo.Align = CharUnits::fromQuantity(
                getTypeStackAlignInBytes(Ty, TypeInfo.Align.getQuantity()));

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, /*Indirect*/ false, TypeInfo,
                          CharUnits::fromQuantity(4),
                          /*AllowHigherAlign*/ true, Slot);
}

bool X86_32TargetCodeGenInfo::isStructReturnInRegABI(
    const llvm::Triple &Triple, const CodeGenOptions &Opts) {
  assert(Triple.getArch() == llvm::Triple::x86);

  switch (Opts.getStructReturnConvention()) {
  case CodeGenOptions::SRCK_Default:
    break;
  case CodeGenOptions::SRCK_OnStack:  // -fpcc-struct-return
    return false;
  case CodeGenOptions::SRCK_InRegs:  // -freg-struct-return
    return true;
  }

  if (Triple.isOSDarwin() || Triple.isOSIAMCU())
    return true;

  switch (Triple.getOS()) {
  case llvm::Triple::DragonFly:
  case llvm::Triple::FreeBSD:
  case llvm::Triple::OpenBSD:
  case llvm::Triple::Win32:
    return true;
  default:
    return false;
  }
}

static void addX86InterruptAttrs(const FunctionDecl *FD, llvm::GlobalValue *GV,
                                 CodeGen::CodeGenModule &CGM) {
  if (!FD->hasAttr<AnyX86InterruptAttr>())
    return;

  llvm::Function *Fn = cast<llvm::Function>(GV);
  Fn->setCallingConv(llvm::CallingConv::X86_INTR);
  if (FD->getNumParams() == 0)
    return;

  auto PtrTy = cast<PointerType>(FD->getParamDecl(0)->getType());
  llvm::Type *ByValTy = CGM.getTypes().ConvertType(PtrTy->getPointeeType());
  llvm::Attribute NewAttr = llvm::Attribute::getWithByValType(
    Fn->getContext(), ByValTy);
  Fn->addParamAttr(0, NewAttr);
}

void X86_32TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->addFnAttr("stackrealign");
    }

    addX86InterruptAttrs(FD, GV, CGM);
  }
}

bool X86_32TargetCodeGenInfo::initDwarfEHRegSizeTable(
                                               CodeGen::CodeGenFunction &CGF,
                                               llvm::Value *Address) const {
  CodeGen::CGBuilderTy &Builder = CGF.Builder;

  llvm::Value *Four8 = llvm::ConstantInt::get(CGF.Int8Ty, 4);

  // 0-7 are the eight integer registers;  the order is different
  //   on Darwin (for EH), but the range is the same.
  // 8 is %eip.
  AssignToArrayRange(Builder, Address, Four8, 0, 8);

  if (CGF.CGM.getTarget().getTriple().isOSDarwin()) {
    // 12-16 are st(0..4).  Not sure why we stop at 4.
    // These have size 16, which is sizeof(long double) on
    // platforms with 8-byte alignment for that type.
    llvm::Value *Sixteen8 = llvm::ConstantInt::get(CGF.Int8Ty, 16);
    AssignToArrayRange(Builder, Address, Sixteen8, 12, 16);

  } else {
    // 9 is %eflags, which doesn't get a size on Darwin for some
    // reason.
    Builder.CreateAlignedStore(
        Four8, Builder.CreateConstInBoundsGEP1_32(CGF.Int8Ty, Address, 9),
                               CharUnits::One());

    // 11-16 are st(0..5).  Not sure why we stop at 5.
    // These have size 12, which is sizeof(long double) on
    // platforms with 4-byte alignment for that type.
    llvm::Value *Twelve8 = llvm::ConstantInt::get(CGF.Int8Ty, 12);
    AssignToArrayRange(Builder, Address, Twelve8, 11, 16);
  }

  return false;
}

//===----------------------------------------------------------------------===//
// X86-64 ABI Implementation
//===----------------------------------------------------------------------===//


namespace {

/// \p returns the size in bits of the largest (native) vector for \p AVXLevel.
static unsigned getNativeVectorSizeForAVXABI(X86AVXABILevel AVXLevel) {
  switch (AVXLevel) {
  case X86AVXABILevel::AVX512:
    return 512;
  case X86AVXABILevel::AVX:
    return 256;
  case X86AVXABILevel::None:
    return 128;
  }
  llvm_unreachable("Unknown AVXLevel");
}

/// X86_64ABIInfo - The X86_64 ABI information.
class X86_64ABIInfo : public ABIInfo {
  enum Class {
    Integer = 0,
    SSE,
    SSEUp,
    X87,
    X87Up,
    ComplexX87,
    NoClass,
    Memory
  };

  /// merge - Implement the X86_64 ABI merging algorithm.
  ///
  /// Merge an accumulating classification \arg Accum with a field
  /// classification \arg Field.
  ///
  /// \param Accum - The accumulating classification. This should
  /// always be either NoClass or the result of a previous merge
  /// call. In addition, this should never be Memory (the caller
  /// should just return Memory for the aggregate).
  static Class merge(Class Accum, Class Field);

  /// postMerge - Implement the X86_64 ABI post merging algorithm.
  ///
  /// Post merger cleanup, reduces a malformed Hi and Lo pair to
  /// final MEMORY or SSE classes when necessary.
  ///
  /// \param AggregateSize - The size of the current aggregate in
  /// the classification process.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the higher words of the containing object.
  ///
  void postMerge(unsigned AggregateSize, Class &Lo, Class &Hi) const;

  /// classify - Determine the x86_64 register classes in which the
  /// given type T should be passed.
  ///
  /// \param Lo - The classification for the parts of the type
  /// residing in the low word of the containing object.
  ///
  /// \param Hi - The classification for the parts of the type
  /// residing in the high word of the containing object.
  ///
  /// \param OffsetBase - The bit offset of this type in the
  /// containing object.  Some parameters are classified different
  /// depending on whether they straddle an eightbyte boundary.
  ///
  /// \param isNamedArg - Whether the argument in question is a "named"
  /// argument, as used in AMD64-ABI 3.5.7.
  ///
  /// \param IsRegCall - Whether the calling conversion is regcall.
  ///
  /// If a word is unused its result will be NoClass; if a type should
  /// be passed in Memory then at least the classification of \arg Lo
  /// will be Memory.
  ///
  /// The \arg Lo class will be NoClass iff the argument is ignored.
  ///
  /// If the \arg Lo class is ComplexX87, then the \arg Hi class will
  /// also be ComplexX87.
  void classify(QualType T, uint64_t OffsetBase, Class &Lo, Class &Hi,
                bool isNamedArg, bool IsRegCall = false) const;

  llvm::Type *GetByteVectorType(QualType Ty) const;
  llvm::Type *GetSSETypeAtOffset(llvm::Type *IRType,
                                 unsigned IROffset, QualType SourceTy,
                                 unsigned SourceOffset) const;
  llvm::Type *GetINTEGERTypeAtOffset(llvm::Type *IRType,
                                     unsigned IROffset, QualType SourceTy,
                                     unsigned SourceOffset) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be returned in memory.
  ABIArgInfo getIndirectReturnResult(QualType Ty) const;

  /// getIndirectResult - Give a source type \arg Ty, return a suitable result
  /// such that the argument will be passed in memory.
  ///
  /// \param freeIntRegs - The number of free integer registers remaining
  /// available.
  ABIArgInfo getIndirectResult(QualType Ty, unsigned freeIntRegs) const;

  ABIArgInfo classifyReturnType(QualType RetTy) const;

  ABIArgInfo classifyArgumentType(QualType Ty, unsigned freeIntRegs,
                                  unsigned &neededInt, unsigned &neededSSE,
                                  bool isNamedArg,
                                  bool IsRegCall = false) const;

  ABIArgInfo classifyRegCallStructType(QualType Ty, unsigned &NeededInt,
                                       unsigned &NeededSSE,
                                       unsigned &MaxVectorWidth) const;

  ABIArgInfo classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                           unsigned &NeededSSE,
                                           unsigned &MaxVectorWidth) const;

  bool IsIllegalVectorType(QualType Ty) const;

  /// The 0.98 ABI revision clarified a lot of ambiguities,
  /// unfortunately in ways that were not always consistent with
  /// certain previous compilers.  In particular, platforms which
  /// required strict binary compatibility with older versions of GCC
  /// may need to exempt themselves.
  bool honorsRevision0_98() const {
    return !getTarget().getTriple().isOSDarwin();
  }

  /// GCC classifies <1 x long long> as SSE but some platform ABIs choose to
  /// classify it as INTEGER (for compatibility with older clang compilers).
  bool classifyIntegerMMXAsSSE() const {
    // Clang <= 3.8 did not do this.
    if (getContext().getLangOpts().getClangABICompat() <=
        LangOptions::ClangABI::Ver3_8)
      return false;

    const llvm::Triple &Triple = getTarget().getTriple();
    if (Triple.isOSDarwin() || Triple.isPS() || Triple.isOSFreeBSD())
      return false;
    return true;
  }

  // GCC classifies vectors of __int128 as memory.
  bool passInt128VectorsInMem() const {
    // Clang <= 9.0 did not do this.
    if (getContext().getLangOpts().getClangABICompat() <=
        LangOptions::ClangABI::Ver9)
      return false;

    const llvm::Triple &T = getTarget().getTriple();
    return T.isOSLinux() || T.isOSNetBSD();
  }

  X86AVXABILevel AVXLevel;
  // Some ABIs (e.g. X32 ABI and Native Client OS) use 32 bit pointers on
  // 64-bit hardware.
  bool Has64BitPointers;

public:
  X86_64ABIInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel)
      : ABIInfo(CGT), AVXLevel(AVXLevel),
        Has64BitPointers(CGT.getDataLayout().getPointerSize(0) == 8) {}

  bool isPassedUsingAVXType(QualType type) const {
    unsigned neededInt, neededSSE;
    // The freeIntRegs argument doesn't matter here.
    ABIArgInfo info = classifyArgumentType(type, 0, neededInt, neededSSE,
                                           /*isNamedArg*/true);
    if (info.isDirect()) {
      llvm::Type *ty = info.getCoerceToType();
      if (llvm::VectorType *vectorTy = dyn_cast_or_null<llvm::VectorType>(ty))
        return vectorTy->getPrimitiveSizeInBits().getFixedValue() > 128;
    }
    return false;
  }

  void computeInfo(CGFunctionInfo &FI) const override;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
  RValue EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                     AggValueSlot Slot) const override;

  bool has64BitPointers() const {
    return Has64BitPointers;
  }
};

/// WinX86_64ABIInfo - The Windows X86_64 ABI information.
class WinX86_64ABIInfo : public ABIInfo {
public:
  WinX86_64ABIInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel)
      : ABIInfo(CGT), AVXLevel(AVXLevel),
        IsMingw64(getTarget().getTriple().isWindowsGNUEnvironment()) {}

  void computeInfo(CGFunctionInfo &FI) const override;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;

  bool isHomogeneousAggregateBaseType(QualType Ty) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorTypeForVectorCall(getContext(), Ty);
  }

  bool isHomogeneousAggregateSmallEnough(const Type *Ty,
                                         uint64_t NumMembers) const override {
    // FIXME: Assumes vectorcall is in use.
    return isX86VectorCallAggregateSmallEnough(NumMembers);
  }

private:
  ABIArgInfo classify(QualType Ty, unsigned &FreeSSERegs, bool IsReturnType,
                      bool IsVectorCall, bool IsRegCall) const;
  ABIArgInfo reclassifyHvaArgForVectorCall(QualType Ty, unsigned &FreeSSERegs,
                                           const ABIArgInfo &current) const;

  X86AVXABILevel AVXLevel;

  bool IsMingw64;
};

class X86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  X86_64TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(std::make_unique<X86_64ABIInfo>(CGT, AVXLevel)) {
    SwiftInfo =
        std::make_unique<SwiftABIInfo>(CGT, /*SwiftErrorInRegister=*/true);
  }

  /// Disable tail call on x86-64. The epilogue code before the tail jump blocks
  /// autoreleaseRV/retainRV and autoreleaseRV/unsafeClaimRV optimizations.
  bool markARCOptimizedReturnCallsAsNoTail() const override { return true; }

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(CGF.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    AssignToArrayRange(CGF.Builder, Address, Eight8, 0, 16);
    return false;
  }

  llvm::Type* adjustInlineAsmType(CodeGen::CodeGenFunction &CGF,
                                  StringRef Constraint,
                                  llvm::Type* Ty) const override {
    return X86AdjustInlineAsmType(CGF, Constraint, Ty);
  }

  bool isNoProtoCallVariadic(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    // The default CC on x86-64 sets %al to the number of SSA
    // registers used, and GCC sets this when calling an unprototyped
    // function, so we override the default behavior.  However, don't do
    // that when AVX types are involved: the ABI explicitly states it is
    // undefined, and it doesn't work in practice because of how the ABI
    // defines varargs anyway.
    if (fnType->getCallConv() == CC_C) {
      bool HasAVXType = false;
      for (CallArgList::const_iterator
             it = args.begin(), ie = args.end(); it != ie; ++it) {
        if (getABIInfo<X86_64ABIInfo>().isPassedUsingAVXType(it->Ty)) {
          HasAVXType = true;
          break;
        }
      }

      if (!HasAVXType)
        return true;
    }

    return TargetCodeGenInfo::isNoProtoCallVariadic(args, fnType);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
      if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
        llvm::Function *Fn = cast<llvm::Function>(GV);
        Fn->addFnAttr("stackrealign");
      }

      addX86InterruptAttrs(FD, GV, CGM);
    }
  }

  void checkFunctionCallABI(CodeGenModule &CGM, SourceLocation CallLoc,
                            const FunctionDecl *Caller,
                            const FunctionDecl *Callee, const CallArgList &Args,
                            QualType ReturnType) const override;
};
} // namespace

static void initFeatureMaps(const ASTContext &Ctx,
                            llvm::StringMap<bool> &CallerMap,
                            const FunctionDecl *Caller,
                            llvm::StringMap<bool> &CalleeMap,
                            const FunctionDecl *Callee) {
  if (CalleeMap.empty() && CallerMap.empty()) {
    // The caller is potentially nullptr in the case where the call isn't in a
    // function.  In this case, the getFunctionFeatureMap ensures we just get
    // the TU level setting (since it cannot be modified by 'target'..
    Ctx.getFunctionFeatureMap(CallerMap, Caller);
    Ctx.getFunctionFeatureMap(CalleeMap, Callee);
  }
}

static bool checkAVXParamFeature(DiagnosticsEngine &Diag,
                                 SourceLocation CallLoc,
                                 const llvm::StringMap<bool> &CallerMap,
                                 const llvm::StringMap<bool> &CalleeMap,
                                 QualType Ty, StringRef Feature,
                                 bool IsArgument) {
  bool CallerHasFeat = CallerMap.lookup(Feature);
  bool CalleeHasFeat = CalleeMap.lookup(Feature);
  if (!CallerHasFeat && !CalleeHasFeat)
    return Diag.Report(CallLoc, diag::warn_avx_calling_convention)
           << IsArgument << Ty << Feature;

  // Mixing calling conventions here is very clearly an error.
  if (!CallerHasFeat || !CalleeHasFeat)
    return Diag.Report(CallLoc, diag::err_avx_calling_convention)
           << IsArgument << Ty << Feature;

  // Else, both caller and callee have the required feature, so there is no need
  // to diagnose.
  return false;
}

static bool checkAVX512ParamFeature(DiagnosticsEngine &Diag,
                                    SourceLocation CallLoc,
                                    const llvm::StringMap<bool> &CallerMap,
                                    const llvm::StringMap<bool> &CalleeMap,
                                    QualType Ty, bool IsArgument) {
  bool Caller256 = CallerMap.lookup("avx512f") && !CallerMap.lookup("evex512");
  bool Callee256 = CalleeMap.lookup("avx512f") && !CalleeMap.lookup("evex512");

  // Forbid 512-bit or larger vector pass or return when we disabled ZMM
  // instructions.
  if (Caller256 || Callee256)
    return Diag.Report(CallLoc, diag::err_avx_calling_convention)
           << IsArgument << Ty << "evex512";

  return checkAVXParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty,
                              "avx512f", IsArgument);
}

static bool checkAVXParam(DiagnosticsEngine &Diag, ASTContext &Ctx,
                          SourceLocation CallLoc,
                          const llvm::StringMap<bool> &CallerMap,
                          const llvm::StringMap<bool> &CalleeMap, QualType Ty,
                          bool IsArgument) {
  uint64_t Size = Ctx.getTypeSize(Ty);
  if (Size > 256)
    return checkAVX512ParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty,
                                   IsArgument);

  if (Size > 128)
    return checkAVXParamFeature(Diag, CallLoc, CallerMap, CalleeMap, Ty, "avx",
                                IsArgument);

  return false;
}

void X86_64TargetCodeGenInfo::checkFunctionCallABI(CodeGenModule &CGM,
                                                   SourceLocation CallLoc,
                                                   const FunctionDecl *Caller,
                                                   const FunctionDecl *Callee,
                                                   const CallArgList &Args,
                                                   QualType ReturnType) const {
  if (!Callee)
    return;

  llvm::StringMap<bool> CallerMap;
  llvm::StringMap<bool> CalleeMap;
  unsigned ArgIndex = 0;

  // We need to loop through the actual call arguments rather than the
  // function's parameters, in case this variadic.
  for (const CallArg &Arg : Args) {
    // The "avx" feature changes how vectors >128 in size are passed. "avx512f"
    // additionally changes how vectors >256 in size are passed. Like GCC, we
    // warn when a function is called with an argument where this will change.
    // Unlike GCC, we also error when it is an obvious ABI mismatch, that is,
    // the caller and callee features are mismatched.
    // Unfortunately, we cannot do this diagnostic in SEMA, since the callee can
    // change its ABI with attribute-target after this call.
    if (Arg.getType()->isVectorType() &&
        CGM.getContext().getTypeSize(Arg.getType()) > 128) {
      initFeatureMaps(CGM.getContext(), CallerMap, Caller, CalleeMap, Callee);
      QualType Ty = Arg.getType();
      // The CallArg seems to have desugared the type already, so for clearer
      // diagnostics, replace it with the type in the FunctionDecl if possible.
      if (ArgIndex < Callee->getNumParams())
        Ty = Callee->getParamDecl(ArgIndex)->getType();

      if (checkAVXParam(CGM.getDiags(), CGM.getContext(), CallLoc, CallerMap,
                        CalleeMap, Ty, /*IsArgument*/ true))
        return;
    }
    ++ArgIndex;
  }

  // Check return always, as we don't have a good way of knowing in codegen
  // whether this value is used, tail-called, etc.
  if (Callee->getReturnType()->isVectorType() &&
      CGM.getContext().getTypeSize(Callee->getReturnType()) > 128) {
    initFeatureMaps(CGM.getContext(), CallerMap, Caller, CalleeMap, Callee);
    checkAVXParam(CGM.getDiags(), CGM.getContext(), CallLoc, CallerMap,
                  CalleeMap, Callee->getReturnType(),
                  /*IsArgument*/ false);
  }
}

std::string TargetCodeGenInfo::qualifyWindowsLibrary(StringRef Lib) {
  // If the argument does not end in .lib, automatically add the suffix.
  // If the argument contains a space, enclose it in quotes.
  // This matches the behavior of MSVC.
  bool Quote = Lib.contains(' ');
  std::string ArgStr = Quote ? "\"" : "";
  ArgStr += Lib;
  if (!Lib.ends_with_insensitive(".lib") && !Lib.ends_with_insensitive(".a"))
    ArgStr += ".lib";
  ArgStr += Quote ? "\"" : "";
  return ArgStr;
}

namespace {
class WinX86_32TargetCodeGenInfo : public X86_32TargetCodeGenInfo {
public:
  WinX86_32TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT,
        bool DarwinVectorABI, bool RetSmallStructInRegABI, bool Win32StructABI,
        unsigned NumRegisterParameters)
    : X86_32TargetCodeGenInfo(CGT, DarwinVectorABI, RetSmallStructInRegABI,
        Win32StructABI, NumRegisterParameters, false) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:";
    Opt += qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name,
                               llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};
} // namespace

void WinX86_32TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  X86_32TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  addStackProbeTargetAttributes(D, GV, CGM);
}

namespace {
class WinX86_64TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  WinX86_64TargetCodeGenInfo(CodeGen::CodeGenTypes &CGT,
                             X86AVXABILevel AVXLevel)
      : TargetCodeGenInfo(std::make_unique<WinX86_64ABIInfo>(CGT, AVXLevel)) {
    SwiftInfo =
        std::make_unique<SwiftABIInfo>(CGT, /*SwiftErrorInRegister=*/true);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override;

  int getDwarfEHStackPointer(CodeGen::CodeGenModule &CGM) const override {
    return 7;
  }

  bool initDwarfEHRegSizeTable(CodeGen::CodeGenFunction &CGF,
                               llvm::Value *Address) const override {
    llvm::Value *Eight8 = llvm::ConstantInt::get(CGF.Int8Ty, 8);

    // 0-15 are the 16 integer registers.
    // 16 is %rip.
    AssignToArrayRange(CGF.Builder, Address, Eight8, 0, 16);
    return false;
  }

  void getDependentLibraryOption(llvm::StringRef Lib,
                                 llvm::SmallString<24> &Opt) const override {
    Opt = "/DEFAULTLIB:";
    Opt += qualifyWindowsLibrary(Lib);
  }

  void getDetectMismatchOption(llvm::StringRef Name,
                               llvm::StringRef Value,
                               llvm::SmallString<32> &Opt) const override {
    Opt = "/FAILIFMISMATCH:\"" + Name.str() + "=" + Value.str() + "\"";
  }
};
} // namespace

void WinX86_64TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &CGM) const {
  TargetCodeGenInfo::setTargetAttributes(D, GV, CGM);
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (FD->hasAttr<X86ForceAlignArgPointerAttr>()) {
      llvm::Function *Fn = cast<llvm::Function>(GV);
      Fn->addFnAttr("stackrealign");
    }

    addX86InterruptAttrs(FD, GV, CGM);
  }

  addStackProbeTargetAttributes(D, GV, CGM);
}

void X86_64ABIInfo::postMerge(unsigned AggregateSize, Class &Lo,
                              Class &Hi) const {
  // AMD64-ABI 3.2.3p2: Rule 5. Then a post merger cleanup is done:
  //
  // (a) If one of the classes is Memory, the whole argument is passed in
  //     memory.
  //
  // (b) If X87UP is not preceded by X87, the whole argument is passed in
  //     memory.
  //
  // (c) If the size of the aggregate exceeds two eightbytes and the first
  //     eightbyte isn't SSE or any other eightbyte isn't SSEUP, the whole
  //     argument is passed in memory. NOTE: This is necessary to keep the
  //     ABI working for processors that don't support the __m256 type.
  //
  // (d) If SSEUP is not preceded by SSE or SSEUP, it is converted to SSE.
  //
  // Some of these are enforced by the merging logic.  Others can arise
  // only with unions; for example:
  //   union { _Complex double; unsigned; }
  //
  // Note that clauses (b) and (c) were added in 0.98.
  //
  if (Hi == Memory)
    Lo = Memory;
  if (Hi == X87Up && Lo != X87 && honorsRevision0_98())
    Lo = Memory;
  if (AggregateSize > 128 && (Lo != SSE || Hi != SSEUp))
    Lo = Memory;
  if (Hi == SSEUp && Lo != SSE)
    Hi = SSE;
}

X86_64ABIInfo::Class X86_64ABIInfo::merge(Class Accum, Class Field) {
  // AMD64-ABI 3.2.3p2: Rule 4. Each field of an object is
  // classified recursively so that always two fields are
  // considered. The resulting class is calculated according to
  // the classes of the fields in the eightbyte:
  //
  // (a) If both classes are equal, this is the resulting class.
  //
  // (b) If one of the classes is NO_CLASS, the resulting class is
  // the other class.
  //
  // (c) If one of the classes is MEMORY, the result is the MEMORY
  // class.
  //
  // (d) If one of the classes is INTEGER, the result is the
  // INTEGER.
  //
  // (e) If one of the classes is X87, X87UP, COMPLEX_X87 class,
  // MEMORY is used as class.
  //
  // (f) Otherwise class SSE is used.

  // Accum should never be memory (we should have returned) or
  // ComplexX87 (because this cannot be passed in a structure).
  assert((Accum != Memory && Accum != ComplexX87) &&
         "Invalid accumulated classification during merge.");
  if (Accum == Field || Field == NoClass)
    return Accum;
  if (Field == Memory)
    return Memory;
  if (Accum == NoClass)
    return Field;
  if (Accum == Integer || Field == Integer)
    return Integer;
  if (Field == X87 || Field == X87Up || Field == ComplexX87 ||
      Accum == X87 || Accum == X87Up)
    return Memory;
  return SSE;
}

void X86_64ABIInfo::classify(QualType Ty, uint64_t OffsetBase, Class &Lo,
                             Class &Hi, bool isNamedArg, bool IsRegCall) const {
  // FIXME: This code can be simplified by introducing a simple value class for
  // Class pairs with appropriate constructor methods for the various
  // situations.

  // FIXME: Some of the split computations are wrong; unaligned vectors
  // shouldn't be passed in registers for example, so there is no chance they
  // can straddle an eightbyte. Verify & simplify.

  Lo = Hi = NoClass;

  Class &Current = OffsetBase < 64 ? Lo : Hi;
  Current = Memory;

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    BuiltinType::Kind k = BT->getKind();

    if (k == BuiltinType::Void) {
      Current = NoClass;
    } else if (k == BuiltinType::Int128 || k == BuiltinType::UInt128) {
      Lo = Integer;
      Hi = Integer;
    } else if (k >= BuiltinType::Bool && k <= BuiltinType::LongLong) {
      Current = Integer;
    } else if (k == BuiltinType::Float || k == BuiltinType::Double ||
               k == BuiltinType::Float16 || k == BuiltinType::BFloat16) {
      Current = SSE;
    } else if (k == BuiltinType::Float128) {
      Lo = SSE;
      Hi = SSEUp;
    } else if (k == BuiltinType::LongDouble) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad()) {
        Lo = SSE;
        Hi = SSEUp;
      } else if (LDF == &llvm::APFloat::x87DoubleExtended()) {
        Lo = X87;
        Hi = X87Up;
      } else if (LDF == &llvm::APFloat::IEEEdouble()) {
        Current = SSE;
      } else
        llvm_unreachable("unexpected long double representation!");
    }
    // FIXME: _Decimal32 and _Decimal64 are SSE.
    // FIXME: _float128 and _Decimal128 are (SSE, SSEUp).
    return;
  }

  if (const EnumType *ET = Ty->getAs<EnumType>()) {
    // Classify the underlying integer type.
    classify(ET->getDecl()->getIntegerType(), OffsetBase, Lo, Hi, isNamedArg);
    return;
  }

  if (Ty->hasPointerRepresentation()) {
    Current = Integer;
    return;
  }

  if (Ty->isMemberPointerType()) {
    if (Ty->isMemberFunctionPointerType()) {
      if (Has64BitPointers) {
        // If Has64BitPointers, this is an {i64, i64}, so classify both
        // Lo and Hi now.
        Lo = Hi = Integer;
      } else {
        // Otherwise, with 32-bit pointers, this is an {i32, i32}. If that
        // straddles an eightbyte boundary, Hi should be classified as well.
        uint64_t EB_FuncPtr = (OffsetBase) / 64;
        uint64_t EB_ThisAdj = (OffsetBase + 64 - 1) / 64;
        if (EB_FuncPtr != EB_ThisAdj) {
          Lo = Hi = Integer;
        } else {
          Current = Integer;
        }
      }
    } else {
      Current = Integer;
    }
    return;
  }

  if (const VectorType *VT = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VT);
    if (Size == 1 || Size == 8 || Size == 16 || Size == 32) {
      // gcc passes the following as integer:
      // 4 bytes - <4 x char>, <2 x short>, <1 x int>, <1 x float>
      // 2 bytes - <2 x char>, <1 x short>
      // 1 byte  - <1 x char>
      Current = Integer;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      uint64_t EB_Lo = (OffsetBase) / 64;
      uint64_t EB_Hi = (OffsetBase + Size - 1) / 64;
      if (EB_Lo != EB_Hi)
        Hi = Lo;
    } else if (Size == 64) {
      QualType ElementType = VT->getElementType();

      // gcc passes <1 x double> in memory. :(
      if (ElementType->isSpecificBuiltinType(BuiltinType::Double))
        return;

      // gcc passes <1 x long long> as SSE but clang used to unconditionally
      // pass them as integer.  For platforms where clang is the de facto
      // platform compiler, we must continue to use integer.
      if (!classifyIntegerMMXAsSSE() &&
          (ElementType->isSpecificBuiltinType(BuiltinType::LongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULongLong) ||
           ElementType->isSpecificBuiltinType(BuiltinType::Long) ||
           ElementType->isSpecificBuiltinType(BuiltinType::ULong)))
        Current = Integer;
      else
        Current = SSE;

      // If this type crosses an eightbyte boundary, it should be
      // split.
      if (OffsetBase && OffsetBase != 64)
        Hi = Lo;
    } else if (Size == 128 ||
               (isNamedArg && Size <= getNativeVectorSizeForAVXABI(AVXLevel))) {
      QualType ElementType = VT->getElementType();

      // gcc passes 256 and 512 bit <X x __int128> vectors in memory. :(
      if (passInt128VectorsInMem() && Size != 128 &&
          (ElementType->isSpecificBuiltinType(BuiltinType::Int128) ||
           ElementType->isSpecificBuiltinType(BuiltinType::UInt128)))
        return;

      // Arguments of 256-bits are split into four eightbyte chunks. The
      // least significant one belongs to class SSE and all the others to class
      // SSEUP. The original Lo and Hi design considers that types can't be
      // greater than 128-bits, so a 64-bit split in Hi and Lo makes sense.
      // This design isn't correct for 256-bits, but since there're no cases
      // where the upper parts would need to be inspected, avoid adding
      // complexity and just consider Hi to match the 64-256 part.
      //
      // Note that per 3.5.7 of AMD64-ABI, 256-bit args are only passed in
      // registers if they are "named", i.e. not part of the "..." of a
      // variadic function.
      //
      // Similarly, per 3.2.3. of the AVX512 draft, 512-bits ("named") args are
      // split into eight eightbyte chunks, one SSE and seven SSEUP.
      Lo = SSE;
      Hi = SSEUp;
    }
    return;
  }

  if (const ComplexType *CT = Ty->getAs<ComplexType>()) {
    QualType ET = getContext().getCanonicalType(CT->getElementType());

    uint64_t Size = getContext().getTypeSize(Ty);
    if (ET->isIntegralOrEnumerationType()) {
      if (Size <= 64)
        Current = Integer;
      else if (Size <= 128)
        Lo = Hi = Integer;
    } else if (ET->isFloat16Type() || ET == getContext().FloatTy ||
               ET->isBFloat16Type()) {
      Current = SSE;
    } else if (ET == getContext().DoubleTy) {
      Lo = Hi = SSE;
    } else if (ET == getContext().LongDoubleTy) {
      const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
      if (LDF == &llvm::APFloat::IEEEquad())
        Current = Memory;
      else if (LDF == &llvm::APFloat::x87DoubleExtended())
        Current = ComplexX87;
      else if (LDF == &llvm::APFloat::IEEEdouble())
        Lo = Hi = SSE;
      else
        llvm_unreachable("unexpected long double representation!");
    }

    // If this complex type crosses an eightbyte boundary then it
    // should be split.
    uint64_t EB_Real = (OffsetBase) / 64;
    uint64_t EB_Imag = (OffsetBase + getContext().getTypeSize(ET)) / 64;
    if (Hi == NoClass && EB_Real != EB_Imag)
      Hi = Lo;

    return;
  }

  if (const auto *EITy = Ty->getAs<BitIntType>()) {
    if (EITy->getNumBits() <= 64)
      Current = Integer;
    else if (EITy->getNumBits() <= 128)
      Lo = Hi = Integer;
    // Larger values need to get passed in memory.
    return;
  }

  if (const ConstantArrayType *AT = getContext().getAsConstantArrayType(Ty)) {
    // Arrays are treated like structures.

    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    // regcall ABI doesn't have limitation to an object. The only limitation
    // is the free registers, which will be checked in computeInfo.
    if (!IsRegCall && Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 1. If ..., or it contains unaligned
    // fields, it has class MEMORY.
    //
    // Only need to check alignment of array base.
    if (OffsetBase % getContext().getTypeAlign(AT->getElementType()))
      return;

    // Otherwise implement simplified merge. We could be smarter about
    // this, but it isn't worth it and would be harder to verify.
    Current = NoClass;
    uint64_t EltSize = getContext().getTypeSize(AT->getElementType());
    uint64_t ArraySize = AT->getZExtSize();

    // The only case a 256-bit wide vector could be used is when the array
    // contains a single 256-bit element. Since Lo and Hi logic isn't extended
    // to work for sizes wider than 128, early check and fallback to memory.
    //
    if (Size > 128 &&
        (Size != EltSize || Size > getNativeVectorSizeForAVXABI(AVXLevel)))
      return;

    for (uint64_t i=0, Offset=OffsetBase; i<ArraySize; ++i, Offset += EltSize) {
      Class FieldLo, FieldHi;
      classify(AT->getElementType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
    assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp array classification.");
    return;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger
    // than eight eightbytes, ..., it has class MEMORY.
    if (Size > 512)
      return;

    // AMD64-ABI 3.2.3p2: Rule 2. If a C++ object has either a non-trivial
    // copy constructor or a non-trivial destructor, it is passed by invisible
    // reference.
    if (getRecordArgABI(RT, getCXXABI()))
      return;

    const RecordDecl *RD = RT->getDecl();

    // Assume variable sized types are passed in memory.
    if (RD->hasFlexibleArrayMember())
      return;

    const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);

    // Reset Lo class, this will be recomputed.
    Current = NoClass;

    // If this is a C++ record, classify the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &I : CXXRD->bases()) {
        assert(!I.isVirtual() && !I.getType()->isDependentType() &&
               "Unexpected base class!");
        const auto *Base =
            cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

        // Classify this field.
        //
        // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate exceeds a
        // single eightbyte, each is classified separately. Each eightbyte gets
        // initialized to class NO_CLASS.
        Class FieldLo, FieldHi;
        uint64_t Offset =
          OffsetBase + getContext().toBits(Layout.getBaseClassOffset(Base));
        classify(I.getType(), Offset, FieldLo, FieldHi, isNamedArg);
        Lo = merge(Lo, FieldLo);
        Hi = merge(Hi, FieldHi);
        if (Lo == Memory || Hi == Memory) {
          postMerge(Size, Lo, Hi);
          return;
        }
      }
    }

    // Classify the fields one at a time, merging the results.
    unsigned idx = 0;
    bool UseClang11Compat = getContext().getLangOpts().getClangABICompat() <=
                                LangOptions::ClangABI::Ver11 ||
                            getContext().getTargetInfo().getTriple().isPS();
    bool IsUnion = RT->isUnionType() && !UseClang11Compat;

    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
           i != e; ++i, ++idx) {
      uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
      bool BitField = i->isBitField();

      // Ignore padding bit-fields.
      if (BitField && i->isUnnamedBitField())
        continue;

      // AMD64-ABI 3.2.3p2: Rule 1. If the size of an object is larger than
      // eight eightbytes, or it contains unaligned fields, it has class MEMORY.
      //
      // The only case a 256-bit or a 512-bit wide vector could be used is when
      // the struct contains a single 256-bit or 512-bit element. Early check
      // and fallback to memory.
      //
      // FIXME: Extended the Lo and Hi logic properly to work for size wider
      // than 128.
      if (Size > 128 &&
          ((!IsUnion && Size != getContext().getTypeSize(i->getType())) ||
           Size > getNativeVectorSizeForAVXABI(AVXLevel))) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }

      bool IsInMemory =
          Offset % getContext().getTypeAlign(i->getType().getCanonicalType());
      // Note, skip this test for bit-fields, see below.
      if (!BitField && IsInMemory) {
        Lo = Memory;
        postMerge(Size, Lo, Hi);
        return;
      }

      // Classify this field.
      //
      // AMD64-ABI 3.2.3p2: Rule 3. If the size of the aggregate
      // exceeds a single eightbyte, each is classified
      // separately. Each eightbyte gets initialized to class
      // NO_CLASS.
      Class FieldLo, FieldHi;

      // Bit-fields require special handling, they do not force the
      // structure to be passed in memory even if unaligned, and
      // therefore they can straddle an eightbyte.
      if (BitField) {
        assert(!i->isUnnamedBitField());
        uint64_t Offset = OffsetBase + Layout.getFieldOffset(idx);
        uint64_t Size = i->getBitWidthValue(getContext());

        uint64_t EB_Lo = Offset / 64;
        uint64_t EB_Hi = (Offset + Size - 1) / 64;

        if (EB_Lo) {
          assert(EB_Hi == EB_Lo && "Invalid classification, type > 16 bytes.");
          FieldLo = NoClass;
          FieldHi = Integer;
        } else {
          FieldLo = Integer;
          FieldHi = EB_Hi ? Integer : NoClass;
        }
      } else
        classify(i->getType(), Offset, FieldLo, FieldHi, isNamedArg);
      Lo = merge(Lo, FieldLo);
      Hi = merge(Hi, FieldHi);
      if (Lo == Memory || Hi == Memory)
        break;
    }

    postMerge(Size, Lo, Hi);
  }
}

ABIArgInfo X86_64ABIInfo::getIndirectReturnResult(QualType Ty) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    if (Ty->isBitIntType())
      return getNaturalAlignIndirect(Ty);

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  return getNaturalAlignIndirect(Ty);
}

bool X86_64ABIInfo::IsIllegalVectorType(QualType Ty) const {
  if (const VectorType *VecTy = Ty->getAs<VectorType>()) {
    uint64_t Size = getContext().getTypeSize(VecTy);
    unsigned LargestVector = getNativeVectorSizeForAVXABI(AVXLevel);
    if (Size <= 64 || Size > LargestVector)
      return true;
    QualType EltTy = VecTy->getElementType();
    if (passInt128VectorsInMem() &&
        (EltTy->isSpecificBuiltinType(BuiltinType::Int128) ||
         EltTy->isSpecificBuiltinType(BuiltinType::UInt128)))
      return true;
  }

  return false;
}

ABIArgInfo X86_64ABIInfo::getIndirectResult(QualType Ty,
                                            unsigned freeIntRegs) const {
  // If this is a scalar LLVM value then assume LLVM will pass it in the right
  // place naturally.
  //
  // This assumption is optimistic, as there could be free registers available
  // when we need to pass this argument in memory, and LLVM could try to pass
  // the argument in the free register. This does not seem to happen currently,
  // but this code would be much safer if we could mark the argument with
  // 'onstack'. See PR12193.
  if (!isAggregateTypeForABI(Ty) && !IsIllegalVectorType(Ty) &&
      !Ty->isBitIntType()) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
    return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);

  // Compute the byval alignment. We specify the alignment of the byval in all
  // cases so that the mid-level optimizer knows the alignment of the byval.
  unsigned Align = std::max(getContext().getTypeAlign(Ty) / 8, 8U);

  // Attempt to avoid passing indirect results using byval when possible. This
  // is important for good codegen.
  //
  // We do this by coercing the value into a scalar type which the backend can
  // handle naturally (i.e., without using byval).
  //
  // For simplicity, we currently only do this when we have exhausted all of the
  // free integer registers. Doing this when there are free integer registers
  // would require more care, as we would have to ensure that the coerced value
  // did not claim the unused register. That would require either reording the
  // arguments to the function (so that any subsequent inreg values came first),
  // or only doing this optimization when there were no following arguments that
  // might be inreg.
  //
  // We currently expect it to be rare (particularly in well written code) for
  // arguments to be passed on the stack when there are still free integer
  // registers available (this would typically imply large structs being passed
  // by value), so this seems like a fair tradeoff for now.
  //
  // We can revisit this if the backend grows support for 'onstack' parameter
  // attributes. See PR12193.
  if (freeIntRegs == 0) {
    uint64_t Size = getContext().getTypeSize(Ty);

    // If this type fits in an eightbyte, coerce it into the matching integral
    // type, which will end up on the stack (with alignment 8).
    if (Align == 8 && Size <= 64)
      return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(),
                                                          Size));
  }

  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(Align));
}

/// The ABI specifies that a value should be passed in a full vector XMM/YMM
/// register. Pick an LLVM IR type that will be passed as a vector register.
llvm::Type *X86_64ABIInfo::GetByteVectorType(QualType Ty) const {
  // Wrapper structs/arrays that only contain vectors are passed just like
  // vectors; strip them off if present.
  if (const Type *InnerTy = isSingleElementStruct(Ty, getContext()))
    Ty = QualType(InnerTy, 0);

  llvm::Type *IRType = CGT.ConvertType(Ty);
  if (isa<llvm::VectorType>(IRType)) {
    // Don't pass vXi128 vectors in their native type, the backend can't
    // legalize them.
    if (passInt128VectorsInMem() &&
        cast<llvm::VectorType>(IRType)->getElementType()->isIntegerTy(128)) {
      // Use a vXi64 vector.
      uint64_t Size = getContext().getTypeSize(Ty);
      return llvm::FixedVectorType::get(llvm::Type::getInt64Ty(getVMContext()),
                                        Size / 64);
    }

    return IRType;
  }

  if (IRType->getTypeID() == llvm::Type::FP128TyID)
    return IRType;

  // We couldn't find the preferred IR vector type for 'Ty'.
  uint64_t Size = getContext().getTypeSize(Ty);
  assert((Size == 128 || Size == 256 || Size == 512) && "Invalid type found!");


  // Return a LLVM IR vector type based on the size of 'Ty'.
  return llvm::FixedVectorType::get(llvm::Type::getDoubleTy(getVMContext()),
                                    Size / 64);
}

/// BitsContainNoUserData - Return true if the specified [start,end) bit range
/// is known to either be off the end of the specified type or being in
/// alignment padding.  The user type specified is known to be at most 128 bits
/// in size, and have passed through X86_64ABIInfo::classify with a successful
/// classification that put one of the two halves in the INTEGER class.
///
/// It is conservatively correct to return false.
static bool BitsContainNoUserData(QualType Ty, unsigned StartBit,
                                  unsigned EndBit, ASTContext &Context) {
  // If the bytes being queried are off the end of the type, there is no user
  // data hiding here.  This handles analysis of builtins, vectors and other
  // types that don't contain interesting padding.
  unsigned TySize = (unsigned)Context.getTypeSize(Ty);
  if (TySize <= StartBit)
    return true;

  if (const ConstantArrayType *AT = Context.getAsConstantArrayType(Ty)) {
    unsigned EltSize = (unsigned)Context.getTypeSize(AT->getElementType());
    unsigned NumElts = (unsigned)AT->getZExtSize();

    // Check each element to see if the element overlaps with the queried range.
    for (unsigned i = 0; i != NumElts; ++i) {
      // If the element is after the span we care about, then we're done..
      unsigned EltOffset = i*EltSize;
      if (EltOffset >= EndBit) break;

      unsigned EltStart = EltOffset < StartBit ? StartBit-EltOffset :0;
      if (!BitsContainNoUserData(AT->getElementType(), EltStart,
                                 EndBit-EltOffset, Context))
        return false;
    }
    // If it overlaps no elements, then it is safe to process as padding.
    return true;
  }

  if (const RecordType *RT = Ty->getAs<RecordType>()) {
    const RecordDecl *RD = RT->getDecl();
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

    // If this is a C++ record, check the bases first.
    if (const CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(RD)) {
      for (const auto &I : CXXRD->bases()) {
        assert(!I.isVirtual() && !I.getType()->isDependentType() &&
               "Unexpected base class!");
        const auto *Base =
            cast<CXXRecordDecl>(I.getType()->castAs<RecordType>()->getDecl());

        // If the base is after the span we care about, ignore it.
        unsigned BaseOffset = Context.toBits(Layout.getBaseClassOffset(Base));
        if (BaseOffset >= EndBit) continue;

        unsigned BaseStart = BaseOffset < StartBit ? StartBit-BaseOffset :0;
        if (!BitsContainNoUserData(I.getType(), BaseStart,
                                   EndBit-BaseOffset, Context))
          return false;
      }
    }

    // Verify that no field has data that overlaps the region of interest.  Yes
    // this could be sped up a lot by being smarter about queried fields,
    // however we're only looking at structs up to 16 bytes, so we don't care
    // much.
    unsigned idx = 0;
    for (RecordDecl::field_iterator i = RD->field_begin(), e = RD->field_end();
         i != e; ++i, ++idx) {
      unsigned FieldOffset = (unsigned)Layout.getFieldOffset(idx);

      // If we found a field after the region we care about, then we're done.
      if (FieldOffset >= EndBit) break;

      unsigned FieldStart = FieldOffset < StartBit ? StartBit-FieldOffset :0;
      if (!BitsContainNoUserData(i->getType(), FieldStart, EndBit-FieldOffset,
                                 Context))
        return false;
    }

    // If nothing in this record overlapped the area of interest, then we're
    // clean.
    return true;
  }

  return false;
}

/// getFPTypeAtOffset - Return a floating point type at the specified offset.
static llvm::Type *getFPTypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                                     const llvm::DataLayout &TD) {
  if (IROffset == 0 && IRType->isFloatingPointTy())
    return IRType;

  // If this is a struct, recurse into the field at the specified offset.
  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    if (!STy->getNumContainedTypes())
      return nullptr;

    const llvm::StructLayout *SL = TD.getStructLayout(STy);
    unsigned Elt = SL->getElementContainingOffset(IROffset);
    IROffset -= SL->getElementOffset(Elt);
    return getFPTypeAtOffset(STy->getElementType(Elt), IROffset, TD);
  }

  // If this is an array, recurse into the field at the specified offset.
  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = TD.getTypeAllocSize(EltTy);
    IROffset -= IROffset / EltSize * EltSize;
    return getFPTypeAtOffset(EltTy, IROffset, TD);
  }

  return nullptr;
}

/// GetSSETypeAtOffset - Return a type that will be passed by the backend in the
/// low 8 bytes of an XMM register, corresponding to the SSE class.
llvm::Type *X86_64ABIInfo::
GetSSETypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                   QualType SourceTy, unsigned SourceOffset) const {
  const llvm::DataLayout &TD = getDataLayout();
  unsigned SourceSize =
      (unsigned)getContext().getTypeSize(SourceTy) / 8 - SourceOffset;
  llvm::Type *T0 = getFPTypeAtOffset(IRType, IROffset, TD);
  if (!T0 || T0->isDoubleTy())
    return llvm::Type::getDoubleTy(getVMContext());

  // Get the adjacent FP type.
  llvm::Type *T1 = nullptr;
  unsigned T0Size = TD.getTypeAllocSize(T0);
  if (SourceSize > T0Size)
      T1 = getFPTypeAtOffset(IRType, IROffset + T0Size, TD);
  if (T1 == nullptr) {
    // Check if IRType is a half/bfloat + float. float type will be in IROffset+4 due
    // to its alignment.
    if (T0->is16bitFPTy() && SourceSize > 4)
      T1 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    // If we can't get a second FP type, return a simple half or float.
    // avx512fp16-abi.c:pr51813_2 shows it works to return float for
    // {float, i8} too.
    if (T1 == nullptr)
      return T0;
  }

  if (T0->isFloatTy() && T1->isFloatTy())
    return llvm::FixedVectorType::get(T0, 2);

  if (T0->is16bitFPTy() && T1->is16bitFPTy()) {
    llvm::Type *T2 = nullptr;
    if (SourceSize > 4)
      T2 = getFPTypeAtOffset(IRType, IROffset + 4, TD);
    if (T2 == nullptr)
      return llvm::FixedVectorType::get(T0, 2);
    return llvm::FixedVectorType::get(T0, 4);
  }

  if (T0->is16bitFPTy() || T1->is16bitFPTy())
    return llvm::FixedVectorType::get(llvm::Type::getHalfTy(getVMContext()), 4);

  return llvm::Type::getDoubleTy(getVMContext());
}


/// GetINTEGERTypeAtOffset - The ABI specifies that a value should be passed in
/// an 8-byte GPR.  This means that we either have a scalar or we are talking
/// about the high or low part of an up-to-16-byte struct.  This routine picks
/// the best LLVM IR type to represent this, which may be i64 or may be anything
/// else that the backend will pass in a GPR that works better (e.g. i8, %foo*,
/// etc).
///
/// PrefType is an LLVM IR type that corresponds to (part of) the IR type for
/// the source type.  IROffset is an offset in bytes into the LLVM IR type that
/// the 8-byte value references.  PrefType may be null.
///
/// SourceTy is the source-level type for the entire argument.  SourceOffset is
/// an offset into this that we're processing (which is always either 0 or 8).
///
llvm::Type *X86_64ABIInfo::
GetINTEGERTypeAtOffset(llvm::Type *IRType, unsigned IROffset,
                       QualType SourceTy, unsigned SourceOffset) const {
  // If we're dealing with an un-offset LLVM IR type, then it means that we're
  // returning an 8-byte unit starting with it.  See if we can safely use it.
  if (IROffset == 0) {
    // Pointers and int64's always fill the 8-byte unit.
    if ((isa<llvm::PointerType>(IRType) && Has64BitPointers) ||
        IRType->isIntegerTy(64))
      return IRType;

    // If we have a 1/2/4-byte integer, we can use it only if the rest of the
    // goodness in the source type is just tail padding.  This is allowed to
    // kick in for struct {double,int} on the int, but not on
    // struct{double,int,int} because we wouldn't return the second int.  We
    // have to do this analysis on the source type because we can't depend on
    // unions being lowered a specific way etc.
    if (IRType->isIntegerTy(8) || IRType->isIntegerTy(16) ||
        IRType->isIntegerTy(32) ||
        (isa<llvm::PointerType>(IRType) && !Has64BitPointers)) {
      unsigned BitWidth = isa<llvm::PointerType>(IRType) ? 32 :
          cast<llvm::IntegerType>(IRType)->getBitWidth();

      if (BitsContainNoUserData(SourceTy, SourceOffset*8+BitWidth,
                                SourceOffset*8+64, getContext()))
        return IRType;
    }
  }

  if (llvm::StructType *STy = dyn_cast<llvm::StructType>(IRType)) {
    // If this is a struct, recurse into the field at the specified offset.
    const llvm::StructLayout *SL = getDataLayout().getStructLayout(STy);
    if (IROffset < SL->getSizeInBytes()) {
      unsigned FieldIdx = SL->getElementContainingOffset(IROffset);
      IROffset -= SL->getElementOffset(FieldIdx);

      return GetINTEGERTypeAtOffset(STy->getElementType(FieldIdx), IROffset,
                                    SourceTy, SourceOffset);
    }
  }

  if (llvm::ArrayType *ATy = dyn_cast<llvm::ArrayType>(IRType)) {
    llvm::Type *EltTy = ATy->getElementType();
    unsigned EltSize = getDataLayout().getTypeAllocSize(EltTy);
    unsigned EltOffset = IROffset/EltSize*EltSize;
    return GetINTEGERTypeAtOffset(EltTy, IROffset-EltOffset, SourceTy,
                                  SourceOffset);
  }

  // Okay, we don't have any better idea of what to pass, so we pass this in an
  // integer register that isn't too big to fit the rest of the struct.
  unsigned TySizeInBytes =
    (unsigned)getContext().getTypeSizeInChars(SourceTy).getQuantity();

  assert(TySizeInBytes != SourceOffset && "Empty field?");

  // It is always safe to classify this as an integer type up to i64 that
  // isn't larger than the structure.
  return llvm::IntegerType::get(getVMContext(),
                                std::min(TySizeInBytes-SourceOffset, 8U)*8);
}


/// GetX86_64ByValArgumentPair - Given a high and low type that can ideally
/// be used as elements of a two register pair to pass or return, return a
/// first class aggregate to represent them.  For example, if the low part of
/// a by-value argument should be passed as i32* and the high part as float,
/// return {i32*, float}.
static llvm::Type *
GetX86_64ByValArgumentPair(llvm::Type *Lo, llvm::Type *Hi,
                           const llvm::DataLayout &TD) {
  // In order to correctly satisfy the ABI, we need to the high part to start
  // at offset 8.  If the high and low parts we inferred are both 4-byte types
  // (e.g. i32 and i32) then the resultant struct type ({i32,i32}) won't have
  // the second element at offset 8.  Check for this:
  unsigned LoSize = (unsigned)TD.getTypeAllocSize(Lo);
  llvm::Align HiAlign = TD.getABITypeAlign(Hi);
  unsigned HiStart = llvm::alignTo(LoSize, HiAlign);
  assert(HiStart != 0 && HiStart <= 8 && "Invalid x86-64 argument pair!");

  // To handle this, we have to increase the size of the low part so that the
  // second element will start at an 8 byte offset.  We can't increase the size
  // of the second element because it might make us access off the end of the
  // struct.
  if (HiStart != 8) {
    // There are usually two sorts of types the ABI generation code can produce
    // for the low part of a pair that aren't 8 bytes in size: half, float or
    // i8/i16/i32.  This can also include pointers when they are 32-bit (X32 and
    // NaCl).
    // Promote these to a larger type.
    if (Lo->isHalfTy() || Lo->isFloatTy())
      Lo = llvm::Type::getDoubleTy(Lo->getContext());
    else {
      assert((Lo->isIntegerTy() || Lo->isPointerTy())
             && "Invalid/unknown lo type");
      Lo = llvm::Type::getInt64Ty(Lo->getContext());
    }
  }

  llvm::StructType *Result = llvm::StructType::get(Lo, Hi);

  // Verify that the second element is at an 8-byte offset.
  assert(TD.getStructLayout(Result)->getElementOffset(1) == 8 &&
         "Invalid x86-64 argument pair!");
  return Result;
}

ABIArgInfo X86_64ABIInfo::
classifyReturnType(QualType RetTy) const {
  // AMD64-ABI 3.2.3p4: Rule 1. Classify the return type with the
  // classification algorithm.
  X86_64ABIInfo::Class Lo, Hi;
  classify(RetTy, 0, Lo, Hi, /*isNamedArg*/ true);

  // Check some invariants.
  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p4: Rule 2. Types of class memory are returned via
    // hidden argument.
  case Memory:
    return getIndirectReturnResult(RetTy);

    // AMD64-ABI 3.2.3p4: Rule 3. If the class is INTEGER, the next
    // available register of the sequence %rax, %rdx is used.
  case Integer:
    ResType = GetINTEGERTypeAtOffset(CGT.ConvertType(RetTy), 0, RetTy, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
        RetTy = EnumTy->getDecl()->getIntegerType();

      if (RetTy->isIntegralOrEnumerationType() &&
          isPromotableIntegerTypeForABI(RetTy))
        return ABIArgInfo::getExtend(RetTy);
    }
    break;

    // AMD64-ABI 3.2.3p4: Rule 4. If the class is SSE, the next
    // available SSE register of the sequence %xmm0, %xmm1 is used.
  case SSE:
    ResType = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 0, RetTy, 0);
    break;

    // AMD64-ABI 3.2.3p4: Rule 6. If the class is X87, the value is
    // returned on the X87 stack in %st0 as 80-bit x87 number.
  case X87:
    ResType = llvm::Type::getX86_FP80Ty(getVMContext());
    break;

    // AMD64-ABI 3.2.3p4: Rule 8. If the class is COMPLEX_X87, the real
    // part of the value is returned in %st0 and the imaginary part in
    // %st1.
  case ComplexX87:
    assert(Hi == ComplexX87 && "Unexpected ComplexX87 classification.");
    ResType = llvm::StructType::get(llvm::Type::getX86_FP80Ty(getVMContext()),
                                    llvm::Type::getX86_FP80Ty(getVMContext()));
    break;
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously and X87 should
    // never occur as a hi class.
  case Memory:
  case X87:
    llvm_unreachable("Invalid classification for hi word.");

  case ComplexX87: // Previously handled.
  case NoClass:
    break;

  case Integer:
    HighPart = GetINTEGERTypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;
  case SSE:
    HighPart = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
    if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // AMD64-ABI 3.2.3p4: Rule 5. If the class is SSEUP, the eightbyte
    // is passed in the next available eightbyte chunk if the last used
    // vector register.
    //
    // SSEUP should always be preceded by SSE, just widen.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification.");
    ResType = GetByteVectorType(RetTy);
    break;

    // AMD64-ABI 3.2.3p4: Rule 7. If the class is X87UP, the value is
    // returned together with the previous X87 value in %st0.
  case X87Up:
    // If X87Up is preceded by X87, we don't need to do
    // anything. However, in some cases with unions it may not be
    // preceded by X87. In such situations we follow gcc and pass the
    // extra bits in an SSE reg.
    if (Lo != X87) {
      HighPart = GetSSETypeAtOffset(CGT.ConvertType(RetTy), 8, RetTy, 8);
      if (Lo == NoClass)  // Return HighPart at offset 8 in memory.
        return ABIArgInfo::getDirect(HighPart, 8);
    }
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = GetX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyArgumentType(QualType Ty, unsigned freeIntRegs,
                                    unsigned &neededInt, unsigned &neededSSE,
                                    bool isNamedArg, bool IsRegCall) const {
  Ty = useFirstFieldIfTransparentUnion(Ty);

  X86_64ABIInfo::Class Lo, Hi;
  classify(Ty, 0, Lo, Hi, isNamedArg, IsRegCall);

  // Check some invariants.
  // FIXME: Enforce these by construction.
  assert((Hi != Memory || Lo == Memory) && "Invalid memory classification.");
  assert((Hi != SSEUp || Lo == SSE) && "Invalid SSEUp classification.");

  neededInt = 0;
  neededSSE = 0;
  llvm::Type *ResType = nullptr;
  switch (Lo) {
  case NoClass:
    if (Hi == NoClass)
      return ABIArgInfo::getIgnore();
    // If the low part is just padding, it takes no register, leave ResType
    // null.
    assert((Hi == SSE || Hi == Integer || Hi == X87Up) &&
           "Unknown missing lo part");
    break;

    // AMD64-ABI 3.2.3p3: Rule 1. If the class is MEMORY, pass the argument
    // on the stack.
  case Memory:

    // AMD64-ABI 3.2.3p3: Rule 5. If the class is X87, X87UP or
    // COMPLEX_X87, it is passed in memory.
  case X87:
  case ComplexX87:
    if (getRecordArgABI(Ty, getCXXABI()) == CGCXXABI::RAA_Indirect)
      ++neededInt;
    return getIndirectResult(Ty, freeIntRegs);

  case SSEUp:
  case X87Up:
    llvm_unreachable("Invalid classification for lo word.");

    // AMD64-ABI 3.2.3p3: Rule 2. If the class is INTEGER, the next
    // available register of the sequence %rdi, %rsi, %rdx, %rcx, %r8
    // and %r9 is used.
  case Integer:
    ++neededInt;

    // Pick an 8-byte type based on the preferred type.
    ResType = GetINTEGERTypeAtOffset(CGT.ConvertType(Ty), 0, Ty, 0);

    // If we have a sign or zero extended integer, make sure to return Extend
    // so that the parameter gets the right LLVM IR attributes.
    if (Hi == NoClass && isa<llvm::IntegerType>(ResType)) {
      // Treat an enum type as its underlying type.
      if (const EnumType *EnumTy = Ty->getAs<EnumType>())
        Ty = EnumTy->getDecl()->getIntegerType();

      if (Ty->isIntegralOrEnumerationType() &&
          isPromotableIntegerTypeForABI(Ty))
        return ABIArgInfo::getExtend(Ty);
    }

    break;

    // AMD64-ABI 3.2.3p3: Rule 3. If the class is SSE, the next
    // available SSE register is used, the registers are taken in the
    // order from %xmm0 to %xmm7.
  case SSE: {
    llvm::Type *IRType = CGT.ConvertType(Ty);
    ResType = GetSSETypeAtOffset(IRType, 0, Ty, 0);
    ++neededSSE;
    break;
  }
  }

  llvm::Type *HighPart = nullptr;
  switch (Hi) {
    // Memory was handled previously, ComplexX87 and X87 should
    // never occur as hi classes, and X87Up must be preceded by X87,
    // which is passed in memory.
  case Memory:
  case X87:
  case ComplexX87:
    llvm_unreachable("Invalid classification for hi word.");

  case NoClass: break;

  case Integer:
    ++neededInt;
    // Pick an 8-byte type based on the preferred type.
    HighPart = GetINTEGERTypeAtOffset(CGT.ConvertType(Ty), 8, Ty, 8);

    if (Lo == NoClass)  // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // X87Up generally doesn't occur here (long double is passed in
    // memory), except in situations involving unions.
  case X87Up:
  case SSE:
    ++neededSSE;
    HighPart = GetSSETypeAtOffset(CGT.ConvertType(Ty), 8, Ty, 8);

    if (Lo == NoClass)  // Pass HighPart at offset 8 in memory.
      return ABIArgInfo::getDirect(HighPart, 8);
    break;

    // AMD64-ABI 3.2.3p3: Rule 4. If the class is SSEUP, the
    // eightbyte is passed in the upper half of the last used SSE
    // register.  This only happens when 128-bit vectors are passed.
  case SSEUp:
    assert(Lo == SSE && "Unexpected SSEUp classification");
    ResType = GetByteVectorType(Ty);
    break;
  }

  // If a high part was specified, merge it together with the low part.  It is
  // known to pass in the high eightbyte of the result.  We do this by forming a
  // first class struct aggregate with the high and low part: {low, high}
  if (HighPart)
    ResType = GetX86_64ByValArgumentPair(ResType, HighPart, getDataLayout());

  return ABIArgInfo::getDirect(ResType);
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructTypeImpl(QualType Ty, unsigned &NeededInt,
                                             unsigned &NeededSSE,
                                             unsigned &MaxVectorWidth) const {
  auto RT = Ty->getAs<RecordType>();
  assert(RT && "classifyRegCallStructType only valid with struct types");

  if (RT->getDecl()->hasFlexibleArrayMember())
    return getIndirectReturnResult(Ty);

  // Sum up bases
  if (auto CXXRD = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
    if (CXXRD->isDynamicClass()) {
      NeededInt = NeededSSE = 0;
      return getIndirectReturnResult(Ty);
    }

    for (const auto &I : CXXRD->bases())
      if (classifyRegCallStructTypeImpl(I.getType(), NeededInt, NeededSSE,
                                        MaxVectorWidth)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
  }

  // Sum up members
  for (const auto *FD : RT->getDecl()->fields()) {
    QualType MTy = FD->getType();
    if (MTy->isRecordType() && !MTy->isUnionType()) {
      if (classifyRegCallStructTypeImpl(MTy, NeededInt, NeededSSE,
                                        MaxVectorWidth)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
    } else {
      unsigned LocalNeededInt, LocalNeededSSE;
      if (classifyArgumentType(MTy, UINT_MAX, LocalNeededInt, LocalNeededSSE,
                               true, true)
              .isIndirect()) {
        NeededInt = NeededSSE = 0;
        return getIndirectReturnResult(Ty);
      }
      if (const auto *AT = getContext().getAsConstantArrayType(MTy))
        MTy = AT->getElementType();
      if (const auto *VT = MTy->getAs<VectorType>())
        if (getContext().getTypeSize(VT) > MaxVectorWidth)
          MaxVectorWidth = getContext().getTypeSize(VT);
      NeededInt += LocalNeededInt;
      NeededSSE += LocalNeededSSE;
    }
  }

  return ABIArgInfo::getDirect();
}

ABIArgInfo
X86_64ABIInfo::classifyRegCallStructType(QualType Ty, unsigned &NeededInt,
                                         unsigned &NeededSSE,
                                         unsigned &MaxVectorWidth) const {

  NeededInt = 0;
  NeededSSE = 0;
  MaxVectorWidth = 0;

  return classifyRegCallStructTypeImpl(Ty, NeededInt, NeededSSE,
                                       MaxVectorWidth);
}

void X86_64ABIInfo::computeInfo(CGFunctionInfo &FI) const {

  const unsigned CallingConv = FI.getCallingConvention();
  // It is possible to force Win64 calling convention on any x86_64 target by
  // using __attribute__((ms_abi)). In such case to correctly emit Win64
  // compatible code delegate this call to WinX86_64ABIInfo::computeInfo.
  if (CallingConv == llvm::CallingConv::Win64) {
    WinX86_64ABIInfo Win64ABIInfo(CGT, AVXLevel);
    Win64ABIInfo.computeInfo(FI);
    return;
  }

  bool IsRegCall = CallingConv == llvm::CallingConv::X86_RegCall;

  // Keep track of the number of assigned registers.
  unsigned FreeIntRegs = IsRegCall ? 11 : 6;
  unsigned FreeSSERegs = IsRegCall ? 16 : 8;
  unsigned NeededInt = 0, NeededSSE = 0, MaxVectorWidth = 0;

  if (!::classifyReturnType(getCXXABI(), FI, *this)) {
    if (IsRegCall && FI.getReturnType()->getTypePtr()->isRecordType() &&
        !FI.getReturnType()->getTypePtr()->isUnionType()) {
      FI.getReturnInfo() = classifyRegCallStructType(
          FI.getReturnType(), NeededInt, NeededSSE, MaxVectorWidth);
      if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
        FreeIntRegs -= NeededInt;
        FreeSSERegs -= NeededSSE;
      } else {
        FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
      }
    } else if (IsRegCall && FI.getReturnType()->getAs<ComplexType>() &&
               getContext().getCanonicalType(FI.getReturnType()
                                                 ->getAs<ComplexType>()
                                                 ->getElementType()) ==
                   getContext().LongDoubleTy)
      // Complex Long Double Type is passed in Memory when Regcall
      // calling convention is used.
      FI.getReturnInfo() = getIndirectReturnResult(FI.getReturnType());
    else
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  }

  // If the return value is indirect, then the hidden argument is consuming one
  // integer register.
  if (FI.getReturnInfo().isIndirect())
    --FreeIntRegs;
  else if (NeededSSE && MaxVectorWidth > 0)
    FI.setMaxVectorWidth(MaxVectorWidth);

  // The chain argument effectively gives us another free register.
  if (FI.isChainCall())
    ++FreeIntRegs;

  unsigned NumRequiredArgs = FI.getNumRequiredArgs();
  // AMD64-ABI 3.2.3p3: Once arguments are classified, the registers
  // get assigned (in left-to-right order) for passing as follows...
  unsigned ArgNo = 0;
  for (CGFunctionInfo::arg_iterator it = FI.arg_begin(), ie = FI.arg_end();
       it != ie; ++it, ++ArgNo) {
    bool IsNamedArg = ArgNo < NumRequiredArgs;

    if (IsRegCall && it->type->isStructureOrClassType())
      it->info = classifyRegCallStructType(it->type, NeededInt, NeededSSE,
                                           MaxVectorWidth);
    else
      it->info = classifyArgumentType(it->type, FreeIntRegs, NeededInt,
                                      NeededSSE, IsNamedArg);

    // AMD64-ABI 3.2.3p3: If there are no registers available for any
    // eightbyte of an argument, the whole argument is passed on the
    // stack. If registers have already been assigned for some
    // eightbytes of such an argument, the assignments get reverted.
    if (FreeIntRegs >= NeededInt && FreeSSERegs >= NeededSSE) {
      FreeIntRegs -= NeededInt;
      FreeSSERegs -= NeededSSE;
      if (MaxVectorWidth > FI.getMaxVectorWidth())
        FI.setMaxVectorWidth(MaxVectorWidth);
    } else {
      it->info = getIndirectResult(it->type, FreeIntRegs);
    }
  }
}

static Address EmitX86_64VAArgFromMemory(CodeGenFunction &CGF,
                                         Address VAListAddr, QualType Ty) {
  Address overflow_arg_area_p =
      CGF.Builder.CreateStructGEP(VAListAddr, 2, "overflow_arg_area_p");
  llvm::Value *overflow_arg_area =
    CGF.Builder.CreateLoad(overflow_arg_area_p, "overflow_arg_area");

  // AMD64-ABI 3.5.7p5: Step 7. Align l->overflow_arg_area upwards to a 16
  // byte boundary if alignment needed by type exceeds 8 byte boundary.
  // It isn't stated explicitly in the standard, but in practice we use
  // alignment greater than 16 where necessary.
  CharUnits Align = CGF.getContext().getTypeAlignInChars(Ty);
  if (Align > CharUnits::fromQuantity(8)) {
    overflow_arg_area = emitRoundPointerUpToAlignment(CGF, overflow_arg_area,
                                                      Align);
  }

  // AMD64-ABI 3.5.7p5: Step 8. Fetch type from l->overflow_arg_area.
  llvm::Type *LTy = CGF.ConvertTypeForMem(Ty);
  llvm::Value *Res = overflow_arg_area;

  // AMD64-ABI 3.5.7p5: Step 9. Set l->overflow_arg_area to:
  // l->overflow_arg_area + sizeof(type).
  // AMD64-ABI 3.5.7p5: Step 10. Align l->overflow_arg_area upwards to
  // an 8 byte boundary.

  uint64_t SizeInBytes = (CGF.getContext().getTypeSize(Ty) + 7) / 8;
  llvm::Value *Offset =
      llvm::ConstantInt::get(CGF.Int32Ty, (SizeInBytes + 7)  & ~7);
  overflow_arg_area = CGF.Builder.CreateGEP(CGF.Int8Ty, overflow_arg_area,
                                            Offset, "overflow_arg_area.next");
  CGF.Builder.CreateStore(overflow_arg_area, overflow_arg_area_p);

  // AMD64-ABI 3.5.7p5: Step 11. Return the fetched type.
  return Address(Res, LTy, Align);
}

RValue X86_64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                QualType Ty, AggValueSlot Slot) const {
  // Assume that va_list type is correct; should be pointer to LLVM type:
  // struct {
  //   i32 gp_offset;
  //   i32 fp_offset;
  //   i8* overflow_arg_area;
  //   i8* reg_save_area;
  // };
  unsigned neededInt, neededSSE;

  Ty = getContext().getCanonicalType(Ty);
  ABIArgInfo AI = classifyArgumentType(Ty, 0, neededInt, neededSSE,
                                       /*isNamedArg*/false);

  // Empty records are ignored for parameter passing purposes.
  if (AI.isIgnore())
    return Slot.asRValue();

  // AMD64-ABI 3.5.7p5: Step 1. Determine whether type may be passed
  // in the registers. If not go to step 7.
  if (!neededInt && !neededSSE)
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(EmitX86_64VAArgFromMemory(CGF, VAListAddr, Ty), Ty),
        Slot);

  // AMD64-ABI 3.5.7p5: Step 2. Compute num_gp to hold the number of
  // general purpose registers needed to pass type and num_fp to hold
  // the number of floating point registers needed.

  // AMD64-ABI 3.5.7p5: Step 3. Verify whether arguments fit into
  // registers. In the case: l->gp_offset > 48 - num_gp * 8 or
  // l->fp_offset > 304 - num_fp * 16 go to step 7.
  //
  // NOTE: 304 is a typo, there are (6 * 8 + 8 * 16) = 176 bytes of
  // register save space).

  llvm::Value *InRegs = nullptr;
  Address gp_offset_p = Address::invalid(), fp_offset_p = Address::invalid();
  llvm::Value *gp_offset = nullptr, *fp_offset = nullptr;
  if (neededInt) {
    gp_offset_p = CGF.Builder.CreateStructGEP(VAListAddr, 0, "gp_offset_p");
    gp_offset = CGF.Builder.CreateLoad(gp_offset_p, "gp_offset");
    InRegs = llvm::ConstantInt::get(CGF.Int32Ty, 48 - neededInt * 8);
    InRegs = CGF.Builder.CreateICmpULE(gp_offset, InRegs, "fits_in_gp");
  }

  if (neededSSE) {
    fp_offset_p = CGF.Builder.CreateStructGEP(VAListAddr, 1, "fp_offset_p");
    fp_offset = CGF.Builder.CreateLoad(fp_offset_p, "fp_offset");
    llvm::Value *FitsInFP =
      llvm::ConstantInt::get(CGF.Int32Ty, 176 - neededSSE * 16);
    FitsInFP = CGF.Builder.CreateICmpULE(fp_offset, FitsInFP, "fits_in_fp");
    InRegs = InRegs ? CGF.Builder.CreateAnd(InRegs, FitsInFP) : FitsInFP;
  }

  llvm::BasicBlock *InRegBlock = CGF.createBasicBlock("vaarg.in_reg");
  llvm::BasicBlock *InMemBlock = CGF.createBasicBlock("vaarg.in_mem");
  llvm::BasicBlock *ContBlock = CGF.createBasicBlock("vaarg.end");
  CGF.Builder.CreateCondBr(InRegs, InRegBlock, InMemBlock);

  // Emit code to load the value if it was passed in registers.

  CGF.EmitBlock(InRegBlock);

  // AMD64-ABI 3.5.7p5: Step 4. Fetch type from l->reg_save_area with
  // an offset of l->gp_offset and/or l->fp_offset. This may require
  // copying to a temporary location in case the parameter is passed
  // in different register classes or requires an alignment greater
  // than 8 for general purpose registers and 16 for XMM registers.
  //
  // FIXME: This really results in shameful code when we end up needing to
  // collect arguments from different places; often what should result in a
  // simple assembling of a structure from scattered addresses has many more
  // loads than necessary. Can we clean this up?
  llvm::Type *LTy = CGF.ConvertTypeForMem(Ty);
  llvm::Value *RegSaveArea = CGF.Builder.CreateLoad(
      CGF.Builder.CreateStructGEP(VAListAddr, 3), "reg_save_area");

  Address RegAddr = Address::invalid();
  if (neededInt && neededSSE) {
    // FIXME: Cleanup.
    assert(AI.isDirect() && "Unexpected ABI info for mixed regs");
    llvm::StructType *ST = cast<llvm::StructType>(AI.getCoerceToType());
    Address Tmp = CGF.CreateMemTemp(Ty);
    Tmp = Tmp.withElementType(ST);
    assert(ST->getNumElements() == 2 && "Unexpected ABI info for mixed regs");
    llvm::Type *TyLo = ST->getElementType(0);
    llvm::Type *TyHi = ST->getElementType(1);
    assert((TyLo->isFPOrFPVectorTy() ^ TyHi->isFPOrFPVectorTy()) &&
           "Unexpected ABI info for mixed regs");
    llvm::Value *GPAddr =
        CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea, gp_offset);
    llvm::Value *FPAddr =
        CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea, fp_offset);
    llvm::Value *RegLoAddr = TyLo->isFPOrFPVectorTy() ? FPAddr : GPAddr;
    llvm::Value *RegHiAddr = TyLo->isFPOrFPVectorTy() ? GPAddr : FPAddr;

    // Copy the first element.
    // FIXME: Our choice of alignment here and below is probably pessimistic.
    llvm::Value *V = CGF.Builder.CreateAlignedLoad(
        TyLo, RegLoAddr,
        CharUnits::fromQuantity(getDataLayout().getABITypeAlign(TyLo)));
    CGF.Builder.CreateStore(V, CGF.Builder.CreateStructGEP(Tmp, 0));

    // Copy the second element.
    V = CGF.Builder.CreateAlignedLoad(
        TyHi, RegHiAddr,
        CharUnits::fromQuantity(getDataLayout().getABITypeAlign(TyHi)));
    CGF.Builder.CreateStore(V, CGF.Builder.CreateStructGEP(Tmp, 1));

    RegAddr = Tmp.withElementType(LTy);
  } else if (neededInt) {
    RegAddr = Address(CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea, gp_offset),
                      LTy, CharUnits::fromQuantity(8));

    // Copy to a temporary if necessary to ensure the appropriate alignment.
    auto TInfo = getContext().getTypeInfoInChars(Ty);
    uint64_t TySize = TInfo.Width.getQuantity();
    CharUnits TyAlign = TInfo.Align;

    // Copy into a temporary if the type is more aligned than the
    // register save area.
    if (TyAlign.getQuantity() > 8) {
      Address Tmp = CGF.CreateMemTemp(Ty);
      CGF.Builder.CreateMemCpy(Tmp, RegAddr, TySize, false);
      RegAddr = Tmp;
    }

  } else if (neededSSE == 1) {
    RegAddr = Address(CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea, fp_offset),
                      LTy, CharUnits::fromQuantity(16));
  } else {
    assert(neededSSE == 2 && "Invalid number of needed registers!");
    // SSE registers are spaced 16 bytes apart in the register save
    // area, we need to collect the two eightbytes together.
    // The ABI isn't explicit about this, but it seems reasonable
    // to assume that the slots are 16-byte aligned, since the stack is
    // naturally 16-byte aligned and the prologue is expected to store
    // all the SSE registers to the RSA.
    Address RegAddrLo = Address(CGF.Builder.CreateGEP(CGF.Int8Ty, RegSaveArea,
                                                      fp_offset),
                                CGF.Int8Ty, CharUnits::fromQuantity(16));
    Address RegAddrHi =
      CGF.Builder.CreateConstInBoundsByteGEP(RegAddrLo,
                                             CharUnits::fromQuantity(16));
    llvm::Type *ST = AI.canHaveCoerceToType()
                         ? AI.getCoerceToType()
                         : llvm::StructType::get(CGF.DoubleTy, CGF.DoubleTy);
    llvm::Value *V;
    Address Tmp = CGF.CreateMemTemp(Ty);
    Tmp = Tmp.withElementType(ST);
    V = CGF.Builder.CreateLoad(
        RegAddrLo.withElementType(ST->getStructElementType(0)));
    CGF.Builder.CreateStore(V, CGF.Builder.CreateStructGEP(Tmp, 0));
    V = CGF.Builder.CreateLoad(
        RegAddrHi.withElementType(ST->getStructElementType(1)));
    CGF.Builder.CreateStore(V, CGF.Builder.CreateStructGEP(Tmp, 1));

    RegAddr = Tmp.withElementType(LTy);
  }

  // AMD64-ABI 3.5.7p5: Step 5. Set:
  // l->gp_offset = l->gp_offset + num_gp * 8
  // l->fp_offset = l->fp_offset + num_fp * 16.
  if (neededInt) {
    llvm::Value *Offset = llvm::ConstantInt::get(CGF.Int32Ty, neededInt * 8);
    CGF.Builder.CreateStore(CGF.Builder.CreateAdd(gp_offset, Offset),
                            gp_offset_p);
  }
  if (neededSSE) {
    llvm::Value *Offset = llvm::ConstantInt::get(CGF.Int32Ty, neededSSE * 16);
    CGF.Builder.CreateStore(CGF.Builder.CreateAdd(fp_offset, Offset),
                            fp_offset_p);
  }
  CGF.EmitBranch(ContBlock);

  // Emit code to load the value if it was passed in memory.

  CGF.EmitBlock(InMemBlock);
  Address MemAddr = EmitX86_64VAArgFromMemory(CGF, VAListAddr, Ty);

  // Return the appropriate result.

  CGF.EmitBlock(ContBlock);
  Address ResAddr = emitMergePHI(CGF, RegAddr, InRegBlock, MemAddr, InMemBlock,
                                 "vaarg.addr");
  return CGF.EmitLoadOfAnyValue(CGF.MakeAddrLValue(ResAddr, Ty), Slot);
}

RValue X86_64ABIInfo::EmitMSVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                  QualType Ty, AggValueSlot Slot) const {
  // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
  // not 1, 2, 4, or 8 bytes, must be passed by reference."
  uint64_t Width = getContext().getTypeSize(Ty);
  bool IsIndirect = Width > 64 || !llvm::isPowerOf2_64(Width);

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          CGF.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false, Slot);
}

ABIArgInfo WinX86_64ABIInfo::reclassifyHvaArgForVectorCall(
    QualType Ty, unsigned &FreeSSERegs, const ABIArgInfo &current) const {
  const Type *Base = nullptr;
  uint64_t NumElts = 0;

  if (!Ty->isBuiltinType() && !Ty->isVectorType() &&
      isHomogeneousAggregate(Ty, Base, NumElts) && FreeSSERegs >= NumElts) {
    FreeSSERegs -= NumElts;
    return getDirectX86Hva();
  }
  return current;
}

ABIArgInfo WinX86_64ABIInfo::classify(QualType Ty, unsigned &FreeSSERegs,
                                      bool IsReturnType, bool IsVectorCall,
                                      bool IsRegCall) const {

  if (Ty->isVoidType())
    return ABIArgInfo::getIgnore();

  if (const EnumType *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  TypeInfo Info = getContext().getTypeInfo(Ty);
  uint64_t Width = Info.Width;
  CharUnits Align = getContext().toCharUnitsFromBits(Info.Align);

  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    if (!IsReturnType) {
      if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI()))
        return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    }

    if (RT->getDecl()->hasFlexibleArrayMember())
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

  }

  const Type *Base = nullptr;
  uint64_t NumElts = 0;
  // vectorcall adds the concept of a homogenous vector aggregate, similar to
  // other targets.
  if ((IsVectorCall || IsRegCall) &&
      isHomogeneousAggregate(Ty, Base, NumElts)) {
    if (IsRegCall) {
      if (FreeSSERegs >= NumElts) {
        FreeSSERegs -= NumElts;
        if (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())
          return ABIArgInfo::getDirect();
        return ABIArgInfo::getExpand();
      }
      return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    } else if (IsVectorCall) {
      if (FreeSSERegs >= NumElts &&
          (IsReturnType || Ty->isBuiltinType() || Ty->isVectorType())) {
        FreeSSERegs -= NumElts;
        return ABIArgInfo::getDirect();
      } else if (IsReturnType) {
        return ABIArgInfo::getExpand();
      } else if (!Ty->isBuiltinType() && !Ty->isVectorType()) {
        // HVAs are delayed and reclassified in the 2nd step.
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
      }
    }
  }

  if (Ty->isMemberPointerType()) {
    // If the member pointer is represented by an LLVM int or ptr, pass it
    // directly.
    llvm::Type *LLTy = CGT.ConvertType(Ty);
    if (LLTy->isPointerTy() || LLTy->isIntegerTy())
      return ABIArgInfo::getDirect();
  }

  if (RT || Ty->isAnyComplexType() || Ty->isMemberPointerType()) {
    // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
    // not 1, 2, 4, or 8 bytes, must be passed by reference."
    if (Width > 64 || !llvm::isPowerOf2_64(Width))
      return getNaturalAlignIndirect(Ty, /*ByVal=*/false);

    // Otherwise, coerce it to a small integer.
    return ABIArgInfo::getDirect(llvm::IntegerType::get(getVMContext(), Width));
  }

  if (const BuiltinType *BT = Ty->getAs<BuiltinType>()) {
    switch (BT->getKind()) {
    case BuiltinType::Bool:
      // Bool type is always extended to the ABI, other builtin types are not
      // extended.
      return ABIArgInfo::getExtend(Ty);

    case BuiltinType::LongDouble:
      // Mingw64 GCC uses the old 80 bit extended precision floating point
      // unit. It passes them indirectly through memory.
      if (IsMingw64) {
        const llvm::fltSemantics *LDF = &getTarget().getLongDoubleFormat();
        if (LDF == &llvm::APFloat::x87DoubleExtended())
          return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
      }
      break;

    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      // If it's a parameter type, the normal ABI rule is that arguments larger
      // than 8 bytes are passed indirectly. GCC follows it. We follow it too,
      // even though it isn't particularly efficient.
      if (!IsReturnType)
        return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);

      // Mingw64 GCC returns i128 in XMM0. Coerce to v2i64 to handle that.
      // Clang matches them for compatibility.
      return ABIArgInfo::getDirect(llvm::FixedVectorType::get(
          llvm::Type::getInt64Ty(getVMContext()), 2));

    default:
      break;
    }
  }

  if (Ty->isBitIntType()) {
    // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
    // not 1, 2, 4, or 8 bytes, must be passed by reference."
    // However, non-power-of-two bit-precise integers will be passed as 1, 2, 4,
    // or 8 bytes anyway as long is it fits in them, so we don't have to check
    // the power of 2.
    if (Width <= 64)
      return ABIArgInfo::getDirect();
    return ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
  }

  return ABIArgInfo::getDirect();
}

void WinX86_64ABIInfo::computeInfo(CGFunctionInfo &FI) const {
  const unsigned CC = FI.getCallingConvention();
  bool IsVectorCall = CC == llvm::CallingConv::X86_VectorCall;
  bool IsRegCall = CC == llvm::CallingConv::X86_RegCall;

  // If __attribute__((sysv_abi)) is in use, use the SysV argument
  // classification rules.
  if (CC == llvm::CallingConv::X86_64_SysV) {
    X86_64ABIInfo SysVABIInfo(CGT, AVXLevel);
    SysVABIInfo.computeInfo(FI);
    return;
  }

  unsigned FreeSSERegs = 0;
  if (IsVectorCall) {
    // We can use up to 4 SSE return registers with vectorcall.
    FreeSSERegs = 4;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers.
    FreeSSERegs = 16;
  }

  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classify(FI.getReturnType(), FreeSSERegs, true,
                                  IsVectorCall, IsRegCall);

  if (IsVectorCall) {
    // We can use up to 6 SSE register parameters with vectorcall.
    FreeSSERegs = 6;
  } else if (IsRegCall) {
    // RegCall gives us 16 SSE registers, we can reuse the return registers.
    FreeSSERegs = 16;
  }

  unsigned ArgNum = 0;
  unsigned ZeroSSERegs = 0;
  for (auto &I : FI.arguments()) {
    // Vectorcall in x64 only permits the first 6 arguments to be passed as
    // XMM/YMM registers. After the sixth argument, pretend no vector
    // registers are left.
    unsigned *MaybeFreeSSERegs =
        (IsVectorCall && ArgNum >= 6) ? &ZeroSSERegs : &FreeSSERegs;
    I.info =
        classify(I.type, *MaybeFreeSSERegs, false, IsVectorCall, IsRegCall);
    ++ArgNum;
  }

  if (IsVectorCall) {
    // For vectorcall, assign aggregate HVAs to any free vector registers in a
    // second pass.
    for (auto &I : FI.arguments())
      I.info = reclassifyHvaArgForVectorCall(I.type, FreeSSERegs, I.info);
  }
}

RValue WinX86_64ABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                                   QualType Ty, AggValueSlot Slot) const {
  // MS x64 ABI requirement: "Any argument that doesn't fit in 8 bytes, or is
  // not 1, 2, 4, or 8 bytes, must be passed by reference."
  uint64_t Width = getContext().getTypeSize(Ty);
  bool IsIndirect = Width > 64 || !llvm::isPowerOf2_64(Width);

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, IsIndirect,
                          CGF.getContext().getTypeInfoInChars(Ty),
                          CharUnits::fromQuantity(8),
                          /*allowHigherAlign*/ false, Slot);
}

std::unique_ptr<TargetCodeGenInfo> CodeGen::createX86_32TargetCodeGenInfo(
    CodeGenModule &CGM, bool DarwinVectorABI, bool Win32StructABI,
    unsigned NumRegisterParameters, bool SoftFloatABI) {
  bool RetSmallStructInRegABI = X86_32TargetCodeGenInfo::isStructReturnInRegABI(
      CGM.getTriple(), CGM.getCodeGenOpts());
  return std::make_unique<X86_32TargetCodeGenInfo>(
      CGM.getTypes(), DarwinVectorABI, RetSmallStructInRegABI, Win32StructABI,
      NumRegisterParameters, SoftFloatABI);
}

std::unique_ptr<TargetCodeGenInfo> CodeGen::createWinX86_32TargetCodeGenInfo(
    CodeGenModule &CGM, bool DarwinVectorABI, bool Win32StructABI,
    unsigned NumRegisterParameters) {
  bool RetSmallStructInRegABI = X86_32TargetCodeGenInfo::isStructReturnInRegABI(
      CGM.getTriple(), CGM.getCodeGenOpts());
  return std::make_unique<WinX86_32TargetCodeGenInfo>(
      CGM.getTypes(), DarwinVectorABI, RetSmallStructInRegABI, Win32StructABI,
      NumRegisterParameters);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createX86_64TargetCodeGenInfo(CodeGenModule &CGM,
                                       X86AVXABILevel AVXLevel) {
  return std::make_unique<X86_64TargetCodeGenInfo>(CGM.getTypes(), AVXLevel);
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createWinX86_64TargetCodeGenInfo(CodeGenModule &CGM,
                                          X86AVXABILevel AVXLevel) {
  return std::make_unique<WinX86_64TargetCodeGenInfo>(CGM.getTypes(), AVXLevel);
}
