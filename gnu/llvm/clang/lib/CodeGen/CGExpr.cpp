//===--- CGExpr.cpp - Emit LLVM Code from Expressions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit Expr nodes as LLVM code.
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "CGCUDARuntime.h"
#include "CGCXXABI.h"
#include "CGCall.h"
#include "CGCleanup.h"
#include "CGDebugInfo.h"
#include "CGObjCRuntime.h"
#include "CGOpenMPRuntime.h"
#include "CGRecordLayout.h"
#include "CodeGenFunction.h"
#include "CodeGenModule.h"
#include "ConstantEmitter.h"
#include "TargetInfo.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/CodeGenOptions.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/MatrixBuilder.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SaveAndRestore.h"
#include "llvm/Support/xxhash.h"
#include "llvm/Transforms/Utils/SanitizerStats.h"

#include <optional>
#include <string>

using namespace clang;
using namespace CodeGen;

// Experiment to make sanitizers easier to debug
static llvm::cl::opt<bool> ClSanitizeDebugDeoptimization(
    "ubsan-unique-traps", llvm::cl::Optional,
    llvm::cl::desc("Deoptimize traps for UBSAN so there is 1 trap per check."));

// TODO: Introduce frontend options to enabled per sanitizers, similar to
// `fsanitize-trap`.
static llvm::cl::opt<bool> ClSanitizeGuardChecks(
    "ubsan-guard-checks", llvm::cl::Optional,
    llvm::cl::desc("Guard UBSAN checks with `llvm.allow.ubsan.check()`."));

//===--------------------------------------------------------------------===//
//                        Miscellaneous Helper Methods
//===--------------------------------------------------------------------===//

/// CreateTempAlloca - This creates a alloca and inserts it into the entry
/// block.
RawAddress
CodeGenFunction::CreateTempAllocaWithoutCast(llvm::Type *Ty, CharUnits Align,
                                             const Twine &Name,
                                             llvm::Value *ArraySize) {
  auto Alloca = CreateTempAlloca(Ty, Name, ArraySize);
  Alloca->setAlignment(Align.getAsAlign());
  return RawAddress(Alloca, Ty, Align, KnownNonNull);
}

/// CreateTempAlloca - This creates a alloca and inserts it into the entry
/// block. The alloca is casted to default address space if necessary.
RawAddress CodeGenFunction::CreateTempAlloca(llvm::Type *Ty, CharUnits Align,
                                             const Twine &Name,
                                             llvm::Value *ArraySize,
                                             RawAddress *AllocaAddr) {
  auto Alloca = CreateTempAllocaWithoutCast(Ty, Align, Name, ArraySize);
  if (AllocaAddr)
    *AllocaAddr = Alloca;
  llvm::Value *V = Alloca.getPointer();
  // Alloca always returns a pointer in alloca address space, which may
  // be different from the type defined by the language. For example,
  // in C++ the auto variables are in the default address space. Therefore
  // cast alloca to the default address space when necessary.
  if (getASTAllocaAddressSpace() != LangAS::Default) {
    auto DestAddrSpace = getContext().getTargetAddressSpace(LangAS::Default);
    llvm::IRBuilderBase::InsertPointGuard IPG(Builder);
    // When ArraySize is nullptr, alloca is inserted at AllocaInsertPt,
    // otherwise alloca is inserted at the current insertion point of the
    // builder.
    if (!ArraySize)
      Builder.SetInsertPoint(getPostAllocaInsertPoint());
    V = getTargetHooks().performAddrSpaceCast(
        *this, V, getASTAllocaAddressSpace(), LangAS::Default,
        Ty->getPointerTo(DestAddrSpace), /*non-null*/ true);
  }

  return RawAddress(V, Ty, Align, KnownNonNull);
}

/// CreateTempAlloca - This creates an alloca and inserts it into the entry
/// block if \p ArraySize is nullptr, otherwise inserts it at the current
/// insertion point of the builder.
llvm::AllocaInst *CodeGenFunction::CreateTempAlloca(llvm::Type *Ty,
                                                    const Twine &Name,
                                                    llvm::Value *ArraySize) {
  llvm::AllocaInst *Alloca;
  if (ArraySize)
    Alloca = Builder.CreateAlloca(Ty, ArraySize, Name);
  else
    Alloca = new llvm::AllocaInst(Ty, CGM.getDataLayout().getAllocaAddrSpace(),
                                  ArraySize, Name, &*AllocaInsertPt);
  if (Allocas) {
    Allocas->Add(Alloca);
  }
  return Alloca;
}

/// CreateDefaultAlignTempAlloca - This creates an alloca with the
/// default alignment of the corresponding LLVM type, which is *not*
/// guaranteed to be related in any way to the expected alignment of
/// an AST type that might have been lowered to Ty.
RawAddress CodeGenFunction::CreateDefaultAlignTempAlloca(llvm::Type *Ty,
                                                         const Twine &Name) {
  CharUnits Align =
      CharUnits::fromQuantity(CGM.getDataLayout().getPrefTypeAlign(Ty));
  return CreateTempAlloca(Ty, Align, Name);
}

RawAddress CodeGenFunction::CreateIRTemp(QualType Ty, const Twine &Name) {
  CharUnits Align = getContext().getTypeAlignInChars(Ty);
  return CreateTempAlloca(ConvertType(Ty), Align, Name);
}

RawAddress CodeGenFunction::CreateMemTemp(QualType Ty, const Twine &Name,
                                          RawAddress *Alloca) {
  // FIXME: Should we prefer the preferred type alignment here?
  return CreateMemTemp(Ty, getContext().getTypeAlignInChars(Ty), Name, Alloca);
}

RawAddress CodeGenFunction::CreateMemTemp(QualType Ty, CharUnits Align,
                                          const Twine &Name,
                                          RawAddress *Alloca) {
  RawAddress Result = CreateTempAlloca(ConvertTypeForMem(Ty), Align, Name,
                                       /*ArraySize=*/nullptr, Alloca);

  if (Ty->isConstantMatrixType()) {
    auto *ArrayTy = cast<llvm::ArrayType>(Result.getElementType());
    auto *VectorTy = llvm::FixedVectorType::get(ArrayTy->getElementType(),
                                                ArrayTy->getNumElements());

    Result = Address(Result.getPointer(), VectorTy, Result.getAlignment(),
                     KnownNonNull);
  }
  return Result;
}

RawAddress CodeGenFunction::CreateMemTempWithoutCast(QualType Ty,
                                                     CharUnits Align,
                                                     const Twine &Name) {
  return CreateTempAllocaWithoutCast(ConvertTypeForMem(Ty), Align, Name);
}

RawAddress CodeGenFunction::CreateMemTempWithoutCast(QualType Ty,
                                                     const Twine &Name) {
  return CreateMemTempWithoutCast(Ty, getContext().getTypeAlignInChars(Ty),
                                  Name);
}

/// EvaluateExprAsBool - Perform the usual unary conversions on the specified
/// expression and compare the result against zero, returning an Int1Ty value.
llvm::Value *CodeGenFunction::EvaluateExprAsBool(const Expr *E) {
  PGO.setCurrentStmt(E);
  if (const MemberPointerType *MPT = E->getType()->getAs<MemberPointerType>()) {
    llvm::Value *MemPtr = EmitScalarExpr(E);
    return CGM.getCXXABI().EmitMemberPointerIsNotNull(*this, MemPtr, MPT);
  }

  QualType BoolTy = getContext().BoolTy;
  SourceLocation Loc = E->getExprLoc();
  CGFPOptionsRAII FPOptsRAII(*this, E);
  if (!E->getType()->isAnyComplexType())
    return EmitScalarConversion(EmitScalarExpr(E), E->getType(), BoolTy, Loc);

  return EmitComplexToScalarConversion(EmitComplexExpr(E), E->getType(), BoolTy,
                                       Loc);
}

/// EmitIgnoredExpr - Emit code to compute the specified expression,
/// ignoring the result.
void CodeGenFunction::EmitIgnoredExpr(const Expr *E) {
  if (E->isPRValue())
    return (void)EmitAnyExpr(E, AggValueSlot::ignored(), true);

  // if this is a bitfield-resulting conditional operator, we can special case
  // emit this. The normal 'EmitLValue' version of this is particularly
  // difficult to codegen for, since creating a single "LValue" for two
  // different sized arguments here is not particularly doable.
  if (const auto *CondOp = dyn_cast<AbstractConditionalOperator>(
          E->IgnoreParenNoopCasts(getContext()))) {
    if (CondOp->getObjectKind() == OK_BitField)
      return EmitIgnoredConditionalOperator(CondOp);
  }

  // Just emit it as an l-value and drop the result.
  EmitLValue(E);
}

/// EmitAnyExpr - Emit code to compute the specified expression which
/// can have any type.  The result is returned as an RValue struct.
/// If this is an aggregate expression, AggSlot indicates where the
/// result should be returned.
RValue CodeGenFunction::EmitAnyExpr(const Expr *E,
                                    AggValueSlot aggSlot,
                                    bool ignoreResult) {
  switch (getEvaluationKind(E->getType())) {
  case TEK_Scalar:
    return RValue::get(EmitScalarExpr(E, ignoreResult));
  case TEK_Complex:
    return RValue::getComplex(EmitComplexExpr(E, ignoreResult, ignoreResult));
  case TEK_Aggregate:
    if (!ignoreResult && aggSlot.isIgnored())
      aggSlot = CreateAggTemp(E->getType(), "agg-temp");
    EmitAggExpr(E, aggSlot);
    return aggSlot.asRValue();
  }
  llvm_unreachable("bad evaluation kind");
}

/// EmitAnyExprToTemp - Similar to EmitAnyExpr(), however, the result will
/// always be accessible even if no aggregate location is provided.
RValue CodeGenFunction::EmitAnyExprToTemp(const Expr *E) {
  AggValueSlot AggSlot = AggValueSlot::ignored();

  if (hasAggregateEvaluationKind(E->getType()))
    AggSlot = CreateAggTemp(E->getType(), "agg.tmp");
  return EmitAnyExpr(E, AggSlot);
}

/// EmitAnyExprToMem - Evaluate an expression into a given memory
/// location.
void CodeGenFunction::EmitAnyExprToMem(const Expr *E,
                                       Address Location,
                                       Qualifiers Quals,
                                       bool IsInit) {
  // FIXME: This function should take an LValue as an argument.
  switch (getEvaluationKind(E->getType())) {
  case TEK_Complex:
    EmitComplexExprIntoLValue(E, MakeAddrLValue(Location, E->getType()),
                              /*isInit*/ false);
    return;

  case TEK_Aggregate: {
    EmitAggExpr(E, AggValueSlot::forAddr(Location, Quals,
                                         AggValueSlot::IsDestructed_t(IsInit),
                                         AggValueSlot::DoesNotNeedGCBarriers,
                                         AggValueSlot::IsAliased_t(!IsInit),
                                         AggValueSlot::MayOverlap));
    return;
  }

  case TEK_Scalar: {
    RValue RV = RValue::get(EmitScalarExpr(E, /*Ignore*/ false));
    LValue LV = MakeAddrLValue(Location, E->getType());
    EmitStoreThroughLValue(RV, LV);
    return;
  }
  }
  llvm_unreachable("bad evaluation kind");
}

static void
pushTemporaryCleanup(CodeGenFunction &CGF, const MaterializeTemporaryExpr *M,
                     const Expr *E, Address ReferenceTemporary) {
  // Objective-C++ ARC:
  //   If we are binding a reference to a temporary that has ownership, we
  //   need to perform retain/release operations on the temporary.
  //
  // FIXME: This should be looking at E, not M.
  if (auto Lifetime = M->getType().getObjCLifetime()) {
    switch (Lifetime) {
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
      // Carry on to normal cleanup handling.
      break;

    case Qualifiers::OCL_Autoreleasing:
      // Nothing to do; cleaned up by an autorelease pool.
      return;

    case Qualifiers::OCL_Strong:
    case Qualifiers::OCL_Weak:
      switch (StorageDuration Duration = M->getStorageDuration()) {
      case SD_Static:
        // Note: we intentionally do not register a cleanup to release
        // the object on program termination.
        return;

      case SD_Thread:
        // FIXME: We should probably register a cleanup in this case.
        return;

      case SD_Automatic:
      case SD_FullExpression:
        CodeGenFunction::Destroyer *Destroy;
        CleanupKind CleanupKind;
        if (Lifetime == Qualifiers::OCL_Strong) {
          const ValueDecl *VD = M->getExtendingDecl();
          bool Precise = isa_and_nonnull<VarDecl>(VD) &&
                         VD->hasAttr<ObjCPreciseLifetimeAttr>();
          CleanupKind = CGF.getARCCleanupKind();
          Destroy = Precise ? &CodeGenFunction::destroyARCStrongPrecise
                            : &CodeGenFunction::destroyARCStrongImprecise;
        } else {
          // __weak objects always get EH cleanups; otherwise, exceptions
          // could cause really nasty crashes instead of mere leaks.
          CleanupKind = NormalAndEHCleanup;
          Destroy = &CodeGenFunction::destroyARCWeak;
        }
        if (Duration == SD_FullExpression)
          CGF.pushDestroy(CleanupKind, ReferenceTemporary,
                          M->getType(), *Destroy,
                          CleanupKind & EHCleanup);
        else
          CGF.pushLifetimeExtendedDestroy(CleanupKind, ReferenceTemporary,
                                          M->getType(),
                                          *Destroy, CleanupKind & EHCleanup);
        return;

      case SD_Dynamic:
        llvm_unreachable("temporary cannot have dynamic storage duration");
      }
      llvm_unreachable("unknown storage duration");
    }
  }

  CXXDestructorDecl *ReferenceTemporaryDtor = nullptr;
  if (const RecordType *RT =
          E->getType()->getBaseElementTypeUnsafe()->getAs<RecordType>()) {
    // Get the destructor for the reference temporary.
    auto *ClassDecl = cast<CXXRecordDecl>(RT->getDecl());
    if (!ClassDecl->hasTrivialDestructor())
      ReferenceTemporaryDtor = ClassDecl->getDestructor();
  }

  if (!ReferenceTemporaryDtor)
    return;

  // Call the destructor for the temporary.
  switch (M->getStorageDuration()) {
  case SD_Static:
  case SD_Thread: {
    llvm::FunctionCallee CleanupFn;
    llvm::Constant *CleanupArg;
    if (E->getType()->isArrayType()) {
      CleanupFn = CodeGenFunction(CGF.CGM).generateDestroyHelper(
          ReferenceTemporary, E->getType(),
          CodeGenFunction::destroyCXXObject, CGF.getLangOpts().Exceptions,
          dyn_cast_or_null<VarDecl>(M->getExtendingDecl()));
      CleanupArg = llvm::Constant::getNullValue(CGF.Int8PtrTy);
    } else {
      CleanupFn = CGF.CGM.getAddrAndTypeOfCXXStructor(
          GlobalDecl(ReferenceTemporaryDtor, Dtor_Complete));
      CleanupArg = cast<llvm::Constant>(ReferenceTemporary.emitRawPointer(CGF));
    }
    CGF.CGM.getCXXABI().registerGlobalDtor(
        CGF, *cast<VarDecl>(M->getExtendingDecl()), CleanupFn, CleanupArg);
    break;
  }

  case SD_FullExpression:
    CGF.pushDestroy(NormalAndEHCleanup, ReferenceTemporary, E->getType(),
                    CodeGenFunction::destroyCXXObject,
                    CGF.getLangOpts().Exceptions);
    break;

  case SD_Automatic:
    CGF.pushLifetimeExtendedDestroy(NormalAndEHCleanup,
                                    ReferenceTemporary, E->getType(),
                                    CodeGenFunction::destroyCXXObject,
                                    CGF.getLangOpts().Exceptions);
    break;

  case SD_Dynamic:
    llvm_unreachable("temporary cannot have dynamic storage duration");
  }
}

static RawAddress createReferenceTemporary(CodeGenFunction &CGF,
                                           const MaterializeTemporaryExpr *M,
                                           const Expr *Inner,
                                           RawAddress *Alloca = nullptr) {
  auto &TCG = CGF.getTargetHooks();
  switch (M->getStorageDuration()) {
  case SD_FullExpression:
  case SD_Automatic: {
    // If we have a constant temporary array or record try to promote it into a
    // constant global under the same rules a normal constant would've been
    // promoted. This is easier on the optimizer and generally emits fewer
    // instructions.
    QualType Ty = Inner->getType();
    if (CGF.CGM.getCodeGenOpts().MergeAllConstants &&
        (Ty->isArrayType() || Ty->isRecordType()) &&
        Ty.isConstantStorage(CGF.getContext(), true, false))
      if (auto Init = ConstantEmitter(CGF).tryEmitAbstract(Inner, Ty)) {
        auto AS = CGF.CGM.GetGlobalConstantAddressSpace();
        auto *GV = new llvm::GlobalVariable(
            CGF.CGM.getModule(), Init->getType(), /*isConstant=*/true,
            llvm::GlobalValue::PrivateLinkage, Init, ".ref.tmp", nullptr,
            llvm::GlobalValue::NotThreadLocal,
            CGF.getContext().getTargetAddressSpace(AS));
        CharUnits alignment = CGF.getContext().getTypeAlignInChars(Ty);
        GV->setAlignment(alignment.getAsAlign());
        llvm::Constant *C = GV;
        if (AS != LangAS::Default)
          C = TCG.performAddrSpaceCast(
              CGF.CGM, GV, AS, LangAS::Default,
              GV->getValueType()->getPointerTo(
                  CGF.getContext().getTargetAddressSpace(LangAS::Default)));
        // FIXME: Should we put the new global into a COMDAT?
        return RawAddress(C, GV->getValueType(), alignment);
      }
    return CGF.CreateMemTemp(Ty, "ref.tmp", Alloca);
  }
  case SD_Thread:
  case SD_Static:
    return CGF.CGM.GetAddrOfGlobalTemporary(M, Inner);

  case SD_Dynamic:
    llvm_unreachable("temporary can't have dynamic storage duration");
  }
  llvm_unreachable("unknown storage duration");
}

/// Helper method to check if the underlying ABI is AAPCS
static bool isAAPCS(const TargetInfo &TargetInfo) {
  return TargetInfo.getABI().starts_with("aapcs");
}

LValue CodeGenFunction::
EmitMaterializeTemporaryExpr(const MaterializeTemporaryExpr *M) {
  const Expr *E = M->getSubExpr();

  assert((!M->getExtendingDecl() || !isa<VarDecl>(M->getExtendingDecl()) ||
          !cast<VarDecl>(M->getExtendingDecl())->isARCPseudoStrong()) &&
         "Reference should never be pseudo-strong!");

  // FIXME: ideally this would use EmitAnyExprToMem, however, we cannot do so
  // as that will cause the lifetime adjustment to be lost for ARC
  auto ownership = M->getType().getObjCLifetime();
  if (ownership != Qualifiers::OCL_None &&
      ownership != Qualifiers::OCL_ExplicitNone) {
    RawAddress Object = createReferenceTemporary(*this, M, E);
    if (auto *Var = dyn_cast<llvm::GlobalVariable>(Object.getPointer())) {
      llvm::Type *Ty = ConvertTypeForMem(E->getType());
      Object = Object.withElementType(Ty);

      // createReferenceTemporary will promote the temporary to a global with a
      // constant initializer if it can.  It can only do this to a value of
      // ARC-manageable type if the value is global and therefore "immune" to
      // ref-counting operations.  Therefore we have no need to emit either a
      // dynamic initialization or a cleanup and we can just return the address
      // of the temporary.
      if (Var->hasInitializer())
        return MakeAddrLValue(Object, M->getType(), AlignmentSource::Decl);

      Var->setInitializer(CGM.EmitNullConstant(E->getType()));
    }
    LValue RefTempDst = MakeAddrLValue(Object, M->getType(),
                                       AlignmentSource::Decl);

    switch (getEvaluationKind(E->getType())) {
    default: llvm_unreachable("expected scalar or aggregate expression");
    case TEK_Scalar:
      EmitScalarInit(E, M->getExtendingDecl(), RefTempDst, false);
      break;
    case TEK_Aggregate: {
      EmitAggExpr(E, AggValueSlot::forAddr(Object,
                                           E->getType().getQualifiers(),
                                           AggValueSlot::IsDestructed,
                                           AggValueSlot::DoesNotNeedGCBarriers,
                                           AggValueSlot::IsNotAliased,
                                           AggValueSlot::DoesNotOverlap));
      break;
    }
    }

    pushTemporaryCleanup(*this, M, E, Object);
    return RefTempDst;
  }

  SmallVector<const Expr *, 2> CommaLHSs;
  SmallVector<SubobjectAdjustment, 2> Adjustments;
  E = E->skipRValueSubobjectAdjustments(CommaLHSs, Adjustments);

  for (const auto &Ignored : CommaLHSs)
    EmitIgnoredExpr(Ignored);

  if (const auto *opaque = dyn_cast<OpaqueValueExpr>(E)) {
    if (opaque->getType()->isRecordType()) {
      assert(Adjustments.empty());
      return EmitOpaqueValueLValue(opaque);
    }
  }

  // Create and initialize the reference temporary.
  RawAddress Alloca = Address::invalid();
  RawAddress Object = createReferenceTemporary(*this, M, E, &Alloca);
  if (auto *Var = dyn_cast<llvm::GlobalVariable>(
          Object.getPointer()->stripPointerCasts())) {
    llvm::Type *TemporaryType = ConvertTypeForMem(E->getType());
    Object = Object.withElementType(TemporaryType);
    // If the temporary is a global and has a constant initializer or is a
    // constant temporary that we promoted to a global, we may have already
    // initialized it.
    if (!Var->hasInitializer()) {
      Var->setInitializer(CGM.EmitNullConstant(E->getType()));
      EmitAnyExprToMem(E, Object, Qualifiers(), /*IsInit*/true);
    }
  } else {
    switch (M->getStorageDuration()) {
    case SD_Automatic:
      if (auto *Size = EmitLifetimeStart(
              CGM.getDataLayout().getTypeAllocSize(Alloca.getElementType()),
              Alloca.getPointer())) {
        pushCleanupAfterFullExpr<CallLifetimeEnd>(NormalEHLifetimeMarker,
                                                  Alloca, Size);
      }
      break;

    case SD_FullExpression: {
      if (!ShouldEmitLifetimeMarkers)
        break;

      // Avoid creating a conditional cleanup just to hold an llvm.lifetime.end
      // marker. Instead, start the lifetime of a conditional temporary earlier
      // so that it's unconditional. Don't do this with sanitizers which need
      // more precise lifetime marks. However when inside an "await.suspend"
      // block, we should always avoid conditional cleanup because it creates
      // boolean marker that lives across await_suspend, which can destroy coro
      // frame.
      ConditionalEvaluation *OldConditional = nullptr;
      CGBuilderTy::InsertPoint OldIP;
      if (isInConditionalBranch() && !E->getType().isDestructedType() &&
          ((!SanOpts.has(SanitizerKind::HWAddress) &&
            !SanOpts.has(SanitizerKind::Memory) &&
            !CGM.getCodeGenOpts().SanitizeAddressUseAfterScope) ||
           inSuspendBlock())) {
        OldConditional = OutermostConditional;
        OutermostConditional = nullptr;

        OldIP = Builder.saveIP();
        llvm::BasicBlock *Block = OldConditional->getStartingBlock();
        Builder.restoreIP(CGBuilderTy::InsertPoint(
            Block, llvm::BasicBlock::iterator(Block->back())));
      }

      if (auto *Size = EmitLifetimeStart(
              CGM.getDataLayout().getTypeAllocSize(Alloca.getElementType()),
              Alloca.getPointer())) {
        pushFullExprCleanup<CallLifetimeEnd>(NormalEHLifetimeMarker, Alloca,
                                             Size);
      }

      if (OldConditional) {
        OutermostConditional = OldConditional;
        Builder.restoreIP(OldIP);
      }
      break;
    }

    default:
      break;
    }
    EmitAnyExprToMem(E, Object, Qualifiers(), /*IsInit*/true);
  }
  pushTemporaryCleanup(*this, M, E, Object);

  // Perform derived-to-base casts and/or field accesses, to get from the
  // temporary object we created (and, potentially, for which we extended
  // the lifetime) to the subobject we're binding the reference to.
  for (SubobjectAdjustment &Adjustment : llvm::reverse(Adjustments)) {
    switch (Adjustment.Kind) {
    case SubobjectAdjustment::DerivedToBaseAdjustment:
      Object =
          GetAddressOfBaseClass(Object, Adjustment.DerivedToBase.DerivedClass,
                                Adjustment.DerivedToBase.BasePath->path_begin(),
                                Adjustment.DerivedToBase.BasePath->path_end(),
                                /*NullCheckValue=*/ false, E->getExprLoc());
      break;

    case SubobjectAdjustment::FieldAdjustment: {
      LValue LV = MakeAddrLValue(Object, E->getType(), AlignmentSource::Decl);
      LV = EmitLValueForField(LV, Adjustment.Field);
      assert(LV.isSimple() &&
             "materialized temporary field is not a simple lvalue");
      Object = LV.getAddress();
      break;
    }

    case SubobjectAdjustment::MemberPointerAdjustment: {
      llvm::Value *Ptr = EmitScalarExpr(Adjustment.Ptr.RHS);
      Object = EmitCXXMemberDataPointerAddress(E, Object, Ptr,
                                               Adjustment.Ptr.MPT);
      break;
    }
    }
  }

  return MakeAddrLValue(Object, M->getType(), AlignmentSource::Decl);
}

RValue
CodeGenFunction::EmitReferenceBindingToExpr(const Expr *E) {
  // Emit the expression as an lvalue.
  LValue LV = EmitLValue(E);
  assert(LV.isSimple());
  llvm::Value *Value = LV.getPointer(*this);

  if (sanitizePerformTypeCheck() && !E->getType()->isFunctionType()) {
    // C++11 [dcl.ref]p5 (as amended by core issue 453):
    //   If a glvalue to which a reference is directly bound designates neither
    //   an existing object or function of an appropriate type nor a region of
    //   storage of suitable size and alignment to contain an object of the
    //   reference's type, the behavior is undefined.
    QualType Ty = E->getType();
    EmitTypeCheck(TCK_ReferenceBinding, E->getExprLoc(), Value, Ty);
  }

  return RValue::get(Value);
}


/// getAccessedFieldNo - Given an encoded value and a result number, return the
/// input field number being accessed.
unsigned CodeGenFunction::getAccessedFieldNo(unsigned Idx,
                                             const llvm::Constant *Elts) {
  return cast<llvm::ConstantInt>(Elts->getAggregateElement(Idx))
      ->getZExtValue();
}

static llvm::Value *emitHashMix(CGBuilderTy &Builder, llvm::Value *Acc,
                                llvm::Value *Ptr) {
  llvm::Value *A0 =
      Builder.CreateMul(Ptr, Builder.getInt64(0xbf58476d1ce4e5b9u));
  llvm::Value *A1 =
      Builder.CreateXor(A0, Builder.CreateLShr(A0, Builder.getInt64(31)));
  return Builder.CreateXor(Acc, A1);
}

bool CodeGenFunction::isNullPointerAllowed(TypeCheckKind TCK) {
  return TCK == TCK_DowncastPointer || TCK == TCK_Upcast ||
         TCK == TCK_UpcastToVirtualBase || TCK == TCK_DynamicOperation;
}

bool CodeGenFunction::isVptrCheckRequired(TypeCheckKind TCK, QualType Ty) {
  CXXRecordDecl *RD = Ty->getAsCXXRecordDecl();
  return (RD && RD->hasDefinition() && RD->isDynamicClass()) &&
         (TCK == TCK_MemberAccess || TCK == TCK_MemberCall ||
          TCK == TCK_DowncastPointer || TCK == TCK_DowncastReference ||
          TCK == TCK_UpcastToVirtualBase || TCK == TCK_DynamicOperation);
}

bool CodeGenFunction::sanitizePerformTypeCheck() const {
  return SanOpts.has(SanitizerKind::Null) ||
         SanOpts.has(SanitizerKind::Alignment) ||
         SanOpts.has(SanitizerKind::ObjectSize) ||
         SanOpts.has(SanitizerKind::Vptr);
}

void CodeGenFunction::EmitTypeCheck(TypeCheckKind TCK, SourceLocation Loc,
                                    llvm::Value *Ptr, QualType Ty,
                                    CharUnits Alignment,
                                    SanitizerSet SkippedChecks,
                                    llvm::Value *ArraySize) {
  if (!sanitizePerformTypeCheck())
    return;

  // Don't check pointers outside the default address space. The null check
  // isn't correct, the object-size check isn't supported by LLVM, and we can't
  // communicate the addresses to the runtime handler for the vptr check.
  if (Ptr->getType()->getPointerAddressSpace())
    return;

  // Don't check pointers to volatile data. The behavior here is implementation-
  // defined.
  if (Ty.isVolatileQualified())
    return;

  SanitizerScope SanScope(this);

  SmallVector<std::pair<llvm::Value *, SanitizerMask>, 3> Checks;
  llvm::BasicBlock *Done = nullptr;

  // Quickly determine whether we have a pointer to an alloca. It's possible
  // to skip null checks, and some alignment checks, for these pointers. This
  // can reduce compile-time significantly.
  auto PtrToAlloca = dyn_cast<llvm::AllocaInst>(Ptr->stripPointerCasts());

  llvm::Value *True = llvm::ConstantInt::getTrue(getLLVMContext());
  llvm::Value *IsNonNull = nullptr;
  bool IsGuaranteedNonNull =
      SkippedChecks.has(SanitizerKind::Null) || PtrToAlloca;
  bool AllowNullPointers = isNullPointerAllowed(TCK);
  if ((SanOpts.has(SanitizerKind::Null) || AllowNullPointers) &&
      !IsGuaranteedNonNull) {
    // The glvalue must not be an empty glvalue.
    IsNonNull = Builder.CreateIsNotNull(Ptr);

    // The IR builder can constant-fold the null check if the pointer points to
    // a constant.
    IsGuaranteedNonNull = IsNonNull == True;

    // Skip the null check if the pointer is known to be non-null.
    if (!IsGuaranteedNonNull) {
      if (AllowNullPointers) {
        // When performing pointer casts, it's OK if the value is null.
        // Skip the remaining checks in that case.
        Done = createBasicBlock("null");
        llvm::BasicBlock *Rest = createBasicBlock("not.null");
        Builder.CreateCondBr(IsNonNull, Rest, Done);
        EmitBlock(Rest);
      } else {
        Checks.push_back(std::make_pair(IsNonNull, SanitizerKind::Null));
      }
    }
  }

  if (SanOpts.has(SanitizerKind::ObjectSize) &&
      !SkippedChecks.has(SanitizerKind::ObjectSize) &&
      !Ty->isIncompleteType()) {
    uint64_t TySize = CGM.getMinimumObjectSize(Ty).getQuantity();
    llvm::Value *Size = llvm::ConstantInt::get(IntPtrTy, TySize);
    if (ArraySize)
      Size = Builder.CreateMul(Size, ArraySize);

    // Degenerate case: new X[0] does not need an objectsize check.
    llvm::Constant *ConstantSize = dyn_cast<llvm::Constant>(Size);
    if (!ConstantSize || !ConstantSize->isNullValue()) {
      // The glvalue must refer to a large enough storage region.
      // FIXME: If Address Sanitizer is enabled, insert dynamic instrumentation
      //        to check this.
      // FIXME: Get object address space
      llvm::Type *Tys[2] = { IntPtrTy, Int8PtrTy };
      llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::objectsize, Tys);
      llvm::Value *Min = Builder.getFalse();
      llvm::Value *NullIsUnknown = Builder.getFalse();
      llvm::Value *Dynamic = Builder.getFalse();
      llvm::Value *LargeEnough = Builder.CreateICmpUGE(
          Builder.CreateCall(F, {Ptr, Min, NullIsUnknown, Dynamic}), Size);
      Checks.push_back(std::make_pair(LargeEnough, SanitizerKind::ObjectSize));
    }
  }

  llvm::MaybeAlign AlignVal;
  llvm::Value *PtrAsInt = nullptr;

  if (SanOpts.has(SanitizerKind::Alignment) &&
      !SkippedChecks.has(SanitizerKind::Alignment)) {
    AlignVal = Alignment.getAsMaybeAlign();
    if (!Ty->isIncompleteType() && !AlignVal)
      AlignVal = CGM.getNaturalTypeAlignment(Ty, nullptr, nullptr,
                                             /*ForPointeeType=*/true)
                     .getAsMaybeAlign();

    // The glvalue must be suitably aligned.
    if (AlignVal && *AlignVal > llvm::Align(1) &&
        (!PtrToAlloca || PtrToAlloca->getAlign() < *AlignVal)) {
      PtrAsInt = Builder.CreatePtrToInt(Ptr, IntPtrTy);
      llvm::Value *Align = Builder.CreateAnd(
          PtrAsInt, llvm::ConstantInt::get(IntPtrTy, AlignVal->value() - 1));
      llvm::Value *Aligned =
          Builder.CreateICmpEQ(Align, llvm::ConstantInt::get(IntPtrTy, 0));
      if (Aligned != True)
        Checks.push_back(std::make_pair(Aligned, SanitizerKind::Alignment));
    }
  }

  if (Checks.size() > 0) {
    llvm::Constant *StaticData[] = {
        EmitCheckSourceLocation(Loc), EmitCheckTypeDescriptor(Ty),
        llvm::ConstantInt::get(Int8Ty, AlignVal ? llvm::Log2(*AlignVal) : 1),
        llvm::ConstantInt::get(Int8Ty, TCK)};
    EmitCheck(Checks, SanitizerHandler::TypeMismatch, StaticData,
              PtrAsInt ? PtrAsInt : Ptr);
  }

  // If possible, check that the vptr indicates that there is a subobject of
  // type Ty at offset zero within this object.
  //
  // C++11 [basic.life]p5,6:
  //   [For storage which does not refer to an object within its lifetime]
  //   The program has undefined behavior if:
  //    -- the [pointer or glvalue] is used to access a non-static data member
  //       or call a non-static member function
  if (SanOpts.has(SanitizerKind::Vptr) &&
      !SkippedChecks.has(SanitizerKind::Vptr) && isVptrCheckRequired(TCK, Ty)) {
    // Ensure that the pointer is non-null before loading it. If there is no
    // compile-time guarantee, reuse the run-time null check or emit a new one.
    if (!IsGuaranteedNonNull) {
      if (!IsNonNull)
        IsNonNull = Builder.CreateIsNotNull(Ptr);
      if (!Done)
        Done = createBasicBlock("vptr.null");
      llvm::BasicBlock *VptrNotNull = createBasicBlock("vptr.not.null");
      Builder.CreateCondBr(IsNonNull, VptrNotNull, Done);
      EmitBlock(VptrNotNull);
    }

    // Compute a deterministic hash of the mangled name of the type.
    SmallString<64> MangledName;
    llvm::raw_svector_ostream Out(MangledName);
    CGM.getCXXABI().getMangleContext().mangleCXXRTTI(Ty.getUnqualifiedType(),
                                                     Out);

    // Contained in NoSanitizeList based on the mangled type.
    if (!CGM.getContext().getNoSanitizeList().containsType(SanitizerKind::Vptr,
                                                           Out.str())) {
      // Load the vptr, and mix it with TypeHash.
      llvm::Value *TypeHash =
          llvm::ConstantInt::get(Int64Ty, xxh3_64bits(Out.str()));

      llvm::Type *VPtrTy = llvm::PointerType::get(IntPtrTy, 0);
      Address VPtrAddr(Ptr, IntPtrTy, getPointerAlign());
      llvm::Value *VPtrVal = GetVTablePtr(VPtrAddr, VPtrTy,
                                          Ty->getAsCXXRecordDecl(),
                                          VTableAuthMode::UnsafeUbsanStrip);
      VPtrVal = Builder.CreateBitOrPointerCast(VPtrVal, IntPtrTy);

      llvm::Value *Hash =
          emitHashMix(Builder, TypeHash, Builder.CreateZExt(VPtrVal, Int64Ty));
      Hash = Builder.CreateTrunc(Hash, IntPtrTy);

      // Look the hash up in our cache.
      const int CacheSize = 128;
      llvm::Type *HashTable = llvm::ArrayType::get(IntPtrTy, CacheSize);
      llvm::Value *Cache = CGM.CreateRuntimeVariable(HashTable,
                                                     "__ubsan_vptr_type_cache");
      llvm::Value *Slot = Builder.CreateAnd(Hash,
                                            llvm::ConstantInt::get(IntPtrTy,
                                                                   CacheSize-1));
      llvm::Value *Indices[] = { Builder.getInt32(0), Slot };
      llvm::Value *CacheVal = Builder.CreateAlignedLoad(
          IntPtrTy, Builder.CreateInBoundsGEP(HashTable, Cache, Indices),
          getPointerAlign());

      // If the hash isn't in the cache, call a runtime handler to perform the
      // hard work of checking whether the vptr is for an object of the right
      // type. This will either fill in the cache and return, or produce a
      // diagnostic.
      llvm::Value *EqualHash = Builder.CreateICmpEQ(CacheVal, Hash);
      llvm::Constant *StaticData[] = {
        EmitCheckSourceLocation(Loc),
        EmitCheckTypeDescriptor(Ty),
        CGM.GetAddrOfRTTIDescriptor(Ty.getUnqualifiedType()),
        llvm::ConstantInt::get(Int8Ty, TCK)
      };
      llvm::Value *DynamicData[] = { Ptr, Hash };
      EmitCheck(std::make_pair(EqualHash, SanitizerKind::Vptr),
                SanitizerHandler::DynamicTypeCacheMiss, StaticData,
                DynamicData);
    }
  }

  if (Done) {
    Builder.CreateBr(Done);
    EmitBlock(Done);
  }
}

llvm::Value *CodeGenFunction::LoadPassedObjectSize(const Expr *E,
                                                   QualType EltTy) {
  ASTContext &C = getContext();
  uint64_t EltSize = C.getTypeSizeInChars(EltTy).getQuantity();
  if (!EltSize)
    return nullptr;

  auto *ArrayDeclRef = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
  if (!ArrayDeclRef)
    return nullptr;

  auto *ParamDecl = dyn_cast<ParmVarDecl>(ArrayDeclRef->getDecl());
  if (!ParamDecl)
    return nullptr;

  auto *POSAttr = ParamDecl->getAttr<PassObjectSizeAttr>();
  if (!POSAttr)
    return nullptr;

  // Don't load the size if it's a lower bound.
  int POSType = POSAttr->getType();
  if (POSType != 0 && POSType != 1)
    return nullptr;

  // Find the implicit size parameter.
  auto PassedSizeIt = SizeArguments.find(ParamDecl);
  if (PassedSizeIt == SizeArguments.end())
    return nullptr;

  const ImplicitParamDecl *PassedSizeDecl = PassedSizeIt->second;
  assert(LocalDeclMap.count(PassedSizeDecl) && "Passed size not loadable");
  Address AddrOfSize = LocalDeclMap.find(PassedSizeDecl)->second;
  llvm::Value *SizeInBytes = EmitLoadOfScalar(AddrOfSize, /*Volatile=*/false,
                                              C.getSizeType(), E->getExprLoc());
  llvm::Value *SizeOfElement =
      llvm::ConstantInt::get(SizeInBytes->getType(), EltSize);
  return Builder.CreateUDiv(SizeInBytes, SizeOfElement);
}

/// If Base is known to point to the start of an array, return the length of
/// that array. Return 0 if the length cannot be determined.
static llvm::Value *getArrayIndexingBound(CodeGenFunction &CGF,
                                          const Expr *Base,
                                          QualType &IndexedType,
                                          LangOptions::StrictFlexArraysLevelKind
                                          StrictFlexArraysLevel) {
  // For the vector indexing extension, the bound is the number of elements.
  if (const VectorType *VT = Base->getType()->getAs<VectorType>()) {
    IndexedType = Base->getType();
    return CGF.Builder.getInt32(VT->getNumElements());
  }

  Base = Base->IgnoreParens();

  if (const auto *CE = dyn_cast<CastExpr>(Base)) {
    if (CE->getCastKind() == CK_ArrayToPointerDecay &&
        !CE->getSubExpr()->isFlexibleArrayMemberLike(CGF.getContext(),
                                                     StrictFlexArraysLevel)) {
      CodeGenFunction::SanitizerScope SanScope(&CGF);

      IndexedType = CE->getSubExpr()->getType();
      const ArrayType *AT = IndexedType->castAsArrayTypeUnsafe();
      if (const auto *CAT = dyn_cast<ConstantArrayType>(AT))
        return CGF.Builder.getInt(CAT->getSize());

      if (const auto *VAT = dyn_cast<VariableArrayType>(AT))
        return CGF.getVLASize(VAT).NumElts;
      // Ignore pass_object_size here. It's not applicable on decayed pointers.
    }
  }

  CodeGenFunction::SanitizerScope SanScope(&CGF);

  QualType EltTy{Base->getType()->getPointeeOrArrayElementType(), 0};
  if (llvm::Value *POS = CGF.LoadPassedObjectSize(Base, EltTy)) {
    IndexedType = Base->getType();
    return POS;
  }

  return nullptr;
}

namespace {

/// \p StructAccessBase returns the base \p Expr of a field access. It returns
/// either a \p DeclRefExpr, representing the base pointer to the struct, i.e.:
///
///     p in p-> a.b.c
///
/// or a \p MemberExpr, if the \p MemberExpr has the \p RecordDecl we're
/// looking for:
///
///     struct s {
///       struct s *ptr;
///       int count;
///       char array[] __attribute__((counted_by(count)));
///     };
///
/// If we have an expression like \p p->ptr->array[index], we want the
/// \p MemberExpr for \p p->ptr instead of \p p.
class StructAccessBase
    : public ConstStmtVisitor<StructAccessBase, const Expr *> {
  const RecordDecl *ExpectedRD;

  bool IsExpectedRecordDecl(const Expr *E) const {
    QualType Ty = E->getType();
    if (Ty->isPointerType())
      Ty = Ty->getPointeeType();
    return ExpectedRD == Ty->getAsRecordDecl();
  }

public:
  StructAccessBase(const RecordDecl *ExpectedRD) : ExpectedRD(ExpectedRD) {}

  //===--------------------------------------------------------------------===//
  //                            Visitor Methods
  //===--------------------------------------------------------------------===//

  // NOTE: If we build C++ support for counted_by, then we'll have to handle
  // horrors like this:
  //
  //     struct S {
  //       int x, y;
  //       int blah[] __attribute__((counted_by(x)));
  //     } s;
  //
  //     int foo(int index, int val) {
  //       int (S::*IHatePMDs)[] = &S::blah;
  //       (s.*IHatePMDs)[index] = val;
  //     }

  const Expr *Visit(const Expr *E) {
    return ConstStmtVisitor<StructAccessBase, const Expr *>::Visit(E);
  }

  const Expr *VisitStmt(const Stmt *S) { return nullptr; }

  // These are the types we expect to return (in order of most to least
  // likely):
  //
  //   1. DeclRefExpr - This is the expression for the base of the structure.
  //      It's exactly what we want to build an access to the \p counted_by
  //      field.
  //   2. MemberExpr - This is the expression that has the same \p RecordDecl
  //      as the flexble array member's lexical enclosing \p RecordDecl. This
  //      allows us to catch things like: "p->p->array"
  //   3. CompoundLiteralExpr - This is for people who create something
  //      heretical like (struct foo has a flexible array member):
  //
  //        (struct foo){ 1, 2 }.blah[idx];
  const Expr *VisitDeclRefExpr(const DeclRefExpr *E) {
    return IsExpectedRecordDecl(E) ? E : nullptr;
  }
  const Expr *VisitMemberExpr(const MemberExpr *E) {
    if (IsExpectedRecordDecl(E) && E->isArrow())
      return E;
    const Expr *Res = Visit(E->getBase());
    return !Res && IsExpectedRecordDecl(E) ? E : Res;
  }
  const Expr *VisitCompoundLiteralExpr(const CompoundLiteralExpr *E) {
    return IsExpectedRecordDecl(E) ? E : nullptr;
  }
  const Expr *VisitCallExpr(const CallExpr *E) {
    return IsExpectedRecordDecl(E) ? E : nullptr;
  }

  const Expr *VisitArraySubscriptExpr(const ArraySubscriptExpr *E) {
    if (IsExpectedRecordDecl(E))
      return E;
    return Visit(E->getBase());
  }
  const Expr *VisitCastExpr(const CastExpr *E) {
    if (E->getCastKind() == CK_LValueToRValue)
      return IsExpectedRecordDecl(E) ? E : nullptr;
    return Visit(E->getSubExpr());
  }
  const Expr *VisitParenExpr(const ParenExpr *E) {
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryAddrOf(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
  const Expr *VisitUnaryDeref(const UnaryOperator *E) {
    return Visit(E->getSubExpr());
  }
};

} // end anonymous namespace

using RecIndicesTy =
    SmallVector<std::pair<const RecordDecl *, llvm::Value *>, 8>;

static bool getGEPIndicesToField(CodeGenFunction &CGF, const RecordDecl *RD,
                                 const FieldDecl *Field,
                                 RecIndicesTy &Indices) {
  const CGRecordLayout &Layout = CGF.CGM.getTypes().getCGRecordLayout(RD);
  int64_t FieldNo = -1;
  for (const FieldDecl *FD : RD->fields()) {
    if (!Layout.containsFieldDecl(FD))
      // This could happen if the field has a struct type that's empty. I don't
      // know why either.
      continue;

    FieldNo = Layout.getLLVMFieldNo(FD);
    if (FD == Field) {
      Indices.emplace_back(std::make_pair(RD, CGF.Builder.getInt32(FieldNo)));
      return true;
    }

    QualType Ty = FD->getType();
    if (Ty->isRecordType()) {
      if (getGEPIndicesToField(CGF, Ty->getAsRecordDecl(), Field, Indices)) {
        if (RD->isUnion())
          FieldNo = 0;
        Indices.emplace_back(std::make_pair(RD, CGF.Builder.getInt32(FieldNo)));
        return true;
      }
    }
  }

  return false;
}

/// This method is typically called in contexts where we can't generate
/// side-effects, like in __builtin_dynamic_object_size. When finding
/// expressions, only choose those that have either already been emitted or can
/// be loaded without side-effects.
///
/// - \p FAMDecl: the \p Decl for the flexible array member. It may not be
///   within the top-level struct.
/// - \p CountDecl: must be within the same non-anonymous struct as \p FAMDecl.
llvm::Value *CodeGenFunction::EmitCountedByFieldExpr(
    const Expr *Base, const FieldDecl *FAMDecl, const FieldDecl *CountDecl) {
  const RecordDecl *RD = CountDecl->getParent()->getOuterLexicalRecordContext();

  // Find the base struct expr (i.e. p in p->a.b.c.d).
  const Expr *StructBase = StructAccessBase(RD).Visit(Base);
  if (!StructBase || StructBase->HasSideEffects(getContext()))
    return nullptr;

  llvm::Value *Res = nullptr;
  if (StructBase->getType()->isPointerType()) {
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Addr = EmitPointerWithAlignment(StructBase, &BaseInfo, &TBAAInfo);
    Res = Addr.emitRawPointer(*this);
  } else if (StructBase->isLValue()) {
    LValue LV = EmitLValue(StructBase);
    Address Addr = LV.getAddress();
    Res = Addr.emitRawPointer(*this);
  } else {
    return nullptr;
  }

  llvm::Value *Zero = Builder.getInt32(0);
  RecIndicesTy Indices;

  getGEPIndicesToField(*this, RD, CountDecl, Indices);

  for (auto I = Indices.rbegin(), E = Indices.rend(); I != E; ++I)
    Res = Builder.CreateInBoundsGEP(
        ConvertType(QualType(I->first->getTypeForDecl(), 0)), Res,
        {Zero, I->second}, "..counted_by.gep");

  return Builder.CreateAlignedLoad(ConvertType(CountDecl->getType()), Res,
                                   getIntAlign(), "..counted_by.load");
}

const FieldDecl *CodeGenFunction::FindCountedByField(const FieldDecl *FD) {
  if (!FD)
    return nullptr;

  const auto *CAT = FD->getType()->getAs<CountAttributedType>();
  if (!CAT)
    return nullptr;

  const auto *CountDRE = cast<DeclRefExpr>(CAT->getCountExpr());
  const auto *CountDecl = CountDRE->getDecl();
  if (const auto *IFD = dyn_cast<IndirectFieldDecl>(CountDecl))
    CountDecl = IFD->getAnonField();

  return dyn_cast<FieldDecl>(CountDecl);
}

void CodeGenFunction::EmitBoundsCheck(const Expr *E, const Expr *Base,
                                      llvm::Value *Index, QualType IndexType,
                                      bool Accessed) {
  assert(SanOpts.has(SanitizerKind::ArrayBounds) &&
         "should not be called unless adding bounds checks");
  const LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel =
      getLangOpts().getStrictFlexArraysLevel();
  QualType IndexedType;
  llvm::Value *Bound =
      getArrayIndexingBound(*this, Base, IndexedType, StrictFlexArraysLevel);

  EmitBoundsCheckImpl(E, Bound, Index, IndexType, IndexedType, Accessed);
}

void CodeGenFunction::EmitBoundsCheckImpl(const Expr *E, llvm::Value *Bound,
                                          llvm::Value *Index,
                                          QualType IndexType,
                                          QualType IndexedType, bool Accessed) {
  if (!Bound)
    return;

  SanitizerScope SanScope(this);

  bool IndexSigned = IndexType->isSignedIntegerOrEnumerationType();
  llvm::Value *IndexVal = Builder.CreateIntCast(Index, SizeTy, IndexSigned);
  llvm::Value *BoundVal = Builder.CreateIntCast(Bound, SizeTy, false);

  llvm::Constant *StaticData[] = {
    EmitCheckSourceLocation(E->getExprLoc()),
    EmitCheckTypeDescriptor(IndexedType),
    EmitCheckTypeDescriptor(IndexType)
  };
  llvm::Value *Check = Accessed ? Builder.CreateICmpULT(IndexVal, BoundVal)
                                : Builder.CreateICmpULE(IndexVal, BoundVal);
  EmitCheck(std::make_pair(Check, SanitizerKind::ArrayBounds),
            SanitizerHandler::OutOfBounds, StaticData, Index);
}

CodeGenFunction::ComplexPairTy CodeGenFunction::
EmitComplexPrePostIncDec(const UnaryOperator *E, LValue LV,
                         bool isInc, bool isPre) {
  ComplexPairTy InVal = EmitLoadOfComplex(LV, E->getExprLoc());

  llvm::Value *NextVal;
  if (isa<llvm::IntegerType>(InVal.first->getType())) {
    uint64_t AmountVal = isInc ? 1 : -1;
    NextVal = llvm::ConstantInt::get(InVal.first->getType(), AmountVal, true);

    // Add the inc/dec to the real part.
    NextVal = Builder.CreateAdd(InVal.first, NextVal, isInc ? "inc" : "dec");
  } else {
    QualType ElemTy = E->getType()->castAs<ComplexType>()->getElementType();
    llvm::APFloat FVal(getContext().getFloatTypeSemantics(ElemTy), 1);
    if (!isInc)
      FVal.changeSign();
    NextVal = llvm::ConstantFP::get(getLLVMContext(), FVal);

    // Add the inc/dec to the real part.
    NextVal = Builder.CreateFAdd(InVal.first, NextVal, isInc ? "inc" : "dec");
  }

  ComplexPairTy IncVal(NextVal, InVal.second);

  // Store the updated result through the lvalue.
  EmitStoreOfComplex(IncVal, LV, /*init*/ false);
  if (getLangOpts().OpenMP)
    CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(*this,
                                                              E->getSubExpr());

  // If this is a postinc, return the value read from memory, otherwise use the
  // updated value.
  return isPre ? IncVal : InVal;
}

void CodeGenModule::EmitExplicitCastExprType(const ExplicitCastExpr *E,
                                             CodeGenFunction *CGF) {
  // Bind VLAs in the cast type.
  if (CGF && E->getType()->isVariablyModifiedType())
    CGF->EmitVariablyModifiedType(E->getType());

  if (CGDebugInfo *DI = getModuleDebugInfo())
    DI->EmitExplicitCastType(E->getType());
}

//===----------------------------------------------------------------------===//
//                         LValue Expression Emission
//===----------------------------------------------------------------------===//

static Address EmitPointerWithAlignment(const Expr *E, LValueBaseInfo *BaseInfo,
                                        TBAAAccessInfo *TBAAInfo,
                                        KnownNonNull_t IsKnownNonNull,
                                        CodeGenFunction &CGF) {
  // We allow this with ObjC object pointers because of fragile ABIs.
  assert(E->getType()->isPointerType() ||
         E->getType()->isObjCObjectPointerType());
  E = E->IgnoreParens();

  // Casts:
  if (const CastExpr *CE = dyn_cast<CastExpr>(E)) {
    if (const auto *ECE = dyn_cast<ExplicitCastExpr>(CE))
      CGF.CGM.EmitExplicitCastExprType(ECE, &CGF);

    switch (CE->getCastKind()) {
    // Non-converting casts (but not C's implicit conversion from void*).
    case CK_BitCast:
    case CK_NoOp:
    case CK_AddressSpaceConversion:
      if (auto PtrTy = CE->getSubExpr()->getType()->getAs<PointerType>()) {
        if (PtrTy->getPointeeType()->isVoidType())
          break;

        LValueBaseInfo InnerBaseInfo;
        TBAAAccessInfo InnerTBAAInfo;
        Address Addr = CGF.EmitPointerWithAlignment(
            CE->getSubExpr(), &InnerBaseInfo, &InnerTBAAInfo, IsKnownNonNull);
        if (BaseInfo) *BaseInfo = InnerBaseInfo;
        if (TBAAInfo) *TBAAInfo = InnerTBAAInfo;

        if (isa<ExplicitCastExpr>(CE)) {
          LValueBaseInfo TargetTypeBaseInfo;
          TBAAAccessInfo TargetTypeTBAAInfo;
          CharUnits Align = CGF.CGM.getNaturalPointeeTypeAlignment(
              E->getType(), &TargetTypeBaseInfo, &TargetTypeTBAAInfo);
          if (TBAAInfo)
            *TBAAInfo =
                CGF.CGM.mergeTBAAInfoForCast(*TBAAInfo, TargetTypeTBAAInfo);
          // If the source l-value is opaque, honor the alignment of the
          // casted-to type.
          if (InnerBaseInfo.getAlignmentSource() != AlignmentSource::Decl) {
            if (BaseInfo)
              BaseInfo->mergeForCast(TargetTypeBaseInfo);
            Addr.setAlignment(Align);
          }
        }

        if (CGF.SanOpts.has(SanitizerKind::CFIUnrelatedCast) &&
            CE->getCastKind() == CK_BitCast) {
          if (auto PT = E->getType()->getAs<PointerType>())
            CGF.EmitVTablePtrCheckForCast(PT->getPointeeType(), Addr,
                                          /*MayBeNull=*/true,
                                          CodeGenFunction::CFITCK_UnrelatedCast,
                                          CE->getBeginLoc());
        }

        llvm::Type *ElemTy =
            CGF.ConvertTypeForMem(E->getType()->getPointeeType());
        Addr = Addr.withElementType(ElemTy);
        if (CE->getCastKind() == CK_AddressSpaceConversion)
          Addr = CGF.Builder.CreateAddrSpaceCast(
              Addr, CGF.ConvertType(E->getType()), ElemTy);
        return CGF.authPointerToPointerCast(Addr, CE->getSubExpr()->getType(),
                                            CE->getType());
      }
      break;

    // Array-to-pointer decay.
    case CK_ArrayToPointerDecay:
      return CGF.EmitArrayToPointerDecay(CE->getSubExpr(), BaseInfo, TBAAInfo);

    // Derived-to-base conversions.
    case CK_UncheckedDerivedToBase:
    case CK_DerivedToBase: {
      // TODO: Support accesses to members of base classes in TBAA. For now, we
      // conservatively pretend that the complete object is of the base class
      // type.
      if (TBAAInfo)
        *TBAAInfo = CGF.CGM.getTBAAAccessInfo(E->getType());
      Address Addr = CGF.EmitPointerWithAlignment(
          CE->getSubExpr(), BaseInfo, nullptr,
          (KnownNonNull_t)(IsKnownNonNull ||
                           CE->getCastKind() == CK_UncheckedDerivedToBase));
      auto Derived = CE->getSubExpr()->getType()->getPointeeCXXRecordDecl();
      return CGF.GetAddressOfBaseClass(
          Addr, Derived, CE->path_begin(), CE->path_end(),
          CGF.ShouldNullCheckClassCastValue(CE), CE->getExprLoc());
    }

    // TODO: Is there any reason to treat base-to-derived conversions
    // specially?
    default:
      break;
    }
  }

  // Unary &.
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_AddrOf) {
      LValue LV = CGF.EmitLValue(UO->getSubExpr(), IsKnownNonNull);
      if (BaseInfo) *BaseInfo = LV.getBaseInfo();
      if (TBAAInfo) *TBAAInfo = LV.getTBAAInfo();
      return LV.getAddress();
    }
  }

  // std::addressof and variants.
  if (auto *Call = dyn_cast<CallExpr>(E)) {
    switch (Call->getBuiltinCallee()) {
    default:
      break;
    case Builtin::BIaddressof:
    case Builtin::BI__addressof:
    case Builtin::BI__builtin_addressof: {
      LValue LV = CGF.EmitLValue(Call->getArg(0), IsKnownNonNull);
      if (BaseInfo) *BaseInfo = LV.getBaseInfo();
      if (TBAAInfo) *TBAAInfo = LV.getTBAAInfo();
      return LV.getAddress();
    }
    }
  }

  // TODO: conditional operators, comma.

  // Otherwise, use the alignment of the type.
  return CGF.makeNaturalAddressForPointer(
      CGF.EmitScalarExpr(E), E->getType()->getPointeeType(), CharUnits(),
      /*ForPointeeType=*/true, BaseInfo, TBAAInfo, IsKnownNonNull);
}

/// EmitPointerWithAlignment - Given an expression of pointer type, try to
/// derive a more accurate bound on the alignment of the pointer.
Address CodeGenFunction::EmitPointerWithAlignment(
    const Expr *E, LValueBaseInfo *BaseInfo, TBAAAccessInfo *TBAAInfo,
    KnownNonNull_t IsKnownNonNull) {
  Address Addr =
      ::EmitPointerWithAlignment(E, BaseInfo, TBAAInfo, IsKnownNonNull, *this);
  if (IsKnownNonNull && !Addr.isKnownNonNull())
    Addr.setKnownNonNull();
  return Addr;
}

llvm::Value *CodeGenFunction::EmitNonNullRValueCheck(RValue RV, QualType T) {
  llvm::Value *V = RV.getScalarVal();
  if (auto MPT = T->getAs<MemberPointerType>())
    return CGM.getCXXABI().EmitMemberPointerIsNotNull(*this, V, MPT);
  return Builder.CreateICmpNE(V, llvm::Constant::getNullValue(V->getType()));
}

RValue CodeGenFunction::GetUndefRValue(QualType Ty) {
  if (Ty->isVoidType())
    return RValue::get(nullptr);

  switch (getEvaluationKind(Ty)) {
  case TEK_Complex: {
    llvm::Type *EltTy =
      ConvertType(Ty->castAs<ComplexType>()->getElementType());
    llvm::Value *U = llvm::UndefValue::get(EltTy);
    return RValue::getComplex(std::make_pair(U, U));
  }

  // If this is a use of an undefined aggregate type, the aggregate must have an
  // identifiable address.  Just because the contents of the value are undefined
  // doesn't mean that the address can't be taken and compared.
  case TEK_Aggregate: {
    Address DestPtr = CreateMemTemp(Ty, "undef.agg.tmp");
    return RValue::getAggregate(DestPtr);
  }

  case TEK_Scalar:
    return RValue::get(llvm::UndefValue::get(ConvertType(Ty)));
  }
  llvm_unreachable("bad evaluation kind");
}

RValue CodeGenFunction::EmitUnsupportedRValue(const Expr *E,
                                              const char *Name) {
  ErrorUnsupported(E, Name);
  return GetUndefRValue(E->getType());
}

LValue CodeGenFunction::EmitUnsupportedLValue(const Expr *E,
                                              const char *Name) {
  ErrorUnsupported(E, Name);
  llvm::Type *ElTy = ConvertType(E->getType());
  llvm::Type *Ty = UnqualPtrTy;
  return MakeAddrLValue(
      Address(llvm::UndefValue::get(Ty), ElTy, CharUnits::One()), E->getType());
}

bool CodeGenFunction::IsWrappedCXXThis(const Expr *Obj) {
  const Expr *Base = Obj;
  while (!isa<CXXThisExpr>(Base)) {
    // The result of a dynamic_cast can be null.
    if (isa<CXXDynamicCastExpr>(Base))
      return false;

    if (const auto *CE = dyn_cast<CastExpr>(Base)) {
      Base = CE->getSubExpr();
    } else if (const auto *PE = dyn_cast<ParenExpr>(Base)) {
      Base = PE->getSubExpr();
    } else if (const auto *UO = dyn_cast<UnaryOperator>(Base)) {
      if (UO->getOpcode() == UO_Extension)
        Base = UO->getSubExpr();
      else
        return false;
    } else {
      return false;
    }
  }
  return true;
}

LValue CodeGenFunction::EmitCheckedLValue(const Expr *E, TypeCheckKind TCK) {
  LValue LV;
  if (SanOpts.has(SanitizerKind::ArrayBounds) && isa<ArraySubscriptExpr>(E))
    LV = EmitArraySubscriptExpr(cast<ArraySubscriptExpr>(E), /*Accessed*/true);
  else
    LV = EmitLValue(E);
  if (!isa<DeclRefExpr>(E) && !LV.isBitField() && LV.isSimple()) {
    SanitizerSet SkippedChecks;
    if (const auto *ME = dyn_cast<MemberExpr>(E)) {
      bool IsBaseCXXThis = IsWrappedCXXThis(ME->getBase());
      if (IsBaseCXXThis)
        SkippedChecks.set(SanitizerKind::Alignment, true);
      if (IsBaseCXXThis || isa<DeclRefExpr>(ME->getBase()))
        SkippedChecks.set(SanitizerKind::Null, true);
    }
    EmitTypeCheck(TCK, E->getExprLoc(), LV, E->getType(), SkippedChecks);
  }
  return LV;
}

/// EmitLValue - Emit code to compute a designator that specifies the location
/// of the expression.
///
/// This can return one of two things: a simple address or a bitfield reference.
/// In either case, the LLVM Value* in the LValue structure is guaranteed to be
/// an LLVM pointer type.
///
/// If this returns a bitfield reference, nothing about the pointee type of the
/// LLVM value is known: For example, it may not be a pointer to an integer.
///
/// If this returns a normal address, and if the lvalue's C type is fixed size,
/// this method guarantees that the returned pointer type will point to an LLVM
/// type of the same size of the lvalue's type.  If the lvalue has a variable
/// length type, this is not possible.
///
LValue CodeGenFunction::EmitLValue(const Expr *E,
                                   KnownNonNull_t IsKnownNonNull) {
  LValue LV = EmitLValueHelper(E, IsKnownNonNull);
  if (IsKnownNonNull && !LV.isKnownNonNull())
    LV.setKnownNonNull();
  return LV;
}

static QualType getConstantExprReferredType(const FullExpr *E,
                                            const ASTContext &Ctx) {
  const Expr *SE = E->getSubExpr()->IgnoreImplicit();
  if (isa<OpaqueValueExpr>(SE))
    return SE->getType();
  return cast<CallExpr>(SE)->getCallReturnType(Ctx)->getPointeeType();
}

LValue CodeGenFunction::EmitLValueHelper(const Expr *E,
                                         KnownNonNull_t IsKnownNonNull) {
  ApplyDebugLocation DL(*this, E);
  switch (E->getStmtClass()) {
  default: return EmitUnsupportedLValue(E, "l-value expression");

  case Expr::ObjCPropertyRefExprClass:
    llvm_unreachable("cannot emit a property reference directly");

  case Expr::ObjCSelectorExprClass:
    return EmitObjCSelectorLValue(cast<ObjCSelectorExpr>(E));
  case Expr::ObjCIsaExprClass:
    return EmitObjCIsaExpr(cast<ObjCIsaExpr>(E));
  case Expr::BinaryOperatorClass:
    return EmitBinaryOperatorLValue(cast<BinaryOperator>(E));
  case Expr::CompoundAssignOperatorClass: {
    QualType Ty = E->getType();
    if (const AtomicType *AT = Ty->getAs<AtomicType>())
      Ty = AT->getValueType();
    if (!Ty->isAnyComplexType())
      return EmitCompoundAssignmentLValue(cast<CompoundAssignOperator>(E));
    return EmitComplexCompoundAssignmentLValue(cast<CompoundAssignOperator>(E));
  }
  case Expr::CallExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::CXXOperatorCallExprClass:
  case Expr::UserDefinedLiteralClass:
    return EmitCallExprLValue(cast<CallExpr>(E));
  case Expr::CXXRewrittenBinaryOperatorClass:
    return EmitLValue(cast<CXXRewrittenBinaryOperator>(E)->getSemanticForm(),
                      IsKnownNonNull);
  case Expr::VAArgExprClass:
    return EmitVAArgExprLValue(cast<VAArgExpr>(E));
  case Expr::DeclRefExprClass:
    return EmitDeclRefLValue(cast<DeclRefExpr>(E));
  case Expr::ConstantExprClass: {
    const ConstantExpr *CE = cast<ConstantExpr>(E);
    if (llvm::Value *Result = ConstantEmitter(*this).tryEmitConstantExpr(CE)) {
      QualType RetType = getConstantExprReferredType(CE, getContext());
      return MakeNaturalAlignAddrLValue(Result, RetType);
    }
    return EmitLValue(cast<ConstantExpr>(E)->getSubExpr(), IsKnownNonNull);
  }
  case Expr::ParenExprClass:
    return EmitLValue(cast<ParenExpr>(E)->getSubExpr(), IsKnownNonNull);
  case Expr::GenericSelectionExprClass:
    return EmitLValue(cast<GenericSelectionExpr>(E)->getResultExpr(),
                      IsKnownNonNull);
  case Expr::PredefinedExprClass:
    return EmitPredefinedLValue(cast<PredefinedExpr>(E));
  case Expr::StringLiteralClass:
    return EmitStringLiteralLValue(cast<StringLiteral>(E));
  case Expr::ObjCEncodeExprClass:
    return EmitObjCEncodeExprLValue(cast<ObjCEncodeExpr>(E));
  case Expr::PseudoObjectExprClass:
    return EmitPseudoObjectLValue(cast<PseudoObjectExpr>(E));
  case Expr::InitListExprClass:
    return EmitInitListLValue(cast<InitListExpr>(E));
  case Expr::CXXTemporaryObjectExprClass:
  case Expr::CXXConstructExprClass:
    return EmitCXXConstructLValue(cast<CXXConstructExpr>(E));
  case Expr::CXXBindTemporaryExprClass:
    return EmitCXXBindTemporaryLValue(cast<CXXBindTemporaryExpr>(E));
  case Expr::CXXUuidofExprClass:
    return EmitCXXUuidofLValue(cast<CXXUuidofExpr>(E));
  case Expr::LambdaExprClass:
    return EmitAggExprToLValue(E);

  case Expr::ExprWithCleanupsClass: {
    const auto *cleanups = cast<ExprWithCleanups>(E);
    RunCleanupsScope Scope(*this);
    LValue LV = EmitLValue(cleanups->getSubExpr(), IsKnownNonNull);
    if (LV.isSimple()) {
      // Defend against branches out of gnu statement expressions surrounded by
      // cleanups.
      Address Addr = LV.getAddress();
      llvm::Value *V = Addr.getBasePointer();
      Scope.ForceCleanup({&V});
      Addr.replaceBasePointer(V);
      return LValue::MakeAddr(Addr, LV.getType(), getContext(),
                              LV.getBaseInfo(), LV.getTBAAInfo());
    }
    // FIXME: Is it possible to create an ExprWithCleanups that produces a
    // bitfield lvalue or some other non-simple lvalue?
    return LV;
  }

  case Expr::CXXDefaultArgExprClass: {
    auto *DAE = cast<CXXDefaultArgExpr>(E);
    CXXDefaultArgExprScope Scope(*this, DAE);
    return EmitLValue(DAE->getExpr(), IsKnownNonNull);
  }
  case Expr::CXXDefaultInitExprClass: {
    auto *DIE = cast<CXXDefaultInitExpr>(E);
    CXXDefaultInitExprScope Scope(*this, DIE);
    return EmitLValue(DIE->getExpr(), IsKnownNonNull);
  }
  case Expr::CXXTypeidExprClass:
    return EmitCXXTypeidLValue(cast<CXXTypeidExpr>(E));

  case Expr::ObjCMessageExprClass:
    return EmitObjCMessageExprLValue(cast<ObjCMessageExpr>(E));
  case Expr::ObjCIvarRefExprClass:
    return EmitObjCIvarRefLValue(cast<ObjCIvarRefExpr>(E));
  case Expr::StmtExprClass:
    return EmitStmtExprLValue(cast<StmtExpr>(E));
  case Expr::UnaryOperatorClass:
    return EmitUnaryOpLValue(cast<UnaryOperator>(E));
  case Expr::ArraySubscriptExprClass:
    return EmitArraySubscriptExpr(cast<ArraySubscriptExpr>(E));
  case Expr::MatrixSubscriptExprClass:
    return EmitMatrixSubscriptExpr(cast<MatrixSubscriptExpr>(E));
  case Expr::ArraySectionExprClass:
    return EmitArraySectionExpr(cast<ArraySectionExpr>(E));
  case Expr::ExtVectorElementExprClass:
    return EmitExtVectorElementExpr(cast<ExtVectorElementExpr>(E));
  case Expr::CXXThisExprClass:
    return MakeAddrLValue(LoadCXXThisAddress(), E->getType());
  case Expr::MemberExprClass:
    return EmitMemberExpr(cast<MemberExpr>(E));
  case Expr::CompoundLiteralExprClass:
    return EmitCompoundLiteralLValue(cast<CompoundLiteralExpr>(E));
  case Expr::ConditionalOperatorClass:
    return EmitConditionalOperatorLValue(cast<ConditionalOperator>(E));
  case Expr::BinaryConditionalOperatorClass:
    return EmitConditionalOperatorLValue(cast<BinaryConditionalOperator>(E));
  case Expr::ChooseExprClass:
    return EmitLValue(cast<ChooseExpr>(E)->getChosenSubExpr(), IsKnownNonNull);
  case Expr::OpaqueValueExprClass:
    return EmitOpaqueValueLValue(cast<OpaqueValueExpr>(E));
  case Expr::SubstNonTypeTemplateParmExprClass:
    return EmitLValue(cast<SubstNonTypeTemplateParmExpr>(E)->getReplacement(),
                      IsKnownNonNull);
  case Expr::ImplicitCastExprClass:
  case Expr::CStyleCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXDynamicCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::CXXAddrspaceCastExprClass:
  case Expr::ObjCBridgedCastExprClass:
    return EmitCastLValue(cast<CastExpr>(E));

  case Expr::MaterializeTemporaryExprClass:
    return EmitMaterializeTemporaryExpr(cast<MaterializeTemporaryExpr>(E));

  case Expr::CoawaitExprClass:
    return EmitCoawaitLValue(cast<CoawaitExpr>(E));
  case Expr::CoyieldExprClass:
    return EmitCoyieldLValue(cast<CoyieldExpr>(E));
  case Expr::PackIndexingExprClass:
    return EmitLValue(cast<PackIndexingExpr>(E)->getSelectedExpr());
  }
}

/// Given an object of the given canonical type, can we safely copy a
/// value out of it based on its initializer?
static bool isConstantEmittableObjectType(QualType type) {
  assert(type.isCanonical());
  assert(!type->isReferenceType());

  // Must be const-qualified but non-volatile.
  Qualifiers qs = type.getLocalQualifiers();
  if (!qs.hasConst() || qs.hasVolatile()) return false;

  // Otherwise, all object types satisfy this except C++ classes with
  // mutable subobjects or non-trivial copy/destroy behavior.
  if (const auto *RT = dyn_cast<RecordType>(type))
    if (const auto *RD = dyn_cast<CXXRecordDecl>(RT->getDecl()))
      if (RD->hasMutableFields() || !RD->isTrivial())
        return false;

  return true;
}

/// Can we constant-emit a load of a reference to a variable of the
/// given type?  This is different from predicates like
/// Decl::mightBeUsableInConstantExpressions because we do want it to apply
/// in situations that don't necessarily satisfy the language's rules
/// for this (e.g. C++'s ODR-use rules).  For example, we want to able
/// to do this with const float variables even if those variables
/// aren't marked 'constexpr'.
enum ConstantEmissionKind {
  CEK_None,
  CEK_AsReferenceOnly,
  CEK_AsValueOrReference,
  CEK_AsValueOnly
};
static ConstantEmissionKind checkVarTypeForConstantEmission(QualType type) {
  type = type.getCanonicalType();
  if (const auto *ref = dyn_cast<ReferenceType>(type)) {
    if (isConstantEmittableObjectType(ref->getPointeeType()))
      return CEK_AsValueOrReference;
    return CEK_AsReferenceOnly;
  }
  if (isConstantEmittableObjectType(type))
    return CEK_AsValueOnly;
  return CEK_None;
}

/// Try to emit a reference to the given value without producing it as
/// an l-value.  This is just an optimization, but it avoids us needing
/// to emit global copies of variables if they're named without triggering
/// a formal use in a context where we can't emit a direct reference to them,
/// for instance if a block or lambda or a member of a local class uses a
/// const int variable or constexpr variable from an enclosing function.
CodeGenFunction::ConstantEmission
CodeGenFunction::tryEmitAsConstant(DeclRefExpr *refExpr) {
  ValueDecl *value = refExpr->getDecl();

  // The value needs to be an enum constant or a constant variable.
  ConstantEmissionKind CEK;
  if (isa<ParmVarDecl>(value)) {
    CEK = CEK_None;
  } else if (auto *var = dyn_cast<VarDecl>(value)) {
    CEK = checkVarTypeForConstantEmission(var->getType());
  } else if (isa<EnumConstantDecl>(value)) {
    CEK = CEK_AsValueOnly;
  } else {
    CEK = CEK_None;
  }
  if (CEK == CEK_None) return ConstantEmission();

  Expr::EvalResult result;
  bool resultIsReference;
  QualType resultType;

  // It's best to evaluate all the way as an r-value if that's permitted.
  if (CEK != CEK_AsReferenceOnly &&
      refExpr->EvaluateAsRValue(result, getContext())) {
    resultIsReference = false;
    resultType = refExpr->getType();

  // Otherwise, try to evaluate as an l-value.
  } else if (CEK != CEK_AsValueOnly &&
             refExpr->EvaluateAsLValue(result, getContext())) {
    resultIsReference = true;
    resultType = value->getType();

  // Failure.
  } else {
    return ConstantEmission();
  }

  // In any case, if the initializer has side-effects, abandon ship.
  if (result.HasSideEffects)
    return ConstantEmission();

  // In CUDA/HIP device compilation, a lambda may capture a reference variable
  // referencing a global host variable by copy. In this case the lambda should
  // make a copy of the value of the global host variable. The DRE of the
  // captured reference variable cannot be emitted as load from the host
  // global variable as compile time constant, since the host variable is not
  // accessible on device. The DRE of the captured reference variable has to be
  // loaded from captures.
  if (CGM.getLangOpts().CUDAIsDevice && result.Val.isLValue() &&
      refExpr->refersToEnclosingVariableOrCapture()) {
    auto *MD = dyn_cast_or_null<CXXMethodDecl>(CurCodeDecl);
    if (MD && MD->getParent()->isLambda() &&
        MD->getOverloadedOperator() == OO_Call) {
      const APValue::LValueBase &base = result.Val.getLValueBase();
      if (const ValueDecl *D = base.dyn_cast<const ValueDecl *>()) {
        if (const VarDecl *VD = dyn_cast<const VarDecl>(D)) {
          if (!VD->hasAttr<CUDADeviceAttr>()) {
            return ConstantEmission();
          }
        }
      }
    }
  }

  // Emit as a constant.
  auto C = ConstantEmitter(*this).emitAbstract(refExpr->getLocation(),
                                               result.Val, resultType);

  // Make sure we emit a debug reference to the global variable.
  // This should probably fire even for
  if (isa<VarDecl>(value)) {
    if (!getContext().DeclMustBeEmitted(cast<VarDecl>(value)))
      EmitDeclRefExprDbgValue(refExpr, result.Val);
  } else {
    assert(isa<EnumConstantDecl>(value));
    EmitDeclRefExprDbgValue(refExpr, result.Val);
  }

  // If we emitted a reference constant, we need to dereference that.
  if (resultIsReference)
    return ConstantEmission::forReference(C);

  return ConstantEmission::forValue(C);
}

static DeclRefExpr *tryToConvertMemberExprToDeclRefExpr(CodeGenFunction &CGF,
                                                        const MemberExpr *ME) {
  if (auto *VD = dyn_cast<VarDecl>(ME->getMemberDecl())) {
    // Try to emit static variable member expressions as DREs.
    return DeclRefExpr::Create(
        CGF.getContext(), NestedNameSpecifierLoc(), SourceLocation(), VD,
        /*RefersToEnclosingVariableOrCapture=*/false, ME->getExprLoc(),
        ME->getType(), ME->getValueKind(), nullptr, nullptr, ME->isNonOdrUse());
  }
  return nullptr;
}

CodeGenFunction::ConstantEmission
CodeGenFunction::tryEmitAsConstant(const MemberExpr *ME) {
  if (DeclRefExpr *DRE = tryToConvertMemberExprToDeclRefExpr(*this, ME))
    return tryEmitAsConstant(DRE);
  return ConstantEmission();
}

llvm::Value *CodeGenFunction::emitScalarConstant(
    const CodeGenFunction::ConstantEmission &Constant, Expr *E) {
  assert(Constant && "not a constant");
  if (Constant.isReference())
    return EmitLoadOfLValue(Constant.getReferenceLValue(*this, E),
                            E->getExprLoc())
        .getScalarVal();
  return Constant.getValue();
}

llvm::Value *CodeGenFunction::EmitLoadOfScalar(LValue lvalue,
                                               SourceLocation Loc) {
  return EmitLoadOfScalar(lvalue.getAddress(), lvalue.isVolatile(),
                          lvalue.getType(), Loc, lvalue.getBaseInfo(),
                          lvalue.getTBAAInfo(), lvalue.isNontemporal());
}

static bool hasBooleanRepresentation(QualType Ty) {
  if (Ty->isBooleanType())
    return true;

  if (const EnumType *ET = Ty->getAs<EnumType>())
    return ET->getDecl()->getIntegerType()->isBooleanType();

  if (const AtomicType *AT = Ty->getAs<AtomicType>())
    return hasBooleanRepresentation(AT->getValueType());

  return false;
}

static bool getRangeForType(CodeGenFunction &CGF, QualType Ty,
                            llvm::APInt &Min, llvm::APInt &End,
                            bool StrictEnums, bool IsBool) {
  const EnumType *ET = Ty->getAs<EnumType>();
  bool IsRegularCPlusPlusEnum = CGF.getLangOpts().CPlusPlus && StrictEnums &&
                                ET && !ET->getDecl()->isFixed();
  if (!IsBool && !IsRegularCPlusPlusEnum)
    return false;

  if (IsBool) {
    Min = llvm::APInt(CGF.getContext().getTypeSize(Ty), 0);
    End = llvm::APInt(CGF.getContext().getTypeSize(Ty), 2);
  } else {
    const EnumDecl *ED = ET->getDecl();
    ED->getValueRange(End, Min);
  }
  return true;
}

llvm::MDNode *CodeGenFunction::getRangeForLoadFromType(QualType Ty) {
  llvm::APInt Min, End;
  if (!getRangeForType(*this, Ty, Min, End, CGM.getCodeGenOpts().StrictEnums,
                       hasBooleanRepresentation(Ty)))
    return nullptr;

  llvm::MDBuilder MDHelper(getLLVMContext());
  return MDHelper.createRange(Min, End);
}

bool CodeGenFunction::EmitScalarRangeCheck(llvm::Value *Value, QualType Ty,
                                           SourceLocation Loc) {
  bool HasBoolCheck = SanOpts.has(SanitizerKind::Bool);
  bool HasEnumCheck = SanOpts.has(SanitizerKind::Enum);
  if (!HasBoolCheck && !HasEnumCheck)
    return false;

  bool IsBool = hasBooleanRepresentation(Ty) ||
                NSAPI(CGM.getContext()).isObjCBOOLType(Ty);
  bool NeedsBoolCheck = HasBoolCheck && IsBool;
  bool NeedsEnumCheck = HasEnumCheck && Ty->getAs<EnumType>();
  if (!NeedsBoolCheck && !NeedsEnumCheck)
    return false;

  // Single-bit booleans don't need to be checked. Special-case this to avoid
  // a bit width mismatch when handling bitfield values. This is handled by
  // EmitFromMemory for the non-bitfield case.
  if (IsBool &&
      cast<llvm::IntegerType>(Value->getType())->getBitWidth() == 1)
    return false;

  llvm::APInt Min, End;
  if (!getRangeForType(*this, Ty, Min, End, /*StrictEnums=*/true, IsBool))
    return true;

  auto &Ctx = getLLVMContext();
  SanitizerScope SanScope(this);
  llvm::Value *Check;
  --End;
  if (!Min) {
    Check = Builder.CreateICmpULE(Value, llvm::ConstantInt::get(Ctx, End));
  } else {
    llvm::Value *Upper =
        Builder.CreateICmpSLE(Value, llvm::ConstantInt::get(Ctx, End));
    llvm::Value *Lower =
        Builder.CreateICmpSGE(Value, llvm::ConstantInt::get(Ctx, Min));
    Check = Builder.CreateAnd(Upper, Lower);
  }
  llvm::Constant *StaticArgs[] = {EmitCheckSourceLocation(Loc),
                                  EmitCheckTypeDescriptor(Ty)};
  SanitizerMask Kind =
      NeedsEnumCheck ? SanitizerKind::Enum : SanitizerKind::Bool;
  EmitCheck(std::make_pair(Check, Kind), SanitizerHandler::LoadInvalidValue,
            StaticArgs, EmitCheckValue(Value));
  return true;
}

llvm::Value *CodeGenFunction::EmitLoadOfScalar(Address Addr, bool Volatile,
                                               QualType Ty,
                                               SourceLocation Loc,
                                               LValueBaseInfo BaseInfo,
                                               TBAAAccessInfo TBAAInfo,
                                               bool isNontemporal) {
  if (auto *GV = dyn_cast<llvm::GlobalValue>(Addr.getBasePointer()))
    if (GV->isThreadLocal())
      Addr = Addr.withPointer(Builder.CreateThreadLocalAddress(GV),
                              NotKnownNonNull);

  if (const auto *ClangVecTy = Ty->getAs<VectorType>()) {
    // Boolean vectors use `iN` as storage type.
    if (ClangVecTy->isExtVectorBoolType()) {
      llvm::Type *ValTy = ConvertType(Ty);
      unsigned ValNumElems =
          cast<llvm::FixedVectorType>(ValTy)->getNumElements();
      // Load the `iP` storage object (P is the padded vector size).
      auto *RawIntV = Builder.CreateLoad(Addr, Volatile, "load_bits");
      const auto *RawIntTy = RawIntV->getType();
      assert(RawIntTy->isIntegerTy() && "compressed iN storage for bitvectors");
      // Bitcast iP --> <P x i1>.
      auto *PaddedVecTy = llvm::FixedVectorType::get(
          Builder.getInt1Ty(), RawIntTy->getPrimitiveSizeInBits());
      llvm::Value *V = Builder.CreateBitCast(RawIntV, PaddedVecTy);
      // Shuffle <P x i1> --> <N x i1> (N is the actual bit size).
      V = emitBoolVecConversion(V, ValNumElems, "extractvec");

      return EmitFromMemory(V, Ty);
    }

    // Handle vectors of size 3 like size 4 for better performance.
    const llvm::Type *EltTy = Addr.getElementType();
    const auto *VTy = cast<llvm::FixedVectorType>(EltTy);

    if (!CGM.getCodeGenOpts().PreserveVec3Type && VTy->getNumElements() == 3) {

      llvm::VectorType *vec4Ty =
          llvm::FixedVectorType::get(VTy->getElementType(), 4);
      Address Cast = Addr.withElementType(vec4Ty);
      // Now load value.
      llvm::Value *V = Builder.CreateLoad(Cast, Volatile, "loadVec4");

      // Shuffle vector to get vec3.
      V = Builder.CreateShuffleVector(V, ArrayRef<int>{0, 1, 2}, "extractVec");
      return EmitFromMemory(V, Ty);
    }
  }

  // Atomic operations have to be done on integral types.
  LValue AtomicLValue =
      LValue::MakeAddr(Addr, Ty, getContext(), BaseInfo, TBAAInfo);
  if (Ty->isAtomicType() || LValueIsSuitableForInlineAtomic(AtomicLValue)) {
    return EmitAtomicLoad(AtomicLValue, Loc).getScalarVal();
  }

  Addr =
      Addr.withElementType(convertTypeForLoadStore(Ty, Addr.getElementType()));

  llvm::LoadInst *Load = Builder.CreateLoad(Addr, Volatile);
  if (isNontemporal) {
    llvm::MDNode *Node = llvm::MDNode::get(
        Load->getContext(), llvm::ConstantAsMetadata::get(Builder.getInt32(1)));
    Load->setMetadata(llvm::LLVMContext::MD_nontemporal, Node);
  }

  CGM.DecorateInstructionWithTBAA(Load, TBAAInfo);

  if (EmitScalarRangeCheck(Load, Ty, Loc)) {
    // In order to prevent the optimizer from throwing away the check, don't
    // attach range metadata to the load.
  } else if (CGM.getCodeGenOpts().OptimizationLevel > 0)
    if (llvm::MDNode *RangeInfo = getRangeForLoadFromType(Ty)) {
      Load->setMetadata(llvm::LLVMContext::MD_range, RangeInfo);
      Load->setMetadata(llvm::LLVMContext::MD_noundef,
                        llvm::MDNode::get(getLLVMContext(), std::nullopt));
    }

  return EmitFromMemory(Load, Ty);
}

/// Converts a scalar value from its primary IR type (as returned
/// by ConvertType) to its load/store type (as returned by
/// convertTypeForLoadStore).
llvm::Value *CodeGenFunction::EmitToMemory(llvm::Value *Value, QualType Ty) {
  if (hasBooleanRepresentation(Ty) || Ty->isBitIntType()) {
    llvm::Type *StoreTy = convertTypeForLoadStore(Ty, Value->getType());
    bool Signed = Ty->isSignedIntegerOrEnumerationType();
    return Builder.CreateIntCast(Value, StoreTy, Signed, "storedv");
  }

  if (Ty->isExtVectorBoolType()) {
    llvm::Type *StoreTy = convertTypeForLoadStore(Ty, Value->getType());
    // Expand to the memory bit width.
    unsigned MemNumElems = StoreTy->getPrimitiveSizeInBits();
    // <N x i1> --> <P x i1>.
    Value = emitBoolVecConversion(Value, MemNumElems, "insertvec");
    // <P x i1> --> iP.
    Value = Builder.CreateBitCast(Value, StoreTy);
  }

  return Value;
}

/// Converts a scalar value from its load/store type (as returned
/// by convertTypeForLoadStore) to its primary IR type (as returned
/// by ConvertType).
llvm::Value *CodeGenFunction::EmitFromMemory(llvm::Value *Value, QualType Ty) {
  if (Ty->isExtVectorBoolType()) {
    const auto *RawIntTy = Value->getType();
    // Bitcast iP --> <P x i1>.
    auto *PaddedVecTy = llvm::FixedVectorType::get(
        Builder.getInt1Ty(), RawIntTy->getPrimitiveSizeInBits());
    auto *V = Builder.CreateBitCast(Value, PaddedVecTy);
    // Shuffle <P x i1> --> <N x i1> (N is the actual bit size).
    llvm::Type *ValTy = ConvertType(Ty);
    unsigned ValNumElems = cast<llvm::FixedVectorType>(ValTy)->getNumElements();
    return emitBoolVecConversion(V, ValNumElems, "extractvec");
  }

  if (hasBooleanRepresentation(Ty) || Ty->isBitIntType()) {
    llvm::Type *ResTy = ConvertType(Ty);
    return Builder.CreateTrunc(Value, ResTy, "loadedv");
  }

  return Value;
}

// Convert the pointer of \p Addr to a pointer to a vector (the value type of
// MatrixType), if it points to a array (the memory type of MatrixType).
static RawAddress MaybeConvertMatrixAddress(RawAddress Addr,
                                            CodeGenFunction &CGF,
                                            bool IsVector = true) {
  auto *ArrayTy = dyn_cast<llvm::ArrayType>(Addr.getElementType());
  if (ArrayTy && IsVector) {
    auto *VectorTy = llvm::FixedVectorType::get(ArrayTy->getElementType(),
                                                ArrayTy->getNumElements());

    return Addr.withElementType(VectorTy);
  }
  auto *VectorTy = dyn_cast<llvm::VectorType>(Addr.getElementType());
  if (VectorTy && !IsVector) {
    auto *ArrayTy = llvm::ArrayType::get(
        VectorTy->getElementType(),
        cast<llvm::FixedVectorType>(VectorTy)->getNumElements());

    return Addr.withElementType(ArrayTy);
  }

  return Addr;
}

// Emit a store of a matrix LValue. This may require casting the original
// pointer to memory address (ArrayType) to a pointer to the value type
// (VectorType).
static void EmitStoreOfMatrixScalar(llvm::Value *value, LValue lvalue,
                                    bool isInit, CodeGenFunction &CGF) {
  Address Addr = MaybeConvertMatrixAddress(lvalue.getAddress(), CGF,
                                           value->getType()->isVectorTy());
  CGF.EmitStoreOfScalar(value, Addr, lvalue.isVolatile(), lvalue.getType(),
                        lvalue.getBaseInfo(), lvalue.getTBAAInfo(), isInit,
                        lvalue.isNontemporal());
}

void CodeGenFunction::EmitStoreOfScalar(llvm::Value *Value, Address Addr,
                                        bool Volatile, QualType Ty,
                                        LValueBaseInfo BaseInfo,
                                        TBAAAccessInfo TBAAInfo,
                                        bool isInit, bool isNontemporal) {
  if (auto *GV = dyn_cast<llvm::GlobalValue>(Addr.getBasePointer()))
    if (GV->isThreadLocal())
      Addr = Addr.withPointer(Builder.CreateThreadLocalAddress(GV),
                              NotKnownNonNull);

  llvm::Type *SrcTy = Value->getType();
  if (const auto *ClangVecTy = Ty->getAs<VectorType>()) {
    auto *VecTy = dyn_cast<llvm::FixedVectorType>(SrcTy);
    if (!CGM.getCodeGenOpts().PreserveVec3Type) {
      // Handle vec3 special.
      if (VecTy && !ClangVecTy->isExtVectorBoolType() &&
          cast<llvm::FixedVectorType>(VecTy)->getNumElements() == 3) {
        // Our source is a vec3, do a shuffle vector to make it a vec4.
        Value = Builder.CreateShuffleVector(Value, ArrayRef<int>{0, 1, 2, -1},
                                            "extractVec");
        SrcTy = llvm::FixedVectorType::get(VecTy->getElementType(), 4);
      }
      if (Addr.getElementType() != SrcTy) {
        Addr = Addr.withElementType(SrcTy);
      }
    }
  }

  Value = EmitToMemory(Value, Ty);

  LValue AtomicLValue =
      LValue::MakeAddr(Addr, Ty, getContext(), BaseInfo, TBAAInfo);
  if (Ty->isAtomicType() ||
      (!isInit && LValueIsSuitableForInlineAtomic(AtomicLValue))) {
    EmitAtomicStore(RValue::get(Value), AtomicLValue, isInit);
    return;
  }

  llvm::StoreInst *Store = Builder.CreateStore(Value, Addr, Volatile);
  if (isNontemporal) {
    llvm::MDNode *Node =
        llvm::MDNode::get(Store->getContext(),
                          llvm::ConstantAsMetadata::get(Builder.getInt32(1)));
    Store->setMetadata(llvm::LLVMContext::MD_nontemporal, Node);
  }

  CGM.DecorateInstructionWithTBAA(Store, TBAAInfo);
}

void CodeGenFunction::EmitStoreOfScalar(llvm::Value *value, LValue lvalue,
                                        bool isInit) {
  if (lvalue.getType()->isConstantMatrixType()) {
    EmitStoreOfMatrixScalar(value, lvalue, isInit, *this);
    return;
  }

  EmitStoreOfScalar(value, lvalue.getAddress(), lvalue.isVolatile(),
                    lvalue.getType(), lvalue.getBaseInfo(),
                    lvalue.getTBAAInfo(), isInit, lvalue.isNontemporal());
}

// Emit a load of a LValue of matrix type. This may require casting the pointer
// to memory address (ArrayType) to a pointer to the value type (VectorType).
static RValue EmitLoadOfMatrixLValue(LValue LV, SourceLocation Loc,
                                     CodeGenFunction &CGF) {
  assert(LV.getType()->isConstantMatrixType());
  Address Addr = MaybeConvertMatrixAddress(LV.getAddress(), CGF);
  LV.setAddress(Addr);
  return RValue::get(CGF.EmitLoadOfScalar(LV, Loc));
}

RValue CodeGenFunction::EmitLoadOfAnyValue(LValue LV, AggValueSlot Slot,
                                           SourceLocation Loc) {
  QualType Ty = LV.getType();
  switch (getEvaluationKind(Ty)) {
  case TEK_Scalar:
    return EmitLoadOfLValue(LV, Loc);
  case TEK_Complex:
    return RValue::getComplex(EmitLoadOfComplex(LV, Loc));
  case TEK_Aggregate:
    EmitAggFinalDestCopy(Ty, Slot, LV, EVK_NonRValue);
    return Slot.asRValue();
  }
  llvm_unreachable("bad evaluation kind");
}

/// EmitLoadOfLValue - Given an expression that represents a value lvalue, this
/// method emits the address of the lvalue, then loads the result as an rvalue,
/// returning the rvalue.
RValue CodeGenFunction::EmitLoadOfLValue(LValue LV, SourceLocation Loc) {
  if (LV.isObjCWeak()) {
    // load of a __weak object.
    Address AddrWeakObj = LV.getAddress();
    return RValue::get(CGM.getObjCRuntime().EmitObjCWeakRead(*this,
                                                             AddrWeakObj));
  }
  if (LV.getQuals().getObjCLifetime() == Qualifiers::OCL_Weak) {
    // In MRC mode, we do a load+autorelease.
    if (!getLangOpts().ObjCAutoRefCount) {
      return RValue::get(EmitARCLoadWeak(LV.getAddress()));
    }

    // In ARC mode, we load retained and then consume the value.
    llvm::Value *Object = EmitARCLoadWeakRetained(LV.getAddress());
    Object = EmitObjCConsumeObject(LV.getType(), Object);
    return RValue::get(Object);
  }

  if (LV.isSimple()) {
    assert(!LV.getType()->isFunctionType());

    if (LV.getType()->isConstantMatrixType())
      return EmitLoadOfMatrixLValue(LV, Loc, *this);

    // Everything needs a load.
    return RValue::get(EmitLoadOfScalar(LV, Loc));
  }

  if (LV.isVectorElt()) {
    llvm::LoadInst *Load = Builder.CreateLoad(LV.getVectorAddress(),
                                              LV.isVolatileQualified());
    return RValue::get(Builder.CreateExtractElement(Load, LV.getVectorIdx(),
                                                    "vecext"));
  }

  // If this is a reference to a subset of the elements of a vector, either
  // shuffle the input or extract/insert them as appropriate.
  if (LV.isExtVectorElt()) {
    return EmitLoadOfExtVectorElementLValue(LV);
  }

  // Global Register variables always invoke intrinsics
  if (LV.isGlobalReg())
    return EmitLoadOfGlobalRegLValue(LV);

  if (LV.isMatrixElt()) {
    llvm::Value *Idx = LV.getMatrixIdx();
    if (CGM.getCodeGenOpts().OptimizationLevel > 0) {
      const auto *const MatTy = LV.getType()->castAs<ConstantMatrixType>();
      llvm::MatrixBuilder MB(Builder);
      MB.CreateIndexAssumption(Idx, MatTy->getNumElementsFlattened());
    }
    llvm::LoadInst *Load =
        Builder.CreateLoad(LV.getMatrixAddress(), LV.isVolatileQualified());
    return RValue::get(Builder.CreateExtractElement(Load, Idx, "matrixext"));
  }

  assert(LV.isBitField() && "Unknown LValue type!");
  return EmitLoadOfBitfieldLValue(LV, Loc);
}

RValue CodeGenFunction::EmitLoadOfBitfieldLValue(LValue LV,
                                                 SourceLocation Loc) {
  const CGBitFieldInfo &Info = LV.getBitFieldInfo();

  // Get the output type.
  llvm::Type *ResLTy = ConvertType(LV.getType());

  Address Ptr = LV.getBitFieldAddress();
  llvm::Value *Val =
      Builder.CreateLoad(Ptr, LV.isVolatileQualified(), "bf.load");

  bool UseVolatile = LV.isVolatileQualified() &&
                     Info.VolatileStorageSize != 0 && isAAPCS(CGM.getTarget());
  const unsigned Offset = UseVolatile ? Info.VolatileOffset : Info.Offset;
  const unsigned StorageSize =
      UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
  if (Info.IsSigned) {
    assert(static_cast<unsigned>(Offset + Info.Size) <= StorageSize);
    unsigned HighBits = StorageSize - Offset - Info.Size;
    if (HighBits)
      Val = Builder.CreateShl(Val, HighBits, "bf.shl");
    if (Offset + HighBits)
      Val = Builder.CreateAShr(Val, Offset + HighBits, "bf.ashr");
  } else {
    if (Offset)
      Val = Builder.CreateLShr(Val, Offset, "bf.lshr");
    if (static_cast<unsigned>(Offset) + Info.Size < StorageSize)
      Val = Builder.CreateAnd(
          Val, llvm::APInt::getLowBitsSet(StorageSize, Info.Size), "bf.clear");
  }
  Val = Builder.CreateIntCast(Val, ResLTy, Info.IsSigned, "bf.cast");
  EmitScalarRangeCheck(Val, LV.getType(), Loc);
  return RValue::get(Val);
}

// If this is a reference to a subset of the elements of a vector, create an
// appropriate shufflevector.
RValue CodeGenFunction::EmitLoadOfExtVectorElementLValue(LValue LV) {
  llvm::Value *Vec = Builder.CreateLoad(LV.getExtVectorAddress(),
                                        LV.isVolatileQualified());

  // HLSL allows treating scalars as one-element vectors. Converting the scalar
  // IR value to a vector here allows the rest of codegen to behave as normal.
  if (getLangOpts().HLSL && !Vec->getType()->isVectorTy()) {
    llvm::Type *DstTy = llvm::FixedVectorType::get(Vec->getType(), 1);
    llvm::Value *Zero = llvm::Constant::getNullValue(CGM.Int64Ty);
    Vec = Builder.CreateInsertElement(DstTy, Vec, Zero, "cast.splat");
  }

  const llvm::Constant *Elts = LV.getExtVectorElts();

  // If the result of the expression is a non-vector type, we must be extracting
  // a single element.  Just codegen as an extractelement.
  const VectorType *ExprVT = LV.getType()->getAs<VectorType>();
  if (!ExprVT) {
    unsigned InIdx = getAccessedFieldNo(0, Elts);
    llvm::Value *Elt = llvm::ConstantInt::get(SizeTy, InIdx);
    return RValue::get(Builder.CreateExtractElement(Vec, Elt));
  }

  // Always use shuffle vector to try to retain the original program structure
  unsigned NumResultElts = ExprVT->getNumElements();

  SmallVector<int, 4> Mask;
  for (unsigned i = 0; i != NumResultElts; ++i)
    Mask.push_back(getAccessedFieldNo(i, Elts));

  Vec = Builder.CreateShuffleVector(Vec, Mask);
  return RValue::get(Vec);
}

/// Generates lvalue for partial ext_vector access.
Address CodeGenFunction::EmitExtVectorElementLValue(LValue LV) {
  Address VectorAddress = LV.getExtVectorAddress();
  QualType EQT = LV.getType()->castAs<VectorType>()->getElementType();
  llvm::Type *VectorElementTy = CGM.getTypes().ConvertType(EQT);

  Address CastToPointerElement = VectorAddress.withElementType(VectorElementTy);

  const llvm::Constant *Elts = LV.getExtVectorElts();
  unsigned ix = getAccessedFieldNo(0, Elts);

  Address VectorBasePtrPlusIx =
    Builder.CreateConstInBoundsGEP(CastToPointerElement, ix,
                                   "vector.elt");

  return VectorBasePtrPlusIx;
}

/// Load of global gamed gegisters are always calls to intrinsics.
RValue CodeGenFunction::EmitLoadOfGlobalRegLValue(LValue LV) {
  assert((LV.getType()->isIntegerType() || LV.getType()->isPointerType()) &&
         "Bad type for register variable");
  llvm::MDNode *RegName = cast<llvm::MDNode>(
      cast<llvm::MetadataAsValue>(LV.getGlobalReg())->getMetadata());

  // We accept integer and pointer types only
  llvm::Type *OrigTy = CGM.getTypes().ConvertType(LV.getType());
  llvm::Type *Ty = OrigTy;
  if (OrigTy->isPointerTy())
    Ty = CGM.getTypes().getDataLayout().getIntPtrType(OrigTy);
  llvm::Type *Types[] = { Ty };

  llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::read_register, Types);
  llvm::Value *Call = Builder.CreateCall(
      F, llvm::MetadataAsValue::get(Ty->getContext(), RegName));
  if (OrigTy->isPointerTy())
    Call = Builder.CreateIntToPtr(Call, OrigTy);
  return RValue::get(Call);
}

/// EmitStoreThroughLValue - Store the specified rvalue into the specified
/// lvalue, where both are guaranteed to the have the same type, and that type
/// is 'Ty'.
void CodeGenFunction::EmitStoreThroughLValue(RValue Src, LValue Dst,
                                             bool isInit) {
  if (!Dst.isSimple()) {
    if (Dst.isVectorElt()) {
      // Read/modify/write the vector, inserting the new element.
      llvm::Value *Vec = Builder.CreateLoad(Dst.getVectorAddress(),
                                            Dst.isVolatileQualified());
      auto *IRStoreTy = dyn_cast<llvm::IntegerType>(Vec->getType());
      if (IRStoreTy) {
        auto *IRVecTy = llvm::FixedVectorType::get(
            Builder.getInt1Ty(), IRStoreTy->getPrimitiveSizeInBits());
        Vec = Builder.CreateBitCast(Vec, IRVecTy);
        // iN --> <N x i1>.
      }
      Vec = Builder.CreateInsertElement(Vec, Src.getScalarVal(),
                                        Dst.getVectorIdx(), "vecins");
      if (IRStoreTy) {
        // <N x i1> --> <iN>.
        Vec = Builder.CreateBitCast(Vec, IRStoreTy);
      }
      Builder.CreateStore(Vec, Dst.getVectorAddress(),
                          Dst.isVolatileQualified());
      return;
    }

    // If this is an update of extended vector elements, insert them as
    // appropriate.
    if (Dst.isExtVectorElt())
      return EmitStoreThroughExtVectorComponentLValue(Src, Dst);

    if (Dst.isGlobalReg())
      return EmitStoreThroughGlobalRegLValue(Src, Dst);

    if (Dst.isMatrixElt()) {
      llvm::Value *Idx = Dst.getMatrixIdx();
      if (CGM.getCodeGenOpts().OptimizationLevel > 0) {
        const auto *const MatTy = Dst.getType()->castAs<ConstantMatrixType>();
        llvm::MatrixBuilder MB(Builder);
        MB.CreateIndexAssumption(Idx, MatTy->getNumElementsFlattened());
      }
      llvm::Instruction *Load = Builder.CreateLoad(Dst.getMatrixAddress());
      llvm::Value *Vec =
          Builder.CreateInsertElement(Load, Src.getScalarVal(), Idx, "matins");
      Builder.CreateStore(Vec, Dst.getMatrixAddress(),
                          Dst.isVolatileQualified());
      return;
    }

    assert(Dst.isBitField() && "Unknown LValue type");
    return EmitStoreThroughBitfieldLValue(Src, Dst);
  }

  // There's special magic for assigning into an ARC-qualified l-value.
  if (Qualifiers::ObjCLifetime Lifetime = Dst.getQuals().getObjCLifetime()) {
    switch (Lifetime) {
    case Qualifiers::OCL_None:
      llvm_unreachable("present but none");

    case Qualifiers::OCL_ExplicitNone:
      // nothing special
      break;

    case Qualifiers::OCL_Strong:
      if (isInit) {
        Src = RValue::get(EmitARCRetain(Dst.getType(), Src.getScalarVal()));
        break;
      }
      EmitARCStoreStrong(Dst, Src.getScalarVal(), /*ignore*/ true);
      return;

    case Qualifiers::OCL_Weak:
      if (isInit)
        // Initialize and then skip the primitive store.
        EmitARCInitWeak(Dst.getAddress(), Src.getScalarVal());
      else
        EmitARCStoreWeak(Dst.getAddress(), Src.getScalarVal(),
                         /*ignore*/ true);
      return;

    case Qualifiers::OCL_Autoreleasing:
      Src = RValue::get(EmitObjCExtendObjectLifetime(Dst.getType(),
                                                     Src.getScalarVal()));
      // fall into the normal path
      break;
    }
  }

  if (Dst.isObjCWeak() && !Dst.isNonGC()) {
    // load of a __weak object.
    Address LvalueDst = Dst.getAddress();
    llvm::Value *src = Src.getScalarVal();
     CGM.getObjCRuntime().EmitObjCWeakAssign(*this, src, LvalueDst);
    return;
  }

  if (Dst.isObjCStrong() && !Dst.isNonGC()) {
    // load of a __strong object.
    Address LvalueDst = Dst.getAddress();
    llvm::Value *src = Src.getScalarVal();
    if (Dst.isObjCIvar()) {
      assert(Dst.getBaseIvarExp() && "BaseIvarExp is NULL");
      llvm::Type *ResultType = IntPtrTy;
      Address dst = EmitPointerWithAlignment(Dst.getBaseIvarExp());
      llvm::Value *RHS = dst.emitRawPointer(*this);
      RHS = Builder.CreatePtrToInt(RHS, ResultType, "sub.ptr.rhs.cast");
      llvm::Value *LHS = Builder.CreatePtrToInt(LvalueDst.emitRawPointer(*this),
                                                ResultType, "sub.ptr.lhs.cast");
      llvm::Value *BytesBetween = Builder.CreateSub(LHS, RHS, "ivar.offset");
      CGM.getObjCRuntime().EmitObjCIvarAssign(*this, src, dst, BytesBetween);
    } else if (Dst.isGlobalObjCRef()) {
      CGM.getObjCRuntime().EmitObjCGlobalAssign(*this, src, LvalueDst,
                                                Dst.isThreadLocalRef());
    }
    else
      CGM.getObjCRuntime().EmitObjCStrongCastAssign(*this, src, LvalueDst);
    return;
  }

  assert(Src.isScalar() && "Can't emit an agg store with this method");
  EmitStoreOfScalar(Src.getScalarVal(), Dst, isInit);
}

void CodeGenFunction::EmitStoreThroughBitfieldLValue(RValue Src, LValue Dst,
                                                     llvm::Value **Result) {
  const CGBitFieldInfo &Info = Dst.getBitFieldInfo();
  llvm::Type *ResLTy = convertTypeForLoadStore(Dst.getType());
  Address Ptr = Dst.getBitFieldAddress();

  // Get the source value, truncated to the width of the bit-field.
  llvm::Value *SrcVal = Src.getScalarVal();

  // Cast the source to the storage type and shift it into place.
  SrcVal = Builder.CreateIntCast(SrcVal, Ptr.getElementType(),
                                 /*isSigned=*/false);
  llvm::Value *MaskedVal = SrcVal;

  const bool UseVolatile =
      CGM.getCodeGenOpts().AAPCSBitfieldWidth && Dst.isVolatileQualified() &&
      Info.VolatileStorageSize != 0 && isAAPCS(CGM.getTarget());
  const unsigned StorageSize =
      UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
  const unsigned Offset = UseVolatile ? Info.VolatileOffset : Info.Offset;
  // See if there are other bits in the bitfield's storage we'll need to load
  // and mask together with source before storing.
  if (StorageSize != Info.Size) {
    assert(StorageSize > Info.Size && "Invalid bitfield size.");
    llvm::Value *Val =
        Builder.CreateLoad(Ptr, Dst.isVolatileQualified(), "bf.load");

    // Mask the source value as needed.
    if (!hasBooleanRepresentation(Dst.getType()))
      SrcVal = Builder.CreateAnd(
          SrcVal, llvm::APInt::getLowBitsSet(StorageSize, Info.Size),
          "bf.value");
    MaskedVal = SrcVal;
    if (Offset)
      SrcVal = Builder.CreateShl(SrcVal, Offset, "bf.shl");

    // Mask out the original value.
    Val = Builder.CreateAnd(
        Val, ~llvm::APInt::getBitsSet(StorageSize, Offset, Offset + Info.Size),
        "bf.clear");

    // Or together the unchanged values and the source value.
    SrcVal = Builder.CreateOr(Val, SrcVal, "bf.set");
  } else {
    assert(Offset == 0);
    // According to the AACPS:
    // When a volatile bit-field is written, and its container does not overlap
    // with any non-bit-field member, its container must be read exactly once
    // and written exactly once using the access width appropriate to the type
    // of the container. The two accesses are not atomic.
    if (Dst.isVolatileQualified() && isAAPCS(CGM.getTarget()) &&
        CGM.getCodeGenOpts().ForceAAPCSBitfieldLoad)
      Builder.CreateLoad(Ptr, true, "bf.load");
  }

  // Write the new value back out.
  Builder.CreateStore(SrcVal, Ptr, Dst.isVolatileQualified());

  // Return the new value of the bit-field, if requested.
  if (Result) {
    llvm::Value *ResultVal = MaskedVal;

    // Sign extend the value if needed.
    if (Info.IsSigned) {
      assert(Info.Size <= StorageSize);
      unsigned HighBits = StorageSize - Info.Size;
      if (HighBits) {
        ResultVal = Builder.CreateShl(ResultVal, HighBits, "bf.result.shl");
        ResultVal = Builder.CreateAShr(ResultVal, HighBits, "bf.result.ashr");
      }
    }

    ResultVal = Builder.CreateIntCast(ResultVal, ResLTy, Info.IsSigned,
                                      "bf.result.cast");
    *Result = EmitFromMemory(ResultVal, Dst.getType());
  }
}

void CodeGenFunction::EmitStoreThroughExtVectorComponentLValue(RValue Src,
                                                               LValue Dst) {
  // HLSL allows storing to scalar values through ExtVector component LValues.
  // To support this we need to handle the case where the destination address is
  // a scalar.
  Address DstAddr = Dst.getExtVectorAddress();
  if (!DstAddr.getElementType()->isVectorTy()) {
    assert(!Dst.getType()->isVectorType() &&
           "this should only occur for non-vector l-values");
    Builder.CreateStore(Src.getScalarVal(), DstAddr, Dst.isVolatileQualified());
    return;
  }

  // This access turns into a read/modify/write of the vector.  Load the input
  // value now.
  llvm::Value *Vec = Builder.CreateLoad(DstAddr, Dst.isVolatileQualified());
  const llvm::Constant *Elts = Dst.getExtVectorElts();

  llvm::Value *SrcVal = Src.getScalarVal();

  if (const VectorType *VTy = Dst.getType()->getAs<VectorType>()) {
    unsigned NumSrcElts = VTy->getNumElements();
    unsigned NumDstElts =
        cast<llvm::FixedVectorType>(Vec->getType())->getNumElements();
    if (NumDstElts == NumSrcElts) {
      // Use shuffle vector is the src and destination are the same number of
      // elements and restore the vector mask since it is on the side it will be
      // stored.
      SmallVector<int, 4> Mask(NumDstElts);
      for (unsigned i = 0; i != NumSrcElts; ++i)
        Mask[getAccessedFieldNo(i, Elts)] = i;

      Vec = Builder.CreateShuffleVector(SrcVal, Mask);
    } else if (NumDstElts > NumSrcElts) {
      // Extended the source vector to the same length and then shuffle it
      // into the destination.
      // FIXME: since we're shuffling with undef, can we just use the indices
      //        into that?  This could be simpler.
      SmallVector<int, 4> ExtMask;
      for (unsigned i = 0; i != NumSrcElts; ++i)
        ExtMask.push_back(i);
      ExtMask.resize(NumDstElts, -1);
      llvm::Value *ExtSrcVal = Builder.CreateShuffleVector(SrcVal, ExtMask);
      // build identity
      SmallVector<int, 4> Mask;
      for (unsigned i = 0; i != NumDstElts; ++i)
        Mask.push_back(i);

      // When the vector size is odd and .odd or .hi is used, the last element
      // of the Elts constant array will be one past the size of the vector.
      // Ignore the last element here, if it is greater than the mask size.
      if (getAccessedFieldNo(NumSrcElts - 1, Elts) == Mask.size())
        NumSrcElts--;

      // modify when what gets shuffled in
      for (unsigned i = 0; i != NumSrcElts; ++i)
        Mask[getAccessedFieldNo(i, Elts)] = i + NumDstElts;
      Vec = Builder.CreateShuffleVector(Vec, ExtSrcVal, Mask);
    } else {
      // We should never shorten the vector
      llvm_unreachable("unexpected shorten vector length");
    }
  } else {
    // If the Src is a scalar (not a vector), and the target is a vector it must
    // be updating one element.
    unsigned InIdx = getAccessedFieldNo(0, Elts);
    llvm::Value *Elt = llvm::ConstantInt::get(SizeTy, InIdx);
    Vec = Builder.CreateInsertElement(Vec, SrcVal, Elt);
  }

  Builder.CreateStore(Vec, Dst.getExtVectorAddress(),
                      Dst.isVolatileQualified());
}

/// Store of global named registers are always calls to intrinsics.
void CodeGenFunction::EmitStoreThroughGlobalRegLValue(RValue Src, LValue Dst) {
  assert((Dst.getType()->isIntegerType() || Dst.getType()->isPointerType()) &&
         "Bad type for register variable");
  llvm::MDNode *RegName = cast<llvm::MDNode>(
      cast<llvm::MetadataAsValue>(Dst.getGlobalReg())->getMetadata());
  assert(RegName && "Register LValue is not metadata");

  // We accept integer and pointer types only
  llvm::Type *OrigTy = CGM.getTypes().ConvertType(Dst.getType());
  llvm::Type *Ty = OrigTy;
  if (OrigTy->isPointerTy())
    Ty = CGM.getTypes().getDataLayout().getIntPtrType(OrigTy);
  llvm::Type *Types[] = { Ty };

  llvm::Function *F = CGM.getIntrinsic(llvm::Intrinsic::write_register, Types);
  llvm::Value *Value = Src.getScalarVal();
  if (OrigTy->isPointerTy())
    Value = Builder.CreatePtrToInt(Value, Ty);
  Builder.CreateCall(
      F, {llvm::MetadataAsValue::get(Ty->getContext(), RegName), Value});
}

// setObjCGCLValueClass - sets class of the lvalue for the purpose of
// generating write-barries API. It is currently a global, ivar,
// or neither.
static void setObjCGCLValueClass(const ASTContext &Ctx, const Expr *E,
                                 LValue &LV,
                                 bool IsMemberAccess=false) {
  if (Ctx.getLangOpts().getGC() == LangOptions::NonGC)
    return;

  if (isa<ObjCIvarRefExpr>(E)) {
    QualType ExpTy = E->getType();
    if (IsMemberAccess && ExpTy->isPointerType()) {
      // If ivar is a structure pointer, assigning to field of
      // this struct follows gcc's behavior and makes it a non-ivar
      // writer-barrier conservatively.
      ExpTy = ExpTy->castAs<PointerType>()->getPointeeType();
      if (ExpTy->isRecordType()) {
        LV.setObjCIvar(false);
        return;
      }
    }
    LV.setObjCIvar(true);
    auto *Exp = cast<ObjCIvarRefExpr>(const_cast<Expr *>(E));
    LV.setBaseIvarExp(Exp->getBase());
    LV.setObjCArray(E->getType()->isArrayType());
    return;
  }

  if (const auto *Exp = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(Exp->getDecl())) {
      if (VD->hasGlobalStorage()) {
        LV.setGlobalObjCRef(true);
        LV.setThreadLocalRef(VD->getTLSKind() != VarDecl::TLS_None);
      }
    }
    LV.setObjCArray(E->getType()->isArrayType());
    return;
  }

  if (const auto *Exp = dyn_cast<UnaryOperator>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getSubExpr(), LV, IsMemberAccess);
    return;
  }

  if (const auto *Exp = dyn_cast<ParenExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getSubExpr(), LV, IsMemberAccess);
    if (LV.isObjCIvar()) {
      // If cast is to a structure pointer, follow gcc's behavior and make it
      // a non-ivar write-barrier.
      QualType ExpTy = E->getType();
      if (ExpTy->isPointerType())
        ExpTy = ExpTy->castAs<PointerType>()->getPointeeType();
      if (ExpTy->isRecordType())
        LV.setObjCIvar(false);
    }
    return;
  }

  if (const auto *Exp = dyn_cast<GenericSelectionExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getResultExpr(), LV);
    return;
  }

  if (const auto *Exp = dyn_cast<ImplicitCastExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getSubExpr(), LV, IsMemberAccess);
    return;
  }

  if (const auto *Exp = dyn_cast<CStyleCastExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getSubExpr(), LV, IsMemberAccess);
    return;
  }

  if (const auto *Exp = dyn_cast<ObjCBridgedCastExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getSubExpr(), LV, IsMemberAccess);
    return;
  }

  if (const auto *Exp = dyn_cast<ArraySubscriptExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getBase(), LV);
    if (LV.isObjCIvar() && !LV.isObjCArray())
      // Using array syntax to assigning to what an ivar points to is not
      // same as assigning to the ivar itself. {id *Names;} Names[i] = 0;
      LV.setObjCIvar(false);
    else if (LV.isGlobalObjCRef() && !LV.isObjCArray())
      // Using array syntax to assigning to what global points to is not
      // same as assigning to the global itself. {id *G;} G[i] = 0;
      LV.setGlobalObjCRef(false);
    return;
  }

  if (const auto *Exp = dyn_cast<MemberExpr>(E)) {
    setObjCGCLValueClass(Ctx, Exp->getBase(), LV, true);
    // We don't know if member is an 'ivar', but this flag is looked at
    // only in the context of LV.isObjCIvar().
    LV.setObjCArray(E->getType()->isArrayType());
    return;
  }
}

static LValue EmitThreadPrivateVarDeclLValue(
    CodeGenFunction &CGF, const VarDecl *VD, QualType T, Address Addr,
    llvm::Type *RealVarTy, SourceLocation Loc) {
  if (CGF.CGM.getLangOpts().OpenMPIRBuilder)
    Addr = CodeGenFunction::OMPBuilderCBHelpers::getAddrOfThreadPrivate(
        CGF, VD, Addr, Loc);
  else
    Addr =
        CGF.CGM.getOpenMPRuntime().getAddrOfThreadPrivate(CGF, VD, Addr, Loc);

  Addr = Addr.withElementType(RealVarTy);
  return CGF.MakeAddrLValue(Addr, T, AlignmentSource::Decl);
}

static Address emitDeclTargetVarDeclLValue(CodeGenFunction &CGF,
                                           const VarDecl *VD, QualType T) {
  std::optional<OMPDeclareTargetDeclAttr::MapTypeTy> Res =
      OMPDeclareTargetDeclAttr::isDeclareTargetDeclaration(VD);
  // Return an invalid address if variable is MT_To (or MT_Enter starting with
  // OpenMP 5.2) and unified memory is not enabled. For all other cases: MT_Link
  // and MT_To (or MT_Enter) with unified memory, return a valid address.
  if (!Res || ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
                *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
               !CGF.CGM.getOpenMPRuntime().hasRequiresUnifiedSharedMemory()))
    return Address::invalid();
  assert(((*Res == OMPDeclareTargetDeclAttr::MT_Link) ||
          ((*Res == OMPDeclareTargetDeclAttr::MT_To ||
            *Res == OMPDeclareTargetDeclAttr::MT_Enter) &&
           CGF.CGM.getOpenMPRuntime().hasRequiresUnifiedSharedMemory())) &&
         "Expected link clause OR to clause with unified memory enabled.");
  QualType PtrTy = CGF.getContext().getPointerType(VD->getType());
  Address Addr = CGF.CGM.getOpenMPRuntime().getAddrOfDeclareTargetVar(VD);
  return CGF.EmitLoadOfPointer(Addr, PtrTy->castAs<PointerType>());
}

Address
CodeGenFunction::EmitLoadOfReference(LValue RefLVal,
                                     LValueBaseInfo *PointeeBaseInfo,
                                     TBAAAccessInfo *PointeeTBAAInfo) {
  llvm::LoadInst *Load =
      Builder.CreateLoad(RefLVal.getAddress(), RefLVal.isVolatile());
  CGM.DecorateInstructionWithTBAA(Load, RefLVal.getTBAAInfo());
  return makeNaturalAddressForPointer(Load, RefLVal.getType()->getPointeeType(),
                                      CharUnits(), /*ForPointeeType=*/true,
                                      PointeeBaseInfo, PointeeTBAAInfo);
}

LValue CodeGenFunction::EmitLoadOfReferenceLValue(LValue RefLVal) {
  LValueBaseInfo PointeeBaseInfo;
  TBAAAccessInfo PointeeTBAAInfo;
  Address PointeeAddr = EmitLoadOfReference(RefLVal, &PointeeBaseInfo,
                                            &PointeeTBAAInfo);
  return MakeAddrLValue(PointeeAddr, RefLVal.getType()->getPointeeType(),
                        PointeeBaseInfo, PointeeTBAAInfo);
}

Address CodeGenFunction::EmitLoadOfPointer(Address Ptr,
                                           const PointerType *PtrTy,
                                           LValueBaseInfo *BaseInfo,
                                           TBAAAccessInfo *TBAAInfo) {
  llvm::Value *Addr = Builder.CreateLoad(Ptr);
  return makeNaturalAddressForPointer(Addr, PtrTy->getPointeeType(),
                                      CharUnits(), /*ForPointeeType=*/true,
                                      BaseInfo, TBAAInfo);
}

LValue CodeGenFunction::EmitLoadOfPointerLValue(Address PtrAddr,
                                                const PointerType *PtrTy) {
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  Address Addr = EmitLoadOfPointer(PtrAddr, PtrTy, &BaseInfo, &TBAAInfo);
  return MakeAddrLValue(Addr, PtrTy->getPointeeType(), BaseInfo, TBAAInfo);
}

static LValue EmitGlobalVarDeclLValue(CodeGenFunction &CGF,
                                      const Expr *E, const VarDecl *VD) {
  QualType T = E->getType();

  // If it's thread_local, emit a call to its wrapper function instead.
  if (VD->getTLSKind() == VarDecl::TLS_Dynamic &&
      CGF.CGM.getCXXABI().usesThreadWrapperFunction(VD))
    return CGF.CGM.getCXXABI().EmitThreadLocalVarDeclLValue(CGF, VD, T);
  // Check if the variable is marked as declare target with link clause in
  // device codegen.
  if (CGF.getLangOpts().OpenMPIsTargetDevice) {
    Address Addr = emitDeclTargetVarDeclLValue(CGF, VD, T);
    if (Addr.isValid())
      return CGF.MakeAddrLValue(Addr, T, AlignmentSource::Decl);
  }

  llvm::Value *V = CGF.CGM.GetAddrOfGlobalVar(VD);

  if (VD->getTLSKind() != VarDecl::TLS_None)
    V = CGF.Builder.CreateThreadLocalAddress(V);

  llvm::Type *RealVarTy = CGF.getTypes().ConvertTypeForMem(VD->getType());
  CharUnits Alignment = CGF.getContext().getDeclAlign(VD);
  Address Addr(V, RealVarTy, Alignment);
  // Emit reference to the private copy of the variable if it is an OpenMP
  // threadprivate variable.
  if (CGF.getLangOpts().OpenMP && !CGF.getLangOpts().OpenMPSimd &&
      VD->hasAttr<OMPThreadPrivateDeclAttr>()) {
    return EmitThreadPrivateVarDeclLValue(CGF, VD, T, Addr, RealVarTy,
                                          E->getExprLoc());
  }
  LValue LV = VD->getType()->isReferenceType() ?
      CGF.EmitLoadOfReferenceLValue(Addr, VD->getType(),
                                    AlignmentSource::Decl) :
      CGF.MakeAddrLValue(Addr, T, AlignmentSource::Decl);
  setObjCGCLValueClass(CGF.getContext(), E, LV);
  return LV;
}

llvm::Constant *CodeGenModule::getRawFunctionPointer(GlobalDecl GD,
                                                     llvm::Type *Ty) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  if (FD->hasAttr<WeakRefAttr>()) {
    ConstantAddress aliasee = GetWeakRefReference(FD);
    return aliasee.getPointer();
  }

  llvm::Constant *V = GetAddrOfFunction(GD, Ty);
  return V;
}

static LValue EmitFunctionDeclLValue(CodeGenFunction &CGF, const Expr *E,
                                     GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());
  llvm::Constant *V = CGF.CGM.getFunctionPointer(GD);
  CharUnits Alignment = CGF.getContext().getDeclAlign(FD);
  return CGF.MakeAddrLValue(V, E->getType(), Alignment,
                            AlignmentSource::Decl);
}

static LValue EmitCapturedFieldLValue(CodeGenFunction &CGF, const FieldDecl *FD,
                                      llvm::Value *ThisValue) {

  return CGF.EmitLValueForLambdaField(FD, ThisValue);
}

/// Named Registers are named metadata pointing to the register name
/// which will be read from/written to as an argument to the intrinsic
/// @llvm.read/write_register.
/// So far, only the name is being passed down, but other options such as
/// register type, allocation type or even optimization options could be
/// passed down via the metadata node.
static LValue EmitGlobalNamedRegister(const VarDecl *VD, CodeGenModule &CGM) {
  SmallString<64> Name("llvm.named.register.");
  AsmLabelAttr *Asm = VD->getAttr<AsmLabelAttr>();
  assert(Asm->getLabel().size() < 64-Name.size() &&
      "Register name too big");
  Name.append(Asm->getLabel());
  llvm::NamedMDNode *M =
    CGM.getModule().getOrInsertNamedMetadata(Name);
  if (M->getNumOperands() == 0) {
    llvm::MDString *Str = llvm::MDString::get(CGM.getLLVMContext(),
                                              Asm->getLabel());
    llvm::Metadata *Ops[] = {Str};
    M->addOperand(llvm::MDNode::get(CGM.getLLVMContext(), Ops));
  }

  CharUnits Alignment = CGM.getContext().getDeclAlign(VD);

  llvm::Value *Ptr =
    llvm::MetadataAsValue::get(CGM.getLLVMContext(), M->getOperand(0));
  return LValue::MakeGlobalReg(Ptr, Alignment, VD->getType());
}

/// Determine whether we can emit a reference to \p VD from the current
/// context, despite not necessarily having seen an odr-use of the variable in
/// this context.
static bool canEmitSpuriousReferenceToVariable(CodeGenFunction &CGF,
                                               const DeclRefExpr *E,
                                               const VarDecl *VD) {
  // For a variable declared in an enclosing scope, do not emit a spurious
  // reference even if we have a capture, as that will emit an unwarranted
  // reference to our capture state, and will likely generate worse code than
  // emitting a local copy.
  if (E->refersToEnclosingVariableOrCapture())
    return false;

  // For a local declaration declared in this function, we can always reference
  // it even if we don't have an odr-use.
  if (VD->hasLocalStorage()) {
    return VD->getDeclContext() ==
           dyn_cast_or_null<DeclContext>(CGF.CurCodeDecl);
  }

  // For a global declaration, we can emit a reference to it if we know
  // for sure that we are able to emit a definition of it.
  VD = VD->getDefinition(CGF.getContext());
  if (!VD)
    return false;

  // Don't emit a spurious reference if it might be to a variable that only
  // exists on a different device / target.
  // FIXME: This is unnecessarily broad. Check whether this would actually be a
  // cross-target reference.
  if (CGF.getLangOpts().OpenMP || CGF.getLangOpts().CUDA ||
      CGF.getLangOpts().OpenCL) {
    return false;
  }

  // We can emit a spurious reference only if the linkage implies that we'll
  // be emitting a non-interposable symbol that will be retained until link
  // time.
  switch (CGF.CGM.getLLVMLinkageVarDefinition(VD)) {
  case llvm::GlobalValue::ExternalLinkage:
  case llvm::GlobalValue::LinkOnceODRLinkage:
  case llvm::GlobalValue::WeakODRLinkage:
  case llvm::GlobalValue::InternalLinkage:
  case llvm::GlobalValue::PrivateLinkage:
    return true;
  default:
    return false;
  }
}

LValue CodeGenFunction::EmitDeclRefLValue(const DeclRefExpr *E) {
  const NamedDecl *ND = E->getDecl();
  QualType T = E->getType();

  assert(E->isNonOdrUse() != NOUR_Unevaluated &&
         "should not emit an unevaluated operand");

  if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    // Global Named registers access via intrinsics only
    if (VD->getStorageClass() == SC_Register &&
        VD->hasAttr<AsmLabelAttr>() && !VD->isLocalVarDecl())
      return EmitGlobalNamedRegister(VD, CGM);

    // If this DeclRefExpr does not constitute an odr-use of the variable,
    // we're not permitted to emit a reference to it in general, and it might
    // not be captured if capture would be necessary for a use. Emit the
    // constant value directly instead.
    if (E->isNonOdrUse() == NOUR_Constant &&
        (VD->getType()->isReferenceType() ||
         !canEmitSpuriousReferenceToVariable(*this, E, VD))) {
      VD->getAnyInitializer(VD);
      llvm::Constant *Val = ConstantEmitter(*this).emitAbstract(
          E->getLocation(), *VD->evaluateValue(), VD->getType());
      assert(Val && "failed to emit constant expression");

      Address Addr = Address::invalid();
      if (!VD->getType()->isReferenceType()) {
        // Spill the constant value to a global.
        Addr = CGM.createUnnamedGlobalFrom(*VD, Val,
                                           getContext().getDeclAlign(VD));
        llvm::Type *VarTy = getTypes().ConvertTypeForMem(VD->getType());
        auto *PTy = llvm::PointerType::get(
            VarTy, getTypes().getTargetAddressSpace(VD->getType()));
        Addr = Builder.CreatePointerBitCastOrAddrSpaceCast(Addr, PTy, VarTy);
      } else {
        // Should we be using the alignment of the constant pointer we emitted?
        CharUnits Alignment =
            CGM.getNaturalTypeAlignment(E->getType(),
                                        /* BaseInfo= */ nullptr,
                                        /* TBAAInfo= */ nullptr,
                                        /* forPointeeType= */ true);
        Addr = makeNaturalAddressForPointer(Val, T, Alignment);
      }
      return MakeAddrLValue(Addr, T, AlignmentSource::Decl);
    }

    // FIXME: Handle other kinds of non-odr-use DeclRefExprs.

    // Check for captured variables.
    if (E->refersToEnclosingVariableOrCapture()) {
      VD = VD->getCanonicalDecl();
      if (auto *FD = LambdaCaptureFields.lookup(VD))
        return EmitCapturedFieldLValue(*this, FD, CXXABIThisValue);
      if (CapturedStmtInfo) {
        auto I = LocalDeclMap.find(VD);
        if (I != LocalDeclMap.end()) {
          LValue CapLVal;
          if (VD->getType()->isReferenceType())
            CapLVal = EmitLoadOfReferenceLValue(I->second, VD->getType(),
                                                AlignmentSource::Decl);
          else
            CapLVal = MakeAddrLValue(I->second, T);
          // Mark lvalue as nontemporal if the variable is marked as nontemporal
          // in simd context.
          if (getLangOpts().OpenMP &&
              CGM.getOpenMPRuntime().isNontemporalDecl(VD))
            CapLVal.setNontemporal(/*Value=*/true);
          return CapLVal;
        }
        LValue CapLVal =
            EmitCapturedFieldLValue(*this, CapturedStmtInfo->lookup(VD),
                                    CapturedStmtInfo->getContextValue());
        Address LValueAddress = CapLVal.getAddress();
        CapLVal = MakeAddrLValue(Address(LValueAddress.emitRawPointer(*this),
                                         LValueAddress.getElementType(),
                                         getContext().getDeclAlign(VD)),
                                 CapLVal.getType(),
                                 LValueBaseInfo(AlignmentSource::Decl),
                                 CapLVal.getTBAAInfo());
        // Mark lvalue as nontemporal if the variable is marked as nontemporal
        // in simd context.
        if (getLangOpts().OpenMP &&
            CGM.getOpenMPRuntime().isNontemporalDecl(VD))
          CapLVal.setNontemporal(/*Value=*/true);
        return CapLVal;
      }

      assert(isa<BlockDecl>(CurCodeDecl));
      Address addr = GetAddrOfBlockDecl(VD);
      return MakeAddrLValue(addr, T, AlignmentSource::Decl);
    }
  }

  // FIXME: We should be able to assert this for FunctionDecls as well!
  // FIXME: We should be able to assert this for all DeclRefExprs, not just
  // those with a valid source location.
  assert((ND->isUsed(false) || !isa<VarDecl>(ND) || E->isNonOdrUse() ||
          !E->getLocation().isValid()) &&
         "Should not use decl without marking it used!");

  if (ND->hasAttr<WeakRefAttr>()) {
    const auto *VD = cast<ValueDecl>(ND);
    ConstantAddress Aliasee = CGM.GetWeakRefReference(VD);
    return MakeAddrLValue(Aliasee, T, AlignmentSource::Decl);
  }

  if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    // Check if this is a global variable.
    if (VD->hasLinkage() || VD->isStaticDataMember())
      return EmitGlobalVarDeclLValue(*this, E, VD);

    Address addr = Address::invalid();

    // The variable should generally be present in the local decl map.
    auto iter = LocalDeclMap.find(VD);
    if (iter != LocalDeclMap.end()) {
      addr = iter->second;

    // Otherwise, it might be static local we haven't emitted yet for
    // some reason; most likely, because it's in an outer function.
    } else if (VD->isStaticLocal()) {
      llvm::Constant *var = CGM.getOrCreateStaticVarDecl(
          *VD, CGM.getLLVMLinkageVarDefinition(VD));
      addr = Address(
          var, ConvertTypeForMem(VD->getType()), getContext().getDeclAlign(VD));

    // No other cases for now.
    } else {
      llvm_unreachable("DeclRefExpr for Decl not entered in LocalDeclMap?");
    }

    // Handle threadlocal function locals.
    if (VD->getTLSKind() != VarDecl::TLS_None)
      addr = addr.withPointer(
          Builder.CreateThreadLocalAddress(addr.getBasePointer()),
          NotKnownNonNull);

    // Check for OpenMP threadprivate variables.
    if (getLangOpts().OpenMP && !getLangOpts().OpenMPSimd &&
        VD->hasAttr<OMPThreadPrivateDeclAttr>()) {
      return EmitThreadPrivateVarDeclLValue(
          *this, VD, T, addr, getTypes().ConvertTypeForMem(VD->getType()),
          E->getExprLoc());
    }

    // Drill into block byref variables.
    bool isBlockByref = VD->isEscapingByref();
    if (isBlockByref) {
      addr = emitBlockByrefAddress(addr, VD);
    }

    // Drill into reference types.
    LValue LV = VD->getType()->isReferenceType() ?
        EmitLoadOfReferenceLValue(addr, VD->getType(), AlignmentSource::Decl) :
        MakeAddrLValue(addr, T, AlignmentSource::Decl);

    bool isLocalStorage = VD->hasLocalStorage();

    bool NonGCable = isLocalStorage &&
                     !VD->getType()->isReferenceType() &&
                     !isBlockByref;
    if (NonGCable) {
      LV.getQuals().removeObjCGCAttr();
      LV.setNonGC(true);
    }

    bool isImpreciseLifetime =
      (isLocalStorage && !VD->hasAttr<ObjCPreciseLifetimeAttr>());
    if (isImpreciseLifetime)
      LV.setARCPreciseLifetime(ARCImpreciseLifetime);
    setObjCGCLValueClass(getContext(), E, LV);
    return LV;
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    return EmitFunctionDeclLValue(*this, E, FD);

  // FIXME: While we're emitting a binding from an enclosing scope, all other
  // DeclRefExprs we see should be implicitly treated as if they also refer to
  // an enclosing scope.
  if (const auto *BD = dyn_cast<BindingDecl>(ND)) {
    if (E->refersToEnclosingVariableOrCapture()) {
      auto *FD = LambdaCaptureFields.lookup(BD);
      return EmitCapturedFieldLValue(*this, FD, CXXABIThisValue);
    }
    return EmitLValue(BD->getBinding());
  }

  // We can form DeclRefExprs naming GUID declarations when reconstituting
  // non-type template parameters into expressions.
  if (const auto *GD = dyn_cast<MSGuidDecl>(ND))
    return MakeAddrLValue(CGM.GetAddrOfMSGuidDecl(GD), T,
                          AlignmentSource::Decl);

  if (const auto *TPO = dyn_cast<TemplateParamObjectDecl>(ND)) {
    auto ATPO = CGM.GetAddrOfTemplateParamObject(TPO);
    auto AS = getLangASFromTargetAS(ATPO.getAddressSpace());

    if (AS != T.getAddressSpace()) {
      auto TargetAS = getContext().getTargetAddressSpace(T.getAddressSpace());
      auto PtrTy = ATPO.getElementType()->getPointerTo(TargetAS);
      auto ASC = getTargetHooks().performAddrSpaceCast(
          CGM, ATPO.getPointer(), AS, T.getAddressSpace(), PtrTy);
      ATPO = ConstantAddress(ASC, ATPO.getElementType(), ATPO.getAlignment());
    }

    return MakeAddrLValue(ATPO, T, AlignmentSource::Decl);
  }

  llvm_unreachable("Unhandled DeclRefExpr");
}

LValue CodeGenFunction::EmitUnaryOpLValue(const UnaryOperator *E) {
  // __extension__ doesn't affect lvalue-ness.
  if (E->getOpcode() == UO_Extension)
    return EmitLValue(E->getSubExpr());

  QualType ExprTy = getContext().getCanonicalType(E->getSubExpr()->getType());
  switch (E->getOpcode()) {
  default: llvm_unreachable("Unknown unary operator lvalue!");
  case UO_Deref: {
    QualType T = E->getSubExpr()->getType()->getPointeeType();
    assert(!T.isNull() && "CodeGenFunction::EmitUnaryOpLValue: Illegal type");

    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Addr = EmitPointerWithAlignment(E->getSubExpr(), &BaseInfo,
                                            &TBAAInfo);
    LValue LV = MakeAddrLValue(Addr, T, BaseInfo, TBAAInfo);
    LV.getQuals().setAddressSpace(ExprTy.getAddressSpace());

    // We should not generate __weak write barrier on indirect reference
    // of a pointer to object; as in void foo (__weak id *param); *param = 0;
    // But, we continue to generate __strong write barrier on indirect write
    // into a pointer to object.
    if (getLangOpts().ObjC &&
        getLangOpts().getGC() != LangOptions::NonGC &&
        LV.isObjCWeak())
      LV.setNonGC(!E->isOBJCGCCandidate(getContext()));
    return LV;
  }
  case UO_Real:
  case UO_Imag: {
    LValue LV = EmitLValue(E->getSubExpr());
    assert(LV.isSimple() && "real/imag on non-ordinary l-value");

    // __real is valid on scalars.  This is a faster way of testing that.
    // __imag can only produce an rvalue on scalars.
    if (E->getOpcode() == UO_Real &&
        !LV.getAddress().getElementType()->isStructTy()) {
      assert(E->getSubExpr()->getType()->isArithmeticType());
      return LV;
    }

    QualType T = ExprTy->castAs<ComplexType>()->getElementType();

    Address Component =
        (E->getOpcode() == UO_Real
             ? emitAddrOfRealComponent(LV.getAddress(), LV.getType())
             : emitAddrOfImagComponent(LV.getAddress(), LV.getType()));
    LValue ElemLV = MakeAddrLValue(Component, T, LV.getBaseInfo(),
                                   CGM.getTBAAInfoForSubobject(LV, T));
    ElemLV.getQuals().addQualifiers(LV.getQuals());
    return ElemLV;
  }
  case UO_PreInc:
  case UO_PreDec: {
    LValue LV = EmitLValue(E->getSubExpr());
    bool isInc = E->getOpcode() == UO_PreInc;

    if (E->getType()->isAnyComplexType())
      EmitComplexPrePostIncDec(E, LV, isInc, true/*isPre*/);
    else
      EmitScalarPrePostIncDec(E, LV, isInc, true/*isPre*/);
    return LV;
  }
  }
}

LValue CodeGenFunction::EmitStringLiteralLValue(const StringLiteral *E) {
  return MakeAddrLValue(CGM.GetAddrOfConstantStringFromLiteral(E),
                        E->getType(), AlignmentSource::Decl);
}

LValue CodeGenFunction::EmitObjCEncodeExprLValue(const ObjCEncodeExpr *E) {
  return MakeAddrLValue(CGM.GetAddrOfConstantStringFromObjCEncode(E),
                        E->getType(), AlignmentSource::Decl);
}

LValue CodeGenFunction::EmitPredefinedLValue(const PredefinedExpr *E) {
  auto SL = E->getFunctionName();
  assert(SL != nullptr && "No StringLiteral name in PredefinedExpr");
  StringRef FnName = CurFn->getName();
  if (FnName.starts_with("\01"))
    FnName = FnName.substr(1);
  StringRef NameItems[] = {
      PredefinedExpr::getIdentKindName(E->getIdentKind()), FnName};
  std::string GVName = llvm::join(NameItems, NameItems + 2, ".");
  if (auto *BD = dyn_cast_or_null<BlockDecl>(CurCodeDecl)) {
    std::string Name = std::string(SL->getString());
    if (!Name.empty()) {
      unsigned Discriminator =
          CGM.getCXXABI().getMangleContext().getBlockId(BD, true);
      if (Discriminator)
        Name += "_" + Twine(Discriminator + 1).str();
      auto C = CGM.GetAddrOfConstantCString(Name, GVName.c_str());
      return MakeAddrLValue(C, E->getType(), AlignmentSource::Decl);
    } else {
      auto C =
          CGM.GetAddrOfConstantCString(std::string(FnName), GVName.c_str());
      return MakeAddrLValue(C, E->getType(), AlignmentSource::Decl);
    }
  }
  auto C = CGM.GetAddrOfConstantStringFromLiteral(SL, GVName);
  return MakeAddrLValue(C, E->getType(), AlignmentSource::Decl);
}

/// Emit a type description suitable for use by a runtime sanitizer library. The
/// format of a type descriptor is
///
/// \code
///   { i16 TypeKind, i16 TypeInfo }
/// \endcode
///
/// followed by an array of i8 containing the type name. TypeKind is 0 for an
/// integer, 1 for a floating point value, and -1 for anything else.
llvm::Constant *CodeGenFunction::EmitCheckTypeDescriptor(QualType T) {
  // Only emit each type's descriptor once.
  if (llvm::Constant *C = CGM.getTypeDescriptorFromMap(T))
    return C;

  uint16_t TypeKind = -1;
  uint16_t TypeInfo = 0;

  if (T->isIntegerType()) {
    TypeKind = 0;
    TypeInfo = (llvm::Log2_32(getContext().getTypeSize(T)) << 1) |
               (T->isSignedIntegerType() ? 1 : 0);
  } else if (T->isFloatingType()) {
    TypeKind = 1;
    TypeInfo = getContext().getTypeSize(T);
  }

  // Format the type name as if for a diagnostic, including quotes and
  // optionally an 'aka'.
  SmallString<32> Buffer;
  CGM.getDiags().ConvertArgToString(
      DiagnosticsEngine::ak_qualtype, (intptr_t)T.getAsOpaquePtr(), StringRef(),
      StringRef(), std::nullopt, Buffer, std::nullopt);

  llvm::Constant *Components[] = {
    Builder.getInt16(TypeKind), Builder.getInt16(TypeInfo),
    llvm::ConstantDataArray::getString(getLLVMContext(), Buffer)
  };
  llvm::Constant *Descriptor = llvm::ConstantStruct::getAnon(Components);

  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), Descriptor->getType(),
      /*isConstant=*/true, llvm::GlobalVariable::PrivateLinkage, Descriptor);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  CGM.getSanitizerMetadata()->disableSanitizerForGlobal(GV);

  // Remember the descriptor for this type.
  CGM.setTypeDescriptorInMap(T, GV);

  return GV;
}

llvm::Value *CodeGenFunction::EmitCheckValue(llvm::Value *V) {
  llvm::Type *TargetTy = IntPtrTy;

  if (V->getType() == TargetTy)
    return V;

  // Floating-point types which fit into intptr_t are bitcast to integers
  // and then passed directly (after zero-extension, if necessary).
  if (V->getType()->isFloatingPointTy()) {
    unsigned Bits = V->getType()->getPrimitiveSizeInBits().getFixedValue();
    if (Bits <= TargetTy->getIntegerBitWidth())
      V = Builder.CreateBitCast(V, llvm::Type::getIntNTy(getLLVMContext(),
                                                         Bits));
  }

  // Integers which fit in intptr_t are zero-extended and passed directly.
  if (V->getType()->isIntegerTy() &&
      V->getType()->getIntegerBitWidth() <= TargetTy->getIntegerBitWidth())
    return Builder.CreateZExt(V, TargetTy);

  // Pointers are passed directly, everything else is passed by address.
  if (!V->getType()->isPointerTy()) {
    RawAddress Ptr = CreateDefaultAlignTempAlloca(V->getType());
    Builder.CreateStore(V, Ptr);
    V = Ptr.getPointer();
  }
  return Builder.CreatePtrToInt(V, TargetTy);
}

/// Emit a representation of a SourceLocation for passing to a handler
/// in a sanitizer runtime library. The format for this data is:
/// \code
///   struct SourceLocation {
///     const char *Filename;
///     int32_t Line, Column;
///   };
/// \endcode
/// For an invalid SourceLocation, the Filename pointer is null.
llvm::Constant *CodeGenFunction::EmitCheckSourceLocation(SourceLocation Loc) {
  llvm::Constant *Filename;
  int Line, Column;

  PresumedLoc PLoc = getContext().getSourceManager().getPresumedLoc(Loc);
  if (PLoc.isValid()) {
    StringRef FilenameString = PLoc.getFilename();

    int PathComponentsToStrip =
        CGM.getCodeGenOpts().EmitCheckPathComponentsToStrip;
    if (PathComponentsToStrip < 0) {
      assert(PathComponentsToStrip != INT_MIN);
      int PathComponentsToKeep = -PathComponentsToStrip;
      auto I = llvm::sys::path::rbegin(FilenameString);
      auto E = llvm::sys::path::rend(FilenameString);
      while (I != E && --PathComponentsToKeep)
        ++I;

      FilenameString = FilenameString.substr(I - E);
    } else if (PathComponentsToStrip > 0) {
      auto I = llvm::sys::path::begin(FilenameString);
      auto E = llvm::sys::path::end(FilenameString);
      while (I != E && PathComponentsToStrip--)
        ++I;

      if (I != E)
        FilenameString =
            FilenameString.substr(I - llvm::sys::path::begin(FilenameString));
      else
        FilenameString = llvm::sys::path::filename(FilenameString);
    }

    auto FilenameGV =
        CGM.GetAddrOfConstantCString(std::string(FilenameString), ".src");
    CGM.getSanitizerMetadata()->disableSanitizerForGlobal(
        cast<llvm::GlobalVariable>(
            FilenameGV.getPointer()->stripPointerCasts()));
    Filename = FilenameGV.getPointer();
    Line = PLoc.getLine();
    Column = PLoc.getColumn();
  } else {
    Filename = llvm::Constant::getNullValue(Int8PtrTy);
    Line = Column = 0;
  }

  llvm::Constant *Data[] = {Filename, Builder.getInt32(Line),
                            Builder.getInt32(Column)};

  return llvm::ConstantStruct::getAnon(Data);
}

namespace {
/// Specify under what conditions this check can be recovered
enum class CheckRecoverableKind {
  /// Always terminate program execution if this check fails.
  Unrecoverable,
  /// Check supports recovering, runtime has both fatal (noreturn) and
  /// non-fatal handlers for this check.
  Recoverable,
  /// Runtime conditionally aborts, always need to support recovery.
  AlwaysRecoverable
};
}

static CheckRecoverableKind getRecoverableKind(SanitizerMask Kind) {
  assert(Kind.countPopulation() == 1);
  if (Kind == SanitizerKind::Vptr)
    return CheckRecoverableKind::AlwaysRecoverable;
  else if (Kind == SanitizerKind::Return || Kind == SanitizerKind::Unreachable)
    return CheckRecoverableKind::Unrecoverable;
  else
    return CheckRecoverableKind::Recoverable;
}

namespace {
struct SanitizerHandlerInfo {
  char const *const Name;
  unsigned Version;
};
}

const SanitizerHandlerInfo SanitizerHandlers[] = {
#define SANITIZER_CHECK(Enum, Name, Version) {#Name, Version},
    LIST_SANITIZER_CHECKS
#undef SANITIZER_CHECK
};

static void emitCheckHandlerCall(CodeGenFunction &CGF,
                                 llvm::FunctionType *FnType,
                                 ArrayRef<llvm::Value *> FnArgs,
                                 SanitizerHandler CheckHandler,
                                 CheckRecoverableKind RecoverKind, bool IsFatal,
                                 llvm::BasicBlock *ContBB) {
  assert(IsFatal || RecoverKind != CheckRecoverableKind::Unrecoverable);
  std::optional<ApplyDebugLocation> DL;
  if (!CGF.Builder.getCurrentDebugLocation()) {
    // Ensure that the call has at least an artificial debug location.
    DL.emplace(CGF, SourceLocation());
  }
  bool NeedsAbortSuffix =
      IsFatal && RecoverKind != CheckRecoverableKind::Unrecoverable;
  bool MinimalRuntime = CGF.CGM.getCodeGenOpts().SanitizeMinimalRuntime;
  const SanitizerHandlerInfo &CheckInfo = SanitizerHandlers[CheckHandler];
  const StringRef CheckName = CheckInfo.Name;
  std::string FnName = "__ubsan_handle_" + CheckName.str();
  if (CheckInfo.Version && !MinimalRuntime)
    FnName += "_v" + llvm::utostr(CheckInfo.Version);
  if (MinimalRuntime)
    FnName += "_minimal";
  if (NeedsAbortSuffix)
    FnName += "_abort";
  bool MayReturn =
      !IsFatal || RecoverKind == CheckRecoverableKind::AlwaysRecoverable;

  llvm::AttrBuilder B(CGF.getLLVMContext());
  if (!MayReturn) {
    B.addAttribute(llvm::Attribute::NoReturn)
        .addAttribute(llvm::Attribute::NoUnwind);
  }
  B.addUWTableAttr(llvm::UWTableKind::Default);

  llvm::FunctionCallee Fn = CGF.CGM.CreateRuntimeFunction(
      FnType, FnName,
      llvm::AttributeList::get(CGF.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex, B),
      /*Local=*/true);
  llvm::CallInst *HandlerCall = CGF.EmitNounwindRuntimeCall(Fn, FnArgs);
  if (!MayReturn) {
    HandlerCall->setDoesNotReturn();
    CGF.Builder.CreateUnreachable();
  } else {
    CGF.Builder.CreateBr(ContBB);
  }
}

void CodeGenFunction::EmitCheck(
    ArrayRef<std::pair<llvm::Value *, SanitizerMask>> Checked,
    SanitizerHandler CheckHandler, ArrayRef<llvm::Constant *> StaticArgs,
    ArrayRef<llvm::Value *> DynamicArgs) {
  assert(IsSanitizerScope);
  assert(Checked.size() > 0);
  assert(CheckHandler >= 0 &&
         size_t(CheckHandler) < std::size(SanitizerHandlers));
  const StringRef CheckName = SanitizerHandlers[CheckHandler].Name;

  llvm::Value *FatalCond = nullptr;
  llvm::Value *RecoverableCond = nullptr;
  llvm::Value *TrapCond = nullptr;
  for (int i = 0, n = Checked.size(); i < n; ++i) {
    llvm::Value *Check = Checked[i].first;
    // -fsanitize-trap= overrides -fsanitize-recover=.
    llvm::Value *&Cond =
        CGM.getCodeGenOpts().SanitizeTrap.has(Checked[i].second)
            ? TrapCond
            : CGM.getCodeGenOpts().SanitizeRecover.has(Checked[i].second)
                  ? RecoverableCond
                  : FatalCond;
    Cond = Cond ? Builder.CreateAnd(Cond, Check) : Check;
  }

  if (ClSanitizeGuardChecks) {
    llvm::Value *Allow =
        Builder.CreateCall(CGM.getIntrinsic(llvm::Intrinsic::allow_ubsan_check),
                           llvm::ConstantInt::get(CGM.Int8Ty, CheckHandler));

    for (llvm::Value **Cond : {&FatalCond, &RecoverableCond, &TrapCond}) {
      if (*Cond)
        *Cond = Builder.CreateOr(*Cond, Builder.CreateNot(Allow));
    }
  }

  if (TrapCond)
    EmitTrapCheck(TrapCond, CheckHandler);
  if (!FatalCond && !RecoverableCond)
    return;

  llvm::Value *JointCond;
  if (FatalCond && RecoverableCond)
    JointCond = Builder.CreateAnd(FatalCond, RecoverableCond);
  else
    JointCond = FatalCond ? FatalCond : RecoverableCond;
  assert(JointCond);

  CheckRecoverableKind RecoverKind = getRecoverableKind(Checked[0].second);
  assert(SanOpts.has(Checked[0].second));
#ifndef NDEBUG
  for (int i = 1, n = Checked.size(); i < n; ++i) {
    assert(RecoverKind == getRecoverableKind(Checked[i].second) &&
           "All recoverable kinds in a single check must be same!");
    assert(SanOpts.has(Checked[i].second));
  }
#endif

  llvm::BasicBlock *Cont = createBasicBlock("cont");
  llvm::BasicBlock *Handlers = createBasicBlock("handler." + CheckName);
  llvm::Instruction *Branch = Builder.CreateCondBr(JointCond, Cont, Handlers);
  // Give hint that we very much don't expect to execute the handler
  llvm::MDBuilder MDHelper(getLLVMContext());
  llvm::MDNode *Node = MDHelper.createLikelyBranchWeights();
  Branch->setMetadata(llvm::LLVMContext::MD_prof, Node);
  EmitBlock(Handlers);

  // Handler functions take an i8* pointing to the (handler-specific) static
  // information block, followed by a sequence of intptr_t arguments
  // representing operand values.
  SmallVector<llvm::Value *, 4> Args;
  SmallVector<llvm::Type *, 4> ArgTypes;
  if (!CGM.getCodeGenOpts().SanitizeMinimalRuntime) {
    Args.reserve(DynamicArgs.size() + 1);
    ArgTypes.reserve(DynamicArgs.size() + 1);

    // Emit handler arguments and create handler function type.
    if (!StaticArgs.empty()) {
      llvm::Constant *Info = llvm::ConstantStruct::getAnon(StaticArgs);
      auto *InfoPtr = new llvm::GlobalVariable(
          CGM.getModule(), Info->getType(), false,
          llvm::GlobalVariable::PrivateLinkage, Info, "", nullptr,
          llvm::GlobalVariable::NotThreadLocal,
          CGM.getDataLayout().getDefaultGlobalsAddressSpace());
      InfoPtr->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
      CGM.getSanitizerMetadata()->disableSanitizerForGlobal(InfoPtr);
      Args.push_back(InfoPtr);
      ArgTypes.push_back(Args.back()->getType());
    }

    for (size_t i = 0, n = DynamicArgs.size(); i != n; ++i) {
      Args.push_back(EmitCheckValue(DynamicArgs[i]));
      ArgTypes.push_back(IntPtrTy);
    }
  }

  llvm::FunctionType *FnType =
    llvm::FunctionType::get(CGM.VoidTy, ArgTypes, false);

  if (!FatalCond || !RecoverableCond) {
    // Simple case: we need to generate a single handler call, either
    // fatal, or non-fatal.
    emitCheckHandlerCall(*this, FnType, Args, CheckHandler, RecoverKind,
                         (FatalCond != nullptr), Cont);
  } else {
    // Emit two handler calls: first one for set of unrecoverable checks,
    // another one for recoverable.
    llvm::BasicBlock *NonFatalHandlerBB =
        createBasicBlock("non_fatal." + CheckName);
    llvm::BasicBlock *FatalHandlerBB = createBasicBlock("fatal." + CheckName);
    Builder.CreateCondBr(FatalCond, NonFatalHandlerBB, FatalHandlerBB);
    EmitBlock(FatalHandlerBB);
    emitCheckHandlerCall(*this, FnType, Args, CheckHandler, RecoverKind, true,
                         NonFatalHandlerBB);
    EmitBlock(NonFatalHandlerBB);
    emitCheckHandlerCall(*this, FnType, Args, CheckHandler, RecoverKind, false,
                         Cont);
  }

  EmitBlock(Cont);
}

void CodeGenFunction::EmitCfiSlowPathCheck(
    SanitizerMask Kind, llvm::Value *Cond, llvm::ConstantInt *TypeId,
    llvm::Value *Ptr, ArrayRef<llvm::Constant *> StaticArgs) {
  llvm::BasicBlock *Cont = createBasicBlock("cfi.cont");

  llvm::BasicBlock *CheckBB = createBasicBlock("cfi.slowpath");
  llvm::BranchInst *BI = Builder.CreateCondBr(Cond, Cont, CheckBB);

  llvm::MDBuilder MDHelper(getLLVMContext());
  llvm::MDNode *Node = MDHelper.createLikelyBranchWeights();
  BI->setMetadata(llvm::LLVMContext::MD_prof, Node);

  EmitBlock(CheckBB);

  bool WithDiag = !CGM.getCodeGenOpts().SanitizeTrap.has(Kind);

  llvm::CallInst *CheckCall;
  llvm::FunctionCallee SlowPathFn;
  if (WithDiag) {
    llvm::Constant *Info = llvm::ConstantStruct::getAnon(StaticArgs);
    auto *InfoPtr =
        new llvm::GlobalVariable(CGM.getModule(), Info->getType(), false,
                                 llvm::GlobalVariable::PrivateLinkage, Info);
    InfoPtr->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    CGM.getSanitizerMetadata()->disableSanitizerForGlobal(InfoPtr);

    SlowPathFn = CGM.getModule().getOrInsertFunction(
        "__cfi_slowpath_diag",
        llvm::FunctionType::get(VoidTy, {Int64Ty, Int8PtrTy, Int8PtrTy},
                                false));
    CheckCall = Builder.CreateCall(SlowPathFn, {TypeId, Ptr, InfoPtr});
  } else {
    SlowPathFn = CGM.getModule().getOrInsertFunction(
        "__cfi_slowpath",
        llvm::FunctionType::get(VoidTy, {Int64Ty, Int8PtrTy}, false));
    CheckCall = Builder.CreateCall(SlowPathFn, {TypeId, Ptr});
  }

  CGM.setDSOLocal(
      cast<llvm::GlobalValue>(SlowPathFn.getCallee()->stripPointerCasts()));
  CheckCall->setDoesNotThrow();

  EmitBlock(Cont);
}

// Emit a stub for __cfi_check function so that the linker knows about this
// symbol in LTO mode.
void CodeGenFunction::EmitCfiCheckStub() {
  llvm::Module *M = &CGM.getModule();
  ASTContext &C = getContext();
  QualType QInt64Ty = C.getIntTypeForBitwidth(64, false);

  FunctionArgList FnArgs;
  ImplicitParamDecl ArgCallsiteTypeId(C, QInt64Ty, ImplicitParamKind::Other);
  ImplicitParamDecl ArgAddr(C, C.VoidPtrTy, ImplicitParamKind::Other);
  ImplicitParamDecl ArgCFICheckFailData(C, C.VoidPtrTy,
                                        ImplicitParamKind::Other);
  FnArgs.push_back(&ArgCallsiteTypeId);
  FnArgs.push_back(&ArgAddr);
  FnArgs.push_back(&ArgCFICheckFailData);
  const CGFunctionInfo &FI =
      CGM.getTypes().arrangeBuiltinFunctionDeclaration(C.VoidTy, FnArgs);

  llvm::Function *F = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, {Int64Ty, VoidPtrTy, VoidPtrTy}, false),
      llvm::GlobalValue::WeakAnyLinkage, "__cfi_check", M);
  CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, F, /*IsThunk=*/false);
  CGM.SetLLVMFunctionAttributesForDefinition(nullptr, F);
  F->setAlignment(llvm::Align(4096));
  CGM.setDSOLocal(F);

  llvm::LLVMContext &Ctx = M->getContext();
  llvm::BasicBlock *BB = llvm::BasicBlock::Create(Ctx, "entry", F);
  // CrossDSOCFI pass is not executed if there is no executable code.
  SmallVector<llvm::Value*> Args{F->getArg(2), F->getArg(1)};
  llvm::CallInst::Create(M->getFunction("__cfi_check_fail"), Args, "", BB);
  llvm::ReturnInst::Create(Ctx, nullptr, BB);
}

// This function is basically a switch over the CFI failure kind, which is
// extracted from CFICheckFailData (1st function argument). Each case is either
// llvm.trap or a call to one of the two runtime handlers, based on
// -fsanitize-trap and -fsanitize-recover settings.  Default case (invalid
// failure kind) traps, but this should really never happen.  CFICheckFailData
// can be nullptr if the calling module has -fsanitize-trap behavior for this
// check kind; in this case __cfi_check_fail traps as well.
void CodeGenFunction::EmitCfiCheckFail() {
  SanitizerScope SanScope(this);
  FunctionArgList Args;
  ImplicitParamDecl ArgData(getContext(), getContext().VoidPtrTy,
                            ImplicitParamKind::Other);
  ImplicitParamDecl ArgAddr(getContext(), getContext().VoidPtrTy,
                            ImplicitParamKind::Other);
  Args.push_back(&ArgData);
  Args.push_back(&ArgAddr);

  const CGFunctionInfo &FI =
    CGM.getTypes().arrangeBuiltinFunctionDeclaration(getContext().VoidTy, Args);

  llvm::Function *F = llvm::Function::Create(
      llvm::FunctionType::get(VoidTy, {VoidPtrTy, VoidPtrTy}, false),
      llvm::GlobalValue::WeakODRLinkage, "__cfi_check_fail", &CGM.getModule());

  CGM.SetLLVMFunctionAttributes(GlobalDecl(), FI, F, /*IsThunk=*/false);
  CGM.SetLLVMFunctionAttributesForDefinition(nullptr, F);
  F->setVisibility(llvm::GlobalValue::HiddenVisibility);

  StartFunction(GlobalDecl(), CGM.getContext().VoidTy, F, FI, Args,
                SourceLocation());

  // This function is not affected by NoSanitizeList. This function does
  // not have a source location, but "src:*" would still apply. Revert any
  // changes to SanOpts made in StartFunction.
  SanOpts = CGM.getLangOpts().Sanitize;

  llvm::Value *Data =
      EmitLoadOfScalar(GetAddrOfLocalVar(&ArgData), /*Volatile=*/false,
                       CGM.getContext().VoidPtrTy, ArgData.getLocation());
  llvm::Value *Addr =
      EmitLoadOfScalar(GetAddrOfLocalVar(&ArgAddr), /*Volatile=*/false,
                       CGM.getContext().VoidPtrTy, ArgAddr.getLocation());

  // Data == nullptr means the calling module has trap behaviour for this check.
  llvm::Value *DataIsNotNullPtr =
      Builder.CreateICmpNE(Data, llvm::ConstantPointerNull::get(Int8PtrTy));
  EmitTrapCheck(DataIsNotNullPtr, SanitizerHandler::CFICheckFail);

  llvm::StructType *SourceLocationTy =
      llvm::StructType::get(VoidPtrTy, Int32Ty, Int32Ty);
  llvm::StructType *CfiCheckFailDataTy =
      llvm::StructType::get(Int8Ty, SourceLocationTy, VoidPtrTy);

  llvm::Value *V = Builder.CreateConstGEP2_32(
      CfiCheckFailDataTy,
      Builder.CreatePointerCast(Data, CfiCheckFailDataTy->getPointerTo(0)), 0,
      0);

  Address CheckKindAddr(V, Int8Ty, getIntAlign());
  llvm::Value *CheckKind = Builder.CreateLoad(CheckKindAddr);

  llvm::Value *AllVtables = llvm::MetadataAsValue::get(
      CGM.getLLVMContext(),
      llvm::MDString::get(CGM.getLLVMContext(), "all-vtables"));
  llvm::Value *ValidVtable = Builder.CreateZExt(
      Builder.CreateCall(CGM.getIntrinsic(llvm::Intrinsic::type_test),
                         {Addr, AllVtables}),
      IntPtrTy);

  const std::pair<int, SanitizerMask> CheckKinds[] = {
      {CFITCK_VCall, SanitizerKind::CFIVCall},
      {CFITCK_NVCall, SanitizerKind::CFINVCall},
      {CFITCK_DerivedCast, SanitizerKind::CFIDerivedCast},
      {CFITCK_UnrelatedCast, SanitizerKind::CFIUnrelatedCast},
      {CFITCK_ICall, SanitizerKind::CFIICall}};

  SmallVector<std::pair<llvm::Value *, SanitizerMask>, 5> Checks;
  for (auto CheckKindMaskPair : CheckKinds) {
    int Kind = CheckKindMaskPair.first;
    SanitizerMask Mask = CheckKindMaskPair.second;
    llvm::Value *Cond =
        Builder.CreateICmpNE(CheckKind, llvm::ConstantInt::get(Int8Ty, Kind));
    if (CGM.getLangOpts().Sanitize.has(Mask))
      EmitCheck(std::make_pair(Cond, Mask), SanitizerHandler::CFICheckFail, {},
                {Data, Addr, ValidVtable});
    else
      EmitTrapCheck(Cond, SanitizerHandler::CFICheckFail);
  }

  FinishFunction();
  // The only reference to this function will be created during LTO link.
  // Make sure it survives until then.
  CGM.addUsedGlobal(F);
}

void CodeGenFunction::EmitUnreachable(SourceLocation Loc) {
  if (SanOpts.has(SanitizerKind::Unreachable)) {
    SanitizerScope SanScope(this);
    EmitCheck(std::make_pair(static_cast<llvm::Value *>(Builder.getFalse()),
                             SanitizerKind::Unreachable),
              SanitizerHandler::BuiltinUnreachable,
              EmitCheckSourceLocation(Loc), std::nullopt);
  }
  Builder.CreateUnreachable();
}

void CodeGenFunction::EmitTrapCheck(llvm::Value *Checked,
                                    SanitizerHandler CheckHandlerID) {
  llvm::BasicBlock *Cont = createBasicBlock("cont");

  // If we're optimizing, collapse all calls to trap down to just one per
  // check-type per function to save on code size.
  if ((int)TrapBBs.size() <= CheckHandlerID)
    TrapBBs.resize(CheckHandlerID + 1);

  llvm::BasicBlock *&TrapBB = TrapBBs[CheckHandlerID];

  if (!ClSanitizeDebugDeoptimization &&
      CGM.getCodeGenOpts().OptimizationLevel && TrapBB &&
      (!CurCodeDecl || !CurCodeDecl->hasAttr<OptimizeNoneAttr>())) {
    auto Call = TrapBB->begin();
    assert(isa<llvm::CallInst>(Call) && "Expected call in trap BB");

    Call->applyMergedLocation(Call->getDebugLoc(),
                              Builder.getCurrentDebugLocation());
    Builder.CreateCondBr(Checked, Cont, TrapBB);
  } else {
    TrapBB = createBasicBlock("trap");
    Builder.CreateCondBr(Checked, Cont, TrapBB);
    EmitBlock(TrapBB);

    llvm::CallInst *TrapCall = Builder.CreateCall(
        CGM.getIntrinsic(llvm::Intrinsic::ubsantrap),
        llvm::ConstantInt::get(CGM.Int8Ty,
                               ClSanitizeDebugDeoptimization
                                   ? TrapBB->getParent()->size()
                                   : static_cast<uint64_t>(CheckHandlerID)));

    if (!CGM.getCodeGenOpts().TrapFuncName.empty()) {
      auto A = llvm::Attribute::get(getLLVMContext(), "trap-func-name",
                                    CGM.getCodeGenOpts().TrapFuncName);
      TrapCall->addFnAttr(A);
    }
    TrapCall->setDoesNotReturn();
    TrapCall->setDoesNotThrow();
    Builder.CreateUnreachable();
  }

  EmitBlock(Cont);
}

llvm::CallInst *CodeGenFunction::EmitTrapCall(llvm::Intrinsic::ID IntrID) {
  llvm::CallInst *TrapCall =
      Builder.CreateCall(CGM.getIntrinsic(IntrID));

  if (!CGM.getCodeGenOpts().TrapFuncName.empty()) {
    auto A = llvm::Attribute::get(getLLVMContext(), "trap-func-name",
                                  CGM.getCodeGenOpts().TrapFuncName);
    TrapCall->addFnAttr(A);
  }

  return TrapCall;
}

Address CodeGenFunction::EmitArrayToPointerDecay(const Expr *E,
                                                 LValueBaseInfo *BaseInfo,
                                                 TBAAAccessInfo *TBAAInfo) {
  assert(E->getType()->isArrayType() &&
         "Array to pointer decay must have array source type!");

  // Expressions of array type can't be bitfields or vector elements.
  LValue LV = EmitLValue(E);
  Address Addr = LV.getAddress();

  // If the array type was an incomplete type, we need to make sure
  // the decay ends up being the right type.
  llvm::Type *NewTy = ConvertType(E->getType());
  Addr = Addr.withElementType(NewTy);

  // Note that VLA pointers are always decayed, so we don't need to do
  // anything here.
  if (!E->getType()->isVariableArrayType()) {
    assert(isa<llvm::ArrayType>(Addr.getElementType()) &&
           "Expected pointer to array");
    Addr = Builder.CreateConstArrayGEP(Addr, 0, "arraydecay");
  }

  // The result of this decay conversion points to an array element within the
  // base lvalue. However, since TBAA currently does not support representing
  // accesses to elements of member arrays, we conservatively represent accesses
  // to the pointee object as if it had no any base lvalue specified.
  // TODO: Support TBAA for member arrays.
  QualType EltType = E->getType()->castAsArrayTypeUnsafe()->getElementType();
  if (BaseInfo) *BaseInfo = LV.getBaseInfo();
  if (TBAAInfo) *TBAAInfo = CGM.getTBAAAccessInfo(EltType);

  return Addr.withElementType(ConvertTypeForMem(EltType));
}

/// isSimpleArrayDecayOperand - If the specified expr is a simple decay from an
/// array to pointer, return the array subexpression.
static const Expr *isSimpleArrayDecayOperand(const Expr *E) {
  // If this isn't just an array->pointer decay, bail out.
  const auto *CE = dyn_cast<CastExpr>(E);
  if (!CE || CE->getCastKind() != CK_ArrayToPointerDecay)
    return nullptr;

  // If this is a decay from variable width array, bail out.
  const Expr *SubExpr = CE->getSubExpr();
  if (SubExpr->getType()->isVariableArrayType())
    return nullptr;

  return SubExpr;
}

static llvm::Value *emitArraySubscriptGEP(CodeGenFunction &CGF,
                                          llvm::Type *elemType,
                                          llvm::Value *ptr,
                                          ArrayRef<llvm::Value*> indices,
                                          bool inbounds,
                                          bool signedIndices,
                                          SourceLocation loc,
                                    const llvm::Twine &name = "arrayidx") {
  if (inbounds) {
    return CGF.EmitCheckedInBoundsGEP(elemType, ptr, indices, signedIndices,
                                      CodeGenFunction::NotSubtraction, loc,
                                      name);
  } else {
    return CGF.Builder.CreateGEP(elemType, ptr, indices, name);
  }
}

static Address emitArraySubscriptGEP(CodeGenFunction &CGF, Address addr,
                                     ArrayRef<llvm::Value *> indices,
                                     llvm::Type *elementType, bool inbounds,
                                     bool signedIndices, SourceLocation loc,
                                     CharUnits align,
                                     const llvm::Twine &name = "arrayidx") {
  if (inbounds) {
    return CGF.EmitCheckedInBoundsGEP(addr, indices, elementType, signedIndices,
                                      CodeGenFunction::NotSubtraction, loc,
                                      align, name);
  } else {
    return CGF.Builder.CreateGEP(addr, indices, elementType, align, name);
  }
}

static CharUnits getArrayElementAlign(CharUnits arrayAlign,
                                      llvm::Value *idx,
                                      CharUnits eltSize) {
  // If we have a constant index, we can use the exact offset of the
  // element we're accessing.
  if (auto constantIdx = dyn_cast<llvm::ConstantInt>(idx)) {
    CharUnits offset = constantIdx->getZExtValue() * eltSize;
    return arrayAlign.alignmentAtOffset(offset);

  // Otherwise, use the worst-case alignment for any element.
  } else {
    return arrayAlign.alignmentOfArrayElement(eltSize);
  }
}

static QualType getFixedSizeElementType(const ASTContext &ctx,
                                        const VariableArrayType *vla) {
  QualType eltType;
  do {
    eltType = vla->getElementType();
  } while ((vla = ctx.getAsVariableArrayType(eltType)));
  return eltType;
}

static bool hasBPFPreserveStaticOffset(const RecordDecl *D) {
  return D && D->hasAttr<BPFPreserveStaticOffsetAttr>();
}

static bool hasBPFPreserveStaticOffset(const Expr *E) {
  if (!E)
    return false;
  QualType PointeeType = E->getType()->getPointeeType();
  if (PointeeType.isNull())
    return false;
  if (const auto *BaseDecl = PointeeType->getAsRecordDecl())
    return hasBPFPreserveStaticOffset(BaseDecl);
  return false;
}

// Wraps Addr with a call to llvm.preserve.static.offset intrinsic.
static Address wrapWithBPFPreserveStaticOffset(CodeGenFunction &CGF,
                                               Address &Addr) {
  if (!CGF.getTarget().getTriple().isBPF())
    return Addr;

  llvm::Function *Fn =
      CGF.CGM.getIntrinsic(llvm::Intrinsic::preserve_static_offset);
  llvm::CallInst *Call = CGF.Builder.CreateCall(Fn, {Addr.emitRawPointer(CGF)});
  return Address(Call, Addr.getElementType(), Addr.getAlignment());
}

/// Given an array base, check whether its member access belongs to a record
/// with preserve_access_index attribute or not.
static bool IsPreserveAIArrayBase(CodeGenFunction &CGF, const Expr *ArrayBase) {
  if (!ArrayBase || !CGF.getDebugInfo())
    return false;

  // Only support base as either a MemberExpr or DeclRefExpr.
  // DeclRefExpr to cover cases like:
  //    struct s { int a; int b[10]; };
  //    struct s *p;
  //    p[1].a
  // p[1] will generate a DeclRefExpr and p[1].a is a MemberExpr.
  // p->b[5] is a MemberExpr example.
  const Expr *E = ArrayBase->IgnoreImpCasts();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl()->hasAttr<BPFPreserveAccessIndexAttr>();

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const auto *VarDef = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VarDef)
      return false;

    const auto *PtrT = VarDef->getType()->getAs<PointerType>();
    if (!PtrT)
      return false;

    const auto *PointeeT = PtrT->getPointeeType()
                             ->getUnqualifiedDesugaredType();
    if (const auto *RecT = dyn_cast<RecordType>(PointeeT))
      return RecT->getDecl()->hasAttr<BPFPreserveAccessIndexAttr>();
    return false;
  }

  return false;
}

static Address emitArraySubscriptGEP(CodeGenFunction &CGF, Address addr,
                                     ArrayRef<llvm::Value *> indices,
                                     QualType eltType, bool inbounds,
                                     bool signedIndices, SourceLocation loc,
                                     QualType *arrayType = nullptr,
                                     const Expr *Base = nullptr,
                                     const llvm::Twine &name = "arrayidx") {
  // All the indices except that last must be zero.
#ifndef NDEBUG
  for (auto *idx : indices.drop_back())
    assert(isa<llvm::ConstantInt>(idx) &&
           cast<llvm::ConstantInt>(idx)->isZero());
#endif

  // Determine the element size of the statically-sized base.  This is
  // the thing that the indices are expressed in terms of.
  if (auto vla = CGF.getContext().getAsVariableArrayType(eltType)) {
    eltType = getFixedSizeElementType(CGF.getContext(), vla);
  }

  // We can use that to compute the best alignment of the element.
  CharUnits eltSize = CGF.getContext().getTypeSizeInChars(eltType);
  CharUnits eltAlign =
      getArrayElementAlign(addr.getAlignment(), indices.back(), eltSize);

  if (hasBPFPreserveStaticOffset(Base))
    addr = wrapWithBPFPreserveStaticOffset(CGF, addr);

  llvm::Value *eltPtr;
  auto LastIndex = dyn_cast<llvm::ConstantInt>(indices.back());
  if (!LastIndex ||
      (!CGF.IsInPreservedAIRegion && !IsPreserveAIArrayBase(CGF, Base))) {
    addr = emitArraySubscriptGEP(CGF, addr, indices,
                                 CGF.ConvertTypeForMem(eltType), inbounds,
                                 signedIndices, loc, eltAlign, name);
    return addr;
  } else {
    // Remember the original array subscript for bpf target
    unsigned idx = LastIndex->getZExtValue();
    llvm::DIType *DbgInfo = nullptr;
    if (arrayType)
      DbgInfo = CGF.getDebugInfo()->getOrCreateStandaloneType(*arrayType, loc);
    eltPtr = CGF.Builder.CreatePreserveArrayAccessIndex(
        addr.getElementType(), addr.emitRawPointer(CGF), indices.size() - 1,
        idx, DbgInfo);
  }

  return Address(eltPtr, CGF.ConvertTypeForMem(eltType), eltAlign);
}

/// The offset of a field from the beginning of the record.
static bool getFieldOffsetInBits(CodeGenFunction &CGF, const RecordDecl *RD,
                                 const FieldDecl *FD, int64_t &Offset) {
  ASTContext &Ctx = CGF.getContext();
  const ASTRecordLayout &Layout = Ctx.getASTRecordLayout(RD);
  unsigned FieldNo = 0;

  for (const Decl *D : RD->decls()) {
    if (const auto *Record = dyn_cast<RecordDecl>(D))
      if (getFieldOffsetInBits(CGF, Record, FD, Offset)) {
        Offset += Layout.getFieldOffset(FieldNo);
        return true;
      }

    if (const auto *Field = dyn_cast<FieldDecl>(D))
      if (FD == Field) {
        Offset += Layout.getFieldOffset(FieldNo);
        return true;
      }

    if (isa<FieldDecl>(D))
      ++FieldNo;
  }

  return false;
}

/// Returns the relative offset difference between \p FD1 and \p FD2.
/// \code
///   offsetof(struct foo, FD1) - offsetof(struct foo, FD2)
/// \endcode
/// Both fields must be within the same struct.
static std::optional<int64_t> getOffsetDifferenceInBits(CodeGenFunction &CGF,
                                                        const FieldDecl *FD1,
                                                        const FieldDecl *FD2) {
  const RecordDecl *FD1OuterRec =
      FD1->getParent()->getOuterLexicalRecordContext();
  const RecordDecl *FD2OuterRec =
      FD2->getParent()->getOuterLexicalRecordContext();

  if (FD1OuterRec != FD2OuterRec)
    // Fields must be within the same RecordDecl.
    return std::optional<int64_t>();

  int64_t FD1Offset = 0;
  if (!getFieldOffsetInBits(CGF, FD1OuterRec, FD1, FD1Offset))
    return std::optional<int64_t>();

  int64_t FD2Offset = 0;
  if (!getFieldOffsetInBits(CGF, FD2OuterRec, FD2, FD2Offset))
    return std::optional<int64_t>();

  return std::make_optional<int64_t>(FD1Offset - FD2Offset);
}

LValue CodeGenFunction::EmitArraySubscriptExpr(const ArraySubscriptExpr *E,
                                               bool Accessed) {
  // The index must always be an integer, which is not an aggregate.  Emit it
  // in lexical order (this complexity is, sadly, required by C++17).
  llvm::Value *IdxPre =
      (E->getLHS() == E->getIdx()) ? EmitScalarExpr(E->getIdx()) : nullptr;
  bool SignedIndices = false;
  auto EmitIdxAfterBase = [&, IdxPre](bool Promote) -> llvm::Value * {
    auto *Idx = IdxPre;
    if (E->getLHS() != E->getIdx()) {
      assert(E->getRHS() == E->getIdx() && "index was neither LHS nor RHS");
      Idx = EmitScalarExpr(E->getIdx());
    }

    QualType IdxTy = E->getIdx()->getType();
    bool IdxSigned = IdxTy->isSignedIntegerOrEnumerationType();
    SignedIndices |= IdxSigned;

    if (SanOpts.has(SanitizerKind::ArrayBounds))
      EmitBoundsCheck(E, E->getBase(), Idx, IdxTy, Accessed);

    // Extend or truncate the index type to 32 or 64-bits.
    if (Promote && Idx->getType() != IntPtrTy)
      Idx = Builder.CreateIntCast(Idx, IntPtrTy, IdxSigned, "idxprom");

    return Idx;
  };
  IdxPre = nullptr;

  // If the base is a vector type, then we are forming a vector element lvalue
  // with this subscript.
  if (E->getBase()->getType()->isSubscriptableVectorType() &&
      !isa<ExtVectorElementExpr>(E->getBase())) {
    // Emit the vector as an lvalue to get its address.
    LValue LHS = EmitLValue(E->getBase());
    auto *Idx = EmitIdxAfterBase(/*Promote*/false);
    assert(LHS.isSimple() && "Can only subscript lvalue vectors here!");
    return LValue::MakeVectorElt(LHS.getAddress(), Idx, E->getBase()->getType(),
                                 LHS.getBaseInfo(), TBAAAccessInfo());
  }

  // All the other cases basically behave like simple offsetting.

  // Handle the extvector case we ignored above.
  if (isa<ExtVectorElementExpr>(E->getBase())) {
    LValue LV = EmitLValue(E->getBase());
    auto *Idx = EmitIdxAfterBase(/*Promote*/true);
    Address Addr = EmitExtVectorElementLValue(LV);

    QualType EltType = LV.getType()->castAs<VectorType>()->getElementType();
    Addr = emitArraySubscriptGEP(*this, Addr, Idx, EltType, /*inbounds*/ true,
                                 SignedIndices, E->getExprLoc());
    return MakeAddrLValue(Addr, EltType, LV.getBaseInfo(),
                          CGM.getTBAAInfoForSubobject(LV, EltType));
  }

  LValueBaseInfo EltBaseInfo;
  TBAAAccessInfo EltTBAAInfo;
  Address Addr = Address::invalid();
  if (const VariableArrayType *vla =
           getContext().getAsVariableArrayType(E->getType())) {
    // The base must be a pointer, which is not an aggregate.  Emit
    // it.  It needs to be emitted first in case it's what captures
    // the VLA bounds.
    Addr = EmitPointerWithAlignment(E->getBase(), &EltBaseInfo, &EltTBAAInfo);
    auto *Idx = EmitIdxAfterBase(/*Promote*/true);

    // The element count here is the total number of non-VLA elements.
    llvm::Value *numElements = getVLASize(vla).NumElts;

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    if (getLangOpts().isSignedOverflowDefined()) {
      Idx = Builder.CreateMul(Idx, numElements);
    } else {
      Idx = Builder.CreateNSWMul(Idx, numElements);
    }

    Addr = emitArraySubscriptGEP(*this, Addr, Idx, vla->getElementType(),
                                 !getLangOpts().isSignedOverflowDefined(),
                                 SignedIndices, E->getExprLoc());

  } else if (const ObjCObjectType *OIT = E->getType()->getAs<ObjCObjectType>()){
    // Indexing over an interface, as in "NSString *P; P[4];"

    // Emit the base pointer.
    Addr = EmitPointerWithAlignment(E->getBase(), &EltBaseInfo, &EltTBAAInfo);
    auto *Idx = EmitIdxAfterBase(/*Promote*/true);

    CharUnits InterfaceSize = getContext().getTypeSizeInChars(OIT);
    llvm::Value *InterfaceSizeVal =
        llvm::ConstantInt::get(Idx->getType(), InterfaceSize.getQuantity());

    llvm::Value *ScaledIdx = Builder.CreateMul(Idx, InterfaceSizeVal);

    // We don't necessarily build correct LLVM struct types for ObjC
    // interfaces, so we can't rely on GEP to do this scaling
    // correctly, so we need to cast to i8*.  FIXME: is this actually
    // true?  A lot of other things in the fragile ABI would break...
    llvm::Type *OrigBaseElemTy = Addr.getElementType();

    // Do the GEP.
    CharUnits EltAlign =
      getArrayElementAlign(Addr.getAlignment(), Idx, InterfaceSize);
    llvm::Value *EltPtr =
        emitArraySubscriptGEP(*this, Int8Ty, Addr.emitRawPointer(*this),
                              ScaledIdx, false, SignedIndices, E->getExprLoc());
    Addr = Address(EltPtr, OrigBaseElemTy, EltAlign);
  } else if (const Expr *Array = isSimpleArrayDecayOperand(E->getBase())) {
    // If this is A[i] where A is an array, the frontend will have decayed the
    // base to be a ArrayToPointerDecay implicit cast.  While correct, it is
    // inefficient at -O0 to emit a "gep A, 0, 0" when codegen'ing it, then a
    // "gep x, i" here.  Emit one "gep A, 0, i".
    assert(Array->getType()->isArrayType() &&
           "Array to pointer decay must have array source type!");
    LValue ArrayLV;
    // For simple multidimensional array indexing, set the 'accessed' flag for
    // better bounds-checking of the base expression.
    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Array))
      ArrayLV = EmitArraySubscriptExpr(ASE, /*Accessed*/ true);
    else
      ArrayLV = EmitLValue(Array);
    auto *Idx = EmitIdxAfterBase(/*Promote*/true);

    if (SanOpts.has(SanitizerKind::ArrayBounds)) {
      // If the array being accessed has a "counted_by" attribute, generate
      // bounds checking code. The "count" field is at the top level of the
      // struct or in an anonymous struct, that's also at the top level. Future
      // expansions may allow the "count" to reside at any place in the struct,
      // but the value of "counted_by" will be a "simple" path to the count,
      // i.e. "a.b.count", so we shouldn't need the full force of EmitLValue or
      // similar to emit the correct GEP.
      const LangOptions::StrictFlexArraysLevelKind StrictFlexArraysLevel =
          getLangOpts().getStrictFlexArraysLevel();

      if (const auto *ME = dyn_cast<MemberExpr>(Array);
          ME &&
          ME->isFlexibleArrayMemberLike(getContext(), StrictFlexArraysLevel) &&
          ME->getMemberDecl()->getType()->isCountAttributedType()) {
        const FieldDecl *FAMDecl = dyn_cast<FieldDecl>(ME->getMemberDecl());
        if (const FieldDecl *CountFD = FindCountedByField(FAMDecl)) {
          if (std::optional<int64_t> Diff =
                  getOffsetDifferenceInBits(*this, CountFD, FAMDecl)) {
            CharUnits OffsetDiff = CGM.getContext().toCharUnitsFromBits(*Diff);

            // Create a GEP with a byte offset between the FAM and count and
            // use that to load the count value.
            Addr = Builder.CreatePointerBitCastOrAddrSpaceCast(
                ArrayLV.getAddress(), Int8PtrTy, Int8Ty);

            llvm::Type *CountTy = ConvertType(CountFD->getType());
            llvm::Value *Res = Builder.CreateInBoundsGEP(
                Int8Ty, Addr.emitRawPointer(*this),
                Builder.getInt32(OffsetDiff.getQuantity()), ".counted_by.gep");
            Res = Builder.CreateAlignedLoad(CountTy, Res, getIntAlign(),
                                            ".counted_by.load");

            // Now emit the bounds checking.
            EmitBoundsCheckImpl(E, Res, Idx, E->getIdx()->getType(),
                                Array->getType(), Accessed);
          }
        }
      }
    }

    // Propagate the alignment from the array itself to the result.
    QualType arrayType = Array->getType();
    Addr = emitArraySubscriptGEP(
        *this, ArrayLV.getAddress(), {CGM.getSize(CharUnits::Zero()), Idx},
        E->getType(), !getLangOpts().isSignedOverflowDefined(), SignedIndices,
        E->getExprLoc(), &arrayType, E->getBase());
    EltBaseInfo = ArrayLV.getBaseInfo();
    EltTBAAInfo = CGM.getTBAAInfoForSubobject(ArrayLV, E->getType());
  } else {
    // The base must be a pointer; emit it with an estimate of its alignment.
    Addr = EmitPointerWithAlignment(E->getBase(), &EltBaseInfo, &EltTBAAInfo);
    auto *Idx = EmitIdxAfterBase(/*Promote*/true);
    QualType ptrType = E->getBase()->getType();
    Addr = emitArraySubscriptGEP(*this, Addr, Idx, E->getType(),
                                 !getLangOpts().isSignedOverflowDefined(),
                                 SignedIndices, E->getExprLoc(), &ptrType,
                                 E->getBase());
  }

  LValue LV = MakeAddrLValue(Addr, E->getType(), EltBaseInfo, EltTBAAInfo);

  if (getLangOpts().ObjC &&
      getLangOpts().getGC() != LangOptions::NonGC) {
    LV.setNonGC(!E->isOBJCGCCandidate(getContext()));
    setObjCGCLValueClass(getContext(), E, LV);
  }
  return LV;
}

LValue CodeGenFunction::EmitMatrixSubscriptExpr(const MatrixSubscriptExpr *E) {
  assert(
      !E->isIncomplete() &&
      "incomplete matrix subscript expressions should be rejected during Sema");
  LValue Base = EmitLValue(E->getBase());
  llvm::Value *RowIdx = EmitScalarExpr(E->getRowIdx());
  llvm::Value *ColIdx = EmitScalarExpr(E->getColumnIdx());
  llvm::Value *NumRows = Builder.getIntN(
      RowIdx->getType()->getScalarSizeInBits(),
      E->getBase()->getType()->castAs<ConstantMatrixType>()->getNumRows());
  llvm::Value *FinalIdx =
      Builder.CreateAdd(Builder.CreateMul(ColIdx, NumRows), RowIdx);
  return LValue::MakeMatrixElt(
      MaybeConvertMatrixAddress(Base.getAddress(), *this), FinalIdx,
      E->getBase()->getType(), Base.getBaseInfo(), TBAAAccessInfo());
}

static Address emitOMPArraySectionBase(CodeGenFunction &CGF, const Expr *Base,
                                       LValueBaseInfo &BaseInfo,
                                       TBAAAccessInfo &TBAAInfo,
                                       QualType BaseTy, QualType ElTy,
                                       bool IsLowerBound) {
  LValue BaseLVal;
  if (auto *ASE = dyn_cast<ArraySectionExpr>(Base->IgnoreParenImpCasts())) {
    BaseLVal = CGF.EmitArraySectionExpr(ASE, IsLowerBound);
    if (BaseTy->isArrayType()) {
      Address Addr = BaseLVal.getAddress();
      BaseInfo = BaseLVal.getBaseInfo();

      // If the array type was an incomplete type, we need to make sure
      // the decay ends up being the right type.
      llvm::Type *NewTy = CGF.ConvertType(BaseTy);
      Addr = Addr.withElementType(NewTy);

      // Note that VLA pointers are always decayed, so we don't need to do
      // anything here.
      if (!BaseTy->isVariableArrayType()) {
        assert(isa<llvm::ArrayType>(Addr.getElementType()) &&
               "Expected pointer to array");
        Addr = CGF.Builder.CreateConstArrayGEP(Addr, 0, "arraydecay");
      }

      return Addr.withElementType(CGF.ConvertTypeForMem(ElTy));
    }
    LValueBaseInfo TypeBaseInfo;
    TBAAAccessInfo TypeTBAAInfo;
    CharUnits Align =
        CGF.CGM.getNaturalTypeAlignment(ElTy, &TypeBaseInfo, &TypeTBAAInfo);
    BaseInfo.mergeForCast(TypeBaseInfo);
    TBAAInfo = CGF.CGM.mergeTBAAInfoForCast(TBAAInfo, TypeTBAAInfo);
    return Address(CGF.Builder.CreateLoad(BaseLVal.getAddress()),
                   CGF.ConvertTypeForMem(ElTy), Align);
  }
  return CGF.EmitPointerWithAlignment(Base, &BaseInfo, &TBAAInfo);
}

LValue CodeGenFunction::EmitArraySectionExpr(const ArraySectionExpr *E,
                                             bool IsLowerBound) {

  assert(!E->isOpenACCArraySection() &&
         "OpenACC Array section codegen not implemented");

  QualType BaseTy = ArraySectionExpr::getBaseOriginalType(E->getBase());
  QualType ResultExprTy;
  if (auto *AT = getContext().getAsArrayType(BaseTy))
    ResultExprTy = AT->getElementType();
  else
    ResultExprTy = BaseTy->getPointeeType();
  llvm::Value *Idx = nullptr;
  if (IsLowerBound || E->getColonLocFirst().isInvalid()) {
    // Requesting lower bound or upper bound, but without provided length and
    // without ':' symbol for the default length -> length = 1.
    // Idx = LowerBound ?: 0;
    if (auto *LowerBound = E->getLowerBound()) {
      Idx = Builder.CreateIntCast(
          EmitScalarExpr(LowerBound), IntPtrTy,
          LowerBound->getType()->hasSignedIntegerRepresentation());
    } else
      Idx = llvm::ConstantInt::getNullValue(IntPtrTy);
  } else {
    // Try to emit length or lower bound as constant. If this is possible, 1
    // is subtracted from constant length or lower bound. Otherwise, emit LLVM
    // IR (LB + Len) - 1.
    auto &C = CGM.getContext();
    auto *Length = E->getLength();
    llvm::APSInt ConstLength;
    if (Length) {
      // Idx = LowerBound + Length - 1;
      if (std::optional<llvm::APSInt> CL = Length->getIntegerConstantExpr(C)) {
        ConstLength = CL->zextOrTrunc(PointerWidthInBits);
        Length = nullptr;
      }
      auto *LowerBound = E->getLowerBound();
      llvm::APSInt ConstLowerBound(PointerWidthInBits, /*isUnsigned=*/false);
      if (LowerBound) {
        if (std::optional<llvm::APSInt> LB =
                LowerBound->getIntegerConstantExpr(C)) {
          ConstLowerBound = LB->zextOrTrunc(PointerWidthInBits);
          LowerBound = nullptr;
        }
      }
      if (!Length)
        --ConstLength;
      else if (!LowerBound)
        --ConstLowerBound;

      if (Length || LowerBound) {
        auto *LowerBoundVal =
            LowerBound
                ? Builder.CreateIntCast(
                      EmitScalarExpr(LowerBound), IntPtrTy,
                      LowerBound->getType()->hasSignedIntegerRepresentation())
                : llvm::ConstantInt::get(IntPtrTy, ConstLowerBound);
        auto *LengthVal =
            Length
                ? Builder.CreateIntCast(
                      EmitScalarExpr(Length), IntPtrTy,
                      Length->getType()->hasSignedIntegerRepresentation())
                : llvm::ConstantInt::get(IntPtrTy, ConstLength);
        Idx = Builder.CreateAdd(LowerBoundVal, LengthVal, "lb_add_len",
                                /*HasNUW=*/false,
                                !getLangOpts().isSignedOverflowDefined());
        if (Length && LowerBound) {
          Idx = Builder.CreateSub(
              Idx, llvm::ConstantInt::get(IntPtrTy, /*V=*/1), "idx_sub_1",
              /*HasNUW=*/false, !getLangOpts().isSignedOverflowDefined());
        }
      } else
        Idx = llvm::ConstantInt::get(IntPtrTy, ConstLength + ConstLowerBound);
    } else {
      // Idx = ArraySize - 1;
      QualType ArrayTy = BaseTy->isPointerType()
                             ? E->getBase()->IgnoreParenImpCasts()->getType()
                             : BaseTy;
      if (auto *VAT = C.getAsVariableArrayType(ArrayTy)) {
        Length = VAT->getSizeExpr();
        if (std::optional<llvm::APSInt> L = Length->getIntegerConstantExpr(C)) {
          ConstLength = *L;
          Length = nullptr;
        }
      } else {
        auto *CAT = C.getAsConstantArrayType(ArrayTy);
        assert(CAT && "unexpected type for array initializer");
        ConstLength = CAT->getSize();
      }
      if (Length) {
        auto *LengthVal = Builder.CreateIntCast(
            EmitScalarExpr(Length), IntPtrTy,
            Length->getType()->hasSignedIntegerRepresentation());
        Idx = Builder.CreateSub(
            LengthVal, llvm::ConstantInt::get(IntPtrTy, /*V=*/1), "len_sub_1",
            /*HasNUW=*/false, !getLangOpts().isSignedOverflowDefined());
      } else {
        ConstLength = ConstLength.zextOrTrunc(PointerWidthInBits);
        --ConstLength;
        Idx = llvm::ConstantInt::get(IntPtrTy, ConstLength);
      }
    }
  }
  assert(Idx);

  Address EltPtr = Address::invalid();
  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  if (auto *VLA = getContext().getAsVariableArrayType(ResultExprTy)) {
    // The base must be a pointer, which is not an aggregate.  Emit
    // it.  It needs to be emitted first in case it's what captures
    // the VLA bounds.
    Address Base =
        emitOMPArraySectionBase(*this, E->getBase(), BaseInfo, TBAAInfo,
                                BaseTy, VLA->getElementType(), IsLowerBound);
    // The element count here is the total number of non-VLA elements.
    llvm::Value *NumElements = getVLASize(VLA).NumElts;

    // Effectively, the multiply by the VLA size is part of the GEP.
    // GEP indexes are signed, and scaling an index isn't permitted to
    // signed-overflow, so we use the same semantics for our explicit
    // multiply.  We suppress this if overflow is not undefined behavior.
    if (getLangOpts().isSignedOverflowDefined())
      Idx = Builder.CreateMul(Idx, NumElements);
    else
      Idx = Builder.CreateNSWMul(Idx, NumElements);
    EltPtr = emitArraySubscriptGEP(*this, Base, Idx, VLA->getElementType(),
                                   !getLangOpts().isSignedOverflowDefined(),
                                   /*signedIndices=*/false, E->getExprLoc());
  } else if (const Expr *Array = isSimpleArrayDecayOperand(E->getBase())) {
    // If this is A[i] where A is an array, the frontend will have decayed the
    // base to be a ArrayToPointerDecay implicit cast.  While correct, it is
    // inefficient at -O0 to emit a "gep A, 0, 0" when codegen'ing it, then a
    // "gep x, i" here.  Emit one "gep A, 0, i".
    assert(Array->getType()->isArrayType() &&
           "Array to pointer decay must have array source type!");
    LValue ArrayLV;
    // For simple multidimensional array indexing, set the 'accessed' flag for
    // better bounds-checking of the base expression.
    if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Array))
      ArrayLV = EmitArraySubscriptExpr(ASE, /*Accessed*/ true);
    else
      ArrayLV = EmitLValue(Array);

    // Propagate the alignment from the array itself to the result.
    EltPtr = emitArraySubscriptGEP(
        *this, ArrayLV.getAddress(), {CGM.getSize(CharUnits::Zero()), Idx},
        ResultExprTy, !getLangOpts().isSignedOverflowDefined(),
        /*signedIndices=*/false, E->getExprLoc());
    BaseInfo = ArrayLV.getBaseInfo();
    TBAAInfo = CGM.getTBAAInfoForSubobject(ArrayLV, ResultExprTy);
  } else {
    Address Base =
        emitOMPArraySectionBase(*this, E->getBase(), BaseInfo, TBAAInfo, BaseTy,
                                ResultExprTy, IsLowerBound);
    EltPtr = emitArraySubscriptGEP(*this, Base, Idx, ResultExprTy,
                                   !getLangOpts().isSignedOverflowDefined(),
                                   /*signedIndices=*/false, E->getExprLoc());
  }

  return MakeAddrLValue(EltPtr, ResultExprTy, BaseInfo, TBAAInfo);
}

LValue CodeGenFunction::
EmitExtVectorElementExpr(const ExtVectorElementExpr *E) {
  // Emit the base vector as an l-value.
  LValue Base;

  // ExtVectorElementExpr's base can either be a vector or pointer to vector.
  if (E->isArrow()) {
    // If it is a pointer to a vector, emit the address and form an lvalue with
    // it.
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Ptr = EmitPointerWithAlignment(E->getBase(), &BaseInfo, &TBAAInfo);
    const auto *PT = E->getBase()->getType()->castAs<PointerType>();
    Base = MakeAddrLValue(Ptr, PT->getPointeeType(), BaseInfo, TBAAInfo);
    Base.getQuals().removeObjCGCAttr();
  } else if (E->getBase()->isGLValue()) {
    // Otherwise, if the base is an lvalue ( as in the case of foo.x.x),
    // emit the base as an lvalue.
    assert(E->getBase()->getType()->isVectorType());
    Base = EmitLValue(E->getBase());
  } else {
    // Otherwise, the base is a normal rvalue (as in (V+V).x), emit it as such.
    assert(E->getBase()->getType()->isVectorType() &&
           "Result must be a vector");
    llvm::Value *Vec = EmitScalarExpr(E->getBase());

    // Store the vector to memory (because LValue wants an address).
    Address VecMem = CreateMemTemp(E->getBase()->getType());
    Builder.CreateStore(Vec, VecMem);
    Base = MakeAddrLValue(VecMem, E->getBase()->getType(),
                          AlignmentSource::Decl);
  }

  QualType type =
    E->getType().withCVRQualifiers(Base.getQuals().getCVRQualifiers());

  // Encode the element access list into a vector of unsigned indices.
  SmallVector<uint32_t, 4> Indices;
  E->getEncodedElementAccess(Indices);

  if (Base.isSimple()) {
    llvm::Constant *CV =
        llvm::ConstantDataVector::get(getLLVMContext(), Indices);
    return LValue::MakeExtVectorElt(Base.getAddress(), CV, type,
                                    Base.getBaseInfo(), TBAAAccessInfo());
  }
  assert(Base.isExtVectorElt() && "Can only subscript lvalue vec elts here!");

  llvm::Constant *BaseElts = Base.getExtVectorElts();
  SmallVector<llvm::Constant *, 4> CElts;

  for (unsigned i = 0, e = Indices.size(); i != e; ++i)
    CElts.push_back(BaseElts->getAggregateElement(Indices[i]));
  llvm::Constant *CV = llvm::ConstantVector::get(CElts);
  return LValue::MakeExtVectorElt(Base.getExtVectorAddress(), CV, type,
                                  Base.getBaseInfo(), TBAAAccessInfo());
}

LValue CodeGenFunction::EmitMemberExpr(const MemberExpr *E) {
  if (DeclRefExpr *DRE = tryToConvertMemberExprToDeclRefExpr(*this, E)) {
    EmitIgnoredExpr(E->getBase());
    return EmitDeclRefLValue(DRE);
  }

  Expr *BaseExpr = E->getBase();
  // If this is s.x, emit s as an lvalue.  If it is s->x, emit s as a scalar.
  LValue BaseLV;
  if (E->isArrow()) {
    LValueBaseInfo BaseInfo;
    TBAAAccessInfo TBAAInfo;
    Address Addr = EmitPointerWithAlignment(BaseExpr, &BaseInfo, &TBAAInfo);
    QualType PtrTy = BaseExpr->getType()->getPointeeType();
    SanitizerSet SkippedChecks;
    bool IsBaseCXXThis = IsWrappedCXXThis(BaseExpr);
    if (IsBaseCXXThis)
      SkippedChecks.set(SanitizerKind::Alignment, true);
    if (IsBaseCXXThis || isa<DeclRefExpr>(BaseExpr))
      SkippedChecks.set(SanitizerKind::Null, true);
    EmitTypeCheck(TCK_MemberAccess, E->getExprLoc(), Addr, PtrTy,
                  /*Alignment=*/CharUnits::Zero(), SkippedChecks);
    BaseLV = MakeAddrLValue(Addr, PtrTy, BaseInfo, TBAAInfo);
  } else
    BaseLV = EmitCheckedLValue(BaseExpr, TCK_MemberAccess);

  NamedDecl *ND = E->getMemberDecl();
  if (auto *Field = dyn_cast<FieldDecl>(ND)) {
    LValue LV = EmitLValueForField(BaseLV, Field);
    setObjCGCLValueClass(getContext(), E, LV);
    if (getLangOpts().OpenMP) {
      // If the member was explicitly marked as nontemporal, mark it as
      // nontemporal. If the base lvalue is marked as nontemporal, mark access
      // to children as nontemporal too.
      if ((IsWrappedCXXThis(BaseExpr) &&
           CGM.getOpenMPRuntime().isNontemporalDecl(Field)) ||
          BaseLV.isNontemporal())
        LV.setNontemporal(/*Value=*/true);
    }
    return LV;
  }

  if (const auto *FD = dyn_cast<FunctionDecl>(ND))
    return EmitFunctionDeclLValue(*this, E, FD);

  llvm_unreachable("Unhandled member declaration!");
}

/// Given that we are currently emitting a lambda, emit an l-value for
/// one of its members.
///
LValue CodeGenFunction::EmitLValueForLambdaField(const FieldDecl *Field,
                                                 llvm::Value *ThisValue) {
  bool HasExplicitObjectParameter = false;
  const auto *MD = dyn_cast_if_present<CXXMethodDecl>(CurCodeDecl);
  if (MD) {
    HasExplicitObjectParameter = MD->isExplicitObjectMemberFunction();
    assert(MD->getParent()->isLambda());
    assert(MD->getParent() == Field->getParent());
  }
  LValue LambdaLV;
  if (HasExplicitObjectParameter) {
    const VarDecl *D = cast<CXXMethodDecl>(CurCodeDecl)->getParamDecl(0);
    auto It = LocalDeclMap.find(D);
    assert(It != LocalDeclMap.end() && "explicit parameter not loaded?");
    Address AddrOfExplicitObject = It->getSecond();
    if (D->getType()->isReferenceType())
      LambdaLV = EmitLoadOfReferenceLValue(AddrOfExplicitObject, D->getType(),
                                           AlignmentSource::Decl);
    else
      LambdaLV = MakeAddrLValue(AddrOfExplicitObject,
                                D->getType().getNonReferenceType());

    // Make sure we have an lvalue to the lambda itself and not a derived class.
    auto *ThisTy = D->getType().getNonReferenceType()->getAsCXXRecordDecl();
    auto *LambdaTy = cast<CXXRecordDecl>(Field->getParent());
    if (ThisTy != LambdaTy) {
      const CXXCastPath &BasePathArray = getContext().LambdaCastPaths.at(MD);
      Address Base = GetAddressOfBaseClass(
          LambdaLV.getAddress(), ThisTy, BasePathArray.begin(),
          BasePathArray.end(), /*NullCheckValue=*/false, SourceLocation());
      LambdaLV = MakeAddrLValue(Base, QualType{LambdaTy->getTypeForDecl(), 0});
    }
  } else {
    QualType LambdaTagType = getContext().getTagDeclType(Field->getParent());
    LambdaLV = MakeNaturalAlignAddrLValue(ThisValue, LambdaTagType);
  }
  return EmitLValueForField(LambdaLV, Field);
}

LValue CodeGenFunction::EmitLValueForLambdaField(const FieldDecl *Field) {
  return EmitLValueForLambdaField(Field, CXXABIThisValue);
}

/// Get the field index in the debug info. The debug info structure/union
/// will ignore the unnamed bitfields.
unsigned CodeGenFunction::getDebugInfoFIndex(const RecordDecl *Rec,
                                             unsigned FieldIndex) {
  unsigned I = 0, Skipped = 0;

  for (auto *F : Rec->getDefinition()->fields()) {
    if (I == FieldIndex)
      break;
    if (F->isUnnamedBitField())
      Skipped++;
    I++;
  }

  return FieldIndex - Skipped;
}

/// Get the address of a zero-sized field within a record. The resulting
/// address doesn't necessarily have the right type.
static Address emitAddrOfZeroSizeField(CodeGenFunction &CGF, Address Base,
                                       const FieldDecl *Field) {
  CharUnits Offset = CGF.getContext().toCharUnitsFromBits(
      CGF.getContext().getFieldOffset(Field));
  if (Offset.isZero())
    return Base;
  Base = Base.withElementType(CGF.Int8Ty);
  return CGF.Builder.CreateConstInBoundsByteGEP(Base, Offset);
}

/// Drill down to the storage of a field without walking into
/// reference types.
///
/// The resulting address doesn't necessarily have the right type.
static Address emitAddrOfFieldStorage(CodeGenFunction &CGF, Address base,
                                      const FieldDecl *field) {
  if (isEmptyFieldForLayout(CGF.getContext(), field))
    return emitAddrOfZeroSizeField(CGF, base, field);

  const RecordDecl *rec = field->getParent();

  unsigned idx =
    CGF.CGM.getTypes().getCGRecordLayout(rec).getLLVMFieldNo(field);

  return CGF.Builder.CreateStructGEP(base, idx, field->getName());
}

static Address emitPreserveStructAccess(CodeGenFunction &CGF, LValue base,
                                        Address addr, const FieldDecl *field) {
  const RecordDecl *rec = field->getParent();
  llvm::DIType *DbgInfo = CGF.getDebugInfo()->getOrCreateStandaloneType(
      base.getType(), rec->getLocation());

  unsigned idx =
      CGF.CGM.getTypes().getCGRecordLayout(rec).getLLVMFieldNo(field);

  return CGF.Builder.CreatePreserveStructAccessIndex(
      addr, idx, CGF.getDebugInfoFIndex(rec, field->getFieldIndex()), DbgInfo);
}

static bool hasAnyVptr(const QualType Type, const ASTContext &Context) {
  const auto *RD = Type.getTypePtr()->getAsCXXRecordDecl();
  if (!RD)
    return false;

  if (RD->isDynamicClass())
    return true;

  for (const auto &Base : RD->bases())
    if (hasAnyVptr(Base.getType(), Context))
      return true;

  for (const FieldDecl *Field : RD->fields())
    if (hasAnyVptr(Field->getType(), Context))
      return true;

  return false;
}

LValue CodeGenFunction::EmitLValueForField(LValue base,
                                           const FieldDecl *field) {
  LValueBaseInfo BaseInfo = base.getBaseInfo();

  if (field->isBitField()) {
    const CGRecordLayout &RL =
        CGM.getTypes().getCGRecordLayout(field->getParent());
    const CGBitFieldInfo &Info = RL.getBitFieldInfo(field);
    const bool UseVolatile = isAAPCS(CGM.getTarget()) &&
                             CGM.getCodeGenOpts().AAPCSBitfieldWidth &&
                             Info.VolatileStorageSize != 0 &&
                             field->getType()
                                 .withCVRQualifiers(base.getVRQualifiers())
                                 .isVolatileQualified();
    Address Addr = base.getAddress();
    unsigned Idx = RL.getLLVMFieldNo(field);
    const RecordDecl *rec = field->getParent();
    if (hasBPFPreserveStaticOffset(rec))
      Addr = wrapWithBPFPreserveStaticOffset(*this, Addr);
    if (!UseVolatile) {
      if (!IsInPreservedAIRegion &&
          (!getDebugInfo() || !rec->hasAttr<BPFPreserveAccessIndexAttr>())) {
        if (Idx != 0)
          // For structs, we GEP to the field that the record layout suggests.
          Addr = Builder.CreateStructGEP(Addr, Idx, field->getName());
      } else {
        llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateRecordType(
            getContext().getRecordType(rec), rec->getLocation());
        Addr = Builder.CreatePreserveStructAccessIndex(
            Addr, Idx, getDebugInfoFIndex(rec, field->getFieldIndex()),
            DbgInfo);
      }
    }
    const unsigned SS =
        UseVolatile ? Info.VolatileStorageSize : Info.StorageSize;
    // Get the access type.
    llvm::Type *FieldIntTy = llvm::Type::getIntNTy(getLLVMContext(), SS);
    Addr = Addr.withElementType(FieldIntTy);
    if (UseVolatile) {
      const unsigned VolatileOffset = Info.VolatileStorageOffset.getQuantity();
      if (VolatileOffset)
        Addr = Builder.CreateConstInBoundsGEP(Addr, VolatileOffset);
    }

    QualType fieldType =
        field->getType().withCVRQualifiers(base.getVRQualifiers());
    // TODO: Support TBAA for bit fields.
    LValueBaseInfo FieldBaseInfo(BaseInfo.getAlignmentSource());
    return LValue::MakeBitfield(Addr, Info, fieldType, FieldBaseInfo,
                                TBAAAccessInfo());
  }

  // Fields of may-alias structures are may-alias themselves.
  // FIXME: this should get propagated down through anonymous structs
  // and unions.
  QualType FieldType = field->getType();
  const RecordDecl *rec = field->getParent();
  AlignmentSource BaseAlignSource = BaseInfo.getAlignmentSource();
  LValueBaseInfo FieldBaseInfo(getFieldAlignmentSource(BaseAlignSource));
  TBAAAccessInfo FieldTBAAInfo;
  if (base.getTBAAInfo().isMayAlias() ||
          rec->hasAttr<MayAliasAttr>() || FieldType->isVectorType()) {
    FieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else if (rec->isUnion()) {
    // TODO: Support TBAA for unions.
    FieldTBAAInfo = TBAAAccessInfo::getMayAliasInfo();
  } else {
    // If no base type been assigned for the base access, then try to generate
    // one for this base lvalue.
    FieldTBAAInfo = base.getTBAAInfo();
    if (!FieldTBAAInfo.BaseType) {
        FieldTBAAInfo.BaseType = CGM.getTBAABaseTypeInfo(base.getType());
        assert(!FieldTBAAInfo.Offset &&
               "Nonzero offset for an access with no base type!");
    }

    // Adjust offset to be relative to the base type.
    const ASTRecordLayout &Layout =
        getContext().getASTRecordLayout(field->getParent());
    unsigned CharWidth = getContext().getCharWidth();
    if (FieldTBAAInfo.BaseType)
      FieldTBAAInfo.Offset +=
          Layout.getFieldOffset(field->getFieldIndex()) / CharWidth;

    // Update the final access type and size.
    FieldTBAAInfo.AccessType = CGM.getTBAATypeInfo(FieldType);
    FieldTBAAInfo.Size =
        getContext().getTypeSizeInChars(FieldType).getQuantity();
  }

  Address addr = base.getAddress();
  if (hasBPFPreserveStaticOffset(rec))
    addr = wrapWithBPFPreserveStaticOffset(*this, addr);
  if (auto *ClassDef = dyn_cast<CXXRecordDecl>(rec)) {
    if (CGM.getCodeGenOpts().StrictVTablePointers &&
        ClassDef->isDynamicClass()) {
      // Getting to any field of dynamic object requires stripping dynamic
      // information provided by invariant.group.  This is because accessing
      // fields may leak the real address of dynamic object, which could result
      // in miscompilation when leaked pointer would be compared.
      auto *stripped =
          Builder.CreateStripInvariantGroup(addr.emitRawPointer(*this));
      addr = Address(stripped, addr.getElementType(), addr.getAlignment());
    }
  }

  unsigned RecordCVR = base.getVRQualifiers();
  if (rec->isUnion()) {
    // For unions, there is no pointer adjustment.
    if (CGM.getCodeGenOpts().StrictVTablePointers &&
        hasAnyVptr(FieldType, getContext()))
      // Because unions can easily skip invariant.barriers, we need to add
      // a barrier every time CXXRecord field with vptr is referenced.
      addr = Builder.CreateLaunderInvariantGroup(addr);

    if (IsInPreservedAIRegion ||
        (getDebugInfo() && rec->hasAttr<BPFPreserveAccessIndexAttr>())) {
      // Remember the original union field index
      llvm::DIType *DbgInfo = getDebugInfo()->getOrCreateStandaloneType(base.getType(),
          rec->getLocation());
      addr =
          Address(Builder.CreatePreserveUnionAccessIndex(
                      addr.emitRawPointer(*this),
                      getDebugInfoFIndex(rec, field->getFieldIndex()), DbgInfo),
                  addr.getElementType(), addr.getAlignment());
    }

    if (FieldType->isReferenceType())
      addr = addr.withElementType(CGM.getTypes().ConvertTypeForMem(FieldType));
  } else {
    if (!IsInPreservedAIRegion &&
        (!getDebugInfo() || !rec->hasAttr<BPFPreserveAccessIndexAttr>()))
      // For structs, we GEP to the field that the record layout suggests.
      addr = emitAddrOfFieldStorage(*this, addr, field);
    else
      // Remember the original struct field index
      addr = emitPreserveStructAccess(*this, base, addr, field);
  }

  // If this is a reference field, load the reference right now.
  if (FieldType->isReferenceType()) {
    LValue RefLVal =
        MakeAddrLValue(addr, FieldType, FieldBaseInfo, FieldTBAAInfo);
    if (RecordCVR & Qualifiers::Volatile)
      RefLVal.getQuals().addVolatile();
    addr = EmitLoadOfReference(RefLVal, &FieldBaseInfo, &FieldTBAAInfo);

    // Qualifiers on the struct don't apply to the referencee.
    RecordCVR = 0;
    FieldType = FieldType->getPointeeType();
  }

  // Make sure that the address is pointing to the right type.  This is critical
  // for both unions and structs.
  addr = addr.withElementType(CGM.getTypes().ConvertTypeForMem(FieldType));

  if (field->hasAttr<AnnotateAttr>())
    addr = EmitFieldAnnotations(field, addr);

  LValue LV = MakeAddrLValue(addr, FieldType, FieldBaseInfo, FieldTBAAInfo);
  LV.getQuals().addCVRQualifiers(RecordCVR);

  // __weak attribute on a field is ignored.
  if (LV.getQuals().getObjCGCAttr() == Qualifiers::Weak)
    LV.getQuals().removeObjCGCAttr();

  return LV;
}

LValue
CodeGenFunction::EmitLValueForFieldInitialization(LValue Base,
                                                  const FieldDecl *Field) {
  QualType FieldType = Field->getType();

  if (!FieldType->isReferenceType())
    return EmitLValueForField(Base, Field);

  Address V = emitAddrOfFieldStorage(*this, Base.getAddress(), Field);

  // Make sure that the address is pointing to the right type.
  llvm::Type *llvmType = ConvertTypeForMem(FieldType);
  V = V.withElementType(llvmType);

  // TODO: Generate TBAA information that describes this access as a structure
  // member access and not just an access to an object of the field's type. This
  // should be similar to what we do in EmitLValueForField().
  LValueBaseInfo BaseInfo = Base.getBaseInfo();
  AlignmentSource FieldAlignSource = BaseInfo.getAlignmentSource();
  LValueBaseInfo FieldBaseInfo(getFieldAlignmentSource(FieldAlignSource));
  return MakeAddrLValue(V, FieldType, FieldBaseInfo,
                        CGM.getTBAAInfoForSubobject(Base, FieldType));
}

LValue CodeGenFunction::EmitCompoundLiteralLValue(const CompoundLiteralExpr *E){
  if (E->isFileScope()) {
    ConstantAddress GlobalPtr = CGM.GetAddrOfConstantCompoundLiteral(E);
    return MakeAddrLValue(GlobalPtr, E->getType(), AlignmentSource::Decl);
  }
  if (E->getType()->isVariablyModifiedType())
    // make sure to emit the VLA size.
    EmitVariablyModifiedType(E->getType());

  Address DeclPtr = CreateMemTemp(E->getType(), ".compoundliteral");
  const Expr *InitExpr = E->getInitializer();
  LValue Result = MakeAddrLValue(DeclPtr, E->getType(), AlignmentSource::Decl);

  EmitAnyExprToMem(InitExpr, DeclPtr, E->getType().getQualifiers(),
                   /*Init*/ true);

  // Block-scope compound literals are destroyed at the end of the enclosing
  // scope in C.
  if (!getLangOpts().CPlusPlus)
    if (QualType::DestructionKind DtorKind = E->getType().isDestructedType())
      pushLifetimeExtendedDestroy(getCleanupKind(DtorKind), DeclPtr,
                                  E->getType(), getDestroyer(DtorKind),
                                  DtorKind & EHCleanup);

  return Result;
}

LValue CodeGenFunction::EmitInitListLValue(const InitListExpr *E) {
  if (!E->isGLValue())
    // Initializing an aggregate temporary in C++11: T{...}.
    return EmitAggExprToLValue(E);

  // An lvalue initializer list must be initializing a reference.
  assert(E->isTransparent() && "non-transparent glvalue init list");
  return EmitLValue(E->getInit(0));
}

/// Emit the operand of a glvalue conditional operator. This is either a glvalue
/// or a (possibly-parenthesized) throw-expression. If this is a throw, no
/// LValue is returned and the current block has been terminated.
static std::optional<LValue> EmitLValueOrThrowExpression(CodeGenFunction &CGF,
                                                         const Expr *Operand) {
  if (auto *ThrowExpr = dyn_cast<CXXThrowExpr>(Operand->IgnoreParens())) {
    CGF.EmitCXXThrowExpr(ThrowExpr, /*KeepInsertionPoint*/false);
    return std::nullopt;
  }

  return CGF.EmitLValue(Operand);
}

namespace {
// Handle the case where the condition is a constant evaluatable simple integer,
// which means we don't have to separately handle the true/false blocks.
std::optional<LValue> HandleConditionalOperatorLValueSimpleCase(
    CodeGenFunction &CGF, const AbstractConditionalOperator *E) {
  const Expr *condExpr = E->getCond();
  bool CondExprBool;
  if (CGF.ConstantFoldsToSimpleInteger(condExpr, CondExprBool)) {
    const Expr *Live = E->getTrueExpr(), *Dead = E->getFalseExpr();
    if (!CondExprBool)
      std::swap(Live, Dead);

    if (!CGF.ContainsLabel(Dead)) {
      // If the true case is live, we need to track its region.
      if (CondExprBool)
        CGF.incrementProfileCounter(E);
      // If a throw expression we emit it and return an undefined lvalue
      // because it can't be used.
      if (auto *ThrowExpr = dyn_cast<CXXThrowExpr>(Live->IgnoreParens())) {
        CGF.EmitCXXThrowExpr(ThrowExpr);
        llvm::Type *ElemTy = CGF.ConvertType(Dead->getType());
        llvm::Type *Ty = CGF.UnqualPtrTy;
        return CGF.MakeAddrLValue(
            Address(llvm::UndefValue::get(Ty), ElemTy, CharUnits::One()),
            Dead->getType());
      }
      return CGF.EmitLValue(Live);
    }
  }
  return std::nullopt;
}
struct ConditionalInfo {
  llvm::BasicBlock *lhsBlock, *rhsBlock;
  std::optional<LValue> LHS, RHS;
};

// Create and generate the 3 blocks for a conditional operator.
// Leaves the 'current block' in the continuation basic block.
template<typename FuncTy>
ConditionalInfo EmitConditionalBlocks(CodeGenFunction &CGF,
                                      const AbstractConditionalOperator *E,
                                      const FuncTy &BranchGenFunc) {
  ConditionalInfo Info{CGF.createBasicBlock("cond.true"),
                       CGF.createBasicBlock("cond.false"), std::nullopt,
                       std::nullopt};
  llvm::BasicBlock *endBlock = CGF.createBasicBlock("cond.end");

  CodeGenFunction::ConditionalEvaluation eval(CGF);
  CGF.EmitBranchOnBoolExpr(E->getCond(), Info.lhsBlock, Info.rhsBlock,
                           CGF.getProfileCount(E));

  // Any temporaries created here are conditional.
  CGF.EmitBlock(Info.lhsBlock);
  CGF.incrementProfileCounter(E);
  eval.begin(CGF);
  Info.LHS = BranchGenFunc(CGF, E->getTrueExpr());
  eval.end(CGF);
  Info.lhsBlock = CGF.Builder.GetInsertBlock();

  if (Info.LHS)
    CGF.Builder.CreateBr(endBlock);

  // Any temporaries created here are conditional.
  CGF.EmitBlock(Info.rhsBlock);
  eval.begin(CGF);
  Info.RHS = BranchGenFunc(CGF, E->getFalseExpr());
  eval.end(CGF);
  Info.rhsBlock = CGF.Builder.GetInsertBlock();
  CGF.EmitBlock(endBlock);

  return Info;
}
} // namespace

void CodeGenFunction::EmitIgnoredConditionalOperator(
    const AbstractConditionalOperator *E) {
  if (!E->isGLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(E->getType()) &&
           "Unexpected conditional operator!");
    return (void)EmitAggExprToLValue(E);
  }

  OpaqueValueMapping binding(*this, E);
  if (HandleConditionalOperatorLValueSimpleCase(*this, E))
    return;

  EmitConditionalBlocks(*this, E, [](CodeGenFunction &CGF, const Expr *E) {
    CGF.EmitIgnoredExpr(E);
    return LValue{};
  });
}
LValue CodeGenFunction::EmitConditionalOperatorLValue(
    const AbstractConditionalOperator *expr) {
  if (!expr->isGLValue()) {
    // ?: here should be an aggregate.
    assert(hasAggregateEvaluationKind(expr->getType()) &&
           "Unexpected conditional operator!");
    return EmitAggExprToLValue(expr);
  }

  OpaqueValueMapping binding(*this, expr);
  if (std::optional<LValue> Res =
          HandleConditionalOperatorLValueSimpleCase(*this, expr))
    return *Res;

  ConditionalInfo Info = EmitConditionalBlocks(
      *this, expr, [](CodeGenFunction &CGF, const Expr *E) {
        return EmitLValueOrThrowExpression(CGF, E);
      });

  if ((Info.LHS && !Info.LHS->isSimple()) ||
      (Info.RHS && !Info.RHS->isSimple()))
    return EmitUnsupportedLValue(expr, "conditional operator");

  if (Info.LHS && Info.RHS) {
    Address lhsAddr = Info.LHS->getAddress();
    Address rhsAddr = Info.RHS->getAddress();
    Address result = mergeAddressesInConditionalExpr(
        lhsAddr, rhsAddr, Info.lhsBlock, Info.rhsBlock,
        Builder.GetInsertBlock(), expr->getType());
    AlignmentSource alignSource =
        std::max(Info.LHS->getBaseInfo().getAlignmentSource(),
                 Info.RHS->getBaseInfo().getAlignmentSource());
    TBAAAccessInfo TBAAInfo = CGM.mergeTBAAInfoForConditionalOperator(
        Info.LHS->getTBAAInfo(), Info.RHS->getTBAAInfo());
    return MakeAddrLValue(result, expr->getType(), LValueBaseInfo(alignSource),
                          TBAAInfo);
  } else {
    assert((Info.LHS || Info.RHS) &&
           "both operands of glvalue conditional are throw-expressions?");
    return Info.LHS ? *Info.LHS : *Info.RHS;
  }
}

/// EmitCastLValue - Casts are never lvalues unless that cast is to a reference
/// type. If the cast is to a reference, we can have the usual lvalue result,
/// otherwise if a cast is needed by the code generator in an lvalue context,
/// then it must mean that we need the address of an aggregate in order to
/// access one of its members.  This can happen for all the reasons that casts
/// are permitted with aggregate result, including noop aggregate casts, and
/// cast from scalar to union.
LValue CodeGenFunction::EmitCastLValue(const CastExpr *E) {
  switch (E->getCastKind()) {
  case CK_ToVoid:
  case CK_BitCast:
  case CK_LValueToRValueBitCast:
  case CK_ArrayToPointerDecay:
  case CK_FunctionToPointerDecay:
  case CK_NullToMemberPointer:
  case CK_NullToPointer:
  case CK_IntegralToPointer:
  case CK_PointerToIntegral:
  case CK_PointerToBoolean:
  case CK_IntegralCast:
  case CK_BooleanToSignedIntegral:
  case CK_IntegralToBoolean:
  case CK_IntegralToFloating:
  case CK_FloatingToIntegral:
  case CK_FloatingToBoolean:
  case CK_FloatingCast:
  case CK_FloatingRealToComplex:
  case CK_FloatingComplexToReal:
  case CK_FloatingComplexToBoolean:
  case CK_FloatingComplexCast:
  case CK_FloatingComplexToIntegralComplex:
  case CK_IntegralRealToComplex:
  case CK_IntegralComplexToReal:
  case CK_IntegralComplexToBoolean:
  case CK_IntegralComplexCast:
  case CK_IntegralComplexToFloatingComplex:
  case CK_DerivedToBaseMemberPointer:
  case CK_BaseToDerivedMemberPointer:
  case CK_MemberPointerToBoolean:
  case CK_ReinterpretMemberPointer:
  case CK_AnyPointerToBlockPointerCast:
  case CK_ARCProduceObject:
  case CK_ARCConsumeObject:
  case CK_ARCReclaimReturnedObject:
  case CK_ARCExtendBlockObject:
  case CK_CopyAndAutoreleaseBlockObject:
  case CK_IntToOCLSampler:
  case CK_FloatingToFixedPoint:
  case CK_FixedPointToFloating:
  case CK_FixedPointCast:
  case CK_FixedPointToBoolean:
  case CK_FixedPointToIntegral:
  case CK_IntegralToFixedPoint:
  case CK_MatrixCast:
  case CK_HLSLVectorTruncation:
  case CK_HLSLArrayRValue:
    return EmitUnsupportedLValue(E, "unexpected cast lvalue");

  case CK_Dependent:
    llvm_unreachable("dependent cast kind in IR gen!");

  case CK_BuiltinFnToFnPtr:
    llvm_unreachable("builtin functions are handled elsewhere");

  // These are never l-values; just use the aggregate emission code.
  case CK_NonAtomicToAtomic:
  case CK_AtomicToNonAtomic:
    return EmitAggExprToLValue(E);

  case CK_Dynamic: {
    LValue LV = EmitLValue(E->getSubExpr());
    Address V = LV.getAddress();
    const auto *DCE = cast<CXXDynamicCastExpr>(E);
    return MakeNaturalAlignRawAddrLValue(EmitDynamicCast(V, DCE), E->getType());
  }

  case CK_ConstructorConversion:
  case CK_UserDefinedConversion:
  case CK_CPointerToObjCPointerCast:
  case CK_BlockPointerToObjCPointerCast:
  case CK_LValueToRValue:
    return EmitLValue(E->getSubExpr());

  case CK_NoOp: {
    // CK_NoOp can model a qualification conversion, which can remove an array
    // bound and change the IR type.
    // FIXME: Once pointee types are removed from IR, remove this.
    LValue LV = EmitLValue(E->getSubExpr());
    // Propagate the volatile qualifer to LValue, if exist in E.
    if (E->changesVolatileQualification())
      LV.getQuals() = E->getType().getQualifiers();
    if (LV.isSimple()) {
      Address V = LV.getAddress();
      if (V.isValid()) {
        llvm::Type *T = ConvertTypeForMem(E->getType());
        if (V.getElementType() != T)
          LV.setAddress(V.withElementType(T));
      }
    }
    return LV;
  }

  case CK_UncheckedDerivedToBase:
  case CK_DerivedToBase: {
    const auto *DerivedClassTy =
        E->getSubExpr()->getType()->castAs<RecordType>();
    auto *DerivedClassDecl = cast<CXXRecordDecl>(DerivedClassTy->getDecl());

    LValue LV = EmitLValue(E->getSubExpr());
    Address This = LV.getAddress();

    // Perform the derived-to-base conversion
    Address Base = GetAddressOfBaseClass(
        This, DerivedClassDecl, E->path_begin(), E->path_end(),
        /*NullCheckValue=*/false, E->getExprLoc());

    // TODO: Support accesses to members of base classes in TBAA. For now, we
    // conservatively pretend that the complete object is of the base class
    // type.
    return MakeAddrLValue(Base, E->getType(), LV.getBaseInfo(),
                          CGM.getTBAAInfoForSubobject(LV, E->getType()));
  }
  case CK_ToUnion:
    return EmitAggExprToLValue(E);
  case CK_BaseToDerived: {
    const auto *DerivedClassTy = E->getType()->castAs<RecordType>();
    auto *DerivedClassDecl = cast<CXXRecordDecl>(DerivedClassTy->getDecl());

    LValue LV = EmitLValue(E->getSubExpr());

    // Perform the base-to-derived conversion
    Address Derived = GetAddressOfDerivedClass(
        LV.getAddress(), DerivedClassDecl, E->path_begin(), E->path_end(),
        /*NullCheckValue=*/false);

    // C++11 [expr.static.cast]p2: Behavior is undefined if a downcast is
    // performed and the object is not of the derived type.
    if (sanitizePerformTypeCheck())
      EmitTypeCheck(TCK_DowncastReference, E->getExprLoc(), Derived,
                    E->getType());

    if (SanOpts.has(SanitizerKind::CFIDerivedCast))
      EmitVTablePtrCheckForCast(E->getType(), Derived,
                                /*MayBeNull=*/false, CFITCK_DerivedCast,
                                E->getBeginLoc());

    return MakeAddrLValue(Derived, E->getType(), LV.getBaseInfo(),
                          CGM.getTBAAInfoForSubobject(LV, E->getType()));
  }
  case CK_LValueBitCast: {
    // This must be a reinterpret_cast (or c-style equivalent).
    const auto *CE = cast<ExplicitCastExpr>(E);

    CGM.EmitExplicitCastExprType(CE, this);
    LValue LV = EmitLValue(E->getSubExpr());
    Address V = LV.getAddress().withElementType(
        ConvertTypeForMem(CE->getTypeAsWritten()->getPointeeType()));

    if (SanOpts.has(SanitizerKind::CFIUnrelatedCast))
      EmitVTablePtrCheckForCast(E->getType(), V,
                                /*MayBeNull=*/false, CFITCK_UnrelatedCast,
                                E->getBeginLoc());

    return MakeAddrLValue(V, E->getType(), LV.getBaseInfo(),
                          CGM.getTBAAInfoForSubobject(LV, E->getType()));
  }
  case CK_AddressSpaceConversion: {
    LValue LV = EmitLValue(E->getSubExpr());
    QualType DestTy = getContext().getPointerType(E->getType());
    llvm::Value *V = getTargetHooks().performAddrSpaceCast(
        *this, LV.getPointer(*this),
        E->getSubExpr()->getType().getAddressSpace(),
        E->getType().getAddressSpace(), ConvertType(DestTy));
    return MakeAddrLValue(Address(V, ConvertTypeForMem(E->getType()),
                                  LV.getAddress().getAlignment()),
                          E->getType(), LV.getBaseInfo(), LV.getTBAAInfo());
  }
  case CK_ObjCObjectLValueCast: {
    LValue LV = EmitLValue(E->getSubExpr());
    Address V = LV.getAddress().withElementType(ConvertType(E->getType()));
    return MakeAddrLValue(V, E->getType(), LV.getBaseInfo(),
                          CGM.getTBAAInfoForSubobject(LV, E->getType()));
  }
  case CK_ZeroToOCLOpaqueType:
    llvm_unreachable("NULL to OpenCL opaque type lvalue cast is not valid");

  case CK_VectorSplat: {
    // LValue results of vector splats are only supported in HLSL.
    if (!getLangOpts().HLSL)
      return EmitUnsupportedLValue(E, "unexpected cast lvalue");
    return EmitLValue(E->getSubExpr());
  }
  }

  llvm_unreachable("Unhandled lvalue cast kind?");
}

LValue CodeGenFunction::EmitOpaqueValueLValue(const OpaqueValueExpr *e) {
  assert(OpaqueValueMappingData::shouldBindAsLValue(e));
  return getOrCreateOpaqueLValueMapping(e);
}

LValue
CodeGenFunction::getOrCreateOpaqueLValueMapping(const OpaqueValueExpr *e) {
  assert(OpaqueValueMapping::shouldBindAsLValue(e));

  llvm::DenseMap<const OpaqueValueExpr*,LValue>::iterator
      it = OpaqueLValues.find(e);

  if (it != OpaqueLValues.end())
    return it->second;

  assert(e->isUnique() && "LValue for a nonunique OVE hasn't been emitted");
  return EmitLValue(e->getSourceExpr());
}

RValue
CodeGenFunction::getOrCreateOpaqueRValueMapping(const OpaqueValueExpr *e) {
  assert(!OpaqueValueMapping::shouldBindAsLValue(e));

  llvm::DenseMap<const OpaqueValueExpr*,RValue>::iterator
      it = OpaqueRValues.find(e);

  if (it != OpaqueRValues.end())
    return it->second;

  assert(e->isUnique() && "RValue for a nonunique OVE hasn't been emitted");
  return EmitAnyExpr(e->getSourceExpr());
}

RValue CodeGenFunction::EmitRValueForField(LValue LV,
                                           const FieldDecl *FD,
                                           SourceLocation Loc) {
  QualType FT = FD->getType();
  LValue FieldLV = EmitLValueForField(LV, FD);
  switch (getEvaluationKind(FT)) {
  case TEK_Complex:
    return RValue::getComplex(EmitLoadOfComplex(FieldLV, Loc));
  case TEK_Aggregate:
    return FieldLV.asAggregateRValue();
  case TEK_Scalar:
    // This routine is used to load fields one-by-one to perform a copy, so
    // don't load reference fields.
    if (FD->getType()->isReferenceType())
      return RValue::get(FieldLV.getPointer(*this));
    // Call EmitLoadOfScalar except when the lvalue is a bitfield to emit a
    // primitive load.
    if (FieldLV.isBitField())
      return EmitLoadOfLValue(FieldLV, Loc);
    return RValue::get(EmitLoadOfScalar(FieldLV, Loc));
  }
  llvm_unreachable("bad evaluation kind");
}

//===--------------------------------------------------------------------===//
//                             Expression Emission
//===--------------------------------------------------------------------===//

RValue CodeGenFunction::EmitCallExpr(const CallExpr *E,
                                     ReturnValueSlot ReturnValue) {
  // Builtins never have block type.
  if (E->getCallee()->getType()->isBlockPointerType())
    return EmitBlockCallExpr(E, ReturnValue);

  if (const auto *CE = dyn_cast<CXXMemberCallExpr>(E))
    return EmitCXXMemberCallExpr(CE, ReturnValue);

  if (const auto *CE = dyn_cast<CUDAKernelCallExpr>(E))
    return EmitCUDAKernelCallExpr(CE, ReturnValue);

  // A CXXOperatorCallExpr is created even for explicit object methods, but
  // these should be treated like static function call.
  if (const auto *CE = dyn_cast<CXXOperatorCallExpr>(E))
    if (const auto *MD =
            dyn_cast_if_present<CXXMethodDecl>(CE->getCalleeDecl());
        MD && MD->isImplicitObjectMemberFunction())
      return EmitCXXOperatorMemberCallExpr(CE, MD, ReturnValue);

  CGCallee callee = EmitCallee(E->getCallee());

  if (callee.isBuiltin()) {
    return EmitBuiltinExpr(callee.getBuiltinDecl(), callee.getBuiltinID(),
                           E, ReturnValue);
  }

  if (callee.isPseudoDestructor()) {
    return EmitCXXPseudoDestructorExpr(callee.getPseudoDestructorExpr());
  }

  return EmitCall(E->getCallee()->getType(), callee, E, ReturnValue);
}

/// Emit a CallExpr without considering whether it might be a subclass.
RValue CodeGenFunction::EmitSimpleCallExpr(const CallExpr *E,
                                           ReturnValueSlot ReturnValue) {
  CGCallee Callee = EmitCallee(E->getCallee());
  return EmitCall(E->getCallee()->getType(), Callee, E, ReturnValue);
}

// Detect the unusual situation where an inline version is shadowed by a
// non-inline version. In that case we should pick the external one
// everywhere. That's GCC behavior too.
static bool OnlyHasInlineBuiltinDeclaration(const FunctionDecl *FD) {
  for (const FunctionDecl *PD = FD; PD; PD = PD->getPreviousDecl())
    if (!PD->isInlineBuiltinDeclaration())
      return false;
  return true;
}

static CGCallee EmitDirectCallee(CodeGenFunction &CGF, GlobalDecl GD) {
  const FunctionDecl *FD = cast<FunctionDecl>(GD.getDecl());

  if (auto builtinID = FD->getBuiltinID()) {
    std::string NoBuiltinFD = ("no-builtin-" + FD->getName()).str();
    std::string NoBuiltins = "no-builtins";

    StringRef Ident = CGF.CGM.getMangledName(GD);
    std::string FDInlineName = (Ident + ".inline").str();

    bool IsPredefinedLibFunction =
        CGF.getContext().BuiltinInfo.isPredefinedLibFunction(builtinID);
    bool HasAttributeNoBuiltin =
        CGF.CurFn->getAttributes().hasFnAttr(NoBuiltinFD) ||
        CGF.CurFn->getAttributes().hasFnAttr(NoBuiltins);

    // When directing calling an inline builtin, call it through it's mangled
    // name to make it clear it's not the actual builtin.
    if (CGF.CurFn->getName() != FDInlineName &&
        OnlyHasInlineBuiltinDeclaration(FD)) {
      llvm::Constant *CalleePtr = CGF.CGM.getRawFunctionPointer(GD);
      llvm::Function *Fn = llvm::cast<llvm::Function>(CalleePtr);
      llvm::Module *M = Fn->getParent();
      llvm::Function *Clone = M->getFunction(FDInlineName);
      if (!Clone) {
        Clone = llvm::Function::Create(Fn->getFunctionType(),
                                       llvm::GlobalValue::InternalLinkage,
                                       Fn->getAddressSpace(), FDInlineName, M);
        Clone->addFnAttr(llvm::Attribute::AlwaysInline);
      }
      return CGCallee::forDirect(Clone, GD);
    }

    // Replaceable builtins provide their own implementation of a builtin. If we
    // are in an inline builtin implementation, avoid trivial infinite
    // recursion. Honor __attribute__((no_builtin("foo"))) or
    // __attribute__((no_builtin)) on the current function unless foo is
    // not a predefined library function which means we must generate the
    // builtin no matter what.
    else if (!IsPredefinedLibFunction || !HasAttributeNoBuiltin)
      return CGCallee::forBuiltin(builtinID, FD);
  }

  llvm::Constant *CalleePtr = CGF.CGM.getRawFunctionPointer(GD);
  if (CGF.CGM.getLangOpts().CUDA && !CGF.CGM.getLangOpts().CUDAIsDevice &&
      FD->hasAttr<CUDAGlobalAttr>())
    CalleePtr = CGF.CGM.getCUDARuntime().getKernelStub(
        cast<llvm::GlobalValue>(CalleePtr->stripPointerCasts()));

  return CGCallee::forDirect(CalleePtr, GD);
}

CGCallee CodeGenFunction::EmitCallee(const Expr *E) {
  E = E->IgnoreParens();

  // Look through function-to-pointer decay.
  if (auto ICE = dyn_cast<ImplicitCastExpr>(E)) {
    if (ICE->getCastKind() == CK_FunctionToPointerDecay ||
        ICE->getCastKind() == CK_BuiltinFnToFnPtr) {
      return EmitCallee(ICE->getSubExpr());
    }

  // Resolve direct calls.
  } else if (auto DRE = dyn_cast<DeclRefExpr>(E)) {
    if (auto FD = dyn_cast<FunctionDecl>(DRE->getDecl())) {
      return EmitDirectCallee(*this, FD);
    }
  } else if (auto ME = dyn_cast<MemberExpr>(E)) {
    if (auto FD = dyn_cast<FunctionDecl>(ME->getMemberDecl())) {
      EmitIgnoredExpr(ME->getBase());
      return EmitDirectCallee(*this, FD);
    }

  // Look through template substitutions.
  } else if (auto NTTP = dyn_cast<SubstNonTypeTemplateParmExpr>(E)) {
    return EmitCallee(NTTP->getReplacement());

  // Treat pseudo-destructor calls differently.
  } else if (auto PDE = dyn_cast<CXXPseudoDestructorExpr>(E)) {
    return CGCallee::forPseudoDestructor(PDE);
  }

  // Otherwise, we have an indirect reference.
  llvm::Value *calleePtr;
  QualType functionType;
  if (auto ptrType = E->getType()->getAs<PointerType>()) {
    calleePtr = EmitScalarExpr(E);
    functionType = ptrType->getPointeeType();
  } else {
    functionType = E->getType();
    calleePtr = EmitLValue(E, KnownNonNull).getPointer(*this);
  }
  assert(functionType->isFunctionType());

  GlobalDecl GD;
  if (const auto *VD =
          dyn_cast_or_null<VarDecl>(E->getReferencedDeclOfCallee()))
    GD = GlobalDecl(VD);

  CGCalleeInfo calleeInfo(functionType->getAs<FunctionProtoType>(), GD);
  CGPointerAuthInfo pointerAuth = CGM.getFunctionPointerAuthInfo(functionType);
  CGCallee callee(calleeInfo, calleePtr, pointerAuth);
  return callee;
}

LValue CodeGenFunction::EmitBinaryOperatorLValue(const BinaryOperator *E) {
  // Comma expressions just emit their LHS then their RHS as an l-value.
  if (E->getOpcode() == BO_Comma) {
    EmitIgnoredExpr(E->getLHS());
    EnsureInsertPoint();
    return EmitLValue(E->getRHS());
  }

  if (E->getOpcode() == BO_PtrMemD ||
      E->getOpcode() == BO_PtrMemI)
    return EmitPointerToDataMemberBinaryExpr(E);

  assert(E->getOpcode() == BO_Assign && "unexpected binary l-value");

  // Note that in all of these cases, __block variables need the RHS
  // evaluated first just in case the variable gets moved by the RHS.

  switch (getEvaluationKind(E->getType())) {
  case TEK_Scalar: {
    switch (E->getLHS()->getType().getObjCLifetime()) {
    case Qualifiers::OCL_Strong:
      return EmitARCStoreStrong(E, /*ignored*/ false).first;

    case Qualifiers::OCL_Autoreleasing:
      return EmitARCStoreAutoreleasing(E).first;

    // No reason to do any of these differently.
    case Qualifiers::OCL_None:
    case Qualifiers::OCL_ExplicitNone:
    case Qualifiers::OCL_Weak:
      break;
    }

    // TODO: Can we de-duplicate this code with the corresponding code in
    // CGExprScalar, similar to the way EmitCompoundAssignmentLValue works?
    RValue RV;
    llvm::Value *Previous = nullptr;
    QualType SrcType = E->getRHS()->getType();
    // Check if LHS is a bitfield, if RHS contains an implicit cast expression
    // we want to extract that value and potentially (if the bitfield sanitizer
    // is enabled) use it to check for an implicit conversion.
    if (E->getLHS()->refersToBitField()) {
      llvm::Value *RHS =
          EmitWithOriginalRHSBitfieldAssignment(E, &Previous, &SrcType);
      RV = RValue::get(RHS);
    } else
      RV = EmitAnyExpr(E->getRHS());

    LValue LV = EmitCheckedLValue(E->getLHS(), TCK_Store);

    if (RV.isScalar())
      EmitNullabilityCheck(LV, RV.getScalarVal(), E->getExprLoc());

    if (LV.isBitField()) {
      llvm::Value *Result = nullptr;
      // If bitfield sanitizers are enabled we want to use the result
      // to check whether a truncation or sign change has occurred.
      if (SanOpts.has(SanitizerKind::ImplicitBitfieldConversion))
        EmitStoreThroughBitfieldLValue(RV, LV, &Result);
      else
        EmitStoreThroughBitfieldLValue(RV, LV);

      // If the expression contained an implicit conversion, make sure
      // to use the value before the scalar conversion.
      llvm::Value *Src = Previous ? Previous : RV.getScalarVal();
      QualType DstType = E->getLHS()->getType();
      EmitBitfieldConversionCheck(Src, SrcType, Result, DstType,
                                  LV.getBitFieldInfo(), E->getExprLoc());
    } else
      EmitStoreThroughLValue(RV, LV);

    if (getLangOpts().OpenMP)
      CGM.getOpenMPRuntime().checkAndEmitLastprivateConditional(*this,
                                                                E->getLHS());
    return LV;
  }

  case TEK_Complex:
    return EmitComplexAssignmentLValue(E);

  case TEK_Aggregate:
    return EmitAggExprToLValue(E);
  }
  llvm_unreachable("bad evaluation kind");
}

LValue CodeGenFunction::EmitCallExprLValue(const CallExpr *E) {
  RValue RV = EmitCallExpr(E);

  if (!RV.isScalar())
    return MakeAddrLValue(RV.getAggregateAddress(), E->getType(),
                          AlignmentSource::Decl);

  assert(E->getCallReturnType(getContext())->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");

  return MakeNaturalAlignPointeeAddrLValue(RV.getScalarVal(), E->getType());
}

LValue CodeGenFunction::EmitVAArgExprLValue(const VAArgExpr *E) {
  // FIXME: This shouldn't require another copy.
  return EmitAggExprToLValue(E);
}

LValue CodeGenFunction::EmitCXXConstructLValue(const CXXConstructExpr *E) {
  assert(E->getType()->getAsCXXRecordDecl()->hasTrivialDestructor()
         && "binding l-value to type which needs a temporary");
  AggValueSlot Slot = CreateAggTemp(E->getType());
  EmitCXXConstructExpr(E, Slot);
  return MakeAddrLValue(Slot.getAddress(), E->getType(), AlignmentSource::Decl);
}

LValue
CodeGenFunction::EmitCXXTypeidLValue(const CXXTypeidExpr *E) {
  return MakeNaturalAlignRawAddrLValue(EmitCXXTypeidExpr(E), E->getType());
}

Address CodeGenFunction::EmitCXXUuidofExpr(const CXXUuidofExpr *E) {
  return CGM.GetAddrOfMSGuidDecl(E->getGuidDecl())
      .withElementType(ConvertType(E->getType()));
}

LValue CodeGenFunction::EmitCXXUuidofLValue(const CXXUuidofExpr *E) {
  return MakeAddrLValue(EmitCXXUuidofExpr(E), E->getType(),
                        AlignmentSource::Decl);
}

LValue
CodeGenFunction::EmitCXXBindTemporaryLValue(const CXXBindTemporaryExpr *E) {
  AggValueSlot Slot = CreateAggTemp(E->getType(), "temp.lvalue");
  Slot.setExternallyDestructed();
  EmitAggExpr(E->getSubExpr(), Slot);
  EmitCXXTemporary(E->getTemporary(), E->getType(), Slot.getAddress());
  return MakeAddrLValue(Slot.getAddress(), E->getType(), AlignmentSource::Decl);
}

LValue CodeGenFunction::EmitObjCMessageExprLValue(const ObjCMessageExpr *E) {
  RValue RV = EmitObjCMessageExpr(E);

  if (!RV.isScalar())
    return MakeAddrLValue(RV.getAggregateAddress(), E->getType(),
                          AlignmentSource::Decl);

  assert(E->getMethodDecl()->getReturnType()->isReferenceType() &&
         "Can't have a scalar return unless the return type is a "
         "reference type!");

  return MakeNaturalAlignPointeeAddrLValue(RV.getScalarVal(), E->getType());
}

LValue CodeGenFunction::EmitObjCSelectorLValue(const ObjCSelectorExpr *E) {
  Address V =
    CGM.getObjCRuntime().GetAddrOfSelector(*this, E->getSelector());
  return MakeAddrLValue(V, E->getType(), AlignmentSource::Decl);
}

llvm::Value *CodeGenFunction::EmitIvarOffset(const ObjCInterfaceDecl *Interface,
                                             const ObjCIvarDecl *Ivar) {
  return CGM.getObjCRuntime().EmitIvarOffset(*this, Interface, Ivar);
}

llvm::Value *
CodeGenFunction::EmitIvarOffsetAsPointerDiff(const ObjCInterfaceDecl *Interface,
                                             const ObjCIvarDecl *Ivar) {
  llvm::Value *OffsetValue = EmitIvarOffset(Interface, Ivar);
  QualType PointerDiffType = getContext().getPointerDiffType();
  return Builder.CreateZExtOrTrunc(OffsetValue,
                                   getTypes().ConvertType(PointerDiffType));
}

LValue CodeGenFunction::EmitLValueForIvar(QualType ObjectTy,
                                          llvm::Value *BaseValue,
                                          const ObjCIvarDecl *Ivar,
                                          unsigned CVRQualifiers) {
  return CGM.getObjCRuntime().EmitObjCValueForIvar(*this, ObjectTy, BaseValue,
                                                   Ivar, CVRQualifiers);
}

LValue CodeGenFunction::EmitObjCIvarRefLValue(const ObjCIvarRefExpr *E) {
  // FIXME: A lot of the code below could be shared with EmitMemberExpr.
  llvm::Value *BaseValue = nullptr;
  const Expr *BaseExpr = E->getBase();
  Qualifiers BaseQuals;
  QualType ObjectTy;
  if (E->isArrow()) {
    BaseValue = EmitScalarExpr(BaseExpr);
    ObjectTy = BaseExpr->getType()->getPointeeType();
    BaseQuals = ObjectTy.getQualifiers();
  } else {
    LValue BaseLV = EmitLValue(BaseExpr);
    BaseValue = BaseLV.getPointer(*this);
    ObjectTy = BaseExpr->getType();
    BaseQuals = ObjectTy.getQualifiers();
  }

  LValue LV =
    EmitLValueForIvar(ObjectTy, BaseValue, E->getDecl(),
                      BaseQuals.getCVRQualifiers());
  setObjCGCLValueClass(getContext(), E, LV);
  return LV;
}

LValue CodeGenFunction::EmitStmtExprLValue(const StmtExpr *E) {
  // Can only get l-value for message expression returning aggregate type
  RValue RV = EmitAnyExprToTemp(E);
  return MakeAddrLValue(RV.getAggregateAddress(), E->getType(),
                        AlignmentSource::Decl);
}

RValue CodeGenFunction::EmitCall(QualType CalleeType, const CGCallee &OrigCallee,
                                 const CallExpr *E, ReturnValueSlot ReturnValue,
                                 llvm::Value *Chain) {
  // Get the actual function type. The callee type will always be a pointer to
  // function type or a block pointer type.
  assert(CalleeType->isFunctionPointerType() &&
         "Call must have function pointer type!");

  const Decl *TargetDecl =
      OrigCallee.getAbstractInfo().getCalleeDecl().getDecl();

  assert((!isa_and_present<FunctionDecl>(TargetDecl) ||
          !cast<FunctionDecl>(TargetDecl)->isImmediateFunction()) &&
         "trying to emit a call to an immediate function");

  CalleeType = getContext().getCanonicalType(CalleeType);

  auto PointeeType = cast<PointerType>(CalleeType)->getPointeeType();

  CGCallee Callee = OrigCallee;

  if (SanOpts.has(SanitizerKind::Function) &&
      (!TargetDecl || !isa<FunctionDecl>(TargetDecl)) &&
      !isa<FunctionNoProtoType>(PointeeType)) {
    if (llvm::Constant *PrefixSig =
            CGM.getTargetCodeGenInfo().getUBSanFunctionSignature(CGM)) {
      SanitizerScope SanScope(this);
      auto *TypeHash = getUBSanFunctionTypeHash(PointeeType);

      llvm::Type *PrefixSigType = PrefixSig->getType();
      llvm::StructType *PrefixStructTy = llvm::StructType::get(
          CGM.getLLVMContext(), {PrefixSigType, Int32Ty}, /*isPacked=*/true);

      llvm::Value *CalleePtr = Callee.getFunctionPointer();
      if (CGM.getCodeGenOpts().PointerAuth.FunctionPointers) {
        // Use raw pointer since we are using the callee pointer as data here.
        Address Addr =
            Address(CalleePtr, CalleePtr->getType(),
                    CharUnits::fromQuantity(
                        CalleePtr->getPointerAlignment(CGM.getDataLayout())),
                    Callee.getPointerAuthInfo(), nullptr);
        CalleePtr = Addr.emitRawPointer(*this);
      }

      // On 32-bit Arm, the low bit of a function pointer indicates whether
      // it's using the Arm or Thumb instruction set. The actual first
      // instruction lives at the same address either way, so we must clear
      // that low bit before using the function address to find the prefix
      // structure.
      //
      // This applies to both Arm and Thumb target triples, because
      // either one could be used in an interworking context where it
      // might be passed function pointers of both types.
      llvm::Value *AlignedCalleePtr;
      if (CGM.getTriple().isARM() || CGM.getTriple().isThumb()) {
        llvm::Value *CalleeAddress =
            Builder.CreatePtrToInt(CalleePtr, IntPtrTy);
        llvm::Value *Mask = llvm::ConstantInt::get(IntPtrTy, ~1);
        llvm::Value *AlignedCalleeAddress =
            Builder.CreateAnd(CalleeAddress, Mask);
        AlignedCalleePtr =
            Builder.CreateIntToPtr(AlignedCalleeAddress, CalleePtr->getType());
      } else {
        AlignedCalleePtr = CalleePtr;
      }

      llvm::Value *CalleePrefixStruct = AlignedCalleePtr;
      llvm::Value *CalleeSigPtr =
          Builder.CreateConstGEP2_32(PrefixStructTy, CalleePrefixStruct, -1, 0);
      llvm::Value *CalleeSig =
          Builder.CreateAlignedLoad(PrefixSigType, CalleeSigPtr, getIntAlign());
      llvm::Value *CalleeSigMatch = Builder.CreateICmpEQ(CalleeSig, PrefixSig);

      llvm::BasicBlock *Cont = createBasicBlock("cont");
      llvm::BasicBlock *TypeCheck = createBasicBlock("typecheck");
      Builder.CreateCondBr(CalleeSigMatch, TypeCheck, Cont);

      EmitBlock(TypeCheck);
      llvm::Value *CalleeTypeHash = Builder.CreateAlignedLoad(
          Int32Ty,
          Builder.CreateConstGEP2_32(PrefixStructTy, CalleePrefixStruct, -1, 1),
          getPointerAlign());
      llvm::Value *CalleeTypeHashMatch =
          Builder.CreateICmpEQ(CalleeTypeHash, TypeHash);
      llvm::Constant *StaticData[] = {EmitCheckSourceLocation(E->getBeginLoc()),
                                      EmitCheckTypeDescriptor(CalleeType)};
      EmitCheck(std::make_pair(CalleeTypeHashMatch, SanitizerKind::Function),
                SanitizerHandler::FunctionTypeMismatch, StaticData,
                {CalleePtr});

      Builder.CreateBr(Cont);
      EmitBlock(Cont);
    }
  }

  const auto *FnType = cast<FunctionType>(PointeeType);

  // If we are checking indirect calls and this call is indirect, check that the
  // function pointer is a member of the bit set for the function type.
  if (SanOpts.has(SanitizerKind::CFIICall) &&
      (!TargetDecl || !isa<FunctionDecl>(TargetDecl))) {
    SanitizerScope SanScope(this);
    EmitSanitizerStatReport(llvm::SanStat_CFI_ICall);

    llvm::Metadata *MD;
    if (CGM.getCodeGenOpts().SanitizeCfiICallGeneralizePointers)
      MD = CGM.CreateMetadataIdentifierGeneralized(QualType(FnType, 0));
    else
      MD = CGM.CreateMetadataIdentifierForType(QualType(FnType, 0));

    llvm::Value *TypeId = llvm::MetadataAsValue::get(getLLVMContext(), MD);

    llvm::Value *CalleePtr = Callee.getFunctionPointer();
    llvm::Value *TypeTest = Builder.CreateCall(
        CGM.getIntrinsic(llvm::Intrinsic::type_test), {CalleePtr, TypeId});

    auto CrossDsoTypeId = CGM.CreateCrossDsoCfiTypeId(MD);
    llvm::Constant *StaticData[] = {
        llvm::ConstantInt::get(Int8Ty, CFITCK_ICall),
        EmitCheckSourceLocation(E->getBeginLoc()),
        EmitCheckTypeDescriptor(QualType(FnType, 0)),
    };
    if (CGM.getCodeGenOpts().SanitizeCfiCrossDso && CrossDsoTypeId) {
      EmitCfiSlowPathCheck(SanitizerKind::CFIICall, TypeTest, CrossDsoTypeId,
                           CalleePtr, StaticData);
    } else {
      EmitCheck(std::make_pair(TypeTest, SanitizerKind::CFIICall),
                SanitizerHandler::CFICheckFail, StaticData,
                {CalleePtr, llvm::UndefValue::get(IntPtrTy)});
    }
  }

  CallArgList Args;
  if (Chain)
    Args.add(RValue::get(Chain), CGM.getContext().VoidPtrTy);

  // C++17 requires that we evaluate arguments to a call using assignment syntax
  // right-to-left, and that we evaluate arguments to certain other operators
  // left-to-right. Note that we allow this to override the order dictated by
  // the calling convention on the MS ABI, which means that parameter
  // destruction order is not necessarily reverse construction order.
  // FIXME: Revisit this based on C++ committee response to unimplementability.
  EvaluationOrder Order = EvaluationOrder::Default;
  bool StaticOperator = false;
  if (auto *OCE = dyn_cast<CXXOperatorCallExpr>(E)) {
    if (OCE->isAssignmentOp())
      Order = EvaluationOrder::ForceRightToLeft;
    else {
      switch (OCE->getOperator()) {
      case OO_LessLess:
      case OO_GreaterGreater:
      case OO_AmpAmp:
      case OO_PipePipe:
      case OO_Comma:
      case OO_ArrowStar:
        Order = EvaluationOrder::ForceLeftToRight;
        break;
      default:
        break;
      }
    }

    if (const auto *MD =
            dyn_cast_if_present<CXXMethodDecl>(OCE->getCalleeDecl());
        MD && MD->isStatic())
      StaticOperator = true;
  }

  auto Arguments = E->arguments();
  if (StaticOperator) {
    // If we're calling a static operator, we need to emit the object argument
    // and ignore it.
    EmitIgnoredExpr(E->getArg(0));
    Arguments = drop_begin(Arguments, 1);
  }
  EmitCallArgs(Args, dyn_cast<FunctionProtoType>(FnType), Arguments,
               E->getDirectCallee(), /*ParamsToSkip=*/0, Order);

  const CGFunctionInfo &FnInfo = CGM.getTypes().arrangeFreeFunctionCall(
      Args, FnType, /*ChainCall=*/Chain);

  // C99 6.5.2.2p6:
  //   If the expression that denotes the called function has a type
  //   that does not include a prototype, [the default argument
  //   promotions are performed]. If the number of arguments does not
  //   equal the number of parameters, the behavior is undefined. If
  //   the function is defined with a type that includes a prototype,
  //   and either the prototype ends with an ellipsis (, ...) or the
  //   types of the arguments after promotion are not compatible with
  //   the types of the parameters, the behavior is undefined. If the
  //   function is defined with a type that does not include a
  //   prototype, and the types of the arguments after promotion are
  //   not compatible with those of the parameters after promotion,
  //   the behavior is undefined [except in some trivial cases].
  // That is, in the general case, we should assume that a call
  // through an unprototyped function type works like a *non-variadic*
  // call.  The way we make this work is to cast to the exact type
  // of the promoted arguments.
  //
  // Chain calls use this same code path to add the invisible chain parameter
  // to the function type.
  if (isa<FunctionNoProtoType>(FnType) || Chain) {
    llvm::Type *CalleeTy = getTypes().GetFunctionType(FnInfo);
    int AS = Callee.getFunctionPointer()->getType()->getPointerAddressSpace();
    CalleeTy = CalleeTy->getPointerTo(AS);

    llvm::Value *CalleePtr = Callee.getFunctionPointer();
    CalleePtr = Builder.CreateBitCast(CalleePtr, CalleeTy, "callee.knr.cast");
    Callee.setFunctionPointer(CalleePtr);
  }

  // HIP function pointer contains kernel handle when it is used in triple
  // chevron. The kernel stub needs to be loaded from kernel handle and used
  // as callee.
  if (CGM.getLangOpts().HIP && !CGM.getLangOpts().CUDAIsDevice &&
      isa<CUDAKernelCallExpr>(E) &&
      (!TargetDecl || !isa<FunctionDecl>(TargetDecl))) {
    llvm::Value *Handle = Callee.getFunctionPointer();
    auto *Stub = Builder.CreateLoad(
        Address(Handle, Handle->getType(), CGM.getPointerAlign()));
    Callee.setFunctionPointer(Stub);
  }
  llvm::CallBase *CallOrInvoke = nullptr;
  RValue Call = EmitCall(FnInfo, Callee, ReturnValue, Args, &CallOrInvoke,
                         E == MustTailCall, E->getExprLoc());

  // Generate function declaration DISuprogram in order to be used
  // in debug info about call sites.
  if (CGDebugInfo *DI = getDebugInfo()) {
    if (auto *CalleeDecl = dyn_cast_or_null<FunctionDecl>(TargetDecl)) {
      FunctionArgList Args;
      QualType ResTy = BuildFunctionArgList(CalleeDecl, Args);
      DI->EmitFuncDeclForCallSite(CallOrInvoke,
                                  DI->getFunctionType(CalleeDecl, ResTy, Args),
                                  CalleeDecl);
    }
  }

  return Call;
}

LValue CodeGenFunction::
EmitPointerToDataMemberBinaryExpr(const BinaryOperator *E) {
  Address BaseAddr = Address::invalid();
  if (E->getOpcode() == BO_PtrMemI) {
    BaseAddr = EmitPointerWithAlignment(E->getLHS());
  } else {
    BaseAddr = EmitLValue(E->getLHS()).getAddress();
  }

  llvm::Value *OffsetV = EmitScalarExpr(E->getRHS());
  const auto *MPT = E->getRHS()->getType()->castAs<MemberPointerType>();

  LValueBaseInfo BaseInfo;
  TBAAAccessInfo TBAAInfo;
  Address MemberAddr =
    EmitCXXMemberDataPointerAddress(E, BaseAddr, OffsetV, MPT, &BaseInfo,
                                    &TBAAInfo);

  return MakeAddrLValue(MemberAddr, MPT->getPointeeType(), BaseInfo, TBAAInfo);
}

/// Given the address of a temporary variable, produce an r-value of
/// its type.
RValue CodeGenFunction::convertTempToRValue(Address addr,
                                            QualType type,
                                            SourceLocation loc) {
  LValue lvalue = MakeAddrLValue(addr, type, AlignmentSource::Decl);
  switch (getEvaluationKind(type)) {
  case TEK_Complex:
    return RValue::getComplex(EmitLoadOfComplex(lvalue, loc));
  case TEK_Aggregate:
    return lvalue.asAggregateRValue();
  case TEK_Scalar:
    return RValue::get(EmitLoadOfScalar(lvalue, loc));
  }
  llvm_unreachable("bad evaluation kind");
}

void CodeGenFunction::SetFPAccuracy(llvm::Value *Val, float Accuracy) {
  assert(Val->getType()->isFPOrFPVectorTy());
  if (Accuracy == 0.0 || !isa<llvm::Instruction>(Val))
    return;

  llvm::MDBuilder MDHelper(getLLVMContext());
  llvm::MDNode *Node = MDHelper.createFPMath(Accuracy);

  cast<llvm::Instruction>(Val)->setMetadata(llvm::LLVMContext::MD_fpmath, Node);
}

void CodeGenFunction::SetSqrtFPAccuracy(llvm::Value *Val) {
  llvm::Type *EltTy = Val->getType()->getScalarType();
  if (!EltTy->isFloatTy())
    return;

  if ((getLangOpts().OpenCL &&
       !CGM.getCodeGenOpts().OpenCLCorrectlyRoundedDivSqrt) ||
      (getLangOpts().HIP && getLangOpts().CUDAIsDevice &&
       !CGM.getCodeGenOpts().HIPCorrectlyRoundedDivSqrt)) {
    // OpenCL v1.1 s7.4: minimum accuracy of single precision / is 3ulp
    //
    // OpenCL v1.2 s5.6.4.2: The -cl-fp32-correctly-rounded-divide-sqrt
    // build option allows an application to specify that single precision
    // floating-point divide (x/y and 1/x) and sqrt used in the program
    // source are correctly rounded.
    //
    // TODO: CUDA has a prec-sqrt flag
    SetFPAccuracy(Val, 3.0f);
  }
}

void CodeGenFunction::SetDivFPAccuracy(llvm::Value *Val) {
  llvm::Type *EltTy = Val->getType()->getScalarType();
  if (!EltTy->isFloatTy())
    return;

  if ((getLangOpts().OpenCL &&
       !CGM.getCodeGenOpts().OpenCLCorrectlyRoundedDivSqrt) ||
      (getLangOpts().HIP && getLangOpts().CUDAIsDevice &&
       !CGM.getCodeGenOpts().HIPCorrectlyRoundedDivSqrt)) {
    // OpenCL v1.1 s7.4: minimum accuracy of single precision / is 2.5ulp
    //
    // OpenCL v1.2 s5.6.4.2: The -cl-fp32-correctly-rounded-divide-sqrt
    // build option allows an application to specify that single precision
    // floating-point divide (x/y and 1/x) and sqrt used in the program
    // source are correctly rounded.
    //
    // TODO: CUDA has a prec-div flag
    SetFPAccuracy(Val, 2.5f);
  }
}

namespace {
  struct LValueOrRValue {
    LValue LV;
    RValue RV;
  };
}

static LValueOrRValue emitPseudoObjectExpr(CodeGenFunction &CGF,
                                           const PseudoObjectExpr *E,
                                           bool forLValue,
                                           AggValueSlot slot) {
  SmallVector<CodeGenFunction::OpaqueValueMappingData, 4> opaques;

  // Find the result expression, if any.
  const Expr *resultExpr = E->getResultExpr();
  LValueOrRValue result;

  for (PseudoObjectExpr::const_semantics_iterator
         i = E->semantics_begin(), e = E->semantics_end(); i != e; ++i) {
    const Expr *semantic = *i;

    // If this semantic expression is an opaque value, bind it
    // to the result of its source expression.
    if (const auto *ov = dyn_cast<OpaqueValueExpr>(semantic)) {
      // Skip unique OVEs.
      if (ov->isUnique()) {
        assert(ov != resultExpr &&
               "A unique OVE cannot be used as the result expression");
        continue;
      }

      // If this is the result expression, we may need to evaluate
      // directly into the slot.
      typedef CodeGenFunction::OpaqueValueMappingData OVMA;
      OVMA opaqueData;
      if (ov == resultExpr && ov->isPRValue() && !forLValue &&
          CodeGenFunction::hasAggregateEvaluationKind(ov->getType())) {
        CGF.EmitAggExpr(ov->getSourceExpr(), slot);
        LValue LV = CGF.MakeAddrLValue(slot.getAddress(), ov->getType(),
                                       AlignmentSource::Decl);
        opaqueData = OVMA::bind(CGF, ov, LV);
        result.RV = slot.asRValue();

      // Otherwise, emit as normal.
      } else {
        opaqueData = OVMA::bind(CGF, ov, ov->getSourceExpr());

        // If this is the result, also evaluate the result now.
        if (ov == resultExpr) {
          if (forLValue)
            result.LV = CGF.EmitLValue(ov);
          else
            result.RV = CGF.EmitAnyExpr(ov, slot);
        }
      }

      opaques.push_back(opaqueData);

    // Otherwise, if the expression is the result, evaluate it
    // and remember the result.
    } else if (semantic == resultExpr) {
      if (forLValue)
        result.LV = CGF.EmitLValue(semantic);
      else
        result.RV = CGF.EmitAnyExpr(semantic, slot);

    // Otherwise, evaluate the expression in an ignored context.
    } else {
      CGF.EmitIgnoredExpr(semantic);
    }
  }

  // Unbind all the opaques now.
  for (unsigned i = 0, e = opaques.size(); i != e; ++i)
    opaques[i].unbind(CGF);

  return result;
}

RValue CodeGenFunction::EmitPseudoObjectRValue(const PseudoObjectExpr *E,
                                               AggValueSlot slot) {
  return emitPseudoObjectExpr(*this, E, false, slot).RV;
}

LValue CodeGenFunction::EmitPseudoObjectLValue(const PseudoObjectExpr *E) {
  return emitPseudoObjectExpr(*this, E, true, AggValueSlot::ignored()).LV;
}
