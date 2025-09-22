//===--- CGExprCXX.cpp - Emit LLVM Code for C++ expressions ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code dealing with code generation of C++ expressions
//
//===----------------------------------------------------------------------===//

#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "CodeGenFunction.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/CodeGen/CGFunctionInfo.h"
#include "llvm/IR/Intrinsics.h"

using namespace clang;
using namespace CodeGen;

namespace {
struct MemberCallInfo {
  RequiredArgs ReqArgs;
  // Number of prefix arguments for the call. Ignores the `this` pointer.
  unsigned PrefixSize;
};
}

static MemberCallInfo
commonEmitCXXMemberOrOperatorCall(CodeGenFunction &CGF, GlobalDecl GD,
                                  llvm::Value *This, llvm::Value *ImplicitParam,
                                  QualType ImplicitParamTy, const CallExpr *CE,
                                  CallArgList &Args, CallArgList *RtlArgs) {
  auto *MD = cast<CXXMethodDecl>(GD.getDecl());

  assert(CE == nullptr || isa<CXXMemberCallExpr>(CE) ||
         isa<CXXOperatorCallExpr>(CE));
  assert(MD->isImplicitObjectMemberFunction() &&
         "Trying to emit a member or operator call expr on a static method!");

  // Push the this ptr.
  const CXXRecordDecl *RD =
      CGF.CGM.getCXXABI().getThisArgumentTypeForMethod(GD);
  Args.add(RValue::get(This), CGF.getTypes().DeriveThisType(RD, MD));

  // If there is an implicit parameter (e.g. VTT), emit it.
  if (ImplicitParam) {
    Args.add(RValue::get(ImplicitParam), ImplicitParamTy);
  }

  const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
  RequiredArgs required = RequiredArgs::forPrototypePlus(FPT, Args.size());
  unsigned PrefixSize = Args.size() - 1;

  // And the rest of the call args.
  if (RtlArgs) {
    // Special case: if the caller emitted the arguments right-to-left already
    // (prior to emitting the *this argument), we're done. This happens for
    // assignment operators.
    Args.addFrom(*RtlArgs);
  } else if (CE) {
    // Special case: skip first argument of CXXOperatorCall (it is "this").
    unsigned ArgsToSkip = 0;
    if (const auto *Op = dyn_cast<CXXOperatorCallExpr>(CE)) {
      if (const auto *M = dyn_cast<CXXMethodDecl>(Op->getCalleeDecl()))
        ArgsToSkip =
            static_cast<unsigned>(!M->isExplicitObjectMemberFunction());
    }
    CGF.EmitCallArgs(Args, FPT, drop_begin(CE->arguments(), ArgsToSkip),
                     CE->getDirectCallee());
  } else {
    assert(
        FPT->getNumParams() == 0 &&
        "No CallExpr specified for function with non-zero number of arguments");
  }
  return {required, PrefixSize};
}

RValue CodeGenFunction::EmitCXXMemberOrOperatorCall(
    const CXXMethodDecl *MD, const CGCallee &Callee,
    ReturnValueSlot ReturnValue,
    llvm::Value *This, llvm::Value *ImplicitParam, QualType ImplicitParamTy,
    const CallExpr *CE, CallArgList *RtlArgs) {
  const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
  CallArgList Args;
  MemberCallInfo CallInfo = commonEmitCXXMemberOrOperatorCall(
      *this, MD, This, ImplicitParam, ImplicitParamTy, CE, Args, RtlArgs);
  auto &FnInfo = CGM.getTypes().arrangeCXXMethodCall(
      Args, FPT, CallInfo.ReqArgs, CallInfo.PrefixSize);
  return EmitCall(FnInfo, Callee, ReturnValue, Args, nullptr,
                  CE && CE == MustTailCall,
                  CE ? CE->getExprLoc() : SourceLocation());
}

RValue CodeGenFunction::EmitCXXDestructorCall(
    GlobalDecl Dtor, const CGCallee &Callee, llvm::Value *This, QualType ThisTy,
    llvm::Value *ImplicitParam, QualType ImplicitParamTy, const CallExpr *CE) {
  const CXXMethodDecl *DtorDecl = cast<CXXMethodDecl>(Dtor.getDecl());

  assert(!ThisTy.isNull());
  assert(ThisTy->getAsCXXRecordDecl() == DtorDecl->getParent() &&
         "Pointer/Object mixup");

  LangAS SrcAS = ThisTy.getAddressSpace();
  LangAS DstAS = DtorDecl->getMethodQualifiers().getAddressSpace();
  if (SrcAS != DstAS) {
    QualType DstTy = DtorDecl->getThisType();
    llvm::Type *NewType = CGM.getTypes().ConvertType(DstTy);
    This = getTargetHooks().performAddrSpaceCast(*this, This, SrcAS, DstAS,
                                                 NewType);
  }

  CallArgList Args;
  commonEmitCXXMemberOrOperatorCall(*this, Dtor, This, ImplicitParam,
                                    ImplicitParamTy, CE, Args, nullptr);
  return EmitCall(CGM.getTypes().arrangeCXXStructorDeclaration(Dtor), Callee,
                  ReturnValueSlot(), Args, nullptr, CE && CE == MustTailCall,
                  CE ? CE->getExprLoc() : SourceLocation{});
}

RValue CodeGenFunction::EmitCXXPseudoDestructorExpr(
                                            const CXXPseudoDestructorExpr *E) {
  QualType DestroyedType = E->getDestroyedType();
  if (DestroyedType.hasStrongOrWeakObjCLifetime()) {
    // Automatic Reference Counting:
    //   If the pseudo-expression names a retainable object with weak or
    //   strong lifetime, the object shall be released.
    Expr *BaseExpr = E->getBase();
    Address BaseValue = Address::invalid();
    Qualifiers BaseQuals;

    // If this is s.x, emit s as an lvalue. If it is s->x, emit s as a scalar.
    if (E->isArrow()) {
      BaseValue = EmitPointerWithAlignment(BaseExpr);
      const auto *PTy = BaseExpr->getType()->castAs<PointerType>();
      BaseQuals = PTy->getPointeeType().getQualifiers();
    } else {
      LValue BaseLV = EmitLValue(BaseExpr);
      BaseValue = BaseLV.getAddress();
      QualType BaseTy = BaseExpr->getType();
      BaseQuals = BaseTy.getQualifiers();
    }

    switch (DestroyedType.getObjCLifetime()) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      break;

    case Qualifiers::OCL_Strong:
      EmitARCRelease(Builder.CreateLoad(BaseValue,
                        DestroyedType.isVolatileQualified()),
                     ARCPreciseLifetime);
      break;

    case Qualifiers::OCL_Weak:
      EmitARCDestroyWeak(BaseValue);
      break;
    }
  } else {
    // C++ [expr.pseudo]p1:
    //   The result shall only be used as the operand for the function call
    //   operator (), and the result of such a call has type void. The only
    //   effect is the evaluation of the postfix-expression before the dot or
    //   arrow.
    EmitIgnoredExpr(E->getBase());
  }

  return RValue::get(nullptr);
}

static CXXRecordDecl *getCXXRecord(const Expr *E) {
  QualType T = E->getType();
  if (const PointerType *PTy = T->getAs<PointerType>())
    T = PTy->getPointeeType();
  const RecordType *Ty = T->castAs<RecordType>();
  return cast<CXXRecordDecl>(Ty->getDecl());
}

// Note: This function also emit constructor calls to support a MSVC
// extensions allowing explicit constructor function call.
RValue CodeGenFunction::EmitCXXMemberCallExpr(const CXXMemberCallExpr *CE,
                                              ReturnValueSlot ReturnValue) {
  const Expr *callee = CE->getCallee()->IgnoreParens();

  if (isa<BinaryOperator>(callee))
    return EmitCXXMemberPointerCallExpr(CE, ReturnValue);

  const MemberExpr *ME = cast<MemberExpr>(callee);
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(ME->getMemberDecl());

  if (MD->isStatic()) {
    // The method is static, emit it as we would a regular call.
    CGCallee callee =
        CGCallee::forDirect(CGM.GetAddrOfFunction(MD), GlobalDecl(MD));
    return EmitCall(getContext().getPointerType(MD->getType()), callee, CE,
                    ReturnValue);
  }

  bool HasQualifier = ME->hasQualifier();
  NestedNameSpecifier *Qualifier = HasQualifier ? ME->getQualifier() : nullptr;
  bool IsArrow = ME->isArrow();
  const Expr *Base = ME->getBase();

  return EmitCXXMemberOrOperatorMemberCallExpr(
      CE, MD, ReturnValue, HasQualifier, Qualifier, IsArrow, Base);
}

RValue CodeGenFunction::EmitCXXMemberOrOperatorMemberCallExpr(
    const CallExpr *CE, const CXXMethodDecl *MD, ReturnValueSlot ReturnValue,
    bool HasQualifier, NestedNameSpecifier *Qualifier, bool IsArrow,
    const Expr *Base) {
  assert(isa<CXXMemberCallExpr>(CE) || isa<CXXOperatorCallExpr>(CE));

  // Compute the object pointer.
  bool CanUseVirtualCall = MD->isVirtual() && !HasQualifier;

  const CXXMethodDecl *DevirtualizedMethod = nullptr;
  if (CanUseVirtualCall &&
      MD->getDevirtualizedMethod(Base, getLangOpts().AppleKext)) {
    const CXXRecordDecl *BestDynamicDecl = Base->getBestDynamicClassType();
    DevirtualizedMethod = MD->getCorrespondingMethodInClass(BestDynamicDecl);
    assert(DevirtualizedMethod);
    const CXXRecordDecl *DevirtualizedClass = DevirtualizedMethod->getParent();
    const Expr *Inner = Base->IgnoreParenBaseCasts();
    if (DevirtualizedMethod->getReturnType().getCanonicalType() !=
        MD->getReturnType().getCanonicalType())
      // If the return types are not the same, this might be a case where more
      // code needs to run to compensate for it. For example, the derived
      // method might return a type that inherits form from the return
      // type of MD and has a prefix.
      // For now we just avoid devirtualizing these covariant cases.
      DevirtualizedMethod = nullptr;
    else if (getCXXRecord(Inner) == DevirtualizedClass)
      // If the class of the Inner expression is where the dynamic method
      // is defined, build the this pointer from it.
      Base = Inner;
    else if (getCXXRecord(Base) != DevirtualizedClass) {
      // If the method is defined in a class that is not the best dynamic
      // one or the one of the full expression, we would have to build
      // a derived-to-base cast to compute the correct this pointer, but
      // we don't have support for that yet, so do a virtual call.
      DevirtualizedMethod = nullptr;
    }
  }

  bool TrivialForCodegen =
      MD->isTrivial() || (MD->isDefaulted() && MD->getParent()->isUnion());
  bool TrivialAssignment =
      TrivialForCodegen &&
      (MD->isCopyAssignmentOperator() || MD->isMoveAssignmentOperator()) &&
      !MD->getParent()->mayInsertExtraPadding();

  // C++17 demands that we evaluate the RHS of a (possibly-compound) assignment
  // operator before the LHS.
  CallArgList RtlArgStorage;
  CallArgList *RtlArgs = nullptr;
  LValue TrivialAssignmentRHS;
  if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(CE)) {
    if (OCE->isAssignmentOp()) {
      if (TrivialAssignment) {
        TrivialAssignmentRHS = EmitLValue(CE->getArg(1));
      } else {
        RtlArgs = &RtlArgStorage;
        EmitCallArgs(*RtlArgs, MD->getType()->castAs<FunctionProtoType>(),
                     drop_begin(CE->arguments(), 1), CE->getDirectCallee(),
                     /*ParamsToSkip*/0, EvaluationOrder::ForceRightToLeft);
      }
    }
  }

  LValue This;
  if (IsArrow) {
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address ThisValue = EmitPointerWithAlignment(Base, &BaseInfo, &TBAAInfo);
    This = MakeAddrLValue(ThisValue, Base->getType()->getPointeeType(),
                          BaseInfo, TBAAInfo);
  } else {
    This = EmitLValue(Base);
  }

  if (const CXXConstructorDecl *Ctor = dyn_cast<CXXConstructorDecl>(MD)) {
    // This is the MSVC p->Ctor::Ctor(...) extension. We assume that's
    // constructing a new complete object of type Ctor.
    assert(!RtlArgs);
    assert(ReturnValue.isNull() && "Constructor shouldn't have return value");
    CallArgList Args;
    commonEmitCXXMemberOrOperatorCall(
        *this, {Ctor, Ctor_Complete}, This.getPointer(*this),
        /*ImplicitParam=*/nullptr,
        /*ImplicitParamTy=*/QualType(), CE, Args, nullptr);

    EmitCXXConstructorCall(Ctor, Ctor_Complete, /*ForVirtualBase=*/false,
                           /*Delegating=*/false, This.getAddress(), Args,
                           AggValueSlot::DoesNotOverlap, CE->getExprLoc(),
                           /*NewPointerIsChecked=*/false);
    return RValue::get(nullptr);
  }

  if (TrivialForCodegen) {
    if (isa<CXXDestructorDecl>(MD))
      return RValue::get(nullptr);

    if (TrivialAssignment) {
      // We don't like to generate the trivial copy/move assignment operator
      // when it isn't necessary; just produce the proper effect here.
      // It's important that we use the result of EmitLValue here rather than
      // emitting call arguments, in order to preserve TBAA information from
      // the RHS.
      LValue RHS = isa<CXXOperatorCallExpr>(CE)
                       ? TrivialAssignmentRHS
                       : EmitLValue(*CE->arg_begin());
      EmitAggregateAssign(This, RHS, CE->getType());
      return RValue::get(This.getPointer(*this));
    }

    assert(MD->getParent()->mayInsertExtraPadding() &&
           "unknown trivial member function");
  }

  // Compute the function type we're calling.
  const CXXMethodDecl *CalleeDecl =
      DevirtualizedMethod ? DevirtualizedMethod : MD;
  const CGFunctionInfo *FInfo = nullptr;
  if (const auto *Dtor = dyn_cast<CXXDestructorDecl>(CalleeDecl))
    FInfo = &CGM.getTypes().arrangeCXXStructorDeclaration(
        GlobalDecl(Dtor, Dtor_Complete));
  else
    FInfo = &CGM.getTypes().arrangeCXXMethodDeclaration(CalleeDecl);

  llvm::FunctionType *Ty = CGM.getTypes().GetFunctionType(*FInfo);

  // C++11 [class.mfct.non-static]p2:
  //   If a non-static member function of a class X is called for an object that
  //   is not of type X, or of a type derived from X, the behavior is undefined.
  SourceLocation CallLoc;
  ASTContext &C = getContext();
  if (CE)
    CallLoc = CE->getExprLoc();

  SanitizerSet SkippedChecks;
  if (const auto *CMCE = dyn_cast<CXXMemberCallExpr>(CE)) {
    auto *IOA = CMCE->getImplicitObjectArgument();
    bool IsImplicitObjectCXXThis = IsWrappedCXXThis(IOA);
    if (IsImplicitObjectCXXThis)
      SkippedChecks.set(SanitizerKind::Alignment, true);
    if (IsImplicitObjectCXXThis || isa<DeclRefExpr>(IOA))
      SkippedChecks.set(SanitizerKind::Null, true);
  }

  if (sanitizePerformTypeCheck())
    EmitTypeCheck(CodeGenFunction::TCK_MemberCall, CallLoc,
                  This.emitRawPointer(*this),
                  C.getRecordType(CalleeDecl->getParent()),
                  /*Alignment=*/CharUnits::Zero(), SkippedChecks);

  // C++ [class.virtual]p12:
  //   Explicit qualification with the scope operator (5.1) suppresses the
  //   virtual call mechanism.
  //
  // We also don't emit a virtual call if the base expression has a record type
  // because then we know what the type is.
  bool UseVirtualCall = CanUseVirtualCall && !DevirtualizedMethod;

  if (const CXXDestructorDecl *Dtor = dyn_cast<CXXDestructorDecl>(CalleeDecl)) {
    assert(CE->arg_begin() == CE->arg_end() &&
           "Destructor shouldn't have explicit parameters");
    assert(ReturnValue.isNull() && "Destructor shouldn't have return value");
    if (UseVirtualCall) {
      CGM.getCXXABI().EmitVirtualDestructorCall(*this, Dtor, Dtor_Complete,
                                                This.getAddress(),
                                                cast<CXXMemberCallExpr>(CE));
    } else {
      GlobalDecl GD(Dtor, Dtor_Complete);
      CGCallee Callee;
      if (getLangOpts().AppleKext && Dtor->isVirtual() && HasQualifier)
        Callee = BuildAppleKextVirtualCall(Dtor, Qualifier, Ty);
      else if (!DevirtualizedMethod)
        Callee =
            CGCallee::forDirect(CGM.getAddrOfCXXStructor(GD, FInfo, Ty), GD);
      else {
        Callee = CGCallee::forDirect(CGM.GetAddrOfFunction(GD, Ty), GD);
      }

      QualType ThisTy =
          IsArrow ? Base->getType()->getPointeeType() : Base->getType();
      EmitCXXDestructorCall(GD, Callee, This.getPointer(*this), ThisTy,
                            /*ImplicitParam=*/nullptr,
                            /*ImplicitParamTy=*/QualType(), CE);
    }
    return RValue::get(nullptr);
  }

  // FIXME: Uses of 'MD' past this point need to be audited. We may need to use
  // 'CalleeDecl' instead.

  CGCallee Callee;
  if (UseVirtualCall) {
    Callee = CGCallee::forVirtual(CE, MD, This.getAddress(), Ty);
  } else {
    if (SanOpts.has(SanitizerKind::CFINVCall) &&
        MD->getParent()->isDynamicClass()) {
      llvm::Value *VTable;
      const CXXRecordDecl *RD;
      std::tie(VTable, RD) = CGM.getCXXABI().LoadVTablePtr(
          *this, This.getAddress(), CalleeDecl->getParent());
      EmitVTablePtrCheckForCall(RD, VTable, CFITCK_NVCall, CE->getBeginLoc());
    }

    if (getLangOpts().AppleKext && MD->isVirtual() && HasQualifier)
      Callee = BuildAppleKextVirtualCall(MD, Qualifier, Ty);
    else if (!DevirtualizedMethod)
      Callee =
          CGCallee::forDirect(CGM.GetAddrOfFunction(MD, Ty), GlobalDecl(MD));
    else {
      Callee =
          CGCallee::forDirect(CGM.GetAddrOfFunction(DevirtualizedMethod, Ty),
                              GlobalDecl(DevirtualizedMethod));
    }
  }

  if (MD->isVirtual()) {
    Address NewThisAddr =
        CGM.getCXXABI().adjustThisArgumentForVirtualFunctionCall(
            *this, CalleeDecl, This.getAddress(), UseVirtualCall);
    This.setAddress(NewThisAddr);
  }

  return EmitCXXMemberOrOperatorCall(
      CalleeDecl, Callee, ReturnValue, This.getPointer(*this),
      /*ImplicitParam=*/nullptr, QualType(), CE, RtlArgs);
}

RValue
CodeGenFunction::EmitCXXMemberPointerCallExpr(const CXXMemberCallExpr *E,
                                              ReturnValueSlot ReturnValue) {
  const BinaryOperator *BO =
      cast<BinaryOperator>(E->getCallee()->IgnoreParens());
  const Expr *BaseExpr = BO->getLHS();
  const Expr *MemFnExpr = BO->getRHS();

  const auto *MPT = MemFnExpr->getType()->castAs<MemberPointerType>();
  const auto *FPT = MPT->getPointeeType()->castAs<FunctionProtoType>();
  const auto *RD =
      cast<CXXRecordDecl>(MPT->getClass()->castAs<RecordType>()->getDecl());

  // Emit the 'this' pointer.
  Address This = Address::invalid();
  if (BO->getOpcode() == BO_PtrMemI)
    This = EmitPointerWithAlignment(BaseExpr, nullptr, nullptr, KnownNonNull);
  else
    This = EmitLValue(BaseExpr, KnownNonNull).getAddress();

  EmitTypeCheck(TCK_MemberCall, E->getExprLoc(), This.emitRawPointer(*this),
                QualType(MPT->getClass(), 0));

  // Get the member function pointer.
  llvm::Value *MemFnPtr = EmitScalarExpr(MemFnExpr);

  // Ask the ABI to load the callee.  Note that This is modified.
  llvm::Value *ThisPtrForCall = nullptr;
  CGCallee Callee =
    CGM.getCXXABI().EmitLoadOfMemberFunctionPointer(*this, BO, This,
                                             ThisPtrForCall, MemFnPtr, MPT);

  CallArgList Args;

  QualType ThisType =
    getContext().getPointerType(getContext().getTagDeclType(RD));

  // Push the this ptr.
  Args.add(RValue::get(ThisPtrForCall), ThisType);

  RequiredArgs required = RequiredArgs::forPrototypePlus(FPT, 1);

  // And the rest of the call args
  EmitCallArgs(Args, FPT, E->arguments());
  return EmitCall(CGM.getTypes().arrangeCXXMethodCall(Args, FPT, required,
                                                      /*PrefixSize=*/0),
                  Callee, ReturnValue, Args, nullptr, E == MustTailCall,
                  E->getExprLoc());
}

RValue
CodeGenFunction::EmitCXXOperatorMemberCallExpr(const CXXOperatorCallExpr *E,
                                               const CXXMethodDecl *MD,
                                               ReturnValueSlot ReturnValue) {
  assert(MD->isImplicitObjectMemberFunction() &&
         "Trying to emit a member call expr on a static method!");
  return EmitCXXMemberOrOperatorMemberCallExpr(
      E, MD, ReturnValue, /*HasQualifier=*/false, /*Qualifier=*/nullptr,
      /*IsArrow=*/false, E->getArg(0));
}

RValue CodeGenFunction::EmitCUDAKernelCallExpr(const CUDAKernelCallExpr *E,
                                               ReturnValueSlot ReturnValue) {
  return CGM.getCUDARuntime().EmitCUDAKernelCallExpr(*this, E, ReturnValue);
}

static void EmitNullBaseClassInitialization(CodeGenFunction &CGF,
                                            Address DestPtr,
                                            const CXXRecordDecl *Base) {
  if (Base->isEmpty())
    return;

  DestPtr = DestPtr.withElementType(CGF.Int8Ty);

  const ASTRecordLayout &Layout = CGF.getContext().getASTRecordLayout(Base);
  CharUnits NVSize = Layout.getNonVirtualSize();

  // We cannot simply zero-initialize the entire base sub-object if vbptrs are
  // present, they are initialized by the most derived class before calling the
  // constructor.
  SmallVector<std::pair<CharUnits, CharUnits>, 1> Stores;
  Stores.emplace_back(CharUnits::Zero(), NVSize);

  // Each store is split by the existence of a vbptr.
  CharUnits VBPtrWidth = CGF.getPointerSize();
  std::vector<CharUnits> VBPtrOffsets =
      CGF.CGM.getCXXABI().getVBPtrOffsets(Base);
  for (CharUnits VBPtrOffset : VBPtrOffsets) {
    // Stop before we hit any virtual base pointers located in virtual bases.
    if (VBPtrOffset >= NVSize)
      break;
    std::pair<CharUnits, CharUnits> LastStore = Stores.pop_back_val();
    CharUnits LastStoreOffset = LastStore.first;
    CharUnits LastStoreSize = LastStore.second;

    CharUnits SplitBeforeOffset = LastStoreOffset;
    CharUnits SplitBeforeSize = VBPtrOffset - SplitBeforeOffset;
    assert(!SplitBeforeSize.isNegative() && "negative store size!");
    if (!SplitBeforeSize.isZero())
      Stores.emplace_back(SplitBeforeOffset, SplitBeforeSize);

    CharUnits SplitAfterOffset = VBPtrOffset + VBPtrWidth;
    CharUnits SplitAfterSize = LastStoreSize - SplitAfterOffset;
    assert(!SplitAfterSize.isNegative() && "negative store size!");
    if (!SplitAfterSize.isZero())
      Stores.emplace_back(SplitAfterOffset, SplitAfterSize);
  }

  // If the type contains a pointer to data member we can't memset it to zero.
  // Instead, create a null constant and copy it to the destination.
  // TODO: there are other patterns besides zero that we can usefully memset,
  // like -1, which happens to be the pattern used by member-pointers.
  // TODO: isZeroInitializable can be over-conservative in the case where a
  // virtual base contains a member pointer.
  llvm::Constant *NullConstantForBase = CGF.CGM.EmitNullConstantForBase(Base);
  if (!NullConstantForBase->isNullValue()) {
    llvm::GlobalVariable *NullVariable = new llvm::GlobalVariable(
        CGF.CGM.getModule(), NullConstantForBase->getType(),
        /*isConstant=*/true, llvm::GlobalVariable::PrivateLinkage,
        NullConstantForBase, Twine());

    CharUnits Align =
        std::max(Layout.getNonVirtualAlignment(), DestPtr.getAlignment());
    NullVariable->setAlignment(Align.getAsAlign());

    Address SrcPtr(NullVariable, CGF.Int8Ty, Align);

    // Get and call the appropriate llvm.memcpy overload.
    for (std::pair<CharUnits, CharUnits> Store : Stores) {
      CharUnits StoreOffset = Store.first;
      CharUnits StoreSize = Store.second;
      llvm::Value *StoreSizeVal = CGF.CGM.getSize(StoreSize);
      CGF.Builder.CreateMemCpy(
          CGF.Builder.CreateConstInBoundsByteGEP(DestPtr, StoreOffset),
          CGF.Builder.CreateConstInBoundsByteGEP(SrcPtr, StoreOffset),
          StoreSizeVal);
    }

  // Otherwise, just memset the whole thing to zero.  This is legal
  // because in LLVM, all default initializers (other than the ones we just
  // handled above) are guaranteed to have a bit pattern of all zeros.
  } else {
    for (std::pair<CharUnits, CharUnits> Store : Stores) {
      CharUnits StoreOffset = Store.first;
      CharUnits StoreSize = Store.second;
      llvm::Value *StoreSizeVal = CGF.CGM.getSize(StoreSize);
      CGF.Builder.CreateMemSet(
          CGF.Builder.CreateConstInBoundsByteGEP(DestPtr, StoreOffset),
          CGF.Builder.getInt8(0), StoreSizeVal);
    }
  }
}

void
CodeGenFunction::EmitCXXConstructExpr(const CXXConstructExpr *E,
                                      AggValueSlot Dest) {
  assert(!Dest.isIgnored() && "Must have a destination!");
  const CXXConstructorDecl *CD = E->getConstructor();

  // If we require zero initialization before (or instead of) calling the
  // constructor, as can be the case with a non-user-provided default
  // constructor, emit the zero initialization now, unless destination is
  // already zeroed.
  if (E->requiresZeroInitialization() && !Dest.isZeroed()) {
    switch (E->getConstructionKind()) {
    case CXXConstructionKind::Delegating:
    case CXXConstructionKind::Complete:
      EmitNullInitialization(Dest.getAddress(), E->getType());
      break;
    case CXXConstructionKind::VirtualBase:
    case CXXConstructionKind::NonVirtualBase:
      EmitNullBaseClassInitialization(*this, Dest.getAddress(),
                                      CD->getParent());
      break;
    }
  }

  // If this is a call to a trivial default constructor, do nothing.
  if (CD->isTrivial() && CD->isDefaultConstructor())
    return;

  // Elide the constructor if we're constructing from a temporary.
  if (getLangOpts().ElideConstructors && E->isElidable()) {
    // FIXME: This only handles the simplest case, where the source object
    //        is passed directly as the first argument to the constructor.
    //        This should also handle stepping though implicit casts and
    //        conversion sequences which involve two steps, with a
    //        conversion operator followed by a converting constructor.
    const Expr *SrcObj = E->getArg(0);
    assert(SrcObj->isTemporaryObject(getContext(), CD->getParent()));
    assert(
        getContext().hasSameUnqualifiedType(E->getType(), SrcObj->getType()));
    EmitAggExpr(SrcObj, Dest);
    return;
  }

  if (const ArrayType *arrayType
        = getContext().getAsArrayType(E->getType())) {
    EmitCXXAggrConstructorCall(CD, arrayType, Dest.getAddress(), E,
                               Dest.isSanitizerChecked());
  } else {
    CXXCtorType Type = Ctor_Complete;
    bool ForVirtualBase = false;
    bool Delegating = false;

    switch (E->getConstructionKind()) {
    case CXXConstructionKind::Delegating:
      // We should be emitting a constructor; GlobalDecl will assert this
      Type = CurGD.getCtorType();
      Delegating = true;
      break;

    case CXXConstructionKind::Complete:
      Type = Ctor_Complete;
      break;

    case CXXConstructionKind::VirtualBase:
      ForVirtualBase = true;
      [[fallthrough]];

    case CXXConstructionKind::NonVirtualBase:
      Type = Ctor_Base;
     }

     // Call the constructor.
     EmitCXXConstructorCall(CD, Type, ForVirtualBase, Delegating, Dest, E);
  }
}

void CodeGenFunction::EmitSynthesizedCXXCopyCtor(Address Dest, Address Src,
                                                 const Expr *Exp) {
  if (const ExprWithCleanups *E = dyn_cast<ExprWithCleanups>(Exp))
    Exp = E->getSubExpr();
  assert(isa<CXXConstructExpr>(Exp) &&
         "EmitSynthesizedCXXCopyCtor - unknown copy ctor expr");
  const CXXConstructExpr* E = cast<CXXConstructExpr>(Exp);
  const CXXConstructorDecl *CD = E->getConstructor();
  RunCleanupsScope Scope(*this);

  // If we require zero initialization before (or instead of) calling the
  // constructor, as can be the case with a non-user-provided default
  // constructor, emit the zero initialization now.
  // FIXME. Do I still need this for a copy ctor synthesis?
  if (E->requiresZeroInitialization())
    EmitNullInitialization(Dest, E->getType());

  assert(!getContext().getAsConstantArrayType(E->getType())
         && "EmitSynthesizedCXXCopyCtor - Copied-in Array");
  EmitSynthesizedCXXCopyCtorCall(CD, Dest, Src, E);
}

static CharUnits CalculateCookiePadding(CodeGenFunction &CGF,
                                        const CXXNewExpr *E) {
  if (!E->isArray())
    return CharUnits::Zero();

  // No cookie is required if the operator new[] being used is the
  // reserved placement operator new[].
  if (E->getOperatorNew()->isReservedGlobalPlacementOperator())
    return CharUnits::Zero();

  return CGF.CGM.getCXXABI().GetArrayCookieSize(E);
}

static llvm::Value *EmitCXXNewAllocSize(CodeGenFunction &CGF,
                                        const CXXNewExpr *e,
                                        unsigned minElements,
                                        llvm::Value *&numElements,
                                        llvm::Value *&sizeWithoutCookie) {
  QualType type = e->getAllocatedType();

  if (!e->isArray()) {
    CharUnits typeSize = CGF.getContext().getTypeSizeInChars(type);
    sizeWithoutCookie
      = llvm::ConstantInt::get(CGF.SizeTy, typeSize.getQuantity());
    return sizeWithoutCookie;
  }

  // The width of size_t.
  unsigned sizeWidth = CGF.SizeTy->getBitWidth();

  // Figure out the cookie size.
  llvm::APInt cookieSize(sizeWidth,
                         CalculateCookiePadding(CGF, e).getQuantity());

  // Emit the array size expression.
  // We multiply the size of all dimensions for NumElements.
  // e.g for 'int[2][3]', ElemType is 'int' and NumElements is 6.
  numElements =
    ConstantEmitter(CGF).tryEmitAbstract(*e->getArraySize(), e->getType());
  if (!numElements)
    numElements = CGF.EmitScalarExpr(*e->getArraySize());
  assert(isa<llvm::IntegerType>(numElements->getType()));

  // The number of elements can be have an arbitrary integer type;
  // essentially, we need to multiply it by a constant factor, add a
  // cookie size, and verify that the result is representable as a
  // size_t.  That's just a gloss, though, and it's wrong in one
  // important way: if the count is negative, it's an error even if
  // the cookie size would bring the total size >= 0.
  bool isSigned
    = (*e->getArraySize())->getType()->isSignedIntegerOrEnumerationType();
  llvm::IntegerType *numElementsType
    = cast<llvm::IntegerType>(numElements->getType());
  unsigned numElementsWidth = numElementsType->getBitWidth();

  // Compute the constant factor.
  llvm::APInt arraySizeMultiplier(sizeWidth, 1);
  while (const ConstantArrayType *CAT
             = CGF.getContext().getAsConstantArrayType(type)) {
    type = CAT->getElementType();
    arraySizeMultiplier *= CAT->getSize();
  }

  CharUnits typeSize = CGF.getContext().getTypeSizeInChars(type);
  llvm::APInt typeSizeMultiplier(sizeWidth, typeSize.getQuantity());
  typeSizeMultiplier *= arraySizeMultiplier;

  // This will be a size_t.
  llvm::Value *size;

  // If someone is doing 'new int[42]' there is no need to do a dynamic check.
  // Don't bloat the -O0 code.
  if (llvm::ConstantInt *numElementsC =
        dyn_cast<llvm::ConstantInt>(numElements)) {
    const llvm::APInt &count = numElementsC->getValue();

    bool hasAnyOverflow = false;

    // If 'count' was a negative number, it's an overflow.
    if (isSigned && count.isNegative())
      hasAnyOverflow = true;

    // We want to do all this arithmetic in size_t.  If numElements is
    // wider than that, check whether it's already too big, and if so,
    // overflow.
    else if (numElementsWidth > sizeWidth &&
             numElementsWidth - sizeWidth > count.countl_zero())
      hasAnyOverflow = true;

    // Okay, compute a count at the right width.
    llvm::APInt adjustedCount = count.zextOrTrunc(sizeWidth);

    // If there is a brace-initializer, we cannot allocate fewer elements than
    // there are initializers. If we do, that's treated like an overflow.
    if (adjustedCount.ult(minElements))
      hasAnyOverflow = true;

    // Scale numElements by that.  This might overflow, but we don't
    // care because it only overflows if allocationSize does, too, and
    // if that overflows then we shouldn't use this.
    numElements = llvm::ConstantInt::get(CGF.SizeTy,
                                         adjustedCount * arraySizeMultiplier);

    // Compute the size before cookie, and track whether it overflowed.
    bool overflow;
    llvm::APInt allocationSize
      = adjustedCount.umul_ov(typeSizeMultiplier, overflow);
    hasAnyOverflow |= overflow;

    // Add in the cookie, and check whether it's overflowed.
    if (cookieSize != 0) {
      // Save the current size without a cookie.  This shouldn't be
      // used if there was overflow.
      sizeWithoutCookie = llvm::ConstantInt::get(CGF.SizeTy, allocationSize);

      allocationSize = allocationSize.uadd_ov(cookieSize, overflow);
      hasAnyOverflow |= overflow;
    }

    // On overflow, produce a -1 so operator new will fail.
    if (hasAnyOverflow) {
      size = llvm::Constant::getAllOnesValue(CGF.SizeTy);
    } else {
      size = llvm::ConstantInt::get(CGF.SizeTy, allocationSize);
    }

  // Otherwise, we might need to use the overflow intrinsics.
  } else {
    // There are up to five conditions we need to test for:
    // 1) if isSigned, we need to check whether numElements is negative;
    // 2) if numElementsWidth > sizeWidth, we need to check whether
    //   numElements is larger than something representable in size_t;
    // 3) if minElements > 0, we need to check whether numElements is smaller
    //    than that.
    // 4) we need to compute
    //      sizeWithoutCookie := numElements * typeSizeMultiplier
    //    and check whether it overflows; and
    // 5) if we need a cookie, we need to compute
    //      size := sizeWithoutCookie + cookieSize
    //    and check whether it overflows.

    llvm::Value *hasOverflow = nullptr;

    // If numElementsWidth > sizeWidth, then one way or another, we're
    // going to have to do a comparison for (2), and this happens to
    // take care of (1), too.
    if (numElementsWidth > sizeWidth) {
      llvm::APInt threshold =
          llvm::APInt::getOneBitSet(numElementsWidth, sizeWidth);

      llvm::Value *thresholdV
        = llvm::ConstantInt::get(numElementsType, threshold);

      hasOverflow = CGF.Builder.CreateICmpUGE(numElements, thresholdV);
      numElements = CGF.Builder.CreateTrunc(numElements, CGF.SizeTy);

    // Otherwise, if we're signed, we want to sext up to size_t.
    } else if (isSigned) {
      if (numElementsWidth < sizeWidth)
        numElements = CGF.Builder.CreateSExt(numElements, CGF.SizeTy);

      // If there's a non-1 type size multiplier, then we can do the
      // signedness check at the same time as we do the multiply
      // because a negative number times anything will cause an
      // unsigned overflow.  Otherwise, we have to do it here. But at least
      // in this case, we can subsume the >= minElements check.
      if (typeSizeMultiplier == 1)
        hasOverflow = CGF.Builder.CreateICmpSLT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements));

    // Otherwise, zext up to size_t if necessary.
    } else if (numElementsWidth < sizeWidth) {
      numElements = CGF.Builder.CreateZExt(numElements, CGF.SizeTy);
    }

    assert(numElements->getType() == CGF.SizeTy);

    if (minElements) {
      // Don't allow allocation of fewer elements than we have initializers.
      if (!hasOverflow) {
        hasOverflow = CGF.Builder.CreateICmpULT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements));
      } else if (numElementsWidth > sizeWidth) {
        // The other existing overflow subsumes this check.
        // We do an unsigned comparison, since any signed value < -1 is
        // taken care of either above or below.
        hasOverflow = CGF.Builder.CreateOr(hasOverflow,
                          CGF.Builder.CreateICmpULT(numElements,
                              llvm::ConstantInt::get(CGF.SizeTy, minElements)));
      }
    }

    size = numElements;

    // Multiply by the type size if necessary.  This multiplier
    // includes all the factors for nested arrays.
    //
    // This step also causes numElements to be scaled up by the
    // nested-array factor if necessary.  Overflow on this computation
    // can be ignored because the result shouldn't be used if
    // allocation fails.
    if (typeSizeMultiplier != 1) {
      llvm::Function *umul_with_overflow
        = CGF.CGM.getIntrinsic(llvm::Intrinsic::umul_with_overflow, CGF.SizeTy);

      llvm::Value *tsmV =
        llvm::ConstantInt::get(CGF.SizeTy, typeSizeMultiplier);
      llvm::Value *result =
          CGF.Builder.CreateCall(umul_with_overflow, {size, tsmV});

      llvm::Value *overflowed = CGF.Builder.CreateExtractValue(result, 1);
      if (hasOverflow)
        hasOverflow = CGF.Builder.CreateOr(hasOverflow, overflowed);
      else
        hasOverflow = overflowed;

      size = CGF.Builder.CreateExtractValue(result, 0);

      // Also scale up numElements by the array size multiplier.
      if (arraySizeMultiplier != 1) {
        // If the base element type size is 1, then we can re-use the
        // multiply we just did.
        if (typeSize.isOne()) {
          assert(arraySizeMultiplier == typeSizeMultiplier);
          numElements = size;

        // Otherwise we need a separate multiply.
        } else {
          llvm::Value *asmV =
            llvm::ConstantInt::get(CGF.SizeTy, arraySizeMultiplier);
          numElements = CGF.Builder.CreateMul(numElements, asmV);
        }
      }
    } else {
      // numElements doesn't need to be scaled.
      assert(arraySizeMultiplier == 1);
    }

    // Add in the cookie size if necessary.
    if (cookieSize != 0) {
      sizeWithoutCookie = size;

      llvm::Function *uadd_with_overflow
        = CGF.CGM.getIntrinsic(llvm::Intrinsic::uadd_with_overflow, CGF.SizeTy);

      llvm::Value *cookieSizeV = llvm::ConstantInt::get(CGF.SizeTy, cookieSize);
      llvm::Value *result =
          CGF.Builder.CreateCall(uadd_with_overflow, {size, cookieSizeV});

      llvm::Value *overflowed = CGF.Builder.CreateExtractValue(result, 1);
      if (hasOverflow)
        hasOverflow = CGF.Builder.CreateOr(hasOverflow, overflowed);
      else
        hasOverflow = overflowed;

      size = CGF.Builder.CreateExtractValue(result, 0);
    }

    // If we had any possibility of dynamic overflow, make a select to
    // overwrite 'size' with an all-ones value, which should cause
    // operator new to throw.
    if (hasOverflow)
      size = CGF.Builder.CreateSelect(hasOverflow,
                                 llvm::Constant::getAllOnesValue(CGF.SizeTy),
                                      size);
  }

  if (cookieSize == 0)
    sizeWithoutCookie = size;
  else
    assert(sizeWithoutCookie && "didn't set sizeWithoutCookie?");

  return size;
}

static void StoreAnyExprIntoOneUnit(CodeGenFunction &CGF, const Expr *Init,
                                    QualType AllocType, Address NewPtr,
                                    AggValueSlot::Overlap_t MayOverlap) {
  // FIXME: Refactor with EmitExprAsInit.
  switch (CGF.getEvaluationKind(AllocType)) {
  case TEK_Scalar:
    CGF.EmitScalarInit(Init, nullptr,
                       CGF.MakeAddrLValue(NewPtr, AllocType), false);
    return;
  case TEK_Complex:
    CGF.EmitComplexExprIntoLValue(Init, CGF.MakeAddrLValue(NewPtr, AllocType),
                                  /*isInit*/ true);
    return;
  case TEK_Aggregate: {
    AggValueSlot Slot
      = AggValueSlot::forAddr(NewPtr, AllocType.getQualifiers(),
                              AggValueSlot::IsDestructed,
                              AggValueSlot::DoesNotNeedGCBarriers,
                              AggValueSlot::IsNotAliased,
                              MayOverlap, AggValueSlot::IsNotZeroed,
                              AggValueSlot::IsSanitizerChecked);
    CGF.EmitAggExpr(Init, Slot);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}

void CodeGenFunction::EmitNewArrayInitializer(
    const CXXNewExpr *E, QualType ElementType, llvm::Type *ElementTy,
    Address BeginPtr, llvm::Value *NumElements,
    llvm::Value *AllocSizeWithoutCookie) {
  // If we have a type with trivial initialization and no initializer,
  // there's nothing to do.
  if (!E->hasInitializer())
    return;

  Address CurPtr = BeginPtr;

  unsigned InitListElements = 0;

  const Expr *Init = E->getInitializer();
  Address EndOfInit = Address::invalid();
  QualType::DestructionKind DtorKind = ElementType.isDestructedType();
  CleanupDeactivationScope deactivation(*this);
  bool pushedCleanup = false;

  CharUnits ElementSize = getContext().getTypeSizeInChars(ElementType);
  CharUnits ElementAlign =
    BeginPtr.getAlignment().alignmentOfArrayElement(ElementSize);

  // Attempt to perform zero-initialization using memset.
  auto TryMemsetInitialization = [&]() -> bool {
    // FIXME: If the type is a pointer-to-data-member under the Itanium ABI,
    // we can initialize with a memset to -1.
    if (!CGM.getTypes().isZeroInitializable(ElementType))
      return false;

    // Optimization: since zero initialization will just set the memory
    // to all zeroes, generate a single memset to do it in one shot.

    // Subtract out the size of any elements we've already initialized.
    auto *RemainingSize = AllocSizeWithoutCookie;
    if (InitListElements) {
      // We know this can't overflow; we check this when doing the allocation.
      auto *InitializedSize = llvm::ConstantInt::get(
          RemainingSize->getType(),
          getContext().getTypeSizeInChars(ElementType).getQuantity() *
              InitListElements);
      RemainingSize = Builder.CreateSub(RemainingSize, InitializedSize);
    }

    // Create the memset.
    Builder.CreateMemSet(CurPtr, Builder.getInt8(0), RemainingSize, false);
    return true;
  };

  const InitListExpr *ILE = dyn_cast<InitListExpr>(Init);
  const CXXParenListInitExpr *CPLIE = nullptr;
  const StringLiteral *SL = nullptr;
  const ObjCEncodeExpr *OCEE = nullptr;
  const Expr *IgnoreParen = nullptr;
  if (!ILE) {
    IgnoreParen = Init->IgnoreParenImpCasts();
    CPLIE = dyn_cast<CXXParenListInitExpr>(IgnoreParen);
    SL = dyn_cast<StringLiteral>(IgnoreParen);
    OCEE = dyn_cast<ObjCEncodeExpr>(IgnoreParen);
  }

  // If the initializer is an initializer list, first do the explicit elements.
  if (ILE || CPLIE || SL || OCEE) {
    // Initializing from a (braced) string literal is a special case; the init
    // list element does not initialize a (single) array element.
    if ((ILE && ILE->isStringLiteralInit()) || SL || OCEE) {
      if (!ILE)
        Init = IgnoreParen;
      // Initialize the initial portion of length equal to that of the string
      // literal. The allocation must be for at least this much; we emitted a
      // check for that earlier.
      AggValueSlot Slot =
          AggValueSlot::forAddr(CurPtr, ElementType.getQualifiers(),
                                AggValueSlot::IsDestructed,
                                AggValueSlot::DoesNotNeedGCBarriers,
                                AggValueSlot::IsNotAliased,
                                AggValueSlot::DoesNotOverlap,
                                AggValueSlot::IsNotZeroed,
                                AggValueSlot::IsSanitizerChecked);
      EmitAggExpr(ILE ? ILE->getInit(0) : Init, Slot);

      // Move past these elements.
      InitListElements =
          cast<ConstantArrayType>(Init->getType()->getAsArrayTypeUnsafe())
              ->getZExtSize();
      CurPtr = Builder.CreateConstInBoundsGEP(
          CurPtr, InitListElements, "string.init.end");

      // Zero out the rest, if any remain.
      llvm::ConstantInt *ConstNum = dyn_cast<llvm::ConstantInt>(NumElements);
      if (!ConstNum || !ConstNum->equalsInt(InitListElements)) {
        bool OK = TryMemsetInitialization();
        (void)OK;
        assert(OK && "couldn't memset character type?");
      }
      return;
    }

    ArrayRef<const Expr *> InitExprs =
        ILE ? ILE->inits() : CPLIE->getInitExprs();
    InitListElements = InitExprs.size();

    // If this is a multi-dimensional array new, we will initialize multiple
    // elements with each init list element.
    QualType AllocType = E->getAllocatedType();
    if (const ConstantArrayType *CAT = dyn_cast_or_null<ConstantArrayType>(
            AllocType->getAsArrayTypeUnsafe())) {
      ElementTy = ConvertTypeForMem(AllocType);
      CurPtr = CurPtr.withElementType(ElementTy);
      InitListElements *= getContext().getConstantArrayElementCount(CAT);
    }

    // Enter a partial-destruction Cleanup if necessary.
    if (DtorKind) {
      AllocaTrackerRAII AllocaTracker(*this);
      // In principle we could tell the Cleanup where we are more
      // directly, but the control flow can get so varied here that it
      // would actually be quite complex.  Therefore we go through an
      // alloca.
      llvm::Instruction *DominatingIP =
          Builder.CreateFlagLoad(llvm::ConstantInt::getNullValue(Int8PtrTy));
      EndOfInit = CreateTempAlloca(BeginPtr.getType(), getPointerAlign(),
                                   "array.init.end");
      pushIrregularPartialArrayCleanup(BeginPtr.emitRawPointer(*this),
                                       EndOfInit, ElementType, ElementAlign,
                                       getDestroyer(DtorKind));
      cast<EHCleanupScope>(*EHStack.find(EHStack.stable_begin()))
          .AddAuxAllocas(AllocaTracker.Take());
      DeferredDeactivationCleanupStack.push_back(
          {EHStack.stable_begin(), DominatingIP});
      pushedCleanup = true;
    }

    CharUnits StartAlign = CurPtr.getAlignment();
    unsigned i = 0;
    for (const Expr *IE : InitExprs) {
      // Tell the cleanup that it needs to destroy up to this
      // element.  TODO: some of these stores can be trivially
      // observed to be unnecessary.
      if (EndOfInit.isValid()) {
        Builder.CreateStore(CurPtr.emitRawPointer(*this), EndOfInit);
      }
      // FIXME: If the last initializer is an incomplete initializer list for
      // an array, and we have an array filler, we can fold together the two
      // initialization loops.
      StoreAnyExprIntoOneUnit(*this, IE, IE->getType(), CurPtr,
                              AggValueSlot::DoesNotOverlap);
      CurPtr = Address(Builder.CreateInBoundsGEP(CurPtr.getElementType(),
                                                 CurPtr.emitRawPointer(*this),
                                                 Builder.getSize(1),
                                                 "array.exp.next"),
                       CurPtr.getElementType(),
                       StartAlign.alignmentAtOffset((++i) * ElementSize));
    }

    // The remaining elements are filled with the array filler expression.
    Init = ILE ? ILE->getArrayFiller() : CPLIE->getArrayFiller();

    // Extract the initializer for the individual array elements by pulling
    // out the array filler from all the nested initializer lists. This avoids
    // generating a nested loop for the initialization.
    while (Init && Init->getType()->isConstantArrayType()) {
      auto *SubILE = dyn_cast<InitListExpr>(Init);
      if (!SubILE)
        break;
      assert(SubILE->getNumInits() == 0 && "explicit inits in array filler?");
      Init = SubILE->getArrayFiller();
    }

    // Switch back to initializing one base element at a time.
    CurPtr = CurPtr.withElementType(BeginPtr.getElementType());
  }

  // If all elements have already been initialized, skip any further
  // initialization.
  llvm::ConstantInt *ConstNum = dyn_cast<llvm::ConstantInt>(NumElements);
  if (ConstNum && ConstNum->getZExtValue() <= InitListElements) {
    return;
  }

  assert(Init && "have trailing elements to initialize but no initializer");

  // If this is a constructor call, try to optimize it out, and failing that
  // emit a single loop to initialize all remaining elements.
  if (const CXXConstructExpr *CCE = dyn_cast<CXXConstructExpr>(Init)) {
    CXXConstructorDecl *Ctor = CCE->getConstructor();
    if (Ctor->isTrivial()) {
      // If new expression did not specify value-initialization, then there
      // is no initialization.
      if (!CCE->requiresZeroInitialization() || Ctor->getParent()->isEmpty())
        return;

      if (TryMemsetInitialization())
        return;
    }

    // Store the new Cleanup position for irregular Cleanups.
    //
    // FIXME: Share this cleanup with the constructor call emission rather than
    // having it create a cleanup of its own.
    if (EndOfInit.isValid())
      Builder.CreateStore(CurPtr.emitRawPointer(*this), EndOfInit);

    // Emit a constructor call loop to initialize the remaining elements.
    if (InitListElements)
      NumElements = Builder.CreateSub(
          NumElements,
          llvm::ConstantInt::get(NumElements->getType(), InitListElements));
    EmitCXXAggrConstructorCall(Ctor, NumElements, CurPtr, CCE,
                               /*NewPointerIsChecked*/true,
                               CCE->requiresZeroInitialization());
    return;
  }

  // If this is value-initialization, we can usually use memset.
  ImplicitValueInitExpr IVIE(ElementType);
  if (isa<ImplicitValueInitExpr>(Init)) {
    if (TryMemsetInitialization())
      return;

    // Switch to an ImplicitValueInitExpr for the element type. This handles
    // only one case: multidimensional array new of pointers to members. In
    // all other cases, we already have an initializer for the array element.
    Init = &IVIE;
  }

  // At this point we should have found an initializer for the individual
  // elements of the array.
  assert(getContext().hasSameUnqualifiedType(ElementType, Init->getType()) &&
         "got wrong type of element to initialize");

  // If we have an empty initializer list, we can usually use memset.
  if (auto *ILE = dyn_cast<InitListExpr>(Init))
    if (ILE->getNumInits() == 0 && TryMemsetInitialization())
      return;

  // If we have a struct whose every field is value-initialized, we can
  // usually use memset.
  if (auto *ILE = dyn_cast<InitListExpr>(Init)) {
    if (const RecordType *RType = ILE->getType()->getAs<RecordType>()) {
      if (RType->getDecl()->isStruct()) {
        unsigned NumElements = 0;
        if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RType->getDecl()))
          NumElements = CXXRD->getNumBases();
        for (auto *Field : RType->getDecl()->fields())
          if (!Field->isUnnamedBitField())
            ++NumElements;
        // FIXME: Recurse into nested InitListExprs.
        if (ILE->getNumInits() == NumElements)
          for (unsigned i = 0, e = ILE->getNumInits(); i != e; ++i)
            if (!isa<ImplicitValueInitExpr>(ILE->getInit(i)))
              --NumElements;
        if (ILE->getNumInits() == NumElements && TryMemsetInitialization())
          return;
      }
    }
  }

  // Create the loop blocks.
  llvm::BasicBlock *EntryBB = Builder.GetInsertBlock();
  llvm::BasicBlock *LoopBB = createBasicBlock("new.loop");
  llvm::BasicBlock *ContBB = createBasicBlock("new.loop.end");

  // Find the end of the array, hoisted out of the loop.
  llvm::Value *EndPtr = Builder.CreateInBoundsGEP(
      BeginPtr.getElementType(), BeginPtr.emitRawPointer(*this), NumElements,
      "array.end");

  // If the number of elements isn't constant, we have to now check if there is
  // anything left to initialize.
  if (!ConstNum) {
    llvm::Value *IsEmpty = Builder.CreateICmpEQ(CurPtr.emitRawPointer(*this),
                                                EndPtr, "array.isempty");
    Builder.CreateCondBr(IsEmpty, ContBB, LoopBB);
  }

  // Enter the loop.
  EmitBlock(LoopBB);

  // Set up the current-element phi.
  llvm::PHINode *CurPtrPhi =
      Builder.CreatePHI(CurPtr.getType(), 2, "array.cur");
  CurPtrPhi->addIncoming(CurPtr.emitRawPointer(*this), EntryBB);

  CurPtr = Address(CurPtrPhi, CurPtr.getElementType(), ElementAlign);

  // Store the new Cleanup position for irregular Cleanups.
  if (EndOfInit.isValid())
    Builder.CreateStore(CurPtr.emitRawPointer(*this), EndOfInit);

  // Enter a partial-destruction Cleanup if necessary.
  if (!pushedCleanup && needsEHCleanup(DtorKind)) {
    llvm::Instruction *DominatingIP =
        Builder.CreateFlagLoad(llvm::ConstantInt::getNullValue(Int8PtrTy));
    pushRegularPartialArrayCleanup(BeginPtr.emitRawPointer(*this),
                                   CurPtr.emitRawPointer(*this), ElementType,
                                   ElementAlign, getDestroyer(DtorKind));
    DeferredDeactivationCleanupStack.push_back(
        {EHStack.stable_begin(), DominatingIP});
  }

  // Emit the initializer into this element.
  StoreAnyExprIntoOneUnit(*this, Init, Init->getType(), CurPtr,
                          AggValueSlot::DoesNotOverlap);

  // Leave the Cleanup if we entered one.
  deactivation.ForceDeactivate();

  // Advance to the next element by adjusting the pointer type as necessary.
  llvm::Value *NextPtr = Builder.CreateConstInBoundsGEP1_32(
      ElementTy, CurPtr.emitRawPointer(*this), 1, "array.next");

  // Check whether we've gotten to the end of the array and, if so,
  // exit the loop.
  llvm::Value *IsEnd = Builder.CreateICmpEQ(NextPtr, EndPtr, "array.atend");
  Builder.CreateCondBr(IsEnd, ContBB, LoopBB);
  CurPtrPhi->addIncoming(NextPtr, Builder.GetInsertBlock());

  EmitBlock(ContBB);
}

static void EmitNewInitializer(CodeGenFunction &CGF, const CXXNewExpr *E,
                               QualType ElementType, llvm::Type *ElementTy,
                               Address NewPtr, llvm::Value *NumElements,
                               llvm::Value *AllocSizeWithoutCookie) {
  ApplyDebugLocation DL(CGF, E);
  if (E->isArray())
    CGF.EmitNewArrayInitializer(E, ElementType, ElementTy, NewPtr, NumElements,
                                AllocSizeWithoutCookie);
  else if (const Expr *Init = E->getInitializer())
    StoreAnyExprIntoOneUnit(CGF, Init, E->getAllocatedType(), NewPtr,
                            AggValueSlot::DoesNotOverlap);
}

/// Emit a call to an operator new or operator delete function, as implicitly
/// created by new-expressions and delete-expressions.
static RValue EmitNewDeleteCall(CodeGenFunction &CGF,
                                const FunctionDecl *CalleeDecl,
                                const FunctionProtoType *CalleeType,
                                const CallArgList &Args) {
  llvm::CallBase *CallOrInvoke;
  llvm::Constant *CalleePtr = CGF.CGM.GetAddrOfFunction(CalleeDecl);
  CGCallee Callee = CGCallee::forDirect(CalleePtr, GlobalDecl(CalleeDecl));
  RValue RV =
      CGF.EmitCall(CGF.CGM.getTypes().arrangeFreeFunctionCall(
                       Args, CalleeType, /*ChainCall=*/false),
                   Callee, ReturnValueSlot(), Args, &CallOrInvoke);

  /// C++1y [expr.new]p10:
  ///   [In a new-expression,] an implementation is allowed to omit a call
  ///   to a replaceable global allocation function.
  ///
  /// We model such elidable calls with the 'builtin' attribute.
  llvm::Function *Fn = dyn_cast<llvm::Function>(CalleePtr);
  if (CalleeDecl->isReplaceableGlobalAllocationFunction() &&
      Fn && Fn->hasFnAttribute(llvm::Attribute::NoBuiltin)) {
    CallOrInvoke->addFnAttr(llvm::Attribute::Builtin);
  }

  return RV;
}

RValue CodeGenFunction::EmitBuiltinNewDeleteCall(const FunctionProtoType *Type,
                                                 const CallExpr *TheCall,
                                                 bool IsDelete) {
  CallArgList Args;
  EmitCallArgs(Args, Type, TheCall->arguments());
  // Find the allocation or deallocation function that we're calling.
  ASTContext &Ctx = getContext();
  DeclarationName Name = Ctx.DeclarationNames
      .getCXXOperatorName(IsDelete ? OO_Delete : OO_New);

  for (auto *Decl : Ctx.getTranslationUnitDecl()->lookup(Name))
    if (auto *FD = dyn_cast<FunctionDecl>(Decl))
      if (Ctx.hasSameType(FD->getType(), QualType(Type, 0)))
        return EmitNewDeleteCall(*this, FD, Type, Args);
  llvm_unreachable("predeclared global operator new/delete is missing");
}

namespace {
/// The parameters to pass to a usual operator delete.
struct UsualDeleteParams {
  bool DestroyingDelete = false;
  bool Size = false;
  bool Alignment = false;
};
}

static UsualDeleteParams getUsualDeleteParams(const FunctionDecl *FD) {
  UsualDeleteParams Params;

  const FunctionProtoType *FPT = FD->getType()->castAs<FunctionProtoType>();
  auto AI = FPT->param_type_begin(), AE = FPT->param_type_end();

  // The first argument is always a void*.
  ++AI;

  // The next parameter may be a std::destroying_delete_t.
  if (FD->isDestroyingOperatorDelete()) {
    Params.DestroyingDelete = true;
    assert(AI != AE);
    ++AI;
  }

  // Figure out what other parameters we should be implicitly passing.
  if (AI != AE && (*AI)->isIntegerType()) {
    Params.Size = true;
    ++AI;
  }

  if (AI != AE && (*AI)->isAlignValT()) {
    Params.Alignment = true;
    ++AI;
  }

  assert(AI == AE && "unexpected usual deallocation function parameter");
  return Params;
}

namespace {
  /// A cleanup to call the given 'operator delete' function upon abnormal
  /// exit from a new expression. Templated on a traits type that deals with
  /// ensuring that the arguments dominate the cleanup if necessary.
  template<typename Traits>
  class CallDeleteDuringNew final : public EHScopeStack::Cleanup {
    /// Type used to hold llvm::Value*s.
    typedef typename Traits::ValueTy ValueTy;
    /// Type used to hold RValues.
    typedef typename Traits::RValueTy RValueTy;
    struct PlacementArg {
      RValueTy ArgValue;
      QualType ArgType;
    };

    unsigned NumPlacementArgs : 31;
    LLVM_PREFERRED_TYPE(bool)
    unsigned PassAlignmentToPlacementDelete : 1;
    const FunctionDecl *OperatorDelete;
    ValueTy Ptr;
    ValueTy AllocSize;
    CharUnits AllocAlign;

    PlacementArg *getPlacementArgs() {
      return reinterpret_cast<PlacementArg *>(this + 1);
    }

  public:
    static size_t getExtraSize(size_t NumPlacementArgs) {
      return NumPlacementArgs * sizeof(PlacementArg);
    }

    CallDeleteDuringNew(size_t NumPlacementArgs,
                        const FunctionDecl *OperatorDelete, ValueTy Ptr,
                        ValueTy AllocSize, bool PassAlignmentToPlacementDelete,
                        CharUnits AllocAlign)
      : NumPlacementArgs(NumPlacementArgs),
        PassAlignmentToPlacementDelete(PassAlignmentToPlacementDelete),
        OperatorDelete(OperatorDelete), Ptr(Ptr), AllocSize(AllocSize),
        AllocAlign(AllocAlign) {}

    void setPlacementArg(unsigned I, RValueTy Arg, QualType Type) {
      assert(I < NumPlacementArgs && "index out of range");
      getPlacementArgs()[I] = {Arg, Type};
    }

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      const auto *FPT = OperatorDelete->getType()->castAs<FunctionProtoType>();
      CallArgList DeleteArgs;

      // The first argument is always a void* (or C* for a destroying operator
      // delete for class type C).
      DeleteArgs.add(Traits::get(CGF, Ptr), FPT->getParamType(0));

      // Figure out what other parameters we should be implicitly passing.
      UsualDeleteParams Params;
      if (NumPlacementArgs) {
        // A placement deallocation function is implicitly passed an alignment
        // if the placement allocation function was, but is never passed a size.
        Params.Alignment = PassAlignmentToPlacementDelete;
      } else {
        // For a non-placement new-expression, 'operator delete' can take a
        // size and/or an alignment if it has the right parameters.
        Params = getUsualDeleteParams(OperatorDelete);
      }

      assert(!Params.DestroyingDelete &&
             "should not call destroying delete in a new-expression");

      // The second argument can be a std::size_t (for non-placement delete).
      if (Params.Size)
        DeleteArgs.add(Traits::get(CGF, AllocSize),
                       CGF.getContext().getSizeType());

      // The next (second or third) argument can be a std::align_val_t, which
      // is an enum whose underlying type is std::size_t.
      // FIXME: Use the right type as the parameter type. Note that in a call
      // to operator delete(size_t, ...), we may not have it available.
      if (Params.Alignment)
        DeleteArgs.add(RValue::get(llvm::ConstantInt::get(
                           CGF.SizeTy, AllocAlign.getQuantity())),
                       CGF.getContext().getSizeType());

      // Pass the rest of the arguments, which must match exactly.
      for (unsigned I = 0; I != NumPlacementArgs; ++I) {
        auto Arg = getPlacementArgs()[I];
        DeleteArgs.add(Traits::get(CGF, Arg.ArgValue), Arg.ArgType);
      }

      // Call 'operator delete'.
      EmitNewDeleteCall(CGF, OperatorDelete, FPT, DeleteArgs);
    }
  };
}

/// Enter a cleanup to call 'operator delete' if the initializer in a
/// new-expression throws.
static void EnterNewDeleteCleanup(CodeGenFunction &CGF,
                                  const CXXNewExpr *E,
                                  Address NewPtr,
                                  llvm::Value *AllocSize,
                                  CharUnits AllocAlign,
                                  const CallArgList &NewArgs) {
  unsigned NumNonPlacementArgs = E->passAlignment() ? 2 : 1;

  // If we're not inside a conditional branch, then the cleanup will
  // dominate and we can do the easier (and more efficient) thing.
  if (!CGF.isInConditionalBranch()) {
    struct DirectCleanupTraits {
      typedef llvm::Value *ValueTy;
      typedef RValue RValueTy;
      static RValue get(CodeGenFunction &, ValueTy V) { return RValue::get(V); }
      static RValue get(CodeGenFunction &, RValueTy V) { return V; }
    };

    typedef CallDeleteDuringNew<DirectCleanupTraits> DirectCleanup;

    DirectCleanup *Cleanup = CGF.EHStack.pushCleanupWithExtra<DirectCleanup>(
        EHCleanup, E->getNumPlacementArgs(), E->getOperatorDelete(),
        NewPtr.emitRawPointer(CGF), AllocSize, E->passAlignment(), AllocAlign);
    for (unsigned I = 0, N = E->getNumPlacementArgs(); I != N; ++I) {
      auto &Arg = NewArgs[I + NumNonPlacementArgs];
      Cleanup->setPlacementArg(I, Arg.getRValue(CGF), Arg.Ty);
    }

    return;
  }

  // Otherwise, we need to save all this stuff.
  DominatingValue<RValue>::saved_type SavedNewPtr =
      DominatingValue<RValue>::save(CGF, RValue::get(NewPtr, CGF));
  DominatingValue<RValue>::saved_type SavedAllocSize =
    DominatingValue<RValue>::save(CGF, RValue::get(AllocSize));

  struct ConditionalCleanupTraits {
    typedef DominatingValue<RValue>::saved_type ValueTy;
    typedef DominatingValue<RValue>::saved_type RValueTy;
    static RValue get(CodeGenFunction &CGF, ValueTy V) {
      return V.restore(CGF);
    }
  };
  typedef CallDeleteDuringNew<ConditionalCleanupTraits> ConditionalCleanup;

  ConditionalCleanup *Cleanup = CGF.EHStack
    .pushCleanupWithExtra<ConditionalCleanup>(EHCleanup,
                                              E->getNumPlacementArgs(),
                                              E->getOperatorDelete(),
                                              SavedNewPtr,
                                              SavedAllocSize,
                                              E->passAlignment(),
                                              AllocAlign);
  for (unsigned I = 0, N = E->getNumPlacementArgs(); I != N; ++I) {
    auto &Arg = NewArgs[I + NumNonPlacementArgs];
    Cleanup->setPlacementArg(
        I, DominatingValue<RValue>::save(CGF, Arg.getRValue(CGF)), Arg.Ty);
  }

  CGF.initFullExprCleanup();
}

llvm::Value *CodeGenFunction::EmitCXXNewExpr(const CXXNewExpr *E) {
  // The element type being allocated.
  QualType allocType = getContext().getBaseElementType(E->getAllocatedType());

  // 1. Build a call to the allocation function.
  FunctionDecl *allocator = E->getOperatorNew();

  // If there is a brace-initializer or C++20 parenthesized initializer, cannot
  // allocate fewer elements than inits.
  unsigned minElements = 0;
  if (E->isArray() && E->hasInitializer()) {
    const Expr *Init = E->getInitializer();
    const InitListExpr *ILE = dyn_cast<InitListExpr>(Init);
    const CXXParenListInitExpr *CPLIE = dyn_cast<CXXParenListInitExpr>(Init);
    const Expr *IgnoreParen = Init->IgnoreParenImpCasts();
    if ((ILE && ILE->isStringLiteralInit()) ||
        isa<StringLiteral>(IgnoreParen) || isa<ObjCEncodeExpr>(IgnoreParen)) {
      minElements =
          cast<ConstantArrayType>(Init->getType()->getAsArrayTypeUnsafe())
              ->getZExtSize();
    } else if (ILE || CPLIE) {
      minElements = ILE ? ILE->getNumInits() : CPLIE->getInitExprs().size();
    }
  }

  llvm::Value *numElements = nullptr;
  llvm::Value *allocSizeWithoutCookie = nullptr;
  llvm::Value *allocSize =
    EmitCXXNewAllocSize(*this, E, minElements, numElements,
                        allocSizeWithoutCookie);
  CharUnits allocAlign = getContext().getTypeAlignInChars(allocType);

  // Emit the allocation call.  If the allocator is a global placement
  // operator, just "inline" it directly.
  Address allocation = Address::invalid();
  CallArgList allocatorArgs;
  if (allocator->isReservedGlobalPlacementOperator()) {
    assert(E->getNumPlacementArgs() == 1);
    const Expr *arg = *E->placement_arguments().begin();

    LValueBaseInfo BaseInfo;
    allocation = EmitPointerWithAlignment(arg, &BaseInfo);

    // The pointer expression will, in many cases, be an opaque void*.
    // In these cases, discard the computed alignment and use the
    // formal alignment of the allocated type.
    if (BaseInfo.getAlignmentSource() != AlignmentSource::Decl)
      allocation.setAlignment(allocAlign);

    // Set up allocatorArgs for the call to operator delete if it's not
    // the reserved global operator.
    if (E->getOperatorDelete() &&
        !E->getOperatorDelete()->isReservedGlobalPlacementOperator()) {
      allocatorArgs.add(RValue::get(allocSize), getContext().getSizeType());
      allocatorArgs.add(RValue::get(allocation, *this), arg->getType());
    }

  } else {
    const FunctionProtoType *allocatorType =
      allocator->getType()->castAs<FunctionProtoType>();
    unsigned ParamsToSkip = 0;

    // The allocation size is the first argument.
    QualType sizeType = getContext().getSizeType();
    allocatorArgs.add(RValue::get(allocSize), sizeType);
    ++ParamsToSkip;

    if (allocSize != allocSizeWithoutCookie) {
      CharUnits cookieAlign = getSizeAlign(); // FIXME: Ask the ABI.
      allocAlign = std::max(allocAlign, cookieAlign);
    }

    // The allocation alignment may be passed as the second argument.
    if (E->passAlignment()) {
      QualType AlignValT = sizeType;
      if (allocatorType->getNumParams() > 1) {
        AlignValT = allocatorType->getParamType(1);
        assert(getContext().hasSameUnqualifiedType(
                   AlignValT->castAs<EnumType>()->getDecl()->getIntegerType(),
                   sizeType) &&
               "wrong type for alignment parameter");
        ++ParamsToSkip;
      } else {
        // Corner case, passing alignment to 'operator new(size_t, ...)'.
        assert(allocator->isVariadic() && "can't pass alignment to allocator");
      }
      allocatorArgs.add(
          RValue::get(llvm::ConstantInt::get(SizeTy, allocAlign.getQuantity())),
          AlignValT);
    }

    // FIXME: Why do we not pass a CalleeDecl here?
    EmitCallArgs(allocatorArgs, allocatorType, E->placement_arguments(),
                 /*AC*/AbstractCallee(), /*ParamsToSkip*/ParamsToSkip);

    RValue RV =
      EmitNewDeleteCall(*this, allocator, allocatorType, allocatorArgs);

    // Set !heapallocsite metadata on the call to operator new.
    if (getDebugInfo())
      if (auto *newCall = dyn_cast<llvm::CallBase>(RV.getScalarVal()))
        getDebugInfo()->addHeapAllocSiteMetadata(newCall, allocType,
                                                 E->getExprLoc());

    // If this was a call to a global replaceable allocation function that does
    // not take an alignment argument, the allocator is known to produce
    // storage that's suitably aligned for any object that fits, up to a known
    // threshold. Otherwise assume it's suitably aligned for the allocated type.
    CharUnits allocationAlign = allocAlign;
    if (!E->passAlignment() &&
        allocator->isReplaceableGlobalAllocationFunction()) {
      unsigned AllocatorAlign = llvm::bit_floor(std::min<uint64_t>(
          Target.getNewAlign(), getContext().getTypeSize(allocType)));
      allocationAlign = std::max(
          allocationAlign, getContext().toCharUnitsFromBits(AllocatorAlign));
    }

    allocation = Address(RV.getScalarVal(), Int8Ty, allocationAlign);
  }

  // Emit a null check on the allocation result if the allocation
  // function is allowed to return null (because it has a non-throwing
  // exception spec or is the reserved placement new) and we have an
  // interesting initializer will be running sanitizers on the initialization.
  bool nullCheck = E->shouldNullCheckAllocation() &&
                   (!allocType.isPODType(getContext()) || E->hasInitializer() ||
                    sanitizePerformTypeCheck());

  llvm::BasicBlock *nullCheckBB = nullptr;
  llvm::BasicBlock *contBB = nullptr;

  // The null-check means that the initializer is conditionally
  // evaluated.
  ConditionalEvaluation conditional(*this);

  if (nullCheck) {
    conditional.begin(*this);

    nullCheckBB = Builder.GetInsertBlock();
    llvm::BasicBlock *notNullBB = createBasicBlock("new.notnull");
    contBB = createBasicBlock("new.cont");

    llvm::Value *isNull = Builder.CreateIsNull(allocation, "new.isnull");
    Builder.CreateCondBr(isNull, contBB, notNullBB);
    EmitBlock(notNullBB);
  }

  // If there's an operator delete, enter a cleanup to call it if an
  // exception is thrown.
  EHScopeStack::stable_iterator operatorDeleteCleanup;
  llvm::Instruction *cleanupDominator = nullptr;
  if (E->getOperatorDelete() &&
      !E->getOperatorDelete()->isReservedGlobalPlacementOperator()) {
    EnterNewDeleteCleanup(*this, E, allocation, allocSize, allocAlign,
                          allocatorArgs);
    operatorDeleteCleanup = EHStack.stable_begin();
    cleanupDominator = Builder.CreateUnreachable();
  }

  assert((allocSize == allocSizeWithoutCookie) ==
         CalculateCookiePadding(*this, E).isZero());
  if (allocSize != allocSizeWithoutCookie) {
    assert(E->isArray());
    allocation = CGM.getCXXABI().InitializeArrayCookie(*this, allocation,
                                                       numElements,
                                                       E, allocType);
  }

  llvm::Type *elementTy = ConvertTypeForMem(allocType);
  Address result = allocation.withElementType(elementTy);

  // Passing pointer through launder.invariant.group to avoid propagation of
  // vptrs information which may be included in previous type.
  // To not break LTO with different optimizations levels, we do it regardless
  // of optimization level.
  if (CGM.getCodeGenOpts().StrictVTablePointers &&
      allocator->isReservedGlobalPlacementOperator())
    result = Builder.CreateLaunderInvariantGroup(result);

  // Emit sanitizer checks for pointer value now, so that in the case of an
  // array it was checked only once and not at each constructor call. We may
  // have already checked that the pointer is non-null.
  // FIXME: If we have an array cookie and a potentially-throwing allocator,
  // we'll null check the wrong pointer here.
  SanitizerSet SkippedChecks;
  SkippedChecks.set(SanitizerKind::Null, nullCheck);
  EmitTypeCheck(CodeGenFunction::TCK_ConstructorCall,
                E->getAllocatedTypeSourceInfo()->getTypeLoc().getBeginLoc(),
                result, allocType, result.getAlignment(), SkippedChecks,
                numElements);

  EmitNewInitializer(*this, E, allocType, elementTy, result, numElements,
                     allocSizeWithoutCookie);
  llvm::Value *resultPtr = result.emitRawPointer(*this);
  if (E->isArray()) {
    // NewPtr is a pointer to the base element type.  If we're
    // allocating an array of arrays, we'll need to cast back to the
    // array pointer type.
    llvm::Type *resultType = ConvertTypeForMem(E->getType());
    if (resultPtr->getType() != resultType)
      resultPtr = Builder.CreateBitCast(resultPtr, resultType);
  }

  // Deactivate the 'operator delete' cleanup if we finished
  // initialization.
  if (operatorDeleteCleanup.isValid()) {
    DeactivateCleanupBlock(operatorDeleteCleanup, cleanupDominator);
    cleanupDominator->eraseFromParent();
  }

  if (nullCheck) {
    conditional.end(*this);

    llvm::BasicBlock *notNullBB = Builder.GetInsertBlock();
    EmitBlock(contBB);

    llvm::PHINode *PHI = Builder.CreatePHI(resultPtr->getType(), 2);
    PHI->addIncoming(resultPtr, notNullBB);
    PHI->addIncoming(llvm::Constant::getNullValue(resultPtr->getType()),
                     nullCheckBB);

    resultPtr = PHI;
  }

  return resultPtr;
}

void CodeGenFunction::EmitDeleteCall(const FunctionDecl *DeleteFD,
                                     llvm::Value *Ptr, QualType DeleteTy,
                                     llvm::Value *NumElements,
                                     CharUnits CookieSize) {
  assert((!NumElements && CookieSize.isZero()) ||
         DeleteFD->getOverloadedOperator() == OO_Array_Delete);

  const auto *DeleteFTy = DeleteFD->getType()->castAs<FunctionProtoType>();
  CallArgList DeleteArgs;

  auto Params = getUsualDeleteParams(DeleteFD);
  auto ParamTypeIt = DeleteFTy->param_type_begin();

  // Pass the pointer itself.
  QualType ArgTy = *ParamTypeIt++;
  llvm::Value *DeletePtr = Builder.CreateBitCast(Ptr, ConvertType(ArgTy));
  DeleteArgs.add(RValue::get(DeletePtr), ArgTy);

  // Pass the std::destroying_delete tag if present.
  llvm::AllocaInst *DestroyingDeleteTag = nullptr;
  if (Params.DestroyingDelete) {
    QualType DDTag = *ParamTypeIt++;
    llvm::Type *Ty = getTypes().ConvertType(DDTag);
    CharUnits Align = CGM.getNaturalTypeAlignment(DDTag);
    DestroyingDeleteTag = CreateTempAlloca(Ty, "destroying.delete.tag");
    DestroyingDeleteTag->setAlignment(Align.getAsAlign());
    DeleteArgs.add(
        RValue::getAggregate(Address(DestroyingDeleteTag, Ty, Align)), DDTag);
  }

  // Pass the size if the delete function has a size_t parameter.
  if (Params.Size) {
    QualType SizeType = *ParamTypeIt++;
    CharUnits DeleteTypeSize = getContext().getTypeSizeInChars(DeleteTy);
    llvm::Value *Size = llvm::ConstantInt::get(ConvertType(SizeType),
                                               DeleteTypeSize.getQuantity());

    // For array new, multiply by the number of elements.
    if (NumElements)
      Size = Builder.CreateMul(Size, NumElements);

    // If there is a cookie, add the cookie size.
    if (!CookieSize.isZero())
      Size = Builder.CreateAdd(
          Size, llvm::ConstantInt::get(SizeTy, CookieSize.getQuantity()));

    DeleteArgs.add(RValue::get(Size), SizeType);
  }

  // Pass the alignment if the delete function has an align_val_t parameter.
  if (Params.Alignment) {
    QualType AlignValType = *ParamTypeIt++;
    CharUnits DeleteTypeAlign =
        getContext().toCharUnitsFromBits(getContext().getTypeAlignIfKnown(
            DeleteTy, true /* NeedsPreferredAlignment */));
    llvm::Value *Align = llvm::ConstantInt::get(ConvertType(AlignValType),
                                                DeleteTypeAlign.getQuantity());
    DeleteArgs.add(RValue::get(Align), AlignValType);
  }

  assert(ParamTypeIt == DeleteFTy->param_type_end() &&
         "unknown parameter to usual delete function");

  // Emit the call to delete.
  EmitNewDeleteCall(*this, DeleteFD, DeleteFTy, DeleteArgs);

  // If call argument lowering didn't use the destroying_delete_t alloca,
  // remove it again.
  if (DestroyingDeleteTag && DestroyingDeleteTag->use_empty())
    DestroyingDeleteTag->eraseFromParent();
}

namespace {
  /// Calls the given 'operator delete' on a single object.
  struct CallObjectDelete final : EHScopeStack::Cleanup {
    llvm::Value *Ptr;
    const FunctionDecl *OperatorDelete;
    QualType ElementType;

    CallObjectDelete(llvm::Value *Ptr,
                     const FunctionDecl *OperatorDelete,
                     QualType ElementType)
      : Ptr(Ptr), OperatorDelete(OperatorDelete), ElementType(ElementType) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitDeleteCall(OperatorDelete, Ptr, ElementType);
    }
  };
}

void
CodeGenFunction::pushCallObjectDeleteCleanup(const FunctionDecl *OperatorDelete,
                                             llvm::Value *CompletePtr,
                                             QualType ElementType) {
  EHStack.pushCleanup<CallObjectDelete>(NormalAndEHCleanup, CompletePtr,
                                        OperatorDelete, ElementType);
}

/// Emit the code for deleting a single object with a destroying operator
/// delete. If the element type has a non-virtual destructor, Ptr has already
/// been converted to the type of the parameter of 'operator delete'. Otherwise
/// Ptr points to an object of the static type.
static void EmitDestroyingObjectDelete(CodeGenFunction &CGF,
                                       const CXXDeleteExpr *DE, Address Ptr,
                                       QualType ElementType) {
  auto *Dtor = ElementType->getAsCXXRecordDecl()->getDestructor();
  if (Dtor && Dtor->isVirtual())
    CGF.CGM.getCXXABI().emitVirtualObjectDelete(CGF, DE, Ptr, ElementType,
                                                Dtor);
  else
    CGF.EmitDeleteCall(DE->getOperatorDelete(), Ptr.emitRawPointer(CGF),
                       ElementType);
}

/// Emit the code for deleting a single object.
/// \return \c true if we started emitting UnconditionalDeleteBlock, \c false
/// if not.
static bool EmitObjectDelete(CodeGenFunction &CGF,
                             const CXXDeleteExpr *DE,
                             Address Ptr,
                             QualType ElementType,
                             llvm::BasicBlock *UnconditionalDeleteBlock) {
  // C++11 [expr.delete]p3:
  //   If the static type of the object to be deleted is different from its
  //   dynamic type, the static type shall be a base class of the dynamic type
  //   of the object to be deleted and the static type shall have a virtual
  //   destructor or the behavior is undefined.
  CGF.EmitTypeCheck(CodeGenFunction::TCK_MemberCall, DE->getExprLoc(), Ptr,
                    ElementType);

  const FunctionDecl *OperatorDelete = DE->getOperatorDelete();
  assert(!OperatorDelete->isDestroyingOperatorDelete());

  // Find the destructor for the type, if applicable.  If the
  // destructor is virtual, we'll just emit the vcall and return.
  const CXXDestructorDecl *Dtor = nullptr;
  if (const RecordType *RT = ElementType->getAs<RecordType>()) {
    CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
    if (RD->hasDefinition() && !RD->hasTrivialDestructor()) {
      Dtor = RD->getDestructor();

      if (Dtor->isVirtual()) {
        bool UseVirtualCall = true;
        const Expr *Base = DE->getArgument();
        if (auto *DevirtualizedDtor =
                dyn_cast_or_null<const CXXDestructorDecl>(
                    Dtor->getDevirtualizedMethod(
                        Base, CGF.CGM.getLangOpts().AppleKext))) {
          UseVirtualCall = false;
          const CXXRecordDecl *DevirtualizedClass =
              DevirtualizedDtor->getParent();
          if (declaresSameEntity(getCXXRecord(Base), DevirtualizedClass)) {
            // Devirtualized to the class of the base type (the type of the
            // whole expression).
            Dtor = DevirtualizedDtor;
          } else {
            // Devirtualized to some other type. Would need to cast the this
            // pointer to that type but we don't have support for that yet, so
            // do a virtual call. FIXME: handle the case where it is
            // devirtualized to the derived type (the type of the inner
            // expression) as in EmitCXXMemberOrOperatorMemberCallExpr.
            UseVirtualCall = true;
          }
        }
        if (UseVirtualCall) {
          CGF.CGM.getCXXABI().emitVirtualObjectDelete(CGF, DE, Ptr, ElementType,
                                                      Dtor);
          return false;
        }
      }
    }
  }

  // Make sure that we call delete even if the dtor throws.
  // This doesn't have to a conditional cleanup because we're going
  // to pop it off in a second.
  CGF.EHStack.pushCleanup<CallObjectDelete>(
      NormalAndEHCleanup, Ptr.emitRawPointer(CGF), OperatorDelete, ElementType);

  if (Dtor)
    CGF.EmitCXXDestructorCall(Dtor, Dtor_Complete,
                              /*ForVirtualBase=*/false,
                              /*Delegating=*/false,
                              Ptr, ElementType);
  else if (auto Lifetime = ElementType.getObjCLifetime()) {
    switch (Lifetime) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Autoreleasing:
      break;

    case Qualifiers::OCL_Strong:
      CGF.EmitARCDestroyStrong(Ptr, ARCPreciseLifetime);
      break;

    case Qualifiers::OCL_Weak:
      CGF.EmitARCDestroyWeak(Ptr);
      break;
    }
  }

  // When optimizing for size, call 'operator delete' unconditionally.
  if (CGF.CGM.getCodeGenOpts().OptimizeSize > 1) {
    CGF.EmitBlock(UnconditionalDeleteBlock);
    CGF.PopCleanupBlock();
    return true;
  }

  CGF.PopCleanupBlock();
  return false;
}

namespace {
  /// Calls the given 'operator delete' on an array of objects.
  struct CallArrayDelete final : EHScopeStack::Cleanup {
    llvm::Value *Ptr;
    const FunctionDecl *OperatorDelete;
    llvm::Value *NumElements;
    QualType ElementType;
    CharUnits CookieSize;

    CallArrayDelete(llvm::Value *Ptr,
                    const FunctionDecl *OperatorDelete,
                    llvm::Value *NumElements,
                    QualType ElementType,
                    CharUnits CookieSize)
      : Ptr(Ptr), OperatorDelete(OperatorDelete), NumElements(NumElements),
        ElementType(ElementType), CookieSize(CookieSize) {}

    void Emit(CodeGenFunction &CGF, Flags flags) override {
      CGF.EmitDeleteCall(OperatorDelete, Ptr, ElementType, NumElements,
                         CookieSize);
    }
  };
}

/// Emit the code for deleting an array of objects.
static void EmitArrayDelete(CodeGenFunction &CGF,
                            const CXXDeleteExpr *E,
                            Address deletedPtr,
                            QualType elementType) {
  llvm::Value *numElements = nullptr;
  llvm::Value *allocatedPtr = nullptr;
  CharUnits cookieSize;
  CGF.CGM.getCXXABI().ReadArrayCookie(CGF, deletedPtr, E, elementType,
                                      numElements, allocatedPtr, cookieSize);

  assert(allocatedPtr && "ReadArrayCookie didn't set allocated pointer");

  // Make sure that we call delete even if one of the dtors throws.
  const FunctionDecl *operatorDelete = E->getOperatorDelete();
  CGF.EHStack.pushCleanup<CallArrayDelete>(NormalAndEHCleanup,
                                           allocatedPtr, operatorDelete,
                                           numElements, elementType,
                                           cookieSize);

  // Destroy the elements.
  if (QualType::DestructionKind dtorKind = elementType.isDestructedType()) {
    assert(numElements && "no element count for a type with a destructor!");

    CharUnits elementSize = CGF.getContext().getTypeSizeInChars(elementType);
    CharUnits elementAlign =
      deletedPtr.getAlignment().alignmentOfArrayElement(elementSize);

    llvm::Value *arrayBegin = deletedPtr.emitRawPointer(CGF);
    llvm::Value *arrayEnd = CGF.Builder.CreateInBoundsGEP(
      deletedPtr.getElementType(), arrayBegin, numElements, "delete.end");

    // Note that it is legal to allocate a zero-length array, and we
    // can never fold the check away because the length should always
    // come from a cookie.
    CGF.emitArrayDestroy(arrayBegin, arrayEnd, elementType, elementAlign,
                         CGF.getDestroyer(dtorKind),
                         /*checkZeroLength*/ true,
                         CGF.needsEHCleanup(dtorKind));
  }

  // Pop the cleanup block.
  CGF.PopCleanupBlock();
}

void CodeGenFunction::EmitCXXDeleteExpr(const CXXDeleteExpr *E) {
  const Expr *Arg = E->getArgument();
  Address Ptr = EmitPointerWithAlignment(Arg);

  // Null check the pointer.
  //
  // We could avoid this null check if we can determine that the object
  // destruction is trivial and doesn't require an array cookie; we can
  // unconditionally perform the operator delete call in that case. For now, we
  // assume that deleted pointers are null rarely enough that it's better to
  // keep the branch. This might be worth revisiting for a -O0 code size win.
  llvm::BasicBlock *DeleteNotNull = createBasicBlock("delete.notnull");
  llvm::BasicBlock *DeleteEnd = createBasicBlock("delete.end");

  llvm::Value *IsNull = Builder.CreateIsNull(Ptr, "isnull");

  Builder.CreateCondBr(IsNull, DeleteEnd, DeleteNotNull);
  EmitBlock(DeleteNotNull);
  Ptr.setKnownNonNull();

  QualType DeleteTy = E->getDestroyedType();

  // A destroying operator delete overrides the entire operation of the
  // delete expression.
  if (E->getOperatorDelete()->isDestroyingOperatorDelete()) {
    EmitDestroyingObjectDelete(*this, E, Ptr, DeleteTy);
    EmitBlock(DeleteEnd);
    return;
  }

  // We might be deleting a pointer to array.  If so, GEP down to the
  // first non-array element.
  // (this assumes that A(*)[3][7] is converted to [3 x [7 x %A]]*)
  if (DeleteTy->isConstantArrayType()) {
    llvm::Value *Zero = Builder.getInt32(0);
    SmallVector<llvm::Value*,8> GEP;

    GEP.push_back(Zero); // point at the outermost array

    // For each layer of array type we're pointing at:
    while (const ConstantArrayType *Arr
             = getContext().getAsConstantArrayType(DeleteTy)) {
      // 1. Unpeel the array type.
      DeleteTy = Arr->getElementType();

      // 2. GEP to the first element of the array.
      GEP.push_back(Zero);
    }

    Ptr = Builder.CreateInBoundsGEP(Ptr, GEP, ConvertTypeForMem(DeleteTy),
                                    Ptr.getAlignment(), "del.first");
  }

  assert(ConvertTypeForMem(DeleteTy) == Ptr.getElementType());

  if (E->isArrayForm()) {
    EmitArrayDelete(*this, E, Ptr, DeleteTy);
    EmitBlock(DeleteEnd);
  } else {
    if (!EmitObjectDelete(*this, E, Ptr, DeleteTy, DeleteEnd))
      EmitBlock(DeleteEnd);
  }
}

static llvm::Value *EmitTypeidFromVTable(CodeGenFunction &CGF, const Expr *E,
                                         llvm::Type *StdTypeInfoPtrTy,
                                         bool HasNullCheck) {
  // Get the vtable pointer.
  Address ThisPtr = CGF.EmitLValue(E).getAddress();

  QualType SrcRecordTy = E->getType();

  // C++ [class.cdtor]p4:
  //   If the operand of typeid refers to the object under construction or
  //   destruction and the static type of the operand is neither the constructor
  //   or destructor’s class nor one of its bases, the behavior is undefined.
  CGF.EmitTypeCheck(CodeGenFunction::TCK_DynamicOperation, E->getExprLoc(),
                    ThisPtr, SrcRecordTy);

  // Whether we need an explicit null pointer check. For example, with the
  // Microsoft ABI, if this is a call to __RTtypeid, the null pointer check and
  // exception throw is inside the __RTtypeid(nullptr) call
  if (HasNullCheck &&
      CGF.CGM.getCXXABI().shouldTypeidBeNullChecked(SrcRecordTy)) {
    llvm::BasicBlock *BadTypeidBlock =
        CGF.createBasicBlock("typeid.bad_typeid");
    llvm::BasicBlock *EndBlock = CGF.createBasicBlock("typeid.end");

    llvm::Value *IsNull = CGF.Builder.CreateIsNull(ThisPtr);
    CGF.Builder.CreateCondBr(IsNull, BadTypeidBlock, EndBlock);

    CGF.EmitBlock(BadTypeidBlock);
    CGF.CGM.getCXXABI().EmitBadTypeidCall(CGF);
    CGF.EmitBlock(EndBlock);
  }

  return CGF.CGM.getCXXABI().EmitTypeid(CGF, SrcRecordTy, ThisPtr,
                                        StdTypeInfoPtrTy);
}

llvm::Value *CodeGenFunction::EmitCXXTypeidExpr(const CXXTypeidExpr *E) {
  // Ideally, we would like to use GlobalsInt8PtrTy here, however, we cannot,
  // primarily because the result of applying typeid is a value of type
  // type_info, which is declared & defined by the standard library
  // implementation and expects to operate on the generic (default) AS.
  // https://reviews.llvm.org/D157452 has more context, and a possible solution.
  llvm::Type *PtrTy = Int8PtrTy;
  LangAS GlobAS = CGM.GetGlobalVarAddressSpace(nullptr);

  auto MaybeASCast = [=](auto &&TypeInfo) {
    if (GlobAS == LangAS::Default)
      return TypeInfo;
    return getTargetHooks().performAddrSpaceCast(CGM,TypeInfo, GlobAS,
                                                 LangAS::Default, PtrTy);
  };

  if (E->isTypeOperand()) {
    llvm::Constant *TypeInfo =
        CGM.GetAddrOfRTTIDescriptor(E->getTypeOperand(getContext()));
    return MaybeASCast(TypeInfo);
  }

  // C++ [expr.typeid]p2:
  //   When typeid is applied to a glvalue expression whose type is a
  //   polymorphic class type, the result refers to a std::type_info object
  //   representing the type of the most derived object (that is, the dynamic
  //   type) to which the glvalue refers.
  // If the operand is already most derived object, no need to look up vtable.
  if (E->isPotentiallyEvaluated() && !E->isMostDerived(getContext()))
    return EmitTypeidFromVTable(*this, E->getExprOperand(), PtrTy,
                                E->hasNullCheck());

  QualType OperandTy = E->getExprOperand()->getType();
  return MaybeASCast(CGM.GetAddrOfRTTIDescriptor(OperandTy));
}

static llvm::Value *EmitDynamicCastToNull(CodeGenFunction &CGF,
                                          QualType DestTy) {
  llvm::Type *DestLTy = CGF.ConvertType(DestTy);
  if (DestTy->isPointerType())
    return llvm::Constant::getNullValue(DestLTy);

  /// C++ [expr.dynamic.cast]p9:
  ///   A failed cast to reference type throws std::bad_cast
  if (!CGF.CGM.getCXXABI().EmitBadCastCall(CGF))
    return nullptr;

  CGF.Builder.ClearInsertionPoint();
  return llvm::PoisonValue::get(DestLTy);
}

llvm::Value *CodeGenFunction::EmitDynamicCast(Address ThisAddr,
                                              const CXXDynamicCastExpr *DCE) {
  CGM.EmitExplicitCastExprType(DCE, this);
  QualType DestTy = DCE->getTypeAsWritten();

  QualType SrcTy = DCE->getSubExpr()->getType();

  // C++ [expr.dynamic.cast]p7:
  //   If T is "pointer to cv void," then the result is a pointer to the most
  //   derived object pointed to by v.
  bool IsDynamicCastToVoid = DestTy->isVoidPointerType();
  QualType SrcRecordTy;
  QualType DestRecordTy;
  if (IsDynamicCastToVoid) {
    SrcRecordTy = SrcTy->getPointeeType();
    // No DestRecordTy.
  } else if (const PointerType *DestPTy = DestTy->getAs<PointerType>()) {
    SrcRecordTy = SrcTy->castAs<PointerType>()->getPointeeType();
    DestRecordTy = DestPTy->getPointeeType();
  } else {
    SrcRecordTy = SrcTy;
    DestRecordTy = DestTy->castAs<ReferenceType>()->getPointeeType();
  }

  // C++ [class.cdtor]p5:
  //   If the operand of the dynamic_cast refers to the object under
  //   construction or destruction and the static type of the operand is not a
  //   pointer to or object of the constructor or destructor’s own class or one
  //   of its bases, the dynamic_cast results in undefined behavior.
  EmitTypeCheck(TCK_DynamicOperation, DCE->getExprLoc(), ThisAddr, SrcRecordTy);

  if (DCE->isAlwaysNull()) {
    if (llvm::Value *T = EmitDynamicCastToNull(*this, DestTy)) {
      // Expression emission is expected to retain a valid insertion point.
      if (!Builder.GetInsertBlock())
        EmitBlock(createBasicBlock("dynamic_cast.unreachable"));
      return T;
    }
  }

  assert(SrcRecordTy->isRecordType() && "source type must be a record type!");

  // If the destination is effectively final, the cast succeeds if and only
  // if the dynamic type of the pointer is exactly the destination type.
  bool IsExact = !IsDynamicCastToVoid &&
                 CGM.getCodeGenOpts().OptimizationLevel > 0 &&
                 DestRecordTy->getAsCXXRecordDecl()->isEffectivelyFinal() &&
                 CGM.getCXXABI().shouldEmitExactDynamicCast(DestRecordTy);

  // C++ [expr.dynamic.cast]p4:
  //   If the value of v is a null pointer value in the pointer case, the result
  //   is the null pointer value of type T.
  bool ShouldNullCheckSrcValue =
      IsExact || CGM.getCXXABI().shouldDynamicCastCallBeNullChecked(
                     SrcTy->isPointerType(), SrcRecordTy);

  llvm::BasicBlock *CastNull = nullptr;
  llvm::BasicBlock *CastNotNull = nullptr;
  llvm::BasicBlock *CastEnd = createBasicBlock("dynamic_cast.end");

  if (ShouldNullCheckSrcValue) {
    CastNull = createBasicBlock("dynamic_cast.null");
    CastNotNull = createBasicBlock("dynamic_cast.notnull");

    llvm::Value *IsNull = Builder.CreateIsNull(ThisAddr);
    Builder.CreateCondBr(IsNull, CastNull, CastNotNull);
    EmitBlock(CastNotNull);
  }

  llvm::Value *Value;
  if (IsDynamicCastToVoid) {
    Value = CGM.getCXXABI().emitDynamicCastToVoid(*this, ThisAddr, SrcRecordTy);
  } else if (IsExact) {
    // If the destination type is effectively final, this pointer points to the
    // right type if and only if its vptr has the right value.
    Value = CGM.getCXXABI().emitExactDynamicCast(
        *this, ThisAddr, SrcRecordTy, DestTy, DestRecordTy, CastEnd, CastNull);
  } else {
    assert(DestRecordTy->isRecordType() &&
           "destination type must be a record type!");
    Value = CGM.getCXXABI().emitDynamicCastCall(*this, ThisAddr, SrcRecordTy,
                                                DestTy, DestRecordTy, CastEnd);
  }
  CastNotNull = Builder.GetInsertBlock();

  llvm::Value *NullValue = nullptr;
  if (ShouldNullCheckSrcValue) {
    EmitBranch(CastEnd);

    EmitBlock(CastNull);
    NullValue = EmitDynamicCastToNull(*this, DestTy);
    CastNull = Builder.GetInsertBlock();

    EmitBranch(CastEnd);
  }

  EmitBlock(CastEnd);

  if (CastNull) {
    llvm::PHINode *PHI = Builder.CreatePHI(Value->getType(), 2);
    PHI->addIncoming(Value, CastNotNull);
    PHI->addIncoming(NullValue, CastNull);

    Value = PHI;
  }

  return Value;
}
