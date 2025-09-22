//===--- SemaInit.cpp - Semantic Analysis for Initializers ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements semantic analysis for initializers.
//
//===----------------------------------------------------------------------===//

#include "CheckExprLifetime.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/ExprOpenMP.h"
#include "clang/AST/IgnoreExpr.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/CharInfo.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/Designator.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Initialization.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/SemaInternal.h"
#include "clang/Sema/SemaObjC.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Sema Initialization Checking
//===----------------------------------------------------------------------===//

/// Check whether T is compatible with a wide character type (wchar_t,
/// char16_t or char32_t).
static bool IsWideCharCompatible(QualType T, ASTContext &Context) {
  if (Context.typesAreCompatible(Context.getWideCharType(), T))
    return true;
  if (Context.getLangOpts().CPlusPlus || Context.getLangOpts().C11) {
    return Context.typesAreCompatible(Context.Char16Ty, T) ||
           Context.typesAreCompatible(Context.Char32Ty, T);
  }
  return false;
}

enum StringInitFailureKind {
  SIF_None,
  SIF_NarrowStringIntoWideChar,
  SIF_WideStringIntoChar,
  SIF_IncompatWideStringIntoWideChar,
  SIF_UTF8StringIntoPlainChar,
  SIF_PlainStringIntoUTF8Char,
  SIF_Other
};

/// Check whether the array of type AT can be initialized by the Init
/// expression by means of string initialization. Returns SIF_None if so,
/// otherwise returns a StringInitFailureKind that describes why the
/// initialization would not work.
static StringInitFailureKind IsStringInit(Expr *Init, const ArrayType *AT,
                                          ASTContext &Context) {
  if (!isa<ConstantArrayType>(AT) && !isa<IncompleteArrayType>(AT))
    return SIF_Other;

  // See if this is a string literal or @encode.
  Init = Init->IgnoreParens();

  // Handle @encode, which is a narrow string.
  if (isa<ObjCEncodeExpr>(Init) && AT->getElementType()->isCharType())
    return SIF_None;

  // Otherwise we can only handle string literals.
  StringLiteral *SL = dyn_cast<StringLiteral>(Init);
  if (!SL)
    return SIF_Other;

  const QualType ElemTy =
      Context.getCanonicalType(AT->getElementType()).getUnqualifiedType();

  auto IsCharOrUnsignedChar = [](const QualType &T) {
    const BuiltinType *BT = dyn_cast<BuiltinType>(T.getTypePtr());
    return BT && BT->isCharType() && BT->getKind() != BuiltinType::SChar;
  };

  switch (SL->getKind()) {
  case StringLiteralKind::UTF8:
    // char8_t array can be initialized with a UTF-8 string.
    // - C++20 [dcl.init.string] (DR)
    //   Additionally, an array of char or unsigned char may be initialized
    //   by a UTF-8 string literal.
    if (ElemTy->isChar8Type() ||
        (Context.getLangOpts().Char8 &&
         IsCharOrUnsignedChar(ElemTy.getCanonicalType())))
      return SIF_None;
    [[fallthrough]];
  case StringLiteralKind::Ordinary:
    // char array can be initialized with a narrow string.
    // Only allow char x[] = "foo";  not char x[] = L"foo";
    if (ElemTy->isCharType())
      return (SL->getKind() == StringLiteralKind::UTF8 &&
              Context.getLangOpts().Char8)
                 ? SIF_UTF8StringIntoPlainChar
                 : SIF_None;
    if (ElemTy->isChar8Type())
      return SIF_PlainStringIntoUTF8Char;
    if (IsWideCharCompatible(ElemTy, Context))
      return SIF_NarrowStringIntoWideChar;
    return SIF_Other;
  // C99 6.7.8p15 (with correction from DR343), or C11 6.7.9p15:
  // "An array with element type compatible with a qualified or unqualified
  // version of wchar_t, char16_t, or char32_t may be initialized by a wide
  // string literal with the corresponding encoding prefix (L, u, or U,
  // respectively), optionally enclosed in braces.
  case StringLiteralKind::UTF16:
    if (Context.typesAreCompatible(Context.Char16Ty, ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (IsWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::UTF32:
    if (Context.typesAreCompatible(Context.Char32Ty, ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (IsWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::Wide:
    if (Context.typesAreCompatible(Context.getWideCharType(), ElemTy))
      return SIF_None;
    if (ElemTy->isCharType() || ElemTy->isChar8Type())
      return SIF_WideStringIntoChar;
    if (IsWideCharCompatible(ElemTy, Context))
      return SIF_IncompatWideStringIntoWideChar;
    return SIF_Other;
  case StringLiteralKind::Unevaluated:
    assert(false && "Unevaluated string literal in initialization");
    break;
  }

  llvm_unreachable("missed a StringLiteral kind?");
}

static StringInitFailureKind IsStringInit(Expr *init, QualType declType,
                                          ASTContext &Context) {
  const ArrayType *arrayType = Context.getAsArrayType(declType);
  if (!arrayType)
    return SIF_Other;
  return IsStringInit(init, arrayType, Context);
}

bool Sema::IsStringInit(Expr *Init, const ArrayType *AT) {
  return ::IsStringInit(Init, AT, Context) == SIF_None;
}

/// Update the type of a string literal, including any surrounding parentheses,
/// to match the type of the object which it is initializing.
static void updateStringLiteralType(Expr *E, QualType Ty) {
  while (true) {
    E->setType(Ty);
    E->setValueKind(VK_PRValue);
    if (isa<StringLiteral>(E) || isa<ObjCEncodeExpr>(E))
      break;
    E = IgnoreParensSingleStep(E);
  }
}

/// Fix a compound literal initializing an array so it's correctly marked
/// as an rvalue.
static void updateGNUCompoundLiteralRValue(Expr *E) {
  while (true) {
    E->setValueKind(VK_PRValue);
    if (isa<CompoundLiteralExpr>(E))
      break;
    E = IgnoreParensSingleStep(E);
  }
}

static bool initializingConstexprVariable(const InitializedEntity &Entity) {
  Decl *D = Entity.getDecl();
  const InitializedEntity *Parent = &Entity;

  while (Parent) {
    D = Parent->getDecl();
    Parent = Parent->getParent();
  }

  if (const auto *VD = dyn_cast_if_present<VarDecl>(D); VD && VD->isConstexpr())
    return true;

  return false;
}

static void CheckC23ConstexprInitStringLiteral(const StringLiteral *SE,
                                               Sema &SemaRef, QualType &TT);

static void CheckStringInit(Expr *Str, QualType &DeclT, const ArrayType *AT,
                            Sema &S, bool CheckC23ConstexprInit = false) {
  // Get the length of the string as parsed.
  auto *ConstantArrayTy =
      cast<ConstantArrayType>(Str->getType()->getAsArrayTypeUnsafe());
  uint64_t StrLength = ConstantArrayTy->getZExtSize();

  if (CheckC23ConstexprInit)
    if (const StringLiteral *SL = dyn_cast<StringLiteral>(Str->IgnoreParens()))
      CheckC23ConstexprInitStringLiteral(SL, S, DeclT);

  if (const IncompleteArrayType *IAT = dyn_cast<IncompleteArrayType>(AT)) {
    // C99 6.7.8p14. We have an array of character type with unknown size
    // being initialized to a string literal.
    llvm::APInt ConstVal(32, StrLength);
    // Return a new array type (C99 6.7.8p22).
    DeclT = S.Context.getConstantArrayType(
        IAT->getElementType(), ConstVal, nullptr, ArraySizeModifier::Normal, 0);
    updateStringLiteralType(Str, DeclT);
    return;
  }

  const ConstantArrayType *CAT = cast<ConstantArrayType>(AT);

  // We have an array of character type with known size.  However,
  // the size may be smaller or larger than the string we are initializing.
  // FIXME: Avoid truncation for 64-bit length strings.
  if (S.getLangOpts().CPlusPlus) {
    if (StringLiteral *SL = dyn_cast<StringLiteral>(Str->IgnoreParens())) {
      // For Pascal strings it's OK to strip off the terminating null character,
      // so the example below is valid:
      //
      // unsigned char a[2] = "\pa";
      if (SL->isPascal())
        StrLength--;
    }

    // [dcl.init.string]p2
    if (StrLength > CAT->getZExtSize())
      S.Diag(Str->getBeginLoc(),
             diag::err_initializer_string_for_char_array_too_long)
          << CAT->getZExtSize() << StrLength << Str->getSourceRange();
  } else {
    // C99 6.7.8p14.
    if (StrLength - 1 > CAT->getZExtSize())
      S.Diag(Str->getBeginLoc(),
             diag::ext_initializer_string_for_char_array_too_long)
          << Str->getSourceRange();
  }

  // Set the type to the actual size that we are initializing.  If we have
  // something like:
  //   char x[1] = "foo";
  // then this will set the string literal's type to char[1].
  updateStringLiteralType(Str, DeclT);
}

//===----------------------------------------------------------------------===//
// Semantic checking for initializer lists.
//===----------------------------------------------------------------------===//

namespace {

/// Semantic checking for initializer lists.
///
/// The InitListChecker class contains a set of routines that each
/// handle the initialization of a certain kind of entity, e.g.,
/// arrays, vectors, struct/union types, scalars, etc. The
/// InitListChecker itself performs a recursive walk of the subobject
/// structure of the type to be initialized, while stepping through
/// the initializer list one element at a time. The IList and Index
/// parameters to each of the Check* routines contain the active
/// (syntactic) initializer list and the index into that initializer
/// list that represents the current initializer. Each routine is
/// responsible for moving that Index forward as it consumes elements.
///
/// Each Check* routine also has a StructuredList/StructuredIndex
/// arguments, which contains the current "structured" (semantic)
/// initializer list and the index into that initializer list where we
/// are copying initializers as we map them over to the semantic
/// list. Once we have completed our recursive walk of the subobject
/// structure, we will have constructed a full semantic initializer
/// list.
///
/// C99 designators cause changes in the initializer list traversal,
/// because they make the initialization "jump" into a specific
/// subobject and then continue the initialization from that
/// point. CheckDesignatedInitializer() recursively steps into the
/// designated subobject and manages backing out the recursion to
/// initialize the subobjects after the one designated.
///
/// If an initializer list contains any designators, we build a placeholder
/// structured list even in 'verify only' mode, so that we can track which
/// elements need 'empty' initializtion.
class InitListChecker {
  Sema &SemaRef;
  bool hadError = false;
  bool VerifyOnly; // No diagnostics.
  bool TreatUnavailableAsInvalid; // Used only in VerifyOnly mode.
  bool InOverloadResolution;
  InitListExpr *FullyStructuredList = nullptr;
  NoInitExpr *DummyExpr = nullptr;
  SmallVectorImpl<QualType> *AggrDeductionCandidateParamTypes = nullptr;
  EmbedExpr *CurEmbed = nullptr; // Save current embed we're processing.
  unsigned CurEmbedIndex = 0;

  NoInitExpr *getDummyInit() {
    if (!DummyExpr)
      DummyExpr = new (SemaRef.Context) NoInitExpr(SemaRef.Context.VoidTy);
    return DummyExpr;
  }

  void CheckImplicitInitList(const InitializedEntity &Entity,
                             InitListExpr *ParentIList, QualType T,
                             unsigned &Index, InitListExpr *StructuredList,
                             unsigned &StructuredIndex);
  void CheckExplicitInitList(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType &T,
                             InitListExpr *StructuredList,
                             bool TopLevelObject = false);
  void CheckListElementTypes(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType &DeclType,
                             bool SubobjectIsDesignatorContext,
                             unsigned &Index,
                             InitListExpr *StructuredList,
                             unsigned &StructuredIndex,
                             bool TopLevelObject = false);
  void CheckSubElementType(const InitializedEntity &Entity,
                           InitListExpr *IList, QualType ElemType,
                           unsigned &Index,
                           InitListExpr *StructuredList,
                           unsigned &StructuredIndex,
                           bool DirectlyDesignated = false);
  void CheckComplexType(const InitializedEntity &Entity,
                        InitListExpr *IList, QualType DeclType,
                        unsigned &Index,
                        InitListExpr *StructuredList,
                        unsigned &StructuredIndex);
  void CheckScalarType(const InitializedEntity &Entity,
                       InitListExpr *IList, QualType DeclType,
                       unsigned &Index,
                       InitListExpr *StructuredList,
                       unsigned &StructuredIndex);
  void CheckReferenceType(const InitializedEntity &Entity,
                          InitListExpr *IList, QualType DeclType,
                          unsigned &Index,
                          InitListExpr *StructuredList,
                          unsigned &StructuredIndex);
  void CheckVectorType(const InitializedEntity &Entity,
                       InitListExpr *IList, QualType DeclType, unsigned &Index,
                       InitListExpr *StructuredList,
                       unsigned &StructuredIndex);
  void CheckStructUnionTypes(const InitializedEntity &Entity,
                             InitListExpr *IList, QualType DeclType,
                             CXXRecordDecl::base_class_const_range Bases,
                             RecordDecl::field_iterator Field,
                             bool SubobjectIsDesignatorContext, unsigned &Index,
                             InitListExpr *StructuredList,
                             unsigned &StructuredIndex,
                             bool TopLevelObject = false);
  void CheckArrayType(const InitializedEntity &Entity,
                      InitListExpr *IList, QualType &DeclType,
                      llvm::APSInt elementIndex,
                      bool SubobjectIsDesignatorContext, unsigned &Index,
                      InitListExpr *StructuredList,
                      unsigned &StructuredIndex);
  bool CheckDesignatedInitializer(const InitializedEntity &Entity,
                                  InitListExpr *IList, DesignatedInitExpr *DIE,
                                  unsigned DesigIdx,
                                  QualType &CurrentObjectType,
                                  RecordDecl::field_iterator *NextField,
                                  llvm::APSInt *NextElementIndex,
                                  unsigned &Index,
                                  InitListExpr *StructuredList,
                                  unsigned &StructuredIndex,
                                  bool FinishSubobjectInit,
                                  bool TopLevelObject);
  InitListExpr *getStructuredSubobjectInit(InitListExpr *IList, unsigned Index,
                                           QualType CurrentObjectType,
                                           InitListExpr *StructuredList,
                                           unsigned StructuredIndex,
                                           SourceRange InitRange,
                                           bool IsFullyOverwritten = false);
  void UpdateStructuredListElement(InitListExpr *StructuredList,
                                   unsigned &StructuredIndex,
                                   Expr *expr);
  InitListExpr *createInitListExpr(QualType CurrentObjectType,
                                   SourceRange InitRange,
                                   unsigned ExpectedNumInits);
  int numArrayElements(QualType DeclType);
  int numStructUnionElements(QualType DeclType);
  static RecordDecl *getRecordDecl(QualType DeclType);

  ExprResult PerformEmptyInit(SourceLocation Loc,
                              const InitializedEntity &Entity);

  /// Diagnose that OldInit (or part thereof) has been overridden by NewInit.
  void diagnoseInitOverride(Expr *OldInit, SourceRange NewInitRange,
                            bool UnionOverride = false,
                            bool FullyOverwritten = true) {
    // Overriding an initializer via a designator is valid with C99 designated
    // initializers, but ill-formed with C++20 designated initializers.
    unsigned DiagID =
        SemaRef.getLangOpts().CPlusPlus
            ? (UnionOverride ? diag::ext_initializer_union_overrides
                             : diag::ext_initializer_overrides)
            : diag::warn_initializer_overrides;

    if (InOverloadResolution && SemaRef.getLangOpts().CPlusPlus) {
      // In overload resolution, we have to strictly enforce the rules, and so
      // don't allow any overriding of prior initializers. This matters for a
      // case such as:
      //
      //   union U { int a, b; };
      //   struct S { int a, b; };
      //   void f(U), f(S);
      //
      // Here, f({.a = 1, .b = 2}) is required to call the struct overload. For
      // consistency, we disallow all overriding of prior initializers in
      // overload resolution, not only overriding of union members.
      hadError = true;
    } else if (OldInit->getType().isDestructedType() && !FullyOverwritten) {
      // If we'll be keeping around the old initializer but overwriting part of
      // the object it initialized, and that object is not trivially
      // destructible, this can leak. Don't allow that, not even as an
      // extension.
      //
      // FIXME: It might be reasonable to allow this in cases where the part of
      // the initializer that we're overriding has trivial destruction.
      DiagID = diag::err_initializer_overrides_destructed;
    } else if (!OldInit->getSourceRange().isValid()) {
      // We need to check on source range validity because the previous
      // initializer does not have to be an explicit initializer. e.g.,
      //
      // struct P { int a, b; };
      // struct PP { struct P p } l = { { .a = 2 }, .p.b = 3 };
      //
      // There is an overwrite taking place because the first braced initializer
      // list "{ .a = 2 }" already provides value for .p.b (which is zero).
      //
      // Such overwrites are harmless, so we don't diagnose them. (Note that in
      // C++, this cannot be reached unless we've already seen and diagnosed a
      // different conformance issue, such as a mixture of designated and
      // non-designated initializers or a multi-level designator.)
      return;
    }

    if (!VerifyOnly) {
      SemaRef.Diag(NewInitRange.getBegin(), DiagID)
          << NewInitRange << FullyOverwritten << OldInit->getType();
      SemaRef.Diag(OldInit->getBeginLoc(), diag::note_previous_initializer)
          << (OldInit->HasSideEffects(SemaRef.Context) && FullyOverwritten)
          << OldInit->getSourceRange();
    }
  }

  // Explanation on the "FillWithNoInit" mode:
  //
  // Assume we have the following definitions (Case#1):
  // struct P { char x[6][6]; } xp = { .x[1] = "bar" };
  // struct PP { struct P lp; } l = { .lp = xp, .lp.x[1][2] = 'f' };
  //
  // l.lp.x[1][0..1] should not be filled with implicit initializers because the
  // "base" initializer "xp" will provide values for them; l.lp.x[1] will be "baf".
  //
  // But if we have (Case#2):
  // struct PP l = { .lp = xp, .lp.x[1] = { [2] = 'f' } };
  //
  // l.lp.x[1][0..1] are implicitly initialized and do not use values from the
  // "base" initializer; l.lp.x[1] will be "\0\0f\0\0\0".
  //
  // To distinguish Case#1 from Case#2, and also to avoid leaving many "holes"
  // in the InitListExpr, the "holes" in Case#1 are filled not with empty
  // initializers but with special "NoInitExpr" place holders, which tells the
  // CodeGen not to generate any initializers for these parts.
  void FillInEmptyInitForBase(unsigned Init, const CXXBaseSpecifier &Base,
                              const InitializedEntity &ParentEntity,
                              InitListExpr *ILE, bool &RequiresSecondPass,
                              bool FillWithNoInit);
  void FillInEmptyInitForField(unsigned Init, FieldDecl *Field,
                               const InitializedEntity &ParentEntity,
                               InitListExpr *ILE, bool &RequiresSecondPass,
                               bool FillWithNoInit = false);
  void FillInEmptyInitializations(const InitializedEntity &Entity,
                                  InitListExpr *ILE, bool &RequiresSecondPass,
                                  InitListExpr *OuterILE, unsigned OuterIndex,
                                  bool FillWithNoInit = false);
  bool CheckFlexibleArrayInit(const InitializedEntity &Entity,
                              Expr *InitExpr, FieldDecl *Field,
                              bool TopLevelObject);
  void CheckEmptyInitializable(const InitializedEntity &Entity,
                               SourceLocation Loc);

  Expr *HandleEmbed(EmbedExpr *Embed, const InitializedEntity &Entity) {
    Expr *Result = nullptr;
    // Undrestand which part of embed we'd like to reference.
    if (!CurEmbed) {
      CurEmbed = Embed;
      CurEmbedIndex = 0;
    }
    // Reference just one if we're initializing a single scalar.
    uint64_t ElsCount = 1;
    // Otherwise try to fill whole array with embed data.
    if (Entity.getKind() == InitializedEntity::EK_ArrayElement) {
      auto *AType =
          SemaRef.Context.getAsArrayType(Entity.getParent()->getType());
      assert(AType && "expected array type when initializing array");
      ElsCount = Embed->getDataElementCount();
      if (const auto *CAType = dyn_cast<ConstantArrayType>(AType))
        ElsCount = std::min(CAType->getSize().getZExtValue(),
                            ElsCount - CurEmbedIndex);
      if (ElsCount == Embed->getDataElementCount()) {
        CurEmbed = nullptr;
        CurEmbedIndex = 0;
        return Embed;
      }
    }

    Result = new (SemaRef.Context)
        EmbedExpr(SemaRef.Context, Embed->getLocation(), Embed->getData(),
                  CurEmbedIndex, ElsCount);
    CurEmbedIndex += ElsCount;
    if (CurEmbedIndex >= Embed->getDataElementCount()) {
      CurEmbed = nullptr;
      CurEmbedIndex = 0;
    }
    return Result;
  }

public:
  InitListChecker(
      Sema &S, const InitializedEntity &Entity, InitListExpr *IL, QualType &T,
      bool VerifyOnly, bool TreatUnavailableAsInvalid,
      bool InOverloadResolution = false,
      SmallVectorImpl<QualType> *AggrDeductionCandidateParamTypes = nullptr);
  InitListChecker(Sema &S, const InitializedEntity &Entity, InitListExpr *IL,
                  QualType &T,
                  SmallVectorImpl<QualType> &AggrDeductionCandidateParamTypes)
      : InitListChecker(S, Entity, IL, T, /*VerifyOnly=*/true,
                        /*TreatUnavailableAsInvalid=*/false,
                        /*InOverloadResolution=*/false,
                        &AggrDeductionCandidateParamTypes) {}

  bool HadError() { return hadError; }

  // Retrieves the fully-structured initializer list used for
  // semantic analysis and code generation.
  InitListExpr *getFullyStructuredList() const { return FullyStructuredList; }
};

} // end anonymous namespace

ExprResult InitListChecker::PerformEmptyInit(SourceLocation Loc,
                                             const InitializedEntity &Entity) {
  InitializationKind Kind = InitializationKind::CreateValue(Loc, Loc, Loc,
                                                            true);
  MultiExprArg SubInit;
  Expr *InitExpr;
  InitListExpr DummyInitList(SemaRef.Context, Loc, std::nullopt, Loc);

  // C++ [dcl.init.aggr]p7:
  //   If there are fewer initializer-clauses in the list than there are
  //   members in the aggregate, then each member not explicitly initialized
  //   ...
  bool EmptyInitList = SemaRef.getLangOpts().CPlusPlus11 &&
      Entity.getType()->getBaseElementTypeUnsafe()->isRecordType();
  if (EmptyInitList) {
    // C++1y / DR1070:
    //   shall be initialized [...] from an empty initializer list.
    //
    // We apply the resolution of this DR to C++11 but not C++98, since C++98
    // does not have useful semantics for initialization from an init list.
    // We treat this as copy-initialization, because aggregate initialization
    // always performs copy-initialization on its elements.
    //
    // Only do this if we're initializing a class type, to avoid filling in
    // the initializer list where possible.
    InitExpr = VerifyOnly
                   ? &DummyInitList
                   : new (SemaRef.Context)
                         InitListExpr(SemaRef.Context, Loc, std::nullopt, Loc);
    InitExpr->setType(SemaRef.Context.VoidTy);
    SubInit = InitExpr;
    Kind = InitializationKind::CreateCopy(Loc, Loc);
  } else {
    // C++03:
    //   shall be value-initialized.
  }

  InitializationSequence InitSeq(SemaRef, Entity, Kind, SubInit);
  // libstdc++4.6 marks the vector default constructor as explicit in
  // _GLIBCXX_DEBUG mode, so recover using the C++03 logic in that case.
  // stlport does so too. Look for std::__debug for libstdc++, and for
  // std:: for stlport.  This is effectively a compiler-side implementation of
  // LWG2193.
  if (!InitSeq && EmptyInitList && InitSeq.getFailureKind() ==
          InitializationSequence::FK_ExplicitConstructor) {
    OverloadCandidateSet::iterator Best;
    OverloadingResult O =
        InitSeq.getFailedCandidateSet()
            .BestViableFunction(SemaRef, Kind.getLocation(), Best);
    (void)O;
    assert(O == OR_Success && "Inconsistent overload resolution");
    CXXConstructorDecl *CtorDecl = cast<CXXConstructorDecl>(Best->Function);
    CXXRecordDecl *R = CtorDecl->getParent();

    if (CtorDecl->getMinRequiredArguments() == 0 &&
        CtorDecl->isExplicit() && R->getDeclName() &&
        SemaRef.SourceMgr.isInSystemHeader(CtorDecl->getLocation())) {
      bool IsInStd = false;
      for (NamespaceDecl *ND = dyn_cast<NamespaceDecl>(R->getDeclContext());
           ND && !IsInStd; ND = dyn_cast<NamespaceDecl>(ND->getParent())) {
        if (SemaRef.getStdNamespace()->InEnclosingNamespaceSetOf(ND))
          IsInStd = true;
      }

      if (IsInStd && llvm::StringSwitch<bool>(R->getName())
              .Cases("basic_string", "deque", "forward_list", true)
              .Cases("list", "map", "multimap", "multiset", true)
              .Cases("priority_queue", "queue", "set", "stack", true)
              .Cases("unordered_map", "unordered_set", "vector", true)
              .Default(false)) {
        InitSeq.InitializeFrom(
            SemaRef, Entity,
            InitializationKind::CreateValue(Loc, Loc, Loc, true),
            MultiExprArg(), /*TopLevelOfInitList=*/false,
            TreatUnavailableAsInvalid);
        // Emit a warning for this.  System header warnings aren't shown
        // by default, but people working on system headers should see it.
        if (!VerifyOnly) {
          SemaRef.Diag(CtorDecl->getLocation(),
                       diag::warn_invalid_initializer_from_system_header);
          if (Entity.getKind() == InitializedEntity::EK_Member)
            SemaRef.Diag(Entity.getDecl()->getLocation(),
                         diag::note_used_in_initialization_here);
          else if (Entity.getKind() == InitializedEntity::EK_ArrayElement)
            SemaRef.Diag(Loc, diag::note_used_in_initialization_here);
        }
      }
    }
  }
  if (!InitSeq) {
    if (!VerifyOnly) {
      InitSeq.Diagnose(SemaRef, Entity, Kind, SubInit);
      if (Entity.getKind() == InitializedEntity::EK_Member)
        SemaRef.Diag(Entity.getDecl()->getLocation(),
                     diag::note_in_omitted_aggregate_initializer)
          << /*field*/1 << Entity.getDecl();
      else if (Entity.getKind() == InitializedEntity::EK_ArrayElement) {
        bool IsTrailingArrayNewMember =
            Entity.getParent() &&
            Entity.getParent()->isVariableLengthArrayNew();
        SemaRef.Diag(Loc, diag::note_in_omitted_aggregate_initializer)
          << (IsTrailingArrayNewMember ? 2 : /*array element*/0)
          << Entity.getElementIndex();
      }
    }
    hadError = true;
    return ExprError();
  }

  return VerifyOnly ? ExprResult()
                    : InitSeq.Perform(SemaRef, Entity, Kind, SubInit);
}

void InitListChecker::CheckEmptyInitializable(const InitializedEntity &Entity,
                                              SourceLocation Loc) {
  // If we're building a fully-structured list, we'll check this at the end
  // once we know which elements are actually initialized. Otherwise, we know
  // that there are no designators so we can just check now.
  if (FullyStructuredList)
    return;
  PerformEmptyInit(Loc, Entity);
}

void InitListChecker::FillInEmptyInitForBase(
    unsigned Init, const CXXBaseSpecifier &Base,
    const InitializedEntity &ParentEntity, InitListExpr *ILE,
    bool &RequiresSecondPass, bool FillWithNoInit) {
  InitializedEntity BaseEntity = InitializedEntity::InitializeBase(
      SemaRef.Context, &Base, false, &ParentEntity);

  if (Init >= ILE->getNumInits() || !ILE->getInit(Init)) {
    ExprResult BaseInit = FillWithNoInit
                              ? new (SemaRef.Context) NoInitExpr(Base.getType())
                              : PerformEmptyInit(ILE->getEndLoc(), BaseEntity);
    if (BaseInit.isInvalid()) {
      hadError = true;
      return;
    }

    if (!VerifyOnly) {
      assert(Init < ILE->getNumInits() && "should have been expanded");
      ILE->setInit(Init, BaseInit.getAs<Expr>());
    }
  } else if (InitListExpr *InnerILE =
                 dyn_cast<InitListExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(BaseEntity, InnerILE, RequiresSecondPass,
                               ILE, Init, FillWithNoInit);
  } else if (DesignatedInitUpdateExpr *InnerDIUE =
               dyn_cast<DesignatedInitUpdateExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(BaseEntity, InnerDIUE->getUpdater(),
                               RequiresSecondPass, ILE, Init,
                               /*FillWithNoInit =*/true);
  }
}

void InitListChecker::FillInEmptyInitForField(unsigned Init, FieldDecl *Field,
                                        const InitializedEntity &ParentEntity,
                                              InitListExpr *ILE,
                                              bool &RequiresSecondPass,
                                              bool FillWithNoInit) {
  SourceLocation Loc = ILE->getEndLoc();
  unsigned NumInits = ILE->getNumInits();
  InitializedEntity MemberEntity
    = InitializedEntity::InitializeMember(Field, &ParentEntity);

  if (Init >= NumInits || !ILE->getInit(Init)) {
    if (const RecordType *RType = ILE->getType()->getAs<RecordType>())
      if (!RType->getDecl()->isUnion())
        assert((Init < NumInits || VerifyOnly) &&
               "This ILE should have been expanded");

    if (FillWithNoInit) {
      assert(!VerifyOnly && "should not fill with no-init in verify-only mode");
      Expr *Filler = new (SemaRef.Context) NoInitExpr(Field->getType());
      if (Init < NumInits)
        ILE->setInit(Init, Filler);
      else
        ILE->updateInit(SemaRef.Context, Init, Filler);
      return;
    }
    // C++1y [dcl.init.aggr]p7:
    //   If there are fewer initializer-clauses in the list than there are
    //   members in the aggregate, then each member not explicitly initialized
    //   shall be initialized from its brace-or-equal-initializer [...]
    if (Field->hasInClassInitializer()) {
      if (VerifyOnly)
        return;

      ExprResult DIE = SemaRef.BuildCXXDefaultInitExpr(Loc, Field);
      if (DIE.isInvalid()) {
        hadError = true;
        return;
      }
      SemaRef.checkInitializerLifetime(MemberEntity, DIE.get());
      if (Init < NumInits)
        ILE->setInit(Init, DIE.get());
      else {
        ILE->updateInit(SemaRef.Context, Init, DIE.get());
        RequiresSecondPass = true;
      }
      return;
    }

    if (Field->getType()->isReferenceType()) {
      if (!VerifyOnly) {
        // C++ [dcl.init.aggr]p9:
        //   If an incomplete or empty initializer-list leaves a
        //   member of reference type uninitialized, the program is
        //   ill-formed.
        SemaRef.Diag(Loc, diag::err_init_reference_member_uninitialized)
            << Field->getType()
            << (ILE->isSyntacticForm() ? ILE : ILE->getSyntacticForm())
                   ->getSourceRange();
        SemaRef.Diag(Field->getLocation(), diag::note_uninit_reference_member);
      }
      hadError = true;
      return;
    }

    ExprResult MemberInit = PerformEmptyInit(Loc, MemberEntity);
    if (MemberInit.isInvalid()) {
      hadError = true;
      return;
    }

    if (hadError || VerifyOnly) {
      // Do nothing
    } else if (Init < NumInits) {
      ILE->setInit(Init, MemberInit.getAs<Expr>());
    } else if (!isa<ImplicitValueInitExpr>(MemberInit.get())) {
      // Empty initialization requires a constructor call, so
      // extend the initializer list to include the constructor
      // call and make a note that we'll need to take another pass
      // through the initializer list.
      ILE->updateInit(SemaRef.Context, Init, MemberInit.getAs<Expr>());
      RequiresSecondPass = true;
    }
  } else if (InitListExpr *InnerILE
               = dyn_cast<InitListExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(MemberEntity, InnerILE,
                               RequiresSecondPass, ILE, Init, FillWithNoInit);
  } else if (DesignatedInitUpdateExpr *InnerDIUE =
                 dyn_cast<DesignatedInitUpdateExpr>(ILE->getInit(Init))) {
    FillInEmptyInitializations(MemberEntity, InnerDIUE->getUpdater(),
                               RequiresSecondPass, ILE, Init,
                               /*FillWithNoInit =*/true);
  }
}

/// Recursively replaces NULL values within the given initializer list
/// with expressions that perform value-initialization of the
/// appropriate type, and finish off the InitListExpr formation.
void
InitListChecker::FillInEmptyInitializations(const InitializedEntity &Entity,
                                            InitListExpr *ILE,
                                            bool &RequiresSecondPass,
                                            InitListExpr *OuterILE,
                                            unsigned OuterIndex,
                                            bool FillWithNoInit) {
  assert((ILE->getType() != SemaRef.Context.VoidTy) &&
         "Should not have void type");

  // We don't need to do any checks when just filling NoInitExprs; that can't
  // fail.
  if (FillWithNoInit && VerifyOnly)
    return;

  // If this is a nested initializer list, we might have changed its contents
  // (and therefore some of its properties, such as instantiation-dependence)
  // while filling it in. Inform the outer initializer list so that its state
  // can be updated to match.
  // FIXME: We should fully build the inner initializers before constructing
  // the outer InitListExpr instead of mutating AST nodes after they have
  // been used as subexpressions of other nodes.
  struct UpdateOuterILEWithUpdatedInit {
    InitListExpr *Outer;
    unsigned OuterIndex;
    ~UpdateOuterILEWithUpdatedInit() {
      if (Outer)
        Outer->setInit(OuterIndex, Outer->getInit(OuterIndex));
    }
  } UpdateOuterRAII = {OuterILE, OuterIndex};

  // A transparent ILE is not performing aggregate initialization and should
  // not be filled in.
  if (ILE->isTransparent())
    return;

  if (const RecordType *RType = ILE->getType()->getAs<RecordType>()) {
    const RecordDecl *RDecl = RType->getDecl();
    if (RDecl->isUnion() && ILE->getInitializedFieldInUnion()) {
      FillInEmptyInitForField(0, ILE->getInitializedFieldInUnion(), Entity, ILE,
                              RequiresSecondPass, FillWithNoInit);
    } else {
      assert((!RDecl->isUnion() || !isa<CXXRecordDecl>(RDecl) ||
              !cast<CXXRecordDecl>(RDecl)->hasInClassInitializer()) &&
             "We should have computed initialized fields already");
      // The fields beyond ILE->getNumInits() are default initialized, so in
      // order to leave them uninitialized, the ILE is expanded and the extra
      // fields are then filled with NoInitExpr.
      unsigned NumElems = numStructUnionElements(ILE->getType());
      if (!RDecl->isUnion() && RDecl->hasFlexibleArrayMember())
        ++NumElems;
      if (!VerifyOnly && ILE->getNumInits() < NumElems)
        ILE->resizeInits(SemaRef.Context, NumElems);

      unsigned Init = 0;

      if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RDecl)) {
        for (auto &Base : CXXRD->bases()) {
          if (hadError)
            return;

          FillInEmptyInitForBase(Init, Base, Entity, ILE, RequiresSecondPass,
                                 FillWithNoInit);
          ++Init;
        }
      }

      for (auto *Field : RDecl->fields()) {
        if (Field->isUnnamedBitField())
          continue;

        if (hadError)
          return;

        FillInEmptyInitForField(Init, Field, Entity, ILE, RequiresSecondPass,
                                FillWithNoInit);
        if (hadError)
          return;

        ++Init;

        // Only look at the first initialization of a union.
        if (RDecl->isUnion())
          break;
      }
    }

    return;
  }

  QualType ElementType;

  InitializedEntity ElementEntity = Entity;
  unsigned NumInits = ILE->getNumInits();
  uint64_t NumElements = NumInits;
  if (const ArrayType *AType = SemaRef.Context.getAsArrayType(ILE->getType())) {
    ElementType = AType->getElementType();
    if (const auto *CAType = dyn_cast<ConstantArrayType>(AType))
      NumElements = CAType->getZExtSize();
    // For an array new with an unknown bound, ask for one additional element
    // in order to populate the array filler.
    if (Entity.isVariableLengthArrayNew())
      ++NumElements;
    ElementEntity = InitializedEntity::InitializeElement(SemaRef.Context,
                                                         0, Entity);
  } else if (const VectorType *VType = ILE->getType()->getAs<VectorType>()) {
    ElementType = VType->getElementType();
    NumElements = VType->getNumElements();
    ElementEntity = InitializedEntity::InitializeElement(SemaRef.Context,
                                                         0, Entity);
  } else
    ElementType = ILE->getType();

  bool SkipEmptyInitChecks = false;
  for (uint64_t Init = 0; Init != NumElements; ++Init) {
    if (hadError)
      return;

    if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement ||
        ElementEntity.getKind() == InitializedEntity::EK_VectorElement)
      ElementEntity.setElementIndex(Init);

    if (Init >= NumInits && (ILE->hasArrayFiller() || SkipEmptyInitChecks))
      return;

    Expr *InitExpr = (Init < NumInits ? ILE->getInit(Init) : nullptr);
    if (!InitExpr && Init < NumInits && ILE->hasArrayFiller())
      ILE->setInit(Init, ILE->getArrayFiller());
    else if (!InitExpr && !ILE->hasArrayFiller()) {
      // In VerifyOnly mode, there's no point performing empty initialization
      // more than once.
      if (SkipEmptyInitChecks)
        continue;

      Expr *Filler = nullptr;

      if (FillWithNoInit)
        Filler = new (SemaRef.Context) NoInitExpr(ElementType);
      else {
        ExprResult ElementInit =
            PerformEmptyInit(ILE->getEndLoc(), ElementEntity);
        if (ElementInit.isInvalid()) {
          hadError = true;
          return;
        }

        Filler = ElementInit.getAs<Expr>();
      }

      if (hadError) {
        // Do nothing
      } else if (VerifyOnly) {
        SkipEmptyInitChecks = true;
      } else if (Init < NumInits) {
        // For arrays, just set the expression used for value-initialization
        // of the "holes" in the array.
        if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement)
          ILE->setArrayFiller(Filler);
        else
          ILE->setInit(Init, Filler);
      } else {
        // For arrays, just set the expression used for value-initialization
        // of the rest of elements and exit.
        if (ElementEntity.getKind() == InitializedEntity::EK_ArrayElement) {
          ILE->setArrayFiller(Filler);
          return;
        }

        if (!isa<ImplicitValueInitExpr>(Filler) && !isa<NoInitExpr>(Filler)) {
          // Empty initialization requires a constructor call, so
          // extend the initializer list to include the constructor
          // call and make a note that we'll need to take another pass
          // through the initializer list.
          ILE->updateInit(SemaRef.Context, Init, Filler);
          RequiresSecondPass = true;
        }
      }
    } else if (InitListExpr *InnerILE
                 = dyn_cast_or_null<InitListExpr>(InitExpr)) {
      FillInEmptyInitializations(ElementEntity, InnerILE, RequiresSecondPass,
                                 ILE, Init, FillWithNoInit);
    } else if (DesignatedInitUpdateExpr *InnerDIUE =
                   dyn_cast_or_null<DesignatedInitUpdateExpr>(InitExpr)) {
      FillInEmptyInitializations(ElementEntity, InnerDIUE->getUpdater(),
                                 RequiresSecondPass, ILE, Init,
                                 /*FillWithNoInit =*/true);
    }
  }
}

static bool hasAnyDesignatedInits(const InitListExpr *IL) {
  for (const Stmt *Init : *IL)
    if (isa_and_nonnull<DesignatedInitExpr>(Init))
      return true;
  return false;
}

InitListChecker::InitListChecker(
    Sema &S, const InitializedEntity &Entity, InitListExpr *IL, QualType &T,
    bool VerifyOnly, bool TreatUnavailableAsInvalid, bool InOverloadResolution,
    SmallVectorImpl<QualType> *AggrDeductionCandidateParamTypes)
    : SemaRef(S), VerifyOnly(VerifyOnly),
      TreatUnavailableAsInvalid(TreatUnavailableAsInvalid),
      InOverloadResolution(InOverloadResolution),
      AggrDeductionCandidateParamTypes(AggrDeductionCandidateParamTypes) {
  if (!VerifyOnly || hasAnyDesignatedInits(IL)) {
    FullyStructuredList =
        createInitListExpr(T, IL->getSourceRange(), IL->getNumInits());

    // FIXME: Check that IL isn't already the semantic form of some other
    // InitListExpr. If it is, we'd create a broken AST.
    if (!VerifyOnly)
      FullyStructuredList->setSyntacticForm(IL);
  }

  CheckExplicitInitList(Entity, IL, T, FullyStructuredList,
                        /*TopLevelObject=*/true);

  if (!hadError && !AggrDeductionCandidateParamTypes && FullyStructuredList) {
    bool RequiresSecondPass = false;
    FillInEmptyInitializations(Entity, FullyStructuredList, RequiresSecondPass,
                               /*OuterILE=*/nullptr, /*OuterIndex=*/0);
    if (RequiresSecondPass && !hadError)
      FillInEmptyInitializations(Entity, FullyStructuredList,
                                 RequiresSecondPass, nullptr, 0);
  }
  if (hadError && FullyStructuredList)
    FullyStructuredList->markError();
}

int InitListChecker::numArrayElements(QualType DeclType) {
  // FIXME: use a proper constant
  int maxElements = 0x7FFFFFFF;
  if (const ConstantArrayType *CAT =
        SemaRef.Context.getAsConstantArrayType(DeclType)) {
    maxElements = static_cast<int>(CAT->getZExtSize());
  }
  return maxElements;
}

int InitListChecker::numStructUnionElements(QualType DeclType) {
  RecordDecl *structDecl = DeclType->castAs<RecordType>()->getDecl();
  int InitializableMembers = 0;
  if (auto *CXXRD = dyn_cast<CXXRecordDecl>(structDecl))
    InitializableMembers += CXXRD->getNumBases();
  for (const auto *Field : structDecl->fields())
    if (!Field->isUnnamedBitField())
      ++InitializableMembers;

  if (structDecl->isUnion())
    return std::min(InitializableMembers, 1);
  return InitializableMembers - structDecl->hasFlexibleArrayMember();
}

RecordDecl *InitListChecker::getRecordDecl(QualType DeclType) {
  if (const auto *RT = DeclType->getAs<RecordType>())
    return RT->getDecl();
  if (const auto *Inject = DeclType->getAs<InjectedClassNameType>())
    return Inject->getDecl();
  return nullptr;
}

/// Determine whether Entity is an entity for which it is idiomatic to elide
/// the braces in aggregate initialization.
static bool isIdiomaticBraceElisionEntity(const InitializedEntity &Entity) {
  // Recursive initialization of the one and only field within an aggregate
  // class is considered idiomatic. This case arises in particular for
  // initialization of std::array, where the C++ standard suggests the idiom of
  //
  //   std::array<T, N> arr = {1, 2, 3};
  //
  // (where std::array is an aggregate struct containing a single array field.

  if (!Entity.getParent())
    return false;

  // Allows elide brace initialization for aggregates with empty base.
  if (Entity.getKind() == InitializedEntity::EK_Base) {
    auto *ParentRD =
        Entity.getParent()->getType()->castAs<RecordType>()->getDecl();
    CXXRecordDecl *CXXRD = cast<CXXRecordDecl>(ParentRD);
    return CXXRD->getNumBases() == 1 && CXXRD->field_empty();
  }

  // Allow brace elision if the only subobject is a field.
  if (Entity.getKind() == InitializedEntity::EK_Member) {
    auto *ParentRD =
        Entity.getParent()->getType()->castAs<RecordType>()->getDecl();
    if (CXXRecordDecl *CXXRD = dyn_cast<CXXRecordDecl>(ParentRD)) {
      if (CXXRD->getNumBases()) {
        return false;
      }
    }
    auto FieldIt = ParentRD->field_begin();
    assert(FieldIt != ParentRD->field_end() &&
           "no fields but have initializer for member?");
    return ++FieldIt == ParentRD->field_end();
  }

  return false;
}

/// Check whether the range of the initializer \p ParentIList from element
/// \p Index onwards can be used to initialize an object of type \p T. Update
/// \p Index to indicate how many elements of the list were consumed.
///
/// This also fills in \p StructuredList, from element \p StructuredIndex
/// onwards, with the fully-braced, desugared form of the initialization.
void InitListChecker::CheckImplicitInitList(const InitializedEntity &Entity,
                                            InitListExpr *ParentIList,
                                            QualType T, unsigned &Index,
                                            InitListExpr *StructuredList,
                                            unsigned &StructuredIndex) {
  int maxElements = 0;

  if (T->isArrayType())
    maxElements = numArrayElements(T);
  else if (T->isRecordType())
    maxElements = numStructUnionElements(T);
  else if (T->isVectorType())
    maxElements = T->castAs<VectorType>()->getNumElements();
  else
    llvm_unreachable("CheckImplicitInitList(): Illegal type");

  if (maxElements == 0) {
    if (!VerifyOnly)
      SemaRef.Diag(ParentIList->getInit(Index)->getBeginLoc(),
                   diag::err_implicit_empty_initializer);
    ++Index;
    hadError = true;
    return;
  }

  // Build a structured initializer list corresponding to this subobject.
  InitListExpr *StructuredSubobjectInitList = getStructuredSubobjectInit(
      ParentIList, Index, T, StructuredList, StructuredIndex,
      SourceRange(ParentIList->getInit(Index)->getBeginLoc(),
                  ParentIList->getSourceRange().getEnd()));
  unsigned StructuredSubobjectInitIndex = 0;

  // Check the element types and build the structural subobject.
  unsigned StartIndex = Index;
  CheckListElementTypes(Entity, ParentIList, T,
                        /*SubobjectIsDesignatorContext=*/false, Index,
                        StructuredSubobjectInitList,
                        StructuredSubobjectInitIndex);

  if (StructuredSubobjectInitList) {
    StructuredSubobjectInitList->setType(T);

    unsigned EndIndex = (Index == StartIndex? StartIndex : Index - 1);
    // Update the structured sub-object initializer so that it's ending
    // range corresponds with the end of the last initializer it used.
    if (EndIndex < ParentIList->getNumInits() &&
        ParentIList->getInit(EndIndex)) {
      SourceLocation EndLoc
        = ParentIList->getInit(EndIndex)->getSourceRange().getEnd();
      StructuredSubobjectInitList->setRBraceLoc(EndLoc);
    }

    // Complain about missing braces.
    if (!VerifyOnly && (T->isArrayType() || T->isRecordType()) &&
        !ParentIList->isIdiomaticZeroInitializer(SemaRef.getLangOpts()) &&
        !isIdiomaticBraceElisionEntity(Entity)) {
      SemaRef.Diag(StructuredSubobjectInitList->getBeginLoc(),
                   diag::warn_missing_braces)
          << StructuredSubobjectInitList->getSourceRange()
          << FixItHint::CreateInsertion(
                 StructuredSubobjectInitList->getBeginLoc(), "{")
          << FixItHint::CreateInsertion(
                 SemaRef.getLocForEndOfToken(
                     StructuredSubobjectInitList->getEndLoc()),
                 "}");
    }

    // Warn if this type won't be an aggregate in future versions of C++.
    auto *CXXRD = T->getAsCXXRecordDecl();
    if (!VerifyOnly && CXXRD && CXXRD->hasUserDeclaredConstructor()) {
      SemaRef.Diag(StructuredSubobjectInitList->getBeginLoc(),
                   diag::warn_cxx20_compat_aggregate_init_with_ctors)
          << StructuredSubobjectInitList->getSourceRange() << T;
    }
  }
}

/// Warn that \p Entity was of scalar type and was initialized by a
/// single-element braced initializer list.
static void warnBracedScalarInit(Sema &S, const InitializedEntity &Entity,
                                 SourceRange Braces) {
  // Don't warn during template instantiation. If the initialization was
  // non-dependent, we warned during the initial parse; otherwise, the
  // type might not be scalar in some uses of the template.
  if (S.inTemplateInstantiation())
    return;

  unsigned DiagID = 0;

  switch (Entity.getKind()) {
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Parameter_CF_Audited:
  case InitializedEntity::EK_TemplateParameter:
  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_ParenAggInitMember:
    // Extra braces here are suspicious.
    DiagID = diag::warn_braces_around_init;
    break;

  case InitializedEntity::EK_Member:
    // Warn on aggregate initialization but not on ctor init list or
    // default member initializer.
    if (Entity.getParent())
      DiagID = diag::warn_braces_around_init;
    break;

  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_LambdaCapture:
    // No warning, might be direct-list-initialization.
    // FIXME: Should we warn for copy-list-initialization in these cases?
    break;

  case InitializedEntity::EK_New:
  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_CompoundLiteralInit:
    // No warning, braces are part of the syntax of the underlying construct.
    break;

  case InitializedEntity::EK_RelatedResult:
    // No warning, we already warned when initializing the result.
    break;

  case InitializedEntity::EK_Exception:
  case InitializedEntity::EK_Base:
  case InitializedEntity::EK_Delegating:
  case InitializedEntity::EK_BlockElement:
  case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
  case InitializedEntity::EK_Binding:
  case InitializedEntity::EK_StmtExprResult:
    llvm_unreachable("unexpected braced scalar init");
  }

  if (DiagID) {
    S.Diag(Braces.getBegin(), DiagID)
        << Entity.getType()->isSizelessBuiltinType() << Braces
        << FixItHint::CreateRemoval(Braces.getBegin())
        << FixItHint::CreateRemoval(Braces.getEnd());
  }
}

/// Check whether the initializer \p IList (that was written with explicit
/// braces) can be used to initialize an object of type \p T.
///
/// This also fills in \p StructuredList with the fully-braced, desugared
/// form of the initialization.
void InitListChecker::CheckExplicitInitList(const InitializedEntity &Entity,
                                            InitListExpr *IList, QualType &T,
                                            InitListExpr *StructuredList,
                                            bool TopLevelObject) {
  unsigned Index = 0, StructuredIndex = 0;
  CheckListElementTypes(Entity, IList, T, /*SubobjectIsDesignatorContext=*/true,
                        Index, StructuredList, StructuredIndex, TopLevelObject);
  if (StructuredList) {
    QualType ExprTy = T;
    if (!ExprTy->isArrayType())
      ExprTy = ExprTy.getNonLValueExprType(SemaRef.Context);
    if (!VerifyOnly)
      IList->setType(ExprTy);
    StructuredList->setType(ExprTy);
  }
  if (hadError)
    return;

  // Don't complain for incomplete types, since we'll get an error elsewhere.
  if (Index < IList->getNumInits() && !T->isIncompleteType()) {
    // We have leftover initializers
    bool ExtraInitsIsError = SemaRef.getLangOpts().CPlusPlus ||
          (SemaRef.getLangOpts().OpenCL && T->isVectorType());
    hadError = ExtraInitsIsError;
    if (VerifyOnly) {
      return;
    } else if (StructuredIndex == 1 &&
               IsStringInit(StructuredList->getInit(0), T, SemaRef.Context) ==
                   SIF_None) {
      unsigned DK =
          ExtraInitsIsError
              ? diag::err_excess_initializers_in_char_array_initializer
              : diag::ext_excess_initializers_in_char_array_initializer;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << IList->getInit(Index)->getSourceRange();
    } else if (T->isSizelessBuiltinType()) {
      unsigned DK = ExtraInitsIsError
                        ? diag::err_excess_initializers_for_sizeless_type
                        : diag::ext_excess_initializers_for_sizeless_type;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << T << IList->getInit(Index)->getSourceRange();
    } else {
      int initKind = T->isArrayType() ? 0 :
                     T->isVectorType() ? 1 :
                     T->isScalarType() ? 2 :
                     T->isUnionType() ? 3 :
                     4;

      unsigned DK = ExtraInitsIsError ? diag::err_excess_initializers
                                      : diag::ext_excess_initializers;
      SemaRef.Diag(IList->getInit(Index)->getBeginLoc(), DK)
          << initKind << IList->getInit(Index)->getSourceRange();
    }
  }

  if (!VerifyOnly) {
    if (T->isScalarType() && IList->getNumInits() == 1 &&
        !isa<InitListExpr>(IList->getInit(0)))
      warnBracedScalarInit(SemaRef, Entity, IList->getSourceRange());

    // Warn if this is a class type that won't be an aggregate in future
    // versions of C++.
    auto *CXXRD = T->getAsCXXRecordDecl();
    if (CXXRD && CXXRD->hasUserDeclaredConstructor()) {
      // Don't warn if there's an equivalent default constructor that would be
      // used instead.
      bool HasEquivCtor = false;
      if (IList->getNumInits() == 0) {
        auto *CD = SemaRef.LookupDefaultConstructor(CXXRD);
        HasEquivCtor = CD && !CD->isDeleted();
      }

      if (!HasEquivCtor) {
        SemaRef.Diag(IList->getBeginLoc(),
                     diag::warn_cxx20_compat_aggregate_init_with_ctors)
            << IList->getSourceRange() << T;
      }
    }
  }
}

void InitListChecker::CheckListElementTypes(const InitializedEntity &Entity,
                                            InitListExpr *IList,
                                            QualType &DeclType,
                                            bool SubobjectIsDesignatorContext,
                                            unsigned &Index,
                                            InitListExpr *StructuredList,
                                            unsigned &StructuredIndex,
                                            bool TopLevelObject) {
  if (DeclType->isAnyComplexType() && SubobjectIsDesignatorContext) {
    // Explicitly braced initializer for complex type can be real+imaginary
    // parts.
    CheckComplexType(Entity, IList, DeclType, Index,
                     StructuredList, StructuredIndex);
  } else if (DeclType->isScalarType()) {
    CheckScalarType(Entity, IList, DeclType, Index,
                    StructuredList, StructuredIndex);
  } else if (DeclType->isVectorType()) {
    CheckVectorType(Entity, IList, DeclType, Index,
                    StructuredList, StructuredIndex);
  } else if (const RecordDecl *RD = getRecordDecl(DeclType)) {
    auto Bases =
        CXXRecordDecl::base_class_const_range(CXXRecordDecl::base_class_const_iterator(),
                                        CXXRecordDecl::base_class_const_iterator());
    if (DeclType->isRecordType()) {
      assert(DeclType->isAggregateType() &&
             "non-aggregate records should be handed in CheckSubElementType");
      if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
        Bases = CXXRD->bases();
    } else {
      Bases = cast<CXXRecordDecl>(RD)->bases();
    }
    CheckStructUnionTypes(Entity, IList, DeclType, Bases, RD->field_begin(),
                          SubobjectIsDesignatorContext, Index, StructuredList,
                          StructuredIndex, TopLevelObject);
  } else if (DeclType->isArrayType()) {
    llvm::APSInt Zero(
                    SemaRef.Context.getTypeSize(SemaRef.Context.getSizeType()),
                    false);
    CheckArrayType(Entity, IList, DeclType, Zero,
                   SubobjectIsDesignatorContext, Index,
                   StructuredList, StructuredIndex);
  } else if (DeclType->isVoidType() || DeclType->isFunctionType()) {
    // This type is invalid, issue a diagnostic.
    ++Index;
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_illegal_initializer_type)
          << DeclType;
    hadError = true;
  } else if (DeclType->isReferenceType()) {
    CheckReferenceType(Entity, IList, DeclType, Index,
                       StructuredList, StructuredIndex);
  } else if (DeclType->isObjCObjectType()) {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_init_objc_class) << DeclType;
    hadError = true;
  } else if (DeclType->isOCLIntelSubgroupAVCType() ||
             DeclType->isSizelessBuiltinType()) {
    // Checks for scalar type are sufficient for these types too.
    CheckScalarType(Entity, IList, DeclType, Index, StructuredList,
                    StructuredIndex);
  } else if (DeclType->isDependentType()) {
    // C++ [over.match.class.deduct]p1.5:
    //   brace elision is not considered for any aggregate element that has a
    //   dependent non-array type or an array type with a value-dependent bound
    ++Index;
    assert(AggrDeductionCandidateParamTypes);
    AggrDeductionCandidateParamTypes->push_back(DeclType);
  } else {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_illegal_initializer_type)
          << DeclType;
    hadError = true;
  }
}

void InitListChecker::CheckSubElementType(const InitializedEntity &Entity,
                                          InitListExpr *IList,
                                          QualType ElemType,
                                          unsigned &Index,
                                          InitListExpr *StructuredList,
                                          unsigned &StructuredIndex,
                                          bool DirectlyDesignated) {
  Expr *expr = IList->getInit(Index);

  if (ElemType->isReferenceType())
    return CheckReferenceType(Entity, IList, ElemType, Index,
                              StructuredList, StructuredIndex);

  if (InitListExpr *SubInitList = dyn_cast<InitListExpr>(expr)) {
    if (SubInitList->getNumInits() == 1 &&
        IsStringInit(SubInitList->getInit(0), ElemType, SemaRef.Context) ==
        SIF_None) {
      // FIXME: It would be more faithful and no less correct to include an
      // InitListExpr in the semantic form of the initializer list in this case.
      expr = SubInitList->getInit(0);
    }
    // Nested aggregate initialization and C++ initialization are handled later.
  } else if (isa<ImplicitValueInitExpr>(expr)) {
    // This happens during template instantiation when we see an InitListExpr
    // that we've already checked once.
    assert(SemaRef.Context.hasSameType(expr->getType(), ElemType) &&
           "found implicit initialization for the wrong type");
    UpdateStructuredListElement(StructuredList, StructuredIndex, expr);
    ++Index;
    return;
  }

  if (SemaRef.getLangOpts().CPlusPlus || isa<InitListExpr>(expr)) {
    // C++ [dcl.init.aggr]p2:
    //   Each member is copy-initialized from the corresponding
    //   initializer-clause.

    // FIXME: Better EqualLoc?
    InitializationKind Kind =
        InitializationKind::CreateCopy(expr->getBeginLoc(), SourceLocation());

    // Vector elements can be initialized from other vectors in which case
    // we need initialization entity with a type of a vector (and not a vector
    // element!) initializing multiple vector elements.
    auto TmpEntity =
        (ElemType->isExtVectorType() && !Entity.getType()->isExtVectorType())
            ? InitializedEntity::InitializeTemporary(ElemType)
            : Entity;

    if (TmpEntity.getType()->isDependentType()) {
      // C++ [over.match.class.deduct]p1.5:
      //   brace elision is not considered for any aggregate element that has a
      //   dependent non-array type or an array type with a value-dependent
      //   bound
      assert(AggrDeductionCandidateParamTypes);

      // In the presence of a braced-init-list within the initializer, we should
      // not perform brace-elision, even if brace elision would otherwise be
      // applicable. For example, given:
      //
      // template <class T> struct Foo {
      //   T t[2];
      // };
      //
      // Foo t = {{1, 2}};
      //
      // we don't want the (T, T) but rather (T [2]) in terms of the initializer
      // {{1, 2}}.
      if (isa<InitListExpr, DesignatedInitExpr>(expr) ||
          !isa_and_present<ConstantArrayType>(
              SemaRef.Context.getAsArrayType(ElemType))) {
        ++Index;
        AggrDeductionCandidateParamTypes->push_back(ElemType);
        return;
      }
    } else {
      InitializationSequence Seq(SemaRef, TmpEntity, Kind, expr,
                                 /*TopLevelOfInitList*/ true);
      // C++14 [dcl.init.aggr]p13:
      //   If the assignment-expression can initialize a member, the member is
      //   initialized. Otherwise [...] brace elision is assumed
      //
      // Brace elision is never performed if the element is not an
      // assignment-expression.
      if (Seq || isa<InitListExpr>(expr)) {
        if (auto *Embed = dyn_cast<EmbedExpr>(expr)) {
          expr = HandleEmbed(Embed, Entity);
        }
        if (!VerifyOnly) {
          ExprResult Result = Seq.Perform(SemaRef, TmpEntity, Kind, expr);
          if (Result.isInvalid())
            hadError = true;

          UpdateStructuredListElement(StructuredList, StructuredIndex,
                                      Result.getAs<Expr>());
        } else if (!Seq) {
          hadError = true;
        } else if (StructuredList) {
          UpdateStructuredListElement(StructuredList, StructuredIndex,
                                      getDummyInit());
        }
        if (!CurEmbed)
          ++Index;
        if (AggrDeductionCandidateParamTypes)
          AggrDeductionCandidateParamTypes->push_back(ElemType);
        return;
      }
    }

    // Fall through for subaggregate initialization
  } else if (ElemType->isScalarType() || ElemType->isAtomicType()) {
    // FIXME: Need to handle atomic aggregate types with implicit init lists.
    return CheckScalarType(Entity, IList, ElemType, Index,
                           StructuredList, StructuredIndex);
  } else if (const ArrayType *arrayType =
                 SemaRef.Context.getAsArrayType(ElemType)) {
    // arrayType can be incomplete if we're initializing a flexible
    // array member.  There's nothing we can do with the completed
    // type here, though.

    if (IsStringInit(expr, arrayType, SemaRef.Context) == SIF_None) {
      // FIXME: Should we do this checking in verify-only mode?
      if (!VerifyOnly)
        CheckStringInit(expr, ElemType, arrayType, SemaRef,
                        SemaRef.getLangOpts().C23 &&
                            initializingConstexprVariable(Entity));
      if (StructuredList)
        UpdateStructuredListElement(StructuredList, StructuredIndex, expr);
      ++Index;
      return;
    }

    // Fall through for subaggregate initialization.

  } else {
    assert((ElemType->isRecordType() || ElemType->isVectorType() ||
            ElemType->isOpenCLSpecificType()) && "Unexpected type");

    // C99 6.7.8p13:
    //
    //   The initializer for a structure or union object that has
    //   automatic storage duration shall be either an initializer
    //   list as described below, or a single expression that has
    //   compatible structure or union type. In the latter case, the
    //   initial value of the object, including unnamed members, is
    //   that of the expression.
    ExprResult ExprRes = expr;
    if (SemaRef.CheckSingleAssignmentConstraints(
            ElemType, ExprRes, !VerifyOnly) != Sema::Incompatible) {
      if (ExprRes.isInvalid())
        hadError = true;
      else {
        ExprRes = SemaRef.DefaultFunctionArrayLvalueConversion(ExprRes.get());
        if (ExprRes.isInvalid())
          hadError = true;
      }
      UpdateStructuredListElement(StructuredList, StructuredIndex,
                                  ExprRes.getAs<Expr>());
      ++Index;
      return;
    }
    ExprRes.get();
    // Fall through for subaggregate initialization
  }

  // C++ [dcl.init.aggr]p12:
  //
  //   [...] Otherwise, if the member is itself a non-empty
  //   subaggregate, brace elision is assumed and the initializer is
  //   considered for the initialization of the first member of
  //   the subaggregate.
  // OpenCL vector initializer is handled elsewhere.
  if ((!SemaRef.getLangOpts().OpenCL && ElemType->isVectorType()) ||
      ElemType->isAggregateType()) {
    CheckImplicitInitList(Entity, IList, ElemType, Index, StructuredList,
                          StructuredIndex);
    ++StructuredIndex;

    // In C++20, brace elision is not permitted for a designated initializer.
    if (DirectlyDesignated && SemaRef.getLangOpts().CPlusPlus && !hadError) {
      if (InOverloadResolution)
        hadError = true;
      if (!VerifyOnly) {
        SemaRef.Diag(expr->getBeginLoc(),
                     diag::ext_designated_init_brace_elision)
            << expr->getSourceRange()
            << FixItHint::CreateInsertion(expr->getBeginLoc(), "{")
            << FixItHint::CreateInsertion(
                   SemaRef.getLocForEndOfToken(expr->getEndLoc()), "}");
      }
    }
  } else {
    if (!VerifyOnly) {
      // We cannot initialize this element, so let PerformCopyInitialization
      // produce the appropriate diagnostic. We already checked that this
      // initialization will fail.
      ExprResult Copy =
          SemaRef.PerformCopyInitialization(Entity, SourceLocation(), expr,
                                            /*TopLevelOfInitList=*/true);
      (void)Copy;
      assert(Copy.isInvalid() &&
             "expected non-aggregate initialization to fail");
    }
    hadError = true;
    ++Index;
    ++StructuredIndex;
  }
}

void InitListChecker::CheckComplexType(const InitializedEntity &Entity,
                                       InitListExpr *IList, QualType DeclType,
                                       unsigned &Index,
                                       InitListExpr *StructuredList,
                                       unsigned &StructuredIndex) {
  assert(Index == 0 && "Index in explicit init list must be zero");

  // As an extension, clang supports complex initializers, which initialize
  // a complex number component-wise.  When an explicit initializer list for
  // a complex number contains two initializers, this extension kicks in:
  // it expects the initializer list to contain two elements convertible to
  // the element type of the complex type. The first element initializes
  // the real part, and the second element intitializes the imaginary part.

  if (IList->getNumInits() < 2)
    return CheckScalarType(Entity, IList, DeclType, Index, StructuredList,
                           StructuredIndex);

  // This is an extension in C.  (The builtin _Complex type does not exist
  // in the C++ standard.)
  if (!SemaRef.getLangOpts().CPlusPlus && !VerifyOnly)
    SemaRef.Diag(IList->getBeginLoc(), diag::ext_complex_component_init)
        << IList->getSourceRange();

  // Initialize the complex number.
  QualType elementType = DeclType->castAs<ComplexType>()->getElementType();
  InitializedEntity ElementEntity =
    InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  for (unsigned i = 0; i < 2; ++i) {
    ElementEntity.setElementIndex(Index);
    CheckSubElementType(ElementEntity, IList, elementType, Index,
                        StructuredList, StructuredIndex);
  }
}

void InitListChecker::CheckScalarType(const InitializedEntity &Entity,
                                      InitListExpr *IList, QualType DeclType,
                                      unsigned &Index,
                                      InitListExpr *StructuredList,
                                      unsigned &StructuredIndex) {
  if (Index >= IList->getNumInits()) {
    if (!VerifyOnly) {
      if (SemaRef.getLangOpts().CPlusPlus) {
        if (DeclType->isSizelessBuiltinType())
          SemaRef.Diag(IList->getBeginLoc(),
                       SemaRef.getLangOpts().CPlusPlus11
                           ? diag::warn_cxx98_compat_empty_sizeless_initializer
                           : diag::err_empty_sizeless_initializer)
              << DeclType << IList->getSourceRange();
        else
          SemaRef.Diag(IList->getBeginLoc(),
                       SemaRef.getLangOpts().CPlusPlus11
                           ? diag::warn_cxx98_compat_empty_scalar_initializer
                           : diag::err_empty_scalar_initializer)
              << IList->getSourceRange();
      }
    }
    hadError =
        SemaRef.getLangOpts().CPlusPlus && !SemaRef.getLangOpts().CPlusPlus11;
    ++Index;
    ++StructuredIndex;
    return;
  }

  Expr *expr = IList->getInit(Index);
  if (InitListExpr *SubIList = dyn_cast<InitListExpr>(expr)) {
    // FIXME: This is invalid, and accepting it causes overload resolution
    // to pick the wrong overload in some corner cases.
    if (!VerifyOnly)
      SemaRef.Diag(SubIList->getBeginLoc(), diag::ext_many_braces_around_init)
          << DeclType->isSizelessBuiltinType() << SubIList->getSourceRange();

    CheckScalarType(Entity, SubIList, DeclType, Index, StructuredList,
                    StructuredIndex);
    return;
  } else if (isa<DesignatedInitExpr>(expr)) {
    if (!VerifyOnly)
      SemaRef.Diag(expr->getBeginLoc(),
                   diag::err_designator_for_scalar_or_sizeless_init)
          << DeclType->isSizelessBuiltinType() << DeclType
          << expr->getSourceRange();
    hadError = true;
    ++Index;
    ++StructuredIndex;
    return;
  } else if (auto *Embed = dyn_cast<EmbedExpr>(expr)) {
    expr = HandleEmbed(Embed, Entity);
  }

  ExprResult Result;
  if (VerifyOnly) {
    if (SemaRef.CanPerformCopyInitialization(Entity, expr))
      Result = getDummyInit();
    else
      Result = ExprError();
  } else {
    Result =
        SemaRef.PerformCopyInitialization(Entity, expr->getBeginLoc(), expr,
                                          /*TopLevelOfInitList=*/true);
  }

  Expr *ResultExpr = nullptr;

  if (Result.isInvalid())
    hadError = true; // types weren't compatible.
  else {
    ResultExpr = Result.getAs<Expr>();

    if (ResultExpr != expr && !VerifyOnly && !CurEmbed) {
      // The type was promoted, update initializer list.
      // FIXME: Why are we updating the syntactic init list?
      IList->setInit(Index, ResultExpr);
    }
  }

  UpdateStructuredListElement(StructuredList, StructuredIndex, ResultExpr);
  if (!CurEmbed)
    ++Index;
  if (AggrDeductionCandidateParamTypes)
    AggrDeductionCandidateParamTypes->push_back(DeclType);
}

void InitListChecker::CheckReferenceType(const InitializedEntity &Entity,
                                         InitListExpr *IList, QualType DeclType,
                                         unsigned &Index,
                                         InitListExpr *StructuredList,
                                         unsigned &StructuredIndex) {
  if (Index >= IList->getNumInits()) {
    // FIXME: It would be wonderful if we could point at the actual member. In
    // general, it would be useful to pass location information down the stack,
    // so that we know the location (or decl) of the "current object" being
    // initialized.
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(),
                   diag::err_init_reference_member_uninitialized)
          << DeclType << IList->getSourceRange();
    hadError = true;
    ++Index;
    ++StructuredIndex;
    return;
  }

  Expr *expr = IList->getInit(Index);
  if (isa<InitListExpr>(expr) && !SemaRef.getLangOpts().CPlusPlus11) {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(), diag::err_init_non_aggr_init_list)
          << DeclType << IList->getSourceRange();
    hadError = true;
    ++Index;
    ++StructuredIndex;
    return;
  }

  ExprResult Result;
  if (VerifyOnly) {
    if (SemaRef.CanPerformCopyInitialization(Entity,expr))
      Result = getDummyInit();
    else
      Result = ExprError();
  } else {
    Result =
        SemaRef.PerformCopyInitialization(Entity, expr->getBeginLoc(), expr,
                                          /*TopLevelOfInitList=*/true);
  }

  if (Result.isInvalid())
    hadError = true;

  expr = Result.getAs<Expr>();
  // FIXME: Why are we updating the syntactic init list?
  if (!VerifyOnly && expr)
    IList->setInit(Index, expr);

  UpdateStructuredListElement(StructuredList, StructuredIndex, expr);
  ++Index;
  if (AggrDeductionCandidateParamTypes)
    AggrDeductionCandidateParamTypes->push_back(DeclType);
}

void InitListChecker::CheckVectorType(const InitializedEntity &Entity,
                                      InitListExpr *IList, QualType DeclType,
                                      unsigned &Index,
                                      InitListExpr *StructuredList,
                                      unsigned &StructuredIndex) {
  const VectorType *VT = DeclType->castAs<VectorType>();
  unsigned maxElements = VT->getNumElements();
  unsigned numEltsInit = 0;
  QualType elementType = VT->getElementType();

  if (Index >= IList->getNumInits()) {
    // Make sure the element type can be value-initialized.
    CheckEmptyInitializable(
        InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity),
        IList->getEndLoc());
    return;
  }

  if (!SemaRef.getLangOpts().OpenCL && !SemaRef.getLangOpts().HLSL ) {
    // If the initializing element is a vector, try to copy-initialize
    // instead of breaking it apart (which is doomed to failure anyway).
    Expr *Init = IList->getInit(Index);
    if (!isa<InitListExpr>(Init) && Init->getType()->isVectorType()) {
      ExprResult Result;
      if (VerifyOnly) {
        if (SemaRef.CanPerformCopyInitialization(Entity, Init))
          Result = getDummyInit();
        else
          Result = ExprError();
      } else {
        Result =
            SemaRef.PerformCopyInitialization(Entity, Init->getBeginLoc(), Init,
                                              /*TopLevelOfInitList=*/true);
      }

      Expr *ResultExpr = nullptr;
      if (Result.isInvalid())
        hadError = true; // types weren't compatible.
      else {
        ResultExpr = Result.getAs<Expr>();

        if (ResultExpr != Init && !VerifyOnly) {
          // The type was promoted, update initializer list.
          // FIXME: Why are we updating the syntactic init list?
          IList->setInit(Index, ResultExpr);
        }
      }
      UpdateStructuredListElement(StructuredList, StructuredIndex, ResultExpr);
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(elementType);
      return;
    }

    InitializedEntity ElementEntity =
      InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

    for (unsigned i = 0; i < maxElements; ++i, ++numEltsInit) {
      // Don't attempt to go past the end of the init list
      if (Index >= IList->getNumInits()) {
        CheckEmptyInitializable(ElementEntity, IList->getEndLoc());
        break;
      }

      ElementEntity.setElementIndex(Index);
      CheckSubElementType(ElementEntity, IList, elementType, Index,
                          StructuredList, StructuredIndex);
    }

    if (VerifyOnly)
      return;

    bool isBigEndian = SemaRef.Context.getTargetInfo().isBigEndian();
    const VectorType *T = Entity.getType()->castAs<VectorType>();
    if (isBigEndian && (T->getVectorKind() == VectorKind::Neon ||
                        T->getVectorKind() == VectorKind::NeonPoly)) {
      // The ability to use vector initializer lists is a GNU vector extension
      // and is unrelated to the NEON intrinsics in arm_neon.h. On little
      // endian machines it works fine, however on big endian machines it
      // exhibits surprising behaviour:
      //
      //   uint32x2_t x = {42, 64};
      //   return vget_lane_u32(x, 0); // Will return 64.
      //
      // Because of this, explicitly call out that it is non-portable.
      //
      SemaRef.Diag(IList->getBeginLoc(),
                   diag::warn_neon_vector_initializer_non_portable);

      const char *typeCode;
      unsigned typeSize = SemaRef.Context.getTypeSize(elementType);

      if (elementType->isFloatingType())
        typeCode = "f";
      else if (elementType->isSignedIntegerType())
        typeCode = "s";
      else if (elementType->isUnsignedIntegerType())
        typeCode = "u";
      else
        llvm_unreachable("Invalid element type!");

      SemaRef.Diag(IList->getBeginLoc(),
                   SemaRef.Context.getTypeSize(VT) > 64
                       ? diag::note_neon_vector_initializer_non_portable_q
                       : diag::note_neon_vector_initializer_non_portable)
          << typeCode << typeSize;
    }

    return;
  }

  InitializedEntity ElementEntity =
    InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  // OpenCL and HLSL initializers allow vectors to be constructed from vectors.
  for (unsigned i = 0; i < maxElements; ++i) {
    // Don't attempt to go past the end of the init list
    if (Index >= IList->getNumInits())
      break;

    ElementEntity.setElementIndex(Index);

    QualType IType = IList->getInit(Index)->getType();
    if (!IType->isVectorType()) {
      CheckSubElementType(ElementEntity, IList, elementType, Index,
                          StructuredList, StructuredIndex);
      ++numEltsInit;
    } else {
      QualType VecType;
      const VectorType *IVT = IType->castAs<VectorType>();
      unsigned numIElts = IVT->getNumElements();

      if (IType->isExtVectorType())
        VecType = SemaRef.Context.getExtVectorType(elementType, numIElts);
      else
        VecType = SemaRef.Context.getVectorType(elementType, numIElts,
                                                IVT->getVectorKind());
      CheckSubElementType(ElementEntity, IList, VecType, Index,
                          StructuredList, StructuredIndex);
      numEltsInit += numIElts;
    }
  }

  // OpenCL and HLSL require all elements to be initialized.
  if (numEltsInit != maxElements) {
    if (!VerifyOnly)
      SemaRef.Diag(IList->getBeginLoc(),
                   diag::err_vector_incorrect_num_initializers)
          << (numEltsInit < maxElements) << maxElements << numEltsInit;
    hadError = true;
  }
}

/// Check if the type of a class element has an accessible destructor, and marks
/// it referenced. Returns true if we shouldn't form a reference to the
/// destructor.
///
/// Aggregate initialization requires a class element's destructor be
/// accessible per 11.6.1 [dcl.init.aggr]:
///
/// The destructor for each element of class type is potentially invoked
/// (15.4 [class.dtor]) from the context where the aggregate initialization
/// occurs.
static bool checkDestructorReference(QualType ElementType, SourceLocation Loc,
                                     Sema &SemaRef) {
  auto *CXXRD = ElementType->getAsCXXRecordDecl();
  if (!CXXRD)
    return false;

  CXXDestructorDecl *Destructor = SemaRef.LookupDestructor(CXXRD);
  SemaRef.CheckDestructorAccess(Loc, Destructor,
                                SemaRef.PDiag(diag::err_access_dtor_temp)
                                << ElementType);
  SemaRef.MarkFunctionReferenced(Loc, Destructor);
  return SemaRef.DiagnoseUseOfDecl(Destructor, Loc);
}

static bool
canInitializeArrayWithEmbedDataString(ArrayRef<Expr *> ExprList,
                                      const InitializedEntity &Entity,
                                      ASTContext &Context) {
  QualType InitType = Entity.getType();
  const InitializedEntity *Parent = &Entity;

  while (Parent) {
    InitType = Parent->getType();
    Parent = Parent->getParent();
  }

  // Only one initializer, it's an embed and the types match;
  EmbedExpr *EE =
      ExprList.size() == 1
          ? dyn_cast_if_present<EmbedExpr>(ExprList[0]->IgnoreParens())
          : nullptr;
  if (!EE)
    return false;

  if (InitType->isArrayType()) {
    const ArrayType *InitArrayType = InitType->getAsArrayTypeUnsafe();
    QualType InitElementTy = InitArrayType->getElementType();
    QualType EmbedExprElementTy = EE->getDataStringLiteral()->getType();
    const bool TypesMatch =
        Context.typesAreCompatible(InitElementTy, EmbedExprElementTy) ||
        (InitElementTy->isCharType() && EmbedExprElementTy->isCharType());
    if (TypesMatch)
      return true;
  }
  return false;
}

void InitListChecker::CheckArrayType(const InitializedEntity &Entity,
                                     InitListExpr *IList, QualType &DeclType,
                                     llvm::APSInt elementIndex,
                                     bool SubobjectIsDesignatorContext,
                                     unsigned &Index,
                                     InitListExpr *StructuredList,
                                     unsigned &StructuredIndex) {
  const ArrayType *arrayType = SemaRef.Context.getAsArrayType(DeclType);

  if (!VerifyOnly) {
    if (checkDestructorReference(arrayType->getElementType(),
                                 IList->getEndLoc(), SemaRef)) {
      hadError = true;
      return;
    }
  }

  if (canInitializeArrayWithEmbedDataString(IList->inits(), Entity,
                                            SemaRef.Context)) {
    EmbedExpr *Embed = cast<EmbedExpr>(IList->inits()[0]);
    IList->setInit(0, Embed->getDataStringLiteral());
  }

  // Check for the special-case of initializing an array with a string.
  if (Index < IList->getNumInits()) {
    if (IsStringInit(IList->getInit(Index), arrayType, SemaRef.Context) ==
        SIF_None) {
      // We place the string literal directly into the resulting
      // initializer list. This is the only place where the structure
      // of the structured initializer list doesn't match exactly,
      // because doing so would involve allocating one character
      // constant for each string.
      // FIXME: Should we do these checks in verify-only mode too?
      if (!VerifyOnly)
        CheckStringInit(IList->getInit(Index), DeclType, arrayType, SemaRef,
                        SemaRef.getLangOpts().C23 &&
                            initializingConstexprVariable(Entity));
      if (StructuredList) {
        UpdateStructuredListElement(StructuredList, StructuredIndex,
                                    IList->getInit(Index));
        StructuredList->resizeInits(SemaRef.Context, StructuredIndex);
      }
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(DeclType);
      return;
    }
  }
  if (const VariableArrayType *VAT = dyn_cast<VariableArrayType>(arrayType)) {
    // Check for VLAs; in standard C it would be possible to check this
    // earlier, but I don't know where clang accepts VLAs (gcc accepts
    // them in all sorts of strange places).
    bool HasErr = IList->getNumInits() != 0 || SemaRef.getLangOpts().CPlusPlus;
    if (!VerifyOnly) {
      // C23 6.7.10p4: An entity of variable length array type shall not be
      // initialized except by an empty initializer.
      //
      // The C extension warnings are issued from ParseBraceInitializer() and
      // do not need to be issued here. However, we continue to issue an error
      // in the case there are initializers or we are compiling C++. We allow
      // use of VLAs in C++, but it's not clear we want to allow {} to zero
      // init a VLA in C++ in all cases (such as with non-trivial constructors).
      // FIXME: should we allow this construct in C++ when it makes sense to do
      // so?
      if (HasErr)
        SemaRef.Diag(VAT->getSizeExpr()->getBeginLoc(),
                     diag::err_variable_object_no_init)
            << VAT->getSizeExpr()->getSourceRange();
    }
    hadError = HasErr;
    ++Index;
    ++StructuredIndex;
    return;
  }

  // We might know the maximum number of elements in advance.
  llvm::APSInt maxElements(elementIndex.getBitWidth(),
                           elementIndex.isUnsigned());
  bool maxElementsKnown = false;
  if (const ConstantArrayType *CAT = dyn_cast<ConstantArrayType>(arrayType)) {
    maxElements = CAT->getSize();
    elementIndex = elementIndex.extOrTrunc(maxElements.getBitWidth());
    elementIndex.setIsUnsigned(maxElements.isUnsigned());
    maxElementsKnown = true;
  }

  QualType elementType = arrayType->getElementType();
  while (Index < IList->getNumInits()) {
    Expr *Init = IList->getInit(Index);
    if (DesignatedInitExpr *DIE = dyn_cast<DesignatedInitExpr>(Init)) {
      // If we're not the subobject that matches up with the '{' for
      // the designator, we shouldn't be handling the
      // designator. Return immediately.
      if (!SubobjectIsDesignatorContext)
        return;

      // Handle this designated initializer. elementIndex will be
      // updated to be the next array element we'll initialize.
      if (CheckDesignatedInitializer(Entity, IList, DIE, 0,
                                     DeclType, nullptr, &elementIndex, Index,
                                     StructuredList, StructuredIndex, true,
                                     false)) {
        hadError = true;
        continue;
      }

      if (elementIndex.getBitWidth() > maxElements.getBitWidth())
        maxElements = maxElements.extend(elementIndex.getBitWidth());
      else if (elementIndex.getBitWidth() < maxElements.getBitWidth())
        elementIndex = elementIndex.extend(maxElements.getBitWidth());
      elementIndex.setIsUnsigned(maxElements.isUnsigned());

      // If the array is of incomplete type, keep track of the number of
      // elements in the initializer.
      if (!maxElementsKnown && elementIndex > maxElements)
        maxElements = elementIndex;

      continue;
    }

    // If we know the maximum number of elements, and we've already
    // hit it, stop consuming elements in the initializer list.
    if (maxElementsKnown && elementIndex == maxElements)
      break;

    InitializedEntity ElementEntity = InitializedEntity::InitializeElement(
        SemaRef.Context, StructuredIndex, Entity);

    unsigned EmbedElementIndexBeforeInit = CurEmbedIndex;
    // Check this element.
    CheckSubElementType(ElementEntity, IList, elementType, Index,
                        StructuredList, StructuredIndex);
    ++elementIndex;
    if ((CurEmbed || isa<EmbedExpr>(Init)) && elementType->isScalarType()) {
      if (CurEmbed) {
        elementIndex =
            elementIndex + CurEmbedIndex - EmbedElementIndexBeforeInit - 1;
      } else {
        auto Embed = cast<EmbedExpr>(Init);
        elementIndex = elementIndex + Embed->getDataElementCount() -
                       EmbedElementIndexBeforeInit - 1;
      }
    }

    // If the array is of incomplete type, keep track of the number of
    // elements in the initializer.
    if (!maxElementsKnown && elementIndex > maxElements)
      maxElements = elementIndex;
  }
  if (!hadError && DeclType->isIncompleteArrayType() && !VerifyOnly) {
    // If this is an incomplete array type, the actual type needs to
    // be calculated here.
    llvm::APSInt Zero(maxElements.getBitWidth(), maxElements.isUnsigned());
    if (maxElements == Zero && !Entity.isVariableLengthArrayNew()) {
      // Sizing an array implicitly to zero is not allowed by ISO C,
      // but is supported by GNU.
      SemaRef.Diag(IList->getBeginLoc(), diag::ext_typecheck_zero_array_size);
    }

    DeclType = SemaRef.Context.getConstantArrayType(
        elementType, maxElements, nullptr, ArraySizeModifier::Normal, 0);
  }
  if (!hadError) {
    // If there are any members of the array that get value-initialized, check
    // that is possible. That happens if we know the bound and don't have
    // enough elements, or if we're performing an array new with an unknown
    // bound.
    if ((maxElementsKnown && elementIndex < maxElements) ||
        Entity.isVariableLengthArrayNew())
      CheckEmptyInitializable(
          InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity),
          IList->getEndLoc());
  }
}

bool InitListChecker::CheckFlexibleArrayInit(const InitializedEntity &Entity,
                                             Expr *InitExpr,
                                             FieldDecl *Field,
                                             bool TopLevelObject) {
  // Handle GNU flexible array initializers.
  unsigned FlexArrayDiag;
  if (isa<InitListExpr>(InitExpr) &&
      cast<InitListExpr>(InitExpr)->getNumInits() == 0) {
    // Empty flexible array init always allowed as an extension
    FlexArrayDiag = diag::ext_flexible_array_init;
  } else if (!TopLevelObject) {
    // Disallow flexible array init on non-top-level object
    FlexArrayDiag = diag::err_flexible_array_init;
  } else if (Entity.getKind() != InitializedEntity::EK_Variable) {
    // Disallow flexible array init on anything which is not a variable.
    FlexArrayDiag = diag::err_flexible_array_init;
  } else if (cast<VarDecl>(Entity.getDecl())->hasLocalStorage()) {
    // Disallow flexible array init on local variables.
    FlexArrayDiag = diag::err_flexible_array_init;
  } else {
    // Allow other cases.
    FlexArrayDiag = diag::ext_flexible_array_init;
  }

  if (!VerifyOnly) {
    SemaRef.Diag(InitExpr->getBeginLoc(), FlexArrayDiag)
        << InitExpr->getBeginLoc();
    SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
      << Field;
  }

  return FlexArrayDiag != diag::ext_flexible_array_init;
}

void InitListChecker::CheckStructUnionTypes(
    const InitializedEntity &Entity, InitListExpr *IList, QualType DeclType,
    CXXRecordDecl::base_class_const_range Bases, RecordDecl::field_iterator Field,
    bool SubobjectIsDesignatorContext, unsigned &Index,
    InitListExpr *StructuredList, unsigned &StructuredIndex,
    bool TopLevelObject) {
  const RecordDecl *RD = getRecordDecl(DeclType);

  // If the record is invalid, some of it's members are invalid. To avoid
  // confusion, we forgo checking the initializer for the entire record.
  if (RD->isInvalidDecl()) {
    // Assume it was supposed to consume a single initializer.
    ++Index;
    hadError = true;
    return;
  }

  if (RD->isUnion() && IList->getNumInits() == 0) {
    if (!VerifyOnly)
      for (FieldDecl *FD : RD->fields()) {
        QualType ET = SemaRef.Context.getBaseElementType(FD->getType());
        if (checkDestructorReference(ET, IList->getEndLoc(), SemaRef)) {
          hadError = true;
          return;
        }
      }

    // If there's a default initializer, use it.
    if (isa<CXXRecordDecl>(RD) &&
        cast<CXXRecordDecl>(RD)->hasInClassInitializer()) {
      if (!StructuredList)
        return;
      for (RecordDecl::field_iterator FieldEnd = RD->field_end();
           Field != FieldEnd; ++Field) {
        if (Field->hasInClassInitializer() ||
            (Field->isAnonymousStructOrUnion() &&
             Field->getType()->getAsCXXRecordDecl()->hasInClassInitializer())) {
          StructuredList->setInitializedFieldInUnion(*Field);
          // FIXME: Actually build a CXXDefaultInitExpr?
          return;
        }
      }
      llvm_unreachable("Couldn't find in-class initializer");
    }

    // Value-initialize the first member of the union that isn't an unnamed
    // bitfield.
    for (RecordDecl::field_iterator FieldEnd = RD->field_end();
         Field != FieldEnd; ++Field) {
      if (!Field->isUnnamedBitField()) {
        CheckEmptyInitializable(
            InitializedEntity::InitializeMember(*Field, &Entity),
            IList->getEndLoc());
        if (StructuredList)
          StructuredList->setInitializedFieldInUnion(*Field);
        break;
      }
    }
    return;
  }

  bool InitializedSomething = false;

  // If we have any base classes, they are initialized prior to the fields.
  for (auto I = Bases.begin(), E = Bases.end(); I != E; ++I) {
    auto &Base = *I;
    Expr *Init = Index < IList->getNumInits() ? IList->getInit(Index) : nullptr;

    // Designated inits always initialize fields, so if we see one, all
    // remaining base classes have no explicit initializer.
    if (isa_and_nonnull<DesignatedInitExpr>(Init))
      Init = nullptr;

    // C++ [over.match.class.deduct]p1.6:
    //   each non-trailing aggregate element that is a pack expansion is assumed
    //   to correspond to no elements of the initializer list, and (1.7) a
    //   trailing aggregate element that is a pack expansion is assumed to
    //   correspond to all remaining elements of the initializer list (if any).

    // C++ [over.match.class.deduct]p1.9:
    //   ... except that additional parameter packs of the form P_j... are
    //   inserted into the parameter list in their original aggregate element
    //   position corresponding to each non-trailing aggregate element of
    //   type P_j that was skipped because it was a parameter pack, and the
    //   trailing sequence of parameters corresponding to a trailing
    //   aggregate element that is a pack expansion (if any) is replaced
    //   by a single parameter of the form T_n....
    if (AggrDeductionCandidateParamTypes && Base.isPackExpansion()) {
      AggrDeductionCandidateParamTypes->push_back(
          SemaRef.Context.getPackExpansionType(Base.getType(), std::nullopt));

      // Trailing pack expansion
      if (I + 1 == E && RD->field_empty()) {
        if (Index < IList->getNumInits())
          Index = IList->getNumInits();
        return;
      }

      continue;
    }

    SourceLocation InitLoc = Init ? Init->getBeginLoc() : IList->getEndLoc();
    InitializedEntity BaseEntity = InitializedEntity::InitializeBase(
        SemaRef.Context, &Base, false, &Entity);
    if (Init) {
      CheckSubElementType(BaseEntity, IList, Base.getType(), Index,
                          StructuredList, StructuredIndex);
      InitializedSomething = true;
    } else {
      CheckEmptyInitializable(BaseEntity, InitLoc);
    }

    if (!VerifyOnly)
      if (checkDestructorReference(Base.getType(), InitLoc, SemaRef)) {
        hadError = true;
        return;
      }
  }

  // If structDecl is a forward declaration, this loop won't do
  // anything except look at designated initializers; That's okay,
  // because an error should get printed out elsewhere. It might be
  // worthwhile to skip over the rest of the initializer, though.
  RecordDecl::field_iterator FieldEnd = RD->field_end();
  size_t NumRecordDecls = llvm::count_if(RD->decls(), [&](const Decl *D) {
    return isa<FieldDecl>(D) || isa<RecordDecl>(D);
  });
  bool HasDesignatedInit = false;

  llvm::SmallPtrSet<FieldDecl *, 4> InitializedFields;

  while (Index < IList->getNumInits()) {
    Expr *Init = IList->getInit(Index);
    SourceLocation InitLoc = Init->getBeginLoc();

    if (DesignatedInitExpr *DIE = dyn_cast<DesignatedInitExpr>(Init)) {
      // If we're not the subobject that matches up with the '{' for
      // the designator, we shouldn't be handling the
      // designator. Return immediately.
      if (!SubobjectIsDesignatorContext)
        return;

      HasDesignatedInit = true;

      // Handle this designated initializer. Field will be updated to
      // the next field that we'll be initializing.
      bool DesignatedInitFailed = CheckDesignatedInitializer(
          Entity, IList, DIE, 0, DeclType, &Field, nullptr, Index,
          StructuredList, StructuredIndex, true, TopLevelObject);
      if (DesignatedInitFailed)
        hadError = true;

      // Find the field named by the designated initializer.
      DesignatedInitExpr::Designator *D = DIE->getDesignator(0);
      if (!VerifyOnly && D->isFieldDesignator()) {
        FieldDecl *F = D->getFieldDecl();
        InitializedFields.insert(F);
        if (!DesignatedInitFailed) {
          QualType ET = SemaRef.Context.getBaseElementType(F->getType());
          if (checkDestructorReference(ET, InitLoc, SemaRef)) {
            hadError = true;
            return;
          }
        }
      }

      InitializedSomething = true;
      continue;
    }

    // Check if this is an initializer of forms:
    //
    //   struct foo f = {};
    //   struct foo g = {0};
    //
    // These are okay for randomized structures. [C99 6.7.8p19]
    //
    // Also, if there is only one element in the structure, we allow something
    // like this, because it's really not randomized in the traditional sense.
    //
    //   struct foo h = {bar};
    auto IsZeroInitializer = [&](const Expr *I) {
      if (IList->getNumInits() == 1) {
        if (NumRecordDecls == 1)
          return true;
        if (const auto *IL = dyn_cast<IntegerLiteral>(I))
          return IL->getValue().isZero();
      }
      return false;
    };

    // Don't allow non-designated initializers on randomized structures.
    if (RD->isRandomized() && !IsZeroInitializer(Init)) {
      if (!VerifyOnly)
        SemaRef.Diag(InitLoc, diag::err_non_designated_init_used);
      hadError = true;
      break;
    }

    if (Field == FieldEnd) {
      // We've run out of fields. We're done.
      break;
    }

    // We've already initialized a member of a union. We can stop entirely.
    if (InitializedSomething && RD->isUnion())
      return;

    // Stop if we've hit a flexible array member.
    if (Field->getType()->isIncompleteArrayType())
      break;

    if (Field->isUnnamedBitField()) {
      // Don't initialize unnamed bitfields, e.g. "int : 20;"
      ++Field;
      continue;
    }

    // Make sure we can use this declaration.
    bool InvalidUse;
    if (VerifyOnly)
      InvalidUse = !SemaRef.CanUseDecl(*Field, TreatUnavailableAsInvalid);
    else
      InvalidUse = SemaRef.DiagnoseUseOfDecl(
          *Field, IList->getInit(Index)->getBeginLoc());
    if (InvalidUse) {
      ++Index;
      ++Field;
      hadError = true;
      continue;
    }

    if (!VerifyOnly) {
      QualType ET = SemaRef.Context.getBaseElementType(Field->getType());
      if (checkDestructorReference(ET, InitLoc, SemaRef)) {
        hadError = true;
        return;
      }
    }

    InitializedEntity MemberEntity =
      InitializedEntity::InitializeMember(*Field, &Entity);
    CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                        StructuredList, StructuredIndex);
    InitializedSomething = true;
    InitializedFields.insert(*Field);

    if (RD->isUnion() && StructuredList) {
      // Initialize the first field within the union.
      StructuredList->setInitializedFieldInUnion(*Field);
    }

    ++Field;
  }

  // Emit warnings for missing struct field initializers.
  // This check is disabled for designated initializers in C.
  // This matches gcc behaviour.
  bool IsCDesignatedInitializer =
      HasDesignatedInit && !SemaRef.getLangOpts().CPlusPlus;
  if (!VerifyOnly && InitializedSomething && !RD->isUnion() &&
      !IList->isIdiomaticZeroInitializer(SemaRef.getLangOpts()) &&
      !IsCDesignatedInitializer) {
    // It is possible we have one or more unnamed bitfields remaining.
    // Find first (if any) named field and emit warning.
    for (RecordDecl::field_iterator it = HasDesignatedInit ? RD->field_begin()
                                                           : Field,
                                    end = RD->field_end();
         it != end; ++it) {
      if (HasDesignatedInit && InitializedFields.count(*it))
        continue;

      if (!it->isUnnamedBitField() && !it->hasInClassInitializer() &&
          !it->getType()->isIncompleteArrayType()) {
        auto Diag = HasDesignatedInit
                        ? diag::warn_missing_designated_field_initializers
                        : diag::warn_missing_field_initializers;
        SemaRef.Diag(IList->getSourceRange().getEnd(), Diag) << *it;
        break;
      }
    }
  }

  // Check that any remaining fields can be value-initialized if we're not
  // building a structured list. (If we are, we'll check this later.)
  if (!StructuredList && Field != FieldEnd && !RD->isUnion() &&
      !Field->getType()->isIncompleteArrayType()) {
    for (; Field != FieldEnd && !hadError; ++Field) {
      if (!Field->isUnnamedBitField() && !Field->hasInClassInitializer())
        CheckEmptyInitializable(
            InitializedEntity::InitializeMember(*Field, &Entity),
            IList->getEndLoc());
    }
  }

  // Check that the types of the remaining fields have accessible destructors.
  if (!VerifyOnly) {
    // If the initializer expression has a designated initializer, check the
    // elements for which a designated initializer is not provided too.
    RecordDecl::field_iterator I = HasDesignatedInit ? RD->field_begin()
                                                     : Field;
    for (RecordDecl::field_iterator E = RD->field_end(); I != E; ++I) {
      QualType ET = SemaRef.Context.getBaseElementType(I->getType());
      if (checkDestructorReference(ET, IList->getEndLoc(), SemaRef)) {
        hadError = true;
        return;
      }
    }
  }

  if (Field == FieldEnd || !Field->getType()->isIncompleteArrayType() ||
      Index >= IList->getNumInits())
    return;

  if (CheckFlexibleArrayInit(Entity, IList->getInit(Index), *Field,
                             TopLevelObject)) {
    hadError = true;
    ++Index;
    return;
  }

  InitializedEntity MemberEntity =
    InitializedEntity::InitializeMember(*Field, &Entity);

  if (isa<InitListExpr>(IList->getInit(Index)) ||
      AggrDeductionCandidateParamTypes)
    CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                        StructuredList, StructuredIndex);
  else
    CheckImplicitInitList(MemberEntity, IList, Field->getType(), Index,
                          StructuredList, StructuredIndex);

  if (RD->isUnion() && StructuredList) {
    // Initialize the first field within the union.
    StructuredList->setInitializedFieldInUnion(*Field);
  }
}

/// Expand a field designator that refers to a member of an
/// anonymous struct or union into a series of field designators that
/// refers to the field within the appropriate subobject.
///
static void ExpandAnonymousFieldDesignator(Sema &SemaRef,
                                           DesignatedInitExpr *DIE,
                                           unsigned DesigIdx,
                                           IndirectFieldDecl *IndirectField) {
  typedef DesignatedInitExpr::Designator Designator;

  // Build the replacement designators.
  SmallVector<Designator, 4> Replacements;
  for (IndirectFieldDecl::chain_iterator PI = IndirectField->chain_begin(),
       PE = IndirectField->chain_end(); PI != PE; ++PI) {
    if (PI + 1 == PE)
      Replacements.push_back(Designator::CreateFieldDesignator(
          (IdentifierInfo *)nullptr, DIE->getDesignator(DesigIdx)->getDotLoc(),
          DIE->getDesignator(DesigIdx)->getFieldLoc()));
    else
      Replacements.push_back(Designator::CreateFieldDesignator(
          (IdentifierInfo *)nullptr, SourceLocation(), SourceLocation()));
    assert(isa<FieldDecl>(*PI));
    Replacements.back().setFieldDecl(cast<FieldDecl>(*PI));
  }

  // Expand the current designator into the set of replacement
  // designators, so we have a full subobject path down to where the
  // member of the anonymous struct/union is actually stored.
  DIE->ExpandDesignator(SemaRef.Context, DesigIdx, &Replacements[0],
                        &Replacements[0] + Replacements.size());
}

static DesignatedInitExpr *CloneDesignatedInitExpr(Sema &SemaRef,
                                                   DesignatedInitExpr *DIE) {
  unsigned NumIndexExprs = DIE->getNumSubExprs() - 1;
  SmallVector<Expr*, 4> IndexExprs(NumIndexExprs);
  for (unsigned I = 0; I < NumIndexExprs; ++I)
    IndexExprs[I] = DIE->getSubExpr(I + 1);
  return DesignatedInitExpr::Create(SemaRef.Context, DIE->designators(),
                                    IndexExprs,
                                    DIE->getEqualOrColonLoc(),
                                    DIE->usesGNUSyntax(), DIE->getInit());
}

namespace {

// Callback to only accept typo corrections that are for field members of
// the given struct or union.
class FieldInitializerValidatorCCC final : public CorrectionCandidateCallback {
 public:
  explicit FieldInitializerValidatorCCC(const RecordDecl *RD)
      : Record(RD) {}

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    FieldDecl *FD = candidate.getCorrectionDeclAs<FieldDecl>();
    return FD && FD->getDeclContext()->getRedeclContext()->Equals(Record);
  }

  std::unique_ptr<CorrectionCandidateCallback> clone() override {
    return std::make_unique<FieldInitializerValidatorCCC>(*this);
  }

 private:
  const RecordDecl *Record;
};

} // end anonymous namespace

/// Check the well-formedness of a C99 designated initializer.
///
/// Determines whether the designated initializer @p DIE, which
/// resides at the given @p Index within the initializer list @p
/// IList, is well-formed for a current object of type @p DeclType
/// (C99 6.7.8). The actual subobject that this designator refers to
/// within the current subobject is returned in either
/// @p NextField or @p NextElementIndex (whichever is appropriate).
///
/// @param IList  The initializer list in which this designated
/// initializer occurs.
///
/// @param DIE The designated initializer expression.
///
/// @param DesigIdx  The index of the current designator.
///
/// @param CurrentObjectType The type of the "current object" (C99 6.7.8p17),
/// into which the designation in @p DIE should refer.
///
/// @param NextField  If non-NULL and the first designator in @p DIE is
/// a field, this will be set to the field declaration corresponding
/// to the field named by the designator. On input, this is expected to be
/// the next field that would be initialized in the absence of designation,
/// if the complete object being initialized is a struct.
///
/// @param NextElementIndex  If non-NULL and the first designator in @p
/// DIE is an array designator or GNU array-range designator, this
/// will be set to the last index initialized by this designator.
///
/// @param Index  Index into @p IList where the designated initializer
/// @p DIE occurs.
///
/// @param StructuredList  The initializer list expression that
/// describes all of the subobject initializers in the order they'll
/// actually be initialized.
///
/// @returns true if there was an error, false otherwise.
bool
InitListChecker::CheckDesignatedInitializer(const InitializedEntity &Entity,
                                            InitListExpr *IList,
                                            DesignatedInitExpr *DIE,
                                            unsigned DesigIdx,
                                            QualType &CurrentObjectType,
                                          RecordDecl::field_iterator *NextField,
                                            llvm::APSInt *NextElementIndex,
                                            unsigned &Index,
                                            InitListExpr *StructuredList,
                                            unsigned &StructuredIndex,
                                            bool FinishSubobjectInit,
                                            bool TopLevelObject) {
  if (DesigIdx == DIE->size()) {
    // C++20 designated initialization can result in direct-list-initialization
    // of the designated subobject. This is the only way that we can end up
    // performing direct initialization as part of aggregate initialization, so
    // it needs special handling.
    if (DIE->isDirectInit()) {
      Expr *Init = DIE->getInit();
      assert(isa<InitListExpr>(Init) &&
             "designator result in direct non-list initialization?");
      InitializationKind Kind = InitializationKind::CreateDirectList(
          DIE->getBeginLoc(), Init->getBeginLoc(), Init->getEndLoc());
      InitializationSequence Seq(SemaRef, Entity, Kind, Init,
                                 /*TopLevelOfInitList*/ true);
      if (StructuredList) {
        ExprResult Result = VerifyOnly
                                ? getDummyInit()
                                : Seq.Perform(SemaRef, Entity, Kind, Init);
        UpdateStructuredListElement(StructuredList, StructuredIndex,
                                    Result.get());
      }
      ++Index;
      if (AggrDeductionCandidateParamTypes)
        AggrDeductionCandidateParamTypes->push_back(CurrentObjectType);
      return !Seq;
    }

    // Check the actual initialization for the designated object type.
    bool prevHadError = hadError;

    // Temporarily remove the designator expression from the
    // initializer list that the child calls see, so that we don't try
    // to re-process the designator.
    unsigned OldIndex = Index;
    IList->setInit(OldIndex, DIE->getInit());

    CheckSubElementType(Entity, IList, CurrentObjectType, Index, StructuredList,
                        StructuredIndex, /*DirectlyDesignated=*/true);

    // Restore the designated initializer expression in the syntactic
    // form of the initializer list.
    if (IList->getInit(OldIndex) != DIE->getInit())
      DIE->setInit(IList->getInit(OldIndex));
    IList->setInit(OldIndex, DIE);

    return hadError && !prevHadError;
  }

  DesignatedInitExpr::Designator *D = DIE->getDesignator(DesigIdx);
  bool IsFirstDesignator = (DesigIdx == 0);
  if (IsFirstDesignator ? FullyStructuredList : StructuredList) {
    // Determine the structural initializer list that corresponds to the
    // current subobject.
    if (IsFirstDesignator)
      StructuredList = FullyStructuredList;
    else {
      Expr *ExistingInit = StructuredIndex < StructuredList->getNumInits() ?
          StructuredList->getInit(StructuredIndex) : nullptr;
      if (!ExistingInit && StructuredList->hasArrayFiller())
        ExistingInit = StructuredList->getArrayFiller();

      if (!ExistingInit)
        StructuredList = getStructuredSubobjectInit(
            IList, Index, CurrentObjectType, StructuredList, StructuredIndex,
            SourceRange(D->getBeginLoc(), DIE->getEndLoc()));
      else if (InitListExpr *Result = dyn_cast<InitListExpr>(ExistingInit))
        StructuredList = Result;
      else {
        // We are creating an initializer list that initializes the
        // subobjects of the current object, but there was already an
        // initialization that completely initialized the current
        // subobject, e.g., by a compound literal:
        //
        // struct X { int a, b; };
        // struct X xs[] = { [0] = (struct X) { 1, 2 }, [0].b = 3 };
        //
        // Here, xs[0].a == 1 and xs[0].b == 3, since the second,
        // designated initializer re-initializes only its current object
        // subobject [0].b.
        diagnoseInitOverride(ExistingInit,
                             SourceRange(D->getBeginLoc(), DIE->getEndLoc()),
                             /*UnionOverride=*/false,
                             /*FullyOverwritten=*/false);

        if (!VerifyOnly) {
          if (DesignatedInitUpdateExpr *E =
                  dyn_cast<DesignatedInitUpdateExpr>(ExistingInit))
            StructuredList = E->getUpdater();
          else {
            DesignatedInitUpdateExpr *DIUE = new (SemaRef.Context)
                DesignatedInitUpdateExpr(SemaRef.Context, D->getBeginLoc(),
                                         ExistingInit, DIE->getEndLoc());
            StructuredList->updateInit(SemaRef.Context, StructuredIndex, DIUE);
            StructuredList = DIUE->getUpdater();
          }
        } else {
          // We don't need to track the structured representation of a
          // designated init update of an already-fully-initialized object in
          // verify-only mode. The only reason we would need the structure is
          // to determine where the uninitialized "holes" are, and in this
          // case, we know there aren't any and we can't introduce any.
          StructuredList = nullptr;
        }
      }
    }
  }

  if (D->isFieldDesignator()) {
    // C99 6.7.8p7:
    //
    //   If a designator has the form
    //
    //      . identifier
    //
    //   then the current object (defined below) shall have
    //   structure or union type and the identifier shall be the
    //   name of a member of that type.
    RecordDecl *RD = getRecordDecl(CurrentObjectType);
    if (!RD) {
      SourceLocation Loc = D->getDotLoc();
      if (Loc.isInvalid())
        Loc = D->getFieldLoc();
      if (!VerifyOnly)
        SemaRef.Diag(Loc, diag::err_field_designator_non_aggr)
          << SemaRef.getLangOpts().CPlusPlus << CurrentObjectType;
      ++Index;
      return true;
    }

    FieldDecl *KnownField = D->getFieldDecl();
    if (!KnownField) {
      const IdentifierInfo *FieldName = D->getFieldName();
      ValueDecl *VD = SemaRef.tryLookupUnambiguousFieldDecl(RD, FieldName);
      if (auto *FD = dyn_cast_if_present<FieldDecl>(VD)) {
        KnownField = FD;
      } else if (auto *IFD = dyn_cast_if_present<IndirectFieldDecl>(VD)) {
        // In verify mode, don't modify the original.
        if (VerifyOnly)
          DIE = CloneDesignatedInitExpr(SemaRef, DIE);
        ExpandAnonymousFieldDesignator(SemaRef, DIE, DesigIdx, IFD);
        D = DIE->getDesignator(DesigIdx);
        KnownField = cast<FieldDecl>(*IFD->chain_begin());
      }
      if (!KnownField) {
        if (VerifyOnly) {
          ++Index;
          return true;  // No typo correction when just trying this out.
        }

        // We found a placeholder variable
        if (SemaRef.DiagRedefinedPlaceholderFieldDecl(DIE->getBeginLoc(), RD,
                                                      FieldName)) {
          ++Index;
          return true;
        }
        // Name lookup found something, but it wasn't a field.
        if (DeclContextLookupResult Lookup = RD->lookup(FieldName);
            !Lookup.empty()) {
          SemaRef.Diag(D->getFieldLoc(), diag::err_field_designator_nonfield)
              << FieldName;
          SemaRef.Diag(Lookup.front()->getLocation(),
                       diag::note_field_designator_found);
          ++Index;
          return true;
        }

        // Name lookup didn't find anything.
        // Determine whether this was a typo for another field name.
        FieldInitializerValidatorCCC CCC(RD);
        if (TypoCorrection Corrected = SemaRef.CorrectTypo(
                DeclarationNameInfo(FieldName, D->getFieldLoc()),
                Sema::LookupMemberName, /*Scope=*/nullptr, /*SS=*/nullptr, CCC,
                Sema::CTK_ErrorRecovery, RD)) {
          SemaRef.diagnoseTypo(
              Corrected,
              SemaRef.PDiag(diag::err_field_designator_unknown_suggest)
                << FieldName << CurrentObjectType);
          KnownField = Corrected.getCorrectionDeclAs<FieldDecl>();
          hadError = true;
        } else {
          // Typo correction didn't find anything.
          SourceLocation Loc = D->getFieldLoc();

          // The loc can be invalid with a "null" designator (i.e. an anonymous
          // union/struct). Do our best to approximate the location.
          if (Loc.isInvalid())
            Loc = IList->getBeginLoc();

          SemaRef.Diag(Loc, diag::err_field_designator_unknown)
            << FieldName << CurrentObjectType << DIE->getSourceRange();
          ++Index;
          return true;
        }
      }
    }

    unsigned NumBases = 0;
    if (auto *CXXRD = dyn_cast<CXXRecordDecl>(RD))
      NumBases = CXXRD->getNumBases();

    unsigned FieldIndex = NumBases;

    for (auto *FI : RD->fields()) {
      if (FI->isUnnamedBitField())
        continue;
      if (declaresSameEntity(KnownField, FI)) {
        KnownField = FI;
        break;
      }
      ++FieldIndex;
    }

    RecordDecl::field_iterator Field =
        RecordDecl::field_iterator(DeclContext::decl_iterator(KnownField));

    // All of the fields of a union are located at the same place in
    // the initializer list.
    if (RD->isUnion()) {
      FieldIndex = 0;
      if (StructuredList) {
        FieldDecl *CurrentField = StructuredList->getInitializedFieldInUnion();
        if (CurrentField && !declaresSameEntity(CurrentField, *Field)) {
          assert(StructuredList->getNumInits() == 1
                 && "A union should never have more than one initializer!");

          Expr *ExistingInit = StructuredList->getInit(0);
          if (ExistingInit) {
            // We're about to throw away an initializer, emit warning.
            diagnoseInitOverride(
                ExistingInit, SourceRange(D->getBeginLoc(), DIE->getEndLoc()),
                /*UnionOverride=*/true,
                /*FullyOverwritten=*/SemaRef.getLangOpts().CPlusPlus ? false
                                                                     : true);
          }

          // remove existing initializer
          StructuredList->resizeInits(SemaRef.Context, 0);
          StructuredList->setInitializedFieldInUnion(nullptr);
        }

        StructuredList->setInitializedFieldInUnion(*Field);
      }
    }

    // Make sure we can use this declaration.
    bool InvalidUse;
    if (VerifyOnly)
      InvalidUse = !SemaRef.CanUseDecl(*Field, TreatUnavailableAsInvalid);
    else
      InvalidUse = SemaRef.DiagnoseUseOfDecl(*Field, D->getFieldLoc());
    if (InvalidUse) {
      ++Index;
      return true;
    }

    // C++20 [dcl.init.list]p3:
    //   The ordered identifiers in the designators of the designated-
    //   initializer-list shall form a subsequence of the ordered identifiers
    //   in the direct non-static data members of T.
    //
    // Note that this is not a condition on forming the aggregate
    // initialization, only on actually performing initialization,
    // so it is not checked in VerifyOnly mode.
    //
    // FIXME: This is the only reordering diagnostic we produce, and it only
    // catches cases where we have a top-level field designator that jumps
    // backwards. This is the only such case that is reachable in an
    // otherwise-valid C++20 program, so is the only case that's required for
    // conformance, but for consistency, we should diagnose all the other
    // cases where a designator takes us backwards too.
    if (IsFirstDesignator && !VerifyOnly && SemaRef.getLangOpts().CPlusPlus &&
        NextField &&
        (*NextField == RD->field_end() ||
         (*NextField)->getFieldIndex() > Field->getFieldIndex() + 1)) {
      // Find the field that we just initialized.
      FieldDecl *PrevField = nullptr;
      for (auto FI = RD->field_begin(); FI != RD->field_end(); ++FI) {
        if (FI->isUnnamedBitField())
          continue;
        if (*NextField != RD->field_end() &&
            declaresSameEntity(*FI, **NextField))
          break;
        PrevField = *FI;
      }

      if (PrevField &&
          PrevField->getFieldIndex() > KnownField->getFieldIndex()) {
        SemaRef.Diag(DIE->getInit()->getBeginLoc(),
                     diag::ext_designated_init_reordered)
            << KnownField << PrevField << DIE->getSourceRange();

        unsigned OldIndex = StructuredIndex - 1;
        if (StructuredList && OldIndex <= StructuredList->getNumInits()) {
          if (Expr *PrevInit = StructuredList->getInit(OldIndex)) {
            SemaRef.Diag(PrevInit->getBeginLoc(),
                         diag::note_previous_field_init)
                << PrevField << PrevInit->getSourceRange();
          }
        }
      }
    }


    // Update the designator with the field declaration.
    if (!VerifyOnly)
      D->setFieldDecl(*Field);

    // Make sure that our non-designated initializer list has space
    // for a subobject corresponding to this field.
    if (StructuredList && FieldIndex >= StructuredList->getNumInits())
      StructuredList->resizeInits(SemaRef.Context, FieldIndex + 1);

    // This designator names a flexible array member.
    if (Field->getType()->isIncompleteArrayType()) {
      bool Invalid = false;
      if ((DesigIdx + 1) != DIE->size()) {
        // We can't designate an object within the flexible array
        // member (because GCC doesn't allow it).
        if (!VerifyOnly) {
          DesignatedInitExpr::Designator *NextD
            = DIE->getDesignator(DesigIdx + 1);
          SemaRef.Diag(NextD->getBeginLoc(),
                       diag::err_designator_into_flexible_array_member)
              << SourceRange(NextD->getBeginLoc(), DIE->getEndLoc());
          SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
            << *Field;
        }
        Invalid = true;
      }

      if (!hadError && !isa<InitListExpr>(DIE->getInit()) &&
          !isa<StringLiteral>(DIE->getInit())) {
        // The initializer is not an initializer list.
        if (!VerifyOnly) {
          SemaRef.Diag(DIE->getInit()->getBeginLoc(),
                       diag::err_flexible_array_init_needs_braces)
              << DIE->getInit()->getSourceRange();
          SemaRef.Diag(Field->getLocation(), diag::note_flexible_array_member)
            << *Field;
        }
        Invalid = true;
      }

      // Check GNU flexible array initializer.
      if (!Invalid && CheckFlexibleArrayInit(Entity, DIE->getInit(), *Field,
                                             TopLevelObject))
        Invalid = true;

      if (Invalid) {
        ++Index;
        return true;
      }

      // Initialize the array.
      bool prevHadError = hadError;
      unsigned newStructuredIndex = FieldIndex;
      unsigned OldIndex = Index;
      IList->setInit(Index, DIE->getInit());

      InitializedEntity MemberEntity =
        InitializedEntity::InitializeMember(*Field, &Entity);
      CheckSubElementType(MemberEntity, IList, Field->getType(), Index,
                          StructuredList, newStructuredIndex);

      IList->setInit(OldIndex, DIE);
      if (hadError && !prevHadError) {
        ++Field;
        ++FieldIndex;
        if (NextField)
          *NextField = Field;
        StructuredIndex = FieldIndex;
        return true;
      }
    } else {
      // Recurse to check later designated subobjects.
      QualType FieldType = Field->getType();
      unsigned newStructuredIndex = FieldIndex;

      InitializedEntity MemberEntity =
        InitializedEntity::InitializeMember(*Field, &Entity);
      if (CheckDesignatedInitializer(MemberEntity, IList, DIE, DesigIdx + 1,
                                     FieldType, nullptr, nullptr, Index,
                                     StructuredList, newStructuredIndex,
                                     FinishSubobjectInit, false))
        return true;
    }

    // Find the position of the next field to be initialized in this
    // subobject.
    ++Field;
    ++FieldIndex;

    // If this the first designator, our caller will continue checking
    // the rest of this struct/class/union subobject.
    if (IsFirstDesignator) {
      if (Field != RD->field_end() && Field->isUnnamedBitField())
        ++Field;

      if (NextField)
        *NextField = Field;

      StructuredIndex = FieldIndex;
      return false;
    }

    if (!FinishSubobjectInit)
      return false;

    // We've already initialized something in the union; we're done.
    if (RD->isUnion())
      return hadError;

    // Check the remaining fields within this class/struct/union subobject.
    bool prevHadError = hadError;

    auto NoBases =
        CXXRecordDecl::base_class_range(CXXRecordDecl::base_class_iterator(),
                                        CXXRecordDecl::base_class_iterator());
    CheckStructUnionTypes(Entity, IList, CurrentObjectType, NoBases, Field,
                          false, Index, StructuredList, FieldIndex);
    return hadError && !prevHadError;
  }

  // C99 6.7.8p6:
  //
  //   If a designator has the form
  //
  //      [ constant-expression ]
  //
  //   then the current object (defined below) shall have array
  //   type and the expression shall be an integer constant
  //   expression. If the array is of unknown size, any
  //   nonnegative value is valid.
  //
  // Additionally, cope with the GNU extension that permits
  // designators of the form
  //
  //      [ constant-expression ... constant-expression ]
  const ArrayType *AT = SemaRef.Context.getAsArrayType(CurrentObjectType);
  if (!AT) {
    if (!VerifyOnly)
      SemaRef.Diag(D->getLBracketLoc(), diag::err_array_designator_non_array)
        << CurrentObjectType;
    ++Index;
    return true;
  }

  Expr *IndexExpr = nullptr;
  llvm::APSInt DesignatedStartIndex, DesignatedEndIndex;
  if (D->isArrayDesignator()) {
    IndexExpr = DIE->getArrayIndex(*D);
    DesignatedStartIndex = IndexExpr->EvaluateKnownConstInt(SemaRef.Context);
    DesignatedEndIndex = DesignatedStartIndex;
  } else {
    assert(D->isArrayRangeDesignator() && "Need array-range designator");

    DesignatedStartIndex =
      DIE->getArrayRangeStart(*D)->EvaluateKnownConstInt(SemaRef.Context);
    DesignatedEndIndex =
      DIE->getArrayRangeEnd(*D)->EvaluateKnownConstInt(SemaRef.Context);
    IndexExpr = DIE->getArrayRangeEnd(*D);

    // Codegen can't handle evaluating array range designators that have side
    // effects, because we replicate the AST value for each initialized element.
    // As such, set the sawArrayRangeDesignator() bit if we initialize multiple
    // elements with something that has a side effect, so codegen can emit an
    // "error unsupported" error instead of miscompiling the app.
    if (DesignatedStartIndex.getZExtValue()!=DesignatedEndIndex.getZExtValue()&&
        DIE->getInit()->HasSideEffects(SemaRef.Context) && !VerifyOnly)
      FullyStructuredList->sawArrayRangeDesignator();
  }

  if (isa<ConstantArrayType>(AT)) {
    llvm::APSInt MaxElements(cast<ConstantArrayType>(AT)->getSize(), false);
    DesignatedStartIndex
      = DesignatedStartIndex.extOrTrunc(MaxElements.getBitWidth());
    DesignatedStartIndex.setIsUnsigned(MaxElements.isUnsigned());
    DesignatedEndIndex
      = DesignatedEndIndex.extOrTrunc(MaxElements.getBitWidth());
    DesignatedEndIndex.setIsUnsigned(MaxElements.isUnsigned());
    if (DesignatedEndIndex >= MaxElements) {
      if (!VerifyOnly)
        SemaRef.Diag(IndexExpr->getBeginLoc(),
                     diag::err_array_designator_too_large)
            << toString(DesignatedEndIndex, 10) << toString(MaxElements, 10)
            << IndexExpr->getSourceRange();
      ++Index;
      return true;
    }
  } else {
    unsigned DesignatedIndexBitWidth =
      ConstantArrayType::getMaxSizeBits(SemaRef.Context);
    DesignatedStartIndex =
      DesignatedStartIndex.extOrTrunc(DesignatedIndexBitWidth);
    DesignatedEndIndex =
      DesignatedEndIndex.extOrTrunc(DesignatedIndexBitWidth);
    DesignatedStartIndex.setIsUnsigned(true);
    DesignatedEndIndex.setIsUnsigned(true);
  }

  bool IsStringLiteralInitUpdate =
      StructuredList && StructuredList->isStringLiteralInit();
  if (IsStringLiteralInitUpdate && VerifyOnly) {
    // We're just verifying an update to a string literal init. We don't need
    // to split the string up into individual characters to do that.
    StructuredList = nullptr;
  } else if (IsStringLiteralInitUpdate) {
    // We're modifying a string literal init; we have to decompose the string
    // so we can modify the individual characters.
    ASTContext &Context = SemaRef.Context;
    Expr *SubExpr = StructuredList->getInit(0)->IgnoreParenImpCasts();

    // Compute the character type
    QualType CharTy = AT->getElementType();

    // Compute the type of the integer literals.
    QualType PromotedCharTy = CharTy;
    if (Context.isPromotableIntegerType(CharTy))
      PromotedCharTy = Context.getPromotedIntegerType(CharTy);
    unsigned PromotedCharTyWidth = Context.getTypeSize(PromotedCharTy);

    if (StringLiteral *SL = dyn_cast<StringLiteral>(SubExpr)) {
      // Get the length of the string.
      uint64_t StrLen = SL->getLength();
      if (cast<ConstantArrayType>(AT)->getSize().ult(StrLen))
        StrLen = cast<ConstantArrayType>(AT)->getZExtSize();
      StructuredList->resizeInits(Context, StrLen);

      // Build a literal for each character in the string, and put them into
      // the init list.
      for (unsigned i = 0, e = StrLen; i != e; ++i) {
        llvm::APInt CodeUnit(PromotedCharTyWidth, SL->getCodeUnit(i));
        Expr *Init = new (Context) IntegerLiteral(
            Context, CodeUnit, PromotedCharTy, SubExpr->getExprLoc());
        if (CharTy != PromotedCharTy)
          Init = ImplicitCastExpr::Create(Context, CharTy, CK_IntegralCast,
                                          Init, nullptr, VK_PRValue,
                                          FPOptionsOverride());
        StructuredList->updateInit(Context, i, Init);
      }
    } else {
      ObjCEncodeExpr *E = cast<ObjCEncodeExpr>(SubExpr);
      std::string Str;
      Context.getObjCEncodingForType(E->getEncodedType(), Str);

      // Get the length of the string.
      uint64_t StrLen = Str.size();
      if (cast<ConstantArrayType>(AT)->getSize().ult(StrLen))
        StrLen = cast<ConstantArrayType>(AT)->getZExtSize();
      StructuredList->resizeInits(Context, StrLen);

      // Build a literal for each character in the string, and put them into
      // the init list.
      for (unsigned i = 0, e = StrLen; i != e; ++i) {
        llvm::APInt CodeUnit(PromotedCharTyWidth, Str[i]);
        Expr *Init = new (Context) IntegerLiteral(
            Context, CodeUnit, PromotedCharTy, SubExpr->getExprLoc());
        if (CharTy != PromotedCharTy)
          Init = ImplicitCastExpr::Create(Context, CharTy, CK_IntegralCast,
                                          Init, nullptr, VK_PRValue,
                                          FPOptionsOverride());
        StructuredList->updateInit(Context, i, Init);
      }
    }
  }

  // Make sure that our non-designated initializer list has space
  // for a subobject corresponding to this array element.
  if (StructuredList &&
      DesignatedEndIndex.getZExtValue() >= StructuredList->getNumInits())
    StructuredList->resizeInits(SemaRef.Context,
                                DesignatedEndIndex.getZExtValue() + 1);

  // Repeatedly perform subobject initializations in the range
  // [DesignatedStartIndex, DesignatedEndIndex].

  // Move to the next designator
  unsigned ElementIndex = DesignatedStartIndex.getZExtValue();
  unsigned OldIndex = Index;

  InitializedEntity ElementEntity =
    InitializedEntity::InitializeElement(SemaRef.Context, 0, Entity);

  while (DesignatedStartIndex <= DesignatedEndIndex) {
    // Recurse to check later designated subobjects.
    QualType ElementType = AT->getElementType();
    Index = OldIndex;

    ElementEntity.setElementIndex(ElementIndex);
    if (CheckDesignatedInitializer(
            ElementEntity, IList, DIE, DesigIdx + 1, ElementType, nullptr,
            nullptr, Index, StructuredList, ElementIndex,
            FinishSubobjectInit && (DesignatedStartIndex == DesignatedEndIndex),
            false))
      return true;

    // Move to the next index in the array that we'll be initializing.
    ++DesignatedStartIndex;
    ElementIndex = DesignatedStartIndex.getZExtValue();
  }

  // If this the first designator, our caller will continue checking
  // the rest of this array subobject.
  if (IsFirstDesignator) {
    if (NextElementIndex)
      *NextElementIndex = DesignatedStartIndex;
    StructuredIndex = ElementIndex;
    return false;
  }

  if (!FinishSubobjectInit)
    return false;

  // Check the remaining elements within this array subobject.
  bool prevHadError = hadError;
  CheckArrayType(Entity, IList, CurrentObjectType, DesignatedStartIndex,
                 /*SubobjectIsDesignatorContext=*/false, Index,
                 StructuredList, ElementIndex);
  return hadError && !prevHadError;
}

// Get the structured initializer list for a subobject of type
// @p CurrentObjectType.
InitListExpr *
InitListChecker::getStructuredSubobjectInit(InitListExpr *IList, unsigned Index,
                                            QualType CurrentObjectType,
                                            InitListExpr *StructuredList,
                                            unsigned StructuredIndex,
                                            SourceRange InitRange,
                                            bool IsFullyOverwritten) {
  if (!StructuredList)
    return nullptr;

  Expr *ExistingInit = nullptr;
  if (StructuredIndex < StructuredList->getNumInits())
    ExistingInit = StructuredList->getInit(StructuredIndex);

  if (InitListExpr *Result = dyn_cast_or_null<InitListExpr>(ExistingInit))
    // There might have already been initializers for subobjects of the current
    // object, but a subsequent initializer list will overwrite the entirety
    // of the current object. (See DR 253 and C99 6.7.8p21). e.g.,
    //
    // struct P { char x[6]; };
    // struct P l = { .x[2] = 'x', .x = { [0] = 'f' } };
    //
    // The first designated initializer is ignored, and l.x is just "f".
    if (!IsFullyOverwritten)
      return Result;

  if (ExistingInit) {
    // We are creating an initializer list that initializes the
    // subobjects of the current object, but there was already an
    // initialization that completely initialized the current
    // subobject:
    //
    // struct X { int a, b; };
    // struct X xs[] = { [0] = { 1, 2 }, [0].b = 3 };
    //
    // Here, xs[0].a == 1 and xs[0].b == 3, since the second,
    // designated initializer overwrites the [0].b initializer
    // from the prior initialization.
    //
    // When the existing initializer is an expression rather than an
    // initializer list, we cannot decompose and update it in this way.
    // For example:
    //
    // struct X xs[] = { [0] = (struct X) { 1, 2 }, [0].b = 3 };
    //
    // This case is handled by CheckDesignatedInitializer.
    diagnoseInitOverride(ExistingInit, InitRange);
  }

  unsigned ExpectedNumInits = 0;
  if (Index < IList->getNumInits()) {
    if (auto *Init = dyn_cast_or_null<InitListExpr>(IList->getInit(Index)))
      ExpectedNumInits = Init->getNumInits();
    else
      ExpectedNumInits = IList->getNumInits() - Index;
  }

  InitListExpr *Result =
      createInitListExpr(CurrentObjectType, InitRange, ExpectedNumInits);

  // Link this new initializer list into the structured initializer
  // lists.
  StructuredList->updateInit(SemaRef.Context, StructuredIndex, Result);
  return Result;
}

InitListExpr *
InitListChecker::createInitListExpr(QualType CurrentObjectType,
                                    SourceRange InitRange,
                                    unsigned ExpectedNumInits) {
  InitListExpr *Result = new (SemaRef.Context) InitListExpr(
      SemaRef.Context, InitRange.getBegin(), std::nullopt, InitRange.getEnd());

  QualType ResultType = CurrentObjectType;
  if (!ResultType->isArrayType())
    ResultType = ResultType.getNonLValueExprType(SemaRef.Context);
  Result->setType(ResultType);

  // Pre-allocate storage for the structured initializer list.
  unsigned NumElements = 0;

  if (const ArrayType *AType
      = SemaRef.Context.getAsArrayType(CurrentObjectType)) {
    if (const ConstantArrayType *CAType = dyn_cast<ConstantArrayType>(AType)) {
      NumElements = CAType->getZExtSize();
      // Simple heuristic so that we don't allocate a very large
      // initializer with many empty entries at the end.
      if (NumElements > ExpectedNumInits)
        NumElements = 0;
    }
  } else if (const VectorType *VType = CurrentObjectType->getAs<VectorType>()) {
    NumElements = VType->getNumElements();
  } else if (CurrentObjectType->isRecordType()) {
    NumElements = numStructUnionElements(CurrentObjectType);
  } else if (CurrentObjectType->isDependentType()) {
    NumElements = 1;
  }

  Result->reserveInits(SemaRef.Context, NumElements);

  return Result;
}

/// Update the initializer at index @p StructuredIndex within the
/// structured initializer list to the value @p expr.
void InitListChecker::UpdateStructuredListElement(InitListExpr *StructuredList,
                                                  unsigned &StructuredIndex,
                                                  Expr *expr) {
  // No structured initializer list to update
  if (!StructuredList)
    return;

  if (Expr *PrevInit = StructuredList->updateInit(SemaRef.Context,
                                                  StructuredIndex, expr)) {
    // This initializer overwrites a previous initializer.
    // No need to diagnose when `expr` is nullptr because a more relevant
    // diagnostic has already been issued and this diagnostic is potentially
    // noise.
    if (expr)
      diagnoseInitOverride(PrevInit, expr->getSourceRange());
  }

  ++StructuredIndex;
}

bool Sema::CanPerformAggregateInitializationForOverloadResolution(
    const InitializedEntity &Entity, InitListExpr *From) {
  QualType Type = Entity.getType();
  InitListChecker Check(*this, Entity, From, Type, /*VerifyOnly=*/true,
                        /*TreatUnavailableAsInvalid=*/false,
                        /*InOverloadResolution=*/true);
  return !Check.HadError();
}

/// Check that the given Index expression is a valid array designator
/// value. This is essentially just a wrapper around
/// VerifyIntegerConstantExpression that also checks for negative values
/// and produces a reasonable diagnostic if there is a
/// failure. Returns the index expression, possibly with an implicit cast
/// added, on success.  If everything went okay, Value will receive the
/// value of the constant expression.
static ExprResult
CheckArrayDesignatorExpr(Sema &S, Expr *Index, llvm::APSInt &Value) {
  SourceLocation Loc = Index->getBeginLoc();

  // Make sure this is an integer constant expression.
  ExprResult Result =
      S.VerifyIntegerConstantExpression(Index, &Value, Sema::AllowFold);
  if (Result.isInvalid())
    return Result;

  if (Value.isSigned() && Value.isNegative())
    return S.Diag(Loc, diag::err_array_designator_negative)
           << toString(Value, 10) << Index->getSourceRange();

  Value.setIsUnsigned(true);
  return Result;
}

ExprResult Sema::ActOnDesignatedInitializer(Designation &Desig,
                                            SourceLocation EqualOrColonLoc,
                                            bool GNUSyntax,
                                            ExprResult Init) {
  typedef DesignatedInitExpr::Designator ASTDesignator;

  bool Invalid = false;
  SmallVector<ASTDesignator, 32> Designators;
  SmallVector<Expr *, 32> InitExpressions;

  // Build designators and check array designator expressions.
  for (unsigned Idx = 0; Idx < Desig.getNumDesignators(); ++Idx) {
    const Designator &D = Desig.getDesignator(Idx);

    if (D.isFieldDesignator()) {
      Designators.push_back(ASTDesignator::CreateFieldDesignator(
          D.getFieldDecl(), D.getDotLoc(), D.getFieldLoc()));
    } else if (D.isArrayDesignator()) {
      Expr *Index = static_cast<Expr *>(D.getArrayIndex());
      llvm::APSInt IndexValue;
      if (!Index->isTypeDependent() && !Index->isValueDependent())
        Index = CheckArrayDesignatorExpr(*this, Index, IndexValue).get();
      if (!Index)
        Invalid = true;
      else {
        Designators.push_back(ASTDesignator::CreateArrayDesignator(
            InitExpressions.size(), D.getLBracketLoc(), D.getRBracketLoc()));
        InitExpressions.push_back(Index);
      }
    } else if (D.isArrayRangeDesignator()) {
      Expr *StartIndex = static_cast<Expr *>(D.getArrayRangeStart());
      Expr *EndIndex = static_cast<Expr *>(D.getArrayRangeEnd());
      llvm::APSInt StartValue;
      llvm::APSInt EndValue;
      bool StartDependent = StartIndex->isTypeDependent() ||
                            StartIndex->isValueDependent();
      bool EndDependent = EndIndex->isTypeDependent() ||
                          EndIndex->isValueDependent();
      if (!StartDependent)
        StartIndex =
            CheckArrayDesignatorExpr(*this, StartIndex, StartValue).get();
      if (!EndDependent)
        EndIndex = CheckArrayDesignatorExpr(*this, EndIndex, EndValue).get();

      if (!StartIndex || !EndIndex)
        Invalid = true;
      else {
        // Make sure we're comparing values with the same bit width.
        if (StartDependent || EndDependent) {
          // Nothing to compute.
        } else if (StartValue.getBitWidth() > EndValue.getBitWidth())
          EndValue = EndValue.extend(StartValue.getBitWidth());
        else if (StartValue.getBitWidth() < EndValue.getBitWidth())
          StartValue = StartValue.extend(EndValue.getBitWidth());

        if (!StartDependent && !EndDependent && EndValue < StartValue) {
          Diag(D.getEllipsisLoc(), diag::err_array_designator_empty_range)
            << toString(StartValue, 10) << toString(EndValue, 10)
            << StartIndex->getSourceRange() << EndIndex->getSourceRange();
          Invalid = true;
        } else {
          Designators.push_back(ASTDesignator::CreateArrayRangeDesignator(
              InitExpressions.size(), D.getLBracketLoc(), D.getEllipsisLoc(),
              D.getRBracketLoc()));
          InitExpressions.push_back(StartIndex);
          InitExpressions.push_back(EndIndex);
        }
      }
    }
  }

  if (Invalid || Init.isInvalid())
    return ExprError();

  return DesignatedInitExpr::Create(Context, Designators, InitExpressions,
                                    EqualOrColonLoc, GNUSyntax,
                                    Init.getAs<Expr>());
}

//===----------------------------------------------------------------------===//
// Initialization entity
//===----------------------------------------------------------------------===//

InitializedEntity::InitializedEntity(ASTContext &Context, unsigned Index,
                                     const InitializedEntity &Parent)
  : Parent(&Parent), Index(Index)
{
  if (const ArrayType *AT = Context.getAsArrayType(Parent.getType())) {
    Kind = EK_ArrayElement;
    Type = AT->getElementType();
  } else if (const VectorType *VT = Parent.getType()->getAs<VectorType>()) {
    Kind = EK_VectorElement;
    Type = VT->getElementType();
  } else {
    const ComplexType *CT = Parent.getType()->getAs<ComplexType>();
    assert(CT && "Unexpected type");
    Kind = EK_ComplexElement;
    Type = CT->getElementType();
  }
}

InitializedEntity
InitializedEntity::InitializeBase(ASTContext &Context,
                                  const CXXBaseSpecifier *Base,
                                  bool IsInheritedVirtualBase,
                                  const InitializedEntity *Parent) {
  InitializedEntity Result;
  Result.Kind = EK_Base;
  Result.Parent = Parent;
  Result.Base = {Base, IsInheritedVirtualBase};
  Result.Type = Base->getType();
  return Result;
}

DeclarationName InitializedEntity::getName() const {
  switch (getKind()) {
  case EK_Parameter:
  case EK_Parameter_CF_Audited: {
    ParmVarDecl *D = Parameter.getPointer();
    return (D ? D->getDeclName() : DeclarationName());
  }

  case EK_Variable:
  case EK_Member:
  case EK_ParenAggInitMember:
  case EK_Binding:
  case EK_TemplateParameter:
    return Variable.VariableOrMember->getDeclName();

  case EK_LambdaCapture:
    return DeclarationName(Capture.VarID);

  case EK_Result:
  case EK_StmtExprResult:
  case EK_Exception:
  case EK_New:
  case EK_Temporary:
  case EK_Base:
  case EK_Delegating:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
  case EK_BlockElement:
  case EK_LambdaToBlockConversionBlockElement:
  case EK_CompoundLiteralInit:
  case EK_RelatedResult:
    return DeclarationName();
  }

  llvm_unreachable("Invalid EntityKind!");
}

ValueDecl *InitializedEntity::getDecl() const {
  switch (getKind()) {
  case EK_Variable:
  case EK_Member:
  case EK_ParenAggInitMember:
  case EK_Binding:
  case EK_TemplateParameter:
    return Variable.VariableOrMember;

  case EK_Parameter:
  case EK_Parameter_CF_Audited:
    return Parameter.getPointer();

  case EK_Result:
  case EK_StmtExprResult:
  case EK_Exception:
  case EK_New:
  case EK_Temporary:
  case EK_Base:
  case EK_Delegating:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
  case EK_BlockElement:
  case EK_LambdaToBlockConversionBlockElement:
  case EK_LambdaCapture:
  case EK_CompoundLiteralInit:
  case EK_RelatedResult:
    return nullptr;
  }

  llvm_unreachable("Invalid EntityKind!");
}

bool InitializedEntity::allowsNRVO() const {
  switch (getKind()) {
  case EK_Result:
  case EK_Exception:
    return LocAndNRVO.NRVO;

  case EK_StmtExprResult:
  case EK_Variable:
  case EK_Parameter:
  case EK_Parameter_CF_Audited:
  case EK_TemplateParameter:
  case EK_Member:
  case EK_ParenAggInitMember:
  case EK_Binding:
  case EK_New:
  case EK_Temporary:
  case EK_CompoundLiteralInit:
  case EK_Base:
  case EK_Delegating:
  case EK_ArrayElement:
  case EK_VectorElement:
  case EK_ComplexElement:
  case EK_BlockElement:
  case EK_LambdaToBlockConversionBlockElement:
  case EK_LambdaCapture:
  case EK_RelatedResult:
    break;
  }

  return false;
}

unsigned InitializedEntity::dumpImpl(raw_ostream &OS) const {
  assert(getParent() != this);
  unsigned Depth = getParent() ? getParent()->dumpImpl(OS) : 0;
  for (unsigned I = 0; I != Depth; ++I)
    OS << "`-";

  switch (getKind()) {
  case EK_Variable: OS << "Variable"; break;
  case EK_Parameter: OS << "Parameter"; break;
  case EK_Parameter_CF_Audited: OS << "CF audited function Parameter";
    break;
  case EK_TemplateParameter: OS << "TemplateParameter"; break;
  case EK_Result: OS << "Result"; break;
  case EK_StmtExprResult: OS << "StmtExprResult"; break;
  case EK_Exception: OS << "Exception"; break;
  case EK_Member:
  case EK_ParenAggInitMember:
    OS << "Member";
    break;
  case EK_Binding: OS << "Binding"; break;
  case EK_New: OS << "New"; break;
  case EK_Temporary: OS << "Temporary"; break;
  case EK_CompoundLiteralInit: OS << "CompoundLiteral";break;
  case EK_RelatedResult: OS << "RelatedResult"; break;
  case EK_Base: OS << "Base"; break;
  case EK_Delegating: OS << "Delegating"; break;
  case EK_ArrayElement: OS << "ArrayElement " << Index; break;
  case EK_VectorElement: OS << "VectorElement " << Index; break;
  case EK_ComplexElement: OS << "ComplexElement " << Index; break;
  case EK_BlockElement: OS << "Block"; break;
  case EK_LambdaToBlockConversionBlockElement:
    OS << "Block (lambda)";
    break;
  case EK_LambdaCapture:
    OS << "LambdaCapture ";
    OS << DeclarationName(Capture.VarID);
    break;
  }

  if (auto *D = getDecl()) {
    OS << " ";
    D->printQualifiedName(OS);
  }

  OS << " '" << getType() << "'\n";

  return Depth + 1;
}

LLVM_DUMP_METHOD void InitializedEntity::dump() const {
  dumpImpl(llvm::errs());
}

//===----------------------------------------------------------------------===//
// Initialization sequence
//===----------------------------------------------------------------------===//

void InitializationSequence::Step::Destroy() {
  switch (Kind) {
  case SK_ResolveAddressOfOverloadedFunction:
  case SK_CastDerivedToBasePRValue:
  case SK_CastDerivedToBaseXValue:
  case SK_CastDerivedToBaseLValue:
  case SK_BindReference:
  case SK_BindReferenceToTemporary:
  case SK_FinalCopy:
  case SK_ExtraneousCopyToTemporary:
  case SK_UserConversion:
  case SK_QualificationConversionPRValue:
  case SK_QualificationConversionXValue:
  case SK_QualificationConversionLValue:
  case SK_FunctionReferenceConversion:
  case SK_AtomicConversion:
  case SK_ListInitialization:
  case SK_UnwrapInitList:
  case SK_RewrapInitList:
  case SK_ConstructorInitialization:
  case SK_ConstructorInitializationFromList:
  case SK_ZeroInitialization:
  case SK_CAssignment:
  case SK_StringInit:
  case SK_ObjCObjectConversion:
  case SK_ArrayLoopIndex:
  case SK_ArrayLoopInit:
  case SK_ArrayInit:
  case SK_GNUArrayInit:
  case SK_ParenthesizedArrayInit:
  case SK_PassByIndirectCopyRestore:
  case SK_PassByIndirectRestore:
  case SK_ProduceObjCObject:
  case SK_StdInitializerList:
  case SK_StdInitializerListConstructorCall:
  case SK_OCLSamplerInit:
  case SK_OCLZeroOpaqueType:
  case SK_ParenthesizedListInit:
    break;

  case SK_ConversionSequence:
  case SK_ConversionSequenceNoNarrowing:
    delete ICS;
  }
}

bool InitializationSequence::isDirectReferenceBinding() const {
  // There can be some lvalue adjustments after the SK_BindReference step.
  for (const Step &S : llvm::reverse(Steps)) {
    if (S.Kind == SK_BindReference)
      return true;
    if (S.Kind == SK_BindReferenceToTemporary)
      return false;
  }
  return false;
}

bool InitializationSequence::isAmbiguous() const {
  if (!Failed())
    return false;

  switch (getFailureKind()) {
  case FK_TooManyInitsForReference:
  case FK_ParenthesizedListInitForReference:
  case FK_ArrayNeedsInitList:
  case FK_ArrayNeedsInitListOrStringLiteral:
  case FK_ArrayNeedsInitListOrWideStringLiteral:
  case FK_NarrowStringIntoWideCharArray:
  case FK_WideStringIntoCharArray:
  case FK_IncompatWideStringIntoWideChar:
  case FK_PlainStringIntoUTF8Char:
  case FK_UTF8StringIntoPlainChar:
  case FK_AddressOfOverloadFailed: // FIXME: Could do better
  case FK_NonConstLValueReferenceBindingToTemporary:
  case FK_NonConstLValueReferenceBindingToBitfield:
  case FK_NonConstLValueReferenceBindingToVectorElement:
  case FK_NonConstLValueReferenceBindingToMatrixElement:
  case FK_NonConstLValueReferenceBindingToUnrelated:
  case FK_RValueReferenceBindingToLValue:
  case FK_ReferenceAddrspaceMismatchTemporary:
  case FK_ReferenceInitDropsQualifiers:
  case FK_ReferenceInitFailed:
  case FK_ConversionFailed:
  case FK_ConversionFromPropertyFailed:
  case FK_TooManyInitsForScalar:
  case FK_ParenthesizedListInitForScalar:
  case FK_ReferenceBindingToInitList:
  case FK_InitListBadDestinationType:
  case FK_DefaultInitOfConst:
  case FK_Incomplete:
  case FK_ArrayTypeMismatch:
  case FK_NonConstantArrayInit:
  case FK_ListInitializationFailed:
  case FK_VariableLengthArrayHasInitializer:
  case FK_PlaceholderType:
  case FK_ExplicitConstructor:
  case FK_AddressOfUnaddressableFunction:
  case FK_ParenthesizedListInitFailed:
  case FK_DesignatedInitForNonAggregate:
    return false;

  case FK_ReferenceInitOverloadFailed:
  case FK_UserConversionOverloadFailed:
  case FK_ConstructorOverloadFailed:
  case FK_ListConstructorOverloadFailed:
    return FailedOverloadResult == OR_Ambiguous;
  }

  llvm_unreachable("Invalid EntityKind!");
}

bool InitializationSequence::isConstructorInitialization() const {
  return !Steps.empty() && Steps.back().Kind == SK_ConstructorInitialization;
}

void
InitializationSequence
::AddAddressOverloadResolutionStep(FunctionDecl *Function,
                                   DeclAccessPair Found,
                                   bool HadMultipleCandidates) {
  Step S;
  S.Kind = SK_ResolveAddressOfOverloadedFunction;
  S.Type = Function->getType();
  S.Function.HadMultipleCandidates = HadMultipleCandidates;
  S.Function.Function = Function;
  S.Function.FoundDecl = Found;
  Steps.push_back(S);
}

void InitializationSequence::AddDerivedToBaseCastStep(QualType BaseType,
                                                      ExprValueKind VK) {
  Step S;
  switch (VK) {
  case VK_PRValue:
    S.Kind = SK_CastDerivedToBasePRValue;
    break;
  case VK_XValue: S.Kind = SK_CastDerivedToBaseXValue; break;
  case VK_LValue: S.Kind = SK_CastDerivedToBaseLValue; break;
  }
  S.Type = BaseType;
  Steps.push_back(S);
}

void InitializationSequence::AddReferenceBindingStep(QualType T,
                                                     bool BindingTemporary) {
  Step S;
  S.Kind = BindingTemporary? SK_BindReferenceToTemporary : SK_BindReference;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddFinalCopy(QualType T) {
  Step S;
  S.Kind = SK_FinalCopy;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddExtraneousCopyToTemporary(QualType T) {
  Step S;
  S.Kind = SK_ExtraneousCopyToTemporary;
  S.Type = T;
  Steps.push_back(S);
}

void
InitializationSequence::AddUserConversionStep(FunctionDecl *Function,
                                              DeclAccessPair FoundDecl,
                                              QualType T,
                                              bool HadMultipleCandidates) {
  Step S;
  S.Kind = SK_UserConversion;
  S.Type = T;
  S.Function.HadMultipleCandidates = HadMultipleCandidates;
  S.Function.Function = Function;
  S.Function.FoundDecl = FoundDecl;
  Steps.push_back(S);
}

void InitializationSequence::AddQualificationConversionStep(QualType Ty,
                                                            ExprValueKind VK) {
  Step S;
  S.Kind = SK_QualificationConversionPRValue; // work around a gcc warning
  switch (VK) {
  case VK_PRValue:
    S.Kind = SK_QualificationConversionPRValue;
    break;
  case VK_XValue:
    S.Kind = SK_QualificationConversionXValue;
    break;
  case VK_LValue:
    S.Kind = SK_QualificationConversionLValue;
    break;
  }
  S.Type = Ty;
  Steps.push_back(S);
}

void InitializationSequence::AddFunctionReferenceConversionStep(QualType Ty) {
  Step S;
  S.Kind = SK_FunctionReferenceConversion;
  S.Type = Ty;
  Steps.push_back(S);
}

void InitializationSequence::AddAtomicConversionStep(QualType Ty) {
  Step S;
  S.Kind = SK_AtomicConversion;
  S.Type = Ty;
  Steps.push_back(S);
}

void InitializationSequence::AddConversionSequenceStep(
    const ImplicitConversionSequence &ICS, QualType T,
    bool TopLevelOfInitList) {
  Step S;
  S.Kind = TopLevelOfInitList ? SK_ConversionSequenceNoNarrowing
                              : SK_ConversionSequence;
  S.Type = T;
  S.ICS = new ImplicitConversionSequence(ICS);
  Steps.push_back(S);
}

void InitializationSequence::AddListInitializationStep(QualType T) {
  Step S;
  S.Kind = SK_ListInitialization;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddConstructorInitializationStep(
    DeclAccessPair FoundDecl, CXXConstructorDecl *Constructor, QualType T,
    bool HadMultipleCandidates, bool FromInitList, bool AsInitList) {
  Step S;
  S.Kind = FromInitList ? AsInitList ? SK_StdInitializerListConstructorCall
                                     : SK_ConstructorInitializationFromList
                        : SK_ConstructorInitialization;
  S.Type = T;
  S.Function.HadMultipleCandidates = HadMultipleCandidates;
  S.Function.Function = Constructor;
  S.Function.FoundDecl = FoundDecl;
  Steps.push_back(S);
}

void InitializationSequence::AddZeroInitializationStep(QualType T) {
  Step S;
  S.Kind = SK_ZeroInitialization;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddCAssignmentStep(QualType T) {
  Step S;
  S.Kind = SK_CAssignment;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddStringInitStep(QualType T) {
  Step S;
  S.Kind = SK_StringInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddObjCObjectConversionStep(QualType T) {
  Step S;
  S.Kind = SK_ObjCObjectConversion;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddArrayInitStep(QualType T, bool IsGNUExtension) {
  Step S;
  S.Kind = IsGNUExtension ? SK_GNUArrayInit : SK_ArrayInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddArrayInitLoopStep(QualType T, QualType EltT) {
  Step S;
  S.Kind = SK_ArrayLoopIndex;
  S.Type = EltT;
  Steps.insert(Steps.begin(), S);

  S.Kind = SK_ArrayLoopInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddParenthesizedArrayInitStep(QualType T) {
  Step S;
  S.Kind = SK_ParenthesizedArrayInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddPassByIndirectCopyRestoreStep(QualType type,
                                                              bool shouldCopy) {
  Step s;
  s.Kind = (shouldCopy ? SK_PassByIndirectCopyRestore
                       : SK_PassByIndirectRestore);
  s.Type = type;
  Steps.push_back(s);
}

void InitializationSequence::AddProduceObjCObjectStep(QualType T) {
  Step S;
  S.Kind = SK_ProduceObjCObject;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddStdInitializerListConstructionStep(QualType T) {
  Step S;
  S.Kind = SK_StdInitializerList;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddOCLSamplerInitStep(QualType T) {
  Step S;
  S.Kind = SK_OCLSamplerInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddOCLZeroOpaqueTypeStep(QualType T) {
  Step S;
  S.Kind = SK_OCLZeroOpaqueType;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::AddParenthesizedListInitStep(QualType T) {
  Step S;
  S.Kind = SK_ParenthesizedListInit;
  S.Type = T;
  Steps.push_back(S);
}

void InitializationSequence::RewrapReferenceInitList(QualType T,
                                                     InitListExpr *Syntactic) {
  assert(Syntactic->getNumInits() == 1 &&
         "Can only rewrap trivial init lists.");
  Step S;
  S.Kind = SK_UnwrapInitList;
  S.Type = Syntactic->getInit(0)->getType();
  Steps.insert(Steps.begin(), S);

  S.Kind = SK_RewrapInitList;
  S.Type = T;
  S.WrappingSyntacticList = Syntactic;
  Steps.push_back(S);
}

void InitializationSequence::SetOverloadFailure(FailureKind Failure,
                                                OverloadingResult Result) {
  setSequenceKind(FailedSequence);
  this->Failure = Failure;
  this->FailedOverloadResult = Result;
}

//===----------------------------------------------------------------------===//
// Attempt initialization
//===----------------------------------------------------------------------===//

/// Tries to add a zero initializer. Returns true if that worked.
static bool
maybeRecoverWithZeroInitialization(Sema &S, InitializationSequence &Sequence,
                                   const InitializedEntity &Entity) {
  if (Entity.getKind() != InitializedEntity::EK_Variable)
    return false;

  VarDecl *VD = cast<VarDecl>(Entity.getDecl());
  if (VD->getInit() || VD->getEndLoc().isMacroID())
    return false;

  QualType VariableTy = VD->getType().getCanonicalType();
  SourceLocation Loc = S.getLocForEndOfToken(VD->getEndLoc());
  std::string Init = S.getFixItZeroInitializerForType(VariableTy, Loc);
  if (!Init.empty()) {
    Sequence.AddZeroInitializationStep(Entity.getType());
    Sequence.SetZeroInitializationFixit(Init, Loc);
    return true;
  }
  return false;
}

static void MaybeProduceObjCObject(Sema &S,
                                   InitializationSequence &Sequence,
                                   const InitializedEntity &Entity) {
  if (!S.getLangOpts().ObjCAutoRefCount) return;

  /// When initializing a parameter, produce the value if it's marked
  /// __attribute__((ns_consumed)).
  if (Entity.isParameterKind()) {
    if (!Entity.isParameterConsumed())
      return;

    assert(Entity.getType()->isObjCRetainableType() &&
           "consuming an object of unretainable type?");
    Sequence.AddProduceObjCObjectStep(Entity.getType());

  /// When initializing a return value, if the return type is a
  /// retainable type, then returns need to immediately retain the
  /// object.  If an autorelease is required, it will be done at the
  /// last instant.
  } else if (Entity.getKind() == InitializedEntity::EK_Result ||
             Entity.getKind() == InitializedEntity::EK_StmtExprResult) {
    if (!Entity.getType()->isObjCRetainableType())
      return;

    Sequence.AddProduceObjCObjectStep(Entity.getType());
  }
}

static void TryListInitialization(Sema &S,
                                  const InitializedEntity &Entity,
                                  const InitializationKind &Kind,
                                  InitListExpr *InitList,
                                  InitializationSequence &Sequence,
                                  bool TreatUnavailableAsInvalid);

/// When initializing from init list via constructor, handle
/// initialization of an object of type std::initializer_list<T>.
///
/// \return true if we have handled initialization of an object of type
/// std::initializer_list<T>, false otherwise.
static bool TryInitializerListConstruction(Sema &S,
                                           InitListExpr *List,
                                           QualType DestType,
                                           InitializationSequence &Sequence,
                                           bool TreatUnavailableAsInvalid) {
  QualType E;
  if (!S.isStdInitializerList(DestType, &E))
    return false;

  if (!S.isCompleteType(List->getExprLoc(), E)) {
    Sequence.setIncompleteTypeFailure(E);
    return true;
  }

  // Try initializing a temporary array from the init list.
  QualType ArrayType = S.Context.getConstantArrayType(
      E.withConst(),
      llvm::APInt(S.Context.getTypeSize(S.Context.getSizeType()),
                  List->getNumInits()),
      nullptr, clang::ArraySizeModifier::Normal, 0);
  InitializedEntity HiddenArray =
      InitializedEntity::InitializeTemporary(ArrayType);
  InitializationKind Kind = InitializationKind::CreateDirectList(
      List->getExprLoc(), List->getBeginLoc(), List->getEndLoc());
  TryListInitialization(S, HiddenArray, Kind, List, Sequence,
                        TreatUnavailableAsInvalid);
  if (Sequence)
    Sequence.AddStdInitializerListConstructionStep(DestType);
  return true;
}

/// Determine if the constructor has the signature of a copy or move
/// constructor for the type T of the class in which it was found. That is,
/// determine if its first parameter is of type T or reference to (possibly
/// cv-qualified) T.
static bool hasCopyOrMoveCtorParam(ASTContext &Ctx,
                                   const ConstructorInfo &Info) {
  if (Info.Constructor->getNumParams() == 0)
    return false;

  QualType ParmT =
      Info.Constructor->getParamDecl(0)->getType().getNonReferenceType();
  QualType ClassT =
      Ctx.getRecordType(cast<CXXRecordDecl>(Info.FoundDecl->getDeclContext()));

  return Ctx.hasSameUnqualifiedType(ParmT, ClassT);
}

static OverloadingResult ResolveConstructorOverload(
    Sema &S, SourceLocation DeclLoc, MultiExprArg Args,
    OverloadCandidateSet &CandidateSet, QualType DestType,
    DeclContext::lookup_result Ctors, OverloadCandidateSet::iterator &Best,
    bool CopyInitializing, bool AllowExplicit, bool OnlyListConstructors,
    bool IsListInit, bool RequireActualConstructor,
    bool SecondStepOfCopyInit = false) {
  CandidateSet.clear(OverloadCandidateSet::CSK_InitByConstructor);
  CandidateSet.setDestAS(DestType.getQualifiers().getAddressSpace());

  for (NamedDecl *D : Ctors) {
    auto Info = getConstructorInfo(D);
    if (!Info.Constructor || Info.Constructor->isInvalidDecl())
      continue;

    if (OnlyListConstructors && !S.isInitListConstructor(Info.Constructor))
      continue;

    // C++11 [over.best.ics]p4:
    //   ... and the constructor or user-defined conversion function is a
    //   candidate by
    //   - 13.3.1.3, when the argument is the temporary in the second step
    //     of a class copy-initialization, or
    //   - 13.3.1.4, 13.3.1.5, or 13.3.1.6 (in all cases), [not handled here]
    //   - the second phase of 13.3.1.7 when the initializer list has exactly
    //     one element that is itself an initializer list, and the target is
    //     the first parameter of a constructor of class X, and the conversion
    //     is to X or reference to (possibly cv-qualified X),
    //   user-defined conversion sequences are not considered.
    bool SuppressUserConversions =
        SecondStepOfCopyInit ||
        (IsListInit && Args.size() == 1 && isa<InitListExpr>(Args[0]) &&
         hasCopyOrMoveCtorParam(S.Context, Info));

    if (Info.ConstructorTmpl)
      S.AddTemplateOverloadCandidate(
          Info.ConstructorTmpl, Info.FoundDecl,
          /*ExplicitArgs*/ nullptr, Args, CandidateSet, SuppressUserConversions,
          /*PartialOverloading=*/false, AllowExplicit);
    else {
      // C++ [over.match.copy]p1:
      //   - When initializing a temporary to be bound to the first parameter
      //     of a constructor [for type T] that takes a reference to possibly
      //     cv-qualified T as its first argument, called with a single
      //     argument in the context of direct-initialization, explicit
      //     conversion functions are also considered.
      // FIXME: What if a constructor template instantiates to such a signature?
      bool AllowExplicitConv = AllowExplicit && !CopyInitializing &&
                               Args.size() == 1 &&
                               hasCopyOrMoveCtorParam(S.Context, Info);
      S.AddOverloadCandidate(Info.Constructor, Info.FoundDecl, Args,
                             CandidateSet, SuppressUserConversions,
                             /*PartialOverloading=*/false, AllowExplicit,
                             AllowExplicitConv);
    }
  }

  // FIXME: Work around a bug in C++17 guaranteed copy elision.
  //
  // When initializing an object of class type T by constructor
  // ([over.match.ctor]) or by list-initialization ([over.match.list])
  // from a single expression of class type U, conversion functions of
  // U that convert to the non-reference type cv T are candidates.
  // Explicit conversion functions are only candidates during
  // direct-initialization.
  //
  // Note: SecondStepOfCopyInit is only ever true in this case when
  // evaluating whether to produce a C++98 compatibility warning.
  if (S.getLangOpts().CPlusPlus17 && Args.size() == 1 &&
      !RequireActualConstructor && !SecondStepOfCopyInit) {
    Expr *Initializer = Args[0];
    auto *SourceRD = Initializer->getType()->getAsCXXRecordDecl();
    if (SourceRD && S.isCompleteType(DeclLoc, Initializer->getType())) {
      const auto &Conversions = SourceRD->getVisibleConversionFunctions();
      for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
        NamedDecl *D = *I;
        CXXRecordDecl *ActingDC = cast<CXXRecordDecl>(D->getDeclContext());
        D = D->getUnderlyingDecl();

        FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(D);
        CXXConversionDecl *Conv;
        if (ConvTemplate)
          Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
        else
          Conv = cast<CXXConversionDecl>(D);

        if (ConvTemplate)
          S.AddTemplateConversionCandidate(
              ConvTemplate, I.getPair(), ActingDC, Initializer, DestType,
              CandidateSet, AllowExplicit, AllowExplicit,
              /*AllowResultConversion*/ false);
        else
          S.AddConversionCandidate(Conv, I.getPair(), ActingDC, Initializer,
                                   DestType, CandidateSet, AllowExplicit,
                                   AllowExplicit,
                                   /*AllowResultConversion*/ false);
      }
    }
  }

  // Perform overload resolution and return the result.
  return CandidateSet.BestViableFunction(S, DeclLoc, Best);
}

/// Attempt initialization by constructor (C++ [dcl.init]), which
/// enumerates the constructors of the initialized entity and performs overload
/// resolution to select the best.
/// \param DestType       The destination class type.
/// \param DestArrayType  The destination type, which is either DestType or
///                       a (possibly multidimensional) array of DestType.
/// \param IsListInit     Is this list-initialization?
/// \param IsInitListCopy Is this non-list-initialization resulting from a
///                       list-initialization from {x} where x is the same
///                       type as the entity?
static void TryConstructorInitialization(Sema &S,
                                         const InitializedEntity &Entity,
                                         const InitializationKind &Kind,
                                         MultiExprArg Args, QualType DestType,
                                         QualType DestArrayType,
                                         InitializationSequence &Sequence,
                                         bool IsListInit = false,
                                         bool IsInitListCopy = false) {
  assert(((!IsListInit && !IsInitListCopy) ||
          (Args.size() == 1 && isa<InitListExpr>(Args[0]))) &&
         "IsListInit/IsInitListCopy must come with a single initializer list "
         "argument.");
  InitListExpr *ILE =
      (IsListInit || IsInitListCopy) ? cast<InitListExpr>(Args[0]) : nullptr;
  MultiExprArg UnwrappedArgs =
      ILE ? MultiExprArg(ILE->getInits(), ILE->getNumInits()) : Args;

  // The type we're constructing needs to be complete.
  if (!S.isCompleteType(Kind.getLocation(), DestType)) {
    Sequence.setIncompleteTypeFailure(DestType);
    return;
  }

  bool RequireActualConstructor =
      !(Entity.getKind() != InitializedEntity::EK_Base &&
        Entity.getKind() != InitializedEntity::EK_Delegating &&
        Entity.getKind() !=
            InitializedEntity::EK_LambdaToBlockConversionBlockElement);

  // C++17 [dcl.init]p17:
  //     - If the initializer expression is a prvalue and the cv-unqualified
  //       version of the source type is the same class as the class of the
  //       destination, the initializer expression is used to initialize the
  //       destination object.
  // Per DR (no number yet), this does not apply when initializing a base
  // class or delegating to another constructor from a mem-initializer.
  // ObjC++: Lambda captured by the block in the lambda to block conversion
  // should avoid copy elision.
  if (S.getLangOpts().CPlusPlus17 && !RequireActualConstructor &&
      UnwrappedArgs.size() == 1 && UnwrappedArgs[0]->isPRValue() &&
      S.Context.hasSameUnqualifiedType(UnwrappedArgs[0]->getType(), DestType)) {
    // Convert qualifications if necessary.
    Sequence.AddQualificationConversionStep(DestType, VK_PRValue);
    if (ILE)
      Sequence.RewrapReferenceInitList(DestType, ILE);
    return;
  }

  const RecordType *DestRecordType = DestType->getAs<RecordType>();
  assert(DestRecordType && "Constructor initialization requires record type");
  CXXRecordDecl *DestRecordDecl
    = cast<CXXRecordDecl>(DestRecordType->getDecl());

  // Build the candidate set directly in the initialization sequence
  // structure, so that it will persist if we fail.
  OverloadCandidateSet &CandidateSet = Sequence.getFailedCandidateSet();

  // Determine whether we are allowed to call explicit constructors or
  // explicit conversion operators.
  bool AllowExplicit = Kind.AllowExplicit() || IsListInit;
  bool CopyInitialization = Kind.getKind() == InitializationKind::IK_Copy;

  //   - Otherwise, if T is a class type, constructors are considered. The
  //     applicable constructors are enumerated, and the best one is chosen
  //     through overload resolution.
  DeclContext::lookup_result Ctors = S.LookupConstructors(DestRecordDecl);

  OverloadingResult Result = OR_No_Viable_Function;
  OverloadCandidateSet::iterator Best;
  bool AsInitializerList = false;

  // C++11 [over.match.list]p1, per DR1467:
  //   When objects of non-aggregate type T are list-initialized, such that
  //   8.5.4 [dcl.init.list] specifies that overload resolution is performed
  //   according to the rules in this section, overload resolution selects
  //   the constructor in two phases:
  //
  //   - Initially, the candidate functions are the initializer-list
  //     constructors of the class T and the argument list consists of the
  //     initializer list as a single argument.
  if (IsListInit) {
    AsInitializerList = true;

    // If the initializer list has no elements and T has a default constructor,
    // the first phase is omitted.
    if (!(UnwrappedArgs.empty() && S.LookupDefaultConstructor(DestRecordDecl)))
      Result = ResolveConstructorOverload(
          S, Kind.getLocation(), Args, CandidateSet, DestType, Ctors, Best,
          CopyInitialization, AllowExplicit,
          /*OnlyListConstructors=*/true, IsListInit, RequireActualConstructor);
  }

  // C++11 [over.match.list]p1:
  //   - If no viable initializer-list constructor is found, overload resolution
  //     is performed again, where the candidate functions are all the
  //     constructors of the class T and the argument list consists of the
  //     elements of the initializer list.
  if (Result == OR_No_Viable_Function) {
    AsInitializerList = false;
    Result = ResolveConstructorOverload(
        S, Kind.getLocation(), UnwrappedArgs, CandidateSet, DestType, Ctors,
        Best, CopyInitialization, AllowExplicit,
        /*OnlyListConstructors=*/false, IsListInit, RequireActualConstructor);
  }
  if (Result) {
    Sequence.SetOverloadFailure(
        IsListInit ? InitializationSequence::FK_ListConstructorOverloadFailed
                   : InitializationSequence::FK_ConstructorOverloadFailed,
        Result);

    if (Result != OR_Deleted)
      return;
  }

  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  // In C++17, ResolveConstructorOverload can select a conversion function
  // instead of a constructor.
  if (auto *CD = dyn_cast<CXXConversionDecl>(Best->Function)) {
    // Add the user-defined conversion step that calls the conversion function.
    QualType ConvType = CD->getConversionType();
    assert(S.Context.hasSameUnqualifiedType(ConvType, DestType) &&
           "should not have selected this conversion function");
    Sequence.AddUserConversionStep(CD, Best->FoundDecl, ConvType,
                                   HadMultipleCandidates);
    if (!S.Context.hasSameType(ConvType, DestType))
      Sequence.AddQualificationConversionStep(DestType, VK_PRValue);
    if (IsListInit)
      Sequence.RewrapReferenceInitList(Entity.getType(), ILE);
    return;
  }

  CXXConstructorDecl *CtorDecl = cast<CXXConstructorDecl>(Best->Function);
  if (Result != OR_Deleted) {
    // C++11 [dcl.init]p6:
    //   If a program calls for the default initialization of an object
    //   of a const-qualified type T, T shall be a class type with a
    //   user-provided default constructor.
    // C++ core issue 253 proposal:
    //   If the implicit default constructor initializes all subobjects, no
    //   initializer should be required.
    // The 253 proposal is for example needed to process libstdc++ headers
    // in 5.x.
    if (Kind.getKind() == InitializationKind::IK_Default &&
        Entity.getType().isConstQualified()) {
      if (!CtorDecl->getParent()->allowConstDefaultInit()) {
        if (!maybeRecoverWithZeroInitialization(S, Sequence, Entity))
          Sequence.SetFailed(InitializationSequence::FK_DefaultInitOfConst);
        return;
      }
    }

    // C++11 [over.match.list]p1:
    //   In copy-list-initialization, if an explicit constructor is chosen, the
    //   initializer is ill-formed.
    if (IsListInit && !Kind.AllowExplicit() && CtorDecl->isExplicit()) {
      Sequence.SetFailed(InitializationSequence::FK_ExplicitConstructor);
      return;
    }
  }

  // [class.copy.elision]p3:
  // In some copy-initialization contexts, a two-stage overload resolution
  // is performed.
  // If the first overload resolution selects a deleted function, we also
  // need the initialization sequence to decide whether to perform the second
  // overload resolution.
  // For deleted functions in other contexts, there is no need to get the
  // initialization sequence.
  if (Result == OR_Deleted && Kind.getKind() != InitializationKind::IK_Copy)
    return;

  // Add the constructor initialization step. Any cv-qualification conversion is
  // subsumed by the initialization.
  Sequence.AddConstructorInitializationStep(
      Best->FoundDecl, CtorDecl, DestArrayType, HadMultipleCandidates,
      IsListInit | IsInitListCopy, AsInitializerList);
}

static bool
ResolveOverloadedFunctionForReferenceBinding(Sema &S,
                                             Expr *Initializer,
                                             QualType &SourceType,
                                             QualType &UnqualifiedSourceType,
                                             QualType UnqualifiedTargetType,
                                             InitializationSequence &Sequence) {
  if (S.Context.getCanonicalType(UnqualifiedSourceType) ==
        S.Context.OverloadTy) {
    DeclAccessPair Found;
    bool HadMultipleCandidates = false;
    if (FunctionDecl *Fn
        = S.ResolveAddressOfOverloadedFunction(Initializer,
                                               UnqualifiedTargetType,
                                               false, Found,
                                               &HadMultipleCandidates)) {
      Sequence.AddAddressOverloadResolutionStep(Fn, Found,
                                                HadMultipleCandidates);
      SourceType = Fn->getType();
      UnqualifiedSourceType = SourceType.getUnqualifiedType();
    } else if (!UnqualifiedTargetType->isRecordType()) {
      Sequence.SetFailed(InitializationSequence::FK_AddressOfOverloadFailed);
      return true;
    }
  }
  return false;
}

static void TryReferenceInitializationCore(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           Expr *Initializer,
                                           QualType cv1T1, QualType T1,
                                           Qualifiers T1Quals,
                                           QualType cv2T2, QualType T2,
                                           Qualifiers T2Quals,
                                           InitializationSequence &Sequence,
                                           bool TopLevelOfInitList);

static void TryValueInitialization(Sema &S,
                                   const InitializedEntity &Entity,
                                   const InitializationKind &Kind,
                                   InitializationSequence &Sequence,
                                   InitListExpr *InitList = nullptr);

/// Attempt list initialization of a reference.
static void TryReferenceListInitialization(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           InitListExpr *InitList,
                                           InitializationSequence &Sequence,
                                           bool TreatUnavailableAsInvalid) {
  // First, catch C++03 where this isn't possible.
  if (!S.getLangOpts().CPlusPlus11) {
    Sequence.SetFailed(InitializationSequence::FK_ReferenceBindingToInitList);
    return;
  }
  // Can't reference initialize a compound literal.
  if (Entity.getKind() == InitializedEntity::EK_CompoundLiteralInit) {
    Sequence.SetFailed(InitializationSequence::FK_ReferenceBindingToInitList);
    return;
  }

  QualType DestType = Entity.getType();
  QualType cv1T1 = DestType->castAs<ReferenceType>()->getPointeeType();
  Qualifiers T1Quals;
  QualType T1 = S.Context.getUnqualifiedArrayType(cv1T1, T1Quals);

  // Reference initialization via an initializer list works thus:
  // If the initializer list consists of a single element that is
  // reference-related to the referenced type, bind directly to that element
  // (possibly creating temporaries).
  // Otherwise, initialize a temporary with the initializer list and
  // bind to that.
  if (InitList->getNumInits() == 1) {
    Expr *Initializer = InitList->getInit(0);
    QualType cv2T2 = S.getCompletedType(Initializer);
    Qualifiers T2Quals;
    QualType T2 = S.Context.getUnqualifiedArrayType(cv2T2, T2Quals);

    // If this fails, creating a temporary wouldn't work either.
    if (ResolveOverloadedFunctionForReferenceBinding(S, Initializer, cv2T2, T2,
                                                     T1, Sequence))
      return;

    SourceLocation DeclLoc = Initializer->getBeginLoc();
    Sema::ReferenceCompareResult RefRelationship
      = S.CompareReferenceRelationship(DeclLoc, cv1T1, cv2T2);
    if (RefRelationship >= Sema::Ref_Related) {
      // Try to bind the reference here.
      TryReferenceInitializationCore(S, Entity, Kind, Initializer, cv1T1, T1,
                                     T1Quals, cv2T2, T2, T2Quals, Sequence,
                                     /*TopLevelOfInitList=*/true);
      if (Sequence)
        Sequence.RewrapReferenceInitList(cv1T1, InitList);
      return;
    }

    // Update the initializer if we've resolved an overloaded function.
    if (Sequence.step_begin() != Sequence.step_end())
      Sequence.RewrapReferenceInitList(cv1T1, InitList);
  }
  // Perform address space compatibility check.
  QualType cv1T1IgnoreAS = cv1T1;
  if (T1Quals.hasAddressSpace()) {
    Qualifiers T2Quals;
    (void)S.Context.getUnqualifiedArrayType(InitList->getType(), T2Quals);
    if (!T1Quals.isAddressSpaceSupersetOf(T2Quals)) {
      Sequence.SetFailed(
          InitializationSequence::FK_ReferenceInitDropsQualifiers);
      return;
    }
    // Ignore address space of reference type at this point and perform address
    // space conversion after the reference binding step.
    cv1T1IgnoreAS =
        S.Context.getQualifiedType(T1, T1Quals.withoutAddressSpace());
  }
  // Not reference-related. Create a temporary and bind to that.
  InitializedEntity TempEntity =
      InitializedEntity::InitializeTemporary(cv1T1IgnoreAS);

  TryListInitialization(S, TempEntity, Kind, InitList, Sequence,
                        TreatUnavailableAsInvalid);
  if (Sequence) {
    if (DestType->isRValueReferenceType() ||
        (T1Quals.hasConst() && !T1Quals.hasVolatile())) {
      if (S.getLangOpts().CPlusPlus20 &&
          isa<IncompleteArrayType>(T1->getUnqualifiedDesugaredType()) &&
          DestType->isRValueReferenceType()) {
        // C++20 [dcl.init.list]p3.10:
        // List-initialization of an object or reference of type T is defined as
        // follows:
        // ..., unless T is “reference to array of unknown bound of U”, in which
        // case the type of the prvalue is the type of x in the declaration U
        // x[] H, where H is the initializer list.
        Sequence.AddQualificationConversionStep(cv1T1, clang::VK_PRValue);
      }
      Sequence.AddReferenceBindingStep(cv1T1IgnoreAS,
                                       /*BindingTemporary=*/true);
      if (T1Quals.hasAddressSpace())
        Sequence.AddQualificationConversionStep(
            cv1T1, DestType->isRValueReferenceType() ? VK_XValue : VK_LValue);
    } else
      Sequence.SetFailed(
          InitializationSequence::FK_NonConstLValueReferenceBindingToTemporary);
  }
}

/// Attempt list initialization (C++0x [dcl.init.list])
static void TryListInitialization(Sema &S,
                                  const InitializedEntity &Entity,
                                  const InitializationKind &Kind,
                                  InitListExpr *InitList,
                                  InitializationSequence &Sequence,
                                  bool TreatUnavailableAsInvalid) {
  QualType DestType = Entity.getType();

  // C++ doesn't allow scalar initialization with more than one argument.
  // But C99 complex numbers are scalars and it makes sense there.
  if (S.getLangOpts().CPlusPlus && DestType->isScalarType() &&
      !DestType->isAnyComplexType() && InitList->getNumInits() > 1) {
    Sequence.SetFailed(InitializationSequence::FK_TooManyInitsForScalar);
    return;
  }
  if (DestType->isReferenceType()) {
    TryReferenceListInitialization(S, Entity, Kind, InitList, Sequence,
                                   TreatUnavailableAsInvalid);
    return;
  }

  if (DestType->isRecordType() &&
      !S.isCompleteType(InitList->getBeginLoc(), DestType)) {
    Sequence.setIncompleteTypeFailure(DestType);
    return;
  }

  // C++20 [dcl.init.list]p3:
  // - If the braced-init-list contains a designated-initializer-list, T shall
  //   be an aggregate class. [...] Aggregate initialization is performed.
  //
  // We allow arrays here too in order to support array designators.
  //
  // FIXME: This check should precede the handling of reference initialization.
  // We follow other compilers in allowing things like 'Aggr &&a = {.x = 1};'
  // as a tentative DR resolution.
  bool IsDesignatedInit = InitList->hasDesignatedInit();
  if (!DestType->isAggregateType() && IsDesignatedInit) {
    Sequence.SetFailed(
        InitializationSequence::FK_DesignatedInitForNonAggregate);
    return;
  }

  // C++11 [dcl.init.list]p3, per DR1467:
  // - If T is a class type and the initializer list has a single element of
  //   type cv U, where U is T or a class derived from T, the object is
  //   initialized from that element (by copy-initialization for
  //   copy-list-initialization, or by direct-initialization for
  //   direct-list-initialization).
  // - Otherwise, if T is a character array and the initializer list has a
  //   single element that is an appropriately-typed string literal
  //   (8.5.2 [dcl.init.string]), initialization is performed as described
  //   in that section.
  // - Otherwise, if T is an aggregate, [...] (continue below).
  if (S.getLangOpts().CPlusPlus11 && InitList->getNumInits() == 1 &&
      !IsDesignatedInit) {
    if (DestType->isRecordType()) {
      QualType InitType = InitList->getInit(0)->getType();
      if (S.Context.hasSameUnqualifiedType(InitType, DestType) ||
          S.IsDerivedFrom(InitList->getBeginLoc(), InitType, DestType)) {
        Expr *InitListAsExpr = InitList;
        TryConstructorInitialization(S, Entity, Kind, InitListAsExpr, DestType,
                                     DestType, Sequence,
                                     /*InitListSyntax*/false,
                                     /*IsInitListCopy*/true);
        return;
      }
    }
    if (const ArrayType *DestAT = S.Context.getAsArrayType(DestType)) {
      Expr *SubInit[1] = {InitList->getInit(0)};
      if (!isa<VariableArrayType>(DestAT) &&
          IsStringInit(SubInit[0], DestAT, S.Context) == SIF_None) {
        InitializationKind SubKind =
            Kind.getKind() == InitializationKind::IK_DirectList
                ? InitializationKind::CreateDirect(Kind.getLocation(),
                                                   InitList->getLBraceLoc(),
                                                   InitList->getRBraceLoc())
                : Kind;
        Sequence.InitializeFrom(S, Entity, SubKind, SubInit,
                                /*TopLevelOfInitList*/ true,
                                TreatUnavailableAsInvalid);

        // TryStringLiteralInitialization() (in InitializeFrom()) will fail if
        // the element is not an appropriately-typed string literal, in which
        // case we should proceed as in C++11 (below).
        if (Sequence) {
          Sequence.RewrapReferenceInitList(Entity.getType(), InitList);
          return;
        }
      }
    }
  }

  // C++11 [dcl.init.list]p3:
  //   - If T is an aggregate, aggregate initialization is performed.
  if ((DestType->isRecordType() && !DestType->isAggregateType()) ||
      (S.getLangOpts().CPlusPlus11 &&
       S.isStdInitializerList(DestType, nullptr) && !IsDesignatedInit)) {
    if (S.getLangOpts().CPlusPlus11) {
      //   - Otherwise, if the initializer list has no elements and T is a
      //     class type with a default constructor, the object is
      //     value-initialized.
      if (InitList->getNumInits() == 0) {
        CXXRecordDecl *RD = DestType->getAsCXXRecordDecl();
        if (S.LookupDefaultConstructor(RD)) {
          TryValueInitialization(S, Entity, Kind, Sequence, InitList);
          return;
        }
      }

      //   - Otherwise, if T is a specialization of std::initializer_list<E>,
      //     an initializer_list object constructed [...]
      if (TryInitializerListConstruction(S, InitList, DestType, Sequence,
                                         TreatUnavailableAsInvalid))
        return;

      //   - Otherwise, if T is a class type, constructors are considered.
      Expr *InitListAsExpr = InitList;
      TryConstructorInitialization(S, Entity, Kind, InitListAsExpr, DestType,
                                   DestType, Sequence, /*InitListSyntax*/true);
    } else
      Sequence.SetFailed(InitializationSequence::FK_InitListBadDestinationType);
    return;
  }

  if (S.getLangOpts().CPlusPlus && !DestType->isAggregateType() &&
      InitList->getNumInits() == 1) {
    Expr *E = InitList->getInit(0);

    //   - Otherwise, if T is an enumeration with a fixed underlying type,
    //     the initializer-list has a single element v, and the initialization
    //     is direct-list-initialization, the object is initialized with the
    //     value T(v); if a narrowing conversion is required to convert v to
    //     the underlying type of T, the program is ill-formed.
    auto *ET = DestType->getAs<EnumType>();
    if (S.getLangOpts().CPlusPlus17 &&
        Kind.getKind() == InitializationKind::IK_DirectList &&
        ET && ET->getDecl()->isFixed() &&
        !S.Context.hasSameUnqualifiedType(E->getType(), DestType) &&
        (E->getType()->isIntegralOrUnscopedEnumerationType() ||
         E->getType()->isFloatingType())) {
      // There are two ways that T(v) can work when T is an enumeration type.
      // If there is either an implicit conversion sequence from v to T or
      // a conversion function that can convert from v to T, then we use that.
      // Otherwise, if v is of integral, unscoped enumeration, or floating-point
      // type, it is converted to the enumeration type via its underlying type.
      // There is no overlap possible between these two cases (except when the
      // source value is already of the destination type), and the first
      // case is handled by the general case for single-element lists below.
      ImplicitConversionSequence ICS;
      ICS.setStandard();
      ICS.Standard.setAsIdentityConversion();
      if (!E->isPRValue())
        ICS.Standard.First = ICK_Lvalue_To_Rvalue;
      // If E is of a floating-point type, then the conversion is ill-formed
      // due to narrowing, but go through the motions in order to produce the
      // right diagnostic.
      ICS.Standard.Second = E->getType()->isFloatingType()
                                ? ICK_Floating_Integral
                                : ICK_Integral_Conversion;
      ICS.Standard.setFromType(E->getType());
      ICS.Standard.setToType(0, E->getType());
      ICS.Standard.setToType(1, DestType);
      ICS.Standard.setToType(2, DestType);
      Sequence.AddConversionSequenceStep(ICS, ICS.Standard.getToType(2),
                                         /*TopLevelOfInitList*/true);
      Sequence.RewrapReferenceInitList(Entity.getType(), InitList);
      return;
    }

    //   - Otherwise, if the initializer list has a single element of type E
    //     [...references are handled above...], the object or reference is
    //     initialized from that element (by copy-initialization for
    //     copy-list-initialization, or by direct-initialization for
    //     direct-list-initialization); if a narrowing conversion is required
    //     to convert the element to T, the program is ill-formed.
    //
    // Per core-24034, this is direct-initialization if we were performing
    // direct-list-initialization and copy-initialization otherwise.
    // We can't use InitListChecker for this, because it always performs
    // copy-initialization. This only matters if we might use an 'explicit'
    // conversion operator, or for the special case conversion of nullptr_t to
    // bool, so we only need to handle those cases.
    //
    // FIXME: Why not do this in all cases?
    Expr *Init = InitList->getInit(0);
    if (Init->getType()->isRecordType() ||
        (Init->getType()->isNullPtrType() && DestType->isBooleanType())) {
      InitializationKind SubKind =
          Kind.getKind() == InitializationKind::IK_DirectList
              ? InitializationKind::CreateDirect(Kind.getLocation(),
                                                 InitList->getLBraceLoc(),
                                                 InitList->getRBraceLoc())
              : Kind;
      Expr *SubInit[1] = { Init };
      Sequence.InitializeFrom(S, Entity, SubKind, SubInit,
                              /*TopLevelOfInitList*/true,
                              TreatUnavailableAsInvalid);
      if (Sequence)
        Sequence.RewrapReferenceInitList(Entity.getType(), InitList);
      return;
    }
  }

  InitListChecker CheckInitList(S, Entity, InitList,
          DestType, /*VerifyOnly=*/true, TreatUnavailableAsInvalid);
  if (CheckInitList.HadError()) {
    Sequence.SetFailed(InitializationSequence::FK_ListInitializationFailed);
    return;
  }

  // Add the list initialization step with the built init list.
  Sequence.AddListInitializationStep(DestType);
}

/// Try a reference initialization that involves calling a conversion
/// function.
static OverloadingResult TryRefInitWithConversionFunction(
    Sema &S, const InitializedEntity &Entity, const InitializationKind &Kind,
    Expr *Initializer, bool AllowRValues, bool IsLValueRef,
    InitializationSequence &Sequence) {
  QualType DestType = Entity.getType();
  QualType cv1T1 = DestType->castAs<ReferenceType>()->getPointeeType();
  QualType T1 = cv1T1.getUnqualifiedType();
  QualType cv2T2 = Initializer->getType();
  QualType T2 = cv2T2.getUnqualifiedType();

  assert(!S.CompareReferenceRelationship(Initializer->getBeginLoc(), T1, T2) &&
         "Must have incompatible references when binding via conversion");

  // Build the candidate set directly in the initialization sequence
  // structure, so that it will persist if we fail.
  OverloadCandidateSet &CandidateSet = Sequence.getFailedCandidateSet();
  CandidateSet.clear(OverloadCandidateSet::CSK_InitByUserDefinedConversion);

  // Determine whether we are allowed to call explicit conversion operators.
  // Note that none of [over.match.copy], [over.match.conv], nor
  // [over.match.ref] permit an explicit constructor to be chosen when
  // initializing a reference, not even for direct-initialization.
  bool AllowExplicitCtors = false;
  bool AllowExplicitConvs = Kind.allowExplicitConversionFunctionsInRefBinding();

  const RecordType *T1RecordType = nullptr;
  if (AllowRValues && (T1RecordType = T1->getAs<RecordType>()) &&
      S.isCompleteType(Kind.getLocation(), T1)) {
    // The type we're converting to is a class type. Enumerate its constructors
    // to see if there is a suitable conversion.
    CXXRecordDecl *T1RecordDecl = cast<CXXRecordDecl>(T1RecordType->getDecl());

    for (NamedDecl *D : S.LookupConstructors(T1RecordDecl)) {
      auto Info = getConstructorInfo(D);
      if (!Info.Constructor)
        continue;

      if (!Info.Constructor->isInvalidDecl() &&
          Info.Constructor->isConvertingConstructor(/*AllowExplicit*/true)) {
        if (Info.ConstructorTmpl)
          S.AddTemplateOverloadCandidate(
              Info.ConstructorTmpl, Info.FoundDecl,
              /*ExplicitArgs*/ nullptr, Initializer, CandidateSet,
              /*SuppressUserConversions=*/true,
              /*PartialOverloading*/ false, AllowExplicitCtors);
        else
          S.AddOverloadCandidate(
              Info.Constructor, Info.FoundDecl, Initializer, CandidateSet,
              /*SuppressUserConversions=*/true,
              /*PartialOverloading*/ false, AllowExplicitCtors);
      }
    }
  }
  if (T1RecordType && T1RecordType->getDecl()->isInvalidDecl())
    return OR_No_Viable_Function;

  const RecordType *T2RecordType = nullptr;
  if ((T2RecordType = T2->getAs<RecordType>()) &&
      S.isCompleteType(Kind.getLocation(), T2)) {
    // The type we're converting from is a class type, enumerate its conversion
    // functions.
    CXXRecordDecl *T2RecordDecl = cast<CXXRecordDecl>(T2RecordType->getDecl());

    const auto &Conversions = T2RecordDecl->getVisibleConversionFunctions();
    for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
      NamedDecl *D = *I;
      CXXRecordDecl *ActingDC = cast<CXXRecordDecl>(D->getDeclContext());
      if (isa<UsingShadowDecl>(D))
        D = cast<UsingShadowDecl>(D)->getTargetDecl();

      FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(D);
      CXXConversionDecl *Conv;
      if (ConvTemplate)
        Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
      else
        Conv = cast<CXXConversionDecl>(D);

      // If the conversion function doesn't return a reference type,
      // it can't be considered for this conversion unless we're allowed to
      // consider rvalues.
      // FIXME: Do we need to make sure that we only consider conversion
      // candidates with reference-compatible results? That might be needed to
      // break recursion.
      if ((AllowRValues ||
           Conv->getConversionType()->isLValueReferenceType())) {
        if (ConvTemplate)
          S.AddTemplateConversionCandidate(
              ConvTemplate, I.getPair(), ActingDC, Initializer, DestType,
              CandidateSet,
              /*AllowObjCConversionOnExplicit=*/false, AllowExplicitConvs);
        else
          S.AddConversionCandidate(
              Conv, I.getPair(), ActingDC, Initializer, DestType, CandidateSet,
              /*AllowObjCConversionOnExplicit=*/false, AllowExplicitConvs);
      }
    }
  }
  if (T2RecordType && T2RecordType->getDecl()->isInvalidDecl())
    return OR_No_Viable_Function;

  SourceLocation DeclLoc = Initializer->getBeginLoc();

  // Perform overload resolution. If it fails, return the failed result.
  OverloadCandidateSet::iterator Best;
  if (OverloadingResult Result
        = CandidateSet.BestViableFunction(S, DeclLoc, Best))
    return Result;

  FunctionDecl *Function = Best->Function;
  // This is the overload that will be used for this initialization step if we
  // use this initialization. Mark it as referenced.
  Function->setReferenced();

  // Compute the returned type and value kind of the conversion.
  QualType cv3T3;
  if (isa<CXXConversionDecl>(Function))
    cv3T3 = Function->getReturnType();
  else
    cv3T3 = T1;

  ExprValueKind VK = VK_PRValue;
  if (cv3T3->isLValueReferenceType())
    VK = VK_LValue;
  else if (const auto *RRef = cv3T3->getAs<RValueReferenceType>())
    VK = RRef->getPointeeType()->isFunctionType() ? VK_LValue : VK_XValue;
  cv3T3 = cv3T3.getNonLValueExprType(S.Context);

  // Add the user-defined conversion step.
  bool HadMultipleCandidates = (CandidateSet.size() > 1);
  Sequence.AddUserConversionStep(Function, Best->FoundDecl, cv3T3,
                                 HadMultipleCandidates);

  // Determine whether we'll need to perform derived-to-base adjustments or
  // other conversions.
  Sema::ReferenceConversions RefConv;
  Sema::ReferenceCompareResult NewRefRelationship =
      S.CompareReferenceRelationship(DeclLoc, T1, cv3T3, &RefConv);

  // Add the final conversion sequence, if necessary.
  if (NewRefRelationship == Sema::Ref_Incompatible) {
    assert(!isa<CXXConstructorDecl>(Function) &&
           "should not have conversion after constructor");

    ImplicitConversionSequence ICS;
    ICS.setStandard();
    ICS.Standard = Best->FinalConversion;
    Sequence.AddConversionSequenceStep(ICS, ICS.Standard.getToType(2));

    // Every implicit conversion results in a prvalue, except for a glvalue
    // derived-to-base conversion, which we handle below.
    cv3T3 = ICS.Standard.getToType(2);
    VK = VK_PRValue;
  }

  //   If the converted initializer is a prvalue, its type T4 is adjusted to
  //   type "cv1 T4" and the temporary materialization conversion is applied.
  //
  // We adjust the cv-qualifications to match the reference regardless of
  // whether we have a prvalue so that the AST records the change. In this
  // case, T4 is "cv3 T3".
  QualType cv1T4 = S.Context.getQualifiedType(cv3T3, cv1T1.getQualifiers());
  if (cv1T4.getQualifiers() != cv3T3.getQualifiers())
    Sequence.AddQualificationConversionStep(cv1T4, VK);
  Sequence.AddReferenceBindingStep(cv1T4, VK == VK_PRValue);
  VK = IsLValueRef ? VK_LValue : VK_XValue;

  if (RefConv & Sema::ReferenceConversions::DerivedToBase)
    Sequence.AddDerivedToBaseCastStep(cv1T1, VK);
  else if (RefConv & Sema::ReferenceConversions::ObjC)
    Sequence.AddObjCObjectConversionStep(cv1T1);
  else if (RefConv & Sema::ReferenceConversions::Function)
    Sequence.AddFunctionReferenceConversionStep(cv1T1);
  else if (RefConv & Sema::ReferenceConversions::Qualification) {
    if (!S.Context.hasSameType(cv1T4, cv1T1))
      Sequence.AddQualificationConversionStep(cv1T1, VK);
  }

  return OR_Success;
}

static void CheckCXX98CompatAccessibleCopy(Sema &S,
                                           const InitializedEntity &Entity,
                                           Expr *CurInitExpr);

/// Attempt reference initialization (C++0x [dcl.init.ref])
static void TryReferenceInitialization(Sema &S, const InitializedEntity &Entity,
                                       const InitializationKind &Kind,
                                       Expr *Initializer,
                                       InitializationSequence &Sequence,
                                       bool TopLevelOfInitList) {
  QualType DestType = Entity.getType();
  QualType cv1T1 = DestType->castAs<ReferenceType>()->getPointeeType();
  Qualifiers T1Quals;
  QualType T1 = S.Context.getUnqualifiedArrayType(cv1T1, T1Quals);
  QualType cv2T2 = S.getCompletedType(Initializer);
  Qualifiers T2Quals;
  QualType T2 = S.Context.getUnqualifiedArrayType(cv2T2, T2Quals);

  // If the initializer is the address of an overloaded function, try
  // to resolve the overloaded function. If all goes well, T2 is the
  // type of the resulting function.
  if (ResolveOverloadedFunctionForReferenceBinding(S, Initializer, cv2T2, T2,
                                                   T1, Sequence))
    return;

  // Delegate everything else to a subfunction.
  TryReferenceInitializationCore(S, Entity, Kind, Initializer, cv1T1, T1,
                                 T1Quals, cv2T2, T2, T2Quals, Sequence,
                                 TopLevelOfInitList);
}

/// Determine whether an expression is a non-referenceable glvalue (one to
/// which a reference can never bind). Attempting to bind a reference to
/// such a glvalue will always create a temporary.
static bool isNonReferenceableGLValue(Expr *E) {
  return E->refersToBitField() || E->refersToVectorElement() ||
         E->refersToMatrixElement();
}

/// Reference initialization without resolving overloaded functions.
///
/// We also can get here in C if we call a builtin which is declared as
/// a function with a parameter of reference type (such as __builtin_va_end()).
static void TryReferenceInitializationCore(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           Expr *Initializer,
                                           QualType cv1T1, QualType T1,
                                           Qualifiers T1Quals,
                                           QualType cv2T2, QualType T2,
                                           Qualifiers T2Quals,
                                           InitializationSequence &Sequence,
                                           bool TopLevelOfInitList) {
  QualType DestType = Entity.getType();
  SourceLocation DeclLoc = Initializer->getBeginLoc();

  // Compute some basic properties of the types and the initializer.
  bool isLValueRef = DestType->isLValueReferenceType();
  bool isRValueRef = !isLValueRef;
  Expr::Classification InitCategory = Initializer->Classify(S.Context);

  Sema::ReferenceConversions RefConv;
  Sema::ReferenceCompareResult RefRelationship =
      S.CompareReferenceRelationship(DeclLoc, cv1T1, cv2T2, &RefConv);

  // C++0x [dcl.init.ref]p5:
  //   A reference to type "cv1 T1" is initialized by an expression of type
  //   "cv2 T2" as follows:
  //
  //     - If the reference is an lvalue reference and the initializer
  //       expression
  // Note the analogous bullet points for rvalue refs to functions. Because
  // there are no function rvalues in C++, rvalue refs to functions are treated
  // like lvalue refs.
  OverloadingResult ConvOvlResult = OR_Success;
  bool T1Function = T1->isFunctionType();
  if (isLValueRef || T1Function) {
    if (InitCategory.isLValue() && !isNonReferenceableGLValue(Initializer) &&
        (RefRelationship == Sema::Ref_Compatible ||
         (Kind.isCStyleOrFunctionalCast() &&
          RefRelationship == Sema::Ref_Related))) {
      //   - is an lvalue (but is not a bit-field), and "cv1 T1" is
      //     reference-compatible with "cv2 T2," or
      if (RefConv & (Sema::ReferenceConversions::DerivedToBase |
                     Sema::ReferenceConversions::ObjC)) {
        // If we're converting the pointee, add any qualifiers first;
        // these qualifiers must all be top-level, so just convert to "cv1 T2".
        if (RefConv & (Sema::ReferenceConversions::Qualification))
          Sequence.AddQualificationConversionStep(
              S.Context.getQualifiedType(T2, T1Quals),
              Initializer->getValueKind());
        if (RefConv & Sema::ReferenceConversions::DerivedToBase)
          Sequence.AddDerivedToBaseCastStep(cv1T1, VK_LValue);
        else
          Sequence.AddObjCObjectConversionStep(cv1T1);
      } else if (RefConv & Sema::ReferenceConversions::Qualification) {
        // Perform a (possibly multi-level) qualification conversion.
        Sequence.AddQualificationConversionStep(cv1T1,
                                                Initializer->getValueKind());
      } else if (RefConv & Sema::ReferenceConversions::Function) {
        Sequence.AddFunctionReferenceConversionStep(cv1T1);
      }

      // We only create a temporary here when binding a reference to a
      // bit-field or vector element. Those cases are't supposed to be
      // handled by this bullet, but the outcome is the same either way.
      Sequence.AddReferenceBindingStep(cv1T1, false);
      return;
    }

    //     - has a class type (i.e., T2 is a class type), where T1 is not
    //       reference-related to T2, and can be implicitly converted to an
    //       lvalue of type "cv3 T3," where "cv1 T1" is reference-compatible
    //       with "cv3 T3" (this conversion is selected by enumerating the
    //       applicable conversion functions (13.3.1.6) and choosing the best
    //       one through overload resolution (13.3)),
    // If we have an rvalue ref to function type here, the rhs must be
    // an rvalue. DR1287 removed the "implicitly" here.
    if (RefRelationship == Sema::Ref_Incompatible && T2->isRecordType() &&
        (isLValueRef || InitCategory.isRValue())) {
      if (S.getLangOpts().CPlusPlus) {
        // Try conversion functions only for C++.
        ConvOvlResult = TryRefInitWithConversionFunction(
            S, Entity, Kind, Initializer, /*AllowRValues*/ isRValueRef,
            /*IsLValueRef*/ isLValueRef, Sequence);
        if (ConvOvlResult == OR_Success)
          return;
        if (ConvOvlResult != OR_No_Viable_Function)
          Sequence.SetOverloadFailure(
              InitializationSequence::FK_ReferenceInitOverloadFailed,
              ConvOvlResult);
      } else {
        ConvOvlResult = OR_No_Viable_Function;
      }
    }
  }

  //     - Otherwise, the reference shall be an lvalue reference to a
  //       non-volatile const type (i.e., cv1 shall be const), or the reference
  //       shall be an rvalue reference.
  //       For address spaces, we interpret this to mean that an addr space
  //       of a reference "cv1 T1" is a superset of addr space of "cv2 T2".
  if (isLValueRef && !(T1Quals.hasConst() && !T1Quals.hasVolatile() &&
                       T1Quals.isAddressSpaceSupersetOf(T2Quals))) {
    if (S.Context.getCanonicalType(T2) == S.Context.OverloadTy)
      Sequence.SetFailed(InitializationSequence::FK_AddressOfOverloadFailed);
    else if (ConvOvlResult && !Sequence.getFailedCandidateSet().empty())
      Sequence.SetOverloadFailure(
                        InitializationSequence::FK_ReferenceInitOverloadFailed,
                                  ConvOvlResult);
    else if (!InitCategory.isLValue())
      Sequence.SetFailed(
          T1Quals.isAddressSpaceSupersetOf(T2Quals)
              ? InitializationSequence::
                    FK_NonConstLValueReferenceBindingToTemporary
              : InitializationSequence::FK_ReferenceInitDropsQualifiers);
    else {
      InitializationSequence::FailureKind FK;
      switch (RefRelationship) {
      case Sema::Ref_Compatible:
        if (Initializer->refersToBitField())
          FK = InitializationSequence::
              FK_NonConstLValueReferenceBindingToBitfield;
        else if (Initializer->refersToVectorElement())
          FK = InitializationSequence::
              FK_NonConstLValueReferenceBindingToVectorElement;
        else if (Initializer->refersToMatrixElement())
          FK = InitializationSequence::
              FK_NonConstLValueReferenceBindingToMatrixElement;
        else
          llvm_unreachable("unexpected kind of compatible initializer");
        break;
      case Sema::Ref_Related:
        FK = InitializationSequence::FK_ReferenceInitDropsQualifiers;
        break;
      case Sema::Ref_Incompatible:
        FK = InitializationSequence::
            FK_NonConstLValueReferenceBindingToUnrelated;
        break;
      }
      Sequence.SetFailed(FK);
    }
    return;
  }

  //    - If the initializer expression
  //      - is an
  // [<=14] xvalue (but not a bit-field), class prvalue, array prvalue, or
  // [1z]   rvalue (but not a bit-field) or
  //        function lvalue and "cv1 T1" is reference-compatible with "cv2 T2"
  //
  // Note: functions are handled above and below rather than here...
  if (!T1Function &&
      (RefRelationship == Sema::Ref_Compatible ||
       (Kind.isCStyleOrFunctionalCast() &&
        RefRelationship == Sema::Ref_Related)) &&
      ((InitCategory.isXValue() && !isNonReferenceableGLValue(Initializer)) ||
       (InitCategory.isPRValue() &&
        (S.getLangOpts().CPlusPlus17 || T2->isRecordType() ||
         T2->isArrayType())))) {
    ExprValueKind ValueKind = InitCategory.isXValue() ? VK_XValue : VK_PRValue;
    if (InitCategory.isPRValue() && T2->isRecordType()) {
      // The corresponding bullet in C++03 [dcl.init.ref]p5 gives the
      // compiler the freedom to perform a copy here or bind to the
      // object, while C++0x requires that we bind directly to the
      // object. Hence, we always bind to the object without making an
      // extra copy. However, in C++03 requires that we check for the
      // presence of a suitable copy constructor:
      //
      //   The constructor that would be used to make the copy shall
      //   be callable whether or not the copy is actually done.
      if (!S.getLangOpts().CPlusPlus11 && !S.getLangOpts().MicrosoftExt)
        Sequence.AddExtraneousCopyToTemporary(cv2T2);
      else if (S.getLangOpts().CPlusPlus11)
        CheckCXX98CompatAccessibleCopy(S, Entity, Initializer);
    }

    // C++1z [dcl.init.ref]/5.2.1.2:
    //   If the converted initializer is a prvalue, its type T4 is adjusted
    //   to type "cv1 T4" and the temporary materialization conversion is
    //   applied.
    // Postpone address space conversions to after the temporary materialization
    // conversion to allow creating temporaries in the alloca address space.
    auto T1QualsIgnoreAS = T1Quals;
    auto T2QualsIgnoreAS = T2Quals;
    if (T1Quals.getAddressSpace() != T2Quals.getAddressSpace()) {
      T1QualsIgnoreAS.removeAddressSpace();
      T2QualsIgnoreAS.removeAddressSpace();
    }
    QualType cv1T4 = S.Context.getQualifiedType(cv2T2, T1QualsIgnoreAS);
    if (T1QualsIgnoreAS != T2QualsIgnoreAS)
      Sequence.AddQualificationConversionStep(cv1T4, ValueKind);
    Sequence.AddReferenceBindingStep(cv1T4, ValueKind == VK_PRValue);
    ValueKind = isLValueRef ? VK_LValue : VK_XValue;
    // Add addr space conversion if required.
    if (T1Quals.getAddressSpace() != T2Quals.getAddressSpace()) {
      auto T4Quals = cv1T4.getQualifiers();
      T4Quals.addAddressSpace(T1Quals.getAddressSpace());
      QualType cv1T4WithAS = S.Context.getQualifiedType(T2, T4Quals);
      Sequence.AddQualificationConversionStep(cv1T4WithAS, ValueKind);
      cv1T4 = cv1T4WithAS;
    }

    //   In any case, the reference is bound to the resulting glvalue (or to
    //   an appropriate base class subobject).
    if (RefConv & Sema::ReferenceConversions::DerivedToBase)
      Sequence.AddDerivedToBaseCastStep(cv1T1, ValueKind);
    else if (RefConv & Sema::ReferenceConversions::ObjC)
      Sequence.AddObjCObjectConversionStep(cv1T1);
    else if (RefConv & Sema::ReferenceConversions::Qualification) {
      if (!S.Context.hasSameType(cv1T4, cv1T1))
        Sequence.AddQualificationConversionStep(cv1T1, ValueKind);
    }
    return;
  }

  //       - has a class type (i.e., T2 is a class type), where T1 is not
  //         reference-related to T2, and can be implicitly converted to an
  //         xvalue, class prvalue, or function lvalue of type "cv3 T3",
  //         where "cv1 T1" is reference-compatible with "cv3 T3",
  //
  // DR1287 removes the "implicitly" here.
  if (T2->isRecordType()) {
    if (RefRelationship == Sema::Ref_Incompatible) {
      ConvOvlResult = TryRefInitWithConversionFunction(
          S, Entity, Kind, Initializer, /*AllowRValues*/ true,
          /*IsLValueRef*/ isLValueRef, Sequence);
      if (ConvOvlResult)
        Sequence.SetOverloadFailure(
            InitializationSequence::FK_ReferenceInitOverloadFailed,
            ConvOvlResult);

      return;
    }

    if (RefRelationship == Sema::Ref_Compatible &&
        isRValueRef && InitCategory.isLValue()) {
      Sequence.SetFailed(
        InitializationSequence::FK_RValueReferenceBindingToLValue);
      return;
    }

    Sequence.SetFailed(InitializationSequence::FK_ReferenceInitDropsQualifiers);
    return;
  }

  //      - Otherwise, a temporary of type "cv1 T1" is created and initialized
  //        from the initializer expression using the rules for a non-reference
  //        copy-initialization (8.5). The reference is then bound to the
  //        temporary. [...]

  // Ignore address space of reference type at this point and perform address
  // space conversion after the reference binding step.
  QualType cv1T1IgnoreAS =
      T1Quals.hasAddressSpace()
          ? S.Context.getQualifiedType(T1, T1Quals.withoutAddressSpace())
          : cv1T1;

  InitializedEntity TempEntity =
      InitializedEntity::InitializeTemporary(cv1T1IgnoreAS);

  // FIXME: Why do we use an implicit conversion here rather than trying
  // copy-initialization?
  ImplicitConversionSequence ICS
    = S.TryImplicitConversion(Initializer, TempEntity.getType(),
                              /*SuppressUserConversions=*/false,
                              Sema::AllowedExplicit::None,
                              /*FIXME:InOverloadResolution=*/false,
                              /*CStyle=*/Kind.isCStyleOrFunctionalCast(),
                              /*AllowObjCWritebackConversion=*/false);

  if (ICS.isBad()) {
    // FIXME: Use the conversion function set stored in ICS to turn
    // this into an overloading ambiguity diagnostic. However, we need
    // to keep that set as an OverloadCandidateSet rather than as some
    // other kind of set.
    if (ConvOvlResult && !Sequence.getFailedCandidateSet().empty())
      Sequence.SetOverloadFailure(
                        InitializationSequence::FK_ReferenceInitOverloadFailed,
                                  ConvOvlResult);
    else if (S.Context.getCanonicalType(T2) == S.Context.OverloadTy)
      Sequence.SetFailed(InitializationSequence::FK_AddressOfOverloadFailed);
    else
      Sequence.SetFailed(InitializationSequence::FK_ReferenceInitFailed);
    return;
  } else {
    Sequence.AddConversionSequenceStep(ICS, TempEntity.getType(),
                                       TopLevelOfInitList);
  }

  //        [...] If T1 is reference-related to T2, cv1 must be the
  //        same cv-qualification as, or greater cv-qualification
  //        than, cv2; otherwise, the program is ill-formed.
  unsigned T1CVRQuals = T1Quals.getCVRQualifiers();
  unsigned T2CVRQuals = T2Quals.getCVRQualifiers();
  if (RefRelationship == Sema::Ref_Related &&
      ((T1CVRQuals | T2CVRQuals) != T1CVRQuals ||
       !T1Quals.isAddressSpaceSupersetOf(T2Quals))) {
    Sequence.SetFailed(InitializationSequence::FK_ReferenceInitDropsQualifiers);
    return;
  }

  //   [...] If T1 is reference-related to T2 and the reference is an rvalue
  //   reference, the initializer expression shall not be an lvalue.
  if (RefRelationship >= Sema::Ref_Related && !isLValueRef &&
      InitCategory.isLValue()) {
    Sequence.SetFailed(
                    InitializationSequence::FK_RValueReferenceBindingToLValue);
    return;
  }

  Sequence.AddReferenceBindingStep(cv1T1IgnoreAS, /*BindingTemporary=*/true);

  if (T1Quals.hasAddressSpace()) {
    if (!Qualifiers::isAddressSpaceSupersetOf(T1Quals.getAddressSpace(),
                                              LangAS::Default)) {
      Sequence.SetFailed(
          InitializationSequence::FK_ReferenceAddrspaceMismatchTemporary);
      return;
    }
    Sequence.AddQualificationConversionStep(cv1T1, isLValueRef ? VK_LValue
                                                               : VK_XValue);
  }
}

/// Attempt character array initialization from a string literal
/// (C++ [dcl.init.string], C99 6.7.8).
static void TryStringLiteralInitialization(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           Expr *Initializer,
                                       InitializationSequence &Sequence) {
  Sequence.AddStringInitStep(Entity.getType());
}

/// Attempt value initialization (C++ [dcl.init]p7).
static void TryValueInitialization(Sema &S,
                                   const InitializedEntity &Entity,
                                   const InitializationKind &Kind,
                                   InitializationSequence &Sequence,
                                   InitListExpr *InitList) {
  assert((!InitList || InitList->getNumInits() == 0) &&
         "Shouldn't use value-init for non-empty init lists");

  // C++98 [dcl.init]p5, C++11 [dcl.init]p7:
  //
  //   To value-initialize an object of type T means:
  QualType T = Entity.getType();

  //     -- if T is an array type, then each element is value-initialized;
  T = S.Context.getBaseElementType(T);

  if (const RecordType *RT = T->getAs<RecordType>()) {
    if (CXXRecordDecl *ClassDecl = dyn_cast<CXXRecordDecl>(RT->getDecl())) {
      bool NeedZeroInitialization = true;
      // C++98:
      // -- if T is a class type (clause 9) with a user-declared constructor
      //    (12.1), then the default constructor for T is called (and the
      //    initialization is ill-formed if T has no accessible default
      //    constructor);
      // C++11:
      // -- if T is a class type (clause 9) with either no default constructor
      //    (12.1 [class.ctor]) or a default constructor that is user-provided
      //    or deleted, then the object is default-initialized;
      //
      // Note that the C++11 rule is the same as the C++98 rule if there are no
      // defaulted or deleted constructors, so we just use it unconditionally.
      CXXConstructorDecl *CD = S.LookupDefaultConstructor(ClassDecl);
      if (!CD || !CD->getCanonicalDecl()->isDefaulted() || CD->isDeleted())
        NeedZeroInitialization = false;

      // -- if T is a (possibly cv-qualified) non-union class type without a
      //    user-provided or deleted default constructor, then the object is
      //    zero-initialized and, if T has a non-trivial default constructor,
      //    default-initialized;
      // The 'non-union' here was removed by DR1502. The 'non-trivial default
      // constructor' part was removed by DR1507.
      if (NeedZeroInitialization)
        Sequence.AddZeroInitializationStep(Entity.getType());

      // C++03:
      // -- if T is a non-union class type without a user-declared constructor,
      //    then every non-static data member and base class component of T is
      //    value-initialized;
      // [...] A program that calls for [...] value-initialization of an
      // entity of reference type is ill-formed.
      //
      // C++11 doesn't need this handling, because value-initialization does not
      // occur recursively there, and the implicit default constructor is
      // defined as deleted in the problematic cases.
      if (!S.getLangOpts().CPlusPlus11 &&
          ClassDecl->hasUninitializedReferenceMember()) {
        Sequence.SetFailed(InitializationSequence::FK_TooManyInitsForReference);
        return;
      }

      // If this is list-value-initialization, pass the empty init list on when
      // building the constructor call. This affects the semantics of a few
      // things (such as whether an explicit default constructor can be called).
      Expr *InitListAsExpr = InitList;
      MultiExprArg Args(&InitListAsExpr, InitList ? 1 : 0);
      bool InitListSyntax = InitList;

      // FIXME: Instead of creating a CXXConstructExpr of array type here,
      // wrap a class-typed CXXConstructExpr in an ArrayInitLoopExpr.
      return TryConstructorInitialization(
          S, Entity, Kind, Args, T, Entity.getType(), Sequence, InitListSyntax);
    }
  }

  Sequence.AddZeroInitializationStep(Entity.getType());
}

/// Attempt default initialization (C++ [dcl.init]p6).
static void TryDefaultInitialization(Sema &S,
                                     const InitializedEntity &Entity,
                                     const InitializationKind &Kind,
                                     InitializationSequence &Sequence) {
  assert(Kind.getKind() == InitializationKind::IK_Default);

  // C++ [dcl.init]p6:
  //   To default-initialize an object of type T means:
  //     - if T is an array type, each element is default-initialized;
  QualType DestType = S.Context.getBaseElementType(Entity.getType());

  //     - if T is a (possibly cv-qualified) class type (Clause 9), the default
  //       constructor for T is called (and the initialization is ill-formed if
  //       T has no accessible default constructor);
  if (DestType->isRecordType() && S.getLangOpts().CPlusPlus) {
    TryConstructorInitialization(S, Entity, Kind, std::nullopt, DestType,
                                 Entity.getType(), Sequence);
    return;
  }

  //     - otherwise, no initialization is performed.

  //   If a program calls for the default initialization of an object of
  //   a const-qualified type T, T shall be a class type with a user-provided
  //   default constructor.
  if (DestType.isConstQualified() && S.getLangOpts().CPlusPlus) {
    if (!maybeRecoverWithZeroInitialization(S, Sequence, Entity))
      Sequence.SetFailed(InitializationSequence::FK_DefaultInitOfConst);
    return;
  }

  // If the destination type has a lifetime property, zero-initialize it.
  if (DestType.getQualifiers().hasObjCLifetime()) {
    Sequence.AddZeroInitializationStep(Entity.getType());
    return;
  }
}

static void TryOrBuildParenListInitialization(
    Sema &S, const InitializedEntity &Entity, const InitializationKind &Kind,
    ArrayRef<Expr *> Args, InitializationSequence &Sequence, bool VerifyOnly,
    ExprResult *Result = nullptr) {
  unsigned EntityIndexToProcess = 0;
  SmallVector<Expr *, 4> InitExprs;
  QualType ResultType;
  Expr *ArrayFiller = nullptr;
  FieldDecl *InitializedFieldInUnion = nullptr;

  auto HandleInitializedEntity = [&](const InitializedEntity &SubEntity,
                                     const InitializationKind &SubKind,
                                     Expr *Arg, Expr **InitExpr = nullptr) {
    InitializationSequence IS = InitializationSequence(
        S, SubEntity, SubKind, Arg ? MultiExprArg(Arg) : std::nullopt);

    if (IS.Failed()) {
      if (!VerifyOnly) {
        IS.Diagnose(S, SubEntity, SubKind, Arg ? ArrayRef(Arg) : std::nullopt);
      } else {
        Sequence.SetFailed(
            InitializationSequence::FK_ParenthesizedListInitFailed);
      }

      return false;
    }
    if (!VerifyOnly) {
      ExprResult ER;
      ER = IS.Perform(S, SubEntity, SubKind,
                      Arg ? MultiExprArg(Arg) : std::nullopt);

      if (ER.isInvalid())
        return false;

      if (InitExpr)
        *InitExpr = ER.get();
      else
        InitExprs.push_back(ER.get());
    }
    return true;
  };

  if (const ArrayType *AT =
          S.getASTContext().getAsArrayType(Entity.getType())) {
    SmallVector<InitializedEntity, 4> ElementEntities;
    uint64_t ArrayLength;
    // C++ [dcl.init]p16.5
    //   if the destination type is an array, the object is initialized as
    //   follows. Let x1, . . . , xk be the elements of the expression-list. If
    //   the destination type is an array of unknown bound, it is defined as
    //   having k elements.
    if (const ConstantArrayType *CAT =
            S.getASTContext().getAsConstantArrayType(Entity.getType())) {
      ArrayLength = CAT->getZExtSize();
      ResultType = Entity.getType();
    } else if (const VariableArrayType *VAT =
                   S.getASTContext().getAsVariableArrayType(Entity.getType())) {
      // Braced-initialization of variable array types is not allowed, even if
      // the size is greater than or equal to the number of args, so we don't
      // allow them to be initialized via parenthesized aggregate initialization
      // either.
      const Expr *SE = VAT->getSizeExpr();
      S.Diag(SE->getBeginLoc(), diag::err_variable_object_no_init)
          << SE->getSourceRange();
      return;
    } else {
      assert(Entity.getType()->isIncompleteArrayType());
      ArrayLength = Args.size();
    }
    EntityIndexToProcess = ArrayLength;

    //   ...the ith array element is copy-initialized with xi for each
    //   1 <= i <= k
    for (Expr *E : Args) {
      InitializedEntity SubEntity = InitializedEntity::InitializeElement(
          S.getASTContext(), EntityIndexToProcess, Entity);
      InitializationKind SubKind = InitializationKind::CreateForInit(
          E->getExprLoc(), /*isDirectInit=*/false, E);
      if (!HandleInitializedEntity(SubEntity, SubKind, E))
        return;
    }
    //   ...and value-initialized for each k < i <= n;
    if (ArrayLength > Args.size() || Entity.isVariableLengthArrayNew()) {
      InitializedEntity SubEntity = InitializedEntity::InitializeElement(
          S.getASTContext(), Args.size(), Entity);
      InitializationKind SubKind = InitializationKind::CreateValue(
          Kind.getLocation(), Kind.getLocation(), Kind.getLocation(), true);
      if (!HandleInitializedEntity(SubEntity, SubKind, nullptr, &ArrayFiller))
        return;
    }

    if (ResultType.isNull()) {
      ResultType = S.Context.getConstantArrayType(
          AT->getElementType(), llvm::APInt(/*numBits=*/32, ArrayLength),
          /*SizeExpr=*/nullptr, ArraySizeModifier::Normal, 0);
    }
  } else if (auto *RT = Entity.getType()->getAs<RecordType>()) {
    bool IsUnion = RT->isUnionType();
    const CXXRecordDecl *RD = cast<CXXRecordDecl>(RT->getDecl());
    if (RD->isInvalidDecl()) {
      // Exit early to avoid confusion when processing members.
      // We do the same for braced list initialization in
      // `CheckStructUnionTypes`.
      Sequence.SetFailed(
          clang::InitializationSequence::FK_ParenthesizedListInitFailed);
      return;
    }

    if (!IsUnion) {
      for (const CXXBaseSpecifier &Base : RD->bases()) {
        InitializedEntity SubEntity = InitializedEntity::InitializeBase(
            S.getASTContext(), &Base, false, &Entity);
        if (EntityIndexToProcess < Args.size()) {
          // C++ [dcl.init]p16.6.2.2.
          //   ...the object is initialized is follows. Let e1, ..., en be the
          //   elements of the aggregate([dcl.init.aggr]). Let x1, ..., xk be
          //   the elements of the expression-list...The element ei is
          //   copy-initialized with xi for 1 <= i <= k.
          Expr *E = Args[EntityIndexToProcess];
          InitializationKind SubKind = InitializationKind::CreateForInit(
              E->getExprLoc(), /*isDirectInit=*/false, E);
          if (!HandleInitializedEntity(SubEntity, SubKind, E))
            return;
        } else {
          // We've processed all of the args, but there are still base classes
          // that have to be initialized.
          // C++ [dcl.init]p17.6.2.2
          //   The remaining elements...otherwise are value initialzed
          InitializationKind SubKind = InitializationKind::CreateValue(
              Kind.getLocation(), Kind.getLocation(), Kind.getLocation(),
              /*IsImplicit=*/true);
          if (!HandleInitializedEntity(SubEntity, SubKind, nullptr))
            return;
        }
        EntityIndexToProcess++;
      }
    }

    for (FieldDecl *FD : RD->fields()) {
      // Unnamed bitfields should not be initialized at all, either with an arg
      // or by default.
      if (FD->isUnnamedBitField())
        continue;

      InitializedEntity SubEntity =
          InitializedEntity::InitializeMemberFromParenAggInit(FD);

      if (EntityIndexToProcess < Args.size()) {
        //   ...The element ei is copy-initialized with xi for 1 <= i <= k.
        Expr *E = Args[EntityIndexToProcess];

        // Incomplete array types indicate flexible array members. Do not allow
        // paren list initializations of structs with these members, as GCC
        // doesn't either.
        if (FD->getType()->isIncompleteArrayType()) {
          if (!VerifyOnly) {
            S.Diag(E->getBeginLoc(), diag::err_flexible_array_init)
                << SourceRange(E->getBeginLoc(), E->getEndLoc());
            S.Diag(FD->getLocation(), diag::note_flexible_array_member) << FD;
          }
          Sequence.SetFailed(
              InitializationSequence::FK_ParenthesizedListInitFailed);
          return;
        }

        InitializationKind SubKind = InitializationKind::CreateForInit(
            E->getExprLoc(), /*isDirectInit=*/false, E);
        if (!HandleInitializedEntity(SubEntity, SubKind, E))
          return;

        // Unions should have only one initializer expression, so we bail out
        // after processing the first field. If there are more initializers then
        // it will be caught when we later check whether EntityIndexToProcess is
        // less than Args.size();
        if (IsUnion) {
          InitializedFieldInUnion = FD;
          EntityIndexToProcess = 1;
          break;
        }
      } else {
        // We've processed all of the args, but there are still members that
        // have to be initialized.
        if (FD->hasInClassInitializer()) {
          if (!VerifyOnly) {
            // C++ [dcl.init]p16.6.2.2
            //   The remaining elements are initialized with their default
            //   member initializers, if any
            ExprResult DIE = S.BuildCXXDefaultInitExpr(
                Kind.getParenOrBraceRange().getEnd(), FD);
            if (DIE.isInvalid())
              return;
            S.checkInitializerLifetime(SubEntity, DIE.get());
            InitExprs.push_back(DIE.get());
          }
        } else {
          // C++ [dcl.init]p17.6.2.2
          //   The remaining elements...otherwise are value initialzed
          if (FD->getType()->isReferenceType()) {
            Sequence.SetFailed(
                InitializationSequence::FK_ParenthesizedListInitFailed);
            if (!VerifyOnly) {
              SourceRange SR = Kind.getParenOrBraceRange();
              S.Diag(SR.getEnd(), diag::err_init_reference_member_uninitialized)
                  << FD->getType() << SR;
              S.Diag(FD->getLocation(), diag::note_uninit_reference_member);
            }
            return;
          }
          InitializationKind SubKind = InitializationKind::CreateValue(
              Kind.getLocation(), Kind.getLocation(), Kind.getLocation(), true);
          if (!HandleInitializedEntity(SubEntity, SubKind, nullptr))
            return;
        }
      }
      EntityIndexToProcess++;
    }
    ResultType = Entity.getType();
  }

  // Not all of the args have been processed, so there must've been more args
  // than were required to initialize the element.
  if (EntityIndexToProcess < Args.size()) {
    Sequence.SetFailed(InitializationSequence::FK_ParenthesizedListInitFailed);
    if (!VerifyOnly) {
      QualType T = Entity.getType();
      int InitKind = T->isArrayType() ? 0 : T->isUnionType() ? 3 : 4;
      SourceRange ExcessInitSR(Args[EntityIndexToProcess]->getBeginLoc(),
                               Args.back()->getEndLoc());
      S.Diag(Kind.getLocation(), diag::err_excess_initializers)
          << InitKind << ExcessInitSR;
    }
    return;
  }

  if (VerifyOnly) {
    Sequence.setSequenceKind(InitializationSequence::NormalSequence);
    Sequence.AddParenthesizedListInitStep(Entity.getType());
  } else if (Result) {
    SourceRange SR = Kind.getParenOrBraceRange();
    auto *CPLIE = CXXParenListInitExpr::Create(
        S.getASTContext(), InitExprs, ResultType, Args.size(),
        Kind.getLocation(), SR.getBegin(), SR.getEnd());
    if (ArrayFiller)
      CPLIE->setArrayFiller(ArrayFiller);
    if (InitializedFieldInUnion)
      CPLIE->setInitializedFieldInUnion(InitializedFieldInUnion);
    *Result = CPLIE;
    S.Diag(Kind.getLocation(),
           diag::warn_cxx17_compat_aggregate_init_paren_list)
        << Kind.getLocation() << SR << ResultType;
  }
}

/// Attempt a user-defined conversion between two types (C++ [dcl.init]),
/// which enumerates all conversion functions and performs overload resolution
/// to select the best.
static void TryUserDefinedConversion(Sema &S,
                                     QualType DestType,
                                     const InitializationKind &Kind,
                                     Expr *Initializer,
                                     InitializationSequence &Sequence,
                                     bool TopLevelOfInitList) {
  assert(!DestType->isReferenceType() && "References are handled elsewhere");
  QualType SourceType = Initializer->getType();
  assert((DestType->isRecordType() || SourceType->isRecordType()) &&
         "Must have a class type to perform a user-defined conversion");

  // Build the candidate set directly in the initialization sequence
  // structure, so that it will persist if we fail.
  OverloadCandidateSet &CandidateSet = Sequence.getFailedCandidateSet();
  CandidateSet.clear(OverloadCandidateSet::CSK_InitByUserDefinedConversion);
  CandidateSet.setDestAS(DestType.getQualifiers().getAddressSpace());

  // Determine whether we are allowed to call explicit constructors or
  // explicit conversion operators.
  bool AllowExplicit = Kind.AllowExplicit();

  if (const RecordType *DestRecordType = DestType->getAs<RecordType>()) {
    // The type we're converting to is a class type. Enumerate its constructors
    // to see if there is a suitable conversion.
    CXXRecordDecl *DestRecordDecl
      = cast<CXXRecordDecl>(DestRecordType->getDecl());

    // Try to complete the type we're converting to.
    if (S.isCompleteType(Kind.getLocation(), DestType)) {
      for (NamedDecl *D : S.LookupConstructors(DestRecordDecl)) {
        auto Info = getConstructorInfo(D);
        if (!Info.Constructor)
          continue;

        if (!Info.Constructor->isInvalidDecl() &&
            Info.Constructor->isConvertingConstructor(/*AllowExplicit*/true)) {
          if (Info.ConstructorTmpl)
            S.AddTemplateOverloadCandidate(
                Info.ConstructorTmpl, Info.FoundDecl,
                /*ExplicitArgs*/ nullptr, Initializer, CandidateSet,
                /*SuppressUserConversions=*/true,
                /*PartialOverloading*/ false, AllowExplicit);
          else
            S.AddOverloadCandidate(Info.Constructor, Info.FoundDecl,
                                   Initializer, CandidateSet,
                                   /*SuppressUserConversions=*/true,
                                   /*PartialOverloading*/ false, AllowExplicit);
        }
      }
    }
  }

  SourceLocation DeclLoc = Initializer->getBeginLoc();

  if (const RecordType *SourceRecordType = SourceType->getAs<RecordType>()) {
    // The type we're converting from is a class type, enumerate its conversion
    // functions.

    // We can only enumerate the conversion functions for a complete type; if
    // the type isn't complete, simply skip this step.
    if (S.isCompleteType(DeclLoc, SourceType)) {
      CXXRecordDecl *SourceRecordDecl
        = cast<CXXRecordDecl>(SourceRecordType->getDecl());

      const auto &Conversions =
          SourceRecordDecl->getVisibleConversionFunctions();
      for (auto I = Conversions.begin(), E = Conversions.end(); I != E; ++I) {
        NamedDecl *D = *I;
        CXXRecordDecl *ActingDC = cast<CXXRecordDecl>(D->getDeclContext());
        if (isa<UsingShadowDecl>(D))
          D = cast<UsingShadowDecl>(D)->getTargetDecl();

        FunctionTemplateDecl *ConvTemplate = dyn_cast<FunctionTemplateDecl>(D);
        CXXConversionDecl *Conv;
        if (ConvTemplate)
          Conv = cast<CXXConversionDecl>(ConvTemplate->getTemplatedDecl());
        else
          Conv = cast<CXXConversionDecl>(D);

        if (ConvTemplate)
          S.AddTemplateConversionCandidate(
              ConvTemplate, I.getPair(), ActingDC, Initializer, DestType,
              CandidateSet, AllowExplicit, AllowExplicit);
        else
          S.AddConversionCandidate(Conv, I.getPair(), ActingDC, Initializer,
                                   DestType, CandidateSet, AllowExplicit,
                                   AllowExplicit);
      }
    }
  }

  // Perform overload resolution. If it fails, return the failed result.
  OverloadCandidateSet::iterator Best;
  if (OverloadingResult Result
        = CandidateSet.BestViableFunction(S, DeclLoc, Best)) {
    Sequence.SetOverloadFailure(
        InitializationSequence::FK_UserConversionOverloadFailed, Result);

    // [class.copy.elision]p3:
    // In some copy-initialization contexts, a two-stage overload resolution
    // is performed.
    // If the first overload resolution selects a deleted function, we also
    // need the initialization sequence to decide whether to perform the second
    // overload resolution.
    if (!(Result == OR_Deleted &&
          Kind.getKind() == InitializationKind::IK_Copy))
      return;
  }

  FunctionDecl *Function = Best->Function;
  Function->setReferenced();
  bool HadMultipleCandidates = (CandidateSet.size() > 1);

  if (isa<CXXConstructorDecl>(Function)) {
    // Add the user-defined conversion step. Any cv-qualification conversion is
    // subsumed by the initialization. Per DR5, the created temporary is of the
    // cv-unqualified type of the destination.
    Sequence.AddUserConversionStep(Function, Best->FoundDecl,
                                   DestType.getUnqualifiedType(),
                                   HadMultipleCandidates);

    // C++14 and before:
    //   - if the function is a constructor, the call initializes a temporary
    //     of the cv-unqualified version of the destination type. The [...]
    //     temporary [...] is then used to direct-initialize, according to the
    //     rules above, the object that is the destination of the
    //     copy-initialization.
    // Note that this just performs a simple object copy from the temporary.
    //
    // C++17:
    //   - if the function is a constructor, the call is a prvalue of the
    //     cv-unqualified version of the destination type whose return object
    //     is initialized by the constructor. The call is used to
    //     direct-initialize, according to the rules above, the object that
    //     is the destination of the copy-initialization.
    // Therefore we need to do nothing further.
    //
    // FIXME: Mark this copy as extraneous.
    if (!S.getLangOpts().CPlusPlus17)
      Sequence.AddFinalCopy(DestType);
    else if (DestType.hasQualifiers())
      Sequence.AddQualificationConversionStep(DestType, VK_PRValue);
    return;
  }

  // Add the user-defined conversion step that calls the conversion function.
  QualType ConvType = Function->getCallResultType();
  Sequence.AddUserConversionStep(Function, Best->FoundDecl, ConvType,
                                 HadMultipleCandidates);

  if (ConvType->getAs<RecordType>()) {
    //   The call is used to direct-initialize [...] the object that is the
    //   destination of the copy-initialization.
    //
    // In C++17, this does not call a constructor if we enter /17.6.1:
    //   - If the initializer expression is a prvalue and the cv-unqualified
    //     version of the source type is the same as the class of the
    //     destination [... do not make an extra copy]
    //
    // FIXME: Mark this copy as extraneous.
    if (!S.getLangOpts().CPlusPlus17 ||
        Function->getReturnType()->isReferenceType() ||
        !S.Context.hasSameUnqualifiedType(ConvType, DestType))
      Sequence.AddFinalCopy(DestType);
    else if (!S.Context.hasSameType(ConvType, DestType))
      Sequence.AddQualificationConversionStep(DestType, VK_PRValue);
    return;
  }

  // If the conversion following the call to the conversion function
  // is interesting, add it as a separate step.
  if (Best->FinalConversion.First || Best->FinalConversion.Second ||
      Best->FinalConversion.Third) {
    ImplicitConversionSequence ICS;
    ICS.setStandard();
    ICS.Standard = Best->FinalConversion;
    Sequence.AddConversionSequenceStep(ICS, DestType, TopLevelOfInitList);
  }
}

/// An egregious hack for compatibility with libstdc++-4.2: in <tr1/hashtable>,
/// a function with a pointer return type contains a 'return false;' statement.
/// In C++11, 'false' is not a null pointer, so this breaks the build of any
/// code using that header.
///
/// Work around this by treating 'return false;' as zero-initializing the result
/// if it's used in a pointer-returning function in a system header.
static bool isLibstdcxxPointerReturnFalseHack(Sema &S,
                                              const InitializedEntity &Entity,
                                              const Expr *Init) {
  return S.getLangOpts().CPlusPlus11 &&
         Entity.getKind() == InitializedEntity::EK_Result &&
         Entity.getType()->isPointerType() &&
         isa<CXXBoolLiteralExpr>(Init) &&
         !cast<CXXBoolLiteralExpr>(Init)->getValue() &&
         S.getSourceManager().isInSystemHeader(Init->getExprLoc());
}

/// The non-zero enum values here are indexes into diagnostic alternatives.
enum InvalidICRKind { IIK_okay, IIK_nonlocal, IIK_nonscalar };

/// Determines whether this expression is an acceptable ICR source.
static InvalidICRKind isInvalidICRSource(ASTContext &C, Expr *e,
                                         bool isAddressOf, bool &isWeakAccess) {
  // Skip parens.
  e = e->IgnoreParens();

  // Skip address-of nodes.
  if (UnaryOperator *op = dyn_cast<UnaryOperator>(e)) {
    if (op->getOpcode() == UO_AddrOf)
      return isInvalidICRSource(C, op->getSubExpr(), /*addressof*/ true,
                                isWeakAccess);

  // Skip certain casts.
  } else if (CastExpr *ce = dyn_cast<CastExpr>(e)) {
    switch (ce->getCastKind()) {
    case CK_Dependent:
    case CK_BitCast:
    case CK_LValueBitCast:
    case CK_NoOp:
      return isInvalidICRSource(C, ce->getSubExpr(), isAddressOf, isWeakAccess);

    case CK_ArrayToPointerDecay:
      return IIK_nonscalar;

    case CK_NullToPointer:
      return IIK_okay;

    default:
      break;
    }

  // If we have a declaration reference, it had better be a local variable.
  } else if (isa<DeclRefExpr>(e)) {
    // set isWeakAccess to true, to mean that there will be an implicit
    // load which requires a cleanup.
    if (e->getType().getObjCLifetime() == Qualifiers::OCL_Weak)
      isWeakAccess = true;

    if (!isAddressOf) return IIK_nonlocal;

    VarDecl *var = dyn_cast<VarDecl>(cast<DeclRefExpr>(e)->getDecl());
    if (!var) return IIK_nonlocal;

    return (var->hasLocalStorage() ? IIK_okay : IIK_nonlocal);

  // If we have a conditional operator, check both sides.
  } else if (ConditionalOperator *cond = dyn_cast<ConditionalOperator>(e)) {
    if (InvalidICRKind iik = isInvalidICRSource(C, cond->getLHS(), isAddressOf,
                                                isWeakAccess))
      return iik;

    return isInvalidICRSource(C, cond->getRHS(), isAddressOf, isWeakAccess);

  // These are never scalar.
  } else if (isa<ArraySubscriptExpr>(e)) {
    return IIK_nonscalar;

  // Otherwise, it needs to be a null pointer constant.
  } else {
    return (e->isNullPointerConstant(C, Expr::NPC_ValueDependentIsNull)
            ? IIK_okay : IIK_nonlocal);
  }

  return IIK_nonlocal;
}

/// Check whether the given expression is a valid operand for an
/// indirect copy/restore.
static void checkIndirectCopyRestoreSource(Sema &S, Expr *src) {
  assert(src->isPRValue());
  bool isWeakAccess = false;
  InvalidICRKind iik = isInvalidICRSource(S.Context, src, false, isWeakAccess);
  // If isWeakAccess to true, there will be an implicit
  // load which requires a cleanup.
  if (S.getLangOpts().ObjCAutoRefCount && isWeakAccess)
    S.Cleanup.setExprNeedsCleanups(true);

  if (iik == IIK_okay) return;

  S.Diag(src->getExprLoc(), diag::err_arc_nonlocal_writeback)
    << ((unsigned) iik - 1)  // shift index into diagnostic explanations
    << src->getSourceRange();
}

/// Determine whether we have compatible array types for the
/// purposes of GNU by-copy array initialization.
static bool hasCompatibleArrayTypes(ASTContext &Context, const ArrayType *Dest,
                                    const ArrayType *Source) {
  // If the source and destination array types are equivalent, we're
  // done.
  if (Context.hasSameType(QualType(Dest, 0), QualType(Source, 0)))
    return true;

  // Make sure that the element types are the same.
  if (!Context.hasSameType(Dest->getElementType(), Source->getElementType()))
    return false;

  // The only mismatch we allow is when the destination is an
  // incomplete array type and the source is a constant array type.
  return Source->isConstantArrayType() && Dest->isIncompleteArrayType();
}

static bool tryObjCWritebackConversion(Sema &S,
                                       InitializationSequence &Sequence,
                                       const InitializedEntity &Entity,
                                       Expr *Initializer) {
  bool ArrayDecay = false;
  QualType ArgType = Initializer->getType();
  QualType ArgPointee;
  if (const ArrayType *ArgArrayType = S.Context.getAsArrayType(ArgType)) {
    ArrayDecay = true;
    ArgPointee = ArgArrayType->getElementType();
    ArgType = S.Context.getPointerType(ArgPointee);
  }

  // Handle write-back conversion.
  QualType ConvertedArgType;
  if (!S.ObjC().isObjCWritebackConversion(ArgType, Entity.getType(),
                                          ConvertedArgType))
    return false;

  // We should copy unless we're passing to an argument explicitly
  // marked 'out'.
  bool ShouldCopy = true;
  if (ParmVarDecl *param = cast_or_null<ParmVarDecl>(Entity.getDecl()))
    ShouldCopy = (param->getObjCDeclQualifier() != ParmVarDecl::OBJC_TQ_Out);

  // Do we need an lvalue conversion?
  if (ArrayDecay || Initializer->isGLValue()) {
    ImplicitConversionSequence ICS;
    ICS.setStandard();
    ICS.Standard.setAsIdentityConversion();

    QualType ResultType;
    if (ArrayDecay) {
      ICS.Standard.First = ICK_Array_To_Pointer;
      ResultType = S.Context.getPointerType(ArgPointee);
    } else {
      ICS.Standard.First = ICK_Lvalue_To_Rvalue;
      ResultType = Initializer->getType().getNonLValueExprType(S.Context);
    }

    Sequence.AddConversionSequenceStep(ICS, ResultType);
  }

  Sequence.AddPassByIndirectCopyRestoreStep(Entity.getType(), ShouldCopy);
  return true;
}

static bool TryOCLSamplerInitialization(Sema &S,
                                        InitializationSequence &Sequence,
                                        QualType DestType,
                                        Expr *Initializer) {
  if (!S.getLangOpts().OpenCL || !DestType->isSamplerT() ||
      (!Initializer->isIntegerConstantExpr(S.Context) &&
      !Initializer->getType()->isSamplerT()))
    return false;

  Sequence.AddOCLSamplerInitStep(DestType);
  return true;
}

static bool IsZeroInitializer(Expr *Initializer, Sema &S) {
  return Initializer->isIntegerConstantExpr(S.getASTContext()) &&
    (Initializer->EvaluateKnownConstInt(S.getASTContext()) == 0);
}

static bool TryOCLZeroOpaqueTypeInitialization(Sema &S,
                                               InitializationSequence &Sequence,
                                               QualType DestType,
                                               Expr *Initializer) {
  if (!S.getLangOpts().OpenCL)
    return false;

  //
  // OpenCL 1.2 spec, s6.12.10
  //
  // The event argument can also be used to associate the
  // async_work_group_copy with a previous async copy allowing
  // an event to be shared by multiple async copies; otherwise
  // event should be zero.
  //
  if (DestType->isEventT() || DestType->isQueueT()) {
    if (!IsZeroInitializer(Initializer, S))
      return false;

    Sequence.AddOCLZeroOpaqueTypeStep(DestType);
    return true;
  }

  // We should allow zero initialization for all types defined in the
  // cl_intel_device_side_avc_motion_estimation extension, except
  // intel_sub_group_avc_mce_payload_t and intel_sub_group_avc_mce_result_t.
  if (S.getOpenCLOptions().isAvailableOption(
          "cl_intel_device_side_avc_motion_estimation", S.getLangOpts()) &&
      DestType->isOCLIntelSubgroupAVCType()) {
    if (DestType->isOCLIntelSubgroupAVCMcePayloadType() ||
        DestType->isOCLIntelSubgroupAVCMceResultType())
      return false;
    if (!IsZeroInitializer(Initializer, S))
      return false;

    Sequence.AddOCLZeroOpaqueTypeStep(DestType);
    return true;
  }

  return false;
}

InitializationSequence::InitializationSequence(
    Sema &S, const InitializedEntity &Entity, const InitializationKind &Kind,
    MultiExprArg Args, bool TopLevelOfInitList, bool TreatUnavailableAsInvalid)
    : FailedOverloadResult(OR_Success),
      FailedCandidateSet(Kind.getLocation(), OverloadCandidateSet::CSK_Normal) {
  InitializeFrom(S, Entity, Kind, Args, TopLevelOfInitList,
                 TreatUnavailableAsInvalid);
}

/// Tries to get a FunctionDecl out of `E`. If it succeeds and we can take the
/// address of that function, this returns true. Otherwise, it returns false.
static bool isExprAnUnaddressableFunction(Sema &S, const Expr *E) {
  auto *DRE = dyn_cast<DeclRefExpr>(E);
  if (!DRE || !isa<FunctionDecl>(DRE->getDecl()))
    return false;

  return !S.checkAddressOfFunctionIsAvailable(
      cast<FunctionDecl>(DRE->getDecl()));
}

/// Determine whether we can perform an elementwise array copy for this kind
/// of entity.
static bool canPerformArrayCopy(const InitializedEntity &Entity) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_LambdaCapture:
    // C++ [expr.prim.lambda]p24:
    //   For array members, the array elements are direct-initialized in
    //   increasing subscript order.
    return true;

  case InitializedEntity::EK_Variable:
    // C++ [dcl.decomp]p1:
    //   [...] each element is copy-initialized or direct-initialized from the
    //   corresponding element of the assignment-expression [...]
    return isa<DecompositionDecl>(Entity.getDecl());

  case InitializedEntity::EK_Member:
    // C++ [class.copy.ctor]p14:
    //   - if the member is an array, each element is direct-initialized with
    //     the corresponding subobject of x
    return Entity.isImplicitMemberInitializer();

  case InitializedEntity::EK_ArrayElement:
    // All the above cases are intended to apply recursively, even though none
    // of them actually say that.
    if (auto *E = Entity.getParent())
      return canPerformArrayCopy(*E);
    break;

  default:
    break;
  }

  return false;
}

void InitializationSequence::InitializeFrom(Sema &S,
                                            const InitializedEntity &Entity,
                                            const InitializationKind &Kind,
                                            MultiExprArg Args,
                                            bool TopLevelOfInitList,
                                            bool TreatUnavailableAsInvalid) {
  ASTContext &Context = S.Context;

  // Eliminate non-overload placeholder types in the arguments.  We
  // need to do this before checking whether types are dependent
  // because lowering a pseudo-object expression might well give us
  // something of dependent type.
  for (unsigned I = 0, E = Args.size(); I != E; ++I)
    if (Args[I]->getType()->isNonOverloadPlaceholderType()) {
      // FIXME: should we be doing this here?
      ExprResult result = S.CheckPlaceholderExpr(Args[I]);
      if (result.isInvalid()) {
        SetFailed(FK_PlaceholderType);
        return;
      }
      Args[I] = result.get();
    }

  // C++0x [dcl.init]p16:
  //   The semantics of initializers are as follows. The destination type is
  //   the type of the object or reference being initialized and the source
  //   type is the type of the initializer expression. The source type is not
  //   defined when the initializer is a braced-init-list or when it is a
  //   parenthesized list of expressions.
  QualType DestType = Entity.getType();

  if (DestType->isDependentType() ||
      Expr::hasAnyTypeDependentArguments(Args)) {
    SequenceKind = DependentSequence;
    return;
  }

  // Almost everything is a normal sequence.
  setSequenceKind(NormalSequence);

  QualType SourceType;
  Expr *Initializer = nullptr;
  if (Args.size() == 1) {
    Initializer = Args[0];
    if (S.getLangOpts().ObjC) {
      if (S.ObjC().CheckObjCBridgeRelatedConversions(
              Initializer->getBeginLoc(), DestType, Initializer->getType(),
              Initializer) ||
          S.ObjC().CheckConversionToObjCLiteral(DestType, Initializer))
        Args[0] = Initializer;
    }
    if (!isa<InitListExpr>(Initializer))
      SourceType = Initializer->getType();
  }

  //     - If the initializer is a (non-parenthesized) braced-init-list, the
  //       object is list-initialized (8.5.4).
  if (Kind.getKind() != InitializationKind::IK_Direct) {
    if (InitListExpr *InitList = dyn_cast_or_null<InitListExpr>(Initializer)) {
      TryListInitialization(S, Entity, Kind, InitList, *this,
                            TreatUnavailableAsInvalid);
      return;
    }
  }

  //     - If the destination type is a reference type, see 8.5.3.
  if (DestType->isReferenceType()) {
    // C++0x [dcl.init.ref]p1:
    //   A variable declared to be a T& or T&&, that is, "reference to type T"
    //   (8.3.2), shall be initialized by an object, or function, of type T or
    //   by an object that can be converted into a T.
    // (Therefore, multiple arguments are not permitted.)
    if (Args.size() != 1)
      SetFailed(FK_TooManyInitsForReference);
    // C++17 [dcl.init.ref]p5:
    //   A reference [...] is initialized by an expression [...] as follows:
    // If the initializer is not an expression, presumably we should reject,
    // but the standard fails to actually say so.
    else if (isa<InitListExpr>(Args[0]))
      SetFailed(FK_ParenthesizedListInitForReference);
    else
      TryReferenceInitialization(S, Entity, Kind, Args[0], *this,
                                 TopLevelOfInitList);
    return;
  }

  //     - If the initializer is (), the object is value-initialized.
  if (Kind.getKind() == InitializationKind::IK_Value ||
      (Kind.getKind() == InitializationKind::IK_Direct && Args.empty())) {
    TryValueInitialization(S, Entity, Kind, *this);
    return;
  }

  // Handle default initialization.
  if (Kind.getKind() == InitializationKind::IK_Default) {
    TryDefaultInitialization(S, Entity, Kind, *this);
    return;
  }

  //     - If the destination type is an array of characters, an array of
  //       char16_t, an array of char32_t, or an array of wchar_t, and the
  //       initializer is a string literal, see 8.5.2.
  //     - Otherwise, if the destination type is an array, the program is
  //       ill-formed.
  //     - Except in HLSL, where non-decaying array parameters behave like
  //       non-array types for initialization.
  if (DestType->isArrayType() && !DestType->isArrayParameterType()) {
    const ArrayType *DestAT = Context.getAsArrayType(DestType);
    if (Initializer && isa<VariableArrayType>(DestAT)) {
      SetFailed(FK_VariableLengthArrayHasInitializer);
      return;
    }

    if (Initializer) {
      switch (IsStringInit(Initializer, DestAT, Context)) {
      case SIF_None:
        TryStringLiteralInitialization(S, Entity, Kind, Initializer, *this);
        return;
      case SIF_NarrowStringIntoWideChar:
        SetFailed(FK_NarrowStringIntoWideCharArray);
        return;
      case SIF_WideStringIntoChar:
        SetFailed(FK_WideStringIntoCharArray);
        return;
      case SIF_IncompatWideStringIntoWideChar:
        SetFailed(FK_IncompatWideStringIntoWideChar);
        return;
      case SIF_PlainStringIntoUTF8Char:
        SetFailed(FK_PlainStringIntoUTF8Char);
        return;
      case SIF_UTF8StringIntoPlainChar:
        SetFailed(FK_UTF8StringIntoPlainChar);
        return;
      case SIF_Other:
        break;
      }
    }

    // Some kinds of initialization permit an array to be initialized from
    // another array of the same type, and perform elementwise initialization.
    if (Initializer && isa<ConstantArrayType>(DestAT) &&
        S.Context.hasSameUnqualifiedType(Initializer->getType(),
                                         Entity.getType()) &&
        canPerformArrayCopy(Entity)) {
      // If source is a prvalue, use it directly.
      if (Initializer->isPRValue()) {
        AddArrayInitStep(DestType, /*IsGNUExtension*/false);
        return;
      }

      // Emit element-at-a-time copy loop.
      InitializedEntity Element =
          InitializedEntity::InitializeElement(S.Context, 0, Entity);
      QualType InitEltT =
          Context.getAsArrayType(Initializer->getType())->getElementType();
      OpaqueValueExpr OVE(Initializer->getExprLoc(), InitEltT,
                          Initializer->getValueKind(),
                          Initializer->getObjectKind());
      Expr *OVEAsExpr = &OVE;
      InitializeFrom(S, Element, Kind, OVEAsExpr, TopLevelOfInitList,
                     TreatUnavailableAsInvalid);
      if (!Failed())
        AddArrayInitLoopStep(Entity.getType(), InitEltT);
      return;
    }

    // Note: as an GNU C extension, we allow initialization of an
    // array from a compound literal that creates an array of the same
    // type, so long as the initializer has no side effects.
    if (!S.getLangOpts().CPlusPlus && Initializer &&
        isa<CompoundLiteralExpr>(Initializer->IgnoreParens()) &&
        Initializer->getType()->isArrayType()) {
      const ArrayType *SourceAT
        = Context.getAsArrayType(Initializer->getType());
      if (!hasCompatibleArrayTypes(S.Context, DestAT, SourceAT))
        SetFailed(FK_ArrayTypeMismatch);
      else if (Initializer->HasSideEffects(S.Context))
        SetFailed(FK_NonConstantArrayInit);
      else {
        AddArrayInitStep(DestType, /*IsGNUExtension*/true);
      }
    }
    // Note: as a GNU C++ extension, we allow list-initialization of a
    // class member of array type from a parenthesized initializer list.
    else if (S.getLangOpts().CPlusPlus &&
             Entity.getKind() == InitializedEntity::EK_Member &&
             isa_and_nonnull<InitListExpr>(Initializer)) {
      TryListInitialization(S, Entity, Kind, cast<InitListExpr>(Initializer),
                            *this, TreatUnavailableAsInvalid);
      AddParenthesizedArrayInitStep(DestType);
    } else if (S.getLangOpts().CPlusPlus20 && !TopLevelOfInitList &&
               Kind.getKind() == InitializationKind::IK_Direct)
      TryOrBuildParenListInitialization(S, Entity, Kind, Args, *this,
                                        /*VerifyOnly=*/true);
    else if (DestAT->getElementType()->isCharType())
      SetFailed(FK_ArrayNeedsInitListOrStringLiteral);
    else if (IsWideCharCompatible(DestAT->getElementType(), Context))
      SetFailed(FK_ArrayNeedsInitListOrWideStringLiteral);
    else
      SetFailed(FK_ArrayNeedsInitList);

    return;
  }

  // Determine whether we should consider writeback conversions for
  // Objective-C ARC.
  bool allowObjCWritebackConversion = S.getLangOpts().ObjCAutoRefCount &&
         Entity.isParameterKind();

  if (TryOCLSamplerInitialization(S, *this, DestType, Initializer))
    return;

  // We're at the end of the line for C: it's either a write-back conversion
  // or it's a C assignment. There's no need to check anything else.
  if (!S.getLangOpts().CPlusPlus) {
    assert(Initializer && "Initializer must be non-null");
    // If allowed, check whether this is an Objective-C writeback conversion.
    if (allowObjCWritebackConversion &&
        tryObjCWritebackConversion(S, *this, Entity, Initializer)) {
      return;
    }

    if (TryOCLZeroOpaqueTypeInitialization(S, *this, DestType, Initializer))
      return;

    // Handle initialization in C
    AddCAssignmentStep(DestType);
    MaybeProduceObjCObject(S, *this, Entity);
    return;
  }

  assert(S.getLangOpts().CPlusPlus);

  //     - If the destination type is a (possibly cv-qualified) class type:
  if (DestType->isRecordType()) {
    //     - If the initialization is direct-initialization, or if it is
    //       copy-initialization where the cv-unqualified version of the
    //       source type is the same class as, or a derived class of, the
    //       class of the destination, constructors are considered. [...]
    if (Kind.getKind() == InitializationKind::IK_Direct ||
        (Kind.getKind() == InitializationKind::IK_Copy &&
         (Context.hasSameUnqualifiedType(SourceType, DestType) ||
          (Initializer && S.IsDerivedFrom(Initializer->getBeginLoc(),
                                          SourceType, DestType))))) {
      TryConstructorInitialization(S, Entity, Kind, Args, DestType, DestType,
                                   *this);

      // We fall back to the "no matching constructor" path if the
      // failed candidate set has functions other than the three default
      // constructors. For example, conversion function.
      if (const auto *RD =
              dyn_cast<CXXRecordDecl>(DestType->getAs<RecordType>()->getDecl());
          // In general, we should call isCompleteType for RD to check its
          // completeness, we don't call it here as it was already called in the
          // above TryConstructorInitialization.
          S.getLangOpts().CPlusPlus20 && RD && RD->hasDefinition() &&
          RD->isAggregate() && Failed() &&
          getFailureKind() == FK_ConstructorOverloadFailed) {
        // Do not attempt paren list initialization if overload resolution
        // resolves to a deleted function .
        //
        // We may reach this condition if we have a union wrapping a class with
        // a non-trivial copy or move constructor and we call one of those two
        // constructors. The union is an aggregate, but the matched constructor
        // is implicitly deleted, so we need to prevent aggregate initialization
        // (otherwise, it'll attempt aggregate initialization by initializing
        // the first element with a reference to the union).
        OverloadCandidateSet::iterator Best;
        OverloadingResult OR = getFailedCandidateSet().BestViableFunction(
            S, Kind.getLocation(), Best);
        if (OR != OverloadingResult::OR_Deleted) {
          // C++20 [dcl.init] 17.6.2.2:
          //   - Otherwise, if no constructor is viable, the destination type is
          //   an
          //      aggregate class, and the initializer is a parenthesized
          //      expression-list.
          TryOrBuildParenListInitialization(S, Entity, Kind, Args, *this,
                                            /*VerifyOnly=*/true);
        }
      }
    } else {
      //     - Otherwise (i.e., for the remaining copy-initialization cases),
      //       user-defined conversion sequences that can convert from the
      //       source type to the destination type or (when a conversion
      //       function is used) to a derived class thereof are enumerated as
      //       described in 13.3.1.4, and the best one is chosen through
      //       overload resolution (13.3).
      assert(Initializer && "Initializer must be non-null");
      TryUserDefinedConversion(S, DestType, Kind, Initializer, *this,
                               TopLevelOfInitList);
    }
    return;
  }

  assert(Args.size() >= 1 && "Zero-argument case handled above");

  // For HLSL ext vector types we allow list initialization behavior for C++
  // constructor syntax. This is accomplished by converting initialization
  // arguments an InitListExpr late.
  if (S.getLangOpts().HLSL && Args.size() > 1 && DestType->isExtVectorType() &&
      (SourceType.isNull() ||
       !Context.hasSameUnqualifiedType(SourceType, DestType))) {

    llvm::SmallVector<Expr *> InitArgs;
    for (auto *Arg : Args) {
      if (Arg->getType()->isExtVectorType()) {
        const auto *VTy = Arg->getType()->castAs<ExtVectorType>();
        unsigned Elm = VTy->getNumElements();
        for (unsigned Idx = 0; Idx < Elm; ++Idx) {
          InitArgs.emplace_back(new (Context) ArraySubscriptExpr(
              Arg,
              IntegerLiteral::Create(
                  Context, llvm::APInt(Context.getIntWidth(Context.IntTy), Idx),
                  Context.IntTy, SourceLocation()),
              VTy->getElementType(), Arg->getValueKind(), Arg->getObjectKind(),
              SourceLocation()));
        }
      } else
        InitArgs.emplace_back(Arg);
    }
    InitListExpr *ILE = new (Context) InitListExpr(
        S.getASTContext(), SourceLocation(), InitArgs, SourceLocation());
    Args[0] = ILE;
    AddListInitializationStep(DestType);
    return;
  }

  // The remaining cases all need a source type.
  if (Args.size() > 1) {
    SetFailed(FK_TooManyInitsForScalar);
    return;
  } else if (isa<InitListExpr>(Args[0])) {
    SetFailed(FK_ParenthesizedListInitForScalar);
    return;
  }

  //    - Otherwise, if the source type is a (possibly cv-qualified) class
  //      type, conversion functions are considered.
  if (!SourceType.isNull() && SourceType->isRecordType()) {
    assert(Initializer && "Initializer must be non-null");
    // For a conversion to _Atomic(T) from either T or a class type derived
    // from T, initialize the T object then convert to _Atomic type.
    bool NeedAtomicConversion = false;
    if (const AtomicType *Atomic = DestType->getAs<AtomicType>()) {
      if (Context.hasSameUnqualifiedType(SourceType, Atomic->getValueType()) ||
          S.IsDerivedFrom(Initializer->getBeginLoc(), SourceType,
                          Atomic->getValueType())) {
        DestType = Atomic->getValueType();
        NeedAtomicConversion = true;
      }
    }

    TryUserDefinedConversion(S, DestType, Kind, Initializer, *this,
                             TopLevelOfInitList);
    MaybeProduceObjCObject(S, *this, Entity);
    if (!Failed() && NeedAtomicConversion)
      AddAtomicConversionStep(Entity.getType());
    return;
  }

  //    - Otherwise, if the initialization is direct-initialization, the source
  //    type is std::nullptr_t, and the destination type is bool, the initial
  //    value of the object being initialized is false.
  if (!SourceType.isNull() && SourceType->isNullPtrType() &&
      DestType->isBooleanType() &&
      Kind.getKind() == InitializationKind::IK_Direct) {
    AddConversionSequenceStep(
        ImplicitConversionSequence::getNullptrToBool(SourceType, DestType,
                                                     Initializer->isGLValue()),
        DestType);
    return;
  }

  //    - Otherwise, the initial value of the object being initialized is the
  //      (possibly converted) value of the initializer expression. Standard
  //      conversions (Clause 4) will be used, if necessary, to convert the
  //      initializer expression to the cv-unqualified version of the
  //      destination type; no user-defined conversions are considered.

  ImplicitConversionSequence ICS
    = S.TryImplicitConversion(Initializer, DestType,
                              /*SuppressUserConversions*/true,
                              Sema::AllowedExplicit::None,
                              /*InOverloadResolution*/ false,
                              /*CStyle=*/Kind.isCStyleOrFunctionalCast(),
                              allowObjCWritebackConversion);

  if (ICS.isStandard() &&
      ICS.Standard.Second == ICK_Writeback_Conversion) {
    // Objective-C ARC writeback conversion.

    // We should copy unless we're passing to an argument explicitly
    // marked 'out'.
    bool ShouldCopy = true;
    if (ParmVarDecl *Param = cast_or_null<ParmVarDecl>(Entity.getDecl()))
      ShouldCopy = (Param->getObjCDeclQualifier() != ParmVarDecl::OBJC_TQ_Out);

    // If there was an lvalue adjustment, add it as a separate conversion.
    if (ICS.Standard.First == ICK_Array_To_Pointer ||
        ICS.Standard.First == ICK_Lvalue_To_Rvalue) {
      ImplicitConversionSequence LvalueICS;
      LvalueICS.setStandard();
      LvalueICS.Standard.setAsIdentityConversion();
      LvalueICS.Standard.setAllToTypes(ICS.Standard.getToType(0));
      LvalueICS.Standard.First = ICS.Standard.First;
      AddConversionSequenceStep(LvalueICS, ICS.Standard.getToType(0));
    }

    AddPassByIndirectCopyRestoreStep(DestType, ShouldCopy);
  } else if (ICS.isBad()) {
    if (isLibstdcxxPointerReturnFalseHack(S, Entity, Initializer))
      AddZeroInitializationStep(Entity.getType());
    else if (DeclAccessPair Found;
             Initializer->getType() == Context.OverloadTy &&
             !S.ResolveAddressOfOverloadedFunction(Initializer, DestType,
                                                   /*Complain=*/false, Found))
      SetFailed(InitializationSequence::FK_AddressOfOverloadFailed);
    else if (Initializer->getType()->isFunctionType() &&
             isExprAnUnaddressableFunction(S, Initializer))
      SetFailed(InitializationSequence::FK_AddressOfUnaddressableFunction);
    else
      SetFailed(InitializationSequence::FK_ConversionFailed);
  } else {
    AddConversionSequenceStep(ICS, DestType, TopLevelOfInitList);

    MaybeProduceObjCObject(S, *this, Entity);
  }
}

InitializationSequence::~InitializationSequence() {
  for (auto &S : Steps)
    S.Destroy();
}

//===----------------------------------------------------------------------===//
// Perform initialization
//===----------------------------------------------------------------------===//
static Sema::AssignmentAction
getAssignmentAction(const InitializedEntity &Entity, bool Diagnose = false) {
  switch(Entity.getKind()) {
  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_New:
  case InitializedEntity::EK_Exception:
  case InitializedEntity::EK_Base:
  case InitializedEntity::EK_Delegating:
    return Sema::AA_Initializing;

  case InitializedEntity::EK_Parameter:
    if (Entity.getDecl() &&
        isa<ObjCMethodDecl>(Entity.getDecl()->getDeclContext()))
      return Sema::AA_Sending;

    return Sema::AA_Passing;

  case InitializedEntity::EK_Parameter_CF_Audited:
    if (Entity.getDecl() &&
      isa<ObjCMethodDecl>(Entity.getDecl()->getDeclContext()))
      return Sema::AA_Sending;

    return !Diagnose ? Sema::AA_Passing : Sema::AA_Passing_CFAudited;

  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_StmtExprResult: // FIXME: Not quite right.
    return Sema::AA_Returning;

  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_RelatedResult:
    // FIXME: Can we tell apart casting vs. converting?
    return Sema::AA_Casting;

  case InitializedEntity::EK_TemplateParameter:
    // This is really initialization, but refer to it as conversion for
    // consistency with CheckConvertedConstantExpression.
    return Sema::AA_Converting;

  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_ParenAggInitMember:
  case InitializedEntity::EK_Binding:
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_BlockElement:
  case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
  case InitializedEntity::EK_LambdaCapture:
  case InitializedEntity::EK_CompoundLiteralInit:
    return Sema::AA_Initializing;
  }

  llvm_unreachable("Invalid EntityKind!");
}

/// Whether we should bind a created object as a temporary when
/// initializing the given entity.
static bool shouldBindAsTemporary(const InitializedEntity &Entity) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_ParenAggInitMember:
  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_StmtExprResult:
  case InitializedEntity::EK_New:
  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_Base:
  case InitializedEntity::EK_Delegating:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_Exception:
  case InitializedEntity::EK_BlockElement:
  case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
  case InitializedEntity::EK_LambdaCapture:
  case InitializedEntity::EK_CompoundLiteralInit:
  case InitializedEntity::EK_TemplateParameter:
    return false;

  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Parameter_CF_Audited:
  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_RelatedResult:
  case InitializedEntity::EK_Binding:
    return true;
  }

  llvm_unreachable("missed an InitializedEntity kind?");
}

/// Whether the given entity, when initialized with an object
/// created for that initialization, requires destruction.
static bool shouldDestroyEntity(const InitializedEntity &Entity) {
  switch (Entity.getKind()) {
    case InitializedEntity::EK_Result:
    case InitializedEntity::EK_StmtExprResult:
    case InitializedEntity::EK_New:
    case InitializedEntity::EK_Base:
    case InitializedEntity::EK_Delegating:
    case InitializedEntity::EK_VectorElement:
    case InitializedEntity::EK_ComplexElement:
    case InitializedEntity::EK_BlockElement:
    case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
    case InitializedEntity::EK_LambdaCapture:
      return false;

    case InitializedEntity::EK_Member:
    case InitializedEntity::EK_ParenAggInitMember:
    case InitializedEntity::EK_Binding:
    case InitializedEntity::EK_Variable:
    case InitializedEntity::EK_Parameter:
    case InitializedEntity::EK_Parameter_CF_Audited:
    case InitializedEntity::EK_TemplateParameter:
    case InitializedEntity::EK_Temporary:
    case InitializedEntity::EK_ArrayElement:
    case InitializedEntity::EK_Exception:
    case InitializedEntity::EK_CompoundLiteralInit:
    case InitializedEntity::EK_RelatedResult:
      return true;
  }

  llvm_unreachable("missed an InitializedEntity kind?");
}

/// Get the location at which initialization diagnostics should appear.
static SourceLocation getInitializationLoc(const InitializedEntity &Entity,
                                           Expr *Initializer) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_Result:
  case InitializedEntity::EK_StmtExprResult:
    return Entity.getReturnLoc();

  case InitializedEntity::EK_Exception:
    return Entity.getThrowLoc();

  case InitializedEntity::EK_Variable:
  case InitializedEntity::EK_Binding:
    return Entity.getDecl()->getLocation();

  case InitializedEntity::EK_LambdaCapture:
    return Entity.getCaptureLoc();

  case InitializedEntity::EK_ArrayElement:
  case InitializedEntity::EK_Member:
  case InitializedEntity::EK_ParenAggInitMember:
  case InitializedEntity::EK_Parameter:
  case InitializedEntity::EK_Parameter_CF_Audited:
  case InitializedEntity::EK_TemplateParameter:
  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_New:
  case InitializedEntity::EK_Base:
  case InitializedEntity::EK_Delegating:
  case InitializedEntity::EK_VectorElement:
  case InitializedEntity::EK_ComplexElement:
  case InitializedEntity::EK_BlockElement:
  case InitializedEntity::EK_LambdaToBlockConversionBlockElement:
  case InitializedEntity::EK_CompoundLiteralInit:
  case InitializedEntity::EK_RelatedResult:
    return Initializer->getBeginLoc();
  }
  llvm_unreachable("missed an InitializedEntity kind?");
}

/// Make a (potentially elidable) temporary copy of the object
/// provided by the given initializer by calling the appropriate copy
/// constructor.
///
/// \param S The Sema object used for type-checking.
///
/// \param T The type of the temporary object, which must either be
/// the type of the initializer expression or a superclass thereof.
///
/// \param Entity The entity being initialized.
///
/// \param CurInit The initializer expression.
///
/// \param IsExtraneousCopy Whether this is an "extraneous" copy that
/// is permitted in C++03 (but not C++0x) when binding a reference to
/// an rvalue.
///
/// \returns An expression that copies the initializer expression into
/// a temporary object, or an error expression if a copy could not be
/// created.
static ExprResult CopyObject(Sema &S,
                             QualType T,
                             const InitializedEntity &Entity,
                             ExprResult CurInit,
                             bool IsExtraneousCopy) {
  if (CurInit.isInvalid())
    return CurInit;
  // Determine which class type we're copying to.
  Expr *CurInitExpr = (Expr *)CurInit.get();
  CXXRecordDecl *Class = nullptr;
  if (const RecordType *Record = T->getAs<RecordType>())
    Class = cast<CXXRecordDecl>(Record->getDecl());
  if (!Class)
    return CurInit;

  SourceLocation Loc = getInitializationLoc(Entity, CurInit.get());

  // Make sure that the type we are copying is complete.
  if (S.RequireCompleteType(Loc, T, diag::err_temp_copy_incomplete))
    return CurInit;

  // Perform overload resolution using the class's constructors. Per
  // C++11 [dcl.init]p16, second bullet for class types, this initialization
  // is direct-initialization.
  OverloadCandidateSet CandidateSet(Loc, OverloadCandidateSet::CSK_Normal);
  DeclContext::lookup_result Ctors = S.LookupConstructors(Class);

  OverloadCandidateSet::iterator Best;
  switch (ResolveConstructorOverload(
      S, Loc, CurInitExpr, CandidateSet, T, Ctors, Best,
      /*CopyInitializing=*/false, /*AllowExplicit=*/true,
      /*OnlyListConstructors=*/false, /*IsListInit=*/false,
      /*RequireActualConstructor=*/false,
      /*SecondStepOfCopyInit=*/true)) {
  case OR_Success:
    break;

  case OR_No_Viable_Function:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(
            Loc, S.PDiag(IsExtraneousCopy && !S.isSFINAEContext()
                             ? diag::ext_rvalue_to_reference_temp_copy_no_viable
                             : diag::err_temp_copy_no_viable)
                     << (int)Entity.getKind() << CurInitExpr->getType()
                     << CurInitExpr->getSourceRange()),
        S, OCD_AllCandidates, CurInitExpr);
    if (!IsExtraneousCopy || S.isSFINAEContext())
      return ExprError();
    return CurInit;

  case OR_Ambiguous:
    CandidateSet.NoteCandidates(
        PartialDiagnosticAt(Loc, S.PDiag(diag::err_temp_copy_ambiguous)
                                     << (int)Entity.getKind()
                                     << CurInitExpr->getType()
                                     << CurInitExpr->getSourceRange()),
        S, OCD_AmbiguousCandidates, CurInitExpr);
    return ExprError();

  case OR_Deleted:
    S.Diag(Loc, diag::err_temp_copy_deleted)
      << (int)Entity.getKind() << CurInitExpr->getType()
      << CurInitExpr->getSourceRange();
    S.NoteDeletedFunction(Best->Function);
    return ExprError();
  }

  bool HadMultipleCandidates = CandidateSet.size() > 1;

  CXXConstructorDecl *Constructor = cast<CXXConstructorDecl>(Best->Function);
  SmallVector<Expr*, 8> ConstructorArgs;
  CurInit.get(); // Ownership transferred into MultiExprArg, below.

  S.CheckConstructorAccess(Loc, Constructor, Best->FoundDecl, Entity,
                           IsExtraneousCopy);

  if (IsExtraneousCopy) {
    // If this is a totally extraneous copy for C++03 reference
    // binding purposes, just return the original initialization
    // expression. We don't generate an (elided) copy operation here
    // because doing so would require us to pass down a flag to avoid
    // infinite recursion, where each step adds another extraneous,
    // elidable copy.

    // Instantiate the default arguments of any extra parameters in
    // the selected copy constructor, as if we were going to create a
    // proper call to the copy constructor.
    for (unsigned I = 1, N = Constructor->getNumParams(); I != N; ++I) {
      ParmVarDecl *Parm = Constructor->getParamDecl(I);
      if (S.RequireCompleteType(Loc, Parm->getType(),
                                diag::err_call_incomplete_argument))
        break;

      // Build the default argument expression; we don't actually care
      // if this succeeds or not, because this routine will complain
      // if there was a problem.
      S.BuildCXXDefaultArgExpr(Loc, Constructor, Parm);
    }

    return CurInitExpr;
  }

  // Determine the arguments required to actually perform the
  // constructor call (we might have derived-to-base conversions, or
  // the copy constructor may have default arguments).
  if (S.CompleteConstructorCall(Constructor, T, CurInitExpr, Loc,
                                ConstructorArgs))
    return ExprError();

  // C++0x [class.copy]p32:
  //   When certain criteria are met, an implementation is allowed to
  //   omit the copy/move construction of a class object, even if the
  //   copy/move constructor and/or destructor for the object have
  //   side effects. [...]
  //     - when a temporary class object that has not been bound to a
  //       reference (12.2) would be copied/moved to a class object
  //       with the same cv-unqualified type, the copy/move operation
  //       can be omitted by constructing the temporary object
  //       directly into the target of the omitted copy/move
  //
  // Note that the other three bullets are handled elsewhere. Copy
  // elision for return statements and throw expressions are handled as part
  // of constructor initialization, while copy elision for exception handlers
  // is handled by the run-time.
  //
  // FIXME: If the function parameter is not the same type as the temporary, we
  // should still be able to elide the copy, but we don't have a way to
  // represent in the AST how much should be elided in this case.
  bool Elidable =
      CurInitExpr->isTemporaryObject(S.Context, Class) &&
      S.Context.hasSameUnqualifiedType(
          Best->Function->getParamDecl(0)->getType().getNonReferenceType(),
          CurInitExpr->getType());

  // Actually perform the constructor call.
  CurInit = S.BuildCXXConstructExpr(
      Loc, T, Best->FoundDecl, Constructor, Elidable, ConstructorArgs,
      HadMultipleCandidates,
      /*ListInit*/ false,
      /*StdInitListInit*/ false,
      /*ZeroInit*/ false, CXXConstructionKind::Complete, SourceRange());

  // If we're supposed to bind temporaries, do so.
  if (!CurInit.isInvalid() && shouldBindAsTemporary(Entity))
    CurInit = S.MaybeBindToTemporary(CurInit.getAs<Expr>());
  return CurInit;
}

/// Check whether elidable copy construction for binding a reference to
/// a temporary would have succeeded if we were building in C++98 mode, for
/// -Wc++98-compat.
static void CheckCXX98CompatAccessibleCopy(Sema &S,
                                           const InitializedEntity &Entity,
                                           Expr *CurInitExpr) {
  assert(S.getLangOpts().CPlusPlus11);

  const RecordType *Record = CurInitExpr->getType()->getAs<RecordType>();
  if (!Record)
    return;

  SourceLocation Loc = getInitializationLoc(Entity, CurInitExpr);
  if (S.Diags.isIgnored(diag::warn_cxx98_compat_temp_copy, Loc))
    return;

  // Find constructors which would have been considered.
  OverloadCandidateSet CandidateSet(Loc, OverloadCandidateSet::CSK_Normal);
  DeclContext::lookup_result Ctors =
      S.LookupConstructors(cast<CXXRecordDecl>(Record->getDecl()));

  // Perform overload resolution.
  OverloadCandidateSet::iterator Best;
  OverloadingResult OR = ResolveConstructorOverload(
      S, Loc, CurInitExpr, CandidateSet, CurInitExpr->getType(), Ctors, Best,
      /*CopyInitializing=*/false, /*AllowExplicit=*/true,
      /*OnlyListConstructors=*/false, /*IsListInit=*/false,
      /*RequireActualConstructor=*/false,
      /*SecondStepOfCopyInit=*/true);

  PartialDiagnostic Diag = S.PDiag(diag::warn_cxx98_compat_temp_copy)
    << OR << (int)Entity.getKind() << CurInitExpr->getType()
    << CurInitExpr->getSourceRange();

  switch (OR) {
  case OR_Success:
    S.CheckConstructorAccess(Loc, cast<CXXConstructorDecl>(Best->Function),
                             Best->FoundDecl, Entity, Diag);
    // FIXME: Check default arguments as far as that's possible.
    break;

  case OR_No_Viable_Function:
    CandidateSet.NoteCandidates(PartialDiagnosticAt(Loc, Diag), S,
                                OCD_AllCandidates, CurInitExpr);
    break;

  case OR_Ambiguous:
    CandidateSet.NoteCandidates(PartialDiagnosticAt(Loc, Diag), S,
                                OCD_AmbiguousCandidates, CurInitExpr);
    break;

  case OR_Deleted:
    S.Diag(Loc, Diag);
    S.NoteDeletedFunction(Best->Function);
    break;
  }
}

void InitializationSequence::PrintInitLocationNote(Sema &S,
                                              const InitializedEntity &Entity) {
  if (Entity.isParamOrTemplateParamKind() && Entity.getDecl()) {
    if (Entity.getDecl()->getLocation().isInvalid())
      return;

    if (Entity.getDecl()->getDeclName())
      S.Diag(Entity.getDecl()->getLocation(), diag::note_parameter_named_here)
        << Entity.getDecl()->getDeclName();
    else
      S.Diag(Entity.getDecl()->getLocation(), diag::note_parameter_here);
  }
  else if (Entity.getKind() == InitializedEntity::EK_RelatedResult &&
           Entity.getMethodDecl())
    S.Diag(Entity.getMethodDecl()->getLocation(),
           diag::note_method_return_type_change)
      << Entity.getMethodDecl()->getDeclName();
}

/// Returns true if the parameters describe a constructor initialization of
/// an explicit temporary object, e.g. "Point(x, y)".
static bool isExplicitTemporary(const InitializedEntity &Entity,
                                const InitializationKind &Kind,
                                unsigned NumArgs) {
  switch (Entity.getKind()) {
  case InitializedEntity::EK_Temporary:
  case InitializedEntity::EK_CompoundLiteralInit:
  case InitializedEntity::EK_RelatedResult:
    break;
  default:
    return false;
  }

  switch (Kind.getKind()) {
  case InitializationKind::IK_DirectList:
    return true;
  // FIXME: Hack to work around cast weirdness.
  case InitializationKind::IK_Direct:
  case InitializationKind::IK_Value:
    return NumArgs != 1;
  default:
    return false;
  }
}

static ExprResult
PerformConstructorInitialization(Sema &S,
                                 const InitializedEntity &Entity,
                                 const InitializationKind &Kind,
                                 MultiExprArg Args,
                                 const InitializationSequence::Step& Step,
                                 bool &ConstructorInitRequiresZeroInit,
                                 bool IsListInitialization,
                                 bool IsStdInitListInitialization,
                                 SourceLocation LBraceLoc,
                                 SourceLocation RBraceLoc) {
  unsigned NumArgs = Args.size();
  CXXConstructorDecl *Constructor
    = cast<CXXConstructorDecl>(Step.Function.Function);
  bool HadMultipleCandidates = Step.Function.HadMultipleCandidates;

  // Build a call to the selected constructor.
  SmallVector<Expr*, 8> ConstructorArgs;
  SourceLocation Loc = (Kind.isCopyInit() && Kind.getEqualLoc().isValid())
                         ? Kind.getEqualLoc()
                         : Kind.getLocation();

  if (Kind.getKind() == InitializationKind::IK_Default) {
    // Force even a trivial, implicit default constructor to be
    // semantically checked. We do this explicitly because we don't build
    // the definition for completely trivial constructors.
    assert(Constructor->getParent() && "No parent class for constructor.");
    if (Constructor->isDefaulted() && Constructor->isDefaultConstructor() &&
        Constructor->isTrivial() && !Constructor->isUsed(false)) {
      S.runWithSufficientStackSpace(Loc, [&] {
        S.DefineImplicitDefaultConstructor(Loc, Constructor);
      });
    }
  }

  ExprResult CurInit((Expr *)nullptr);

  // C++ [over.match.copy]p1:
  //   - When initializing a temporary to be bound to the first parameter
  //     of a constructor that takes a reference to possibly cv-qualified
  //     T as its first argument, called with a single argument in the
  //     context of direct-initialization, explicit conversion functions
  //     are also considered.
  bool AllowExplicitConv =
      Kind.AllowExplicit() && !Kind.isCopyInit() && Args.size() == 1 &&
      hasCopyOrMoveCtorParam(S.Context,
                             getConstructorInfo(Step.Function.FoundDecl));

  // A smart pointer constructed from a nullable pointer is nullable.
  if (NumArgs == 1 && !Kind.isExplicitCast())
    S.diagnoseNullableToNonnullConversion(
        Entity.getType(), Args.front()->getType(), Kind.getLocation());

  // Determine the arguments required to actually perform the constructor
  // call.
  if (S.CompleteConstructorCall(Constructor, Step.Type, Args, Loc,
                                ConstructorArgs, AllowExplicitConv,
                                IsListInitialization))
    return ExprError();

  if (isExplicitTemporary(Entity, Kind, NumArgs)) {
    // An explicitly-constructed temporary, e.g., X(1, 2).
    if (S.DiagnoseUseOfDecl(Step.Function.FoundDecl, Loc))
      return ExprError();

    TypeSourceInfo *TSInfo = Entity.getTypeSourceInfo();
    if (!TSInfo)
      TSInfo = S.Context.getTrivialTypeSourceInfo(Entity.getType(), Loc);
    SourceRange ParenOrBraceRange =
        (Kind.getKind() == InitializationKind::IK_DirectList)
        ? SourceRange(LBraceLoc, RBraceLoc)
        : Kind.getParenOrBraceRange();

    CXXConstructorDecl *CalleeDecl = Constructor;
    if (auto *Shadow = dyn_cast<ConstructorUsingShadowDecl>(
            Step.Function.FoundDecl.getDecl())) {
      CalleeDecl = S.findInheritingConstructor(Loc, Constructor, Shadow);
    }
    S.MarkFunctionReferenced(Loc, CalleeDecl);

    CurInit = S.CheckForImmediateInvocation(
        CXXTemporaryObjectExpr::Create(
            S.Context, CalleeDecl,
            Entity.getType().getNonLValueExprType(S.Context), TSInfo,
            ConstructorArgs, ParenOrBraceRange, HadMultipleCandidates,
            IsListInitialization, IsStdInitListInitialization,
            ConstructorInitRequiresZeroInit),
        CalleeDecl);
  } else {
    CXXConstructionKind ConstructKind = CXXConstructionKind::Complete;

    if (Entity.getKind() == InitializedEntity::EK_Base) {
      ConstructKind = Entity.getBaseSpecifier()->isVirtual()
                          ? CXXConstructionKind::VirtualBase
                          : CXXConstructionKind::NonVirtualBase;
    } else if (Entity.getKind() == InitializedEntity::EK_Delegating) {
      ConstructKind = CXXConstructionKind::Delegating;
    }

    // Only get the parenthesis or brace range if it is a list initialization or
    // direct construction.
    SourceRange ParenOrBraceRange;
    if (IsListInitialization)
      ParenOrBraceRange = SourceRange(LBraceLoc, RBraceLoc);
    else if (Kind.getKind() == InitializationKind::IK_Direct)
      ParenOrBraceRange = Kind.getParenOrBraceRange();

    // If the entity allows NRVO, mark the construction as elidable
    // unconditionally.
    if (Entity.allowsNRVO())
      CurInit = S.BuildCXXConstructExpr(Loc, Step.Type,
                                        Step.Function.FoundDecl,
                                        Constructor, /*Elidable=*/true,
                                        ConstructorArgs,
                                        HadMultipleCandidates,
                                        IsListInitialization,
                                        IsStdInitListInitialization,
                                        ConstructorInitRequiresZeroInit,
                                        ConstructKind,
                                        ParenOrBraceRange);
    else
      CurInit = S.BuildCXXConstructExpr(Loc, Step.Type,
                                        Step.Function.FoundDecl,
                                        Constructor,
                                        ConstructorArgs,
                                        HadMultipleCandidates,
                                        IsListInitialization,
                                        IsStdInitListInitialization,
                                        ConstructorInitRequiresZeroInit,
                                        ConstructKind,
                                        ParenOrBraceRange);
  }
  if (CurInit.isInvalid())
    return ExprError();

  // Only check access if all of that succeeded.
  S.CheckConstructorAccess(Loc, Constructor, Step.Function.FoundDecl, Entity);
  if (S.DiagnoseUseOfDecl(Step.Function.FoundDecl, Loc))
    return ExprError();

  if (const ArrayType *AT = S.Context.getAsArrayType(Entity.getType()))
    if (checkDestructorReference(S.Context.getBaseElementType(AT), Loc, S))
      return ExprError();

  if (shouldBindAsTemporary(Entity))
    CurInit = S.MaybeBindToTemporary(CurInit.get());

  return CurInit;
}

void Sema::checkInitializerLifetime(const InitializedEntity &Entity,
                                    Expr *Init) {
  return sema::checkExprLifetime(*this, Entity, Init);
}

static void DiagnoseNarrowingInInitList(Sema &S,
                                        const ImplicitConversionSequence &ICS,
                                        QualType PreNarrowingType,
                                        QualType EntityType,
                                        const Expr *PostInit);

static void CheckC23ConstexprInitConversion(Sema &S, QualType FromType,
                                            QualType ToType, Expr *Init);

/// Provide warnings when std::move is used on construction.
static void CheckMoveOnConstruction(Sema &S, const Expr *InitExpr,
                                    bool IsReturnStmt) {
  if (!InitExpr)
    return;

  if (S.inTemplateInstantiation())
    return;

  QualType DestType = InitExpr->getType();
  if (!DestType->isRecordType())
    return;

  unsigned DiagID = 0;
  if (IsReturnStmt) {
    const CXXConstructExpr *CCE =
        dyn_cast<CXXConstructExpr>(InitExpr->IgnoreParens());
    if (!CCE || CCE->getNumArgs() != 1)
      return;

    if (!CCE->getConstructor()->isCopyOrMoveConstructor())
      return;

    InitExpr = CCE->getArg(0)->IgnoreImpCasts();
  }

  // Find the std::move call and get the argument.
  const CallExpr *CE = dyn_cast<CallExpr>(InitExpr->IgnoreParens());
  if (!CE || !CE->isCallToStdMove())
    return;

  const Expr *Arg = CE->getArg(0)->IgnoreImplicit();

  if (IsReturnStmt) {
    const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Arg->IgnoreParenImpCasts());
    if (!DRE || DRE->refersToEnclosingVariableOrCapture())
      return;

    const VarDecl *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD || !VD->hasLocalStorage())
      return;

    // __block variables are not moved implicitly.
    if (VD->hasAttr<BlocksAttr>())
      return;

    QualType SourceType = VD->getType();
    if (!SourceType->isRecordType())
      return;

    if (!S.Context.hasSameUnqualifiedType(DestType, SourceType)) {
      return;
    }

    // If we're returning a function parameter, copy elision
    // is not possible.
    if (isa<ParmVarDecl>(VD))
      DiagID = diag::warn_redundant_move_on_return;
    else
      DiagID = diag::warn_pessimizing_move_on_return;
  } else {
    DiagID = diag::warn_pessimizing_move_on_initialization;
    const Expr *ArgStripped = Arg->IgnoreImplicit()->IgnoreParens();
    if (!ArgStripped->isPRValue() || !ArgStripped->getType()->isRecordType())
      return;
  }

  S.Diag(CE->getBeginLoc(), DiagID);

  // Get all the locations for a fix-it.  Don't emit the fix-it if any location
  // is within a macro.
  SourceLocation CallBegin = CE->getCallee()->getBeginLoc();
  if (CallBegin.isMacroID())
    return;
  SourceLocation RParen = CE->getRParenLoc();
  if (RParen.isMacroID())
    return;
  SourceLocation LParen;
  SourceLocation ArgLoc = Arg->getBeginLoc();

  // Special testing for the argument location.  Since the fix-it needs the
  // location right before the argument, the argument location can be in a
  // macro only if it is at the beginning of the macro.
  while (ArgLoc.isMacroID() &&
         S.getSourceManager().isAtStartOfImmediateMacroExpansion(ArgLoc)) {
    ArgLoc = S.getSourceManager().getImmediateExpansionRange(ArgLoc).getBegin();
  }

  if (LParen.isMacroID())
    return;

  LParen = ArgLoc.getLocWithOffset(-1);

  S.Diag(CE->getBeginLoc(), diag::note_remove_move)
      << FixItHint::CreateRemoval(SourceRange(CallBegin, LParen))
      << FixItHint::CreateRemoval(SourceRange(RParen, RParen));
}

static void CheckForNullPointerDereference(Sema &S, const Expr *E) {
  // Check to see if we are dereferencing a null pointer.  If so, this is
  // undefined behavior, so warn about it.  This only handles the pattern
  // "*null", which is a very syntactic check.
  if (const UnaryOperator *UO = dyn_cast<UnaryOperator>(E->IgnoreParenCasts()))
    if (UO->getOpcode() == UO_Deref &&
        UO->getSubExpr()->IgnoreParenCasts()->
        isNullPointerConstant(S.Context, Expr::NPC_ValueDependentIsNotNull)) {
    S.DiagRuntimeBehavior(UO->getOperatorLoc(), UO,
                          S.PDiag(diag::warn_binding_null_to_reference)
                            << UO->getSubExpr()->getSourceRange());
  }
}

MaterializeTemporaryExpr *
Sema::CreateMaterializeTemporaryExpr(QualType T, Expr *Temporary,
                                     bool BoundToLvalueReference) {
  auto MTE = new (Context)
      MaterializeTemporaryExpr(T, Temporary, BoundToLvalueReference);

  // Order an ExprWithCleanups for lifetime marks.
  //
  // TODO: It'll be good to have a single place to check the access of the
  // destructor and generate ExprWithCleanups for various uses. Currently these
  // are done in both CreateMaterializeTemporaryExpr and MaybeBindToTemporary,
  // but there may be a chance to merge them.
  Cleanup.setExprNeedsCleanups(false);
  if (isInLifetimeExtendingContext()) {
    auto &Record = ExprEvalContexts.back();
    Record.ForRangeLifetimeExtendTemps.push_back(MTE);
  }
  return MTE;
}

ExprResult Sema::TemporaryMaterializationConversion(Expr *E) {
  // In C++98, we don't want to implicitly create an xvalue.
  // FIXME: This means that AST consumers need to deal with "prvalues" that
  // denote materialized temporaries. Maybe we should add another ValueKind
  // for "xvalue pretending to be a prvalue" for C++98 support.
  if (!E->isPRValue() || !getLangOpts().CPlusPlus11)
    return E;

  // C++1z [conv.rval]/1: T shall be a complete type.
  // FIXME: Does this ever matter (can we form a prvalue of incomplete type)?
  // If so, we should check for a non-abstract class type here too.
  QualType T = E->getType();
  if (RequireCompleteType(E->getExprLoc(), T, diag::err_incomplete_type))
    return ExprError();

  return CreateMaterializeTemporaryExpr(E->getType(), E, false);
}

ExprResult Sema::PerformQualificationConversion(Expr *E, QualType Ty,
                                                ExprValueKind VK,
                                                CheckedConversionKind CCK) {

  CastKind CK = CK_NoOp;

  if (VK == VK_PRValue) {
    auto PointeeTy = Ty->getPointeeType();
    auto ExprPointeeTy = E->getType()->getPointeeType();
    if (!PointeeTy.isNull() &&
        PointeeTy.getAddressSpace() != ExprPointeeTy.getAddressSpace())
      CK = CK_AddressSpaceConversion;
  } else if (Ty.getAddressSpace() != E->getType().getAddressSpace()) {
    CK = CK_AddressSpaceConversion;
  }

  return ImpCastExprToType(E, Ty, CK, VK, /*BasePath=*/nullptr, CCK);
}

ExprResult InitializationSequence::Perform(Sema &S,
                                           const InitializedEntity &Entity,
                                           const InitializationKind &Kind,
                                           MultiExprArg Args,
                                           QualType *ResultType) {
  if (Failed()) {
    Diagnose(S, Entity, Kind, Args);
    return ExprError();
  }
  if (!ZeroInitializationFixit.empty()) {
    const Decl *D = Entity.getDecl();
    const auto *VD = dyn_cast_or_null<VarDecl>(D);
    QualType DestType = Entity.getType();

    // The initialization would have succeeded with this fixit. Since the fixit
    // is on the error, we need to build a valid AST in this case, so this isn't
    // handled in the Failed() branch above.
    if (!DestType->isRecordType() && VD && VD->isConstexpr()) {
      // Use a more useful diagnostic for constexpr variables.
      S.Diag(Kind.getLocation(), diag::err_constexpr_var_requires_const_init)
          << VD
          << FixItHint::CreateInsertion(ZeroInitializationFixitLoc,
                                        ZeroInitializationFixit);
    } else {
      unsigned DiagID = diag::err_default_init_const;
      if (S.getLangOpts().MSVCCompat && D && D->hasAttr<SelectAnyAttr>())
        DiagID = diag::ext_default_init_const;

      S.Diag(Kind.getLocation(), DiagID)
          << DestType << (bool)DestType->getAs<RecordType>()
          << FixItHint::CreateInsertion(ZeroInitializationFixitLoc,
                                        ZeroInitializationFixit);
    }
  }

  if (getKind() == DependentSequence) {
    // If the declaration is a non-dependent, incomplete array type
    // that has an initializer, then its type will be completed once
    // the initializer is instantiated.
    if (ResultType && !Entity.getType()->isDependentType() &&
        Args.size() == 1) {
      QualType DeclType = Entity.getType();
      if (const IncompleteArrayType *ArrayT
                           = S.Context.getAsIncompleteArrayType(DeclType)) {
        // FIXME: We don't currently have the ability to accurately
        // compute the length of an initializer list without
        // performing full type-checking of the initializer list
        // (since we have to determine where braces are implicitly
        // introduced and such).  So, we fall back to making the array
        // type a dependently-sized array type with no specified
        // bound.
        if (isa<InitListExpr>((Expr *)Args[0])) {
          SourceRange Brackets;

          // Scavange the location of the brackets from the entity, if we can.
          if (auto *DD = dyn_cast_or_null<DeclaratorDecl>(Entity.getDecl())) {
            if (TypeSourceInfo *TInfo = DD->getTypeSourceInfo()) {
              TypeLoc TL = TInfo->getTypeLoc();
              if (IncompleteArrayTypeLoc ArrayLoc =
                      TL.getAs<IncompleteArrayTypeLoc>())
                Brackets = ArrayLoc.getBracketsRange();
            }
          }

          *ResultType
            = S.Context.getDependentSizedArrayType(ArrayT->getElementType(),
                                                   /*NumElts=*/nullptr,
                                                   ArrayT->getSizeModifier(),
                                       ArrayT->getIndexTypeCVRQualifiers(),
                                                   Brackets);
        }

      }
    }
    if (Kind.getKind() == InitializationKind::IK_Direct &&
        !Kind.isExplicitCast()) {
      // Rebuild the ParenListExpr.
      SourceRange ParenRange = Kind.getParenOrBraceRange();
      return S.ActOnParenListExpr(ParenRange.getBegin(), ParenRange.getEnd(),
                                  Args);
    }
    assert(Kind.getKind() == InitializationKind::IK_Copy ||
           Kind.isExplicitCast() ||
           Kind.getKind() == InitializationKind::IK_DirectList);
    return ExprResult(Args[0]);
  }

  // No steps means no initialization.
  if (Steps.empty())
    return ExprResult((Expr *)nullptr);

  if (S.getLangOpts().CPlusPlus11 && Entity.getType()->isReferenceType() &&
      Args.size() == 1 && isa<InitListExpr>(Args[0]) &&
      !Entity.isParamOrTemplateParamKind()) {
    // Produce a C++98 compatibility warning if we are initializing a reference
    // from an initializer list. For parameters, we produce a better warning
    // elsewhere.
    Expr *Init = Args[0];
    S.Diag(Init->getBeginLoc(), diag::warn_cxx98_compat_reference_list_init)
        << Init->getSourceRange();
  }

  if (S.getLangOpts().MicrosoftExt && Args.size() == 1 &&
      isa<PredefinedExpr>(Args[0]) && Entity.getType()->isArrayType()) {
    // Produce a Microsoft compatibility warning when initializing from a
    // predefined expression since MSVC treats predefined expressions as string
    // literals.
    Expr *Init = Args[0];
    S.Diag(Init->getBeginLoc(), diag::ext_init_from_predefined) << Init;
  }

  // OpenCL v2.0 s6.13.11.1. atomic variables can be initialized in global scope
  QualType ETy = Entity.getType();
  bool HasGlobalAS = ETy.hasAddressSpace() &&
                     ETy.getAddressSpace() == LangAS::opencl_global;

  if (S.getLangOpts().OpenCLVersion >= 200 &&
      ETy->isAtomicType() && !HasGlobalAS &&
      Entity.getKind() == InitializedEntity::EK_Variable && Args.size() > 0) {
    S.Diag(Args[0]->getBeginLoc(), diag::err_opencl_atomic_init)
        << 1
        << SourceRange(Entity.getDecl()->getBeginLoc(), Args[0]->getEndLoc());
    return ExprError();
  }

  QualType DestType = Entity.getType().getNonReferenceType();
  // FIXME: Ugly hack around the fact that Entity.getType() is not
  // the same as Entity.getDecl()->getType() in cases involving type merging,
  //  and we want latter when it makes sense.
  if (ResultType)
    *ResultType = Entity.getDecl() ? Entity.getDecl()->getType() :
                                     Entity.getType();

  ExprResult CurInit((Expr *)nullptr);
  SmallVector<Expr*, 4> ArrayLoopCommonExprs;

  // HLSL allows vector initialization to function like list initialization, but
  // use the syntax of a C++-like constructor.
  bool IsHLSLVectorInit = S.getLangOpts().HLSL && DestType->isExtVectorType() &&
                          isa<InitListExpr>(Args[0]);
  (void)IsHLSLVectorInit;

  // For initialization steps that start with a single initializer,
  // grab the only argument out the Args and place it into the "current"
  // initializer.
  switch (Steps.front().Kind) {
  case SK_ResolveAddressOfOverloadedFunction:
  case SK_CastDerivedToBasePRValue:
  case SK_CastDerivedToBaseXValue:
  case SK_CastDerivedToBaseLValue:
  case SK_BindReference:
  case SK_BindReferenceToTemporary:
  case SK_FinalCopy:
  case SK_ExtraneousCopyToTemporary:
  case SK_UserConversion:
  case SK_QualificationConversionLValue:
  case SK_QualificationConversionXValue:
  case SK_QualificationConversionPRValue:
  case SK_FunctionReferenceConversion:
  case SK_AtomicConversion:
  case SK_ConversionSequence:
  case SK_ConversionSequenceNoNarrowing:
  case SK_ListInitialization:
  case SK_UnwrapInitList:
  case SK_RewrapInitList:
  case SK_CAssignment:
  case SK_StringInit:
  case SK_ObjCObjectConversion:
  case SK_ArrayLoopIndex:
  case SK_ArrayLoopInit:
  case SK_ArrayInit:
  case SK_GNUArrayInit:
  case SK_ParenthesizedArrayInit:
  case SK_PassByIndirectCopyRestore:
  case SK_PassByIndirectRestore:
  case SK_ProduceObjCObject:
  case SK_StdInitializerList:
  case SK_OCLSamplerInit:
  case SK_OCLZeroOpaqueType: {
    assert(Args.size() == 1 || IsHLSLVectorInit);
    CurInit = Args[0];
    if (!CurInit.get()) return ExprError();
    break;
  }

  case SK_ConstructorInitialization:
  case SK_ConstructorInitializationFromList:
  case SK_StdInitializerListConstructorCall:
  case SK_ZeroInitialization:
  case SK_ParenthesizedListInit:
    break;
  }

  // Promote from an unevaluated context to an unevaluated list context in
  // C++11 list-initialization; we need to instantiate entities usable in
  // constant expressions here in order to perform narrowing checks =(
  EnterExpressionEvaluationContext Evaluated(
      S, EnterExpressionEvaluationContext::InitList,
      isa_and_nonnull<InitListExpr>(CurInit.get()));

  // C++ [class.abstract]p2:
  //   no objects of an abstract class can be created except as subobjects
  //   of a class derived from it
  auto checkAbstractType = [&](QualType T) -> bool {
    if (Entity.getKind() == InitializedEntity::EK_Base ||
        Entity.getKind() == InitializedEntity::EK_Delegating)
      return false;
    return S.RequireNonAbstractType(Kind.getLocation(), T,
                                    diag::err_allocation_of_abstract_type);
  };

  // Walk through the computed steps for the initialization sequence,
  // performing the specified conversions along the way.
  bool ConstructorInitRequiresZeroInit = false;
  for (step_iterator Step = step_begin(), StepEnd = step_end();
       Step != StepEnd; ++Step) {
    if (CurInit.isInvalid())
      return ExprError();

    QualType SourceType = CurInit.get() ? CurInit.get()->getType() : QualType();

    switch (Step->Kind) {
    case SK_ResolveAddressOfOverloadedFunction:
      // Overload resolution determined which function invoke; update the
      // initializer to reflect that choice.
      S.CheckAddressOfMemberAccess(CurInit.get(), Step->Function.FoundDecl);
      if (S.DiagnoseUseOfDecl(Step->Function.FoundDecl, Kind.getLocation()))
        return ExprError();
      CurInit = S.FixOverloadedFunctionReference(CurInit,
                                                 Step->Function.FoundDecl,
                                                 Step->Function.Function);
      // We might get back another placeholder expression if we resolved to a
      // builtin.
      if (!CurInit.isInvalid())
        CurInit = S.CheckPlaceholderExpr(CurInit.get());
      break;

    case SK_CastDerivedToBasePRValue:
    case SK_CastDerivedToBaseXValue:
    case SK_CastDerivedToBaseLValue: {
      // We have a derived-to-base cast that produces either an rvalue or an
      // lvalue. Perform that cast.

      CXXCastPath BasePath;

      // Casts to inaccessible base classes are allowed with C-style casts.
      bool IgnoreBaseAccess = Kind.isCStyleOrFunctionalCast();
      if (S.CheckDerivedToBaseConversion(
              SourceType, Step->Type, CurInit.get()->getBeginLoc(),
              CurInit.get()->getSourceRange(), &BasePath, IgnoreBaseAccess))
        return ExprError();

      ExprValueKind VK =
          Step->Kind == SK_CastDerivedToBaseLValue
              ? VK_LValue
              : (Step->Kind == SK_CastDerivedToBaseXValue ? VK_XValue
                                                          : VK_PRValue);
      CurInit = ImplicitCastExpr::Create(S.Context, Step->Type,
                                         CK_DerivedToBase, CurInit.get(),
                                         &BasePath, VK, FPOptionsOverride());
      break;
    }

    case SK_BindReference:
      // Reference binding does not have any corresponding ASTs.

      // Check exception specifications
      if (S.CheckExceptionSpecCompatibility(CurInit.get(), DestType))
        return ExprError();

      // We don't check for e.g. function pointers here, since address
      // availability checks should only occur when the function first decays
      // into a pointer or reference.
      if (CurInit.get()->getType()->isFunctionProtoType()) {
        if (auto *DRE = dyn_cast<DeclRefExpr>(CurInit.get()->IgnoreParens())) {
          if (auto *FD = dyn_cast<FunctionDecl>(DRE->getDecl())) {
            if (!S.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                                     DRE->getBeginLoc()))
              return ExprError();
          }
        }
      }

      CheckForNullPointerDereference(S, CurInit.get());
      break;

    case SK_BindReferenceToTemporary: {
      // Make sure the "temporary" is actually an rvalue.
      assert(CurInit.get()->isPRValue() && "not a temporary");

      // Check exception specifications
      if (S.CheckExceptionSpecCompatibility(CurInit.get(), DestType))
        return ExprError();

      QualType MTETy = Step->Type;

      // When this is an incomplete array type (such as when this is
      // initializing an array of unknown bounds from an init list), use THAT
      // type instead so that we propagate the array bounds.
      if (MTETy->isIncompleteArrayType() &&
          !CurInit.get()->getType()->isIncompleteArrayType() &&
          S.Context.hasSameType(
              MTETy->getPointeeOrArrayElementType(),
              CurInit.get()->getType()->getPointeeOrArrayElementType()))
        MTETy = CurInit.get()->getType();

      // Materialize the temporary into memory.
      MaterializeTemporaryExpr *MTE = S.CreateMaterializeTemporaryExpr(
          MTETy, CurInit.get(), Entity.getType()->isLValueReferenceType());
      CurInit = MTE;

      // If we're extending this temporary to automatic storage duration -- we
      // need to register its cleanup during the full-expression's cleanups.
      if (MTE->getStorageDuration() == SD_Automatic &&
          MTE->getType().isDestructedType())
        S.Cleanup.setExprNeedsCleanups(true);
      break;
    }

    case SK_FinalCopy:
      if (checkAbstractType(Step->Type))
        return ExprError();

      // If the overall initialization is initializing a temporary, we already
      // bound our argument if it was necessary to do so. If not (if we're
      // ultimately initializing a non-temporary), our argument needs to be
      // bound since it's initializing a function parameter.
      // FIXME: This is a mess. Rationalize temporary destruction.
      if (!shouldBindAsTemporary(Entity))
        CurInit = S.MaybeBindToTemporary(CurInit.get());
      CurInit = CopyObject(S, Step->Type, Entity, CurInit,
                           /*IsExtraneousCopy=*/false);
      break;

    case SK_ExtraneousCopyToTemporary:
      CurInit = CopyObject(S, Step->Type, Entity, CurInit,
                           /*IsExtraneousCopy=*/true);
      break;

    case SK_UserConversion: {
      // We have a user-defined conversion that invokes either a constructor
      // or a conversion function.
      CastKind CastKind;
      FunctionDecl *Fn = Step->Function.Function;
      DeclAccessPair FoundFn = Step->Function.FoundDecl;
      bool HadMultipleCandidates = Step->Function.HadMultipleCandidates;
      bool CreatedObject = false;
      if (CXXConstructorDecl *Constructor = dyn_cast<CXXConstructorDecl>(Fn)) {
        // Build a call to the selected constructor.
        SmallVector<Expr*, 8> ConstructorArgs;
        SourceLocation Loc = CurInit.get()->getBeginLoc();

        // Determine the arguments required to actually perform the constructor
        // call.
        Expr *Arg = CurInit.get();
        if (S.CompleteConstructorCall(Constructor, Step->Type,
                                      MultiExprArg(&Arg, 1), Loc,
                                      ConstructorArgs))
          return ExprError();

        // Build an expression that constructs a temporary.
        CurInit = S.BuildCXXConstructExpr(
            Loc, Step->Type, FoundFn, Constructor, ConstructorArgs,
            HadMultipleCandidates,
            /*ListInit*/ false,
            /*StdInitListInit*/ false,
            /*ZeroInit*/ false, CXXConstructionKind::Complete, SourceRange());
        if (CurInit.isInvalid())
          return ExprError();

        S.CheckConstructorAccess(Kind.getLocation(), Constructor, FoundFn,
                                 Entity);
        if (S.DiagnoseUseOfDecl(FoundFn, Kind.getLocation()))
          return ExprError();

        CastKind = CK_ConstructorConversion;
        CreatedObject = true;
      } else {
        // Build a call to the conversion function.
        CXXConversionDecl *Conversion = cast<CXXConversionDecl>(Fn);
        S.CheckMemberOperatorAccess(Kind.getLocation(), CurInit.get(), nullptr,
                                    FoundFn);
        if (S.DiagnoseUseOfDecl(FoundFn, Kind.getLocation()))
          return ExprError();

        CurInit = S.BuildCXXMemberCallExpr(CurInit.get(), FoundFn, Conversion,
                                           HadMultipleCandidates);
        if (CurInit.isInvalid())
          return ExprError();

        CastKind = CK_UserDefinedConversion;
        CreatedObject = Conversion->getReturnType()->isRecordType();
      }

      if (CreatedObject && checkAbstractType(CurInit.get()->getType()))
        return ExprError();

      CurInit = ImplicitCastExpr::Create(
          S.Context, CurInit.get()->getType(), CastKind, CurInit.get(), nullptr,
          CurInit.get()->getValueKind(), S.CurFPFeatureOverrides());

      if (shouldBindAsTemporary(Entity))
        // The overall entity is temporary, so this expression should be
        // destroyed at the end of its full-expression.
        CurInit = S.MaybeBindToTemporary(CurInit.getAs<Expr>());
      else if (CreatedObject && shouldDestroyEntity(Entity)) {
        // The object outlasts the full-expression, but we need to prepare for
        // a destructor being run on it.
        // FIXME: It makes no sense to do this here. This should happen
        // regardless of how we initialized the entity.
        QualType T = CurInit.get()->getType();
        if (const RecordType *Record = T->getAs<RecordType>()) {
          CXXDestructorDecl *Destructor
            = S.LookupDestructor(cast<CXXRecordDecl>(Record->getDecl()));
          S.CheckDestructorAccess(CurInit.get()->getBeginLoc(), Destructor,
                                  S.PDiag(diag::err_access_dtor_temp) << T);
          S.MarkFunctionReferenced(CurInit.get()->getBeginLoc(), Destructor);
          if (S.DiagnoseUseOfDecl(Destructor, CurInit.get()->getBeginLoc()))
            return ExprError();
        }
      }
      break;
    }

    case SK_QualificationConversionLValue:
    case SK_QualificationConversionXValue:
    case SK_QualificationConversionPRValue: {
      // Perform a qualification conversion; these can never go wrong.
      ExprValueKind VK =
          Step->Kind == SK_QualificationConversionLValue
              ? VK_LValue
              : (Step->Kind == SK_QualificationConversionXValue ? VK_XValue
                                                                : VK_PRValue);
      CurInit = S.PerformQualificationConversion(CurInit.get(), Step->Type, VK);
      break;
    }

    case SK_FunctionReferenceConversion:
      assert(CurInit.get()->isLValue() &&
             "function reference should be lvalue");
      CurInit =
          S.ImpCastExprToType(CurInit.get(), Step->Type, CK_NoOp, VK_LValue);
      break;

    case SK_AtomicConversion: {
      assert(CurInit.get()->isPRValue() && "cannot convert glvalue to atomic");
      CurInit = S.ImpCastExprToType(CurInit.get(), Step->Type,
                                    CK_NonAtomicToAtomic, VK_PRValue);
      break;
    }

    case SK_ConversionSequence:
    case SK_ConversionSequenceNoNarrowing: {
      if (const auto *FromPtrType =
              CurInit.get()->getType()->getAs<PointerType>()) {
        if (const auto *ToPtrType = Step->Type->getAs<PointerType>()) {
          if (FromPtrType->getPointeeType()->hasAttr(attr::NoDeref) &&
              !ToPtrType->getPointeeType()->hasAttr(attr::NoDeref)) {
            // Do not check static casts here because they are checked earlier
            // in Sema::ActOnCXXNamedCast()
            if (!Kind.isStaticCast()) {
              S.Diag(CurInit.get()->getExprLoc(),
                     diag::warn_noderef_to_dereferenceable_pointer)
                  << CurInit.get()->getSourceRange();
            }
          }
        }
      }
      Expr *Init = CurInit.get();
      CheckedConversionKind CCK =
          Kind.isCStyleCast()       ? CheckedConversionKind::CStyleCast
          : Kind.isFunctionalCast() ? CheckedConversionKind::FunctionalCast
          : Kind.isExplicitCast()   ? CheckedConversionKind::OtherCast
                                    : CheckedConversionKind::Implicit;
      ExprResult CurInitExprRes = S.PerformImplicitConversion(
          Init, Step->Type, *Step->ICS, getAssignmentAction(Entity), CCK);
      if (CurInitExprRes.isInvalid())
        return ExprError();

      S.DiscardMisalignedMemberAddress(Step->Type.getTypePtr(), Init);

      CurInit = CurInitExprRes;

      if (Step->Kind == SK_ConversionSequenceNoNarrowing &&
          S.getLangOpts().CPlusPlus)
        DiagnoseNarrowingInInitList(S, *Step->ICS, SourceType, Entity.getType(),
                                    CurInit.get());

      break;
    }

    case SK_ListInitialization: {
      if (checkAbstractType(Step->Type))
        return ExprError();

      InitListExpr *InitList = cast<InitListExpr>(CurInit.get());
      // If we're not initializing the top-level entity, we need to create an
      // InitializeTemporary entity for our target type.
      QualType Ty = Step->Type;
      bool IsTemporary = !S.Context.hasSameType(Entity.getType(), Ty);
      InitializedEntity TempEntity = InitializedEntity::InitializeTemporary(Ty);
      InitializedEntity InitEntity = IsTemporary ? TempEntity : Entity;
      InitListChecker PerformInitList(S, InitEntity,
          InitList, Ty, /*VerifyOnly=*/false,
          /*TreatUnavailableAsInvalid=*/false);
      if (PerformInitList.HadError())
        return ExprError();

      // Hack: We must update *ResultType if available in order to set the
      // bounds of arrays, e.g. in 'int ar[] = {1, 2, 3};'.
      // Worst case: 'const int (&arref)[] = {1, 2, 3};'.
      if (ResultType &&
          ResultType->getNonReferenceType()->isIncompleteArrayType()) {
        if ((*ResultType)->isRValueReferenceType())
          Ty = S.Context.getRValueReferenceType(Ty);
        else if ((*ResultType)->isLValueReferenceType())
          Ty = S.Context.getLValueReferenceType(Ty,
            (*ResultType)->castAs<LValueReferenceType>()->isSpelledAsLValue());
        *ResultType = Ty;
      }

      InitListExpr *StructuredInitList =
          PerformInitList.getFullyStructuredList();
      CurInit.get();
      CurInit = shouldBindAsTemporary(InitEntity)
          ? S.MaybeBindToTemporary(StructuredInitList)
          : StructuredInitList;
      break;
    }

    case SK_ConstructorInitializationFromList: {
      if (checkAbstractType(Step->Type))
        return ExprError();

      // When an initializer list is passed for a parameter of type "reference
      // to object", we don't get an EK_Temporary entity, but instead an
      // EK_Parameter entity with reference type.
      // FIXME: This is a hack. What we really should do is create a user
      // conversion step for this case, but this makes it considerably more
      // complicated. For now, this will do.
      InitializedEntity TempEntity = InitializedEntity::InitializeTemporary(
                                        Entity.getType().getNonReferenceType());
      bool UseTemporary = Entity.getType()->isReferenceType();
      assert(Args.size() == 1 && "expected a single argument for list init");
      InitListExpr *InitList = cast<InitListExpr>(Args[0]);
      S.Diag(InitList->getExprLoc(), diag::warn_cxx98_compat_ctor_list_init)
        << InitList->getSourceRange();
      MultiExprArg Arg(InitList->getInits(), InitList->getNumInits());
      CurInit = PerformConstructorInitialization(S, UseTemporary ? TempEntity :
                                                                   Entity,
                                                 Kind, Arg, *Step,
                                               ConstructorInitRequiresZeroInit,
                                               /*IsListInitialization*/true,
                                               /*IsStdInitListInit*/false,
                                               InitList->getLBraceLoc(),
                                               InitList->getRBraceLoc());
      break;
    }

    case SK_UnwrapInitList:
      CurInit = cast<InitListExpr>(CurInit.get())->getInit(0);
      break;

    case SK_RewrapInitList: {
      Expr *E = CurInit.get();
      InitListExpr *Syntactic = Step->WrappingSyntacticList;
      InitListExpr *ILE = new (S.Context) InitListExpr(S.Context,
          Syntactic->getLBraceLoc(), E, Syntactic->getRBraceLoc());
      ILE->setSyntacticForm(Syntactic);
      ILE->setType(E->getType());
      ILE->setValueKind(E->getValueKind());
      CurInit = ILE;
      break;
    }

    case SK_ConstructorInitialization:
    case SK_StdInitializerListConstructorCall: {
      if (checkAbstractType(Step->Type))
        return ExprError();

      // When an initializer list is passed for a parameter of type "reference
      // to object", we don't get an EK_Temporary entity, but instead an
      // EK_Parameter entity with reference type.
      // FIXME: This is a hack. What we really should do is create a user
      // conversion step for this case, but this makes it considerably more
      // complicated. For now, this will do.
      InitializedEntity TempEntity = InitializedEntity::InitializeTemporary(
                                        Entity.getType().getNonReferenceType());
      bool UseTemporary = Entity.getType()->isReferenceType();
      bool IsStdInitListInit =
          Step->Kind == SK_StdInitializerListConstructorCall;
      Expr *Source = CurInit.get();
      SourceRange Range = Kind.hasParenOrBraceRange()
                              ? Kind.getParenOrBraceRange()
                              : SourceRange();
      CurInit = PerformConstructorInitialization(
          S, UseTemporary ? TempEntity : Entity, Kind,
          Source ? MultiExprArg(Source) : Args, *Step,
          ConstructorInitRequiresZeroInit,
          /*IsListInitialization*/ IsStdInitListInit,
          /*IsStdInitListInitialization*/ IsStdInitListInit,
          /*LBraceLoc*/ Range.getBegin(),
          /*RBraceLoc*/ Range.getEnd());
      break;
    }

    case SK_ZeroInitialization: {
      step_iterator NextStep = Step;
      ++NextStep;
      if (NextStep != StepEnd &&
          (NextStep->Kind == SK_ConstructorInitialization ||
           NextStep->Kind == SK_ConstructorInitializationFromList)) {
        // The need for zero-initialization is recorded directly into
        // the call to the object's constructor within the next step.
        ConstructorInitRequiresZeroInit = true;
      } else if (Kind.getKind() == InitializationKind::IK_Value &&
                 S.getLangOpts().CPlusPlus &&
                 !Kind.isImplicitValueInit()) {
        TypeSourceInfo *TSInfo = Entity.getTypeSourceInfo();
        if (!TSInfo)
          TSInfo = S.Context.getTrivialTypeSourceInfo(Step->Type,
                                                    Kind.getRange().getBegin());

        CurInit = new (S.Context) CXXScalarValueInitExpr(
            Entity.getType().getNonLValueExprType(S.Context), TSInfo,
            Kind.getRange().getEnd());
      } else {
        CurInit = new (S.Context) ImplicitValueInitExpr(Step->Type);
      }
      break;
    }

    case SK_CAssignment: {
      QualType SourceType = CurInit.get()->getType();
      Expr *Init = CurInit.get();

      // Save off the initial CurInit in case we need to emit a diagnostic
      ExprResult InitialCurInit = Init;
      ExprResult Result = Init;
      Sema::AssignConvertType ConvTy =
        S.CheckSingleAssignmentConstraints(Step->Type, Result, true,
            Entity.getKind() == InitializedEntity::EK_Parameter_CF_Audited);
      if (Result.isInvalid())
        return ExprError();
      CurInit = Result;

      // If this is a call, allow conversion to a transparent union.
      ExprResult CurInitExprRes = CurInit;
      if (ConvTy != Sema::Compatible &&
          Entity.isParameterKind() &&
          S.CheckTransparentUnionArgumentConstraints(Step->Type, CurInitExprRes)
            == Sema::Compatible)
        ConvTy = Sema::Compatible;
      if (CurInitExprRes.isInvalid())
        return ExprError();
      CurInit = CurInitExprRes;

      if (S.getLangOpts().C23 && initializingConstexprVariable(Entity)) {
        CheckC23ConstexprInitConversion(S, SourceType, Entity.getType(),
                                        CurInit.get());

        // C23 6.7.1p6: If an object or subobject declared with storage-class
        // specifier constexpr has pointer, integer, or arithmetic type, any
        // explicit initializer value for it shall be null, an integer
        // constant expression, or an arithmetic constant expression,
        // respectively.
        Expr::EvalResult ER;
        if (Entity.getType()->getAs<PointerType>() &&
            CurInit.get()->EvaluateAsRValue(ER, S.Context) &&
            !ER.Val.isNullPointer()) {
          S.Diag(Kind.getLocation(), diag::err_c23_constexpr_pointer_not_null);
        }
      }

      bool Complained;
      if (S.DiagnoseAssignmentResult(ConvTy, Kind.getLocation(),
                                     Step->Type, SourceType,
                                     InitialCurInit.get(),
                                     getAssignmentAction(Entity, true),
                                     &Complained)) {
        PrintInitLocationNote(S, Entity);
        return ExprError();
      } else if (Complained)
        PrintInitLocationNote(S, Entity);
      break;
    }

    case SK_StringInit: {
      QualType Ty = Step->Type;
      bool UpdateType = ResultType && Entity.getType()->isIncompleteArrayType();
      CheckStringInit(CurInit.get(), UpdateType ? *ResultType : Ty,
                      S.Context.getAsArrayType(Ty), S,
                      S.getLangOpts().C23 &&
                          initializingConstexprVariable(Entity));
      break;
    }

    case SK_ObjCObjectConversion:
      CurInit = S.ImpCastExprToType(CurInit.get(), Step->Type,
                          CK_ObjCObjectLValueCast,
                          CurInit.get()->getValueKind());
      break;

    case SK_ArrayLoopIndex: {
      Expr *Cur = CurInit.get();
      Expr *BaseExpr = new (S.Context)
          OpaqueValueExpr(Cur->getExprLoc(), Cur->getType(),
                          Cur->getValueKind(), Cur->getObjectKind(), Cur);
      Expr *IndexExpr =
          new (S.Context) ArrayInitIndexExpr(S.Context.getSizeType());
      CurInit = S.CreateBuiltinArraySubscriptExpr(
          BaseExpr, Kind.getLocation(), IndexExpr, Kind.getLocation());
      ArrayLoopCommonExprs.push_back(BaseExpr);
      break;
    }

    case SK_ArrayLoopInit: {
      assert(!ArrayLoopCommonExprs.empty() &&
             "mismatched SK_ArrayLoopIndex and SK_ArrayLoopInit");
      Expr *Common = ArrayLoopCommonExprs.pop_back_val();
      CurInit = new (S.Context) ArrayInitLoopExpr(Step->Type, Common,
                                                  CurInit.get());
      break;
    }

    case SK_GNUArrayInit:
      // Okay: we checked everything before creating this step. Note that
      // this is a GNU extension.
      S.Diag(Kind.getLocation(), diag::ext_array_init_copy)
        << Step->Type << CurInit.get()->getType()
        << CurInit.get()->getSourceRange();
      updateGNUCompoundLiteralRValue(CurInit.get());
      [[fallthrough]];
    case SK_ArrayInit:
      // If the destination type is an incomplete array type, update the
      // type accordingly.
      if (ResultType) {
        if (const IncompleteArrayType *IncompleteDest
                           = S.Context.getAsIncompleteArrayType(Step->Type)) {
          if (const ConstantArrayType *ConstantSource
                 = S.Context.getAsConstantArrayType(CurInit.get()->getType())) {
            *ResultType = S.Context.getConstantArrayType(
                IncompleteDest->getElementType(), ConstantSource->getSize(),
                ConstantSource->getSizeExpr(), ArraySizeModifier::Normal, 0);
          }
        }
      }
      break;

    case SK_ParenthesizedArrayInit:
      // Okay: we checked everything before creating this step. Note that
      // this is a GNU extension.
      S.Diag(Kind.getLocation(), diag::ext_array_init_parens)
        << CurInit.get()->getSourceRange();
      break;

    case SK_PassByIndirectCopyRestore:
    case SK_PassByIndirectRestore:
      checkIndirectCopyRestoreSource(S, CurInit.get());
      CurInit = new (S.Context) ObjCIndirectCopyRestoreExpr(
          CurInit.get(), Step->Type,
          Step->Kind == SK_PassByIndirectCopyRestore);
      break;

    case SK_ProduceObjCObject:
      CurInit = ImplicitCastExpr::Create(
          S.Context, Step->Type, CK_ARCProduceObject, CurInit.get(), nullptr,
          VK_PRValue, FPOptionsOverride());
      break;

    case SK_StdInitializerList: {
      S.Diag(CurInit.get()->getExprLoc(),
             diag::warn_cxx98_compat_initializer_list_init)
        << CurInit.get()->getSourceRange();

      // Materialize the temporary into memory.
      MaterializeTemporaryExpr *MTE = S.CreateMaterializeTemporaryExpr(
          CurInit.get()->getType(), CurInit.get(),
          /*BoundToLvalueReference=*/false);

      // Wrap it in a construction of a std::initializer_list<T>.
      CurInit = new (S.Context) CXXStdInitializerListExpr(Step->Type, MTE);

      if (!Step->Type->isDependentType()) {
        QualType ElementType;
        [[maybe_unused]] bool IsStdInitializerList =
            S.isStdInitializerList(Step->Type, &ElementType);
        assert(IsStdInitializerList &&
               "StdInitializerList step to non-std::initializer_list");
        const CXXRecordDecl *Record =
            Step->Type->getAsCXXRecordDecl()->getDefinition();
        assert(Record && Record->isCompleteDefinition() &&
               "std::initializer_list should have already be "
               "complete/instantiated by this point");

        auto InvalidType = [&] {
          S.Diag(Record->getLocation(),
                 diag::err_std_initializer_list_malformed)
              << Step->Type.getUnqualifiedType();
          return ExprError();
        };

        if (Record->isUnion() || Record->getNumBases() != 0 ||
            Record->isPolymorphic())
          return InvalidType();

        RecordDecl::field_iterator Field = Record->field_begin();
        if (Field == Record->field_end())
          return InvalidType();

        // Start pointer
        if (!Field->getType()->isPointerType() ||
            !S.Context.hasSameType(Field->getType()->getPointeeType(),
                                   ElementType.withConst()))
          return InvalidType();

        if (++Field == Record->field_end())
          return InvalidType();

        // Size or end pointer
        if (const auto *PT = Field->getType()->getAs<PointerType>()) {
          if (!S.Context.hasSameType(PT->getPointeeType(),
                                     ElementType.withConst()))
            return InvalidType();
        } else {
          if (Field->isBitField() ||
              !S.Context.hasSameType(Field->getType(), S.Context.getSizeType()))
            return InvalidType();
        }

        if (++Field != Record->field_end())
          return InvalidType();
      }

      // Bind the result, in case the library has given initializer_list a
      // non-trivial destructor.
      if (shouldBindAsTemporary(Entity))
        CurInit = S.MaybeBindToTemporary(CurInit.get());
      break;
    }

    case SK_OCLSamplerInit: {
      // Sampler initialization have 5 cases:
      //   1. function argument passing
      //      1a. argument is a file-scope variable
      //      1b. argument is a function-scope variable
      //      1c. argument is one of caller function's parameters
      //   2. variable initialization
      //      2a. initializing a file-scope variable
      //      2b. initializing a function-scope variable
      //
      // For file-scope variables, since they cannot be initialized by function
      // call of __translate_sampler_initializer in LLVM IR, their references
      // need to be replaced by a cast from their literal initializers to
      // sampler type. Since sampler variables can only be used in function
      // calls as arguments, we only need to replace them when handling the
      // argument passing.
      assert(Step->Type->isSamplerT() &&
             "Sampler initialization on non-sampler type.");
      Expr *Init = CurInit.get()->IgnoreParens();
      QualType SourceType = Init->getType();
      // Case 1
      if (Entity.isParameterKind()) {
        if (!SourceType->isSamplerT() && !SourceType->isIntegerType()) {
          S.Diag(Kind.getLocation(), diag::err_sampler_argument_required)
            << SourceType;
          break;
        } else if (const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(Init)) {
          auto Var = cast<VarDecl>(DRE->getDecl());
          // Case 1b and 1c
          // No cast from integer to sampler is needed.
          if (!Var->hasGlobalStorage()) {
            CurInit = ImplicitCastExpr::Create(
                S.Context, Step->Type, CK_LValueToRValue, Init,
                /*BasePath=*/nullptr, VK_PRValue, FPOptionsOverride());
            break;
          }
          // Case 1a
          // For function call with a file-scope sampler variable as argument,
          // get the integer literal.
          // Do not diagnose if the file-scope variable does not have initializer
          // since this has already been diagnosed when parsing the variable
          // declaration.
          if (!Var->getInit() || !isa<ImplicitCastExpr>(Var->getInit()))
            break;
          Init = cast<ImplicitCastExpr>(const_cast<Expr*>(
            Var->getInit()))->getSubExpr();
          SourceType = Init->getType();
        }
      } else {
        // Case 2
        // Check initializer is 32 bit integer constant.
        // If the initializer is taken from global variable, do not diagnose since
        // this has already been done when parsing the variable declaration.
        if (!Init->isConstantInitializer(S.Context, false))
          break;

        if (!SourceType->isIntegerType() ||
            32 != S.Context.getIntWidth(SourceType)) {
          S.Diag(Kind.getLocation(), diag::err_sampler_initializer_not_integer)
            << SourceType;
          break;
        }

        Expr::EvalResult EVResult;
        Init->EvaluateAsInt(EVResult, S.Context);
        llvm::APSInt Result = EVResult.Val.getInt();
        const uint64_t SamplerValue = Result.getLimitedValue();
        // 32-bit value of sampler's initializer is interpreted as
        // bit-field with the following structure:
        // |unspecified|Filter|Addressing Mode| Normalized Coords|
        // |31        6|5    4|3             1|                 0|
        // This structure corresponds to enum values of sampler properties
        // defined in SPIR spec v1.2 and also opencl-c.h
        unsigned AddressingMode  = (0x0E & SamplerValue) >> 1;
        unsigned FilterMode      = (0x30 & SamplerValue) >> 4;
        if (FilterMode != 1 && FilterMode != 2 &&
            !S.getOpenCLOptions().isAvailableOption(
                "cl_intel_device_side_avc_motion_estimation", S.getLangOpts()))
          S.Diag(Kind.getLocation(),
                 diag::warn_sampler_initializer_invalid_bits)
                 << "Filter Mode";
        if (AddressingMode > 4)
          S.Diag(Kind.getLocation(),
                 diag::warn_sampler_initializer_invalid_bits)
                 << "Addressing Mode";
      }

      // Cases 1a, 2a and 2b
      // Insert cast from integer to sampler.
      CurInit = S.ImpCastExprToType(Init, S.Context.OCLSamplerTy,
                                      CK_IntToOCLSampler);
      break;
    }
    case SK_OCLZeroOpaqueType: {
      assert((Step->Type->isEventT() || Step->Type->isQueueT() ||
              Step->Type->isOCLIntelSubgroupAVCType()) &&
             "Wrong type for initialization of OpenCL opaque type.");

      CurInit = S.ImpCastExprToType(CurInit.get(), Step->Type,
                                    CK_ZeroToOCLOpaqueType,
                                    CurInit.get()->getValueKind());
      break;
    }
    case SK_ParenthesizedListInit: {
      CurInit = nullptr;
      TryOrBuildParenListInitialization(S, Entity, Kind, Args, *this,
                                        /*VerifyOnly=*/false, &CurInit);
      if (CurInit.get() && ResultType)
        *ResultType = CurInit.get()->getType();
      if (shouldBindAsTemporary(Entity))
        CurInit = S.MaybeBindToTemporary(CurInit.get());
      break;
    }
    }
  }

  Expr *Init = CurInit.get();
  if (!Init)
    return ExprError();

  // Check whether the initializer has a shorter lifetime than the initialized
  // entity, and if not, either lifetime-extend or warn as appropriate.
  S.checkInitializerLifetime(Entity, Init);

  // Diagnose non-fatal problems with the completed initialization.
  if (InitializedEntity::EntityKind EK = Entity.getKind();
      (EK == InitializedEntity::EK_Member ||
       EK == InitializedEntity::EK_ParenAggInitMember) &&
      cast<FieldDecl>(Entity.getDecl())->isBitField())
    S.CheckBitFieldInitialization(Kind.getLocation(),
                                  cast<FieldDecl>(Entity.getDecl()), Init);

  // Check for std::move on construction.
  CheckMoveOnConstruction(S, Init,
                          Entity.getKind() == InitializedEntity::EK_Result);

  return Init;
}

/// Somewhere within T there is an uninitialized reference subobject.
/// Dig it out and diagnose it.
static bool DiagnoseUninitializedReference(Sema &S, SourceLocation Loc,
                                           QualType T) {
  if (T->isReferenceType()) {
    S.Diag(Loc, diag::err_reference_without_init)
      << T.getNonReferenceType();
    return true;
  }

  CXXRecordDecl *RD = T->getBaseElementTypeUnsafe()->getAsCXXRecordDecl();
  if (!RD || !RD->hasUninitializedReferenceMember())
    return false;

  for (const auto *FI : RD->fields()) {
    if (FI->isUnnamedBitField())
      continue;

    if (DiagnoseUninitializedReference(S, FI->getLocation(), FI->getType())) {
      S.Diag(Loc, diag::note_value_initialization_here) << RD;
      return true;
    }
  }

  for (const auto &BI : RD->bases()) {
    if (DiagnoseUninitializedReference(S, BI.getBeginLoc(), BI.getType())) {
      S.Diag(Loc, diag::note_value_initialization_here) << RD;
      return true;
    }
  }

  return false;
}


//===----------------------------------------------------------------------===//
// Diagnose initialization failures
//===----------------------------------------------------------------------===//

/// Emit notes associated with an initialization that failed due to a
/// "simple" conversion failure.
static void emitBadConversionNotes(Sema &S, const InitializedEntity &entity,
                                   Expr *op) {
  QualType destType = entity.getType();
  if (destType.getNonReferenceType()->isObjCObjectPointerType() &&
      op->getType()->isObjCObjectPointerType()) {

    // Emit a possible note about the conversion failing because the
    // operand is a message send with a related result type.
    S.ObjC().EmitRelatedResultTypeNote(op);

    // Emit a possible note about a return failing because we're
    // expecting a related result type.
    if (entity.getKind() == InitializedEntity::EK_Result)
      S.ObjC().EmitRelatedResultTypeNoteForReturn(destType);
  }
  QualType fromType = op->getType();
  QualType fromPointeeType = fromType.getCanonicalType()->getPointeeType();
  QualType destPointeeType = destType.getCanonicalType()->getPointeeType();
  auto *fromDecl = fromType->getPointeeCXXRecordDecl();
  auto *destDecl = destType->getPointeeCXXRecordDecl();
  if (fromDecl && destDecl && fromDecl->getDeclKind() == Decl::CXXRecord &&
      destDecl->getDeclKind() == Decl::CXXRecord &&
      !fromDecl->isInvalidDecl() && !destDecl->isInvalidDecl() &&
      !fromDecl->hasDefinition() &&
      destPointeeType.getQualifiers().compatiblyIncludes(
          fromPointeeType.getQualifiers()))
    S.Diag(fromDecl->getLocation(), diag::note_forward_class_conversion)
        << S.getASTContext().getTagDeclType(fromDecl)
        << S.getASTContext().getTagDeclType(destDecl);
}

static void diagnoseListInit(Sema &S, const InitializedEntity &Entity,
                             InitListExpr *InitList) {
  QualType DestType = Entity.getType();

  QualType E;
  if (S.getLangOpts().CPlusPlus11 && S.isStdInitializerList(DestType, &E)) {
    QualType ArrayType = S.Context.getConstantArrayType(
        E.withConst(),
        llvm::APInt(S.Context.getTypeSize(S.Context.getSizeType()),
                    InitList->getNumInits()),
        nullptr, clang::ArraySizeModifier::Normal, 0);
    InitializedEntity HiddenArray =
        InitializedEntity::InitializeTemporary(ArrayType);
    return diagnoseListInit(S, HiddenArray, InitList);
  }

  if (DestType->isReferenceType()) {
    // A list-initialization failure for a reference means that we tried to
    // create a temporary of the inner type (per [dcl.init.list]p3.6) and the
    // inner initialization failed.
    QualType T = DestType->castAs<ReferenceType>()->getPointeeType();
    diagnoseListInit(S, InitializedEntity::InitializeTemporary(T), InitList);
    SourceLocation Loc = InitList->getBeginLoc();
    if (auto *D = Entity.getDecl())
      Loc = D->getLocation();
    S.Diag(Loc, diag::note_in_reference_temporary_list_initializer) << T;
    return;
  }

  InitListChecker DiagnoseInitList(S, Entity, InitList, DestType,
                                   /*VerifyOnly=*/false,
                                   /*TreatUnavailableAsInvalid=*/false);
  assert(DiagnoseInitList.HadError() &&
         "Inconsistent init list check result.");
}

bool InitializationSequence::Diagnose(Sema &S,
                                      const InitializedEntity &Entity,
                                      const InitializationKind &Kind,
                                      ArrayRef<Expr *> Args) {
  if (!Failed())
    return false;

  QualType DestType = Entity.getType();

  // When we want to diagnose only one element of a braced-init-list,
  // we need to factor it out.
  Expr *OnlyArg;
  if (Args.size() == 1) {
    auto *List = dyn_cast<InitListExpr>(Args[0]);
    if (List && List->getNumInits() == 1)
      OnlyArg = List->getInit(0);
    else
      OnlyArg = Args[0];

    if (OnlyArg->getType() == S.Context.OverloadTy) {
      DeclAccessPair Found;
      if (FunctionDecl *FD = S.ResolveAddressOfOverloadedFunction(
              OnlyArg, DestType.getNonReferenceType(), /*Complain=*/false,
              Found)) {
        if (Expr *Resolved =
                S.FixOverloadedFunctionReference(OnlyArg, Found, FD).get())
          OnlyArg = Resolved;
      }
    }
  }
  else
    OnlyArg = nullptr;

  switch (Failure) {
  case FK_TooManyInitsForReference:
    // FIXME: Customize for the initialized entity?
    if (Args.empty()) {
      // Dig out the reference subobject which is uninitialized and diagnose it.
      // If this is value-initialization, this could be nested some way within
      // the target type.
      assert(Kind.getKind() == InitializationKind::IK_Value ||
             DestType->isReferenceType());
      bool Diagnosed =
        DiagnoseUninitializedReference(S, Kind.getLocation(), DestType);
      assert(Diagnosed && "couldn't find uninitialized reference to diagnose");
      (void)Diagnosed;
    } else  // FIXME: diagnostic below could be better!
      S.Diag(Kind.getLocation(), diag::err_reference_has_multiple_inits)
          << SourceRange(Args.front()->getBeginLoc(), Args.back()->getEndLoc());
    break;
  case FK_ParenthesizedListInitForReference:
    S.Diag(Kind.getLocation(), diag::err_list_init_in_parens)
      << 1 << Entity.getType() << Args[0]->getSourceRange();
    break;

  case FK_ArrayNeedsInitList:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 0;
    break;
  case FK_ArrayNeedsInitListOrStringLiteral:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 1;
    break;
  case FK_ArrayNeedsInitListOrWideStringLiteral:
    S.Diag(Kind.getLocation(), diag::err_array_init_not_init_list) << 2;
    break;
  case FK_NarrowStringIntoWideCharArray:
    S.Diag(Kind.getLocation(), diag::err_array_init_narrow_string_into_wchar);
    break;
  case FK_WideStringIntoCharArray:
    S.Diag(Kind.getLocation(), diag::err_array_init_wide_string_into_char);
    break;
  case FK_IncompatWideStringIntoWideChar:
    S.Diag(Kind.getLocation(),
           diag::err_array_init_incompat_wide_string_into_wchar);
    break;
  case FK_PlainStringIntoUTF8Char:
    S.Diag(Kind.getLocation(),
           diag::err_array_init_plain_string_into_char8_t);
    S.Diag(Args.front()->getBeginLoc(),
           diag::note_array_init_plain_string_into_char8_t)
        << FixItHint::CreateInsertion(Args.front()->getBeginLoc(), "u8");
    break;
  case FK_UTF8StringIntoPlainChar:
    S.Diag(Kind.getLocation(), diag::err_array_init_utf8_string_into_char)
        << DestType->isSignedIntegerType() << S.getLangOpts().CPlusPlus20;
    break;
  case FK_ArrayTypeMismatch:
  case FK_NonConstantArrayInit:
    S.Diag(Kind.getLocation(),
           (Failure == FK_ArrayTypeMismatch
              ? diag::err_array_init_different_type
              : diag::err_array_init_non_constant_array))
      << DestType.getNonReferenceType()
      << OnlyArg->getType()
      << Args[0]->getSourceRange();
    break;

  case FK_VariableLengthArrayHasInitializer:
    S.Diag(Kind.getLocation(), diag::err_variable_object_no_init)
      << Args[0]->getSourceRange();
    break;

  case FK_AddressOfOverloadFailed: {
    DeclAccessPair Found;
    S.ResolveAddressOfOverloadedFunction(OnlyArg,
                                         DestType.getNonReferenceType(),
                                         true,
                                         Found);
    break;
  }

  case FK_AddressOfUnaddressableFunction: {
    auto *FD = cast<FunctionDecl>(cast<DeclRefExpr>(OnlyArg)->getDecl());
    S.checkAddressOfFunctionIsAvailable(FD, /*Complain=*/true,
                                        OnlyArg->getBeginLoc());
    break;
  }

  case FK_ReferenceInitOverloadFailed:
  case FK_UserConversionOverloadFailed:
    switch (FailedOverloadResult) {
    case OR_Ambiguous:

      FailedCandidateSet.NoteCandidates(
          PartialDiagnosticAt(
              Kind.getLocation(),
              Failure == FK_UserConversionOverloadFailed
                  ? (S.PDiag(diag::err_typecheck_ambiguous_condition)
                     << OnlyArg->getType() << DestType
                     << Args[0]->getSourceRange())
                  : (S.PDiag(diag::err_ref_init_ambiguous)
                     << DestType << OnlyArg->getType()
                     << Args[0]->getSourceRange())),
          S, OCD_AmbiguousCandidates, Args);
      break;

    case OR_No_Viable_Function: {
      auto Cands = FailedCandidateSet.CompleteCandidates(S, OCD_AllCandidates, Args);
      if (!S.RequireCompleteType(Kind.getLocation(),
                                 DestType.getNonReferenceType(),
                          diag::err_typecheck_nonviable_condition_incomplete,
                               OnlyArg->getType(), Args[0]->getSourceRange()))
        S.Diag(Kind.getLocation(), diag::err_typecheck_nonviable_condition)
          << (Entity.getKind() == InitializedEntity::EK_Result)
          << OnlyArg->getType() << Args[0]->getSourceRange()
          << DestType.getNonReferenceType();

      FailedCandidateSet.NoteCandidates(S, Args, Cands);
      break;
    }
    case OR_Deleted: {
      OverloadCandidateSet::iterator Best;
      OverloadingResult Ovl
        = FailedCandidateSet.BestViableFunction(S, Kind.getLocation(), Best);

      StringLiteral *Msg = Best->Function->getDeletedMessage();
      S.Diag(Kind.getLocation(), diag::err_typecheck_deleted_function)
          << OnlyArg->getType() << DestType.getNonReferenceType()
          << (Msg != nullptr) << (Msg ? Msg->getString() : StringRef())
          << Args[0]->getSourceRange();
      if (Ovl == OR_Deleted) {
        S.NoteDeletedFunction(Best->Function);
      } else {
        llvm_unreachable("Inconsistent overload resolution?");
      }
      break;
    }

    case OR_Success:
      llvm_unreachable("Conversion did not fail!");
    }
    break;

  case FK_NonConstLValueReferenceBindingToTemporary:
    if (isa<InitListExpr>(Args[0])) {
      S.Diag(Kind.getLocation(),
             diag::err_lvalue_reference_bind_to_initlist)
      << DestType.getNonReferenceType().isVolatileQualified()
      << DestType.getNonReferenceType()
      << Args[0]->getSourceRange();
      break;
    }
    [[fallthrough]];

  case FK_NonConstLValueReferenceBindingToUnrelated:
    S.Diag(Kind.getLocation(),
           Failure == FK_NonConstLValueReferenceBindingToTemporary
             ? diag::err_lvalue_reference_bind_to_temporary
             : diag::err_lvalue_reference_bind_to_unrelated)
      << DestType.getNonReferenceType().isVolatileQualified()
      << DestType.getNonReferenceType()
      << OnlyArg->getType()
      << Args[0]->getSourceRange();
    break;

  case FK_NonConstLValueReferenceBindingToBitfield: {
    // We don't necessarily have an unambiguous source bit-field.
    FieldDecl *BitField = Args[0]->getSourceBitField();
    S.Diag(Kind.getLocation(), diag::err_reference_bind_to_bitfield)
      << DestType.isVolatileQualified()
      << (BitField ? BitField->getDeclName() : DeclarationName())
      << (BitField != nullptr)
      << Args[0]->getSourceRange();
    if (BitField)
      S.Diag(BitField->getLocation(), diag::note_bitfield_decl);
    break;
  }

  case FK_NonConstLValueReferenceBindingToVectorElement:
    S.Diag(Kind.getLocation(), diag::err_reference_bind_to_vector_element)
      << DestType.isVolatileQualified()
      << Args[0]->getSourceRange();
    break;

  case FK_NonConstLValueReferenceBindingToMatrixElement:
    S.Diag(Kind.getLocation(), diag::err_reference_bind_to_matrix_element)
        << DestType.isVolatileQualified() << Args[0]->getSourceRange();
    break;

  case FK_RValueReferenceBindingToLValue:
    S.Diag(Kind.getLocation(), diag::err_lvalue_to_rvalue_ref)
      << DestType.getNonReferenceType() << OnlyArg->getType()
      << Args[0]->getSourceRange();
    break;

  case FK_ReferenceAddrspaceMismatchTemporary:
    S.Diag(Kind.getLocation(), diag::err_reference_bind_temporary_addrspace)
        << DestType << Args[0]->getSourceRange();
    break;

  case FK_ReferenceInitDropsQualifiers: {
    QualType SourceType = OnlyArg->getType();
    QualType NonRefType = DestType.getNonReferenceType();
    Qualifiers DroppedQualifiers =
        SourceType.getQualifiers() - NonRefType.getQualifiers();

    if (!NonRefType.getQualifiers().isAddressSpaceSupersetOf(
            SourceType.getQualifiers()))
      S.Diag(Kind.getLocation(), diag::err_reference_bind_drops_quals)
          << NonRefType << SourceType << 1 /*addr space*/
          << Args[0]->getSourceRange();
    else if (DroppedQualifiers.hasQualifiers())
      S.Diag(Kind.getLocation(), diag::err_reference_bind_drops_quals)
          << NonRefType << SourceType << 0 /*cv quals*/
          << Qualifiers::fromCVRMask(DroppedQualifiers.getCVRQualifiers())
          << DroppedQualifiers.getCVRQualifiers() << Args[0]->getSourceRange();
    else
      // FIXME: Consider decomposing the type and explaining which qualifiers
      // were dropped where, or on which level a 'const' is missing, etc.
      S.Diag(Kind.getLocation(), diag::err_reference_bind_drops_quals)
          << NonRefType << SourceType << 2 /*incompatible quals*/
          << Args[0]->getSourceRange();
    break;
  }

  case FK_ReferenceInitFailed:
    S.Diag(Kind.getLocation(), diag::err_reference_bind_failed)
      << DestType.getNonReferenceType()
      << DestType.getNonReferenceType()->isIncompleteType()
      << OnlyArg->isLValue()
      << OnlyArg->getType()
      << Args[0]->getSourceRange();
    emitBadConversionNotes(S, Entity, Args[0]);
    break;

  case FK_ConversionFailed: {
    QualType FromType = OnlyArg->getType();
    PartialDiagnostic PDiag = S.PDiag(diag::err_init_conversion_failed)
      << (int)Entity.getKind()
      << DestType
      << OnlyArg->isLValue()
      << FromType
      << Args[0]->getSourceRange();
    S.HandleFunctionTypeMismatch(PDiag, FromType, DestType);
    S.Diag(Kind.getLocation(), PDiag);
    emitBadConversionNotes(S, Entity, Args[0]);
    break;
  }

  case FK_ConversionFromPropertyFailed:
    // No-op. This error has already been reported.
    break;

  case FK_TooManyInitsForScalar: {
    SourceRange R;

    auto *InitList = dyn_cast<InitListExpr>(Args[0]);
    if (InitList && InitList->getNumInits() >= 1) {
      R = SourceRange(InitList->getInit(0)->getEndLoc(), InitList->getEndLoc());
    } else {
      assert(Args.size() > 1 && "Expected multiple initializers!");
      R = SourceRange(Args.front()->getEndLoc(), Args.back()->getEndLoc());
    }

    R.setBegin(S.getLocForEndOfToken(R.getBegin()));
    if (Kind.isCStyleOrFunctionalCast())
      S.Diag(Kind.getLocation(), diag::err_builtin_func_cast_more_than_one_arg)
        << R;
    else
      S.Diag(Kind.getLocation(), diag::err_excess_initializers)
        << /*scalar=*/2 << R;
    break;
  }

  case FK_ParenthesizedListInitForScalar:
    S.Diag(Kind.getLocation(), diag::err_list_init_in_parens)
      << 0 << Entity.getType() << Args[0]->getSourceRange();
    break;

  case FK_ReferenceBindingToInitList:
    S.Diag(Kind.getLocation(), diag::err_reference_bind_init_list)
      << DestType.getNonReferenceType() << Args[0]->getSourceRange();
    break;

  case FK_InitListBadDestinationType:
    S.Diag(Kind.getLocation(), diag::err_init_list_bad_dest_type)
      << (DestType->isRecordType()) << DestType << Args[0]->getSourceRange();
    break;

  case FK_ListConstructorOverloadFailed:
  case FK_ConstructorOverloadFailed: {
    SourceRange ArgsRange;
    if (Args.size())
      ArgsRange =
          SourceRange(Args.front()->getBeginLoc(), Args.back()->getEndLoc());

    if (Failure == FK_ListConstructorOverloadFailed) {
      assert(Args.size() == 1 &&
             "List construction from other than 1 argument.");
      InitListExpr *InitList = cast<InitListExpr>(Args[0]);
      Args = MultiExprArg(InitList->getInits(), InitList->getNumInits());
    }

    // FIXME: Using "DestType" for the entity we're printing is probably
    // bad.
    switch (FailedOverloadResult) {
      case OR_Ambiguous:
        FailedCandidateSet.NoteCandidates(
            PartialDiagnosticAt(Kind.getLocation(),
                                S.PDiag(diag::err_ovl_ambiguous_init)
                                    << DestType << ArgsRange),
            S, OCD_AmbiguousCandidates, Args);
        break;

      case OR_No_Viable_Function:
        if (Kind.getKind() == InitializationKind::IK_Default &&
            (Entity.getKind() == InitializedEntity::EK_Base ||
             Entity.getKind() == InitializedEntity::EK_Member ||
             Entity.getKind() == InitializedEntity::EK_ParenAggInitMember) &&
            isa<CXXConstructorDecl>(S.CurContext)) {
          // This is implicit default initialization of a member or
          // base within a constructor. If no viable function was
          // found, notify the user that they need to explicitly
          // initialize this base/member.
          CXXConstructorDecl *Constructor
            = cast<CXXConstructorDecl>(S.CurContext);
          const CXXRecordDecl *InheritedFrom = nullptr;
          if (auto Inherited = Constructor->getInheritedConstructor())
            InheritedFrom = Inherited.getShadowDecl()->getNominatedBaseClass();
          if (Entity.getKind() == InitializedEntity::EK_Base) {
            S.Diag(Kind.getLocation(), diag::err_missing_default_ctor)
              << (InheritedFrom ? 2 : Constructor->isImplicit() ? 1 : 0)
              << S.Context.getTypeDeclType(Constructor->getParent())
              << /*base=*/0
              << Entity.getType()
              << InheritedFrom;

            RecordDecl *BaseDecl
              = Entity.getBaseSpecifier()->getType()->castAs<RecordType>()
                                                                  ->getDecl();
            S.Diag(BaseDecl->getLocation(), diag::note_previous_decl)
              << S.Context.getTagDeclType(BaseDecl);
          } else {
            S.Diag(Kind.getLocation(), diag::err_missing_default_ctor)
              << (InheritedFrom ? 2 : Constructor->isImplicit() ? 1 : 0)
              << S.Context.getTypeDeclType(Constructor->getParent())
              << /*member=*/1
              << Entity.getName()
              << InheritedFrom;
            S.Diag(Entity.getDecl()->getLocation(),
                   diag::note_member_declared_at);

            if (const RecordType *Record
                                 = Entity.getType()->getAs<RecordType>())
              S.Diag(Record->getDecl()->getLocation(),
                     diag::note_previous_decl)
                << S.Context.getTagDeclType(Record->getDecl());
          }
          break;
        }

        FailedCandidateSet.NoteCandidates(
            PartialDiagnosticAt(
                Kind.getLocation(),
                S.PDiag(diag::err_ovl_no_viable_function_in_init)
                    << DestType << ArgsRange),
            S, OCD_AllCandidates, Args);
        break;

      case OR_Deleted: {
        OverloadCandidateSet::iterator Best;
        OverloadingResult Ovl
          = FailedCandidateSet.BestViableFunction(S, Kind.getLocation(), Best);
        if (Ovl != OR_Deleted) {
          S.Diag(Kind.getLocation(), diag::err_ovl_deleted_init)
              << DestType << ArgsRange;
          llvm_unreachable("Inconsistent overload resolution?");
          break;
        }

        // If this is a defaulted or implicitly-declared function, then
        // it was implicitly deleted. Make it clear that the deletion was
        // implicit.
        if (S.isImplicitlyDeleted(Best->Function))
          S.Diag(Kind.getLocation(), diag::err_ovl_deleted_special_init)
              << llvm::to_underlying(
                     S.getSpecialMember(cast<CXXMethodDecl>(Best->Function)))
              << DestType << ArgsRange;
        else {
          StringLiteral *Msg = Best->Function->getDeletedMessage();
          S.Diag(Kind.getLocation(), diag::err_ovl_deleted_init)
              << DestType << (Msg != nullptr)
              << (Msg ? Msg->getString() : StringRef()) << ArgsRange;
        }

        S.NoteDeletedFunction(Best->Function);
        break;
      }

      case OR_Success:
        llvm_unreachable("Conversion did not fail!");
    }
  }
  break;

  case FK_DefaultInitOfConst:
    if (Entity.getKind() == InitializedEntity::EK_Member &&
        isa<CXXConstructorDecl>(S.CurContext)) {
      // This is implicit default-initialization of a const member in
      // a constructor. Complain that it needs to be explicitly
      // initialized.
      CXXConstructorDecl *Constructor = cast<CXXConstructorDecl>(S.CurContext);
      S.Diag(Kind.getLocation(), diag::err_uninitialized_member_in_ctor)
        << (Constructor->getInheritedConstructor() ? 2 :
            Constructor->isImplicit() ? 1 : 0)
        << S.Context.getTypeDeclType(Constructor->getParent())
        << /*const=*/1
        << Entity.getName();
      S.Diag(Entity.getDecl()->getLocation(), diag::note_previous_decl)
        << Entity.getName();
    } else if (const auto *VD = dyn_cast_if_present<VarDecl>(Entity.getDecl());
               VD && VD->isConstexpr()) {
      S.Diag(Kind.getLocation(), diag::err_constexpr_var_requires_const_init)
          << VD;
    } else {
      S.Diag(Kind.getLocation(), diag::err_default_init_const)
          << DestType << (bool)DestType->getAs<RecordType>();
    }
    break;

  case FK_Incomplete:
    S.RequireCompleteType(Kind.getLocation(), FailedIncompleteType,
                          diag::err_init_incomplete_type);
    break;

  case FK_ListInitializationFailed: {
    // Run the init list checker again to emit diagnostics.
    InitListExpr *InitList = cast<InitListExpr>(Args[0]);
    diagnoseListInit(S, Entity, InitList);
    break;
  }

  case FK_PlaceholderType: {
    // FIXME: Already diagnosed!
    break;
  }

  case FK_ExplicitConstructor: {
    S.Diag(Kind.getLocation(), diag::err_selected_explicit_constructor)
      << Args[0]->getSourceRange();
    OverloadCandidateSet::iterator Best;
    OverloadingResult Ovl
      = FailedCandidateSet.BestViableFunction(S, Kind.getLocation(), Best);
    (void)Ovl;
    assert(Ovl == OR_Success && "Inconsistent overload resolution");
    CXXConstructorDecl *CtorDecl = cast<CXXConstructorDecl>(Best->Function);
    S.Diag(CtorDecl->getLocation(),
           diag::note_explicit_ctor_deduction_guide_here) << false;
    break;
  }

  case FK_ParenthesizedListInitFailed:
    TryOrBuildParenListInitialization(S, Entity, Kind, Args, *this,
                                      /*VerifyOnly=*/false);
    break;

  case FK_DesignatedInitForNonAggregate:
    InitListExpr *InitList = cast<InitListExpr>(Args[0]);
    S.Diag(Kind.getLocation(), diag::err_designated_init_for_non_aggregate)
        << Entity.getType() << InitList->getSourceRange();
    break;
  }

  PrintInitLocationNote(S, Entity);
  return true;
}

void InitializationSequence::dump(raw_ostream &OS) const {
  switch (SequenceKind) {
  case FailedSequence: {
    OS << "Failed sequence: ";
    switch (Failure) {
    case FK_TooManyInitsForReference:
      OS << "too many initializers for reference";
      break;

    case FK_ParenthesizedListInitForReference:
      OS << "parenthesized list init for reference";
      break;

    case FK_ArrayNeedsInitList:
      OS << "array requires initializer list";
      break;

    case FK_AddressOfUnaddressableFunction:
      OS << "address of unaddressable function was taken";
      break;

    case FK_ArrayNeedsInitListOrStringLiteral:
      OS << "array requires initializer list or string literal";
      break;

    case FK_ArrayNeedsInitListOrWideStringLiteral:
      OS << "array requires initializer list or wide string literal";
      break;

    case FK_NarrowStringIntoWideCharArray:
      OS << "narrow string into wide char array";
      break;

    case FK_WideStringIntoCharArray:
      OS << "wide string into char array";
      break;

    case FK_IncompatWideStringIntoWideChar:
      OS << "incompatible wide string into wide char array";
      break;

    case FK_PlainStringIntoUTF8Char:
      OS << "plain string literal into char8_t array";
      break;

    case FK_UTF8StringIntoPlainChar:
      OS << "u8 string literal into char array";
      break;

    case FK_ArrayTypeMismatch:
      OS << "array type mismatch";
      break;

    case FK_NonConstantArrayInit:
      OS << "non-constant array initializer";
      break;

    case FK_AddressOfOverloadFailed:
      OS << "address of overloaded function failed";
      break;

    case FK_ReferenceInitOverloadFailed:
      OS << "overload resolution for reference initialization failed";
      break;

    case FK_NonConstLValueReferenceBindingToTemporary:
      OS << "non-const lvalue reference bound to temporary";
      break;

    case FK_NonConstLValueReferenceBindingToBitfield:
      OS << "non-const lvalue reference bound to bit-field";
      break;

    case FK_NonConstLValueReferenceBindingToVectorElement:
      OS << "non-const lvalue reference bound to vector element";
      break;

    case FK_NonConstLValueReferenceBindingToMatrixElement:
      OS << "non-const lvalue reference bound to matrix element";
      break;

    case FK_NonConstLValueReferenceBindingToUnrelated:
      OS << "non-const lvalue reference bound to unrelated type";
      break;

    case FK_RValueReferenceBindingToLValue:
      OS << "rvalue reference bound to an lvalue";
      break;

    case FK_ReferenceInitDropsQualifiers:
      OS << "reference initialization drops qualifiers";
      break;

    case FK_ReferenceAddrspaceMismatchTemporary:
      OS << "reference with mismatching address space bound to temporary";
      break;

    case FK_ReferenceInitFailed:
      OS << "reference initialization failed";
      break;

    case FK_ConversionFailed:
      OS << "conversion failed";
      break;

    case FK_ConversionFromPropertyFailed:
      OS << "conversion from property failed";
      break;

    case FK_TooManyInitsForScalar:
      OS << "too many initializers for scalar";
      break;

    case FK_ParenthesizedListInitForScalar:
      OS << "parenthesized list init for reference";
      break;

    case FK_ReferenceBindingToInitList:
      OS << "referencing binding to initializer list";
      break;

    case FK_InitListBadDestinationType:
      OS << "initializer list for non-aggregate, non-scalar type";
      break;

    case FK_UserConversionOverloadFailed:
      OS << "overloading failed for user-defined conversion";
      break;

    case FK_ConstructorOverloadFailed:
      OS << "constructor overloading failed";
      break;

    case FK_DefaultInitOfConst:
      OS << "default initialization of a const variable";
      break;

    case FK_Incomplete:
      OS << "initialization of incomplete type";
      break;

    case FK_ListInitializationFailed:
      OS << "list initialization checker failure";
      break;

    case FK_VariableLengthArrayHasInitializer:
      OS << "variable length array has an initializer";
      break;

    case FK_PlaceholderType:
      OS << "initializer expression isn't contextually valid";
      break;

    case FK_ListConstructorOverloadFailed:
      OS << "list constructor overloading failed";
      break;

    case FK_ExplicitConstructor:
      OS << "list copy initialization chose explicit constructor";
      break;

    case FK_ParenthesizedListInitFailed:
      OS << "parenthesized list initialization failed";
      break;

    case FK_DesignatedInitForNonAggregate:
      OS << "designated initializer for non-aggregate type";
      break;
    }
    OS << '\n';
    return;
  }

  case DependentSequence:
    OS << "Dependent sequence\n";
    return;

  case NormalSequence:
    OS << "Normal sequence: ";
    break;
  }

  for (step_iterator S = step_begin(), SEnd = step_end(); S != SEnd; ++S) {
    if (S != step_begin()) {
      OS << " -> ";
    }

    switch (S->Kind) {
    case SK_ResolveAddressOfOverloadedFunction:
      OS << "resolve address of overloaded function";
      break;

    case SK_CastDerivedToBasePRValue:
      OS << "derived-to-base (prvalue)";
      break;

    case SK_CastDerivedToBaseXValue:
      OS << "derived-to-base (xvalue)";
      break;

    case SK_CastDerivedToBaseLValue:
      OS << "derived-to-base (lvalue)";
      break;

    case SK_BindReference:
      OS << "bind reference to lvalue";
      break;

    case SK_BindReferenceToTemporary:
      OS << "bind reference to a temporary";
      break;

    case SK_FinalCopy:
      OS << "final copy in class direct-initialization";
      break;

    case SK_ExtraneousCopyToTemporary:
      OS << "extraneous C++03 copy to temporary";
      break;

    case SK_UserConversion:
      OS << "user-defined conversion via " << *S->Function.Function;
      break;

    case SK_QualificationConversionPRValue:
      OS << "qualification conversion (prvalue)";
      break;

    case SK_QualificationConversionXValue:
      OS << "qualification conversion (xvalue)";
      break;

    case SK_QualificationConversionLValue:
      OS << "qualification conversion (lvalue)";
      break;

    case SK_FunctionReferenceConversion:
      OS << "function reference conversion";
      break;

    case SK_AtomicConversion:
      OS << "non-atomic-to-atomic conversion";
      break;

    case SK_ConversionSequence:
      OS << "implicit conversion sequence (";
      S->ICS->dump(); // FIXME: use OS
      OS << ")";
      break;

    case SK_ConversionSequenceNoNarrowing:
      OS << "implicit conversion sequence with narrowing prohibited (";
      S->ICS->dump(); // FIXME: use OS
      OS << ")";
      break;

    case SK_ListInitialization:
      OS << "list aggregate initialization";
      break;

    case SK_UnwrapInitList:
      OS << "unwrap reference initializer list";
      break;

    case SK_RewrapInitList:
      OS << "rewrap reference initializer list";
      break;

    case SK_ConstructorInitialization:
      OS << "constructor initialization";
      break;

    case SK_ConstructorInitializationFromList:
      OS << "list initialization via constructor";
      break;

    case SK_ZeroInitialization:
      OS << "zero initialization";
      break;

    case SK_CAssignment:
      OS << "C assignment";
      break;

    case SK_StringInit:
      OS << "string initialization";
      break;

    case SK_ObjCObjectConversion:
      OS << "Objective-C object conversion";
      break;

    case SK_ArrayLoopIndex:
      OS << "indexing for array initialization loop";
      break;

    case SK_ArrayLoopInit:
      OS << "array initialization loop";
      break;

    case SK_ArrayInit:
      OS << "array initialization";
      break;

    case SK_GNUArrayInit:
      OS << "array initialization (GNU extension)";
      break;

    case SK_ParenthesizedArrayInit:
      OS << "parenthesized array initialization";
      break;

    case SK_PassByIndirectCopyRestore:
      OS << "pass by indirect copy and restore";
      break;

    case SK_PassByIndirectRestore:
      OS << "pass by indirect restore";
      break;

    case SK_ProduceObjCObject:
      OS << "Objective-C object retension";
      break;

    case SK_StdInitializerList:
      OS << "std::initializer_list from initializer list";
      break;

    case SK_StdInitializerListConstructorCall:
      OS << "list initialization from std::initializer_list";
      break;

    case SK_OCLSamplerInit:
      OS << "OpenCL sampler_t from integer constant";
      break;

    case SK_OCLZeroOpaqueType:
      OS << "OpenCL opaque type from zero";
      break;
    case SK_ParenthesizedListInit:
      OS << "initialization from a parenthesized list of values";
      break;
    }

    OS << " [" << S->Type << ']';
  }

  OS << '\n';
}

void InitializationSequence::dump() const {
  dump(llvm::errs());
}

static void DiagnoseNarrowingInInitList(Sema &S,
                                        const ImplicitConversionSequence &ICS,
                                        QualType PreNarrowingType,
                                        QualType EntityType,
                                        const Expr *PostInit) {
  const StandardConversionSequence *SCS = nullptr;
  switch (ICS.getKind()) {
  case ImplicitConversionSequence::StandardConversion:
    SCS = &ICS.Standard;
    break;
  case ImplicitConversionSequence::UserDefinedConversion:
    SCS = &ICS.UserDefined.After;
    break;
  case ImplicitConversionSequence::AmbiguousConversion:
  case ImplicitConversionSequence::StaticObjectArgumentConversion:
  case ImplicitConversionSequence::EllipsisConversion:
  case ImplicitConversionSequence::BadConversion:
    return;
  }

  auto MakeDiag = [&](bool IsConstRef, unsigned DefaultDiagID,
                      unsigned ConstRefDiagID, unsigned WarnDiagID) {
    unsigned DiagID;
    auto &L = S.getLangOpts();
    if (L.CPlusPlus11 &&
        (!L.MicrosoftExt || L.isCompatibleWithMSVC(LangOptions::MSVC2015)))
      DiagID = IsConstRef ? ConstRefDiagID : DefaultDiagID;
    else
      DiagID = WarnDiagID;
    return S.Diag(PostInit->getBeginLoc(), DiagID)
           << PostInit->getSourceRange();
  };

  // C++11 [dcl.init.list]p7: Check whether this is a narrowing conversion.
  APValue ConstantValue;
  QualType ConstantType;
  switch (SCS->getNarrowingKind(S.Context, PostInit, ConstantValue,
                                ConstantType)) {
  case NK_Not_Narrowing:
  case NK_Dependent_Narrowing:
    // No narrowing occurred.
    return;

  case NK_Type_Narrowing: {
    // This was a floating-to-integer conversion, which is always considered a
    // narrowing conversion even if the value is a constant and can be
    // represented exactly as an integer.
    QualType T = EntityType.getNonReferenceType();
    MakeDiag(T != EntityType, diag::ext_init_list_type_narrowing,
             diag::ext_init_list_type_narrowing_const_reference,
             diag::warn_init_list_type_narrowing)
        << PreNarrowingType.getLocalUnqualifiedType()
        << T.getLocalUnqualifiedType();
    break;
  }

  case NK_Constant_Narrowing: {
    // A constant value was narrowed.
    MakeDiag(EntityType.getNonReferenceType() != EntityType,
             diag::ext_init_list_constant_narrowing,
             diag::ext_init_list_constant_narrowing_const_reference,
             diag::warn_init_list_constant_narrowing)
        << ConstantValue.getAsString(S.getASTContext(), ConstantType)
        << EntityType.getNonReferenceType().getLocalUnqualifiedType();
    break;
  }

  case NK_Variable_Narrowing: {
    // A variable's value may have been narrowed.
    MakeDiag(EntityType.getNonReferenceType() != EntityType,
             diag::ext_init_list_variable_narrowing,
             diag::ext_init_list_variable_narrowing_const_reference,
             diag::warn_init_list_variable_narrowing)
        << PreNarrowingType.getLocalUnqualifiedType()
        << EntityType.getNonReferenceType().getLocalUnqualifiedType();
    break;
  }
  }

  SmallString<128> StaticCast;
  llvm::raw_svector_ostream OS(StaticCast);
  OS << "static_cast<";
  if (const TypedefType *TT = EntityType->getAs<TypedefType>()) {
    // It's important to use the typedef's name if there is one so that the
    // fixit doesn't break code using types like int64_t.
    //
    // FIXME: This will break if the typedef requires qualification.  But
    // getQualifiedNameAsString() includes non-machine-parsable components.
    OS << *TT->getDecl();
  } else if (const BuiltinType *BT = EntityType->getAs<BuiltinType>())
    OS << BT->getName(S.getLangOpts());
  else {
    // Oops, we didn't find the actual type of the variable.  Don't emit a fixit
    // with a broken cast.
    return;
  }
  OS << ">(";
  S.Diag(PostInit->getBeginLoc(), diag::note_init_list_narrowing_silence)
      << PostInit->getSourceRange()
      << FixItHint::CreateInsertion(PostInit->getBeginLoc(), OS.str())
      << FixItHint::CreateInsertion(
             S.getLocForEndOfToken(PostInit->getEndLoc()), ")");
}

static void CheckC23ConstexprInitConversion(Sema &S, QualType FromType,
                                            QualType ToType, Expr *Init) {
  assert(S.getLangOpts().C23);
  ImplicitConversionSequence ICS = S.TryImplicitConversion(
      Init->IgnoreParenImpCasts(), ToType, /*SuppressUserConversions*/ false,
      Sema::AllowedExplicit::None,
      /*InOverloadResolution*/ false,
      /*CStyle*/ false,
      /*AllowObjCWritebackConversion=*/false);

  if (!ICS.isStandard())
    return;

  APValue Value;
  QualType PreNarrowingType;
  // Reuse C++ narrowing check.
  switch (ICS.Standard.getNarrowingKind(
      S.Context, Init, Value, PreNarrowingType,
      /*IgnoreFloatToIntegralConversion*/ false)) {
  // The value doesn't fit.
  case NK_Constant_Narrowing:
    S.Diag(Init->getBeginLoc(), diag::err_c23_constexpr_init_not_representable)
        << Value.getAsString(S.Context, PreNarrowingType) << ToType;
    return;

  // Conversion to a narrower type.
  case NK_Type_Narrowing:
    S.Diag(Init->getBeginLoc(), diag::err_c23_constexpr_init_type_mismatch)
        << ToType << FromType;
    return;

  // Since we only reuse narrowing check for C23 constexpr variables here, we're
  // not really interested in these cases.
  case NK_Dependent_Narrowing:
  case NK_Variable_Narrowing:
  case NK_Not_Narrowing:
    return;
  }
  llvm_unreachable("unhandled case in switch");
}

static void CheckC23ConstexprInitStringLiteral(const StringLiteral *SE,
                                               Sema &SemaRef, QualType &TT) {
  assert(SemaRef.getLangOpts().C23);
  // character that string literal contains fits into TT - target type.
  const ArrayType *AT = SemaRef.Context.getAsArrayType(TT);
  QualType CharType = AT->getElementType();
  uint32_t BitWidth = SemaRef.Context.getTypeSize(CharType);
  bool isUnsigned = CharType->isUnsignedIntegerType();
  llvm::APSInt Value(BitWidth, isUnsigned);
  for (unsigned I = 0, N = SE->getLength(); I != N; ++I) {
    int64_t C = SE->getCodeUnitS(I, SemaRef.Context.getCharWidth());
    Value = C;
    if (Value != C) {
      SemaRef.Diag(SemaRef.getLocationOfStringLiteralByte(SE, I),
                   diag::err_c23_constexpr_init_not_representable)
          << C << CharType;
      return;
    }
  }
  return;
}

//===----------------------------------------------------------------------===//
// Initialization helper functions
//===----------------------------------------------------------------------===//
bool
Sema::CanPerformCopyInitialization(const InitializedEntity &Entity,
                                   ExprResult Init) {
  if (Init.isInvalid())
    return false;

  Expr *InitE = Init.get();
  assert(InitE && "No initialization expression");

  InitializationKind Kind =
      InitializationKind::CreateCopy(InitE->getBeginLoc(), SourceLocation());
  InitializationSequence Seq(*this, Entity, Kind, InitE);
  return !Seq.Failed();
}

ExprResult
Sema::PerformCopyInitialization(const InitializedEntity &Entity,
                                SourceLocation EqualLoc,
                                ExprResult Init,
                                bool TopLevelOfInitList,
                                bool AllowExplicit) {
  if (Init.isInvalid())
    return ExprError();

  Expr *InitE = Init.get();
  assert(InitE && "No initialization expression?");

  if (EqualLoc.isInvalid())
    EqualLoc = InitE->getBeginLoc();

  InitializationKind Kind = InitializationKind::CreateCopy(
      InitE->getBeginLoc(), EqualLoc, AllowExplicit);
  InitializationSequence Seq(*this, Entity, Kind, InitE, TopLevelOfInitList);

  // Prevent infinite recursion when performing parameter copy-initialization.
  const bool ShouldTrackCopy =
      Entity.isParameterKind() && Seq.isConstructorInitialization();
  if (ShouldTrackCopy) {
    if (llvm::is_contained(CurrentParameterCopyTypes, Entity.getType())) {
      Seq.SetOverloadFailure(
          InitializationSequence::FK_ConstructorOverloadFailed,
          OR_No_Viable_Function);

      // Try to give a meaningful diagnostic note for the problematic
      // constructor.
      const auto LastStep = Seq.step_end() - 1;
      assert(LastStep->Kind ==
             InitializationSequence::SK_ConstructorInitialization);
      const FunctionDecl *Function = LastStep->Function.Function;
      auto Candidate =
          llvm::find_if(Seq.getFailedCandidateSet(),
                        [Function](const OverloadCandidate &Candidate) -> bool {
                          return Candidate.Viable &&
                                 Candidate.Function == Function &&
                                 Candidate.Conversions.size() > 0;
                        });
      if (Candidate != Seq.getFailedCandidateSet().end() &&
          Function->getNumParams() > 0) {
        Candidate->Viable = false;
        Candidate->FailureKind = ovl_fail_bad_conversion;
        Candidate->Conversions[0].setBad(BadConversionSequence::no_conversion,
                                         InitE,
                                         Function->getParamDecl(0)->getType());
      }
    }
    CurrentParameterCopyTypes.push_back(Entity.getType());
  }

  ExprResult Result = Seq.Perform(*this, Entity, Kind, InitE);

  if (ShouldTrackCopy)
    CurrentParameterCopyTypes.pop_back();

  return Result;
}

/// Determine whether RD is, or is derived from, a specialization of CTD.
static bool isOrIsDerivedFromSpecializationOf(CXXRecordDecl *RD,
                                              ClassTemplateDecl *CTD) {
  auto NotSpecialization = [&] (const CXXRecordDecl *Candidate) {
    auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(Candidate);
    return !CTSD || !declaresSameEntity(CTSD->getSpecializedTemplate(), CTD);
  };
  return !(NotSpecialization(RD) && RD->forallBases(NotSpecialization));
}

QualType Sema::DeduceTemplateSpecializationFromInitializer(
    TypeSourceInfo *TSInfo, const InitializedEntity &Entity,
    const InitializationKind &Kind, MultiExprArg Inits) {
  auto *DeducedTST = dyn_cast<DeducedTemplateSpecializationType>(
      TSInfo->getType()->getContainedDeducedType());
  assert(DeducedTST && "not a deduced template specialization type");

  auto TemplateName = DeducedTST->getTemplateName();
  if (TemplateName.isDependent())
    return SubstAutoTypeDependent(TSInfo->getType());

  // We can only perform deduction for class templates or alias templates.
  auto *Template =
      dyn_cast_or_null<ClassTemplateDecl>(TemplateName.getAsTemplateDecl());
  TemplateDecl *LookupTemplateDecl = Template;
  if (!Template) {
    if (auto *AliasTemplate = dyn_cast_or_null<TypeAliasTemplateDecl>(
            TemplateName.getAsTemplateDecl())) {
      Diag(Kind.getLocation(),
           diag::warn_cxx17_compat_ctad_for_alias_templates);
      LookupTemplateDecl = AliasTemplate;
      auto UnderlyingType = AliasTemplate->getTemplatedDecl()
                                ->getUnderlyingType()
                                .getCanonicalType();
      // C++ [over.match.class.deduct#3]: ..., the defining-type-id of A must be
      // of the form
      //   [typename] [nested-name-specifier] [template] simple-template-id
      if (const auto *TST =
              UnderlyingType->getAs<TemplateSpecializationType>()) {
        Template = dyn_cast_or_null<ClassTemplateDecl>(
            TST->getTemplateName().getAsTemplateDecl());
      } else if (const auto *RT = UnderlyingType->getAs<RecordType>()) {
        // Cases where template arguments in the RHS of the alias are not
        // dependent. e.g.
        //   using AliasFoo = Foo<bool>;
        if (const auto *CTSD = llvm::dyn_cast<ClassTemplateSpecializationDecl>(
                RT->getAsCXXRecordDecl()))
          Template = CTSD->getSpecializedTemplate();
      }
    }
  }
  if (!Template) {
    Diag(Kind.getLocation(),
         diag::err_deduced_non_class_or_alias_template_specialization_type)
        << (int)getTemplateNameKindForDiagnostics(TemplateName) << TemplateName;
    if (auto *TD = TemplateName.getAsTemplateDecl())
      NoteTemplateLocation(*TD);
    return QualType();
  }

  // Can't deduce from dependent arguments.
  if (Expr::hasAnyTypeDependentArguments(Inits)) {
    Diag(TSInfo->getTypeLoc().getBeginLoc(),
         diag::warn_cxx14_compat_class_template_argument_deduction)
        << TSInfo->getTypeLoc().getSourceRange() << 0;
    return SubstAutoTypeDependent(TSInfo->getType());
  }

  // FIXME: Perform "exact type" matching first, per CWG discussion?
  //        Or implement this via an implied 'T(T) -> T' deduction guide?

  // Look up deduction guides, including those synthesized from constructors.
  //
  // C++1z [over.match.class.deduct]p1:
  //   A set of functions and function templates is formed comprising:
  //   - For each constructor of the class template designated by the
  //     template-name, a function template [...]
  //  - For each deduction-guide, a function or function template [...]
  DeclarationNameInfo NameInfo(
      Context.DeclarationNames.getCXXDeductionGuideName(LookupTemplateDecl),
      TSInfo->getTypeLoc().getEndLoc());
  LookupResult Guides(*this, NameInfo, LookupOrdinaryName);
  LookupQualifiedName(Guides, LookupTemplateDecl->getDeclContext());

  // FIXME: Do not diagnose inaccessible deduction guides. The standard isn't
  // clear on this, but they're not found by name so access does not apply.
  Guides.suppressDiagnostics();

  // Figure out if this is list-initialization.
  InitListExpr *ListInit =
      (Inits.size() == 1 && Kind.getKind() != InitializationKind::IK_Direct)
          ? dyn_cast<InitListExpr>(Inits[0])
          : nullptr;

  // C++1z [over.match.class.deduct]p1:
  //   Initialization and overload resolution are performed as described in
  //   [dcl.init] and [over.match.ctor], [over.match.copy], or [over.match.list]
  //   (as appropriate for the type of initialization performed) for an object
  //   of a hypothetical class type, where the selected functions and function
  //   templates are considered to be the constructors of that class type
  //
  // Since we know we're initializing a class type of a type unrelated to that
  // of the initializer, this reduces to something fairly reasonable.
  OverloadCandidateSet Candidates(Kind.getLocation(),
                                  OverloadCandidateSet::CSK_Normal);
  OverloadCandidateSet::iterator Best;

  bool AllowExplicit = !Kind.isCopyInit() || ListInit;

  // Return true if the candidate is added successfully, false otherwise.
  auto addDeductionCandidate = [&](FunctionTemplateDecl *TD,
                                   CXXDeductionGuideDecl *GD,
                                   DeclAccessPair FoundDecl,
                                   bool OnlyListConstructors,
                                   bool AllowAggregateDeductionCandidate) {
    // C++ [over.match.ctor]p1: (non-list copy-initialization from non-class)
    //   For copy-initialization, the candidate functions are all the
    //   converting constructors (12.3.1) of that class.
    // C++ [over.match.copy]p1: (non-list copy-initialization from class)
    //   The converting constructors of T are candidate functions.
    if (!AllowExplicit) {
      // Overload resolution checks whether the deduction guide is declared
      // explicit for us.

      // When looking for a converting constructor, deduction guides that
      // could never be called with one argument are not interesting to
      // check or note.
      if (GD->getMinRequiredArguments() > 1 ||
          (GD->getNumParams() == 0 && !GD->isVariadic()))
        return;
    }

    // C++ [over.match.list]p1.1: (first phase list initialization)
    //   Initially, the candidate functions are the initializer-list
    //   constructors of the class T
    if (OnlyListConstructors && !isInitListConstructor(GD))
      return;

    if (!AllowAggregateDeductionCandidate &&
        GD->getDeductionCandidateKind() == DeductionCandidate::Aggregate)
      return;

    // C++ [over.match.list]p1.2: (second phase list initialization)
    //   the candidate functions are all the constructors of the class T
    // C++ [over.match.ctor]p1: (all other cases)
    //   the candidate functions are all the constructors of the class of
    //   the object being initialized

    // C++ [over.best.ics]p4:
    //   When [...] the constructor [...] is a candidate by
    //    - [over.match.copy] (in all cases)
    if (TD) {
      SmallVector<Expr *, 8> TmpInits;
      for (Expr *E : Inits)
        if (auto *DI = dyn_cast<DesignatedInitExpr>(E))
          TmpInits.push_back(DI->getInit());
        else
          TmpInits.push_back(E);
      AddTemplateOverloadCandidate(
          TD, FoundDecl, /*ExplicitArgs=*/nullptr, TmpInits, Candidates,
          /*SuppressUserConversions=*/false,
          /*PartialOverloading=*/false, AllowExplicit, ADLCallKind::NotADL,
          /*PO=*/{}, AllowAggregateDeductionCandidate);
    } else {
      AddOverloadCandidate(GD, FoundDecl, Inits, Candidates,
                           /*SuppressUserConversions=*/false,
                           /*PartialOverloading=*/false, AllowExplicit);
    }
  };

  bool FoundDeductionGuide = false;

  auto TryToResolveOverload =
      [&](bool OnlyListConstructors) -> OverloadingResult {
    Candidates.clear(OverloadCandidateSet::CSK_Normal);
    bool HasAnyDeductionGuide = false;

    auto SynthesizeAggrGuide = [&](InitListExpr *ListInit) {
      auto *Pattern = Template;
      while (Pattern->getInstantiatedFromMemberTemplate()) {
        if (Pattern->isMemberSpecialization())
          break;
        Pattern = Pattern->getInstantiatedFromMemberTemplate();
      }

      auto *RD = cast<CXXRecordDecl>(Pattern->getTemplatedDecl());
      if (!(RD->getDefinition() && RD->isAggregate()))
        return;
      QualType Ty = Context.getRecordType(RD);
      SmallVector<QualType, 8> ElementTypes;

      InitListChecker CheckInitList(*this, Entity, ListInit, Ty, ElementTypes);
      if (!CheckInitList.HadError()) {
        // C++ [over.match.class.deduct]p1.8:
        //   if e_i is of array type and x_i is a braced-init-list, T_i is an
        //   rvalue reference to the declared type of e_i and
        // C++ [over.match.class.deduct]p1.9:
        //   if e_i is of array type and x_i is a string-literal, T_i is an
        //   lvalue reference to the const-qualified declared type of e_i and
        // C++ [over.match.class.deduct]p1.10:
        //   otherwise, T_i is the declared type of e_i
        for (int I = 0, E = ListInit->getNumInits();
             I < E && !isa<PackExpansionType>(ElementTypes[I]); ++I)
          if (ElementTypes[I]->isArrayType()) {
            if (isa<InitListExpr, DesignatedInitExpr>(ListInit->getInit(I)))
              ElementTypes[I] = Context.getRValueReferenceType(ElementTypes[I]);
            else if (isa<StringLiteral>(
                         ListInit->getInit(I)->IgnoreParenImpCasts()))
              ElementTypes[I] =
                  Context.getLValueReferenceType(ElementTypes[I].withConst());
          }

        if (FunctionTemplateDecl *TD =
                DeclareAggregateDeductionGuideFromInitList(
                    LookupTemplateDecl, ElementTypes,
                    TSInfo->getTypeLoc().getEndLoc())) {
          auto *GD = cast<CXXDeductionGuideDecl>(TD->getTemplatedDecl());
          addDeductionCandidate(TD, GD, DeclAccessPair::make(TD, AS_public),
                                OnlyListConstructors,
                                /*AllowAggregateDeductionCandidate=*/true);
          HasAnyDeductionGuide = true;
        }
      }
    };

    for (auto I = Guides.begin(), E = Guides.end(); I != E; ++I) {
      NamedDecl *D = (*I)->getUnderlyingDecl();
      if (D->isInvalidDecl())
        continue;

      auto *TD = dyn_cast<FunctionTemplateDecl>(D);
      auto *GD = dyn_cast_if_present<CXXDeductionGuideDecl>(
          TD ? TD->getTemplatedDecl() : dyn_cast<FunctionDecl>(D));
      if (!GD)
        continue;

      if (!GD->isImplicit())
        HasAnyDeductionGuide = true;

      addDeductionCandidate(TD, GD, I.getPair(), OnlyListConstructors,
                            /*AllowAggregateDeductionCandidate=*/false);
    }

    // C++ [over.match.class.deduct]p1.4:
    //   if C is defined and its definition satisfies the conditions for an
    //   aggregate class ([dcl.init.aggr]) with the assumption that any
    //   dependent base class has no virtual functions and no virtual base
    //   classes, and the initializer is a non-empty braced-init-list or
    //   parenthesized expression-list, and there are no deduction-guides for
    //   C, the set contains an additional function template, called the
    //   aggregate deduction candidate, defined as follows.
    if (getLangOpts().CPlusPlus20 && !HasAnyDeductionGuide) {
      if (ListInit && ListInit->getNumInits()) {
        SynthesizeAggrGuide(ListInit);
      } else if (Inits.size()) { // parenthesized expression-list
        // Inits are expressions inside the parentheses. We don't have
        // the parentheses source locations, use the begin/end of Inits as the
        // best heuristic.
        InitListExpr TempListInit(getASTContext(), Inits.front()->getBeginLoc(),
                                  Inits, Inits.back()->getEndLoc());
        SynthesizeAggrGuide(&TempListInit);
      }
    }

    FoundDeductionGuide = FoundDeductionGuide || HasAnyDeductionGuide;

    return Candidates.BestViableFunction(*this, Kind.getLocation(), Best);
  };

  OverloadingResult Result = OR_No_Viable_Function;

  // C++11 [over.match.list]p1, per DR1467: for list-initialization, first
  // try initializer-list constructors.
  if (ListInit) {
    bool TryListConstructors = true;

    // Try list constructors unless the list is empty and the class has one or
    // more default constructors, in which case those constructors win.
    if (!ListInit->getNumInits()) {
      for (NamedDecl *D : Guides) {
        auto *FD = dyn_cast<FunctionDecl>(D->getUnderlyingDecl());
        if (FD && FD->getMinRequiredArguments() == 0) {
          TryListConstructors = false;
          break;
        }
      }
    } else if (ListInit->getNumInits() == 1) {
      // C++ [over.match.class.deduct]:
      //   As an exception, the first phase in [over.match.list] (considering
      //   initializer-list constructors) is omitted if the initializer list
      //   consists of a single expression of type cv U, where U is a
      //   specialization of C or a class derived from a specialization of C.
      Expr *E = ListInit->getInit(0);
      auto *RD = E->getType()->getAsCXXRecordDecl();
      if (!isa<InitListExpr>(E) && RD &&
          isCompleteType(Kind.getLocation(), E->getType()) &&
          isOrIsDerivedFromSpecializationOf(RD, Template))
        TryListConstructors = false;
    }

    if (TryListConstructors)
      Result = TryToResolveOverload(/*OnlyListConstructor*/true);
    // Then unwrap the initializer list and try again considering all
    // constructors.
    Inits = MultiExprArg(ListInit->getInits(), ListInit->getNumInits());
  }

  // If list-initialization fails, or if we're doing any other kind of
  // initialization, we (eventually) consider constructors.
  if (Result == OR_No_Viable_Function)
    Result = TryToResolveOverload(/*OnlyListConstructor*/false);

  switch (Result) {
  case OR_Ambiguous:
    // FIXME: For list-initialization candidates, it'd usually be better to
    // list why they were not viable when given the initializer list itself as
    // an argument.
    Candidates.NoteCandidates(
        PartialDiagnosticAt(
            Kind.getLocation(),
            PDiag(diag::err_deduced_class_template_ctor_ambiguous)
                << TemplateName),
        *this, OCD_AmbiguousCandidates, Inits);
    return QualType();

  case OR_No_Viable_Function: {
    CXXRecordDecl *Primary =
        cast<ClassTemplateDecl>(Template)->getTemplatedDecl();
    bool Complete =
        isCompleteType(Kind.getLocation(), Context.getTypeDeclType(Primary));
    Candidates.NoteCandidates(
        PartialDiagnosticAt(
            Kind.getLocation(),
            PDiag(Complete ? diag::err_deduced_class_template_ctor_no_viable
                           : diag::err_deduced_class_template_incomplete)
                << TemplateName << !Guides.empty()),
        *this, OCD_AllCandidates, Inits);
    return QualType();
  }

  case OR_Deleted: {
    // FIXME: There are no tests for this diagnostic, and it doesn't seem
    // like we ever get here; attempts to trigger this seem to yield a
    // generic c'all to deleted function' diagnostic instead.
    Diag(Kind.getLocation(), diag::err_deduced_class_template_deleted)
      << TemplateName;
    NoteDeletedFunction(Best->Function);
    return QualType();
  }

  case OR_Success:
    // C++ [over.match.list]p1:
    //   In copy-list-initialization, if an explicit constructor is chosen, the
    //   initialization is ill-formed.
    if (Kind.isCopyInit() && ListInit &&
        cast<CXXDeductionGuideDecl>(Best->Function)->isExplicit()) {
      bool IsDeductionGuide = !Best->Function->isImplicit();
      Diag(Kind.getLocation(), diag::err_deduced_class_template_explicit)
          << TemplateName << IsDeductionGuide;
      Diag(Best->Function->getLocation(),
           diag::note_explicit_ctor_deduction_guide_here)
          << IsDeductionGuide;
      return QualType();
    }

    // Make sure we didn't select an unusable deduction guide, and mark it
    // as referenced.
    DiagnoseUseOfDecl(Best->FoundDecl, Kind.getLocation());
    MarkFunctionReferenced(Kind.getLocation(), Best->Function);
    break;
  }

  // C++ [dcl.type.class.deduct]p1:
  //  The placeholder is replaced by the return type of the function selected
  //  by overload resolution for class template deduction.
  QualType DeducedType =
      SubstAutoType(TSInfo->getType(), Best->Function->getReturnType());
  Diag(TSInfo->getTypeLoc().getBeginLoc(),
       diag::warn_cxx14_compat_class_template_argument_deduction)
      << TSInfo->getTypeLoc().getSourceRange() << 1 << DeducedType;

  // Warn if CTAD was used on a type that does not have any user-defined
  // deduction guides.
  if (!FoundDeductionGuide) {
    Diag(TSInfo->getTypeLoc().getBeginLoc(),
         diag::warn_ctad_maybe_unsupported)
        << TemplateName;
    Diag(Template->getLocation(), diag::note_suppress_ctad_maybe_unsupported);
  }

  return DeducedType;
}
