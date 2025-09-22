//===------- ItaniumCXXABI.cpp - Emit LLVM Code from ASTs for a Module ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides C++ code generation targeting the Itanium C++ ABI.  The class
// in this file generates structures that follow the Itanium C++ ABI, which is
// documented at:
//  https://itanium-cxx-abi.github.io/cxx-abi/abi.html
//  https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM ABI, documented at:
// https://developer.arm.com/documentation/ihi0041/g/
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGRecordLayout.h"
#include "CGVTables.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/Type.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/ScopedPrinter.h"

#include <optional>

using namespace clang;
using namespace CodeGen;

namespace {
class ItaniumCXXABI : public CodeGen::CGCXXABI {
  /// VTables - All the vtables which have been defined.
  llvm::DenseMap<const CXXRecordDecl *, llvm::GlobalVariable *> VTables;

  /// All the thread wrapper functions that have been used.
  llvm::SmallVector<std::pair<const VarDecl *, llvm::Function *>, 8>
      ThreadWrappers;

protected:
  bool UseARMMethodPtrABI;
  bool UseARMGuardVarABI;
  bool Use32BitVTableOffsetABI;

  ItaniumMangleContext &getMangleContext() {
    return cast<ItaniumMangleContext>(CodeGen::CGCXXABI::getMangleContext());
  }

public:
  ItaniumCXXABI(CodeGen::CodeGenModule &CGM,
                bool UseARMMethodPtrABI = false,
                bool UseARMGuardVarABI = false) :
    CGCXXABI(CGM), UseARMMethodPtrABI(UseARMMethodPtrABI),
    UseARMGuardVarABI(UseARMGuardVarABI),
    Use32BitVTableOffsetABI(false) { }

  bool classifyReturnType(CGFunctionInfo &FI) const override;

  RecordArgABI getRecordArgABI(const CXXRecordDecl *RD) const override {
    // If C++ prohibits us from making a copy, pass by address.
    if (!RD->canPassInRegisters())
      return RAA_Indirect;
    return RAA_Default;
  }

  bool isThisCompleteObject(GlobalDecl GD) const override {
    // The Itanium ABI has separate complete-object vs.  base-object
    // variants of both constructors and destructors.
    if (isa<CXXDestructorDecl>(GD.getDecl())) {
      switch (GD.getDtorType()) {
      case Dtor_Complete:
      case Dtor_Deleting:
        return true;

      case Dtor_Base:
        return false;

      case Dtor_Comdat:
        llvm_unreachable("emitting dtor comdat as function?");
      }
      llvm_unreachable("bad dtor kind");
    }
    if (isa<CXXConstructorDecl>(GD.getDecl())) {
      switch (GD.getCtorType()) {
      case Ctor_Complete:
        return true;

      case Ctor_Base:
        return false;

      case Ctor_CopyingClosure:
      case Ctor_DefaultClosure:
        llvm_unreachable("closure ctors in Itanium ABI?");

      case Ctor_Comdat:
        llvm_unreachable("emitting ctor comdat as function?");
      }
      llvm_unreachable("bad dtor kind");
    }

    // No other kinds.
    return false;
  }

  bool isZeroInitializable(const MemberPointerType *MPT) override;

  llvm::Type *ConvertMemberPointerType(const MemberPointerType *MPT) override;

  CGCallee
    EmitLoadOfMemberFunctionPointer(CodeGenFunction &CGF,
                                    const Expr *E,
                                    Address This,
                                    llvm::Value *&ThisPtrForCall,
                                    llvm::Value *MemFnPtr,
                                    const MemberPointerType *MPT) override;

  llvm::Value *
    EmitMemberDataPointerAddress(CodeGenFunction &CGF, const Expr *E,
                                 Address Base,
                                 llvm::Value *MemPtr,
                                 const MemberPointerType *MPT) override;

  llvm::Value *EmitMemberPointerConversion(CodeGenFunction &CGF,
                                           const CastExpr *E,
                                           llvm::Value *Src) override;
  llvm::Constant *EmitMemberPointerConversion(const CastExpr *E,
                                              llvm::Constant *Src) override;

  llvm::Constant *EmitNullMemberPointer(const MemberPointerType *MPT) override;

  llvm::Constant *EmitMemberFunctionPointer(const CXXMethodDecl *MD) override;
  llvm::Constant *EmitMemberDataPointer(const MemberPointerType *MPT,
                                        CharUnits offset) override;
  llvm::Constant *EmitMemberPointer(const APValue &MP, QualType MPT) override;
  llvm::Constant *BuildMemberPointer(const CXXMethodDecl *MD,
                                     CharUnits ThisAdjustment);

  llvm::Value *EmitMemberPointerComparison(CodeGenFunction &CGF,
                                           llvm::Value *L, llvm::Value *R,
                                           const MemberPointerType *MPT,
                                           bool Inequality) override;

  llvm::Value *EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                         llvm::Value *Addr,
                                         const MemberPointerType *MPT) override;

  void emitVirtualObjectDelete(CodeGenFunction &CGF, const CXXDeleteExpr *DE,
                               Address Ptr, QualType ElementType,
                               const CXXDestructorDecl *Dtor) override;

  void emitRethrow(CodeGenFunction &CGF, bool isNoReturn) override;
  void emitThrow(CodeGenFunction &CGF, const CXXThrowExpr *E) override;

  void emitBeginCatch(CodeGenFunction &CGF, const CXXCatchStmt *C) override;

  llvm::CallInst *
  emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                      llvm::Value *Exn) override;

  void EmitFundamentalRTTIDescriptors(const CXXRecordDecl *RD);
  llvm::Constant *getAddrOfRTTIDescriptor(QualType Ty) override;
  CatchTypeInfo
  getAddrOfCXXCatchHandlerType(QualType Ty,
                               QualType CatchHandlerType) override {
    return CatchTypeInfo{getAddrOfRTTIDescriptor(Ty), 0};
  }

  bool shouldTypeidBeNullChecked(QualType SrcRecordTy) override;
  void EmitBadTypeidCall(CodeGenFunction &CGF) override;
  llvm::Value *EmitTypeid(CodeGenFunction &CGF, QualType SrcRecordTy,
                          Address ThisPtr,
                          llvm::Type *StdTypeInfoPtrTy) override;

  bool shouldDynamicCastCallBeNullChecked(bool SrcIsPtr,
                                          QualType SrcRecordTy) override;

  /// Determine whether we know that all instances of type RecordTy will have
  /// the same vtable pointer values, that is distinct from all other vtable
  /// pointers. While this is required by the Itanium ABI, it doesn't happen in
  /// practice in some cases due to language extensions.
  bool hasUniqueVTablePointer(QualType RecordTy) {
    const CXXRecordDecl *RD = RecordTy->getAsCXXRecordDecl();

    // Under -fapple-kext, multiple definitions of the same vtable may be
    // emitted.
    if (!CGM.getCodeGenOpts().AssumeUniqueVTables ||
        getContext().getLangOpts().AppleKext)
      return false;

    // If the type_info* would be null, the vtable might be merged with that of
    // another type.
    if (!CGM.shouldEmitRTTI())
      return false;

    // If there's only one definition of the vtable in the program, it has a
    // unique address.
    if (!llvm::GlobalValue::isWeakForLinker(CGM.getVTableLinkage(RD)))
      return true;

    // Even if there are multiple definitions of the vtable, they are required
    // by the ABI to use the same symbol name, so should be merged at load
    // time. However, if the class has hidden visibility, there can be
    // different versions of the class in different modules, and the ABI
    // library might treat them as being the same.
    if (CGM.GetLLVMVisibility(RD->getVisibility()) !=
        llvm::GlobalValue::DefaultVisibility)
      return false;

    return true;
  }

  bool shouldEmitExactDynamicCast(QualType DestRecordTy) override {
    return hasUniqueVTablePointer(DestRecordTy);
  }

  llvm::Value *emitDynamicCastCall(CodeGenFunction &CGF, Address Value,
                                   QualType SrcRecordTy, QualType DestTy,
                                   QualType DestRecordTy,
                                   llvm::BasicBlock *CastEnd) override;

  llvm::Value *emitExactDynamicCast(CodeGenFunction &CGF, Address ThisAddr,
                                    QualType SrcRecordTy, QualType DestTy,
                                    QualType DestRecordTy,
                                    llvm::BasicBlock *CastSuccess,
                                    llvm::BasicBlock *CastFail) override;

  llvm::Value *emitDynamicCastToVoid(CodeGenFunction &CGF, Address Value,
                                     QualType SrcRecordTy) override;

  bool EmitBadCastCall(CodeGenFunction &CGF) override;

  llvm::Value *
    GetVirtualBaseClassOffset(CodeGenFunction &CGF, Address This,
                              const CXXRecordDecl *ClassDecl,
                              const CXXRecordDecl *BaseClassDecl) override;

  void EmitCXXConstructors(const CXXConstructorDecl *D) override;

  AddedStructorArgCounts
  buildStructorSignature(GlobalDecl GD,
                         SmallVectorImpl<CanQualType> &ArgTys) override;

  bool useThunkForDtorVariant(const CXXDestructorDecl *Dtor,
                              CXXDtorType DT) const override {
    // Itanium does not emit any destructor variant as an inline thunk.
    // Delegating may occur as an optimization, but all variants are either
    // emitted with external linkage or as linkonce if they are inline and used.
    return false;
  }

  void EmitCXXDestructors(const CXXDestructorDecl *D) override;

  void addImplicitStructorParams(CodeGenFunction &CGF, QualType &ResTy,
                                 FunctionArgList &Params) override;

  void EmitInstanceFunctionProlog(CodeGenFunction &CGF) override;

  AddedStructorArgs getImplicitConstructorArgs(CodeGenFunction &CGF,
                                               const CXXConstructorDecl *D,
                                               CXXCtorType Type,
                                               bool ForVirtualBase,
                                               bool Delegating) override;

  llvm::Value *getCXXDestructorImplicitParam(CodeGenFunction &CGF,
                                             const CXXDestructorDecl *DD,
                                             CXXDtorType Type,
                                             bool ForVirtualBase,
                                             bool Delegating) override;

  void EmitDestructorCall(CodeGenFunction &CGF, const CXXDestructorDecl *DD,
                          CXXDtorType Type, bool ForVirtualBase,
                          bool Delegating, Address This,
                          QualType ThisTy) override;

  void emitVTableDefinitions(CodeGenVTables &CGVT,
                             const CXXRecordDecl *RD) override;

  bool isVirtualOffsetNeededForVTableField(CodeGenFunction &CGF,
                                           CodeGenFunction::VPtr Vptr) override;

  bool doStructorsInitializeVPtrs(const CXXRecordDecl *VTableClass) override {
    return true;
  }

  llvm::Constant *
  getVTableAddressPoint(BaseSubobject Base,
                        const CXXRecordDecl *VTableClass) override;

  llvm::Value *getVTableAddressPointInStructor(
      CodeGenFunction &CGF, const CXXRecordDecl *VTableClass,
      BaseSubobject Base, const CXXRecordDecl *NearestVBase) override;

  llvm::Value *getVTableAddressPointInStructorWithVTT(
      CodeGenFunction &CGF, const CXXRecordDecl *VTableClass,
      BaseSubobject Base, const CXXRecordDecl *NearestVBase);

  llvm::GlobalVariable *getAddrOfVTable(const CXXRecordDecl *RD,
                                        CharUnits VPtrOffset) override;

  CGCallee getVirtualFunctionPointer(CodeGenFunction &CGF, GlobalDecl GD,
                                     Address This, llvm::Type *Ty,
                                     SourceLocation Loc) override;

  llvm::Value *EmitVirtualDestructorCall(CodeGenFunction &CGF,
                                         const CXXDestructorDecl *Dtor,
                                         CXXDtorType DtorType, Address This,
                                         DeleteOrMemberCallExpr E) override;

  void emitVirtualInheritanceTables(const CXXRecordDecl *RD) override;

  bool canSpeculativelyEmitVTable(const CXXRecordDecl *RD) const override;
  bool canSpeculativelyEmitVTableAsBaseClass(const CXXRecordDecl *RD) const;

  void setThunkLinkage(llvm::Function *Thunk, bool ForVTable, GlobalDecl GD,
                       bool ReturnAdjustment) override {
    // Allow inlining of thunks by emitting them with available_externally
    // linkage together with vtables when needed.
    if (ForVTable && !Thunk->hasLocalLinkage())
      Thunk->setLinkage(llvm::GlobalValue::AvailableExternallyLinkage);
    CGM.setGVProperties(Thunk, GD);
  }

  bool exportThunk() override { return true; }

  llvm::Value *performThisAdjustment(CodeGenFunction &CGF, Address This,
                                     const CXXRecordDecl *UnadjustedThisClass,
                                     const ThunkInfo &TI) override;

  llvm::Value *performReturnAdjustment(CodeGenFunction &CGF, Address Ret,
                                       const CXXRecordDecl *UnadjustedRetClass,
                                       const ReturnAdjustment &RA) override;

  size_t getSrcArgforCopyCtor(const CXXConstructorDecl *,
                              FunctionArgList &Args) const override {
    assert(!Args.empty() && "expected the arglist to not be empty!");
    return Args.size() - 1;
  }

  StringRef GetPureVirtualCallName() override { return "__cxa_pure_virtual"; }
  StringRef GetDeletedVirtualCallName() override
    { return "__cxa_deleted_virtual"; }

  CharUnits getArrayCookieSizeImpl(QualType elementType) override;
  Address InitializeArrayCookie(CodeGenFunction &CGF,
                                Address NewPtr,
                                llvm::Value *NumElements,
                                const CXXNewExpr *expr,
                                QualType ElementType) override;
  llvm::Value *readArrayCookieImpl(CodeGenFunction &CGF,
                                   Address allocPtr,
                                   CharUnits cookieSize) override;

  void EmitGuardedInit(CodeGenFunction &CGF, const VarDecl &D,
                       llvm::GlobalVariable *DeclPtr,
                       bool PerformInit) override;
  void registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                          llvm::FunctionCallee dtor,
                          llvm::Constant *addr) override;

  llvm::Function *getOrCreateThreadLocalWrapper(const VarDecl *VD,
                                                llvm::Value *Val);
  void EmitThreadLocalInitFuncs(
      CodeGenModule &CGM,
      ArrayRef<const VarDecl *> CXXThreadLocals,
      ArrayRef<llvm::Function *> CXXThreadLocalInits,
      ArrayRef<const VarDecl *> CXXThreadLocalInitVars) override;

  bool usesThreadWrapperFunction(const VarDecl *VD) const override {
    return !isEmittedWithConstantInitializer(VD) ||
           mayNeedDestruction(VD);
  }
  LValue EmitThreadLocalVarDeclLValue(CodeGenFunction &CGF, const VarDecl *VD,
                                      QualType LValType) override;

  bool NeedsVTTParameter(GlobalDecl GD) override;

  llvm::Constant *
  getOrCreateVirtualFunctionPointerThunk(const CXXMethodDecl *MD);

  /**************************** RTTI Uniqueness ******************************/

protected:
  /// Returns true if the ABI requires RTTI type_info objects to be unique
  /// across a program.
  virtual bool shouldRTTIBeUnique() const { return true; }

public:
  /// What sort of unique-RTTI behavior should we use?
  enum RTTIUniquenessKind {
    /// We are guaranteeing, or need to guarantee, that the RTTI string
    /// is unique.
    RUK_Unique,

    /// We are not guaranteeing uniqueness for the RTTI string, so we
    /// can demote to hidden visibility but must use string comparisons.
    RUK_NonUniqueHidden,

    /// We are not guaranteeing uniqueness for the RTTI string, so we
    /// have to use string comparisons, but we also have to emit it with
    /// non-hidden visibility.
    RUK_NonUniqueVisible
  };

  /// Return the required visibility status for the given type and linkage in
  /// the current ABI.
  RTTIUniquenessKind
  classifyRTTIUniqueness(QualType CanTy,
                         llvm::GlobalValue::LinkageTypes Linkage) const;
  friend class ItaniumRTTIBuilder;

  void emitCXXStructor(GlobalDecl GD) override;

  std::pair<llvm::Value *, const CXXRecordDecl *>
  LoadVTablePtr(CodeGenFunction &CGF, Address This,
                const CXXRecordDecl *RD) override;

 private:
   llvm::Constant *
   getSignedVirtualMemberFunctionPointer(const CXXMethodDecl *MD);

   bool hasAnyUnusedVirtualInlineFunction(const CXXRecordDecl *RD) const {
     const auto &VtableLayout =
         CGM.getItaniumVTableContext().getVTableLayout(RD);

     for (const auto &VtableComponent : VtableLayout.vtable_components()) {
       // Skip empty slot.
       if (!VtableComponent.isUsedFunctionPointerKind())
         continue;

       const CXXMethodDecl *Method = VtableComponent.getFunctionDecl();
       if (!Method->getCanonicalDecl()->isInlined())
         continue;

       StringRef Name = CGM.getMangledName(VtableComponent.getGlobalDecl());
       auto *Entry = CGM.GetGlobalValue(Name);
       // This checks if virtual inline function has already been emitted.
       // Note that it is possible that this inline function would be emitted
       // after trying to emit vtable speculatively. Because of this we do
       // an extra pass after emitting all deferred vtables to find and emit
       // these vtables opportunistically.
       if (!Entry || Entry->isDeclaration())
         return true;
     }
     return false;
  }

  bool isVTableHidden(const CXXRecordDecl *RD) const {
    const auto &VtableLayout =
            CGM.getItaniumVTableContext().getVTableLayout(RD);

    for (const auto &VtableComponent : VtableLayout.vtable_components()) {
      if (VtableComponent.isRTTIKind()) {
        const CXXRecordDecl *RTTIDecl = VtableComponent.getRTTIDecl();
        if (RTTIDecl->getVisibility() == Visibility::HiddenVisibility)
          return true;
      } else if (VtableComponent.isUsedFunctionPointerKind()) {
        const CXXMethodDecl *Method = VtableComponent.getFunctionDecl();
        if (Method->getVisibility() == Visibility::HiddenVisibility &&
            !Method->isDefined())
          return true;
      }
    }
    return false;
  }
};

class ARMCXXABI : public ItaniumCXXABI {
public:
  ARMCXXABI(CodeGen::CodeGenModule &CGM) :
    ItaniumCXXABI(CGM, /*UseARMMethodPtrABI=*/true,
                  /*UseARMGuardVarABI=*/true) {}

  bool constructorsAndDestructorsReturnThis() const override { return true; }

  void EmitReturnFromThunk(CodeGenFunction &CGF, RValue RV,
                           QualType ResTy) override;

  CharUnits getArrayCookieSizeImpl(QualType elementType) override;
  Address InitializeArrayCookie(CodeGenFunction &CGF,
                                Address NewPtr,
                                llvm::Value *NumElements,
                                const CXXNewExpr *expr,
                                QualType ElementType) override;
  llvm::Value *readArrayCookieImpl(CodeGenFunction &CGF, Address allocPtr,
                                   CharUnits cookieSize) override;
};

class AppleARM64CXXABI : public ARMCXXABI {
public:
  AppleARM64CXXABI(CodeGen::CodeGenModule &CGM) : ARMCXXABI(CGM) {
    Use32BitVTableOffsetABI = true;
  }

  // ARM64 libraries are prepared for non-unique RTTI.
  bool shouldRTTIBeUnique() const override { return false; }
};

class FuchsiaCXXABI final : public ItaniumCXXABI {
public:
  explicit FuchsiaCXXABI(CodeGen::CodeGenModule &CGM)
      : ItaniumCXXABI(CGM) {}

private:
  bool constructorsAndDestructorsReturnThis() const override { return true; }
};

class WebAssemblyCXXABI final : public ItaniumCXXABI {
public:
  explicit WebAssemblyCXXABI(CodeGen::CodeGenModule &CGM)
      : ItaniumCXXABI(CGM, /*UseARMMethodPtrABI=*/true,
                      /*UseARMGuardVarABI=*/true) {}
  void emitBeginCatch(CodeGenFunction &CGF, const CXXCatchStmt *C) override;
  llvm::CallInst *
  emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                      llvm::Value *Exn) override;

private:
  bool constructorsAndDestructorsReturnThis() const override { return true; }
  bool canCallMismatchedFunctionType() const override { return false; }
};

class XLCXXABI final : public ItaniumCXXABI {
public:
  explicit XLCXXABI(CodeGen::CodeGenModule &CGM)
      : ItaniumCXXABI(CGM) {}

  void registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                          llvm::FunctionCallee dtor,
                          llvm::Constant *addr) override;

  bool useSinitAndSterm() const override { return true; }

private:
  void emitCXXStermFinalizer(const VarDecl &D, llvm::Function *dtorStub,
                             llvm::Constant *addr);
};
}

CodeGen::CGCXXABI *CodeGen::CreateItaniumCXXABI(CodeGenModule &CGM) {
  switch (CGM.getContext().getCXXABIKind()) {
  // For IR-generation purposes, there's no significant difference
  // between the ARM and iOS ABIs.
  case TargetCXXABI::GenericARM:
  case TargetCXXABI::iOS:
  case TargetCXXABI::WatchOS:
    return new ARMCXXABI(CGM);

  case TargetCXXABI::AppleARM64:
    return new AppleARM64CXXABI(CGM);

  case TargetCXXABI::Fuchsia:
    return new FuchsiaCXXABI(CGM);

  // Note that AArch64 uses the generic ItaniumCXXABI class since it doesn't
  // include the other 32-bit ARM oddities: constructor/destructor return values
  // and array cookies.
  case TargetCXXABI::GenericAArch64:
    return new ItaniumCXXABI(CGM, /*UseARMMethodPtrABI=*/true,
                             /*UseARMGuardVarABI=*/true);

  case TargetCXXABI::GenericMIPS:
    return new ItaniumCXXABI(CGM, /*UseARMMethodPtrABI=*/true);

  case TargetCXXABI::WebAssembly:
    return new WebAssemblyCXXABI(CGM);

  case TargetCXXABI::XL:
    return new XLCXXABI(CGM);

  case TargetCXXABI::GenericItanium:
    if (CGM.getContext().getTargetInfo().getTriple().getArch()
        == llvm::Triple::le32) {
      // For PNaCl, use ARM-style method pointers so that PNaCl code
      // does not assume anything about the alignment of function
      // pointers.
      return new ItaniumCXXABI(CGM, /*UseARMMethodPtrABI=*/true);
    }
    return new ItaniumCXXABI(CGM);

  case TargetCXXABI::Microsoft:
    llvm_unreachable("Microsoft ABI is not Itanium-based");
  }
  llvm_unreachable("bad ABI kind");
}

llvm::Type *
ItaniumCXXABI::ConvertMemberPointerType(const MemberPointerType *MPT) {
  if (MPT->isMemberDataPointer())
    return CGM.PtrDiffTy;
  return llvm::StructType::get(CGM.PtrDiffTy, CGM.PtrDiffTy);
}

/// In the Itanium and ARM ABIs, method pointers have the form:
///   struct { ptrdiff_t ptr; ptrdiff_t adj; } memptr;
///
/// In the Itanium ABI:
///  - method pointers are virtual if (memptr.ptr & 1) is nonzero
///  - the this-adjustment is (memptr.adj)
///  - the virtual offset is (memptr.ptr - 1)
///
/// In the ARM ABI:
///  - method pointers are virtual if (memptr.adj & 1) is nonzero
///  - the this-adjustment is (memptr.adj >> 1)
///  - the virtual offset is (memptr.ptr)
/// ARM uses 'adj' for the virtual flag because Thumb functions
/// may be only single-byte aligned.
///
/// If the member is virtual, the adjusted 'this' pointer points
/// to a vtable pointer from which the virtual offset is applied.
///
/// If the member is non-virtual, memptr.ptr is the address of
/// the function to call.
CGCallee ItaniumCXXABI::EmitLoadOfMemberFunctionPointer(
    CodeGenFunction &CGF, const Expr *E, Address ThisAddr,
    llvm::Value *&ThisPtrForCall,
    llvm::Value *MemFnPtr, const MemberPointerType *MPT) {
  CGBuilderTy &Builder = CGF.Builder;

  const FunctionProtoType *FPT =
      MPT->getPointeeType()->castAs<FunctionProtoType>();
  auto *RD =
      cast<CXXRecordDecl>(MPT->getClass()->castAs<RecordType>()->getDecl());

  llvm::Constant *ptrdiff_1 = llvm::ConstantInt::get(CGM.PtrDiffTy, 1);

  llvm::BasicBlock *FnVirtual = CGF.createBasicBlock("memptr.virtual");
  llvm::BasicBlock *FnNonVirtual = CGF.createBasicBlock("memptr.nonvirtual");
  llvm::BasicBlock *FnEnd = CGF.createBasicBlock("memptr.end");

  // Extract memptr.adj, which is in the second field.
  llvm::Value *RawAdj = Builder.CreateExtractValue(MemFnPtr, 1, "memptr.adj");

  // Compute the true adjustment.
  llvm::Value *Adj = RawAdj;
  if (UseARMMethodPtrABI)
    Adj = Builder.CreateAShr(Adj, ptrdiff_1, "memptr.adj.shifted");

  // Apply the adjustment and cast back to the original struct type
  // for consistency.
  llvm::Value *This = ThisAddr.emitRawPointer(CGF);
  This = Builder.CreateInBoundsGEP(Builder.getInt8Ty(), This, Adj);
  ThisPtrForCall = This;

  // Load the function pointer.
  llvm::Value *FnAsInt = Builder.CreateExtractValue(MemFnPtr, 0, "memptr.ptr");

  // If the LSB in the function pointer is 1, the function pointer points to
  // a virtual function.
  llvm::Value *IsVirtual;
  if (UseARMMethodPtrABI)
    IsVirtual = Builder.CreateAnd(RawAdj, ptrdiff_1);
  else
    IsVirtual = Builder.CreateAnd(FnAsInt, ptrdiff_1);
  IsVirtual = Builder.CreateIsNotNull(IsVirtual, "memptr.isvirtual");
  Builder.CreateCondBr(IsVirtual, FnVirtual, FnNonVirtual);

  // In the virtual path, the adjustment left 'This' pointing to the
  // vtable of the correct base subobject.  The "function pointer" is an
  // offset within the vtable (+1 for the virtual flag on non-ARM).
  CGF.EmitBlock(FnVirtual);

  // Cast the adjusted this to a pointer to vtable pointer and load.
  llvm::Type *VTableTy = CGF.CGM.GlobalsInt8PtrTy;
  CharUnits VTablePtrAlign =
    CGF.CGM.getDynamicOffsetAlignment(ThisAddr.getAlignment(), RD,
                                      CGF.getPointerAlign());
  llvm::Value *VTable = CGF.GetVTablePtr(
      Address(This, ThisAddr.getElementType(), VTablePtrAlign), VTableTy, RD);

  // Apply the offset.
  // On ARM64, to reserve extra space in virtual member function pointers,
  // we only pay attention to the low 32 bits of the offset.
  llvm::Value *VTableOffset = FnAsInt;
  if (!UseARMMethodPtrABI)
    VTableOffset = Builder.CreateSub(VTableOffset, ptrdiff_1);
  if (Use32BitVTableOffsetABI) {
    VTableOffset = Builder.CreateTrunc(VTableOffset, CGF.Int32Ty);
    VTableOffset = Builder.CreateZExt(VTableOffset, CGM.PtrDiffTy);
  }

  // Check the address of the function pointer if CFI on member function
  // pointers is enabled.
  llvm::Constant *CheckSourceLocation;
  llvm::Constant *CheckTypeDesc;
  bool ShouldEmitCFICheck = CGF.SanOpts.has(SanitizerKind::CFIMFCall) &&
                            CGM.HasHiddenLTOVisibility(RD);
  bool ShouldEmitVFEInfo = CGM.getCodeGenOpts().VirtualFunctionElimination &&
                           CGM.HasHiddenLTOVisibility(RD);
  bool ShouldEmitWPDInfo =
      CGM.getCodeGenOpts().WholeProgramVTables &&
      // Don't insert type tests if we are forcing public visibility.
      !CGM.AlwaysHasLTOVisibilityPublic(RD);
  llvm::Value *VirtualFn = nullptr;

  {
    CodeGenFunction::SanitizerScope SanScope(&CGF);
    llvm::Value *TypeId = nullptr;
    llvm::Value *CheckResult = nullptr;

    if (ShouldEmitCFICheck || ShouldEmitVFEInfo || ShouldEmitWPDInfo) {
      // If doing CFI, VFE or WPD, we will need the metadata node to check
      // against.
      llvm::Metadata *MD =
          CGM.CreateMetadataIdentifierForVirtualMemPtrType(QualType(MPT, 0));
      TypeId = llvm::MetadataAsValue::get(CGF.getLLVMContext(), MD);
    }

    if (ShouldEmitVFEInfo) {
      llvm::Value *VFPAddr =
          Builder.CreateGEP(CGF.Int8Ty, VTable, VTableOffset);

      // If doing VFE, load from the vtable with a type.checked.load intrinsic
      // call. Note that we use the GEP to calculate the address to load from
      // and pass 0 as the offset to the intrinsic. This is because every
      // vtable slot of the correct type is marked with matching metadata, and
      // we know that the load must be from one of these slots.
      llvm::Value *CheckedLoad = Builder.CreateCall(
          CGM.getIntrinsic(llvm::Intrinsic::type_checked_load),
          {VFPAddr, llvm::ConstantInt::get(CGM.Int32Ty, 0), TypeId});
      CheckResult = Builder.CreateExtractValue(CheckedLoad, 1);
      VirtualFn = Builder.CreateExtractValue(CheckedLoad, 0);
    } else {
      // When not doing VFE, emit a normal load, as it allows more
      // optimisations than type.checked.load.
      if (ShouldEmitCFICheck || ShouldEmitWPDInfo) {
        llvm::Value *VFPAddr =
            Builder.CreateGEP(CGF.Int8Ty, VTable, VTableOffset);
        llvm::Intrinsic::ID IID = CGM.HasHiddenLTOVisibility(RD)
                                      ? llvm::Intrinsic::type_test
                                      : llvm::Intrinsic::public_type_test;

        CheckResult =
            Builder.CreateCall(CGM.getIntrinsic(IID), {VFPAddr, TypeId});
      }

      if (CGM.getItaniumVTableContext().isRelativeLayout()) {
        VirtualFn = CGF.Builder.CreateCall(
            CGM.getIntrinsic(llvm::Intrinsic::load_relative,
                             {VTableOffset->getType()}),
            {VTable, VTableOffset});
      } else {
        llvm::Value *VFPAddr =
            CGF.Builder.CreateGEP(CGF.Int8Ty, VTable, VTableOffset);
        VirtualFn = CGF.Builder.CreateAlignedLoad(CGF.UnqualPtrTy, VFPAddr,
                                                  CGF.getPointerAlign(),
                                                  "memptr.virtualfn");
      }
    }
    assert(VirtualFn && "Virtual fuction pointer not created!");
    assert((!ShouldEmitCFICheck || !ShouldEmitVFEInfo || !ShouldEmitWPDInfo ||
            CheckResult) &&
           "Check result required but not created!");

    if (ShouldEmitCFICheck) {
      // If doing CFI, emit the check.
      CheckSourceLocation = CGF.EmitCheckSourceLocation(E->getBeginLoc());
      CheckTypeDesc = CGF.EmitCheckTypeDescriptor(QualType(MPT, 0));
      llvm::Constant *StaticData[] = {
          llvm::ConstantInt::get(CGF.Int8Ty, CodeGenFunction::CFITCK_VMFCall),
          CheckSourceLocation,
          CheckTypeDesc,
      };

      if (CGM.getCodeGenOpts().SanitizeTrap.has(SanitizerKind::CFIMFCall)) {
        CGF.EmitTrapCheck(CheckResult, SanitizerHandler::CFICheckFail);
      } else {
        llvm::Value *AllVtables = llvm::MetadataAsValue::get(
            CGM.getLLVMContext(),
            llvm::MDString::get(CGM.getLLVMContext(), "all-vtables"));
        llvm::Value *ValidVtable = Builder.CreateCall(
            CGM.getIntrinsic(llvm::Intrinsic::type_test), {VTable, AllVtables});
        CGF.EmitCheck(std::make_pair(CheckResult, SanitizerKind::CFIMFCall),
                      SanitizerHandler::CFICheckFail, StaticData,
                      {VTable, ValidVtable});
      }

      FnVirtual = Builder.GetInsertBlock();
    }
  } // End of sanitizer scope

  CGF.EmitBranch(FnEnd);

  // In the non-virtual path, the function pointer is actually a
  // function pointer.
  CGF.EmitBlock(FnNonVirtual);
  llvm::Value *NonVirtualFn =
      Builder.CreateIntToPtr(FnAsInt, CGF.UnqualPtrTy, "memptr.nonvirtualfn");

  // Check the function pointer if CFI on member function pointers is enabled.
  if (ShouldEmitCFICheck) {
    CXXRecordDecl *RD = MPT->getClass()->getAsCXXRecordDecl();
    if (RD->hasDefinition()) {
      CodeGenFunction::SanitizerScope SanScope(&CGF);

      llvm::Constant *StaticData[] = {
          llvm::ConstantInt::get(CGF.Int8Ty, CodeGenFunction::CFITCK_NVMFCall),
          CheckSourceLocation,
          CheckTypeDesc,
      };

      llvm::Value *Bit = Builder.getFalse();
      for (const CXXRecordDecl *Base : CGM.getMostBaseClasses(RD)) {
        llvm::Metadata *MD = CGM.CreateMetadataIdentifierForType(
            getContext().getMemberPointerType(
                MPT->getPointeeType(),
                getContext().getRecordType(Base).getTypePtr()));
        llvm::Value *TypeId =
            llvm::MetadataAsValue::get(CGF.getLLVMContext(), MD);

        llvm::Value *TypeTest =
            Builder.CreateCall(CGM.getIntrinsic(llvm::Intrinsic::type_test),
                               {NonVirtualFn, TypeId});
        Bit = Builder.CreateOr(Bit, TypeTest);
      }

      CGF.EmitCheck(std::make_pair(Bit, SanitizerKind::CFIMFCall),
                    SanitizerHandler::CFICheckFail, StaticData,
                    {NonVirtualFn, llvm::UndefValue::get(CGF.IntPtrTy)});

      FnNonVirtual = Builder.GetInsertBlock();
    }
  }

  // We're done.
  CGF.EmitBlock(FnEnd);
  llvm::PHINode *CalleePtr = Builder.CreatePHI(CGF.UnqualPtrTy, 2);
  CalleePtr->addIncoming(VirtualFn, FnVirtual);
  CalleePtr->addIncoming(NonVirtualFn, FnNonVirtual);

  CGPointerAuthInfo PointerAuth;

  if (const auto &Schema =
          CGM.getCodeGenOpts().PointerAuth.CXXMemberFunctionPointers) {
    llvm::PHINode *DiscriminatorPHI = Builder.CreatePHI(CGF.IntPtrTy, 2);
    DiscriminatorPHI->addIncoming(llvm::ConstantInt::get(CGF.IntPtrTy, 0),
                                  FnVirtual);
    const auto &AuthInfo =
        CGM.getMemberFunctionPointerAuthInfo(QualType(MPT, 0));
    assert(Schema.getKey() == AuthInfo.getKey() &&
           "Keys for virtual and non-virtual member functions must match");
    auto *NonVirtualDiscriminator = AuthInfo.getDiscriminator();
    DiscriminatorPHI->addIncoming(NonVirtualDiscriminator, FnNonVirtual);
    PointerAuth = CGPointerAuthInfo(
        Schema.getKey(), Schema.getAuthenticationMode(), Schema.isIsaPointer(),
        Schema.authenticatesNullValues(), DiscriminatorPHI);
  }

  CGCallee Callee(FPT, CalleePtr, PointerAuth);
  return Callee;
}

/// Compute an l-value by applying the given pointer-to-member to a
/// base object.
llvm::Value *ItaniumCXXABI::EmitMemberDataPointerAddress(
    CodeGenFunction &CGF, const Expr *E, Address Base, llvm::Value *MemPtr,
    const MemberPointerType *MPT) {
  assert(MemPtr->getType() == CGM.PtrDiffTy);

  CGBuilderTy &Builder = CGF.Builder;

  // Apply the offset, which we assume is non-null.
  return Builder.CreateInBoundsGEP(CGF.Int8Ty, Base.emitRawPointer(CGF), MemPtr,
                                   "memptr.offset");
}

// See if it's possible to return a constant signed pointer.
static llvm::Constant *pointerAuthResignConstant(
    llvm::Value *Ptr, const CGPointerAuthInfo &CurAuthInfo,
    const CGPointerAuthInfo &NewAuthInfo, CodeGenModule &CGM) {
  const auto *CPA = dyn_cast<llvm::ConstantPtrAuth>(Ptr);

  if (!CPA)
    return nullptr;

  assert(CPA->getKey()->getZExtValue() == CurAuthInfo.getKey() &&
         CPA->getAddrDiscriminator()->isZeroValue() &&
         CPA->getDiscriminator() == CurAuthInfo.getDiscriminator() &&
         "unexpected key or discriminators");

  return CGM.getConstantSignedPointer(
      CPA->getPointer(), NewAuthInfo.getKey(), nullptr,
      cast<llvm::ConstantInt>(NewAuthInfo.getDiscriminator()));
}

/// Perform a bitcast, derived-to-base, or base-to-derived member pointer
/// conversion.
///
/// Bitcast conversions are always a no-op under Itanium.
///
/// Obligatory offset/adjustment diagram:
///         <-- offset -->          <-- adjustment -->
///   |--------------------------|----------------------|--------------------|
///   ^Derived address point     ^Base address point    ^Member address point
///
/// So when converting a base member pointer to a derived member pointer,
/// we add the offset to the adjustment because the address point has
/// decreased;  and conversely, when converting a derived MP to a base MP
/// we subtract the offset from the adjustment because the address point
/// has increased.
///
/// The standard forbids (at compile time) conversion to and from
/// virtual bases, which is why we don't have to consider them here.
///
/// The standard forbids (at run time) casting a derived MP to a base
/// MP when the derived MP does not point to a member of the base.
/// This is why -1 is a reasonable choice for null data member
/// pointers.
llvm::Value *
ItaniumCXXABI::EmitMemberPointerConversion(CodeGenFunction &CGF,
                                           const CastExpr *E,
                                           llvm::Value *src) {
  // Use constant emission if we can.
  if (isa<llvm::Constant>(src))
    return EmitMemberPointerConversion(E, cast<llvm::Constant>(src));

  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer ||
         E->getCastKind() == CK_ReinterpretMemberPointer);

  CGBuilderTy &Builder = CGF.Builder;
  QualType DstType = E->getType();

  if (DstType->isMemberFunctionPointerType()) {
    if (const auto &NewAuthInfo =
            CGM.getMemberFunctionPointerAuthInfo(DstType)) {
      QualType SrcType = E->getSubExpr()->getType();
      assert(SrcType->isMemberFunctionPointerType());
      const auto &CurAuthInfo = CGM.getMemberFunctionPointerAuthInfo(SrcType);
      llvm::Value *MemFnPtr = Builder.CreateExtractValue(src, 0, "memptr.ptr");
      llvm::Type *OrigTy = MemFnPtr->getType();

      llvm::BasicBlock *StartBB = Builder.GetInsertBlock();
      llvm::BasicBlock *ResignBB = CGF.createBasicBlock("resign");
      llvm::BasicBlock *MergeBB = CGF.createBasicBlock("merge");

      // Check whether we have a virtual offset or a pointer to a function.
      assert(UseARMMethodPtrABI && "ARM ABI expected");
      llvm::Value *Adj = Builder.CreateExtractValue(src, 1, "memptr.adj");
      llvm::Constant *Ptrdiff_1 = llvm::ConstantInt::get(CGM.PtrDiffTy, 1);
      llvm::Value *AndVal = Builder.CreateAnd(Adj, Ptrdiff_1);
      llvm::Value *IsVirtualOffset =
          Builder.CreateIsNotNull(AndVal, "is.virtual.offset");
      Builder.CreateCondBr(IsVirtualOffset, MergeBB, ResignBB);

      CGF.EmitBlock(ResignBB);
      llvm::Type *PtrTy = llvm::PointerType::getUnqual(CGM.Int8Ty);
      MemFnPtr = Builder.CreateIntToPtr(MemFnPtr, PtrTy);
      MemFnPtr =
          CGF.emitPointerAuthResign(MemFnPtr, SrcType, CurAuthInfo, NewAuthInfo,
                                    isa<llvm::Constant>(src));
      MemFnPtr = Builder.CreatePtrToInt(MemFnPtr, OrigTy);
      llvm::Value *ResignedVal = Builder.CreateInsertValue(src, MemFnPtr, 0);
      ResignBB = Builder.GetInsertBlock();

      CGF.EmitBlock(MergeBB);
      llvm::PHINode *NewSrc = Builder.CreatePHI(src->getType(), 2);
      NewSrc->addIncoming(src, StartBB);
      NewSrc->addIncoming(ResignedVal, ResignBB);
      src = NewSrc;
    }
  }

  // Under Itanium, reinterprets don't require any additional processing.
  if (E->getCastKind() == CK_ReinterpretMemberPointer) return src;

  llvm::Constant *adj = getMemberPointerAdjustment(E);
  if (!adj) return src;

  bool isDerivedToBase = (E->getCastKind() == CK_DerivedToBaseMemberPointer);

  const MemberPointerType *destTy =
    E->getType()->castAs<MemberPointerType>();

  // For member data pointers, this is just a matter of adding the
  // offset if the source is non-null.
  if (destTy->isMemberDataPointer()) {
    llvm::Value *dst;
    if (isDerivedToBase)
      dst = Builder.CreateNSWSub(src, adj, "adj");
    else
      dst = Builder.CreateNSWAdd(src, adj, "adj");

    // Null check.
    llvm::Value *null = llvm::Constant::getAllOnesValue(src->getType());
    llvm::Value *isNull = Builder.CreateICmpEQ(src, null, "memptr.isnull");
    return Builder.CreateSelect(isNull, src, dst);
  }

  // The this-adjustment is left-shifted by 1 on ARM.
  if (UseARMMethodPtrABI) {
    uint64_t offset = cast<llvm::ConstantInt>(adj)->getZExtValue();
    offset <<= 1;
    adj = llvm::ConstantInt::get(adj->getType(), offset);
  }

  llvm::Value *srcAdj = Builder.CreateExtractValue(src, 1, "src.adj");
  llvm::Value *dstAdj;
  if (isDerivedToBase)
    dstAdj = Builder.CreateNSWSub(srcAdj, adj, "adj");
  else
    dstAdj = Builder.CreateNSWAdd(srcAdj, adj, "adj");

  return Builder.CreateInsertValue(src, dstAdj, 1);
}

static llvm::Constant *
pointerAuthResignMemberFunctionPointer(llvm::Constant *Src, QualType DestType,
                                       QualType SrcType, CodeGenModule &CGM) {
  assert(DestType->isMemberFunctionPointerType() &&
         SrcType->isMemberFunctionPointerType() &&
         "member function pointers expected");
  if (DestType == SrcType)
    return Src;

  const auto &NewAuthInfo = CGM.getMemberFunctionPointerAuthInfo(DestType);
  const auto &CurAuthInfo = CGM.getMemberFunctionPointerAuthInfo(SrcType);

  if (!NewAuthInfo && !CurAuthInfo)
    return Src;

  llvm::Constant *MemFnPtr = Src->getAggregateElement(0u);
  if (MemFnPtr->getNumOperands() == 0) {
    // src must be a pair of null pointers.
    assert(isa<llvm::ConstantInt>(MemFnPtr) && "constant int expected");
    return Src;
  }

  llvm::Constant *ConstPtr = pointerAuthResignConstant(
      cast<llvm::User>(MemFnPtr)->getOperand(0), CurAuthInfo, NewAuthInfo, CGM);
  ConstPtr = llvm::ConstantExpr::getPtrToInt(ConstPtr, MemFnPtr->getType());
  return ConstantFoldInsertValueInstruction(Src, ConstPtr, 0);
}

llvm::Constant *
ItaniumCXXABI::EmitMemberPointerConversion(const CastExpr *E,
                                           llvm::Constant *src) {
  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer ||
         E->getCastKind() == CK_ReinterpretMemberPointer);

  QualType DstType = E->getType();

  if (DstType->isMemberFunctionPointerType())
    src = pointerAuthResignMemberFunctionPointer(
        src, DstType, E->getSubExpr()->getType(), CGM);

  // Under Itanium, reinterprets don't require any additional processing.
  if (E->getCastKind() == CK_ReinterpretMemberPointer) return src;

  // If the adjustment is trivial, we don't need to do anything.
  llvm::Constant *adj = getMemberPointerAdjustment(E);
  if (!adj) return src;

  bool isDerivedToBase = (E->getCastKind() == CK_DerivedToBaseMemberPointer);

  const MemberPointerType *destTy =
    E->getType()->castAs<MemberPointerType>();

  // For member data pointers, this is just a matter of adding the
  // offset if the source is non-null.
  if (destTy->isMemberDataPointer()) {
    // null maps to null.
    if (src->isAllOnesValue()) return src;

    if (isDerivedToBase)
      return llvm::ConstantExpr::getNSWSub(src, adj);
    else
      return llvm::ConstantExpr::getNSWAdd(src, adj);
  }

  // The this-adjustment is left-shifted by 1 on ARM.
  if (UseARMMethodPtrABI) {
    uint64_t offset = cast<llvm::ConstantInt>(adj)->getZExtValue();
    offset <<= 1;
    adj = llvm::ConstantInt::get(adj->getType(), offset);
  }

  llvm::Constant *srcAdj = src->getAggregateElement(1);
  llvm::Constant *dstAdj;
  if (isDerivedToBase)
    dstAdj = llvm::ConstantExpr::getNSWSub(srcAdj, adj);
  else
    dstAdj = llvm::ConstantExpr::getNSWAdd(srcAdj, adj);

  llvm::Constant *res = ConstantFoldInsertValueInstruction(src, dstAdj, 1);
  assert(res != nullptr && "Folding must succeed");
  return res;
}

llvm::Constant *
ItaniumCXXABI::EmitNullMemberPointer(const MemberPointerType *MPT) {
  // Itanium C++ ABI 2.3:
  //   A NULL pointer is represented as -1.
  if (MPT->isMemberDataPointer())
    return llvm::ConstantInt::get(CGM.PtrDiffTy, -1ULL, /*isSigned=*/true);

  llvm::Constant *Zero = llvm::ConstantInt::get(CGM.PtrDiffTy, 0);
  llvm::Constant *Values[2] = { Zero, Zero };
  return llvm::ConstantStruct::getAnon(Values);
}

llvm::Constant *
ItaniumCXXABI::EmitMemberDataPointer(const MemberPointerType *MPT,
                                     CharUnits offset) {
  // Itanium C++ ABI 2.3:
  //   A pointer to data member is an offset from the base address of
  //   the class object containing it, represented as a ptrdiff_t
  return llvm::ConstantInt::get(CGM.PtrDiffTy, offset.getQuantity());
}

llvm::Constant *
ItaniumCXXABI::EmitMemberFunctionPointer(const CXXMethodDecl *MD) {
  return BuildMemberPointer(MD, CharUnits::Zero());
}

llvm::Constant *ItaniumCXXABI::BuildMemberPointer(const CXXMethodDecl *MD,
                                                  CharUnits ThisAdjustment) {
  assert(MD->isInstance() && "Member function must not be static!");

  CodeGenTypes &Types = CGM.getTypes();

  // Get the function pointer (or index if this is a virtual function).
  llvm::Constant *MemPtr[2];
  if (MD->isVirtual()) {
    uint64_t Index = CGM.getItaniumVTableContext().getMethodVTableIndex(MD);
    uint64_t VTableOffset;
    if (CGM.getItaniumVTableContext().isRelativeLayout()) {
      // Multiply by 4-byte relative offsets.
      VTableOffset = Index * 4;
    } else {
      const ASTContext &Context = getContext();
      CharUnits PointerWidth = Context.toCharUnitsFromBits(
          Context.getTargetInfo().getPointerWidth(LangAS::Default));
      VTableOffset = Index * PointerWidth.getQuantity();
    }

    if (UseARMMethodPtrABI) {
      // ARM C++ ABI 3.2.1:
      //   This ABI specifies that adj contains twice the this
      //   adjustment, plus 1 if the member function is virtual. The
      //   least significant bit of adj then makes exactly the same
      //   discrimination as the least significant bit of ptr does for
      //   Itanium.

      // We cannot use the Itanium ABI's representation for virtual member
      // function pointers under pointer authentication because it would
      // require us to store both the virtual offset and the constant
      // discriminator in the pointer, which would be immediately vulnerable
      // to attack.  Instead we introduce a thunk that does the virtual dispatch
      // and store it as if it were a non-virtual member function.  This means
      // that virtual function pointers may not compare equal anymore, but
      // fortunately they aren't required to by the standard, and we do make
      // a best-effort attempt to re-use the thunk.
      //
      // To support interoperation with code in which pointer authentication
      // is disabled, derefencing a member function pointer must still handle
      // the virtual case, but it can use a discriminator which should never
      // be valid.
      const auto &Schema =
          CGM.getCodeGenOpts().PointerAuth.CXXMemberFunctionPointers;
      if (Schema)
        MemPtr[0] = llvm::ConstantExpr::getPtrToInt(
            getSignedVirtualMemberFunctionPointer(MD), CGM.PtrDiffTy);
      else
        MemPtr[0] = llvm::ConstantInt::get(CGM.PtrDiffTy, VTableOffset);
      // Don't set the LSB of adj to 1 if pointer authentication for member
      // function pointers is enabled.
      MemPtr[1] = llvm::ConstantInt::get(
          CGM.PtrDiffTy, 2 * ThisAdjustment.getQuantity() + !Schema);
    } else {
      // Itanium C++ ABI 2.3:
      //   For a virtual function, [the pointer field] is 1 plus the
      //   virtual table offset (in bytes) of the function,
      //   represented as a ptrdiff_t.
      MemPtr[0] = llvm::ConstantInt::get(CGM.PtrDiffTy, VTableOffset + 1);
      MemPtr[1] = llvm::ConstantInt::get(CGM.PtrDiffTy,
                                         ThisAdjustment.getQuantity());
    }
  } else {
    const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
    llvm::Type *Ty;
    // Check whether the function has a computable LLVM signature.
    if (Types.isFuncTypeConvertible(FPT)) {
      // The function has a computable LLVM signature; use the correct type.
      Ty = Types.GetFunctionType(Types.arrangeCXXMethodDeclaration(MD));
    } else {
      // Use an arbitrary non-function type to tell GetAddrOfFunction that the
      // function type is incomplete.
      Ty = CGM.PtrDiffTy;
    }
    llvm::Constant *addr = CGM.getMemberFunctionPointer(MD, Ty);

    MemPtr[0] = llvm::ConstantExpr::getPtrToInt(addr, CGM.PtrDiffTy);
    MemPtr[1] = llvm::ConstantInt::get(CGM.PtrDiffTy,
                                       (UseARMMethodPtrABI ? 2 : 1) *
                                       ThisAdjustment.getQuantity());
  }

  return llvm::ConstantStruct::getAnon(MemPtr);
}

llvm::Constant *ItaniumCXXABI::EmitMemberPointer(const APValue &MP,
                                                 QualType MPType) {
  const MemberPointerType *MPT = MPType->castAs<MemberPointerType>();
  const ValueDecl *MPD = MP.getMemberPointerDecl();
  if (!MPD)
    return EmitNullMemberPointer(MPT);

  CharUnits ThisAdjustment = getContext().getMemberPointerPathAdjustment(MP);

  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(MPD)) {
    llvm::Constant *Src = BuildMemberPointer(MD, ThisAdjustment);
    QualType SrcType = getContext().getMemberPointerType(
        MD->getType(), MD->getParent()->getTypeForDecl());
    return pointerAuthResignMemberFunctionPointer(Src, MPType, SrcType, CGM);
  }

  CharUnits FieldOffset =
    getContext().toCharUnitsFromBits(getContext().getFieldOffset(MPD));
  return EmitMemberDataPointer(MPT, ThisAdjustment + FieldOffset);
}

/// The comparison algorithm is pretty easy: the member pointers are
/// the same if they're either bitwise identical *or* both null.
///
/// ARM is different here only because null-ness is more complicated.
llvm::Value *
ItaniumCXXABI::EmitMemberPointerComparison(CodeGenFunction &CGF,
                                           llvm::Value *L,
                                           llvm::Value *R,
                                           const MemberPointerType *MPT,
                                           bool Inequality) {
  CGBuilderTy &Builder = CGF.Builder;

  llvm::ICmpInst::Predicate Eq;
  llvm::Instruction::BinaryOps And, Or;
  if (Inequality) {
    Eq = llvm::ICmpInst::ICMP_NE;
    And = llvm::Instruction::Or;
    Or = llvm::Instruction::And;
  } else {
    Eq = llvm::ICmpInst::ICMP_EQ;
    And = llvm::Instruction::And;
    Or = llvm::Instruction::Or;
  }

  // Member data pointers are easy because there's a unique null
  // value, so it just comes down to bitwise equality.
  if (MPT->isMemberDataPointer())
    return Builder.CreateICmp(Eq, L, R);

  // For member function pointers, the tautologies are more complex.
  // The Itanium tautology is:
  //   (L == R) <==> (L.ptr == R.ptr && (L.ptr == 0 || L.adj == R.adj))
  // The ARM tautology is:
  //   (L == R) <==> (L.ptr == R.ptr &&
  //                  (L.adj == R.adj ||
  //                   (L.ptr == 0 && ((L.adj|R.adj) & 1) == 0)))
  // The inequality tautologies have exactly the same structure, except
  // applying De Morgan's laws.

  llvm::Value *LPtr = Builder.CreateExtractValue(L, 0, "lhs.memptr.ptr");
  llvm::Value *RPtr = Builder.CreateExtractValue(R, 0, "rhs.memptr.ptr");

  // This condition tests whether L.ptr == R.ptr.  This must always be
  // true for equality to hold.
  llvm::Value *PtrEq = Builder.CreateICmp(Eq, LPtr, RPtr, "cmp.ptr");

  // This condition, together with the assumption that L.ptr == R.ptr,
  // tests whether the pointers are both null.  ARM imposes an extra
  // condition.
  llvm::Value *Zero = llvm::Constant::getNullValue(LPtr->getType());
  llvm::Value *EqZero = Builder.CreateICmp(Eq, LPtr, Zero, "cmp.ptr.null");

  // This condition tests whether L.adj == R.adj.  If this isn't
  // true, the pointers are unequal unless they're both null.
  llvm::Value *LAdj = Builder.CreateExtractValue(L, 1, "lhs.memptr.adj");
  llvm::Value *RAdj = Builder.CreateExtractValue(R, 1, "rhs.memptr.adj");
  llvm::Value *AdjEq = Builder.CreateICmp(Eq, LAdj, RAdj, "cmp.adj");

  // Null member function pointers on ARM clear the low bit of Adj,
  // so the zero condition has to check that neither low bit is set.
  if (UseARMMethodPtrABI) {
    llvm::Value *One = llvm::ConstantInt::get(LPtr->getType(), 1);

    // Compute (l.adj | r.adj) & 1 and test it against zero.
    llvm::Value *OrAdj = Builder.CreateOr(LAdj, RAdj, "or.adj");
    llvm::Value *OrAdjAnd1 = Builder.CreateAnd(OrAdj, One);
    llvm::Value *OrAdjAnd1EqZero = Builder.CreateICmp(Eq, OrAdjAnd1, Zero,
                                                      "cmp.or.adj");
    EqZero = Builder.CreateBinOp(And, EqZero, OrAdjAnd1EqZero);
  }

  // Tie together all our conditions.
  llvm::Value *Result = Builder.CreateBinOp(Or, EqZero, AdjEq);
  Result = Builder.CreateBinOp(And, PtrEq, Result,
                               Inequality ? "memptr.ne" : "memptr.eq");
  return Result;
}

llvm::Value *
ItaniumCXXABI::EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                          llvm::Value *MemPtr,
                                          const MemberPointerType *MPT) {
  CGBuilderTy &Builder = CGF.Builder;

  /// For member data pointers, this is just a check against -1.
  if (MPT->isMemberDataPointer()) {
    assert(MemPtr->getType() == CGM.PtrDiffTy);
    llvm::Value *NegativeOne =
      llvm::Constant::getAllOnesValue(MemPtr->getType());
    return Builder.CreateICmpNE(MemPtr, NegativeOne, "memptr.tobool");
  }

  // In Itanium, a member function pointer is not null if 'ptr' is not null.
  llvm::Value *Ptr = Builder.CreateExtractValue(MemPtr, 0, "memptr.ptr");

  llvm::Constant *Zero = llvm::ConstantInt::get(Ptr->getType(), 0);
  llvm::Value *Result = Builder.CreateICmpNE(Ptr, Zero, "memptr.tobool");

  // On ARM, a member function pointer is also non-null if the low bit of 'adj'
  // (the virtual bit) is set.
  if (UseARMMethodPtrABI) {
    llvm::Constant *One = llvm::ConstantInt::get(Ptr->getType(), 1);
    llvm::Value *Adj = Builder.CreateExtractValue(MemPtr, 1, "memptr.adj");
    llvm::Value *VirtualBit = Builder.CreateAnd(Adj, One, "memptr.virtualbit");
    llvm::Value *IsVirtual = Builder.CreateICmpNE(VirtualBit, Zero,
                                                  "memptr.isvirtual");
    Result = Builder.CreateOr(Result, IsVirtual);
  }

  return Result;
}

bool ItaniumCXXABI::classifyReturnType(CGFunctionInfo &FI) const {
  const CXXRecordDecl *RD = FI.getReturnType()->getAsCXXRecordDecl();
  if (!RD)
    return false;

  // If C++ prohibits us from making a copy, return by address.
  if (!RD->canPassInRegisters()) {
    auto Align = CGM.getContext().getTypeAlignInChars(FI.getReturnType());
    FI.getReturnInfo() = ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    return true;
  }
  return false;
}

/// The Itanium ABI requires non-zero initialization only for data
/// member pointers, for which '0' is a valid offset.
bool ItaniumCXXABI::isZeroInitializable(const MemberPointerType *MPT) {
  return MPT->isMemberFunctionPointer();
}

/// The Itanium ABI always places an offset to the complete object
/// at entry -2 in the vtable.
void ItaniumCXXABI::emitVirtualObjectDelete(CodeGenFunction &CGF,
                                            const CXXDeleteExpr *DE,
                                            Address Ptr,
                                            QualType ElementType,
                                            const CXXDestructorDecl *Dtor) {
  bool UseGlobalDelete = DE->isGlobalDelete();
  if (UseGlobalDelete) {
    // Derive the complete-object pointer, which is what we need
    // to pass to the deallocation function.

    // Grab the vtable pointer as an intptr_t*.
    auto *ClassDecl =
        cast<CXXRecordDecl>(ElementType->castAs<RecordType>()->getDecl());
    llvm::Value *VTable = CGF.GetVTablePtr(Ptr, CGF.UnqualPtrTy, ClassDecl);

    // Track back to entry -2 and pull out the offset there.
    llvm::Value *OffsetPtr = CGF.Builder.CreateConstInBoundsGEP1_64(
        CGF.IntPtrTy, VTable, -2, "complete-offset.ptr");
    llvm::Value *Offset = CGF.Builder.CreateAlignedLoad(CGF.IntPtrTy, OffsetPtr,
                                                        CGF.getPointerAlign());

    // Apply the offset.
    llvm::Value *CompletePtr = Ptr.emitRawPointer(CGF);
    CompletePtr =
        CGF.Builder.CreateInBoundsGEP(CGF.Int8Ty, CompletePtr, Offset);

    // If we're supposed to call the global delete, make sure we do so
    // even if the destructor throws.
    CGF.pushCallObjectDeleteCleanup(DE->getOperatorDelete(), CompletePtr,
                                    ElementType);
  }

  // FIXME: Provide a source location here even though there's no
  // CXXMemberCallExpr for dtor call.
  CXXDtorType DtorType = UseGlobalDelete ? Dtor_Complete : Dtor_Deleting;
  EmitVirtualDestructorCall(CGF, Dtor, DtorType, Ptr, DE);

  if (UseGlobalDelete)
    CGF.PopCleanupBlock();
}

void ItaniumCXXABI::emitRethrow(CodeGenFunction &CGF, bool isNoReturn) {
  // void __cxa_rethrow();

  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);

  llvm::FunctionCallee Fn = CGM.CreateRuntimeFunction(FTy, "__cxa_rethrow");

  if (isNoReturn)
    CGF.EmitNoreturnRuntimeCallOrInvoke(Fn, std::nullopt);
  else
    CGF.EmitRuntimeCallOrInvoke(Fn);
}

static llvm::FunctionCallee getAllocateExceptionFn(CodeGenModule &CGM) {
  // void *__cxa_allocate_exception(size_t thrown_size);

  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.Int8PtrTy, CGM.SizeTy, /*isVarArg=*/false);

  return CGM.CreateRuntimeFunction(FTy, "__cxa_allocate_exception");
}

static llvm::FunctionCallee getThrowFn(CodeGenModule &CGM) {
  // void __cxa_throw(void *thrown_exception, std::type_info *tinfo,
  //                  void (*dest) (void *));

  llvm::Type *Args[3] = { CGM.Int8PtrTy, CGM.GlobalsInt8PtrTy, CGM.Int8PtrTy };
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.VoidTy, Args, /*isVarArg=*/false);

  return CGM.CreateRuntimeFunction(FTy, "__cxa_throw");
}

void ItaniumCXXABI::emitThrow(CodeGenFunction &CGF, const CXXThrowExpr *E) {
  QualType ThrowType = E->getSubExpr()->getType();
  // Now allocate the exception object.
  llvm::Type *SizeTy = CGF.ConvertType(getContext().getSizeType());
  uint64_t TypeSize = getContext().getTypeSizeInChars(ThrowType).getQuantity();

  llvm::FunctionCallee AllocExceptionFn = getAllocateExceptionFn(CGM);
  llvm::CallInst *ExceptionPtr = CGF.EmitNounwindRuntimeCall(
      AllocExceptionFn, llvm::ConstantInt::get(SizeTy, TypeSize), "exception");

  CharUnits ExnAlign = CGF.getContext().getExnObjectAlignment();
  CGF.EmitAnyExprToExn(
      E->getSubExpr(), Address(ExceptionPtr, CGM.Int8Ty, ExnAlign));

  // Now throw the exception.
  llvm::Constant *TypeInfo = CGM.GetAddrOfRTTIDescriptor(ThrowType,
                                                         /*ForEH=*/true);

  // The address of the destructor.  If the exception type has a
  // trivial destructor (or isn't a record), we just pass null.
  llvm::Constant *Dtor = nullptr;
  if (const RecordType *RecordTy = ThrowType->getAs<RecordType>()) {
    CXXRecordDecl *Record = cast<CXXRecordDecl>(RecordTy->getDecl());
    if (!Record->hasTrivialDestructor()) {
      // __cxa_throw is declared to take its destructor as void (*)(void *). We
      // must match that if function pointers can be authenticated with a
      // discriminator based on their type.
      const ASTContext &Ctx = getContext();
      QualType DtorTy = Ctx.getFunctionType(Ctx.VoidTy, {Ctx.VoidPtrTy},
                                            FunctionProtoType::ExtProtoInfo());

      CXXDestructorDecl *DtorD = Record->getDestructor();
      Dtor = CGM.getAddrOfCXXStructor(GlobalDecl(DtorD, Dtor_Complete));
      Dtor = CGM.getFunctionPointer(Dtor, DtorTy);
    }
  }
  if (!Dtor) Dtor = llvm::Constant::getNullValue(CGM.Int8PtrTy);

  llvm::Value *args[] = { ExceptionPtr, TypeInfo, Dtor };
  CGF.EmitNoreturnRuntimeCallOrInvoke(getThrowFn(CGM), args);
}

static llvm::FunctionCallee getItaniumDynamicCastFn(CodeGenFunction &CGF) {
  // void *__dynamic_cast(const void *sub,
  //                      global_as const abi::__class_type_info *src,
  //                      global_as const abi::__class_type_info *dst,
  //                      std::ptrdiff_t src2dst_offset);

  llvm::Type *Int8PtrTy = CGF.Int8PtrTy;
  llvm::Type *GlobInt8PtrTy = CGF.GlobalsInt8PtrTy;
  llvm::Type *PtrDiffTy =
    CGF.ConvertType(CGF.getContext().getPointerDiffType());

  llvm::Type *Args[4] = { Int8PtrTy, GlobInt8PtrTy, GlobInt8PtrTy, PtrDiffTy };

  llvm::FunctionType *FTy = llvm::FunctionType::get(Int8PtrTy, Args, false);

  // Mark the function as nounwind willreturn readonly.
  llvm::AttrBuilder FuncAttrs(CGF.getLLVMContext());
  FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
  FuncAttrs.addAttribute(llvm::Attribute::WillReturn);
  FuncAttrs.addMemoryAttr(llvm::MemoryEffects::readOnly());
  llvm::AttributeList Attrs = llvm::AttributeList::get(
      CGF.getLLVMContext(), llvm::AttributeList::FunctionIndex, FuncAttrs);

  return CGF.CGM.CreateRuntimeFunction(FTy, "__dynamic_cast", Attrs);
}

static llvm::FunctionCallee getBadCastFn(CodeGenFunction &CGF) {
  // void __cxa_bad_cast();
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGF.VoidTy, false);
  return CGF.CGM.CreateRuntimeFunction(FTy, "__cxa_bad_cast");
}

/// Compute the src2dst_offset hint as described in the
/// Itanium C++ ABI [2.9.7]
static CharUnits computeOffsetHint(ASTContext &Context,
                                   const CXXRecordDecl *Src,
                                   const CXXRecordDecl *Dst) {
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);

  // If Dst is not derived from Src we can skip the whole computation below and
  // return that Src is not a public base of Dst.  Record all inheritance paths.
  if (!Dst->isDerivedFrom(Src, Paths))
    return CharUnits::fromQuantity(-2ULL);

  unsigned NumPublicPaths = 0;
  CharUnits Offset;

  // Now walk all possible inheritance paths.
  for (const CXXBasePath &Path : Paths) {
    if (Path.Access != AS_public)  // Ignore non-public inheritance.
      continue;

    ++NumPublicPaths;

    for (const CXXBasePathElement &PathElement : Path) {
      // If the path contains a virtual base class we can't give any hint.
      // -1: no hint.
      if (PathElement.Base->isVirtual())
        return CharUnits::fromQuantity(-1ULL);

      if (NumPublicPaths > 1) // Won't use offsets, skip computation.
        continue;

      // Accumulate the base class offsets.
      const ASTRecordLayout &L = Context.getASTRecordLayout(PathElement.Class);
      Offset += L.getBaseClassOffset(
          PathElement.Base->getType()->getAsCXXRecordDecl());
    }
  }

  // -2: Src is not a public base of Dst.
  if (NumPublicPaths == 0)
    return CharUnits::fromQuantity(-2ULL);

  // -3: Src is a multiple public base type but never a virtual base type.
  if (NumPublicPaths > 1)
    return CharUnits::fromQuantity(-3ULL);

  // Otherwise, the Src type is a unique public nonvirtual base type of Dst.
  // Return the offset of Src from the origin of Dst.
  return Offset;
}

static llvm::FunctionCallee getBadTypeidFn(CodeGenFunction &CGF) {
  // void __cxa_bad_typeid();
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGF.VoidTy, false);

  return CGF.CGM.CreateRuntimeFunction(FTy, "__cxa_bad_typeid");
}

bool ItaniumCXXABI::shouldTypeidBeNullChecked(QualType SrcRecordTy) {
  return true;
}

void ItaniumCXXABI::EmitBadTypeidCall(CodeGenFunction &CGF) {
  llvm::FunctionCallee Fn = getBadTypeidFn(CGF);
  llvm::CallBase *Call = CGF.EmitRuntimeCallOrInvoke(Fn);
  Call->setDoesNotReturn();
  CGF.Builder.CreateUnreachable();
}

llvm::Value *ItaniumCXXABI::EmitTypeid(CodeGenFunction &CGF,
                                       QualType SrcRecordTy,
                                       Address ThisPtr,
                                       llvm::Type *StdTypeInfoPtrTy) {
  auto *ClassDecl =
      cast<CXXRecordDecl>(SrcRecordTy->castAs<RecordType>()->getDecl());
  llvm::Value *Value = CGF.GetVTablePtr(ThisPtr, CGM.GlobalsInt8PtrTy,
                                        ClassDecl);

  if (CGM.getItaniumVTableContext().isRelativeLayout()) {
    // Load the type info.
    Value = CGF.Builder.CreateCall(
        CGM.getIntrinsic(llvm::Intrinsic::load_relative, {CGM.Int32Ty}),
        {Value, llvm::ConstantInt::get(CGM.Int32Ty, -4)});
  } else {
    // Load the type info.
    Value =
        CGF.Builder.CreateConstInBoundsGEP1_64(StdTypeInfoPtrTy, Value, -1ULL);
  }
  return CGF.Builder.CreateAlignedLoad(StdTypeInfoPtrTy, Value,
                                       CGF.getPointerAlign());
}

bool ItaniumCXXABI::shouldDynamicCastCallBeNullChecked(bool SrcIsPtr,
                                                       QualType SrcRecordTy) {
  return SrcIsPtr;
}

llvm::Value *ItaniumCXXABI::emitDynamicCastCall(
    CodeGenFunction &CGF, Address ThisAddr, QualType SrcRecordTy,
    QualType DestTy, QualType DestRecordTy, llvm::BasicBlock *CastEnd) {
  llvm::Type *PtrDiffLTy =
      CGF.ConvertType(CGF.getContext().getPointerDiffType());

  llvm::Value *SrcRTTI =
      CGF.CGM.GetAddrOfRTTIDescriptor(SrcRecordTy.getUnqualifiedType());
  llvm::Value *DestRTTI =
      CGF.CGM.GetAddrOfRTTIDescriptor(DestRecordTy.getUnqualifiedType());

  // Compute the offset hint.
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  const CXXRecordDecl *DestDecl = DestRecordTy->getAsCXXRecordDecl();
  llvm::Value *OffsetHint = llvm::ConstantInt::get(
      PtrDiffLTy,
      computeOffsetHint(CGF.getContext(), SrcDecl, DestDecl).getQuantity());

  // Emit the call to __dynamic_cast.
  llvm::Value *Value = ThisAddr.emitRawPointer(CGF);
  if (CGM.getCodeGenOpts().PointerAuth.CXXVTablePointers) {
    // We perform a no-op load of the vtable pointer here to force an
    // authentication. In environments that do not support pointer
    // authentication this is a an actual no-op that will be elided. When
    // pointer authentication is supported and enforced on vtable pointers this
    // load can trap.
    llvm::Value *Vtable =
        CGF.GetVTablePtr(ThisAddr, CGM.Int8PtrTy, SrcDecl,
                         CodeGenFunction::VTableAuthMode::MustTrap);
    assert(Vtable);
    (void)Vtable;
  }

  llvm::Value *args[] = {Value, SrcRTTI, DestRTTI, OffsetHint};
  Value = CGF.EmitNounwindRuntimeCall(getItaniumDynamicCastFn(CGF), args);

  /// C++ [expr.dynamic.cast]p9:
  ///   A failed cast to reference type throws std::bad_cast
  if (DestTy->isReferenceType()) {
    llvm::BasicBlock *BadCastBlock =
        CGF.createBasicBlock("dynamic_cast.bad_cast");

    llvm::Value *IsNull = CGF.Builder.CreateIsNull(Value);
    CGF.Builder.CreateCondBr(IsNull, BadCastBlock, CastEnd);

    CGF.EmitBlock(BadCastBlock);
    EmitBadCastCall(CGF);
  }

  return Value;
}

llvm::Value *ItaniumCXXABI::emitExactDynamicCast(
    CodeGenFunction &CGF, Address ThisAddr, QualType SrcRecordTy,
    QualType DestTy, QualType DestRecordTy, llvm::BasicBlock *CastSuccess,
    llvm::BasicBlock *CastFail) {
  ASTContext &Context = getContext();

  // Find all the inheritance paths.
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  const CXXRecordDecl *DestDecl = DestRecordTy->getAsCXXRecordDecl();
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  (void)DestDecl->isDerivedFrom(SrcDecl, Paths);

  // Find an offset within `DestDecl` where a `SrcDecl` instance and its vptr
  // might appear.
  std::optional<CharUnits> Offset;
  for (const CXXBasePath &Path : Paths) {
    // dynamic_cast only finds public inheritance paths.
    if (Path.Access != AS_public)
      continue;

    CharUnits PathOffset;
    for (const CXXBasePathElement &PathElement : Path) {
      // Find the offset along this inheritance step.
      const CXXRecordDecl *Base =
          PathElement.Base->getType()->getAsCXXRecordDecl();
      if (PathElement.Base->isVirtual()) {
        // For a virtual base class, we know that the derived class is exactly
        // DestDecl, so we can use the vbase offset from its layout.
        const ASTRecordLayout &L = Context.getASTRecordLayout(DestDecl);
        PathOffset = L.getVBaseClassOffset(Base);
      } else {
        const ASTRecordLayout &L =
            Context.getASTRecordLayout(PathElement.Class);
        PathOffset += L.getBaseClassOffset(Base);
      }
    }

    if (!Offset)
      Offset = PathOffset;
    else if (Offset != PathOffset) {
      // Base appears in at least two different places. Find the most-derived
      // object and see if it's a DestDecl. Note that the most-derived object
      // must be at least as aligned as this base class subobject, and must
      // have a vptr at offset 0.
      ThisAddr = Address(emitDynamicCastToVoid(CGF, ThisAddr, SrcRecordTy),
                         CGF.VoidPtrTy, ThisAddr.getAlignment());
      SrcDecl = DestDecl;
      Offset = CharUnits::Zero();
      break;
    }
  }

  if (!Offset) {
    // If there are no public inheritance paths, the cast always fails.
    CGF.EmitBranch(CastFail);
    return llvm::PoisonValue::get(CGF.VoidPtrTy);
  }

  // Compare the vptr against the expected vptr for the destination type at
  // this offset. Note that we do not know what type ThisAddr points to in
  // the case where the derived class multiply inherits from the base class
  // so we can't use GetVTablePtr, so we load the vptr directly instead.
  llvm::Instruction *VPtr = CGF.Builder.CreateLoad(
      ThisAddr.withElementType(CGF.VoidPtrPtrTy), "vtable");
  CGM.DecorateInstructionWithTBAA(
      VPtr, CGM.getTBAAVTablePtrAccessInfo(CGF.VoidPtrPtrTy));
  llvm::Value *Success = CGF.Builder.CreateICmpEQ(
      VPtr, getVTableAddressPoint(BaseSubobject(SrcDecl, *Offset), DestDecl));
  llvm::Value *Result = ThisAddr.emitRawPointer(CGF);
  if (!Offset->isZero())
    Result = CGF.Builder.CreateInBoundsGEP(
        CGF.CharTy, Result,
        {llvm::ConstantInt::get(CGF.PtrDiffTy, -Offset->getQuantity())});
  CGF.Builder.CreateCondBr(Success, CastSuccess, CastFail);
  return Result;
}

llvm::Value *ItaniumCXXABI::emitDynamicCastToVoid(CodeGenFunction &CGF,
                                                  Address ThisAddr,
                                                  QualType SrcRecordTy) {
  auto *ClassDecl =
      cast<CXXRecordDecl>(SrcRecordTy->castAs<RecordType>()->getDecl());
  llvm::Value *OffsetToTop;
  if (CGM.getItaniumVTableContext().isRelativeLayout()) {
    // Get the vtable pointer.
    llvm::Value *VTable =
        CGF.GetVTablePtr(ThisAddr, CGF.UnqualPtrTy, ClassDecl);

    // Get the offset-to-top from the vtable.
    OffsetToTop =
        CGF.Builder.CreateConstInBoundsGEP1_32(CGM.Int32Ty, VTable, -2U);
    OffsetToTop = CGF.Builder.CreateAlignedLoad(
        CGM.Int32Ty, OffsetToTop, CharUnits::fromQuantity(4), "offset.to.top");
  } else {
    llvm::Type *PtrDiffLTy =
        CGF.ConvertType(CGF.getContext().getPointerDiffType());

    // Get the vtable pointer.
    llvm::Value *VTable =
        CGF.GetVTablePtr(ThisAddr, CGF.UnqualPtrTy, ClassDecl);

    // Get the offset-to-top from the vtable.
    OffsetToTop =
        CGF.Builder.CreateConstInBoundsGEP1_64(PtrDiffLTy, VTable, -2ULL);
    OffsetToTop = CGF.Builder.CreateAlignedLoad(
        PtrDiffLTy, OffsetToTop, CGF.getPointerAlign(), "offset.to.top");
  }
  // Finally, add the offset to the pointer.
  return CGF.Builder.CreateInBoundsGEP(CGF.Int8Ty, ThisAddr.emitRawPointer(CGF),
                                       OffsetToTop);
}

bool ItaniumCXXABI::EmitBadCastCall(CodeGenFunction &CGF) {
  llvm::FunctionCallee Fn = getBadCastFn(CGF);
  llvm::CallBase *Call = CGF.EmitRuntimeCallOrInvoke(Fn);
  Call->setDoesNotReturn();
  CGF.Builder.CreateUnreachable();
  return true;
}

llvm::Value *
ItaniumCXXABI::GetVirtualBaseClassOffset(CodeGenFunction &CGF,
                                         Address This,
                                         const CXXRecordDecl *ClassDecl,
                                         const CXXRecordDecl *BaseClassDecl) {
  llvm::Value *VTablePtr = CGF.GetVTablePtr(This, CGM.Int8PtrTy, ClassDecl);
  CharUnits VBaseOffsetOffset =
      CGM.getItaniumVTableContext().getVirtualBaseOffsetOffset(ClassDecl,
                                                               BaseClassDecl);
  llvm::Value *VBaseOffsetPtr =
    CGF.Builder.CreateConstGEP1_64(
        CGF.Int8Ty, VTablePtr, VBaseOffsetOffset.getQuantity(),
        "vbase.offset.ptr");

  llvm::Value *VBaseOffset;
  if (CGM.getItaniumVTableContext().isRelativeLayout()) {
    VBaseOffset = CGF.Builder.CreateAlignedLoad(
        CGF.Int32Ty, VBaseOffsetPtr, CharUnits::fromQuantity(4),
        "vbase.offset");
  } else {
    VBaseOffset = CGF.Builder.CreateAlignedLoad(
        CGM.PtrDiffTy, VBaseOffsetPtr, CGF.getPointerAlign(), "vbase.offset");
  }
  return VBaseOffset;
}

void ItaniumCXXABI::EmitCXXConstructors(const CXXConstructorDecl *D) {
  // Just make sure we're in sync with TargetCXXABI.
  assert(CGM.getTarget().getCXXABI().hasConstructorVariants());

  // The constructor used for constructing this as a base class;
  // ignores virtual bases.
  CGM.EmitGlobal(GlobalDecl(D, Ctor_Base));

  // The constructor used for constructing this as a complete class;
  // constructs the virtual bases, then calls the base constructor.
  if (!D->getParent()->isAbstract()) {
    // We don't need to emit the complete ctor if the class is abstract.
    CGM.EmitGlobal(GlobalDecl(D, Ctor_Complete));
  }
}

CGCXXABI::AddedStructorArgCounts
ItaniumCXXABI::buildStructorSignature(GlobalDecl GD,
                                      SmallVectorImpl<CanQualType> &ArgTys) {
  ASTContext &Context = getContext();

  // All parameters are already in place except VTT, which goes after 'this'.
  // These are Clang types, so we don't need to worry about sret yet.

  // Check if we need to add a VTT parameter (which has type global void **).
  if ((isa<CXXConstructorDecl>(GD.getDecl()) ? GD.getCtorType() == Ctor_Base
                                             : GD.getDtorType() == Dtor_Base) &&
      cast<CXXMethodDecl>(GD.getDecl())->getParent()->getNumVBases() != 0) {
    LangAS AS = CGM.GetGlobalVarAddressSpace(nullptr);
    QualType Q = Context.getAddrSpaceQualType(Context.VoidPtrTy, AS);
    ArgTys.insert(ArgTys.begin() + 1,
                  Context.getPointerType(CanQualType::CreateUnsafe(Q)));
    return AddedStructorArgCounts::prefix(1);
  }
  return AddedStructorArgCounts{};
}

void ItaniumCXXABI::EmitCXXDestructors(const CXXDestructorDecl *D) {
  // The destructor used for destructing this as a base class; ignores
  // virtual bases.
  CGM.EmitGlobal(GlobalDecl(D, Dtor_Base));

  // The destructor used for destructing this as a most-derived class;
  // call the base destructor and then destructs any virtual bases.
  CGM.EmitGlobal(GlobalDecl(D, Dtor_Complete));

  // The destructor in a virtual table is always a 'deleting'
  // destructor, which calls the complete destructor and then uses the
  // appropriate operator delete.
  if (D->isVirtual())
    CGM.EmitGlobal(GlobalDecl(D, Dtor_Deleting));
}

void ItaniumCXXABI::addImplicitStructorParams(CodeGenFunction &CGF,
                                              QualType &ResTy,
                                              FunctionArgList &Params) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());
  assert(isa<CXXConstructorDecl>(MD) || isa<CXXDestructorDecl>(MD));

  // Check if we need a VTT parameter as well.
  if (NeedsVTTParameter(CGF.CurGD)) {
    ASTContext &Context = getContext();

    // FIXME: avoid the fake decl
    LangAS AS = CGM.GetGlobalVarAddressSpace(nullptr);
    QualType Q = Context.getAddrSpaceQualType(Context.VoidPtrTy, AS);
    QualType T = Context.getPointerType(Q);
    auto *VTTDecl = ImplicitParamDecl::Create(
        Context, /*DC=*/nullptr, MD->getLocation(), &Context.Idents.get("vtt"),
        T, ImplicitParamKind::CXXVTT);
    Params.insert(Params.begin() + 1, VTTDecl);
    getStructorImplicitParamDecl(CGF) = VTTDecl;
  }
}

void ItaniumCXXABI::EmitInstanceFunctionProlog(CodeGenFunction &CGF) {
  // Naked functions have no prolog.
  if (CGF.CurFuncDecl && CGF.CurFuncDecl->hasAttr<NakedAttr>())
    return;

  /// Initialize the 'this' slot. In the Itanium C++ ABI, no prologue
  /// adjustments are required, because they are all handled by thunks.
  setCXXABIThisValue(CGF, loadIncomingCXXThis(CGF));

  /// Initialize the 'vtt' slot if needed.
  if (getStructorImplicitParamDecl(CGF)) {
    getStructorImplicitParamValue(CGF) = CGF.Builder.CreateLoad(
        CGF.GetAddrOfLocalVar(getStructorImplicitParamDecl(CGF)), "vtt");
  }

  /// If this is a function that the ABI specifies returns 'this', initialize
  /// the return slot to 'this' at the start of the function.
  ///
  /// Unlike the setting of return types, this is done within the ABI
  /// implementation instead of by clients of CGCXXABI because:
  /// 1) getThisValue is currently protected
  /// 2) in theory, an ABI could implement 'this' returns some other way;
  ///    HasThisReturn only specifies a contract, not the implementation
  if (HasThisReturn(CGF.CurGD))
    CGF.Builder.CreateStore(getThisValue(CGF), CGF.ReturnValue);
}

CGCXXABI::AddedStructorArgs ItaniumCXXABI::getImplicitConstructorArgs(
    CodeGenFunction &CGF, const CXXConstructorDecl *D, CXXCtorType Type,
    bool ForVirtualBase, bool Delegating) {
  if (!NeedsVTTParameter(GlobalDecl(D, Type)))
    return AddedStructorArgs{};

  // Insert the implicit 'vtt' argument as the second argument. Make sure to
  // correctly reflect its address space, which can differ from generic on
  // some targets.
  llvm::Value *VTT =
      CGF.GetVTTParameter(GlobalDecl(D, Type), ForVirtualBase, Delegating);
  LangAS AS = CGM.GetGlobalVarAddressSpace(nullptr);
  QualType Q = getContext().getAddrSpaceQualType(getContext().VoidPtrTy, AS);
  QualType VTTTy = getContext().getPointerType(Q);
  return AddedStructorArgs::prefix({{VTT, VTTTy}});
}

llvm::Value *ItaniumCXXABI::getCXXDestructorImplicitParam(
    CodeGenFunction &CGF, const CXXDestructorDecl *DD, CXXDtorType Type,
    bool ForVirtualBase, bool Delegating) {
  GlobalDecl GD(DD, Type);
  return CGF.GetVTTParameter(GD, ForVirtualBase, Delegating);
}

void ItaniumCXXABI::EmitDestructorCall(CodeGenFunction &CGF,
                                       const CXXDestructorDecl *DD,
                                       CXXDtorType Type, bool ForVirtualBase,
                                       bool Delegating, Address This,
                                       QualType ThisTy) {
  GlobalDecl GD(DD, Type);
  llvm::Value *VTT =
      getCXXDestructorImplicitParam(CGF, DD, Type, ForVirtualBase, Delegating);
  QualType VTTTy = getContext().getPointerType(getContext().VoidPtrTy);

  CGCallee Callee;
  if (getContext().getLangOpts().AppleKext &&
      Type != Dtor_Base && DD->isVirtual())
    Callee = CGF.BuildAppleKextVirtualDestructorCall(DD, Type, DD->getParent());
  else
    Callee = CGCallee::forDirect(CGM.getAddrOfCXXStructor(GD), GD);

  CGF.EmitCXXDestructorCall(GD, Callee, CGF.getAsNaturalPointerTo(This, ThisTy),
                            ThisTy, VTT, VTTTy, nullptr);
}

// Check if any non-inline method has the specified attribute.
template <typename T>
static bool CXXRecordNonInlineHasAttr(const CXXRecordDecl *RD) {
  for (const auto *D : RD->noload_decls()) {
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isInlined() || FD->doesThisDeclarationHaveABody() ||
          FD->isPureVirtual())
        continue;
      if (D->hasAttr<T>())
        return true;
    }
  }

  return false;
}

static void setVTableSelectiveDLLImportExport(CodeGenModule &CGM,
                                              llvm::GlobalVariable *VTable,
                                              const CXXRecordDecl *RD) {
  if (VTable->getDLLStorageClass() !=
          llvm::GlobalVariable::DefaultStorageClass ||
      RD->hasAttr<DLLImportAttr>() || RD->hasAttr<DLLExportAttr>())
    return;

  if (CGM.getVTables().isVTableExternal(RD)) {
    if (CXXRecordNonInlineHasAttr<DLLImportAttr>(RD))
      VTable->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  } else if (CXXRecordNonInlineHasAttr<DLLExportAttr>(RD))
    VTable->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
}

void ItaniumCXXABI::emitVTableDefinitions(CodeGenVTables &CGVT,
                                          const CXXRecordDecl *RD) {
  llvm::GlobalVariable *VTable = getAddrOfVTable(RD, CharUnits());
  if (VTable->hasInitializer())
    return;

  ItaniumVTableContext &VTContext = CGM.getItaniumVTableContext();
  const VTableLayout &VTLayout = VTContext.getVTableLayout(RD);
  llvm::GlobalVariable::LinkageTypes Linkage = CGM.getVTableLinkage(RD);
  llvm::Constant *RTTI =
      CGM.GetAddrOfRTTIDescriptor(CGM.getContext().getTagDeclType(RD));

  // Create and set the initializer.
  ConstantInitBuilder builder(CGM);
  auto components = builder.beginStruct();
  CGVT.createVTableInitializer(components, VTLayout, RTTI,
                               llvm::GlobalValue::isLocalLinkage(Linkage));
  components.finishAndSetAsInitializer(VTable);

  // Set the correct linkage.
  VTable->setLinkage(Linkage);

  if (CGM.supportsCOMDAT() && VTable->isWeakForLinker())
    VTable->setComdat(CGM.getModule().getOrInsertComdat(VTable->getName()));

  if (CGM.getTarget().hasPS4DLLImportExport())
    setVTableSelectiveDLLImportExport(CGM, VTable, RD);

  // Set the right visibility.
  CGM.setGVProperties(VTable, RD);

  // If this is the magic class __cxxabiv1::__fundamental_type_info,
  // we will emit the typeinfo for the fundamental types. This is the
  // same behaviour as GCC.
  const DeclContext *DC = RD->getDeclContext();
  if (RD->getIdentifier() &&
      RD->getIdentifier()->isStr("__fundamental_type_info") &&
      isa<NamespaceDecl>(DC) && cast<NamespaceDecl>(DC)->getIdentifier() &&
      cast<NamespaceDecl>(DC)->getIdentifier()->isStr("__cxxabiv1") &&
      DC->getParent()->isTranslationUnit())
    EmitFundamentalRTTIDescriptors(RD);

  // Always emit type metadata on non-available_externally definitions, and on
  // available_externally definitions if we are performing whole program
  // devirtualization. For WPD we need the type metadata on all vtable
  // definitions to ensure we associate derived classes with base classes
  // defined in headers but with a strong definition only in a shared library.
  if (!VTable->isDeclarationForLinker() ||
      CGM.getCodeGenOpts().WholeProgramVTables) {
    CGM.EmitVTableTypeMetadata(RD, VTable, VTLayout);
    // For available_externally definitions, add the vtable to
    // @llvm.compiler.used so that it isn't deleted before whole program
    // analysis.
    if (VTable->isDeclarationForLinker()) {
      assert(CGM.getCodeGenOpts().WholeProgramVTables);
      CGM.addCompilerUsedGlobal(VTable);
    }
  }

  if (VTContext.isRelativeLayout()) {
    CGVT.RemoveHwasanMetadata(VTable);
    if (!VTable->isDSOLocal())
      CGVT.GenerateRelativeVTableAlias(VTable, VTable->getName());
  }
}

bool ItaniumCXXABI::isVirtualOffsetNeededForVTableField(
    CodeGenFunction &CGF, CodeGenFunction::VPtr Vptr) {
  if (Vptr.NearestVBase == nullptr)
    return false;
  return NeedsVTTParameter(CGF.CurGD);
}

llvm::Value *ItaniumCXXABI::getVTableAddressPointInStructor(
    CodeGenFunction &CGF, const CXXRecordDecl *VTableClass, BaseSubobject Base,
    const CXXRecordDecl *NearestVBase) {

  if ((Base.getBase()->getNumVBases() || NearestVBase != nullptr) &&
      NeedsVTTParameter(CGF.CurGD)) {
    return getVTableAddressPointInStructorWithVTT(CGF, VTableClass, Base,
                                                  NearestVBase);
  }
  return getVTableAddressPoint(Base, VTableClass);
}

llvm::Constant *
ItaniumCXXABI::getVTableAddressPoint(BaseSubobject Base,
                                     const CXXRecordDecl *VTableClass) {
  llvm::GlobalValue *VTable = getAddrOfVTable(VTableClass, CharUnits());

  // Find the appropriate vtable within the vtable group, and the address point
  // within that vtable.
  const VTableLayout &Layout =
      CGM.getItaniumVTableContext().getVTableLayout(VTableClass);
  VTableLayout::AddressPointLocation AddressPoint =
      Layout.getAddressPoint(Base);
  llvm::Value *Indices[] = {
    llvm::ConstantInt::get(CGM.Int32Ty, 0),
    llvm::ConstantInt::get(CGM.Int32Ty, AddressPoint.VTableIndex),
    llvm::ConstantInt::get(CGM.Int32Ty, AddressPoint.AddressPointIndex),
  };

  // Add inrange attribute to indicate that only the VTableIndex can be
  // accessed.
  unsigned ComponentSize =
      CGM.getDataLayout().getTypeAllocSize(CGM.getVTableComponentType());
  unsigned VTableSize =
      ComponentSize * Layout.getVTableSize(AddressPoint.VTableIndex);
  unsigned Offset = ComponentSize * AddressPoint.AddressPointIndex;
  llvm::ConstantRange InRange(llvm::APInt(32, -Offset, true),
                              llvm::APInt(32, VTableSize - Offset, true));
  return llvm::ConstantExpr::getGetElementPtr(
      VTable->getValueType(), VTable, Indices, /*InBounds=*/true, InRange);
}

llvm::Value *ItaniumCXXABI::getVTableAddressPointInStructorWithVTT(
    CodeGenFunction &CGF, const CXXRecordDecl *VTableClass, BaseSubobject Base,
    const CXXRecordDecl *NearestVBase) {
  assert((Base.getBase()->getNumVBases() || NearestVBase != nullptr) &&
         NeedsVTTParameter(CGF.CurGD) && "This class doesn't have VTT");

  // Get the secondary vpointer index.
  uint64_t VirtualPointerIndex =
      CGM.getVTables().getSecondaryVirtualPointerIndex(VTableClass, Base);

  /// Load the VTT.
  llvm::Value *VTT = CGF.LoadCXXVTT();
  if (VirtualPointerIndex)
    VTT = CGF.Builder.CreateConstInBoundsGEP1_64(CGF.GlobalsVoidPtrTy, VTT,
                                                 VirtualPointerIndex);

  // And load the address point from the VTT.
  llvm::Value *AP =
      CGF.Builder.CreateAlignedLoad(CGF.GlobalsVoidPtrTy, VTT,
                                    CGF.getPointerAlign());

  if (auto &Schema = CGF.CGM.getCodeGenOpts().PointerAuth.CXXVTTVTablePointers) {
    CGPointerAuthInfo PointerAuth = CGF.EmitPointerAuthInfo(Schema, VTT,
                                                            GlobalDecl(),
                                                            QualType());
    AP = CGF.EmitPointerAuthAuth(PointerAuth, AP);
  }

  return AP;
}

llvm::GlobalVariable *ItaniumCXXABI::getAddrOfVTable(const CXXRecordDecl *RD,
                                                     CharUnits VPtrOffset) {
  assert(VPtrOffset.isZero() && "Itanium ABI only supports zero vptr offsets");

  llvm::GlobalVariable *&VTable = VTables[RD];
  if (VTable)
    return VTable;

  // Queue up this vtable for possible deferred emission.
  CGM.addDeferredVTable(RD);

  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  getMangleContext().mangleCXXVTable(RD, Out);

  const VTableLayout &VTLayout =
      CGM.getItaniumVTableContext().getVTableLayout(RD);
  llvm::Type *VTableType = CGM.getVTables().getVTableType(VTLayout);

  // Use pointer to global alignment for the vtable. Otherwise we would align
  // them based on the size of the initializer which doesn't make sense as only
  // single values are read.
  LangAS AS = CGM.GetGlobalVarAddressSpace(nullptr);
  unsigned PAlign = CGM.getItaniumVTableContext().isRelativeLayout()
                        ? 32
                        : CGM.getTarget().getPointerAlign(AS);

  VTable = CGM.CreateOrReplaceCXXRuntimeVariable(
      Name, VTableType, llvm::GlobalValue::ExternalLinkage,
      getContext().toCharUnitsFromBits(PAlign).getAsAlign());
  VTable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  if (CGM.getTarget().hasPS4DLLImportExport())
    setVTableSelectiveDLLImportExport(CGM, VTable, RD);

  CGM.setGVProperties(VTable, RD);
  return VTable;
}

CGCallee ItaniumCXXABI::getVirtualFunctionPointer(CodeGenFunction &CGF,
                                                  GlobalDecl GD,
                                                  Address This,
                                                  llvm::Type *Ty,
                                                  SourceLocation Loc) {
  llvm::Type *PtrTy = CGM.GlobalsInt8PtrTy;
  auto *MethodDecl = cast<CXXMethodDecl>(GD.getDecl());
  llvm::Value *VTable = CGF.GetVTablePtr(This, PtrTy, MethodDecl->getParent());

  uint64_t VTableIndex = CGM.getItaniumVTableContext().getMethodVTableIndex(GD);
  llvm::Value *VFunc, *VTableSlotPtr = nullptr;
  auto &Schema = CGM.getCodeGenOpts().PointerAuth.CXXVirtualFunctionPointers;
  if (!Schema && CGF.ShouldEmitVTableTypeCheckedLoad(MethodDecl->getParent())) {
    VFunc = CGF.EmitVTableTypeCheckedLoad(
        MethodDecl->getParent(), VTable, PtrTy,
        VTableIndex *
            CGM.getContext().getTargetInfo().getPointerWidth(LangAS::Default) /
            8);
  } else {
    CGF.EmitTypeMetadataCodeForVCall(MethodDecl->getParent(), VTable, Loc);

    llvm::Value *VFuncLoad;
    if (CGM.getItaniumVTableContext().isRelativeLayout()) {
      VFuncLoad = CGF.Builder.CreateCall(
          CGM.getIntrinsic(llvm::Intrinsic::load_relative, {CGM.Int32Ty}),
          {VTable, llvm::ConstantInt::get(CGM.Int32Ty, 4 * VTableIndex)});
    } else {
      VTableSlotPtr = CGF.Builder.CreateConstInBoundsGEP1_64(
          PtrTy, VTable, VTableIndex, "vfn");
      VFuncLoad = CGF.Builder.CreateAlignedLoad(PtrTy, VTableSlotPtr,
                                                CGF.getPointerAlign());
    }

    // Add !invariant.load md to virtual function load to indicate that
    // function didn't change inside vtable.
    // It's safe to add it without -fstrict-vtable-pointers, but it would not
    // help in devirtualization because it will only matter if we will have 2
    // the same virtual function loads from the same vtable load, which won't
    // happen without enabled devirtualization with -fstrict-vtable-pointers.
    if (CGM.getCodeGenOpts().OptimizationLevel > 0 &&
        CGM.getCodeGenOpts().StrictVTablePointers) {
      if (auto *VFuncLoadInstr = dyn_cast<llvm::Instruction>(VFuncLoad)) {
        VFuncLoadInstr->setMetadata(
            llvm::LLVMContext::MD_invariant_load,
            llvm::MDNode::get(CGM.getLLVMContext(),
                              llvm::ArrayRef<llvm::Metadata *>()));
      }
    }
    VFunc = VFuncLoad;
  }

  CGPointerAuthInfo PointerAuth;
  if (Schema) {
    assert(VTableSlotPtr && "virtual function pointer not set");
    GD = CGM.getItaniumVTableContext().findOriginalMethod(GD.getCanonicalDecl());
    PointerAuth = CGF.EmitPointerAuthInfo(Schema, VTableSlotPtr, GD, QualType());
  }
  CGCallee Callee(GD, VFunc, PointerAuth);
  return Callee;
}

llvm::Value *ItaniumCXXABI::EmitVirtualDestructorCall(
    CodeGenFunction &CGF, const CXXDestructorDecl *Dtor, CXXDtorType DtorType,
    Address This, DeleteOrMemberCallExpr E) {
  auto *CE = E.dyn_cast<const CXXMemberCallExpr *>();
  auto *D = E.dyn_cast<const CXXDeleteExpr *>();
  assert((CE != nullptr) ^ (D != nullptr));
  assert(CE == nullptr || CE->arg_begin() == CE->arg_end());
  assert(DtorType == Dtor_Deleting || DtorType == Dtor_Complete);

  GlobalDecl GD(Dtor, DtorType);
  const CGFunctionInfo *FInfo =
      &CGM.getTypes().arrangeCXXStructorDeclaration(GD);
  llvm::FunctionType *Ty = CGF.CGM.getTypes().GetFunctionType(*FInfo);
  CGCallee Callee = CGCallee::forVirtual(CE, GD, This, Ty);

  QualType ThisTy;
  if (CE) {
    ThisTy = CE->getObjectType();
  } else {
    ThisTy = D->getDestroyedType();
  }

  CGF.EmitCXXDestructorCall(GD, Callee, This.emitRawPointer(CGF), ThisTy,
                            nullptr, QualType(), nullptr);
  return nullptr;
}

void ItaniumCXXABI::emitVirtualInheritanceTables(const CXXRecordDecl *RD) {
  CodeGenVTables &VTables = CGM.getVTables();
  llvm::GlobalVariable *VTT = VTables.GetAddrOfVTT(RD);
  VTables.EmitVTTDefinition(VTT, CGM.getVTableLinkage(RD), RD);
}

bool ItaniumCXXABI::canSpeculativelyEmitVTableAsBaseClass(
    const CXXRecordDecl *RD) const {
  // We don't emit available_externally vtables if we are in -fapple-kext mode
  // because kext mode does not permit devirtualization.
  if (CGM.getLangOpts().AppleKext)
    return false;

  // If the vtable is hidden then it is not safe to emit an available_externally
  // copy of vtable.
  if (isVTableHidden(RD))
    return false;

  if (CGM.getCodeGenOpts().ForceEmitVTables)
    return true;

  // If we don't have any not emitted inline virtual function then we are safe
  // to emit an available_externally copy of vtable.
  // FIXME we can still emit a copy of the vtable if we
  // can emit definition of the inline functions.
  if (hasAnyUnusedVirtualInlineFunction(RD))
    return false;

  // For a class with virtual bases, we must also be able to speculatively
  // emit the VTT, because CodeGen doesn't have separate notions of "can emit
  // the vtable" and "can emit the VTT". For a base subobject, this means we
  // need to be able to emit non-virtual base vtables.
  if (RD->getNumVBases()) {
    for (const auto &B : RD->bases()) {
      auto *BRD = B.getType()->getAsCXXRecordDecl();
      assert(BRD && "no class for base specifier");
      if (B.isVirtual() || !BRD->isDynamicClass())
        continue;
      if (!canSpeculativelyEmitVTableAsBaseClass(BRD))
        return false;
    }
  }

  return true;
}

bool ItaniumCXXABI::canSpeculativelyEmitVTable(const CXXRecordDecl *RD) const {
  if (!canSpeculativelyEmitVTableAsBaseClass(RD))
    return false;

  if (RD->shouldEmitInExternalSource())
    return false;

  // For a complete-object vtable (or more specifically, for the VTT), we need
  // to be able to speculatively emit the vtables of all dynamic virtual bases.
  for (const auto &B : RD->vbases()) {
    auto *BRD = B.getType()->getAsCXXRecordDecl();
    assert(BRD && "no class for base specifier");
    if (!BRD->isDynamicClass())
      continue;
    if (!canSpeculativelyEmitVTableAsBaseClass(BRD))
      return false;
  }

  return true;
}
static llvm::Value *performTypeAdjustment(CodeGenFunction &CGF,
                                          Address InitialPtr,
                                          const CXXRecordDecl *UnadjustedClass,
                                          int64_t NonVirtualAdjustment,
                                          int64_t VirtualAdjustment,
                                          bool IsReturnAdjustment) {
  if (!NonVirtualAdjustment && !VirtualAdjustment)
    return InitialPtr.emitRawPointer(CGF);

  Address V = InitialPtr.withElementType(CGF.Int8Ty);

  // In a base-to-derived cast, the non-virtual adjustment is applied first.
  if (NonVirtualAdjustment && !IsReturnAdjustment) {
    V = CGF.Builder.CreateConstInBoundsByteGEP(V,
                              CharUnits::fromQuantity(NonVirtualAdjustment));
  }

  // Perform the virtual adjustment if we have one.
  llvm::Value *ResultPtr;
  if (VirtualAdjustment) {
    llvm::Value *VTablePtr =
        CGF.GetVTablePtr(V, CGF.Int8PtrTy, UnadjustedClass);

    llvm::Value *Offset;
    llvm::Value *OffsetPtr = CGF.Builder.CreateConstInBoundsGEP1_64(
        CGF.Int8Ty, VTablePtr, VirtualAdjustment);
    if (CGF.CGM.getItaniumVTableContext().isRelativeLayout()) {
      // Load the adjustment offset from the vtable as a 32-bit int.
      Offset =
          CGF.Builder.CreateAlignedLoad(CGF.Int32Ty, OffsetPtr,
                                        CharUnits::fromQuantity(4));
    } else {
      llvm::Type *PtrDiffTy =
          CGF.ConvertType(CGF.getContext().getPointerDiffType());

      // Load the adjustment offset from the vtable.
      Offset = CGF.Builder.CreateAlignedLoad(PtrDiffTy, OffsetPtr,
                                             CGF.getPointerAlign());
    }
    // Adjust our pointer.
    ResultPtr = CGF.Builder.CreateInBoundsGEP(V.getElementType(),
                                              V.emitRawPointer(CGF), Offset);
  } else {
    ResultPtr = V.emitRawPointer(CGF);
  }

  // In a derived-to-base conversion, the non-virtual adjustment is
  // applied second.
  if (NonVirtualAdjustment && IsReturnAdjustment) {
    ResultPtr = CGF.Builder.CreateConstInBoundsGEP1_64(CGF.Int8Ty, ResultPtr,
                                                       NonVirtualAdjustment);
  }

  return ResultPtr;
}

llvm::Value *
ItaniumCXXABI::performThisAdjustment(CodeGenFunction &CGF, Address This,
                                     const CXXRecordDecl *UnadjustedClass,
                                     const ThunkInfo &TI) {
  return performTypeAdjustment(CGF, This, UnadjustedClass, TI.This.NonVirtual,
                               TI.This.Virtual.Itanium.VCallOffsetOffset,
                               /*IsReturnAdjustment=*/false);
}

llvm::Value *
ItaniumCXXABI::performReturnAdjustment(CodeGenFunction &CGF, Address Ret,
                                       const CXXRecordDecl *UnadjustedClass,
                                       const ReturnAdjustment &RA) {
  return performTypeAdjustment(CGF, Ret, UnadjustedClass, RA.NonVirtual,
                               RA.Virtual.Itanium.VBaseOffsetOffset,
                               /*IsReturnAdjustment=*/true);
}

void ARMCXXABI::EmitReturnFromThunk(CodeGenFunction &CGF,
                                    RValue RV, QualType ResultType) {
  if (!isa<CXXDestructorDecl>(CGF.CurGD.getDecl()))
    return ItaniumCXXABI::EmitReturnFromThunk(CGF, RV, ResultType);

  // Destructor thunks in the ARM ABI have indeterminate results.
  llvm::Type *T = CGF.ReturnValue.getElementType();
  RValue Undef = RValue::get(llvm::UndefValue::get(T));
  return ItaniumCXXABI::EmitReturnFromThunk(CGF, Undef, ResultType);
}

/************************** Array allocation cookies **************************/

CharUnits ItaniumCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // The array cookie is a size_t; pad that up to the element alignment.
  // The cookie is actually right-justified in that space.
  return std::max(CharUnits::fromQuantity(CGM.SizeSizeInBytes),
                  CGM.getContext().getPreferredTypeAlignInChars(elementType));
}

Address ItaniumCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                             Address NewPtr,
                                             llvm::Value *NumElements,
                                             const CXXNewExpr *expr,
                                             QualType ElementType) {
  assert(requiresArrayCookie(expr));

  unsigned AS = NewPtr.getAddressSpace();

  ASTContext &Ctx = getContext();
  CharUnits SizeSize = CGF.getSizeSize();

  // The size of the cookie.
  CharUnits CookieSize =
      std::max(SizeSize, Ctx.getPreferredTypeAlignInChars(ElementType));
  assert(CookieSize == getArrayCookieSizeImpl(ElementType));

  // Compute an offset to the cookie.
  Address CookiePtr = NewPtr;
  CharUnits CookieOffset = CookieSize - SizeSize;
  if (!CookieOffset.isZero())
    CookiePtr = CGF.Builder.CreateConstInBoundsByteGEP(CookiePtr, CookieOffset);

  // Write the number of elements into the appropriate slot.
  Address NumElementsPtr = CookiePtr.withElementType(CGF.SizeTy);
  llvm::Instruction *SI = CGF.Builder.CreateStore(NumElements, NumElementsPtr);

  // Handle the array cookie specially in ASan.
  if (CGM.getLangOpts().Sanitize.has(SanitizerKind::Address) && AS == 0 &&
      (expr->getOperatorNew()->isReplaceableGlobalAllocationFunction() ||
       CGM.getCodeGenOpts().SanitizeAddressPoisonCustomArrayCookie)) {
    // The store to the CookiePtr does not need to be instrumented.
    SI->setNoSanitizeMetadata();
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(CGM.VoidTy, NumElementsPtr.getType(), false);
    llvm::FunctionCallee F =
        CGM.CreateRuntimeFunction(FTy, "__asan_poison_cxx_array_cookie");
    CGF.Builder.CreateCall(F, NumElementsPtr.emitRawPointer(CGF));
  }

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  return CGF.Builder.CreateConstInBoundsByteGEP(NewPtr, CookieSize);
}

llvm::Value *ItaniumCXXABI::readArrayCookieImpl(CodeGenFunction &CGF,
                                                Address allocPtr,
                                                CharUnits cookieSize) {
  // The element size is right-justified in the cookie.
  Address numElementsPtr = allocPtr;
  CharUnits numElementsOffset = cookieSize - CGF.getSizeSize();
  if (!numElementsOffset.isZero())
    numElementsPtr =
      CGF.Builder.CreateConstInBoundsByteGEP(numElementsPtr, numElementsOffset);

  unsigned AS = allocPtr.getAddressSpace();
  numElementsPtr = numElementsPtr.withElementType(CGF.SizeTy);
  if (!CGM.getLangOpts().Sanitize.has(SanitizerKind::Address) || AS != 0)
    return CGF.Builder.CreateLoad(numElementsPtr);
  // In asan mode emit a function call instead of a regular load and let the
  // run-time deal with it: if the shadow is properly poisoned return the
  // cookie, otherwise return 0 to avoid an infinite loop calling DTORs.
  // We can't simply ignore this load using nosanitize metadata because
  // the metadata may be lost.
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGF.SizeTy, CGF.UnqualPtrTy, false);
  llvm::FunctionCallee F =
      CGM.CreateRuntimeFunction(FTy, "__asan_load_cxx_array_cookie");
  return CGF.Builder.CreateCall(F, numElementsPtr.emitRawPointer(CGF));
}

CharUnits ARMCXXABI::getArrayCookieSizeImpl(QualType elementType) {
  // ARM says that the cookie is always:
  //   struct array_cookie {
  //     std::size_t element_size; // element_size != 0
  //     std::size_t element_count;
  //   };
  // But the base ABI doesn't give anything an alignment greater than
  // 8, so we can dismiss this as typical ABI-author blindness to
  // actual language complexity and round up to the element alignment.
  return std::max(CharUnits::fromQuantity(2 * CGM.SizeSizeInBytes),
                  CGM.getContext().getTypeAlignInChars(elementType));
}

Address ARMCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                         Address newPtr,
                                         llvm::Value *numElements,
                                         const CXXNewExpr *expr,
                                         QualType elementType) {
  assert(requiresArrayCookie(expr));

  // The cookie is always at the start of the buffer.
  Address cookie = newPtr;

  // The first element is the element size.
  cookie = cookie.withElementType(CGF.SizeTy);
  llvm::Value *elementSize = llvm::ConstantInt::get(CGF.SizeTy,
                 getContext().getTypeSizeInChars(elementType).getQuantity());
  CGF.Builder.CreateStore(elementSize, cookie);

  // The second element is the element count.
  cookie = CGF.Builder.CreateConstInBoundsGEP(cookie, 1);
  CGF.Builder.CreateStore(numElements, cookie);

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  CharUnits cookieSize = ARMCXXABI::getArrayCookieSizeImpl(elementType);
  return CGF.Builder.CreateConstInBoundsByteGEP(newPtr, cookieSize);
}

llvm::Value *ARMCXXABI::readArrayCookieImpl(CodeGenFunction &CGF,
                                            Address allocPtr,
                                            CharUnits cookieSize) {
  // The number of elements is at offset sizeof(size_t) relative to
  // the allocated pointer.
  Address numElementsPtr
    = CGF.Builder.CreateConstInBoundsByteGEP(allocPtr, CGF.getSizeSize());

  numElementsPtr = numElementsPtr.withElementType(CGF.SizeTy);
  return CGF.Builder.CreateLoad(numElementsPtr);
}

/*********************** Static local initialization **************************/

static llvm::FunctionCallee getGuardAcquireFn(CodeGenModule &CGM,
                                              llvm::PointerType *GuardPtrTy) {
  // int __cxa_guard_acquire(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.getTypes().ConvertType(CGM.getContext().IntTy),
                            GuardPtrTy, /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "__cxa_guard_acquire",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind));
}

static llvm::FunctionCallee getGuardReleaseFn(CodeGenModule &CGM,
                                              llvm::PointerType *GuardPtrTy) {
  // void __cxa_guard_release(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.VoidTy, GuardPtrTy, /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "__cxa_guard_release",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind));
}

static llvm::FunctionCallee getGuardAbortFn(CodeGenModule &CGM,
                                            llvm::PointerType *GuardPtrTy) {
  // void __cxa_guard_abort(__guard *guard_object);
  llvm::FunctionType *FTy =
    llvm::FunctionType::get(CGM.VoidTy, GuardPtrTy, /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "__cxa_guard_abort",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind));
}

namespace {
  struct CallGuardAbort final : EHScopeStack::Cleanup {
    llvm::GlobalVariable *Guard;
    CallGuardAbort(llvm::GlobalVariable *Guard) : Guard(Guard) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitNounwindRuntimeCall(getGuardAbortFn(CGF.CGM, Guard->getType()),
                                  Guard);
    }
  };
}

/// The ARM code here follows the Itanium code closely enough that we
/// just special-case it at particular places.
void ItaniumCXXABI::EmitGuardedInit(CodeGenFunction &CGF,
                                    const VarDecl &D,
                                    llvm::GlobalVariable *var,
                                    bool shouldPerformInit) {
  CGBuilderTy &Builder = CGF.Builder;

  // Inline variables that weren't instantiated from variable templates have
  // partially-ordered initialization within their translation unit.
  bool NonTemplateInline =
      D.isInline() &&
      !isTemplateInstantiation(D.getTemplateSpecializationKind());

  // We only need to use thread-safe statics for local non-TLS variables and
  // inline variables; other global initialization is always single-threaded
  // or (through lazy dynamic loading in multiple threads) unsequenced.
  bool threadsafe = getContext().getLangOpts().ThreadsafeStatics &&
                    (D.isLocalVarDecl() || NonTemplateInline) &&
                    !D.getTLSKind();

  // If we have a global variable with internal linkage and thread-safe statics
  // are disabled, we can just let the guard variable be of type i8.
  bool useInt8GuardVariable = !threadsafe && var->hasInternalLinkage();

  llvm::IntegerType *guardTy;
  CharUnits guardAlignment;
  if (useInt8GuardVariable) {
    guardTy = CGF.Int8Ty;
    guardAlignment = CharUnits::One();
  } else {
    // Guard variables are 64 bits in the generic ABI and size width on ARM
    // (i.e. 32-bit on AArch32, 64-bit on AArch64).
    if (UseARMGuardVarABI) {
      guardTy = CGF.SizeTy;
      guardAlignment = CGF.getSizeAlign();
    } else {
      guardTy = CGF.Int64Ty;
      guardAlignment =
          CharUnits::fromQuantity(CGM.getDataLayout().getABITypeAlign(guardTy));
    }
  }
  llvm::PointerType *guardPtrTy = llvm::PointerType::get(
      CGF.CGM.getLLVMContext(),
      CGF.CGM.getDataLayout().getDefaultGlobalsAddressSpace());

  // Create the guard variable if we don't already have it (as we
  // might if we're double-emitting this function body).
  llvm::GlobalVariable *guard = CGM.getStaticLocalDeclGuardAddress(&D);
  if (!guard) {
    // Mangle the name for the guard.
    SmallString<256> guardName;
    {
      llvm::raw_svector_ostream out(guardName);
      getMangleContext().mangleStaticGuardVariable(&D, out);
    }

    // Create the guard variable with a zero-initializer.
    // Just absorb linkage, visibility and dll storage class  from the guarded
    // variable.
    guard = new llvm::GlobalVariable(CGM.getModule(), guardTy,
                                     false, var->getLinkage(),
                                     llvm::ConstantInt::get(guardTy, 0),
                                     guardName.str());
    guard->setDSOLocal(var->isDSOLocal());
    guard->setVisibility(var->getVisibility());
    guard->setDLLStorageClass(var->getDLLStorageClass());
    // If the variable is thread-local, so is its guard variable.
    guard->setThreadLocalMode(var->getThreadLocalMode());
    guard->setAlignment(guardAlignment.getAsAlign());

    // The ABI says: "It is suggested that it be emitted in the same COMDAT
    // group as the associated data object." In practice, this doesn't work for
    // non-ELF and non-Wasm object formats, so only do it for ELF and Wasm.
    llvm::Comdat *C = var->getComdat();
    if (!D.isLocalVarDecl() && C &&
        (CGM.getTarget().getTriple().isOSBinFormatELF() ||
         CGM.getTarget().getTriple().isOSBinFormatWasm())) {
      guard->setComdat(C);
    } else if (CGM.supportsCOMDAT() && guard->isWeakForLinker()) {
      guard->setComdat(CGM.getModule().getOrInsertComdat(guard->getName()));
    }

    CGM.setStaticLocalDeclGuardAddress(&D, guard);
  }

  Address guardAddr = Address(guard, guard->getValueType(), guardAlignment);

  // Test whether the variable has completed initialization.
  //
  // Itanium C++ ABI 3.3.2:
  //   The following is pseudo-code showing how these functions can be used:
  //     if (obj_guard.first_byte == 0) {
  //       if ( __cxa_guard_acquire (&obj_guard) ) {
  //         try {
  //           ... initialize the object ...;
  //         } catch (...) {
  //            __cxa_guard_abort (&obj_guard);
  //            throw;
  //         }
  //         ... queue object destructor with __cxa_atexit() ...;
  //         __cxa_guard_release (&obj_guard);
  //       }
  //     }
  //
  // If threadsafe statics are enabled, but we don't have inline atomics, just
  // call __cxa_guard_acquire unconditionally.  The "inline" check isn't
  // actually inline, and the user might not expect calls to __atomic libcalls.

  unsigned MaxInlineWidthInBits = CGF.getTarget().getMaxAtomicInlineWidth();
  llvm::BasicBlock *EndBlock = CGF.createBasicBlock("init.end");
  if (!threadsafe || MaxInlineWidthInBits) {
    // Load the first byte of the guard variable.
    llvm::LoadInst *LI =
        Builder.CreateLoad(guardAddr.withElementType(CGM.Int8Ty));

    // Itanium ABI:
    //   An implementation supporting thread-safety on multiprocessor
    //   systems must also guarantee that references to the initialized
    //   object do not occur before the load of the initialization flag.
    //
    // In LLVM, we do this by marking the load Acquire.
    if (threadsafe)
      LI->setAtomic(llvm::AtomicOrdering::Acquire);

    // For ARM, we should only check the first bit, rather than the entire byte:
    //
    // ARM C++ ABI 3.2.3.1:
    //   To support the potential use of initialization guard variables
    //   as semaphores that are the target of ARM SWP and LDREX/STREX
    //   synchronizing instructions we define a static initialization
    //   guard variable to be a 4-byte aligned, 4-byte word with the
    //   following inline access protocol.
    //     #define INITIALIZED 1
    //     if ((obj_guard & INITIALIZED) != INITIALIZED) {
    //       if (__cxa_guard_acquire(&obj_guard))
    //         ...
    //     }
    //
    // and similarly for ARM64:
    //
    // ARM64 C++ ABI 3.2.2:
    //   This ABI instead only specifies the value bit 0 of the static guard
    //   variable; all other bits are platform defined. Bit 0 shall be 0 when the
    //   variable is not initialized and 1 when it is.
    llvm::Value *V =
        (UseARMGuardVarABI && !useInt8GuardVariable)
            ? Builder.CreateAnd(LI, llvm::ConstantInt::get(CGM.Int8Ty, 1))
            : LI;
    llvm::Value *NeedsInit = Builder.CreateIsNull(V, "guard.uninitialized");

    llvm::BasicBlock *InitCheckBlock = CGF.createBasicBlock("init.check");

    // Check if the first byte of the guard variable is zero.
    CGF.EmitCXXGuardedInitBranch(NeedsInit, InitCheckBlock, EndBlock,
                                 CodeGenFunction::GuardKind::VariableGuard, &D);

    CGF.EmitBlock(InitCheckBlock);
  }

  // The semantics of dynamic initialization of variables with static or thread
  // storage duration depends on whether they are declared at block-scope. The
  // initialization of such variables at block-scope can be aborted with an
  // exception and later retried (per C++20 [stmt.dcl]p4), and recursive entry
  // to their initialization has undefined behavior (also per C++20
  // [stmt.dcl]p4). For such variables declared at non-block scope, exceptions
  // lead to termination (per C++20 [except.terminate]p1), and recursive
  // references to the variables are governed only by the lifetime rules (per
  // C++20 [class.cdtor]p2), which means such references are perfectly fine as
  // long as they avoid touching memory. As a result, block-scope variables must
  // not be marked as initialized until after initialization completes (unless
  // the mark is reverted following an exception), but non-block-scope variables
  // must be marked prior to initialization so that recursive accesses during
  // initialization do not restart initialization.

  // Variables used when coping with thread-safe statics and exceptions.
  if (threadsafe) {
    // Call __cxa_guard_acquire.
    llvm::Value *V
      = CGF.EmitNounwindRuntimeCall(getGuardAcquireFn(CGM, guardPtrTy), guard);

    llvm::BasicBlock *InitBlock = CGF.createBasicBlock("init");

    Builder.CreateCondBr(Builder.CreateIsNotNull(V, "tobool"),
                         InitBlock, EndBlock);

    // Call __cxa_guard_abort along the exceptional edge.
    CGF.EHStack.pushCleanup<CallGuardAbort>(EHCleanup, guard);

    CGF.EmitBlock(InitBlock);
  } else if (!D.isLocalVarDecl()) {
    // For non-local variables, store 1 into the first byte of the guard
    // variable before the object initialization begins so that references
    // to the variable during initialization don't restart initialization.
    Builder.CreateStore(llvm::ConstantInt::get(CGM.Int8Ty, 1),
                        guardAddr.withElementType(CGM.Int8Ty));
  }

  // Emit the initializer and add a global destructor if appropriate.
  CGF.EmitCXXGlobalVarDeclInit(D, var, shouldPerformInit);

  if (threadsafe) {
    // Pop the guard-abort cleanup if we pushed one.
    CGF.PopCleanupBlock();

    // Call __cxa_guard_release.  This cannot throw.
    CGF.EmitNounwindRuntimeCall(getGuardReleaseFn(CGM, guardPtrTy),
                                guardAddr.emitRawPointer(CGF));
  } else if (D.isLocalVarDecl()) {
    // For local variables, store 1 into the first byte of the guard variable
    // after the object initialization completes so that initialization is
    // retried if initialization is interrupted by an exception.
    Builder.CreateStore(llvm::ConstantInt::get(CGM.Int8Ty, 1),
                        guardAddr.withElementType(CGM.Int8Ty));
  }

  CGF.EmitBlock(EndBlock);
}

/// Register a global destructor using __cxa_atexit.
static void emitGlobalDtorWithCXAAtExit(CodeGenFunction &CGF,
                                        llvm::FunctionCallee dtor,
                                        llvm::Constant *addr, bool TLS) {
  assert(!CGF.getTarget().getTriple().isOSAIX() &&
         "unexpected call to emitGlobalDtorWithCXAAtExit");
  assert((TLS || CGF.getTypes().getCodeGenOpts().CXAAtExit) &&
         "__cxa_atexit is disabled");
  const char *Name = "__cxa_atexit";
  if (TLS) {
    const llvm::Triple &T = CGF.getTarget().getTriple();
    Name = T.isOSDarwin() ?  "_tlv_atexit" : "__cxa_thread_atexit";
  }

  // We're assuming that the destructor function is something we can
  // reasonably call with the default CC.
  llvm::Type *dtorTy = CGF.UnqualPtrTy;

  // Preserve address space of addr.
  auto AddrAS = addr ? addr->getType()->getPointerAddressSpace() : 0;
  auto AddrPtrTy = AddrAS ? llvm::PointerType::get(CGF.getLLVMContext(), AddrAS)
                          : CGF.Int8PtrTy;

  // Create a variable that binds the atexit to this shared object.
  llvm::Constant *handle =
      CGF.CGM.CreateRuntimeVariable(CGF.Int8Ty, "__dso_handle");
  auto *GV = cast<llvm::GlobalValue>(handle->stripPointerCasts());
  GV->setVisibility(llvm::GlobalValue::HiddenVisibility);

  // extern "C" int __cxa_atexit(void (*f)(void *), void *p, void *d);
  llvm::Type *paramTys[] = {dtorTy, AddrPtrTy, handle->getType()};
  llvm::FunctionType *atexitTy =
    llvm::FunctionType::get(CGF.IntTy, paramTys, false);

  // Fetch the actual function.
  llvm::FunctionCallee atexit = CGF.CGM.CreateRuntimeFunction(atexitTy, Name);
  if (llvm::Function *fn = dyn_cast<llvm::Function>(atexit.getCallee()))
    fn->setDoesNotThrow();

  const auto &Context = CGF.CGM.getContext();
  FunctionProtoType::ExtProtoInfo EPI(Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/false));
  QualType fnType =
      Context.getFunctionType(Context.VoidTy, {Context.VoidPtrTy}, EPI);
  llvm::Constant *dtorCallee = cast<llvm::Constant>(dtor.getCallee());
  dtorCallee = CGF.CGM.getFunctionPointer(dtorCallee, fnType);

  if (!addr)
    // addr is null when we are trying to register a dtor annotated with
    // __attribute__((destructor)) in a constructor function. Using null here is
    // okay because this argument is just passed back to the destructor
    // function.
    addr = llvm::Constant::getNullValue(CGF.Int8PtrTy);

  llvm::Value *args[] = {dtorCallee, addr, handle};
  CGF.EmitNounwindRuntimeCall(atexit, args);
}

static llvm::Function *createGlobalInitOrCleanupFn(CodeGen::CodeGenModule &CGM,
                                                   StringRef FnName) {
  // Create a function that registers/unregisters destructors that have the same
  // priority.
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGM.VoidTy, false);
  llvm::Function *GlobalInitOrCleanupFn = CGM.CreateGlobalInitOrCleanUpFunction(
      FTy, FnName, CGM.getTypes().arrangeNullaryFunction(), SourceLocation());

  return GlobalInitOrCleanupFn;
}

void CodeGenModule::unregisterGlobalDtorsWithUnAtExit() {
  for (const auto &I : DtorsUsingAtExit) {
    int Priority = I.first;
    std::string GlobalCleanupFnName =
        std::string("__GLOBAL_cleanup_") + llvm::to_string(Priority);

    llvm::Function *GlobalCleanupFn =
        createGlobalInitOrCleanupFn(*this, GlobalCleanupFnName);

    CodeGenFunction CGF(*this);
    CGF.StartFunction(GlobalDecl(), getContext().VoidTy, GlobalCleanupFn,
                      getTypes().arrangeNullaryFunction(), FunctionArgList(),
                      SourceLocation(), SourceLocation());
    auto AL = ApplyDebugLocation::CreateArtificial(CGF);

    // Get the destructor function type, void(*)(void).
    llvm::FunctionType *dtorFuncTy = llvm::FunctionType::get(CGF.VoidTy, false);

    // Destructor functions are run/unregistered in non-ascending
    // order of their priorities.
    const llvm::TinyPtrVector<llvm::Function *> &Dtors = I.second;
    auto itv = Dtors.rbegin();
    while (itv != Dtors.rend()) {
      llvm::Function *Dtor = *itv;

      // We're assuming that the destructor function is something we can
      // reasonably call with the correct CC.
      llvm::Value *V = CGF.unregisterGlobalDtorWithUnAtExit(Dtor);
      llvm::Value *NeedsDestruct =
          CGF.Builder.CreateIsNull(V, "needs_destruct");

      llvm::BasicBlock *DestructCallBlock =
          CGF.createBasicBlock("destruct.call");
      llvm::BasicBlock *EndBlock = CGF.createBasicBlock(
          (itv + 1) != Dtors.rend() ? "unatexit.call" : "destruct.end");
      // Check if unatexit returns a value of 0. If it does, jump to
      // DestructCallBlock, otherwise jump to EndBlock directly.
      CGF.Builder.CreateCondBr(NeedsDestruct, DestructCallBlock, EndBlock);

      CGF.EmitBlock(DestructCallBlock);

      // Emit the call to casted Dtor.
      llvm::CallInst *CI = CGF.Builder.CreateCall(dtorFuncTy, Dtor);
      // Make sure the call and the callee agree on calling convention.
      CI->setCallingConv(Dtor->getCallingConv());

      CGF.EmitBlock(EndBlock);

      itv++;
    }

    CGF.FinishFunction();
    AddGlobalDtor(GlobalCleanupFn, Priority);
  }
}

void CodeGenModule::registerGlobalDtorsWithAtExit() {
  for (const auto &I : DtorsUsingAtExit) {
    int Priority = I.first;
    std::string GlobalInitFnName =
        std::string("__GLOBAL_init_") + llvm::to_string(Priority);
    llvm::Function *GlobalInitFn =
        createGlobalInitOrCleanupFn(*this, GlobalInitFnName);

    CodeGenFunction CGF(*this);
    CGF.StartFunction(GlobalDecl(), getContext().VoidTy, GlobalInitFn,
                      getTypes().arrangeNullaryFunction(), FunctionArgList(),
                      SourceLocation(), SourceLocation());
    auto AL = ApplyDebugLocation::CreateArtificial(CGF);

    // Since constructor functions are run in non-descending order of their
    // priorities, destructors are registered in non-descending order of their
    // priorities, and since destructor functions are run in the reverse order
    // of their registration, destructor functions are run in non-ascending
    // order of their priorities.
    const llvm::TinyPtrVector<llvm::Function *> &Dtors = I.second;
    for (auto *Dtor : Dtors) {
      // Register the destructor function calling __cxa_atexit if it is
      // available. Otherwise fall back on calling atexit.
      if (getCodeGenOpts().CXAAtExit) {
        emitGlobalDtorWithCXAAtExit(CGF, Dtor, nullptr, false);
      } else {
        // We're assuming that the destructor function is something we can
        // reasonably call with the correct CC.
        CGF.registerGlobalDtorWithAtExit(Dtor);
      }
    }

    CGF.FinishFunction();
    AddGlobalCtor(GlobalInitFn, Priority);
  }

  if (getCXXABI().useSinitAndSterm())
    unregisterGlobalDtorsWithUnAtExit();
}

/// Register a global destructor as best as we know how.
void ItaniumCXXABI::registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                                       llvm::FunctionCallee dtor,
                                       llvm::Constant *addr) {
  if (D.isNoDestroy(CGM.getContext()))
    return;

  // OpenMP offloading supports C++ constructors and destructors but we do not
  // always have 'atexit' available. Instead lower these to use the LLVM global
  // destructors which we can handle directly in the runtime. Note that this is
  // not strictly 1-to-1 with using `atexit` because we no longer tear down
  // globals in reverse order of when they were constructed.
  if (!CGM.getLangOpts().hasAtExit() && !D.isStaticLocal())
    return CGF.registerGlobalDtorWithLLVM(D, dtor, addr);

  // emitGlobalDtorWithCXAAtExit will emit a call to either __cxa_thread_atexit
  // or __cxa_atexit depending on whether this VarDecl is a thread-local storage
  // or not. CXAAtExit controls only __cxa_atexit, so use it if it is enabled.
  // We can always use __cxa_thread_atexit.
  if (CGM.getCodeGenOpts().CXAAtExit || D.getTLSKind())
    return emitGlobalDtorWithCXAAtExit(CGF, dtor, addr, D.getTLSKind());

  // In Apple kexts, we want to add a global destructor entry.
  // FIXME: shouldn't this be guarded by some variable?
  if (CGM.getLangOpts().AppleKext) {
    // Generate a global destructor entry.
    return CGM.AddCXXDtorEntry(dtor, addr);
  }

  CGF.registerGlobalDtorWithAtExit(D, dtor, addr);
}

static bool isThreadWrapperReplaceable(const VarDecl *VD,
                                       CodeGen::CodeGenModule &CGM) {
  assert(!VD->isStaticLocal() && "static local VarDecls don't need wrappers!");
  // Darwin prefers to have references to thread local variables to go through
  // the thread wrapper instead of directly referencing the backing variable.
  return VD->getTLSKind() == VarDecl::TLS_Dynamic &&
         CGM.getTarget().getTriple().isOSDarwin();
}

/// Get the appropriate linkage for the wrapper function. This is essentially
/// the weak form of the variable's linkage; every translation unit which needs
/// the wrapper emits a copy, and we want the linker to merge them.
static llvm::GlobalValue::LinkageTypes
getThreadLocalWrapperLinkage(const VarDecl *VD, CodeGen::CodeGenModule &CGM) {
  llvm::GlobalValue::LinkageTypes VarLinkage =
      CGM.getLLVMLinkageVarDefinition(VD);

  // For internal linkage variables, we don't need an external or weak wrapper.
  if (llvm::GlobalValue::isLocalLinkage(VarLinkage))
    return VarLinkage;

  // If the thread wrapper is replaceable, give it appropriate linkage.
  if (isThreadWrapperReplaceable(VD, CGM))
    if (!llvm::GlobalVariable::isLinkOnceLinkage(VarLinkage) &&
        !llvm::GlobalVariable::isWeakODRLinkage(VarLinkage))
      return VarLinkage;
  return llvm::GlobalValue::WeakODRLinkage;
}

llvm::Function *
ItaniumCXXABI::getOrCreateThreadLocalWrapper(const VarDecl *VD,
                                             llvm::Value *Val) {
  // Mangle the name for the thread_local wrapper function.
  SmallString<256> WrapperName;
  {
    llvm::raw_svector_ostream Out(WrapperName);
    getMangleContext().mangleItaniumThreadLocalWrapper(VD, Out);
  }

  // FIXME: If VD is a definition, we should regenerate the function attributes
  // before returning.
  if (llvm::Value *V = CGM.getModule().getNamedValue(WrapperName))
    return cast<llvm::Function>(V);

  QualType RetQT = VD->getType();
  if (RetQT->isReferenceType())
    RetQT = RetQT.getNonReferenceType();

  const CGFunctionInfo &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
      getContext().getPointerType(RetQT), FunctionArgList());

  llvm::FunctionType *FnTy = CGM.getTypes().GetFunctionType(FI);
  llvm::Function *Wrapper =
      llvm::Function::Create(FnTy, getThreadLocalWrapperLinkage(VD, CGM),
                             WrapperName.str(), &CGM.getModule());

  if (CGM.supportsCOMDAT() && Wrapper->isWeakForLinker())
    Wrapper->setComdat(CGM.getModule().getOrInsertComdat(Wrapper->getName()));

  CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, Wrapper, /*IsThunk=*/false);

  // Always resolve references to the wrapper at link time.
  if (!Wrapper->hasLocalLinkage())
    if (!isThreadWrapperReplaceable(VD, CGM) ||
        llvm::GlobalVariable::isLinkOnceLinkage(Wrapper->getLinkage()) ||
        llvm::GlobalVariable::isWeakODRLinkage(Wrapper->getLinkage()) ||
        VD->getVisibility() == HiddenVisibility)
      Wrapper->setVisibility(llvm::GlobalValue::HiddenVisibility);

  if (isThreadWrapperReplaceable(VD, CGM)) {
    Wrapper->setCallingConv(llvm::CallingConv::CXX_FAST_TLS);
    Wrapper->addFnAttr(llvm::Attribute::NoUnwind);
  }

  ThreadWrappers.push_back({VD, Wrapper});
  return Wrapper;
}

void ItaniumCXXABI::EmitThreadLocalInitFuncs(
    CodeGenModule &CGM, ArrayRef<const VarDecl *> CXXThreadLocals,
    ArrayRef<llvm::Function *> CXXThreadLocalInits,
    ArrayRef<const VarDecl *> CXXThreadLocalInitVars) {
  llvm::Function *InitFunc = nullptr;

  // Separate initializers into those with ordered (or partially-ordered)
  // initialization and those with unordered initialization.
  llvm::SmallVector<llvm::Function *, 8> OrderedInits;
  llvm::SmallDenseMap<const VarDecl *, llvm::Function *> UnorderedInits;
  for (unsigned I = 0; I != CXXThreadLocalInits.size(); ++I) {
    if (isTemplateInstantiation(
            CXXThreadLocalInitVars[I]->getTemplateSpecializationKind()))
      UnorderedInits[CXXThreadLocalInitVars[I]->getCanonicalDecl()] =
          CXXThreadLocalInits[I];
    else
      OrderedInits.push_back(CXXThreadLocalInits[I]);
  }

  if (!OrderedInits.empty()) {
    // Generate a guarded initialization function.
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
    const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
    InitFunc = CGM.CreateGlobalInitOrCleanUpFunction(FTy, "__tls_init", FI,
                                                     SourceLocation(),
                                                     /*TLS=*/true);
    llvm::GlobalVariable *Guard = new llvm::GlobalVariable(
        CGM.getModule(), CGM.Int8Ty, /*isConstant=*/false,
        llvm::GlobalVariable::InternalLinkage,
        llvm::ConstantInt::get(CGM.Int8Ty, 0), "__tls_guard");
    Guard->setThreadLocal(true);
    Guard->setThreadLocalMode(CGM.GetDefaultLLVMTLSModel());

    CharUnits GuardAlign = CharUnits::One();
    Guard->setAlignment(GuardAlign.getAsAlign());

    CodeGenFunction(CGM).GenerateCXXGlobalInitFunc(
        InitFunc, OrderedInits, ConstantAddress(Guard, CGM.Int8Ty, GuardAlign));
    // On Darwin platforms, use CXX_FAST_TLS calling convention.
    if (CGM.getTarget().getTriple().isOSDarwin()) {
      InitFunc->setCallingConv(llvm::CallingConv::CXX_FAST_TLS);
      InitFunc->addFnAttr(llvm::Attribute::NoUnwind);
    }
  }

  // Create declarations for thread wrappers for all thread-local variables
  // with non-discardable definitions in this translation unit.
  for (const VarDecl *VD : CXXThreadLocals) {
    if (VD->hasDefinition() &&
        !isDiscardableGVALinkage(getContext().GetGVALinkageForVariable(VD))) {
      llvm::GlobalValue *GV = CGM.GetGlobalValue(CGM.getMangledName(VD));
      getOrCreateThreadLocalWrapper(VD, GV);
    }
  }

  // Emit all referenced thread wrappers.
  for (auto VDAndWrapper : ThreadWrappers) {
    const VarDecl *VD = VDAndWrapper.first;
    llvm::GlobalVariable *Var =
        cast<llvm::GlobalVariable>(CGM.GetGlobalValue(CGM.getMangledName(VD)));
    llvm::Function *Wrapper = VDAndWrapper.second;

    // Some targets require that all access to thread local variables go through
    // the thread wrapper.  This means that we cannot attempt to create a thread
    // wrapper or a thread helper.
    if (!VD->hasDefinition()) {
      if (isThreadWrapperReplaceable(VD, CGM)) {
        Wrapper->setLinkage(llvm::Function::ExternalLinkage);
        continue;
      }

      // If this isn't a TU in which this variable is defined, the thread
      // wrapper is discardable.
      if (Wrapper->getLinkage() == llvm::Function::WeakODRLinkage)
        Wrapper->setLinkage(llvm::Function::LinkOnceODRLinkage);
    }

    CGM.SetLLVMFunctionAttributesForDefinition(nullptr, Wrapper);

    // Mangle the name for the thread_local initialization function.
    SmallString<256> InitFnName;
    {
      llvm::raw_svector_ostream Out(InitFnName);
      getMangleContext().mangleItaniumThreadLocalInit(VD, Out);
    }

    llvm::FunctionType *InitFnTy = llvm::FunctionType::get(CGM.VoidTy, false);

    // If we have a definition for the variable, emit the initialization
    // function as an alias to the global Init function (if any). Otherwise,
    // produce a declaration of the initialization function.
    llvm::GlobalValue *Init = nullptr;
    bool InitIsInitFunc = false;
    bool HasConstantInitialization = false;
    if (!usesThreadWrapperFunction(VD)) {
      HasConstantInitialization = true;
    } else if (VD->hasDefinition()) {
      InitIsInitFunc = true;
      llvm::Function *InitFuncToUse = InitFunc;
      if (isTemplateInstantiation(VD->getTemplateSpecializationKind()))
        InitFuncToUse = UnorderedInits.lookup(VD->getCanonicalDecl());
      if (InitFuncToUse)
        Init = llvm::GlobalAlias::create(Var->getLinkage(), InitFnName.str(),
                                         InitFuncToUse);
    } else {
      // Emit a weak global function referring to the initialization function.
      // This function will not exist if the TU defining the thread_local
      // variable in question does not need any dynamic initialization for
      // its thread_local variables.
      Init = llvm::Function::Create(InitFnTy,
                                    llvm::GlobalVariable::ExternalWeakLinkage,
                                    InitFnName.str(), &CGM.getModule());
      const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
      CGM.SetLLVMFunctionAttributes(
          GlobalDecl(), FI, cast<llvm::Function>(Init), /*IsThunk=*/false);
    }

    if (Init) {
      Init->setVisibility(Var->getVisibility());
      // Don't mark an extern_weak function DSO local on windows.
      if (!CGM.getTriple().isOSWindows() || !Init->hasExternalWeakLinkage())
        Init->setDSOLocal(Var->isDSOLocal());
    }

    llvm::LLVMContext &Context = CGM.getModule().getContext();

    // The linker on AIX is not happy with missing weak symbols.  However,
    // other TUs will not know whether the initialization routine exists
    // so create an empty, init function to satisfy the linker.
    // This is needed whenever a thread wrapper function is not used, and
    // also when the symbol is weak.
    if (CGM.getTriple().isOSAIX() && VD->hasDefinition() &&
        isEmittedWithConstantInitializer(VD, true) &&
        !mayNeedDestruction(VD)) {
      // Init should be null.  If it were non-null, then the logic above would
      // either be defining the function to be an alias or declaring the
      // function with the expectation that the definition of the variable
      // is elsewhere.
      assert(Init == nullptr && "Expected Init to be null.");

      llvm::Function *Func = llvm::Function::Create(
          InitFnTy, Var->getLinkage(), InitFnName.str(), &CGM.getModule());
      const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
      CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI,
                                    cast<llvm::Function>(Func),
                                    /*IsThunk=*/false);
      // Create a function body that just returns
      llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Context, "", Func);
      CGBuilderTy Builder(CGM, Entry);
      Builder.CreateRetVoid();
    }

    llvm::BasicBlock *Entry = llvm::BasicBlock::Create(Context, "", Wrapper);
    CGBuilderTy Builder(CGM, Entry);
    if (HasConstantInitialization) {
      // No dynamic initialization to invoke.
    } else if (InitIsInitFunc) {
      if (Init) {
        llvm::CallInst *CallVal = Builder.CreateCall(InitFnTy, Init);
        if (isThreadWrapperReplaceable(VD, CGM)) {
          CallVal->setCallingConv(llvm::CallingConv::CXX_FAST_TLS);
          llvm::Function *Fn =
              cast<llvm::Function>(cast<llvm::GlobalAlias>(Init)->getAliasee());
          Fn->setCallingConv(llvm::CallingConv::CXX_FAST_TLS);
        }
      }
    } else if (CGM.getTriple().isOSAIX()) {
      // On AIX, except if constinit and also neither of class type or of
      // (possibly multi-dimensional) array of class type, thread_local vars
      // will have init routines regardless of whether they are
      // const-initialized.  Since the routine is guaranteed to exist, we can
      // unconditionally call it without testing for its existance.  This
      // avoids potentially unresolved weak symbols which the AIX linker
      // isn't happy with.
      Builder.CreateCall(InitFnTy, Init);
    } else {
      // Don't know whether we have an init function. Call it if it exists.
      llvm::Value *Have = Builder.CreateIsNotNull(Init);
      llvm::BasicBlock *InitBB = llvm::BasicBlock::Create(Context, "", Wrapper);
      llvm::BasicBlock *ExitBB = llvm::BasicBlock::Create(Context, "", Wrapper);
      Builder.CreateCondBr(Have, InitBB, ExitBB);

      Builder.SetInsertPoint(InitBB);
      Builder.CreateCall(InitFnTy, Init);
      Builder.CreateBr(ExitBB);

      Builder.SetInsertPoint(ExitBB);
    }

    // For a reference, the result of the wrapper function is a pointer to
    // the referenced object.
    llvm::Value *Val = Builder.CreateThreadLocalAddress(Var);

    if (VD->getType()->isReferenceType()) {
      CharUnits Align = CGM.getContext().getDeclAlign(VD);
      Val = Builder.CreateAlignedLoad(Var->getValueType(), Val, Align);
    }

    Builder.CreateRet(Val);
  }
}

LValue ItaniumCXXABI::EmitThreadLocalVarDeclLValue(CodeGenFunction &CGF,
                                                   const VarDecl *VD,
                                                   QualType LValType) {
  llvm::Value *Val = CGF.CGM.GetAddrOfGlobalVar(VD);
  llvm::Function *Wrapper = getOrCreateThreadLocalWrapper(VD, Val);

  llvm::CallInst *CallVal = CGF.Builder.CreateCall(Wrapper);
  CallVal->setCallingConv(Wrapper->getCallingConv());

  LValue LV;
  if (VD->getType()->isReferenceType())
    LV = CGF.MakeNaturalAlignRawAddrLValue(CallVal, LValType);
  else
    LV = CGF.MakeRawAddrLValue(CallVal, LValType,
                               CGF.getContext().getDeclAlign(VD));
  // FIXME: need setObjCGCLValueClass?
  return LV;
}

/// Return whether the given global decl needs a VTT parameter, which it does
/// if it's a base constructor or destructor with virtual bases.
bool ItaniumCXXABI::NeedsVTTParameter(GlobalDecl GD) {
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());

  // We don't have any virtual bases, just return early.
  if (!MD->getParent()->getNumVBases())
    return false;

  // Check if we have a base constructor.
  if (isa<CXXConstructorDecl>(MD) && GD.getCtorType() == Ctor_Base)
    return true;

  // Check if we have a base destructor.
  if (isa<CXXDestructorDecl>(MD) && GD.getDtorType() == Dtor_Base)
    return true;

  return false;
}

llvm::Constant *
ItaniumCXXABI::getOrCreateVirtualFunctionPointerThunk(const CXXMethodDecl *MD) {
  SmallString<256> MethodName;
  llvm::raw_svector_ostream Out(MethodName);
  getMangleContext().mangleCXXName(MD, Out);
  MethodName += "_vfpthunk_";
  StringRef ThunkName = MethodName.str();
  llvm::Function *ThunkFn;
  if ((ThunkFn = cast_or_null<llvm::Function>(
           CGM.getModule().getNamedValue(ThunkName))))
    return ThunkFn;

  const CGFunctionInfo &FnInfo = CGM.getTypes().arrangeCXXMethodDeclaration(MD);
  llvm::FunctionType *ThunkTy = CGM.getTypes().GetFunctionType(FnInfo);
  llvm::GlobalValue::LinkageTypes Linkage =
      MD->isExternallyVisible() ? llvm::GlobalValue::LinkOnceODRLinkage
                                : llvm::GlobalValue::InternalLinkage;
  ThunkFn =
      llvm::Function::Create(ThunkTy, Linkage, ThunkName, &CGM.getModule());
  if (Linkage == llvm::GlobalValue::LinkOnceODRLinkage)
    ThunkFn->setVisibility(llvm::GlobalValue::HiddenVisibility);
  assert(ThunkFn->getName() == ThunkName && "name was uniqued!");

  CGM.SetLLVMFunctionAttributes(MD, FnInfo, ThunkFn, /*IsThunk=*/true);
  CGM.SetLLVMFunctionAttributesForDefinition(MD, ThunkFn);

  // Stack protection sometimes gets inserted after the musttail call.
  ThunkFn->removeFnAttr(llvm::Attribute::StackProtect);
  ThunkFn->removeFnAttr(llvm::Attribute::StackProtectStrong);
  ThunkFn->removeFnAttr(llvm::Attribute::StackProtectReq);

  // Start codegen.
  CodeGenFunction CGF(CGM);
  CGF.CurGD = GlobalDecl(MD);
  CGF.CurFuncIsThunk = true;

  // Build FunctionArgs.
  FunctionArgList FunctionArgs;
  CGF.BuildFunctionArgList(CGF.CurGD, FunctionArgs);

  CGF.StartFunction(GlobalDecl(), FnInfo.getReturnType(), ThunkFn, FnInfo,
                    FunctionArgs, MD->getLocation(), SourceLocation());
  llvm::Value *ThisVal = loadIncomingCXXThis(CGF);
  setCXXABIThisValue(CGF, ThisVal);

  CallArgList CallArgs;
  for (const VarDecl *VD : FunctionArgs)
    CGF.EmitDelegateCallArg(CallArgs, VD, SourceLocation());

  const FunctionProtoType *FPT = MD->getType()->getAs<FunctionProtoType>();
  RequiredArgs Required = RequiredArgs::forPrototypePlus(FPT, /*this*/ 1);
  const CGFunctionInfo &CallInfo =
      CGM.getTypes().arrangeCXXMethodCall(CallArgs, FPT, Required, 0);
  CGCallee Callee = CGCallee::forVirtual(nullptr, GlobalDecl(MD),
                                         getThisAddress(CGF), ThunkTy);
  llvm::CallBase *CallOrInvoke;
  CGF.EmitCall(CallInfo, Callee, ReturnValueSlot(), CallArgs, &CallOrInvoke,
               /*IsMustTail=*/true, SourceLocation(), true);
  auto *Call = cast<llvm::CallInst>(CallOrInvoke);
  Call->setTailCallKind(llvm::CallInst::TCK_MustTail);
  if (Call->getType()->isVoidTy())
    CGF.Builder.CreateRetVoid();
  else
    CGF.Builder.CreateRet(Call);

  // Finish the function to maintain CodeGenFunction invariants.
  // FIXME: Don't emit unreachable code.
  CGF.EmitBlock(CGF.createBasicBlock());
  CGF.FinishFunction();
  return ThunkFn;
}

namespace {
class ItaniumRTTIBuilder {
  CodeGenModule &CGM;  // Per-module state.
  llvm::LLVMContext &VMContext;
  const ItaniumCXXABI &CXXABI;  // Per-module state.

  /// Fields - The fields of the RTTI descriptor currently being built.
  SmallVector<llvm::Constant *, 16> Fields;

  /// GetAddrOfTypeName - Returns the mangled type name of the given type.
  llvm::GlobalVariable *
  GetAddrOfTypeName(QualType Ty, llvm::GlobalVariable::LinkageTypes Linkage);

  /// GetAddrOfExternalRTTIDescriptor - Returns the constant for the RTTI
  /// descriptor of the given type.
  llvm::Constant *GetAddrOfExternalRTTIDescriptor(QualType Ty);

  /// BuildVTablePointer - Build the vtable pointer for the given type.
  void BuildVTablePointer(const Type *Ty);

  /// BuildSIClassTypeInfo - Build an abi::__si_class_type_info, used for single
  /// inheritance, according to the Itanium C++ ABI, 2.9.5p6b.
  void BuildSIClassTypeInfo(const CXXRecordDecl *RD);

  /// BuildVMIClassTypeInfo - Build an abi::__vmi_class_type_info, used for
  /// classes with bases that do not satisfy the abi::__si_class_type_info
  /// constraints, according ti the Itanium C++ ABI, 2.9.5p5c.
  void BuildVMIClassTypeInfo(const CXXRecordDecl *RD);

  /// BuildPointerTypeInfo - Build an abi::__pointer_type_info struct, used
  /// for pointer types.
  void BuildPointerTypeInfo(QualType PointeeTy);

  /// BuildObjCObjectTypeInfo - Build the appropriate kind of
  /// type_info for an object type.
  void BuildObjCObjectTypeInfo(const ObjCObjectType *Ty);

  /// BuildPointerToMemberTypeInfo - Build an abi::__pointer_to_member_type_info
  /// struct, used for member pointer types.
  void BuildPointerToMemberTypeInfo(const MemberPointerType *Ty);

public:
  ItaniumRTTIBuilder(const ItaniumCXXABI &ABI)
      : CGM(ABI.CGM), VMContext(CGM.getModule().getContext()), CXXABI(ABI) {}

  // Pointer type info flags.
  enum {
    /// PTI_Const - Type has const qualifier.
    PTI_Const = 0x1,

    /// PTI_Volatile - Type has volatile qualifier.
    PTI_Volatile = 0x2,

    /// PTI_Restrict - Type has restrict qualifier.
    PTI_Restrict = 0x4,

    /// PTI_Incomplete - Type is incomplete.
    PTI_Incomplete = 0x8,

    /// PTI_ContainingClassIncomplete - Containing class is incomplete.
    /// (in pointer to member).
    PTI_ContainingClassIncomplete = 0x10,

    /// PTI_TransactionSafe - Pointee is transaction_safe function (C++ TM TS).
    //PTI_TransactionSafe = 0x20,

    /// PTI_Noexcept - Pointee is noexcept function (C++1z).
    PTI_Noexcept = 0x40,
  };

  // VMI type info flags.
  enum {
    /// VMI_NonDiamondRepeat - Class has non-diamond repeated inheritance.
    VMI_NonDiamondRepeat = 0x1,

    /// VMI_DiamondShaped - Class is diamond shaped.
    VMI_DiamondShaped = 0x2
  };

  // Base class type info flags.
  enum {
    /// BCTI_Virtual - Base class is virtual.
    BCTI_Virtual = 0x1,

    /// BCTI_Public - Base class is public.
    BCTI_Public = 0x2
  };

  /// BuildTypeInfo - Build the RTTI type info struct for the given type, or
  /// link to an existing RTTI descriptor if one already exists.
  llvm::Constant *BuildTypeInfo(QualType Ty);

  /// BuildTypeInfo - Build the RTTI type info struct for the given type.
  llvm::Constant *BuildTypeInfo(
      QualType Ty,
      llvm::GlobalVariable::LinkageTypes Linkage,
      llvm::GlobalValue::VisibilityTypes Visibility,
      llvm::GlobalValue::DLLStorageClassTypes DLLStorageClass);
};
}

llvm::GlobalVariable *ItaniumRTTIBuilder::GetAddrOfTypeName(
    QualType Ty, llvm::GlobalVariable::LinkageTypes Linkage) {
  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  CGM.getCXXABI().getMangleContext().mangleCXXRTTIName(Ty, Out);

  // We know that the mangled name of the type starts at index 4 of the
  // mangled name of the typename, so we can just index into it in order to
  // get the mangled name of the type.
  llvm::Constant *Init = llvm::ConstantDataArray::getString(VMContext,
                                                            Name.substr(4));
  auto Align = CGM.getContext().getTypeAlignInChars(CGM.getContext().CharTy);

  llvm::GlobalVariable *GV = CGM.CreateOrReplaceCXXRuntimeVariable(
      Name, Init->getType(), Linkage, Align.getAsAlign());

  GV->setInitializer(Init);

  return GV;
}

llvm::Constant *
ItaniumRTTIBuilder::GetAddrOfExternalRTTIDescriptor(QualType Ty) {
  // Mangle the RTTI name.
  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  CGM.getCXXABI().getMangleContext().mangleCXXRTTI(Ty, Out);

  // Look for an existing global.
  llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(Name);

  if (!GV) {
    // Create a new global variable.
    // Note for the future: If we would ever like to do deferred emission of
    // RTTI, check if emitting vtables opportunistically need any adjustment.

    GV = new llvm::GlobalVariable(
        CGM.getModule(), CGM.GlobalsInt8PtrTy,
        /*isConstant=*/true, llvm::GlobalValue::ExternalLinkage, nullptr, Name);
    const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
    CGM.setGVProperties(GV, RD);
    // Import the typeinfo symbol when all non-inline virtual methods are
    // imported.
    if (CGM.getTarget().hasPS4DLLImportExport()) {
      if (RD && CXXRecordNonInlineHasAttr<DLLImportAttr>(RD)) {
        GV->setDLLStorageClass(llvm::GlobalVariable::DLLImportStorageClass);
        CGM.setDSOLocal(GV);
      }
    }
  }

  return GV;
}

/// TypeInfoIsInStandardLibrary - Given a builtin type, returns whether the type
/// info for that type is defined in the standard library.
static bool TypeInfoIsInStandardLibrary(const BuiltinType *Ty) {
  // Itanium C++ ABI 2.9.2:
  //   Basic type information (e.g. for "int", "bool", etc.) will be kept in
  //   the run-time support library. Specifically, the run-time support
  //   library should contain type_info objects for the types X, X* and
  //   X const*, for every X in: void, std::nullptr_t, bool, wchar_t, char,
  //   unsigned char, signed char, short, unsigned short, int, unsigned int,
  //   long, unsigned long, long long, unsigned long long, float, double,
  //   long double, char16_t, char32_t, and the IEEE 754r decimal and
  //   half-precision floating point types.
  //
  // GCC also emits RTTI for __int128.
  // FIXME: We do not emit RTTI information for decimal types here.

  // Types added here must also be added to EmitFundamentalRTTIDescriptors.
  switch (Ty->getKind()) {
    case BuiltinType::Void:
    case BuiltinType::NullPtr:
    case BuiltinType::Bool:
    case BuiltinType::WChar_S:
    case BuiltinType::WChar_U:
    case BuiltinType::Char_U:
    case BuiltinType::Char_S:
    case BuiltinType::UChar:
    case BuiltinType::SChar:
    case BuiltinType::Short:
    case BuiltinType::UShort:
    case BuiltinType::Int:
    case BuiltinType::UInt:
    case BuiltinType::Long:
    case BuiltinType::ULong:
    case BuiltinType::LongLong:
    case BuiltinType::ULongLong:
    case BuiltinType::Half:
    case BuiltinType::Float:
    case BuiltinType::Double:
    case BuiltinType::LongDouble:
    case BuiltinType::Float16:
    case BuiltinType::Float128:
    case BuiltinType::Ibm128:
    case BuiltinType::Char8:
    case BuiltinType::Char16:
    case BuiltinType::Char32:
    case BuiltinType::Int128:
    case BuiltinType::UInt128:
      return true;

#define IMAGE_TYPE(ImgType, Id, SingletonId, Access, Suffix) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLImageTypes.def"
#define EXT_OPAQUE_TYPE(ExtType, Id, Ext) \
    case BuiltinType::Id:
#include "clang/Basic/OpenCLExtensionTypes.def"
    case BuiltinType::OCLSampler:
    case BuiltinType::OCLEvent:
    case BuiltinType::OCLClkEvent:
    case BuiltinType::OCLQueue:
    case BuiltinType::OCLReserveID:
#define SVE_TYPE(Name, Id, SingletonId) \
    case BuiltinType::Id:
#include "clang/Basic/AArch64SVEACLETypes.def"
#define PPC_VECTOR_TYPE(Name, Id, Size) \
    case BuiltinType::Id:
#include "clang/Basic/PPCTypes.def"
#define RVV_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/RISCVVTypes.def"
#define WASM_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/WebAssemblyReferenceTypes.def"
#define AMDGPU_TYPE(Name, Id, SingletonId) case BuiltinType::Id:
#include "clang/Basic/AMDGPUTypes.def"
    case BuiltinType::ShortAccum:
    case BuiltinType::Accum:
    case BuiltinType::LongAccum:
    case BuiltinType::UShortAccum:
    case BuiltinType::UAccum:
    case BuiltinType::ULongAccum:
    case BuiltinType::ShortFract:
    case BuiltinType::Fract:
    case BuiltinType::LongFract:
    case BuiltinType::UShortFract:
    case BuiltinType::UFract:
    case BuiltinType::ULongFract:
    case BuiltinType::SatShortAccum:
    case BuiltinType::SatAccum:
    case BuiltinType::SatLongAccum:
    case BuiltinType::SatUShortAccum:
    case BuiltinType::SatUAccum:
    case BuiltinType::SatULongAccum:
    case BuiltinType::SatShortFract:
    case BuiltinType::SatFract:
    case BuiltinType::SatLongFract:
    case BuiltinType::SatUShortFract:
    case BuiltinType::SatUFract:
    case BuiltinType::SatULongFract:
    case BuiltinType::BFloat16:
      return false;

    case BuiltinType::Dependent:
#define BUILTIN_TYPE(Id, SingletonId)
#define PLACEHOLDER_TYPE(Id, SingletonId) \
    case BuiltinType::Id:
#include "clang/AST/BuiltinTypes.def"
      llvm_unreachable("asking for RRTI for a placeholder type!");

    case BuiltinType::ObjCId:
    case BuiltinType::ObjCClass:
    case BuiltinType::ObjCSel:
      llvm_unreachable("FIXME: Objective-C types are unsupported!");
  }

  llvm_unreachable("Invalid BuiltinType Kind!");
}

static bool TypeInfoIsInStandardLibrary(const PointerType *PointerTy) {
  QualType PointeeTy = PointerTy->getPointeeType();
  const BuiltinType *BuiltinTy = dyn_cast<BuiltinType>(PointeeTy);
  if (!BuiltinTy)
    return false;

  // Check the qualifiers.
  Qualifiers Quals = PointeeTy.getQualifiers();
  Quals.removeConst();

  if (!Quals.empty())
    return false;

  return TypeInfoIsInStandardLibrary(BuiltinTy);
}

/// IsStandardLibraryRTTIDescriptor - Returns whether the type
/// information for the given type exists in the standard library.
static bool IsStandardLibraryRTTIDescriptor(QualType Ty) {
  // Type info for builtin types is defined in the standard library.
  if (const BuiltinType *BuiltinTy = dyn_cast<BuiltinType>(Ty))
    return TypeInfoIsInStandardLibrary(BuiltinTy);

  // Type info for some pointer types to builtin types is defined in the
  // standard library.
  if (const PointerType *PointerTy = dyn_cast<PointerType>(Ty))
    return TypeInfoIsInStandardLibrary(PointerTy);

  return false;
}

/// ShouldUseExternalRTTIDescriptor - Returns whether the type information for
/// the given type exists somewhere else, and that we should not emit the type
/// information in this translation unit.  Assumes that it is not a
/// standard-library type.
static bool ShouldUseExternalRTTIDescriptor(CodeGenModule &CGM,
                                            QualType Ty) {
  ASTContext &Context = CGM.getContext();

  // If RTTI is disabled, assume it might be disabled in the
  // translation unit that defines any potential key function, too.
  if (!Context.getLangOpts().RTTI) return false;

  if (const RecordType *RecordTy = dyn_cast<RecordType>(Ty)) {
    const CXXRecordDecl *RD = cast<CXXRecordDecl>(RecordTy->getDecl());
    if (!RD->hasDefinition())
      return false;

    if (!RD->isDynamicClass())
      return false;

    // FIXME: this may need to be reconsidered if the key function
    // changes.
    // N.B. We must always emit the RTTI data ourselves if there exists a key
    // function.
    bool IsDLLImport = RD->hasAttr<DLLImportAttr>();

    // Don't import the RTTI but emit it locally.
    if (CGM.getTriple().isWindowsGNUEnvironment())
      return false;

    if (CGM.getVTables().isVTableExternal(RD)) {
      if (CGM.getTarget().hasPS4DLLImportExport())
        return true;

      return IsDLLImport && !CGM.getTriple().isWindowsItaniumEnvironment()
                 ? false
                 : true;
    }
    if (IsDLLImport)
      return true;
  }

  return false;
}

/// IsIncompleteClassType - Returns whether the given record type is incomplete.
static bool IsIncompleteClassType(const RecordType *RecordTy) {
  return !RecordTy->getDecl()->isCompleteDefinition();
}

/// ContainsIncompleteClassType - Returns whether the given type contains an
/// incomplete class type. This is true if
///
///   * The given type is an incomplete class type.
///   * The given type is a pointer type whose pointee type contains an
///     incomplete class type.
///   * The given type is a member pointer type whose class is an incomplete
///     class type.
///   * The given type is a member pointer type whoise pointee type contains an
///     incomplete class type.
/// is an indirect or direct pointer to an incomplete class type.
static bool ContainsIncompleteClassType(QualType Ty) {
  if (const RecordType *RecordTy = dyn_cast<RecordType>(Ty)) {
    if (IsIncompleteClassType(RecordTy))
      return true;
  }

  if (const PointerType *PointerTy = dyn_cast<PointerType>(Ty))
    return ContainsIncompleteClassType(PointerTy->getPointeeType());

  if (const MemberPointerType *MemberPointerTy =
      dyn_cast<MemberPointerType>(Ty)) {
    // Check if the class type is incomplete.
    const RecordType *ClassType = cast<RecordType>(MemberPointerTy->getClass());
    if (IsIncompleteClassType(ClassType))
      return true;

    return ContainsIncompleteClassType(MemberPointerTy->getPointeeType());
  }

  return false;
}

// CanUseSingleInheritance - Return whether the given record decl has a "single,
// public, non-virtual base at offset zero (i.e. the derived class is dynamic
// iff the base is)", according to Itanium C++ ABI, 2.95p6b.
static bool CanUseSingleInheritance(const CXXRecordDecl *RD) {
  // Check the number of bases.
  if (RD->getNumBases() != 1)
    return false;

  // Get the base.
  CXXRecordDecl::base_class_const_iterator Base = RD->bases_begin();

  // Check that the base is not virtual.
  if (Base->isVirtual())
    return false;

  // Check that the base is public.
  if (Base->getAccessSpecifier() != AS_public)
    return false;

  // Check that the class is dynamic iff the base is.
  auto *BaseDecl =
      cast<CXXRecordDecl>(Base->getType()->castAs<RecordType>()->getDecl());
  if (!BaseDecl->isEmpty() &&
      BaseDecl->isDynamicClass() != RD->isDynamicClass())
    return false;

  return true;
}

void ItaniumRTTIBuilder::BuildVTablePointer(const Type *Ty) {
  // abi::__class_type_info.
  static const char * const ClassTypeInfo =
    "_ZTVN10__cxxabiv117__class_type_infoE";
  // abi::__si_class_type_info.
  static const char * const SIClassTypeInfo =
    "_ZTVN10__cxxabiv120__si_class_type_infoE";
  // abi::__vmi_class_type_info.
  static const char * const VMIClassTypeInfo =
    "_ZTVN10__cxxabiv121__vmi_class_type_infoE";

  const char *VTableName = nullptr;

  switch (Ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  case Type::LValueReference:
  case Type::RValueReference:
    llvm_unreachable("References shouldn't get here");

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    llvm_unreachable("Undeduced type shouldn't get here");

  case Type::Pipe:
    llvm_unreachable("Pipe types shouldn't get here");

  case Type::ArrayParameter:
    llvm_unreachable("Array Parameter types should not get here.");

  case Type::Builtin:
  case Type::BitInt:
  // GCC treats vector and complex types as fundamental types.
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::Complex:
  case Type::Atomic:
  // FIXME: GCC treats block pointers as fundamental types?!
  case Type::BlockPointer:
    // abi::__fundamental_type_info.
    VTableName = "_ZTVN10__cxxabiv123__fundamental_type_infoE";
    break;

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
    // abi::__array_type_info.
    VTableName = "_ZTVN10__cxxabiv117__array_type_infoE";
    break;

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // abi::__function_type_info.
    VTableName = "_ZTVN10__cxxabiv120__function_type_infoE";
    break;

  case Type::Enum:
    // abi::__enum_type_info.
    VTableName = "_ZTVN10__cxxabiv116__enum_type_infoE";
    break;

  case Type::Record: {
    const CXXRecordDecl *RD =
      cast<CXXRecordDecl>(cast<RecordType>(Ty)->getDecl());

    if (!RD->hasDefinition() || !RD->getNumBases()) {
      VTableName = ClassTypeInfo;
    } else if (CanUseSingleInheritance(RD)) {
      VTableName = SIClassTypeInfo;
    } else {
      VTableName = VMIClassTypeInfo;
    }

    break;
  }

  case Type::ObjCObject:
    // Ignore protocol qualifiers.
    Ty = cast<ObjCObjectType>(Ty)->getBaseType().getTypePtr();

    // Handle id and Class.
    if (isa<BuiltinType>(Ty)) {
      VTableName = ClassTypeInfo;
      break;
    }

    assert(isa<ObjCInterfaceType>(Ty));
    [[fallthrough]];

  case Type::ObjCInterface:
    if (cast<ObjCInterfaceType>(Ty)->getDecl()->getSuperClass()) {
      VTableName = SIClassTypeInfo;
    } else {
      VTableName = ClassTypeInfo;
    }
    break;

  case Type::ObjCObjectPointer:
  case Type::Pointer:
    // abi::__pointer_type_info.
    VTableName = "_ZTVN10__cxxabiv119__pointer_type_infoE";
    break;

  case Type::MemberPointer:
    // abi::__pointer_to_member_type_info.
    VTableName = "_ZTVN10__cxxabiv129__pointer_to_member_type_infoE";
    break;
  }

  llvm::Constant *VTable = nullptr;

  // Check if the alias exists. If it doesn't, then get or create the global.
  if (CGM.getItaniumVTableContext().isRelativeLayout())
    VTable = CGM.getModule().getNamedAlias(VTableName);
  if (!VTable) {
    llvm::Type *Ty = llvm::ArrayType::get(CGM.GlobalsInt8PtrTy, 0);
    VTable = CGM.getModule().getOrInsertGlobal(VTableName, Ty);
  }

  CGM.setDSOLocal(cast<llvm::GlobalValue>(VTable->stripPointerCasts()));

  llvm::Type *PtrDiffTy =
      CGM.getTypes().ConvertType(CGM.getContext().getPointerDiffType());

  // The vtable address point is 2.
  if (CGM.getItaniumVTableContext().isRelativeLayout()) {
    // The vtable address point is 8 bytes after its start:
    // 4 for the offset to top + 4 for the relative offset to rtti.
    llvm::Constant *Eight = llvm::ConstantInt::get(CGM.Int32Ty, 8);
    VTable =
        llvm::ConstantExpr::getInBoundsGetElementPtr(CGM.Int8Ty, VTable, Eight);
  } else {
    llvm::Constant *Two = llvm::ConstantInt::get(PtrDiffTy, 2);
    VTable = llvm::ConstantExpr::getInBoundsGetElementPtr(CGM.GlobalsInt8PtrTy,
                                                          VTable, Two);
  }

  if (auto &Schema = CGM.getCodeGenOpts().PointerAuth.CXXTypeInfoVTablePointer)
    VTable = CGM.getConstantSignedPointer(VTable, Schema, nullptr, GlobalDecl(),
                                          QualType(Ty, 0));

  Fields.push_back(VTable);
}

/// Return the linkage that the type info and type info name constants
/// should have for the given type.
static llvm::GlobalVariable::LinkageTypes getTypeInfoLinkage(CodeGenModule &CGM,
                                                             QualType Ty) {
  // Itanium C++ ABI 2.9.5p7:
  //   In addition, it and all of the intermediate abi::__pointer_type_info
  //   structs in the chain down to the abi::__class_type_info for the
  //   incomplete class type must be prevented from resolving to the
  //   corresponding type_info structs for the complete class type, possibly
  //   by making them local static objects. Finally, a dummy class RTTI is
  //   generated for the incomplete type that will not resolve to the final
  //   complete class RTTI (because the latter need not exist), possibly by
  //   making it a local static object.
  if (ContainsIncompleteClassType(Ty))
    return llvm::GlobalValue::InternalLinkage;

  switch (Ty->getLinkage()) {
  case Linkage::Invalid:
    llvm_unreachable("Linkage hasn't been computed!");

  case Linkage::None:
  case Linkage::Internal:
  case Linkage::UniqueExternal:
    return llvm::GlobalValue::InternalLinkage;

  case Linkage::VisibleNone:
  case Linkage::Module:
  case Linkage::External:
    // RTTI is not enabled, which means that this type info struct is going
    // to be used for exception handling. Give it linkonce_odr linkage.
    if (!CGM.getLangOpts().RTTI)
      return llvm::GlobalValue::LinkOnceODRLinkage;

    if (const RecordType *Record = dyn_cast<RecordType>(Ty)) {
      const CXXRecordDecl *RD = cast<CXXRecordDecl>(Record->getDecl());
      if (RD->hasAttr<WeakAttr>())
        return llvm::GlobalValue::WeakODRLinkage;
      if (CGM.getTriple().isWindowsItaniumEnvironment())
        if (RD->hasAttr<DLLImportAttr>() &&
            ShouldUseExternalRTTIDescriptor(CGM, Ty))
          return llvm::GlobalValue::ExternalLinkage;
      // MinGW always uses LinkOnceODRLinkage for type info.
      if (RD->isDynamicClass() &&
          !CGM.getContext()
               .getTargetInfo()
               .getTriple()
               .isWindowsGNUEnvironment())
        return CGM.getVTableLinkage(RD);
    }

    return llvm::GlobalValue::LinkOnceODRLinkage;
  }

  llvm_unreachable("Invalid linkage!");
}

llvm::Constant *ItaniumRTTIBuilder::BuildTypeInfo(QualType Ty) {
  // We want to operate on the canonical type.
  Ty = Ty.getCanonicalType();

  // Check if we've already emitted an RTTI descriptor for this type.
  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  CGM.getCXXABI().getMangleContext().mangleCXXRTTI(Ty, Out);

  llvm::GlobalVariable *OldGV = CGM.getModule().getNamedGlobal(Name);
  if (OldGV && !OldGV->isDeclaration()) {
    assert(!OldGV->hasAvailableExternallyLinkage() &&
           "available_externally typeinfos not yet implemented");

    return OldGV;
  }

  // Check if there is already an external RTTI descriptor for this type.
  if (IsStandardLibraryRTTIDescriptor(Ty) ||
      ShouldUseExternalRTTIDescriptor(CGM, Ty))
    return GetAddrOfExternalRTTIDescriptor(Ty);

  // Emit the standard library with external linkage.
  llvm::GlobalVariable::LinkageTypes Linkage = getTypeInfoLinkage(CGM, Ty);

  // Give the type_info object and name the formal visibility of the
  // type itself.
  llvm::GlobalValue::VisibilityTypes llvmVisibility;
  if (llvm::GlobalValue::isLocalLinkage(Linkage))
    // If the linkage is local, only default visibility makes sense.
    llvmVisibility = llvm::GlobalValue::DefaultVisibility;
  else if (CXXABI.classifyRTTIUniqueness(Ty, Linkage) ==
           ItaniumCXXABI::RUK_NonUniqueHidden)
    llvmVisibility = llvm::GlobalValue::HiddenVisibility;
  else
    llvmVisibility = CodeGenModule::GetLLVMVisibility(Ty->getVisibility());

  llvm::GlobalValue::DLLStorageClassTypes DLLStorageClass =
      llvm::GlobalValue::DefaultStorageClass;
  if (auto RD = Ty->getAsCXXRecordDecl()) {
    if ((CGM.getTriple().isWindowsItaniumEnvironment() &&
         RD->hasAttr<DLLExportAttr>()) ||
        (CGM.shouldMapVisibilityToDLLExport(RD) &&
         !llvm::GlobalValue::isLocalLinkage(Linkage) &&
         llvmVisibility == llvm::GlobalValue::DefaultVisibility))
      DLLStorageClass = llvm::GlobalValue::DLLExportStorageClass;
  }
  return BuildTypeInfo(Ty, Linkage, llvmVisibility, DLLStorageClass);
}

llvm::Constant *ItaniumRTTIBuilder::BuildTypeInfo(
      QualType Ty,
      llvm::GlobalVariable::LinkageTypes Linkage,
      llvm::GlobalValue::VisibilityTypes Visibility,
      llvm::GlobalValue::DLLStorageClassTypes DLLStorageClass) {
  // Add the vtable pointer.
  BuildVTablePointer(cast<Type>(Ty));

  // And the name.
  llvm::GlobalVariable *TypeName = GetAddrOfTypeName(Ty, Linkage);
  llvm::Constant *TypeNameField;

  // If we're supposed to demote the visibility, be sure to set a flag
  // to use a string comparison for type_info comparisons.
  ItaniumCXXABI::RTTIUniquenessKind RTTIUniqueness =
      CXXABI.classifyRTTIUniqueness(Ty, Linkage);
  if (RTTIUniqueness != ItaniumCXXABI::RUK_Unique) {
    // The flag is the sign bit, which on ARM64 is defined to be clear
    // for global pointers.  This is very ARM64-specific.
    TypeNameField = llvm::ConstantExpr::getPtrToInt(TypeName, CGM.Int64Ty);
    llvm::Constant *flag =
        llvm::ConstantInt::get(CGM.Int64Ty, ((uint64_t)1) << 63);
    TypeNameField = llvm::ConstantExpr::getAdd(TypeNameField, flag);
    TypeNameField =
        llvm::ConstantExpr::getIntToPtr(TypeNameField, CGM.GlobalsInt8PtrTy);
  } else {
    TypeNameField = TypeName;
  }
  Fields.push_back(TypeNameField);

  switch (Ty->getTypeClass()) {
#define TYPE(Class, Base)
#define ABSTRACT_TYPE(Class, Base)
#define NON_CANONICAL_UNLESS_DEPENDENT_TYPE(Class, Base) case Type::Class:
#define NON_CANONICAL_TYPE(Class, Base) case Type::Class:
#define DEPENDENT_TYPE(Class, Base) case Type::Class:
#include "clang/AST/TypeNodes.inc"
    llvm_unreachable("Non-canonical and dependent types shouldn't get here");

  // GCC treats vector types as fundamental types.
  case Type::Builtin:
  case Type::Vector:
  case Type::ExtVector:
  case Type::ConstantMatrix:
  case Type::Complex:
  case Type::BlockPointer:
    // Itanium C++ ABI 2.9.5p4:
    // abi::__fundamental_type_info adds no data members to std::type_info.
    break;

  case Type::LValueReference:
  case Type::RValueReference:
    llvm_unreachable("References shouldn't get here");

  case Type::Auto:
  case Type::DeducedTemplateSpecialization:
    llvm_unreachable("Undeduced type shouldn't get here");

  case Type::Pipe:
    break;

  case Type::BitInt:
    break;

  case Type::ConstantArray:
  case Type::IncompleteArray:
  case Type::VariableArray:
  case Type::ArrayParameter:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__array_type_info adds no data members to std::type_info.
    break;

  case Type::FunctionNoProto:
  case Type::FunctionProto:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__function_type_info adds no data members to std::type_info.
    break;

  case Type::Enum:
    // Itanium C++ ABI 2.9.5p5:
    // abi::__enum_type_info adds no data members to std::type_info.
    break;

  case Type::Record: {
    const CXXRecordDecl *RD =
      cast<CXXRecordDecl>(cast<RecordType>(Ty)->getDecl());
    if (!RD->hasDefinition() || !RD->getNumBases()) {
      // We don't need to emit any fields.
      break;
    }

    if (CanUseSingleInheritance(RD))
      BuildSIClassTypeInfo(RD);
    else
      BuildVMIClassTypeInfo(RD);

    break;
  }

  case Type::ObjCObject:
  case Type::ObjCInterface:
    BuildObjCObjectTypeInfo(cast<ObjCObjectType>(Ty));
    break;

  case Type::ObjCObjectPointer:
    BuildPointerTypeInfo(cast<ObjCObjectPointerType>(Ty)->getPointeeType());
    break;

  case Type::Pointer:
    BuildPointerTypeInfo(cast<PointerType>(Ty)->getPointeeType());
    break;

  case Type::MemberPointer:
    BuildPointerToMemberTypeInfo(cast<MemberPointerType>(Ty));
    break;

  case Type::Atomic:
    // No fields, at least for the moment.
    break;
  }

  llvm::Constant *Init = llvm::ConstantStruct::getAnon(Fields);

  SmallString<256> Name;
  llvm::raw_svector_ostream Out(Name);
  CGM.getCXXABI().getMangleContext().mangleCXXRTTI(Ty, Out);
  llvm::Module &M = CGM.getModule();
  llvm::GlobalVariable *OldGV = M.getNamedGlobal(Name);
  llvm::GlobalVariable *GV =
      new llvm::GlobalVariable(M, Init->getType(),
                               /*isConstant=*/true, Linkage, Init, Name);

  // Export the typeinfo in the same circumstances as the vtable is exported.
  auto GVDLLStorageClass = DLLStorageClass;
  if (CGM.getTarget().hasPS4DLLImportExport() &&
      GVDLLStorageClass != llvm::GlobalVariable::DLLExportStorageClass) {
    if (const RecordType *RecordTy = dyn_cast<RecordType>(Ty)) {
      const CXXRecordDecl *RD = cast<CXXRecordDecl>(RecordTy->getDecl());
      if (RD->hasAttr<DLLExportAttr>() ||
          CXXRecordNonInlineHasAttr<DLLExportAttr>(RD))
        GVDLLStorageClass = llvm::GlobalVariable::DLLExportStorageClass;
    }
  }

  // If there's already an old global variable, replace it with the new one.
  if (OldGV) {
    GV->takeName(OldGV);
    OldGV->replaceAllUsesWith(GV);
    OldGV->eraseFromParent();
  }

  if (CGM.supportsCOMDAT() && GV->isWeakForLinker())
    GV->setComdat(M.getOrInsertComdat(GV->getName()));

  CharUnits Align = CGM.getContext().toCharUnitsFromBits(
      CGM.getTarget().getPointerAlign(CGM.GetGlobalVarAddressSpace(nullptr)));
  GV->setAlignment(Align.getAsAlign());

  // The Itanium ABI specifies that type_info objects must be globally
  // unique, with one exception: if the type is an incomplete class
  // type or a (possibly indirect) pointer to one.  That exception
  // affects the general case of comparing type_info objects produced
  // by the typeid operator, which is why the comparison operators on
  // std::type_info generally use the type_info name pointers instead
  // of the object addresses.  However, the language's built-in uses
  // of RTTI generally require class types to be complete, even when
  // manipulating pointers to those class types.  This allows the
  // implementation of dynamic_cast to rely on address equality tests,
  // which is much faster.

  // All of this is to say that it's important that both the type_info
  // object and the type_info name be uniqued when weakly emitted.

  TypeName->setVisibility(Visibility);
  CGM.setDSOLocal(TypeName);

  GV->setVisibility(Visibility);
  CGM.setDSOLocal(GV);

  TypeName->setDLLStorageClass(DLLStorageClass);
  GV->setDLLStorageClass(GVDLLStorageClass);

  TypeName->setPartition(CGM.getCodeGenOpts().SymbolPartition);
  GV->setPartition(CGM.getCodeGenOpts().SymbolPartition);

  return GV;
}

/// BuildObjCObjectTypeInfo - Build the appropriate kind of type_info
/// for the given Objective-C object type.
void ItaniumRTTIBuilder::BuildObjCObjectTypeInfo(const ObjCObjectType *OT) {
  // Drop qualifiers.
  const Type *T = OT->getBaseType().getTypePtr();
  assert(isa<BuiltinType>(T) || isa<ObjCInterfaceType>(T));

  // The builtin types are abi::__class_type_infos and don't require
  // extra fields.
  if (isa<BuiltinType>(T)) return;

  ObjCInterfaceDecl *Class = cast<ObjCInterfaceType>(T)->getDecl();
  ObjCInterfaceDecl *Super = Class->getSuperClass();

  // Root classes are also __class_type_info.
  if (!Super) return;

  QualType SuperTy = CGM.getContext().getObjCInterfaceType(Super);

  // Everything else is single inheritance.
  llvm::Constant *BaseTypeInfo =
      ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(SuperTy);
  Fields.push_back(BaseTypeInfo);
}

/// BuildSIClassTypeInfo - Build an abi::__si_class_type_info, used for single
/// inheritance, according to the Itanium C++ ABI, 2.95p6b.
void ItaniumRTTIBuilder::BuildSIClassTypeInfo(const CXXRecordDecl *RD) {
  // Itanium C++ ABI 2.9.5p6b:
  // It adds to abi::__class_type_info a single member pointing to the
  // type_info structure for the base type,
  llvm::Constant *BaseTypeInfo =
    ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(RD->bases_begin()->getType());
  Fields.push_back(BaseTypeInfo);
}

namespace {
  /// SeenBases - Contains virtual and non-virtual bases seen when traversing
  /// a class hierarchy.
  struct SeenBases {
    llvm::SmallPtrSet<const CXXRecordDecl *, 16> NonVirtualBases;
    llvm::SmallPtrSet<const CXXRecordDecl *, 16> VirtualBases;
  };
}

/// ComputeVMIClassTypeInfoFlags - Compute the value of the flags member in
/// abi::__vmi_class_type_info.
///
static unsigned ComputeVMIClassTypeInfoFlags(const CXXBaseSpecifier *Base,
                                             SeenBases &Bases) {

  unsigned Flags = 0;

  auto *BaseDecl =
      cast<CXXRecordDecl>(Base->getType()->castAs<RecordType>()->getDecl());

  if (Base->isVirtual()) {
    // Mark the virtual base as seen.
    if (!Bases.VirtualBases.insert(BaseDecl).second) {
      // If this virtual base has been seen before, then the class is diamond
      // shaped.
      Flags |= ItaniumRTTIBuilder::VMI_DiamondShaped;
    } else {
      if (Bases.NonVirtualBases.count(BaseDecl))
        Flags |= ItaniumRTTIBuilder::VMI_NonDiamondRepeat;
    }
  } else {
    // Mark the non-virtual base as seen.
    if (!Bases.NonVirtualBases.insert(BaseDecl).second) {
      // If this non-virtual base has been seen before, then the class has non-
      // diamond shaped repeated inheritance.
      Flags |= ItaniumRTTIBuilder::VMI_NonDiamondRepeat;
    } else {
      if (Bases.VirtualBases.count(BaseDecl))
        Flags |= ItaniumRTTIBuilder::VMI_NonDiamondRepeat;
    }
  }

  // Walk all bases.
  for (const auto &I : BaseDecl->bases())
    Flags |= ComputeVMIClassTypeInfoFlags(&I, Bases);

  return Flags;
}

static unsigned ComputeVMIClassTypeInfoFlags(const CXXRecordDecl *RD) {
  unsigned Flags = 0;
  SeenBases Bases;

  // Walk all bases.
  for (const auto &I : RD->bases())
    Flags |= ComputeVMIClassTypeInfoFlags(&I, Bases);

  return Flags;
}

/// BuildVMIClassTypeInfo - Build an abi::__vmi_class_type_info, used for
/// classes with bases that do not satisfy the abi::__si_class_type_info
/// constraints, according ti the Itanium C++ ABI, 2.9.5p5c.
void ItaniumRTTIBuilder::BuildVMIClassTypeInfo(const CXXRecordDecl *RD) {
  llvm::Type *UnsignedIntLTy =
    CGM.getTypes().ConvertType(CGM.getContext().UnsignedIntTy);

  // Itanium C++ ABI 2.9.5p6c:
  //   __flags is a word with flags describing details about the class
  //   structure, which may be referenced by using the __flags_masks
  //   enumeration. These flags refer to both direct and indirect bases.
  unsigned Flags = ComputeVMIClassTypeInfoFlags(RD);
  Fields.push_back(llvm::ConstantInt::get(UnsignedIntLTy, Flags));

  // Itanium C++ ABI 2.9.5p6c:
  //   __base_count is a word with the number of direct proper base class
  //   descriptions that follow.
  Fields.push_back(llvm::ConstantInt::get(UnsignedIntLTy, RD->getNumBases()));

  if (!RD->getNumBases())
    return;

  // Now add the base class descriptions.

  // Itanium C++ ABI 2.9.5p6c:
  //   __base_info[] is an array of base class descriptions -- one for every
  //   direct proper base. Each description is of the type:
  //
  //   struct abi::__base_class_type_info {
  //   public:
  //     const __class_type_info *__base_type;
  //     long __offset_flags;
  //
  //     enum __offset_flags_masks {
  //       __virtual_mask = 0x1,
  //       __public_mask = 0x2,
  //       __offset_shift = 8
  //     };
  //   };

  // If we're in mingw and 'long' isn't wide enough for a pointer, use 'long
  // long' instead of 'long' for __offset_flags. libstdc++abi uses long long on
  // LLP64 platforms.
  // FIXME: Consider updating libc++abi to match, and extend this logic to all
  // LLP64 platforms.
  QualType OffsetFlagsTy = CGM.getContext().LongTy;
  const TargetInfo &TI = CGM.getContext().getTargetInfo();
  if (TI.getTriple().isOSCygMing() &&
      TI.getPointerWidth(LangAS::Default) > TI.getLongWidth())
    OffsetFlagsTy = CGM.getContext().LongLongTy;
  llvm::Type *OffsetFlagsLTy =
      CGM.getTypes().ConvertType(OffsetFlagsTy);

  for (const auto &Base : RD->bases()) {
    // The __base_type member points to the RTTI for the base type.
    Fields.push_back(ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(Base.getType()));

    auto *BaseDecl =
        cast<CXXRecordDecl>(Base.getType()->castAs<RecordType>()->getDecl());

    int64_t OffsetFlags = 0;

    // All but the lower 8 bits of __offset_flags are a signed offset.
    // For a non-virtual base, this is the offset in the object of the base
    // subobject. For a virtual base, this is the offset in the virtual table of
    // the virtual base offset for the virtual base referenced (negative).
    CharUnits Offset;
    if (Base.isVirtual())
      Offset =
        CGM.getItaniumVTableContext().getVirtualBaseOffsetOffset(RD, BaseDecl);
    else {
      const ASTRecordLayout &Layout = CGM.getContext().getASTRecordLayout(RD);
      Offset = Layout.getBaseClassOffset(BaseDecl);
    };

    OffsetFlags = uint64_t(Offset.getQuantity()) << 8;

    // The low-order byte of __offset_flags contains flags, as given by the
    // masks from the enumeration __offset_flags_masks.
    if (Base.isVirtual())
      OffsetFlags |= BCTI_Virtual;
    if (Base.getAccessSpecifier() == AS_public)
      OffsetFlags |= BCTI_Public;

    Fields.push_back(llvm::ConstantInt::get(OffsetFlagsLTy, OffsetFlags));
  }
}

/// Compute the flags for a __pbase_type_info, and remove the corresponding
/// pieces from \p Type.
static unsigned extractPBaseFlags(ASTContext &Ctx, QualType &Type) {
  unsigned Flags = 0;

  if (Type.isConstQualified())
    Flags |= ItaniumRTTIBuilder::PTI_Const;
  if (Type.isVolatileQualified())
    Flags |= ItaniumRTTIBuilder::PTI_Volatile;
  if (Type.isRestrictQualified())
    Flags |= ItaniumRTTIBuilder::PTI_Restrict;
  Type = Type.getUnqualifiedType();

  // Itanium C++ ABI 2.9.5p7:
  //   When the abi::__pbase_type_info is for a direct or indirect pointer to an
  //   incomplete class type, the incomplete target type flag is set.
  if (ContainsIncompleteClassType(Type))
    Flags |= ItaniumRTTIBuilder::PTI_Incomplete;

  if (auto *Proto = Type->getAs<FunctionProtoType>()) {
    if (Proto->isNothrow()) {
      Flags |= ItaniumRTTIBuilder::PTI_Noexcept;
      Type = Ctx.getFunctionTypeWithExceptionSpec(Type, EST_None);
    }
  }

  return Flags;
}

/// BuildPointerTypeInfo - Build an abi::__pointer_type_info struct,
/// used for pointer types.
void ItaniumRTTIBuilder::BuildPointerTypeInfo(QualType PointeeTy) {
  // Itanium C++ ABI 2.9.5p7:
  //   __flags is a flag word describing the cv-qualification and other
  //   attributes of the type pointed to
  unsigned Flags = extractPBaseFlags(CGM.getContext(), PointeeTy);

  llvm::Type *UnsignedIntLTy =
    CGM.getTypes().ConvertType(CGM.getContext().UnsignedIntTy);
  Fields.push_back(llvm::ConstantInt::get(UnsignedIntLTy, Flags));

  // Itanium C++ ABI 2.9.5p7:
  //  __pointee is a pointer to the std::type_info derivation for the
  //  unqualified type being pointed to.
  llvm::Constant *PointeeTypeInfo =
      ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(PointeeTy);
  Fields.push_back(PointeeTypeInfo);
}

/// BuildPointerToMemberTypeInfo - Build an abi::__pointer_to_member_type_info
/// struct, used for member pointer types.
void
ItaniumRTTIBuilder::BuildPointerToMemberTypeInfo(const MemberPointerType *Ty) {
  QualType PointeeTy = Ty->getPointeeType();

  // Itanium C++ ABI 2.9.5p7:
  //   __flags is a flag word describing the cv-qualification and other
  //   attributes of the type pointed to.
  unsigned Flags = extractPBaseFlags(CGM.getContext(), PointeeTy);

  const RecordType *ClassType = cast<RecordType>(Ty->getClass());
  if (IsIncompleteClassType(ClassType))
    Flags |= PTI_ContainingClassIncomplete;

  llvm::Type *UnsignedIntLTy =
    CGM.getTypes().ConvertType(CGM.getContext().UnsignedIntTy);
  Fields.push_back(llvm::ConstantInt::get(UnsignedIntLTy, Flags));

  // Itanium C++ ABI 2.9.5p7:
  //   __pointee is a pointer to the std::type_info derivation for the
  //   unqualified type being pointed to.
  llvm::Constant *PointeeTypeInfo =
      ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(PointeeTy);
  Fields.push_back(PointeeTypeInfo);

  // Itanium C++ ABI 2.9.5p9:
  //   __context is a pointer to an abi::__class_type_info corresponding to the
  //   class type containing the member pointed to
  //   (e.g., the "A" in "int A::*").
  Fields.push_back(
      ItaniumRTTIBuilder(CXXABI).BuildTypeInfo(QualType(ClassType, 0)));
}

llvm::Constant *ItaniumCXXABI::getAddrOfRTTIDescriptor(QualType Ty) {
  return ItaniumRTTIBuilder(*this).BuildTypeInfo(Ty);
}

void ItaniumCXXABI::EmitFundamentalRTTIDescriptors(const CXXRecordDecl *RD) {
  // Types added here must also be added to TypeInfoIsInStandardLibrary.
  QualType FundamentalTypes[] = {
      getContext().VoidTy,             getContext().NullPtrTy,
      getContext().BoolTy,             getContext().WCharTy,
      getContext().CharTy,             getContext().UnsignedCharTy,
      getContext().SignedCharTy,       getContext().ShortTy,
      getContext().UnsignedShortTy,    getContext().IntTy,
      getContext().UnsignedIntTy,      getContext().LongTy,
      getContext().UnsignedLongTy,     getContext().LongLongTy,
      getContext().UnsignedLongLongTy, getContext().Int128Ty,
      getContext().UnsignedInt128Ty,   getContext().HalfTy,
      getContext().FloatTy,            getContext().DoubleTy,
      getContext().LongDoubleTy,       getContext().Float128Ty,
      getContext().Char8Ty,            getContext().Char16Ty,
      getContext().Char32Ty
  };
  llvm::GlobalValue::DLLStorageClassTypes DLLStorageClass =
      RD->hasAttr<DLLExportAttr>() || CGM.shouldMapVisibilityToDLLExport(RD)
          ? llvm::GlobalValue::DLLExportStorageClass
          : llvm::GlobalValue::DefaultStorageClass;
  llvm::GlobalValue::VisibilityTypes Visibility =
      CodeGenModule::GetLLVMVisibility(RD->getVisibility());
  for (const QualType &FundamentalType : FundamentalTypes) {
    QualType PointerType = getContext().getPointerType(FundamentalType);
    QualType PointerTypeConst = getContext().getPointerType(
        FundamentalType.withConst());
    for (QualType Type : {FundamentalType, PointerType, PointerTypeConst})
      ItaniumRTTIBuilder(*this).BuildTypeInfo(
          Type, llvm::GlobalValue::ExternalLinkage,
          Visibility, DLLStorageClass);
  }
}

/// What sort of uniqueness rules should we use for the RTTI for the
/// given type?
ItaniumCXXABI::RTTIUniquenessKind ItaniumCXXABI::classifyRTTIUniqueness(
    QualType CanTy, llvm::GlobalValue::LinkageTypes Linkage) const {
  if (shouldRTTIBeUnique())
    return RUK_Unique;

  // It's only necessary for linkonce_odr or weak_odr linkage.
  if (Linkage != llvm::GlobalValue::LinkOnceODRLinkage &&
      Linkage != llvm::GlobalValue::WeakODRLinkage)
    return RUK_Unique;

  // It's only necessary with default visibility.
  if (CanTy->getVisibility() != DefaultVisibility)
    return RUK_Unique;

  // If we're not required to publish this symbol, hide it.
  if (Linkage == llvm::GlobalValue::LinkOnceODRLinkage)
    return RUK_NonUniqueHidden;

  // If we're required to publish this symbol, as we might be under an
  // explicit instantiation, leave it with default visibility but
  // enable string-comparisons.
  assert(Linkage == llvm::GlobalValue::WeakODRLinkage);
  return RUK_NonUniqueVisible;
}

// Find out how to codegen the complete destructor and constructor
namespace {
enum class StructorCodegen { Emit, RAUW, Alias, COMDAT };
}
static StructorCodegen getCodegenToUse(CodeGenModule &CGM,
                                       const CXXMethodDecl *MD) {
  if (!CGM.getCodeGenOpts().CXXCtorDtorAliases)
    return StructorCodegen::Emit;

  // The complete and base structors are not equivalent if there are any virtual
  // bases, so emit separate functions.
  if (MD->getParent()->getNumVBases())
    return StructorCodegen::Emit;

  GlobalDecl AliasDecl;
  if (const auto *DD = dyn_cast<CXXDestructorDecl>(MD)) {
    AliasDecl = GlobalDecl(DD, Dtor_Complete);
  } else {
    const auto *CD = cast<CXXConstructorDecl>(MD);
    AliasDecl = GlobalDecl(CD, Ctor_Complete);
  }
  llvm::GlobalValue::LinkageTypes Linkage = CGM.getFunctionLinkage(AliasDecl);

  if (llvm::GlobalValue::isDiscardableIfUnused(Linkage))
    return StructorCodegen::RAUW;

  // FIXME: Should we allow available_externally aliases?
  if (!llvm::GlobalAlias::isValidLinkage(Linkage))
    return StructorCodegen::RAUW;

  if (llvm::GlobalValue::isWeakForLinker(Linkage)) {
    // Only ELF and wasm support COMDATs with arbitrary names (C5/D5).
    if (CGM.getTarget().getTriple().isOSBinFormatELF() ||
        CGM.getTarget().getTriple().isOSBinFormatWasm())
      return StructorCodegen::COMDAT;
    return StructorCodegen::Emit;
  }

  return StructorCodegen::Alias;
}

static void emitConstructorDestructorAlias(CodeGenModule &CGM,
                                           GlobalDecl AliasDecl,
                                           GlobalDecl TargetDecl) {
  llvm::GlobalValue::LinkageTypes Linkage = CGM.getFunctionLinkage(AliasDecl);

  StringRef MangledName = CGM.getMangledName(AliasDecl);
  llvm::GlobalValue *Entry = CGM.GetGlobalValue(MangledName);
  if (Entry && !Entry->isDeclaration())
    return;

  auto *Aliasee = cast<llvm::GlobalValue>(CGM.GetAddrOfGlobal(TargetDecl));

  // Create the alias with no name.
  auto *Alias = llvm::GlobalAlias::create(Linkage, "", Aliasee);

  // Constructors and destructors are always unnamed_addr.
  Alias->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  // Switch any previous uses to the alias.
  if (Entry) {
    assert(Entry->getType() == Aliasee->getType() &&
           "declaration exists with different type");
    Alias->takeName(Entry);
    Entry->replaceAllUsesWith(Alias);
    Entry->eraseFromParent();
  } else {
    Alias->setName(MangledName);
  }

  // Finally, set up the alias with its proper name and attributes.
  CGM.SetCommonAttributes(AliasDecl, Alias);
}

void ItaniumCXXABI::emitCXXStructor(GlobalDecl GD) {
  auto *MD = cast<CXXMethodDecl>(GD.getDecl());
  auto *CD = dyn_cast<CXXConstructorDecl>(MD);
  const CXXDestructorDecl *DD = CD ? nullptr : cast<CXXDestructorDecl>(MD);

  StructorCodegen CGType = getCodegenToUse(CGM, MD);

  if (CD ? GD.getCtorType() == Ctor_Complete
         : GD.getDtorType() == Dtor_Complete) {
    GlobalDecl BaseDecl;
    if (CD)
      BaseDecl = GD.getWithCtorType(Ctor_Base);
    else
      BaseDecl = GD.getWithDtorType(Dtor_Base);

    if (CGType == StructorCodegen::Alias || CGType == StructorCodegen::COMDAT) {
      emitConstructorDestructorAlias(CGM, GD, BaseDecl);
      return;
    }

    if (CGType == StructorCodegen::RAUW) {
      StringRef MangledName = CGM.getMangledName(GD);
      auto *Aliasee = CGM.GetAddrOfGlobal(BaseDecl);
      CGM.addReplacement(MangledName, Aliasee);
      return;
    }
  }

  // The base destructor is equivalent to the base destructor of its
  // base class if there is exactly one non-virtual base class with a
  // non-trivial destructor, there are no fields with a non-trivial
  // destructor, and the body of the destructor is trivial.
  if (DD && GD.getDtorType() == Dtor_Base &&
      CGType != StructorCodegen::COMDAT &&
      !CGM.TryEmitBaseDestructorAsAlias(DD))
    return;

  // FIXME: The deleting destructor is equivalent to the selected operator
  // delete if:
  //  * either the delete is a destroying operator delete or the destructor
  //    would be trivial if it weren't virtual,
  //  * the conversion from the 'this' parameter to the first parameter of the
  //    destructor is equivalent to a bitcast,
  //  * the destructor does not have an implicit "this" return, and
  //  * the operator delete has the same calling convention and IR function type
  //    as the destructor.
  // In such cases we should try to emit the deleting dtor as an alias to the
  // selected 'operator delete'.

  llvm::Function *Fn = CGM.codegenCXXStructor(GD);

  if (CGType == StructorCodegen::COMDAT) {
    SmallString<256> Buffer;
    llvm::raw_svector_ostream Out(Buffer);
    if (DD)
      getMangleContext().mangleCXXDtorComdat(DD, Out);
    else
      getMangleContext().mangleCXXCtorComdat(CD, Out);
    llvm::Comdat *C = CGM.getModule().getOrInsertComdat(Out.str());
    Fn->setComdat(C);
  } else {
    CGM.maybeSetTrivialComdat(*MD, *Fn);
  }
}

static llvm::FunctionCallee getBeginCatchFn(CodeGenModule &CGM) {
  // void *__cxa_begin_catch(void*);
  llvm::FunctionType *FTy = llvm::FunctionType::get(
      CGM.Int8PtrTy, CGM.Int8PtrTy, /*isVarArg=*/false);

  return CGM.CreateRuntimeFunction(FTy, "__cxa_begin_catch");
}

static llvm::FunctionCallee getEndCatchFn(CodeGenModule &CGM) {
  // void __cxa_end_catch();
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);

  return CGM.CreateRuntimeFunction(FTy, "__cxa_end_catch");
}

static llvm::FunctionCallee getGetExceptionPtrFn(CodeGenModule &CGM) {
  // void *__cxa_get_exception_ptr(void*);
  llvm::FunctionType *FTy = llvm::FunctionType::get(
      CGM.Int8PtrTy, CGM.Int8PtrTy, /*isVarArg=*/false);

  return CGM.CreateRuntimeFunction(FTy, "__cxa_get_exception_ptr");
}

namespace {
  /// A cleanup to call __cxa_end_catch.  In many cases, the caught
  /// exception type lets us state definitively that the thrown exception
  /// type does not have a destructor.  In particular:
  ///   - Catch-alls tell us nothing, so we have to conservatively
  ///     assume that the thrown exception might have a destructor.
  ///   - Catches by reference behave according to their base types.
  ///   - Catches of non-record types will only trigger for exceptions
  ///     of non-record types, which never have destructors.
  ///   - Catches of record types can trigger for arbitrary subclasses
  ///     of the caught type, so we have to assume the actual thrown
  ///     exception type might have a throwing destructor, even if the
  ///     caught type's destructor is trivial or nothrow.
  struct CallEndCatch final : EHScopeStack::Cleanup {
    CallEndCatch(bool MightThrow) : MightThrow(MightThrow) {}
    bool MightThrow;

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      if (!MightThrow) {
        CGF.EmitNounwindRuntimeCall(getEndCatchFn(CGF.CGM));
        return;
      }

      CGF.EmitRuntimeCallOrInvoke(getEndCatchFn(CGF.CGM));
    }
  };
}

/// Emits a call to __cxa_begin_catch and enters a cleanup to call
/// __cxa_end_catch. If -fassume-nothrow-exception-dtor is specified, we assume
/// that the exception object's dtor is nothrow, therefore the __cxa_end_catch
/// call can be marked as nounwind even if EndMightThrow is true.
///
/// \param EndMightThrow - true if __cxa_end_catch might throw
static llvm::Value *CallBeginCatch(CodeGenFunction &CGF,
                                   llvm::Value *Exn,
                                   bool EndMightThrow) {
  llvm::CallInst *call =
    CGF.EmitNounwindRuntimeCall(getBeginCatchFn(CGF.CGM), Exn);

  CGF.EHStack.pushCleanup<CallEndCatch>(
      NormalAndEHCleanup,
      EndMightThrow && !CGF.CGM.getLangOpts().AssumeNothrowExceptionDtor);

  return call;
}

/// A "special initializer" callback for initializing a catch
/// parameter during catch initialization.
static void InitCatchParam(CodeGenFunction &CGF,
                           const VarDecl &CatchParam,
                           Address ParamAddr,
                           SourceLocation Loc) {
  // Load the exception from where the landing pad saved it.
  llvm::Value *Exn = CGF.getExceptionFromSlot();

  CanQualType CatchType =
    CGF.CGM.getContext().getCanonicalType(CatchParam.getType());
  llvm::Type *LLVMCatchTy = CGF.ConvertTypeForMem(CatchType);

  // If we're catching by reference, we can just cast the object
  // pointer to the appropriate pointer.
  if (isa<ReferenceType>(CatchType)) {
    QualType CaughtType = cast<ReferenceType>(CatchType)->getPointeeType();
    bool EndCatchMightThrow = CaughtType->isRecordType();

    // __cxa_begin_catch returns the adjusted object pointer.
    llvm::Value *AdjustedExn = CallBeginCatch(CGF, Exn, EndCatchMightThrow);

    // We have no way to tell the personality function that we're
    // catching by reference, so if we're catching a pointer,
    // __cxa_begin_catch will actually return that pointer by value.
    if (const PointerType *PT = dyn_cast<PointerType>(CaughtType)) {
      QualType PointeeType = PT->getPointeeType();

      // When catching by reference, generally we should just ignore
      // this by-value pointer and use the exception object instead.
      if (!PointeeType->isRecordType()) {

        // Exn points to the struct _Unwind_Exception header, which
        // we have to skip past in order to reach the exception data.
        unsigned HeaderSize =
          CGF.CGM.getTargetCodeGenInfo().getSizeOfUnwindException();
        AdjustedExn =
            CGF.Builder.CreateConstGEP1_32(CGF.Int8Ty, Exn, HeaderSize);

      // However, if we're catching a pointer-to-record type that won't
      // work, because the personality function might have adjusted
      // the pointer.  There's actually no way for us to fully satisfy
      // the language/ABI contract here:  we can't use Exn because it
      // might have the wrong adjustment, but we can't use the by-value
      // pointer because it's off by a level of abstraction.
      //
      // The current solution is to dump the adjusted pointer into an
      // alloca, which breaks language semantics (because changing the
      // pointer doesn't change the exception) but at least works.
      // The better solution would be to filter out non-exact matches
      // and rethrow them, but this is tricky because the rethrow
      // really needs to be catchable by other sites at this landing
      // pad.  The best solution is to fix the personality function.
      } else {
        // Pull the pointer for the reference type off.
        llvm::Type *PtrTy = CGF.ConvertTypeForMem(CaughtType);

        // Create the temporary and write the adjusted pointer into it.
        Address ExnPtrTmp =
          CGF.CreateTempAlloca(PtrTy, CGF.getPointerAlign(), "exn.byref.tmp");
        llvm::Value *Casted = CGF.Builder.CreateBitCast(AdjustedExn, PtrTy);
        CGF.Builder.CreateStore(Casted, ExnPtrTmp);

        // Bind the reference to the temporary.
        AdjustedExn = ExnPtrTmp.emitRawPointer(CGF);
      }
    }

    llvm::Value *ExnCast =
      CGF.Builder.CreateBitCast(AdjustedExn, LLVMCatchTy, "exn.byref");
    CGF.Builder.CreateStore(ExnCast, ParamAddr);
    return;
  }

  // Scalars and complexes.
  TypeEvaluationKind TEK = CGF.getEvaluationKind(CatchType);
  if (TEK != TEK_Aggregate) {
    llvm::Value *AdjustedExn = CallBeginCatch(CGF, Exn, false);

    // If the catch type is a pointer type, __cxa_begin_catch returns
    // the pointer by value.
    if (CatchType->hasPointerRepresentation()) {
      llvm::Value *CastExn =
        CGF.Builder.CreateBitCast(AdjustedExn, LLVMCatchTy, "exn.casted");

      switch (CatchType.getQualifiers().getObjCLifetime()) {
      case Qualifiers::OCL_Strong:
        CastExn = CGF.EmitARCRetainNonBlock(CastExn);
        [[fallthrough]];

      case Qualifiers::OCL_None:
      case Qualifiers::OCL_ExplicitNone:
      case Qualifiers::OCL_Autoreleasing:
        CGF.Builder.CreateStore(CastExn, ParamAddr);
        return;

      case Qualifiers::OCL_Weak:
        CGF.EmitARCInitWeak(ParamAddr, CastExn);
        return;
      }
      llvm_unreachable("bad ownership qualifier!");
    }

    // Otherwise, it returns a pointer into the exception object.

    LValue srcLV = CGF.MakeNaturalAlignAddrLValue(AdjustedExn, CatchType);
    LValue destLV = CGF.MakeAddrLValue(ParamAddr, CatchType);
    switch (TEK) {
    case TEK_Complex:
      CGF.EmitStoreOfComplex(CGF.EmitLoadOfComplex(srcLV, Loc), destLV,
                             /*init*/ true);
      return;
    case TEK_Scalar: {
      llvm::Value *ExnLoad = CGF.EmitLoadOfScalar(srcLV, Loc);
      CGF.EmitStoreOfScalar(ExnLoad, destLV, /*init*/ true);
      return;
    }
    case TEK_Aggregate:
      llvm_unreachable("evaluation kind filtered out!");
    }
    llvm_unreachable("bad evaluation kind");
  }

  assert(isa<RecordType>(CatchType) && "unexpected catch type!");
  auto catchRD = CatchType->getAsCXXRecordDecl();
  CharUnits caughtExnAlignment = CGF.CGM.getClassPointerAlignment(catchRD);

  llvm::Type *PtrTy = CGF.UnqualPtrTy; // addrspace 0 ok

  // Check for a copy expression.  If we don't have a copy expression,
  // that means a trivial copy is okay.
  const Expr *copyExpr = CatchParam.getInit();
  if (!copyExpr) {
    llvm::Value *rawAdjustedExn = CallBeginCatch(CGF, Exn, true);
    Address adjustedExn(CGF.Builder.CreateBitCast(rawAdjustedExn, PtrTy),
                        LLVMCatchTy, caughtExnAlignment);
    LValue Dest = CGF.MakeAddrLValue(ParamAddr, CatchType);
    LValue Src = CGF.MakeAddrLValue(adjustedExn, CatchType);
    CGF.EmitAggregateCopy(Dest, Src, CatchType, AggValueSlot::DoesNotOverlap);
    return;
  }

  // We have to call __cxa_get_exception_ptr to get the adjusted
  // pointer before copying.
  llvm::CallInst *rawAdjustedExn =
    CGF.EmitNounwindRuntimeCall(getGetExceptionPtrFn(CGF.CGM), Exn);

  // Cast that to the appropriate type.
  Address adjustedExn(CGF.Builder.CreateBitCast(rawAdjustedExn, PtrTy),
                      LLVMCatchTy, caughtExnAlignment);

  // The copy expression is defined in terms of an OpaqueValueExpr.
  // Find it and map it to the adjusted expression.
  CodeGenFunction::OpaqueValueMapping
    opaque(CGF, OpaqueValueExpr::findInCopyConstruct(copyExpr),
           CGF.MakeAddrLValue(adjustedExn, CatchParam.getType()));

  // Call the copy ctor in a terminate scope.
  CGF.EHStack.pushTerminate();

  // Perform the copy construction.
  CGF.EmitAggExpr(copyExpr,
                  AggValueSlot::forAddr(ParamAddr, Qualifiers(),
                                        AggValueSlot::IsNotDestructed,
                                        AggValueSlot::DoesNotNeedGCBarriers,
                                        AggValueSlot::IsNotAliased,
                                        AggValueSlot::DoesNotOverlap));

  // Leave the terminate scope.
  CGF.EHStack.popTerminate();

  // Undo the opaque value mapping.
  opaque.pop();

  // Finally we can call __cxa_begin_catch.
  CallBeginCatch(CGF, Exn, true);
}

/// Begins a catch statement by initializing the catch variable and
/// calling __cxa_begin_catch.
void ItaniumCXXABI::emitBeginCatch(CodeGenFunction &CGF,
                                   const CXXCatchStmt *S) {
  // We have to be very careful with the ordering of cleanups here:
  //   C++ [except.throw]p4:
  //     The destruction [of the exception temporary] occurs
  //     immediately after the destruction of the object declared in
  //     the exception-declaration in the handler.
  //
  // So the precise ordering is:
  //   1.  Construct catch variable.
  //   2.  __cxa_begin_catch
  //   3.  Enter __cxa_end_catch cleanup
  //   4.  Enter dtor cleanup
  //
  // We do this by using a slightly abnormal initialization process.
  // Delegation sequence:
  //   - ExitCXXTryStmt opens a RunCleanupsScope
  //     - EmitAutoVarAlloca creates the variable and debug info
  //       - InitCatchParam initializes the variable from the exception
  //       - CallBeginCatch calls __cxa_begin_catch
  //       - CallBeginCatch enters the __cxa_end_catch cleanup
  //     - EmitAutoVarCleanups enters the variable destructor cleanup
  //   - EmitCXXTryStmt emits the code for the catch body
  //   - EmitCXXTryStmt close the RunCleanupsScope

  VarDecl *CatchParam = S->getExceptionDecl();
  if (!CatchParam) {
    llvm::Value *Exn = CGF.getExceptionFromSlot();
    CallBeginCatch(CGF, Exn, true);
    return;
  }

  // Emit the local.
  CodeGenFunction::AutoVarEmission var = CGF.EmitAutoVarAlloca(*CatchParam);
  InitCatchParam(CGF, *CatchParam, var.getObjectAddress(CGF), S->getBeginLoc());
  CGF.EmitAutoVarCleanups(var);
}

/// Get or define the following function:
///   void @__clang_call_terminate(i8* %exn) nounwind noreturn
/// This code is used only in C++.
static llvm::FunctionCallee getClangCallTerminateFn(CodeGenModule &CGM) {
  ASTContext &C = CGM.getContext();
  const CGFunctionInfo &FI = CGM.getTypes().arrangeBuiltinFunctionDeclaration(
      C.VoidTy, {C.getPointerType(C.CharTy)});
  llvm::FunctionType *fnTy = CGM.getTypes().GetFunctionType(FI);
  llvm::FunctionCallee fnRef = CGM.CreateRuntimeFunction(
      fnTy, "__clang_call_terminate", llvm::AttributeList(), /*Local=*/true);
  llvm::Function *fn =
      cast<llvm::Function>(fnRef.getCallee()->stripPointerCasts());
  if (fn->empty()) {
    CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, fn, /*IsThunk=*/false);
    CGM.SetLLVMFunctionAttributesForDefinition(nullptr, fn);
    fn->setDoesNotThrow();
    fn->setDoesNotReturn();

    // What we really want is to massively penalize inlining without
    // forbidding it completely.  The difference between that and
    // 'noinline' is negligible.
    fn->addFnAttr(llvm::Attribute::NoInline);

    // Allow this function to be shared across translation units, but
    // we don't want it to turn into an exported symbol.
    fn->setLinkage(llvm::Function::LinkOnceODRLinkage);
    fn->setVisibility(llvm::Function::HiddenVisibility);
    if (CGM.supportsCOMDAT())
      fn->setComdat(CGM.getModule().getOrInsertComdat(fn->getName()));

    // Set up the function.
    llvm::BasicBlock *entry =
        llvm::BasicBlock::Create(CGM.getLLVMContext(), "", fn);
    CGBuilderTy builder(CGM, entry);

    // Pull the exception pointer out of the parameter list.
    llvm::Value *exn = &*fn->arg_begin();

    // Call __cxa_begin_catch(exn).
    llvm::CallInst *catchCall = builder.CreateCall(getBeginCatchFn(CGM), exn);
    catchCall->setDoesNotThrow();
    catchCall->setCallingConv(CGM.getRuntimeCC());

    // Call std::terminate().
    llvm::CallInst *termCall = builder.CreateCall(CGM.getTerminateFn());
    termCall->setDoesNotThrow();
    termCall->setDoesNotReturn();
    termCall->setCallingConv(CGM.getRuntimeCC());

    // std::terminate cannot return.
    builder.CreateUnreachable();
  }
  return fnRef;
}

llvm::CallInst *
ItaniumCXXABI::emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                                   llvm::Value *Exn) {
  // In C++, we want to call __cxa_begin_catch() before terminating.
  if (Exn) {
    assert(CGF.CGM.getLangOpts().CPlusPlus);
    return CGF.EmitNounwindRuntimeCall(getClangCallTerminateFn(CGF.CGM), Exn);
  }
  return CGF.EmitNounwindRuntimeCall(CGF.CGM.getTerminateFn());
}

std::pair<llvm::Value *, const CXXRecordDecl *>
ItaniumCXXABI::LoadVTablePtr(CodeGenFunction &CGF, Address This,
                             const CXXRecordDecl *RD) {
  return {CGF.GetVTablePtr(This, CGM.Int8PtrTy, RD), RD};
}

llvm::Constant *
ItaniumCXXABI::getSignedVirtualMemberFunctionPointer(const CXXMethodDecl *MD) {
  const CXXMethodDecl *origMD =
      cast<CXXMethodDecl>(CGM.getItaniumVTableContext()
                              .findOriginalMethod(MD->getCanonicalDecl())
                              .getDecl());
  llvm::Constant *thunk = getOrCreateVirtualFunctionPointerThunk(origMD);
  QualType funcType = CGM.getContext().getMemberPointerType(
      MD->getType(), MD->getParent()->getTypeForDecl());
  return CGM.getMemberFunctionPointer(thunk, funcType);
}

void WebAssemblyCXXABI::emitBeginCatch(CodeGenFunction &CGF,
                                       const CXXCatchStmt *C) {
  if (CGF.getTarget().hasFeature("exception-handling"))
    CGF.EHStack.pushCleanup<CatchRetScope>(
        NormalCleanup, cast<llvm::CatchPadInst>(CGF.CurrentFuncletPad));
  ItaniumCXXABI::emitBeginCatch(CGF, C);
}

llvm::CallInst *
WebAssemblyCXXABI::emitTerminateForUnexpectedException(CodeGenFunction &CGF,
                                                       llvm::Value *Exn) {
  // Itanium ABI calls __clang_call_terminate(), which __cxa_begin_catch() on
  // the violating exception to mark it handled, but it is currently hard to do
  // with wasm EH instruction structure with catch/catch_all, we just call
  // std::terminate and ignore the violating exception as in CGCXXABI.
  // TODO Consider code transformation that makes calling __clang_call_terminate
  // possible.
  return CGCXXABI::emitTerminateForUnexpectedException(CGF, Exn);
}

/// Register a global destructor as best as we know how.
void XLCXXABI::registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                                  llvm::FunctionCallee Dtor,
                                  llvm::Constant *Addr) {
  if (D.getTLSKind() != VarDecl::TLS_None) {
    llvm::PointerType *PtrTy = CGF.UnqualPtrTy;

    // extern "C" int __pt_atexit_np(int flags, int(*)(int,...), ...);
    llvm::FunctionType *AtExitTy =
        llvm::FunctionType::get(CGM.IntTy, {CGM.IntTy, PtrTy}, true);

    // Fetch the actual function.
    llvm::FunctionCallee AtExit =
        CGM.CreateRuntimeFunction(AtExitTy, "__pt_atexit_np");

    // Create __dtor function for the var decl.
    llvm::Function *DtorStub = CGF.createTLSAtExitStub(D, Dtor, Addr, AtExit);

    // Register above __dtor with atexit().
    // First param is flags and must be 0, second param is function ptr
    llvm::Value *NV = llvm::Constant::getNullValue(CGM.IntTy);
    CGF.EmitNounwindRuntimeCall(AtExit, {NV, DtorStub});

    // Cannot unregister TLS __dtor so done
    return;
  }

  // Create __dtor function for the var decl.
  llvm::Function *DtorStub =
      cast<llvm::Function>(CGF.createAtExitStub(D, Dtor, Addr));

  // Register above __dtor with atexit().
  CGF.registerGlobalDtorWithAtExit(DtorStub);

  // Emit __finalize function to unregister __dtor and (as appropriate) call
  // __dtor.
  emitCXXStermFinalizer(D, DtorStub, Addr);
}

void XLCXXABI::emitCXXStermFinalizer(const VarDecl &D, llvm::Function *dtorStub,
                                     llvm::Constant *addr) {
  llvm::FunctionType *FTy = llvm::FunctionType::get(CGM.VoidTy, false);
  SmallString<256> FnName;
  {
    llvm::raw_svector_ostream Out(FnName);
    getMangleContext().mangleDynamicStermFinalizer(&D, Out);
  }

  // Create the finalization action associated with a variable.
  const CGFunctionInfo &FI = CGM.getTypes().arrangeNullaryFunction();
  llvm::Function *StermFinalizer = CGM.CreateGlobalInitOrCleanUpFunction(
      FTy, FnName.str(), FI, D.getLocation());

  CodeGenFunction CGF(CGM);

  CGF.StartFunction(GlobalDecl(), CGM.getContext().VoidTy, StermFinalizer, FI,
                    FunctionArgList(), D.getLocation(),
                    D.getInit()->getExprLoc());

  // The unatexit subroutine unregisters __dtor functions that were previously
  // registered by the atexit subroutine. If the referenced function is found,
  // the unatexit returns a value of 0, meaning that the cleanup is still
  // pending (and we should call the __dtor function).
  llvm::Value *V = CGF.unregisterGlobalDtorWithUnAtExit(dtorStub);

  llvm::Value *NeedsDestruct = CGF.Builder.CreateIsNull(V, "needs_destruct");

  llvm::BasicBlock *DestructCallBlock = CGF.createBasicBlock("destruct.call");
  llvm::BasicBlock *EndBlock = CGF.createBasicBlock("destruct.end");

  // Check if unatexit returns a value of 0. If it does, jump to
  // DestructCallBlock, otherwise jump to EndBlock directly.
  CGF.Builder.CreateCondBr(NeedsDestruct, DestructCallBlock, EndBlock);

  CGF.EmitBlock(DestructCallBlock);

  // Emit the call to dtorStub.
  llvm::CallInst *CI = CGF.Builder.CreateCall(dtorStub);

  // Make sure the call and the callee agree on calling convention.
  CI->setCallingConv(dtorStub->getCallingConv());

  CGF.EmitBlock(EndBlock);

  CGF.FinishFunction();

  if (auto *IPA = D.getAttr<InitPriorityAttr>()) {
    CGM.AddCXXPrioritizedStermFinalizerEntry(StermFinalizer,
                                             IPA->getPriority());
  } else if (isTemplateInstantiation(D.getTemplateSpecializationKind()) ||
             getContext().GetGVALinkageForVariable(&D) == GVA_DiscardableODR) {
    // According to C++ [basic.start.init]p2, class template static data
    // members (i.e., implicitly or explicitly instantiated specializations)
    // have unordered initialization. As a consequence, we can put them into
    // their own llvm.global_dtors entry.
    CGM.AddCXXStermFinalizerToGlobalDtor(StermFinalizer, 65535);
  } else {
    CGM.AddCXXStermFinalizerEntry(StermFinalizer);
  }
}
