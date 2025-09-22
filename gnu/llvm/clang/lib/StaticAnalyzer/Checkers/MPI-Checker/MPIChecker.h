//===-- MPIChecker.h - Verify MPI API usage- --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the main class of MPI-Checker which serves as an entry
/// point. It is created once for each translation unit analysed.
/// The checker defines path-sensitive checks, to verify correct usage of the
/// MPI API.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPICHECKER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MPICHECKER_MPICHECKER_H

#include "MPIBugReporter.h"
#include "MPITypes.h"
#include "clang/StaticAnalyzer/Checkers/MPIFunctionClassifier.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

namespace clang {
namespace ento {
namespace mpi {

class MPIChecker : public Checker<check::PreCall, check::DeadSymbols> {
public:
  MPIChecker() : BReporter(*this) {}

  // path-sensitive callbacks
  void checkPreCall(const CallEvent &CE, CheckerContext &Ctx) const {
    dynamicInit(Ctx);
    checkUnmatchedWaits(CE, Ctx);
    checkDoubleNonblocking(CE, Ctx);
  }

  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &Ctx) const {
    dynamicInit(Ctx);
    checkMissingWaits(SymReaper, Ctx);
  }

  void dynamicInit(CheckerContext &Ctx) const {
    if (FuncClassifier)
      return;
    const_cast<std::unique_ptr<MPIFunctionClassifier> &>(FuncClassifier)
        .reset(new MPIFunctionClassifier{Ctx.getASTContext()});
  }

  /// Checks if a request is used by nonblocking calls multiple times
  /// in sequence without intermediate wait. The check contains a guard,
  /// in order to only inspect nonblocking functions.
  ///
  /// \param PreCallEvent MPI call to verify
  void checkDoubleNonblocking(const clang::ento::CallEvent &PreCallEvent,
                              clang::ento::CheckerContext &Ctx) const;

  /// Checks if the request used by the wait function was not used at all
  /// before. The check contains a guard, in order to only inspect wait
  /// functions.
  ///
  /// \param PreCallEvent MPI call to verify
  void checkUnmatchedWaits(const clang::ento::CallEvent &PreCallEvent,
                           clang::ento::CheckerContext &Ctx) const;

  /// Check if a nonblocking call is not matched by a wait.
  /// If a memory region is not alive and the last function using the
  /// request was a nonblocking call, this is rated as a missing wait.
  void checkMissingWaits(clang::ento::SymbolReaper &SymReaper,
                         clang::ento::CheckerContext &Ctx) const;

private:
  /// Collects all memory regions of a request(array) used by a wait
  /// function. If the wait function uses a single request, this is a single
  /// region. For wait functions using multiple requests, multiple regions
  /// representing elements in the array are collected.
  ///
  /// \param ReqRegions vector the regions get pushed into
  /// \param MR top most region to iterate
  /// \param CE MPI wait call using the request(s)
  void allRegionsUsedByWait(
      llvm::SmallVector<const clang::ento::MemRegion *, 2> &ReqRegions,
      const clang::ento::MemRegion *const MR, const clang::ento::CallEvent &CE,
      clang::ento::CheckerContext &Ctx) const;

  /// Returns the memory region used by a wait function.
  /// Distinguishes between MPI_Wait and MPI_Waitall.
  ///
  /// \param CE MPI wait call
  const clang::ento::MemRegion *
  topRegionUsedByWait(const clang::ento::CallEvent &CE) const;

  const std::unique_ptr<MPIFunctionClassifier> FuncClassifier;
  MPIBugReporter BReporter;
};

} // end of namespace: mpi
} // end of namespace: ento
} // end of namespace: clang

#endif
