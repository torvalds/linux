//===-- SimpleStreamChecker.cpp -----------------------------------*- C++ -*--//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines a checker for proper use of fopen/fclose APIs.
//   - If a file has been closed with fclose, it should not be accessed again.
//   Accessing a closed file results in undefined behavior.
//   - If a file was opened with fopen, it must be closed with fclose before
//   the execution ends. Failing to do so results in a resource leak.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallDescription.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include <utility>

using namespace clang;
using namespace ento;

namespace {
typedef SmallVector<SymbolRef, 2> SymbolVector;

struct StreamState {
private:
  enum Kind { Opened, Closed } K;
  StreamState(Kind InK) : K(InK) { }

public:
  bool isOpened() const { return K == Opened; }
  bool isClosed() const { return K == Closed; }

  static StreamState getOpened() { return StreamState(Opened); }
  static StreamState getClosed() { return StreamState(Closed); }

  bool operator==(const StreamState &X) const {
    return K == X.K;
  }
  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddInteger(K);
  }
};

class SimpleStreamChecker : public Checker<check::PostCall,
                                           check::PreCall,
                                           check::DeadSymbols,
                                           check::PointerEscape> {
  const CallDescription OpenFn{CDM::CLibrary, {"fopen"}, 2};
  const CallDescription CloseFn{CDM::CLibrary, {"fclose"}, 1};

  const BugType DoubleCloseBugType{this, "Double fclose",
                                   "Unix Stream API Error"};
  const BugType LeakBugType{this, "Resource Leak", "Unix Stream API Error",
                            /*SuppressOnSink=*/true};

  void reportDoubleClose(SymbolRef FileDescSym,
                         const CallEvent &Call,
                         CheckerContext &C) const;

  void reportLeaks(ArrayRef<SymbolRef> LeakedStreams, CheckerContext &C,
                   ExplodedNode *ErrNode) const;

  bool guaranteedNotToCloseFile(const CallEvent &Call) const;

public:
  /// Process fopen.
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  /// Process fclose.
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;

  void checkDeadSymbols(SymbolReaper &SymReaper, CheckerContext &C) const;

  /// Stop tracking addresses which escape.
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                    const InvalidatedSymbols &Escaped,
                                    const CallEvent *Call,
                                    PointerEscapeKind Kind) const;
};

} // end anonymous namespace

/// The state of the checker is a map from tracked stream symbols to their
/// state. Let's store it in the ProgramState.
REGISTER_MAP_WITH_PROGRAMSTATE(StreamMap, SymbolRef, StreamState)

void SimpleStreamChecker::checkPostCall(const CallEvent &Call,
                                        CheckerContext &C) const {
  if (!OpenFn.matches(Call))
    return;

  // Get the symbolic value corresponding to the file handle.
  SymbolRef FileDesc = Call.getReturnValue().getAsSymbol();
  if (!FileDesc)
    return;

  // Generate the next transition (an edge in the exploded graph).
  ProgramStateRef State = C.getState();
  State = State->set<StreamMap>(FileDesc, StreamState::getOpened());
  C.addTransition(State);
}

void SimpleStreamChecker::checkPreCall(const CallEvent &Call,
                                       CheckerContext &C) const {
  if (!CloseFn.matches(Call))
    return;

  // Get the symbolic value corresponding to the file handle.
  SymbolRef FileDesc = Call.getArgSVal(0).getAsSymbol();
  if (!FileDesc)
    return;

  // Check if the stream has already been closed.
  ProgramStateRef State = C.getState();
  const StreamState *SS = State->get<StreamMap>(FileDesc);
  if (SS && SS->isClosed()) {
    reportDoubleClose(FileDesc, Call, C);
    return;
  }

  // Generate the next transition, in which the stream is closed.
  State = State->set<StreamMap>(FileDesc, StreamState::getClosed());
  C.addTransition(State);
}

static bool isLeaked(SymbolRef Sym, const StreamState &SS,
                     bool IsSymDead, ProgramStateRef State) {
  if (IsSymDead && SS.isOpened()) {
    // If a symbol is NULL, assume that fopen failed on this path.
    // A symbol should only be considered leaked if it is non-null.
    ConstraintManager &CMgr = State->getConstraintManager();
    ConditionTruthVal OpenFailed = CMgr.isNull(State, Sym);
    return !OpenFailed.isConstrainedTrue();
  }
  return false;
}

void SimpleStreamChecker::checkDeadSymbols(SymbolReaper &SymReaper,
                                           CheckerContext &C) const {
  ProgramStateRef State = C.getState();
  SymbolVector LeakedStreams;
  StreamMapTy TrackedStreams = State->get<StreamMap>();
  for (auto [Sym, StreamStatus] : TrackedStreams) {
    bool IsSymDead = SymReaper.isDead(Sym);

    // Collect leaked symbols.
    if (isLeaked(Sym, StreamStatus, IsSymDead, State))
      LeakedStreams.push_back(Sym);

    // Remove the dead symbol from the streams map.
    if (IsSymDead)
      State = State->remove<StreamMap>(Sym);
  }

  ExplodedNode *N = C.generateNonFatalErrorNode(State);
  if (!N)
    return;
  reportLeaks(LeakedStreams, C, N);
}

void SimpleStreamChecker::reportDoubleClose(SymbolRef FileDescSym,
                                            const CallEvent &Call,
                                            CheckerContext &C) const {
  // We reached a bug, stop exploring the path here by generating a sink.
  ExplodedNode *ErrNode = C.generateErrorNode();
  // If we've already reached this node on another path, return.
  if (!ErrNode)
    return;

  // Generate the report.
  auto R = std::make_unique<PathSensitiveBugReport>(
      DoubleCloseBugType, "Closing a previously closed file stream", ErrNode);
  R->addRange(Call.getSourceRange());
  R->markInteresting(FileDescSym);
  C.emitReport(std::move(R));
}

void SimpleStreamChecker::reportLeaks(ArrayRef<SymbolRef> LeakedStreams,
                                      CheckerContext &C,
                                      ExplodedNode *ErrNode) const {
  // Attach bug reports to the leak node.
  // TODO: Identify the leaked file descriptor.
  for (SymbolRef LeakedStream : LeakedStreams) {
    auto R = std::make_unique<PathSensitiveBugReport>(
        LeakBugType, "Opened file is never closed; potential resource leak",
        ErrNode);
    R->markInteresting(LeakedStream);
    C.emitReport(std::move(R));
  }
}

bool SimpleStreamChecker::guaranteedNotToCloseFile(const CallEvent &Call) const{
  // If it's not in a system header, assume it might close a file.
  if (!Call.isInSystemHeader())
    return false;

  // Handle cases where we know a buffer's /address/ can escape.
  if (Call.argumentsMayEscape())
    return false;

  // Note, even though fclose closes the file, we do not list it here
  // since the checker is modeling the call.

  return true;
}

// If the pointer we are tracking escaped, do not track the symbol as
// we cannot reason about it anymore.
ProgramStateRef
SimpleStreamChecker::checkPointerEscape(ProgramStateRef State,
                                        const InvalidatedSymbols &Escaped,
                                        const CallEvent *Call,
                                        PointerEscapeKind Kind) const {
  // If we know that the call cannot close a file, there is nothing to do.
  if (Kind == PSK_DirectEscapeOnCall && guaranteedNotToCloseFile(*Call)) {
    return State;
  }

  for (SymbolRef Sym : Escaped) {
    // The symbol escaped. Optimistically, assume that the corresponding file
    // handle will be closed somewhere else.
    State = State->remove<StreamMap>(Sym);
  }
  return State;
}

void ento::registerSimpleStreamChecker(CheckerManager &mgr) {
  mgr.registerChecker<SimpleStreamChecker>();
}

// This checker should be enabled regardless of how language options are set.
bool ento::shouldRegisterSimpleStreamChecker(const CheckerManager &mgr) {
  return true;
}
