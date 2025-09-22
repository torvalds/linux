//===----- SemaObjC.cpp ---- Semantic Analysis for Objective-C ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for Objective-C.
///
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaObjC.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Attr.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/Support/ConvertUTF.h"

namespace clang {

SemaObjC::SemaObjC(Sema &S)
    : SemaBase(S), NSNumberDecl(nullptr), NSValueDecl(nullptr),
      NSStringDecl(nullptr), StringWithUTF8StringMethod(nullptr),
      ValueWithBytesObjCTypeMethod(nullptr), NSArrayDecl(nullptr),
      ArrayWithObjectsMethod(nullptr), NSDictionaryDecl(nullptr),
      DictionaryWithObjectsMethod(nullptr) {}

StmtResult SemaObjC::ActOnObjCForCollectionStmt(SourceLocation ForLoc,
                                                Stmt *First, Expr *collection,
                                                SourceLocation RParenLoc) {
  ASTContext &Context = getASTContext();
  SemaRef.setFunctionHasBranchProtectedScope();

  ExprResult CollectionExprResult =
      CheckObjCForCollectionOperand(ForLoc, collection);

  if (First) {
    QualType FirstType;
    if (DeclStmt *DS = dyn_cast<DeclStmt>(First)) {
      if (!DS->isSingleDecl())
        return StmtError(Diag((*DS->decl_begin())->getLocation(),
                              diag::err_toomany_element_decls));

      VarDecl *D = dyn_cast<VarDecl>(DS->getSingleDecl());
      if (!D || D->isInvalidDecl())
        return StmtError();

      FirstType = D->getType();
      // C99 6.8.5p3: The declaration part of a 'for' statement shall only
      // declare identifiers for objects having storage class 'auto' or
      // 'register'.
      if (!D->hasLocalStorage())
        return StmtError(
            Diag(D->getLocation(), diag::err_non_local_variable_decl_in_for));

      // If the type contained 'auto', deduce the 'auto' to 'id'.
      if (FirstType->getContainedAutoType()) {
        SourceLocation Loc = D->getLocation();
        OpaqueValueExpr OpaqueId(Loc, Context.getObjCIdType(), VK_PRValue);
        Expr *DeducedInit = &OpaqueId;
        sema::TemplateDeductionInfo Info(Loc);
        FirstType = QualType();
        TemplateDeductionResult Result = SemaRef.DeduceAutoType(
            D->getTypeSourceInfo()->getTypeLoc(), DeducedInit, FirstType, Info);
        if (Result != TemplateDeductionResult::Success &&
            Result != TemplateDeductionResult::AlreadyDiagnosed)
          SemaRef.DiagnoseAutoDeductionFailure(D, DeducedInit);
        if (FirstType.isNull()) {
          D->setInvalidDecl();
          return StmtError();
        }

        D->setType(FirstType);

        if (!SemaRef.inTemplateInstantiation()) {
          SourceLocation Loc =
              D->getTypeSourceInfo()->getTypeLoc().getBeginLoc();
          Diag(Loc, diag::warn_auto_var_is_id) << D->getDeclName();
        }
      }

    } else {
      Expr *FirstE = cast<Expr>(First);
      if (!FirstE->isTypeDependent() && !FirstE->isLValue())
        return StmtError(
            Diag(First->getBeginLoc(), diag::err_selector_element_not_lvalue)
            << First->getSourceRange());

      FirstType = static_cast<Expr *>(First)->getType();
      if (FirstType.isConstQualified())
        Diag(ForLoc, diag::err_selector_element_const_type)
            << FirstType << First->getSourceRange();
    }
    if (!FirstType->isDependentType() &&
        !FirstType->isObjCObjectPointerType() &&
        !FirstType->isBlockPointerType())
      return StmtError(Diag(ForLoc, diag::err_selector_element_type)
                       << FirstType << First->getSourceRange());
  }

  if (CollectionExprResult.isInvalid())
    return StmtError();

  CollectionExprResult = SemaRef.ActOnFinishFullExpr(CollectionExprResult.get(),
                                                     /*DiscardedValue*/ false);
  if (CollectionExprResult.isInvalid())
    return StmtError();

  return new (Context) ObjCForCollectionStmt(First, CollectionExprResult.get(),
                                             nullptr, ForLoc, RParenLoc);
}

ExprResult SemaObjC::CheckObjCForCollectionOperand(SourceLocation forLoc,
                                                   Expr *collection) {
  ASTContext &Context = getASTContext();
  if (!collection)
    return ExprError();

  ExprResult result = SemaRef.CorrectDelayedTyposInExpr(collection);
  if (!result.isUsable())
    return ExprError();
  collection = result.get();

  // Bail out early if we've got a type-dependent expression.
  if (collection->isTypeDependent())
    return collection;

  // Perform normal l-value conversion.
  result = SemaRef.DefaultFunctionArrayLvalueConversion(collection);
  if (result.isInvalid())
    return ExprError();
  collection = result.get();

  // The operand needs to have object-pointer type.
  // TODO: should we do a contextual conversion?
  const ObjCObjectPointerType *pointerType =
      collection->getType()->getAs<ObjCObjectPointerType>();
  if (!pointerType)
    return Diag(forLoc, diag::err_collection_expr_type)
           << collection->getType() << collection->getSourceRange();

  // Check that the operand provides
  //   - countByEnumeratingWithState:objects:count:
  const ObjCObjectType *objectType = pointerType->getObjectType();
  ObjCInterfaceDecl *iface = objectType->getInterface();

  // If we have a forward-declared type, we can't do this check.
  // Under ARC, it is an error not to have a forward-declared class.
  if (iface &&
      (getLangOpts().ObjCAutoRefCount
           ? SemaRef.RequireCompleteType(forLoc, QualType(objectType, 0),
                                         diag::err_arc_collection_forward,
                                         collection)
           : !SemaRef.isCompleteType(forLoc, QualType(objectType, 0)))) {
    // Otherwise, if we have any useful type information, check that
    // the type declares the appropriate method.
  } else if (iface || !objectType->qual_empty()) {
    const IdentifierInfo *selectorIdents[] = {
        &Context.Idents.get("countByEnumeratingWithState"),
        &Context.Idents.get("objects"), &Context.Idents.get("count")};
    Selector selector = Context.Selectors.getSelector(3, &selectorIdents[0]);

    ObjCMethodDecl *method = nullptr;

    // If there's an interface, look in both the public and private APIs.
    if (iface) {
      method = iface->lookupInstanceMethod(selector);
      if (!method)
        method = iface->lookupPrivateMethod(selector);
    }

    // Also check protocol qualifiers.
    if (!method)
      method = LookupMethodInQualifiedType(selector, pointerType,
                                           /*instance*/ true);

    // If we didn't find it anywhere, give up.
    if (!method) {
      Diag(forLoc, diag::warn_collection_expr_type)
          << collection->getType() << selector << collection->getSourceRange();
    }

    // TODO: check for an incompatible signature?
  }

  // Wrap up any cleanups in the expression.
  return collection;
}

StmtResult SemaObjC::FinishObjCForCollectionStmt(Stmt *S, Stmt *B) {
  if (!S || !B)
    return StmtError();
  ObjCForCollectionStmt *ForStmt = cast<ObjCForCollectionStmt>(S);

  ForStmt->setBody(B);
  return S;
}

StmtResult SemaObjC::ActOnObjCAtCatchStmt(SourceLocation AtLoc,
                                          SourceLocation RParen, Decl *Parm,
                                          Stmt *Body) {
  ASTContext &Context = getASTContext();
  VarDecl *Var = cast_or_null<VarDecl>(Parm);
  if (Var && Var->isInvalidDecl())
    return StmtError();

  return new (Context) ObjCAtCatchStmt(AtLoc, RParen, Var, Body);
}

StmtResult SemaObjC::ActOnObjCAtFinallyStmt(SourceLocation AtLoc, Stmt *Body) {
  ASTContext &Context = getASTContext();
  return new (Context) ObjCAtFinallyStmt(AtLoc, Body);
}

StmtResult SemaObjC::ActOnObjCAtTryStmt(SourceLocation AtLoc, Stmt *Try,
                                        MultiStmtArg CatchStmts,
                                        Stmt *Finally) {
  ASTContext &Context = getASTContext();
  if (!getLangOpts().ObjCExceptions)
    Diag(AtLoc, diag::err_objc_exceptions_disabled) << "@try";

  // Objective-C try is incompatible with SEH __try.
  sema::FunctionScopeInfo *FSI = SemaRef.getCurFunction();
  if (FSI->FirstSEHTryLoc.isValid()) {
    Diag(AtLoc, diag::err_mixing_cxx_try_seh_try) << 1;
    Diag(FSI->FirstSEHTryLoc, diag::note_conflicting_try_here) << "'__try'";
  }

  FSI->setHasObjCTry(AtLoc);
  unsigned NumCatchStmts = CatchStmts.size();
  return ObjCAtTryStmt::Create(Context, AtLoc, Try, CatchStmts.data(),
                               NumCatchStmts, Finally);
}

StmtResult SemaObjC::BuildObjCAtThrowStmt(SourceLocation AtLoc, Expr *Throw) {
  ASTContext &Context = getASTContext();
  if (Throw) {
    ExprResult Result = SemaRef.DefaultLvalueConversion(Throw);
    if (Result.isInvalid())
      return StmtError();

    Result =
        SemaRef.ActOnFinishFullExpr(Result.get(), /*DiscardedValue*/ false);
    if (Result.isInvalid())
      return StmtError();
    Throw = Result.get();

    QualType ThrowType = Throw->getType();
    // Make sure the expression type is an ObjC pointer or "void *".
    if (!ThrowType->isDependentType() &&
        !ThrowType->isObjCObjectPointerType()) {
      const PointerType *PT = ThrowType->getAs<PointerType>();
      if (!PT || !PT->getPointeeType()->isVoidType())
        return StmtError(Diag(AtLoc, diag::err_objc_throw_expects_object)
                         << Throw->getType() << Throw->getSourceRange());
    }
  }

  return new (Context) ObjCAtThrowStmt(AtLoc, Throw);
}

StmtResult SemaObjC::ActOnObjCAtThrowStmt(SourceLocation AtLoc, Expr *Throw,
                                          Scope *CurScope) {
  if (!getLangOpts().ObjCExceptions)
    Diag(AtLoc, diag::err_objc_exceptions_disabled) << "@throw";

  if (!Throw) {
    // @throw without an expression designates a rethrow (which must occur
    // in the context of an @catch clause).
    Scope *AtCatchParent = CurScope;
    while (AtCatchParent && !AtCatchParent->isAtCatchScope())
      AtCatchParent = AtCatchParent->getParent();
    if (!AtCatchParent)
      return StmtError(Diag(AtLoc, diag::err_rethrow_used_outside_catch));
  }
  return BuildObjCAtThrowStmt(AtLoc, Throw);
}

ExprResult SemaObjC::ActOnObjCAtSynchronizedOperand(SourceLocation atLoc,
                                                    Expr *operand) {
  ExprResult result = SemaRef.DefaultLvalueConversion(operand);
  if (result.isInvalid())
    return ExprError();
  operand = result.get();

  // Make sure the expression type is an ObjC pointer or "void *".
  QualType type = operand->getType();
  if (!type->isDependentType() && !type->isObjCObjectPointerType()) {
    const PointerType *pointerType = type->getAs<PointerType>();
    if (!pointerType || !pointerType->getPointeeType()->isVoidType()) {
      if (getLangOpts().CPlusPlus) {
        if (SemaRef.RequireCompleteType(atLoc, type,
                                        diag::err_incomplete_receiver_type))
          return Diag(atLoc, diag::err_objc_synchronized_expects_object)
                 << type << operand->getSourceRange();

        ExprResult result =
            SemaRef.PerformContextuallyConvertToObjCPointer(operand);
        if (result.isInvalid())
          return ExprError();
        if (!result.isUsable())
          return Diag(atLoc, diag::err_objc_synchronized_expects_object)
                 << type << operand->getSourceRange();

        operand = result.get();
      } else {
        return Diag(atLoc, diag::err_objc_synchronized_expects_object)
               << type << operand->getSourceRange();
      }
    }
  }

  // The operand to @synchronized is a full-expression.
  return SemaRef.ActOnFinishFullExpr(operand, /*DiscardedValue*/ false);
}

StmtResult SemaObjC::ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc,
                                                 Expr *SyncExpr,
                                                 Stmt *SyncBody) {
  ASTContext &Context = getASTContext();
  // We can't jump into or indirect-jump out of a @synchronized block.
  SemaRef.setFunctionHasBranchProtectedScope();
  return new (Context) ObjCAtSynchronizedStmt(AtLoc, SyncExpr, SyncBody);
}

StmtResult SemaObjC::ActOnObjCAutoreleasePoolStmt(SourceLocation AtLoc,
                                                  Stmt *Body) {
  ASTContext &Context = getASTContext();
  SemaRef.setFunctionHasBranchProtectedScope();
  return new (Context) ObjCAutoreleasePoolStmt(AtLoc, Body);
}

TypeResult SemaObjC::actOnObjCProtocolQualifierType(
    SourceLocation lAngleLoc, ArrayRef<Decl *> protocols,
    ArrayRef<SourceLocation> protocolLocs, SourceLocation rAngleLoc) {
  ASTContext &Context = getASTContext();
  // Form id<protocol-list>.
  QualType Result = Context.getObjCObjectType(
      Context.ObjCBuiltinIdTy, {},
      llvm::ArrayRef((ObjCProtocolDecl *const *)protocols.data(),
                     protocols.size()),
      false);
  Result = Context.getObjCObjectPointerType(Result);

  TypeSourceInfo *ResultTInfo = Context.CreateTypeSourceInfo(Result);
  TypeLoc ResultTL = ResultTInfo->getTypeLoc();

  auto ObjCObjectPointerTL = ResultTL.castAs<ObjCObjectPointerTypeLoc>();
  ObjCObjectPointerTL.setStarLoc(SourceLocation()); // implicit

  auto ObjCObjectTL =
      ObjCObjectPointerTL.getPointeeLoc().castAs<ObjCObjectTypeLoc>();
  ObjCObjectTL.setHasBaseTypeAsWritten(false);
  ObjCObjectTL.getBaseLoc().initialize(Context, SourceLocation());

  // No type arguments.
  ObjCObjectTL.setTypeArgsLAngleLoc(SourceLocation());
  ObjCObjectTL.setTypeArgsRAngleLoc(SourceLocation());

  // Fill in protocol qualifiers.
  ObjCObjectTL.setProtocolLAngleLoc(lAngleLoc);
  ObjCObjectTL.setProtocolRAngleLoc(rAngleLoc);
  for (unsigned i = 0, n = protocols.size(); i != n; ++i)
    ObjCObjectTL.setProtocolLoc(i, protocolLocs[i]);

  // We're done. Return the completed type to the parser.
  return SemaRef.CreateParsedType(Result, ResultTInfo);
}

TypeResult SemaObjC::actOnObjCTypeArgsAndProtocolQualifiers(
    Scope *S, SourceLocation Loc, ParsedType BaseType,
    SourceLocation TypeArgsLAngleLoc, ArrayRef<ParsedType> TypeArgs,
    SourceLocation TypeArgsRAngleLoc, SourceLocation ProtocolLAngleLoc,
    ArrayRef<Decl *> Protocols, ArrayRef<SourceLocation> ProtocolLocs,
    SourceLocation ProtocolRAngleLoc) {
  ASTContext &Context = getASTContext();
  TypeSourceInfo *BaseTypeInfo = nullptr;
  QualType T = SemaRef.GetTypeFromParser(BaseType, &BaseTypeInfo);
  if (T.isNull())
    return true;

  // Handle missing type-source info.
  if (!BaseTypeInfo)
    BaseTypeInfo = Context.getTrivialTypeSourceInfo(T, Loc);

  // Extract type arguments.
  SmallVector<TypeSourceInfo *, 4> ActualTypeArgInfos;
  for (unsigned i = 0, n = TypeArgs.size(); i != n; ++i) {
    TypeSourceInfo *TypeArgInfo = nullptr;
    QualType TypeArg = SemaRef.GetTypeFromParser(TypeArgs[i], &TypeArgInfo);
    if (TypeArg.isNull()) {
      ActualTypeArgInfos.clear();
      break;
    }

    assert(TypeArgInfo && "No type source info?");
    ActualTypeArgInfos.push_back(TypeArgInfo);
  }

  // Build the object type.
  QualType Result = BuildObjCObjectType(
      T, BaseTypeInfo->getTypeLoc().getSourceRange().getBegin(),
      TypeArgsLAngleLoc, ActualTypeArgInfos, TypeArgsRAngleLoc,
      ProtocolLAngleLoc,
      llvm::ArrayRef((ObjCProtocolDecl *const *)Protocols.data(),
                     Protocols.size()),
      ProtocolLocs, ProtocolRAngleLoc,
      /*FailOnError=*/false,
      /*Rebuilding=*/false);

  if (Result == T)
    return BaseType;

  // Create source information for this type.
  TypeSourceInfo *ResultTInfo = Context.CreateTypeSourceInfo(Result);
  TypeLoc ResultTL = ResultTInfo->getTypeLoc();

  // For id<Proto1, Proto2> or Class<Proto1, Proto2>, we'll have an
  // object pointer type. Fill in source information for it.
  if (auto ObjCObjectPointerTL = ResultTL.getAs<ObjCObjectPointerTypeLoc>()) {
    // The '*' is implicit.
    ObjCObjectPointerTL.setStarLoc(SourceLocation());
    ResultTL = ObjCObjectPointerTL.getPointeeLoc();
  }

  if (auto OTPTL = ResultTL.getAs<ObjCTypeParamTypeLoc>()) {
    // Protocol qualifier information.
    if (OTPTL.getNumProtocols() > 0) {
      assert(OTPTL.getNumProtocols() == Protocols.size());
      OTPTL.setProtocolLAngleLoc(ProtocolLAngleLoc);
      OTPTL.setProtocolRAngleLoc(ProtocolRAngleLoc);
      for (unsigned i = 0, n = Protocols.size(); i != n; ++i)
        OTPTL.setProtocolLoc(i, ProtocolLocs[i]);
    }

    // We're done. Return the completed type to the parser.
    return SemaRef.CreateParsedType(Result, ResultTInfo);
  }

  auto ObjCObjectTL = ResultTL.castAs<ObjCObjectTypeLoc>();

  // Type argument information.
  if (ObjCObjectTL.getNumTypeArgs() > 0) {
    assert(ObjCObjectTL.getNumTypeArgs() == ActualTypeArgInfos.size());
    ObjCObjectTL.setTypeArgsLAngleLoc(TypeArgsLAngleLoc);
    ObjCObjectTL.setTypeArgsRAngleLoc(TypeArgsRAngleLoc);
    for (unsigned i = 0, n = ActualTypeArgInfos.size(); i != n; ++i)
      ObjCObjectTL.setTypeArgTInfo(i, ActualTypeArgInfos[i]);
  } else {
    ObjCObjectTL.setTypeArgsLAngleLoc(SourceLocation());
    ObjCObjectTL.setTypeArgsRAngleLoc(SourceLocation());
  }

  // Protocol qualifier information.
  if (ObjCObjectTL.getNumProtocols() > 0) {
    assert(ObjCObjectTL.getNumProtocols() == Protocols.size());
    ObjCObjectTL.setProtocolLAngleLoc(ProtocolLAngleLoc);
    ObjCObjectTL.setProtocolRAngleLoc(ProtocolRAngleLoc);
    for (unsigned i = 0, n = Protocols.size(); i != n; ++i)
      ObjCObjectTL.setProtocolLoc(i, ProtocolLocs[i]);
  } else {
    ObjCObjectTL.setProtocolLAngleLoc(SourceLocation());
    ObjCObjectTL.setProtocolRAngleLoc(SourceLocation());
  }

  // Base type.
  ObjCObjectTL.setHasBaseTypeAsWritten(true);
  if (ObjCObjectTL.getType() == T)
    ObjCObjectTL.getBaseLoc().initializeFullCopy(BaseTypeInfo->getTypeLoc());
  else
    ObjCObjectTL.getBaseLoc().initialize(Context, Loc);

  // We're done. Return the completed type to the parser.
  return SemaRef.CreateParsedType(Result, ResultTInfo);
}

QualType SemaObjC::BuildObjCTypeParamType(
    const ObjCTypeParamDecl *Decl, SourceLocation ProtocolLAngleLoc,
    ArrayRef<ObjCProtocolDecl *> Protocols,
    ArrayRef<SourceLocation> ProtocolLocs, SourceLocation ProtocolRAngleLoc,
    bool FailOnError) {
  ASTContext &Context = getASTContext();
  QualType Result = QualType(Decl->getTypeForDecl(), 0);
  if (!Protocols.empty()) {
    bool HasError;
    Result = Context.applyObjCProtocolQualifiers(Result, Protocols, HasError);
    if (HasError) {
      Diag(SourceLocation(), diag::err_invalid_protocol_qualifiers)
          << SourceRange(ProtocolLAngleLoc, ProtocolRAngleLoc);
      if (FailOnError)
        Result = QualType();
    }
    if (FailOnError && Result.isNull())
      return QualType();
  }

  return Result;
}

/// Apply Objective-C type arguments to the given type.
static QualType applyObjCTypeArgs(Sema &S, SourceLocation loc, QualType type,
                                  ArrayRef<TypeSourceInfo *> typeArgs,
                                  SourceRange typeArgsRange, bool failOnError,
                                  bool rebuilding) {
  // We can only apply type arguments to an Objective-C class type.
  const auto *objcObjectType = type->getAs<ObjCObjectType>();
  if (!objcObjectType || !objcObjectType->getInterface()) {
    S.Diag(loc, diag::err_objc_type_args_non_class) << type << typeArgsRange;

    if (failOnError)
      return QualType();
    return type;
  }

  // The class type must be parameterized.
  ObjCInterfaceDecl *objcClass = objcObjectType->getInterface();
  ObjCTypeParamList *typeParams = objcClass->getTypeParamList();
  if (!typeParams) {
    S.Diag(loc, diag::err_objc_type_args_non_parameterized_class)
        << objcClass->getDeclName() << FixItHint::CreateRemoval(typeArgsRange);

    if (failOnError)
      return QualType();

    return type;
  }

  // The type must not already be specialized.
  if (objcObjectType->isSpecialized()) {
    S.Diag(loc, diag::err_objc_type_args_specialized_class)
        << type << FixItHint::CreateRemoval(typeArgsRange);

    if (failOnError)
      return QualType();

    return type;
  }

  // Check the type arguments.
  SmallVector<QualType, 4> finalTypeArgs;
  unsigned numTypeParams = typeParams->size();
  bool anyPackExpansions = false;
  for (unsigned i = 0, n = typeArgs.size(); i != n; ++i) {
    TypeSourceInfo *typeArgInfo = typeArgs[i];
    QualType typeArg = typeArgInfo->getType();

    // Type arguments cannot have explicit qualifiers or nullability.
    // We ignore indirect sources of these, e.g. behind typedefs or
    // template arguments.
    if (TypeLoc qual = typeArgInfo->getTypeLoc().findExplicitQualifierLoc()) {
      bool diagnosed = false;
      SourceRange rangeToRemove;
      if (auto attr = qual.getAs<AttributedTypeLoc>()) {
        rangeToRemove = attr.getLocalSourceRange();
        if (attr.getTypePtr()->getImmediateNullability()) {
          typeArg = attr.getTypePtr()->getModifiedType();
          S.Diag(attr.getBeginLoc(),
                 diag::err_objc_type_arg_explicit_nullability)
              << typeArg << FixItHint::CreateRemoval(rangeToRemove);
          diagnosed = true;
        }
      }

      // When rebuilding, qualifiers might have gotten here through a
      // final substitution.
      if (!rebuilding && !diagnosed) {
        S.Diag(qual.getBeginLoc(), diag::err_objc_type_arg_qualified)
            << typeArg << typeArg.getQualifiers().getAsString()
            << FixItHint::CreateRemoval(rangeToRemove);
      }
    }

    // Remove qualifiers even if they're non-local.
    typeArg = typeArg.getUnqualifiedType();

    finalTypeArgs.push_back(typeArg);

    if (typeArg->getAs<PackExpansionType>())
      anyPackExpansions = true;

    // Find the corresponding type parameter, if there is one.
    ObjCTypeParamDecl *typeParam = nullptr;
    if (!anyPackExpansions) {
      if (i < numTypeParams) {
        typeParam = typeParams->begin()[i];
      } else {
        // Too many arguments.
        S.Diag(loc, diag::err_objc_type_args_wrong_arity)
            << false << objcClass->getDeclName() << (unsigned)typeArgs.size()
            << numTypeParams;
        S.Diag(objcClass->getLocation(), diag::note_previous_decl) << objcClass;

        if (failOnError)
          return QualType();

        return type;
      }
    }

    // Objective-C object pointer types must be substitutable for the bounds.
    if (const auto *typeArgObjC = typeArg->getAs<ObjCObjectPointerType>()) {
      // If we don't have a type parameter to match against, assume
      // everything is fine. There was a prior pack expansion that
      // means we won't be able to match anything.
      if (!typeParam) {
        assert(anyPackExpansions && "Too many arguments?");
        continue;
      }

      // Retrieve the bound.
      QualType bound = typeParam->getUnderlyingType();
      const auto *boundObjC = bound->castAs<ObjCObjectPointerType>();

      // Determine whether the type argument is substitutable for the bound.
      if (typeArgObjC->isObjCIdType()) {
        // When the type argument is 'id', the only acceptable type
        // parameter bound is 'id'.
        if (boundObjC->isObjCIdType())
          continue;
      } else if (S.Context.canAssignObjCInterfaces(boundObjC, typeArgObjC)) {
        // Otherwise, we follow the assignability rules.
        continue;
      }

      // Diagnose the mismatch.
      S.Diag(typeArgInfo->getTypeLoc().getBeginLoc(),
             diag::err_objc_type_arg_does_not_match_bound)
          << typeArg << bound << typeParam->getDeclName();
      S.Diag(typeParam->getLocation(), diag::note_objc_type_param_here)
          << typeParam->getDeclName();

      if (failOnError)
        return QualType();

      return type;
    }

    // Block pointer types are permitted for unqualified 'id' bounds.
    if (typeArg->isBlockPointerType()) {
      // If we don't have a type parameter to match against, assume
      // everything is fine. There was a prior pack expansion that
      // means we won't be able to match anything.
      if (!typeParam) {
        assert(anyPackExpansions && "Too many arguments?");
        continue;
      }

      // Retrieve the bound.
      QualType bound = typeParam->getUnderlyingType();
      if (bound->isBlockCompatibleObjCPointerType(S.Context))
        continue;

      // Diagnose the mismatch.
      S.Diag(typeArgInfo->getTypeLoc().getBeginLoc(),
             diag::err_objc_type_arg_does_not_match_bound)
          << typeArg << bound << typeParam->getDeclName();
      S.Diag(typeParam->getLocation(), diag::note_objc_type_param_here)
          << typeParam->getDeclName();

      if (failOnError)
        return QualType();

      return type;
    }

    // Types that have __attribute__((NSObject)) are permitted.
    if (typeArg->isObjCNSObjectType()) {
      continue;
    }

    // Dependent types will be checked at instantiation time.
    if (typeArg->isDependentType()) {
      continue;
    }

    // Diagnose non-id-compatible type arguments.
    S.Diag(typeArgInfo->getTypeLoc().getBeginLoc(),
           diag::err_objc_type_arg_not_id_compatible)
        << typeArg << typeArgInfo->getTypeLoc().getSourceRange();

    if (failOnError)
      return QualType();

    return type;
  }

  // Make sure we didn't have the wrong number of arguments.
  if (!anyPackExpansions && finalTypeArgs.size() != numTypeParams) {
    S.Diag(loc, diag::err_objc_type_args_wrong_arity)
        << (typeArgs.size() < typeParams->size()) << objcClass->getDeclName()
        << (unsigned)finalTypeArgs.size() << (unsigned)numTypeParams;
    S.Diag(objcClass->getLocation(), diag::note_previous_decl) << objcClass;

    if (failOnError)
      return QualType();

    return type;
  }

  // Success. Form the specialized type.
  return S.Context.getObjCObjectType(type, finalTypeArgs, {}, false);
}

QualType SemaObjC::BuildObjCObjectType(
    QualType BaseType, SourceLocation Loc, SourceLocation TypeArgsLAngleLoc,
    ArrayRef<TypeSourceInfo *> TypeArgs, SourceLocation TypeArgsRAngleLoc,
    SourceLocation ProtocolLAngleLoc, ArrayRef<ObjCProtocolDecl *> Protocols,
    ArrayRef<SourceLocation> ProtocolLocs, SourceLocation ProtocolRAngleLoc,
    bool FailOnError, bool Rebuilding) {
  ASTContext &Context = getASTContext();
  QualType Result = BaseType;
  if (!TypeArgs.empty()) {
    Result =
        applyObjCTypeArgs(SemaRef, Loc, Result, TypeArgs,
                          SourceRange(TypeArgsLAngleLoc, TypeArgsRAngleLoc),
                          FailOnError, Rebuilding);
    if (FailOnError && Result.isNull())
      return QualType();
  }

  if (!Protocols.empty()) {
    bool HasError;
    Result = Context.applyObjCProtocolQualifiers(Result, Protocols, HasError);
    if (HasError) {
      Diag(Loc, diag::err_invalid_protocol_qualifiers)
          << SourceRange(ProtocolLAngleLoc, ProtocolRAngleLoc);
      if (FailOnError)
        Result = QualType();
    }
    if (FailOnError && Result.isNull())
      return QualType();
  }

  return Result;
}

ParsedType SemaObjC::ActOnObjCInstanceType(SourceLocation Loc) {
  ASTContext &Context = getASTContext();
  QualType T = Context.getObjCInstanceType();
  TypeSourceInfo *TInfo = Context.getTrivialTypeSourceInfo(T, Loc);
  return SemaRef.CreateParsedType(T, TInfo);
}

//===--- CHECK: Objective-C retain cycles ----------------------------------//

namespace {

struct RetainCycleOwner {
  VarDecl *Variable = nullptr;
  SourceRange Range;
  SourceLocation Loc;
  bool Indirect = false;

  RetainCycleOwner() = default;

  void setLocsFrom(Expr *e) {
    Loc = e->getExprLoc();
    Range = e->getSourceRange();
  }
};

} // namespace

/// Consider whether capturing the given variable can possibly lead to
/// a retain cycle.
static bool considerVariable(VarDecl *var, Expr *ref, RetainCycleOwner &owner) {
  // In ARC, it's captured strongly iff the variable has __strong
  // lifetime.  In MRR, it's captured strongly if the variable is
  // __block and has an appropriate type.
  if (var->getType().getObjCLifetime() != Qualifiers::OCL_Strong)
    return false;

  owner.Variable = var;
  if (ref)
    owner.setLocsFrom(ref);
  return true;
}

static bool findRetainCycleOwner(Sema &S, Expr *e, RetainCycleOwner &owner) {
  while (true) {
    e = e->IgnoreParens();
    if (CastExpr *cast = dyn_cast<CastExpr>(e)) {
      switch (cast->getCastKind()) {
      case CK_BitCast:
      case CK_LValueBitCast:
      case CK_LValueToRValue:
      case CK_ARCReclaimReturnedObject:
        e = cast->getSubExpr();
        continue;

      default:
        return false;
      }
    }

    if (ObjCIvarRefExpr *ref = dyn_cast<ObjCIvarRefExpr>(e)) {
      ObjCIvarDecl *ivar = ref->getDecl();
      if (ivar->getType().getObjCLifetime() != Qualifiers::OCL_Strong)
        return false;

      // Try to find a retain cycle in the base.
      if (!findRetainCycleOwner(S, ref->getBase(), owner))
        return false;

      if (ref->isFreeIvar())
        owner.setLocsFrom(ref);
      owner.Indirect = true;
      return true;
    }

    if (DeclRefExpr *ref = dyn_cast<DeclRefExpr>(e)) {
      VarDecl *var = dyn_cast<VarDecl>(ref->getDecl());
      if (!var)
        return false;
      return considerVariable(var, ref, owner);
    }

    if (MemberExpr *member = dyn_cast<MemberExpr>(e)) {
      if (member->isArrow())
        return false;

      // Don't count this as an indirect ownership.
      e = member->getBase();
      continue;
    }

    if (PseudoObjectExpr *pseudo = dyn_cast<PseudoObjectExpr>(e)) {
      // Only pay attention to pseudo-objects on property references.
      ObjCPropertyRefExpr *pre = dyn_cast<ObjCPropertyRefExpr>(
          pseudo->getSyntacticForm()->IgnoreParens());
      if (!pre)
        return false;
      if (pre->isImplicitProperty())
        return false;
      ObjCPropertyDecl *property = pre->getExplicitProperty();
      if (!property->isRetaining() &&
          !(property->getPropertyIvarDecl() &&
            property->getPropertyIvarDecl()->getType().getObjCLifetime() ==
                Qualifiers::OCL_Strong))
        return false;

      owner.Indirect = true;
      if (pre->isSuperReceiver()) {
        owner.Variable = S.getCurMethodDecl()->getSelfDecl();
        if (!owner.Variable)
          return false;
        owner.Loc = pre->getLocation();
        owner.Range = pre->getSourceRange();
        return true;
      }
      e = const_cast<Expr *>(
          cast<OpaqueValueExpr>(pre->getBase())->getSourceExpr());
      continue;
    }

    // Array ivars?

    return false;
  }
}

namespace {

struct FindCaptureVisitor : EvaluatedExprVisitor<FindCaptureVisitor> {
  VarDecl *Variable;
  Expr *Capturer = nullptr;
  bool VarWillBeReased = false;

  FindCaptureVisitor(ASTContext &Context, VarDecl *variable)
      : EvaluatedExprVisitor<FindCaptureVisitor>(Context), Variable(variable) {}

  void VisitDeclRefExpr(DeclRefExpr *ref) {
    if (ref->getDecl() == Variable && !Capturer)
      Capturer = ref;
  }

  void VisitObjCIvarRefExpr(ObjCIvarRefExpr *ref) {
    if (Capturer)
      return;
    Visit(ref->getBase());
    if (Capturer && ref->isFreeIvar())
      Capturer = ref;
  }

  void VisitBlockExpr(BlockExpr *block) {
    // Look inside nested blocks
    if (block->getBlockDecl()->capturesVariable(Variable))
      Visit(block->getBlockDecl()->getBody());
  }

  void VisitOpaqueValueExpr(OpaqueValueExpr *OVE) {
    if (Capturer)
      return;
    if (OVE->getSourceExpr())
      Visit(OVE->getSourceExpr());
  }

  void VisitBinaryOperator(BinaryOperator *BinOp) {
    if (!Variable || VarWillBeReased || BinOp->getOpcode() != BO_Assign)
      return;
    Expr *LHS = BinOp->getLHS();
    if (const DeclRefExpr *DRE = dyn_cast_or_null<DeclRefExpr>(LHS)) {
      if (DRE->getDecl() != Variable)
        return;
      if (Expr *RHS = BinOp->getRHS()) {
        RHS = RHS->IgnoreParenCasts();
        std::optional<llvm::APSInt> Value;
        VarWillBeReased =
            (RHS && (Value = RHS->getIntegerConstantExpr(Context)) &&
             *Value == 0);
      }
    }
  }
};

} // namespace

/// Check whether the given argument is a block which captures a
/// variable.
static Expr *findCapturingExpr(Sema &S, Expr *e, RetainCycleOwner &owner) {
  assert(owner.Variable && owner.Loc.isValid());

  e = e->IgnoreParenCasts();

  // Look through [^{...} copy] and Block_copy(^{...}).
  if (ObjCMessageExpr *ME = dyn_cast<ObjCMessageExpr>(e)) {
    Selector Cmd = ME->getSelector();
    if (Cmd.isUnarySelector() && Cmd.getNameForSlot(0) == "copy") {
      e = ME->getInstanceReceiver();
      if (!e)
        return nullptr;
      e = e->IgnoreParenCasts();
    }
  } else if (CallExpr *CE = dyn_cast<CallExpr>(e)) {
    if (CE->getNumArgs() == 1) {
      FunctionDecl *Fn = dyn_cast_or_null<FunctionDecl>(CE->getCalleeDecl());
      if (Fn) {
        const IdentifierInfo *FnI = Fn->getIdentifier();
        if (FnI && FnI->isStr("_Block_copy")) {
          e = CE->getArg(0)->IgnoreParenCasts();
        }
      }
    }
  }

  BlockExpr *block = dyn_cast<BlockExpr>(e);
  if (!block || !block->getBlockDecl()->capturesVariable(owner.Variable))
    return nullptr;

  FindCaptureVisitor visitor(S.Context, owner.Variable);
  visitor.Visit(block->getBlockDecl()->getBody());
  return visitor.VarWillBeReased ? nullptr : visitor.Capturer;
}

static void diagnoseRetainCycle(Sema &S, Expr *capturer,
                                RetainCycleOwner &owner) {
  assert(capturer);
  assert(owner.Variable && owner.Loc.isValid());

  S.Diag(capturer->getExprLoc(), diag::warn_arc_retain_cycle)
      << owner.Variable << capturer->getSourceRange();
  S.Diag(owner.Loc, diag::note_arc_retain_cycle_owner)
      << owner.Indirect << owner.Range;
}

/// Check for a keyword selector that starts with the word 'add' or
/// 'set'.
static bool isSetterLikeSelector(Selector sel) {
  if (sel.isUnarySelector())
    return false;

  StringRef str = sel.getNameForSlot(0);
  str = str.ltrim('_');
  if (str.starts_with("set"))
    str = str.substr(3);
  else if (str.starts_with("add")) {
    // Specially allow 'addOperationWithBlock:'.
    if (sel.getNumArgs() == 1 && str.starts_with("addOperationWithBlock"))
      return false;
    str = str.substr(3);
  } else
    return false;

  if (str.empty())
    return true;
  return !isLowercase(str.front());
}

static std::optional<int>
GetNSMutableArrayArgumentIndex(SemaObjC &S, ObjCMessageExpr *Message) {
  bool IsMutableArray = S.NSAPIObj->isSubclassOfNSClass(
      Message->getReceiverInterface(), NSAPI::ClassId_NSMutableArray);
  if (!IsMutableArray) {
    return std::nullopt;
  }

  Selector Sel = Message->getSelector();

  std::optional<NSAPI::NSArrayMethodKind> MKOpt =
      S.NSAPIObj->getNSArrayMethodKind(Sel);
  if (!MKOpt) {
    return std::nullopt;
  }

  NSAPI::NSArrayMethodKind MK = *MKOpt;

  switch (MK) {
  case NSAPI::NSMutableArr_addObject:
  case NSAPI::NSMutableArr_insertObjectAtIndex:
  case NSAPI::NSMutableArr_setObjectAtIndexedSubscript:
    return 0;
  case NSAPI::NSMutableArr_replaceObjectAtIndex:
    return 1;

  default:
    return std::nullopt;
  }

  return std::nullopt;
}

static std::optional<int>
GetNSMutableDictionaryArgumentIndex(SemaObjC &S, ObjCMessageExpr *Message) {
  bool IsMutableDictionary = S.NSAPIObj->isSubclassOfNSClass(
      Message->getReceiverInterface(), NSAPI::ClassId_NSMutableDictionary);
  if (!IsMutableDictionary) {
    return std::nullopt;
  }

  Selector Sel = Message->getSelector();

  std::optional<NSAPI::NSDictionaryMethodKind> MKOpt =
      S.NSAPIObj->getNSDictionaryMethodKind(Sel);
  if (!MKOpt) {
    return std::nullopt;
  }

  NSAPI::NSDictionaryMethodKind MK = *MKOpt;

  switch (MK) {
  case NSAPI::NSMutableDict_setObjectForKey:
  case NSAPI::NSMutableDict_setValueForKey:
  case NSAPI::NSMutableDict_setObjectForKeyedSubscript:
    return 0;

  default:
    return std::nullopt;
  }

  return std::nullopt;
}

static std::optional<int> GetNSSetArgumentIndex(SemaObjC &S,
                                                ObjCMessageExpr *Message) {
  bool IsMutableSet = S.NSAPIObj->isSubclassOfNSClass(
      Message->getReceiverInterface(), NSAPI::ClassId_NSMutableSet);

  bool IsMutableOrderedSet = S.NSAPIObj->isSubclassOfNSClass(
      Message->getReceiverInterface(), NSAPI::ClassId_NSMutableOrderedSet);
  if (!IsMutableSet && !IsMutableOrderedSet) {
    return std::nullopt;
  }

  Selector Sel = Message->getSelector();

  std::optional<NSAPI::NSSetMethodKind> MKOpt =
      S.NSAPIObj->getNSSetMethodKind(Sel);
  if (!MKOpt) {
    return std::nullopt;
  }

  NSAPI::NSSetMethodKind MK = *MKOpt;

  switch (MK) {
  case NSAPI::NSMutableSet_addObject:
  case NSAPI::NSOrderedSet_setObjectAtIndex:
  case NSAPI::NSOrderedSet_setObjectAtIndexedSubscript:
  case NSAPI::NSOrderedSet_insertObjectAtIndex:
    return 0;
  case NSAPI::NSOrderedSet_replaceObjectAtIndexWithObject:
    return 1;
  }

  return std::nullopt;
}

void SemaObjC::CheckObjCCircularContainer(ObjCMessageExpr *Message) {
  if (!Message->isInstanceMessage()) {
    return;
  }

  std::optional<int> ArgOpt;

  if (!(ArgOpt = GetNSMutableArrayArgumentIndex(*this, Message)) &&
      !(ArgOpt = GetNSMutableDictionaryArgumentIndex(*this, Message)) &&
      !(ArgOpt = GetNSSetArgumentIndex(*this, Message))) {
    return;
  }

  int ArgIndex = *ArgOpt;

  Expr *Arg = Message->getArg(ArgIndex)->IgnoreImpCasts();
  if (OpaqueValueExpr *OE = dyn_cast<OpaqueValueExpr>(Arg)) {
    Arg = OE->getSourceExpr()->IgnoreImpCasts();
  }

  if (Message->getReceiverKind() == ObjCMessageExpr::SuperInstance) {
    if (DeclRefExpr *ArgRE = dyn_cast<DeclRefExpr>(Arg)) {
      if (ArgRE->isObjCSelfExpr()) {
        Diag(Message->getSourceRange().getBegin(),
             diag::warn_objc_circular_container)
            << ArgRE->getDecl() << StringRef("'super'");
      }
    }
  } else {
    Expr *Receiver = Message->getInstanceReceiver()->IgnoreImpCasts();

    if (OpaqueValueExpr *OE = dyn_cast<OpaqueValueExpr>(Receiver)) {
      Receiver = OE->getSourceExpr()->IgnoreImpCasts();
    }

    if (DeclRefExpr *ReceiverRE = dyn_cast<DeclRefExpr>(Receiver)) {
      if (DeclRefExpr *ArgRE = dyn_cast<DeclRefExpr>(Arg)) {
        if (ReceiverRE->getDecl() == ArgRE->getDecl()) {
          ValueDecl *Decl = ReceiverRE->getDecl();
          Diag(Message->getSourceRange().getBegin(),
               diag::warn_objc_circular_container)
              << Decl << Decl;
          if (!ArgRE->isObjCSelfExpr()) {
            Diag(Decl->getLocation(),
                 diag::note_objc_circular_container_declared_here)
                << Decl;
          }
        }
      }
    } else if (ObjCIvarRefExpr *IvarRE = dyn_cast<ObjCIvarRefExpr>(Receiver)) {
      if (ObjCIvarRefExpr *IvarArgRE = dyn_cast<ObjCIvarRefExpr>(Arg)) {
        if (IvarRE->getDecl() == IvarArgRE->getDecl()) {
          ObjCIvarDecl *Decl = IvarRE->getDecl();
          Diag(Message->getSourceRange().getBegin(),
               diag::warn_objc_circular_container)
              << Decl << Decl;
          Diag(Decl->getLocation(),
               diag::note_objc_circular_container_declared_here)
              << Decl;
        }
      }
    }
  }
}

/// Check a message send to see if it's likely to cause a retain cycle.
void SemaObjC::checkRetainCycles(ObjCMessageExpr *msg) {
  // Only check instance methods whose selector looks like a setter.
  if (!msg->isInstanceMessage() || !isSetterLikeSelector(msg->getSelector()))
    return;

  // Try to find a variable that the receiver is strongly owned by.
  RetainCycleOwner owner;
  if (msg->getReceiverKind() == ObjCMessageExpr::Instance) {
    if (!findRetainCycleOwner(SemaRef, msg->getInstanceReceiver(), owner))
      return;
  } else {
    assert(msg->getReceiverKind() == ObjCMessageExpr::SuperInstance);
    owner.Variable = SemaRef.getCurMethodDecl()->getSelfDecl();
    owner.Loc = msg->getSuperLoc();
    owner.Range = msg->getSuperLoc();
  }

  // Check whether the receiver is captured by any of the arguments.
  const ObjCMethodDecl *MD = msg->getMethodDecl();
  for (unsigned i = 0, e = msg->getNumArgs(); i != e; ++i) {
    if (Expr *capturer = findCapturingExpr(SemaRef, msg->getArg(i), owner)) {
      // noescape blocks should not be retained by the method.
      if (MD && MD->parameters()[i]->hasAttr<NoEscapeAttr>())
        continue;
      return diagnoseRetainCycle(SemaRef, capturer, owner);
    }
  }
}

/// Check a property assign to see if it's likely to cause a retain cycle.
void SemaObjC::checkRetainCycles(Expr *receiver, Expr *argument) {
  RetainCycleOwner owner;
  if (!findRetainCycleOwner(SemaRef, receiver, owner))
    return;

  if (Expr *capturer = findCapturingExpr(SemaRef, argument, owner))
    diagnoseRetainCycle(SemaRef, capturer, owner);
}

void SemaObjC::checkRetainCycles(VarDecl *Var, Expr *Init) {
  RetainCycleOwner Owner;
  if (!considerVariable(Var, /*DeclRefExpr=*/nullptr, Owner))
    return;

  // Because we don't have an expression for the variable, we have to set the
  // location explicitly here.
  Owner.Loc = Var->getLocation();
  Owner.Range = Var->getSourceRange();

  if (Expr *Capturer = findCapturingExpr(SemaRef, Init, Owner))
    diagnoseRetainCycle(SemaRef, Capturer, Owner);
}

/// CheckObjCString - Checks that the argument to the builtin
/// CFString constructor is correct
/// Note: It might also make sense to do the UTF-16 conversion here (would
/// simplify the backend).
bool SemaObjC::CheckObjCString(Expr *Arg) {
  Arg = Arg->IgnoreParenCasts();
  StringLiteral *Literal = dyn_cast<StringLiteral>(Arg);

  if (!Literal || !Literal->isOrdinary()) {
    Diag(Arg->getBeginLoc(), diag::err_cfstring_literal_not_string_constant)
        << Arg->getSourceRange();
    return true;
  }

  if (Literal->containsNonAsciiOrNull()) {
    StringRef String = Literal->getString();
    unsigned NumBytes = String.size();
    SmallVector<llvm::UTF16, 128> ToBuf(NumBytes);
    const llvm::UTF8 *FromPtr = (const llvm::UTF8 *)String.data();
    llvm::UTF16 *ToPtr = &ToBuf[0];

    llvm::ConversionResult Result =
        llvm::ConvertUTF8toUTF16(&FromPtr, FromPtr + NumBytes, &ToPtr,
                                 ToPtr + NumBytes, llvm::strictConversion);
    // Check for conversion failure.
    if (Result != llvm::conversionOK)
      Diag(Arg->getBeginLoc(), diag::warn_cfstring_truncated)
          << Arg->getSourceRange();
  }
  return false;
}

bool SemaObjC::CheckObjCMethodCall(ObjCMethodDecl *Method, SourceLocation lbrac,
                                   ArrayRef<const Expr *> Args) {
  Sema::VariadicCallType CallType =
      Method->isVariadic() ? Sema::VariadicMethod : Sema::VariadicDoesNotApply;

  SemaRef.checkCall(Method, nullptr, /*ThisArg=*/nullptr, Args,
                    /*IsMemberFunction=*/false, lbrac, Method->getSourceRange(),
                    CallType);

  SemaRef.CheckTCBEnforcement(lbrac, Method);

  return false;
}

const DeclContext *SemaObjC::getCurObjCLexicalContext() const {
  const DeclContext *DC = SemaRef.getCurLexicalContext();
  // A category implicitly has the attribute of the interface.
  if (const ObjCCategoryDecl *CatD = dyn_cast<ObjCCategoryDecl>(DC))
    DC = CatD->getClassInterface();
  return DC;
}

/// Retrieve the identifier "NSError".
IdentifierInfo *SemaObjC::getNSErrorIdent() {
  if (!Ident_NSError)
    Ident_NSError = SemaRef.PP.getIdentifierInfo("NSError");

  return Ident_NSError;
}

void SemaObjC::ActOnObjCContainerStartDefinition(ObjCContainerDecl *IDecl) {
  assert(
      IDecl->getLexicalParent() == SemaRef.CurContext &&
      "The next DeclContext should be lexically contained in the current one.");
  SemaRef.CurContext = IDecl;
}

void SemaObjC::ActOnObjCContainerFinishDefinition() {
  // Exit this scope of this interface definition.
  SemaRef.PopDeclContext();
}

void SemaObjC::ActOnObjCTemporaryExitContainerContext(
    ObjCContainerDecl *ObjCCtx) {
  assert(ObjCCtx == SemaRef.CurContext && "Mismatch of container contexts");
  SemaRef.OriginalLexicalContext = ObjCCtx;
  ActOnObjCContainerFinishDefinition();
}

void SemaObjC::ActOnObjCReenterContainerContext(ObjCContainerDecl *ObjCCtx) {
  ActOnObjCContainerStartDefinition(ObjCCtx);
  SemaRef.OriginalLexicalContext = nullptr;
}

/// Find the protocol with the given name, if any.
ObjCProtocolDecl *SemaObjC::LookupProtocol(IdentifierInfo *II,
                                           SourceLocation IdLoc,
                                           RedeclarationKind Redecl) {
  Decl *D = SemaRef.LookupSingleName(SemaRef.TUScope, II, IdLoc,
                                     Sema::LookupObjCProtocolName, Redecl);
  return cast_or_null<ObjCProtocolDecl>(D);
}

/// Determine whether this is an Objective-C writeback conversion,
/// used for parameter passing when performing automatic reference counting.
///
/// \param FromType The type we're converting form.
///
/// \param ToType The type we're converting to.
///
/// \param ConvertedType The type that will be produced after applying
/// this conversion.
bool SemaObjC::isObjCWritebackConversion(QualType FromType, QualType ToType,
                                         QualType &ConvertedType) {
  ASTContext &Context = getASTContext();
  if (!getLangOpts().ObjCAutoRefCount ||
      Context.hasSameUnqualifiedType(FromType, ToType))
    return false;

  // Parameter must be a pointer to __autoreleasing (with no other qualifiers).
  QualType ToPointee;
  if (const PointerType *ToPointer = ToType->getAs<PointerType>())
    ToPointee = ToPointer->getPointeeType();
  else
    return false;

  Qualifiers ToQuals = ToPointee.getQualifiers();
  if (!ToPointee->isObjCLifetimeType() ||
      ToQuals.getObjCLifetime() != Qualifiers::OCL_Autoreleasing ||
      !ToQuals.withoutObjCLifetime().empty())
    return false;

  // Argument must be a pointer to __strong to __weak.
  QualType FromPointee;
  if (const PointerType *FromPointer = FromType->getAs<PointerType>())
    FromPointee = FromPointer->getPointeeType();
  else
    return false;

  Qualifiers FromQuals = FromPointee.getQualifiers();
  if (!FromPointee->isObjCLifetimeType() ||
      (FromQuals.getObjCLifetime() != Qualifiers::OCL_Strong &&
       FromQuals.getObjCLifetime() != Qualifiers::OCL_Weak))
    return false;

  // Make sure that we have compatible qualifiers.
  FromQuals.setObjCLifetime(Qualifiers::OCL_Autoreleasing);
  if (!ToQuals.compatiblyIncludes(FromQuals))
    return false;

  // Remove qualifiers from the pointee type we're converting from; they
  // aren't used in the compatibility check belong, and we'll be adding back
  // qualifiers (with __autoreleasing) if the compatibility check succeeds.
  FromPointee = FromPointee.getUnqualifiedType();

  // The unqualified form of the pointee types must be compatible.
  ToPointee = ToPointee.getUnqualifiedType();
  bool IncompatibleObjC;
  if (Context.typesAreCompatible(FromPointee, ToPointee))
    FromPointee = ToPointee;
  else if (!SemaRef.isObjCPointerConversion(FromPointee, ToPointee, FromPointee,
                                            IncompatibleObjC))
    return false;

  /// Construct the type we're converting to, which is a pointer to
  /// __autoreleasing pointee.
  FromPointee = Context.getQualifiedType(FromPointee, FromQuals);
  ConvertedType = Context.getPointerType(FromPointee);
  return true;
}

/// CheckSubscriptingKind - This routine decide what type
/// of indexing represented by "FromE" is being done.
SemaObjC::ObjCSubscriptKind SemaObjC::CheckSubscriptingKind(Expr *FromE) {
  // If the expression already has integral or enumeration type, we're golden.
  QualType T = FromE->getType();
  if (T->isIntegralOrEnumerationType())
    return SemaObjC::OS_Array;

  // If we don't have a class type in C++, there's no way we can get an
  // expression of integral or enumeration type.
  const RecordType *RecordTy = T->getAs<RecordType>();
  if (!RecordTy && (T->isObjCObjectPointerType() || T->isVoidPointerType()))
    // All other scalar cases are assumed to be dictionary indexing which
    // caller handles, with diagnostics if needed.
    return SemaObjC::OS_Dictionary;
  if (!getLangOpts().CPlusPlus || !RecordTy || RecordTy->isIncompleteType()) {
    // No indexing can be done. Issue diagnostics and quit.
    const Expr *IndexExpr = FromE->IgnoreParenImpCasts();
    if (isa<StringLiteral>(IndexExpr))
      Diag(FromE->getExprLoc(), diag::err_objc_subscript_pointer)
          << T << FixItHint::CreateInsertion(FromE->getExprLoc(), "@");
    else
      Diag(FromE->getExprLoc(), diag::err_objc_subscript_type_conversion) << T;
    return SemaObjC::OS_Error;
  }

  // We must have a complete class type.
  if (SemaRef.RequireCompleteType(FromE->getExprLoc(), T,
                                  diag::err_objc_index_incomplete_class_type,
                                  FromE))
    return SemaObjC::OS_Error;

  // Look for a conversion to an integral, enumeration type, or
  // objective-C pointer type.
  int NoIntegrals = 0, NoObjCIdPointers = 0;
  SmallVector<CXXConversionDecl *, 4> ConversionDecls;

  for (NamedDecl *D : cast<CXXRecordDecl>(RecordTy->getDecl())
                          ->getVisibleConversionFunctions()) {
    if (CXXConversionDecl *Conversion =
            dyn_cast<CXXConversionDecl>(D->getUnderlyingDecl())) {
      QualType CT = Conversion->getConversionType().getNonReferenceType();
      if (CT->isIntegralOrEnumerationType()) {
        ++NoIntegrals;
        ConversionDecls.push_back(Conversion);
      } else if (CT->isObjCIdType() || CT->isBlockPointerType()) {
        ++NoObjCIdPointers;
        ConversionDecls.push_back(Conversion);
      }
    }
  }
  if (NoIntegrals == 1 && NoObjCIdPointers == 0)
    return SemaObjC::OS_Array;
  if (NoIntegrals == 0 && NoObjCIdPointers == 1)
    return SemaObjC::OS_Dictionary;
  if (NoIntegrals == 0 && NoObjCIdPointers == 0) {
    // No conversion function was found. Issue diagnostic and return.
    Diag(FromE->getExprLoc(), diag::err_objc_subscript_type_conversion)
        << FromE->getType();
    return SemaObjC::OS_Error;
  }
  Diag(FromE->getExprLoc(), diag::err_objc_multiple_subscript_type_conversion)
      << FromE->getType();
  for (unsigned int i = 0; i < ConversionDecls.size(); i++)
    Diag(ConversionDecls[i]->getLocation(),
         diag::note_conv_function_declared_at);

  return SemaObjC::OS_Error;
}

void SemaObjC::AddCFAuditedAttribute(Decl *D) {
  ASTContext &Context = getASTContext();
  IdentifierInfo *Ident;
  SourceLocation Loc;
  std::tie(Ident, Loc) = SemaRef.PP.getPragmaARCCFCodeAuditedInfo();
  if (!Loc.isValid())
    return;

  // Don't add a redundant or conflicting attribute.
  if (D->hasAttr<CFAuditedTransferAttr>() ||
      D->hasAttr<CFUnknownTransferAttr>())
    return;

  AttributeCommonInfo Info(Ident, SourceRange(Loc),
                           AttributeCommonInfo::Form::Pragma());
  D->addAttr(CFAuditedTransferAttr::CreateImplicit(Context, Info));
}

bool SemaObjC::isCFError(RecordDecl *RD) {
  // If we already know about CFError, test it directly.
  if (CFError)
    return CFError == RD;

  // Check whether this is CFError, which we identify based on its bridge to
  // NSError. CFErrorRef used to be declared with "objc_bridge" but is now
  // declared with "objc_bridge_mutable", so look for either one of the two
  // attributes.
  if (RD->getTagKind() == TagTypeKind::Struct) {
    IdentifierInfo *bridgedType = nullptr;
    if (auto bridgeAttr = RD->getAttr<ObjCBridgeAttr>())
      bridgedType = bridgeAttr->getBridgedType();
    else if (auto bridgeAttr = RD->getAttr<ObjCBridgeMutableAttr>())
      bridgedType = bridgeAttr->getBridgedType();

    if (bridgedType == getNSErrorIdent()) {
      CFError = RD;
      return true;
    }
  }

  return false;
}

bool SemaObjC::isNSStringType(QualType T, bool AllowNSAttributedString) {
  const auto *PT = T->getAs<ObjCObjectPointerType>();
  if (!PT)
    return false;

  ObjCInterfaceDecl *Cls = PT->getObjectType()->getInterface();
  if (!Cls)
    return false;

  IdentifierInfo *ClsName = Cls->getIdentifier();

  if (AllowNSAttributedString &&
      ClsName == &getASTContext().Idents.get("NSAttributedString"))
    return true;
  // FIXME: Should we walk the chain of classes?
  return ClsName == &getASTContext().Idents.get("NSString") ||
         ClsName == &getASTContext().Idents.get("NSMutableString");
}

bool SemaObjC::isCFStringType(QualType T) {
  const auto *PT = T->getAs<PointerType>();
  if (!PT)
    return false;

  const auto *RT = PT->getPointeeType()->getAs<RecordType>();
  if (!RT)
    return false;

  const RecordDecl *RD = RT->getDecl();
  if (RD->getTagKind() != TagTypeKind::Struct)
    return false;

  return RD->getIdentifier() == &getASTContext().Idents.get("__CFString");
}

static bool checkIBOutletCommon(Sema &S, Decl *D, const ParsedAttr &AL) {
  // The IBOutlet/IBOutletCollection attributes only apply to instance
  // variables or properties of Objective-C classes.  The outlet must also
  // have an object reference type.
  if (const auto *VD = dyn_cast<ObjCIvarDecl>(D)) {
    if (!VD->getType()->getAs<ObjCObjectPointerType>()) {
      S.Diag(AL.getLoc(), diag::warn_iboutlet_object_type)
          << AL << VD->getType() << 0;
      return false;
    }
  } else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    if (!PD->getType()->getAs<ObjCObjectPointerType>()) {
      S.Diag(AL.getLoc(), diag::warn_iboutlet_object_type)
          << AL << PD->getType() << 1;
      return false;
    }
  } else {
    S.Diag(AL.getLoc(), diag::warn_attribute_iboutlet) << AL;
    return false;
  }

  return true;
}

void SemaObjC::handleIBOutlet(Decl *D, const ParsedAttr &AL) {
  if (!checkIBOutletCommon(SemaRef, D, AL))
    return;

  D->addAttr(::new (getASTContext()) IBOutletAttr(getASTContext(), AL));
}

void SemaObjC::handleIBOutletCollection(Decl *D, const ParsedAttr &AL) {

  ASTContext &Context = getASTContext();
  // The iboutletcollection attribute can have zero or one arguments.
  if (AL.getNumArgs() > 1) {
    Diag(AL.getLoc(), diag::err_attribute_wrong_number_arguments) << AL << 1;
    return;
  }

  if (!checkIBOutletCommon(SemaRef, D, AL))
    return;

  ParsedType PT;

  if (AL.hasParsedType())
    PT = AL.getTypeArg();
  else {
    PT = SemaRef.getTypeName(
        Context.Idents.get("NSObject"), AL.getLoc(),
        SemaRef.getScopeForContext(D->getDeclContext()->getParent()));
    if (!PT) {
      Diag(AL.getLoc(), diag::err_iboutletcollection_type) << "NSObject";
      return;
    }
  }

  TypeSourceInfo *QTLoc = nullptr;
  QualType QT = SemaRef.GetTypeFromParser(PT, &QTLoc);
  if (!QTLoc)
    QTLoc = Context.getTrivialTypeSourceInfo(QT, AL.getLoc());

  // Diagnose use of non-object type in iboutletcollection attribute.
  // FIXME. Gnu attribute extension ignores use of builtin types in
  // attributes. So, __attribute__((iboutletcollection(char))) will be
  // treated as __attribute__((iboutletcollection())).
  if (!QT->isObjCIdType() && !QT->isObjCObjectType()) {
    Diag(AL.getLoc(), QT->isBuiltinType()
                          ? diag::err_iboutletcollection_builtintype
                          : diag::err_iboutletcollection_type)
        << QT;
    return;
  }

  D->addAttr(::new (Context) IBOutletCollectionAttr(Context, AL, QTLoc));
}

void SemaObjC::handleSuppresProtocolAttr(Decl *D, const ParsedAttr &AL) {
  if (!cast<ObjCProtocolDecl>(D)->isThisDeclarationADefinition()) {
    Diag(AL.getLoc(), diag::err_objc_attr_protocol_requires_definition)
        << AL << AL.getRange();
    return;
  }

  D->addAttr(::new (getASTContext())
                 ObjCExplicitProtocolImplAttr(getASTContext(), AL));
}

void SemaObjC::handleDirectAttr(Decl *D, const ParsedAttr &AL) {
  // objc_direct cannot be set on methods declared in the context of a protocol
  if (isa<ObjCProtocolDecl>(D->getDeclContext())) {
    Diag(AL.getLoc(), diag::err_objc_direct_on_protocol) << false;
    return;
  }

  if (getLangOpts().ObjCRuntime.allowsDirectDispatch()) {
    handleSimpleAttribute<ObjCDirectAttr>(*this, D, AL);
  } else {
    Diag(AL.getLoc(), diag::warn_objc_direct_ignored) << AL;
  }
}

void SemaObjC::handleDirectMembersAttr(Decl *D, const ParsedAttr &AL) {
  if (getLangOpts().ObjCRuntime.allowsDirectDispatch()) {
    handleSimpleAttribute<ObjCDirectMembersAttr>(*this, D, AL);
  } else {
    Diag(AL.getLoc(), diag::warn_objc_direct_ignored) << AL;
  }
}

void SemaObjC::handleMethodFamilyAttr(Decl *D, const ParsedAttr &AL) {
  const auto *M = cast<ObjCMethodDecl>(D);
  if (!AL.isArgIdent(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierLoc *IL = AL.getArgAsIdent(0);
  ObjCMethodFamilyAttr::FamilyKind F;
  if (!ObjCMethodFamilyAttr::ConvertStrToFamilyKind(IL->Ident->getName(), F)) {
    Diag(IL->Loc, diag::warn_attribute_type_not_supported) << AL << IL->Ident;
    return;
  }

  if (F == ObjCMethodFamilyAttr::OMF_init &&
      !M->getReturnType()->isObjCObjectPointerType()) {
    Diag(M->getLocation(), diag::err_init_method_bad_return_type)
        << M->getReturnType();
    // Ignore the attribute.
    return;
  }

  D->addAttr(new (getASTContext())
                 ObjCMethodFamilyAttr(getASTContext(), AL, F));
}

void SemaObjC::handleNSObject(Decl *D, const ParsedAttr &AL) {
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    QualType T = TD->getUnderlyingType();
    if (!T->isCARCBridgableType()) {
      Diag(TD->getLocation(), diag::err_nsobject_attribute);
      return;
    }
  } else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    QualType T = PD->getType();
    if (!T->isCARCBridgableType()) {
      Diag(PD->getLocation(), diag::err_nsobject_attribute);
      return;
    }
  } else {
    // It is okay to include this attribute on properties, e.g.:
    //
    //  @property (retain, nonatomic) struct Bork *Q __attribute__((NSObject));
    //
    // In this case it follows tradition and suppresses an error in the above
    // case.
    Diag(D->getLocation(), diag::warn_nsobject_attribute);
  }
  D->addAttr(::new (getASTContext()) ObjCNSObjectAttr(getASTContext(), AL));
}

void SemaObjC::handleIndependentClass(Decl *D, const ParsedAttr &AL) {
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    QualType T = TD->getUnderlyingType();
    if (!T->isObjCObjectPointerType()) {
      Diag(TD->getLocation(), diag::warn_ptr_independentclass_attribute);
      return;
    }
  } else {
    Diag(D->getLocation(), diag::warn_independentclass_attribute);
    return;
  }
  D->addAttr(::new (getASTContext())
                 ObjCIndependentClassAttr(getASTContext(), AL));
}

void SemaObjC::handleBlocksAttr(Decl *D, const ParsedAttr &AL) {
  if (!AL.isArgIdent(0)) {
    Diag(AL.getLoc(), diag::err_attribute_argument_n_type)
        << AL << 1 << AANT_ArgumentIdentifier;
    return;
  }

  IdentifierInfo *II = AL.getArgAsIdent(0)->Ident;
  BlocksAttr::BlockType type;
  if (!BlocksAttr::ConvertStrToBlockType(II->getName(), type)) {
    Diag(AL.getLoc(), diag::warn_attribute_type_not_supported) << AL << II;
    return;
  }

  D->addAttr(::new (getASTContext()) BlocksAttr(getASTContext(), AL, type));
}

static bool isValidSubjectOfNSReturnsRetainedAttribute(QualType QT) {
  return QT->isDependentType() || QT->isObjCRetainableType();
}

static bool isValidSubjectOfNSAttribute(QualType QT) {
  return QT->isDependentType() || QT->isObjCObjectPointerType() ||
         QT->isObjCNSObjectType();
}

static bool isValidSubjectOfCFAttribute(QualType QT) {
  return QT->isDependentType() || QT->isPointerType() ||
         isValidSubjectOfNSAttribute(QT);
}

static bool isValidSubjectOfOSAttribute(QualType QT) {
  if (QT->isDependentType())
    return true;
  QualType PT = QT->getPointeeType();
  return !PT.isNull() && PT->getAsCXXRecordDecl() != nullptr;
}

void SemaObjC::AddXConsumedAttr(Decl *D, const AttributeCommonInfo &CI,
                                Sema::RetainOwnershipKind K,
                                bool IsTemplateInstantiation) {
  ValueDecl *VD = cast<ValueDecl>(D);
  switch (K) {
  case Sema::RetainOwnershipKind::OS:
    handleSimpleAttributeOrDiagnose<OSConsumedAttr>(
        *this, VD, CI, isValidSubjectOfOSAttribute(VD->getType()),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*ExtraArgs=*/CI.getRange(), "os_consumed", /*pointers*/ 1);
    return;
  case Sema::RetainOwnershipKind::NS:
    handleSimpleAttributeOrDiagnose<NSConsumedAttr>(
        *this, VD, CI, isValidSubjectOfNSAttribute(VD->getType()),

        // These attributes are normally just advisory, but in ARC, ns_consumed
        // is significant.  Allow non-dependent code to contain inappropriate
        // attributes even in ARC, but require template instantiations to be
        // set up correctly.
        ((IsTemplateInstantiation && getLangOpts().ObjCAutoRefCount)
             ? diag::err_ns_attribute_wrong_parameter_type
             : diag::warn_ns_attribute_wrong_parameter_type),
        /*ExtraArgs=*/CI.getRange(), "ns_consumed", /*objc pointers*/ 0);
    return;
  case Sema::RetainOwnershipKind::CF:
    handleSimpleAttributeOrDiagnose<CFConsumedAttr>(
        *this, VD, CI, isValidSubjectOfCFAttribute(VD->getType()),
        diag::warn_ns_attribute_wrong_parameter_type,
        /*ExtraArgs=*/CI.getRange(), "cf_consumed", /*pointers*/ 1);
    return;
  }
}

Sema::RetainOwnershipKind
SemaObjC::parsedAttrToRetainOwnershipKind(const ParsedAttr &AL) {
  switch (AL.getKind()) {
  case ParsedAttr::AT_CFConsumed:
  case ParsedAttr::AT_CFReturnsRetained:
  case ParsedAttr::AT_CFReturnsNotRetained:
    return Sema::RetainOwnershipKind::CF;
  case ParsedAttr::AT_OSConsumesThis:
  case ParsedAttr::AT_OSConsumed:
  case ParsedAttr::AT_OSReturnsRetained:
  case ParsedAttr::AT_OSReturnsNotRetained:
  case ParsedAttr::AT_OSReturnsRetainedOnZero:
  case ParsedAttr::AT_OSReturnsRetainedOnNonZero:
    return Sema::RetainOwnershipKind::OS;
  case ParsedAttr::AT_NSConsumesSelf:
  case ParsedAttr::AT_NSConsumed:
  case ParsedAttr::AT_NSReturnsRetained:
  case ParsedAttr::AT_NSReturnsNotRetained:
  case ParsedAttr::AT_NSReturnsAutoreleased:
    return Sema::RetainOwnershipKind::NS;
  default:
    llvm_unreachable("Wrong argument supplied");
  }
}

bool SemaObjC::checkNSReturnsRetainedReturnType(SourceLocation Loc,
                                                QualType QT) {
  if (isValidSubjectOfNSReturnsRetainedAttribute(QT))
    return false;

  Diag(Loc, diag::warn_ns_attribute_wrong_return_type)
      << "'ns_returns_retained'" << 0 << 0;
  return true;
}

/// \return whether the parameter is a pointer to OSObject pointer.
bool SemaObjC::isValidOSObjectOutParameter(const Decl *D) {
  const auto *PVD = dyn_cast<ParmVarDecl>(D);
  if (!PVD)
    return false;
  QualType QT = PVD->getType();
  QualType PT = QT->getPointeeType();
  return !PT.isNull() && isValidSubjectOfOSAttribute(PT);
}

void SemaObjC::handleXReturnsXRetainedAttr(Decl *D, const ParsedAttr &AL) {
  QualType ReturnType;
  Sema::RetainOwnershipKind K = parsedAttrToRetainOwnershipKind(AL);

  if (const auto *MD = dyn_cast<ObjCMethodDecl>(D)) {
    ReturnType = MD->getReturnType();
  } else if (getLangOpts().ObjCAutoRefCount && hasDeclarator(D) &&
             (AL.getKind() == ParsedAttr::AT_NSReturnsRetained)) {
    return; // ignore: was handled as a type attribute
  } else if (const auto *PD = dyn_cast<ObjCPropertyDecl>(D)) {
    ReturnType = PD->getType();
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    ReturnType = FD->getReturnType();
  } else if (const auto *Param = dyn_cast<ParmVarDecl>(D)) {
    // Attributes on parameters are used for out-parameters,
    // passed as pointers-to-pointers.
    unsigned DiagID = K == Sema::RetainOwnershipKind::CF
                          ? /*pointer-to-CF-pointer*/ 2
                          : /*pointer-to-OSObject-pointer*/ 3;
    ReturnType = Param->getType()->getPointeeType();
    if (ReturnType.isNull()) {
      Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_parameter_type)
          << AL << DiagID << AL.getRange();
      return;
    }
  } else if (AL.isUsedAsTypeAttr()) {
    return;
  } else {
    AttributeDeclKind ExpectedDeclKind;
    switch (AL.getKind()) {
    default:
      llvm_unreachable("invalid ownership attribute");
    case ParsedAttr::AT_NSReturnsRetained:
    case ParsedAttr::AT_NSReturnsAutoreleased:
    case ParsedAttr::AT_NSReturnsNotRetained:
      ExpectedDeclKind = ExpectedFunctionOrMethod;
      break;

    case ParsedAttr::AT_OSReturnsRetained:
    case ParsedAttr::AT_OSReturnsNotRetained:
    case ParsedAttr::AT_CFReturnsRetained:
    case ParsedAttr::AT_CFReturnsNotRetained:
      ExpectedDeclKind = ExpectedFunctionMethodOrParameter;
      break;
    }
    Diag(D->getBeginLoc(), diag::warn_attribute_wrong_decl_type)
        << AL.getRange() << AL << AL.isRegularKeywordAttribute()
        << ExpectedDeclKind;
    return;
  }

  bool TypeOK;
  bool Cf;
  unsigned ParmDiagID = 2; // Pointer-to-CF-pointer
  switch (AL.getKind()) {
  default:
    llvm_unreachable("invalid ownership attribute");
  case ParsedAttr::AT_NSReturnsRetained:
    TypeOK = isValidSubjectOfNSReturnsRetainedAttribute(ReturnType);
    Cf = false;
    break;

  case ParsedAttr::AT_NSReturnsAutoreleased:
  case ParsedAttr::AT_NSReturnsNotRetained:
    TypeOK = isValidSubjectOfNSAttribute(ReturnType);
    Cf = false;
    break;

  case ParsedAttr::AT_CFReturnsRetained:
  case ParsedAttr::AT_CFReturnsNotRetained:
    TypeOK = isValidSubjectOfCFAttribute(ReturnType);
    Cf = true;
    break;

  case ParsedAttr::AT_OSReturnsRetained:
  case ParsedAttr::AT_OSReturnsNotRetained:
    TypeOK = isValidSubjectOfOSAttribute(ReturnType);
    Cf = true;
    ParmDiagID = 3; // Pointer-to-OSObject-pointer
    break;
  }

  if (!TypeOK) {
    if (AL.isUsedAsTypeAttr())
      return;

    if (isa<ParmVarDecl>(D)) {
      Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_parameter_type)
          << AL << ParmDiagID << AL.getRange();
    } else {
      // Needs to be kept in sync with warn_ns_attribute_wrong_return_type.
      enum : unsigned { Function, Method, Property } SubjectKind = Function;
      if (isa<ObjCMethodDecl>(D))
        SubjectKind = Method;
      else if (isa<ObjCPropertyDecl>(D))
        SubjectKind = Property;
      Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_return_type)
          << AL << SubjectKind << Cf << AL.getRange();
    }
    return;
  }

  switch (AL.getKind()) {
  default:
    llvm_unreachable("invalid ownership attribute");
  case ParsedAttr::AT_NSReturnsAutoreleased:
    handleSimpleAttribute<NSReturnsAutoreleasedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_CFReturnsNotRetained:
    handleSimpleAttribute<CFReturnsNotRetainedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_NSReturnsNotRetained:
    handleSimpleAttribute<NSReturnsNotRetainedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_CFReturnsRetained:
    handleSimpleAttribute<CFReturnsRetainedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_NSReturnsRetained:
    handleSimpleAttribute<NSReturnsRetainedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_OSReturnsRetained:
    handleSimpleAttribute<OSReturnsRetainedAttr>(*this, D, AL);
    return;
  case ParsedAttr::AT_OSReturnsNotRetained:
    handleSimpleAttribute<OSReturnsNotRetainedAttr>(*this, D, AL);
    return;
  };
}

void SemaObjC::handleReturnsInnerPointerAttr(Decl *D, const ParsedAttr &Attrs) {
  const int EP_ObjCMethod = 1;
  const int EP_ObjCProperty = 2;

  SourceLocation loc = Attrs.getLoc();
  QualType resultType;
  if (isa<ObjCMethodDecl>(D))
    resultType = cast<ObjCMethodDecl>(D)->getReturnType();
  else
    resultType = cast<ObjCPropertyDecl>(D)->getType();

  if (!resultType->isReferenceType() &&
      (!resultType->isPointerType() || resultType->isObjCRetainableType())) {
    Diag(D->getBeginLoc(), diag::warn_ns_attribute_wrong_return_type)
        << SourceRange(loc) << Attrs
        << (isa<ObjCMethodDecl>(D) ? EP_ObjCMethod : EP_ObjCProperty)
        << /*non-retainable pointer*/ 2;

    // Drop the attribute.
    return;
  }

  D->addAttr(::new (getASTContext())
                 ObjCReturnsInnerPointerAttr(getASTContext(), Attrs));
}

void SemaObjC::handleRequiresSuperAttr(Decl *D, const ParsedAttr &Attrs) {
  const auto *Method = cast<ObjCMethodDecl>(D);

  const DeclContext *DC = Method->getDeclContext();
  if (const auto *PDecl = dyn_cast_if_present<ObjCProtocolDecl>(DC)) {
    Diag(D->getBeginLoc(), diag::warn_objc_requires_super_protocol)
        << Attrs << 0;
    Diag(PDecl->getLocation(), diag::note_protocol_decl);
    return;
  }
  if (Method->getMethodFamily() == OMF_dealloc) {
    Diag(D->getBeginLoc(), diag::warn_objc_requires_super_protocol)
        << Attrs << 1;
    return;
  }

  D->addAttr(::new (getASTContext())
                 ObjCRequiresSuperAttr(getASTContext(), Attrs));
}

void SemaObjC::handleNSErrorDomain(Decl *D, const ParsedAttr &Attr) {
  if (!isa<TagDecl>(D)) {
    Diag(D->getBeginLoc(), diag::err_nserrordomain_invalid_decl) << 0;
    return;
  }

  IdentifierLoc *IdentLoc =
      Attr.isArgIdent(0) ? Attr.getArgAsIdent(0) : nullptr;
  if (!IdentLoc || !IdentLoc->Ident) {
    // Try to locate the argument directly.
    SourceLocation Loc = Attr.getLoc();
    if (Attr.isArgExpr(0) && Attr.getArgAsExpr(0))
      Loc = Attr.getArgAsExpr(0)->getBeginLoc();

    Diag(Loc, diag::err_nserrordomain_invalid_decl) << 0;
    return;
  }

  // Verify that the identifier is a valid decl in the C decl namespace.
  LookupResult Result(SemaRef, DeclarationName(IdentLoc->Ident),
                      SourceLocation(),
                      Sema::LookupNameKind::LookupOrdinaryName);
  if (!SemaRef.LookupName(Result, SemaRef.TUScope) ||
      !Result.getAsSingle<VarDecl>()) {
    Diag(IdentLoc->Loc, diag::err_nserrordomain_invalid_decl)
        << 1 << IdentLoc->Ident;
    return;
  }

  D->addAttr(::new (getASTContext())
                 NSErrorDomainAttr(getASTContext(), Attr, IdentLoc->Ident));
}

void SemaObjC::handleBridgeAttr(Decl *D, const ParsedAttr &AL) {
  IdentifierLoc *Parm = AL.isArgIdent(0) ? AL.getArgAsIdent(0) : nullptr;

  if (!Parm) {
    Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }

  // Typedefs only allow objc_bridge(id) and have some additional checking.
  if (const auto *TD = dyn_cast<TypedefNameDecl>(D)) {
    if (!Parm->Ident->isStr("id")) {
      Diag(AL.getLoc(), diag::err_objc_attr_typedef_not_id) << AL;
      return;
    }

    // Only allow 'cv void *'.
    QualType T = TD->getUnderlyingType();
    if (!T->isVoidPointerType()) {
      Diag(AL.getLoc(), diag::err_objc_attr_typedef_not_void_pointer);
      return;
    }
  }

  D->addAttr(::new (getASTContext())
                 ObjCBridgeAttr(getASTContext(), AL, Parm->Ident));
}

void SemaObjC::handleBridgeMutableAttr(Decl *D, const ParsedAttr &AL) {
  IdentifierLoc *Parm = AL.isArgIdent(0) ? AL.getArgAsIdent(0) : nullptr;

  if (!Parm) {
    Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }

  D->addAttr(::new (getASTContext())
                 ObjCBridgeMutableAttr(getASTContext(), AL, Parm->Ident));
}

void SemaObjC::handleBridgeRelatedAttr(Decl *D, const ParsedAttr &AL) {
  IdentifierInfo *RelatedClass =
      AL.isArgIdent(0) ? AL.getArgAsIdent(0)->Ident : nullptr;
  if (!RelatedClass) {
    Diag(D->getBeginLoc(), diag::err_objc_attr_not_id) << AL << 0;
    return;
  }
  IdentifierInfo *ClassMethod =
      AL.getArgAsIdent(1) ? AL.getArgAsIdent(1)->Ident : nullptr;
  IdentifierInfo *InstanceMethod =
      AL.getArgAsIdent(2) ? AL.getArgAsIdent(2)->Ident : nullptr;
  D->addAttr(::new (getASTContext()) ObjCBridgeRelatedAttr(
      getASTContext(), AL, RelatedClass, ClassMethod, InstanceMethod));
}

void SemaObjC::handleDesignatedInitializer(Decl *D, const ParsedAttr &AL) {
  DeclContext *Ctx = D->getDeclContext();

  // This attribute can only be applied to methods in interfaces or class
  // extensions.
  if (!isa<ObjCInterfaceDecl>(Ctx) &&
      !(isa<ObjCCategoryDecl>(Ctx) &&
        cast<ObjCCategoryDecl>(Ctx)->IsClassExtension())) {
    Diag(D->getLocation(), diag::err_designated_init_attr_non_init);
    return;
  }

  ObjCInterfaceDecl *IFace;
  if (auto *CatDecl = dyn_cast<ObjCCategoryDecl>(Ctx))
    IFace = CatDecl->getClassInterface();
  else
    IFace = cast<ObjCInterfaceDecl>(Ctx);

  if (!IFace)
    return;

  IFace->setHasDesignatedInitializers();
  D->addAttr(::new (getASTContext())
                 ObjCDesignatedInitializerAttr(getASTContext(), AL));
}

void SemaObjC::handleRuntimeName(Decl *D, const ParsedAttr &AL) {
  StringRef MetaDataName;
  if (!SemaRef.checkStringLiteralArgumentAttr(AL, 0, MetaDataName))
    return;
  D->addAttr(::new (getASTContext())
                 ObjCRuntimeNameAttr(getASTContext(), AL, MetaDataName));
}

// When a user wants to use objc_boxable with a union or struct
// but they don't have access to the declaration (legacy/third-party code)
// then they can 'enable' this feature with a typedef:
// typedef struct __attribute((objc_boxable)) legacy_struct legacy_struct;
void SemaObjC::handleBoxable(Decl *D, const ParsedAttr &AL) {
  bool notify = false;

  auto *RD = dyn_cast<RecordDecl>(D);
  if (RD && RD->getDefinition()) {
    RD = RD->getDefinition();
    notify = true;
  }

  if (RD) {
    ObjCBoxableAttr *BoxableAttr =
        ::new (getASTContext()) ObjCBoxableAttr(getASTContext(), AL);
    RD->addAttr(BoxableAttr);
    if (notify) {
      // we need to notify ASTReader/ASTWriter about
      // modification of existing declaration
      if (ASTMutationListener *L = SemaRef.getASTMutationListener())
        L->AddedAttributeToRecord(BoxableAttr, RD);
    }
  }
}

void SemaObjC::handleOwnershipAttr(Decl *D, const ParsedAttr &AL) {
  if (hasDeclarator(D))
    return;

  Diag(D->getBeginLoc(), diag::err_attribute_wrong_decl_type)
      << AL.getRange() << AL << AL.isRegularKeywordAttribute()
      << ExpectedVariable;
}

void SemaObjC::handlePreciseLifetimeAttr(Decl *D, const ParsedAttr &AL) {
  const auto *VD = cast<ValueDecl>(D);
  QualType QT = VD->getType();

  if (!QT->isDependentType() && !QT->isObjCLifetimeType()) {
    Diag(AL.getLoc(), diag::err_objc_precise_lifetime_bad_type) << QT;
    return;
  }

  Qualifiers::ObjCLifetime Lifetime = QT.getObjCLifetime();

  // If we have no lifetime yet, check the lifetime we're presumably
  // going to infer.
  if (Lifetime == Qualifiers::OCL_None && !QT->isDependentType())
    Lifetime = QT->getObjCARCImplicitLifetime();

  switch (Lifetime) {
  case Qualifiers::OCL_None:
    assert(QT->isDependentType() &&
           "didn't infer lifetime for non-dependent type?");
    break;

  case Qualifiers::OCL_Weak:   // meaningful
  case Qualifiers::OCL_Strong: // meaningful
    break;

  case Qualifiers::OCL_ExplicitNone:
  case Qualifiers::OCL_Autoreleasing:
    Diag(AL.getLoc(), diag::warn_objc_precise_lifetime_meaningless)
        << (Lifetime == Qualifiers::OCL_Autoreleasing);
    break;
  }

  D->addAttr(::new (getASTContext())
                 ObjCPreciseLifetimeAttr(getASTContext(), AL));
}

static bool tryMakeVariablePseudoStrong(Sema &S, VarDecl *VD,
                                        bool DiagnoseFailure) {
  QualType Ty = VD->getType();
  if (!Ty->isObjCRetainableType()) {
    if (DiagnoseFailure) {
      S.Diag(VD->getBeginLoc(), diag::warn_ignored_objc_externally_retained)
          << 0;
    }
    return false;
  }

  Qualifiers::ObjCLifetime LifetimeQual = Ty.getQualifiers().getObjCLifetime();

  // SemaObjC::inferObjCARCLifetime must run after processing decl attributes
  // (because __block lowers to an attribute), so if the lifetime hasn't been
  // explicitly specified, infer it locally now.
  if (LifetimeQual == Qualifiers::OCL_None)
    LifetimeQual = Ty->getObjCARCImplicitLifetime();

  // The attributes only really makes sense for __strong variables; ignore any
  // attempts to annotate a parameter with any other lifetime qualifier.
  if (LifetimeQual != Qualifiers::OCL_Strong) {
    if (DiagnoseFailure) {
      S.Diag(VD->getBeginLoc(), diag::warn_ignored_objc_externally_retained)
          << 1;
    }
    return false;
  }

  // Tampering with the type of a VarDecl here is a bit of a hack, but we need
  // to ensure that the variable is 'const' so that we can error on
  // modification, which can otherwise over-release.
  VD->setType(Ty.withConst());
  VD->setARCPseudoStrong(true);
  return true;
}

void SemaObjC::handleExternallyRetainedAttr(Decl *D, const ParsedAttr &AL) {
  if (auto *VD = dyn_cast<VarDecl>(D)) {
    assert(!isa<ParmVarDecl>(VD) && "should be diagnosed automatically");
    if (!VD->hasLocalStorage()) {
      Diag(D->getBeginLoc(), diag::warn_ignored_objc_externally_retained) << 0;
      return;
    }

    if (!tryMakeVariablePseudoStrong(SemaRef, VD, /*DiagnoseFailure=*/true))
      return;

    handleSimpleAttribute<ObjCExternallyRetainedAttr>(*this, D, AL);
    return;
  }

  // If D is a function-like declaration (method, block, or function), then we
  // make every parameter psuedo-strong.
  unsigned NumParams =
      hasFunctionProto(D) ? getFunctionOrMethodNumParams(D) : 0;
  for (unsigned I = 0; I != NumParams; ++I) {
    auto *PVD = const_cast<ParmVarDecl *>(getFunctionOrMethodParam(D, I));
    QualType Ty = PVD->getType();

    // If a user wrote a parameter with __strong explicitly, then assume they
    // want "real" strong semantics for that parameter. This works because if
    // the parameter was written with __strong, then the strong qualifier will
    // be non-local.
    if (Ty.getLocalUnqualifiedType().getQualifiers().getObjCLifetime() ==
        Qualifiers::OCL_Strong)
      continue;

    tryMakeVariablePseudoStrong(SemaRef, PVD, /*DiagnoseFailure=*/false);
  }
  handleSimpleAttribute<ObjCExternallyRetainedAttr>(*this, D, AL);
}

bool SemaObjC::GetFormatNSStringIdx(const FormatAttr *Format, unsigned &Idx) {
  Sema::FormatStringInfo FSI;
  if ((SemaRef.GetFormatStringType(Format) == Sema::FST_NSString) &&
      SemaRef.getFormatStringInfo(Format, false, true, &FSI)) {
    Idx = FSI.FormatIdx;
    return true;
  }
  return false;
}

/// Diagnose use of %s directive in an NSString which is being passed
/// as formatting string to formatting method.
void SemaObjC::DiagnoseCStringFormatDirectiveInCFAPI(const NamedDecl *FDecl,
                                                     Expr **Args,
                                                     unsigned NumArgs) {
  unsigned Idx = 0;
  bool Format = false;
  ObjCStringFormatFamily SFFamily = FDecl->getObjCFStringFormattingFamily();
  if (SFFamily == ObjCStringFormatFamily::SFF_CFString) {
    Idx = 2;
    Format = true;
  } else
    for (const auto *I : FDecl->specific_attrs<FormatAttr>()) {
      if (GetFormatNSStringIdx(I, Idx)) {
        Format = true;
        break;
      }
    }
  if (!Format || NumArgs <= Idx)
    return;
  const Expr *FormatExpr = Args[Idx];
  if (const CStyleCastExpr *CSCE = dyn_cast<CStyleCastExpr>(FormatExpr))
    FormatExpr = CSCE->getSubExpr();
  const StringLiteral *FormatString;
  if (const ObjCStringLiteral *OSL =
          dyn_cast<ObjCStringLiteral>(FormatExpr->IgnoreParenImpCasts()))
    FormatString = OSL->getString();
  else
    FormatString = dyn_cast<StringLiteral>(FormatExpr->IgnoreParenImpCasts());
  if (!FormatString)
    return;
  if (SemaRef.FormatStringHasSArg(FormatString)) {
    Diag(FormatExpr->getExprLoc(), diag::warn_objc_cdirective_format_string)
        << "%s" << 1 << 1;
    Diag(FDecl->getLocation(), diag::note_entity_declared_at)
        << FDecl->getDeclName();
  }
}

bool SemaObjC::isSignedCharBool(QualType Ty) {
  return Ty->isSpecificBuiltinType(BuiltinType::SChar) && getLangOpts().ObjC &&
         NSAPIObj->isObjCBOOLType(Ty);
}

void SemaObjC::adornBoolConversionDiagWithTernaryFixit(
    Expr *SourceExpr, const Sema::SemaDiagnosticBuilder &Builder) {
  Expr *Ignored = SourceExpr->IgnoreImplicit();
  if (const auto *OVE = dyn_cast<OpaqueValueExpr>(Ignored))
    Ignored = OVE->getSourceExpr();
  bool NeedsParens = isa<AbstractConditionalOperator>(Ignored) ||
                     isa<BinaryOperator>(Ignored) ||
                     isa<CXXOperatorCallExpr>(Ignored);
  SourceLocation EndLoc = SemaRef.getLocForEndOfToken(SourceExpr->getEndLoc());
  if (NeedsParens)
    Builder << FixItHint::CreateInsertion(SourceExpr->getBeginLoc(), "(")
            << FixItHint::CreateInsertion(EndLoc, ")");
  Builder << FixItHint::CreateInsertion(EndLoc, " ? YES : NO");
}

/// Check a single element within a collection literal against the
/// target element type.
static void checkCollectionLiteralElement(Sema &S, QualType TargetElementType,
                                          Expr *Element, unsigned ElementKind) {
  // Skip a bitcast to 'id' or qualified 'id'.
  if (auto ICE = dyn_cast<ImplicitCastExpr>(Element)) {
    if (ICE->getCastKind() == CK_BitCast &&
        ICE->getSubExpr()->getType()->getAs<ObjCObjectPointerType>())
      Element = ICE->getSubExpr();
  }

  QualType ElementType = Element->getType();
  ExprResult ElementResult(Element);
  if (ElementType->getAs<ObjCObjectPointerType>() &&
      S.CheckSingleAssignmentConstraints(TargetElementType, ElementResult,
                                         false, false) != Sema::Compatible) {
    S.Diag(Element->getBeginLoc(), diag::warn_objc_collection_literal_element)
        << ElementType << ElementKind << TargetElementType
        << Element->getSourceRange();
  }

  if (auto ArrayLiteral = dyn_cast<ObjCArrayLiteral>(Element))
    S.ObjC().checkArrayLiteral(TargetElementType, ArrayLiteral);
  else if (auto DictionaryLiteral = dyn_cast<ObjCDictionaryLiteral>(Element))
    S.ObjC().checkDictionaryLiteral(TargetElementType, DictionaryLiteral);
}

/// Check an Objective-C array literal being converted to the given
/// target type.
void SemaObjC::checkArrayLiteral(QualType TargetType,
                                 ObjCArrayLiteral *ArrayLiteral) {
  if (!NSArrayDecl)
    return;

  const auto *TargetObjCPtr = TargetType->getAs<ObjCObjectPointerType>();
  if (!TargetObjCPtr)
    return;

  if (TargetObjCPtr->isUnspecialized() ||
      TargetObjCPtr->getInterfaceDecl()->getCanonicalDecl() !=
          NSArrayDecl->getCanonicalDecl())
    return;

  auto TypeArgs = TargetObjCPtr->getTypeArgs();
  if (TypeArgs.size() != 1)
    return;

  QualType TargetElementType = TypeArgs[0];
  for (unsigned I = 0, N = ArrayLiteral->getNumElements(); I != N; ++I) {
    checkCollectionLiteralElement(SemaRef, TargetElementType,
                                  ArrayLiteral->getElement(I), 0);
  }
}

void SemaObjC::checkDictionaryLiteral(
    QualType TargetType, ObjCDictionaryLiteral *DictionaryLiteral) {
  if (!NSDictionaryDecl)
    return;

  const auto *TargetObjCPtr = TargetType->getAs<ObjCObjectPointerType>();
  if (!TargetObjCPtr)
    return;

  if (TargetObjCPtr->isUnspecialized() ||
      TargetObjCPtr->getInterfaceDecl()->getCanonicalDecl() !=
          NSDictionaryDecl->getCanonicalDecl())
    return;

  auto TypeArgs = TargetObjCPtr->getTypeArgs();
  if (TypeArgs.size() != 2)
    return;

  QualType TargetKeyType = TypeArgs[0];
  QualType TargetObjectType = TypeArgs[1];
  for (unsigned I = 0, N = DictionaryLiteral->getNumElements(); I != N; ++I) {
    auto Element = DictionaryLiteral->getKeyValueElement(I);
    checkCollectionLiteralElement(SemaRef, TargetKeyType, Element.Key, 1);
    checkCollectionLiteralElement(SemaRef, TargetObjectType, Element.Value, 2);
  }
}

} // namespace clang
