//===---- OpenACCClause.cpp - Classes for OpenACC Clauses  ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the subclasses of the OpenACCClause class declared in
// OpenACCClause.h
//
//===----------------------------------------------------------------------===//

#include "clang/AST/OpenACCClause.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"

using namespace clang;

bool OpenACCClauseWithParams::classof(const OpenACCClause *C) {
  return OpenACCDeviceTypeClause::classof(C) ||
         OpenACCClauseWithCondition::classof(C) ||
         OpenACCClauseWithExprs::classof(C);
}
bool OpenACCClauseWithExprs::classof(const OpenACCClause *C) {
  return OpenACCWaitClause::classof(C) || OpenACCNumGangsClause::classof(C) ||
         OpenACCClauseWithSingleIntExpr::classof(C) ||
         OpenACCClauseWithVarList::classof(C);
}
bool OpenACCClauseWithVarList::classof(const OpenACCClause *C) {
  return OpenACCPrivateClause::classof(C) ||
         OpenACCFirstPrivateClause::classof(C) ||
         OpenACCDevicePtrClause::classof(C) ||
         OpenACCDevicePtrClause::classof(C) ||
         OpenACCAttachClause::classof(C) || OpenACCNoCreateClause::classof(C) ||
         OpenACCPresentClause::classof(C) || OpenACCCopyClause::classof(C) ||
         OpenACCCopyInClause::classof(C) || OpenACCCopyOutClause::classof(C) ||
         OpenACCReductionClause::classof(C) || OpenACCCreateClause::classof(C);
}
bool OpenACCClauseWithCondition::classof(const OpenACCClause *C) {
  return OpenACCIfClause::classof(C) || OpenACCSelfClause::classof(C);
}
bool OpenACCClauseWithSingleIntExpr::classof(const OpenACCClause *C) {
  return OpenACCNumWorkersClause::classof(C) ||
         OpenACCVectorLengthClause::classof(C) ||
         OpenACCAsyncClause::classof(C);
}
OpenACCDefaultClause *OpenACCDefaultClause::Create(const ASTContext &C,
                                                   OpenACCDefaultClauseKind K,
                                                   SourceLocation BeginLoc,
                                                   SourceLocation LParenLoc,
                                                   SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(sizeof(OpenACCDefaultClause), alignof(OpenACCDefaultClause));

  return new (Mem) OpenACCDefaultClause(K, BeginLoc, LParenLoc, EndLoc);
}

OpenACCIfClause *OpenACCIfClause::Create(const ASTContext &C,
                                         SourceLocation BeginLoc,
                                         SourceLocation LParenLoc,
                                         Expr *ConditionExpr,
                                         SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCIfClause), alignof(OpenACCIfClause));
  return new (Mem) OpenACCIfClause(BeginLoc, LParenLoc, ConditionExpr, EndLoc);
}

OpenACCIfClause::OpenACCIfClause(SourceLocation BeginLoc,
                                 SourceLocation LParenLoc, Expr *ConditionExpr,
                                 SourceLocation EndLoc)
    : OpenACCClauseWithCondition(OpenACCClauseKind::If, BeginLoc, LParenLoc,
                                 ConditionExpr, EndLoc) {
  assert(ConditionExpr && "if clause requires condition expr");
  assert((ConditionExpr->isInstantiationDependent() ||
          ConditionExpr->getType()->isScalarType()) &&
         "Condition expression type not scalar/dependent");
}

OpenACCSelfClause *OpenACCSelfClause::Create(const ASTContext &C,
                                             SourceLocation BeginLoc,
                                             SourceLocation LParenLoc,
                                             Expr *ConditionExpr,
                                             SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCIfClause), alignof(OpenACCIfClause));
  return new (Mem)
      OpenACCSelfClause(BeginLoc, LParenLoc, ConditionExpr, EndLoc);
}

OpenACCSelfClause::OpenACCSelfClause(SourceLocation BeginLoc,
                                     SourceLocation LParenLoc,
                                     Expr *ConditionExpr, SourceLocation EndLoc)
    : OpenACCClauseWithCondition(OpenACCClauseKind::Self, BeginLoc, LParenLoc,
                                 ConditionExpr, EndLoc) {
  assert((!ConditionExpr || ConditionExpr->isInstantiationDependent() ||
          ConditionExpr->getType()->isScalarType()) &&
         "Condition expression type not scalar/dependent");
}

OpenACCClause::child_range OpenACCClause::children() {
  switch (getClauseKind()) {
  default:
    assert(false && "Clause children function not implemented");
    break;
#define VISIT_CLAUSE(CLAUSE_NAME)                                              \
  case OpenACCClauseKind::CLAUSE_NAME:                                         \
    return cast<OpenACC##CLAUSE_NAME##Clause>(this)->children();
#define CLAUSE_ALIAS(ALIAS_NAME, CLAUSE_NAME, DEPRECATED)                      \
  case OpenACCClauseKind::ALIAS_NAME:                                          \
    return cast<OpenACC##CLAUSE_NAME##Clause>(this)->children();

#include "clang/Basic/OpenACCClauses.def"
  }
  return child_range(child_iterator(), child_iterator());
}

OpenACCNumWorkersClause::OpenACCNumWorkersClause(SourceLocation BeginLoc,
                                                 SourceLocation LParenLoc,
                                                 Expr *IntExpr,
                                                 SourceLocation EndLoc)
    : OpenACCClauseWithSingleIntExpr(OpenACCClauseKind::NumWorkers, BeginLoc,
                                     LParenLoc, IntExpr, EndLoc) {
  assert((!IntExpr || IntExpr->isInstantiationDependent() ||
          IntExpr->getType()->isIntegerType()) &&
         "Condition expression type not scalar/dependent");
}

OpenACCNumWorkersClause *
OpenACCNumWorkersClause::Create(const ASTContext &C, SourceLocation BeginLoc,
                                SourceLocation LParenLoc, Expr *IntExpr,
                                SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCNumWorkersClause),
                         alignof(OpenACCNumWorkersClause));
  return new (Mem)
      OpenACCNumWorkersClause(BeginLoc, LParenLoc, IntExpr, EndLoc);
}

OpenACCVectorLengthClause::OpenACCVectorLengthClause(SourceLocation BeginLoc,
                                                     SourceLocation LParenLoc,
                                                     Expr *IntExpr,
                                                     SourceLocation EndLoc)
    : OpenACCClauseWithSingleIntExpr(OpenACCClauseKind::VectorLength, BeginLoc,
                                     LParenLoc, IntExpr, EndLoc) {
  assert((!IntExpr || IntExpr->isInstantiationDependent() ||
          IntExpr->getType()->isIntegerType()) &&
         "Condition expression type not scalar/dependent");
}

OpenACCVectorLengthClause *
OpenACCVectorLengthClause::Create(const ASTContext &C, SourceLocation BeginLoc,
                                  SourceLocation LParenLoc, Expr *IntExpr,
                                  SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCVectorLengthClause),
                         alignof(OpenACCVectorLengthClause));
  return new (Mem)
      OpenACCVectorLengthClause(BeginLoc, LParenLoc, IntExpr, EndLoc);
}

OpenACCAsyncClause::OpenACCAsyncClause(SourceLocation BeginLoc,
                                       SourceLocation LParenLoc, Expr *IntExpr,
                                       SourceLocation EndLoc)
    : OpenACCClauseWithSingleIntExpr(OpenACCClauseKind::Async, BeginLoc,
                                     LParenLoc, IntExpr, EndLoc) {
  assert((!IntExpr || IntExpr->isInstantiationDependent() ||
          IntExpr->getType()->isIntegerType()) &&
         "Condition expression type not scalar/dependent");
}

OpenACCAsyncClause *OpenACCAsyncClause::Create(const ASTContext &C,
                                               SourceLocation BeginLoc,
                                               SourceLocation LParenLoc,
                                               Expr *IntExpr,
                                               SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(sizeof(OpenACCAsyncClause), alignof(OpenACCAsyncClause));
  return new (Mem) OpenACCAsyncClause(BeginLoc, LParenLoc, IntExpr, EndLoc);
}

OpenACCWaitClause *OpenACCWaitClause::Create(
    const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
    Expr *DevNumExpr, SourceLocation QueuesLoc, ArrayRef<Expr *> QueueIdExprs,
    SourceLocation EndLoc) {
  // Allocates enough room in trailing storage for all the int-exprs, plus a
  // placeholder for the devnum.
  void *Mem = C.Allocate(
      OpenACCWaitClause::totalSizeToAlloc<Expr *>(QueueIdExprs.size() + 1));
  return new (Mem) OpenACCWaitClause(BeginLoc, LParenLoc, DevNumExpr, QueuesLoc,
                                     QueueIdExprs, EndLoc);
}

OpenACCNumGangsClause *OpenACCNumGangsClause::Create(const ASTContext &C,
                                                     SourceLocation BeginLoc,
                                                     SourceLocation LParenLoc,
                                                     ArrayRef<Expr *> IntExprs,
                                                     SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCNumGangsClause::totalSizeToAlloc<Expr *>(IntExprs.size()));
  return new (Mem) OpenACCNumGangsClause(BeginLoc, LParenLoc, IntExprs, EndLoc);
}

OpenACCPrivateClause *OpenACCPrivateClause::Create(const ASTContext &C,
                                                   SourceLocation BeginLoc,
                                                   SourceLocation LParenLoc,
                                                   ArrayRef<Expr *> VarList,
                                                   SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCPrivateClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCPrivateClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCFirstPrivateClause *OpenACCFirstPrivateClause::Create(
    const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
    ArrayRef<Expr *> VarList, SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCFirstPrivateClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem)
      OpenACCFirstPrivateClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCAttachClause *OpenACCAttachClause::Create(const ASTContext &C,
                                                 SourceLocation BeginLoc,
                                                 SourceLocation LParenLoc,
                                                 ArrayRef<Expr *> VarList,
                                                 SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(OpenACCAttachClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCAttachClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCDevicePtrClause *OpenACCDevicePtrClause::Create(const ASTContext &C,
                                                       SourceLocation BeginLoc,
                                                       SourceLocation LParenLoc,
                                                       ArrayRef<Expr *> VarList,
                                                       SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCDevicePtrClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCDevicePtrClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCNoCreateClause *OpenACCNoCreateClause::Create(const ASTContext &C,
                                                     SourceLocation BeginLoc,
                                                     SourceLocation LParenLoc,
                                                     ArrayRef<Expr *> VarList,
                                                     SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCNoCreateClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCNoCreateClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCPresentClause *OpenACCPresentClause::Create(const ASTContext &C,
                                                   SourceLocation BeginLoc,
                                                   SourceLocation LParenLoc,
                                                   ArrayRef<Expr *> VarList,
                                                   SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCPresentClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCPresentClause(BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCCopyClause *
OpenACCCopyClause::Create(const ASTContext &C, OpenACCClauseKind Spelling,
                          SourceLocation BeginLoc, SourceLocation LParenLoc,
                          ArrayRef<Expr *> VarList, SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(OpenACCCopyClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem)
      OpenACCCopyClause(Spelling, BeginLoc, LParenLoc, VarList, EndLoc);
}

OpenACCCopyInClause *
OpenACCCopyInClause::Create(const ASTContext &C, OpenACCClauseKind Spelling,
                            SourceLocation BeginLoc, SourceLocation LParenLoc,
                            bool IsReadOnly, ArrayRef<Expr *> VarList,
                            SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(OpenACCCopyInClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCCopyInClause(Spelling, BeginLoc, LParenLoc,
                                       IsReadOnly, VarList, EndLoc);
}

OpenACCCopyOutClause *
OpenACCCopyOutClause::Create(const ASTContext &C, OpenACCClauseKind Spelling,
                             SourceLocation BeginLoc, SourceLocation LParenLoc,
                             bool IsZero, ArrayRef<Expr *> VarList,
                             SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCCopyOutClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCCopyOutClause(Spelling, BeginLoc, LParenLoc, IsZero,
                                        VarList, EndLoc);
}

OpenACCCreateClause *
OpenACCCreateClause::Create(const ASTContext &C, OpenACCClauseKind Spelling,
                            SourceLocation BeginLoc, SourceLocation LParenLoc,
                            bool IsZero, ArrayRef<Expr *> VarList,
                            SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(OpenACCCreateClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem) OpenACCCreateClause(Spelling, BeginLoc, LParenLoc, IsZero,
                                       VarList, EndLoc);
}

OpenACCDeviceTypeClause *OpenACCDeviceTypeClause::Create(
    const ASTContext &C, OpenACCClauseKind K, SourceLocation BeginLoc,
    SourceLocation LParenLoc, ArrayRef<DeviceTypeArgument> Archs,
    SourceLocation EndLoc) {
  void *Mem =
      C.Allocate(OpenACCDeviceTypeClause::totalSizeToAlloc<DeviceTypeArgument>(
          Archs.size()));
  return new (Mem)
      OpenACCDeviceTypeClause(K, BeginLoc, LParenLoc, Archs, EndLoc);
}

OpenACCReductionClause *OpenACCReductionClause::Create(
    const ASTContext &C, SourceLocation BeginLoc, SourceLocation LParenLoc,
    OpenACCReductionOperator Operator, ArrayRef<Expr *> VarList,
    SourceLocation EndLoc) {
  void *Mem = C.Allocate(
      OpenACCReductionClause::totalSizeToAlloc<Expr *>(VarList.size()));
  return new (Mem)
      OpenACCReductionClause(BeginLoc, LParenLoc, Operator, VarList, EndLoc);
}

OpenACCAutoClause *OpenACCAutoClause::Create(const ASTContext &C,
                                             SourceLocation BeginLoc,
                                             SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCAutoClause));
  return new (Mem) OpenACCAutoClause(BeginLoc, EndLoc);
}

OpenACCIndependentClause *
OpenACCIndependentClause::Create(const ASTContext &C, SourceLocation BeginLoc,
                                 SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCIndependentClause));
  return new (Mem) OpenACCIndependentClause(BeginLoc, EndLoc);
}

OpenACCSeqClause *OpenACCSeqClause::Create(const ASTContext &C,
                                           SourceLocation BeginLoc,
                                           SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCSeqClause));
  return new (Mem) OpenACCSeqClause(BeginLoc, EndLoc);
}

OpenACCGangClause *OpenACCGangClause::Create(const ASTContext &C,
                                             SourceLocation BeginLoc,
                                             SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCGangClause));
  return new (Mem) OpenACCGangClause(BeginLoc, EndLoc);
}

OpenACCWorkerClause *OpenACCWorkerClause::Create(const ASTContext &C,
                                                 SourceLocation BeginLoc,
                                                 SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCWorkerClause));
  return new (Mem) OpenACCWorkerClause(BeginLoc, EndLoc);
}

OpenACCVectorClause *OpenACCVectorClause::Create(const ASTContext &C,
                                                 SourceLocation BeginLoc,
                                                 SourceLocation EndLoc) {
  void *Mem = C.Allocate(sizeof(OpenACCVectorClause));
  return new (Mem) OpenACCVectorClause(BeginLoc, EndLoc);
}

//===----------------------------------------------------------------------===//
//  OpenACC clauses printing methods
//===----------------------------------------------------------------------===//

void OpenACCClausePrinter::printExpr(const Expr *E) {
  E->printPretty(OS, nullptr, Policy, 0);
}

void OpenACCClausePrinter::VisitDefaultClause(const OpenACCDefaultClause &C) {
  OS << "default(" << C.getDefaultClauseKind() << ")";
}

void OpenACCClausePrinter::VisitIfClause(const OpenACCIfClause &C) {
  OS << "if(";
  printExpr(C.getConditionExpr());
  OS << ")";
}

void OpenACCClausePrinter::VisitSelfClause(const OpenACCSelfClause &C) {
  OS << "self";
  if (const Expr *CondExpr = C.getConditionExpr()) {
    OS << "(";
    printExpr(CondExpr);
    OS << ")";
  }
}

void OpenACCClausePrinter::VisitNumGangsClause(const OpenACCNumGangsClause &C) {
  OS << "num_gangs(";
  llvm::interleaveComma(C.getIntExprs(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitNumWorkersClause(
    const OpenACCNumWorkersClause &C) {
  OS << "num_workers(";
  printExpr(C.getIntExpr());
  OS << ")";
}

void OpenACCClausePrinter::VisitVectorLengthClause(
    const OpenACCVectorLengthClause &C) {
  OS << "vector_length(";
  printExpr(C.getIntExpr());
  OS << ")";
}

void OpenACCClausePrinter::VisitAsyncClause(const OpenACCAsyncClause &C) {
  OS << "async";
  if (C.hasIntExpr()) {
    OS << "(";
    printExpr(C.getIntExpr());
    OS << ")";
  }
}

void OpenACCClausePrinter::VisitPrivateClause(const OpenACCPrivateClause &C) {
  OS << "private(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitFirstPrivateClause(
    const OpenACCFirstPrivateClause &C) {
  OS << "firstprivate(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitAttachClause(const OpenACCAttachClause &C) {
  OS << "attach(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitDevicePtrClause(
    const OpenACCDevicePtrClause &C) {
  OS << "deviceptr(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitNoCreateClause(const OpenACCNoCreateClause &C) {
  OS << "no_create(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitPresentClause(const OpenACCPresentClause &C) {
  OS << "present(";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitCopyClause(const OpenACCCopyClause &C) {
  OS << C.getClauseKind() << '(';
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitCopyInClause(const OpenACCCopyInClause &C) {
  OS << C.getClauseKind() << '(';
  if (C.isReadOnly())
    OS << "readonly: ";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitCopyOutClause(const OpenACCCopyOutClause &C) {
  OS << C.getClauseKind() << '(';
  if (C.isZero())
    OS << "zero: ";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitCreateClause(const OpenACCCreateClause &C) {
  OS << C.getClauseKind() << '(';
  if (C.isZero())
    OS << "zero: ";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitReductionClause(
    const OpenACCReductionClause &C) {
  OS << "reduction(" << C.getReductionOp() << ": ";
  llvm::interleaveComma(C.getVarList(), OS,
                        [&](const Expr *E) { printExpr(E); });
  OS << ")";
}

void OpenACCClausePrinter::VisitWaitClause(const OpenACCWaitClause &C) {
  OS << "wait";
  if (!C.getLParenLoc().isInvalid()) {
    OS << "(";
    if (C.hasDevNumExpr()) {
      OS << "devnum: ";
      printExpr(C.getDevNumExpr());
      OS << " : ";
    }

    if (C.hasQueuesTag())
      OS << "queues: ";

    llvm::interleaveComma(C.getQueueIdExprs(), OS,
                          [&](const Expr *E) { printExpr(E); });
    OS << ")";
  }
}

void OpenACCClausePrinter::VisitDeviceTypeClause(
    const OpenACCDeviceTypeClause &C) {
  OS << C.getClauseKind();
  OS << "(";
  llvm::interleaveComma(C.getArchitectures(), OS,
                        [&](const DeviceTypeArgument &Arch) {
                          if (Arch.first == nullptr)
                            OS << "*";
                          else
                            OS << Arch.first->getName();
                        });
  OS << ")";
}

void OpenACCClausePrinter::VisitAutoClause(const OpenACCAutoClause &C) {
  OS << "auto";
}

void OpenACCClausePrinter::VisitIndependentClause(
    const OpenACCIndependentClause &C) {
  OS << "independent";
}

void OpenACCClausePrinter::VisitSeqClause(const OpenACCSeqClause &C) {
  OS << "seq";
}
