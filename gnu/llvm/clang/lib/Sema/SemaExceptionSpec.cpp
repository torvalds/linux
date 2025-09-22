//===--- SemaExceptionSpec.cpp - C++ Exception Specifications ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Sema routines for C++ exception specification testing.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaInternal.h"
#include "clang/AST/ASTMutationListener.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include <optional>

namespace clang {

static const FunctionProtoType *GetUnderlyingFunction(QualType T)
{
  if (const PointerType *PtrTy = T->getAs<PointerType>())
    T = PtrTy->getPointeeType();
  else if (const ReferenceType *RefTy = T->getAs<ReferenceType>())
    T = RefTy->getPointeeType();
  else if (const MemberPointerType *MPTy = T->getAs<MemberPointerType>())
    T = MPTy->getPointeeType();
  return T->getAs<FunctionProtoType>();
}

/// HACK: 2014-11-14 libstdc++ had a bug where it shadows std::swap with a
/// member swap function then tries to call std::swap unqualified from the
/// exception specification of that function. This function detects whether
/// we're in such a case and turns off delay-parsing of exception
/// specifications. Libstdc++ 6.1 (released 2016-04-27) appears to have
/// resolved it as side-effect of commit ddb63209a8d (2015-06-05).
bool Sema::isLibstdcxxEagerExceptionSpecHack(const Declarator &D) {
  auto *RD = dyn_cast<CXXRecordDecl>(CurContext);

  // All the problem cases are member functions named "swap" within class
  // templates declared directly within namespace std or std::__debug or
  // std::__profile.
  if (!RD || !RD->getIdentifier() || !RD->getDescribedClassTemplate() ||
      !D.getIdentifier() || !D.getIdentifier()->isStr("swap"))
    return false;

  auto *ND = dyn_cast<NamespaceDecl>(RD->getDeclContext());
  if (!ND)
    return false;

  bool IsInStd = ND->isStdNamespace();
  if (!IsInStd) {
    // This isn't a direct member of namespace std, but it might still be
    // libstdc++'s std::__debug::array or std::__profile::array.
    IdentifierInfo *II = ND->getIdentifier();
    if (!II || !(II->isStr("__debug") || II->isStr("__profile")) ||
        !ND->isInStdNamespace())
      return false;
  }

  // Only apply this hack within a system header.
  if (!Context.getSourceManager().isInSystemHeader(D.getBeginLoc()))
    return false;

  return llvm::StringSwitch<bool>(RD->getIdentifier()->getName())
      .Case("array", true)
      .Case("pair", IsInStd)
      .Case("priority_queue", IsInStd)
      .Case("stack", IsInStd)
      .Case("queue", IsInStd)
      .Default(false);
}

ExprResult Sema::ActOnNoexceptSpec(Expr *NoexceptExpr,
                                   ExceptionSpecificationType &EST) {

  if (NoexceptExpr->isTypeDependent() ||
      NoexceptExpr->containsUnexpandedParameterPack()) {
    EST = EST_DependentNoexcept;
    return NoexceptExpr;
  }

  llvm::APSInt Result;
  ExprResult Converted = CheckConvertedConstantExpression(
      NoexceptExpr, Context.BoolTy, Result, CCEK_Noexcept);

  if (Converted.isInvalid()) {
    EST = EST_NoexceptFalse;
    // Fill in an expression of 'false' as a fixup.
    auto *BoolExpr = new (Context)
        CXXBoolLiteralExpr(false, Context.BoolTy, NoexceptExpr->getBeginLoc());
    llvm::APSInt Value{1};
    Value = 0;
    return ConstantExpr::Create(Context, BoolExpr, APValue{Value});
  }

  if (Converted.get()->isValueDependent()) {
    EST = EST_DependentNoexcept;
    return Converted;
  }

  if (!Converted.isInvalid())
    EST = !Result ? EST_NoexceptFalse : EST_NoexceptTrue;
  return Converted;
}

bool Sema::CheckSpecifiedExceptionType(QualType &T, SourceRange Range) {
  // C++11 [except.spec]p2:
  //   A type cv T, "array of T", or "function returning T" denoted
  //   in an exception-specification is adjusted to type T, "pointer to T", or
  //   "pointer to function returning T", respectively.
  //
  // We also apply this rule in C++98.
  if (T->isArrayType())
    T = Context.getArrayDecayedType(T);
  else if (T->isFunctionType())
    T = Context.getPointerType(T);

  int Kind = 0;
  QualType PointeeT = T;
  if (const PointerType *PT = T->getAs<PointerType>()) {
    PointeeT = PT->getPointeeType();
    Kind = 1;

    // cv void* is explicitly permitted, despite being a pointer to an
    // incomplete type.
    if (PointeeT->isVoidType())
      return false;
  } else if (const ReferenceType *RT = T->getAs<ReferenceType>()) {
    PointeeT = RT->getPointeeType();
    Kind = 2;

    if (RT->isRValueReferenceType()) {
      // C++11 [except.spec]p2:
      //   A type denoted in an exception-specification shall not denote [...]
      //   an rvalue reference type.
      Diag(Range.getBegin(), diag::err_rref_in_exception_spec)
        << T << Range;
      return true;
    }
  }

  // C++11 [except.spec]p2:
  //   A type denoted in an exception-specification shall not denote an
  //   incomplete type other than a class currently being defined [...].
  //   A type denoted in an exception-specification shall not denote a
  //   pointer or reference to an incomplete type, other than (cv) void* or a
  //   pointer or reference to a class currently being defined.
  // In Microsoft mode, downgrade this to a warning.
  unsigned DiagID = diag::err_incomplete_in_exception_spec;
  bool ReturnValueOnError = true;
  if (getLangOpts().MSVCCompat) {
    DiagID = diag::ext_incomplete_in_exception_spec;
    ReturnValueOnError = false;
  }
  if (!(PointeeT->isRecordType() &&
        PointeeT->castAs<RecordType>()->isBeingDefined()) &&
      RequireCompleteType(Range.getBegin(), PointeeT, DiagID, Kind, Range))
    return ReturnValueOnError;

  // WebAssembly reference types can't be used in exception specifications.
  if (PointeeT.isWebAssemblyReferenceType()) {
    Diag(Range.getBegin(), diag::err_wasm_reftype_exception_spec);
    return true;
  }

  // The MSVC compatibility mode doesn't extend to sizeless types,
  // so diagnose them separately.
  if (PointeeT->isSizelessType() && Kind != 1) {
    Diag(Range.getBegin(), diag::err_sizeless_in_exception_spec)
        << (Kind == 2 ? 1 : 0) << PointeeT << Range;
    return true;
  }

  return false;
}

bool Sema::CheckDistantExceptionSpec(QualType T) {
  // C++17 removes this rule in favor of putting exception specifications into
  // the type system.
  if (getLangOpts().CPlusPlus17)
    return false;

  if (const PointerType *PT = T->getAs<PointerType>())
    T = PT->getPointeeType();
  else if (const MemberPointerType *PT = T->getAs<MemberPointerType>())
    T = PT->getPointeeType();
  else
    return false;

  const FunctionProtoType *FnT = T->getAs<FunctionProtoType>();
  if (!FnT)
    return false;

  return FnT->hasExceptionSpec();
}

const FunctionProtoType *
Sema::ResolveExceptionSpec(SourceLocation Loc, const FunctionProtoType *FPT) {
  if (FPT->getExceptionSpecType() == EST_Unparsed) {
    Diag(Loc, diag::err_exception_spec_not_parsed);
    return nullptr;
  }

  if (!isUnresolvedExceptionSpec(FPT->getExceptionSpecType()))
    return FPT;

  FunctionDecl *SourceDecl = FPT->getExceptionSpecDecl();
  const FunctionProtoType *SourceFPT =
      SourceDecl->getType()->castAs<FunctionProtoType>();

  // If the exception specification has already been resolved, just return it.
  if (!isUnresolvedExceptionSpec(SourceFPT->getExceptionSpecType()))
    return SourceFPT;

  // Compute or instantiate the exception specification now.
  if (SourceFPT->getExceptionSpecType() == EST_Unevaluated)
    EvaluateImplicitExceptionSpec(Loc, SourceDecl);
  else
    InstantiateExceptionSpec(Loc, SourceDecl);

  const FunctionProtoType *Proto =
    SourceDecl->getType()->castAs<FunctionProtoType>();
  if (Proto->getExceptionSpecType() == clang::EST_Unparsed) {
    Diag(Loc, diag::err_exception_spec_not_parsed);
    Proto = nullptr;
  }
  return Proto;
}

void
Sema::UpdateExceptionSpec(FunctionDecl *FD,
                          const FunctionProtoType::ExceptionSpecInfo &ESI) {
  // If we've fully resolved the exception specification, notify listeners.
  if (!isUnresolvedExceptionSpec(ESI.Type))
    if (auto *Listener = getASTMutationListener())
      Listener->ResolvedExceptionSpec(FD);

  for (FunctionDecl *Redecl : FD->redecls())
    Context.adjustExceptionSpec(Redecl, ESI);
}

static bool exceptionSpecNotKnownYet(const FunctionDecl *FD) {
  ExceptionSpecificationType EST =
      FD->getType()->castAs<FunctionProtoType>()->getExceptionSpecType();
  if (EST == EST_Unparsed)
    return true;
  else if (EST != EST_Unevaluated)
    return false;
  const DeclContext *DC = FD->getLexicalDeclContext();
  return DC->isRecord() && cast<RecordDecl>(DC)->isBeingDefined();
}

static bool CheckEquivalentExceptionSpecImpl(
    Sema &S, const PartialDiagnostic &DiagID, const PartialDiagnostic &NoteID,
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc,
    bool *MissingExceptionSpecification = nullptr,
    bool *MissingEmptyExceptionSpecification = nullptr,
    bool AllowNoexceptAllMatchWithNoSpec = false, bool IsOperatorNew = false);

/// Determine whether a function has an implicitly-generated exception
/// specification.
static bool hasImplicitExceptionSpec(FunctionDecl *Decl) {
  if (!isa<CXXDestructorDecl>(Decl) &&
      Decl->getDeclName().getCXXOverloadedOperator() != OO_Delete &&
      Decl->getDeclName().getCXXOverloadedOperator() != OO_Array_Delete)
    return false;

  // For a function that the user didn't declare:
  //  - if this is a destructor, its exception specification is implicit.
  //  - if this is 'operator delete' or 'operator delete[]', the exception
  //    specification is as-if an explicit exception specification was given
  //    (per [basic.stc.dynamic]p2).
  if (!Decl->getTypeSourceInfo())
    return isa<CXXDestructorDecl>(Decl);

  auto *Ty = Decl->getTypeSourceInfo()->getType()->castAs<FunctionProtoType>();
  return !Ty->hasExceptionSpec();
}

bool Sema::CheckEquivalentExceptionSpec(FunctionDecl *Old, FunctionDecl *New) {
  // Just completely ignore this under -fno-exceptions prior to C++17.
  // In C++17 onwards, the exception specification is part of the type and
  // we will diagnose mismatches anyway, so it's better to check for them here.
  if (!getLangOpts().CXXExceptions && !getLangOpts().CPlusPlus17)
    return false;

  OverloadedOperatorKind OO = New->getDeclName().getCXXOverloadedOperator();
  bool IsOperatorNew = OO == OO_New || OO == OO_Array_New;
  bool MissingExceptionSpecification = false;
  bool MissingEmptyExceptionSpecification = false;

  unsigned DiagID = diag::err_mismatched_exception_spec;
  bool ReturnValueOnError = true;
  if (getLangOpts().MSVCCompat) {
    DiagID = diag::ext_mismatched_exception_spec;
    ReturnValueOnError = false;
  }

  // If we're befriending a member function of a class that's currently being
  // defined, we might not be able to work out its exception specification yet.
  // If not, defer the check until later.
  if (exceptionSpecNotKnownYet(Old) || exceptionSpecNotKnownYet(New)) {
    DelayedEquivalentExceptionSpecChecks.push_back({New, Old});
    return false;
  }

  // Check the types as written: they must match before any exception
  // specification adjustment is applied.
  if (!CheckEquivalentExceptionSpecImpl(
        *this, PDiag(DiagID), PDiag(diag::note_previous_declaration),
        Old->getType()->getAs<FunctionProtoType>(), Old->getLocation(),
        New->getType()->getAs<FunctionProtoType>(), New->getLocation(),
        &MissingExceptionSpecification, &MissingEmptyExceptionSpecification,
        /*AllowNoexceptAllMatchWithNoSpec=*/true, IsOperatorNew)) {
    // C++11 [except.spec]p4 [DR1492]:
    //   If a declaration of a function has an implicit
    //   exception-specification, other declarations of the function shall
    //   not specify an exception-specification.
    if (getLangOpts().CPlusPlus11 && getLangOpts().CXXExceptions &&
        hasImplicitExceptionSpec(Old) != hasImplicitExceptionSpec(New)) {
      Diag(New->getLocation(), diag::ext_implicit_exception_spec_mismatch)
        << hasImplicitExceptionSpec(Old);
      if (Old->getLocation().isValid())
        Diag(Old->getLocation(), diag::note_previous_declaration);
    }
    return false;
  }

  // The failure was something other than an missing exception
  // specification; return an error, except in MS mode where this is a warning.
  if (!MissingExceptionSpecification)
    return ReturnValueOnError;

  const auto *NewProto = New->getType()->castAs<FunctionProtoType>();

  // The new function declaration is only missing an empty exception
  // specification "throw()". If the throw() specification came from a
  // function in a system header that has C linkage, just add an empty
  // exception specification to the "new" declaration. Note that C library
  // implementations are permitted to add these nothrow exception
  // specifications.
  //
  // Likewise if the old function is a builtin.
  if (MissingEmptyExceptionSpecification &&
      (Old->getLocation().isInvalid() ||
       Context.getSourceManager().isInSystemHeader(Old->getLocation()) ||
       Old->getBuiltinID()) &&
      Old->isExternC()) {
    New->setType(Context.getFunctionType(
        NewProto->getReturnType(), NewProto->getParamTypes(),
        NewProto->getExtProtoInfo().withExceptionSpec(EST_DynamicNone)));
    return false;
  }

  const auto *OldProto = Old->getType()->castAs<FunctionProtoType>();

  FunctionProtoType::ExceptionSpecInfo ESI = OldProto->getExceptionSpecType();
  if (ESI.Type == EST_Dynamic) {
    // FIXME: What if the exceptions are described in terms of the old
    // prototype's parameters?
    ESI.Exceptions = OldProto->exceptions();
  }

  if (ESI.Type == EST_NoexceptFalse)
    ESI.Type = EST_None;
  if (ESI.Type == EST_NoexceptTrue)
    ESI.Type = EST_BasicNoexcept;

  // For dependent noexcept, we can't just take the expression from the old
  // prototype. It likely contains references to the old prototype's parameters.
  if (ESI.Type == EST_DependentNoexcept) {
    New->setInvalidDecl();
  } else {
    // Update the type of the function with the appropriate exception
    // specification.
    New->setType(Context.getFunctionType(
        NewProto->getReturnType(), NewProto->getParamTypes(),
        NewProto->getExtProtoInfo().withExceptionSpec(ESI)));
  }

  if (getLangOpts().MSVCCompat && isDynamicExceptionSpec(ESI.Type)) {
    DiagID = diag::ext_missing_exception_specification;
    ReturnValueOnError = false;
  } else if (New->isReplaceableGlobalAllocationFunction() &&
             ESI.Type != EST_DependentNoexcept) {
    // Allow missing exception specifications in redeclarations as an extension,
    // when declaring a replaceable global allocation function.
    DiagID = diag::ext_missing_exception_specification;
    ReturnValueOnError = false;
  } else if (ESI.Type == EST_NoThrow) {
    // Don't emit any warning for missing 'nothrow' in MSVC.
    if (getLangOpts().MSVCCompat) {
      return false;
    }
    // Allow missing attribute 'nothrow' in redeclarations, since this is a very
    // common omission.
    DiagID = diag::ext_missing_exception_specification;
    ReturnValueOnError = false;
  } else {
    DiagID = diag::err_missing_exception_specification;
    ReturnValueOnError = true;
  }

  // Warn about the lack of exception specification.
  SmallString<128> ExceptionSpecString;
  llvm::raw_svector_ostream OS(ExceptionSpecString);
  switch (OldProto->getExceptionSpecType()) {
  case EST_DynamicNone:
    OS << "throw()";
    break;

  case EST_Dynamic: {
    OS << "throw(";
    bool OnFirstException = true;
    for (const auto &E : OldProto->exceptions()) {
      if (OnFirstException)
        OnFirstException = false;
      else
        OS << ", ";

      OS << E.getAsString(getPrintingPolicy());
    }
    OS << ")";
    break;
  }

  case EST_BasicNoexcept:
    OS << "noexcept";
    break;

  case EST_DependentNoexcept:
  case EST_NoexceptFalse:
  case EST_NoexceptTrue:
    OS << "noexcept(";
    assert(OldProto->getNoexceptExpr() != nullptr && "Expected non-null Expr");
    OldProto->getNoexceptExpr()->printPretty(OS, nullptr, getPrintingPolicy());
    OS << ")";
    break;
  case EST_NoThrow:
    OS <<"__attribute__((nothrow))";
    break;
  case EST_None:
  case EST_MSAny:
  case EST_Unevaluated:
  case EST_Uninstantiated:
  case EST_Unparsed:
    llvm_unreachable("This spec type is compatible with none.");
  }

  SourceLocation FixItLoc;
  if (TypeSourceInfo *TSInfo = New->getTypeSourceInfo()) {
    TypeLoc TL = TSInfo->getTypeLoc().IgnoreParens();
    // FIXME: Preserve enough information so that we can produce a correct fixit
    // location when there is a trailing return type.
    if (auto FTLoc = TL.getAs<FunctionProtoTypeLoc>())
      if (!FTLoc.getTypePtr()->hasTrailingReturn())
        FixItLoc = getLocForEndOfToken(FTLoc.getLocalRangeEnd());
  }

  if (FixItLoc.isInvalid())
    Diag(New->getLocation(), DiagID)
      << New << OS.str();
  else {
    Diag(New->getLocation(), DiagID)
      << New << OS.str()
      << FixItHint::CreateInsertion(FixItLoc, " " + OS.str().str());
  }

  if (Old->getLocation().isValid())
    Diag(Old->getLocation(), diag::note_previous_declaration);

  return ReturnValueOnError;
}

bool Sema::CheckEquivalentExceptionSpec(
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc) {
  if (!getLangOpts().CXXExceptions)
    return false;

  unsigned DiagID = diag::err_mismatched_exception_spec;
  if (getLangOpts().MSVCCompat)
    DiagID = diag::ext_mismatched_exception_spec;
  bool Result = CheckEquivalentExceptionSpecImpl(
      *this, PDiag(DiagID), PDiag(diag::note_previous_declaration),
      Old, OldLoc, New, NewLoc);

  // In Microsoft mode, mismatching exception specifications just cause a warning.
  if (getLangOpts().MSVCCompat)
    return false;
  return Result;
}

/// CheckEquivalentExceptionSpec - Check if the two types have compatible
/// exception specifications. See C++ [except.spec]p3.
///
/// \return \c false if the exception specifications match, \c true if there is
/// a problem. If \c true is returned, either a diagnostic has already been
/// produced or \c *MissingExceptionSpecification is set to \c true.
static bool CheckEquivalentExceptionSpecImpl(
    Sema &S, const PartialDiagnostic &DiagID, const PartialDiagnostic &NoteID,
    const FunctionProtoType *Old, SourceLocation OldLoc,
    const FunctionProtoType *New, SourceLocation NewLoc,
    bool *MissingExceptionSpecification,
    bool *MissingEmptyExceptionSpecification,
    bool AllowNoexceptAllMatchWithNoSpec, bool IsOperatorNew) {
  if (MissingExceptionSpecification)
    *MissingExceptionSpecification = false;

  if (MissingEmptyExceptionSpecification)
    *MissingEmptyExceptionSpecification = false;

  Old = S.ResolveExceptionSpec(NewLoc, Old);
  if (!Old)
    return false;
  New = S.ResolveExceptionSpec(NewLoc, New);
  if (!New)
    return false;

  // C++0x [except.spec]p3: Two exception-specifications are compatible if:
  //   - both are non-throwing, regardless of their form,
  //   - both have the form noexcept(constant-expression) and the constant-
  //     expressions are equivalent,
  //   - both are dynamic-exception-specifications that have the same set of
  //     adjusted types.
  //
  // C++0x [except.spec]p12: An exception-specification is non-throwing if it is
  //   of the form throw(), noexcept, or noexcept(constant-expression) where the
  //   constant-expression yields true.
  //
  // C++0x [except.spec]p4: If any declaration of a function has an exception-
  //   specifier that is not a noexcept-specification allowing all exceptions,
  //   all declarations [...] of that function shall have a compatible
  //   exception-specification.
  //
  // That last point basically means that noexcept(false) matches no spec.
  // It's considered when AllowNoexceptAllMatchWithNoSpec is true.

  ExceptionSpecificationType OldEST = Old->getExceptionSpecType();
  ExceptionSpecificationType NewEST = New->getExceptionSpecType();

  assert(!isUnresolvedExceptionSpec(OldEST) &&
         !isUnresolvedExceptionSpec(NewEST) &&
         "Shouldn't see unknown exception specifications here");

  CanThrowResult OldCanThrow = Old->canThrow();
  CanThrowResult NewCanThrow = New->canThrow();

  // Any non-throwing specifications are compatible.
  if (OldCanThrow == CT_Cannot && NewCanThrow == CT_Cannot)
    return false;

  // Any throws-anything specifications are usually compatible.
  if (OldCanThrow == CT_Can && OldEST != EST_Dynamic &&
      NewCanThrow == CT_Can && NewEST != EST_Dynamic) {
    // The exception is that the absence of an exception specification only
    // matches noexcept(false) for functions, as described above.
    if (!AllowNoexceptAllMatchWithNoSpec &&
        ((OldEST == EST_None && NewEST == EST_NoexceptFalse) ||
         (OldEST == EST_NoexceptFalse && NewEST == EST_None))) {
      // This is the disallowed case.
    } else {
      return false;
    }
  }

  // C++14 [except.spec]p3:
  //   Two exception-specifications are compatible if [...] both have the form
  //   noexcept(constant-expression) and the constant-expressions are equivalent
  if (OldEST == EST_DependentNoexcept && NewEST == EST_DependentNoexcept) {
    llvm::FoldingSetNodeID OldFSN, NewFSN;
    Old->getNoexceptExpr()->Profile(OldFSN, S.Context, true);
    New->getNoexceptExpr()->Profile(NewFSN, S.Context, true);
    if (OldFSN == NewFSN)
      return false;
  }

  // Dynamic exception specifications with the same set of adjusted types
  // are compatible.
  if (OldEST == EST_Dynamic && NewEST == EST_Dynamic) {
    bool Success = true;
    // Both have a dynamic exception spec. Collect the first set, then compare
    // to the second.
    llvm::SmallPtrSet<CanQualType, 8> OldTypes, NewTypes;
    for (const auto &I : Old->exceptions())
      OldTypes.insert(S.Context.getCanonicalType(I).getUnqualifiedType());

    for (const auto &I : New->exceptions()) {
      CanQualType TypePtr = S.Context.getCanonicalType(I).getUnqualifiedType();
      if (OldTypes.count(TypePtr))
        NewTypes.insert(TypePtr);
      else {
        Success = false;
        break;
      }
    }

    if (Success && OldTypes.size() == NewTypes.size())
      return false;
  }

  // As a special compatibility feature, under C++0x we accept no spec and
  // throw(std::bad_alloc) as equivalent for operator new and operator new[].
  // This is because the implicit declaration changed, but old code would break.
  if (S.getLangOpts().CPlusPlus11 && IsOperatorNew) {
    const FunctionProtoType *WithExceptions = nullptr;
    if (OldEST == EST_None && NewEST == EST_Dynamic)
      WithExceptions = New;
    else if (OldEST == EST_Dynamic && NewEST == EST_None)
      WithExceptions = Old;
    if (WithExceptions && WithExceptions->getNumExceptions() == 1) {
      // One has no spec, the other throw(something). If that something is
      // std::bad_alloc, all conditions are met.
      QualType Exception = *WithExceptions->exception_begin();
      if (CXXRecordDecl *ExRecord = Exception->getAsCXXRecordDecl()) {
        IdentifierInfo* Name = ExRecord->getIdentifier();
        if (Name && Name->getName() == "bad_alloc") {
          // It's called bad_alloc, but is it in std?
          if (ExRecord->isInStdNamespace()) {
            return false;
          }
        }
      }
    }
  }

  // If the caller wants to handle the case that the new function is
  // incompatible due to a missing exception specification, let it.
  if (MissingExceptionSpecification && OldEST != EST_None &&
      NewEST == EST_None) {
    // The old type has an exception specification of some sort, but
    // the new type does not.
    *MissingExceptionSpecification = true;

    if (MissingEmptyExceptionSpecification && OldCanThrow == CT_Cannot) {
      // The old type has a throw() or noexcept(true) exception specification
      // and the new type has no exception specification, and the caller asked
      // to handle this itself.
      *MissingEmptyExceptionSpecification = true;
    }

    return true;
  }

  S.Diag(NewLoc, DiagID);
  if (NoteID.getDiagID() != 0 && OldLoc.isValid())
    S.Diag(OldLoc, NoteID);
  return true;
}

bool Sema::CheckEquivalentExceptionSpec(const PartialDiagnostic &DiagID,
                                        const PartialDiagnostic &NoteID,
                                        const FunctionProtoType *Old,
                                        SourceLocation OldLoc,
                                        const FunctionProtoType *New,
                                        SourceLocation NewLoc) {
  if (!getLangOpts().CXXExceptions)
    return false;
  return CheckEquivalentExceptionSpecImpl(*this, DiagID, NoteID, Old, OldLoc,
                                          New, NewLoc);
}

bool Sema::handlerCanCatch(QualType HandlerType, QualType ExceptionType) {
  // [except.handle]p3:
  //   A handler is a match for an exception object of type E if:

  // HandlerType must be ExceptionType or derived from it, or pointer or
  // reference to such types.
  const ReferenceType *RefTy = HandlerType->getAs<ReferenceType>();
  if (RefTy)
    HandlerType = RefTy->getPointeeType();

  //   -- the handler is of type cv T or cv T& and E and T are the same type
  if (Context.hasSameUnqualifiedType(ExceptionType, HandlerType))
    return true;

  // FIXME: ObjC pointer types?
  if (HandlerType->isPointerType() || HandlerType->isMemberPointerType()) {
    if (RefTy && (!HandlerType.isConstQualified() ||
                  HandlerType.isVolatileQualified()))
      return false;

    // -- the handler is of type cv T or const T& where T is a pointer or
    //    pointer to member type and E is std::nullptr_t
    if (ExceptionType->isNullPtrType())
      return true;

    // -- the handler is of type cv T or const T& where T is a pointer or
    //    pointer to member type and E is a pointer or pointer to member type
    //    that can be converted to T by one or more of
    //    -- a qualification conversion
    //    -- a function pointer conversion
    bool LifetimeConv;
    QualType Result;
    // FIXME: Should we treat the exception as catchable if a lifetime
    // conversion is required?
    if (IsQualificationConversion(ExceptionType, HandlerType, false,
                                  LifetimeConv) ||
        IsFunctionConversion(ExceptionType, HandlerType, Result))
      return true;

    //    -- a standard pointer conversion [...]
    if (!ExceptionType->isPointerType() || !HandlerType->isPointerType())
      return false;

    // Handle the "qualification conversion" portion.
    Qualifiers EQuals, HQuals;
    ExceptionType = Context.getUnqualifiedArrayType(
        ExceptionType->getPointeeType(), EQuals);
    HandlerType = Context.getUnqualifiedArrayType(
        HandlerType->getPointeeType(), HQuals);
    if (!HQuals.compatiblyIncludes(EQuals))
      return false;

    if (HandlerType->isVoidType() && ExceptionType->isObjectType())
      return true;

    // The only remaining case is a derived-to-base conversion.
  }

  //   -- the handler is of type cg T or cv T& and T is an unambiguous public
  //      base class of E
  if (!ExceptionType->isRecordType() || !HandlerType->isRecordType())
    return false;
  CXXBasePaths Paths(/*FindAmbiguities=*/true, /*RecordPaths=*/true,
                     /*DetectVirtual=*/false);
  if (!IsDerivedFrom(SourceLocation(), ExceptionType, HandlerType, Paths) ||
      Paths.isAmbiguous(Context.getCanonicalType(HandlerType)))
    return false;

  // Do this check from a context without privileges.
  switch (CheckBaseClassAccess(SourceLocation(), HandlerType, ExceptionType,
                               Paths.front(),
                               /*Diagnostic*/ 0,
                               /*ForceCheck*/ true,
                               /*ForceUnprivileged*/ true)) {
  case AR_accessible: return true;
  case AR_inaccessible: return false;
  case AR_dependent:
    llvm_unreachable("access check dependent for unprivileged context");
  case AR_delayed:
    llvm_unreachable("access check delayed in non-declaration");
  }
  llvm_unreachable("unexpected access check result");
}

bool Sema::CheckExceptionSpecSubset(
    const PartialDiagnostic &DiagID, const PartialDiagnostic &NestedDiagID,
    const PartialDiagnostic &NoteID, const PartialDiagnostic &NoThrowDiagID,
    const FunctionProtoType *Superset, bool SkipSupersetFirstParameter,
    SourceLocation SuperLoc, const FunctionProtoType *Subset,
    bool SkipSubsetFirstParameter, SourceLocation SubLoc) {

  // Just auto-succeed under -fno-exceptions.
  if (!getLangOpts().CXXExceptions)
    return false;

  // FIXME: As usual, we could be more specific in our error messages, but
  // that better waits until we've got types with source locations.

  if (!SubLoc.isValid())
    SubLoc = SuperLoc;

  // Resolve the exception specifications, if needed.
  Superset = ResolveExceptionSpec(SuperLoc, Superset);
  if (!Superset)
    return false;
  Subset = ResolveExceptionSpec(SubLoc, Subset);
  if (!Subset)
    return false;

  ExceptionSpecificationType SuperEST = Superset->getExceptionSpecType();
  ExceptionSpecificationType SubEST = Subset->getExceptionSpecType();
  assert(!isUnresolvedExceptionSpec(SuperEST) &&
         !isUnresolvedExceptionSpec(SubEST) &&
         "Shouldn't see unknown exception specifications here");

  // If there are dependent noexcept specs, assume everything is fine. Unlike
  // with the equivalency check, this is safe in this case, because we don't
  // want to merge declarations. Checks after instantiation will catch any
  // omissions we make here.
  if (SuperEST == EST_DependentNoexcept || SubEST == EST_DependentNoexcept)
    return false;

  CanThrowResult SuperCanThrow = Superset->canThrow();
  CanThrowResult SubCanThrow = Subset->canThrow();

  // If the superset contains everything or the subset contains nothing, we're
  // done.
  if ((SuperCanThrow == CT_Can && SuperEST != EST_Dynamic) ||
      SubCanThrow == CT_Cannot)
    return CheckParamExceptionSpec(NestedDiagID, NoteID, Superset,
                                   SkipSupersetFirstParameter, SuperLoc, Subset,
                                   SkipSubsetFirstParameter, SubLoc);

  // Allow __declspec(nothrow) to be missing on redeclaration as an extension in
  // some cases.
  if (NoThrowDiagID.getDiagID() != 0 && SubCanThrow == CT_Can &&
      SuperCanThrow == CT_Cannot && SuperEST == EST_NoThrow) {
    Diag(SubLoc, NoThrowDiagID);
    if (NoteID.getDiagID() != 0)
      Diag(SuperLoc, NoteID);
    return true;
  }

  // If the subset contains everything or the superset contains nothing, we've
  // failed.
  if ((SubCanThrow == CT_Can && SubEST != EST_Dynamic) ||
      SuperCanThrow == CT_Cannot) {
    Diag(SubLoc, DiagID);
    if (NoteID.getDiagID() != 0)
      Diag(SuperLoc, NoteID);
    return true;
  }

  assert(SuperEST == EST_Dynamic && SubEST == EST_Dynamic &&
         "Exception spec subset: non-dynamic case slipped through.");

  // Neither contains everything or nothing. Do a proper comparison.
  for (QualType SubI : Subset->exceptions()) {
    if (const ReferenceType *RefTy = SubI->getAs<ReferenceType>())
      SubI = RefTy->getPointeeType();

    // Make sure it's in the superset.
    bool Contained = false;
    for (QualType SuperI : Superset->exceptions()) {
      // [except.spec]p5:
      //   the target entity shall allow at least the exceptions allowed by the
      //   source
      //
      // We interpret this as meaning that a handler for some target type would
      // catch an exception of each source type.
      if (handlerCanCatch(SuperI, SubI)) {
        Contained = true;
        break;
      }
    }
    if (!Contained) {
      Diag(SubLoc, DiagID);
      if (NoteID.getDiagID() != 0)
        Diag(SuperLoc, NoteID);
      return true;
    }
  }
  // We've run half the gauntlet.
  return CheckParamExceptionSpec(NestedDiagID, NoteID, Superset,
                                 SkipSupersetFirstParameter, SuperLoc, Subset,
                                 SkipSupersetFirstParameter, SubLoc);
}

static bool
CheckSpecForTypesEquivalent(Sema &S, const PartialDiagnostic &DiagID,
                            const PartialDiagnostic &NoteID, QualType Target,
                            SourceLocation TargetLoc, QualType Source,
                            SourceLocation SourceLoc) {
  const FunctionProtoType *TFunc = GetUnderlyingFunction(Target);
  if (!TFunc)
    return false;
  const FunctionProtoType *SFunc = GetUnderlyingFunction(Source);
  if (!SFunc)
    return false;

  return S.CheckEquivalentExceptionSpec(DiagID, NoteID, TFunc, TargetLoc,
                                        SFunc, SourceLoc);
}

bool Sema::CheckParamExceptionSpec(
    const PartialDiagnostic &DiagID, const PartialDiagnostic &NoteID,
    const FunctionProtoType *Target, bool SkipTargetFirstParameter,
    SourceLocation TargetLoc, const FunctionProtoType *Source,
    bool SkipSourceFirstParameter, SourceLocation SourceLoc) {
  auto RetDiag = DiagID;
  RetDiag << 0;
  if (CheckSpecForTypesEquivalent(
          *this, RetDiag, PDiag(),
          Target->getReturnType(), TargetLoc, Source->getReturnType(),
          SourceLoc))
    return true;

  // We shouldn't even be testing this unless the arguments are otherwise
  // compatible.
  assert((Target->getNumParams() - (unsigned)SkipTargetFirstParameter) ==
             (Source->getNumParams() - (unsigned)SkipSourceFirstParameter) &&
         "Functions have different argument counts.");
  for (unsigned i = 0, E = Target->getNumParams(); i != E; ++i) {
    auto ParamDiag = DiagID;
    ParamDiag << 1;
    if (CheckSpecForTypesEquivalent(
            *this, ParamDiag, PDiag(),
            Target->getParamType(i + (SkipTargetFirstParameter ? 1 : 0)),
            TargetLoc, Source->getParamType(SkipSourceFirstParameter ? 1 : 0),
            SourceLoc))
      return true;
  }
  return false;
}

bool Sema::CheckExceptionSpecCompatibility(Expr *From, QualType ToType) {
  // First we check for applicability.
  // Target type must be a function, function pointer or function reference.
  const FunctionProtoType *ToFunc = GetUnderlyingFunction(ToType);
  if (!ToFunc || ToFunc->hasDependentExceptionSpec())
    return false;

  // SourceType must be a function or function pointer.
  const FunctionProtoType *FromFunc = GetUnderlyingFunction(From->getType());
  if (!FromFunc || FromFunc->hasDependentExceptionSpec())
    return false;

  unsigned DiagID = diag::err_incompatible_exception_specs;
  unsigned NestedDiagID = diag::err_deep_exception_specs_differ;
  // This is not an error in C++17 onwards, unless the noexceptness doesn't
  // match, but in that case we have a full-on type mismatch, not just a
  // type sugar mismatch.
  if (getLangOpts().CPlusPlus17) {
    DiagID = diag::warn_incompatible_exception_specs;
    NestedDiagID = diag::warn_deep_exception_specs_differ;
  }

  // Now we've got the correct types on both sides, check their compatibility.
  // This means that the source of the conversion can only throw a subset of
  // the exceptions of the target, and any exception specs on arguments or
  // return types must be equivalent.
  //
  // FIXME: If there is a nested dependent exception specification, we should
  // not be checking it here. This is fine:
  //   template<typename T> void f() {
  //     void (*p)(void (*) throw(T));
  //     void (*q)(void (*) throw(int)) = p;
  //   }
  // ... because it might be instantiated with T=int.
  return CheckExceptionSpecSubset(PDiag(DiagID), PDiag(NestedDiagID), PDiag(),
                                  PDiag(), ToFunc, 0,
                                  From->getSourceRange().getBegin(), FromFunc,
                                  0, SourceLocation()) &&
         !getLangOpts().CPlusPlus17;
}

bool Sema::CheckOverridingFunctionExceptionSpec(const CXXMethodDecl *New,
                                                const CXXMethodDecl *Old) {
  // If the new exception specification hasn't been parsed yet, skip the check.
  // We'll get called again once it's been parsed.
  if (New->getType()->castAs<FunctionProtoType>()->getExceptionSpecType() ==
      EST_Unparsed)
    return false;

  // Don't check uninstantiated template destructors at all. We can only
  // synthesize correct specs after the template is instantiated.
  if (isa<CXXDestructorDecl>(New) && New->getParent()->isDependentType())
    return false;

  // If the old exception specification hasn't been parsed yet, or the new
  // exception specification can't be computed yet, remember that we need to
  // perform this check when we get to the end of the outermost
  // lexically-surrounding class.
  if (exceptionSpecNotKnownYet(Old) || exceptionSpecNotKnownYet(New)) {
    DelayedOverridingExceptionSpecChecks.push_back({New, Old});
    return false;
  }

  unsigned DiagID = diag::err_override_exception_spec;
  if (getLangOpts().MSVCCompat)
    DiagID = diag::ext_override_exception_spec;
  return CheckExceptionSpecSubset(
      PDiag(DiagID), PDiag(diag::err_deep_exception_specs_differ),
      PDiag(diag::note_overridden_virtual_function),
      PDiag(diag::ext_override_exception_spec),
      Old->getType()->castAs<FunctionProtoType>(),
      Old->hasCXXExplicitFunctionObjectParameter(), Old->getLocation(),
      New->getType()->castAs<FunctionProtoType>(),
      New->hasCXXExplicitFunctionObjectParameter(), New->getLocation());
}

static CanThrowResult canSubStmtsThrow(Sema &Self, const Stmt *S) {
  CanThrowResult R = CT_Cannot;
  for (const Stmt *SubStmt : S->children()) {
    if (!SubStmt)
      continue;
    R = mergeCanThrow(R, Self.canThrow(SubStmt));
    if (R == CT_Can)
      break;
  }
  return R;
}

CanThrowResult Sema::canCalleeThrow(Sema &S, const Expr *E, const Decl *D,
                                    SourceLocation Loc) {
  // As an extension, we assume that __attribute__((nothrow)) functions don't
  // throw.
  if (isa_and_nonnull<FunctionDecl>(D) && D->hasAttr<NoThrowAttr>())
    return CT_Cannot;

  QualType T;

  // In C++1z, just look at the function type of the callee.
  if (S.getLangOpts().CPlusPlus17 && isa_and_nonnull<CallExpr>(E)) {
    E = cast<CallExpr>(E)->getCallee();
    T = E->getType();
    if (T->isSpecificPlaceholderType(BuiltinType::BoundMember)) {
      // Sadly we don't preserve the actual type as part of the "bound member"
      // placeholder, so we need to reconstruct it.
      E = E->IgnoreParenImpCasts();

      // Could be a call to a pointer-to-member or a plain member access.
      if (auto *Op = dyn_cast<BinaryOperator>(E)) {
        assert(Op->getOpcode() == BO_PtrMemD || Op->getOpcode() == BO_PtrMemI);
        T = Op->getRHS()->getType()
              ->castAs<MemberPointerType>()->getPointeeType();
      } else {
        T = cast<MemberExpr>(E)->getMemberDecl()->getType();
      }
    }
  } else if (const ValueDecl *VD = dyn_cast_or_null<ValueDecl>(D))
    T = VD->getType();
  else
    // If we have no clue what we're calling, assume the worst.
    return CT_Can;

  const FunctionProtoType *FT;
  if ((FT = T->getAs<FunctionProtoType>())) {
  } else if (const PointerType *PT = T->getAs<PointerType>())
    FT = PT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const ReferenceType *RT = T->getAs<ReferenceType>())
    FT = RT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const MemberPointerType *MT = T->getAs<MemberPointerType>())
    FT = MT->getPointeeType()->getAs<FunctionProtoType>();
  else if (const BlockPointerType *BT = T->getAs<BlockPointerType>())
    FT = BT->getPointeeType()->getAs<FunctionProtoType>();

  if (!FT)
    return CT_Can;

  if (Loc.isValid() || (Loc.isInvalid() && E))
    FT = S.ResolveExceptionSpec(Loc.isInvalid() ? E->getBeginLoc() : Loc, FT);
  if (!FT)
    return CT_Can;

  return FT->canThrow();
}

static CanThrowResult canVarDeclThrow(Sema &Self, const VarDecl *VD) {
  CanThrowResult CT = CT_Cannot;

  // Initialization might throw.
  if (!VD->isUsableInConstantExpressions(Self.Context))
    if (const Expr *Init = VD->getInit())
      CT = mergeCanThrow(CT, Self.canThrow(Init));

  // Destructor might throw.
  if (VD->needsDestruction(Self.Context) == QualType::DK_cxx_destructor) {
    if (auto *RD =
            VD->getType()->getBaseElementTypeUnsafe()->getAsCXXRecordDecl()) {
      if (auto *Dtor = RD->getDestructor()) {
        CT = mergeCanThrow(
            CT, Sema::canCalleeThrow(Self, nullptr, Dtor, VD->getLocation()));
      }
    }
  }

  // If this is a decomposition declaration, bindings might throw.
  if (auto *DD = dyn_cast<DecompositionDecl>(VD))
    for (auto *B : DD->bindings())
      if (auto *HD = B->getHoldingVar())
        CT = mergeCanThrow(CT, canVarDeclThrow(Self, HD));

  return CT;
}

static CanThrowResult canDynamicCastThrow(const CXXDynamicCastExpr *DC) {
  if (DC->isTypeDependent())
    return CT_Dependent;

  if (!DC->getTypeAsWritten()->isReferenceType())
    return CT_Cannot;

  if (DC->getSubExpr()->isTypeDependent())
    return CT_Dependent;

  return DC->getCastKind() == clang::CK_Dynamic? CT_Can : CT_Cannot;
}

static CanThrowResult canTypeidThrow(Sema &S, const CXXTypeidExpr *DC) {
  // A typeid of a type is a constant and does not throw.
  if (DC->isTypeOperand())
    return CT_Cannot;

  if (DC->isValueDependent())
    return CT_Dependent;

  // If this operand is not evaluated it cannot possibly throw.
  if (!DC->isPotentiallyEvaluated())
    return CT_Cannot;

  // Can throw std::bad_typeid if a nullptr is dereferenced.
  if (DC->hasNullCheck())
    return CT_Can;

  return S.canThrow(DC->getExprOperand());
}

CanThrowResult Sema::canThrow(const Stmt *S) {
  // C++ [expr.unary.noexcept]p3:
  //   [Can throw] if in a potentially-evaluated context the expression would
  //   contain:
  switch (S->getStmtClass()) {
  case Expr::ConstantExprClass:
    return canThrow(cast<ConstantExpr>(S)->getSubExpr());

  case Expr::CXXThrowExprClass:
    //   - a potentially evaluated throw-expression
    return CT_Can;

  case Expr::CXXDynamicCastExprClass: {
    //   - a potentially evaluated dynamic_cast expression dynamic_cast<T>(v),
    //     where T is a reference type, that requires a run-time check
    auto *CE = cast<CXXDynamicCastExpr>(S);
    // FIXME: Properly determine whether a variably-modified type can throw.
    if (CE->getType()->isVariablyModifiedType())
      return CT_Can;
    CanThrowResult CT = canDynamicCastThrow(CE);
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, CE));
  }

  case Expr::CXXTypeidExprClass:
    //   - a potentially evaluated typeid expression applied to a (possibly
    //     parenthesized) built-in unary * operator applied to a pointer to a
    //     polymorphic class type
    return canTypeidThrow(*this, cast<CXXTypeidExpr>(S));

    //   - a potentially evaluated call to a function, member function, function
    //     pointer, or member function pointer that does not have a non-throwing
    //     exception-specification
  case Expr::CallExprClass:
  case Expr::CXXMemberCallExprClass:
  case Expr::CXXOperatorCallExprClass:
  case Expr::UserDefinedLiteralClass: {
    const CallExpr *CE = cast<CallExpr>(S);
    CanThrowResult CT;
    if (CE->isTypeDependent())
      CT = CT_Dependent;
    else if (isa<CXXPseudoDestructorExpr>(CE->getCallee()->IgnoreParens()))
      CT = CT_Cannot;
    else
      CT = canCalleeThrow(*this, CE, CE->getCalleeDecl());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, CE));
  }

  case Expr::CXXConstructExprClass:
  case Expr::CXXTemporaryObjectExprClass: {
    auto *CE = cast<CXXConstructExpr>(S);
    // FIXME: Properly determine whether a variably-modified type can throw.
    if (CE->getType()->isVariablyModifiedType())
      return CT_Can;
    CanThrowResult CT = canCalleeThrow(*this, CE, CE->getConstructor());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, CE));
  }

  case Expr::CXXInheritedCtorInitExprClass: {
    auto *ICIE = cast<CXXInheritedCtorInitExpr>(S);
    return canCalleeThrow(*this, ICIE, ICIE->getConstructor());
  }

  case Expr::LambdaExprClass: {
    const LambdaExpr *Lambda = cast<LambdaExpr>(S);
    CanThrowResult CT = CT_Cannot;
    for (LambdaExpr::const_capture_init_iterator
             Cap = Lambda->capture_init_begin(),
             CapEnd = Lambda->capture_init_end();
         Cap != CapEnd; ++Cap)
      CT = mergeCanThrow(CT, canThrow(*Cap));
    return CT;
  }

  case Expr::CXXNewExprClass: {
    auto *NE = cast<CXXNewExpr>(S);
    CanThrowResult CT;
    if (NE->isTypeDependent())
      CT = CT_Dependent;
    else
      CT = canCalleeThrow(*this, NE, NE->getOperatorNew());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, NE));
  }

  case Expr::CXXDeleteExprClass: {
    auto *DE = cast<CXXDeleteExpr>(S);
    CanThrowResult CT;
    QualType DTy = DE->getDestroyedType();
    if (DTy.isNull() || DTy->isDependentType()) {
      CT = CT_Dependent;
    } else {
      CT = canCalleeThrow(*this, DE, DE->getOperatorDelete());
      if (const RecordType *RT = DTy->getAs<RecordType>()) {
        const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
        const CXXDestructorDecl *DD = RD->getDestructor();
        if (DD)
          CT = mergeCanThrow(CT, canCalleeThrow(*this, DE, DD));
      }
      if (CT == CT_Can)
        return CT;
    }
    return mergeCanThrow(CT, canSubStmtsThrow(*this, DE));
  }

  case Expr::CXXBindTemporaryExprClass: {
    auto *BTE = cast<CXXBindTemporaryExpr>(S);
    // The bound temporary has to be destroyed again, which might throw.
    CanThrowResult CT =
        canCalleeThrow(*this, BTE, BTE->getTemporary()->getDestructor());
    if (CT == CT_Can)
      return CT;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, BTE));
  }

  case Expr::PseudoObjectExprClass: {
    auto *POE = cast<PseudoObjectExpr>(S);
    CanThrowResult CT = CT_Cannot;
    for (const Expr *E : POE->semantics()) {
      CT = mergeCanThrow(CT, canThrow(E));
      if (CT == CT_Can)
        break;
    }
    return CT;
  }

    // ObjC message sends are like function calls, but never have exception
    // specs.
  case Expr::ObjCMessageExprClass:
  case Expr::ObjCPropertyRefExprClass:
  case Expr::ObjCSubscriptRefExprClass:
    return CT_Can;

    // All the ObjC literals that are implemented as calls are
    // potentially throwing unless we decide to close off that
    // possibility.
  case Expr::ObjCArrayLiteralClass:
  case Expr::ObjCDictionaryLiteralClass:
  case Expr::ObjCBoxedExprClass:
    return CT_Can;

    // Many other things have subexpressions, so we have to test those.
    // Some are simple:
  case Expr::CoawaitExprClass:
  case Expr::ConditionalOperatorClass:
  case Expr::CoyieldExprClass:
  case Expr::CXXRewrittenBinaryOperatorClass:
  case Expr::CXXStdInitializerListExprClass:
  case Expr::DesignatedInitExprClass:
  case Expr::DesignatedInitUpdateExprClass:
  case Expr::ExprWithCleanupsClass:
  case Expr::ExtVectorElementExprClass:
  case Expr::InitListExprClass:
  case Expr::ArrayInitLoopExprClass:
  case Expr::MemberExprClass:
  case Expr::ObjCIsaExprClass:
  case Expr::ObjCIvarRefExprClass:
  case Expr::ParenExprClass:
  case Expr::ParenListExprClass:
  case Expr::ShuffleVectorExprClass:
  case Expr::StmtExprClass:
  case Expr::ConvertVectorExprClass:
  case Expr::VAArgExprClass:
  case Expr::CXXParenListInitExprClass:
    return canSubStmtsThrow(*this, S);

  case Expr::CompoundLiteralExprClass:
  case Expr::CXXConstCastExprClass:
  case Expr::CXXAddrspaceCastExprClass:
  case Expr::CXXReinterpretCastExprClass:
  case Expr::BuiltinBitCastExprClass:
      // FIXME: Properly determine whether a variably-modified type can throw.
    if (cast<Expr>(S)->getType()->isVariablyModifiedType())
      return CT_Can;
    return canSubStmtsThrow(*this, S);

    // Some might be dependent for other reasons.
  case Expr::ArraySubscriptExprClass:
  case Expr::MatrixSubscriptExprClass:
  case Expr::ArraySectionExprClass:
  case Expr::OMPArrayShapingExprClass:
  case Expr::OMPIteratorExprClass:
  case Expr::BinaryOperatorClass:
  case Expr::DependentCoawaitExprClass:
  case Expr::CompoundAssignOperatorClass:
  case Expr::CStyleCastExprClass:
  case Expr::CXXStaticCastExprClass:
  case Expr::CXXFunctionalCastExprClass:
  case Expr::ImplicitCastExprClass:
  case Expr::MaterializeTemporaryExprClass:
  case Expr::UnaryOperatorClass: {
    // FIXME: Properly determine whether a variably-modified type can throw.
    if (auto *CE = dyn_cast<CastExpr>(S))
      if (CE->getType()->isVariablyModifiedType())
        return CT_Can;
    CanThrowResult CT =
        cast<Expr>(S)->isTypeDependent() ? CT_Dependent : CT_Cannot;
    return mergeCanThrow(CT, canSubStmtsThrow(*this, S));
  }

  case Expr::CXXDefaultArgExprClass:
    return canThrow(cast<CXXDefaultArgExpr>(S)->getExpr());

  case Expr::CXXDefaultInitExprClass:
    return canThrow(cast<CXXDefaultInitExpr>(S)->getExpr());

  case Expr::ChooseExprClass: {
    auto *CE = cast<ChooseExpr>(S);
    if (CE->isTypeDependent() || CE->isValueDependent())
      return CT_Dependent;
    return canThrow(CE->getChosenSubExpr());
  }

  case Expr::GenericSelectionExprClass:
    if (cast<GenericSelectionExpr>(S)->isResultDependent())
      return CT_Dependent;
    return canThrow(cast<GenericSelectionExpr>(S)->getResultExpr());

    // Some expressions are always dependent.
  case Expr::CXXDependentScopeMemberExprClass:
  case Expr::CXXUnresolvedConstructExprClass:
  case Expr::DependentScopeDeclRefExprClass:
  case Expr::CXXFoldExprClass:
  case Expr::RecoveryExprClass:
    return CT_Dependent;

  case Expr::AsTypeExprClass:
  case Expr::BinaryConditionalOperatorClass:
  case Expr::BlockExprClass:
  case Expr::CUDAKernelCallExprClass:
  case Expr::DeclRefExprClass:
  case Expr::ObjCBridgedCastExprClass:
  case Expr::ObjCIndirectCopyRestoreExprClass:
  case Expr::ObjCProtocolExprClass:
  case Expr::ObjCSelectorExprClass:
  case Expr::ObjCAvailabilityCheckExprClass:
  case Expr::OffsetOfExprClass:
  case Expr::PackExpansionExprClass:
  case Expr::SubstNonTypeTemplateParmExprClass:
  case Expr::SubstNonTypeTemplateParmPackExprClass:
  case Expr::FunctionParmPackExprClass:
  case Expr::UnaryExprOrTypeTraitExprClass:
  case Expr::UnresolvedLookupExprClass:
  case Expr::UnresolvedMemberExprClass:
  case Expr::TypoExprClass:
    // FIXME: Many of the above can throw.
    return CT_Cannot;

  case Expr::AddrLabelExprClass:
  case Expr::ArrayTypeTraitExprClass:
  case Expr::AtomicExprClass:
  case Expr::TypeTraitExprClass:
  case Expr::CXXBoolLiteralExprClass:
  case Expr::CXXNoexceptExprClass:
  case Expr::CXXNullPtrLiteralExprClass:
  case Expr::CXXPseudoDestructorExprClass:
  case Expr::CXXScalarValueInitExprClass:
  case Expr::CXXThisExprClass:
  case Expr::CXXUuidofExprClass:
  case Expr::CharacterLiteralClass:
  case Expr::ExpressionTraitExprClass:
  case Expr::FloatingLiteralClass:
  case Expr::GNUNullExprClass:
  case Expr::ImaginaryLiteralClass:
  case Expr::ImplicitValueInitExprClass:
  case Expr::IntegerLiteralClass:
  case Expr::FixedPointLiteralClass:
  case Expr::ArrayInitIndexExprClass:
  case Expr::NoInitExprClass:
  case Expr::ObjCEncodeExprClass:
  case Expr::ObjCStringLiteralClass:
  case Expr::ObjCBoolLiteralExprClass:
  case Expr::OpaqueValueExprClass:
  case Expr::PredefinedExprClass:
  case Expr::SizeOfPackExprClass:
  case Expr::PackIndexingExprClass:
  case Expr::StringLiteralClass:
  case Expr::SourceLocExprClass:
  case Expr::EmbedExprClass:
  case Expr::ConceptSpecializationExprClass:
  case Expr::RequiresExprClass:
    // These expressions can never throw.
    return CT_Cannot;

  case Expr::MSPropertyRefExprClass:
  case Expr::MSPropertySubscriptExprClass:
    llvm_unreachable("Invalid class for expression");

    // Most statements can throw if any substatement can throw.
  case Stmt::OpenACCComputeConstructClass:
  case Stmt::OpenACCLoopConstructClass:
  case Stmt::AttributedStmtClass:
  case Stmt::BreakStmtClass:
  case Stmt::CapturedStmtClass:
  case Stmt::CaseStmtClass:
  case Stmt::CompoundStmtClass:
  case Stmt::ContinueStmtClass:
  case Stmt::CoreturnStmtClass:
  case Stmt::CoroutineBodyStmtClass:
  case Stmt::CXXCatchStmtClass:
  case Stmt::CXXForRangeStmtClass:
  case Stmt::DefaultStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::ForStmtClass:
  case Stmt::GCCAsmStmtClass:
  case Stmt::GotoStmtClass:
  case Stmt::IndirectGotoStmtClass:
  case Stmt::LabelStmtClass:
  case Stmt::MSAsmStmtClass:
  case Stmt::MSDependentExistsStmtClass:
  case Stmt::NullStmtClass:
  case Stmt::ObjCAtCatchStmtClass:
  case Stmt::ObjCAtFinallyStmtClass:
  case Stmt::ObjCAtSynchronizedStmtClass:
  case Stmt::ObjCAutoreleasePoolStmtClass:
  case Stmt::ObjCForCollectionStmtClass:
  case Stmt::OMPAtomicDirectiveClass:
  case Stmt::OMPBarrierDirectiveClass:
  case Stmt::OMPCancelDirectiveClass:
  case Stmt::OMPCancellationPointDirectiveClass:
  case Stmt::OMPCriticalDirectiveClass:
  case Stmt::OMPDistributeDirectiveClass:
  case Stmt::OMPDistributeParallelForDirectiveClass:
  case Stmt::OMPDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPDistributeSimdDirectiveClass:
  case Stmt::OMPFlushDirectiveClass:
  case Stmt::OMPDepobjDirectiveClass:
  case Stmt::OMPScanDirectiveClass:
  case Stmt::OMPForDirectiveClass:
  case Stmt::OMPForSimdDirectiveClass:
  case Stmt::OMPMasterDirectiveClass:
  case Stmt::OMPMasterTaskLoopDirectiveClass:
  case Stmt::OMPMaskedTaskLoopDirectiveClass:
  case Stmt::OMPMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPOrderedDirectiveClass:
  case Stmt::OMPCanonicalLoopClass:
  case Stmt::OMPParallelDirectiveClass:
  case Stmt::OMPParallelForDirectiveClass:
  case Stmt::OMPParallelForSimdDirectiveClass:
  case Stmt::OMPParallelMasterDirectiveClass:
  case Stmt::OMPParallelMaskedDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopDirectiveClass:
  case Stmt::OMPParallelMasterTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelMaskedTaskLoopSimdDirectiveClass:
  case Stmt::OMPParallelSectionsDirectiveClass:
  case Stmt::OMPSectionDirectiveClass:
  case Stmt::OMPSectionsDirectiveClass:
  case Stmt::OMPSimdDirectiveClass:
  case Stmt::OMPTileDirectiveClass:
  case Stmt::OMPUnrollDirectiveClass:
  case Stmt::OMPReverseDirectiveClass:
  case Stmt::OMPInterchangeDirectiveClass:
  case Stmt::OMPSingleDirectiveClass:
  case Stmt::OMPTargetDataDirectiveClass:
  case Stmt::OMPTargetDirectiveClass:
  case Stmt::OMPTargetEnterDataDirectiveClass:
  case Stmt::OMPTargetExitDataDirectiveClass:
  case Stmt::OMPTargetParallelDirectiveClass:
  case Stmt::OMPTargetParallelForDirectiveClass:
  case Stmt::OMPTargetParallelForSimdDirectiveClass:
  case Stmt::OMPTargetSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTargetTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPTargetUpdateDirectiveClass:
  case Stmt::OMPScopeDirectiveClass:
  case Stmt::OMPTaskDirectiveClass:
  case Stmt::OMPTaskgroupDirectiveClass:
  case Stmt::OMPTaskLoopDirectiveClass:
  case Stmt::OMPTaskLoopSimdDirectiveClass:
  case Stmt::OMPTaskwaitDirectiveClass:
  case Stmt::OMPTaskyieldDirectiveClass:
  case Stmt::OMPErrorDirectiveClass:
  case Stmt::OMPTeamsDirectiveClass:
  case Stmt::OMPTeamsDistributeDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForDirectiveClass:
  case Stmt::OMPTeamsDistributeParallelForSimdDirectiveClass:
  case Stmt::OMPTeamsDistributeSimdDirectiveClass:
  case Stmt::OMPInteropDirectiveClass:
  case Stmt::OMPDispatchDirectiveClass:
  case Stmt::OMPMaskedDirectiveClass:
  case Stmt::OMPMetaDirectiveClass:
  case Stmt::OMPGenericLoopDirectiveClass:
  case Stmt::OMPTeamsGenericLoopDirectiveClass:
  case Stmt::OMPTargetTeamsGenericLoopDirectiveClass:
  case Stmt::OMPParallelGenericLoopDirectiveClass:
  case Stmt::OMPTargetParallelGenericLoopDirectiveClass:
  case Stmt::ReturnStmtClass:
  case Stmt::SEHExceptStmtClass:
  case Stmt::SEHFinallyStmtClass:
  case Stmt::SEHLeaveStmtClass:
  case Stmt::SEHTryStmtClass:
  case Stmt::SwitchStmtClass:
  case Stmt::WhileStmtClass:
    return canSubStmtsThrow(*this, S);

  case Stmt::DeclStmtClass: {
    CanThrowResult CT = CT_Cannot;
    for (const Decl *D : cast<DeclStmt>(S)->decls()) {
      if (auto *VD = dyn_cast<VarDecl>(D))
        CT = mergeCanThrow(CT, canVarDeclThrow(*this, VD));

      // FIXME: Properly determine whether a variably-modified type can throw.
      if (auto *TND = dyn_cast<TypedefNameDecl>(D))
        if (TND->getUnderlyingType()->isVariablyModifiedType())
          return CT_Can;
      if (auto *VD = dyn_cast<ValueDecl>(D))
        if (VD->getType()->isVariablyModifiedType())
          return CT_Can;
    }
    return CT;
  }

  case Stmt::IfStmtClass: {
    auto *IS = cast<IfStmt>(S);
    CanThrowResult CT = CT_Cannot;
    if (const Stmt *Init = IS->getInit())
      CT = mergeCanThrow(CT, canThrow(Init));
    if (const Stmt *CondDS = IS->getConditionVariableDeclStmt())
      CT = mergeCanThrow(CT, canThrow(CondDS));
    CT = mergeCanThrow(CT, canThrow(IS->getCond()));

    // For 'if constexpr', consider only the non-discarded case.
    // FIXME: We should add a DiscardedStmt marker to the AST.
    if (std::optional<const Stmt *> Case = IS->getNondiscardedCase(Context))
      return *Case ? mergeCanThrow(CT, canThrow(*Case)) : CT;

    CanThrowResult Then = canThrow(IS->getThen());
    CanThrowResult Else = IS->getElse() ? canThrow(IS->getElse()) : CT_Cannot;
    if (Then == Else)
      return mergeCanThrow(CT, Then);

    // For a dependent 'if constexpr', the result is dependent if it depends on
    // the value of the condition.
    return mergeCanThrow(CT, IS->isConstexpr() ? CT_Dependent
                                               : mergeCanThrow(Then, Else));
  }

  case Stmt::CXXTryStmtClass: {
    auto *TS = cast<CXXTryStmt>(S);
    // try /*...*/ catch (...) { H } can throw only if H can throw.
    // Any other try-catch can throw if any substatement can throw.
    const CXXCatchStmt *FinalHandler = TS->getHandler(TS->getNumHandlers() - 1);
    if (!FinalHandler->getExceptionDecl())
      return canThrow(FinalHandler->getHandlerBlock());
    return canSubStmtsThrow(*this, S);
  }

  case Stmt::ObjCAtThrowStmtClass:
    return CT_Can;

  case Stmt::ObjCAtTryStmtClass: {
    auto *TS = cast<ObjCAtTryStmt>(S);

    // @catch(...) need not be last in Objective-C. Walk backwards until we
    // see one or hit the @try.
    CanThrowResult CT = CT_Cannot;
    if (const Stmt *Finally = TS->getFinallyStmt())
      CT = mergeCanThrow(CT, canThrow(Finally));
    for (unsigned I = TS->getNumCatchStmts(); I != 0; --I) {
      const ObjCAtCatchStmt *Catch = TS->getCatchStmt(I - 1);
      CT = mergeCanThrow(CT, canThrow(Catch));
      // If we reach a @catch(...), no earlier exceptions can escape.
      if (Catch->hasEllipsis())
        return CT;
    }

    // Didn't find an @catch(...). Exceptions from the @try body can escape.
    return mergeCanThrow(CT, canThrow(TS->getTryBody()));
  }

  case Stmt::SYCLUniqueStableNameExprClass:
    return CT_Cannot;
  case Stmt::NoStmtClass:
    llvm_unreachable("Invalid class for statement");
  }
  llvm_unreachable("Bogus StmtClass");
}

} // end namespace clang
