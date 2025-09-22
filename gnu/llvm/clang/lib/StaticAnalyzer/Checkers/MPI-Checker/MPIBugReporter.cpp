//===-- MPIBugReporter.cpp - bug reporter -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines prefabricated reports which are emitted in
/// case of MPI related bugs, detected by path-sensitive analysis.
///
//===----------------------------------------------------------------------===//

#include "MPIBugReporter.h"
#include "MPIChecker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"

namespace clang {
namespace ento {
namespace mpi {

void MPIBugReporter::reportDoubleNonblocking(
    const CallEvent &MPICallEvent, const ento::mpi::Request &Req,
    const MemRegion *const RequestRegion,
    const ExplodedNode *const ExplNode,
    BugReporter &BReporter) const {

  std::string ErrorText;
  ErrorText = "Double nonblocking on request " +
              RequestRegion->getDescriptiveName() + ". ";

  auto Report = std::make_unique<PathSensitiveBugReport>(
      DoubleNonblockingBugType, ErrorText, ExplNode);

  Report->addRange(MPICallEvent.getSourceRange());
  SourceRange Range = RequestRegion->sourceRange();

  if (Range.isValid())
    Report->addRange(Range);

  Report->addVisitor(std::make_unique<RequestNodeVisitor>(
      RequestRegion, "Request is previously used by nonblocking call here. "));
  Report->markInteresting(RequestRegion);

  BReporter.emitReport(std::move(Report));
}

void MPIBugReporter::reportMissingWait(
    const ento::mpi::Request &Req, const MemRegion *const RequestRegion,
    const ExplodedNode *const ExplNode,
    BugReporter &BReporter) const {
  std::string ErrorText{"Request " + RequestRegion->getDescriptiveName() +
                        " has no matching wait. "};

  auto Report = std::make_unique<PathSensitiveBugReport>(MissingWaitBugType,
                                                         ErrorText, ExplNode);

  SourceRange Range = RequestRegion->sourceRange();
  if (Range.isValid())
    Report->addRange(Range);
  Report->addVisitor(std::make_unique<RequestNodeVisitor>(
      RequestRegion, "Request is previously used by nonblocking call here. "));
  Report->markInteresting(RequestRegion);

  BReporter.emitReport(std::move(Report));
}

void MPIBugReporter::reportUnmatchedWait(
    const CallEvent &CE, const clang::ento::MemRegion *const RequestRegion,
    const ExplodedNode *const ExplNode,
    BugReporter &BReporter) const {
  std::string ErrorText{"Request " + RequestRegion->getDescriptiveName() +
                        " has no matching nonblocking call. "};

  auto Report = std::make_unique<PathSensitiveBugReport>(UnmatchedWaitBugType,
                                                         ErrorText, ExplNode);

  Report->addRange(CE.getSourceRange());
  SourceRange Range = RequestRegion->sourceRange();
  if (Range.isValid())
    Report->addRange(Range);

  BReporter.emitReport(std::move(Report));
}

PathDiagnosticPieceRef
MPIBugReporter::RequestNodeVisitor::VisitNode(const ExplodedNode *N,
                                              BugReporterContext &BRC,
                                              PathSensitiveBugReport &BR) {

  if (IsNodeFound)
    return nullptr;

  const Request *const Req = N->getState()->get<RequestMap>(RequestRegion);
  assert(Req && "The region must be tracked and alive, given that we've "
                "just emitted a report against it");
  const Request *const PrevReq =
      N->getFirstPred()->getState()->get<RequestMap>(RequestRegion);

  // Check if request was previously unused or in a different state.
  if (!PrevReq || (Req->CurrentState != PrevReq->CurrentState)) {
    IsNodeFound = true;

    ProgramPoint P = N->getFirstPred()->getLocation();
    PathDiagnosticLocation L =
        PathDiagnosticLocation::create(P, BRC.getSourceManager());

    return std::make_shared<PathDiagnosticEventPiece>(L, ErrorText);
  }

  return nullptr;
}

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang
