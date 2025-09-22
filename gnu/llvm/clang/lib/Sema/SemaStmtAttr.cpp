//===--- SemaStmtAttr.cpp - Statement Attribute Handling ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements stmt-related attribute processing.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/EvaluatedExprVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/DelayedDiagnostic.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/SemaInternal.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace clang;
using namespace sema;

static Attr *handleFallThroughAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                   SourceRange Range) {
  FallThroughAttr Attr(S.Context, A);
  if (isa<SwitchCase>(St)) {
    S.Diag(A.getRange().getBegin(), diag::err_fallthrough_attr_wrong_target)
        << A << St->getBeginLoc();
    SourceLocation L = S.getLocForEndOfToken(Range.getEnd());
    S.Diag(L, diag::note_fallthrough_insert_semi_fixit)
        << FixItHint::CreateInsertion(L, ";");
    return nullptr;
  }
  auto *FnScope = S.getCurFunction();
  if (FnScope->SwitchStack.empty()) {
    S.Diag(A.getRange().getBegin(), diag::err_fallthrough_attr_outside_switch);
    return nullptr;
  }

  // If this is spelled as the standard C++17 attribute, but not in C++17, warn
  // about using it as an extension.
  if (!S.getLangOpts().CPlusPlus17 && A.isCXX11Attribute() &&
      !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_cxx17_attr) << A;

  FnScope->setHasFallthroughStmt();
  return ::new (S.Context) FallThroughAttr(S.Context, A);
}

static Attr *handleSuppressAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                SourceRange Range) {
  if (A.getAttributeSpellingListIndex() == SuppressAttr::CXX11_gsl_suppress &&
      A.getNumArgs() < 1) {
    // Suppression attribute with GSL spelling requires at least 1 argument.
    S.Diag(A.getLoc(), diag::err_attribute_too_few_arguments) << A << 1;
    return nullptr;
  }

  std::vector<StringRef> DiagnosticIdentifiers;
  for (unsigned I = 0, E = A.getNumArgs(); I != E; ++I) {
    StringRef RuleName;

    if (!S.checkStringLiteralArgumentAttr(A, I, RuleName, nullptr))
      return nullptr;

    DiagnosticIdentifiers.push_back(RuleName);
  }

  return ::new (S.Context) SuppressAttr(
      S.Context, A, DiagnosticIdentifiers.data(), DiagnosticIdentifiers.size());
}

static Attr *handleLoopHintAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                SourceRange) {
  IdentifierLoc *PragmaNameLoc = A.getArgAsIdent(0);
  IdentifierLoc *OptionLoc = A.getArgAsIdent(1);
  IdentifierLoc *StateLoc = A.getArgAsIdent(2);
  Expr *ValueExpr = A.getArgAsExpr(3);

  StringRef PragmaName =
      llvm::StringSwitch<StringRef>(PragmaNameLoc->Ident->getName())
          .Cases("unroll", "nounroll", "unroll_and_jam", "nounroll_and_jam",
                 PragmaNameLoc->Ident->getName())
          .Default("clang loop");

  // This could be handled automatically by adding a Subjects definition in
  // Attr.td, but that would make the diagnostic behavior worse in this case
  // because the user spells this attribute as a pragma.
  if (!isa<DoStmt, ForStmt, CXXForRangeStmt, WhileStmt>(St)) {
    std::string Pragma = "#pragma " + std::string(PragmaName);
    S.Diag(St->getBeginLoc(), diag::err_pragma_loop_precedes_nonloop) << Pragma;
    return nullptr;
  }

  LoopHintAttr::OptionType Option;
  LoopHintAttr::LoopHintState State;

  auto SetHints = [&Option, &State](LoopHintAttr::OptionType O,
                                    LoopHintAttr::LoopHintState S) {
    Option = O;
    State = S;
  };

  if (PragmaName == "nounroll") {
    SetHints(LoopHintAttr::Unroll, LoopHintAttr::Disable);
  } else if (PragmaName == "unroll") {
    // #pragma unroll N
    if (ValueExpr) {
      if (!ValueExpr->isValueDependent()) {
        auto Value = ValueExpr->EvaluateKnownConstInt(S.getASTContext());
        if (Value.isZero() || Value.isOne())
          SetHints(LoopHintAttr::Unroll, LoopHintAttr::Disable);
        else
          SetHints(LoopHintAttr::UnrollCount, LoopHintAttr::Numeric);
      } else
        SetHints(LoopHintAttr::UnrollCount, LoopHintAttr::Numeric);
    } else
      SetHints(LoopHintAttr::Unroll, LoopHintAttr::Enable);
  } else if (PragmaName == "nounroll_and_jam") {
    SetHints(LoopHintAttr::UnrollAndJam, LoopHintAttr::Disable);
  } else if (PragmaName == "unroll_and_jam") {
    // #pragma unroll_and_jam N
    if (ValueExpr)
      SetHints(LoopHintAttr::UnrollAndJamCount, LoopHintAttr::Numeric);
    else
      SetHints(LoopHintAttr::UnrollAndJam, LoopHintAttr::Enable);
  } else {
    // #pragma clang loop ...
    assert(OptionLoc && OptionLoc->Ident &&
           "Attribute must have valid option info.");
    Option = llvm::StringSwitch<LoopHintAttr::OptionType>(
                 OptionLoc->Ident->getName())
                 .Case("vectorize", LoopHintAttr::Vectorize)
                 .Case("vectorize_width", LoopHintAttr::VectorizeWidth)
                 .Case("interleave", LoopHintAttr::Interleave)
                 .Case("vectorize_predicate", LoopHintAttr::VectorizePredicate)
                 .Case("interleave_count", LoopHintAttr::InterleaveCount)
                 .Case("unroll", LoopHintAttr::Unroll)
                 .Case("unroll_count", LoopHintAttr::UnrollCount)
                 .Case("pipeline", LoopHintAttr::PipelineDisabled)
                 .Case("pipeline_initiation_interval",
                       LoopHintAttr::PipelineInitiationInterval)
                 .Case("distribute", LoopHintAttr::Distribute)
                 .Default(LoopHintAttr::Vectorize);
    if (Option == LoopHintAttr::VectorizeWidth) {
      assert((ValueExpr || (StateLoc && StateLoc->Ident)) &&
             "Attribute must have a valid value expression or argument.");
      if (ValueExpr && S.CheckLoopHintExpr(ValueExpr, St->getBeginLoc(),
                                           /*AllowZero=*/false))
        return nullptr;
      if (StateLoc && StateLoc->Ident && StateLoc->Ident->isStr("scalable"))
        State = LoopHintAttr::ScalableWidth;
      else
        State = LoopHintAttr::FixedWidth;
    } else if (Option == LoopHintAttr::InterleaveCount ||
               Option == LoopHintAttr::UnrollCount ||
               Option == LoopHintAttr::PipelineInitiationInterval) {
      assert(ValueExpr && "Attribute must have a valid value expression.");
      if (S.CheckLoopHintExpr(ValueExpr, St->getBeginLoc(),
                              /*AllowZero=*/false))
        return nullptr;
      State = LoopHintAttr::Numeric;
    } else if (Option == LoopHintAttr::Vectorize ||
               Option == LoopHintAttr::Interleave ||
               Option == LoopHintAttr::VectorizePredicate ||
               Option == LoopHintAttr::Unroll ||
               Option == LoopHintAttr::Distribute ||
               Option == LoopHintAttr::PipelineDisabled) {
      assert(StateLoc && StateLoc->Ident && "Loop hint must have an argument");
      if (StateLoc->Ident->isStr("disable"))
        State = LoopHintAttr::Disable;
      else if (StateLoc->Ident->isStr("assume_safety"))
        State = LoopHintAttr::AssumeSafety;
      else if (StateLoc->Ident->isStr("full"))
        State = LoopHintAttr::Full;
      else if (StateLoc->Ident->isStr("enable"))
        State = LoopHintAttr::Enable;
      else
        llvm_unreachable("bad loop hint argument");
    } else
      llvm_unreachable("bad loop hint");
  }

  return LoopHintAttr::CreateImplicit(S.Context, Option, State, ValueExpr, A);
}

namespace {
class CallExprFinder : public ConstEvaluatedExprVisitor<CallExprFinder> {
  bool FoundAsmStmt = false;
  std::vector<const CallExpr *> CallExprs;

public:
  typedef ConstEvaluatedExprVisitor<CallExprFinder> Inherited;

  CallExprFinder(Sema &S, const Stmt *St) : Inherited(S.Context) { Visit(St); }

  bool foundCallExpr() { return !CallExprs.empty(); }
  const std::vector<const CallExpr *> &getCallExprs() { return CallExprs; }

  bool foundAsmStmt() { return FoundAsmStmt; }

  void VisitCallExpr(const CallExpr *E) { CallExprs.push_back(E); }

  void VisitAsmStmt(const AsmStmt *S) { FoundAsmStmt = true; }

  void Visit(const Stmt *St) {
    if (!St)
      return;
    ConstEvaluatedExprVisitor<CallExprFinder>::Visit(St);
  }
};
} // namespace

static Attr *handleNoMergeAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                               SourceRange Range) {
  NoMergeAttr NMA(S.Context, A);
  CallExprFinder CEF(S, St);

  if (!CEF.foundCallExpr() && !CEF.foundAsmStmt()) {
    S.Diag(St->getBeginLoc(), diag::warn_attribute_ignored_no_calls_in_stmt)
        << A;
    return nullptr;
  }

  return ::new (S.Context) NoMergeAttr(S.Context, A);
}

template <typename OtherAttr, int DiagIdx>
static bool CheckStmtInlineAttr(Sema &SemaRef, const Stmt *OrigSt,
                                const Stmt *CurSt,
                                const AttributeCommonInfo &A) {
  CallExprFinder OrigCEF(SemaRef, OrigSt);
  CallExprFinder CEF(SemaRef, CurSt);

  // If the call expressions lists are equal in size, we can skip
  // previously emitted diagnostics. However, if the statement has a pack
  // expansion, we have no way of telling which CallExpr is the instantiated
  // version of the other. In this case, we will end up re-diagnosing in the
  // instantiation.
  // ie: [[clang::always_inline]] non_dependent(), (other_call<Pack>()...)
  // will diagnose nondependent again.
  bool CanSuppressDiag =
      OrigSt && CEF.getCallExprs().size() == OrigCEF.getCallExprs().size();

  if (!CEF.foundCallExpr()) {
    return SemaRef.Diag(CurSt->getBeginLoc(),
                        diag::warn_attribute_ignored_no_calls_in_stmt)
           << A;
  }

  for (const auto &Tup :
       llvm::zip_longest(OrigCEF.getCallExprs(), CEF.getCallExprs())) {
    // If the original call expression already had a callee, we already
    // diagnosed this, so skip it here. We can't skip if there isn't a 1:1
    // relationship between the two lists of call expressions.
    if (!CanSuppressDiag || !(*std::get<0>(Tup))->getCalleeDecl()) {
      const Decl *Callee = (*std::get<1>(Tup))->getCalleeDecl();
      if (Callee &&
          (Callee->hasAttr<OtherAttr>() || Callee->hasAttr<FlattenAttr>())) {
        SemaRef.Diag(CurSt->getBeginLoc(),
                     diag::warn_function_stmt_attribute_precedence)
            << A << (Callee->hasAttr<OtherAttr>() ? DiagIdx : 1);
        SemaRef.Diag(Callee->getBeginLoc(), diag::note_conflicting_attribute);
      }
    }
  }

  return false;
}

bool Sema::CheckNoInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                             const AttributeCommonInfo &A) {
  return CheckStmtInlineAttr<AlwaysInlineAttr, 0>(*this, OrigSt, CurSt, A);
}

bool Sema::CheckAlwaysInlineAttr(const Stmt *OrigSt, const Stmt *CurSt,
                                 const AttributeCommonInfo &A) {
  return CheckStmtInlineAttr<NoInlineAttr, 2>(*this, OrigSt, CurSt, A);
}

static Attr *handleNoInlineAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                SourceRange Range) {
  NoInlineAttr NIA(S.Context, A);
  if (!NIA.isStmtNoInline()) {
    S.Diag(St->getBeginLoc(), diag::warn_function_attribute_ignored_in_stmt)
        << "[[clang::noinline]]";
    return nullptr;
  }

  if (S.CheckNoInlineAttr(/*OrigSt=*/nullptr, St, A))
    return nullptr;

  return ::new (S.Context) NoInlineAttr(S.Context, A);
}

static Attr *handleAlwaysInlineAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                    SourceRange Range) {
  AlwaysInlineAttr AIA(S.Context, A);
  if (!AIA.isClangAlwaysInline()) {
    S.Diag(St->getBeginLoc(), diag::warn_function_attribute_ignored_in_stmt)
        << "[[clang::always_inline]]";
    return nullptr;
  }

  if (S.CheckAlwaysInlineAttr(/*OrigSt=*/nullptr, St, A))
    return nullptr;

  return ::new (S.Context) AlwaysInlineAttr(S.Context, A);
}

static Attr *handleCXXAssumeAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                 SourceRange Range) {
  ExprResult Res = S.ActOnCXXAssumeAttr(St, A, Range);
  if (!Res.isUsable())
    return nullptr;

  return ::new (S.Context) CXXAssumeAttr(S.Context, A, Res.get());
}

static Attr *handleMustTailAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                SourceRange Range) {
  // Validation is in Sema::ActOnAttributedStmt().
  return ::new (S.Context) MustTailAttr(S.Context, A);
}

static Attr *handleLikely(Sema &S, Stmt *St, const ParsedAttr &A,
                          SourceRange Range) {

  if (!S.getLangOpts().CPlusPlus20 && A.isCXX11Attribute() && !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_cxx20_attr) << A << Range;

  return ::new (S.Context) LikelyAttr(S.Context, A);
}

static Attr *handleUnlikely(Sema &S, Stmt *St, const ParsedAttr &A,
                            SourceRange Range) {

  if (!S.getLangOpts().CPlusPlus20 && A.isCXX11Attribute() && !A.getScopeName())
    S.Diag(A.getLoc(), diag::ext_cxx20_attr) << A << Range;

  return ::new (S.Context) UnlikelyAttr(S.Context, A);
}

CodeAlignAttr *Sema::BuildCodeAlignAttr(const AttributeCommonInfo &CI,
                                        Expr *E) {
  if (!E->isValueDependent()) {
    llvm::APSInt ArgVal;
    ExprResult Res = VerifyIntegerConstantExpression(E, &ArgVal);
    if (Res.isInvalid())
      return nullptr;
    E = Res.get();

    // This attribute requires an integer argument which is a constant power of
    // two between 1 and 4096 inclusive.
    if (ArgVal < CodeAlignAttr::MinimumAlignment ||
        ArgVal > CodeAlignAttr::MaximumAlignment || !ArgVal.isPowerOf2()) {
      if (std::optional<int64_t> Value = ArgVal.trySExtValue())
        Diag(CI.getLoc(), diag::err_attribute_power_of_two_in_range)
            << CI << CodeAlignAttr::MinimumAlignment
            << CodeAlignAttr::MaximumAlignment << Value.value();
      else
        Diag(CI.getLoc(), diag::err_attribute_power_of_two_in_range)
            << CI << CodeAlignAttr::MinimumAlignment
            << CodeAlignAttr::MaximumAlignment << E;
      return nullptr;
    }
  }
  return new (Context) CodeAlignAttr(Context, CI, E);
}

static Attr *handleCodeAlignAttr(Sema &S, Stmt *St, const ParsedAttr &A) {

  Expr *E = A.getArgAsExpr(0);
  return S.BuildCodeAlignAttr(A, E);
}

// Diagnose non-identical duplicates as a 'conflicting' loop attributes
// and suppress duplicate errors in cases where the two match.
template <typename LoopAttrT>
static void CheckForDuplicateLoopAttrs(Sema &S, ArrayRef<const Attr *> Attrs) {
  auto FindFunc = [](const Attr *A) { return isa<const LoopAttrT>(A); };
  const auto *FirstItr = std::find_if(Attrs.begin(), Attrs.end(), FindFunc);

  if (FirstItr == Attrs.end()) // no attributes found
    return;

  const auto *LastFoundItr = FirstItr;
  std::optional<llvm::APSInt> FirstValue;

  const auto *CAFA =
      dyn_cast<ConstantExpr>(cast<LoopAttrT>(*FirstItr)->getAlignment());
  // Return early if first alignment expression is dependent (since we don't
  // know what the effective size will be), and skip the loop entirely.
  if (!CAFA)
    return;

  while (Attrs.end() != (LastFoundItr = std::find_if(LastFoundItr + 1,
                                                     Attrs.end(), FindFunc))) {
    const auto *CASA =
        dyn_cast<ConstantExpr>(cast<LoopAttrT>(*LastFoundItr)->getAlignment());
    // If the value is dependent, we can not test anything.
    if (!CASA)
      return;
    // Test the attribute values.
    llvm::APSInt SecondValue = CASA->getResultAsAPSInt();
    if (!FirstValue)
      FirstValue = CAFA->getResultAsAPSInt();

    if (FirstValue != SecondValue) {
      S.Diag((*LastFoundItr)->getLocation(), diag::err_loop_attr_conflict)
          << *FirstItr;
      S.Diag((*FirstItr)->getLocation(), diag::note_previous_attribute);
    }
  }
  return;
}

static Attr *handleMSConstexprAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                   SourceRange Range) {
  if (!S.getLangOpts().isCompatibleWithMSVC(LangOptions::MSVC2022_3)) {
    S.Diag(A.getLoc(), diag::warn_unknown_attribute_ignored)
        << A << A.getRange();
    return nullptr;
  }
  return ::new (S.Context) MSConstexprAttr(S.Context, A);
}

#define WANT_STMT_MERGE_LOGIC
#include "clang/Sema/AttrParsedAttrImpl.inc"
#undef WANT_STMT_MERGE_LOGIC

static void
CheckForIncompatibleAttributes(Sema &S,
                               const SmallVectorImpl<const Attr *> &Attrs) {
  // The vast majority of attributed statements will only have one attribute
  // on them, so skip all of the checking in the common case.
  if (Attrs.size() < 2)
    return;

  // First, check for the easy cases that are table-generated for us.
  if (!DiagnoseMutualExclusions(S, Attrs))
    return;

  enum CategoryType {
    // For the following categories, they come in two variants: a state form and
    // a numeric form. The state form may be one of default, enable, and
    // disable. The numeric form provides an integer hint (for example, unroll
    // count) to the transformer.
    Vectorize,
    Interleave,
    UnrollAndJam,
    Pipeline,
    // For unroll, default indicates full unrolling rather than enabling the
    // transformation.
    Unroll,
    // The loop distribution transformation only has a state form that is
    // exposed by #pragma clang loop distribute (enable | disable).
    Distribute,
    // The vector predication only has a state form that is exposed by
    // #pragma clang loop vectorize_predicate (enable | disable).
    VectorizePredicate,
    // This serves as a indicator to how many category are listed in this enum.
    NumberOfCategories
  };
  // The following array accumulates the hints encountered while iterating
  // through the attributes to check for compatibility.
  struct {
    const LoopHintAttr *StateAttr;
    const LoopHintAttr *NumericAttr;
  } HintAttrs[CategoryType::NumberOfCategories] = {};

  for (const auto *I : Attrs) {
    const LoopHintAttr *LH = dyn_cast<LoopHintAttr>(I);

    // Skip non loop hint attributes
    if (!LH)
      continue;

    CategoryType Category = CategoryType::NumberOfCategories;
    LoopHintAttr::OptionType Option = LH->getOption();
    switch (Option) {
    case LoopHintAttr::Vectorize:
    case LoopHintAttr::VectorizeWidth:
      Category = Vectorize;
      break;
    case LoopHintAttr::Interleave:
    case LoopHintAttr::InterleaveCount:
      Category = Interleave;
      break;
    case LoopHintAttr::Unroll:
    case LoopHintAttr::UnrollCount:
      Category = Unroll;
      break;
    case LoopHintAttr::UnrollAndJam:
    case LoopHintAttr::UnrollAndJamCount:
      Category = UnrollAndJam;
      break;
    case LoopHintAttr::Distribute:
      // Perform the check for duplicated 'distribute' hints.
      Category = Distribute;
      break;
    case LoopHintAttr::PipelineDisabled:
    case LoopHintAttr::PipelineInitiationInterval:
      Category = Pipeline;
      break;
    case LoopHintAttr::VectorizePredicate:
      Category = VectorizePredicate;
      break;
    };

    assert(Category != NumberOfCategories && "Unhandled loop hint option");
    auto &CategoryState = HintAttrs[Category];
    const LoopHintAttr *PrevAttr;
    if (Option == LoopHintAttr::Vectorize ||
        Option == LoopHintAttr::Interleave || Option == LoopHintAttr::Unroll ||
        Option == LoopHintAttr::UnrollAndJam ||
        Option == LoopHintAttr::VectorizePredicate ||
        Option == LoopHintAttr::PipelineDisabled ||
        Option == LoopHintAttr::Distribute) {
      // Enable|Disable|AssumeSafety hint.  For example, vectorize(enable).
      PrevAttr = CategoryState.StateAttr;
      CategoryState.StateAttr = LH;
    } else {
      // Numeric hint.  For example, vectorize_width(8).
      PrevAttr = CategoryState.NumericAttr;
      CategoryState.NumericAttr = LH;
    }

    PrintingPolicy Policy(S.Context.getLangOpts());
    SourceLocation OptionLoc = LH->getRange().getBegin();
    if (PrevAttr)
      // Cannot specify same type of attribute twice.
      S.Diag(OptionLoc, diag::err_pragma_loop_compatibility)
          << /*Duplicate=*/true << PrevAttr->getDiagnosticName(Policy)
          << LH->getDiagnosticName(Policy);

    if (CategoryState.StateAttr && CategoryState.NumericAttr &&
        (Category == Unroll || Category == UnrollAndJam ||
         CategoryState.StateAttr->getState() == LoopHintAttr::Disable)) {
      // Disable hints are not compatible with numeric hints of the same
      // category.  As a special case, numeric unroll hints are also not
      // compatible with enable or full form of the unroll pragma because these
      // directives indicate full unrolling.
      S.Diag(OptionLoc, diag::err_pragma_loop_compatibility)
          << /*Duplicate=*/false
          << CategoryState.StateAttr->getDiagnosticName(Policy)
          << CategoryState.NumericAttr->getDiagnosticName(Policy);
    }
  }
}

static Attr *handleOpenCLUnrollHint(Sema &S, Stmt *St, const ParsedAttr &A,
                                    SourceRange Range) {
  // Although the feature was introduced only in OpenCL C v2.0 s6.11.5, it's
  // useful for OpenCL 1.x too and doesn't require HW support.
  // opencl_unroll_hint can have 0 arguments (compiler
  // determines unrolling factor) or 1 argument (the unroll factor provided
  // by the user).
  unsigned UnrollFactor = 0;
  if (A.getNumArgs() == 1) {
    Expr *E = A.getArgAsExpr(0);
    std::optional<llvm::APSInt> ArgVal;

    if (!(ArgVal = E->getIntegerConstantExpr(S.Context))) {
      S.Diag(A.getLoc(), diag::err_attribute_argument_type)
          << A << AANT_ArgumentIntegerConstant << E->getSourceRange();
      return nullptr;
    }

    int Val = ArgVal->getSExtValue();
    if (Val <= 0) {
      S.Diag(A.getRange().getBegin(),
             diag::err_attribute_requires_positive_integer)
          << A << /* positive */ 0;
      return nullptr;
    }
    UnrollFactor = static_cast<unsigned>(Val);
  }

  return ::new (S.Context) OpenCLUnrollHintAttr(S.Context, A, UnrollFactor);
}

static Attr *handleHLSLLoopHintAttr(Sema &S, Stmt *St, const ParsedAttr &A,
                                    SourceRange Range) {

  if (A.getSemanticSpelling() == HLSLLoopHintAttr::Spelling::Microsoft_loop &&
      !A.checkAtMostNumArgs(S, 0))
    return nullptr;

  unsigned UnrollFactor = 0;
  if (A.getNumArgs() == 1) {

    if (A.isArgIdent(0)) {
      S.Diag(A.getLoc(), diag::err_attribute_argument_type)
          << A << AANT_ArgumentIntegerConstant << A.getRange();
      return nullptr;
    }

    Expr *E = A.getArgAsExpr(0);

    if (S.CheckLoopHintExpr(E, St->getBeginLoc(),
                            /*AllowZero=*/false))
      return nullptr;

    std::optional<llvm::APSInt> ArgVal = E->getIntegerConstantExpr(S.Context);
    // CheckLoopHintExpr handles non int const cases
    assert(ArgVal != std::nullopt && "ArgVal should be an integer constant.");
    int Val = ArgVal->getSExtValue();
    // CheckLoopHintExpr handles negative and zero cases
    assert(Val > 0 && "Val should be a positive integer greater than zero.");
    UnrollFactor = static_cast<unsigned>(Val);
  }
  return ::new (S.Context) HLSLLoopHintAttr(S.Context, A, UnrollFactor);
}

static Attr *ProcessStmtAttribute(Sema &S, Stmt *St, const ParsedAttr &A,
                                  SourceRange Range) {
  if (A.isInvalid() || A.getKind() == ParsedAttr::IgnoredAttribute)
    return nullptr;

  // Unknown attributes are automatically warned on. Target-specific attributes
  // which do not apply to the current target architecture are treated as
  // though they were unknown attributes.
  const TargetInfo *Aux = S.Context.getAuxTargetInfo();
  if (A.getKind() == ParsedAttr::UnknownAttribute ||
      !(A.existsInTarget(S.Context.getTargetInfo()) ||
        (S.Context.getLangOpts().SYCLIsDevice && Aux &&
         A.existsInTarget(*Aux)))) {
    S.Diag(A.getLoc(), A.isRegularKeywordAttribute()
                           ? (unsigned)diag::err_keyword_not_supported_on_target
                       : A.isDeclspecAttribute()
                           ? (unsigned)diag::warn_unhandled_ms_attribute_ignored
                           : (unsigned)diag::warn_unknown_attribute_ignored)
        << A << A.getRange();
    return nullptr;
  }

  if (S.checkCommonAttributeFeatures(St, A))
    return nullptr;

  switch (A.getKind()) {
  case ParsedAttr::AT_AlwaysInline:
    return handleAlwaysInlineAttr(S, St, A, Range);
  case ParsedAttr::AT_CXXAssume:
    return handleCXXAssumeAttr(S, St, A, Range);
  case ParsedAttr::AT_FallThrough:
    return handleFallThroughAttr(S, St, A, Range);
  case ParsedAttr::AT_LoopHint:
    return handleLoopHintAttr(S, St, A, Range);
  case ParsedAttr::AT_HLSLLoopHint:
    return handleHLSLLoopHintAttr(S, St, A, Range);
  case ParsedAttr::AT_OpenCLUnrollHint:
    return handleOpenCLUnrollHint(S, St, A, Range);
  case ParsedAttr::AT_Suppress:
    return handleSuppressAttr(S, St, A, Range);
  case ParsedAttr::AT_NoMerge:
    return handleNoMergeAttr(S, St, A, Range);
  case ParsedAttr::AT_NoInline:
    return handleNoInlineAttr(S, St, A, Range);
  case ParsedAttr::AT_MustTail:
    return handleMustTailAttr(S, St, A, Range);
  case ParsedAttr::AT_Likely:
    return handleLikely(S, St, A, Range);
  case ParsedAttr::AT_Unlikely:
    return handleUnlikely(S, St, A, Range);
  case ParsedAttr::AT_CodeAlign:
    return handleCodeAlignAttr(S, St, A);
  case ParsedAttr::AT_MSConstexpr:
    return handleMSConstexprAttr(S, St, A, Range);
  default:
    // N.B., ClangAttrEmitter.cpp emits a diagnostic helper that ensures a
    // declaration attribute is not written on a statement, but this code is
    // needed for attributes in Attr.td that do not list any subjects.
    S.Diag(A.getRange().getBegin(), diag::err_decl_attribute_invalid_on_stmt)
        << A << A.isRegularKeywordAttribute() << St->getBeginLoc();
    return nullptr;
  }
}

void Sema::ProcessStmtAttributes(Stmt *S, const ParsedAttributes &InAttrs,
                                 SmallVectorImpl<const Attr *> &OutAttrs) {
  for (const ParsedAttr &AL : InAttrs) {
    if (const Attr *A = ProcessStmtAttribute(*this, S, AL, InAttrs.Range))
      OutAttrs.push_back(A);
  }

  CheckForIncompatibleAttributes(*this, OutAttrs);
  CheckForDuplicateLoopAttrs<CodeAlignAttr>(*this, OutAttrs);
}

bool Sema::CheckRebuiltStmtAttributes(ArrayRef<const Attr *> Attrs) {
  CheckForDuplicateLoopAttrs<CodeAlignAttr>(*this, Attrs);
  return false;
}

ExprResult Sema::ActOnCXXAssumeAttr(Stmt *St, const ParsedAttr &A,
                                    SourceRange Range) {
  if (A.getNumArgs() != 1 || !A.getArgAsExpr(0)) {
    Diag(A.getLoc(), diag::err_attribute_wrong_number_arguments)
        << A.getAttrName() << 1 << Range;
    return ExprError();
  }

  auto *Assumption = A.getArgAsExpr(0);

  if (DiagnoseUnexpandedParameterPack(Assumption)) {
    return ExprError();
  }

  if (Assumption->getDependence() == ExprDependence::None) {
    ExprResult Res = BuildCXXAssumeExpr(Assumption, A.getAttrName(), Range);
    if (Res.isInvalid())
      return ExprError();
    Assumption = Res.get();
  }

  if (!getLangOpts().CPlusPlus23 &&
      A.getSyntax() == AttributeCommonInfo::AS_CXX11)
    Diag(A.getLoc(), diag::ext_cxx23_attr) << A << Range;

  return Assumption;
}

ExprResult Sema::BuildCXXAssumeExpr(Expr *Assumption,
                                    const IdentifierInfo *AttrName,
                                    SourceRange Range) {
  ExprResult Res = CorrectDelayedTyposInExpr(Assumption);
  if (Res.isInvalid())
    return ExprError();

  Res = CheckPlaceholderExpr(Res.get());
  if (Res.isInvalid())
    return ExprError();

  Res = PerformContextuallyConvertToBool(Res.get());
  if (Res.isInvalid())
    return ExprError();

  Assumption = Res.get();
  if (Assumption->HasSideEffects(Context))
    Diag(Assumption->getBeginLoc(), diag::warn_assume_side_effects)
        << AttrName << Range;

  return Assumption;
}
