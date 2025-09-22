//===--- CGNonTrivialStruct.cpp - Emit Special Functions for C Structs ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions to generate various special functions for C
// structs.
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "clang/AST/NonTrivialTypeVisitor.h"
#include "clang/CodeGen/CodeGenABITypes.h"
#include "llvm/Support/ScopedPrinter.h"
#include <array>

using namespace clang;
using namespace CodeGen;

// Return the size of a field in number of bits.
static uint64_t getFieldSize(const FieldDecl *FD, QualType FT,
                             ASTContext &Ctx) {
  if (FD && FD->isBitField())
    return FD->getBitWidthValue(Ctx);
  return Ctx.getTypeSize(FT);
}

namespace {
enum { DstIdx = 0, SrcIdx = 1 };
const char *ValNameStr[2] = {"dst", "src"};

template <class Derived> struct StructVisitor {
  StructVisitor(ASTContext &Ctx) : Ctx(Ctx) {}

  template <class... Ts>
  void visitStructFields(QualType QT, CharUnits CurStructOffset, Ts... Args) {
    const RecordDecl *RD = QT->castAs<RecordType>()->getDecl();

    // Iterate over the fields of the struct.
    for (const FieldDecl *FD : RD->fields()) {
      QualType FT = FD->getType();
      FT = QT.isVolatileQualified() ? FT.withVolatile() : FT;
      asDerived().visit(FT, FD, CurStructOffset, Args...);
    }

    asDerived().flushTrivialFields(Args...);
  }

  template <class... Ts> void visitTrivial(Ts... Args) {}

  template <class... Ts> void visitCXXDestructor(Ts... Args) {
    llvm_unreachable("field of a C++ struct type is not expected");
  }

  template <class... Ts> void flushTrivialFields(Ts... Args) {}

  uint64_t getFieldOffsetInBits(const FieldDecl *FD) {
    return FD ? Ctx.getASTRecordLayout(FD->getParent())
                    .getFieldOffset(FD->getFieldIndex())
              : 0;
  }

  CharUnits getFieldOffset(const FieldDecl *FD) {
    return Ctx.toCharUnitsFromBits(getFieldOffsetInBits(FD));
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }

  ASTContext &getContext() { return Ctx; }
  ASTContext &Ctx;
};

template <class Derived, bool IsMove>
struct CopyStructVisitor : StructVisitor<Derived>,
                           CopiedTypeVisitor<Derived, IsMove> {
  using StructVisitor<Derived>::asDerived;
  using Super = CopiedTypeVisitor<Derived, IsMove>;

  CopyStructVisitor(ASTContext &Ctx) : StructVisitor<Derived>(Ctx) {}

  template <class... Ts>
  void preVisit(QualType::PrimitiveCopyKind PCK, QualType FT,
                const FieldDecl *FD, CharUnits CurStructOffset, Ts &&... Args) {
    if (PCK)
      asDerived().flushTrivialFields(std::forward<Ts>(Args)...);
  }

  template <class... Ts>
  void visitWithKind(QualType::PrimitiveCopyKind PCK, QualType FT,
                     const FieldDecl *FD, CharUnits CurStructOffset,
                     Ts &&... Args) {
    if (const auto *AT = asDerived().getContext().getAsArrayType(FT)) {
      asDerived().visitArray(PCK, AT, FT.isVolatileQualified(), FD,
                             CurStructOffset, std::forward<Ts>(Args)...);
      return;
    }

    Super::visitWithKind(PCK, FT, FD, CurStructOffset,
                         std::forward<Ts>(Args)...);
  }

  template <class... Ts>
  void visitTrivial(QualType FT, const FieldDecl *FD, CharUnits CurStructOffset,
                    Ts... Args) {
    assert(!FT.isVolatileQualified() && "volatile field not expected");
    ASTContext &Ctx = asDerived().getContext();
    uint64_t FieldSize = getFieldSize(FD, FT, Ctx);

    // Ignore zero-sized fields.
    if (FieldSize == 0)
      return;

    uint64_t FStartInBits = asDerived().getFieldOffsetInBits(FD);
    uint64_t FEndInBits = FStartInBits + FieldSize;
    uint64_t RoundedFEnd = llvm::alignTo(FEndInBits, Ctx.getCharWidth());

    // Set Start if this is the first field of a sequence of trivial fields.
    if (Start == End)
      Start = CurStructOffset + Ctx.toCharUnitsFromBits(FStartInBits);
    End = CurStructOffset + Ctx.toCharUnitsFromBits(RoundedFEnd);
  }

  CharUnits Start = CharUnits::Zero(), End = CharUnits::Zero();
};

// This function creates the mangled name of a special function of a non-trivial
// C struct. Since there is no ODR in C, the function is mangled based on the
// struct contents and not the name. The mangled name has the following
// structure:
//
// <function-name> ::= <prefix> <alignment-info> "_" <struct-field-info>
// <prefix> ::= "__destructor_" | "__default_constructor_" |
//              "__copy_constructor_" | "__move_constructor_" |
//              "__copy_assignment_" | "__move_assignment_"
// <alignment-info> ::= <dst-alignment> ["_" <src-alignment>]
// <struct-field-info> ::= <field-info>+
// <field-info> ::= <struct-or-scalar-field-info> | <array-field-info>
// <struct-or-scalar-field-info> ::= "_S" <struct-field-info> |
//                                   <strong-field-info> | <trivial-field-info>
// <array-field-info> ::= "_AB" <array-offset> "s" <element-size> "n"
//                        <num-elements> <innermost-element-info> "_AE"
// <innermost-element-info> ::= <struct-or-scalar-field-info>
// <strong-field-info> ::= "_s" ["b"] ["v"] <field-offset>
// <trivial-field-info> ::= "_t" ["v"] <field-offset> "_" <field-size>

template <class Derived> struct GenFuncNameBase {
  std::string getVolatileOffsetStr(bool IsVolatile, CharUnits Offset) {
    std::string S;
    if (IsVolatile)
      S = "v";
    S += llvm::to_string(Offset.getQuantity());
    return S;
  }

  void visitARCStrong(QualType FT, const FieldDecl *FD,
                      CharUnits CurStructOffset) {
    appendStr("_s");
    if (FT->isBlockPointerType())
      appendStr("b");
    CharUnits FieldOffset = CurStructOffset + asDerived().getFieldOffset(FD);
    appendStr(getVolatileOffsetStr(FT.isVolatileQualified(), FieldOffset));
  }

  void visitARCWeak(QualType FT, const FieldDecl *FD,
                    CharUnits CurStructOffset) {
    appendStr("_w");
    CharUnits FieldOffset = CurStructOffset + asDerived().getFieldOffset(FD);
    appendStr(getVolatileOffsetStr(FT.isVolatileQualified(), FieldOffset));
  }

  void visitStruct(QualType QT, const FieldDecl *FD,
                   CharUnits CurStructOffset) {
    CharUnits FieldOffset = CurStructOffset + asDerived().getFieldOffset(FD);
    appendStr("_S");
    asDerived().visitStructFields(QT, FieldOffset);
  }

  template <class FieldKind>
  void visitArray(FieldKind FK, const ArrayType *AT, bool IsVolatile,
                  const FieldDecl *FD, CharUnits CurStructOffset) {
    // String for non-volatile trivial fields is emitted when
    // flushTrivialFields is called.
    if (!FK)
      return asDerived().visitTrivial(QualType(AT, 0), FD, CurStructOffset);

    asDerived().flushTrivialFields();
    CharUnits FieldOffset = CurStructOffset + asDerived().getFieldOffset(FD);
    ASTContext &Ctx = asDerived().getContext();
    const ConstantArrayType *CAT = cast<ConstantArrayType>(AT);
    unsigned NumElts = Ctx.getConstantArrayElementCount(CAT);
    QualType EltTy = Ctx.getBaseElementType(CAT);
    CharUnits EltSize = Ctx.getTypeSizeInChars(EltTy);
    appendStr("_AB" + llvm::to_string(FieldOffset.getQuantity()) + "s" +
              llvm::to_string(EltSize.getQuantity()) + "n" +
              llvm::to_string(NumElts));
    EltTy = IsVolatile ? EltTy.withVolatile() : EltTy;
    asDerived().visitWithKind(FK, EltTy, nullptr, FieldOffset);
    appendStr("_AE");
  }

  void appendStr(StringRef Str) { Name += Str; }

  std::string getName(QualType QT, bool IsVolatile) {
    QT = IsVolatile ? QT.withVolatile() : QT;
    asDerived().visitStructFields(QT, CharUnits::Zero());
    return Name;
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }

  std::string Name;
};

template <class Derived>
struct GenUnaryFuncName : StructVisitor<Derived>, GenFuncNameBase<Derived> {
  GenUnaryFuncName(StringRef Prefix, CharUnits DstAlignment, ASTContext &Ctx)
      : StructVisitor<Derived>(Ctx) {
    this->appendStr(Prefix);
    this->appendStr(llvm::to_string(DstAlignment.getQuantity()));
  }
};

// Helper function to create a null constant.
static llvm::Constant *getNullForVariable(Address Addr) {
  llvm::Type *Ty = Addr.getElementType();
  return llvm::ConstantPointerNull::get(cast<llvm::PointerType>(Ty));
}

template <bool IsMove>
struct GenBinaryFuncName : CopyStructVisitor<GenBinaryFuncName<IsMove>, IsMove>,
                           GenFuncNameBase<GenBinaryFuncName<IsMove>> {

  GenBinaryFuncName(StringRef Prefix, CharUnits DstAlignment,
                    CharUnits SrcAlignment, ASTContext &Ctx)
      : CopyStructVisitor<GenBinaryFuncName<IsMove>, IsMove>(Ctx) {
    this->appendStr(Prefix);
    this->appendStr(llvm::to_string(DstAlignment.getQuantity()));
    this->appendStr("_" + llvm::to_string(SrcAlignment.getQuantity()));
  }

  void flushTrivialFields() {
    if (this->Start == this->End)
      return;

    this->appendStr("_t" + llvm::to_string(this->Start.getQuantity()) + "w" +
                    llvm::to_string((this->End - this->Start).getQuantity()));

    this->Start = this->End = CharUnits::Zero();
  }

  void visitVolatileTrivial(QualType FT, const FieldDecl *FD,
                            CharUnits CurStructOffset) {
    // Zero-length bit-fields don't need to be copied/assigned.
    if (FD && FD->isZeroLengthBitField(this->Ctx))
      return;

    // Because volatile fields can be bit-fields and are individually copied,
    // their offset and width are in bits.
    uint64_t OffsetInBits =
        this->Ctx.toBits(CurStructOffset) + this->getFieldOffsetInBits(FD);
    this->appendStr("_tv" + llvm::to_string(OffsetInBits) + "w" +
                    llvm::to_string(getFieldSize(FD, FT, this->Ctx)));
  }
};

struct GenDefaultInitializeFuncName
    : GenUnaryFuncName<GenDefaultInitializeFuncName>,
      DefaultInitializedTypeVisitor<GenDefaultInitializeFuncName> {
  using Super = DefaultInitializedTypeVisitor<GenDefaultInitializeFuncName>;
  GenDefaultInitializeFuncName(CharUnits DstAlignment, ASTContext &Ctx)
      : GenUnaryFuncName<GenDefaultInitializeFuncName>("__default_constructor_",
                                                       DstAlignment, Ctx) {}
  void visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK, QualType FT,
                     const FieldDecl *FD, CharUnits CurStructOffset) {
    if (const auto *AT = getContext().getAsArrayType(FT)) {
      visitArray(PDIK, AT, FT.isVolatileQualified(), FD, CurStructOffset);
      return;
    }

    Super::visitWithKind(PDIK, FT, FD, CurStructOffset);
  }
};

struct GenDestructorFuncName : GenUnaryFuncName<GenDestructorFuncName>,
                               DestructedTypeVisitor<GenDestructorFuncName> {
  using Super = DestructedTypeVisitor<GenDestructorFuncName>;
  GenDestructorFuncName(const char *Prefix, CharUnits DstAlignment,
                        ASTContext &Ctx)
      : GenUnaryFuncName<GenDestructorFuncName>(Prefix, DstAlignment, Ctx) {}
  void visitWithKind(QualType::DestructionKind DK, QualType FT,
                     const FieldDecl *FD, CharUnits CurStructOffset) {
    if (const auto *AT = getContext().getAsArrayType(FT)) {
      visitArray(DK, AT, FT.isVolatileQualified(), FD, CurStructOffset);
      return;
    }

    Super::visitWithKind(DK, FT, FD, CurStructOffset);
  }
};

// Helper function that creates CGFunctionInfo for an N-ary special function.
template <size_t N>
static const CGFunctionInfo &getFunctionInfo(CodeGenModule &CGM,
                                             FunctionArgList &Args) {
  ASTContext &Ctx = CGM.getContext();
  llvm::SmallVector<ImplicitParamDecl *, N> Params;
  QualType ParamTy = Ctx.getPointerType(Ctx.VoidPtrTy);

  for (unsigned I = 0; I < N; ++I)
    Params.push_back(ImplicitParamDecl::Create(
        Ctx, nullptr, SourceLocation(), &Ctx.Idents.get(ValNameStr[I]), ParamTy,
        ImplicitParamKind::Other));

  llvm::append_range(Args, Params);

  return CGM.getTypes().arrangeBuiltinFunctionDeclaration(Ctx.VoidTy, Args);
}

template <size_t N, size_t... Ints>
static std::array<Address, N> getParamAddrs(std::index_sequence<Ints...> IntSeq,
                                            std::array<CharUnits, N> Alignments,
                                            const FunctionArgList &Args,
                                            CodeGenFunction *CGF) {
  return std::array<Address, N>{
      {Address(CGF->Builder.CreateLoad(CGF->GetAddrOfLocalVar(Args[Ints])),
               CGF->VoidPtrTy, Alignments[Ints], KnownNonNull)...}};
}

// Template classes that are used as bases for classes that emit special
// functions.
template <class Derived> struct GenFuncBase {
  template <size_t N>
  void visitStruct(QualType FT, const FieldDecl *FD, CharUnits CurStructOffset,
                   std::array<Address, N> Addrs) {
    this->asDerived().callSpecialFunction(
        FT, CurStructOffset + asDerived().getFieldOffset(FD), Addrs);
  }

  template <class FieldKind, size_t N>
  void visitArray(FieldKind FK, const ArrayType *AT, bool IsVolatile,
                  const FieldDecl *FD, CharUnits CurStructOffset,
                  std::array<Address, N> Addrs) {
    // Non-volatile trivial fields are copied when flushTrivialFields is called.
    if (!FK)
      return asDerived().visitTrivial(QualType(AT, 0), FD, CurStructOffset,
                                      Addrs);

    asDerived().flushTrivialFields(Addrs);
    CodeGenFunction &CGF = *this->CGF;
    ASTContext &Ctx = CGF.getContext();

    // Compute the end address.
    QualType BaseEltQT;
    std::array<Address, N> StartAddrs = Addrs;
    for (unsigned I = 0; I < N; ++I)
      StartAddrs[I] = getAddrWithOffset(Addrs[I], CurStructOffset, FD);
    Address DstAddr = StartAddrs[DstIdx];
    llvm::Value *NumElts = CGF.emitArrayLength(AT, BaseEltQT, DstAddr);
    unsigned BaseEltSize = Ctx.getTypeSizeInChars(BaseEltQT).getQuantity();
    llvm::Value *BaseEltSizeVal =
        llvm::ConstantInt::get(NumElts->getType(), BaseEltSize);
    llvm::Value *SizeInBytes =
        CGF.Builder.CreateNUWMul(BaseEltSizeVal, NumElts);
    llvm::Value *DstArrayEnd = CGF.Builder.CreateInBoundsGEP(
        CGF.Int8Ty, DstAddr.emitRawPointer(CGF), SizeInBytes);
    llvm::BasicBlock *PreheaderBB = CGF.Builder.GetInsertBlock();

    // Create the header block and insert the phi instructions.
    llvm::BasicBlock *HeaderBB = CGF.createBasicBlock("loop.header");
    CGF.EmitBlock(HeaderBB);
    llvm::PHINode *PHIs[N];

    for (unsigned I = 0; I < N; ++I) {
      PHIs[I] = CGF.Builder.CreatePHI(CGF.CGM.Int8PtrPtrTy, 2, "addr.cur");
      PHIs[I]->addIncoming(StartAddrs[I].emitRawPointer(CGF), PreheaderBB);
    }

    // Create the exit and loop body blocks.
    llvm::BasicBlock *ExitBB = CGF.createBasicBlock("loop.exit");
    llvm::BasicBlock *LoopBB = CGF.createBasicBlock("loop.body");

    // Emit the comparison and conditional branch instruction that jumps to
    // either the exit or the loop body.
    llvm::Value *Done =
        CGF.Builder.CreateICmpEQ(PHIs[DstIdx], DstArrayEnd, "done");
    CGF.Builder.CreateCondBr(Done, ExitBB, LoopBB);

    // Visit the element of the array in the loop body.
    CGF.EmitBlock(LoopBB);
    QualType EltQT = AT->getElementType();
    CharUnits EltSize = Ctx.getTypeSizeInChars(EltQT);
    std::array<Address, N> NewAddrs = Addrs;

    for (unsigned I = 0; I < N; ++I)
      NewAddrs[I] =
            Address(PHIs[I], CGF.Int8PtrTy,
                    StartAddrs[I].getAlignment().alignmentAtOffset(EltSize));

    EltQT = IsVolatile ? EltQT.withVolatile() : EltQT;
    this->asDerived().visitWithKind(FK, EltQT, nullptr, CharUnits::Zero(),
                                    NewAddrs);

    LoopBB = CGF.Builder.GetInsertBlock();

    for (unsigned I = 0; I < N; ++I) {
      // Instrs to update the destination and source addresses.
      // Update phi instructions.
      NewAddrs[I] = getAddrWithOffset(NewAddrs[I], EltSize);
      PHIs[I]->addIncoming(NewAddrs[I].emitRawPointer(CGF), LoopBB);
    }

    // Insert an unconditional branch to the header block.
    CGF.Builder.CreateBr(HeaderBB);
    CGF.EmitBlock(ExitBB);
  }

  /// Return an address with the specified offset from the passed address.
  Address getAddrWithOffset(Address Addr, CharUnits Offset) {
    assert(Addr.isValid() && "invalid address");
    if (Offset.getQuantity() == 0)
      return Addr;
    Addr = Addr.withElementType(CGF->CGM.Int8Ty);
    Addr = CGF->Builder.CreateConstInBoundsGEP(Addr, Offset.getQuantity());
    return Addr.withElementType(CGF->CGM.Int8PtrTy);
  }

  Address getAddrWithOffset(Address Addr, CharUnits StructFieldOffset,
                            const FieldDecl *FD) {
    return getAddrWithOffset(Addr, StructFieldOffset +
                                       asDerived().getFieldOffset(FD));
  }

  template <size_t N>
  llvm::Function *getFunction(StringRef FuncName, QualType QT,
                              std::array<CharUnits, N> Alignments,
                              CodeGenModule &CGM) {
    // If the special function already exists in the module, return it.
    if (llvm::Function *F = CGM.getModule().getFunction(FuncName)) {
      bool WrongType = false;
      if (!F->getReturnType()->isVoidTy())
        WrongType = true;
      else {
        for (const llvm::Argument &Arg : F->args())
          if (Arg.getType() != CGM.Int8PtrPtrTy)
            WrongType = true;
      }

      if (WrongType) {
        std::string FuncName = std::string(F->getName());
        SourceLocation Loc = QT->castAs<RecordType>()->getDecl()->getLocation();
        CGM.Error(Loc, "special function " + FuncName +
                           " for non-trivial C struct has incorrect type");
        return nullptr;
      }
      return F;
    }

    ASTContext &Ctx = CGM.getContext();
    FunctionArgList Args;
    const CGFunctionInfo &FI = getFunctionInfo<N>(CGM, Args);
    llvm::FunctionType *FuncTy = CGM.getTypes().GetFunctionType(FI);
    llvm::Function *F =
        llvm::Function::Create(FuncTy, llvm::GlobalValue::LinkOnceODRLinkage,
                               FuncName, &CGM.getModule());
    F->setVisibility(llvm::GlobalValue::HiddenVisibility);
    CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, F, /*IsThunk=*/false);
    CGM.SetLLVMFunctionAttributesForDefinition(nullptr, F);
    CodeGenFunction NewCGF(CGM);
    setCGF(&NewCGF);
    CGF->StartFunction(GlobalDecl(), Ctx.VoidTy, F, FI, Args);
    auto AL = ApplyDebugLocation::CreateArtificial(*CGF);
    std::array<Address, N> Addrs =
        getParamAddrs<N>(std::make_index_sequence<N>{}, Alignments, Args, CGF);
    asDerived().visitStructFields(QT, CharUnits::Zero(), Addrs);
    CGF->FinishFunction();
    return F;
  }

  template <size_t N>
  void callFunc(StringRef FuncName, QualType QT, std::array<Address, N> Addrs,
                CodeGenFunction &CallerCGF) {
    std::array<CharUnits, N> Alignments;
    llvm::Value *Ptrs[N];

    for (unsigned I = 0; I < N; ++I) {
      Alignments[I] = Addrs[I].getAlignment();
      Ptrs[I] = Addrs[I].emitRawPointer(CallerCGF);
    }

    if (llvm::Function *F =
            getFunction(FuncName, QT, Alignments, CallerCGF.CGM))
      CallerCGF.EmitNounwindRuntimeCall(F, Ptrs);
  }

  Derived &asDerived() { return static_cast<Derived &>(*this); }

  void setCGF(CodeGenFunction *F) { CGF = F; }

  CodeGenFunction *CGF = nullptr;
};

template <class Derived, bool IsMove>
struct GenBinaryFunc : CopyStructVisitor<Derived, IsMove>,
                       GenFuncBase<Derived> {
  GenBinaryFunc(ASTContext &Ctx) : CopyStructVisitor<Derived, IsMove>(Ctx) {}

  void flushTrivialFields(std::array<Address, 2> Addrs) {
    CharUnits Size = this->End - this->Start;

    if (Size.getQuantity() == 0)
      return;

    Address DstAddr = this->getAddrWithOffset(Addrs[DstIdx], this->Start);
    Address SrcAddr = this->getAddrWithOffset(Addrs[SrcIdx], this->Start);

    // Emit memcpy.
    if (Size.getQuantity() >= 16 ||
        !llvm::has_single_bit<uint32_t>(Size.getQuantity())) {
      llvm::Value *SizeVal =
          llvm::ConstantInt::get(this->CGF->SizeTy, Size.getQuantity());
      DstAddr = DstAddr.withElementType(this->CGF->Int8Ty);
      SrcAddr = SrcAddr.withElementType(this->CGF->Int8Ty);
      this->CGF->Builder.CreateMemCpy(DstAddr, SrcAddr, SizeVal, false);
    } else {
      llvm::Type *Ty = llvm::Type::getIntNTy(
          this->CGF->getLLVMContext(),
          Size.getQuantity() * this->CGF->getContext().getCharWidth());
      DstAddr = DstAddr.withElementType(Ty);
      SrcAddr = SrcAddr.withElementType(Ty);
      llvm::Value *SrcVal = this->CGF->Builder.CreateLoad(SrcAddr, false);
      this->CGF->Builder.CreateStore(SrcVal, DstAddr, false);
    }

    this->Start = this->End = CharUnits::Zero();
  }

  template <class... Ts>
  void visitVolatileTrivial(QualType FT, const FieldDecl *FD, CharUnits Offset,
                            std::array<Address, 2> Addrs) {
    LValue DstLV, SrcLV;
    if (FD) {
      // No need to copy zero-length bit-fields.
      if (FD->isZeroLengthBitField(this->CGF->getContext()))
        return;

      QualType RT = QualType(FD->getParent()->getTypeForDecl(), 0);
      llvm::Type *Ty = this->CGF->ConvertType(RT);
      Address DstAddr = this->getAddrWithOffset(Addrs[DstIdx], Offset);
      LValue DstBase =
          this->CGF->MakeAddrLValue(DstAddr.withElementType(Ty), FT);
      DstLV = this->CGF->EmitLValueForField(DstBase, FD);
      Address SrcAddr = this->getAddrWithOffset(Addrs[SrcIdx], Offset);
      LValue SrcBase =
          this->CGF->MakeAddrLValue(SrcAddr.withElementType(Ty), FT);
      SrcLV = this->CGF->EmitLValueForField(SrcBase, FD);
    } else {
      llvm::Type *Ty = this->CGF->ConvertTypeForMem(FT);
      Address DstAddr = Addrs[DstIdx].withElementType(Ty);
      Address SrcAddr = Addrs[SrcIdx].withElementType(Ty);
      DstLV = this->CGF->MakeAddrLValue(DstAddr, FT);
      SrcLV = this->CGF->MakeAddrLValue(SrcAddr, FT);
    }
    RValue SrcVal = this->CGF->EmitLoadOfLValue(SrcLV, SourceLocation());
    this->CGF->EmitStoreThroughLValue(SrcVal, DstLV);
  }
};

// These classes that emit the special functions for a non-trivial struct.
struct GenDestructor : StructVisitor<GenDestructor>,
                       GenFuncBase<GenDestructor>,
                       DestructedTypeVisitor<GenDestructor> {
  using Super = DestructedTypeVisitor<GenDestructor>;
  GenDestructor(ASTContext &Ctx) : StructVisitor<GenDestructor>(Ctx) {}

  void visitWithKind(QualType::DestructionKind DK, QualType FT,
                     const FieldDecl *FD, CharUnits CurStructOffset,
                     std::array<Address, 1> Addrs) {
    if (const auto *AT = getContext().getAsArrayType(FT)) {
      visitArray(DK, AT, FT.isVolatileQualified(), FD, CurStructOffset, Addrs);
      return;
    }

    Super::visitWithKind(DK, FT, FD, CurStructOffset, Addrs);
  }

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 1> Addrs) {
    CGF->destroyARCStrongImprecise(
        *CGF, getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD), QT);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 1> Addrs) {
    CGF->destroyARCWeak(
        *CGF, getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD), QT);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 1> Addrs) {
    CGF->callCStructDestructor(
        CGF->MakeAddrLValue(getAddrWithOffset(Addrs[DstIdx], Offset), FT));
  }
};

struct GenDefaultInitialize
    : StructVisitor<GenDefaultInitialize>,
      GenFuncBase<GenDefaultInitialize>,
      DefaultInitializedTypeVisitor<GenDefaultInitialize> {
  using Super = DefaultInitializedTypeVisitor<GenDefaultInitialize>;
  typedef GenFuncBase<GenDefaultInitialize> GenFuncBaseTy;

  GenDefaultInitialize(ASTContext &Ctx)
      : StructVisitor<GenDefaultInitialize>(Ctx) {}

  void visitWithKind(QualType::PrimitiveDefaultInitializeKind PDIK, QualType FT,
                     const FieldDecl *FD, CharUnits CurStructOffset,
                     std::array<Address, 1> Addrs) {
    if (const auto *AT = getContext().getAsArrayType(FT)) {
      visitArray(PDIK, AT, FT.isVolatileQualified(), FD, CurStructOffset,
                 Addrs);
      return;
    }

    Super::visitWithKind(PDIK, FT, FD, CurStructOffset, Addrs);
  }

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 1> Addrs) {
    CGF->EmitNullInitialization(
        getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD), QT);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 1> Addrs) {
    CGF->EmitNullInitialization(
        getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD), QT);
  }

  template <class FieldKind, size_t... Is>
  void visitArray(FieldKind FK, const ArrayType *AT, bool IsVolatile,
                  const FieldDecl *FD, CharUnits CurStructOffset,
                  std::array<Address, 1> Addrs) {
    if (!FK)
      return visitTrivial(QualType(AT, 0), FD, CurStructOffset, Addrs);

    ASTContext &Ctx = getContext();
    CharUnits Size = Ctx.getTypeSizeInChars(QualType(AT, 0));
    QualType EltTy = Ctx.getBaseElementType(QualType(AT, 0));

    if (Size < CharUnits::fromQuantity(16) || EltTy->getAs<RecordType>()) {
      GenFuncBaseTy::visitArray(FK, AT, IsVolatile, FD, CurStructOffset, Addrs);
      return;
    }

    llvm::Constant *SizeVal = CGF->Builder.getInt64(Size.getQuantity());
    Address DstAddr = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Address Loc = DstAddr.withElementType(CGF->Int8Ty);
    CGF->Builder.CreateMemSet(Loc, CGF->Builder.getInt8(0), SizeVal,
                              IsVolatile);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 1> Addrs) {
    CGF->callCStructDefaultConstructor(
        CGF->MakeAddrLValue(getAddrWithOffset(Addrs[DstIdx], Offset), FT));
  }
};

struct GenCopyConstructor : GenBinaryFunc<GenCopyConstructor, false> {
  GenCopyConstructor(ASTContext &Ctx)
      : GenBinaryFunc<GenCopyConstructor, false>(Ctx) {}

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    llvm::Value *SrcVal = CGF->EmitLoadOfScalar(
        Addrs[SrcIdx], QT.isVolatileQualified(), QT, SourceLocation());
    llvm::Value *Val = CGF->EmitARCRetain(QT, SrcVal);
    CGF->EmitStoreOfScalar(Val, CGF->MakeAddrLValue(Addrs[DstIdx], QT), true);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    CGF->EmitARCCopyWeak(Addrs[DstIdx], Addrs[SrcIdx]);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], Offset);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], Offset);
    CGF->callCStructCopyConstructor(CGF->MakeAddrLValue(Addrs[DstIdx], FT),
                                    CGF->MakeAddrLValue(Addrs[SrcIdx], FT));
  }
};

struct GenMoveConstructor : GenBinaryFunc<GenMoveConstructor, true> {
  GenMoveConstructor(ASTContext &Ctx)
      : GenBinaryFunc<GenMoveConstructor, true>(Ctx) {}

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    LValue SrcLV = CGF->MakeAddrLValue(Addrs[SrcIdx], QT);
    llvm::Value *SrcVal =
        CGF->EmitLoadOfLValue(SrcLV, SourceLocation()).getScalarVal();
    CGF->EmitStoreOfScalar(getNullForVariable(SrcLV.getAddress()), SrcLV);
    CGF->EmitStoreOfScalar(SrcVal, CGF->MakeAddrLValue(Addrs[DstIdx], QT),
                           /* isInitialization */ true);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    CGF->EmitARCMoveWeak(Addrs[DstIdx], Addrs[SrcIdx]);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], Offset);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], Offset);
    CGF->callCStructMoveConstructor(CGF->MakeAddrLValue(Addrs[DstIdx], FT),
                                    CGF->MakeAddrLValue(Addrs[SrcIdx], FT));
  }
};

struct GenCopyAssignment : GenBinaryFunc<GenCopyAssignment, false> {
  GenCopyAssignment(ASTContext &Ctx)
      : GenBinaryFunc<GenCopyAssignment, false>(Ctx) {}

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    llvm::Value *SrcVal = CGF->EmitLoadOfScalar(
        Addrs[SrcIdx], QT.isVolatileQualified(), QT, SourceLocation());
    CGF->EmitARCStoreStrong(CGF->MakeAddrLValue(Addrs[DstIdx], QT), SrcVal,
                            false);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    CGF->emitARCCopyAssignWeak(QT, Addrs[DstIdx], Addrs[SrcIdx]);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], Offset);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], Offset);
    CGF->callCStructCopyAssignmentOperator(
        CGF->MakeAddrLValue(Addrs[DstIdx], FT),
        CGF->MakeAddrLValue(Addrs[SrcIdx], FT));
  }
};

struct GenMoveAssignment : GenBinaryFunc<GenMoveAssignment, true> {
  GenMoveAssignment(ASTContext &Ctx)
      : GenBinaryFunc<GenMoveAssignment, true>(Ctx) {}

  void visitARCStrong(QualType QT, const FieldDecl *FD,
                      CharUnits CurStructOffset, std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    LValue SrcLV = CGF->MakeAddrLValue(Addrs[SrcIdx], QT);
    llvm::Value *SrcVal =
        CGF->EmitLoadOfLValue(SrcLV, SourceLocation()).getScalarVal();
    CGF->EmitStoreOfScalar(getNullForVariable(SrcLV.getAddress()), SrcLV);
    LValue DstLV = CGF->MakeAddrLValue(Addrs[DstIdx], QT);
    llvm::Value *DstVal =
        CGF->EmitLoadOfLValue(DstLV, SourceLocation()).getScalarVal();
    CGF->EmitStoreOfScalar(SrcVal, DstLV);
    CGF->EmitARCRelease(DstVal, ARCImpreciseLifetime);
  }

  void visitARCWeak(QualType QT, const FieldDecl *FD, CharUnits CurStructOffset,
                    std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], CurStructOffset, FD);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], CurStructOffset, FD);
    CGF->emitARCMoveAssignWeak(QT, Addrs[DstIdx], Addrs[SrcIdx]);
  }

  void callSpecialFunction(QualType FT, CharUnits Offset,
                           std::array<Address, 2> Addrs) {
    Addrs[DstIdx] = getAddrWithOffset(Addrs[DstIdx], Offset);
    Addrs[SrcIdx] = getAddrWithOffset(Addrs[SrcIdx], Offset);
    CGF->callCStructMoveAssignmentOperator(
        CGF->MakeAddrLValue(Addrs[DstIdx], FT),
        CGF->MakeAddrLValue(Addrs[SrcIdx], FT));
  }
};

} // namespace

void CodeGenFunction::destroyNonTrivialCStruct(CodeGenFunction &CGF,
                                               Address Addr, QualType Type) {
  CGF.callCStructDestructor(CGF.MakeAddrLValue(Addr, Type));
}

// Default-initialize a variable that is a non-trivial struct or an array of
// such structure.
void CodeGenFunction::defaultInitNonTrivialCStructVar(LValue Dst) {
  GenDefaultInitialize Gen(getContext());
  Address DstPtr = Dst.getAddress().withElementType(CGM.Int8PtrTy);
  Gen.setCGF(this);
  QualType QT = Dst.getType();
  QT = Dst.isVolatile() ? QT.withVolatile() : QT;
  Gen.visit(QT, nullptr, CharUnits::Zero(), std::array<Address, 1>({{DstPtr}}));
}

template <class G, size_t N>
static void callSpecialFunction(G &&Gen, StringRef FuncName, QualType QT,
                                bool IsVolatile, CodeGenFunction &CGF,
                                std::array<Address, N> Addrs) {
  auto SetArtificialLoc = ApplyDebugLocation::CreateArtificial(CGF);
  for (unsigned I = 0; I < N; ++I)
    Addrs[I] = Addrs[I].withElementType(CGF.CGM.Int8PtrTy);
  QT = IsVolatile ? QT.withVolatile() : QT;
  Gen.callFunc(FuncName, QT, Addrs, CGF);
}

template <class G, size_t N>
static llvm::Function *
getSpecialFunction(G &&Gen, StringRef FuncName, QualType QT, bool IsVolatile,
                   std::array<CharUnits, N> Alignments, CodeGenModule &CGM) {
  QT = IsVolatile ? QT.withVolatile() : QT;
  // The following call requires an array of addresses as arguments, but doesn't
  // actually use them (it overwrites them with the addresses of the arguments
  // of the created function).
  return Gen.getFunction(FuncName, QT, Alignments, CGM);
}

// Functions to emit calls to the special functions of a non-trivial C struct.
void CodeGenFunction::callCStructDefaultConstructor(LValue Dst) {
  bool IsVolatile = Dst.isVolatile();
  Address DstPtr = Dst.getAddress();
  QualType QT = Dst.getType();
  GenDefaultInitializeFuncName GenName(DstPtr.getAlignment(), getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenDefaultInitialize(getContext()), FuncName, QT,
                      IsVolatile, *this, std::array<Address, 1>({{DstPtr}}));
}

std::string CodeGenFunction::getNonTrivialCopyConstructorStr(
    QualType QT, CharUnits Alignment, bool IsVolatile, ASTContext &Ctx) {
  GenBinaryFuncName<false> GenName("", Alignment, Alignment, Ctx);
  return GenName.getName(QT, IsVolatile);
}

std::string CodeGenFunction::getNonTrivialDestructorStr(QualType QT,
                                                        CharUnits Alignment,
                                                        bool IsVolatile,
                                                        ASTContext &Ctx) {
  GenDestructorFuncName GenName("", Alignment, Ctx);
  return GenName.getName(QT, IsVolatile);
}

void CodeGenFunction::callCStructDestructor(LValue Dst) {
  bool IsVolatile = Dst.isVolatile();
  Address DstPtr = Dst.getAddress();
  QualType QT = Dst.getType();
  GenDestructorFuncName GenName("__destructor_", DstPtr.getAlignment(),
                                getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenDestructor(getContext()), FuncName, QT, IsVolatile,
                      *this, std::array<Address, 1>({{DstPtr}}));
}

void CodeGenFunction::callCStructCopyConstructor(LValue Dst, LValue Src) {
  bool IsVolatile = Dst.isVolatile() || Src.isVolatile();
  Address DstPtr = Dst.getAddress(), SrcPtr = Src.getAddress();
  QualType QT = Dst.getType();
  GenBinaryFuncName<false> GenName("__copy_constructor_", DstPtr.getAlignment(),
                                   SrcPtr.getAlignment(), getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenCopyConstructor(getContext()), FuncName, QT,
                      IsVolatile, *this,
                      std::array<Address, 2>({{DstPtr, SrcPtr}}));
}

void CodeGenFunction::callCStructCopyAssignmentOperator(LValue Dst, LValue Src

) {
  bool IsVolatile = Dst.isVolatile() || Src.isVolatile();
  Address DstPtr = Dst.getAddress(), SrcPtr = Src.getAddress();
  QualType QT = Dst.getType();
  GenBinaryFuncName<false> GenName("__copy_assignment_", DstPtr.getAlignment(),
                                   SrcPtr.getAlignment(), getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenCopyAssignment(getContext()), FuncName, QT, IsVolatile,
                      *this, std::array<Address, 2>({{DstPtr, SrcPtr}}));
}

void CodeGenFunction::callCStructMoveConstructor(LValue Dst, LValue Src) {
  bool IsVolatile = Dst.isVolatile() || Src.isVolatile();
  Address DstPtr = Dst.getAddress(), SrcPtr = Src.getAddress();
  QualType QT = Dst.getType();
  GenBinaryFuncName<true> GenName("__move_constructor_", DstPtr.getAlignment(),
                                  SrcPtr.getAlignment(), getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenMoveConstructor(getContext()), FuncName, QT,
                      IsVolatile, *this,
                      std::array<Address, 2>({{DstPtr, SrcPtr}}));
}

void CodeGenFunction::callCStructMoveAssignmentOperator(LValue Dst, LValue Src

) {
  bool IsVolatile = Dst.isVolatile() || Src.isVolatile();
  Address DstPtr = Dst.getAddress(), SrcPtr = Src.getAddress();
  QualType QT = Dst.getType();
  GenBinaryFuncName<true> GenName("__move_assignment_", DstPtr.getAlignment(),
                                  SrcPtr.getAlignment(), getContext());
  std::string FuncName = GenName.getName(QT, IsVolatile);
  callSpecialFunction(GenMoveAssignment(getContext()), FuncName, QT, IsVolatile,
                      *this, std::array<Address, 2>({{DstPtr, SrcPtr}}));
}

llvm::Function *clang::CodeGen::getNonTrivialCStructDefaultConstructor(
    CodeGenModule &CGM, CharUnits DstAlignment, bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenDefaultInitializeFuncName GenName(DstAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(GenDefaultInitialize(Ctx), FuncName, QT, IsVolatile,
                            std::array<CharUnits, 1>({{DstAlignment}}), CGM);
}

llvm::Function *clang::CodeGen::getNonTrivialCStructCopyConstructor(
    CodeGenModule &CGM, CharUnits DstAlignment, CharUnits SrcAlignment,
    bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenBinaryFuncName<false> GenName("__copy_constructor_", DstAlignment,
                                   SrcAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(
      GenCopyConstructor(Ctx), FuncName, QT, IsVolatile,
      std::array<CharUnits, 2>({{DstAlignment, SrcAlignment}}), CGM);
}

llvm::Function *clang::CodeGen::getNonTrivialCStructMoveConstructor(
    CodeGenModule &CGM, CharUnits DstAlignment, CharUnits SrcAlignment,
    bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenBinaryFuncName<true> GenName("__move_constructor_", DstAlignment,
                                  SrcAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(
      GenMoveConstructor(Ctx), FuncName, QT, IsVolatile,
      std::array<CharUnits, 2>({{DstAlignment, SrcAlignment}}), CGM);
}

llvm::Function *clang::CodeGen::getNonTrivialCStructCopyAssignmentOperator(
    CodeGenModule &CGM, CharUnits DstAlignment, CharUnits SrcAlignment,
    bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenBinaryFuncName<false> GenName("__copy_assignment_", DstAlignment,
                                   SrcAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(
      GenCopyAssignment(Ctx), FuncName, QT, IsVolatile,
      std::array<CharUnits, 2>({{DstAlignment, SrcAlignment}}), CGM);
}

llvm::Function *clang::CodeGen::getNonTrivialCStructMoveAssignmentOperator(
    CodeGenModule &CGM, CharUnits DstAlignment, CharUnits SrcAlignment,
    bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenBinaryFuncName<true> GenName("__move_assignment_", DstAlignment,
                                  SrcAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(
      GenMoveAssignment(Ctx), FuncName, QT, IsVolatile,
      std::array<CharUnits, 2>({{DstAlignment, SrcAlignment}}), CGM);
}

llvm::Function *clang::CodeGen::getNonTrivialCStructDestructor(
    CodeGenModule &CGM, CharUnits DstAlignment, bool IsVolatile, QualType QT) {
  ASTContext &Ctx = CGM.getContext();
  GenDestructorFuncName GenName("__destructor_", DstAlignment, Ctx);
  std::string FuncName = GenName.getName(QT, IsVolatile);
  return getSpecialFunction(GenDestructor(Ctx), FuncName, QT, IsVolatile,
                            std::array<CharUnits, 1>({{DstAlignment}}), CGM);
}
