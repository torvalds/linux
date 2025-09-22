//===--- SemaOpenACC.cpp - Semantic Analysis for OpenACC constructs -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements semantic analysis for OpenACC constructs and
/// clauses.
///
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaOpenACC.h"
#include "clang/AST/StmtOpenACC.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/OpenACCKinds.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Casting.h"

using namespace clang;

namespace {
bool diagnoseConstructAppertainment(SemaOpenACC &S, OpenACCDirectiveKind K,
                                    SourceLocation StartLoc, bool IsStmt) {
  switch (K) {
  default:
  case OpenACCDirectiveKind::Invalid:
    // Nothing to do here, both invalid and unimplemented don't really need to
    // do anything.
    break;
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels:
  case OpenACCDirectiveKind::Loop:
    if (!IsStmt)
      return S.Diag(StartLoc, diag::err_acc_construct_appertainment) << K;
    break;
  }
  return false;
}

bool doesClauseApplyToDirective(OpenACCDirectiveKind DirectiveKind,
                                OpenACCClauseKind ClauseKind) {
  switch (ClauseKind) {
    // FIXME: For each clause as we implement them, we can add the
    // 'legalization' list here.
  case OpenACCClauseKind::Default:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
    case OpenACCDirectiveKind::Data:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::If:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::ExitData:
    case OpenACCDirectiveKind::HostData:
    case OpenACCDirectiveKind::Init:
    case OpenACCDirectiveKind::Shutdown:
    case OpenACCDirectiveKind::Set:
    case OpenACCDirectiveKind::Update:
    case OpenACCDirectiveKind::Wait:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Self:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Update:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::NumGangs:
  case OpenACCClauseKind::NumWorkers:
  case OpenACCClauseKind::VectorLength:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::FirstPrivate:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Private:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Loop:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::NoCreate:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Present:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::Declare:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::Copy:
  case OpenACCClauseKind::PCopy:
  case OpenACCClauseKind::PresentOrCopy:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::Declare:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::CopyIn:
  case OpenACCClauseKind::PCopyIn:
  case OpenACCClauseKind::PresentOrCopyIn:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::Declare:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::CopyOut:
  case OpenACCClauseKind::PCopyOut:
  case OpenACCClauseKind::PresentOrCopyOut:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::ExitData:
    case OpenACCDirectiveKind::Declare:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Create:
  case OpenACCClauseKind::PCreate:
  case OpenACCClauseKind::PresentOrCreate:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::Attach:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::DevicePtr:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::Declare:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Async:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::ExitData:
    case OpenACCDirectiveKind::Set:
    case OpenACCDirectiveKind::Update:
    case OpenACCDirectiveKind::Wait:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }
  case OpenACCClauseKind::Wait:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::EnterData:
    case OpenACCDirectiveKind::ExitData:
    case OpenACCDirectiveKind::Update:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::Seq:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Loop:
    case OpenACCDirectiveKind::Routine:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::Independent:
  case OpenACCClauseKind::Auto:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Loop:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::Reduction:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Loop:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  case OpenACCClauseKind::DeviceType:
  case OpenACCClauseKind::DType:
    switch (DirectiveKind) {
    case OpenACCDirectiveKind::Parallel:
    case OpenACCDirectiveKind::Serial:
    case OpenACCDirectiveKind::Kernels:
    case OpenACCDirectiveKind::Data:
    case OpenACCDirectiveKind::Init:
    case OpenACCDirectiveKind::Shutdown:
    case OpenACCDirectiveKind::Set:
    case OpenACCDirectiveKind::Update:
    case OpenACCDirectiveKind::Loop:
    case OpenACCDirectiveKind::Routine:
    case OpenACCDirectiveKind::ParallelLoop:
    case OpenACCDirectiveKind::SerialLoop:
    case OpenACCDirectiveKind::KernelsLoop:
      return true;
    default:
      return false;
    }

  default:
    // Do nothing so we can go to the 'unimplemented' diagnostic instead.
    return true;
  }
  llvm_unreachable("Invalid clause kind");
}

bool checkAlreadyHasClauseOfKind(
    SemaOpenACC &S, ArrayRef<const OpenACCClause *> ExistingClauses,
    SemaOpenACC::OpenACCParsedClause &Clause) {
  const auto *Itr = llvm::find_if(ExistingClauses, [&](const OpenACCClause *C) {
    return C->getClauseKind() == Clause.getClauseKind();
  });
  if (Itr != ExistingClauses.end()) {
    S.Diag(Clause.getBeginLoc(), diag::err_acc_duplicate_clause_disallowed)
        << Clause.getDirectiveKind() << Clause.getClauseKind();
    S.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    return true;
  }
  return false;
}

bool checkValidAfterDeviceType(
    SemaOpenACC &S, const OpenACCDeviceTypeClause &DeviceTypeClause,
    const SemaOpenACC::OpenACCParsedClause &NewClause) {
  // This is only a requirement on compute and loop constructs so far, so this
  // is fine otherwise.
  if (!isOpenACCComputeDirectiveKind(NewClause.getDirectiveKind()) &&
      NewClause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return false;

  // OpenACC3.3: Section 2.4: Clauses that precede any device_type clause are
  // default clauses.  Clauses that follow a device_type clause up to the end of
  // the directive or up to the next device_type clause are device-specific
  // clauses for the device types specified in the device_type argument.
  //
  // The above implies that despite what the individual text says, these are
  // valid.
  if (NewClause.getClauseKind() == OpenACCClauseKind::DType ||
      NewClause.getClauseKind() == OpenACCClauseKind::DeviceType)
    return false;

  // Implement check from OpenACC3.3: section 2.5.4:
  // Only the async, wait, num_gangs, num_workers, and vector_length clauses may
  // follow a device_type clause.
  if (isOpenACCComputeDirectiveKind(NewClause.getDirectiveKind())) {
    switch (NewClause.getClauseKind()) {
    case OpenACCClauseKind::Async:
    case OpenACCClauseKind::Wait:
    case OpenACCClauseKind::NumGangs:
    case OpenACCClauseKind::NumWorkers:
    case OpenACCClauseKind::VectorLength:
      return false;
    default:
      break;
    }
  } else if (NewClause.getDirectiveKind() == OpenACCDirectiveKind::Loop) {
    // Implement check from OpenACC3.3: section 2.9:
    // Only the collapse, gang, worker, vector, seq, independent, auto, and tile
    // clauses may follow a device_type clause.
    switch (NewClause.getClauseKind()) {
    case OpenACCClauseKind::Collapse:
    case OpenACCClauseKind::Gang:
    case OpenACCClauseKind::Worker:
    case OpenACCClauseKind::Vector:
    case OpenACCClauseKind::Seq:
    case OpenACCClauseKind::Independent:
    case OpenACCClauseKind::Auto:
    case OpenACCClauseKind::Tile:
      return false;
    default:
      break;
    }
  }
  S.Diag(NewClause.getBeginLoc(), diag::err_acc_clause_after_device_type)
      << NewClause.getClauseKind() << DeviceTypeClause.getClauseKind()
      << isOpenACCComputeDirectiveKind(NewClause.getDirectiveKind())
      << NewClause.getDirectiveKind();
  S.Diag(DeviceTypeClause.getBeginLoc(), diag::note_acc_previous_clause_here);
  return true;
}

class SemaOpenACCClauseVisitor {
  SemaOpenACC &SemaRef;
  ASTContext &Ctx;
  ArrayRef<const OpenACCClause *> ExistingClauses;
  bool NotImplemented = false;

  OpenACCClause *isNotImplemented() {
    NotImplemented = true;
    return nullptr;
  }

public:
  SemaOpenACCClauseVisitor(SemaOpenACC &S,
                           ArrayRef<const OpenACCClause *> ExistingClauses)
      : SemaRef(S), Ctx(S.getASTContext()), ExistingClauses(ExistingClauses) {}
  // Once we've implemented everything, we shouldn't need this infrastructure.
  // But in the meantime, we use this to help decide whether the clause was
  // handled for this directive.
  bool diagNotImplemented() { return NotImplemented; }

  OpenACCClause *Visit(SemaOpenACC::OpenACCParsedClause &Clause) {
    switch (Clause.getClauseKind()) {
  case OpenACCClauseKind::Gang:
  case OpenACCClauseKind::Worker:
  case OpenACCClauseKind::Vector: {
    // TODO OpenACC: These are only implemented enough for the 'seq' diagnostic,
    // otherwise treats itself as unimplemented.  When we implement these, we
    // can remove them from here.

    // OpenACC 3.3 2.9:
    // A 'gang', 'worker', or 'vector' clause may not appear if a 'seq' clause
    // appears.
    const auto *Itr =
        llvm::find_if(ExistingClauses, llvm::IsaPred<OpenACCSeqClause>);

    if (Itr != ExistingClauses.end()) {
      SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_clause_cannot_combine)
          << Clause.getClauseKind() << (*Itr)->getClauseKind();
      SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    }
    return isNotImplemented();
  }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  case OpenACCClauseKind::CLAUSE_NAME:                                         \
    return Visit##CLAUSE_NAME##Clause(Clause);
#define CLAUSE_ALIAS(ALIAS, CLAUSE_NAME, DEPRECATED)                           \
  case OpenACCClauseKind::ALIAS:                                               \
  if (DEPRECATED)                                                              \
    SemaRef.Diag(Clause.getBeginLoc(), diag::warn_acc_deprecated_alias_name)   \
        << Clause.getClauseKind() << OpenACCClauseKind::CLAUSE_NAME;           \
  return Visit##CLAUSE_NAME##Clause(Clause);
#include "clang/Basic/OpenACCClauses.def"
    default:
      return isNotImplemented();
    }
    llvm_unreachable("Invalid clause kind");
  }

#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  OpenACCClause *Visit##CLAUSE_NAME##Clause(                                   \
      SemaOpenACC::OpenACCParsedClause &Clause);
#include "clang/Basic/OpenACCClauses.def"
};

OpenACCClause *SemaOpenACCClauseVisitor::VisitDefaultClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // Don't add an invalid clause to the AST.
  if (Clause.getDefaultClauseKind() == OpenACCDefaultClauseKind::Invalid)
    return nullptr;

  // OpenACC 3.3, Section 2.5.4:
  // At most one 'default' clause may appear, and it must have a value of
  // either 'none' or 'present'.
  // Second half of the sentence is diagnosed during parsing.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  return OpenACCDefaultClause::Create(
      Ctx, Clause.getDefaultClauseKind(), Clause.getBeginLoc(),
      Clause.getLParenLoc(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitIfClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  // The parser has ensured that we have a proper condition expr, so there
  // isn't really much to do here.

  // If the 'if' clause is true, it makes the 'self' clause have no effect,
  // diagnose that here.
  // TODO OpenACC: When we add these two to other constructs, we might not
  // want to warn on this (for example, 'update').
  const auto *Itr =
      llvm::find_if(ExistingClauses, llvm::IsaPred<OpenACCSelfClause>);
  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::warn_acc_if_self_conflict);
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
  }

  return OpenACCIfClause::Create(Ctx, Clause.getBeginLoc(),
                                 Clause.getLParenLoc(),
                                 Clause.getConditionExpr(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitSelfClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // TODO OpenACC: When we implement this for 'update', this takes a
  // 'var-list' instead of a condition expression, so semantics/handling has
  // to happen differently here.

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  // If the 'if' clause is true, it makes the 'self' clause have no effect,
  // diagnose that here.
  // TODO OpenACC: When we add these two to other constructs, we might not
  // want to warn on this (for example, 'update').
  const auto *Itr =
      llvm::find_if(ExistingClauses, llvm::IsaPred<OpenACCIfClause>);
  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::warn_acc_if_self_conflict);
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
  }
  return OpenACCSelfClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.getConditionExpr(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitNumGangsClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  // num_gangs requires at least 1 int expr in all forms.  Diagnose here, but
  // allow us to continue, an empty clause might be useful for future
  // diagnostics.
  if (Clause.getIntExprs().empty())
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_num_gangs_num_args)
        << /*NoArgs=*/0;

  unsigned MaxArgs =
      (Clause.getDirectiveKind() == OpenACCDirectiveKind::Parallel ||
       Clause.getDirectiveKind() == OpenACCDirectiveKind::ParallelLoop)
          ? 3
          : 1;
  // The max number of args differs between parallel and other constructs.
  // Again, allow us to continue for the purposes of future diagnostics.
  if (Clause.getIntExprs().size() > MaxArgs)
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_num_gangs_num_args)
        << /*NoArgs=*/1 << Clause.getDirectiveKind() << MaxArgs
        << Clause.getIntExprs().size();

  // OpenACC 3.3 Section 2.5.4:
  // A reduction clause may not appear on a parallel construct with a
  // num_gangs clause that has more than one argument.
  if (Clause.getDirectiveKind() == OpenACCDirectiveKind::Parallel &&
      Clause.getIntExprs().size() > 1) {
    auto *Parallel =
        llvm::find_if(ExistingClauses, llvm::IsaPred<OpenACCReductionClause>);

    if (Parallel != ExistingClauses.end()) {
      SemaRef.Diag(Clause.getBeginLoc(),
                   diag::err_acc_reduction_num_gangs_conflict)
          << Clause.getIntExprs().size();
      SemaRef.Diag((*Parallel)->getBeginLoc(),
                   diag::note_acc_previous_clause_here);
      return nullptr;
    }
  }
  return OpenACCNumGangsClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getIntExprs(),
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitNumWorkersClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  assert(Clause.getIntExprs().size() == 1 &&
         "Invalid number of expressions for NumWorkers");
  return OpenACCNumWorkersClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getIntExprs()[0],
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitVectorLengthClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  assert(Clause.getIntExprs().size() == 1 &&
         "Invalid number of expressions for NumWorkers");
  return OpenACCVectorLengthClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getIntExprs()[0],
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitAsyncClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // There is no prose in the standard that says duplicates aren't allowed,
  // but this diagnostic is present in other compilers, as well as makes
  // sense.
  if (checkAlreadyHasClauseOfKind(SemaRef, ExistingClauses, Clause))
    return nullptr;

  assert(Clause.getNumIntExprs() < 2 &&
         "Invalid number of expressions for Async");
  return OpenACCAsyncClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.getNumIntExprs() != 0 ? Clause.getIntExprs()[0] : nullptr,
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitPrivateClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' and 'loop'
  // constructs, and 'compute'/'loop' constructs are the only construct that
  // can do anything with this yet, so skip/treat as unimplemented in this
  // case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()) &&
      Clause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return isNotImplemented();

  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCPrivateClause::Create(Ctx, Clause.getBeginLoc(),
                                      Clause.getLParenLoc(),
                                      Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitFirstPrivateClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCFirstPrivateClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getVarList(),
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitNoCreateClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCNoCreateClause::Create(Ctx, Clause.getBeginLoc(),
                                       Clause.getLParenLoc(),
                                       Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitPresentClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCPresentClause::Create(Ctx, Clause.getBeginLoc(),
                                      Clause.getLParenLoc(),
                                      Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitCopyClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCCopyClause::Create(
      Ctx, Clause.getClauseKind(), Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitCopyInClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCCopyInClause::Create(
      Ctx, Clause.getClauseKind(), Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.isReadOnly(), Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitCopyOutClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCCopyOutClause::Create(
      Ctx, Clause.getClauseKind(), Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.isZero(), Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitCreateClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();
  // ActOnVar ensured that everything is a valid variable reference, so there
  // really isn't anything to do here. GCC does some duplicate-finding, though
  // it isn't apparent in the standard where this is justified.

  return OpenACCCreateClause::Create(
      Ctx, Clause.getClauseKind(), Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.isZero(), Clause.getVarList(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitAttachClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // ActOnVar ensured that everything is a valid variable reference, but we
  // still have to make sure it is a pointer type.
  llvm::SmallVector<Expr *> VarList{Clause.getVarList()};
  llvm::erase_if(VarList, [&](Expr *E) {
    return SemaRef.CheckVarIsPointerType(OpenACCClauseKind::Attach, E);
  });
  Clause.setVarListDetails(VarList,
                           /*IsReadOnly=*/false, /*IsZero=*/false);
  return OpenACCAttachClause::Create(Ctx, Clause.getBeginLoc(),
                                     Clause.getLParenLoc(), Clause.getVarList(),
                                     Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitDevicePtrClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // ActOnVar ensured that everything is a valid variable reference, but we
  // still have to make sure it is a pointer type.
  llvm::SmallVector<Expr *> VarList{Clause.getVarList()};
  llvm::erase_if(VarList, [&](Expr *E) {
    return SemaRef.CheckVarIsPointerType(OpenACCClauseKind::DevicePtr, E);
  });
  Clause.setVarListDetails(VarList,
                           /*IsReadOnly=*/false, /*IsZero=*/false);

  return OpenACCDevicePtrClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getVarList(),
      Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitWaitClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  return OpenACCWaitClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getDevNumExpr(),
      Clause.getQueuesLoc(), Clause.getQueueIdExprs(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitDeviceTypeClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' and 'loop'
  // constructs, and 'compute'/'loop' constructs are the only construct that
  // can do anything with this yet, so skip/treat as unimplemented in this
  // case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()) &&
      Clause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return isNotImplemented();

  // TODO OpenACC: Once we get enough of the CodeGen implemented that we have
  // a source for the list of valid architectures, we need to warn on unknown
  // identifiers here.

  return OpenACCDeviceTypeClause::Create(
      Ctx, Clause.getClauseKind(), Clause.getBeginLoc(), Clause.getLParenLoc(),
      Clause.getDeviceTypeArchitectures(), Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitAutoClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'loop' constructs, and it is
  // the only construct that can do anything with this, so skip/treat as
  // unimplemented for the combined constructs.
  if (Clause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return isNotImplemented();

  // OpenACC 3.3 2.9:
  // Only one of the seq, independent, and auto clauses may appear.
  const auto *Itr =
      llvm::find_if(ExistingClauses,
                    llvm::IsaPred<OpenACCIndependentClause, OpenACCSeqClause>);
  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_loop_spec_conflict)
        << Clause.getClauseKind() << Clause.getDirectiveKind();
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    return nullptr;
  }

  return OpenACCAutoClause::Create(Ctx, Clause.getBeginLoc(),
                                   Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitIndependentClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'loop' constructs, and it is
  // the only construct that can do anything with this, so skip/treat as
  // unimplemented for the combined constructs.
  if (Clause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return isNotImplemented();

  // OpenACC 3.3 2.9:
  // Only one of the seq, independent, and auto clauses may appear.
  const auto *Itr = llvm::find_if(
      ExistingClauses, llvm::IsaPred<OpenACCAutoClause, OpenACCSeqClause>);
  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_loop_spec_conflict)
        << Clause.getClauseKind() << Clause.getDirectiveKind();
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    return nullptr;
  }

  return OpenACCIndependentClause::Create(Ctx, Clause.getBeginLoc(),
                                          Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitSeqClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'loop' constructs, and it is
  // the only construct that can do anything with this, so skip/treat as
  // unimplemented for the combined constructs.
  if (Clause.getDirectiveKind() != OpenACCDirectiveKind::Loop)
    return isNotImplemented();

  // OpenACC 3.3 2.9:
  // Only one of the seq, independent, and auto clauses may appear.
  const auto *Itr =
      llvm::find_if(ExistingClauses,
                    llvm::IsaPred<OpenACCAutoClause, OpenACCIndependentClause>);
  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_loop_spec_conflict)
        << Clause.getClauseKind() << Clause.getDirectiveKind();
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    return nullptr;
  }

  // OpenACC 3.3 2.9:
  // A 'gang', 'worker', or 'vector' clause may not appear if a 'seq' clause
  // appears.
  Itr = llvm::find_if(ExistingClauses,
                      llvm::IsaPred<OpenACCGangClause, OpenACCWorkerClause,
                                    OpenACCVectorClause>);

  if (Itr != ExistingClauses.end()) {
    SemaRef.Diag(Clause.getBeginLoc(), diag::err_acc_clause_cannot_combine)
        << Clause.getClauseKind() << (*Itr)->getClauseKind();
    SemaRef.Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
    return nullptr;
  }

  // TODO OpenACC: 2.9 ~ line 2010 specifies that the associated loop has some
  // restrictions when there is a 'seq' clause in place. We probably need to
  // implement that.
  return OpenACCSeqClause::Create(Ctx, Clause.getBeginLoc(),
                                  Clause.getEndLoc());
}

OpenACCClause *SemaOpenACCClauseVisitor::VisitReductionClause(
    SemaOpenACC::OpenACCParsedClause &Clause) {
  // Restrictions only properly implemented on 'compute' constructs, and
  // 'compute' constructs are the only construct that can do anything with
  // this yet, so skip/treat as unimplemented in this case.
  if (!isOpenACCComputeDirectiveKind(Clause.getDirectiveKind()))
    return isNotImplemented();

  // OpenACC 3.3 Section 2.5.4:
  // A reduction clause may not appear on a parallel construct with a
  // num_gangs clause that has more than one argument.
  if (Clause.getDirectiveKind() == OpenACCDirectiveKind::Parallel) {
    auto NumGangsClauses = llvm::make_filter_range(
        ExistingClauses, llvm::IsaPred<OpenACCNumGangsClause>);

    for (auto *NGC : NumGangsClauses) {
      unsigned NumExprs =
          cast<OpenACCNumGangsClause>(NGC)->getIntExprs().size();

      if (NumExprs > 1) {
        SemaRef.Diag(Clause.getBeginLoc(),
                     diag::err_acc_reduction_num_gangs_conflict)
            << NumExprs;
        SemaRef.Diag(NGC->getBeginLoc(), diag::note_acc_previous_clause_here);
        return nullptr;
      }
    }
  }

  SmallVector<Expr *> ValidVars;

  for (Expr *Var : Clause.getVarList()) {
    ExprResult Res = SemaRef.CheckReductionVar(Var);

    if (Res.isUsable())
      ValidVars.push_back(Res.get());
  }

  return OpenACCReductionClause::Create(
      Ctx, Clause.getBeginLoc(), Clause.getLParenLoc(), Clause.getReductionOp(),
      ValidVars, Clause.getEndLoc());
}

} // namespace

SemaOpenACC::SemaOpenACC(Sema &S) : SemaBase(S) {}

SemaOpenACC::AssociatedStmtRAII::AssociatedStmtRAII(SemaOpenACC &S,
                                                    OpenACCDirectiveKind DK)
    : SemaRef(S), WasInsideComputeConstruct(S.InsideComputeConstruct),
      DirKind(DK) {
  // Compute constructs end up taking their 'loop'.
  if (DirKind == OpenACCDirectiveKind::Parallel ||
      DirKind == OpenACCDirectiveKind::Serial ||
      DirKind == OpenACCDirectiveKind::Kernels) {
    SemaRef.InsideComputeConstruct = true;
    SemaRef.ParentlessLoopConstructs.swap(ParentlessLoopConstructs);
  }
}

SemaOpenACC::AssociatedStmtRAII::~AssociatedStmtRAII() {
  SemaRef.InsideComputeConstruct = WasInsideComputeConstruct;
  if (DirKind == OpenACCDirectiveKind::Parallel ||
      DirKind == OpenACCDirectiveKind::Serial ||
      DirKind == OpenACCDirectiveKind::Kernels) {
    assert(SemaRef.ParentlessLoopConstructs.empty() &&
           "Didn't consume loop construct list?");
    SemaRef.ParentlessLoopConstructs.swap(ParentlessLoopConstructs);
  }
}

OpenACCClause *
SemaOpenACC::ActOnClause(ArrayRef<const OpenACCClause *> ExistingClauses,
                         OpenACCParsedClause &Clause) {
  if (Clause.getClauseKind() == OpenACCClauseKind::Invalid)
    return nullptr;

  // Diagnose that we don't support this clause on this directive.
  if (!doesClauseApplyToDirective(Clause.getDirectiveKind(),
                                  Clause.getClauseKind())) {
    Diag(Clause.getBeginLoc(), diag::err_acc_clause_appertainment)
        << Clause.getDirectiveKind() << Clause.getClauseKind();
    return nullptr;
  }

  if (const auto *DevTypeClause =
          llvm::find_if(ExistingClauses,
                        [&](const OpenACCClause *C) {
                          return isa<OpenACCDeviceTypeClause>(C);
                        });
      DevTypeClause != ExistingClauses.end()) {
    if (checkValidAfterDeviceType(
            *this, *cast<OpenACCDeviceTypeClause>(*DevTypeClause), Clause))
      return nullptr;
  }

  SemaOpenACCClauseVisitor Visitor{*this, ExistingClauses};
  OpenACCClause *Result = Visitor.Visit(Clause);
  assert((!Result || Result->getClauseKind() == Clause.getClauseKind()) &&
         "Created wrong clause?");

  if (Visitor.diagNotImplemented())
    Diag(Clause.getBeginLoc(), diag::warn_acc_clause_unimplemented)
        << Clause.getClauseKind();

  return Result;

  //  switch (Clause.getClauseKind()) {
  //  case OpenACCClauseKind::PresentOrCopy:
  //  case OpenACCClauseKind::PCopy:
  //    Diag(Clause.getBeginLoc(), diag::warn_acc_deprecated_alias_name)
  //        << Clause.getClauseKind() << OpenACCClauseKind::Copy;
  //    LLVM_FALLTHROUGH;
  //  case OpenACCClauseKind::PresentOrCreate:
  //  case OpenACCClauseKind::PCreate:
  //    Diag(Clause.getBeginLoc(), diag::warn_acc_deprecated_alias_name)
  //        << Clause.getClauseKind() << OpenACCClauseKind::Create;
  //    LLVM_FALLTHROUGH;
  //
  //
  //
  //
  //  case OpenACCClauseKind::DType:
  //
  //
  //
  //
  //
  //
  //
  //
  //  case OpenACCClauseKind::Gang:
  //  case OpenACCClauseKind::Worker:
  //  case OpenACCClauseKind::Vector: {
  //    // OpenACC 3.3 2.9:
  //    // A 'gang', 'worker', or 'vector' clause may not appear if a 'seq'
  //    clause
  //    // appears.
  //    const auto *Itr =
  //        llvm::find_if(ExistingClauses, llvm::IsaPred<OpenACCSeqClause>);
  //
  //    if (Itr != ExistingClauses.end()) {
  //      Diag(Clause.getBeginLoc(), diag::err_acc_clause_cannot_combine)
  //          << Clause.getClauseKind() << (*Itr)->getClauseKind();
  //      Diag((*Itr)->getBeginLoc(), diag::note_acc_previous_clause_here);
  //    }
  //    // Not yet implemented, so immediately drop to the 'not yet implemented'
  //    // diagnostic.
  //    break;
  //  }
  //  */

}

/// OpenACC 3.3 section 2.5.15:
/// At a mininmum, the supported data types include ... the numerical data types
/// in C, C++, and Fortran.
///
/// If the reduction var is a composite variable, each
/// member of the composite variable must be a supported datatype for the
/// reduction operation.
ExprResult SemaOpenACC::CheckReductionVar(Expr *VarExpr) {
  VarExpr = VarExpr->IgnoreParenCasts();

  auto TypeIsValid = [](QualType Ty) {
    return Ty->isDependentType() || Ty->isScalarType();
  };

  if (isa<ArraySectionExpr>(VarExpr)) {
    Expr *ASExpr = VarExpr;
    QualType BaseTy = ArraySectionExpr::getBaseOriginalType(ASExpr);
    QualType EltTy = getASTContext().getBaseElementType(BaseTy);

    if (!TypeIsValid(EltTy)) {
      Diag(VarExpr->getExprLoc(), diag::err_acc_reduction_type)
          << EltTy << /*Sub array base type*/ 1;
      return ExprError();
    }
  } else if (auto *RD = VarExpr->getType()->getAsRecordDecl()) {
    if (!RD->isStruct() && !RD->isClass()) {
      Diag(VarExpr->getExprLoc(), diag::err_acc_reduction_composite_type)
          << /*not class or struct*/ 0 << VarExpr->getType();
      return ExprError();
    }

    if (!RD->isCompleteDefinition()) {
      Diag(VarExpr->getExprLoc(), diag::err_acc_reduction_composite_type)
          << /*incomplete*/ 1 << VarExpr->getType();
      return ExprError();
    }
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
        CXXRD && !CXXRD->isAggregate()) {
      Diag(VarExpr->getExprLoc(), diag::err_acc_reduction_composite_type)
          << /*aggregate*/ 2 << VarExpr->getType();
      return ExprError();
    }

    for (FieldDecl *FD : RD->fields()) {
      if (!TypeIsValid(FD->getType())) {
        Diag(VarExpr->getExprLoc(),
             diag::err_acc_reduction_composite_member_type);
        Diag(FD->getLocation(), diag::note_acc_reduction_composite_member_loc);
        return ExprError();
      }
    }
  } else if (!TypeIsValid(VarExpr->getType())) {
    Diag(VarExpr->getExprLoc(), diag::err_acc_reduction_type)
        << VarExpr->getType() << /*Sub array base type*/ 0;
    return ExprError();
  }

  return VarExpr;
}

void SemaOpenACC::ActOnConstruct(OpenACCDirectiveKind K,
                                 SourceLocation DirLoc) {
  switch (K) {
  case OpenACCDirectiveKind::Invalid:
    // Nothing to do here, an invalid kind has nothing we can check here.  We
    // want to continue parsing clauses as far as we can, so we will just
    // ensure that we can still work and don't check any construct-specific
    // rules anywhere.
    break;
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels:
  case OpenACCDirectiveKind::Loop:
    // Nothing to do here, there is no real legalization that needs to happen
    // here as these constructs do not take any arguments.
    break;
  default:
    Diag(DirLoc, diag::warn_acc_construct_unimplemented) << K;
    break;
  }
}

ExprResult SemaOpenACC::ActOnIntExpr(OpenACCDirectiveKind DK,
                                     OpenACCClauseKind CK, SourceLocation Loc,
                                     Expr *IntExpr) {

  assert(((DK != OpenACCDirectiveKind::Invalid &&
           CK == OpenACCClauseKind::Invalid) ||
          (DK == OpenACCDirectiveKind::Invalid &&
           CK != OpenACCClauseKind::Invalid) ||
          (DK == OpenACCDirectiveKind::Invalid &&
           CK == OpenACCClauseKind::Invalid)) &&
         "Only one of directive or clause kind should be provided");

  class IntExprConverter : public Sema::ICEConvertDiagnoser {
    OpenACCDirectiveKind DirectiveKind;
    OpenACCClauseKind ClauseKind;
    Expr *IntExpr;

    // gets the index into the diagnostics so we can use this for clauses,
    // directives, and sub array.s
    unsigned getDiagKind() const {
      if (ClauseKind != OpenACCClauseKind::Invalid)
        return 0;
      if (DirectiveKind != OpenACCDirectiveKind::Invalid)
        return 1;
      return 2;
    }

  public:
    IntExprConverter(OpenACCDirectiveKind DK, OpenACCClauseKind CK,
                     Expr *IntExpr)
        : ICEConvertDiagnoser(/*AllowScopedEnumerations=*/false,
                              /*Suppress=*/false,
                              /*SuppressConversion=*/true),
          DirectiveKind(DK), ClauseKind(CK), IntExpr(IntExpr) {}

    bool match(QualType T) override {
      // OpenACC spec just calls this 'integer expression' as having an
      // 'integer type', so fall back on C99's 'integer type'.
      return T->isIntegerType();
    }
    SemaBase::SemaDiagnosticBuilder diagnoseNotInt(Sema &S, SourceLocation Loc,
                                                   QualType T) override {
      return S.Diag(Loc, diag::err_acc_int_expr_requires_integer)
             << getDiagKind() << ClauseKind << DirectiveKind << T;
    }

    SemaBase::SemaDiagnosticBuilder
    diagnoseIncomplete(Sema &S, SourceLocation Loc, QualType T) override {
      return S.Diag(Loc, diag::err_acc_int_expr_incomplete_class_type)
             << T << IntExpr->getSourceRange();
    }

    SemaBase::SemaDiagnosticBuilder
    diagnoseExplicitConv(Sema &S, SourceLocation Loc, QualType T,
                         QualType ConvTy) override {
      return S.Diag(Loc, diag::err_acc_int_expr_explicit_conversion)
             << T << ConvTy;
    }

    SemaBase::SemaDiagnosticBuilder noteExplicitConv(Sema &S,
                                                     CXXConversionDecl *Conv,
                                                     QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_acc_int_expr_conversion)
             << ConvTy->isEnumeralType() << ConvTy;
    }

    SemaBase::SemaDiagnosticBuilder
    diagnoseAmbiguous(Sema &S, SourceLocation Loc, QualType T) override {
      return S.Diag(Loc, diag::err_acc_int_expr_multiple_conversions) << T;
    }

    SemaBase::SemaDiagnosticBuilder
    noteAmbiguous(Sema &S, CXXConversionDecl *Conv, QualType ConvTy) override {
      return S.Diag(Conv->getLocation(), diag::note_acc_int_expr_conversion)
             << ConvTy->isEnumeralType() << ConvTy;
    }

    SemaBase::SemaDiagnosticBuilder
    diagnoseConversion(Sema &S, SourceLocation Loc, QualType T,
                       QualType ConvTy) override {
      llvm_unreachable("conversion functions are permitted");
    }
  } IntExprDiagnoser(DK, CK, IntExpr);

  ExprResult IntExprResult = SemaRef.PerformContextualImplicitConversion(
      Loc, IntExpr, IntExprDiagnoser);
  if (IntExprResult.isInvalid())
    return ExprError();

  IntExpr = IntExprResult.get();
  if (!IntExpr->isTypeDependent() && !IntExpr->getType()->isIntegerType())
    return ExprError();

  // TODO OpenACC: Do we want to perform usual unary conversions here? When
  // doing codegen we might find that is necessary, but skip it for now.
  return IntExpr;
}

bool SemaOpenACC::CheckVarIsPointerType(OpenACCClauseKind ClauseKind,
                                        Expr *VarExpr) {
  // We already know that VarExpr is a proper reference to a variable, so we
  // should be able to just take the type of the expression to get the type of
  // the referenced variable.

  // We've already seen an error, don't diagnose anything else.
  if (!VarExpr || VarExpr->containsErrors())
    return false;

  if (isa<ArraySectionExpr>(VarExpr->IgnoreParenImpCasts()) ||
      VarExpr->hasPlaceholderType(BuiltinType::ArraySection)) {
    Diag(VarExpr->getExprLoc(), diag::err_array_section_use) << /*OpenACC=*/0;
    Diag(VarExpr->getExprLoc(), diag::note_acc_expected_pointer_var);
    return true;
  }

  QualType Ty = VarExpr->getType();
  Ty = Ty.getNonReferenceType().getUnqualifiedType();

  // Nothing we can do if this is a dependent type.
  if (Ty->isDependentType())
    return false;

  if (!Ty->isPointerType())
    return Diag(VarExpr->getExprLoc(), diag::err_acc_var_not_pointer_type)
           << ClauseKind << Ty;
  return false;
}

ExprResult SemaOpenACC::ActOnVar(OpenACCClauseKind CK, Expr *VarExpr) {
  Expr *CurVarExpr = VarExpr->IgnoreParenImpCasts();

  // Sub-arrays/subscript-exprs are fine as long as the base is a
  // VarExpr/MemberExpr. So strip all of those off.
  while (isa<ArraySectionExpr, ArraySubscriptExpr>(CurVarExpr)) {
    if (auto *SubScrpt = dyn_cast<ArraySubscriptExpr>(CurVarExpr))
      CurVarExpr = SubScrpt->getBase()->IgnoreParenImpCasts();
    else
      CurVarExpr =
          cast<ArraySectionExpr>(CurVarExpr)->getBase()->IgnoreParenImpCasts();
  }

  // References to a VarDecl are fine.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(CurVarExpr)) {
    if (isa<VarDecl, NonTypeTemplateParmDecl>(
            DRE->getFoundDecl()->getCanonicalDecl()))
      return VarExpr;
  }

  // If CK is a Reduction, this special cases for OpenACC3.3 2.5.15: "A var in a
  // reduction clause must be a scalar variable name, an aggregate variable
  // name, an array element, or a subarray.
  // A MemberExpr that references a Field is valid.
  if (CK != OpenACCClauseKind::Reduction) {
    if (const auto *ME = dyn_cast<MemberExpr>(CurVarExpr)) {
      if (isa<FieldDecl>(ME->getMemberDecl()->getCanonicalDecl()))
        return VarExpr;
    }
  }

  // Referring to 'this' is always OK.
  if (isa<CXXThisExpr>(CurVarExpr))
    return VarExpr;

  // Nothing really we can do here, as these are dependent.  So just return they
  // are valid.
  if (isa<DependentScopeDeclRefExpr>(CurVarExpr) ||
      (CK != OpenACCClauseKind::Reduction &&
       isa<CXXDependentScopeMemberExpr>(CurVarExpr)))
    return VarExpr;

  // There isn't really anything we can do in the case of a recovery expr, so
  // skip the diagnostic rather than produce a confusing diagnostic.
  if (isa<RecoveryExpr>(CurVarExpr))
    return ExprError();

  Diag(VarExpr->getExprLoc(), diag::err_acc_not_a_var_ref)
      << (CK != OpenACCClauseKind::Reduction);
  return ExprError();
}

ExprResult SemaOpenACC::ActOnArraySectionExpr(Expr *Base, SourceLocation LBLoc,
                                              Expr *LowerBound,
                                              SourceLocation ColonLoc,
                                              Expr *Length,
                                              SourceLocation RBLoc) {
  ASTContext &Context = getASTContext();

  // Handle placeholders.
  if (Base->hasPlaceholderType() &&
      !Base->hasPlaceholderType(BuiltinType::ArraySection)) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Base);
    if (Result.isInvalid())
      return ExprError();
    Base = Result.get();
  }
  if (LowerBound && LowerBound->getType()->isNonOverloadPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(LowerBound);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    LowerBound = Result.get();
  }
  if (Length && Length->getType()->isNonOverloadPlaceholderType()) {
    ExprResult Result = SemaRef.CheckPlaceholderExpr(Length);
    if (Result.isInvalid())
      return ExprError();
    Result = SemaRef.DefaultLvalueConversion(Result.get());
    if (Result.isInvalid())
      return ExprError();
    Length = Result.get();
  }

  // Check the 'base' value, it must be an array or pointer type, and not to/of
  // a function type.
  QualType OriginalBaseTy = ArraySectionExpr::getBaseOriginalType(Base);
  QualType ResultTy;
  if (!Base->isTypeDependent()) {
    if (OriginalBaseTy->isAnyPointerType()) {
      ResultTy = OriginalBaseTy->getPointeeType();
    } else if (OriginalBaseTy->isArrayType()) {
      ResultTy = OriginalBaseTy->getAsArrayTypeUnsafe()->getElementType();
    } else {
      return ExprError(
          Diag(Base->getExprLoc(), diag::err_acc_typecheck_subarray_value)
          << Base->getSourceRange());
    }

    if (ResultTy->isFunctionType()) {
      Diag(Base->getExprLoc(), diag::err_acc_subarray_function_type)
          << ResultTy << Base->getSourceRange();
      return ExprError();
    }

    if (SemaRef.RequireCompleteType(Base->getExprLoc(), ResultTy,
                                    diag::err_acc_subarray_incomplete_type,
                                    Base))
      return ExprError();

    if (!Base->hasPlaceholderType(BuiltinType::ArraySection)) {
      ExprResult Result = SemaRef.DefaultFunctionArrayLvalueConversion(Base);
      if (Result.isInvalid())
        return ExprError();
      Base = Result.get();
    }
  }

  auto GetRecovery = [&](Expr *E, QualType Ty) {
    ExprResult Recovery =
        SemaRef.CreateRecoveryExpr(E->getBeginLoc(), E->getEndLoc(), E, Ty);
    return Recovery.isUsable() ? Recovery.get() : nullptr;
  };

  // Ensure both of the expressions are int-exprs.
  if (LowerBound && !LowerBound->isTypeDependent()) {
    ExprResult LBRes =
        ActOnIntExpr(OpenACCDirectiveKind::Invalid, OpenACCClauseKind::Invalid,
                     LowerBound->getExprLoc(), LowerBound);

    if (LBRes.isUsable())
      LBRes = SemaRef.DefaultLvalueConversion(LBRes.get());
    LowerBound =
        LBRes.isUsable() ? LBRes.get() : GetRecovery(LowerBound, Context.IntTy);
  }

  if (Length && !Length->isTypeDependent()) {
    ExprResult LenRes =
        ActOnIntExpr(OpenACCDirectiveKind::Invalid, OpenACCClauseKind::Invalid,
                     Length->getExprLoc(), Length);

    if (LenRes.isUsable())
      LenRes = SemaRef.DefaultLvalueConversion(LenRes.get());
    Length =
        LenRes.isUsable() ? LenRes.get() : GetRecovery(Length, Context.IntTy);
  }

  // Length is required if the base type is not an array of known bounds.
  if (!Length && (OriginalBaseTy.isNull() ||
                  (!OriginalBaseTy->isDependentType() &&
                   !OriginalBaseTy->isConstantArrayType() &&
                   !OriginalBaseTy->isDependentSizedArrayType()))) {
    bool IsArray = !OriginalBaseTy.isNull() && OriginalBaseTy->isArrayType();
    Diag(ColonLoc, diag::err_acc_subarray_no_length) << IsArray;
    // Fill in a dummy 'length' so that when we instantiate this we don't
    // double-diagnose here.
    ExprResult Recovery = SemaRef.CreateRecoveryExpr(
        ColonLoc, SourceLocation(), ArrayRef<Expr *>{std::nullopt},
        Context.IntTy);
    Length = Recovery.isUsable() ? Recovery.get() : nullptr;
  }

  // Check the values of each of the arguments, they cannot be negative(we
  // assume), and if the array bound is known, must be within range. As we do
  // so, do our best to continue with evaluation, we can set the
  // value/expression to nullptr/nullopt if they are invalid, and treat them as
  // not present for the rest of evaluation.

  // We don't have to check for dependence, because the dependent size is
  // represented as a different AST node.
  std::optional<llvm::APSInt> BaseSize;
  if (!OriginalBaseTy.isNull() && OriginalBaseTy->isConstantArrayType()) {
    const auto *ArrayTy = Context.getAsConstantArrayType(OriginalBaseTy);
    BaseSize = ArrayTy->getSize();
  }

  auto GetBoundValue = [&](Expr *E) -> std::optional<llvm::APSInt> {
    if (!E || E->isInstantiationDependent())
      return std::nullopt;

    Expr::EvalResult Res;
    if (!E->EvaluateAsInt(Res, Context))
      return std::nullopt;
    return Res.Val.getInt();
  };

  std::optional<llvm::APSInt> LowerBoundValue = GetBoundValue(LowerBound);
  std::optional<llvm::APSInt> LengthValue = GetBoundValue(Length);

  // Check lower bound for negative or out of range.
  if (LowerBoundValue.has_value()) {
    if (LowerBoundValue->isNegative()) {
      Diag(LowerBound->getExprLoc(), diag::err_acc_subarray_negative)
          << /*LowerBound=*/0 << toString(*LowerBoundValue, /*Radix=*/10);
      LowerBoundValue.reset();
      LowerBound = GetRecovery(LowerBound, LowerBound->getType());
    } else if (BaseSize.has_value() &&
               llvm::APSInt::compareValues(*LowerBoundValue, *BaseSize) >= 0) {
      // Lower bound (start index) must be less than the size of the array.
      Diag(LowerBound->getExprLoc(), diag::err_acc_subarray_out_of_range)
          << /*LowerBound=*/0 << toString(*LowerBoundValue, /*Radix=*/10)
          << toString(*BaseSize, /*Radix=*/10);
      LowerBoundValue.reset();
      LowerBound = GetRecovery(LowerBound, LowerBound->getType());
    }
  }

  // Check length for negative or out of range.
  if (LengthValue.has_value()) {
    if (LengthValue->isNegative()) {
      Diag(Length->getExprLoc(), diag::err_acc_subarray_negative)
          << /*Length=*/1 << toString(*LengthValue, /*Radix=*/10);
      LengthValue.reset();
      Length = GetRecovery(Length, Length->getType());
    } else if (BaseSize.has_value() &&
               llvm::APSInt::compareValues(*LengthValue, *BaseSize) > 0) {
      // Length must be lessthan or EQUAL to the size of the array.
      Diag(Length->getExprLoc(), diag::err_acc_subarray_out_of_range)
          << /*Length=*/1 << toString(*LengthValue, /*Radix=*/10)
          << toString(*BaseSize, /*Radix=*/10);
      LengthValue.reset();
      Length = GetRecovery(Length, Length->getType());
    }
  }

  // Adding two APSInts requires matching sign, so extract that here.
  auto AddAPSInt = [](llvm::APSInt LHS, llvm::APSInt RHS) -> llvm::APSInt {
    if (LHS.isSigned() == RHS.isSigned())
      return LHS + RHS;

    unsigned Width = std::max(LHS.getBitWidth(), RHS.getBitWidth()) + 1;
    return llvm::APSInt(LHS.sext(Width) + RHS.sext(Width), /*Signed=*/true);
  };

  // If we know all 3 values, we can diagnose that the total value would be out
  // of range.
  if (BaseSize.has_value() && LowerBoundValue.has_value() &&
      LengthValue.has_value() &&
      llvm::APSInt::compareValues(AddAPSInt(*LowerBoundValue, *LengthValue),
                                  *BaseSize) > 0) {
    Diag(Base->getExprLoc(),
         diag::err_acc_subarray_base_plus_length_out_of_range)
        << toString(*LowerBoundValue, /*Radix=*/10)
        << toString(*LengthValue, /*Radix=*/10)
        << toString(*BaseSize, /*Radix=*/10);

    LowerBoundValue.reset();
    LowerBound = GetRecovery(LowerBound, LowerBound->getType());
    LengthValue.reset();
    Length = GetRecovery(Length, Length->getType());
  }

  // If any part of the expression is dependent, return a dependent sub-array.
  QualType ArrayExprTy = Context.ArraySectionTy;
  if (Base->isTypeDependent() ||
      (LowerBound && LowerBound->isInstantiationDependent()) ||
      (Length && Length->isInstantiationDependent()))
    ArrayExprTy = Context.DependentTy;

  return new (Context)
      ArraySectionExpr(Base, LowerBound, Length, ArrayExprTy, VK_LValue,
                       OK_Ordinary, ColonLoc, RBLoc);
}

bool SemaOpenACC::ActOnStartStmtDirective(OpenACCDirectiveKind K,
                                          SourceLocation StartLoc) {
  return diagnoseConstructAppertainment(*this, K, StartLoc, /*IsStmt=*/true);
}

StmtResult SemaOpenACC::ActOnEndStmtDirective(OpenACCDirectiveKind K,
                                              SourceLocation StartLoc,
                                              SourceLocation DirLoc,
                                              SourceLocation EndLoc,
                                              ArrayRef<OpenACCClause *> Clauses,
                                              StmtResult AssocStmt) {
  switch (K) {
  default:
    return StmtEmpty();
  case OpenACCDirectiveKind::Invalid:
    return StmtError();
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels: {
    auto *ComputeConstruct = OpenACCComputeConstruct::Create(
        getASTContext(), K, StartLoc, DirLoc, EndLoc, Clauses,
        AssocStmt.isUsable() ? AssocStmt.get() : nullptr,
        ParentlessLoopConstructs);

    ParentlessLoopConstructs.clear();
    return ComputeConstruct;
  }
  case OpenACCDirectiveKind::Loop: {
    auto *LoopConstruct = OpenACCLoopConstruct::Create(
        getASTContext(), StartLoc, DirLoc, EndLoc, Clauses,
        AssocStmt.isUsable() ? AssocStmt.get() : nullptr);

    // If we are in the scope of a compute construct, add this to the list of
    // loop constructs that need assigning to the next closing compute
    // construct.
    if (InsideComputeConstruct)
      ParentlessLoopConstructs.push_back(LoopConstruct);

    return LoopConstruct;
  }
  }
  llvm_unreachable("Unhandled case in directive handling?");
}

StmtResult SemaOpenACC::ActOnAssociatedStmt(SourceLocation DirectiveLoc,
                                            OpenACCDirectiveKind K,
                                            StmtResult AssocStmt) {
  switch (K) {
  default:
    llvm_unreachable("Unimplemented associated statement application");
  case OpenACCDirectiveKind::Parallel:
  case OpenACCDirectiveKind::Serial:
  case OpenACCDirectiveKind::Kernels:
    // There really isn't any checking here that could happen. As long as we
    // have a statement to associate, this should be fine.
    // OpenACC 3.3 Section 6:
    // Structured Block: in C or C++, an executable statement, possibly
    // compound, with a single entry at the top and a single exit at the
    // bottom.
    // FIXME: Should we reject DeclStmt's here? The standard isn't clear, and
    // an interpretation of it is to allow this and treat the initializer as
    // the 'structured block'.
    return AssocStmt;
  case OpenACCDirectiveKind::Loop:
    if (AssocStmt.isUsable() &&
        !isa<CXXForRangeStmt, ForStmt>(AssocStmt.get())) {
      Diag(AssocStmt.get()->getBeginLoc(), diag::err_acc_loop_not_for_loop);
      Diag(DirectiveLoc, diag::note_acc_construct_here) << K;
      return StmtError();
    }
    // TODO OpenACC: 2.9 ~ line 2010 specifies that the associated loop has some
    // restrictions when there is a 'seq' clause in place. We probably need to
    // implement that, including piping in the clauses here.
    return AssocStmt;
  }
  llvm_unreachable("Invalid associated statement application");
}

bool SemaOpenACC::ActOnStartDeclDirective(OpenACCDirectiveKind K,
                                          SourceLocation StartLoc) {
  return diagnoseConstructAppertainment(*this, K, StartLoc, /*IsStmt=*/false);
}

DeclGroupRef SemaOpenACC::ActOnEndDeclDirective() { return DeclGroupRef{}; }
