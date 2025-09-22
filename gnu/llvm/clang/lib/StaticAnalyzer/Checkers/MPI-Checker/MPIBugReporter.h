//===-- MPIBugReporter.h - bug reporter -----------------------*- C++ -*-===//
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

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPIBUGREPORTER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPIBUGREPORTER_H

#include "MPITypes.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace ento {
namespace mpi {

class MPIBugReporter {
public:
  MPIBugReporter(const CheckerBase &CB)
      : UnmatchedWaitBugType(&CB, "Unmatched wait", MPIError),
        MissingWaitBugType(&CB, "Missing wait", MPIError),
        DoubleNonblockingBugType(&CB, "Double nonblocking", MPIError) {}

  /// Report duplicate request use by nonblocking calls without intermediate
  /// wait.
  ///
  /// \param MPICallEvent MPI call that caused the double nonblocking
  /// \param Req request that was used by two nonblocking calls in sequence
  /// \param RequestRegion memory region of the request
  /// \param ExplNode node in the graph the bug appeared at
  /// \param BReporter bug reporter for current context
  void reportDoubleNonblocking(const CallEvent &MPICallEvent,
                               const Request &Req,
                               const MemRegion *const RequestRegion,
                               const ExplodedNode *const ExplNode,
                              BugReporter &BReporter) const;

  /// Report a missing wait for a nonblocking call.
  ///
  /// \param Req request that is not matched by a wait
  /// \param RequestRegion memory region of the request
  /// \param ExplNode node in the graph the bug appeared at
  /// \param BReporter bug reporter for current context
  void reportMissingWait(const Request &Req,
                         const MemRegion *const RequestRegion,
                         const ExplodedNode *const ExplNode,
                         BugReporter &BReporter) const;

  /// Report a wait on a request that has not been used at all before.
  ///
  /// \param CE wait call that uses the request
  /// \param RequestRegion memory region of the request
  /// \param ExplNode node in the graph the bug appeared at
  /// \param BReporter bug reporter for current context
  void reportUnmatchedWait(const CallEvent &CE,
                           const MemRegion *const RequestRegion,
                           const ExplodedNode *const ExplNode,
                           BugReporter &BReporter) const;

private:
  const llvm::StringLiteral MPIError = "MPI Error";
  const BugType UnmatchedWaitBugType;
  const BugType MissingWaitBugType;
  const BugType DoubleNonblockingBugType;

  /// Bug visitor class to find the node where the request region was previously
  /// used in order to include it into the BugReport path.
  class RequestNodeVisitor : public BugReporterVisitor {
  public:
    RequestNodeVisitor(const MemRegion *const MemoryRegion,
                       const std::string &ErrText)
        : RequestRegion(MemoryRegion), ErrorText(ErrText) {}

    void Profile(llvm::FoldingSetNodeID &ID) const override {
      static int X = 0;
      ID.AddPointer(&X);
      ID.AddPointer(RequestRegion);
    }

    PathDiagnosticPieceRef VisitNode(const ExplodedNode *N,
                                     BugReporterContext &BRC,
                                     PathSensitiveBugReport &BR) override;

  private:
    const MemRegion *const RequestRegion;
    bool IsNodeFound = false;
    std::string ErrorText;
  };
};

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang

#endif
