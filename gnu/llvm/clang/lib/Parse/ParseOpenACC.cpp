//===--- ParseOpenACC.cpp - OpenACC-specific parsing support --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the parsing logic for OpenACC language features.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/OpenACCClause.h"
#include "clang/Basic/OpenACCKinds.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/RAIIObjectsForParser.h"
#include "clang/Sema/SemaOpenACC.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"

using namespace clang;
using namespace llvm;

namespace {
// An enum that contains the extended 'partial' parsed variants. This type
// should never escape the initial parse functionality, but is useful for
// simplifying the implementation.
enum class OpenACCDirectiveKindEx {
  Invalid = static_cast<int>(OpenACCDirectiveKind::Invalid),
  // 'enter data' and 'exit data'
  Enter,
  Exit,
};

// Translate single-token string representations to the OpenACC Directive Kind.
// This doesn't completely comprehend 'Compound Constructs' (as it just
// identifies the first token), and doesn't fully handle 'enter data', 'exit
// data', nor any of the 'atomic' variants, just the first token of each.  So
// this should only be used by `ParseOpenACCDirectiveKind`.
OpenACCDirectiveKindEx getOpenACCDirectiveKind(Token Tok) {
  if (!Tok.is(tok::identifier))
    return OpenACCDirectiveKindEx::Invalid;
  OpenACCDirectiveKind DirKind =
      llvm::StringSwitch<OpenACCDirectiveKind>(
          Tok.getIdentifierInfo()->getName())
          .Case("parallel", OpenACCDirectiveKind::Parallel)
          .Case("serial", OpenACCDirectiveKind::Serial)
          .Case("kernels", OpenACCDirectiveKind::Kernels)
          .Case("data", OpenACCDirectiveKind::Data)
          .Case("host_data", OpenACCDirectiveKind::HostData)
          .Case("loop", OpenACCDirectiveKind::Loop)
          .Case("cache", OpenACCDirectiveKind::Cache)
          .Case("atomic", OpenACCDirectiveKind::Atomic)
          .Case("routine", OpenACCDirectiveKind::Routine)
          .Case("declare", OpenACCDirectiveKind::Declare)
          .Case("init", OpenACCDirectiveKind::Init)
          .Case("shutdown", OpenACCDirectiveKind::Shutdown)
          .Case("set", OpenACCDirectiveKind::Set)
          .Case("update", OpenACCDirectiveKind::Update)
          .Case("wait", OpenACCDirectiveKind::Wait)
          .Default(OpenACCDirectiveKind::Invalid);

  if (DirKind != OpenACCDirectiveKind::Invalid)
    return static_cast<OpenACCDirectiveKindEx>(DirKind);

  return llvm::StringSwitch<OpenACCDirectiveKindEx>(
             Tok.getIdentifierInfo()->getName())
      .Case("enter", OpenACCDirectiveKindEx::Enter)
      .Case("exit", OpenACCDirectiveKindEx::Exit)
      .Default(OpenACCDirectiveKindEx::Invalid);
}

// Translate single-token string representations to the OpenCC Clause Kind.
OpenACCClauseKind getOpenACCClauseKind(Token Tok) {
  // auto is a keyword in some language modes, so make sure we parse it
  // correctly.
  if (Tok.is(tok::kw_auto))
    return OpenACCClauseKind::Auto;

  // default is a keyword, so make sure we parse it correctly.
  if (Tok.is(tok::kw_default))
    return OpenACCClauseKind::Default;

  // if is also a keyword, make sure we parse it correctly.
  if (Tok.is(tok::kw_if))
    return OpenACCClauseKind::If;

  // 'private' is also a keyword, make sure we pare it correctly.
  if (Tok.is(tok::kw_private))
    return OpenACCClauseKind::Private;

  if (!Tok.is(tok::identifier))
    return OpenACCClauseKind::Invalid;

  return llvm::StringSwitch<OpenACCClauseKind>(
             Tok.getIdentifierInfo()->getName())
      .Case("async", OpenACCClauseKind::Async)
      .Case("attach", OpenACCClauseKind::Attach)
      .Case("auto", OpenACCClauseKind::Auto)
      .Case("bind", OpenACCClauseKind::Bind)
      .Case("create", OpenACCClauseKind::Create)
      .Case("pcreate", OpenACCClauseKind::PCreate)
      .Case("present_or_create", OpenACCClauseKind::PresentOrCreate)
      .Case("collapse", OpenACCClauseKind::Collapse)
      .Case("copy", OpenACCClauseKind::Copy)
      .Case("pcopy", OpenACCClauseKind::PCopy)
      .Case("present_or_copy", OpenACCClauseKind::PresentOrCopy)
      .Case("copyin", OpenACCClauseKind::CopyIn)
      .Case("pcopyin", OpenACCClauseKind::PCopyIn)
      .Case("present_or_copyin", OpenACCClauseKind::PresentOrCopyIn)
      .Case("copyout", OpenACCClauseKind::CopyOut)
      .Case("pcopyout", OpenACCClauseKind::PCopyOut)
      .Case("present_or_copyout", OpenACCClauseKind::PresentOrCopyOut)
      .Case("default", OpenACCClauseKind::Default)
      .Case("default_async", OpenACCClauseKind::DefaultAsync)
      .Case("delete", OpenACCClauseKind::Delete)
      .Case("detach", OpenACCClauseKind::Detach)
      .Case("device", OpenACCClauseKind::Device)
      .Case("device_num", OpenACCClauseKind::DeviceNum)
      .Case("device_resident", OpenACCClauseKind::DeviceResident)
      .Case("device_type", OpenACCClauseKind::DeviceType)
      .Case("deviceptr", OpenACCClauseKind::DevicePtr)
      .Case("dtype", OpenACCClauseKind::DType)
      .Case("finalize", OpenACCClauseKind::Finalize)
      .Case("firstprivate", OpenACCClauseKind::FirstPrivate)
      .Case("gang", OpenACCClauseKind::Gang)
      .Case("host", OpenACCClauseKind::Host)
      .Case("if", OpenACCClauseKind::If)
      .Case("if_present", OpenACCClauseKind::IfPresent)
      .Case("independent", OpenACCClauseKind::Independent)
      .Case("link", OpenACCClauseKind::Link)
      .Case("no_create", OpenACCClauseKind::NoCreate)
      .Case("num_gangs", OpenACCClauseKind::NumGangs)
      .Case("num_workers", OpenACCClauseKind::NumWorkers)
      .Case("nohost", OpenACCClauseKind::NoHost)
      .Case("present", OpenACCClauseKind::Present)
      .Case("private", OpenACCClauseKind::Private)
      .Case("reduction", OpenACCClauseKind::Reduction)
      .Case("self", OpenACCClauseKind::Self)
      .Case("seq", OpenACCClauseKind::Seq)
      .Case("tile", OpenACCClauseKind::Tile)
      .Case("use_device", OpenACCClauseKind::UseDevice)
      .Case("vector", OpenACCClauseKind::Vector)
      .Case("vector_length", OpenACCClauseKind::VectorLength)
      .Case("wait", OpenACCClauseKind::Wait)
      .Case("worker", OpenACCClauseKind::Worker)
      .Default(OpenACCClauseKind::Invalid);
}

// Since 'atomic' is effectively a compound directive, this will decode the
// second part of the directive.
OpenACCAtomicKind getOpenACCAtomicKind(Token Tok) {
  if (!Tok.is(tok::identifier))
    return OpenACCAtomicKind::Invalid;
  return llvm::StringSwitch<OpenACCAtomicKind>(
             Tok.getIdentifierInfo()->getName())
      .Case("read", OpenACCAtomicKind::Read)
      .Case("write", OpenACCAtomicKind::Write)
      .Case("update", OpenACCAtomicKind::Update)
      .Case("capture", OpenACCAtomicKind::Capture)
      .Default(OpenACCAtomicKind::Invalid);
}

OpenACCDefaultClauseKind getOpenACCDefaultClauseKind(Token Tok) {
  if (!Tok.is(tok::identifier))
    return OpenACCDefaultClauseKind::Invalid;

  return llvm::StringSwitch<OpenACCDefaultClauseKind>(
             Tok.getIdentifierInfo()->getName())
      .Case("none", OpenACCDefaultClauseKind::None)
      .Case("present", OpenACCDefaultClauseKind::Present)
      .Default(OpenACCDefaultClauseKind::Invalid);
}

enum class OpenACCSpecialTokenKind {
  ReadOnly,
  DevNum,
  Queues,
  Zero,
  Force,
  Num,
  Length,
  Dim,
  Static,
};

bool isOpenACCSpecialToken(OpenACCSpecialTokenKind Kind, Token Tok) {
  if (Tok.is(tok::kw_static) && Kind == OpenACCSpecialTokenKind::Static)
    return true;

  if (!Tok.is(tok::identifier))
    return false;

  switch (Kind) {
  case OpenACCSpecialTokenKind::ReadOnly:
    return Tok.getIdentifierInfo()->isStr("readonly");
  case OpenACCSpecialTokenKind::DevNum:
    return Tok.getIdentifierInfo()->isStr("devnum");
  case OpenACCSpecialTokenKind::Queues:
    return Tok.getIdentifierInfo()->isStr("queues");
  case OpenACCSpecialTokenKind::Zero:
    return Tok.getIdentifierInfo()->isStr("zero");
  case OpenACCSpecialTokenKind::Force:
    return Tok.getIdentifierInfo()->isStr("force");
  case OpenACCSpecialTokenKind::Num:
    return Tok.getIdentifierInfo()->isStr("num");
  case OpenACCSpecialTokenKind::Length:
    return Tok.getIdentifierInfo()->isStr("length");
  case OpenACCSpecialTokenKind::Dim:
    return Tok.getIdentifierInfo()->isStr("dim");
  case OpenACCSpecialTokenKind::Static:
    return Tok.getIdentifierInfo()->isStr("static");
  }
  llvm_unreachable("Unknown 'Kind' Passed");
}

/// Used for cases where we have a token we want to check against an
/// 'identifier-like' token, but don't want to give awkward error messages in
/// cases where it is accidentially a keyword.
bool isTokenIdentifierOrKeyword(Parser &P, Token Tok) {
  if (Tok.is(tok::identifier))
    return true;

  if (!Tok.isAnnotation() && Tok.getIdentifierInfo() &&
      Tok.getIdentifierInfo()->isKeyword(P.getLangOpts()))
    return true;

  return false;
}

/// Parses and consumes an identifer followed immediately by a single colon, and
/// diagnoses if it is not the 'special token' kind that we require. Used when
/// the tag is the only valid value.
/// Return 'true' if the special token was matched, false if no special token,
/// or an invalid special token was found.
template <typename DirOrClauseTy>
bool tryParseAndConsumeSpecialTokenKind(Parser &P, OpenACCSpecialTokenKind Kind,
                                        DirOrClauseTy DirOrClause) {
  Token IdentTok = P.getCurToken();
  // If this is an identifier-like thing followed by ':', it is one of the
  // OpenACC 'special' name tags, so consume it.
  if (isTokenIdentifierOrKeyword(P, IdentTok) && P.NextToken().is(tok::colon)) {
    P.ConsumeToken();
    P.ConsumeToken();

    if (!isOpenACCSpecialToken(Kind, IdentTok)) {
      P.Diag(IdentTok, diag::err_acc_invalid_tag_kind)
          << IdentTok.getIdentifierInfo() << DirOrClause
          << std::is_same_v<DirOrClauseTy, OpenACCClauseKind>;
      return false;
    }

    return true;
  }

  return false;
}

bool isOpenACCDirectiveKind(OpenACCDirectiveKind Kind, Token Tok) {
  if (!Tok.is(tok::identifier))
    return false;

  switch (Kind) {
  case OpenACCDirectiveKind::Parallel:
    return Tok.getIdentifierInfo()->isStr("parallel");
  case OpenACCDirectiveKind::Serial:
    return Tok.getIdentifierInfo()->isStr("serial");
  case OpenACCDirectiveKind::Kernels:
    return Tok.getIdentifierInfo()->isStr("kernels");
  case OpenACCDirectiveKind::Data:
    return Tok.getIdentifierInfo()->isStr("data");
  case OpenACCDirectiveKind::HostData:
    return Tok.getIdentifierInfo()->isStr("host_data");
  case OpenACCDirectiveKind::Loop:
    return Tok.getIdentifierInfo()->isStr("loop");
  case OpenACCDirectiveKind::Cache:
    return Tok.getIdentifierInfo()->isStr("cache");

  case OpenACCDirectiveKind::ParallelLoop:
  case OpenACCDirectiveKind::SerialLoop:
  case OpenACCDirectiveKind::KernelsLoop:
  case OpenACCDirectiveKind::EnterData:
  case OpenACCDirectiveKind::ExitData:
    return false;

  case OpenACCDirectiveKind::Atomic:
    return Tok.getIdentifierInfo()->isStr("atomic");
  case OpenACCDirectiveKind::Routine:
    return Tok.getIdentifierInfo()->isStr("routine");
  case OpenACCDirectiveKind::Declare:
    return Tok.getIdentifierInfo()->isStr("declare");
  case OpenACCDirectiveKind::Init:
    return Tok.getIdentifierInfo()->isStr("init");
  case OpenACCDirectiveKind::Shutdown:
    return Tok.getIdentifierInfo()->isStr("shutdown");
  case OpenACCDirectiveKind::Set:
    return Tok.getIdentifierInfo()->isStr("set");
  case OpenACCDirectiveKind::Update:
    return Tok.getIdentifierInfo()->isStr("update");
  case OpenACCDirectiveKind::Wait:
    return Tok.getIdentifierInfo()->isStr("wait");
  case OpenACCDirectiveKind::Invalid:
    return false;
  }
  llvm_unreachable("Unknown 'Kind' Passed");
}

OpenACCReductionOperator ParseReductionOperator(Parser &P) {
  // If there is no colon, treat as if the reduction operator was missing, else
  // we probably will not recover from it in the case where an expression starts
  // with one of the operator tokens.
  if (P.NextToken().isNot(tok::colon)) {
    P.Diag(P.getCurToken(), diag::err_acc_expected_reduction_operator);
    return OpenACCReductionOperator::Invalid;
  }
  Token ReductionKindTok = P.getCurToken();
  // Consume both the kind and the colon.
  P.ConsumeToken();
  P.ConsumeToken();

  switch (ReductionKindTok.getKind()) {
  case tok::plus:
    return OpenACCReductionOperator::Addition;
  case tok::star:
    return OpenACCReductionOperator::Multiplication;
  case tok::amp:
    return OpenACCReductionOperator::BitwiseAnd;
  case tok::pipe:
    return OpenACCReductionOperator::BitwiseOr;
  case tok::caret:
    return OpenACCReductionOperator::BitwiseXOr;
  case tok::ampamp:
    return OpenACCReductionOperator::And;
  case tok::pipepipe:
    return OpenACCReductionOperator::Or;
  case tok::identifier:
    if (ReductionKindTok.getIdentifierInfo()->isStr("max"))
      return OpenACCReductionOperator::Max;
    if (ReductionKindTok.getIdentifierInfo()->isStr("min"))
      return OpenACCReductionOperator::Min;
    [[fallthrough]];
  default:
    P.Diag(ReductionKindTok, diag::err_acc_invalid_reduction_operator);
    return OpenACCReductionOperator::Invalid;
  }
  llvm_unreachable("Reduction op token kind not caught by 'default'?");
}

/// Used for cases where we expect an identifier-like token, but don't want to
/// give awkward error messages in cases where it is accidentially a keyword.
bool expectIdentifierOrKeyword(Parser &P) {
  Token Tok = P.getCurToken();

  if (isTokenIdentifierOrKeyword(P, Tok))
    return false;

  P.Diag(P.getCurToken(), diag::err_expected) << tok::identifier;
  return true;
}

OpenACCDirectiveKind
ParseOpenACCEnterExitDataDirective(Parser &P, Token FirstTok,
                                   OpenACCDirectiveKindEx ExtDirKind) {
  Token SecondTok = P.getCurToken();

  if (SecondTok.isAnnotation()) {
    P.Diag(FirstTok, diag::err_acc_invalid_directive)
        << 0 << FirstTok.getIdentifierInfo();
    return OpenACCDirectiveKind::Invalid;
  }

  // Consume the second name anyway, this way we can continue on without making
  // this oddly look like a clause.
  P.ConsumeAnyToken();

  if (!isOpenACCDirectiveKind(OpenACCDirectiveKind::Data, SecondTok)) {
    if (!SecondTok.is(tok::identifier))
      P.Diag(SecondTok, diag::err_expected) << tok::identifier;
    else
      P.Diag(FirstTok, diag::err_acc_invalid_directive)
          << 1 << FirstTok.getIdentifierInfo()->getName()
          << SecondTok.getIdentifierInfo()->getName();
    return OpenACCDirectiveKind::Invalid;
  }

  return ExtDirKind == OpenACCDirectiveKindEx::Enter
             ? OpenACCDirectiveKind::EnterData
             : OpenACCDirectiveKind::ExitData;
}

OpenACCAtomicKind ParseOpenACCAtomicKind(Parser &P) {
  Token AtomicClauseToken = P.getCurToken();

  // #pragma acc atomic is equivilent to update:
  if (AtomicClauseToken.isAnnotation())
    return OpenACCAtomicKind::Update;

  OpenACCAtomicKind AtomicKind = getOpenACCAtomicKind(AtomicClauseToken);

  // If we don't know what this is, treat it as 'nothing', and treat the rest of
  // this as a clause list, which, despite being invalid, is likely what the
  // user was trying to do.
  if (AtomicKind == OpenACCAtomicKind::Invalid)
    return OpenACCAtomicKind::Update;

  P.ConsumeToken();
  return AtomicKind;
}

// Parse and consume the tokens for OpenACC Directive/Construct kinds.
OpenACCDirectiveKind ParseOpenACCDirectiveKind(Parser &P) {
  Token FirstTok = P.getCurToken();

  // Just #pragma acc can get us immediately to the end, make sure we don't
  // introspect on the spelling before then.
  if (FirstTok.isNot(tok::identifier)) {
    P.Diag(FirstTok, diag::err_acc_missing_directive);

    if (P.getCurToken().isNot(tok::annot_pragma_openacc_end))
      P.ConsumeAnyToken();

    return OpenACCDirectiveKind::Invalid;
  }

  P.ConsumeToken();

  OpenACCDirectiveKindEx ExDirKind = getOpenACCDirectiveKind(FirstTok);

  // OpenACCDirectiveKindEx is meant to be an extended list
  // over OpenACCDirectiveKind, so any value below Invalid is one of the
  // OpenACCDirectiveKind values.  This switch takes care of all of the extra
  // parsing required for the Extended values.  At the end of this block,
  // ExDirKind can be assumed to be a valid OpenACCDirectiveKind, so we can
  // immediately cast it and use it as that.
  if (ExDirKind >= OpenACCDirectiveKindEx::Invalid) {
    switch (ExDirKind) {
    case OpenACCDirectiveKindEx::Invalid: {
      P.Diag(FirstTok, diag::err_acc_invalid_directive)
          << 0 << FirstTok.getIdentifierInfo();
      return OpenACCDirectiveKind::Invalid;
    }
    case OpenACCDirectiveKindEx::Enter:
    case OpenACCDirectiveKindEx::Exit:
      return ParseOpenACCEnterExitDataDirective(P, FirstTok, ExDirKind);
    }
  }

  OpenACCDirectiveKind DirKind = static_cast<OpenACCDirectiveKind>(ExDirKind);

  // Combined Constructs allows parallel loop, serial loop, or kernels loop. Any
  // other attempt at a combined construct will be diagnosed as an invalid
  // clause.
  Token SecondTok = P.getCurToken();
  if (!SecondTok.isAnnotation() &&
      isOpenACCDirectiveKind(OpenACCDirectiveKind::Loop, SecondTok)) {
    switch (DirKind) {
    default:
      // Nothing to do except in the below cases, as they should be diagnosed as
      // a clause.
      break;
    case OpenACCDirectiveKind::Parallel:
      P.ConsumeToken();
      return OpenACCDirectiveKind::ParallelLoop;
    case OpenACCDirectiveKind::Serial:
      P.ConsumeToken();
      return OpenACCDirectiveKind::SerialLoop;
    case OpenACCDirectiveKind::Kernels:
      P.ConsumeToken();
      return OpenACCDirectiveKind::KernelsLoop;
    }
  }

  return DirKind;
}

enum ClauseParensKind {
  None,
  Optional,
  Required
};

ClauseParensKind getClauseParensKind(OpenACCDirectiveKind DirKind,
                                     OpenACCClauseKind Kind) {
  switch (Kind) {
  case OpenACCClauseKind::Self:
    return DirKind == OpenACCDirectiveKind::Update ? ClauseParensKind::Required
                                                   : ClauseParensKind::Optional;
  case OpenACCClauseKind::Async:
  case OpenACCClauseKind::Worker:
  case OpenACCClauseKind::Vector:
  case OpenACCClauseKind::Gang:
  case OpenACCClauseKind::Wait:
    return ClauseParensKind::Optional;

  case OpenACCClauseKind::Default:
  case OpenACCClauseKind::If:
  case OpenACCClauseKind::Create:
  case OpenACCClauseKind::PCreate:
  case OpenACCClauseKind::PresentOrCreate:
  case OpenACCClauseKind::Copy:
  case OpenACCClauseKind::PCopy:
  case OpenACCClauseKind::PresentOrCopy:
  case OpenACCClauseKind::CopyIn:
  case OpenACCClauseKind::PCopyIn:
  case OpenACCClauseKind::PresentOrCopyIn:
  case OpenACCClauseKind::CopyOut:
  case OpenACCClauseKind::PCopyOut:
  case OpenACCClauseKind::PresentOrCopyOut:
  case OpenACCClauseKind::UseDevice:
  case OpenACCClauseKind::NoCreate:
  case OpenACCClauseKind::Present:
  case OpenACCClauseKind::DevicePtr:
  case OpenACCClauseKind::Attach:
  case OpenACCClauseKind::Detach:
  case OpenACCClauseKind::Private:
  case OpenACCClauseKind::FirstPrivate:
  case OpenACCClauseKind::Delete:
  case OpenACCClauseKind::DeviceResident:
  case OpenACCClauseKind::Device:
  case OpenACCClauseKind::Link:
  case OpenACCClauseKind::Host:
  case OpenACCClauseKind::Reduction:
  case OpenACCClauseKind::Collapse:
  case OpenACCClauseKind::Bind:
  case OpenACCClauseKind::VectorLength:
  case OpenACCClauseKind::NumGangs:
  case OpenACCClauseKind::NumWorkers:
  case OpenACCClauseKind::DeviceNum:
  case OpenACCClauseKind::DefaultAsync:
  case OpenACCClauseKind::DeviceType:
  case OpenACCClauseKind::DType:
  case OpenACCClauseKind::Tile:
    return ClauseParensKind::Required;

  case OpenACCClauseKind::Auto:
  case OpenACCClauseKind::Finalize:
  case OpenACCClauseKind::IfPresent:
  case OpenACCClauseKind::Independent:
  case OpenACCClauseKind::Invalid:
  case OpenACCClauseKind::NoHost:
  case OpenACCClauseKind::Seq:
    return ClauseParensKind::None;
  }
  llvm_unreachable("Unhandled clause kind");
}

bool ClauseHasOptionalParens(OpenACCDirectiveKind DirKind,
                             OpenACCClauseKind Kind) {
  return getClauseParensKind(DirKind, Kind) == ClauseParensKind::Optional;
}

bool ClauseHasRequiredParens(OpenACCDirectiveKind DirKind,
                             OpenACCClauseKind Kind) {
  return getClauseParensKind(DirKind, Kind) == ClauseParensKind::Required;
}

// Skip until we see the end of pragma token, but don't consume it. This is us
// just giving up on the rest of the pragma so we can continue executing. We
// have to do this because 'SkipUntil' considers paren balancing, which isn't
// what we want.
void SkipUntilEndOfDirective(Parser &P) {
  while (P.getCurToken().isNot(tok::annot_pragma_openacc_end))
    P.ConsumeAnyToken();
}

bool doesDirectiveHaveAssociatedStmt(OpenACCDirectiveKind DirKind) {
  switch (DirKind) {
  default:
    return false;
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels:
  case OpenACCDirectiveKind::Loop:
    return true;
  }
  llvm_unreachable("Unhandled directive->assoc stmt");
}

unsigned getOpenACCScopeFlags(OpenACCDirectiveKind DirKind) {
  switch (DirKind) {
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels:
    // Mark this as a BreakScope/ContinueScope as well as a compute construct
    // so that we can diagnose trying to 'break'/'continue' inside of one.
    return Scope::BreakScope | Scope::ContinueScope |
           Scope::OpenACCComputeConstructScope;
  case OpenACCDirectiveKind::Invalid:
    llvm_unreachable("Shouldn't be creating a scope for an invalid construct");
  default:
    break;
  }
  return 0;
}

} // namespace

Parser::OpenACCClauseParseResult Parser::OpenACCCanContinue() {
  return {nullptr, OpenACCParseCanContinue::Can};
}

Parser::OpenACCClauseParseResult Parser::OpenACCCannotContinue() {
  return {nullptr, OpenACCParseCanContinue::Cannot};
}

Parser::OpenACCClauseParseResult Parser::OpenACCSuccess(OpenACCClause *Clause) {
  return {Clause, OpenACCParseCanContinue::Can};
}

ExprResult Parser::ParseOpenACCConditionExpr() {
  // FIXME: It isn't clear if the spec saying 'condition' means the same as
  // it does in an if/while/etc (See ParseCXXCondition), however as it was
  // written with Fortran/C in mind, we're going to assume it just means an
  // 'expression evaluating to boolean'.
  ExprResult ER = getActions().CorrectDelayedTyposInExpr(ParseExpression());

  if (!ER.isUsable())
    return ER;

  Sema::ConditionResult R =
      getActions().ActOnCondition(getCurScope(), ER.get()->getExprLoc(),
                                  ER.get(), Sema::ConditionKind::Boolean);

  return R.isInvalid() ? ExprError() : R.get().second;
}

// OpenACC 3.3, section 1.7:
// To simplify the specification and convey appropriate constraint information,
// a pqr-list is a comma-separated list of pdr items. The one exception is a
// clause-list, which is a list of one or more clauses optionally separated by
// commas.
SmallVector<OpenACCClause *>
Parser::ParseOpenACCClauseList(OpenACCDirectiveKind DirKind) {
  SmallVector<OpenACCClause *> Clauses;
  bool FirstClause = true;
  while (getCurToken().isNot(tok::annot_pragma_openacc_end)) {
    // Comma is optional in a clause-list.
    if (!FirstClause && getCurToken().is(tok::comma))
      ConsumeToken();
    FirstClause = false;

    OpenACCClauseParseResult Result = ParseOpenACCClause(Clauses, DirKind);
    if (OpenACCClause *Clause = Result.getPointer()) {
      Clauses.push_back(Clause);
    } else if (Result.getInt() == OpenACCParseCanContinue::Cannot) {
      // Recovering from a bad clause is really difficult, so we just give up on
      // error.
      SkipUntilEndOfDirective(*this);
      return Clauses;
    }
  }
  return Clauses;
}

Parser::OpenACCIntExprParseResult
Parser::ParseOpenACCIntExpr(OpenACCDirectiveKind DK, OpenACCClauseKind CK,
                            SourceLocation Loc) {
  ExprResult ER = ParseAssignmentExpression();

  // If the actual parsing failed, we don't know the state of the parse, so
  // don't try to continue.
  if (!ER.isUsable())
    return {ER, OpenACCParseCanContinue::Cannot};

  // Parsing can continue after the initial assignment expression parsing, so
  // even if there was a typo, we can continue.
  ER = getActions().CorrectDelayedTyposInExpr(ER);
  if (!ER.isUsable())
    return {ER, OpenACCParseCanContinue::Can};

  return {getActions().OpenACC().ActOnIntExpr(DK, CK, Loc, ER.get()),
          OpenACCParseCanContinue::Can};
}

bool Parser::ParseOpenACCIntExprList(OpenACCDirectiveKind DK,
                                     OpenACCClauseKind CK, SourceLocation Loc,
                                     llvm::SmallVectorImpl<Expr *> &IntExprs) {
  OpenACCIntExprParseResult CurResult = ParseOpenACCIntExpr(DK, CK, Loc);

  if (!CurResult.first.isUsable() &&
      CurResult.second == OpenACCParseCanContinue::Cannot) {
    SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
              Parser::StopBeforeMatch);
    return true;
  }

  IntExprs.push_back(CurResult.first.get());

  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    ExpectAndConsume(tok::comma);

    CurResult = ParseOpenACCIntExpr(DK, CK, Loc);

    if (!CurResult.first.isUsable() &&
        CurResult.second == OpenACCParseCanContinue::Cannot) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
                Parser::StopBeforeMatch);
      return true;
    }
    IntExprs.push_back(CurResult.first.get());
  }
  return false;
}

/// OpenACC 3.3 Section 2.4:
/// The argument to the device_type clause is a comma-separated list of one or
/// more device architecture name identifiers, or an asterisk.
///
/// The syntax of the device_type clause is
/// device_type( * )
/// device_type( device-type-list )
///
/// The device_type clause may be abbreviated to dtype.
bool Parser::ParseOpenACCDeviceTypeList(
    llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>> &Archs) {

  if (expectIdentifierOrKeyword(*this)) {
    SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
              Parser::StopBeforeMatch);
    return true;
  }
  IdentifierInfo *Ident = getCurToken().getIdentifierInfo();
  Archs.emplace_back(Ident, ConsumeToken());

  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    ExpectAndConsume(tok::comma);

    if (expectIdentifierOrKeyword(*this)) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
                Parser::StopBeforeMatch);
      return true;
    }
    Ident = getCurToken().getIdentifierInfo();
    Archs.emplace_back(Ident, ConsumeToken());
  }
  return false;
}

/// OpenACC 3.3 Section 2.9:
/// size-expr is one of:
//    *
//    int-expr
// Note that this is specified under 'gang-arg-list', but also applies to 'tile'
// via reference.
bool Parser::ParseOpenACCSizeExpr() {
  // FIXME: Ensure these are constant expressions.

  // The size-expr ends up being ambiguous when only looking at the current
  // token, as it could be a deref of a variable/expression.
  if (getCurToken().is(tok::star) &&
      NextToken().isOneOf(tok::comma, tok::r_paren,
                          tok::annot_pragma_openacc_end)) {
    ConsumeToken();
    return false;
  }

  return getActions()
      .CorrectDelayedTyposInExpr(ParseAssignmentExpression())
      .isInvalid();
}

bool Parser::ParseOpenACCSizeExprList() {
  if (ParseOpenACCSizeExpr()) {
    SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
              Parser::StopBeforeMatch);
    return false;
  }

  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    ExpectAndConsume(tok::comma);

    if (ParseOpenACCSizeExpr()) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
                Parser::StopBeforeMatch);
      return false;
    }
  }
  return false;
}

/// OpenACC 3.3 Section 2.9:
///
/// where gang-arg is one of:
/// [num:]int-expr
/// dim:int-expr
/// static:size-expr
bool Parser::ParseOpenACCGangArg(SourceLocation GangLoc) {

  if (isOpenACCSpecialToken(OpenACCSpecialTokenKind::Static, getCurToken()) &&
      NextToken().is(tok::colon)) {
    // 'static' just takes a size-expr, which is an int-expr or an asterisk.
    ConsumeToken();
    ConsumeToken();
    return ParseOpenACCSizeExpr();
  }

  if (isOpenACCSpecialToken(OpenACCSpecialTokenKind::Dim, getCurToken()) &&
      NextToken().is(tok::colon)) {
    ConsumeToken();
    ConsumeToken();
    return ParseOpenACCIntExpr(OpenACCDirectiveKind::Invalid,
                               OpenACCClauseKind::Gang, GangLoc)
        .first.isInvalid();
  }

  if (isOpenACCSpecialToken(OpenACCSpecialTokenKind::Num, getCurToken()) &&
      NextToken().is(tok::colon)) {
    ConsumeToken();
    ConsumeToken();
    // Fallthrough to the 'int-expr' handling for when 'num' is omitted.
  }
  // This is just the 'num' case where 'num' is optional.
  return ParseOpenACCIntExpr(OpenACCDirectiveKind::Invalid,
                             OpenACCClauseKind::Gang, GangLoc)
      .first.isInvalid();
}

bool Parser::ParseOpenACCGangArgList(SourceLocation GangLoc) {
  if (ParseOpenACCGangArg(GangLoc)) {
    SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
              Parser::StopBeforeMatch);
    return false;
  }

  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    ExpectAndConsume(tok::comma);

    if (ParseOpenACCGangArg(GangLoc)) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end,
                Parser::StopBeforeMatch);
      return false;
    }
  }
  return false;
}

// The OpenACC Clause List is a comma or space-delimited list of clauses (see
// the comment on ParseOpenACCClauseList).  The concept of a 'clause' doesn't
// really have its owner grammar and each individual one has its own definition.
// However, they all are named with a single-identifier (or auto/default!)
// token, followed in some cases by either braces or parens.
Parser::OpenACCClauseParseResult
Parser::ParseOpenACCClause(ArrayRef<const OpenACCClause *> ExistingClauses,
                           OpenACCDirectiveKind DirKind) {
  // A number of clause names are actually keywords, so accept a keyword that
  // can be converted to a name.
  if (expectIdentifierOrKeyword(*this))
    return OpenACCCannotContinue();

  OpenACCClauseKind Kind = getOpenACCClauseKind(getCurToken());

  if (Kind == OpenACCClauseKind::Invalid) {
    Diag(getCurToken(), diag::err_acc_invalid_clause)
        << getCurToken().getIdentifierInfo();
    return OpenACCCannotContinue();
  }

  // Consume the clause name.
  SourceLocation ClauseLoc = ConsumeToken();

  return ParseOpenACCClauseParams(ExistingClauses, DirKind, Kind, ClauseLoc);
}

Parser::OpenACCClauseParseResult Parser::ParseOpenACCClauseParams(
    ArrayRef<const OpenACCClause *> ExistingClauses,
    OpenACCDirectiveKind DirKind, OpenACCClauseKind ClauseKind,
    SourceLocation ClauseLoc) {
  BalancedDelimiterTracker Parens(*this, tok::l_paren,
                                  tok::annot_pragma_openacc_end);
  SemaOpenACC::OpenACCParsedClause ParsedClause(DirKind, ClauseKind, ClauseLoc);

  if (ClauseHasRequiredParens(DirKind, ClauseKind)) {
    if (Parens.expectAndConsume()) {
      // We are missing a paren, so assume that the person just forgot the
      // parameter.  Return 'false' so we try to continue on and parse the next
      // clause.
      SkipUntil(tok::comma, tok::r_paren, tok::annot_pragma_openacc_end,
                Parser::StopBeforeMatch);
      return OpenACCCanContinue();
    }
    ParsedClause.setLParenLoc(Parens.getOpenLocation());

    switch (ClauseKind) {
    case OpenACCClauseKind::Default: {
      Token DefKindTok = getCurToken();

      if (expectIdentifierOrKeyword(*this)) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }

      ConsumeToken();

      OpenACCDefaultClauseKind DefKind =
          getOpenACCDefaultClauseKind(DefKindTok);

      if (DefKind == OpenACCDefaultClauseKind::Invalid) {
        Diag(DefKindTok, diag::err_acc_invalid_default_clause_kind);
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }

      ParsedClause.setDefaultDetails(DefKind);
      break;
    }
    case OpenACCClauseKind::If: {
      ExprResult CondExpr = ParseOpenACCConditionExpr();
      ParsedClause.setConditionDetails(CondExpr.isUsable() ? CondExpr.get()
                                                           : nullptr);

      if (CondExpr.isInvalid()) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }

      break;
    }
    case OpenACCClauseKind::CopyIn:
    case OpenACCClauseKind::PCopyIn:
    case OpenACCClauseKind::PresentOrCopyIn: {
      bool IsReadOnly = tryParseAndConsumeSpecialTokenKind(
          *this, OpenACCSpecialTokenKind::ReadOnly, ClauseKind);
      ParsedClause.setVarListDetails(ParseOpenACCVarList(ClauseKind),
                                     IsReadOnly,
                                     /*IsZero=*/false);
      break;
    }
    case OpenACCClauseKind::Create:
    case OpenACCClauseKind::PCreate:
    case OpenACCClauseKind::PresentOrCreate:
    case OpenACCClauseKind::CopyOut:
    case OpenACCClauseKind::PCopyOut:
    case OpenACCClauseKind::PresentOrCopyOut: {
      bool IsZero = tryParseAndConsumeSpecialTokenKind(
          *this, OpenACCSpecialTokenKind::Zero, ClauseKind);
      ParsedClause.setVarListDetails(ParseOpenACCVarList(ClauseKind),
                                     /*IsReadOnly=*/false, IsZero);
      break;
    }
    case OpenACCClauseKind::Reduction: {
      // If we're missing a clause-kind (or it is invalid), see if we can parse
      // the var-list anyway.
      OpenACCReductionOperator Op = ParseReductionOperator(*this);
      ParsedClause.setReductionDetails(Op, ParseOpenACCVarList(ClauseKind));
      break;
    }
    case OpenACCClauseKind::Self:
      // The 'self' clause is a var-list instead of a 'condition' in the case of
      // the 'update' clause, so we have to handle it here.  U se an assert to
      // make sure we get the right differentiator.
      assert(DirKind == OpenACCDirectiveKind::Update);
      [[fallthrough]];
    case OpenACCClauseKind::Delete:
    case OpenACCClauseKind::Detach:
    case OpenACCClauseKind::Device:
    case OpenACCClauseKind::DeviceResident:
    case OpenACCClauseKind::Host:
    case OpenACCClauseKind::Link:
    case OpenACCClauseKind::UseDevice:
      ParseOpenACCVarList(ClauseKind);
      break;
    case OpenACCClauseKind::Attach:
    case OpenACCClauseKind::DevicePtr:
      ParsedClause.setVarListDetails(ParseOpenACCVarList(ClauseKind),
                                     /*IsReadOnly=*/false, /*IsZero=*/false);
      break;
    case OpenACCClauseKind::Copy:
    case OpenACCClauseKind::PCopy:
    case OpenACCClauseKind::PresentOrCopy:
    case OpenACCClauseKind::FirstPrivate:
    case OpenACCClauseKind::NoCreate:
    case OpenACCClauseKind::Present:
    case OpenACCClauseKind::Private:
      ParsedClause.setVarListDetails(ParseOpenACCVarList(ClauseKind),
                                     /*IsReadOnly=*/false, /*IsZero=*/false);
      break;
    case OpenACCClauseKind::Collapse: {
      tryParseAndConsumeSpecialTokenKind(*this, OpenACCSpecialTokenKind::Force,
                                         ClauseKind);
      ExprResult NumLoops =
          getActions().CorrectDelayedTyposInExpr(ParseConstantExpression());
      if (NumLoops.isInvalid()) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }
      break;
    }
    case OpenACCClauseKind::Bind: {
      ExprResult BindArg = ParseOpenACCBindClauseArgument();
      if (BindArg.isInvalid()) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }
      break;
    }
    case OpenACCClauseKind::NumGangs: {
      llvm::SmallVector<Expr *> IntExprs;

      if (ParseOpenACCIntExprList(OpenACCDirectiveKind::Invalid,
                                  OpenACCClauseKind::NumGangs, ClauseLoc,
                                  IntExprs)) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }
      ParsedClause.setIntExprDetails(std::move(IntExprs));
      break;
    }
    case OpenACCClauseKind::NumWorkers:
    case OpenACCClauseKind::DeviceNum:
    case OpenACCClauseKind::DefaultAsync:
    case OpenACCClauseKind::VectorLength: {
      ExprResult IntExpr = ParseOpenACCIntExpr(OpenACCDirectiveKind::Invalid,
                                               ClauseKind, ClauseLoc)
                               .first;
      if (IntExpr.isInvalid()) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }

      // TODO OpenACC: as we implement the 'rest' of the above, this 'if' should
      // be removed leaving just the 'setIntExprDetails'.
      if (ClauseKind == OpenACCClauseKind::NumWorkers ||
          ClauseKind == OpenACCClauseKind::VectorLength)
        ParsedClause.setIntExprDetails(IntExpr.get());

      break;
    }
    case OpenACCClauseKind::DType:
    case OpenACCClauseKind::DeviceType: {
      llvm::SmallVector<std::pair<IdentifierInfo *, SourceLocation>> Archs;
      if (getCurToken().is(tok::star)) {
        // FIXME: We want to mark that this is an 'everything else' type of
        // device_type in Sema.
        ParsedClause.setDeviceTypeDetails({{nullptr, ConsumeToken()}});
      } else if (!ParseOpenACCDeviceTypeList(Archs)) {
        ParsedClause.setDeviceTypeDetails(std::move(Archs));
      } else {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }
      break;
    }
    case OpenACCClauseKind::Tile:
      if (ParseOpenACCSizeExprList()) {
        Parens.skipToEnd();
        return OpenACCCanContinue();
      }
      break;
    default:
      llvm_unreachable("Not a required parens type?");
    }

    ParsedClause.setEndLoc(getCurToken().getLocation());

    if (Parens.consumeClose())
      return OpenACCCannotContinue();

  } else if (ClauseHasOptionalParens(DirKind, ClauseKind)) {
    if (!Parens.consumeOpen()) {
      ParsedClause.setLParenLoc(Parens.getOpenLocation());
      switch (ClauseKind) {
      case OpenACCClauseKind::Self: {
        assert(DirKind != OpenACCDirectiveKind::Update);
        ExprResult CondExpr = ParseOpenACCConditionExpr();
        ParsedClause.setConditionDetails(CondExpr.isUsable() ? CondExpr.get()
                                                             : nullptr);

        if (CondExpr.isInvalid()) {
          Parens.skipToEnd();
          return OpenACCCanContinue();
        }
        break;
      }
      case OpenACCClauseKind::Vector:
      case OpenACCClauseKind::Worker: {
        tryParseAndConsumeSpecialTokenKind(*this,
                                           ClauseKind ==
                                                   OpenACCClauseKind::Vector
                                               ? OpenACCSpecialTokenKind::Length
                                               : OpenACCSpecialTokenKind::Num,
                                           ClauseKind);
        ExprResult IntExpr = ParseOpenACCIntExpr(OpenACCDirectiveKind::Invalid,
                                                 ClauseKind, ClauseLoc)
                                 .first;
        if (IntExpr.isInvalid()) {
          Parens.skipToEnd();
          return OpenACCCanContinue();
        }
        break;
      }
      case OpenACCClauseKind::Async: {
        ExprResult AsyncArg =
            ParseOpenACCAsyncArgument(OpenACCDirectiveKind::Invalid,
                                      OpenACCClauseKind::Async, ClauseLoc)
                .first;
        ParsedClause.setIntExprDetails(AsyncArg.isUsable() ? AsyncArg.get()
                                                           : nullptr);
        if (AsyncArg.isInvalid()) {
          Parens.skipToEnd();
          return OpenACCCanContinue();
        }
        break;
      }
      case OpenACCClauseKind::Gang:
        if (ParseOpenACCGangArgList(ClauseLoc)) {
          Parens.skipToEnd();
          return OpenACCCanContinue();
        }
        break;
      case OpenACCClauseKind::Wait: {
        OpenACCWaitParseInfo Info =
            ParseOpenACCWaitArgument(ClauseLoc,
                                     /*IsDirective=*/false);
        if (Info.Failed) {
          Parens.skipToEnd();
          return OpenACCCanContinue();
        }

        ParsedClause.setWaitDetails(Info.DevNumExpr, Info.QueuesLoc,
                                    std::move(Info.QueueIdExprs));
        break;
      }
      default:
        llvm_unreachable("Not an optional parens type?");
      }
      ParsedClause.setEndLoc(getCurToken().getLocation());
      if (Parens.consumeClose())
        return OpenACCCannotContinue();
    } else {
      // If we have optional parens, make sure we set the end-location to the
      // clause, as we are a 'single token' clause.
      ParsedClause.setEndLoc(ClauseLoc);
    }
  } else {
    ParsedClause.setEndLoc(ClauseLoc);
  }
  return OpenACCSuccess(
      Actions.OpenACC().ActOnClause(ExistingClauses, ParsedClause));
}

/// OpenACC 3.3 section 2.16:
/// In this section and throughout the specification, the term async-argument
/// means a nonnegative scalar integer expression (int for C or C++, integer for
/// Fortran), or one of the special values acc_async_noval or acc_async_sync, as
/// defined in the C header file and the Fortran openacc module. The special
/// values are negative values, so as not to conflict with a user-specified
/// nonnegative async-argument.
Parser::OpenACCIntExprParseResult
Parser::ParseOpenACCAsyncArgument(OpenACCDirectiveKind DK, OpenACCClauseKind CK,
                                  SourceLocation Loc) {
  return ParseOpenACCIntExpr(DK, CK, Loc);
}

/// OpenACC 3.3, section 2.16:
/// In this section and throughout the specification, the term wait-argument
/// means:
/// [ devnum : int-expr : ] [ queues : ] async-argument-list
Parser::OpenACCWaitParseInfo
Parser::ParseOpenACCWaitArgument(SourceLocation Loc, bool IsDirective) {
  OpenACCWaitParseInfo Result;
  // [devnum : int-expr : ]
  if (isOpenACCSpecialToken(OpenACCSpecialTokenKind::DevNum, Tok) &&
      NextToken().is(tok::colon)) {
    // Consume devnum.
    ConsumeToken();
    // Consume colon.
    ConsumeToken();

    OpenACCIntExprParseResult Res = ParseOpenACCIntExpr(
        IsDirective ? OpenACCDirectiveKind::Wait
                    : OpenACCDirectiveKind::Invalid,
        IsDirective ? OpenACCClauseKind::Invalid : OpenACCClauseKind::Wait,
        Loc);
    if (Res.first.isInvalid() &&
        Res.second == OpenACCParseCanContinue::Cannot) {
      Result.Failed = true;
      return Result;
    }

    if (ExpectAndConsume(tok::colon)) {
      Result.Failed = true;
      return Result;
    }

    Result.DevNumExpr = Res.first.get();
  }

  // [ queues : ]
  if (isOpenACCSpecialToken(OpenACCSpecialTokenKind::Queues, Tok) &&
      NextToken().is(tok::colon)) {
    // Consume queues.
    Result.QueuesLoc = ConsumeToken();
    // Consume colon.
    ConsumeToken();
  }

  // OpenACC 3.3, section 2.16:
  // the term 'async-argument' means a nonnegative scalar integer expression, or
  // one of the special values 'acc_async_noval' or 'acc_async_sync', as defined
  // in the C header file and the Fortran opacc module.
  bool FirstArg = true;
  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    if (!FirstArg) {
      if (ExpectAndConsume(tok::comma)) {
        Result.Failed = true;
        return Result;
      }
    }
    FirstArg = false;

    OpenACCIntExprParseResult Res = ParseOpenACCAsyncArgument(
        IsDirective ? OpenACCDirectiveKind::Wait
                    : OpenACCDirectiveKind::Invalid,
        IsDirective ? OpenACCClauseKind::Invalid : OpenACCClauseKind::Wait,
        Loc);

    if (Res.first.isInvalid() &&
        Res.second == OpenACCParseCanContinue::Cannot) {
      Result.Failed = true;
      return Result;
    }

    Result.QueueIdExprs.push_back(Res.first.get());
  }

  return Result;
}

ExprResult Parser::ParseOpenACCIDExpression() {
  ExprResult Res;
  if (getLangOpts().CPlusPlus) {
    Res = ParseCXXIdExpression(/*isAddressOfOperand=*/true);
  } else {
    // There isn't anything quite the same as ParseCXXIdExpression for C, so we
    // need to get the identifier, then call into Sema ourselves.

    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected) << tok::identifier;
      return ExprError();
    }

    Token FuncName = getCurToken();
    UnqualifiedId Name;
    CXXScopeSpec ScopeSpec;
    SourceLocation TemplateKWLoc;
    Name.setIdentifier(FuncName.getIdentifierInfo(), ConsumeToken());

    // Ensure this is a valid identifier. We don't accept causing implicit
    // function declarations per the spec, so always claim to not have trailing
    // L Paren.
    Res = Actions.ActOnIdExpression(getCurScope(), ScopeSpec, TemplateKWLoc,
                                    Name, /*HasTrailingLParen=*/false,
                                    /*isAddressOfOperand=*/false);
  }

  return getActions().CorrectDelayedTyposInExpr(Res);
}

ExprResult Parser::ParseOpenACCBindClauseArgument() {
  // OpenACC 3.3 section 2.15:
  // The bind clause specifies the name to use when calling the procedure on a
  // device other than the host. If the name is specified as an identifier, it
  // is called as if that name were specified in the language being compiled. If
  // the name is specified as a string, the string is used for the procedure
  // name unmodified.
  if (getCurToken().is(tok::r_paren)) {
    Diag(getCurToken(), diag::err_acc_incorrect_bind_arg);
    return ExprError();
  }

  if (tok::isStringLiteral(getCurToken().getKind()))
    return getActions().CorrectDelayedTyposInExpr(ParseStringLiteralExpression(
        /*AllowUserDefinedLiteral=*/false, /*Unevaluated=*/true));

  return ParseOpenACCIDExpression();
}

/// OpenACC 3.3, section 1.6:
/// In this spec, a 'var' (in italics) is one of the following:
/// - a variable name (a scalar, array, or composite variable name)
/// - a subarray specification with subscript ranges
/// - an array element
/// - a member of a composite variable
/// - a common block name between slashes (fortran only)
Parser::OpenACCVarParseResult Parser::ParseOpenACCVar(OpenACCClauseKind CK) {
  OpenACCArraySectionRAII ArraySections(*this);

  ExprResult Res = ParseAssignmentExpression();
  if (!Res.isUsable())
    return {Res, OpenACCParseCanContinue::Cannot};

  Res = getActions().CorrectDelayedTyposInExpr(Res.get());
  if (!Res.isUsable())
    return {Res, OpenACCParseCanContinue::Can};

  Res = getActions().OpenACC().ActOnVar(CK, Res.get());

  return {Res, OpenACCParseCanContinue::Can};
}

llvm::SmallVector<Expr *> Parser::ParseOpenACCVarList(OpenACCClauseKind CK) {
  llvm::SmallVector<Expr *> Vars;

  auto [Res, CanContinue] = ParseOpenACCVar(CK);
  if (Res.isUsable()) {
    Vars.push_back(Res.get());
  } else if (CanContinue == OpenACCParseCanContinue::Cannot) {
    SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end, StopBeforeMatch);
    return Vars;
  }

  while (!getCurToken().isOneOf(tok::r_paren, tok::annot_pragma_openacc_end)) {
    ExpectAndConsume(tok::comma);

    auto [Res, CanContinue] = ParseOpenACCVar(CK);

    if (Res.isUsable()) {
      Vars.push_back(Res.get());
    } else if (CanContinue == OpenACCParseCanContinue::Cannot) {
      SkipUntil(tok::r_paren, tok::annot_pragma_openacc_end, StopBeforeMatch);
      return Vars;
    }
  }
  return Vars;
}

/// OpenACC 3.3, section 2.10:
/// In C and C++, the syntax of the cache directive is:
///
/// #pragma acc cache ([readonly:]var-list) new-line
void Parser::ParseOpenACCCacheVarList() {
  // If this is the end of the line, just return 'false' and count on the close
  // paren diagnostic to catch the issue.
  if (getCurToken().isAnnotation())
    return;

  // The VarList is an optional `readonly:` followed by a list of a variable
  // specifications. Consume something that looks like a 'tag', and diagnose if
  // it isn't 'readonly'.
  if (tryParseAndConsumeSpecialTokenKind(*this,
                                         OpenACCSpecialTokenKind::ReadOnly,
                                         OpenACCDirectiveKind::Cache)) {
    // FIXME: Record that this is a 'readonly' so that we can use that during
    // Sema/AST generation.
  }

  // ParseOpenACCVarList should leave us before a r-paren, so no need to skip
  // anything here.
  ParseOpenACCVarList(OpenACCClauseKind::Invalid);
}

Parser::OpenACCDirectiveParseInfo
Parser::ParseOpenACCDirective() {
  SourceLocation StartLoc = ConsumeAnnotationToken();
  SourceLocation DirLoc = getCurToken().getLocation();
  OpenACCDirectiveKind DirKind = ParseOpenACCDirectiveKind(*this);

  getActions().OpenACC().ActOnConstruct(DirKind, DirLoc);

  // Once we've parsed the construct/directive name, some have additional
  // specifiers that need to be taken care of. Atomic has an 'atomic-clause'
  // that needs to be parsed.
  if (DirKind == OpenACCDirectiveKind::Atomic)
    ParseOpenACCAtomicKind(*this);

  // We've successfully parsed the construct/directive name, however a few of
  // the constructs have optional parens that contain further details.
  BalancedDelimiterTracker T(*this, tok::l_paren,
                             tok::annot_pragma_openacc_end);

  if (!T.consumeOpen()) {
    switch (DirKind) {
    default:
      Diag(T.getOpenLocation(), diag::err_acc_invalid_open_paren);
      T.skipToEnd();
      break;
    case OpenACCDirectiveKind::Routine: {
      // Routine has an optional paren-wrapped name of a function in the local
      // scope. We parse the name, emitting any diagnostics
      ExprResult RoutineName = ParseOpenACCIDExpression();
      // If the routine name is invalid, just skip until the closing paren to
      // recover more gracefully.
      if (RoutineName.isInvalid())
        T.skipToEnd();
      else
        T.consumeClose();
      break;
    }
    case OpenACCDirectiveKind::Cache:
      ParseOpenACCCacheVarList();
      // The ParseOpenACCCacheVarList function manages to recover from failures,
      // so we can always consume the close.
      T.consumeClose();
      break;
    case OpenACCDirectiveKind::Wait:
      // OpenACC has an optional paren-wrapped 'wait-argument'.
      if (ParseOpenACCWaitArgument(DirLoc, /*IsDirective=*/true).Failed)
        T.skipToEnd();
      else
        T.consumeClose();
      break;
    }
  } else if (DirKind == OpenACCDirectiveKind::Cache) {
    // Cache's paren var-list is required, so error here if it isn't provided.
    // We know that the consumeOpen above left the first non-paren here, so
    // diagnose, then continue as if it was completely omitted.
    Diag(Tok, diag::err_expected) << tok::l_paren;
  }

  // Parses the list of clauses, if present, plus set up return value.
  OpenACCDirectiveParseInfo ParseInfo{DirKind, StartLoc, DirLoc,
                                      SourceLocation{},
                                      ParseOpenACCClauseList(DirKind)};

  assert(Tok.is(tok::annot_pragma_openacc_end) &&
         "Didn't parse all OpenACC Clauses");
  ParseInfo.EndLoc = ConsumeAnnotationToken();
  assert(ParseInfo.EndLoc.isValid() &&
         "Terminating annotation token not present");

  return ParseInfo;
}

// Parse OpenACC directive on a declaration.
Parser::DeclGroupPtrTy Parser::ParseOpenACCDirectiveDecl() {
  assert(Tok.is(tok::annot_pragma_openacc) && "expected OpenACC Start Token");

  ParsingOpenACCDirectiveRAII DirScope(*this);

  OpenACCDirectiveParseInfo DirInfo = ParseOpenACCDirective();

  if (getActions().OpenACC().ActOnStartDeclDirective(DirInfo.DirKind,
                                                     DirInfo.StartLoc))
    return nullptr;

  // TODO OpenACC: Do whatever decl parsing is required here.
  return DeclGroupPtrTy::make(getActions().OpenACC().ActOnEndDeclDirective());
}

// Parse OpenACC Directive on a Statement.
StmtResult Parser::ParseOpenACCDirectiveStmt() {
  assert(Tok.is(tok::annot_pragma_openacc) && "expected OpenACC Start Token");

  ParsingOpenACCDirectiveRAII DirScope(*this);

  OpenACCDirectiveParseInfo DirInfo = ParseOpenACCDirective();
  if (getActions().OpenACC().ActOnStartStmtDirective(DirInfo.DirKind,
                                                     DirInfo.StartLoc))
    return StmtError();

  StmtResult AssocStmt;
  SemaOpenACC::AssociatedStmtRAII AssocStmtRAII(getActions().OpenACC(),
                                                DirInfo.DirKind);
  if (doesDirectiveHaveAssociatedStmt(DirInfo.DirKind)) {
    ParsingOpenACCDirectiveRAII DirScope(*this, /*Value=*/false);
    ParseScope ACCScope(this, getOpenACCScopeFlags(DirInfo.DirKind));

    AssocStmt = getActions().OpenACC().ActOnAssociatedStmt(
        DirInfo.StartLoc, DirInfo.DirKind, ParseStatement());
  }

  return getActions().OpenACC().ActOnEndStmtDirective(
      DirInfo.DirKind, DirInfo.StartLoc, DirInfo.DirLoc, DirInfo.EndLoc,
      DirInfo.Clauses, AssocStmt);
}
