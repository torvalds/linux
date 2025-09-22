//===--- ParseOpenMP.cpp - OpenMP directives parsing ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements parsing of all OpenMP directives and clauses.
///
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/OpenMPClause.h"
#include "clang/AST/StmtOpenMP.h"
#include "clang/Basic/OpenMPKinds.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaAMDGPU.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "clang/Sema/SemaOpenMP.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Frontend/OpenMP/OMPAssume.h"
#include "llvm/Frontend/OpenMP/OMPContext.h"
#include <optional>

using namespace clang;
using namespace llvm::omp;

//===----------------------------------------------------------------------===//
// OpenMP declarative directives.
//===----------------------------------------------------------------------===//

namespace {
enum OpenMPDirectiveKindEx {
  OMPD_cancellation = llvm::omp::Directive_enumSize + 1,
  OMPD_data,
  OMPD_declare,
  OMPD_end,
  OMPD_end_declare,
  OMPD_enter,
  OMPD_exit,
  OMPD_point,
  OMPD_reduction,
  OMPD_target_enter,
  OMPD_target_exit,
  OMPD_update,
  OMPD_distribute_parallel,
  OMPD_teams_distribute_parallel,
  OMPD_target_teams_distribute_parallel,
  OMPD_mapper,
  OMPD_variant,
  OMPD_begin,
  OMPD_begin_declare,
};

// Helper to unify the enum class OpenMPDirectiveKind with its extension
// the OpenMPDirectiveKindEx enum which allows to use them together as if they
// are unsigned values.
struct OpenMPDirectiveKindExWrapper {
  OpenMPDirectiveKindExWrapper(unsigned Value) : Value(Value) {}
  OpenMPDirectiveKindExWrapper(OpenMPDirectiveKind DK) : Value(unsigned(DK)) {}
  bool operator==(OpenMPDirectiveKindExWrapper V) const {
    return Value == V.Value;
  }
  bool operator!=(OpenMPDirectiveKindExWrapper V) const {
    return Value != V.Value;
  }
  bool operator==(OpenMPDirectiveKind V) const { return Value == unsigned(V); }
  bool operator!=(OpenMPDirectiveKind V) const { return Value != unsigned(V); }
  bool operator<(OpenMPDirectiveKind V) const { return Value < unsigned(V); }
  operator unsigned() const { return Value; }
  operator OpenMPDirectiveKind() const { return OpenMPDirectiveKind(Value); }
  unsigned Value;
};

class DeclDirectiveListParserHelper final {
  SmallVector<Expr *, 4> Identifiers;
  Parser *P;
  OpenMPDirectiveKind Kind;

public:
  DeclDirectiveListParserHelper(Parser *P, OpenMPDirectiveKind Kind)
      : P(P), Kind(Kind) {}
  void operator()(CXXScopeSpec &SS, DeclarationNameInfo NameInfo) {
    ExprResult Res = P->getActions().OpenMP().ActOnOpenMPIdExpression(
        P->getCurScope(), SS, NameInfo, Kind);
    if (Res.isUsable())
      Identifiers.push_back(Res.get());
  }
  llvm::ArrayRef<Expr *> getIdentifiers() const { return Identifiers; }
};
} // namespace

// Map token string to extended OMP token kind that are
// OpenMPDirectiveKind + OpenMPDirectiveKindEx.
static unsigned getOpenMPDirectiveKindEx(StringRef S) {
  OpenMPDirectiveKindExWrapper DKind = getOpenMPDirectiveKind(S);
  if (DKind != OMPD_unknown)
    return DKind;

  return llvm::StringSwitch<OpenMPDirectiveKindExWrapper>(S)
      .Case("cancellation", OMPD_cancellation)
      .Case("data", OMPD_data)
      .Case("declare", OMPD_declare)
      .Case("end", OMPD_end)
      .Case("enter", OMPD_enter)
      .Case("exit", OMPD_exit)
      .Case("point", OMPD_point)
      .Case("reduction", OMPD_reduction)
      .Case("update", OMPD_update)
      .Case("mapper", OMPD_mapper)
      .Case("variant", OMPD_variant)
      .Case("begin", OMPD_begin)
      .Default(OMPD_unknown);
}

static OpenMPDirectiveKindExWrapper parseOpenMPDirectiveKind(Parser &P) {
  // Array of foldings: F[i][0] F[i][1] ===> F[i][2].
  // E.g.: OMPD_for OMPD_simd ===> OMPD_for_simd
  // TODO: add other combined directives in topological order.
  static const OpenMPDirectiveKindExWrapper F[][3] = {
      {OMPD_begin, OMPD_declare, OMPD_begin_declare},
      {OMPD_begin, OMPD_assumes, OMPD_begin_assumes},
      {OMPD_end, OMPD_declare, OMPD_end_declare},
      {OMPD_end, OMPD_assumes, OMPD_end_assumes},
      {OMPD_cancellation, OMPD_point, OMPD_cancellation_point},
      {OMPD_declare, OMPD_reduction, OMPD_declare_reduction},
      {OMPD_declare, OMPD_mapper, OMPD_declare_mapper},
      {OMPD_declare, OMPD_simd, OMPD_declare_simd},
      {OMPD_declare, OMPD_target, OMPD_declare_target},
      {OMPD_declare, OMPD_variant, OMPD_declare_variant},
      {OMPD_begin_declare, OMPD_target, OMPD_begin_declare_target},
      {OMPD_begin_declare, OMPD_variant, OMPD_begin_declare_variant},
      {OMPD_end_declare, OMPD_variant, OMPD_end_declare_variant},
      {OMPD_distribute, OMPD_parallel, OMPD_distribute_parallel},
      {OMPD_distribute_parallel, OMPD_for, OMPD_distribute_parallel_for},
      {OMPD_distribute_parallel_for, OMPD_simd,
       OMPD_distribute_parallel_for_simd},
      {OMPD_distribute, OMPD_simd, OMPD_distribute_simd},
      {OMPD_end_declare, OMPD_target, OMPD_end_declare_target},
      {OMPD_target, OMPD_data, OMPD_target_data},
      {OMPD_target, OMPD_enter, OMPD_target_enter},
      {OMPD_target, OMPD_exit, OMPD_target_exit},
      {OMPD_target, OMPD_update, OMPD_target_update},
      {OMPD_target_enter, OMPD_data, OMPD_target_enter_data},
      {OMPD_target_exit, OMPD_data, OMPD_target_exit_data},
      {OMPD_for, OMPD_simd, OMPD_for_simd},
      {OMPD_parallel, OMPD_for, OMPD_parallel_for},
      {OMPD_parallel_for, OMPD_simd, OMPD_parallel_for_simd},
      {OMPD_parallel, OMPD_loop, OMPD_parallel_loop},
      {OMPD_parallel, OMPD_sections, OMPD_parallel_sections},
      {OMPD_taskloop, OMPD_simd, OMPD_taskloop_simd},
      {OMPD_target, OMPD_parallel, OMPD_target_parallel},
      {OMPD_target, OMPD_simd, OMPD_target_simd},
      {OMPD_target_parallel, OMPD_loop, OMPD_target_parallel_loop},
      {OMPD_target_parallel, OMPD_for, OMPD_target_parallel_for},
      {OMPD_target_parallel_for, OMPD_simd, OMPD_target_parallel_for_simd},
      {OMPD_teams, OMPD_distribute, OMPD_teams_distribute},
      {OMPD_teams_distribute, OMPD_simd, OMPD_teams_distribute_simd},
      {OMPD_teams_distribute, OMPD_parallel, OMPD_teams_distribute_parallel},
      {OMPD_teams_distribute_parallel, OMPD_for,
       OMPD_teams_distribute_parallel_for},
      {OMPD_teams_distribute_parallel_for, OMPD_simd,
       OMPD_teams_distribute_parallel_for_simd},
      {OMPD_teams, OMPD_loop, OMPD_teams_loop},
      {OMPD_target, OMPD_teams, OMPD_target_teams},
      {OMPD_target_teams, OMPD_distribute, OMPD_target_teams_distribute},
      {OMPD_target_teams, OMPD_loop, OMPD_target_teams_loop},
      {OMPD_target_teams_distribute, OMPD_parallel,
       OMPD_target_teams_distribute_parallel},
      {OMPD_target_teams_distribute, OMPD_simd,
       OMPD_target_teams_distribute_simd},
      {OMPD_target_teams_distribute_parallel, OMPD_for,
       OMPD_target_teams_distribute_parallel_for},
      {OMPD_target_teams_distribute_parallel_for, OMPD_simd,
       OMPD_target_teams_distribute_parallel_for_simd},
      {OMPD_master, OMPD_taskloop, OMPD_master_taskloop},
      {OMPD_masked, OMPD_taskloop, OMPD_masked_taskloop},
      {OMPD_master_taskloop, OMPD_simd, OMPD_master_taskloop_simd},
      {OMPD_masked_taskloop, OMPD_simd, OMPD_masked_taskloop_simd},
      {OMPD_parallel, OMPD_master, OMPD_parallel_master},
      {OMPD_parallel, OMPD_masked, OMPD_parallel_masked},
      {OMPD_parallel_master, OMPD_taskloop, OMPD_parallel_master_taskloop},
      {OMPD_parallel_masked, OMPD_taskloop, OMPD_parallel_masked_taskloop},
      {OMPD_parallel_master_taskloop, OMPD_simd,
       OMPD_parallel_master_taskloop_simd},
      {OMPD_parallel_masked_taskloop, OMPD_simd,
       OMPD_parallel_masked_taskloop_simd}};
  enum { CancellationPoint = 0, DeclareReduction = 1, TargetData = 2 };
  Token Tok = P.getCurToken();
  OpenMPDirectiveKindExWrapper DKind =
      Tok.isAnnotation()
          ? static_cast<unsigned>(OMPD_unknown)
          : getOpenMPDirectiveKindEx(P.getPreprocessor().getSpelling(Tok));
  if (DKind == OMPD_unknown)
    return OMPD_unknown;

  for (const auto &I : F) {
    if (DKind != I[0])
      continue;

    Tok = P.getPreprocessor().LookAhead(0);
    OpenMPDirectiveKindExWrapper SDKind =
        Tok.isAnnotation()
            ? static_cast<unsigned>(OMPD_unknown)
            : getOpenMPDirectiveKindEx(P.getPreprocessor().getSpelling(Tok));
    if (SDKind == OMPD_unknown)
      continue;

    if (SDKind == I[1]) {
      P.ConsumeToken();
      DKind = I[2];
    }
  }
  return unsigned(DKind) < llvm::omp::Directive_enumSize
             ? static_cast<OpenMPDirectiveKind>(DKind)
             : OMPD_unknown;
}

static DeclarationName parseOpenMPReductionId(Parser &P) {
  Token Tok = P.getCurToken();
  Sema &Actions = P.getActions();
  OverloadedOperatorKind OOK = OO_None;
  // Allow to use 'operator' keyword for C++ operators
  bool WithOperator = false;
  if (Tok.is(tok::kw_operator)) {
    P.ConsumeToken();
    Tok = P.getCurToken();
    WithOperator = true;
  }
  switch (Tok.getKind()) {
  case tok::plus: // '+'
    OOK = OO_Plus;
    break;
  case tok::minus: // '-'
    OOK = OO_Minus;
    break;
  case tok::star: // '*'
    OOK = OO_Star;
    break;
  case tok::amp: // '&'
    OOK = OO_Amp;
    break;
  case tok::pipe: // '|'
    OOK = OO_Pipe;
    break;
  case tok::caret: // '^'
    OOK = OO_Caret;
    break;
  case tok::ampamp: // '&&'
    OOK = OO_AmpAmp;
    break;
  case tok::pipepipe: // '||'
    OOK = OO_PipePipe;
    break;
  case tok::identifier: // identifier
    if (!WithOperator)
      break;
    [[fallthrough]];
  default:
    P.Diag(Tok.getLocation(), diag::err_omp_expected_reduction_identifier);
    P.SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                Parser::StopBeforeMatch);
    return DeclarationName();
  }
  P.ConsumeToken();
  auto &DeclNames = Actions.getASTContext().DeclarationNames;
  return OOK == OO_None ? DeclNames.getIdentifier(Tok.getIdentifierInfo())
                        : DeclNames.getCXXOperatorName(OOK);
}

/// Parse 'omp declare reduction' construct.
///
///       declare-reduction-directive:
///        annot_pragma_openmp 'declare' 'reduction'
///        '(' <reduction_id> ':' <type> {',' <type>} ':' <expression> ')'
///        ['initializer' '(' ('omp_priv' '=' <expression>)|<function_call> ')']
///        annot_pragma_openmp_end
/// <reduction_id> is either a base language identifier or one of the following
/// operators: '+', '-', '*', '&', '|', '^', '&&' and '||'.
///
Parser::DeclGroupPtrTy
Parser::ParseOpenMPDeclareReductionDirective(AccessSpecifier AS) {
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(
          diag::err_expected_lparen_after,
          getOpenMPDirectiveName(OMPD_declare_reduction).data())) {
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  DeclarationName Name = parseOpenMPReductionId(*this);
  if (Name.isEmpty() && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  // Consume ':'.
  bool IsCorrect = !ExpectAndConsume(tok::colon);

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  IsCorrect = IsCorrect && !Name.isEmpty();

  if (Tok.is(tok::colon) || Tok.is(tok::annot_pragma_openmp_end)) {
    Diag(Tok.getLocation(), diag::err_expected_type);
    IsCorrect = false;
  }

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  SmallVector<std::pair<QualType, SourceLocation>, 8> ReductionTypes;
  // Parse list of types until ':' token.
  do {
    ColonProtectionRAIIObject ColonRAII(*this);
    SourceRange Range;
    TypeResult TR = ParseTypeName(&Range, DeclaratorContext::Prototype, AS);
    if (TR.isUsable()) {
      QualType ReductionType = Actions.OpenMP().ActOnOpenMPDeclareReductionType(
          Range.getBegin(), TR);
      if (!ReductionType.isNull()) {
        ReductionTypes.push_back(
            std::make_pair(ReductionType, Range.getBegin()));
      }
    } else {
      SkipUntil(tok::comma, tok::colon, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }

    if (Tok.is(tok::colon) || Tok.is(tok::annot_pragma_openmp_end))
      break;

    // Consume ','.
    if (ExpectAndConsume(tok::comma)) {
      IsCorrect = false;
      if (Tok.is(tok::annot_pragma_openmp_end)) {
        Diag(Tok.getLocation(), diag::err_expected_type);
        return DeclGroupPtrTy();
      }
    }
  } while (Tok.isNot(tok::annot_pragma_openmp_end));

  if (ReductionTypes.empty()) {
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  // Consume ':'.
  if (ExpectAndConsume(tok::colon))
    IsCorrect = false;

  if (Tok.is(tok::annot_pragma_openmp_end)) {
    Diag(Tok.getLocation(), diag::err_expected_expression);
    return DeclGroupPtrTy();
  }

  DeclGroupPtrTy DRD =
      Actions.OpenMP().ActOnOpenMPDeclareReductionDirectiveStart(
          getCurScope(), Actions.getCurLexicalContext(), Name, ReductionTypes,
          AS);

  // Parse <combiner> expression and then parse initializer if any for each
  // correct type.
  unsigned I = 0, E = ReductionTypes.size();
  for (Decl *D : DRD.get()) {
    TentativeParsingAction TPA(*this);
    ParseScope OMPDRScope(this, Scope::FnScope | Scope::DeclScope |
                                    Scope::CompoundStmtScope |
                                    Scope::OpenMPDirectiveScope);
    // Parse <combiner> expression.
    Actions.OpenMP().ActOnOpenMPDeclareReductionCombinerStart(getCurScope(), D);
    ExprResult CombinerResult = Actions.ActOnFinishFullExpr(
        ParseExpression().get(), D->getLocation(), /*DiscardedValue*/ false);
    Actions.OpenMP().ActOnOpenMPDeclareReductionCombinerEnd(
        D, CombinerResult.get());

    if (CombinerResult.isInvalid() && Tok.isNot(tok::r_paren) &&
        Tok.isNot(tok::annot_pragma_openmp_end)) {
      TPA.Commit();
      IsCorrect = false;
      break;
    }
    IsCorrect = !T.consumeClose() && IsCorrect && CombinerResult.isUsable();
    ExprResult InitializerResult;
    if (Tok.isNot(tok::annot_pragma_openmp_end)) {
      // Parse <initializer> expression.
      if (Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->isStr("initializer")) {
        ConsumeToken();
      } else {
        Diag(Tok.getLocation(), diag::err_expected) << "'initializer'";
        TPA.Commit();
        IsCorrect = false;
        break;
      }
      // Parse '('.
      BalancedDelimiterTracker T(*this, tok::l_paren,
                                 tok::annot_pragma_openmp_end);
      IsCorrect =
          !T.expectAndConsume(diag::err_expected_lparen_after, "initializer") &&
          IsCorrect;
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        ParseScope OMPDRScope(this, Scope::FnScope | Scope::DeclScope |
                                        Scope::CompoundStmtScope |
                                        Scope::OpenMPDirectiveScope);
        // Parse expression.
        VarDecl *OmpPrivParm =
            Actions.OpenMP().ActOnOpenMPDeclareReductionInitializerStart(
                getCurScope(), D);
        // Check if initializer is omp_priv <init_expr> or something else.
        if (Tok.is(tok::identifier) &&
            Tok.getIdentifierInfo()->isStr("omp_priv")) {
          ConsumeToken();
          ParseOpenMPReductionInitializerForDecl(OmpPrivParm);
        } else {
          InitializerResult = Actions.ActOnFinishFullExpr(
              ParseAssignmentExpression().get(), D->getLocation(),
              /*DiscardedValue*/ false);
        }
        Actions.OpenMP().ActOnOpenMPDeclareReductionInitializerEnd(
            D, InitializerResult.get(), OmpPrivParm);
        if (InitializerResult.isInvalid() && Tok.isNot(tok::r_paren) &&
            Tok.isNot(tok::annot_pragma_openmp_end)) {
          TPA.Commit();
          IsCorrect = false;
          break;
        }
        IsCorrect =
            !T.consumeClose() && IsCorrect && !InitializerResult.isInvalid();
      }
    }

    ++I;
    // Revert parsing if not the last type, otherwise accept it, we're done with
    // parsing.
    if (I != E)
      TPA.Revert();
    else
      TPA.Commit();
  }
  return Actions.OpenMP().ActOnOpenMPDeclareReductionDirectiveEnd(
      getCurScope(), DRD, IsCorrect);
}

void Parser::ParseOpenMPReductionInitializerForDecl(VarDecl *OmpPrivParm) {
  // Parse declarator '=' initializer.
  // If a '==' or '+=' is found, suggest a fixit to '='.
  if (isTokenEqualOrEqualTypo()) {
    ConsumeToken();

    if (Tok.is(tok::code_completion)) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteInitializer(getCurScope(),
                                                       OmpPrivParm);
      Actions.FinalizeDeclaration(OmpPrivParm);
      return;
    }

    PreferredType.enterVariableInit(Tok.getLocation(), OmpPrivParm);
    ExprResult Init = ParseInitializer();

    if (Init.isInvalid()) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openmp_end, StopBeforeMatch);
      Actions.ActOnInitializerError(OmpPrivParm);
    } else {
      Actions.AddInitializerToDecl(OmpPrivParm, Init.get(),
                                   /*DirectInit=*/false);
    }
  } else if (Tok.is(tok::l_paren)) {
    // Parse C++ direct initializer: '(' expression-list ')'
    BalancedDelimiterTracker T(*this, tok::l_paren);
    T.consumeOpen();

    ExprVector Exprs;

    SourceLocation LParLoc = T.getOpenLocation();
    auto RunSignatureHelp = [this, OmpPrivParm, LParLoc, &Exprs]() {
      QualType PreferredType =
          Actions.CodeCompletion().ProduceConstructorSignatureHelp(
              OmpPrivParm->getType()->getCanonicalTypeInternal(),
              OmpPrivParm->getLocation(), Exprs, LParLoc, /*Braced=*/false);
      CalledSignatureHelp = true;
      return PreferredType;
    };
    if (ParseExpressionList(Exprs, [&] {
          PreferredType.enterFunctionArgument(Tok.getLocation(),
                                              RunSignatureHelp);
        })) {
      if (PP.isCodeCompletionReached() && !CalledSignatureHelp)
        RunSignatureHelp();
      Actions.ActOnInitializerError(OmpPrivParm);
      SkipUntil(tok::r_paren, tok::annot_pragma_openmp_end, StopBeforeMatch);
    } else {
      // Match the ')'.
      SourceLocation RLoc = Tok.getLocation();
      if (!T.consumeClose())
        RLoc = T.getCloseLocation();

      ExprResult Initializer =
          Actions.ActOnParenListExpr(T.getOpenLocation(), RLoc, Exprs);
      Actions.AddInitializerToDecl(OmpPrivParm, Initializer.get(),
                                   /*DirectInit=*/true);
    }
  } else if (getLangOpts().CPlusPlus11 && Tok.is(tok::l_brace)) {
    // Parse C++0x braced-init-list.
    Diag(Tok, diag::warn_cxx98_compat_generalized_initializer_lists);

    ExprResult Init(ParseBraceInitializer());

    if (Init.isInvalid()) {
      Actions.ActOnInitializerError(OmpPrivParm);
    } else {
      Actions.AddInitializerToDecl(OmpPrivParm, Init.get(),
                                   /*DirectInit=*/true);
    }
  } else {
    Actions.ActOnUninitializedDecl(OmpPrivParm);
  }
}

/// Parses 'omp declare mapper' directive.
///
///       declare-mapper-directive:
///         annot_pragma_openmp 'declare' 'mapper' '(' [<mapper-identifier> ':']
///         <type> <var> ')' [<clause>[[,] <clause>] ... ]
///         annot_pragma_openmp_end
/// <mapper-identifier> and <var> are base language identifiers.
///
Parser::DeclGroupPtrTy
Parser::ParseOpenMPDeclareMapperDirective(AccessSpecifier AS) {
  bool IsCorrect = true;
  // Parse '('
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPDirectiveName(OMPD_declare_mapper).data())) {
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  // Parse <mapper-identifier>
  auto &DeclNames = Actions.getASTContext().DeclarationNames;
  DeclarationName MapperId;
  if (PP.LookAhead(0).is(tok::colon)) {
    if (Tok.isNot(tok::identifier) && Tok.isNot(tok::kw_default)) {
      Diag(Tok.getLocation(), diag::err_omp_mapper_illegal_identifier);
      IsCorrect = false;
    } else {
      MapperId = DeclNames.getIdentifier(Tok.getIdentifierInfo());
    }
    ConsumeToken();
    // Consume ':'.
    ExpectAndConsume(tok::colon);
  } else {
    // If no mapper identifier is provided, its name is "default" by default
    MapperId =
        DeclNames.getIdentifier(&Actions.getASTContext().Idents.get("default"));
  }

  if (!IsCorrect && Tok.is(tok::annot_pragma_openmp_end))
    return DeclGroupPtrTy();

  // Parse <type> <var>
  DeclarationName VName;
  QualType MapperType;
  SourceRange Range;
  TypeResult ParsedType = parseOpenMPDeclareMapperVarDecl(Range, VName, AS);
  if (ParsedType.isUsable())
    MapperType = Actions.OpenMP().ActOnOpenMPDeclareMapperType(Range.getBegin(),
                                                               ParsedType);
  if (MapperType.isNull())
    IsCorrect = false;
  if (!IsCorrect) {
    SkipUntil(tok::annot_pragma_openmp_end, Parser::StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  // Consume ')'.
  IsCorrect &= !T.consumeClose();
  if (!IsCorrect) {
    SkipUntil(tok::annot_pragma_openmp_end, Parser::StopBeforeMatch);
    return DeclGroupPtrTy();
  }

  // Enter scope.
  DeclarationNameInfo DirName;
  SourceLocation Loc = Tok.getLocation();
  unsigned ScopeFlags = Scope::FnScope | Scope::DeclScope |
                        Scope::CompoundStmtScope | Scope::OpenMPDirectiveScope;
  ParseScope OMPDirectiveScope(this, ScopeFlags);
  Actions.OpenMP().StartOpenMPDSABlock(OMPD_declare_mapper, DirName,
                                       getCurScope(), Loc);

  // Add the mapper variable declaration.
  ExprResult MapperVarRef =
      Actions.OpenMP().ActOnOpenMPDeclareMapperDirectiveVarDecl(
          getCurScope(), MapperType, Range.getBegin(), VName);

  // Parse map clauses.
  SmallVector<OMPClause *, 6> Clauses;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    OpenMPClauseKind CKind = Tok.isAnnotation()
                                 ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
    Actions.OpenMP().StartOpenMPClause(CKind);
    OMPClause *Clause =
        ParseOpenMPClause(OMPD_declare_mapper, CKind, Clauses.empty());
    if (Clause)
      Clauses.push_back(Clause);
    else
      IsCorrect = false;
    // Skip ',' if any.
    if (Tok.is(tok::comma))
      ConsumeToken();
    Actions.OpenMP().EndOpenMPClause();
  }
  if (Clauses.empty()) {
    Diag(Tok, diag::err_omp_expected_clause)
        << getOpenMPDirectiveName(OMPD_declare_mapper);
    IsCorrect = false;
  }

  // Exit scope.
  Actions.OpenMP().EndOpenMPDSABlock(nullptr);
  OMPDirectiveScope.Exit();
  DeclGroupPtrTy DG = Actions.OpenMP().ActOnOpenMPDeclareMapperDirective(
      getCurScope(), Actions.getCurLexicalContext(), MapperId, MapperType,
      Range.getBegin(), VName, AS, MapperVarRef.get(), Clauses);
  if (!IsCorrect)
    return DeclGroupPtrTy();

  return DG;
}

TypeResult Parser::parseOpenMPDeclareMapperVarDecl(SourceRange &Range,
                                                   DeclarationName &Name,
                                                   AccessSpecifier AS) {
  // Parse the common declaration-specifiers piece.
  Parser::DeclSpecContext DSC = Parser::DeclSpecContext::DSC_type_specifier;
  DeclSpec DS(AttrFactory);
  ParseSpecifierQualifierList(DS, AS, DSC);

  // Parse the declarator.
  DeclaratorContext Context = DeclaratorContext::Prototype;
  Declarator DeclaratorInfo(DS, ParsedAttributesView::none(), Context);
  ParseDeclarator(DeclaratorInfo);
  Range = DeclaratorInfo.getSourceRange();
  if (DeclaratorInfo.getIdentifier() == nullptr) {
    Diag(Tok.getLocation(), diag::err_omp_mapper_expected_declarator);
    return true;
  }
  Name = Actions.GetNameForDeclarator(DeclaratorInfo).getName();

  return Actions.OpenMP().ActOnOpenMPDeclareMapperVarDecl(getCurScope(),
                                                          DeclaratorInfo);
}

namespace {
/// RAII that recreates function context for correct parsing of clauses of
/// 'declare simd' construct.
/// OpenMP, 2.8.2 declare simd Construct
/// The expressions appearing in the clauses of this directive are evaluated in
/// the scope of the arguments of the function declaration or definition.
class FNContextRAII final {
  Parser &P;
  Sema::CXXThisScopeRAII *ThisScope;
  Parser::MultiParseScope Scopes;
  bool HasFunScope = false;
  FNContextRAII() = delete;
  FNContextRAII(const FNContextRAII &) = delete;
  FNContextRAII &operator=(const FNContextRAII &) = delete;

public:
  FNContextRAII(Parser &P, Parser::DeclGroupPtrTy Ptr) : P(P), Scopes(P) {
    Decl *D = *Ptr.get().begin();
    NamedDecl *ND = dyn_cast<NamedDecl>(D);
    RecordDecl *RD = dyn_cast_or_null<RecordDecl>(D->getDeclContext());
    Sema &Actions = P.getActions();

    // Allow 'this' within late-parsed attributes.
    ThisScope = new Sema::CXXThisScopeRAII(Actions, RD, Qualifiers(),
                                           ND && ND->isCXXInstanceMember());

    // If the Decl is templatized, add template parameters to scope.
    // FIXME: Track CurTemplateDepth?
    P.ReenterTemplateScopes(Scopes, D);

    // If the Decl is on a function, add function parameters to the scope.
    if (D->isFunctionOrFunctionTemplate()) {
      HasFunScope = true;
      Scopes.Enter(Scope::FnScope | Scope::DeclScope |
                   Scope::CompoundStmtScope);
      Actions.ActOnReenterFunctionContext(Actions.getCurScope(), D);
    }
  }
  ~FNContextRAII() {
    if (HasFunScope)
      P.getActions().ActOnExitFunctionContext();
    delete ThisScope;
  }
};
} // namespace

/// Parses clauses for 'declare simd' directive.
///    clause:
///      'inbranch' | 'notinbranch'
///      'simdlen' '(' <expr> ')'
///      { 'uniform' '(' <argument_list> ')' }
///      { 'aligned '(' <argument_list> [ ':' <alignment> ] ')' }
///      { 'linear '(' <argument_list> [ ':' <step> ] ')' }
static bool parseDeclareSimdClauses(
    Parser &P, OMPDeclareSimdDeclAttr::BranchStateTy &BS, ExprResult &SimdLen,
    SmallVectorImpl<Expr *> &Uniforms, SmallVectorImpl<Expr *> &Aligneds,
    SmallVectorImpl<Expr *> &Alignments, SmallVectorImpl<Expr *> &Linears,
    SmallVectorImpl<unsigned> &LinModifiers, SmallVectorImpl<Expr *> &Steps) {
  SourceRange BSRange;
  const Token &Tok = P.getCurToken();
  bool IsError = false;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    if (Tok.isNot(tok::identifier))
      break;
    OMPDeclareSimdDeclAttr::BranchStateTy Out;
    IdentifierInfo *II = Tok.getIdentifierInfo();
    StringRef ClauseName = II->getName();
    // Parse 'inranch|notinbranch' clauses.
    if (OMPDeclareSimdDeclAttr::ConvertStrToBranchStateTy(ClauseName, Out)) {
      if (BS != OMPDeclareSimdDeclAttr::BS_Undefined && BS != Out) {
        P.Diag(Tok, diag::err_omp_declare_simd_inbranch_notinbranch)
            << ClauseName
            << OMPDeclareSimdDeclAttr::ConvertBranchStateTyToStr(BS) << BSRange;
        IsError = true;
      }
      BS = Out;
      BSRange = SourceRange(Tok.getLocation(), Tok.getEndLoc());
      P.ConsumeToken();
    } else if (ClauseName == "simdlen") {
      if (SimdLen.isUsable()) {
        P.Diag(Tok, diag::err_omp_more_one_clause)
            << getOpenMPDirectiveName(OMPD_declare_simd) << ClauseName << 0;
        IsError = true;
      }
      P.ConsumeToken();
      SourceLocation RLoc;
      SimdLen = P.ParseOpenMPParensExpr(ClauseName, RLoc);
      if (SimdLen.isInvalid())
        IsError = true;
    } else {
      OpenMPClauseKind CKind = getOpenMPClauseKind(ClauseName);
      if (CKind == OMPC_uniform || CKind == OMPC_aligned ||
          CKind == OMPC_linear) {
        SemaOpenMP::OpenMPVarListDataTy Data;
        SmallVectorImpl<Expr *> *Vars = &Uniforms;
        if (CKind == OMPC_aligned) {
          Vars = &Aligneds;
        } else if (CKind == OMPC_linear) {
          Data.ExtraModifier = OMPC_LINEAR_val;
          Vars = &Linears;
        }

        P.ConsumeToken();
        if (P.ParseOpenMPVarList(OMPD_declare_simd,
                                 getOpenMPClauseKind(ClauseName), *Vars, Data))
          IsError = true;
        if (CKind == OMPC_aligned) {
          Alignments.append(Aligneds.size() - Alignments.size(),
                            Data.DepModOrTailExpr);
        } else if (CKind == OMPC_linear) {
          assert(0 <= Data.ExtraModifier &&
                 Data.ExtraModifier <= OMPC_LINEAR_unknown &&
                 "Unexpected linear modifier.");
          if (P.getActions().OpenMP().CheckOpenMPLinearModifier(
                  static_cast<OpenMPLinearClauseKind>(Data.ExtraModifier),
                  Data.ExtraModifierLoc))
            Data.ExtraModifier = OMPC_LINEAR_val;
          LinModifiers.append(Linears.size() - LinModifiers.size(),
                              Data.ExtraModifier);
          Steps.append(Linears.size() - Steps.size(), Data.DepModOrTailExpr);
        }
      } else
        // TODO: add parsing of other clauses.
        break;
    }
    // Skip ',' if any.
    if (Tok.is(tok::comma))
      P.ConsumeToken();
  }
  return IsError;
}

/// Parse clauses for '#pragma omp declare simd'.
Parser::DeclGroupPtrTy
Parser::ParseOMPDeclareSimdClauses(Parser::DeclGroupPtrTy Ptr,
                                   CachedTokens &Toks, SourceLocation Loc) {
  PP.EnterToken(Tok, /*IsReinject*/ true);
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject*/ true);
  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  FNContextRAII FnContext(*this, Ptr);
  OMPDeclareSimdDeclAttr::BranchStateTy BS =
      OMPDeclareSimdDeclAttr::BS_Undefined;
  ExprResult Simdlen;
  SmallVector<Expr *, 4> Uniforms;
  SmallVector<Expr *, 4> Aligneds;
  SmallVector<Expr *, 4> Alignments;
  SmallVector<Expr *, 4> Linears;
  SmallVector<unsigned, 4> LinModifiers;
  SmallVector<Expr *, 4> Steps;
  bool IsError =
      parseDeclareSimdClauses(*this, BS, Simdlen, Uniforms, Aligneds,
                              Alignments, Linears, LinModifiers, Steps);
  skipUntilPragmaOpenMPEnd(OMPD_declare_simd);
  // Skip the last annot_pragma_openmp_end.
  SourceLocation EndLoc = ConsumeAnnotationToken();
  if (IsError)
    return Ptr;
  return Actions.OpenMP().ActOnOpenMPDeclareSimdDirective(
      Ptr, BS, Simdlen.get(), Uniforms, Aligneds, Alignments, Linears,
      LinModifiers, Steps, SourceRange(Loc, EndLoc));
}

namespace {
/// Constant used in the diagnostics to distinguish the levels in an OpenMP
/// contexts: selector-set={selector(trait, ...), ...}, ....
enum OMPContextLvl {
  CONTEXT_SELECTOR_SET_LVL = 0,
  CONTEXT_SELECTOR_LVL = 1,
  CONTEXT_TRAIT_LVL = 2,
};

static StringRef stringLiteralParser(Parser &P) {
  ExprResult Res = P.ParseStringLiteralExpression(true);
  return Res.isUsable() ? Res.getAs<StringLiteral>()->getString() : "";
}

static StringRef getNameFromIdOrString(Parser &P, Token &Tok,
                                       OMPContextLvl Lvl) {
  if (Tok.is(tok::identifier) || Tok.is(tok::kw_for)) {
    llvm::SmallString<16> Buffer;
    StringRef Name = P.getPreprocessor().getSpelling(Tok, Buffer);
    (void)P.ConsumeToken();
    return Name;
  }

  if (tok::isStringLiteral(Tok.getKind()))
    return stringLiteralParser(P);

  P.Diag(Tok.getLocation(),
         diag::warn_omp_declare_variant_string_literal_or_identifier)
      << Lvl;
  return "";
}

static bool checkForDuplicates(Parser &P, StringRef Name,
                               SourceLocation NameLoc,
                               llvm::StringMap<SourceLocation> &Seen,
                               OMPContextLvl Lvl) {
  auto Res = Seen.try_emplace(Name, NameLoc);
  if (Res.second)
    return false;

  // Each trait-set-selector-name, trait-selector-name and trait-name can
  // only be specified once.
  P.Diag(NameLoc, diag::warn_omp_declare_variant_ctx_mutiple_use)
      << Lvl << Name;
  P.Diag(Res.first->getValue(), diag::note_omp_declare_variant_ctx_used_here)
      << Lvl << Name;
  return true;
}
} // namespace

void Parser::parseOMPTraitPropertyKind(OMPTraitProperty &TIProperty,
                                       llvm::omp::TraitSet Set,
                                       llvm::omp::TraitSelector Selector,
                                       llvm::StringMap<SourceLocation> &Seen) {
  TIProperty.Kind = TraitProperty::invalid;

  SourceLocation NameLoc = Tok.getLocation();
  StringRef Name = getNameFromIdOrString(*this, Tok, CONTEXT_TRAIT_LVL);
  if (Name.empty()) {
    Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_options)
        << CONTEXT_TRAIT_LVL << listOpenMPContextTraitProperties(Set, Selector);
    return;
  }

  TIProperty.RawString = Name;
  TIProperty.Kind = getOpenMPContextTraitPropertyKind(Set, Selector, Name);
  if (TIProperty.Kind != TraitProperty::invalid) {
    if (checkForDuplicates(*this, Name, NameLoc, Seen, CONTEXT_TRAIT_LVL))
      TIProperty.Kind = TraitProperty::invalid;
    return;
  }

  // It follows diagnosis and helping notes.
  // FIXME: We should move the diagnosis string generation into libFrontend.
  Diag(NameLoc, diag::warn_omp_declare_variant_ctx_not_a_property)
      << Name << getOpenMPContextTraitSelectorName(Selector)
      << getOpenMPContextTraitSetName(Set);

  TraitSet SetForName = getOpenMPContextTraitSetKind(Name);
  if (SetForName != TraitSet::invalid) {
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_SELECTOR_SET_LVL << CONTEXT_TRAIT_LVL;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << Name << "<selector-name>"
        << "(<property-name>)";
    return;
  }
  TraitSelector SelectorForName = getOpenMPContextTraitSelectorKind(Name);
  if (SelectorForName != TraitSelector::invalid) {
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_SELECTOR_LVL << CONTEXT_TRAIT_LVL;
    bool AllowsTraitScore = false;
    bool RequiresProperty = false;
    isValidTraitSelectorForTraitSet(
        SelectorForName, getOpenMPContextTraitSetForSelector(SelectorForName),
        AllowsTraitScore, RequiresProperty);
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForSelector(SelectorForName))
        << Name << (RequiresProperty ? "(<property-name>)" : "");
    return;
  }
  for (const auto &PotentialSet :
       {TraitSet::construct, TraitSet::user, TraitSet::implementation,
        TraitSet::device}) {
    TraitProperty PropertyForName =
        getOpenMPContextTraitPropertyKind(PotentialSet, Selector, Name);
    if (PropertyForName == TraitProperty::invalid)
      continue;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForProperty(PropertyForName))
        << getOpenMPContextTraitSelectorName(
               getOpenMPContextTraitSelectorForProperty(PropertyForName))
        << ("(" + Name + ")").str();
    return;
  }
  Diag(NameLoc, diag::note_omp_declare_variant_ctx_options)
      << CONTEXT_TRAIT_LVL << listOpenMPContextTraitProperties(Set, Selector);
}

static bool checkExtensionProperty(Parser &P, SourceLocation Loc,
                                   OMPTraitProperty &TIProperty,
                                   OMPTraitSelector &TISelector,
                                   llvm::StringMap<SourceLocation> &Seen) {
  assert(TISelector.Kind ==
             llvm::omp::TraitSelector::implementation_extension &&
         "Only for extension properties, e.g., "
         "`implementation={extension(PROPERTY)}`");
  if (TIProperty.Kind == TraitProperty::invalid)
    return false;

  if (TIProperty.Kind ==
      TraitProperty::implementation_extension_disable_implicit_base)
    return true;

  if (TIProperty.Kind ==
      TraitProperty::implementation_extension_allow_templates)
    return true;

  if (TIProperty.Kind ==
      TraitProperty::implementation_extension_bind_to_declaration)
    return true;

  auto IsMatchExtension = [](OMPTraitProperty &TP) {
    return (TP.Kind ==
                llvm::omp::TraitProperty::implementation_extension_match_all ||
            TP.Kind ==
                llvm::omp::TraitProperty::implementation_extension_match_any ||
            TP.Kind ==
                llvm::omp::TraitProperty::implementation_extension_match_none);
  };

  if (IsMatchExtension(TIProperty)) {
    for (OMPTraitProperty &SeenProp : TISelector.Properties)
      if (IsMatchExtension(SeenProp)) {
        P.Diag(Loc, diag::err_omp_variant_ctx_second_match_extension);
        StringRef SeenName = llvm::omp::getOpenMPContextTraitPropertyName(
            SeenProp.Kind, SeenProp.RawString);
        SourceLocation SeenLoc = Seen[SeenName];
        P.Diag(SeenLoc, diag::note_omp_declare_variant_ctx_used_here)
            << CONTEXT_TRAIT_LVL << SeenName;
        return false;
      }
    return true;
  }

  llvm_unreachable("Unknown extension property!");
}

void Parser::parseOMPContextProperty(OMPTraitSelector &TISelector,
                                     llvm::omp::TraitSet Set,
                                     llvm::StringMap<SourceLocation> &Seen) {
  assert(TISelector.Kind != TraitSelector::user_condition &&
         "User conditions are special properties not handled here!");

  SourceLocation PropertyLoc = Tok.getLocation();
  OMPTraitProperty TIProperty;
  parseOMPTraitPropertyKind(TIProperty, Set, TISelector.Kind, Seen);

  if (TISelector.Kind == llvm::omp::TraitSelector::implementation_extension)
    if (!checkExtensionProperty(*this, Tok.getLocation(), TIProperty,
                                TISelector, Seen))
      TIProperty.Kind = TraitProperty::invalid;

  // If we have an invalid property here we already issued a warning.
  if (TIProperty.Kind == TraitProperty::invalid) {
    if (PropertyLoc != Tok.getLocation())
      Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_continue_here)
          << CONTEXT_TRAIT_LVL;
    return;
  }

  if (isValidTraitPropertyForTraitSetAndSelector(TIProperty.Kind,
                                                 TISelector.Kind, Set)) {

    // If we make it here the property, selector, set, score, condition, ... are
    // all valid (or have been corrected). Thus we can record the property.
    TISelector.Properties.push_back(TIProperty);
    return;
  }

  Diag(PropertyLoc, diag::warn_omp_ctx_incompatible_property_for_selector)
      << getOpenMPContextTraitPropertyName(TIProperty.Kind,
                                           TIProperty.RawString)
      << getOpenMPContextTraitSelectorName(TISelector.Kind)
      << getOpenMPContextTraitSetName(Set);
  Diag(PropertyLoc, diag::note_omp_ctx_compatible_set_and_selector_for_property)
      << getOpenMPContextTraitPropertyName(TIProperty.Kind,
                                           TIProperty.RawString)
      << getOpenMPContextTraitSelectorName(
             getOpenMPContextTraitSelectorForProperty(TIProperty.Kind))
      << getOpenMPContextTraitSetName(
             getOpenMPContextTraitSetForProperty(TIProperty.Kind));
  Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_continue_here)
      << CONTEXT_TRAIT_LVL;
}

void Parser::parseOMPTraitSelectorKind(OMPTraitSelector &TISelector,
                                       llvm::omp::TraitSet Set,
                                       llvm::StringMap<SourceLocation> &Seen) {
  TISelector.Kind = TraitSelector::invalid;

  SourceLocation NameLoc = Tok.getLocation();
  StringRef Name = getNameFromIdOrString(*this, Tok, CONTEXT_SELECTOR_LVL);
  if (Name.empty()) {
    Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_options)
        << CONTEXT_SELECTOR_LVL << listOpenMPContextTraitSelectors(Set);
    return;
  }

  TISelector.Kind = getOpenMPContextTraitSelectorKind(Name);
  if (TISelector.Kind != TraitSelector::invalid) {
    if (checkForDuplicates(*this, Name, NameLoc, Seen, CONTEXT_SELECTOR_LVL))
      TISelector.Kind = TraitSelector::invalid;
    return;
  }

  // It follows diagnosis and helping notes.
  Diag(NameLoc, diag::warn_omp_declare_variant_ctx_not_a_selector)
      << Name << getOpenMPContextTraitSetName(Set);

  TraitSet SetForName = getOpenMPContextTraitSetKind(Name);
  if (SetForName != TraitSet::invalid) {
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_SELECTOR_SET_LVL << CONTEXT_SELECTOR_LVL;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << Name << "<selector-name>"
        << "<property-name>";
    return;
  }
  for (const auto &PotentialSet :
       {TraitSet::construct, TraitSet::user, TraitSet::implementation,
        TraitSet::device}) {
    TraitProperty PropertyForName = getOpenMPContextTraitPropertyKind(
        PotentialSet, TraitSelector::invalid, Name);
    if (PropertyForName == TraitProperty::invalid)
      continue;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_TRAIT_LVL << CONTEXT_SELECTOR_LVL;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForProperty(PropertyForName))
        << getOpenMPContextTraitSelectorName(
               getOpenMPContextTraitSelectorForProperty(PropertyForName))
        << ("(" + Name + ")").str();
    return;
  }
  Diag(NameLoc, diag::note_omp_declare_variant_ctx_options)
      << CONTEXT_SELECTOR_LVL << listOpenMPContextTraitSelectors(Set);
}

/// Parse optional 'score' '(' <expr> ')' ':'.
static ExprResult parseContextScore(Parser &P) {
  ExprResult ScoreExpr;
  llvm::SmallString<16> Buffer;
  StringRef SelectorName =
      P.getPreprocessor().getSpelling(P.getCurToken(), Buffer);
  if (SelectorName != "score")
    return ScoreExpr;
  (void)P.ConsumeToken();
  SourceLocation RLoc;
  ScoreExpr = P.ParseOpenMPParensExpr(SelectorName, RLoc);
  // Parse ':'
  if (P.getCurToken().is(tok::colon))
    (void)P.ConsumeAnyToken();
  else
    P.Diag(P.getCurToken(), diag::warn_omp_declare_variant_expected)
        << "':'"
        << "score expression";
  return ScoreExpr;
}

/// Parses an OpenMP context selector.
///
/// <trait-selector-name> ['('[<trait-score>] <trait-property> [, <t-p>]* ')']
void Parser::parseOMPContextSelector(
    OMPTraitSelector &TISelector, llvm::omp::TraitSet Set,
    llvm::StringMap<SourceLocation> &SeenSelectors) {
  unsigned short OuterPC = ParenCount;

  // If anything went wrong we issue an error or warning and then skip the rest
  // of the selector. However, commas are ambiguous so we look for the nesting
  // of parentheses here as well.
  auto FinishSelector = [OuterPC, this]() -> void {
    bool Done = false;
    while (!Done) {
      while (!SkipUntil({tok::r_brace, tok::r_paren, tok::comma,
                         tok::annot_pragma_openmp_end},
                        StopBeforeMatch))
        ;
      if (Tok.is(tok::r_paren) && OuterPC > ParenCount)
        (void)ConsumeParen();
      if (OuterPC <= ParenCount) {
        Done = true;
        break;
      }
      if (!Tok.is(tok::comma) && !Tok.is(tok::r_paren)) {
        Done = true;
        break;
      }
      (void)ConsumeAnyToken();
    }
    Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_continue_here)
        << CONTEXT_SELECTOR_LVL;
  };

  SourceLocation SelectorLoc = Tok.getLocation();
  parseOMPTraitSelectorKind(TISelector, Set, SeenSelectors);
  if (TISelector.Kind == TraitSelector::invalid)
    return FinishSelector();

  bool AllowsTraitScore = false;
  bool RequiresProperty = false;
  if (!isValidTraitSelectorForTraitSet(TISelector.Kind, Set, AllowsTraitScore,
                                       RequiresProperty)) {
    Diag(SelectorLoc, diag::warn_omp_ctx_incompatible_selector_for_set)
        << getOpenMPContextTraitSelectorName(TISelector.Kind)
        << getOpenMPContextTraitSetName(Set);
    Diag(SelectorLoc, diag::note_omp_ctx_compatible_set_for_selector)
        << getOpenMPContextTraitSelectorName(TISelector.Kind)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForSelector(TISelector.Kind))
        << RequiresProperty;
    return FinishSelector();
  }

  if (!RequiresProperty) {
    TISelector.Properties.push_back(
        {getOpenMPContextTraitPropertyForSelector(TISelector.Kind),
         getOpenMPContextTraitSelectorName(TISelector.Kind)});
    return;
  }

  if (!Tok.is(tok::l_paren)) {
    Diag(SelectorLoc, diag::warn_omp_ctx_selector_without_properties)
        << getOpenMPContextTraitSelectorName(TISelector.Kind)
        << getOpenMPContextTraitSetName(Set);
    return FinishSelector();
  }

  if (TISelector.Kind == TraitSelector::user_condition) {
    SourceLocation RLoc;
    ExprResult Condition = ParseOpenMPParensExpr("user condition", RLoc);
    if (!Condition.isUsable())
      return FinishSelector();
    TISelector.ScoreOrCondition = Condition.get();
    TISelector.Properties.push_back(
        {TraitProperty::user_condition_unknown, "<condition>"});
    return;
  }

  BalancedDelimiterTracker BDT(*this, tok::l_paren,
                               tok::annot_pragma_openmp_end);
  // Parse '('.
  (void)BDT.consumeOpen();

  SourceLocation ScoreLoc = Tok.getLocation();
  ExprResult Score = parseContextScore(*this);

  if (!AllowsTraitScore && !Score.isUnset()) {
    if (Score.isUsable()) {
      Diag(ScoreLoc, diag::warn_omp_ctx_incompatible_score_for_property)
          << getOpenMPContextTraitSelectorName(TISelector.Kind)
          << getOpenMPContextTraitSetName(Set) << Score.get();
    } else {
      Diag(ScoreLoc, diag::warn_omp_ctx_incompatible_score_for_property)
          << getOpenMPContextTraitSelectorName(TISelector.Kind)
          << getOpenMPContextTraitSetName(Set) << "<invalid>";
    }
    Score = ExprResult();
  }

  if (Score.isUsable())
    TISelector.ScoreOrCondition = Score.get();

  llvm::StringMap<SourceLocation> SeenProperties;
  do {
    parseOMPContextProperty(TISelector, Set, SeenProperties);
  } while (TryConsumeToken(tok::comma));

  // Parse ')'.
  BDT.consumeClose();
}

void Parser::parseOMPTraitSetKind(OMPTraitSet &TISet,
                                  llvm::StringMap<SourceLocation> &Seen) {
  TISet.Kind = TraitSet::invalid;

  SourceLocation NameLoc = Tok.getLocation();
  StringRef Name = getNameFromIdOrString(*this, Tok, CONTEXT_SELECTOR_SET_LVL);
  if (Name.empty()) {
    Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_options)
        << CONTEXT_SELECTOR_SET_LVL << listOpenMPContextTraitSets();
    return;
  }

  TISet.Kind = getOpenMPContextTraitSetKind(Name);
  if (TISet.Kind != TraitSet::invalid) {
    if (checkForDuplicates(*this, Name, NameLoc, Seen,
                           CONTEXT_SELECTOR_SET_LVL))
      TISet.Kind = TraitSet::invalid;
    return;
  }

  // It follows diagnosis and helping notes.
  Diag(NameLoc, diag::warn_omp_declare_variant_ctx_not_a_set) << Name;

  TraitSelector SelectorForName = getOpenMPContextTraitSelectorKind(Name);
  if (SelectorForName != TraitSelector::invalid) {
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_SELECTOR_LVL << CONTEXT_SELECTOR_SET_LVL;
    bool AllowsTraitScore = false;
    bool RequiresProperty = false;
    isValidTraitSelectorForTraitSet(
        SelectorForName, getOpenMPContextTraitSetForSelector(SelectorForName),
        AllowsTraitScore, RequiresProperty);
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForSelector(SelectorForName))
        << Name << (RequiresProperty ? "(<property-name>)" : "");
    return;
  }
  for (const auto &PotentialSet :
       {TraitSet::construct, TraitSet::user, TraitSet::implementation,
        TraitSet::device}) {
    TraitProperty PropertyForName = getOpenMPContextTraitPropertyKind(
        PotentialSet, TraitSelector::invalid, Name);
    if (PropertyForName == TraitProperty::invalid)
      continue;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_is_a)
        << Name << CONTEXT_TRAIT_LVL << CONTEXT_SELECTOR_SET_LVL;
    Diag(NameLoc, diag::note_omp_declare_variant_ctx_try)
        << getOpenMPContextTraitSetName(
               getOpenMPContextTraitSetForProperty(PropertyForName))
        << getOpenMPContextTraitSelectorName(
               getOpenMPContextTraitSelectorForProperty(PropertyForName))
        << ("(" + Name + ")").str();
    return;
  }
  Diag(NameLoc, diag::note_omp_declare_variant_ctx_options)
      << CONTEXT_SELECTOR_SET_LVL << listOpenMPContextTraitSets();
}

/// Parses an OpenMP context selector set.
///
/// <trait-set-selector-name> '=' '{' <trait-selector> [, <trait-selector>]* '}'
void Parser::parseOMPContextSelectorSet(
    OMPTraitSet &TISet, llvm::StringMap<SourceLocation> &SeenSets) {
  auto OuterBC = BraceCount;

  // If anything went wrong we issue an error or warning and then skip the rest
  // of the set. However, commas are ambiguous so we look for the nesting
  // of braces here as well.
  auto FinishSelectorSet = [this, OuterBC]() -> void {
    bool Done = false;
    while (!Done) {
      while (!SkipUntil({tok::comma, tok::r_brace, tok::r_paren,
                         tok::annot_pragma_openmp_end},
                        StopBeforeMatch))
        ;
      if (Tok.is(tok::r_brace) && OuterBC > BraceCount)
        (void)ConsumeBrace();
      if (OuterBC <= BraceCount) {
        Done = true;
        break;
      }
      if (!Tok.is(tok::comma) && !Tok.is(tok::r_brace)) {
        Done = true;
        break;
      }
      (void)ConsumeAnyToken();
    }
    Diag(Tok.getLocation(), diag::note_omp_declare_variant_ctx_continue_here)
        << CONTEXT_SELECTOR_SET_LVL;
  };

  parseOMPTraitSetKind(TISet, SeenSets);
  if (TISet.Kind == TraitSet::invalid)
    return FinishSelectorSet();

  // Parse '='.
  if (!TryConsumeToken(tok::equal))
    Diag(Tok.getLocation(), diag::warn_omp_declare_variant_expected)
        << "="
        << ("context set name \"" + getOpenMPContextTraitSetName(TISet.Kind) +
            "\"")
               .str();

  // Parse '{'.
  if (Tok.is(tok::l_brace)) {
    (void)ConsumeBrace();
  } else {
    Diag(Tok.getLocation(), diag::warn_omp_declare_variant_expected)
        << "{"
        << ("'=' that follows the context set name \"" +
            getOpenMPContextTraitSetName(TISet.Kind) + "\"")
               .str();
  }

  llvm::StringMap<SourceLocation> SeenSelectors;
  do {
    OMPTraitSelector TISelector;
    parseOMPContextSelector(TISelector, TISet.Kind, SeenSelectors);
    if (TISelector.Kind != TraitSelector::invalid &&
        !TISelector.Properties.empty())
      TISet.Selectors.push_back(TISelector);
  } while (TryConsumeToken(tok::comma));

  // Parse '}'.
  if (Tok.is(tok::r_brace)) {
    (void)ConsumeBrace();
  } else {
    Diag(Tok.getLocation(), diag::warn_omp_declare_variant_expected)
        << "}"
        << ("context selectors for the context set \"" +
            getOpenMPContextTraitSetName(TISet.Kind) + "\"")
               .str();
  }
}

/// Parse OpenMP context selectors:
///
/// <trait-set-selector> [, <trait-set-selector>]*
bool Parser::parseOMPContextSelectors(SourceLocation Loc, OMPTraitInfo &TI) {
  llvm::StringMap<SourceLocation> SeenSets;
  do {
    OMPTraitSet TISet;
    parseOMPContextSelectorSet(TISet, SeenSets);
    if (TISet.Kind != TraitSet::invalid && !TISet.Selectors.empty())
      TI.Sets.push_back(TISet);
  } while (TryConsumeToken(tok::comma));

  return false;
}

/// Parse clauses for '#pragma omp declare variant ( variant-func-id ) clause'.
void Parser::ParseOMPDeclareVariantClauses(Parser::DeclGroupPtrTy Ptr,
                                           CachedTokens &Toks,
                                           SourceLocation Loc) {
  PP.EnterToken(Tok, /*IsReinject*/ true);
  PP.EnterTokenStream(Toks, /*DisableMacroExpansion=*/true,
                      /*IsReinject*/ true);
  // Consume the previously pushed token.
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);
  ConsumeAnyToken(/*ConsumeCodeCompletionTok=*/true);

  FNContextRAII FnContext(*this, Ptr);
  // Parse function declaration id.
  SourceLocation RLoc;
  // Parse with IsAddressOfOperand set to true to parse methods as DeclRefExprs
  // instead of MemberExprs.
  ExprResult AssociatedFunction;
  {
    // Do not mark function as is used to prevent its emission if this is the
    // only place where it is used.
    EnterExpressionEvaluationContext Unevaluated(
        Actions, Sema::ExpressionEvaluationContext::Unevaluated);
    AssociatedFunction = ParseOpenMPParensExpr(
        getOpenMPDirectiveName(OMPD_declare_variant), RLoc,
        /*IsAddressOfOperand=*/true);
  }
  if (!AssociatedFunction.isUsable()) {
    if (!Tok.is(tok::annot_pragma_openmp_end))
      while (!SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch))
        ;
    // Skip the last annot_pragma_openmp_end.
    (void)ConsumeAnnotationToken();
    return;
  }

  OMPTraitInfo *ParentTI =
      Actions.OpenMP().getOMPTraitInfoForSurroundingScope();
  ASTContext &ASTCtx = Actions.getASTContext();
  OMPTraitInfo &TI = ASTCtx.getNewOMPTraitInfo();
  SmallVector<Expr *, 6> AdjustNothing;
  SmallVector<Expr *, 6> AdjustNeedDevicePtr;
  SmallVector<OMPInteropInfo, 3> AppendArgs;
  SourceLocation AdjustArgsLoc, AppendArgsLoc;

  // At least one clause is required.
  if (Tok.is(tok::annot_pragma_openmp_end)) {
    Diag(Tok.getLocation(), diag::err_omp_declare_variant_wrong_clause)
        << (getLangOpts().OpenMP < 51 ? 0 : 1);
  }

  bool IsError = false;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    OpenMPClauseKind CKind = Tok.isAnnotation()
                                 ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
    if (!isAllowedClauseForDirective(OMPD_declare_variant, CKind,
                                     getLangOpts().OpenMP)) {
      Diag(Tok.getLocation(), diag::err_omp_declare_variant_wrong_clause)
          << (getLangOpts().OpenMP < 51 ? 0 : 1);
      IsError = true;
    }
    if (!IsError) {
      switch (CKind) {
      case OMPC_match:
        IsError = parseOMPDeclareVariantMatchClause(Loc, TI, ParentTI);
        break;
      case OMPC_adjust_args: {
        AdjustArgsLoc = Tok.getLocation();
        ConsumeToken();
        SemaOpenMP::OpenMPVarListDataTy Data;
        SmallVector<Expr *> Vars;
        IsError = ParseOpenMPVarList(OMPD_declare_variant, OMPC_adjust_args,
                                     Vars, Data);
        if (!IsError)
          llvm::append_range(Data.ExtraModifier == OMPC_ADJUST_ARGS_nothing
                                 ? AdjustNothing
                                 : AdjustNeedDevicePtr,
                             Vars);
        break;
      }
      case OMPC_append_args:
        if (!AppendArgs.empty()) {
          Diag(AppendArgsLoc, diag::err_omp_more_one_clause)
              << getOpenMPDirectiveName(OMPD_declare_variant)
              << getOpenMPClauseName(CKind) << 0;
          IsError = true;
        }
        if (!IsError) {
          AppendArgsLoc = Tok.getLocation();
          ConsumeToken();
          IsError = parseOpenMPAppendArgs(AppendArgs);
        }
        break;
      default:
        llvm_unreachable("Unexpected clause for declare variant.");
      }
    }
    if (IsError) {
      while (!SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch))
        ;
      // Skip the last annot_pragma_openmp_end.
      (void)ConsumeAnnotationToken();
      return;
    }
    // Skip ',' if any.
    if (Tok.is(tok::comma))
      ConsumeToken();
  }

  std::optional<std::pair<FunctionDecl *, Expr *>> DeclVarData =
      Actions.OpenMP().checkOpenMPDeclareVariantFunction(
          Ptr, AssociatedFunction.get(), TI, AppendArgs.size(),
          SourceRange(Loc, Tok.getLocation()));

  if (DeclVarData && !TI.Sets.empty())
    Actions.OpenMP().ActOnOpenMPDeclareVariantDirective(
        DeclVarData->first, DeclVarData->second, TI, AdjustNothing,
        AdjustNeedDevicePtr, AppendArgs, AdjustArgsLoc, AppendArgsLoc,
        SourceRange(Loc, Tok.getLocation()));

  // Skip the last annot_pragma_openmp_end.
  (void)ConsumeAnnotationToken();
}

bool Parser::parseOpenMPAppendArgs(
    SmallVectorImpl<OMPInteropInfo> &InteropInfos) {
  bool HasError = false;
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(OMPC_append_args).data()))
    return true;

  // Parse the list of append-ops, each is;
  // interop(interop-type[,interop-type]...)
  while (Tok.is(tok::identifier) && Tok.getIdentifierInfo()->isStr("interop")) {
    ConsumeToken();
    BalancedDelimiterTracker IT(*this, tok::l_paren,
                                tok::annot_pragma_openmp_end);
    if (IT.expectAndConsume(diag::err_expected_lparen_after, "interop"))
      return true;

    OMPInteropInfo InteropInfo;
    if (ParseOMPInteropInfo(InteropInfo, OMPC_append_args))
      HasError = true;
    else
      InteropInfos.push_back(InteropInfo);

    IT.consumeClose();
    if (Tok.is(tok::comma))
      ConsumeToken();
  }
  if (!HasError && InteropInfos.empty()) {
    HasError = true;
    Diag(Tok.getLocation(), diag::err_omp_unexpected_append_op);
    SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
              StopBeforeMatch);
  }
  HasError = T.consumeClose() || HasError;
  return HasError;
}

bool Parser::parseOMPDeclareVariantMatchClause(SourceLocation Loc,
                                               OMPTraitInfo &TI,
                                               OMPTraitInfo *ParentTI) {
  // Parse 'match'.
  OpenMPClauseKind CKind = Tok.isAnnotation()
                               ? OMPC_unknown
                               : getOpenMPClauseKind(PP.getSpelling(Tok));
  if (CKind != OMPC_match) {
    Diag(Tok.getLocation(), diag::err_omp_declare_variant_wrong_clause)
        << (getLangOpts().OpenMP < 51 ? 0 : 1);
    return true;
  }
  (void)ConsumeToken();
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(OMPC_match).data()))
    return true;

  // Parse inner context selectors.
  parseOMPContextSelectors(Loc, TI);

  // Parse ')'
  (void)T.consumeClose();

  if (!ParentTI)
    return false;

  // Merge the parent/outer trait info into the one we just parsed and diagnose
  // problems.
  // TODO: Keep some source location in the TI to provide better diagnostics.
  // TODO: Perform some kind of equivalence check on the condition and score
  //       expressions.
  for (const OMPTraitSet &ParentSet : ParentTI->Sets) {
    bool MergedSet = false;
    for (OMPTraitSet &Set : TI.Sets) {
      if (Set.Kind != ParentSet.Kind)
        continue;
      MergedSet = true;
      for (const OMPTraitSelector &ParentSelector : ParentSet.Selectors) {
        bool MergedSelector = false;
        for (OMPTraitSelector &Selector : Set.Selectors) {
          if (Selector.Kind != ParentSelector.Kind)
            continue;
          MergedSelector = true;
          for (const OMPTraitProperty &ParentProperty :
               ParentSelector.Properties) {
            bool MergedProperty = false;
            for (OMPTraitProperty &Property : Selector.Properties) {
              // Ignore "equivalent" properties.
              if (Property.Kind != ParentProperty.Kind)
                continue;

              // If the kind is the same but the raw string not, we don't want
              // to skip out on the property.
              MergedProperty |= Property.RawString == ParentProperty.RawString;

              if (Property.RawString == ParentProperty.RawString &&
                  Selector.ScoreOrCondition == ParentSelector.ScoreOrCondition)
                continue;

              if (Selector.Kind == llvm::omp::TraitSelector::user_condition) {
                Diag(Loc, diag::err_omp_declare_variant_nested_user_condition);
              } else if (Selector.ScoreOrCondition !=
                         ParentSelector.ScoreOrCondition) {
                Diag(Loc, diag::err_omp_declare_variant_duplicate_nested_trait)
                    << getOpenMPContextTraitPropertyName(
                           ParentProperty.Kind, ParentProperty.RawString)
                    << getOpenMPContextTraitSelectorName(ParentSelector.Kind)
                    << getOpenMPContextTraitSetName(ParentSet.Kind);
              }
            }
            if (!MergedProperty)
              Selector.Properties.push_back(ParentProperty);
          }
        }
        if (!MergedSelector)
          Set.Selectors.push_back(ParentSelector);
      }
    }
    if (!MergedSet)
      TI.Sets.push_back(ParentSet);
  }

  return false;
}

/// <clause> [clause[ [,] clause] ... ]
///
///  clauses: for error directive
///     'at' '(' compilation | execution ')'
///     'severity' '(' fatal | warning ')'
///     'message' '(' msg-string ')'
/// ....
void Parser::ParseOpenMPClauses(OpenMPDirectiveKind DKind,
                                SmallVectorImpl<OMPClause *> &Clauses,
                                SourceLocation Loc) {
  std::bitset<llvm::omp::Clause_enumSize + 1> SeenClauses;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    OpenMPClauseKind CKind = Tok.isAnnotation()
                                 ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
    Actions.OpenMP().StartOpenMPClause(CKind);
    OMPClause *Clause =
        ParseOpenMPClause(DKind, CKind, !SeenClauses[unsigned(CKind)]);
    SkipUntil(tok::comma, tok::identifier, tok::annot_pragma_openmp_end,
              StopBeforeMatch);
    SeenClauses[unsigned(CKind)] = true;
    if (Clause != nullptr)
      Clauses.push_back(Clause);
    if (Tok.is(tok::annot_pragma_openmp_end)) {
      Actions.OpenMP().EndOpenMPClause();
      break;
    }
    // Skip ',' if any.
    if (Tok.is(tok::comma))
      ConsumeToken();
    Actions.OpenMP().EndOpenMPClause();
  }
}

/// `omp assumes` or `omp begin/end assumes` <clause> [[,]<clause>]...
/// where
///
///   clause:
///     'ext_IMPL_DEFINED'
///     'absent' '(' directive-name [, directive-name]* ')'
///     'contains' '(' directive-name [, directive-name]* ')'
///     'holds' '(' scalar-expression ')'
///     'no_openmp'
///     'no_openmp_routines'
///     'no_parallelism'
///
void Parser::ParseOpenMPAssumesDirective(OpenMPDirectiveKind DKind,
                                         SourceLocation Loc) {
  SmallVector<std::string, 4> Assumptions;
  bool SkippedClauses = false;

  auto SkipBraces = [&](llvm::StringRef Spelling, bool IssueNote) {
    BalancedDelimiterTracker T(*this, tok::l_paren,
                               tok::annot_pragma_openmp_end);
    if (T.expectAndConsume(diag::err_expected_lparen_after, Spelling.data()))
      return;
    T.skipToEnd();
    if (IssueNote && T.getCloseLocation().isValid())
      Diag(T.getCloseLocation(),
           diag::note_omp_assumption_clause_continue_here);
  };

  /// Helper to determine which AssumptionClauseMapping (ACM) in the
  /// AssumptionClauseMappings table matches \p RawString. The return value is
  /// the index of the matching ACM into the table or -1 if there was no match.
  auto MatchACMClause = [&](StringRef RawString) {
    llvm::StringSwitch<int> SS(RawString);
    unsigned ACMIdx = 0;
    for (const AssumptionClauseMappingInfo &ACMI : AssumptionClauseMappings) {
      if (ACMI.StartsWith)
        SS.StartsWith(ACMI.Identifier, ACMIdx++);
      else
        SS.Case(ACMI.Identifier, ACMIdx++);
    }
    return SS.Default(-1);
  };

  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    IdentifierInfo *II = nullptr;
    SourceLocation StartLoc = Tok.getLocation();
    int Idx = -1;
    if (Tok.isAnyIdentifier()) {
      II = Tok.getIdentifierInfo();
      Idx = MatchACMClause(II->getName());
    }
    ConsumeAnyToken();

    bool NextIsLPar = Tok.is(tok::l_paren);
    // Handle unknown clauses by skipping them.
    if (Idx == -1) {
      Diag(StartLoc, diag::warn_omp_unknown_assumption_clause_missing_id)
          << llvm::omp::getOpenMPDirectiveName(DKind)
          << llvm::omp::getAllAssumeClauseOptions() << NextIsLPar;
      if (NextIsLPar)
        SkipBraces(II ? II->getName() : "", /* IssueNote */ true);
      SkippedClauses = true;
      continue;
    }
    const AssumptionClauseMappingInfo &ACMI = AssumptionClauseMappings[Idx];
    if (ACMI.HasDirectiveList || ACMI.HasExpression) {
      // TODO: We ignore absent, contains, and holds assumptions for now. We
      //       also do not verify the content in the parenthesis at all.
      SkippedClauses = true;
      SkipBraces(II->getName(), /* IssueNote */ false);
      continue;
    }

    if (NextIsLPar) {
      Diag(Tok.getLocation(),
           diag::warn_omp_unknown_assumption_clause_without_args)
          << II;
      SkipBraces(II->getName(), /* IssueNote */ true);
    }

    assert(II && "Expected an identifier clause!");
    std::string Assumption = II->getName().str();
    if (ACMI.StartsWith)
      Assumption = "ompx_" + Assumption.substr(ACMI.Identifier.size());
    else
      Assumption = "omp_" + Assumption;
    Assumptions.push_back(Assumption);
  }

  Actions.OpenMP().ActOnOpenMPAssumesDirective(Loc, DKind, Assumptions,
                                               SkippedClauses);
}

void Parser::ParseOpenMPEndAssumesDirective(SourceLocation Loc) {
  if (Actions.OpenMP().isInOpenMPAssumeScope())
    Actions.OpenMP().ActOnOpenMPEndAssumesDirective();
  else
    Diag(Loc, diag::err_expected_begin_assumes);
}

/// Parsing of simple OpenMP clauses like 'default' or 'proc_bind'.
///
///    default-clause:
///         'default' '(' 'none' | 'shared'  | 'private' | 'firstprivate' ')
///
///    proc_bind-clause:
///         'proc_bind' '(' 'master' | 'close' | 'spread' ')
///
///    device_type-clause:
///         'device_type' '(' 'host' | 'nohost' | 'any' )'
namespace {
struct SimpleClauseData {
  unsigned Type;
  SourceLocation Loc;
  SourceLocation LOpen;
  SourceLocation TypeLoc;
  SourceLocation RLoc;
  SimpleClauseData(unsigned Type, SourceLocation Loc, SourceLocation LOpen,
                   SourceLocation TypeLoc, SourceLocation RLoc)
      : Type(Type), Loc(Loc), LOpen(LOpen), TypeLoc(TypeLoc), RLoc(RLoc) {}
};
} // anonymous namespace

static std::optional<SimpleClauseData>
parseOpenMPSimpleClause(Parser &P, OpenMPClauseKind Kind) {
  const Token &Tok = P.getCurToken();
  SourceLocation Loc = Tok.getLocation();
  SourceLocation LOpen = P.ConsumeToken();
  // Parse '('.
  BalancedDelimiterTracker T(P, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind).data()))
    return std::nullopt;

  unsigned Type = getOpenMPSimpleClauseType(
      Kind, Tok.isAnnotation() ? "" : P.getPreprocessor().getSpelling(Tok),
      P.getLangOpts());
  SourceLocation TypeLoc = Tok.getLocation();
  if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
      Tok.isNot(tok::annot_pragma_openmp_end))
    P.ConsumeAnyToken();

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  return SimpleClauseData(Type, Loc, LOpen, TypeLoc, RLoc);
}

void Parser::ParseOMPDeclareTargetClauses(
    SemaOpenMP::DeclareTargetContextInfo &DTCI) {
  SourceLocation DeviceTypeLoc;
  bool RequiresToOrLinkOrIndirectClause = false;
  bool HasToOrLinkOrIndirectClause = false;
  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    OMPDeclareTargetDeclAttr::MapTypeTy MT = OMPDeclareTargetDeclAttr::MT_To;
    bool HasIdentifier = Tok.is(tok::identifier);
    if (HasIdentifier) {
      // If we see any clause we need a to or link clause.
      RequiresToOrLinkOrIndirectClause = true;
      IdentifierInfo *II = Tok.getIdentifierInfo();
      StringRef ClauseName = II->getName();
      bool IsDeviceTypeClause =
          getLangOpts().OpenMP >= 50 &&
          getOpenMPClauseKind(ClauseName) == OMPC_device_type;

      bool IsIndirectClause = getLangOpts().OpenMP >= 51 &&
                              getOpenMPClauseKind(ClauseName) == OMPC_indirect;
      if (DTCI.Indirect && IsIndirectClause) {
        Diag(Tok, diag::err_omp_more_one_clause)
            << getOpenMPDirectiveName(OMPD_declare_target)
            << getOpenMPClauseName(OMPC_indirect) << 0;
        break;
      }
      bool IsToEnterOrLinkClause =
          OMPDeclareTargetDeclAttr::ConvertStrToMapTypeTy(ClauseName, MT);
      assert((!IsDeviceTypeClause || !IsToEnterOrLinkClause) &&
             "Cannot be both!");

      // Starting with OpenMP 5.2 the `to` clause has been replaced by the
      // `enter` clause.
      if (getLangOpts().OpenMP >= 52 && ClauseName == "to") {
        Diag(Tok, diag::err_omp_declare_target_unexpected_to_clause);
        break;
      }
      if (getLangOpts().OpenMP <= 51 && ClauseName == "enter") {
        Diag(Tok, diag::err_omp_declare_target_unexpected_enter_clause);
        break;
      }

      if (!IsDeviceTypeClause && !IsIndirectClause &&
          DTCI.Kind == OMPD_begin_declare_target) {
        Diag(Tok, diag::err_omp_declare_target_unexpected_clause)
            << ClauseName << (getLangOpts().OpenMP >= 51 ? 3 : 0);
        break;
      }
      if (!IsDeviceTypeClause && !IsToEnterOrLinkClause && !IsIndirectClause) {
        Diag(Tok, getLangOpts().OpenMP >= 52
                      ? diag::err_omp_declare_target_unexpected_clause_52
                      : diag::err_omp_declare_target_unexpected_clause)
            << ClauseName
            << (getLangOpts().OpenMP >= 51
                    ? 4
                    : getLangOpts().OpenMP >= 50 ? 2 : 1);
        break;
      }

      if (IsToEnterOrLinkClause || IsIndirectClause)
        HasToOrLinkOrIndirectClause = true;

      if (IsIndirectClause) {
        if (!ParseOpenMPIndirectClause(DTCI, /*ParseOnly*/ false))
          break;
        continue;
      }
      // Parse 'device_type' clause and go to next clause if any.
      if (IsDeviceTypeClause) {
        std::optional<SimpleClauseData> DevTypeData =
            parseOpenMPSimpleClause(*this, OMPC_device_type);
        if (DevTypeData) {
          if (DeviceTypeLoc.isValid()) {
            // We already saw another device_type clause, diagnose it.
            Diag(DevTypeData->Loc,
                 diag::warn_omp_more_one_device_type_clause);
            break;
          }
          switch (static_cast<OpenMPDeviceType>(DevTypeData->Type)) {
          case OMPC_DEVICE_TYPE_any:
            DTCI.DT = OMPDeclareTargetDeclAttr::DT_Any;
            break;
          case OMPC_DEVICE_TYPE_host:
            DTCI.DT = OMPDeclareTargetDeclAttr::DT_Host;
            break;
          case OMPC_DEVICE_TYPE_nohost:
            DTCI.DT = OMPDeclareTargetDeclAttr::DT_NoHost;
            break;
          case OMPC_DEVICE_TYPE_unknown:
            llvm_unreachable("Unexpected device_type");
          }
          DeviceTypeLoc = DevTypeData->Loc;
        }
        continue;
      }
      ConsumeToken();
    }

    if (DTCI.Kind == OMPD_declare_target || HasIdentifier) {
      auto &&Callback = [this, MT, &DTCI](CXXScopeSpec &SS,
                                          DeclarationNameInfo NameInfo) {
        NamedDecl *ND = Actions.OpenMP().lookupOpenMPDeclareTargetName(
            getCurScope(), SS, NameInfo);
        if (!ND)
          return;
        SemaOpenMP::DeclareTargetContextInfo::MapInfo MI{MT, NameInfo.getLoc()};
        bool FirstMapping = DTCI.ExplicitlyMapped.try_emplace(ND, MI).second;
        if (!FirstMapping)
          Diag(NameInfo.getLoc(), diag::err_omp_declare_target_multiple)
              << NameInfo.getName();
      };
      if (ParseOpenMPSimpleVarList(OMPD_declare_target, Callback,
                                   /*AllowScopeSpecifier=*/true))
        break;
    }

    if (Tok.is(tok::l_paren)) {
      Diag(Tok,
           diag::err_omp_begin_declare_target_unexpected_implicit_to_clause);
      break;
    }
    if (!HasIdentifier && Tok.isNot(tok::annot_pragma_openmp_end)) {
      Diag(Tok,
           getLangOpts().OpenMP >= 52
               ? diag::err_omp_declare_target_wrong_clause_after_implicit_enter
               : diag::err_omp_declare_target_wrong_clause_after_implicit_to);
      break;
    }

    // Consume optional ','.
    if (Tok.is(tok::comma))
      ConsumeToken();
  }

  if (DTCI.Indirect && DTCI.DT != OMPDeclareTargetDeclAttr::DT_Any)
    Diag(DeviceTypeLoc, diag::err_omp_declare_target_indirect_device_type);

  // For declare target require at least 'to' or 'link' to be present.
  if (DTCI.Kind == OMPD_declare_target && RequiresToOrLinkOrIndirectClause &&
      !HasToOrLinkOrIndirectClause)
    Diag(DTCI.Loc,
         getLangOpts().OpenMP >= 52
             ? diag::err_omp_declare_target_missing_enter_or_link_clause
             : diag::err_omp_declare_target_missing_to_or_link_clause)
        << (getLangOpts().OpenMP >= 51 ? 1 : 0);

  SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
}

void Parser::skipUntilPragmaOpenMPEnd(OpenMPDirectiveKind DKind) {
  // The last seen token is annot_pragma_openmp_end - need to check for
  // extra tokens.
  if (Tok.is(tok::annot_pragma_openmp_end))
    return;

  Diag(Tok, diag::warn_omp_extra_tokens_at_eol)
      << getOpenMPDirectiveName(DKind);
  while (Tok.isNot(tok::annot_pragma_openmp_end))
    ConsumeAnyToken();
}

void Parser::parseOMPEndDirective(OpenMPDirectiveKind BeginKind,
                                  OpenMPDirectiveKind ExpectedKind,
                                  OpenMPDirectiveKind FoundKind,
                                  SourceLocation BeginLoc,
                                  SourceLocation FoundLoc,
                                  bool SkipUntilOpenMPEnd) {
  int DiagSelection = ExpectedKind == OMPD_end_declare_target ? 0 : 1;

  if (FoundKind == ExpectedKind) {
    ConsumeAnyToken();
    skipUntilPragmaOpenMPEnd(ExpectedKind);
    return;
  }

  Diag(FoundLoc, diag::err_expected_end_declare_target_or_variant)
      << DiagSelection;
  Diag(BeginLoc, diag::note_matching)
      << ("'#pragma omp " + getOpenMPDirectiveName(BeginKind) + "'").str();
  if (SkipUntilOpenMPEnd)
    SkipUntil(tok::annot_pragma_openmp_end, StopBeforeMatch);
}

void Parser::ParseOMPEndDeclareTargetDirective(OpenMPDirectiveKind BeginDKind,
                                               OpenMPDirectiveKind EndDKind,
                                               SourceLocation DKLoc) {
  parseOMPEndDirective(BeginDKind, OMPD_end_declare_target, EndDKind, DKLoc,
                       Tok.getLocation(),
                       /* SkipUntilOpenMPEnd */ false);
  // Skip the last annot_pragma_openmp_end.
  if (Tok.is(tok::annot_pragma_openmp_end))
    ConsumeAnnotationToken();
}

/// Parsing of declarative OpenMP directives.
///
///       threadprivate-directive:
///         annot_pragma_openmp 'threadprivate' simple-variable-list
///         annot_pragma_openmp_end
///
///       allocate-directive:
///         annot_pragma_openmp 'allocate' simple-variable-list [<clause>]
///         annot_pragma_openmp_end
///
///       declare-reduction-directive:
///        annot_pragma_openmp 'declare' 'reduction' [...]
///        annot_pragma_openmp_end
///
///       declare-mapper-directive:
///         annot_pragma_openmp 'declare' 'mapper' '(' [<mapper-identifer> ':']
///         <type> <var> ')' [<clause>[[,] <clause>] ... ]
///         annot_pragma_openmp_end
///
///       declare-simd-directive:
///         annot_pragma_openmp 'declare simd' {<clause> [,]}
///         annot_pragma_openmp_end
///         <function declaration/definition>
///
///       requires directive:
///         annot_pragma_openmp 'requires' <clause> [[[,] <clause>] ... ]
///         annot_pragma_openmp_end
///
///       assumes directive:
///         annot_pragma_openmp 'assumes' <clause> [[[,] <clause>] ... ]
///         annot_pragma_openmp_end
///       or
///         annot_pragma_openmp 'begin assumes' <clause> [[[,] <clause>] ... ]
///         annot_pragma_openmp 'end assumes'
///         annot_pragma_openmp_end
///
Parser::DeclGroupPtrTy Parser::ParseOpenMPDeclarativeDirectiveWithExtDecl(
    AccessSpecifier &AS, ParsedAttributes &Attrs, bool Delayed,
    DeclSpec::TST TagType, Decl *Tag) {
  assert(Tok.isOneOf(tok::annot_pragma_openmp, tok::annot_attr_openmp) &&
         "Not an OpenMP directive!");
  ParsingOpenMPDirectiveRAII DirScope(*this);
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  SourceLocation Loc;
  OpenMPDirectiveKind DKind;
  if (Delayed) {
    TentativeParsingAction TPA(*this);
    Loc = ConsumeAnnotationToken();
    DKind = parseOpenMPDirectiveKind(*this);
    if (DKind == OMPD_declare_reduction || DKind == OMPD_declare_mapper) {
      // Need to delay parsing until completion of the parent class.
      TPA.Revert();
      CachedTokens Toks;
      unsigned Cnt = 1;
      Toks.push_back(Tok);
      while (Cnt && Tok.isNot(tok::eof)) {
        (void)ConsumeAnyToken();
        if (Tok.isOneOf(tok::annot_pragma_openmp, tok::annot_attr_openmp))
          ++Cnt;
        else if (Tok.is(tok::annot_pragma_openmp_end))
          --Cnt;
        Toks.push_back(Tok);
      }
      // Skip last annot_pragma_openmp_end.
      if (Cnt == 0)
        (void)ConsumeAnyToken();
      auto *LP = new LateParsedPragma(this, AS);
      LP->takeToks(Toks);
      getCurrentClass().LateParsedDeclarations.push_back(LP);
      return nullptr;
    }
    TPA.Commit();
  } else {
    Loc = ConsumeAnnotationToken();
    DKind = parseOpenMPDirectiveKind(*this);
  }

  switch (DKind) {
  case OMPD_threadprivate: {
    ConsumeToken();
    DeclDirectiveListParserHelper Helper(this, DKind);
    if (!ParseOpenMPSimpleVarList(DKind, Helper,
                                  /*AllowScopeSpecifier=*/true)) {
      skipUntilPragmaOpenMPEnd(DKind);
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Actions.OpenMP().ActOnOpenMPThreadprivateDirective(
          Loc, Helper.getIdentifiers());
    }
    break;
  }
  case OMPD_allocate: {
    ConsumeToken();
    DeclDirectiveListParserHelper Helper(this, DKind);
    if (!ParseOpenMPSimpleVarList(DKind, Helper,
                                  /*AllowScopeSpecifier=*/true)) {
      SmallVector<OMPClause *, 1> Clauses;
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        std::bitset<llvm::omp::Clause_enumSize + 1> SeenClauses;
        while (Tok.isNot(tok::annot_pragma_openmp_end)) {
          OpenMPClauseKind CKind =
              Tok.isAnnotation() ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
          Actions.OpenMP().StartOpenMPClause(CKind);
          OMPClause *Clause = ParseOpenMPClause(OMPD_allocate, CKind,
                                                !SeenClauses[unsigned(CKind)]);
          SkipUntil(tok::comma, tok::identifier, tok::annot_pragma_openmp_end,
                    StopBeforeMatch);
          SeenClauses[unsigned(CKind)] = true;
          if (Clause != nullptr)
            Clauses.push_back(Clause);
          if (Tok.is(tok::annot_pragma_openmp_end)) {
            Actions.OpenMP().EndOpenMPClause();
            break;
          }
          // Skip ',' if any.
          if (Tok.is(tok::comma))
            ConsumeToken();
          Actions.OpenMP().EndOpenMPClause();
        }
        skipUntilPragmaOpenMPEnd(DKind);
      }
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Actions.OpenMP().ActOnOpenMPAllocateDirective(
          Loc, Helper.getIdentifiers(), Clauses);
    }
    break;
  }
  case OMPD_requires: {
    SourceLocation StartLoc = ConsumeToken();
    SmallVector<OMPClause *, 5> Clauses;
    llvm::SmallBitVector SeenClauses(llvm::omp::Clause_enumSize + 1);
    if (Tok.is(tok::annot_pragma_openmp_end)) {
      Diag(Tok, diag::err_omp_expected_clause)
          << getOpenMPDirectiveName(OMPD_requires);
      break;
    }
    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      OpenMPClauseKind CKind = Tok.isAnnotation()
                                   ? OMPC_unknown
                                   : getOpenMPClauseKind(PP.getSpelling(Tok));
      Actions.OpenMP().StartOpenMPClause(CKind);
      OMPClause *Clause = ParseOpenMPClause(OMPD_requires, CKind,
                                            !SeenClauses[unsigned(CKind)]);
      SkipUntil(tok::comma, tok::identifier, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
      SeenClauses[unsigned(CKind)] = true;
      if (Clause != nullptr)
        Clauses.push_back(Clause);
      if (Tok.is(tok::annot_pragma_openmp_end)) {
        Actions.OpenMP().EndOpenMPClause();
        break;
      }
      // Skip ',' if any.
      if (Tok.is(tok::comma))
        ConsumeToken();
      Actions.OpenMP().EndOpenMPClause();
    }
    // Consume final annot_pragma_openmp_end
    if (Clauses.empty()) {
      Diag(Tok, diag::err_omp_expected_clause)
          << getOpenMPDirectiveName(OMPD_requires);
      ConsumeAnnotationToken();
      return nullptr;
    }
    ConsumeAnnotationToken();
    return Actions.OpenMP().ActOnOpenMPRequiresDirective(StartLoc, Clauses);
  }
  case OMPD_error: {
    SmallVector<OMPClause *, 1> Clauses;
    SourceLocation StartLoc = ConsumeToken();
    ParseOpenMPClauses(DKind, Clauses, StartLoc);
    Actions.OpenMP().ActOnOpenMPErrorDirective(Clauses, StartLoc,
                                               SourceLocation(),
                                               /*InExContext = */ false);
    break;
  }
  case OMPD_assumes:
  case OMPD_begin_assumes:
    ParseOpenMPAssumesDirective(DKind, ConsumeToken());
    break;
  case OMPD_end_assumes:
    ParseOpenMPEndAssumesDirective(ConsumeToken());
    break;
  case OMPD_declare_reduction:
    ConsumeToken();
    if (DeclGroupPtrTy Res = ParseOpenMPDeclareReductionDirective(AS)) {
      skipUntilPragmaOpenMPEnd(OMPD_declare_reduction);
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Res;
    }
    break;
  case OMPD_declare_mapper: {
    ConsumeToken();
    if (DeclGroupPtrTy Res = ParseOpenMPDeclareMapperDirective(AS)) {
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      return Res;
    }
    break;
  }
  case OMPD_begin_declare_variant: {
    // The syntax is:
    // { #pragma omp begin declare variant clause }
    // <function-declaration-or-definition-sequence>
    // { #pragma omp end declare variant }
    //
    ConsumeToken();
    OMPTraitInfo *ParentTI =
        Actions.OpenMP().getOMPTraitInfoForSurroundingScope();
    ASTContext &ASTCtx = Actions.getASTContext();
    OMPTraitInfo &TI = ASTCtx.getNewOMPTraitInfo();
    if (parseOMPDeclareVariantMatchClause(Loc, TI, ParentTI)) {
      while (!SkipUntil(tok::annot_pragma_openmp_end, Parser::StopBeforeMatch))
        ;
      // Skip the last annot_pragma_openmp_end.
      (void)ConsumeAnnotationToken();
      break;
    }

    // Skip last tokens.
    skipUntilPragmaOpenMPEnd(OMPD_begin_declare_variant);

    ParsingOpenMPDirectiveRAII NormalScope(*this, /*Value=*/false);

    VariantMatchInfo VMI;
    TI.getAsVariantMatchInfo(ASTCtx, VMI);

    std::function<void(StringRef)> DiagUnknownTrait =
        [this, Loc](StringRef ISATrait) {
          // TODO Track the selector locations in a way that is accessible here
          // to improve the diagnostic location.
          Diag(Loc, diag::warn_unknown_declare_variant_isa_trait) << ISATrait;
        };
    TargetOMPContext OMPCtx(
        ASTCtx, std::move(DiagUnknownTrait),
        /* CurrentFunctionDecl */ nullptr,
        /* ConstructTraits */ ArrayRef<llvm::omp::TraitProperty>());

    if (isVariantApplicableInContext(VMI, OMPCtx, /* DeviceSetOnly */ true)) {
      Actions.OpenMP().ActOnOpenMPBeginDeclareVariant(Loc, TI);
      break;
    }

    // Elide all the code till the matching end declare variant was found.
    unsigned Nesting = 1;
    SourceLocation DKLoc;
    OpenMPDirectiveKind DK = OMPD_unknown;
    do {
      DKLoc = Tok.getLocation();
      DK = parseOpenMPDirectiveKind(*this);
      if (DK == OMPD_end_declare_variant)
        --Nesting;
      else if (DK == OMPD_begin_declare_variant)
        ++Nesting;
      if (!Nesting || isEofOrEom())
        break;
      ConsumeAnyToken();
    } while (true);

    parseOMPEndDirective(OMPD_begin_declare_variant, OMPD_end_declare_variant,
                         DK, Loc, DKLoc, /* SkipUntilOpenMPEnd */ true);
    if (isEofOrEom())
      return nullptr;
    break;
  }
  case OMPD_end_declare_variant: {
    if (Actions.OpenMP().isInOpenMPDeclareVariantScope())
      Actions.OpenMP().ActOnOpenMPEndDeclareVariant();
    else
      Diag(Loc, diag::err_expected_begin_declare_variant);
    ConsumeToken();
    break;
  }
  case OMPD_declare_variant:
  case OMPD_declare_simd: {
    // The syntax is:
    // { #pragma omp declare {simd|variant} }
    // <function-declaration-or-definition>
    //
    CachedTokens Toks;
    Toks.push_back(Tok);
    ConsumeToken();
    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      Toks.push_back(Tok);
      ConsumeAnyToken();
    }
    Toks.push_back(Tok);
    ConsumeAnyToken();

    DeclGroupPtrTy Ptr;
    if (Tok.isOneOf(tok::annot_pragma_openmp, tok::annot_attr_openmp)) {
      Ptr = ParseOpenMPDeclarativeDirectiveWithExtDecl(AS, Attrs, Delayed,
                                                       TagType, Tag);
    } else if (Tok.isNot(tok::r_brace) && !isEofOrEom()) {
      // Here we expect to see some function declaration.
      if (AS == AS_none) {
        assert(TagType == DeclSpec::TST_unspecified);
        ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
        MaybeParseCXX11Attributes(Attrs);
        ParsingDeclSpec PDS(*this);
        Ptr = ParseExternalDeclaration(Attrs, EmptyDeclSpecAttrs, &PDS);
      } else {
        Ptr =
            ParseCXXClassMemberDeclarationWithPragmas(AS, Attrs, TagType, Tag);
      }
    }
    if (!Ptr) {
      Diag(Loc, diag::err_omp_decl_in_declare_simd_variant)
          << (DKind == OMPD_declare_simd ? 0 : 1);
      return DeclGroupPtrTy();
    }
    if (DKind == OMPD_declare_simd)
      return ParseOMPDeclareSimdClauses(Ptr, Toks, Loc);
    assert(DKind == OMPD_declare_variant &&
           "Expected declare variant directive only");
    ParseOMPDeclareVariantClauses(Ptr, Toks, Loc);
    return Ptr;
  }
  case OMPD_begin_declare_target:
  case OMPD_declare_target: {
    SourceLocation DTLoc = ConsumeAnyToken();
    bool HasClauses = Tok.isNot(tok::annot_pragma_openmp_end);
    SemaOpenMP::DeclareTargetContextInfo DTCI(DKind, DTLoc);
    if (HasClauses)
      ParseOMPDeclareTargetClauses(DTCI);
    bool HasImplicitMappings = DKind == OMPD_begin_declare_target ||
                               !HasClauses ||
                               (DTCI.ExplicitlyMapped.empty() && DTCI.Indirect);

    // Skip the last annot_pragma_openmp_end.
    ConsumeAnyToken();

    if (HasImplicitMappings) {
      Actions.OpenMP().ActOnStartOpenMPDeclareTargetContext(DTCI);
      return nullptr;
    }

    Actions.OpenMP().ActOnFinishedOpenMPDeclareTargetContext(DTCI);
    llvm::SmallVector<Decl *, 4> Decls;
    for (auto &It : DTCI.ExplicitlyMapped)
      Decls.push_back(It.first);
    return Actions.BuildDeclaratorGroup(Decls);
  }
  case OMPD_end_declare_target: {
    if (!Actions.OpenMP().isInOpenMPDeclareTargetContext()) {
      Diag(Tok, diag::err_omp_unexpected_directive)
          << 1 << getOpenMPDirectiveName(DKind);
      break;
    }
    const SemaOpenMP::DeclareTargetContextInfo &DTCI =
        Actions.OpenMP().ActOnOpenMPEndDeclareTargetDirective();
    ParseOMPEndDeclareTargetDirective(DTCI.Kind, DKind, DTCI.Loc);
    return nullptr;
  }
  case OMPD_unknown:
    Diag(Tok, diag::err_omp_unknown_directive);
    break;
  default:
    switch (getDirectiveCategory(DKind)) {
    case Category::Executable:
    case Category::Meta:
    case Category::Subsidiary:
    case Category::Utility:
      Diag(Tok, diag::err_omp_unexpected_directive)
          << 1 << getOpenMPDirectiveName(DKind);
      break;
    case Category::Declarative:
    case Category::Informational:
      break;
    }
  }
  while (Tok.isNot(tok::annot_pragma_openmp_end))
    ConsumeAnyToken();
  ConsumeAnyToken();
  return nullptr;
}

StmtResult Parser::ParseOpenMPExecutableDirective(
    ParsedStmtContext StmtCtx, OpenMPDirectiveKind DKind, SourceLocation Loc,
    bool ReadDirectiveWithinMetadirective) {
  assert(isOpenMPExecutableDirective(DKind) && "Unexpected directive category");

  bool HasAssociatedStatement = true;
  Association Assoc = getDirectiveAssociation(DKind);

  // OMPD_ordered has None as association, but it comes in two variants,
  // the second of which is associated with a block.
  // OMPD_scan and OMPD_section are both "separating", but section is treated
  // as if it was associated with a statement, while scan is not.
  if (DKind != OMPD_ordered && DKind != OMPD_section &&
      (Assoc == Association::None || Assoc == Association::Separating)) {
    if ((StmtCtx & ParsedStmtContext::AllowStandaloneOpenMPDirectives) ==
        ParsedStmtContext()) {
      Diag(Tok, diag::err_omp_immediate_directive)
          << getOpenMPDirectiveName(DKind) << 0;
      if (DKind == OMPD_error) {
        SkipUntil(tok::annot_pragma_openmp_end);
        return StmtError();
      }
    }
    HasAssociatedStatement = false;
  }

  SourceLocation EndLoc;
  SmallVector<OMPClause *, 5> Clauses;
  llvm::SmallBitVector SeenClauses(llvm::omp::Clause_enumSize + 1);
  DeclarationNameInfo DirName;
  OpenMPDirectiveKind CancelRegion = OMPD_unknown;
  unsigned ScopeFlags = Scope::FnScope | Scope::DeclScope |
                        Scope::CompoundStmtScope | Scope::OpenMPDirectiveScope;

  // Special processing for flush and depobj clauses.
  Token ImplicitTok;
  bool ImplicitClauseAllowed = false;
  if (DKind == OMPD_flush || DKind == OMPD_depobj) {
    ImplicitTok = Tok;
    ImplicitClauseAllowed = true;
  }
  ConsumeToken();
  // Parse directive name of the 'critical' directive if any.
  if (DKind == OMPD_critical) {
    BalancedDelimiterTracker T(*this, tok::l_paren,
                               tok::annot_pragma_openmp_end);
    if (!T.consumeOpen()) {
      if (Tok.isAnyIdentifier()) {
        DirName =
            DeclarationNameInfo(Tok.getIdentifierInfo(), Tok.getLocation());
        ConsumeAnyToken();
      } else {
        Diag(Tok, diag::err_omp_expected_identifier_for_critical);
      }
      T.consumeClose();
    }
  } else if (DKind == OMPD_cancellation_point || DKind == OMPD_cancel) {
    CancelRegion = parseOpenMPDirectiveKind(*this);
    if (Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeToken();
  }

  if (isOpenMPLoopDirective(DKind))
    ScopeFlags |= Scope::OpenMPLoopDirectiveScope;
  if (isOpenMPSimdDirective(DKind))
    ScopeFlags |= Scope::OpenMPSimdDirectiveScope;
  ParseScope OMPDirectiveScope(this, ScopeFlags);
  Actions.OpenMP().StartOpenMPDSABlock(DKind, DirName, Actions.getCurScope(),
                                       Loc);

  while (Tok.isNot(tok::annot_pragma_openmp_end)) {
    // If we are parsing for a directive within a metadirective, the directive
    // ends with a ')'.
    if (ReadDirectiveWithinMetadirective && Tok.is(tok::r_paren)) {
      while (Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
      break;
    }
    bool HasImplicitClause = false;
    if (ImplicitClauseAllowed && Tok.is(tok::l_paren)) {
      HasImplicitClause = true;
      // Push copy of the current token back to stream to properly parse
      // pseudo-clause OMPFlushClause or OMPDepobjClause.
      PP.EnterToken(Tok, /*IsReinject*/ true);
      PP.EnterToken(ImplicitTok, /*IsReinject*/ true);
      ConsumeAnyToken();
    }
    OpenMPClauseKind CKind = Tok.isAnnotation()
                                 ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
    if (HasImplicitClause) {
      assert(CKind == OMPC_unknown && "Must be unknown implicit clause.");
      if (DKind == OMPD_flush) {
        CKind = OMPC_flush;
      } else {
        assert(DKind == OMPD_depobj && "Expected flush or depobj directives.");
        CKind = OMPC_depobj;
      }
    }
    // No more implicit clauses allowed.
    ImplicitClauseAllowed = false;
    Actions.OpenMP().StartOpenMPClause(CKind);
    HasImplicitClause = false;
    OMPClause *Clause =
        ParseOpenMPClause(DKind, CKind, !SeenClauses[unsigned(CKind)]);
    SeenClauses[unsigned(CKind)] = true;
    if (Clause)
      Clauses.push_back(Clause);

    // Skip ',' if any.
    if (Tok.is(tok::comma))
      ConsumeToken();
    Actions.OpenMP().EndOpenMPClause();
  }
  // End location of the directive.
  EndLoc = Tok.getLocation();
  // Consume final annot_pragma_openmp_end.
  ConsumeAnnotationToken();

  if (DKind == OMPD_ordered) {
    // If the depend or doacross clause is specified, the ordered construct
    // is a stand-alone directive.
    for (auto CK : {OMPC_depend, OMPC_doacross}) {
      if (SeenClauses[unsigned(CK)]) {
        if ((StmtCtx & ParsedStmtContext::AllowStandaloneOpenMPDirectives) ==
            ParsedStmtContext()) {
          Diag(Loc, diag::err_omp_immediate_directive)
              << getOpenMPDirectiveName(DKind) << 1 << getOpenMPClauseName(CK);
        }
        HasAssociatedStatement = false;
      }
    }
  }

  if (DKind == OMPD_tile && !SeenClauses[unsigned(OMPC_sizes)]) {
    Diag(Loc, diag::err_omp_required_clause)
        << getOpenMPDirectiveName(OMPD_tile) << "sizes";
  }

  StmtResult AssociatedStmt;
  if (HasAssociatedStatement) {
    // The body is a block scope like in Lambdas and Blocks.
    Actions.OpenMP().ActOnOpenMPRegionStart(DKind, getCurScope());
    // FIXME: We create a bogus CompoundStmt scope to hold the contents of
    // the captured region. Code elsewhere assumes that any FunctionScopeInfo
    // should have at least one compound statement scope within it.
    ParsingOpenMPDirectiveRAII NormalScope(*this, /*Value=*/false);
    {
      Sema::CompoundScopeRAII Scope(Actions);
      AssociatedStmt = ParseStatement();

      if (AssociatedStmt.isUsable() && isOpenMPLoopDirective(DKind) &&
          getLangOpts().OpenMPIRBuilder)
        AssociatedStmt =
            Actions.OpenMP().ActOnOpenMPLoopnest(AssociatedStmt.get());
    }
    AssociatedStmt =
        Actions.OpenMP().ActOnOpenMPRegionEnd(AssociatedStmt, Clauses);
  } else if (DKind == OMPD_target_update || DKind == OMPD_target_enter_data ||
             DKind == OMPD_target_exit_data) {
    Actions.OpenMP().ActOnOpenMPRegionStart(DKind, getCurScope());
    AssociatedStmt = (Sema::CompoundScopeRAII(Actions),
                      Actions.ActOnCompoundStmt(Loc, Loc, std::nullopt,
                                                /*isStmtExpr=*/false));
    AssociatedStmt =
        Actions.OpenMP().ActOnOpenMPRegionEnd(AssociatedStmt, Clauses);
  }

  StmtResult Directive = Actions.OpenMP().ActOnOpenMPExecutableDirective(
      DKind, DirName, CancelRegion, Clauses, AssociatedStmt.get(), Loc, EndLoc);

  // Exit scope.
  Actions.OpenMP().EndOpenMPDSABlock(Directive.get());
  OMPDirectiveScope.Exit();

  return Directive;
}

/// Parsing of declarative or executable OpenMP directives.
///
///       threadprivate-directive:
///         annot_pragma_openmp 'threadprivate' simple-variable-list
///         annot_pragma_openmp_end
///
///       allocate-directive:
///         annot_pragma_openmp 'allocate' simple-variable-list
///         annot_pragma_openmp_end
///
///       declare-reduction-directive:
///         annot_pragma_openmp 'declare' 'reduction' '(' <reduction_id> ':'
///         <type> {',' <type>} ':' <expression> ')' ['initializer' '('
///         ('omp_priv' '=' <expression>|<function_call>) ')']
///         annot_pragma_openmp_end
///
///       declare-mapper-directive:
///         annot_pragma_openmp 'declare' 'mapper' '(' [<mapper-identifer> ':']
///         <type> <var> ')' [<clause>[[,] <clause>] ... ]
///         annot_pragma_openmp_end
///
///       executable-directive:
///         annot_pragma_openmp 'parallel' | 'simd' | 'for' | 'sections' |
///         'section' | 'single' | 'master' | 'critical' [ '(' <name> ')' ] |
///         'parallel for' | 'parallel sections' | 'parallel master' | 'task' |
///         'taskyield' | 'barrier' | 'taskwait' | 'flush' | 'ordered' | 'error'
///         | 'atomic' | 'for simd' | 'parallel for simd' | 'target' | 'target
///         data' | 'taskgroup' | 'teams' | 'taskloop' | 'taskloop simd' |
///         'master taskloop' | 'master taskloop simd' | 'parallel master
///         taskloop' | 'parallel master taskloop simd' | 'distribute' | 'target
///         enter data' | 'target exit data' | 'target parallel' | 'target
///         parallel for' | 'target update' | 'distribute parallel for' |
///         'distribute paralle for simd' | 'distribute simd' | 'target parallel
///         for simd' | 'target simd' | 'teams distribute' | 'teams distribute
///         simd' | 'teams distribute parallel for simd' | 'teams distribute
///         parallel for' | 'target teams' | 'target teams distribute' | 'target
///         teams distribute parallel for' | 'target teams distribute parallel
///         for simd' | 'target teams distribute simd' | 'masked' |
///         'parallel masked' {clause} annot_pragma_openmp_end
///
StmtResult Parser::ParseOpenMPDeclarativeOrExecutableDirective(
    ParsedStmtContext StmtCtx, bool ReadDirectiveWithinMetadirective) {
  if (!ReadDirectiveWithinMetadirective)
    assert(Tok.isOneOf(tok::annot_pragma_openmp, tok::annot_attr_openmp) &&
           "Not an OpenMP directive!");
  ParsingOpenMPDirectiveRAII DirScope(*this);
  ParenBraceBracketBalancer BalancerRAIIObj(*this);
  SourceLocation Loc = ReadDirectiveWithinMetadirective
                           ? Tok.getLocation()
                           : ConsumeAnnotationToken();
  OpenMPDirectiveKind DKind = parseOpenMPDirectiveKind(*this);
  if (ReadDirectiveWithinMetadirective && DKind == OMPD_unknown) {
    Diag(Tok, diag::err_omp_unknown_directive);
    return StmtError();
  }

  StmtResult Directive = StmtError();

  bool IsExecutable = [&]() {
    if (DKind == OMPD_error) // OMPD_error is handled as executable
      return true;
    auto Res = getDirectiveCategory(DKind);
    return Res == Category::Executable || Res == Category::Subsidiary;
  }();

  if (IsExecutable) {
    Directive = ParseOpenMPExecutableDirective(
        StmtCtx, DKind, Loc, ReadDirectiveWithinMetadirective);
    assert(!Directive.isUnset() && "Executable directive remained unprocessed");
    return Directive;
  }

  switch (DKind) {
  case OMPD_nothing:
    ConsumeToken();
    // If we are parsing the directive within a metadirective, the directive
    // ends with a ')'.
    if (ReadDirectiveWithinMetadirective && Tok.is(tok::r_paren))
      while (Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
    else
      skipUntilPragmaOpenMPEnd(DKind);
    if (Tok.is(tok::annot_pragma_openmp_end))
      ConsumeAnnotationToken();
    // return an empty statement
    return StmtEmpty();
  case OMPD_metadirective: {
    ConsumeToken();
    SmallVector<VariantMatchInfo, 4> VMIs;

    // First iteration of parsing all clauses of metadirective.
    // This iteration only parses and collects all context selector ignoring the
    // associated directives.
    TentativeParsingAction TPA(*this);
    ASTContext &ASTContext = Actions.getASTContext();

    BalancedDelimiterTracker T(*this, tok::l_paren,
                               tok::annot_pragma_openmp_end);
    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      OpenMPClauseKind CKind = Tok.isAnnotation()
                                   ? OMPC_unknown
                                   : getOpenMPClauseKind(PP.getSpelling(Tok));
      SourceLocation Loc = ConsumeToken();

      // Parse '('.
      if (T.expectAndConsume(diag::err_expected_lparen_after,
                             getOpenMPClauseName(CKind).data()))
        return Directive;

      OMPTraitInfo &TI = Actions.getASTContext().getNewOMPTraitInfo();
      if (CKind == OMPC_when) {
        // parse and get OMPTraitInfo to pass to the When clause
        parseOMPContextSelectors(Loc, TI);
        if (TI.Sets.size() == 0) {
          Diag(Tok, diag::err_omp_expected_context_selector) << "when clause";
          TPA.Commit();
          return Directive;
        }

        // Parse ':'
        if (Tok.is(tok::colon))
          ConsumeAnyToken();
        else {
          Diag(Tok, diag::err_omp_expected_colon) << "when clause";
          TPA.Commit();
          return Directive;
        }
      }
      // Skip Directive for now. We will parse directive in the second iteration
      int paren = 0;
      while (Tok.isNot(tok::r_paren) || paren != 0) {
        if (Tok.is(tok::l_paren))
          paren++;
        if (Tok.is(tok::r_paren))
          paren--;
        if (Tok.is(tok::annot_pragma_openmp_end)) {
          Diag(Tok, diag::err_omp_expected_punc)
              << getOpenMPClauseName(CKind) << 0;
          TPA.Commit();
          return Directive;
        }
        ConsumeAnyToken();
      }
      // Parse ')'
      if (Tok.is(tok::r_paren))
        T.consumeClose();

      VariantMatchInfo VMI;
      TI.getAsVariantMatchInfo(ASTContext, VMI);

      VMIs.push_back(VMI);
    }

    TPA.Revert();
    // End of the first iteration. Parser is reset to the start of metadirective

    std::function<void(StringRef)> DiagUnknownTrait =
        [this, Loc](StringRef ISATrait) {
          // TODO Track the selector locations in a way that is accessible here
          // to improve the diagnostic location.
          Diag(Loc, diag::warn_unknown_declare_variant_isa_trait) << ISATrait;
        };
    TargetOMPContext OMPCtx(ASTContext, std::move(DiagUnknownTrait),
                            /* CurrentFunctionDecl */ nullptr,
                            ArrayRef<llvm::omp::TraitProperty>());

    // A single match is returned for OpenMP 5.0
    int BestIdx = getBestVariantMatchForContext(VMIs, OMPCtx);

    int Idx = 0;
    // In OpenMP 5.0 metadirective is either replaced by another directive or
    // ignored.
    // TODO: In OpenMP 5.1 generate multiple directives based upon the matches
    // found by getBestWhenMatchForContext.
    while (Tok.isNot(tok::annot_pragma_openmp_end)) {
      // OpenMP 5.0 implementation - Skip to the best index found.
      if (Idx++ != BestIdx) {
        ConsumeToken();  // Consume clause name
        T.consumeOpen(); // Consume '('
        int paren = 0;
        // Skip everything inside the clause
        while (Tok.isNot(tok::r_paren) || paren != 0) {
          if (Tok.is(tok::l_paren))
            paren++;
          if (Tok.is(tok::r_paren))
            paren--;
          ConsumeAnyToken();
        }
        // Parse ')'
        if (Tok.is(tok::r_paren))
          T.consumeClose();
        continue;
      }

      OpenMPClauseKind CKind = Tok.isAnnotation()
                                   ? OMPC_unknown
                                   : getOpenMPClauseKind(PP.getSpelling(Tok));
      SourceLocation Loc = ConsumeToken();

      // Parse '('.
      T.consumeOpen();

      // Skip ContextSelectors for when clause
      if (CKind == OMPC_when) {
        OMPTraitInfo &TI = Actions.getASTContext().getNewOMPTraitInfo();
        // parse and skip the ContextSelectors
        parseOMPContextSelectors(Loc, TI);

        // Parse ':'
        ConsumeAnyToken();
      }

      // If no directive is passed, skip in OpenMP 5.0.
      // TODO: Generate nothing directive from OpenMP 5.1.
      if (Tok.is(tok::r_paren)) {
        SkipUntil(tok::annot_pragma_openmp_end);
        break;
      }

      // Parse Directive
      Directive = ParseOpenMPDeclarativeOrExecutableDirective(
          StmtCtx,
          /*ReadDirectiveWithinMetadirective=*/true);
      break;
    }
    break;
  }
  case OMPD_threadprivate: {
    // FIXME: Should this be permitted in C++?
    if ((StmtCtx & ParsedStmtContext::AllowStandaloneOpenMPDirectives) ==
        ParsedStmtContext()) {
      Diag(Tok, diag::err_omp_immediate_directive)
          << getOpenMPDirectiveName(DKind) << 0;
    }
    ConsumeToken();
    DeclDirectiveListParserHelper Helper(this, DKind);
    if (!ParseOpenMPSimpleVarList(DKind, Helper,
                                  /*AllowScopeSpecifier=*/false)) {
      skipUntilPragmaOpenMPEnd(DKind);
      DeclGroupPtrTy Res = Actions.OpenMP().ActOnOpenMPThreadprivateDirective(
          Loc, Helper.getIdentifiers());
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    }
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  }
  case OMPD_allocate: {
    // FIXME: Should this be permitted in C++?
    if ((StmtCtx & ParsedStmtContext::AllowStandaloneOpenMPDirectives) ==
        ParsedStmtContext()) {
      Diag(Tok, diag::err_omp_immediate_directive)
          << getOpenMPDirectiveName(DKind) << 0;
    }
    ConsumeToken();
    DeclDirectiveListParserHelper Helper(this, DKind);
    if (!ParseOpenMPSimpleVarList(DKind, Helper,
                                  /*AllowScopeSpecifier=*/false)) {
      SmallVector<OMPClause *, 1> Clauses;
      if (Tok.isNot(tok::annot_pragma_openmp_end)) {
        llvm::SmallBitVector SeenClauses(llvm::omp::Clause_enumSize + 1);
        while (Tok.isNot(tok::annot_pragma_openmp_end)) {
          OpenMPClauseKind CKind =
              Tok.isAnnotation() ? OMPC_unknown
                                 : getOpenMPClauseKind(PP.getSpelling(Tok));
          Actions.OpenMP().StartOpenMPClause(CKind);
          OMPClause *Clause = ParseOpenMPClause(OMPD_allocate, CKind,
                                                !SeenClauses[unsigned(CKind)]);
          SkipUntil(tok::comma, tok::identifier, tok::annot_pragma_openmp_end,
                    StopBeforeMatch);
          SeenClauses[unsigned(CKind)] = true;
          if (Clause != nullptr)
            Clauses.push_back(Clause);
          if (Tok.is(tok::annot_pragma_openmp_end)) {
            Actions.OpenMP().EndOpenMPClause();
            break;
          }
          // Skip ',' if any.
          if (Tok.is(tok::comma))
            ConsumeToken();
          Actions.OpenMP().EndOpenMPClause();
        }
        skipUntilPragmaOpenMPEnd(DKind);
      }
      DeclGroupPtrTy Res = Actions.OpenMP().ActOnOpenMPAllocateDirective(
          Loc, Helper.getIdentifiers(), Clauses);
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    }
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  }
  case OMPD_declare_reduction:
    ConsumeToken();
    if (DeclGroupPtrTy Res =
            ParseOpenMPDeclareReductionDirective(/*AS=*/AS_none)) {
      skipUntilPragmaOpenMPEnd(OMPD_declare_reduction);
      ConsumeAnyToken();
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    } else {
      SkipUntil(tok::annot_pragma_openmp_end);
    }
    break;
  case OMPD_declare_mapper: {
    ConsumeToken();
    if (DeclGroupPtrTy Res =
            ParseOpenMPDeclareMapperDirective(/*AS=*/AS_none)) {
      // Skip the last annot_pragma_openmp_end.
      ConsumeAnnotationToken();
      Directive = Actions.ActOnDeclStmt(Res, Loc, Tok.getLocation());
    } else {
      SkipUntil(tok::annot_pragma_openmp_end);
    }
    break;
  }
  case OMPD_reverse:
  case OMPD_interchange:
  case OMPD_declare_target: {
    SourceLocation DTLoc = ConsumeAnyToken();
    bool HasClauses = Tok.isNot(tok::annot_pragma_openmp_end);
    SemaOpenMP::DeclareTargetContextInfo DTCI(DKind, DTLoc);
    if (HasClauses)
      ParseOMPDeclareTargetClauses(DTCI);
    bool HasImplicitMappings =
        !HasClauses || (DTCI.ExplicitlyMapped.empty() && DTCI.Indirect);

    if (HasImplicitMappings) {
      Diag(Tok, diag::err_omp_unexpected_directive)
          << 1 << getOpenMPDirectiveName(DKind);
      SkipUntil(tok::annot_pragma_openmp_end);
      break;
    }

    // Skip the last annot_pragma_openmp_end.
    ConsumeAnyToken();

    Actions.OpenMP().ActOnFinishedOpenMPDeclareTargetContext(DTCI);
    break;
  }
  case OMPD_declare_simd:
  case OMPD_begin_declare_target:
  case OMPD_end_declare_target:
  case OMPD_requires:
  case OMPD_begin_declare_variant:
  case OMPD_end_declare_variant:
  case OMPD_declare_variant:
    Diag(Tok, diag::err_omp_unexpected_directive)
        << 1 << getOpenMPDirectiveName(DKind);
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  case OMPD_unknown:
  default:
    Diag(Tok, diag::err_omp_unknown_directive);
    SkipUntil(tok::annot_pragma_openmp_end);
    break;
  }
  return Directive;
}

// Parses simple list:
//   simple-variable-list:
//         '(' id-expression {, id-expression} ')'
//
bool Parser::ParseOpenMPSimpleVarList(
    OpenMPDirectiveKind Kind,
    const llvm::function_ref<void(CXXScopeSpec &, DeclarationNameInfo)>
        &Callback,
    bool AllowScopeSpecifier) {
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPDirectiveName(Kind).data()))
    return true;
  bool IsCorrect = true;
  bool NoIdentIsFound = true;

  // Read tokens while ')' or annot_pragma_openmp_end is not found.
  while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::annot_pragma_openmp_end)) {
    CXXScopeSpec SS;
    UnqualifiedId Name;
    // Read var name.
    Token PrevTok = Tok;
    NoIdentIsFound = false;

    if (AllowScopeSpecifier && getLangOpts().CPlusPlus &&
        ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                       /*ObjectHasErrors=*/false, false)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else if (ParseUnqualifiedId(SS, /*ObjectType=*/nullptr,
                                  /*ObjectHadErrors=*/false, false, false,
                                  false, false, nullptr, Name)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else if (Tok.isNot(tok::comma) && Tok.isNot(tok::r_paren) &&
               Tok.isNot(tok::annot_pragma_openmp_end)) {
      IsCorrect = false;
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
      Diag(PrevTok.getLocation(), diag::err_expected)
          << tok::identifier
          << SourceRange(PrevTok.getLocation(), PrevTokLocation);
    } else {
      Callback(SS, Actions.GetNameFromUnqualifiedId(Name));
    }
    // Consume ','.
    if (Tok.is(tok::comma)) {
      ConsumeToken();
    }
  }

  if (NoIdentIsFound) {
    Diag(Tok, diag::err_expected) << tok::identifier;
    IsCorrect = false;
  }

  // Parse ')'.
  IsCorrect = !T.consumeClose() && IsCorrect;

  return !IsCorrect;
}

OMPClause *Parser::ParseOpenMPSizesClause() {
  SourceLocation ClauseNameLoc, OpenLoc, CloseLoc;
  SmallVector<Expr *, 4> ValExprs;
  if (ParseOpenMPExprListClause(OMPC_sizes, ClauseNameLoc, OpenLoc, CloseLoc,
                                ValExprs))
    return nullptr;

  return Actions.OpenMP().ActOnOpenMPSizesClause(ValExprs, ClauseNameLoc,
                                                 OpenLoc, CloseLoc);
}

OMPClause *Parser::ParseOpenMPUsesAllocatorClause(OpenMPDirectiveKind DKind) {
  SourceLocation Loc = Tok.getLocation();
  ConsumeAnyToken();

  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after, "uses_allocator"))
    return nullptr;
  SmallVector<SemaOpenMP::UsesAllocatorsData, 4> Data;
  do {
    CXXScopeSpec SS;
    Token Replacement;
    ExprResult Allocator =
        getLangOpts().CPlusPlus
            ? ParseCXXIdExpression()
            : tryParseCXXIdExpression(SS, /*isAddressOfOperand=*/false,
                                      Replacement);
    if (Allocator.isInvalid()) {
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
      break;
    }
    SemaOpenMP::UsesAllocatorsData &D = Data.emplace_back();
    D.Allocator = Allocator.get();
    if (Tok.is(tok::l_paren)) {
      BalancedDelimiterTracker T(*this, tok::l_paren,
                                 tok::annot_pragma_openmp_end);
      T.consumeOpen();
      ExprResult AllocatorTraits =
          getLangOpts().CPlusPlus ? ParseCXXIdExpression() : ParseExpression();
      T.consumeClose();
      if (AllocatorTraits.isInvalid()) {
        SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                  StopBeforeMatch);
        break;
      }
      D.AllocatorTraits = AllocatorTraits.get();
      D.LParenLoc = T.getOpenLocation();
      D.RParenLoc = T.getCloseLocation();
    }
    if (Tok.isNot(tok::comma) && Tok.isNot(tok::r_paren))
      Diag(Tok, diag::err_omp_expected_punc) << "uses_allocators" << 0;
    // Parse ','
    if (Tok.is(tok::comma))
      ConsumeAnyToken();
  } while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::annot_pragma_openmp_end));
  T.consumeClose();
  return Actions.OpenMP().ActOnOpenMPUsesAllocatorClause(
      Loc, T.getOpenLocation(), T.getCloseLocation(), Data);
}

/// Parsing of OpenMP clauses.
///
///    clause:
///       if-clause | final-clause | num_threads-clause | safelen-clause |
///       default-clause | private-clause | firstprivate-clause | shared-clause
///       | linear-clause | aligned-clause | collapse-clause | bind-clause |
///       lastprivate-clause | reduction-clause | proc_bind-clause |
///       schedule-clause | copyin-clause | copyprivate-clause | untied-clause |
///       mergeable-clause | flush-clause | read-clause | write-clause |
///       update-clause | capture-clause | seq_cst-clause | device-clause |
///       simdlen-clause | threads-clause | simd-clause | num_teams-clause |
///       thread_limit-clause | priority-clause | grainsize-clause |
///       nogroup-clause | num_tasks-clause | hint-clause | to-clause |
///       from-clause | is_device_ptr-clause | task_reduction-clause |
///       in_reduction-clause | allocator-clause | allocate-clause |
///       acq_rel-clause | acquire-clause | release-clause | relaxed-clause |
///       depobj-clause | destroy-clause | detach-clause | inclusive-clause |
///       exclusive-clause | uses_allocators-clause | use_device_addr-clause |
///       has_device_addr
///
OMPClause *Parser::ParseOpenMPClause(OpenMPDirectiveKind DKind,
                                     OpenMPClauseKind CKind, bool FirstClause) {
  OMPClauseKind = CKind;
  OMPClause *Clause = nullptr;
  bool ErrorFound = false;
  bool WrongDirective = false;
  // Check if clause is allowed for the given directive.
  if (CKind != OMPC_unknown &&
      !isAllowedClauseForDirective(DKind, CKind, getLangOpts().OpenMP)) {
    Diag(Tok, diag::err_omp_unexpected_clause)
        << getOpenMPClauseName(CKind) << getOpenMPDirectiveName(DKind);
    ErrorFound = true;
    WrongDirective = true;
  }

  switch (CKind) {
  case OMPC_final:
  case OMPC_num_threads:
  case OMPC_safelen:
  case OMPC_simdlen:
  case OMPC_collapse:
  case OMPC_ordered:
  case OMPC_num_teams:
  case OMPC_thread_limit:
  case OMPC_priority:
  case OMPC_grainsize:
  case OMPC_num_tasks:
  case OMPC_hint:
  case OMPC_allocator:
  case OMPC_depobj:
  case OMPC_detach:
  case OMPC_novariants:
  case OMPC_nocontext:
  case OMPC_filter:
  case OMPC_partial:
  case OMPC_align:
  case OMPC_message:
  case OMPC_ompx_dyn_cgroup_mem:
    // OpenMP [2.5, Restrictions]
    //  At most one num_threads clause can appear on the directive.
    // OpenMP [2.8.1, simd construct, Restrictions]
    //  Only one safelen  clause can appear on a simd directive.
    //  Only one simdlen  clause can appear on a simd directive.
    //  Only one collapse clause can appear on a simd directive.
    // OpenMP [2.11.1, task Construct, Restrictions]
    //  At most one if clause can appear on the directive.
    //  At most one final clause can appear on the directive.
    // OpenMP [teams Construct, Restrictions]
    //  At most one num_teams clause can appear on the directive.
    //  At most one thread_limit clause can appear on the directive.
    // OpenMP [2.9.1, task Construct, Restrictions]
    // At most one priority clause can appear on the directive.
    // OpenMP [2.9.2, taskloop Construct, Restrictions]
    // At most one grainsize clause can appear on the directive.
    // OpenMP [2.9.2, taskloop Construct, Restrictions]
    // At most one num_tasks clause can appear on the directive.
    // OpenMP [2.11.3, allocate Directive, Restrictions]
    // At most one allocator clause can appear on the directive.
    // OpenMP 5.0, 2.10.1 task Construct, Restrictions.
    // At most one detach clause can appear on the directive.
    // OpenMP 5.1, 2.3.6 dispatch Construct, Restrictions.
    // At most one novariants clause can appear on a dispatch directive.
    // At most one nocontext clause can appear on a dispatch directive.
    // OpenMP [5.1, error directive, Restrictions]
    // At most one message clause can appear on the directive
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    if ((CKind == OMPC_ordered || CKind == OMPC_partial) &&
        PP.LookAhead(/*N=*/0).isNot(tok::l_paren))
      Clause = ParseOpenMPClause(CKind, WrongDirective);
    else if (CKind == OMPC_grainsize || CKind == OMPC_num_tasks)
      Clause = ParseOpenMPSingleExprWithArgClause(DKind, CKind, WrongDirective);
    else
      Clause = ParseOpenMPSingleExprClause(CKind, WrongDirective);
    break;
  case OMPC_fail:
  case OMPC_default:
  case OMPC_proc_bind:
  case OMPC_atomic_default_mem_order:
  case OMPC_at:
  case OMPC_severity:
  case OMPC_bind:
    // OpenMP [2.14.3.1, Restrictions]
    //  Only a single default clause may be specified on a parallel, task or
    //  teams directive.
    // OpenMP [2.5, parallel Construct, Restrictions]
    //  At most one proc_bind clause can appear on the directive.
    // OpenMP [5.0, Requires directive, Restrictions]
    //  At most one atomic_default_mem_order clause can appear
    //  on the directive
    // OpenMP [5.1, error directive, Restrictions]
    //  At most one at clause can appear on the directive
    //  At most one severity clause can appear on the directive
    // OpenMP 5.1, 2.11.7 loop Construct, Restrictions.
    // At most one bind clause can appear on a loop directive.
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = ParseOpenMPSimpleClause(CKind, WrongDirective);
    break;
  case OMPC_device:
  case OMPC_schedule:
  case OMPC_dist_schedule:
  case OMPC_defaultmap:
  case OMPC_order:
    // OpenMP [2.7.1, Restrictions, p. 3]
    //  Only one schedule clause can appear on a loop directive.
    // OpenMP 4.5 [2.10.4, Restrictions, p. 106]
    //  At most one defaultmap clause can appear on the directive.
    // OpenMP 5.0 [2.12.5, target construct, Restrictions]
    //  At most one device clause can appear on the directive.
    // OpenMP 5.1 [2.11.3, order clause, Restrictions]
    //  At most one order clause may appear on a construct.
    if ((getLangOpts().OpenMP < 50 || CKind != OMPC_defaultmap) &&
        (CKind != OMPC_order || getLangOpts().OpenMP >= 51) && !FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }
    [[fallthrough]];
  case OMPC_if:
    Clause = ParseOpenMPSingleExprWithArgClause(DKind, CKind, WrongDirective);
    break;
  case OMPC_nowait:
  case OMPC_untied:
  case OMPC_mergeable:
  case OMPC_read:
  case OMPC_write:
  case OMPC_capture:
  case OMPC_compare:
  case OMPC_seq_cst:
  case OMPC_acq_rel:
  case OMPC_acquire:
  case OMPC_release:
  case OMPC_relaxed:
  case OMPC_weak:
  case OMPC_threads:
  case OMPC_simd:
  case OMPC_nogroup:
  case OMPC_unified_address:
  case OMPC_unified_shared_memory:
  case OMPC_reverse_offload:
  case OMPC_dynamic_allocators:
  case OMPC_full:
    // OpenMP [2.7.1, Restrictions, p. 9]
    //  Only one ordered clause can appear on a loop directive.
    // OpenMP [2.7.1, Restrictions, C/C++, p. 4]
    //  Only one nowait clause can appear on a for directive.
    // OpenMP [5.0, Requires directive, Restrictions]
    //   Each of the requires clauses can appear at most once on the directive.
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = ParseOpenMPClause(CKind, WrongDirective);
    break;
  case OMPC_update:
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = (DKind == OMPD_depobj)
                 ? ParseOpenMPSimpleClause(CKind, WrongDirective)
                 : ParseOpenMPClause(CKind, WrongDirective);
    break;
  case OMPC_private:
  case OMPC_firstprivate:
  case OMPC_lastprivate:
  case OMPC_shared:
  case OMPC_reduction:
  case OMPC_task_reduction:
  case OMPC_in_reduction:
  case OMPC_linear:
  case OMPC_aligned:
  case OMPC_copyin:
  case OMPC_copyprivate:
  case OMPC_flush:
  case OMPC_depend:
  case OMPC_map:
  case OMPC_to:
  case OMPC_from:
  case OMPC_use_device_ptr:
  case OMPC_use_device_addr:
  case OMPC_is_device_ptr:
  case OMPC_has_device_addr:
  case OMPC_allocate:
  case OMPC_nontemporal:
  case OMPC_inclusive:
  case OMPC_exclusive:
  case OMPC_affinity:
  case OMPC_doacross:
  case OMPC_enter:
    if (getLangOpts().OpenMP >= 52 && DKind == OMPD_ordered &&
        CKind == OMPC_depend)
      Diag(Tok, diag::warn_omp_depend_in_ordered_deprecated);
    Clause = ParseOpenMPVarListClause(DKind, CKind, WrongDirective);
    break;
  case OMPC_sizes:
    if (!FirstClause) {
      Diag(Tok, diag::err_omp_more_one_clause)
          << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
      ErrorFound = true;
    }

    Clause = ParseOpenMPSizesClause();
    break;
  case OMPC_uses_allocators:
    Clause = ParseOpenMPUsesAllocatorClause(DKind);
    break;
  case OMPC_destroy:
    if (DKind != OMPD_interop) {
      if (!FirstClause) {
        Diag(Tok, diag::err_omp_more_one_clause)
            << getOpenMPDirectiveName(DKind) << getOpenMPClauseName(CKind) << 0;
        ErrorFound = true;
      }
      Clause = ParseOpenMPClause(CKind, WrongDirective);
      break;
    }
    [[fallthrough]];
  case OMPC_init:
  case OMPC_use:
    Clause = ParseOpenMPInteropClause(CKind, WrongDirective);
    break;
  case OMPC_device_type:
  case OMPC_unknown:
    skipUntilPragmaOpenMPEnd(DKind);
    break;
  case OMPC_threadprivate:
  case OMPC_uniform:
  case OMPC_match:
    if (!WrongDirective)
      Diag(Tok, diag::err_omp_unexpected_clause)
          << getOpenMPClauseName(CKind) << getOpenMPDirectiveName(DKind);
    SkipUntil(tok::comma, tok::annot_pragma_openmp_end, StopBeforeMatch);
    break;
  case OMPC_ompx_attribute:
    Clause = ParseOpenMPOMPXAttributesClause(WrongDirective);
    break;
  case OMPC_ompx_bare:
    if (WrongDirective)
      Diag(Tok, diag::note_ompx_bare_clause)
          << getOpenMPClauseName(CKind) << "target teams";
    if (!ErrorFound && !getLangOpts().OpenMPExtensions) {
      Diag(Tok, diag::err_omp_unexpected_clause_extension_only)
          << getOpenMPClauseName(CKind) << getOpenMPDirectiveName(DKind);
      ErrorFound = true;
    }
    Clause = ParseOpenMPClause(CKind, WrongDirective);
    break;
  default:
    break;
  }
  return ErrorFound ? nullptr : Clause;
}

/// Parses simple expression in parens for single-expression clauses of OpenMP
/// constructs.
/// \param RLoc Returned location of right paren.
ExprResult Parser::ParseOpenMPParensExpr(StringRef ClauseName,
                                         SourceLocation &RLoc,
                                         bool IsAddressOfOperand) {
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after, ClauseName.data()))
    return ExprError();

  SourceLocation ELoc = Tok.getLocation();
  ExprResult LHS(
      ParseCastExpression(AnyCastExpr, IsAddressOfOperand, NotTypeCast));
  ExprResult Val(ParseRHSOfBinaryExpression(LHS, prec::Conditional));
  Val = Actions.ActOnFinishFullExpr(Val.get(), ELoc, /*DiscardedValue*/ false);

  // Parse ')'.
  RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  return Val;
}

/// Parsing of OpenMP clauses with single expressions like 'final',
/// 'collapse', 'safelen', 'num_threads', 'simdlen', 'num_teams',
/// 'thread_limit', 'simdlen', 'priority', 'grainsize', 'num_tasks', 'hint' or
/// 'detach'.
///
///    final-clause:
///      'final' '(' expression ')'
///
///    num_threads-clause:
///      'num_threads' '(' expression ')'
///
///    safelen-clause:
///      'safelen' '(' expression ')'
///
///    simdlen-clause:
///      'simdlen' '(' expression ')'
///
///    collapse-clause:
///      'collapse' '(' expression ')'
///
///    priority-clause:
///      'priority' '(' expression ')'
///
///    grainsize-clause:
///      'grainsize' '(' expression ')'
///
///    num_tasks-clause:
///      'num_tasks' '(' expression ')'
///
///    hint-clause:
///      'hint' '(' expression ')'
///
///    allocator-clause:
///      'allocator' '(' expression ')'
///
///    detach-clause:
///      'detach' '(' event-handler-expression ')'
///
///    align-clause
///      'align' '(' positive-integer-constant ')'
///
OMPClause *Parser::ParseOpenMPSingleExprClause(OpenMPClauseKind Kind,
                                               bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  SourceLocation LLoc = Tok.getLocation();
  SourceLocation RLoc;

  ExprResult Val = ParseOpenMPParensExpr(getOpenMPClauseName(Kind), RLoc);

  if (Val.isInvalid())
    return nullptr;

  if (ParseOnly)
    return nullptr;
  return Actions.OpenMP().ActOnOpenMPSingleExprClause(Kind, Val.get(), Loc,
                                                      LLoc, RLoc);
}

/// Parse indirect clause for '#pragma omp declare target' directive.
///  'indirect' '[' '(' invoked-by-fptr ')' ']'
/// where invoked-by-fptr is a constant boolean expression that evaluates to
/// true or false at compile time.
bool Parser::ParseOpenMPIndirectClause(
    SemaOpenMP::DeclareTargetContextInfo &DTCI, bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  SourceLocation RLoc;

  if (Tok.isNot(tok::l_paren)) {
    if (ParseOnly)
      return false;
    DTCI.Indirect = nullptr;
    return true;
  }

  ExprResult Val =
      ParseOpenMPParensExpr(getOpenMPClauseName(OMPC_indirect), RLoc);
  if (Val.isInvalid())
    return false;

  if (ParseOnly)
    return false;

  if (!Val.get()->isValueDependent() && !Val.get()->isTypeDependent() &&
      !Val.get()->isInstantiationDependent() &&
      !Val.get()->containsUnexpandedParameterPack()) {
    ExprResult Ret = Actions.CheckBooleanCondition(Loc, Val.get());
    if (Ret.isInvalid())
      return false;
    llvm::APSInt Result;
    Ret = Actions.VerifyIntegerConstantExpression(Val.get(), &Result,
                                                  Sema::AllowFold);
    if (Ret.isInvalid())
      return false;
    DTCI.Indirect = Val.get();
    return true;
  }
  return false;
}

/// Parses a comma-separated list of interop-types and a prefer_type list.
///
bool Parser::ParseOMPInteropInfo(OMPInteropInfo &InteropInfo,
                                 OpenMPClauseKind Kind) {
  const Token &Tok = getCurToken();
  bool HasError = false;
  bool IsTarget = false;
  bool IsTargetSync = false;

  while (Tok.is(tok::identifier)) {
    // Currently prefer_type is only allowed with 'init' and it must be first.
    bool PreferTypeAllowed = Kind == OMPC_init &&
                             InteropInfo.PreferTypes.empty() && !IsTarget &&
                             !IsTargetSync;
    if (Tok.getIdentifierInfo()->isStr("target")) {
      // OpenMP 5.1 [2.15.1, interop Construct, Restrictions]
      // Each interop-type may be specified on an action-clause at most
      // once.
      if (IsTarget)
        Diag(Tok, diag::warn_omp_more_one_interop_type) << "target";
      IsTarget = true;
      ConsumeToken();
    } else if (Tok.getIdentifierInfo()->isStr("targetsync")) {
      if (IsTargetSync)
        Diag(Tok, diag::warn_omp_more_one_interop_type) << "targetsync";
      IsTargetSync = true;
      ConsumeToken();
    } else if (Tok.getIdentifierInfo()->isStr("prefer_type") &&
               PreferTypeAllowed) {
      ConsumeToken();
      BalancedDelimiterTracker PT(*this, tok::l_paren,
                                  tok::annot_pragma_openmp_end);
      if (PT.expectAndConsume(diag::err_expected_lparen_after, "prefer_type"))
        HasError = true;

      while (Tok.isNot(tok::r_paren)) {
        SourceLocation Loc = Tok.getLocation();
        ExprResult LHS = ParseCastExpression(AnyCastExpr);
        ExprResult PTExpr = Actions.CorrectDelayedTyposInExpr(
            ParseRHSOfBinaryExpression(LHS, prec::Conditional));
        PTExpr = Actions.ActOnFinishFullExpr(PTExpr.get(), Loc,
                                             /*DiscardedValue=*/false);
        if (PTExpr.isUsable()) {
          InteropInfo.PreferTypes.push_back(PTExpr.get());
        } else {
          HasError = true;
          SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                    StopBeforeMatch);
        }

        if (Tok.is(tok::comma))
          ConsumeToken();
      }
      PT.consumeClose();
    } else {
      HasError = true;
      Diag(Tok, diag::err_omp_expected_interop_type);
      ConsumeToken();
    }
    if (!Tok.is(tok::comma))
      break;
    ConsumeToken();
  }

  if (!HasError && !IsTarget && !IsTargetSync) {
    Diag(Tok, diag::err_omp_expected_interop_type);
    HasError = true;
  }

  if (Kind == OMPC_init) {
    if (Tok.isNot(tok::colon) && (IsTarget || IsTargetSync))
      Diag(Tok, diag::warn_pragma_expected_colon) << "interop types";
    if (Tok.is(tok::colon))
      ConsumeToken();
  }

  // As of OpenMP 5.1,there are two interop-types, "target" and
  // "targetsync". Either or both are allowed for a single interop.
  InteropInfo.IsTarget = IsTarget;
  InteropInfo.IsTargetSync = IsTargetSync;

  return HasError;
}

/// Parsing of OpenMP clauses that use an interop-var.
///
/// init-clause:
///   init([interop-modifier, ]interop-type[[, interop-type] ... ]:interop-var)
///
/// destroy-clause:
///   destroy(interop-var)
///
/// use-clause:
///   use(interop-var)
///
/// interop-modifier:
///   prefer_type(preference-list)
///
/// preference-list:
///   foreign-runtime-id [, foreign-runtime-id]...
///
/// foreign-runtime-id:
///   <string-literal> | <constant-integral-expression>
///
/// interop-type:
///   target | targetsync
///
OMPClause *Parser::ParseOpenMPInteropClause(OpenMPClauseKind Kind,
                                            bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind).data()))
    return nullptr;

  bool InteropError = false;
  OMPInteropInfo InteropInfo;
  if (Kind == OMPC_init)
    InteropError = ParseOMPInteropInfo(InteropInfo, OMPC_init);

  // Parse the variable.
  SourceLocation VarLoc = Tok.getLocation();
  ExprResult InteropVarExpr =
      Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
  if (!InteropVarExpr.isUsable()) {
    SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
              StopBeforeMatch);
  }

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  if (ParseOnly || !InteropVarExpr.isUsable() || InteropError)
    return nullptr;

  if (Kind == OMPC_init)
    return Actions.OpenMP().ActOnOpenMPInitClause(
        InteropVarExpr.get(), InteropInfo, Loc, T.getOpenLocation(), VarLoc,
        RLoc);
  if (Kind == OMPC_use)
    return Actions.OpenMP().ActOnOpenMPUseClause(
        InteropVarExpr.get(), Loc, T.getOpenLocation(), VarLoc, RLoc);

  if (Kind == OMPC_destroy)
    return Actions.OpenMP().ActOnOpenMPDestroyClause(
        InteropVarExpr.get(), Loc, T.getOpenLocation(), VarLoc, RLoc);

  llvm_unreachable("Unexpected interop variable clause.");
}

OMPClause *Parser::ParseOpenMPOMPXAttributesClause(bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(OMPC_ompx_attribute).data()))
    return nullptr;

  ParsedAttributes ParsedAttrs(AttrFactory);
  ParseAttributes(PAKM_GNU | PAKM_CXX11, ParsedAttrs);

  // Parse ')'.
  if (T.consumeClose())
    return nullptr;

  if (ParseOnly)
    return nullptr;

  SmallVector<Attr *> Attrs;
  for (const ParsedAttr &PA : ParsedAttrs) {
    switch (PA.getKind()) {
    case ParsedAttr::AT_AMDGPUFlatWorkGroupSize:
      if (!PA.checkExactlyNumArgs(Actions, 2))
        continue;
      if (auto *A = Actions.AMDGPU().CreateAMDGPUFlatWorkGroupSizeAttr(
              PA, PA.getArgAsExpr(0), PA.getArgAsExpr(1)))
        Attrs.push_back(A);
      continue;
    case ParsedAttr::AT_AMDGPUWavesPerEU:
      if (!PA.checkAtLeastNumArgs(Actions, 1) ||
          !PA.checkAtMostNumArgs(Actions, 2))
        continue;
      if (auto *A = Actions.AMDGPU().CreateAMDGPUWavesPerEUAttr(
              PA, PA.getArgAsExpr(0),
              PA.getNumArgs() > 1 ? PA.getArgAsExpr(1) : nullptr))
        Attrs.push_back(A);
      continue;
    case ParsedAttr::AT_CUDALaunchBounds:
      if (!PA.checkAtLeastNumArgs(Actions, 1) ||
          !PA.checkAtMostNumArgs(Actions, 2))
        continue;
      if (auto *A = Actions.CreateLaunchBoundsAttr(
              PA, PA.getArgAsExpr(0),
              PA.getNumArgs() > 1 ? PA.getArgAsExpr(1) : nullptr,
              PA.getNumArgs() > 2 ? PA.getArgAsExpr(2) : nullptr))
        Attrs.push_back(A);
      continue;
    default:
      Diag(Loc, diag::warn_omp_invalid_attribute_for_ompx_attributes) << PA;
      continue;
    };
  }

  return Actions.OpenMP().ActOnOpenMPXAttributeClause(
      Attrs, Loc, T.getOpenLocation(), T.getCloseLocation());
}

/// Parsing of simple OpenMP clauses like 'default' or 'proc_bind'.
///
///    default-clause:
///         'default' '(' 'none' | 'shared' | 'private' | 'firstprivate' ')'
///
///    proc_bind-clause:
///         'proc_bind' '(' 'master' | 'close' | 'spread' ')'
///
///    bind-clause:
///         'bind' '(' 'teams' | 'parallel' | 'thread' ')'
///
///    update-clause:
///         'update' '(' 'in' | 'out' | 'inout' | 'mutexinoutset' |
///         'inoutset' ')'
///
OMPClause *Parser::ParseOpenMPSimpleClause(OpenMPClauseKind Kind,
                                           bool ParseOnly) {
  std::optional<SimpleClauseData> Val = parseOpenMPSimpleClause(*this, Kind);
  if (!Val || ParseOnly)
    return nullptr;
  if (getLangOpts().OpenMP < 51 && Kind == OMPC_default &&
      (static_cast<DefaultKind>(Val->Type) == OMP_DEFAULT_private ||
       static_cast<DefaultKind>(Val->Type) ==
           OMP_DEFAULT_firstprivate)) {
    Diag(Val->LOpen, diag::err_omp_invalid_dsa)
        << getOpenMPClauseName(static_cast<DefaultKind>(Val->Type) ==
                                       OMP_DEFAULT_private
                                   ? OMPC_private
                                   : OMPC_firstprivate)
        << getOpenMPClauseName(OMPC_default) << "5.1";
    return nullptr;
  }
  return Actions.OpenMP().ActOnOpenMPSimpleClause(
      Kind, Val->Type, Val->TypeLoc, Val->LOpen, Val->Loc, Val->RLoc);
}

/// Parsing of OpenMP clauses like 'ordered'.
///
///    ordered-clause:
///         'ordered'
///
///    nowait-clause:
///         'nowait'
///
///    untied-clause:
///         'untied'
///
///    mergeable-clause:
///         'mergeable'
///
///    read-clause:
///         'read'
///
///    threads-clause:
///         'threads'
///
///    simd-clause:
///         'simd'
///
///    nogroup-clause:
///         'nogroup'
///
OMPClause *Parser::ParseOpenMPClause(OpenMPClauseKind Kind, bool ParseOnly) {
  SourceLocation Loc = Tok.getLocation();
  ConsumeAnyToken();

  if (ParseOnly)
    return nullptr;
  return Actions.OpenMP().ActOnOpenMPClause(Kind, Loc, Tok.getLocation());
}

/// Parsing of OpenMP clauses with single expressions and some additional
/// argument like 'schedule' or 'dist_schedule'.
///
///    schedule-clause:
///      'schedule' '(' [ modifier [ ',' modifier ] ':' ] kind [',' expression ]
///      ')'
///
///    if-clause:
///      'if' '(' [ directive-name-modifier ':' ] expression ')'
///
///    defaultmap:
///      'defaultmap' '(' modifier [ ':' kind ] ')'
///
///    device-clause:
///      'device' '(' [ device-modifier ':' ] expression ')'
///
OMPClause *Parser::ParseOpenMPSingleExprWithArgClause(OpenMPDirectiveKind DKind,
                                                      OpenMPClauseKind Kind,
                                                      bool ParseOnly) {
  SourceLocation Loc = ConsumeToken();
  SourceLocation DelimLoc;
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind).data()))
    return nullptr;

  ExprResult Val;
  SmallVector<unsigned, 4> Arg;
  SmallVector<SourceLocation, 4> KLoc;
  if (Kind == OMPC_schedule) {
    enum { Modifier1, Modifier2, ScheduleKind, NumberOfElements };
    Arg.resize(NumberOfElements);
    KLoc.resize(NumberOfElements);
    Arg[Modifier1] = OMPC_SCHEDULE_MODIFIER_unknown;
    Arg[Modifier2] = OMPC_SCHEDULE_MODIFIER_unknown;
    Arg[ScheduleKind] = OMPC_SCHEDULE_unknown;
    unsigned KindModifier = getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
    if (KindModifier > OMPC_SCHEDULE_unknown) {
      // Parse 'modifier'
      Arg[Modifier1] = KindModifier;
      KLoc[Modifier1] = Tok.getLocation();
      if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
          Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
      if (Tok.is(tok::comma)) {
        // Parse ',' 'modifier'
        ConsumeAnyToken();
        KindModifier = getOpenMPSimpleClauseType(
            Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
        Arg[Modifier2] = KindModifier > OMPC_SCHEDULE_unknown
                             ? KindModifier
                             : (unsigned)OMPC_SCHEDULE_unknown;
        KLoc[Modifier2] = Tok.getLocation();
        if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
            Tok.isNot(tok::annot_pragma_openmp_end))
          ConsumeAnyToken();
      }
      // Parse ':'
      if (Tok.is(tok::colon))
        ConsumeAnyToken();
      else
        Diag(Tok, diag::warn_pragma_expected_colon) << "schedule modifier";
      KindModifier = getOpenMPSimpleClauseType(
          Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
    }
    Arg[ScheduleKind] = KindModifier;
    KLoc[ScheduleKind] = Tok.getLocation();
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    if ((Arg[ScheduleKind] == OMPC_SCHEDULE_static ||
         Arg[ScheduleKind] == OMPC_SCHEDULE_dynamic ||
         Arg[ScheduleKind] == OMPC_SCHEDULE_guided) &&
        Tok.is(tok::comma))
      DelimLoc = ConsumeAnyToken();
  } else if (Kind == OMPC_dist_schedule) {
    Arg.push_back(getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts()));
    KLoc.push_back(Tok.getLocation());
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    if (Arg.back() == OMPC_DIST_SCHEDULE_static && Tok.is(tok::comma))
      DelimLoc = ConsumeAnyToken();
  } else if (Kind == OMPC_defaultmap) {
    // Get a defaultmap modifier
    unsigned Modifier = getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
    // Set defaultmap modifier to unknown if it is either scalar, aggregate, or
    // pointer
    if (Modifier < OMPC_DEFAULTMAP_MODIFIER_unknown)
      Modifier = OMPC_DEFAULTMAP_MODIFIER_unknown;
    Arg.push_back(Modifier);
    KLoc.push_back(Tok.getLocation());
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
    // Parse ':'
    if (Tok.is(tok::colon) || getLangOpts().OpenMP < 50) {
      if (Tok.is(tok::colon))
        ConsumeAnyToken();
      else if (Arg.back() != OMPC_DEFAULTMAP_MODIFIER_unknown)
        Diag(Tok, diag::warn_pragma_expected_colon) << "defaultmap modifier";
      // Get a defaultmap kind
      Arg.push_back(getOpenMPSimpleClauseType(
          Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts()));
      KLoc.push_back(Tok.getLocation());
      if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
          Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
    } else {
      Arg.push_back(OMPC_DEFAULTMAP_unknown);
      KLoc.push_back(SourceLocation());
    }
  } else if (Kind == OMPC_order) {
    enum { Modifier, OrderKind, NumberOfElements };
    Arg.resize(NumberOfElements);
    KLoc.resize(NumberOfElements);
    Arg[Modifier] = OMPC_ORDER_MODIFIER_unknown;
    Arg[OrderKind] = OMPC_ORDER_unknown;
    unsigned KindModifier = getOpenMPSimpleClauseType(
        Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
    if (KindModifier > OMPC_ORDER_unknown) {
      // Parse 'modifier'
      Arg[Modifier] = KindModifier;
      KLoc[Modifier] = Tok.getLocation();
      if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
          Tok.isNot(tok::annot_pragma_openmp_end))
        ConsumeAnyToken();
      // Parse ':'
      if (Tok.is(tok::colon))
        ConsumeAnyToken();
      else
        Diag(Tok, diag::warn_pragma_expected_colon) << "order modifier";
      KindModifier = getOpenMPSimpleClauseType(
          Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts());
    }
    Arg[OrderKind] = KindModifier;
    KLoc[OrderKind] = Tok.getLocation();
    if (Tok.isNot(tok::r_paren) && Tok.isNot(tok::comma) &&
        Tok.isNot(tok::annot_pragma_openmp_end))
      ConsumeAnyToken();
  } else if (Kind == OMPC_device) {
    // Only target executable directives support extended device construct.
    if (isOpenMPTargetExecutionDirective(DKind) && getLangOpts().OpenMP >= 50 &&
        NextToken().is(tok::colon)) {
      // Parse optional <device modifier> ':'
      Arg.push_back(getOpenMPSimpleClauseType(
          Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok), getLangOpts()));
      KLoc.push_back(Tok.getLocation());
      ConsumeAnyToken();
      // Parse ':'
      ConsumeAnyToken();
    } else {
      Arg.push_back(OMPC_DEVICE_unknown);
      KLoc.emplace_back();
    }
  } else if (Kind == OMPC_grainsize) {
    // Parse optional <grainsize modifier> ':'
    OpenMPGrainsizeClauseModifier Modifier =
        static_cast<OpenMPGrainsizeClauseModifier>(getOpenMPSimpleClauseType(
            Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok),
            getLangOpts()));
    if (getLangOpts().OpenMP >= 51) {
      if (NextToken().is(tok::colon)) {
        Arg.push_back(Modifier);
        KLoc.push_back(Tok.getLocation());
        // Parse modifier
        ConsumeAnyToken();
        // Parse ':'
        ConsumeAnyToken();
      } else {
        if (Modifier == OMPC_GRAINSIZE_strict) {
          Diag(Tok, diag::err_modifier_expected_colon) << "strict";
          // Parse modifier
          ConsumeAnyToken();
        }
        Arg.push_back(OMPC_GRAINSIZE_unknown);
        KLoc.emplace_back();
      }
    } else {
      Arg.push_back(OMPC_GRAINSIZE_unknown);
      KLoc.emplace_back();
    }
  } else if (Kind == OMPC_num_tasks) {
    // Parse optional <num_tasks modifier> ':'
    OpenMPNumTasksClauseModifier Modifier =
        static_cast<OpenMPNumTasksClauseModifier>(getOpenMPSimpleClauseType(
            Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok),
            getLangOpts()));
    if (getLangOpts().OpenMP >= 51) {
      if (NextToken().is(tok::colon)) {
        Arg.push_back(Modifier);
        KLoc.push_back(Tok.getLocation());
        // Parse modifier
        ConsumeAnyToken();
        // Parse ':'
        ConsumeAnyToken();
      } else {
        if (Modifier == OMPC_NUMTASKS_strict) {
          Diag(Tok, diag::err_modifier_expected_colon) << "strict";
          // Parse modifier
          ConsumeAnyToken();
        }
        Arg.push_back(OMPC_NUMTASKS_unknown);
        KLoc.emplace_back();
      }
    } else {
      Arg.push_back(OMPC_NUMTASKS_unknown);
      KLoc.emplace_back();
    }
  } else {
    assert(Kind == OMPC_if);
    KLoc.push_back(Tok.getLocation());
    TentativeParsingAction TPA(*this);
    auto DK = parseOpenMPDirectiveKind(*this);
    Arg.push_back(DK);
    if (DK != OMPD_unknown) {
      ConsumeToken();
      if (Tok.is(tok::colon) && getLangOpts().OpenMP > 40) {
        TPA.Commit();
        DelimLoc = ConsumeToken();
      } else {
        TPA.Revert();
        Arg.back() = unsigned(OMPD_unknown);
      }
    } else {
      TPA.Revert();
    }
  }

  bool NeedAnExpression = (Kind == OMPC_schedule && DelimLoc.isValid()) ||
                          (Kind == OMPC_dist_schedule && DelimLoc.isValid()) ||
                          Kind == OMPC_if || Kind == OMPC_device ||
                          Kind == OMPC_grainsize || Kind == OMPC_num_tasks;
  if (NeedAnExpression) {
    SourceLocation ELoc = Tok.getLocation();
    ExprResult LHS(ParseCastExpression(AnyCastExpr, false, NotTypeCast));
    Val = ParseRHSOfBinaryExpression(LHS, prec::Conditional);
    Val =
        Actions.ActOnFinishFullExpr(Val.get(), ELoc, /*DiscardedValue*/ false);
  }

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  if (NeedAnExpression && Val.isInvalid())
    return nullptr;

  if (ParseOnly)
    return nullptr;
  return Actions.OpenMP().ActOnOpenMPSingleExprWithArgClause(
      Kind, Arg, Val.get(), Loc, T.getOpenLocation(), KLoc, DelimLoc, RLoc);
}

static bool ParseReductionId(Parser &P, CXXScopeSpec &ReductionIdScopeSpec,
                             UnqualifiedId &ReductionId) {
  if (ReductionIdScopeSpec.isEmpty()) {
    auto OOK = OO_None;
    switch (P.getCurToken().getKind()) {
    case tok::plus:
      OOK = OO_Plus;
      break;
    case tok::minus:
      OOK = OO_Minus;
      break;
    case tok::star:
      OOK = OO_Star;
      break;
    case tok::amp:
      OOK = OO_Amp;
      break;
    case tok::pipe:
      OOK = OO_Pipe;
      break;
    case tok::caret:
      OOK = OO_Caret;
      break;
    case tok::ampamp:
      OOK = OO_AmpAmp;
      break;
    case tok::pipepipe:
      OOK = OO_PipePipe;
      break;
    default:
      break;
    }
    if (OOK != OO_None) {
      SourceLocation OpLoc = P.ConsumeToken();
      SourceLocation SymbolLocations[] = {OpLoc, OpLoc, SourceLocation()};
      ReductionId.setOperatorFunctionId(OpLoc, OOK, SymbolLocations);
      return false;
    }
  }
  return P.ParseUnqualifiedId(
      ReductionIdScopeSpec, /*ObjectType=*/nullptr,
      /*ObjectHadErrors=*/false, /*EnteringContext*/ false,
      /*AllowDestructorName*/ false,
      /*AllowConstructorName*/ false,
      /*AllowDeductionGuide*/ false, nullptr, ReductionId);
}

/// Checks if the token is a valid map-type-modifier.
/// FIXME: It will return an OpenMPMapClauseKind if that's what it parses.
static OpenMPMapModifierKind isMapModifier(Parser &P) {
  Token Tok = P.getCurToken();
  if (!Tok.is(tok::identifier))
    return OMPC_MAP_MODIFIER_unknown;

  Preprocessor &PP = P.getPreprocessor();
  OpenMPMapModifierKind TypeModifier =
      static_cast<OpenMPMapModifierKind>(getOpenMPSimpleClauseType(
          OMPC_map, PP.getSpelling(Tok), P.getLangOpts()));
  return TypeModifier;
}

/// Parse the mapper modifier in map, to, and from clauses.
bool Parser::parseMapperModifier(SemaOpenMP::OpenMPVarListDataTy &Data) {
  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::colon);
  if (T.expectAndConsume(diag::err_expected_lparen_after, "mapper")) {
    SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
              StopBeforeMatch);
    return true;
  }
  // Parse mapper-identifier
  if (getLangOpts().CPlusPlus)
    ParseOptionalCXXScopeSpecifier(Data.ReductionOrMapperIdScopeSpec,
                                   /*ObjectType=*/nullptr,
                                   /*ObjectHasErrors=*/false,
                                   /*EnteringContext=*/false);
  if (Tok.isNot(tok::identifier) && Tok.isNot(tok::kw_default)) {
    Diag(Tok.getLocation(), diag::err_omp_mapper_illegal_identifier);
    SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
              StopBeforeMatch);
    return true;
  }
  auto &DeclNames = Actions.getASTContext().DeclarationNames;
  Data.ReductionOrMapperId = DeclarationNameInfo(
      DeclNames.getIdentifier(Tok.getIdentifierInfo()), Tok.getLocation());
  ConsumeToken();
  // Parse ')'.
  return T.consumeClose();
}

static OpenMPMapClauseKind isMapType(Parser &P);

/// Parse map-type-modifiers in map clause.
/// map([ [map-type-modifier[,] [map-type-modifier[,] ...] [map-type] : ] list)
/// where, map-type-modifier ::= always | close | mapper(mapper-identifier) |
/// present
/// where, map-type ::= alloc | delete | from | release | to | tofrom
bool Parser::parseMapTypeModifiers(SemaOpenMP::OpenMPVarListDataTy &Data) {
  bool HasMapType = false;
  SourceLocation PreMapLoc = Tok.getLocation();
  StringRef PreMapName = "";
  while (getCurToken().isNot(tok::colon)) {
    OpenMPMapModifierKind TypeModifier = isMapModifier(*this);
    OpenMPMapClauseKind MapKind = isMapType(*this);
    if (TypeModifier == OMPC_MAP_MODIFIER_always ||
        TypeModifier == OMPC_MAP_MODIFIER_close ||
        TypeModifier == OMPC_MAP_MODIFIER_present ||
        TypeModifier == OMPC_MAP_MODIFIER_ompx_hold) {
      Data.MapTypeModifiers.push_back(TypeModifier);
      Data.MapTypeModifiersLoc.push_back(Tok.getLocation());
      if (PP.LookAhead(0).isNot(tok::comma) &&
          PP.LookAhead(0).isNot(tok::colon) && getLangOpts().OpenMP >= 52)
        Diag(Tok.getLocation(), diag::err_omp_missing_comma)
            << "map type modifier";
      ConsumeToken();
    } else if (TypeModifier == OMPC_MAP_MODIFIER_mapper) {
      Data.MapTypeModifiers.push_back(TypeModifier);
      Data.MapTypeModifiersLoc.push_back(Tok.getLocation());
      ConsumeToken();
      if (parseMapperModifier(Data))
        return true;
      if (Tok.isNot(tok::comma) && Tok.isNot(tok::colon) &&
          getLangOpts().OpenMP >= 52)
        Diag(Data.MapTypeModifiersLoc.back(), diag::err_omp_missing_comma)
            << "map type modifier";

    } else if (getLangOpts().OpenMP >= 60 && MapKind != OMPC_MAP_unknown) {
      if (!HasMapType) {
        HasMapType = true;
        Data.ExtraModifier = MapKind;
        MapKind = OMPC_MAP_unknown;
        PreMapLoc = Tok.getLocation();
        PreMapName = Tok.getIdentifierInfo()->getName();
      } else {
        Diag(Tok, diag::err_omp_more_one_map_type);
        Diag(PreMapLoc, diag::note_previous_map_type_specified_here)
            << PreMapName;
      }
      ConsumeToken();
    } else {
      // For the case of unknown map-type-modifier or a map-type.
      // Map-type is followed by a colon; the function returns when it
      // encounters a token followed by a colon.
      if (Tok.is(tok::comma)) {
        Diag(Tok, diag::err_omp_map_type_modifier_missing);
        ConsumeToken();
        continue;
      }
      // Potential map-type token as it is followed by a colon.
      if (PP.LookAhead(0).is(tok::colon)) {
        if (getLangOpts().OpenMP >= 60) {
          break;
        } else {
          return false;
        }
      }

      Diag(Tok, diag::err_omp_unknown_map_type_modifier)
          << (getLangOpts().OpenMP >= 51 ? (getLangOpts().OpenMP >= 52 ? 2 : 1)
                                         : 0)
          << getLangOpts().OpenMPExtensions;
      ConsumeToken();
    }
    if (getCurToken().is(tok::comma))
      ConsumeToken();
  }
  if (getLangOpts().OpenMP >= 60 && !HasMapType) {
    if (!Tok.is(tok::colon)) {
      Diag(Tok, diag::err_omp_unknown_map_type);
      ConsumeToken();
    } else {
      Data.ExtraModifier = OMPC_MAP_unknown;
    }
  }
  return false;
}

/// Checks if the token is a valid map-type.
/// If it is not MapType kind, OMPC_MAP_unknown is returned.
static OpenMPMapClauseKind isMapType(Parser &P) {
  Token Tok = P.getCurToken();
  // The map-type token can be either an identifier or the C++ delete keyword.
  if (!Tok.isOneOf(tok::identifier, tok::kw_delete))
    return OMPC_MAP_unknown;
  Preprocessor &PP = P.getPreprocessor();
  unsigned MapType =
      getOpenMPSimpleClauseType(OMPC_map, PP.getSpelling(Tok), P.getLangOpts());
  if (MapType == OMPC_MAP_to || MapType == OMPC_MAP_from ||
      MapType == OMPC_MAP_tofrom || MapType == OMPC_MAP_alloc ||
      MapType == OMPC_MAP_delete || MapType == OMPC_MAP_release)
    return static_cast<OpenMPMapClauseKind>(MapType);
  return OMPC_MAP_unknown;
}

/// Parse map-type in map clause.
/// map([ [map-type-modifier[,] [map-type-modifier[,] ...] map-type : ] list)
/// where, map-type ::= to | from | tofrom | alloc | release | delete
static void parseMapType(Parser &P, SemaOpenMP::OpenMPVarListDataTy &Data) {
  Token Tok = P.getCurToken();
  if (Tok.is(tok::colon)) {
    P.Diag(Tok, diag::err_omp_map_type_missing);
    return;
  }
  Data.ExtraModifier = isMapType(P);
  if (Data.ExtraModifier == OMPC_MAP_unknown)
    P.Diag(Tok, diag::err_omp_unknown_map_type);
  P.ConsumeToken();
}

/// Parses simple expression in parens for single-expression clauses of OpenMP
/// constructs.
ExprResult Parser::ParseOpenMPIteratorsExpr() {
  assert(Tok.is(tok::identifier) && PP.getSpelling(Tok) == "iterator" &&
         "Expected 'iterator' token.");
  SourceLocation IteratorKwLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after, "iterator"))
    return ExprError();

  SourceLocation LLoc = T.getOpenLocation();
  SmallVector<SemaOpenMP::OMPIteratorData, 4> Data;
  while (Tok.isNot(tok::r_paren) && Tok.isNot(tok::annot_pragma_openmp_end)) {
    // Check if the type parsing is required.
    ParsedType IteratorType;
    if (Tok.isNot(tok::identifier) || NextToken().isNot(tok::equal)) {
      // identifier '=' is not found - parse type.
      TypeResult TR = ParseTypeName();
      if (TR.isInvalid()) {
        T.skipToEnd();
        return ExprError();
      }
      IteratorType = TR.get();
    }

    // Parse identifier.
    IdentifierInfo *II = nullptr;
    SourceLocation IdLoc;
    if (Tok.is(tok::identifier)) {
      II = Tok.getIdentifierInfo();
      IdLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected_unqualified_id) << 0;
    }

    // Parse '='.
    SourceLocation AssignLoc;
    if (Tok.is(tok::equal))
      AssignLoc = ConsumeToken();
    else
      Diag(Tok, diag::err_omp_expected_equal_in_iterator);

    // Parse range-specification - <begin> ':' <end> [ ':' <step> ]
    ColonProtectionRAIIObject ColonRAII(*this);
    // Parse <begin>
    SourceLocation Loc = Tok.getLocation();
    ExprResult LHS = ParseCastExpression(AnyCastExpr);
    ExprResult Begin = Actions.CorrectDelayedTyposInExpr(
        ParseRHSOfBinaryExpression(LHS, prec::Conditional));
    Begin = Actions.ActOnFinishFullExpr(Begin.get(), Loc,
                                        /*DiscardedValue=*/false);
    // Parse ':'.
    SourceLocation ColonLoc;
    if (Tok.is(tok::colon))
      ColonLoc = ConsumeToken();

    // Parse <end>
    Loc = Tok.getLocation();
    LHS = ParseCastExpression(AnyCastExpr);
    ExprResult End = Actions.CorrectDelayedTyposInExpr(
        ParseRHSOfBinaryExpression(LHS, prec::Conditional));
    End = Actions.ActOnFinishFullExpr(End.get(), Loc,
                                      /*DiscardedValue=*/false);

    SourceLocation SecColonLoc;
    ExprResult Step;
    // Parse optional step.
    if (Tok.is(tok::colon)) {
      // Parse ':'
      SecColonLoc = ConsumeToken();
      // Parse <step>
      Loc = Tok.getLocation();
      LHS = ParseCastExpression(AnyCastExpr);
      Step = Actions.CorrectDelayedTyposInExpr(
          ParseRHSOfBinaryExpression(LHS, prec::Conditional));
      Step = Actions.ActOnFinishFullExpr(Step.get(), Loc,
                                         /*DiscardedValue=*/false);
    }

    // Parse ',' or ')'
    if (Tok.isNot(tok::comma) && Tok.isNot(tok::r_paren))
      Diag(Tok, diag::err_omp_expected_punc_after_iterator);
    if (Tok.is(tok::comma))
      ConsumeToken();

    SemaOpenMP::OMPIteratorData &D = Data.emplace_back();
    D.DeclIdent = II;
    D.DeclIdentLoc = IdLoc;
    D.Type = IteratorType;
    D.AssignLoc = AssignLoc;
    D.ColonLoc = ColonLoc;
    D.SecColonLoc = SecColonLoc;
    D.Range.Begin = Begin.get();
    D.Range.End = End.get();
    D.Range.Step = Step.get();
  }

  // Parse ')'.
  SourceLocation RLoc = Tok.getLocation();
  if (!T.consumeClose())
    RLoc = T.getCloseLocation();

  return Actions.OpenMP().ActOnOMPIteratorExpr(getCurScope(), IteratorKwLoc,
                                               LLoc, RLoc, Data);
}

bool Parser::ParseOpenMPReservedLocator(OpenMPClauseKind Kind,
                                        SemaOpenMP::OpenMPVarListDataTy &Data,
                                        const LangOptions &LangOpts) {
  // Currently the only reserved locator is 'omp_all_memory' which is only
  // allowed on a depend clause.
  if (Kind != OMPC_depend || LangOpts.OpenMP < 51)
    return false;

  if (Tok.is(tok::identifier) &&
      Tok.getIdentifierInfo()->isStr("omp_all_memory")) {

    if (Data.ExtraModifier == OMPC_DEPEND_outallmemory ||
        Data.ExtraModifier == OMPC_DEPEND_inoutallmemory)
      Diag(Tok, diag::warn_omp_more_one_omp_all_memory);
    else if (Data.ExtraModifier != OMPC_DEPEND_out &&
             Data.ExtraModifier != OMPC_DEPEND_inout)
      Diag(Tok, diag::err_omp_requires_out_inout_depend_type);
    else
      Data.ExtraModifier = Data.ExtraModifier == OMPC_DEPEND_out
                               ? OMPC_DEPEND_outallmemory
                               : OMPC_DEPEND_inoutallmemory;
    ConsumeToken();
    return true;
  }
  return false;
}

/// Parse step size expression. Returns true if parsing is successfull,
/// otherwise returns false.
static bool parseStepSize(Parser &P, SemaOpenMP::OpenMPVarListDataTy &Data,
                          OpenMPClauseKind CKind, SourceLocation ELoc) {
  ExprResult Tail = P.ParseAssignmentExpression();
  Sema &Actions = P.getActions();
  Tail = Actions.ActOnFinishFullExpr(Tail.get(), ELoc,
                                     /*DiscardedValue*/ false);
  if (Tail.isUsable()) {
    Data.DepModOrTailExpr = Tail.get();
    Token CurTok = P.getCurToken();
    if (CurTok.isNot(tok::r_paren) && CurTok.isNot(tok::comma)) {
      P.Diag(CurTok, diag::err_expected_punc) << "step expression";
    }
    return true;
  }
  return false;
}

/// Parses clauses with list.
bool Parser::ParseOpenMPVarList(OpenMPDirectiveKind DKind,
                                OpenMPClauseKind Kind,
                                SmallVectorImpl<Expr *> &Vars,
                                SemaOpenMP::OpenMPVarListDataTy &Data) {
  UnqualifiedId UnqualifiedReductionId;
  bool InvalidReductionId = false;
  bool IsInvalidMapperModifier = false;

  // Parse '('.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.expectAndConsume(diag::err_expected_lparen_after,
                         getOpenMPClauseName(Kind).data()))
    return true;

  bool HasIterator = false;
  bool InvalidIterator = false;
  bool NeedRParenForLinear = false;
  BalancedDelimiterTracker LinearT(*this, tok::l_paren,
                                   tok::annot_pragma_openmp_end);
  // Handle reduction-identifier for reduction clause.
  if (Kind == OMPC_reduction || Kind == OMPC_task_reduction ||
      Kind == OMPC_in_reduction) {
    Data.ExtraModifier = OMPC_REDUCTION_unknown;
    if (Kind == OMPC_reduction && getLangOpts().OpenMP >= 50 &&
        (Tok.is(tok::identifier) || Tok.is(tok::kw_default)) &&
        NextToken().is(tok::comma)) {
      // Parse optional reduction modifier.
      Data.ExtraModifier =
          getOpenMPSimpleClauseType(Kind, PP.getSpelling(Tok), getLangOpts());
      Data.ExtraModifierLoc = Tok.getLocation();
      ConsumeToken();
      assert(Tok.is(tok::comma) && "Expected comma.");
      (void)ConsumeToken();
    }
    ColonProtectionRAIIObject ColonRAII(*this);
    if (getLangOpts().CPlusPlus)
      ParseOptionalCXXScopeSpecifier(Data.ReductionOrMapperIdScopeSpec,
                                     /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     /*EnteringContext=*/false);
    InvalidReductionId = ParseReductionId(
        *this, Data.ReductionOrMapperIdScopeSpec, UnqualifiedReductionId);
    if (InvalidReductionId) {
      SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }
    if (Tok.is(tok::colon))
      Data.ColonLoc = ConsumeToken();
    else
      Diag(Tok, diag::warn_pragma_expected_colon) << "reduction identifier";
    if (!InvalidReductionId)
      Data.ReductionOrMapperId =
          Actions.GetNameFromUnqualifiedId(UnqualifiedReductionId);
  } else if (Kind == OMPC_depend || Kind == OMPC_doacross) {
    if (getLangOpts().OpenMP >= 50) {
      if (Tok.is(tok::identifier) && PP.getSpelling(Tok) == "iterator") {
        // Handle optional dependence modifier.
        // iterator(iterators-definition)
        // where iterators-definition is iterator-specifier [,
        // iterators-definition ]
        // where iterator-specifier is [ iterator-type ] identifier =
        // range-specification
        HasIterator = true;
        EnterScope(Scope::OpenMPDirectiveScope | Scope::DeclScope);
        ExprResult IteratorRes = ParseOpenMPIteratorsExpr();
        Data.DepModOrTailExpr = IteratorRes.get();
        // Parse ','
        ExpectAndConsume(tok::comma);
      }
    }
    // Handle dependency type for depend clause.
    ColonProtectionRAIIObject ColonRAII(*this);
    Data.ExtraModifier = getOpenMPSimpleClauseType(
        Kind, Tok.is(tok::identifier) ? PP.getSpelling(Tok) : "",
        getLangOpts());
    Data.ExtraModifierLoc = Tok.getLocation();
    if ((Kind == OMPC_depend && Data.ExtraModifier == OMPC_DEPEND_unknown) ||
        (Kind == OMPC_doacross &&
         Data.ExtraModifier == OMPC_DOACROSS_unknown)) {
      SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    } else {
      ConsumeToken();
      // Special processing for depend(source) clause.
      if (DKind == OMPD_ordered && Kind == OMPC_depend &&
          Data.ExtraModifier == OMPC_DEPEND_source) {
        // Parse ')'.
        T.consumeClose();
        return false;
      }
    }
    if (Tok.is(tok::colon)) {
      Data.ColonLoc = ConsumeToken();
    } else if (Kind != OMPC_doacross || Tok.isNot(tok::r_paren)) {
      Diag(Tok, DKind == OMPD_ordered ? diag::warn_pragma_expected_colon_r_paren
                                      : diag::warn_pragma_expected_colon)
          << (Kind == OMPC_depend ? "dependency type" : "dependence-type");
    }
    if (Kind == OMPC_doacross) {
      if (Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo()->isStr("omp_cur_iteration")) {
        Data.ExtraModifier = Data.ExtraModifier == OMPC_DOACROSS_source
                                 ? OMPC_DOACROSS_source_omp_cur_iteration
                                 : OMPC_DOACROSS_sink_omp_cur_iteration;
        ConsumeToken();
      }
      if (Data.ExtraModifier == OMPC_DOACROSS_sink_omp_cur_iteration) {
        if (Tok.isNot(tok::minus)) {
          Diag(Tok, diag::err_omp_sink_and_source_iteration_not_allowd)
              << getOpenMPClauseName(Kind) << 0 << 0;
          SkipUntil(tok::r_paren);
          return false;
        } else {
          ConsumeToken();
          SourceLocation Loc = Tok.getLocation();
          uint64_t Value = 0;
          if (Tok.isNot(tok::numeric_constant) ||
              (PP.parseSimpleIntegerLiteral(Tok, Value) && Value != 1)) {
            Diag(Loc, diag::err_omp_sink_and_source_iteration_not_allowd)
                << getOpenMPClauseName(Kind) << 0 << 0;
            SkipUntil(tok::r_paren);
            return false;
          }
        }
      }
      if (Data.ExtraModifier == OMPC_DOACROSS_source_omp_cur_iteration) {
        if (Tok.isNot(tok::r_paren)) {
          Diag(Tok, diag::err_omp_sink_and_source_iteration_not_allowd)
              << getOpenMPClauseName(Kind) << 1 << 1;
          SkipUntil(tok::r_paren);
          return false;
        }
      }
      // Only the 'sink' case has the expression list.
      if (Kind == OMPC_doacross &&
          (Data.ExtraModifier == OMPC_DOACROSS_source ||
           Data.ExtraModifier == OMPC_DOACROSS_source_omp_cur_iteration ||
           Data.ExtraModifier == OMPC_DOACROSS_sink_omp_cur_iteration)) {
        // Parse ')'.
        T.consumeClose();
        return false;
      }
    }
  } else if (Kind == OMPC_linear) {
    // Try to parse modifier if any.
    Data.ExtraModifier = OMPC_LINEAR_val;
    if (Tok.is(tok::identifier) && PP.LookAhead(0).is(tok::l_paren)) {
      Data.ExtraModifier =
          getOpenMPSimpleClauseType(Kind, PP.getSpelling(Tok), getLangOpts());
      Data.ExtraModifierLoc = ConsumeToken();
      LinearT.consumeOpen();
      NeedRParenForLinear = true;
      if (getLangOpts().OpenMP >= 52)
        Diag(Data.ExtraModifierLoc, diag::err_omp_deprecate_old_syntax)
            << "linear-modifier(list)" << getOpenMPClauseName(Kind)
            << "linear(list: [linear-modifier,] step(step-size))";
    }
  } else if (Kind == OMPC_lastprivate) {
    // Try to parse modifier if any.
    Data.ExtraModifier = OMPC_LASTPRIVATE_unknown;
    // Conditional modifier allowed only in OpenMP 5.0 and not supported in
    // distribute and taskloop based directives.
    if ((getLangOpts().OpenMP >= 50 && !isOpenMPDistributeDirective(DKind) &&
         !isOpenMPTaskLoopDirective(DKind)) &&
        Tok.is(tok::identifier) && PP.LookAhead(0).is(tok::colon)) {
      Data.ExtraModifier =
          getOpenMPSimpleClauseType(Kind, PP.getSpelling(Tok), getLangOpts());
      Data.ExtraModifierLoc = Tok.getLocation();
      ConsumeToken();
      assert(Tok.is(tok::colon) && "Expected colon.");
      Data.ColonLoc = ConsumeToken();
    }
  } else if (Kind == OMPC_map) {
    // Handle optional iterator map modifier.
    if (Tok.is(tok::identifier) && PP.getSpelling(Tok) == "iterator") {
      HasIterator = true;
      EnterScope(Scope::OpenMPDirectiveScope | Scope::DeclScope);
      Data.MapTypeModifiers.push_back(OMPC_MAP_MODIFIER_iterator);
      Data.MapTypeModifiersLoc.push_back(Tok.getLocation());
      ExprResult IteratorRes = ParseOpenMPIteratorsExpr();
      Data.IteratorExpr = IteratorRes.get();
      // Parse ','
      ExpectAndConsume(tok::comma);
      if (getLangOpts().OpenMP < 52) {
        Diag(Tok, diag::err_omp_unknown_map_type_modifier)
            << (getLangOpts().OpenMP >= 51 ? 1 : 0)
            << getLangOpts().OpenMPExtensions;
        InvalidIterator = true;
      }
    }
    // Handle map type for map clause.
    ColonProtectionRAIIObject ColonRAII(*this);

    // The first identifier may be a list item, a map-type or a
    // map-type-modifier. The map-type can also be delete which has the same
    // spelling of the C++ delete keyword.
    Data.ExtraModifier = OMPC_MAP_unknown;
    Data.ExtraModifierLoc = Tok.getLocation();

    // Check for presence of a colon in the map clause.
    TentativeParsingAction TPA(*this);
    bool ColonPresent = false;
    if (SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                  StopBeforeMatch)) {
      if (Tok.is(tok::colon))
        ColonPresent = true;
    }
    TPA.Revert();
    // Only parse map-type-modifier[s] and map-type if a colon is present in
    // the map clause.
    if (ColonPresent) {
      if (getLangOpts().OpenMP >= 60 && getCurToken().is(tok::colon))
        Diag(Tok, diag::err_omp_map_modifier_specification_list);
      IsInvalidMapperModifier = parseMapTypeModifiers(Data);
      if (getLangOpts().OpenMP < 60 && !IsInvalidMapperModifier)
        parseMapType(*this, Data);
      else
        SkipUntil(tok::colon, tok::annot_pragma_openmp_end, StopBeforeMatch);
    }
    if (Data.ExtraModifier == OMPC_MAP_unknown) {
      Data.ExtraModifier = OMPC_MAP_tofrom;
      if (getLangOpts().OpenMP >= 52) {
        if (DKind == OMPD_target_enter_data)
          Data.ExtraModifier = OMPC_MAP_to;
        else if (DKind == OMPD_target_exit_data)
          Data.ExtraModifier = OMPC_MAP_from;
      }
      Data.IsMapTypeImplicit = true;
    }

    if (Tok.is(tok::colon))
      Data.ColonLoc = ConsumeToken();
  } else if (Kind == OMPC_to || Kind == OMPC_from) {
    while (Tok.is(tok::identifier)) {
      auto Modifier = static_cast<OpenMPMotionModifierKind>(
          getOpenMPSimpleClauseType(Kind, PP.getSpelling(Tok), getLangOpts()));
      if (Modifier == OMPC_MOTION_MODIFIER_unknown)
        break;
      Data.MotionModifiers.push_back(Modifier);
      Data.MotionModifiersLoc.push_back(Tok.getLocation());
      ConsumeToken();
      if (Modifier == OMPC_MOTION_MODIFIER_mapper) {
        IsInvalidMapperModifier = parseMapperModifier(Data);
        if (IsInvalidMapperModifier)
          break;
      }
      // OpenMP < 5.1 doesn't permit a ',' or additional modifiers.
      if (getLangOpts().OpenMP < 51)
        break;
      // OpenMP 5.1 accepts an optional ',' even if the next character is ':'.
      // TODO: Is that intentional?
      if (Tok.is(tok::comma))
        ConsumeToken();
    }
    if (!Data.MotionModifiers.empty() && Tok.isNot(tok::colon)) {
      if (!IsInvalidMapperModifier) {
        if (getLangOpts().OpenMP < 51)
          Diag(Tok, diag::warn_pragma_expected_colon) << ")";
        else
          Diag(Tok, diag::warn_pragma_expected_colon) << "motion modifier";
      }
      SkipUntil(tok::colon, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }
    // OpenMP 5.1 permits a ':' even without a preceding modifier.  TODO: Is
    // that intentional?
    if ((!Data.MotionModifiers.empty() || getLangOpts().OpenMP >= 51) &&
        Tok.is(tok::colon))
      Data.ColonLoc = ConsumeToken();
  } else if (Kind == OMPC_allocate ||
             (Kind == OMPC_affinity && Tok.is(tok::identifier) &&
              PP.getSpelling(Tok) == "iterator")) {
    // Handle optional allocator expression followed by colon delimiter.
    ColonProtectionRAIIObject ColonRAII(*this);
    TentativeParsingAction TPA(*this);
    // OpenMP 5.0, 2.10.1, task Construct.
    // where aff-modifier is one of the following:
    // iterator(iterators-definition)
    ExprResult Tail;
    if (Kind == OMPC_allocate) {
      Tail = ParseAssignmentExpression();
    } else {
      HasIterator = true;
      EnterScope(Scope::OpenMPDirectiveScope | Scope::DeclScope);
      Tail = ParseOpenMPIteratorsExpr();
    }
    Tail = Actions.CorrectDelayedTyposInExpr(Tail);
    Tail = Actions.ActOnFinishFullExpr(Tail.get(), T.getOpenLocation(),
                                       /*DiscardedValue=*/false);
    if (Tail.isUsable()) {
      if (Tok.is(tok::colon)) {
        Data.DepModOrTailExpr = Tail.get();
        Data.ColonLoc = ConsumeToken();
        TPA.Commit();
      } else {
        // Colon not found, parse only list of variables.
        TPA.Revert();
      }
    } else {
      // Parsing was unsuccessfull, revert and skip to the end of clause or
      // directive.
      TPA.Revert();
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                StopBeforeMatch);
    }
  } else if (Kind == OMPC_adjust_args) {
    // Handle adjust-op for adjust_args clause.
    ColonProtectionRAIIObject ColonRAII(*this);
    Data.ExtraModifier = getOpenMPSimpleClauseType(
        Kind, Tok.is(tok::identifier) ? PP.getSpelling(Tok) : "",
        getLangOpts());
    Data.ExtraModifierLoc = Tok.getLocation();
    if (Data.ExtraModifier == OMPC_ADJUST_ARGS_unknown) {
      Diag(Tok, diag::err_omp_unknown_adjust_args_op);
      SkipUntil(tok::r_paren, tok::annot_pragma_openmp_end, StopBeforeMatch);
    } else {
      ConsumeToken();
      if (Tok.is(tok::colon))
        Data.ColonLoc = Tok.getLocation();
      ExpectAndConsume(tok::colon, diag::warn_pragma_expected_colon,
                       "adjust-op");
    }
  }

  bool IsComma =
      (Kind != OMPC_reduction && Kind != OMPC_task_reduction &&
       Kind != OMPC_in_reduction && Kind != OMPC_depend &&
       Kind != OMPC_doacross && Kind != OMPC_map && Kind != OMPC_adjust_args) ||
      (Kind == OMPC_reduction && !InvalidReductionId) ||
      (Kind == OMPC_map && Data.ExtraModifier != OMPC_MAP_unknown) ||
      (Kind == OMPC_depend && Data.ExtraModifier != OMPC_DEPEND_unknown) ||
      (Kind == OMPC_doacross && Data.ExtraModifier != OMPC_DOACROSS_unknown) ||
      (Kind == OMPC_adjust_args &&
       Data.ExtraModifier != OMPC_ADJUST_ARGS_unknown);
  const bool MayHaveTail = (Kind == OMPC_linear || Kind == OMPC_aligned);
  while (IsComma || (Tok.isNot(tok::r_paren) && Tok.isNot(tok::colon) &&
                     Tok.isNot(tok::annot_pragma_openmp_end))) {
    ParseScope OMPListScope(this, Scope::OpenMPDirectiveScope);
    ColonProtectionRAIIObject ColonRAII(*this, MayHaveTail);
    if (!ParseOpenMPReservedLocator(Kind, Data, getLangOpts())) {
      // Parse variable
      ExprResult VarExpr =
          Actions.CorrectDelayedTyposInExpr(ParseAssignmentExpression());
      if (VarExpr.isUsable()) {
        Vars.push_back(VarExpr.get());
      } else {
        SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                  StopBeforeMatch);
      }
    }
    // Skip ',' if any
    IsComma = Tok.is(tok::comma);
    if (IsComma)
      ConsumeToken();
    else if (Tok.isNot(tok::r_paren) &&
             Tok.isNot(tok::annot_pragma_openmp_end) &&
             (!MayHaveTail || Tok.isNot(tok::colon)))
      Diag(Tok, diag::err_omp_expected_punc)
          << ((Kind == OMPC_flush) ? getOpenMPDirectiveName(OMPD_flush)
                                   : getOpenMPClauseName(Kind))
          << (Kind == OMPC_flush);
  }

  // Parse ')' for linear clause with modifier.
  if (NeedRParenForLinear)
    LinearT.consumeClose();

  // Parse ':' linear modifiers (val, uval, ref or step(step-size))
  // or parse ':' alignment.
  const bool MustHaveTail = MayHaveTail && Tok.is(tok::colon);
  bool StepFound = false;
  bool ModifierFound = false;
  if (MustHaveTail) {
    Data.ColonLoc = Tok.getLocation();
    SourceLocation ELoc = ConsumeToken();

    if (getLangOpts().OpenMP >= 52 && Kind == OMPC_linear) {
      while (Tok.isNot(tok::r_paren)) {
        if (Tok.is(tok::identifier)) {
          // identifier could be a linear kind (val, uval, ref) or step
          // modifier or step size
          OpenMPLinearClauseKind LinKind =
              static_cast<OpenMPLinearClauseKind>(getOpenMPSimpleClauseType(
                  Kind, Tok.isAnnotation() ? "" : PP.getSpelling(Tok),
                  getLangOpts()));

          if (LinKind == OMPC_LINEAR_step) {
            if (StepFound)
              Diag(Tok, diag::err_omp_multiple_step_or_linear_modifier) << 0;

            BalancedDelimiterTracker StepT(*this, tok::l_paren,
                                           tok::annot_pragma_openmp_end);
            SourceLocation StepModifierLoc = ConsumeToken();
            // parse '('
            if (StepT.consumeOpen())
              Diag(StepModifierLoc, diag::err_expected_lparen_after) << "step";

            // parse step size expression
            StepFound = parseStepSize(*this, Data, Kind, Tok.getLocation());
            if (StepFound)
              Data.StepModifierLoc = StepModifierLoc;

            // parse ')'
            StepT.consumeClose();
          } else if (LinKind >= 0 && LinKind < OMPC_LINEAR_step) {
            if (ModifierFound)
              Diag(Tok, diag::err_omp_multiple_step_or_linear_modifier) << 1;

            Data.ExtraModifier = LinKind;
            Data.ExtraModifierLoc = ConsumeToken();
            ModifierFound = true;
          } else {
            StepFound = parseStepSize(*this, Data, Kind, Tok.getLocation());
          }
        } else {
          // parse an integer expression as step size
          StepFound = parseStepSize(*this, Data, Kind, Tok.getLocation());
        }

        if (Tok.is(tok::comma))
          ConsumeToken();
        if (Tok.is(tok::r_paren) || Tok.is(tok::annot_pragma_openmp_end))
          break;
      }
      if (!StepFound && !ModifierFound)
        Diag(ELoc, diag::err_expected_expression);
    } else {
      // for OMPC_aligned and OMPC_linear (with OpenMP <= 5.1)
      ExprResult Tail = ParseAssignmentExpression();
      Tail = Actions.ActOnFinishFullExpr(Tail.get(), ELoc,
                                         /*DiscardedValue*/ false);
      if (Tail.isUsable())
        Data.DepModOrTailExpr = Tail.get();
      else
        SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openmp_end,
                  StopBeforeMatch);
    }
  }

  // Parse ')'.
  Data.RLoc = Tok.getLocation();
  if (!T.consumeClose())
    Data.RLoc = T.getCloseLocation();
  // Exit from scope when the iterator is used in depend clause.
  if (HasIterator)
    ExitScope();
  return (Kind != OMPC_depend && Kind != OMPC_doacross && Kind != OMPC_map &&
          Vars.empty()) ||
         (MustHaveTail && !Data.DepModOrTailExpr && StepFound) ||
         InvalidReductionId || IsInvalidMapperModifier || InvalidIterator;
}

/// Parsing of OpenMP clause 'private', 'firstprivate', 'lastprivate',
/// 'shared', 'copyin', 'copyprivate', 'flush', 'reduction', 'task_reduction',
/// 'in_reduction', 'nontemporal', 'exclusive' or 'inclusive'.
///
///    private-clause:
///       'private' '(' list ')'
///    firstprivate-clause:
///       'firstprivate' '(' list ')'
///    lastprivate-clause:
///       'lastprivate' '(' list ')'
///    shared-clause:
///       'shared' '(' list ')'
///    linear-clause:
///       'linear' '(' linear-list [ ':' linear-step ] ')'
///    aligned-clause:
///       'aligned' '(' list [ ':' alignment ] ')'
///    reduction-clause:
///       'reduction' '(' [ modifier ',' ] reduction-identifier ':' list ')'
///    task_reduction-clause:
///       'task_reduction' '(' reduction-identifier ':' list ')'
///    in_reduction-clause:
///       'in_reduction' '(' reduction-identifier ':' list ')'
///    copyprivate-clause:
///       'copyprivate' '(' list ')'
///    flush-clause:
///       'flush' '(' list ')'
///    depend-clause:
///       'depend' '(' in | out | inout : list | source ')'
///    map-clause:
///       'map' '(' [ [ always [,] ] [ close [,] ]
///          [ mapper '(' mapper-identifier ')' [,] ]
///          to | from | tofrom | alloc | release | delete ':' ] list ')';
///    to-clause:
///       'to' '(' [ mapper '(' mapper-identifier ')' ':' ] list ')'
///    from-clause:
///       'from' '(' [ mapper '(' mapper-identifier ')' ':' ] list ')'
///    use_device_ptr-clause:
///       'use_device_ptr' '(' list ')'
///    use_device_addr-clause:
///       'use_device_addr' '(' list ')'
///    is_device_ptr-clause:
///       'is_device_ptr' '(' list ')'
///    has_device_addr-clause:
///       'has_device_addr' '(' list ')'
///    allocate-clause:
///       'allocate' '(' [ allocator ':' ] list ')'
///    nontemporal-clause:
///       'nontemporal' '(' list ')'
///    inclusive-clause:
///       'inclusive' '(' list ')'
///    exclusive-clause:
///       'exclusive' '(' list ')'
///
/// For 'linear' clause linear-list may have the following forms:
///  list
///  modifier(list)
/// where modifier is 'val' (C) or 'ref', 'val' or 'uval'(C++).
OMPClause *Parser::ParseOpenMPVarListClause(OpenMPDirectiveKind DKind,
                                            OpenMPClauseKind Kind,
                                            bool ParseOnly) {
  SourceLocation Loc = Tok.getLocation();
  SourceLocation LOpen = ConsumeToken();
  SmallVector<Expr *, 4> Vars;
  SemaOpenMP::OpenMPVarListDataTy Data;

  if (ParseOpenMPVarList(DKind, Kind, Vars, Data))
    return nullptr;

  if (ParseOnly)
    return nullptr;
  OMPVarListLocTy Locs(Loc, LOpen, Data.RLoc);
  return Actions.OpenMP().ActOnOpenMPVarListClause(Kind, Vars, Locs, Data);
}

bool Parser::ParseOpenMPExprListClause(OpenMPClauseKind Kind,
                                       SourceLocation &ClauseNameLoc,
                                       SourceLocation &OpenLoc,
                                       SourceLocation &CloseLoc,
                                       SmallVectorImpl<Expr *> &Exprs,
                                       bool ReqIntConst) {
  assert(getOpenMPClauseName(Kind) == PP.getSpelling(Tok) &&
         "Expected parsing to start at clause name");
  ClauseNameLoc = ConsumeToken();

  // Parse inside of '(' and ')'.
  BalancedDelimiterTracker T(*this, tok::l_paren, tok::annot_pragma_openmp_end);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_paren;
    return true;
  }

  // Parse the list with interleaved commas.
  do {
    ExprResult Val =
        ReqIntConst ? ParseConstantExpression() : ParseAssignmentExpression();
    if (!Val.isUsable()) {
      // Encountered something other than an expression; abort to ')'.
      T.skipToEnd();
      return true;
    }
    Exprs.push_back(Val.get());
  } while (TryConsumeToken(tok::comma));

  bool Result = T.consumeClose();
  OpenLoc = T.getOpenLocation();
  CloseLoc = T.getCloseLocation();
  return Result;
}
