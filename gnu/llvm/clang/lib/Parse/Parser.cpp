//===--- Parser.cpp - C Language Family Parser ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Parser interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTLambda.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/Basic/FileManager.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/ParsedTemplate.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaCodeCompletion.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TimeProfiler.h"
using namespace clang;


namespace {
/// A comment handler that passes comments found by the preprocessor
/// to the parser action.
class ActionCommentHandler : public CommentHandler {
  Sema &S;

public:
  explicit ActionCommentHandler(Sema &S) : S(S) { }

  bool HandleComment(Preprocessor &PP, SourceRange Comment) override {
    S.ActOnComment(Comment);
    return false;
  }
};
} // end anonymous namespace

IdentifierInfo *Parser::getSEHExceptKeyword() {
  // __except is accepted as a (contextual) keyword
  if (!Ident__except && (getLangOpts().MicrosoftExt || getLangOpts().Borland))
    Ident__except = PP.getIdentifierInfo("__except");

  return Ident__except;
}

Parser::Parser(Preprocessor &pp, Sema &actions, bool skipFunctionBodies)
    : PP(pp), PreferredType(pp.isCodeCompletionEnabled()), Actions(actions),
      Diags(PP.getDiagnostics()), GreaterThanIsOperator(true),
      ColonIsSacred(false), InMessageExpression(false),
      TemplateParameterDepth(0), ParsingInObjCContainer(false) {
  SkipFunctionBodies = pp.isCodeCompletionEnabled() || skipFunctionBodies;
  Tok.startToken();
  Tok.setKind(tok::eof);
  Actions.CurScope = nullptr;
  NumCachedScopes = 0;
  CurParsedObjCImpl = nullptr;

  // Add #pragma handlers. These are removed and destroyed in the
  // destructor.
  initializePragmaHandlers();

  CommentSemaHandler.reset(new ActionCommentHandler(actions));
  PP.addCommentHandler(CommentSemaHandler.get());

  PP.setCodeCompletionHandler(*this);

  Actions.ParseTypeFromStringCallback =
      [this](StringRef TypeStr, StringRef Context, SourceLocation IncludeLoc) {
        return this->ParseTypeFromString(TypeStr, Context, IncludeLoc);
      };
}

DiagnosticBuilder Parser::Diag(SourceLocation Loc, unsigned DiagID) {
  return Diags.Report(Loc, DiagID);
}

DiagnosticBuilder Parser::Diag(const Token &Tok, unsigned DiagID) {
  return Diag(Tok.getLocation(), DiagID);
}

/// Emits a diagnostic suggesting parentheses surrounding a
/// given range.
///
/// \param Loc The location where we'll emit the diagnostic.
/// \param DK The kind of diagnostic to emit.
/// \param ParenRange Source range enclosing code that should be parenthesized.
void Parser::SuggestParentheses(SourceLocation Loc, unsigned DK,
                                SourceRange ParenRange) {
  SourceLocation EndLoc = PP.getLocForEndOfToken(ParenRange.getEnd());
  if (!ParenRange.getEnd().isFileID() || EndLoc.isInvalid()) {
    // We can't display the parentheses, so just dig the
    // warning/error and return.
    Diag(Loc, DK);
    return;
  }

  Diag(Loc, DK)
    << FixItHint::CreateInsertion(ParenRange.getBegin(), "(")
    << FixItHint::CreateInsertion(EndLoc, ")");
}

static bool IsCommonTypo(tok::TokenKind ExpectedTok, const Token &Tok) {
  switch (ExpectedTok) {
  case tok::semi:
    return Tok.is(tok::colon) || Tok.is(tok::comma); // : or , for ;
  default: return false;
  }
}

bool Parser::ExpectAndConsume(tok::TokenKind ExpectedTok, unsigned DiagID,
                              StringRef Msg) {
  if (Tok.is(ExpectedTok) || Tok.is(tok::code_completion)) {
    ConsumeAnyToken();
    return false;
  }

  // Detect common single-character typos and resume.
  if (IsCommonTypo(ExpectedTok, Tok)) {
    SourceLocation Loc = Tok.getLocation();
    {
      DiagnosticBuilder DB = Diag(Loc, DiagID);
      DB << FixItHint::CreateReplacement(
                SourceRange(Loc), tok::getPunctuatorSpelling(ExpectedTok));
      if (DiagID == diag::err_expected)
        DB << ExpectedTok;
      else if (DiagID == diag::err_expected_after)
        DB << Msg << ExpectedTok;
      else
        DB << Msg;
    }

    // Pretend there wasn't a problem.
    ConsumeAnyToken();
    return false;
  }

  SourceLocation EndLoc = PP.getLocForEndOfToken(PrevTokLocation);
  const char *Spelling = nullptr;
  if (EndLoc.isValid())
    Spelling = tok::getPunctuatorSpelling(ExpectedTok);

  DiagnosticBuilder DB =
      Spelling
          ? Diag(EndLoc, DiagID) << FixItHint::CreateInsertion(EndLoc, Spelling)
          : Diag(Tok, DiagID);
  if (DiagID == diag::err_expected)
    DB << ExpectedTok;
  else if (DiagID == diag::err_expected_after)
    DB << Msg << ExpectedTok;
  else
    DB << Msg;

  return true;
}

bool Parser::ExpectAndConsumeSemi(unsigned DiagID, StringRef TokenUsed) {
  if (TryConsumeToken(tok::semi))
    return false;

  if (Tok.is(tok::code_completion)) {
    handleUnexpectedCodeCompletionToken();
    return false;
  }

  if ((Tok.is(tok::r_paren) || Tok.is(tok::r_square)) &&
      NextToken().is(tok::semi)) {
    Diag(Tok, diag::err_extraneous_token_before_semi)
      << PP.getSpelling(Tok)
      << FixItHint::CreateRemoval(Tok.getLocation());
    ConsumeAnyToken(); // The ')' or ']'.
    ConsumeToken(); // The ';'.
    return false;
  }

  return ExpectAndConsume(tok::semi, DiagID , TokenUsed);
}

void Parser::ConsumeExtraSemi(ExtraSemiKind Kind, DeclSpec::TST TST) {
  if (!Tok.is(tok::semi)) return;

  bool HadMultipleSemis = false;
  SourceLocation StartLoc = Tok.getLocation();
  SourceLocation EndLoc = Tok.getLocation();
  ConsumeToken();

  while ((Tok.is(tok::semi) && !Tok.isAtStartOfLine())) {
    HadMultipleSemis = true;
    EndLoc = Tok.getLocation();
    ConsumeToken();
  }

  // C++11 allows extra semicolons at namespace scope, but not in any of the
  // other contexts.
  if (Kind == OutsideFunction && getLangOpts().CPlusPlus) {
    if (getLangOpts().CPlusPlus11)
      Diag(StartLoc, diag::warn_cxx98_compat_top_level_semi)
          << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
    else
      Diag(StartLoc, diag::ext_extra_semi_cxx11)
          << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
    return;
  }

  if (Kind != AfterMemberFunctionDefinition || HadMultipleSemis)
    Diag(StartLoc, diag::ext_extra_semi)
        << Kind << DeclSpec::getSpecifierName(TST,
                                    Actions.getASTContext().getPrintingPolicy())
        << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
  else
    // A single semicolon is valid after a member function definition.
    Diag(StartLoc, diag::warn_extra_semi_after_mem_fn_def)
      << FixItHint::CreateRemoval(SourceRange(StartLoc, EndLoc));
}

bool Parser::expectIdentifier() {
  if (Tok.is(tok::identifier))
    return false;
  if (const auto *II = Tok.getIdentifierInfo()) {
    if (II->isCPlusPlusKeyword(getLangOpts())) {
      Diag(Tok, diag::err_expected_token_instead_of_objcxx_keyword)
          << tok::identifier << Tok.getIdentifierInfo();
      // Objective-C++: Recover by treating this keyword as a valid identifier.
      return false;
    }
  }
  Diag(Tok, diag::err_expected) << tok::identifier;
  return true;
}

void Parser::checkCompoundToken(SourceLocation FirstTokLoc,
                                tok::TokenKind FirstTokKind, CompoundToken Op) {
  if (FirstTokLoc.isInvalid())
    return;
  SourceLocation SecondTokLoc = Tok.getLocation();

  // If either token is in a macro, we expect both tokens to come from the same
  // macro expansion.
  if ((FirstTokLoc.isMacroID() || SecondTokLoc.isMacroID()) &&
      PP.getSourceManager().getFileID(FirstTokLoc) !=
          PP.getSourceManager().getFileID(SecondTokLoc)) {
    Diag(FirstTokLoc, diag::warn_compound_token_split_by_macro)
        << (FirstTokKind == Tok.getKind()) << FirstTokKind << Tok.getKind()
        << static_cast<int>(Op) << SourceRange(FirstTokLoc);
    Diag(SecondTokLoc, diag::note_compound_token_split_second_token_here)
        << (FirstTokKind == Tok.getKind()) << Tok.getKind()
        << SourceRange(SecondTokLoc);
    return;
  }

  // We expect the tokens to abut.
  if (Tok.hasLeadingSpace() || Tok.isAtStartOfLine()) {
    SourceLocation SpaceLoc = PP.getLocForEndOfToken(FirstTokLoc);
    if (SpaceLoc.isInvalid())
      SpaceLoc = FirstTokLoc;
    Diag(SpaceLoc, diag::warn_compound_token_split_by_whitespace)
        << (FirstTokKind == Tok.getKind()) << FirstTokKind << Tok.getKind()
        << static_cast<int>(Op) << SourceRange(FirstTokLoc, SecondTokLoc);
    return;
  }
}

//===----------------------------------------------------------------------===//
// Error recovery.
//===----------------------------------------------------------------------===//

static bool HasFlagsSet(Parser::SkipUntilFlags L, Parser::SkipUntilFlags R) {
  return (static_cast<unsigned>(L) & static_cast<unsigned>(R)) != 0;
}

/// SkipUntil - Read tokens until we get to the specified token, then consume
/// it (unless no flag StopBeforeMatch).  Because we cannot guarantee that the
/// token will ever occur, this skips to the next token, or to some likely
/// good stopping point.  If StopAtSemi is true, skipping will stop at a ';'
/// character.
///
/// If SkipUntil finds the specified token, it returns true, otherwise it
/// returns false.
bool Parser::SkipUntil(ArrayRef<tok::TokenKind> Toks, SkipUntilFlags Flags) {
  // We always want this function to skip at least one token if the first token
  // isn't T and if not at EOF.
  bool isFirstTokenSkipped = true;
  while (true) {
    // If we found one of the tokens, stop and return true.
    for (unsigned i = 0, NumToks = Toks.size(); i != NumToks; ++i) {
      if (Tok.is(Toks[i])) {
        if (HasFlagsSet(Flags, StopBeforeMatch)) {
          // Noop, don't consume the token.
        } else {
          ConsumeAnyToken();
        }
        return true;
      }
    }

    // Important special case: The caller has given up and just wants us to
    // skip the rest of the file. Do this without recursing, since we can
    // get here precisely because the caller detected too much recursion.
    if (Toks.size() == 1 && Toks[0] == tok::eof &&
        !HasFlagsSet(Flags, StopAtSemi) &&
        !HasFlagsSet(Flags, StopAtCodeCompletion)) {
      while (Tok.isNot(tok::eof))
        ConsumeAnyToken();
      return true;
    }

    switch (Tok.getKind()) {
    case tok::eof:
      // Ran out of tokens.
      return false;

    case tok::annot_pragma_openmp:
    case tok::annot_attr_openmp:
    case tok::annot_pragma_openmp_end:
      // Stop before an OpenMP pragma boundary.
      if (OpenMPDirectiveParsing)
        return false;
      ConsumeAnnotationToken();
      break;
    case tok::annot_pragma_openacc:
    case tok::annot_pragma_openacc_end:
      // Stop before an OpenACC pragma boundary.
      if (OpenACCDirectiveParsing)
        return false;
      ConsumeAnnotationToken();
      break;
    case tok::annot_module_begin:
    case tok::annot_module_end:
    case tok::annot_module_include:
    case tok::annot_repl_input_end:
      // Stop before we change submodules. They generally indicate a "good"
      // place to pick up parsing again (except in the special case where
      // we're trying to skip to EOF).
      return false;

    case tok::code_completion:
      if (!HasFlagsSet(Flags, StopAtCodeCompletion))
        handleUnexpectedCodeCompletionToken();
      return false;

    case tok::l_paren:
      // Recursively skip properly-nested parens.
      ConsumeParen();
      if (HasFlagsSet(Flags, StopAtCodeCompletion))
        SkipUntil(tok::r_paren, StopAtCodeCompletion);
      else
        SkipUntil(tok::r_paren);
      break;
    case tok::l_square:
      // Recursively skip properly-nested square brackets.
      ConsumeBracket();
      if (HasFlagsSet(Flags, StopAtCodeCompletion))
        SkipUntil(tok::r_square, StopAtCodeCompletion);
      else
        SkipUntil(tok::r_square);
      break;
    case tok::l_brace:
      // Recursively skip properly-nested braces.
      ConsumeBrace();
      if (HasFlagsSet(Flags, StopAtCodeCompletion))
        SkipUntil(tok::r_brace, StopAtCodeCompletion);
      else
        SkipUntil(tok::r_brace);
      break;
    case tok::question:
      // Recursively skip ? ... : pairs; these function as brackets. But
      // still stop at a semicolon if requested.
      ConsumeToken();
      SkipUntil(tok::colon,
                SkipUntilFlags(unsigned(Flags) &
                               unsigned(StopAtCodeCompletion | StopAtSemi)));
      break;

    // Okay, we found a ']' or '}' or ')', which we think should be balanced.
    // Since the user wasn't looking for this token (if they were, it would
    // already be handled), this isn't balanced.  If there is a LHS token at a
    // higher level, we will assume that this matches the unbalanced token
    // and return it.  Otherwise, this is a spurious RHS token, which we skip.
    case tok::r_paren:
      if (ParenCount && !isFirstTokenSkipped)
        return false;  // Matches something.
      ConsumeParen();
      break;
    case tok::r_square:
      if (BracketCount && !isFirstTokenSkipped)
        return false;  // Matches something.
      ConsumeBracket();
      break;
    case tok::r_brace:
      if (BraceCount && !isFirstTokenSkipped)
        return false;  // Matches something.
      ConsumeBrace();
      break;

    case tok::semi:
      if (HasFlagsSet(Flags, StopAtSemi))
        return false;
      [[fallthrough]];
    default:
      // Skip this token.
      ConsumeAnyToken();
      break;
    }
    isFirstTokenSkipped = false;
  }
}

//===----------------------------------------------------------------------===//
// Scope manipulation
//===----------------------------------------------------------------------===//

/// EnterScope - Start a new scope.
void Parser::EnterScope(unsigned ScopeFlags) {
  if (NumCachedScopes) {
    Scope *N = ScopeCache[--NumCachedScopes];
    N->Init(getCurScope(), ScopeFlags);
    Actions.CurScope = N;
  } else {
    Actions.CurScope = new Scope(getCurScope(), ScopeFlags, Diags);
  }
}

/// ExitScope - Pop a scope off the scope stack.
void Parser::ExitScope() {
  assert(getCurScope() && "Scope imbalance!");

  // Inform the actions module that this scope is going away if there are any
  // decls in it.
  Actions.ActOnPopScope(Tok.getLocation(), getCurScope());

  Scope *OldScope = getCurScope();
  Actions.CurScope = OldScope->getParent();

  if (NumCachedScopes == ScopeCacheSize)
    delete OldScope;
  else
    ScopeCache[NumCachedScopes++] = OldScope;
}

/// Set the flags for the current scope to ScopeFlags. If ManageFlags is false,
/// this object does nothing.
Parser::ParseScopeFlags::ParseScopeFlags(Parser *Self, unsigned ScopeFlags,
                                 bool ManageFlags)
  : CurScope(ManageFlags ? Self->getCurScope() : nullptr) {
  if (CurScope) {
    OldFlags = CurScope->getFlags();
    CurScope->setFlags(ScopeFlags);
  }
}

/// Restore the flags for the current scope to what they were before this
/// object overrode them.
Parser::ParseScopeFlags::~ParseScopeFlags() {
  if (CurScope)
    CurScope->setFlags(OldFlags);
}


//===----------------------------------------------------------------------===//
// C99 6.9: External Definitions.
//===----------------------------------------------------------------------===//

Parser::~Parser() {
  // If we still have scopes active, delete the scope tree.
  delete getCurScope();
  Actions.CurScope = nullptr;

  // Free the scope cache.
  for (unsigned i = 0, e = NumCachedScopes; i != e; ++i)
    delete ScopeCache[i];

  resetPragmaHandlers();

  PP.removeCommentHandler(CommentSemaHandler.get());

  PP.clearCodeCompletionHandler();

  DestroyTemplateIds();
}

/// Initialize - Warm up the parser.
///
void Parser::Initialize() {
  // Create the translation unit scope.  Install it as the current scope.
  assert(getCurScope() == nullptr && "A scope is already active?");
  EnterScope(Scope::DeclScope);
  Actions.ActOnTranslationUnitScope(getCurScope());

  // Initialization for Objective-C context sensitive keywords recognition.
  // Referenced in Parser::ParseObjCTypeQualifierList.
  if (getLangOpts().ObjC) {
    ObjCTypeQuals[objc_in] = &PP.getIdentifierTable().get("in");
    ObjCTypeQuals[objc_out] = &PP.getIdentifierTable().get("out");
    ObjCTypeQuals[objc_inout] = &PP.getIdentifierTable().get("inout");
    ObjCTypeQuals[objc_oneway] = &PP.getIdentifierTable().get("oneway");
    ObjCTypeQuals[objc_bycopy] = &PP.getIdentifierTable().get("bycopy");
    ObjCTypeQuals[objc_byref] = &PP.getIdentifierTable().get("byref");
    ObjCTypeQuals[objc_nonnull] = &PP.getIdentifierTable().get("nonnull");
    ObjCTypeQuals[objc_nullable] = &PP.getIdentifierTable().get("nullable");
    ObjCTypeQuals[objc_null_unspecified]
      = &PP.getIdentifierTable().get("null_unspecified");
  }

  Ident_instancetype = nullptr;
  Ident_final = nullptr;
  Ident_sealed = nullptr;
  Ident_abstract = nullptr;
  Ident_override = nullptr;
  Ident_GNU_final = nullptr;
  Ident_import = nullptr;
  Ident_module = nullptr;

  Ident_super = &PP.getIdentifierTable().get("super");

  Ident_vector = nullptr;
  Ident_bool = nullptr;
  Ident_Bool = nullptr;
  Ident_pixel = nullptr;
  if (getLangOpts().AltiVec || getLangOpts().ZVector) {
    Ident_vector = &PP.getIdentifierTable().get("vector");
    Ident_bool = &PP.getIdentifierTable().get("bool");
    Ident_Bool = &PP.getIdentifierTable().get("_Bool");
  }
  if (getLangOpts().AltiVec)
    Ident_pixel = &PP.getIdentifierTable().get("pixel");

  Ident_introduced = nullptr;
  Ident_deprecated = nullptr;
  Ident_obsoleted = nullptr;
  Ident_unavailable = nullptr;
  Ident_strict = nullptr;
  Ident_replacement = nullptr;

  Ident_language = Ident_defined_in = Ident_generated_declaration = Ident_USR =
      nullptr;

  Ident__except = nullptr;

  Ident__exception_code = Ident__exception_info = nullptr;
  Ident__abnormal_termination = Ident___exception_code = nullptr;
  Ident___exception_info = Ident___abnormal_termination = nullptr;
  Ident_GetExceptionCode = Ident_GetExceptionInfo = nullptr;
  Ident_AbnormalTermination = nullptr;

  if(getLangOpts().Borland) {
    Ident__exception_info        = PP.getIdentifierInfo("_exception_info");
    Ident___exception_info       = PP.getIdentifierInfo("__exception_info");
    Ident_GetExceptionInfo       = PP.getIdentifierInfo("GetExceptionInformation");
    Ident__exception_code        = PP.getIdentifierInfo("_exception_code");
    Ident___exception_code       = PP.getIdentifierInfo("__exception_code");
    Ident_GetExceptionCode       = PP.getIdentifierInfo("GetExceptionCode");
    Ident__abnormal_termination  = PP.getIdentifierInfo("_abnormal_termination");
    Ident___abnormal_termination = PP.getIdentifierInfo("__abnormal_termination");
    Ident_AbnormalTermination    = PP.getIdentifierInfo("AbnormalTermination");

    PP.SetPoisonReason(Ident__exception_code,diag::err_seh___except_block);
    PP.SetPoisonReason(Ident___exception_code,diag::err_seh___except_block);
    PP.SetPoisonReason(Ident_GetExceptionCode,diag::err_seh___except_block);
    PP.SetPoisonReason(Ident__exception_info,diag::err_seh___except_filter);
    PP.SetPoisonReason(Ident___exception_info,diag::err_seh___except_filter);
    PP.SetPoisonReason(Ident_GetExceptionInfo,diag::err_seh___except_filter);
    PP.SetPoisonReason(Ident__abnormal_termination,diag::err_seh___finally_block);
    PP.SetPoisonReason(Ident___abnormal_termination,diag::err_seh___finally_block);
    PP.SetPoisonReason(Ident_AbnormalTermination,diag::err_seh___finally_block);
  }

  if (getLangOpts().CPlusPlusModules) {
    Ident_import = PP.getIdentifierInfo("import");
    Ident_module = PP.getIdentifierInfo("module");
  }

  Actions.Initialize();

  // Prime the lexer look-ahead.
  ConsumeToken();
}

void Parser::DestroyTemplateIds() {
  for (TemplateIdAnnotation *Id : TemplateIds)
    Id->Destroy();
  TemplateIds.clear();
}

/// Parse the first top-level declaration in a translation unit.
///
///   translation-unit:
/// [C]     external-declaration
/// [C]     translation-unit external-declaration
/// [C++]   top-level-declaration-seq[opt]
/// [C++20] global-module-fragment[opt] module-declaration
///                 top-level-declaration-seq[opt] private-module-fragment[opt]
///
/// Note that in C, it is an error if there is no first declaration.
bool Parser::ParseFirstTopLevelDecl(DeclGroupPtrTy &Result,
                                    Sema::ModuleImportState &ImportState) {
  Actions.ActOnStartOfTranslationUnit();

  // For C++20 modules, a module decl must be the first in the TU.  We also
  // need to track module imports.
  ImportState = Sema::ModuleImportState::FirstDecl;
  bool NoTopLevelDecls = ParseTopLevelDecl(Result, ImportState);

  // C11 6.9p1 says translation units must have at least one top-level
  // declaration. C++ doesn't have this restriction. We also don't want to
  // complain if we have a precompiled header, although technically if the PCH
  // is empty we should still emit the (pedantic) diagnostic.
  // If the main file is a header, we're only pretending it's a TU; don't warn.
  if (NoTopLevelDecls && !Actions.getASTContext().getExternalSource() &&
      !getLangOpts().CPlusPlus && !getLangOpts().IsHeaderFile)
    Diag(diag::ext_empty_translation_unit);

  return NoTopLevelDecls;
}

/// ParseTopLevelDecl - Parse one top-level declaration, return whatever the
/// action tells us to.  This returns true if the EOF was encountered.
///
///   top-level-declaration:
///           declaration
/// [C++20]   module-import-declaration
bool Parser::ParseTopLevelDecl(DeclGroupPtrTy &Result,
                               Sema::ModuleImportState &ImportState) {
  DestroyTemplateIdAnnotationsRAIIObj CleanupRAII(*this);

  // Skip over the EOF token, flagging end of previous input for incremental
  // processing
  if (PP.isIncrementalProcessingEnabled() && Tok.is(tok::eof))
    ConsumeToken();

  Result = nullptr;
  switch (Tok.getKind()) {
  case tok::annot_pragma_unused:
    HandlePragmaUnused();
    return false;

  case tok::kw_export:
    switch (NextToken().getKind()) {
    case tok::kw_module:
      goto module_decl;

    // Note: no need to handle kw_import here. We only form kw_import under
    // the Standard C++ Modules, and in that case 'export import' is parsed as
    // an export-declaration containing an import-declaration.

    // Recognize context-sensitive C++20 'export module' and 'export import'
    // declarations.
    case tok::identifier: {
      IdentifierInfo *II = NextToken().getIdentifierInfo();
      if ((II == Ident_module || II == Ident_import) &&
          GetLookAheadToken(2).isNot(tok::coloncolon)) {
        if (II == Ident_module)
          goto module_decl;
        else
          goto import_decl;
      }
      break;
    }

    default:
      break;
    }
    break;

  case tok::kw_module:
  module_decl:
    Result = ParseModuleDecl(ImportState);
    return false;

  case tok::kw_import:
  import_decl: {
    Decl *ImportDecl = ParseModuleImport(SourceLocation(), ImportState);
    Result = Actions.ConvertDeclToDeclGroup(ImportDecl);
    return false;
  }

  case tok::annot_module_include: {
    auto Loc = Tok.getLocation();
    Module *Mod = reinterpret_cast<Module *>(Tok.getAnnotationValue());
    // FIXME: We need a better way to disambiguate C++ clang modules and
    // standard C++ modules.
    if (!getLangOpts().CPlusPlusModules || !Mod->isHeaderUnit())
      Actions.ActOnAnnotModuleInclude(Loc, Mod);
    else {
      DeclResult Import =
          Actions.ActOnModuleImport(Loc, SourceLocation(), Loc, Mod);
      Decl *ImportDecl = Import.isInvalid() ? nullptr : Import.get();
      Result = Actions.ConvertDeclToDeclGroup(ImportDecl);
    }
    ConsumeAnnotationToken();
    return false;
  }

  case tok::annot_module_begin:
    Actions.ActOnAnnotModuleBegin(
        Tok.getLocation(),
        reinterpret_cast<Module *>(Tok.getAnnotationValue()));
    ConsumeAnnotationToken();
    ImportState = Sema::ModuleImportState::NotACXX20Module;
    return false;

  case tok::annot_module_end:
    Actions.ActOnAnnotModuleEnd(
        Tok.getLocation(),
        reinterpret_cast<Module *>(Tok.getAnnotationValue()));
    ConsumeAnnotationToken();
    ImportState = Sema::ModuleImportState::NotACXX20Module;
    return false;

  case tok::eof:
  case tok::annot_repl_input_end:
    // Check whether -fmax-tokens= was reached.
    if (PP.getMaxTokens() != 0 && PP.getTokenCount() > PP.getMaxTokens()) {
      PP.Diag(Tok.getLocation(), diag::warn_max_tokens_total)
          << PP.getTokenCount() << PP.getMaxTokens();
      SourceLocation OverrideLoc = PP.getMaxTokensOverrideLoc();
      if (OverrideLoc.isValid()) {
        PP.Diag(OverrideLoc, diag::note_max_tokens_total_override);
      }
    }

    // Late template parsing can begin.
    Actions.SetLateTemplateParser(LateTemplateParserCallback, nullptr, this);
    Actions.ActOnEndOfTranslationUnit();
    //else don't tell Sema that we ended parsing: more input might come.
    return true;

  case tok::identifier:
    // C++2a [basic.link]p3:
    //   A token sequence beginning with 'export[opt] module' or
    //   'export[opt] import' and not immediately followed by '::'
    //   is never interpreted as the declaration of a top-level-declaration.
    if ((Tok.getIdentifierInfo() == Ident_module ||
         Tok.getIdentifierInfo() == Ident_import) &&
        NextToken().isNot(tok::coloncolon)) {
      if (Tok.getIdentifierInfo() == Ident_module)
        goto module_decl;
      else
        goto import_decl;
    }
    break;

  default:
    break;
  }

  ParsedAttributes DeclAttrs(AttrFactory);
  ParsedAttributes DeclSpecAttrs(AttrFactory);
  // GNU attributes are applied to the declaration specification while the
  // standard attributes are applied to the declaration.  We parse the two
  // attribute sets into different containters so we can apply them during
  // the regular parsing process.
  while (MaybeParseCXX11Attributes(DeclAttrs) ||
         MaybeParseGNUAttributes(DeclSpecAttrs))
    ;

  Result = ParseExternalDeclaration(DeclAttrs, DeclSpecAttrs);
  // An empty Result might mean a line with ';' or some parsing error, ignore
  // it.
  if (Result) {
    if (ImportState == Sema::ModuleImportState::FirstDecl)
      // First decl was not modular.
      ImportState = Sema::ModuleImportState::NotACXX20Module;
    else if (ImportState == Sema::ModuleImportState::ImportAllowed)
      // Non-imports disallow further imports.
      ImportState = Sema::ModuleImportState::ImportFinished;
    else if (ImportState ==
             Sema::ModuleImportState::PrivateFragmentImportAllowed)
      // Non-imports disallow further imports.
      ImportState = Sema::ModuleImportState::PrivateFragmentImportFinished;
  }
  return false;
}

/// ParseExternalDeclaration:
///
/// The `Attrs` that are passed in are C++11 attributes and appertain to the
/// declaration.
///
///       external-declaration: [C99 6.9], declaration: [C++ dcl.dcl]
///         function-definition
///         declaration
/// [GNU]   asm-definition
/// [GNU]   __extension__ external-declaration
/// [OBJC]  objc-class-definition
/// [OBJC]  objc-class-declaration
/// [OBJC]  objc-alias-declaration
/// [OBJC]  objc-protocol-definition
/// [OBJC]  objc-method-definition
/// [OBJC]  @end
/// [C++]   linkage-specification
/// [GNU] asm-definition:
///         simple-asm-expr ';'
/// [C++11] empty-declaration
/// [C++11] attribute-declaration
///
/// [C++11] empty-declaration:
///           ';'
///
/// [C++0x/GNU] 'extern' 'template' declaration
///
/// [C++20] module-import-declaration
///
Parser::DeclGroupPtrTy
Parser::ParseExternalDeclaration(ParsedAttributes &Attrs,
                                 ParsedAttributes &DeclSpecAttrs,
                                 ParsingDeclSpec *DS) {
  DestroyTemplateIdAnnotationsRAIIObj CleanupRAII(*this);
  ParenBraceBracketBalancer BalancerRAIIObj(*this);

  if (PP.isCodeCompletionReached()) {
    cutOffParsing();
    return nullptr;
  }

  Decl *SingleDecl = nullptr;
  switch (Tok.getKind()) {
  case tok::annot_pragma_vis:
    HandlePragmaVisibility();
    return nullptr;
  case tok::annot_pragma_pack:
    HandlePragmaPack();
    return nullptr;
  case tok::annot_pragma_msstruct:
    HandlePragmaMSStruct();
    return nullptr;
  case tok::annot_pragma_align:
    HandlePragmaAlign();
    return nullptr;
  case tok::annot_pragma_weak:
    HandlePragmaWeak();
    return nullptr;
  case tok::annot_pragma_weakalias:
    HandlePragmaWeakAlias();
    return nullptr;
  case tok::annot_pragma_redefine_extname:
    HandlePragmaRedefineExtname();
    return nullptr;
  case tok::annot_pragma_fp_contract:
    HandlePragmaFPContract();
    return nullptr;
  case tok::annot_pragma_fenv_access:
  case tok::annot_pragma_fenv_access_ms:
    HandlePragmaFEnvAccess();
    return nullptr;
  case tok::annot_pragma_fenv_round:
    HandlePragmaFEnvRound();
    return nullptr;
  case tok::annot_pragma_cx_limited_range:
    HandlePragmaCXLimitedRange();
    return nullptr;
  case tok::annot_pragma_float_control:
    HandlePragmaFloatControl();
    return nullptr;
  case tok::annot_pragma_fp:
    HandlePragmaFP();
    break;
  case tok::annot_pragma_opencl_extension:
    HandlePragmaOpenCLExtension();
    return nullptr;
  case tok::annot_attr_openmp:
  case tok::annot_pragma_openmp: {
    AccessSpecifier AS = AS_none;
    return ParseOpenMPDeclarativeDirectiveWithExtDecl(AS, Attrs);
  }
  case tok::annot_pragma_openacc:
    return ParseOpenACCDirectiveDecl();
  case tok::annot_pragma_ms_pointers_to_members:
    HandlePragmaMSPointersToMembers();
    return nullptr;
  case tok::annot_pragma_ms_vtordisp:
    HandlePragmaMSVtorDisp();
    return nullptr;
  case tok::annot_pragma_ms_pragma:
    HandlePragmaMSPragma();
    return nullptr;
  case tok::annot_pragma_dump:
    HandlePragmaDump();
    return nullptr;
  case tok::annot_pragma_attribute:
    HandlePragmaAttribute();
    return nullptr;
  case tok::semi:
    // Either a C++11 empty-declaration or attribute-declaration.
    SingleDecl =
        Actions.ActOnEmptyDeclaration(getCurScope(), Attrs, Tok.getLocation());
    ConsumeExtraSemi(OutsideFunction);
    break;
  case tok::r_brace:
    Diag(Tok, diag::err_extraneous_closing_brace);
    ConsumeBrace();
    return nullptr;
  case tok::eof:
    Diag(Tok, diag::err_expected_external_declaration);
    return nullptr;
  case tok::kw___extension__: {
    // __extension__ silences extension warnings in the subexpression.
    ExtensionRAIIObject O(Diags);  // Use RAII to do this.
    ConsumeToken();
    return ParseExternalDeclaration(Attrs, DeclSpecAttrs);
  }
  case tok::kw_asm: {
    ProhibitAttributes(Attrs);

    SourceLocation StartLoc = Tok.getLocation();
    SourceLocation EndLoc;

    ExprResult Result(ParseSimpleAsm(/*ForAsmLabel*/ false, &EndLoc));

    // Check if GNU-style InlineAsm is disabled.
    // Empty asm string is allowed because it will not introduce
    // any assembly code.
    if (!(getLangOpts().GNUAsm || Result.isInvalid())) {
      const auto *SL = cast<StringLiteral>(Result.get());
      if (!SL->getString().trim().empty())
        Diag(StartLoc, diag::err_gnu_inline_asm_disabled);
    }

    ExpectAndConsume(tok::semi, diag::err_expected_after,
                     "top-level asm block");

    if (Result.isInvalid())
      return nullptr;
    SingleDecl = Actions.ActOnFileScopeAsmDecl(Result.get(), StartLoc, EndLoc);
    break;
  }
  case tok::at:
    return ParseObjCAtDirectives(Attrs, DeclSpecAttrs);
  case tok::minus:
  case tok::plus:
    if (!getLangOpts().ObjC) {
      Diag(Tok, diag::err_expected_external_declaration);
      ConsumeToken();
      return nullptr;
    }
    SingleDecl = ParseObjCMethodDefinition();
    break;
  case tok::code_completion:
    cutOffParsing();
    if (CurParsedObjCImpl) {
      // Code-complete Objective-C methods even without leading '-'/'+' prefix.
      Actions.CodeCompletion().CodeCompleteObjCMethodDecl(
          getCurScope(),
          /*IsInstanceMethod=*/std::nullopt,
          /*ReturnType=*/nullptr);
    }

    SemaCodeCompletion::ParserCompletionContext PCC;
    if (CurParsedObjCImpl) {
      PCC = SemaCodeCompletion::PCC_ObjCImplementation;
    } else if (PP.isIncrementalProcessingEnabled()) {
      PCC = SemaCodeCompletion::PCC_TopLevelOrExpression;
    } else {
      PCC = SemaCodeCompletion::PCC_Namespace;
    };
    Actions.CodeCompletion().CodeCompleteOrdinaryName(getCurScope(), PCC);
    return nullptr;
  case tok::kw_import: {
    Sema::ModuleImportState IS = Sema::ModuleImportState::NotACXX20Module;
    if (getLangOpts().CPlusPlusModules) {
      llvm_unreachable("not expecting a c++20 import here");
      ProhibitAttributes(Attrs);
    }
    SingleDecl = ParseModuleImport(SourceLocation(), IS);
  } break;
  case tok::kw_export:
    if (getLangOpts().CPlusPlusModules || getLangOpts().HLSL) {
      ProhibitAttributes(Attrs);
      SingleDecl = ParseExportDeclaration();
      break;
    }
    // This must be 'export template'. Parse it so we can diagnose our lack
    // of support.
    [[fallthrough]];
  case tok::kw_using:
  case tok::kw_namespace:
  case tok::kw_typedef:
  case tok::kw_template:
  case tok::kw_static_assert:
  case tok::kw__Static_assert:
    // A function definition cannot start with any of these keywords.
    {
      SourceLocation DeclEnd;
      return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                              DeclSpecAttrs);
    }

  case tok::kw_cbuffer:
  case tok::kw_tbuffer:
    if (getLangOpts().HLSL) {
      SourceLocation DeclEnd;
      return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                              DeclSpecAttrs);
    }
    goto dont_know;

  case tok::kw_static:
    // Parse (then ignore) 'static' prior to a template instantiation. This is
    // a GCC extension that we intentionally do not support.
    if (getLangOpts().CPlusPlus && NextToken().is(tok::kw_template)) {
      Diag(ConsumeToken(), diag::warn_static_inline_explicit_inst_ignored)
        << 0;
      SourceLocation DeclEnd;
      return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                              DeclSpecAttrs);
    }
    goto dont_know;

  case tok::kw_inline:
    if (getLangOpts().CPlusPlus) {
      tok::TokenKind NextKind = NextToken().getKind();

      // Inline namespaces. Allowed as an extension even in C++03.
      if (NextKind == tok::kw_namespace) {
        SourceLocation DeclEnd;
        return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                                DeclSpecAttrs);
      }

      // Parse (then ignore) 'inline' prior to a template instantiation. This is
      // a GCC extension that we intentionally do not support.
      if (NextKind == tok::kw_template) {
        Diag(ConsumeToken(), diag::warn_static_inline_explicit_inst_ignored)
          << 1;
        SourceLocation DeclEnd;
        return ParseDeclaration(DeclaratorContext::File, DeclEnd, Attrs,
                                DeclSpecAttrs);
      }
    }
    goto dont_know;

  case tok::kw_extern:
    if (getLangOpts().CPlusPlus && NextToken().is(tok::kw_template)) {
      // Extern templates
      SourceLocation ExternLoc = ConsumeToken();
      SourceLocation TemplateLoc = ConsumeToken();
      Diag(ExternLoc, getLangOpts().CPlusPlus11 ?
             diag::warn_cxx98_compat_extern_template :
             diag::ext_extern_template) << SourceRange(ExternLoc, TemplateLoc);
      SourceLocation DeclEnd;
      return ParseExplicitInstantiation(DeclaratorContext::File, ExternLoc,
                                        TemplateLoc, DeclEnd, Attrs);
    }
    goto dont_know;

  case tok::kw___if_exists:
  case tok::kw___if_not_exists:
    ParseMicrosoftIfExistsExternalDeclaration();
    return nullptr;

  case tok::kw_module:
    Diag(Tok, diag::err_unexpected_module_decl);
    SkipUntil(tok::semi);
    return nullptr;

  default:
  dont_know:
    if (Tok.isEditorPlaceholder()) {
      ConsumeToken();
      return nullptr;
    }
    if (getLangOpts().IncrementalExtensions &&
        !isDeclarationStatement(/*DisambiguatingWithExpression=*/true))
      return ParseTopLevelStmtDecl();

    // We can't tell whether this is a function-definition or declaration yet.
    if (!SingleDecl)
      return ParseDeclarationOrFunctionDefinition(Attrs, DeclSpecAttrs, DS);
  }

  // This routine returns a DeclGroup, if the thing we parsed only contains a
  // single decl, convert it now.
  return Actions.ConvertDeclToDeclGroup(SingleDecl);
}

/// Determine whether the current token, if it occurs after a
/// declarator, continues a declaration or declaration list.
bool Parser::isDeclarationAfterDeclarator() {
  // Check for '= delete' or '= default'
  if (getLangOpts().CPlusPlus && Tok.is(tok::equal)) {
    const Token &KW = NextToken();
    if (KW.is(tok::kw_default) || KW.is(tok::kw_delete))
      return false;
  }

  return Tok.is(tok::equal) ||      // int X()=  -> not a function def
    Tok.is(tok::comma) ||           // int X(),  -> not a function def
    Tok.is(tok::semi)  ||           // int X();  -> not a function def
    Tok.is(tok::kw_asm) ||          // int X() __asm__ -> not a function def
    Tok.is(tok::kw___attribute) ||  // int X() __attr__ -> not a function def
    (getLangOpts().CPlusPlus &&
     Tok.is(tok::l_paren));         // int X(0) -> not a function def [C++]
}

/// Determine whether the current token, if it occurs after a
/// declarator, indicates the start of a function definition.
bool Parser::isStartOfFunctionDefinition(const ParsingDeclarator &Declarator) {
  assert(Declarator.isFunctionDeclarator() && "Isn't a function declarator");
  if (Tok.is(tok::l_brace))   // int X() {}
    return true;

  // Handle K&R C argument lists: int X(f) int f; {}
  if (!getLangOpts().CPlusPlus &&
      Declarator.getFunctionTypeInfo().isKNRPrototype())
    return isDeclarationSpecifier(ImplicitTypenameContext::No);

  if (getLangOpts().CPlusPlus && Tok.is(tok::equal)) {
    const Token &KW = NextToken();
    return KW.is(tok::kw_default) || KW.is(tok::kw_delete);
  }

  return Tok.is(tok::colon) ||         // X() : Base() {} (used for ctors)
         Tok.is(tok::kw_try);          // X() try { ... }
}

/// Parse either a function-definition or a declaration.  We can't tell which
/// we have until we read up to the compound-statement in function-definition.
/// TemplateParams, if non-NULL, provides the template parameters when we're
/// parsing a C++ template-declaration.
///
///       function-definition: [C99 6.9.1]
///         decl-specs      declarator declaration-list[opt] compound-statement
/// [C90] function-definition: [C99 6.7.1] - implicit int result
/// [C90]   decl-specs[opt] declarator declaration-list[opt] compound-statement
///
///       declaration: [C99 6.7]
///         declaration-specifiers init-declarator-list[opt] ';'
/// [!C99]  init-declarator-list ';'                   [TODO: warn in c99 mode]
/// [OMP]   threadprivate-directive
/// [OMP]   allocate-directive                         [TODO]
///
Parser::DeclGroupPtrTy Parser::ParseDeclOrFunctionDefInternal(
    ParsedAttributes &Attrs, ParsedAttributes &DeclSpecAttrs,
    ParsingDeclSpec &DS, AccessSpecifier AS) {
  // Because we assume that the DeclSpec has not yet been initialised, we simply
  // overwrite the source range and attribute the provided leading declspec
  // attributes.
  assert(DS.getSourceRange().isInvalid() &&
         "expected uninitialised source range");
  DS.SetRangeStart(DeclSpecAttrs.Range.getBegin());
  DS.SetRangeEnd(DeclSpecAttrs.Range.getEnd());
  DS.takeAttributesFrom(DeclSpecAttrs);

  ParsedTemplateInfo TemplateInfo;
  MaybeParseMicrosoftAttributes(DS.getAttributes());
  // Parse the common declaration-specifiers piece.
  ParseDeclarationSpecifiers(DS, TemplateInfo, AS,
                             DeclSpecContext::DSC_top_level);

  // If we had a free-standing type definition with a missing semicolon, we
  // may get this far before the problem becomes obvious.
  if (DS.hasTagDefinition() && DiagnoseMissingSemiAfterTagDefinition(
                                   DS, AS, DeclSpecContext::DSC_top_level))
    return nullptr;

  // C99 6.7.2.3p6: Handle "struct-or-union identifier;", "enum { X };"
  // declaration-specifiers init-declarator-list[opt] ';'
  if (Tok.is(tok::semi)) {
    auto LengthOfTSTToken = [](DeclSpec::TST TKind) {
      assert(DeclSpec::isDeclRep(TKind));
      switch(TKind) {
      case DeclSpec::TST_class:
        return 5;
      case DeclSpec::TST_struct:
        return 6;
      case DeclSpec::TST_union:
        return 5;
      case DeclSpec::TST_enum:
        return 4;
      case DeclSpec::TST_interface:
        return 9;
      default:
        llvm_unreachable("we only expect to get the length of the class/struct/union/enum");
      }

    };
    // Suggest correct location to fix '[[attrib]] struct' to 'struct [[attrib]]'
    SourceLocation CorrectLocationForAttributes =
        DeclSpec::isDeclRep(DS.getTypeSpecType())
            ? DS.getTypeSpecTypeLoc().getLocWithOffset(
                  LengthOfTSTToken(DS.getTypeSpecType()))
            : SourceLocation();
    ProhibitAttributes(Attrs, CorrectLocationForAttributes);
    ConsumeToken();
    RecordDecl *AnonRecord = nullptr;
    Decl *TheDecl = Actions.ParsedFreeStandingDeclSpec(
        getCurScope(), AS_none, DS, ParsedAttributesView::none(), AnonRecord);
    DS.complete(TheDecl);
    Actions.ActOnDefinedDeclarationSpecifier(TheDecl);
    if (AnonRecord) {
      Decl* decls[] = {AnonRecord, TheDecl};
      return Actions.BuildDeclaratorGroup(decls);
    }
    return Actions.ConvertDeclToDeclGroup(TheDecl);
  }

  if (DS.hasTagDefinition())
    Actions.ActOnDefinedDeclarationSpecifier(DS.getRepAsDecl());

  // ObjC2 allows prefix attributes on class interfaces and protocols.
  // FIXME: This still needs better diagnostics. We should only accept
  // attributes here, no types, etc.
  if (getLangOpts().ObjC && Tok.is(tok::at)) {
    SourceLocation AtLoc = ConsumeToken(); // the "@"
    if (!Tok.isObjCAtKeyword(tok::objc_interface) &&
        !Tok.isObjCAtKeyword(tok::objc_protocol) &&
        !Tok.isObjCAtKeyword(tok::objc_implementation)) {
      Diag(Tok, diag::err_objc_unexpected_attr);
      SkipUntil(tok::semi);
      return nullptr;
    }

    DS.abort();
    DS.takeAttributesFrom(Attrs);

    const char *PrevSpec = nullptr;
    unsigned DiagID;
    if (DS.SetTypeSpecType(DeclSpec::TST_unspecified, AtLoc, PrevSpec, DiagID,
                           Actions.getASTContext().getPrintingPolicy()))
      Diag(AtLoc, DiagID) << PrevSpec;

    if (Tok.isObjCAtKeyword(tok::objc_protocol))
      return ParseObjCAtProtocolDeclaration(AtLoc, DS.getAttributes());

    if (Tok.isObjCAtKeyword(tok::objc_implementation))
      return ParseObjCAtImplementationDeclaration(AtLoc, DS.getAttributes());

    return Actions.ConvertDeclToDeclGroup(
            ParseObjCAtInterfaceDeclaration(AtLoc, DS.getAttributes()));
  }

  // If the declspec consisted only of 'extern' and we have a string
  // literal following it, this must be a C++ linkage specifier like
  // 'extern "C"'.
  if (getLangOpts().CPlusPlus && isTokenStringLiteral() &&
      DS.getStorageClassSpec() == DeclSpec::SCS_extern &&
      DS.getParsedSpecifiers() == DeclSpec::PQ_StorageClassSpecifier) {
    ProhibitAttributes(Attrs);
    Decl *TheDecl = ParseLinkage(DS, DeclaratorContext::File);
    return Actions.ConvertDeclToDeclGroup(TheDecl);
  }

  return ParseDeclGroup(DS, DeclaratorContext::File, Attrs, TemplateInfo);
}

Parser::DeclGroupPtrTy Parser::ParseDeclarationOrFunctionDefinition(
    ParsedAttributes &Attrs, ParsedAttributes &DeclSpecAttrs,
    ParsingDeclSpec *DS, AccessSpecifier AS) {
  // Add an enclosing time trace scope for a bunch of small scopes with
  // "EvaluateAsConstExpr".
  llvm::TimeTraceScope TimeScope("ParseDeclarationOrFunctionDefinition", [&]() {
    return Tok.getLocation().printToString(
        Actions.getASTContext().getSourceManager());
  });

  if (DS) {
    return ParseDeclOrFunctionDefInternal(Attrs, DeclSpecAttrs, *DS, AS);
  } else {
    ParsingDeclSpec PDS(*this);
    // Must temporarily exit the objective-c container scope for
    // parsing c constructs and re-enter objc container scope
    // afterwards.
    ObjCDeclContextSwitch ObjCDC(*this);

    return ParseDeclOrFunctionDefInternal(Attrs, DeclSpecAttrs, PDS, AS);
  }
}

/// ParseFunctionDefinition - We parsed and verified that the specified
/// Declarator is well formed.  If this is a K&R-style function, read the
/// parameters declaration-list, then start the compound-statement.
///
///       function-definition: [C99 6.9.1]
///         decl-specs      declarator declaration-list[opt] compound-statement
/// [C90] function-definition: [C99 6.7.1] - implicit int result
/// [C90]   decl-specs[opt] declarator declaration-list[opt] compound-statement
/// [C++] function-definition: [C++ 8.4]
///         decl-specifier-seq[opt] declarator ctor-initializer[opt]
///         function-body
/// [C++] function-definition: [C++ 8.4]
///         decl-specifier-seq[opt] declarator function-try-block
///
Decl *Parser::ParseFunctionDefinition(ParsingDeclarator &D,
                                      const ParsedTemplateInfo &TemplateInfo,
                                      LateParsedAttrList *LateParsedAttrs) {
  llvm::TimeTraceScope TimeScope("ParseFunctionDefinition", [&]() {
    return Actions.GetNameForDeclarator(D).getName().getAsString();
  });

  // Poison SEH identifiers so they are flagged as illegal in function bodies.
  PoisonSEHIdentifiersRAIIObject PoisonSEHIdentifiers(*this, true);
  const DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();
  TemplateParameterDepthRAII CurTemplateDepthTracker(TemplateParameterDepth);

  // If this is C89 and the declspecs were completely missing, fudge in an
  // implicit int.  We do this here because this is the only place where
  // declaration-specifiers are completely optional in the grammar.
  if (getLangOpts().isImplicitIntRequired() && D.getDeclSpec().isEmpty()) {
    Diag(D.getIdentifierLoc(), diag::warn_missing_type_specifier)
        << D.getDeclSpec().getSourceRange();
    const char *PrevSpec;
    unsigned DiagID;
    const PrintingPolicy &Policy = Actions.getASTContext().getPrintingPolicy();
    D.getMutableDeclSpec().SetTypeSpecType(DeclSpec::TST_int,
                                           D.getIdentifierLoc(),
                                           PrevSpec, DiagID,
                                           Policy);
    D.SetRangeBegin(D.getDeclSpec().getSourceRange().getBegin());
  }

  // If this declaration was formed with a K&R-style identifier list for the
  // arguments, parse declarations for all of the args next.
  // int foo(a,b) int a; float b; {}
  if (FTI.isKNRPrototype())
    ParseKNRParamDeclarations(D);

  // We should have either an opening brace or, in a C++ constructor,
  // we may have a colon.
  if (Tok.isNot(tok::l_brace) &&
      (!getLangOpts().CPlusPlus ||
       (Tok.isNot(tok::colon) && Tok.isNot(tok::kw_try) &&
        Tok.isNot(tok::equal)))) {
    Diag(Tok, diag::err_expected_fn_body);

    // Skip over garbage, until we get to '{'.  Don't eat the '{'.
    SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch);

    // If we didn't find the '{', bail out.
    if (Tok.isNot(tok::l_brace))
      return nullptr;
  }

  // Check to make sure that any normal attributes are allowed to be on
  // a definition.  Late parsed attributes are checked at the end.
  if (Tok.isNot(tok::equal)) {
    for (const ParsedAttr &AL : D.getAttributes())
      if (AL.isKnownToGCC() && !AL.isStandardAttributeSyntax())
        Diag(AL.getLoc(), diag::warn_attribute_on_function_definition) << AL;
  }

  // In delayed template parsing mode, for function template we consume the
  // tokens and store them for late parsing at the end of the translation unit.
  if (getLangOpts().DelayedTemplateParsing && Tok.isNot(tok::equal) &&
      TemplateInfo.Kind == ParsedTemplateInfo::Template &&
      Actions.canDelayFunctionBody(D)) {
    MultiTemplateParamsArg TemplateParameterLists(*TemplateInfo.TemplateParams);

    ParseScope BodyScope(this, Scope::FnScope | Scope::DeclScope |
                                   Scope::CompoundStmtScope);
    Scope *ParentScope = getCurScope()->getParent();

    D.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
    Decl *DP = Actions.HandleDeclarator(ParentScope, D,
                                        TemplateParameterLists);
    D.complete(DP);
    D.getMutableDeclSpec().abort();

    if (SkipFunctionBodies && (!DP || Actions.canSkipFunctionBody(DP)) &&
        trySkippingFunctionBody()) {
      BodyScope.Exit();
      return Actions.ActOnSkippedFunctionBody(DP);
    }

    CachedTokens Toks;
    LexTemplateFunctionForLateParsing(Toks);

    if (DP) {
      FunctionDecl *FnD = DP->getAsFunction();
      Actions.CheckForFunctionRedefinition(FnD);
      Actions.MarkAsLateParsedTemplate(FnD, DP, Toks);
    }
    return DP;
  }
  else if (CurParsedObjCImpl &&
           !TemplateInfo.TemplateParams &&
           (Tok.is(tok::l_brace) || Tok.is(tok::kw_try) ||
            Tok.is(tok::colon)) &&
      Actions.CurContext->isTranslationUnit()) {
    ParseScope BodyScope(this, Scope::FnScope | Scope::DeclScope |
                                   Scope::CompoundStmtScope);
    Scope *ParentScope = getCurScope()->getParent();

    D.setFunctionDefinitionKind(FunctionDefinitionKind::Definition);
    Decl *FuncDecl = Actions.HandleDeclarator(ParentScope, D,
                                              MultiTemplateParamsArg());
    D.complete(FuncDecl);
    D.getMutableDeclSpec().abort();
    if (FuncDecl) {
      // Consume the tokens and store them for later parsing.
      StashAwayMethodOrFunctionBodyTokens(FuncDecl);
      CurParsedObjCImpl->HasCFunction = true;
      return FuncDecl;
    }
    // FIXME: Should we really fall through here?
  }

  // Enter a scope for the function body.
  ParseScope BodyScope(this, Scope::FnScope | Scope::DeclScope |
                                 Scope::CompoundStmtScope);

  // Parse function body eagerly if it is either '= delete;' or '= default;' as
  // ActOnStartOfFunctionDef needs to know whether the function is deleted.
  StringLiteral *DeletedMessage = nullptr;
  Sema::FnBodyKind BodyKind = Sema::FnBodyKind::Other;
  SourceLocation KWLoc;
  if (TryConsumeToken(tok::equal)) {
    assert(getLangOpts().CPlusPlus && "Only C++ function definitions have '='");

    if (TryConsumeToken(tok::kw_delete, KWLoc)) {
      Diag(KWLoc, getLangOpts().CPlusPlus11
                      ? diag::warn_cxx98_compat_defaulted_deleted_function
                      : diag::ext_defaulted_deleted_function)
          << 1 /* deleted */;
      BodyKind = Sema::FnBodyKind::Delete;
      DeletedMessage = ParseCXXDeletedFunctionMessage();
    } else if (TryConsumeToken(tok::kw_default, KWLoc)) {
      Diag(KWLoc, getLangOpts().CPlusPlus11
                      ? diag::warn_cxx98_compat_defaulted_deleted_function
                      : diag::ext_defaulted_deleted_function)
          << 0 /* defaulted */;
      BodyKind = Sema::FnBodyKind::Default;
    } else {
      llvm_unreachable("function definition after = not 'delete' or 'default'");
    }

    if (Tok.is(tok::comma)) {
      Diag(KWLoc, diag::err_default_delete_in_multiple_declaration)
          << (BodyKind == Sema::FnBodyKind::Delete);
      SkipUntil(tok::semi);
    } else if (ExpectAndConsume(tok::semi, diag::err_expected_after,
                                BodyKind == Sema::FnBodyKind::Delete
                                    ? "delete"
                                    : "default")) {
      SkipUntil(tok::semi);
    }
  }

  Sema::FPFeaturesStateRAII SaveFPFeatures(Actions);

  // Tell the actions module that we have entered a function definition with the
  // specified Declarator for the function.
  SkipBodyInfo SkipBody;
  Decl *Res = Actions.ActOnStartOfFunctionDef(getCurScope(), D,
                                              TemplateInfo.TemplateParams
                                                  ? *TemplateInfo.TemplateParams
                                                  : MultiTemplateParamsArg(),
                                              &SkipBody, BodyKind);

  if (SkipBody.ShouldSkip) {
    // Do NOT enter SkipFunctionBody if we already consumed the tokens.
    if (BodyKind == Sema::FnBodyKind::Other)
      SkipFunctionBody();

    // ExpressionEvaluationContext is pushed in ActOnStartOfFunctionDef
    // and it would be popped in ActOnFinishFunctionBody.
    // We pop it explcitly here since ActOnFinishFunctionBody won't get called.
    //
    // Do not call PopExpressionEvaluationContext() if it is a lambda because
    // one is already popped when finishing the lambda in BuildLambdaExpr().
    //
    // FIXME: It looks not easy to balance PushExpressionEvaluationContext()
    // and PopExpressionEvaluationContext().
    if (!isLambdaCallOperator(dyn_cast_if_present<FunctionDecl>(Res)))
      Actions.PopExpressionEvaluationContext();
    return Res;
  }

  // Break out of the ParsingDeclarator context before we parse the body.
  D.complete(Res);

  // Break out of the ParsingDeclSpec context, too.  This const_cast is
  // safe because we're always the sole owner.
  D.getMutableDeclSpec().abort();

  if (BodyKind != Sema::FnBodyKind::Other) {
    Actions.SetFunctionBodyKind(Res, KWLoc, BodyKind, DeletedMessage);
    Stmt *GeneratedBody = Res ? Res->getBody() : nullptr;
    Actions.ActOnFinishFunctionBody(Res, GeneratedBody, false);
    return Res;
  }

  // With abbreviated function templates - we need to explicitly add depth to
  // account for the implicit template parameter list induced by the template.
  if (const auto *Template = dyn_cast_if_present<FunctionTemplateDecl>(Res);
      Template && Template->isAbbreviated() &&
      Template->getTemplateParameters()->getParam(0)->isImplicit())
    // First template parameter is implicit - meaning no explicit template
    // parameter list was specified.
    CurTemplateDepthTracker.addDepth(1);

  if (SkipFunctionBodies && (!Res || Actions.canSkipFunctionBody(Res)) &&
      trySkippingFunctionBody()) {
    BodyScope.Exit();
    Actions.ActOnSkippedFunctionBody(Res);
    return Actions.ActOnFinishFunctionBody(Res, nullptr, false);
  }

  if (Tok.is(tok::kw_try))
    return ParseFunctionTryBlock(Res, BodyScope);

  // If we have a colon, then we're probably parsing a C++
  // ctor-initializer.
  if (Tok.is(tok::colon)) {
    ParseConstructorInitializer(Res);

    // Recover from error.
    if (!Tok.is(tok::l_brace)) {
      BodyScope.Exit();
      Actions.ActOnFinishFunctionBody(Res, nullptr);
      return Res;
    }
  } else
    Actions.ActOnDefaultCtorInitializers(Res);

  // Late attributes are parsed in the same scope as the function body.
  if (LateParsedAttrs)
    ParseLexedAttributeList(*LateParsedAttrs, Res, false, true);

  return ParseFunctionStatementBody(Res, BodyScope);
}

void Parser::SkipFunctionBody() {
  if (Tok.is(tok::equal)) {
    SkipUntil(tok::semi);
    return;
  }

  bool IsFunctionTryBlock = Tok.is(tok::kw_try);
  if (IsFunctionTryBlock)
    ConsumeToken();

  CachedTokens Skipped;
  if (ConsumeAndStoreFunctionPrologue(Skipped))
    SkipMalformedDecl();
  else {
    SkipUntil(tok::r_brace);
    while (IsFunctionTryBlock && Tok.is(tok::kw_catch)) {
      SkipUntil(tok::l_brace);
      SkipUntil(tok::r_brace);
    }
  }
}

/// ParseKNRParamDeclarations - Parse 'declaration-list[opt]' which provides
/// types for a function with a K&R-style identifier list for arguments.
void Parser::ParseKNRParamDeclarations(Declarator &D) {
  // We know that the top-level of this declarator is a function.
  DeclaratorChunk::FunctionTypeInfo &FTI = D.getFunctionTypeInfo();

  // Enter function-declaration scope, limiting any declarators to the
  // function prototype scope, including parameter declarators.
  ParseScope PrototypeScope(this, Scope::FunctionPrototypeScope |
                            Scope::FunctionDeclarationScope | Scope::DeclScope);

  // Read all the argument declarations.
  while (isDeclarationSpecifier(ImplicitTypenameContext::No)) {
    SourceLocation DSStart = Tok.getLocation();

    // Parse the common declaration-specifiers piece.
    DeclSpec DS(AttrFactory);
    ParsedTemplateInfo TemplateInfo;
    ParseDeclarationSpecifiers(DS, TemplateInfo);

    // C99 6.9.1p6: 'each declaration in the declaration list shall have at
    // least one declarator'.
    // NOTE: GCC just makes this an ext-warn.  It's not clear what it does with
    // the declarations though.  It's trivial to ignore them, really hard to do
    // anything else with them.
    if (TryConsumeToken(tok::semi)) {
      Diag(DSStart, diag::err_declaration_does_not_declare_param);
      continue;
    }

    // C99 6.9.1p6: Declarations shall contain no storage-class specifiers other
    // than register.
    if (DS.getStorageClassSpec() != DeclSpec::SCS_unspecified &&
        DS.getStorageClassSpec() != DeclSpec::SCS_register) {
      Diag(DS.getStorageClassSpecLoc(),
           diag::err_invalid_storage_class_in_func_decl);
      DS.ClearStorageClassSpecs();
    }
    if (DS.getThreadStorageClassSpec() != DeclSpec::TSCS_unspecified) {
      Diag(DS.getThreadStorageClassSpecLoc(),
           diag::err_invalid_storage_class_in_func_decl);
      DS.ClearStorageClassSpecs();
    }

    // Parse the first declarator attached to this declspec.
    Declarator ParmDeclarator(DS, ParsedAttributesView::none(),
                              DeclaratorContext::KNRTypeList);
    ParseDeclarator(ParmDeclarator);

    // Handle the full declarator list.
    while (true) {
      // If attributes are present, parse them.
      MaybeParseGNUAttributes(ParmDeclarator);

      // Ask the actions module to compute the type for this declarator.
      Decl *Param =
        Actions.ActOnParamDeclarator(getCurScope(), ParmDeclarator);

      if (Param &&
          // A missing identifier has already been diagnosed.
          ParmDeclarator.getIdentifier()) {

        // Scan the argument list looking for the correct param to apply this
        // type.
        for (unsigned i = 0; ; ++i) {
          // C99 6.9.1p6: those declarators shall declare only identifiers from
          // the identifier list.
          if (i == FTI.NumParams) {
            Diag(ParmDeclarator.getIdentifierLoc(), diag::err_no_matching_param)
              << ParmDeclarator.getIdentifier();
            break;
          }

          if (FTI.Params[i].Ident == ParmDeclarator.getIdentifier()) {
            // Reject redefinitions of parameters.
            if (FTI.Params[i].Param) {
              Diag(ParmDeclarator.getIdentifierLoc(),
                   diag::err_param_redefinition)
                 << ParmDeclarator.getIdentifier();
            } else {
              FTI.Params[i].Param = Param;
            }
            break;
          }
        }
      }

      // If we don't have a comma, it is either the end of the list (a ';') or
      // an error, bail out.
      if (Tok.isNot(tok::comma))
        break;

      ParmDeclarator.clear();

      // Consume the comma.
      ParmDeclarator.setCommaLoc(ConsumeToken());

      // Parse the next declarator.
      ParseDeclarator(ParmDeclarator);
    }

    // Consume ';' and continue parsing.
    if (!ExpectAndConsumeSemi(diag::err_expected_semi_declaration))
      continue;

    // Otherwise recover by skipping to next semi or mandatory function body.
    if (SkipUntil(tok::l_brace, StopAtSemi | StopBeforeMatch))
      break;
    TryConsumeToken(tok::semi);
  }

  // The actions module must verify that all arguments were declared.
  Actions.ActOnFinishKNRParamDeclarations(getCurScope(), D, Tok.getLocation());
}


/// ParseAsmStringLiteral - This is just a normal string-literal, but is not
/// allowed to be a wide string, and is not subject to character translation.
/// Unlike GCC, we also diagnose an empty string literal when parsing for an
/// asm label as opposed to an asm statement, because such a construct does not
/// behave well.
///
/// [GNU] asm-string-literal:
///         string-literal
///
ExprResult Parser::ParseAsmStringLiteral(bool ForAsmLabel) {
  if (!isTokenStringLiteral()) {
    Diag(Tok, diag::err_expected_string_literal)
      << /*Source='in...'*/0 << "'asm'";
    return ExprError();
  }

  ExprResult AsmString(ParseStringLiteralExpression());
  if (!AsmString.isInvalid()) {
    const auto *SL = cast<StringLiteral>(AsmString.get());
    if (!SL->isOrdinary()) {
      Diag(Tok, diag::err_asm_operand_wide_string_literal)
        << SL->isWide()
        << SL->getSourceRange();
      return ExprError();
    }
    if (ForAsmLabel && SL->getString().empty()) {
      Diag(Tok, diag::err_asm_operand_wide_string_literal)
          << 2 /* an empty */ << SL->getSourceRange();
      return ExprError();
    }
  }
  return AsmString;
}

/// ParseSimpleAsm
///
/// [GNU] simple-asm-expr:
///         'asm' '(' asm-string-literal ')'
///
ExprResult Parser::ParseSimpleAsm(bool ForAsmLabel, SourceLocation *EndLoc) {
  assert(Tok.is(tok::kw_asm) && "Not an asm!");
  SourceLocation Loc = ConsumeToken();

  if (isGNUAsmQualifier(Tok)) {
    // Remove from the end of 'asm' to the end of the asm qualifier.
    SourceRange RemovalRange(PP.getLocForEndOfToken(Loc),
                             PP.getLocForEndOfToken(Tok.getLocation()));
    Diag(Tok, diag::err_global_asm_qualifier_ignored)
        << GNUAsmQualifiers::getQualifierName(getGNUAsmQualifier(Tok))
        << FixItHint::CreateRemoval(RemovalRange);
    ConsumeToken();
  }

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected_lparen_after) << "asm";
    return ExprError();
  }

  ExprResult Result(ParseAsmStringLiteral(ForAsmLabel));

  if (!Result.isInvalid()) {
    // Close the paren and get the location of the end bracket
    T.consumeClose();
    if (EndLoc)
      *EndLoc = T.getCloseLocation();
  } else if (SkipUntil(tok::r_paren, StopAtSemi | StopBeforeMatch)) {
    if (EndLoc)
      *EndLoc = Tok.getLocation();
    ConsumeParen();
  }

  return Result;
}

/// Get the TemplateIdAnnotation from the token and put it in the
/// cleanup pool so that it gets destroyed when parsing the current top level
/// declaration is finished.
TemplateIdAnnotation *Parser::takeTemplateIdAnnotation(const Token &tok) {
  assert(tok.is(tok::annot_template_id) && "Expected template-id token");
  TemplateIdAnnotation *
      Id = static_cast<TemplateIdAnnotation *>(tok.getAnnotationValue());
  return Id;
}

void Parser::AnnotateScopeToken(CXXScopeSpec &SS, bool IsNewAnnotation) {
  // Push the current token back into the token stream (or revert it if it is
  // cached) and use an annotation scope token for current token.
  if (PP.isBacktrackEnabled())
    PP.RevertCachedTokens(1);
  else
    PP.EnterToken(Tok, /*IsReinject=*/true);
  Tok.setKind(tok::annot_cxxscope);
  Tok.setAnnotationValue(Actions.SaveNestedNameSpecifierAnnotation(SS));
  Tok.setAnnotationRange(SS.getRange());

  // In case the tokens were cached, have Preprocessor replace them
  // with the annotation token.  We don't need to do this if we've
  // just reverted back to a prior state.
  if (IsNewAnnotation)
    PP.AnnotateCachedTokens(Tok);
}

/// Attempt to classify the name at the current token position. This may
/// form a type, scope or primary expression annotation, or replace the token
/// with a typo-corrected keyword. This is only appropriate when the current
/// name must refer to an entity which has already been declared.
///
/// \param CCC Indicates how to perform typo-correction for this name. If NULL,
///        no typo correction will be performed.
/// \param AllowImplicitTypename Whether we are in a context where a dependent
///        nested-name-specifier without typename is treated as a type (e.g.
///        T::type).
Parser::AnnotatedNameKind
Parser::TryAnnotateName(CorrectionCandidateCallback *CCC,
                        ImplicitTypenameContext AllowImplicitTypename) {
  assert(Tok.is(tok::identifier) || Tok.is(tok::annot_cxxscope));

  const bool EnteringContext = false;
  const bool WasScopeAnnotation = Tok.is(tok::annot_cxxscope);

  CXXScopeSpec SS;
  if (getLangOpts().CPlusPlus &&
      ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     EnteringContext))
    return ANK_Error;

  if (Tok.isNot(tok::identifier) || SS.isInvalid()) {
    if (TryAnnotateTypeOrScopeTokenAfterScopeSpec(SS, !WasScopeAnnotation,
                                                  AllowImplicitTypename))
      return ANK_Error;
    return ANK_Unresolved;
  }

  IdentifierInfo *Name = Tok.getIdentifierInfo();
  SourceLocation NameLoc = Tok.getLocation();

  // FIXME: Move the tentative declaration logic into ClassifyName so we can
  // typo-correct to tentatively-declared identifiers.
  if (isTentativelyDeclared(Name) && SS.isEmpty()) {
    // Identifier has been tentatively declared, and thus cannot be resolved as
    // an expression. Fall back to annotating it as a type.
    if (TryAnnotateTypeOrScopeTokenAfterScopeSpec(SS, !WasScopeAnnotation,
                                                  AllowImplicitTypename))
      return ANK_Error;
    return Tok.is(tok::annot_typename) ? ANK_Success : ANK_TentativeDecl;
  }

  Token Next = NextToken();

  // Look up and classify the identifier. We don't perform any typo-correction
  // after a scope specifier, because in general we can't recover from typos
  // there (eg, after correcting 'A::template B<X>::C' [sic], we would need to
  // jump back into scope specifier parsing).
  Sema::NameClassification Classification = Actions.ClassifyName(
      getCurScope(), SS, Name, NameLoc, Next, SS.isEmpty() ? CCC : nullptr);

  // If name lookup found nothing and we guessed that this was a template name,
  // double-check before committing to that interpretation. C++20 requires that
  // we interpret this as a template-id if it can be, but if it can't be, then
  // this is an error recovery case.
  if (Classification.getKind() == Sema::NC_UndeclaredTemplate &&
      isTemplateArgumentList(1) == TPResult::False) {
    // It's not a template-id; re-classify without the '<' as a hint.
    Token FakeNext = Next;
    FakeNext.setKind(tok::unknown);
    Classification =
        Actions.ClassifyName(getCurScope(), SS, Name, NameLoc, FakeNext,
                             SS.isEmpty() ? CCC : nullptr);
  }

  switch (Classification.getKind()) {
  case Sema::NC_Error:
    return ANK_Error;

  case Sema::NC_Keyword:
    // The identifier was typo-corrected to a keyword.
    Tok.setIdentifierInfo(Name);
    Tok.setKind(Name->getTokenID());
    PP.TypoCorrectToken(Tok);
    if (SS.isNotEmpty())
      AnnotateScopeToken(SS, !WasScopeAnnotation);
    // We've "annotated" this as a keyword.
    return ANK_Success;

  case Sema::NC_Unknown:
    // It's not something we know about. Leave it unannotated.
    break;

  case Sema::NC_Type: {
    if (TryAltiVecVectorToken())
      // vector has been found as a type id when altivec is enabled but
      // this is followed by a declaration specifier so this is really the
      // altivec vector token.  Leave it unannotated.
      break;
    SourceLocation BeginLoc = NameLoc;
    if (SS.isNotEmpty())
      BeginLoc = SS.getBeginLoc();

    /// An Objective-C object type followed by '<' is a specialization of
    /// a parameterized class type or a protocol-qualified type.
    ParsedType Ty = Classification.getType();
    if (getLangOpts().ObjC && NextToken().is(tok::less) &&
        (Ty.get()->isObjCObjectType() ||
         Ty.get()->isObjCObjectPointerType())) {
      // Consume the name.
      SourceLocation IdentifierLoc = ConsumeToken();
      SourceLocation NewEndLoc;
      TypeResult NewType
          = parseObjCTypeArgsAndProtocolQualifiers(IdentifierLoc, Ty,
                                                   /*consumeLastToken=*/false,
                                                   NewEndLoc);
      if (NewType.isUsable())
        Ty = NewType.get();
      else if (Tok.is(tok::eof)) // Nothing to do here, bail out...
        return ANK_Error;
    }

    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Ty);
    Tok.setAnnotationEndLoc(Tok.getLocation());
    Tok.setLocation(BeginLoc);
    PP.AnnotateCachedTokens(Tok);
    return ANK_Success;
  }

  case Sema::NC_OverloadSet:
    Tok.setKind(tok::annot_overload_set);
    setExprAnnotation(Tok, Classification.getExpression());
    Tok.setAnnotationEndLoc(NameLoc);
    if (SS.isNotEmpty())
      Tok.setLocation(SS.getBeginLoc());
    PP.AnnotateCachedTokens(Tok);
    return ANK_Success;

  case Sema::NC_NonType:
    if (TryAltiVecVectorToken())
      // vector has been found as a non-type id when altivec is enabled but
      // this is followed by a declaration specifier so this is really the
      // altivec vector token.  Leave it unannotated.
      break;
    Tok.setKind(tok::annot_non_type);
    setNonTypeAnnotation(Tok, Classification.getNonTypeDecl());
    Tok.setLocation(NameLoc);
    Tok.setAnnotationEndLoc(NameLoc);
    PP.AnnotateCachedTokens(Tok);
    if (SS.isNotEmpty())
      AnnotateScopeToken(SS, !WasScopeAnnotation);
    return ANK_Success;

  case Sema::NC_UndeclaredNonType:
  case Sema::NC_DependentNonType:
    Tok.setKind(Classification.getKind() == Sema::NC_UndeclaredNonType
                    ? tok::annot_non_type_undeclared
                    : tok::annot_non_type_dependent);
    setIdentifierAnnotation(Tok, Name);
    Tok.setLocation(NameLoc);
    Tok.setAnnotationEndLoc(NameLoc);
    PP.AnnotateCachedTokens(Tok);
    if (SS.isNotEmpty())
      AnnotateScopeToken(SS, !WasScopeAnnotation);
    return ANK_Success;

  case Sema::NC_TypeTemplate:
    if (Next.isNot(tok::less)) {
      // This may be a type template being used as a template template argument.
      if (SS.isNotEmpty())
        AnnotateScopeToken(SS, !WasScopeAnnotation);
      return ANK_TemplateName;
    }
    [[fallthrough]];
  case Sema::NC_Concept:
  case Sema::NC_VarTemplate:
  case Sema::NC_FunctionTemplate:
  case Sema::NC_UndeclaredTemplate: {
    bool IsConceptName = Classification.getKind() == Sema::NC_Concept;
    // We have a template name followed by '<'. Consume the identifier token so
    // we reach the '<' and annotate it.
    if (Next.is(tok::less))
      ConsumeToken();
    UnqualifiedId Id;
    Id.setIdentifier(Name, NameLoc);
    if (AnnotateTemplateIdToken(
            TemplateTy::make(Classification.getTemplateName()),
            Classification.getTemplateNameKind(), SS, SourceLocation(), Id,
            /*AllowTypeAnnotation=*/!IsConceptName,
            /*TypeConstraint=*/IsConceptName))
      return ANK_Error;
    if (SS.isNotEmpty())
      AnnotateScopeToken(SS, !WasScopeAnnotation);
    return ANK_Success;
  }
  }

  // Unable to classify the name, but maybe we can annotate a scope specifier.
  if (SS.isNotEmpty())
    AnnotateScopeToken(SS, !WasScopeAnnotation);
  return ANK_Unresolved;
}

bool Parser::TryKeywordIdentFallback(bool DisableKeyword) {
  assert(Tok.isNot(tok::identifier));
  Diag(Tok, diag::ext_keyword_as_ident)
    << PP.getSpelling(Tok)
    << DisableKeyword;
  if (DisableKeyword)
    Tok.getIdentifierInfo()->revertTokenIDToIdentifier();
  Tok.setKind(tok::identifier);
  return true;
}

/// TryAnnotateTypeOrScopeToken - If the current token position is on a
/// typename (possibly qualified in C++) or a C++ scope specifier not followed
/// by a typename, TryAnnotateTypeOrScopeToken will replace one or more tokens
/// with a single annotation token representing the typename or C++ scope
/// respectively.
/// This simplifies handling of C++ scope specifiers and allows efficient
/// backtracking without the need to re-parse and resolve nested-names and
/// typenames.
/// It will mainly be called when we expect to treat identifiers as typenames
/// (if they are typenames). For example, in C we do not expect identifiers
/// inside expressions to be treated as typenames so it will not be called
/// for expressions in C.
/// The benefit for C/ObjC is that a typename will be annotated and
/// Actions.getTypeName will not be needed to be called again (e.g. getTypeName
/// will not be called twice, once to check whether we have a declaration
/// specifier, and another one to get the actual type inside
/// ParseDeclarationSpecifiers).
///
/// This returns true if an error occurred.
///
/// Note that this routine emits an error if you call it with ::new or ::delete
/// as the current tokens, so only call it in contexts where these are invalid.
bool Parser::TryAnnotateTypeOrScopeToken(
    ImplicitTypenameContext AllowImplicitTypename) {
  assert((Tok.is(tok::identifier) || Tok.is(tok::coloncolon) ||
          Tok.is(tok::kw_typename) || Tok.is(tok::annot_cxxscope) ||
          Tok.is(tok::kw_decltype) || Tok.is(tok::annot_template_id) ||
          Tok.is(tok::kw___super) || Tok.is(tok::kw_auto) ||
          Tok.is(tok::annot_pack_indexing_type)) &&
         "Cannot be a type or scope token!");

  if (Tok.is(tok::kw_typename)) {
    // MSVC lets you do stuff like:
    //   typename typedef T_::D D;
    //
    // We will consume the typedef token here and put it back after we have
    // parsed the first identifier, transforming it into something more like:
    //   typename T_::D typedef D;
    if (getLangOpts().MSVCCompat && NextToken().is(tok::kw_typedef)) {
      Token TypedefToken;
      PP.Lex(TypedefToken);
      bool Result = TryAnnotateTypeOrScopeToken(AllowImplicitTypename);
      PP.EnterToken(Tok, /*IsReinject=*/true);
      Tok = TypedefToken;
      if (!Result)
        Diag(Tok.getLocation(), diag::warn_expected_qualified_after_typename);
      return Result;
    }

    // Parse a C++ typename-specifier, e.g., "typename T::type".
    //
    //   typename-specifier:
    //     'typename' '::' [opt] nested-name-specifier identifier
    //     'typename' '::' [opt] nested-name-specifier template [opt]
    //            simple-template-id
    SourceLocation TypenameLoc = ConsumeToken();
    CXXScopeSpec SS;
    if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                       /*ObjectHasErrors=*/false,
                                       /*EnteringContext=*/false, nullptr,
                                       /*IsTypename*/ true))
      return true;
    if (SS.isEmpty()) {
      if (Tok.is(tok::identifier) || Tok.is(tok::annot_template_id) ||
          Tok.is(tok::annot_decltype)) {
        // Attempt to recover by skipping the invalid 'typename'
        if (Tok.is(tok::annot_decltype) ||
            (!TryAnnotateTypeOrScopeToken(AllowImplicitTypename) &&
             Tok.isAnnotation())) {
          unsigned DiagID = diag::err_expected_qualified_after_typename;
          // MS compatibility: MSVC permits using known types with typename.
          // e.g. "typedef typename T* pointer_type"
          if (getLangOpts().MicrosoftExt)
            DiagID = diag::warn_expected_qualified_after_typename;
          Diag(Tok.getLocation(), DiagID);
          return false;
        }
      }
      if (Tok.isEditorPlaceholder())
        return true;

      Diag(Tok.getLocation(), diag::err_expected_qualified_after_typename);
      return true;
    }

    bool TemplateKWPresent = false;
    if (Tok.is(tok::kw_template)) {
      ConsumeToken();
      TemplateKWPresent = true;
    }

    TypeResult Ty;
    if (Tok.is(tok::identifier)) {
      if (TemplateKWPresent && NextToken().isNot(tok::less)) {
        Diag(Tok.getLocation(),
             diag::missing_template_arg_list_after_template_kw);
        return true;
      }
      Ty = Actions.ActOnTypenameType(getCurScope(), TypenameLoc, SS,
                                     *Tok.getIdentifierInfo(),
                                     Tok.getLocation());
    } else if (Tok.is(tok::annot_template_id)) {
      TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
      if (!TemplateId->mightBeType()) {
        Diag(Tok, diag::err_typename_refers_to_non_type_template)
          << Tok.getAnnotationRange();
        return true;
      }

      ASTTemplateArgsPtr TemplateArgsPtr(TemplateId->getTemplateArgs(),
                                         TemplateId->NumArgs);

      Ty = TemplateId->isInvalid()
               ? TypeError()
               : Actions.ActOnTypenameType(
                     getCurScope(), TypenameLoc, SS, TemplateId->TemplateKWLoc,
                     TemplateId->Template, TemplateId->Name,
                     TemplateId->TemplateNameLoc, TemplateId->LAngleLoc,
                     TemplateArgsPtr, TemplateId->RAngleLoc);
    } else {
      Diag(Tok, diag::err_expected_type_name_after_typename)
        << SS.getRange();
      return true;
    }

    SourceLocation EndLoc = Tok.getLastLoc();
    Tok.setKind(tok::annot_typename);
    setTypeAnnotation(Tok, Ty);
    Tok.setAnnotationEndLoc(EndLoc);
    Tok.setLocation(TypenameLoc);
    PP.AnnotateCachedTokens(Tok);
    return false;
  }

  // Remembers whether the token was originally a scope annotation.
  bool WasScopeAnnotation = Tok.is(tok::annot_cxxscope);

  CXXScopeSpec SS;
  if (getLangOpts().CPlusPlus)
    if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                       /*ObjectHasErrors=*/false,
                                       /*EnteringContext*/ false))
      return true;

  return TryAnnotateTypeOrScopeTokenAfterScopeSpec(SS, !WasScopeAnnotation,
                                                   AllowImplicitTypename);
}

/// Try to annotate a type or scope token, having already parsed an
/// optional scope specifier. \p IsNewScope should be \c true unless the scope
/// specifier was extracted from an existing tok::annot_cxxscope annotation.
bool Parser::TryAnnotateTypeOrScopeTokenAfterScopeSpec(
    CXXScopeSpec &SS, bool IsNewScope,
    ImplicitTypenameContext AllowImplicitTypename) {
  if (Tok.is(tok::identifier)) {
    // Determine whether the identifier is a type name.
    if (ParsedType Ty = Actions.getTypeName(
            *Tok.getIdentifierInfo(), Tok.getLocation(), getCurScope(), &SS,
            false, NextToken().is(tok::period), nullptr,
            /*IsCtorOrDtorName=*/false,
            /*NonTrivialTypeSourceInfo=*/true,
            /*IsClassTemplateDeductionContext=*/true, AllowImplicitTypename)) {
      SourceLocation BeginLoc = Tok.getLocation();
      if (SS.isNotEmpty()) // it was a C++ qualified type name.
        BeginLoc = SS.getBeginLoc();

      /// An Objective-C object type followed by '<' is a specialization of
      /// a parameterized class type or a protocol-qualified type.
      if (getLangOpts().ObjC && NextToken().is(tok::less) &&
          (Ty.get()->isObjCObjectType() ||
           Ty.get()->isObjCObjectPointerType())) {
        // Consume the name.
        SourceLocation IdentifierLoc = ConsumeToken();
        SourceLocation NewEndLoc;
        TypeResult NewType
          = parseObjCTypeArgsAndProtocolQualifiers(IdentifierLoc, Ty,
                                                   /*consumeLastToken=*/false,
                                                   NewEndLoc);
        if (NewType.isUsable())
          Ty = NewType.get();
        else if (Tok.is(tok::eof)) // Nothing to do here, bail out...
          return false;
      }

      // This is a typename. Replace the current token in-place with an
      // annotation type token.
      Tok.setKind(tok::annot_typename);
      setTypeAnnotation(Tok, Ty);
      Tok.setAnnotationEndLoc(Tok.getLocation());
      Tok.setLocation(BeginLoc);

      // In case the tokens were cached, have Preprocessor replace
      // them with the annotation token.
      PP.AnnotateCachedTokens(Tok);
      return false;
    }

    if (!getLangOpts().CPlusPlus) {
      // If we're in C, the only place we can have :: tokens is C23
      // attribute which is parsed elsewhere. If the identifier is not a type,
      // then it can't be scope either, just early exit.
      return false;
    }

    // If this is a template-id, annotate with a template-id or type token.
    // FIXME: This appears to be dead code. We already have formed template-id
    // tokens when parsing the scope specifier; this can never form a new one.
    if (NextToken().is(tok::less)) {
      TemplateTy Template;
      UnqualifiedId TemplateName;
      TemplateName.setIdentifier(Tok.getIdentifierInfo(), Tok.getLocation());
      bool MemberOfUnknownSpecialization;
      if (TemplateNameKind TNK = Actions.isTemplateName(
              getCurScope(), SS,
              /*hasTemplateKeyword=*/false, TemplateName,
              /*ObjectType=*/nullptr, /*EnteringContext*/false, Template,
              MemberOfUnknownSpecialization)) {
        // Only annotate an undeclared template name as a template-id if the
        // following tokens have the form of a template argument list.
        if (TNK != TNK_Undeclared_template ||
            isTemplateArgumentList(1) != TPResult::False) {
          // Consume the identifier.
          ConsumeToken();
          if (AnnotateTemplateIdToken(Template, TNK, SS, SourceLocation(),
                                      TemplateName)) {
            // If an unrecoverable error occurred, we need to return true here,
            // because the token stream is in a damaged state.  We may not
            // return a valid identifier.
            return true;
          }
        }
      }
    }

    // The current token, which is either an identifier or a
    // template-id, is not part of the annotation. Fall through to
    // push that token back into the stream and complete the C++ scope
    // specifier annotation.
  }

  if (Tok.is(tok::annot_template_id)) {
    TemplateIdAnnotation *TemplateId = takeTemplateIdAnnotation(Tok);
    if (TemplateId->Kind == TNK_Type_template) {
      // A template-id that refers to a type was parsed into a
      // template-id annotation in a context where we weren't allowed
      // to produce a type annotation token. Update the template-id
      // annotation token to a type annotation token now.
      AnnotateTemplateIdTokenAsType(SS, AllowImplicitTypename);
      return false;
    }
  }

  if (SS.isEmpty())
    return false;

  // A C++ scope specifier that isn't followed by a typename.
  AnnotateScopeToken(SS, IsNewScope);
  return false;
}

/// TryAnnotateScopeToken - Like TryAnnotateTypeOrScopeToken but only
/// annotates C++ scope specifiers and template-ids.  This returns
/// true if there was an error that could not be recovered from.
///
/// Note that this routine emits an error if you call it with ::new or ::delete
/// as the current tokens, so only call it in contexts where these are invalid.
bool Parser::TryAnnotateCXXScopeToken(bool EnteringContext) {
  assert(getLangOpts().CPlusPlus &&
         "Call sites of this function should be guarded by checking for C++");
  assert(MightBeCXXScopeToken() && "Cannot be a type or scope token!");

  CXXScopeSpec SS;
  if (ParseOptionalCXXScopeSpecifier(SS, /*ObjectType=*/nullptr,
                                     /*ObjectHasErrors=*/false,
                                     EnteringContext))
    return true;
  if (SS.isEmpty())
    return false;

  AnnotateScopeToken(SS, true);
  return false;
}

bool Parser::isTokenEqualOrEqualTypo() {
  tok::TokenKind Kind = Tok.getKind();
  switch (Kind) {
  default:
    return false;
  case tok::ampequal:            // &=
  case tok::starequal:           // *=
  case tok::plusequal:           // +=
  case tok::minusequal:          // -=
  case tok::exclaimequal:        // !=
  case tok::slashequal:          // /=
  case tok::percentequal:        // %=
  case tok::lessequal:           // <=
  case tok::lesslessequal:       // <<=
  case tok::greaterequal:        // >=
  case tok::greatergreaterequal: // >>=
  case tok::caretequal:          // ^=
  case tok::pipeequal:           // |=
  case tok::equalequal:          // ==
    Diag(Tok, diag::err_invalid_token_after_declarator_suggest_equal)
        << Kind
        << FixItHint::CreateReplacement(SourceRange(Tok.getLocation()), "=");
    [[fallthrough]];
  case tok::equal:
    return true;
  }
}

SourceLocation Parser::handleUnexpectedCodeCompletionToken() {
  assert(Tok.is(tok::code_completion));
  PrevTokLocation = Tok.getLocation();

  for (Scope *S = getCurScope(); S; S = S->getParent()) {
    if (S->isFunctionScope()) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteOrdinaryName(
          getCurScope(), SemaCodeCompletion::PCC_RecoveryInFunction);
      return PrevTokLocation;
    }

    if (S->isClassScope()) {
      cutOffParsing();
      Actions.CodeCompletion().CodeCompleteOrdinaryName(
          getCurScope(), SemaCodeCompletion::PCC_Class);
      return PrevTokLocation;
    }
  }

  cutOffParsing();
  Actions.CodeCompletion().CodeCompleteOrdinaryName(
      getCurScope(), SemaCodeCompletion::PCC_Namespace);
  return PrevTokLocation;
}

// Code-completion pass-through functions

void Parser::CodeCompleteDirective(bool InConditional) {
  Actions.CodeCompletion().CodeCompletePreprocessorDirective(InConditional);
}

void Parser::CodeCompleteInConditionalExclusion() {
  Actions.CodeCompletion().CodeCompleteInPreprocessorConditionalExclusion(
      getCurScope());
}

void Parser::CodeCompleteMacroName(bool IsDefinition) {
  Actions.CodeCompletion().CodeCompletePreprocessorMacroName(IsDefinition);
}

void Parser::CodeCompletePreprocessorExpression() {
  Actions.CodeCompletion().CodeCompletePreprocessorExpression();
}

void Parser::CodeCompleteMacroArgument(IdentifierInfo *Macro,
                                       MacroInfo *MacroInfo,
                                       unsigned ArgumentIndex) {
  Actions.CodeCompletion().CodeCompletePreprocessorMacroArgument(
      getCurScope(), Macro, MacroInfo, ArgumentIndex);
}

void Parser::CodeCompleteIncludedFile(llvm::StringRef Dir, bool IsAngled) {
  Actions.CodeCompletion().CodeCompleteIncludedFile(Dir, IsAngled);
}

void Parser::CodeCompleteNaturalLanguage() {
  Actions.CodeCompletion().CodeCompleteNaturalLanguage();
}

bool Parser::ParseMicrosoftIfExistsCondition(IfExistsCondition& Result) {
  assert((Tok.is(tok::kw___if_exists) || Tok.is(tok::kw___if_not_exists)) &&
         "Expected '__if_exists' or '__if_not_exists'");
  Result.IsIfExists = Tok.is(tok::kw___if_exists);
  Result.KeywordLoc = ConsumeToken();

  BalancedDelimiterTracker T(*this, tok::l_paren);
  if (T.consumeOpen()) {
    Diag(Tok, diag::err_expected_lparen_after)
      << (Result.IsIfExists? "__if_exists" : "__if_not_exists");
    return true;
  }

  // Parse nested-name-specifier.
  if (getLangOpts().CPlusPlus)
    ParseOptionalCXXScopeSpecifier(Result.SS, /*ObjectType=*/nullptr,
                                   /*ObjectHasErrors=*/false,
                                   /*EnteringContext=*/false);

  // Check nested-name specifier.
  if (Result.SS.isInvalid()) {
    T.skipToEnd();
    return true;
  }

  // Parse the unqualified-id.
  SourceLocation TemplateKWLoc; // FIXME: parsed, but unused.
  if (ParseUnqualifiedId(Result.SS, /*ObjectType=*/nullptr,
                         /*ObjectHadErrors=*/false, /*EnteringContext*/ false,
                         /*AllowDestructorName*/ true,
                         /*AllowConstructorName*/ true,
                         /*AllowDeductionGuide*/ false, &TemplateKWLoc,
                         Result.Name)) {
    T.skipToEnd();
    return true;
  }

  if (T.consumeClose())
    return true;

  // Check if the symbol exists.
  switch (Actions.CheckMicrosoftIfExistsSymbol(getCurScope(), Result.KeywordLoc,
                                               Result.IsIfExists, Result.SS,
                                               Result.Name)) {
  case Sema::IER_Exists:
    Result.Behavior = Result.IsIfExists ? IEB_Parse : IEB_Skip;
    break;

  case Sema::IER_DoesNotExist:
    Result.Behavior = !Result.IsIfExists ? IEB_Parse : IEB_Skip;
    break;

  case Sema::IER_Dependent:
    Result.Behavior = IEB_Dependent;
    break;

  case Sema::IER_Error:
    return true;
  }

  return false;
}

void Parser::ParseMicrosoftIfExistsExternalDeclaration() {
  IfExistsCondition Result;
  if (ParseMicrosoftIfExistsCondition(Result))
    return;

  BalancedDelimiterTracker Braces(*this, tok::l_brace);
  if (Braces.consumeOpen()) {
    Diag(Tok, diag::err_expected) << tok::l_brace;
    return;
  }

  switch (Result.Behavior) {
  case IEB_Parse:
    // Parse declarations below.
    break;

  case IEB_Dependent:
    llvm_unreachable("Cannot have a dependent external declaration");

  case IEB_Skip:
    Braces.skipToEnd();
    return;
  }

  // Parse the declarations.
  // FIXME: Support module import within __if_exists?
  while (Tok.isNot(tok::r_brace) && !isEofOrEom()) {
    ParsedAttributes Attrs(AttrFactory);
    MaybeParseCXX11Attributes(Attrs);
    ParsedAttributes EmptyDeclSpecAttrs(AttrFactory);
    DeclGroupPtrTy Result = ParseExternalDeclaration(Attrs, EmptyDeclSpecAttrs);
    if (Result && !getCurScope()->getParent())
      Actions.getASTConsumer().HandleTopLevelDecl(Result.get());
  }
  Braces.consumeClose();
}

/// Parse a declaration beginning with the 'module' keyword or C++20
/// context-sensitive keyword (optionally preceded by 'export').
///
///   module-declaration:   [C++20]
///     'export'[opt] 'module' module-name attribute-specifier-seq[opt] ';'
///
///   global-module-fragment:  [C++2a]
///     'module' ';' top-level-declaration-seq[opt]
///   module-declaration:      [C++2a]
///     'export'[opt] 'module' module-name module-partition[opt]
///            attribute-specifier-seq[opt] ';'
///   private-module-fragment: [C++2a]
///     'module' ':' 'private' ';' top-level-declaration-seq[opt]
Parser::DeclGroupPtrTy
Parser::ParseModuleDecl(Sema::ModuleImportState &ImportState) {
  SourceLocation StartLoc = Tok.getLocation();

  Sema::ModuleDeclKind MDK = TryConsumeToken(tok::kw_export)
                                 ? Sema::ModuleDeclKind::Interface
                                 : Sema::ModuleDeclKind::Implementation;

  assert(
      (Tok.is(tok::kw_module) ||
       (Tok.is(tok::identifier) && Tok.getIdentifierInfo() == Ident_module)) &&
      "not a module declaration");
  SourceLocation ModuleLoc = ConsumeToken();

  // Attributes appear after the module name, not before.
  // FIXME: Suggest moving the attributes later with a fixit.
  DiagnoseAndSkipCXX11Attributes();

  // Parse a global-module-fragment, if present.
  if (getLangOpts().CPlusPlusModules && Tok.is(tok::semi)) {
    SourceLocation SemiLoc = ConsumeToken();
    if (ImportState != Sema::ModuleImportState::FirstDecl) {
      Diag(StartLoc, diag::err_global_module_introducer_not_at_start)
        << SourceRange(StartLoc, SemiLoc);
      return nullptr;
    }
    if (MDK == Sema::ModuleDeclKind::Interface) {
      Diag(StartLoc, diag::err_module_fragment_exported)
        << /*global*/0 << FixItHint::CreateRemoval(StartLoc);
    }
    ImportState = Sema::ModuleImportState::GlobalFragment;
    return Actions.ActOnGlobalModuleFragmentDecl(ModuleLoc);
  }

  // Parse a private-module-fragment, if present.
  if (getLangOpts().CPlusPlusModules && Tok.is(tok::colon) &&
      NextToken().is(tok::kw_private)) {
    if (MDK == Sema::ModuleDeclKind::Interface) {
      Diag(StartLoc, diag::err_module_fragment_exported)
        << /*private*/1 << FixItHint::CreateRemoval(StartLoc);
    }
    ConsumeToken();
    SourceLocation PrivateLoc = ConsumeToken();
    DiagnoseAndSkipCXX11Attributes();
    ExpectAndConsumeSemi(diag::err_private_module_fragment_expected_semi);
    ImportState = ImportState == Sema::ModuleImportState::ImportAllowed
                      ? Sema::ModuleImportState::PrivateFragmentImportAllowed
                      : Sema::ModuleImportState::PrivateFragmentImportFinished;
    return Actions.ActOnPrivateModuleFragmentDecl(ModuleLoc, PrivateLoc);
  }

  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
  if (ParseModuleName(ModuleLoc, Path, /*IsImport*/ false))
    return nullptr;

  // Parse the optional module-partition.
  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Partition;
  if (Tok.is(tok::colon)) {
    SourceLocation ColonLoc = ConsumeToken();
    if (!getLangOpts().CPlusPlusModules)
      Diag(ColonLoc, diag::err_unsupported_module_partition)
          << SourceRange(ColonLoc, Partition.back().second);
    // Recover by ignoring the partition name.
    else if (ParseModuleName(ModuleLoc, Partition, /*IsImport*/ false))
      return nullptr;
  }

  // We don't support any module attributes yet; just parse them and diagnose.
  ParsedAttributes Attrs(AttrFactory);
  MaybeParseCXX11Attributes(Attrs);
  ProhibitCXX11Attributes(Attrs, diag::err_attribute_not_module_attr,
                          diag::err_keyword_not_module_attr,
                          /*DiagnoseEmptyAttrs=*/false,
                          /*WarnOnUnknownAttrs=*/true);

  ExpectAndConsumeSemi(diag::err_module_expected_semi);

  return Actions.ActOnModuleDecl(StartLoc, ModuleLoc, MDK, Path, Partition,
                                 ImportState);
}

/// Parse a module import declaration. This is essentially the same for
/// Objective-C and C++20 except for the leading '@' (in ObjC) and the
/// trailing optional attributes (in C++).
///
/// [ObjC]  @import declaration:
///           '@' 'import' module-name ';'
/// [ModTS] module-import-declaration:
///           'import' module-name attribute-specifier-seq[opt] ';'
/// [C++20] module-import-declaration:
///           'export'[opt] 'import' module-name
///                   attribute-specifier-seq[opt] ';'
///           'export'[opt] 'import' module-partition
///                   attribute-specifier-seq[opt] ';'
///           'export'[opt] 'import' header-name
///                   attribute-specifier-seq[opt] ';'
Decl *Parser::ParseModuleImport(SourceLocation AtLoc,
                                Sema::ModuleImportState &ImportState) {
  SourceLocation StartLoc = AtLoc.isInvalid() ? Tok.getLocation() : AtLoc;

  SourceLocation ExportLoc;
  TryConsumeToken(tok::kw_export, ExportLoc);

  assert((AtLoc.isInvalid() ? Tok.isOneOf(tok::kw_import, tok::identifier)
                            : Tok.isObjCAtKeyword(tok::objc_import)) &&
         "Improper start to module import");
  bool IsObjCAtImport = Tok.isObjCAtKeyword(tok::objc_import);
  SourceLocation ImportLoc = ConsumeToken();

  // For C++20 modules, we can have "name" or ":Partition name" as valid input.
  SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> Path;
  bool IsPartition = false;
  Module *HeaderUnit = nullptr;
  if (Tok.is(tok::header_name)) {
    // This is a header import that the preprocessor decided we should skip
    // because it was malformed in some way. Parse and ignore it; it's already
    // been diagnosed.
    ConsumeToken();
  } else if (Tok.is(tok::annot_header_unit)) {
    // This is a header import that the preprocessor mapped to a module import.
    HeaderUnit = reinterpret_cast<Module *>(Tok.getAnnotationValue());
    ConsumeAnnotationToken();
  } else if (Tok.is(tok::colon)) {
    SourceLocation ColonLoc = ConsumeToken();
    if (!getLangOpts().CPlusPlusModules)
      Diag(ColonLoc, diag::err_unsupported_module_partition)
          << SourceRange(ColonLoc, Path.back().second);
    // Recover by leaving partition empty.
    else if (ParseModuleName(ColonLoc, Path, /*IsImport*/ true))
      return nullptr;
    else
      IsPartition = true;
  } else {
    if (ParseModuleName(ImportLoc, Path, /*IsImport*/ true))
      return nullptr;
  }

  ParsedAttributes Attrs(AttrFactory);
  MaybeParseCXX11Attributes(Attrs);
  // We don't support any module import attributes yet.
  ProhibitCXX11Attributes(Attrs, diag::err_attribute_not_import_attr,
                          diag::err_keyword_not_import_attr,
                          /*DiagnoseEmptyAttrs=*/false,
                          /*WarnOnUnknownAttrs=*/true);

  if (PP.hadModuleLoaderFatalFailure()) {
    // With a fatal failure in the module loader, we abort parsing.
    cutOffParsing();
    return nullptr;
  }

  // Diagnose mis-imports.
  bool SeenError = true;
  switch (ImportState) {
  case Sema::ModuleImportState::ImportAllowed:
    SeenError = false;
    break;
  case Sema::ModuleImportState::FirstDecl:
    // If we found an import decl as the first declaration, we must be not in
    // a C++20 module unit or we are in an invalid state.
    ImportState = Sema::ModuleImportState::NotACXX20Module;
    [[fallthrough]];
  case Sema::ModuleImportState::NotACXX20Module:
    // We can only import a partition within a module purview.
    if (IsPartition)
      Diag(ImportLoc, diag::err_partition_import_outside_module);
    else
      SeenError = false;
    break;
  case Sema::ModuleImportState::GlobalFragment:
  case Sema::ModuleImportState::PrivateFragmentImportAllowed:
    // We can only have pre-processor directives in the global module fragment
    // which allows pp-import, but not of a partition (since the global module
    // does not have partitions).
    // We cannot import a partition into a private module fragment, since
    // [module.private.frag]/1 disallows private module fragments in a multi-
    // TU module.
    if (IsPartition || (HeaderUnit && HeaderUnit->Kind !=
                                          Module::ModuleKind::ModuleHeaderUnit))
      Diag(ImportLoc, diag::err_import_in_wrong_fragment)
          << IsPartition
          << (ImportState == Sema::ModuleImportState::GlobalFragment ? 0 : 1);
    else
      SeenError = false;
    break;
  case Sema::ModuleImportState::ImportFinished:
  case Sema::ModuleImportState::PrivateFragmentImportFinished:
    if (getLangOpts().CPlusPlusModules)
      Diag(ImportLoc, diag::err_import_not_allowed_here);
    else
      SeenError = false;
    break;
  }
  if (SeenError) {
    ExpectAndConsumeSemi(diag::err_module_expected_semi);
    return nullptr;
  }

  DeclResult Import;
  if (HeaderUnit)
    Import =
        Actions.ActOnModuleImport(StartLoc, ExportLoc, ImportLoc, HeaderUnit);
  else if (!Path.empty())
    Import = Actions.ActOnModuleImport(StartLoc, ExportLoc, ImportLoc, Path,
                                       IsPartition);
  ExpectAndConsumeSemi(diag::err_module_expected_semi);
  if (Import.isInvalid())
    return nullptr;

  // Using '@import' in framework headers requires modules to be enabled so that
  // the header is parseable. Emit a warning to make the user aware.
  if (IsObjCAtImport && AtLoc.isValid()) {
    auto &SrcMgr = PP.getSourceManager();
    auto FE = SrcMgr.getFileEntryRefForID(SrcMgr.getFileID(AtLoc));
    if (FE && llvm::sys::path::parent_path(FE->getDir().getName())
                  .ends_with(".framework"))
      Diags.Report(AtLoc, diag::warn_atimport_in_framework_header);
  }

  return Import.get();
}

/// Parse a C++ / Objective-C module name (both forms use the same
/// grammar).
///
///         module-name:
///           module-name-qualifier[opt] identifier
///         module-name-qualifier:
///           module-name-qualifier[opt] identifier '.'
bool Parser::ParseModuleName(
    SourceLocation UseLoc,
    SmallVectorImpl<std::pair<IdentifierInfo *, SourceLocation>> &Path,
    bool IsImport) {
  // Parse the module path.
  while (true) {
    if (!Tok.is(tok::identifier)) {
      if (Tok.is(tok::code_completion)) {
        cutOffParsing();
        Actions.CodeCompletion().CodeCompleteModuleImport(UseLoc, Path);
        return true;
      }

      Diag(Tok, diag::err_module_expected_ident) << IsImport;
      SkipUntil(tok::semi);
      return true;
    }

    // Record this part of the module path.
    Path.push_back(std::make_pair(Tok.getIdentifierInfo(), Tok.getLocation()));
    ConsumeToken();

    if (Tok.isNot(tok::period))
      return false;

    ConsumeToken();
  }
}

/// Try recover parser when module annotation appears where it must not
/// be found.
/// \returns false if the recover was successful and parsing may be continued, or
/// true if parser must bail out to top level and handle the token there.
bool Parser::parseMisplacedModuleImport() {
  while (true) {
    switch (Tok.getKind()) {
    case tok::annot_module_end:
      // If we recovered from a misplaced module begin, we expect to hit a
      // misplaced module end too. Stay in the current context when this
      // happens.
      if (MisplacedModuleBeginCount) {
        --MisplacedModuleBeginCount;
        Actions.ActOnAnnotModuleEnd(
            Tok.getLocation(),
            reinterpret_cast<Module *>(Tok.getAnnotationValue()));
        ConsumeAnnotationToken();
        continue;
      }
      // Inform caller that recovery failed, the error must be handled at upper
      // level. This will generate the desired "missing '}' at end of module"
      // diagnostics on the way out.
      return true;
    case tok::annot_module_begin:
      // Recover by entering the module (Sema will diagnose).
      Actions.ActOnAnnotModuleBegin(
          Tok.getLocation(),
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      ConsumeAnnotationToken();
      ++MisplacedModuleBeginCount;
      continue;
    case tok::annot_module_include:
      // Module import found where it should not be, for instance, inside a
      // namespace. Recover by importing the module.
      Actions.ActOnAnnotModuleInclude(
          Tok.getLocation(),
          reinterpret_cast<Module *>(Tok.getAnnotationValue()));
      ConsumeAnnotationToken();
      // If there is another module import, process it.
      continue;
    default:
      return false;
    }
  }
  return false;
}

void Parser::diagnoseUseOfC11Keyword(const Token &Tok) {
  // Warn that this is a C11 extension if in an older mode or if in C++.
  // Otherwise, warn that it is incompatible with standards before C11 if in
  // C11 or later.
  Diag(Tok, getLangOpts().C11 ? diag::warn_c11_compat_keyword
                              : diag::ext_c11_feature)
      << Tok.getName();
}

bool BalancedDelimiterTracker::diagnoseOverflow() {
  P.Diag(P.Tok, diag::err_bracket_depth_exceeded)
    << P.getLangOpts().BracketDepth;
  P.Diag(P.Tok, diag::note_bracket_depth);
  P.cutOffParsing();
  return true;
}

bool BalancedDelimiterTracker::expectAndConsume(unsigned DiagID,
                                                const char *Msg,
                                                tok::TokenKind SkipToTok) {
  LOpen = P.Tok.getLocation();
  if (P.ExpectAndConsume(Kind, DiagID, Msg)) {
    if (SkipToTok != tok::unknown)
      P.SkipUntil(SkipToTok, Parser::StopAtSemi);
    return true;
  }

  if (getDepth() < P.getLangOpts().BracketDepth)
    return false;

  return diagnoseOverflow();
}

bool BalancedDelimiterTracker::diagnoseMissingClose() {
  assert(!P.Tok.is(Close) && "Should have consumed closing delimiter");

  if (P.Tok.is(tok::annot_module_end))
    P.Diag(P.Tok, diag::err_missing_before_module_end) << Close;
  else
    P.Diag(P.Tok, diag::err_expected) << Close;
  P.Diag(LOpen, diag::note_matching) << Kind;

  // If we're not already at some kind of closing bracket, skip to our closing
  // token.
  if (P.Tok.isNot(tok::r_paren) && P.Tok.isNot(tok::r_brace) &&
      P.Tok.isNot(tok::r_square) &&
      P.SkipUntil(Close, FinalToken,
                  Parser::StopAtSemi | Parser::StopBeforeMatch) &&
      P.Tok.is(Close))
    LClose = P.ConsumeAnyToken();
  return true;
}

void BalancedDelimiterTracker::skipToEnd() {
  P.SkipUntil(Close, Parser::StopBeforeMatch);
  consumeClose();
}
