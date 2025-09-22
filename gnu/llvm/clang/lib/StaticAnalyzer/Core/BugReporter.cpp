//===- BugReporter.cpp - Generate PathDiagnostics for bugs ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines BugReporter, a utility class for generating
//  PathDiagnostics.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporter.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/StmtObjC.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CFGStmtMap.h"
#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/AnalyzerOptions.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/BugReporter/Z3CrosscheckVisitor.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/CheckerRegistryData.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SMTConv.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymbolManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;
using namespace llvm;

#define DEBUG_TYPE "BugReporter"

STATISTIC(MaxBugClassSize,
          "The maximum number of bug reports in the same equivalence class");
STATISTIC(MaxValidBugClassSize,
          "The maximum number of bug reports in the same equivalence class "
          "where at least one report is valid (not suppressed)");

STATISTIC(NumTimesReportPassesZ3, "Number of reports passed Z3");
STATISTIC(NumTimesReportRefuted, "Number of reports refuted by Z3");
STATISTIC(NumTimesReportEQClassAborted,
          "Number of times a report equivalence class was aborted by the Z3 "
          "oracle heuristic");
STATISTIC(NumTimesReportEQClassWasExhausted,
          "Number of times all reports of an equivalence class was refuted");

BugReporterVisitor::~BugReporterVisitor() = default;

void BugReporterContext::anchor() {}

//===----------------------------------------------------------------------===//
// PathDiagnosticBuilder and its associated routines and helper objects.
//===----------------------------------------------------------------------===//

namespace {

/// A (CallPiece, node assiciated with its CallEnter) pair.
using CallWithEntry =
    std::pair<PathDiagnosticCallPiece *, const ExplodedNode *>;
using CallWithEntryStack = SmallVector<CallWithEntry, 6>;

/// Map from each node to the diagnostic pieces visitors emit for them.
using VisitorsDiagnosticsTy =
    llvm::DenseMap<const ExplodedNode *, std::vector<PathDiagnosticPieceRef>>;

/// A map from PathDiagnosticPiece to the LocationContext of the inlined
/// function call it represents.
using LocationContextMap =
    llvm::DenseMap<const PathPieces *, const LocationContext *>;

/// A helper class that contains everything needed to construct a
/// PathDiagnostic object. It does no much more then providing convenient
/// getters and some well placed asserts for extra security.
class PathDiagnosticConstruct {
  /// The consumer we're constructing the bug report for.
  const PathDiagnosticConsumer *Consumer;
  /// Our current position in the bug path, which is owned by
  /// PathDiagnosticBuilder.
  const ExplodedNode *CurrentNode;
  /// A mapping from parts of the bug path (for example, a function call, which
  /// would span backwards from a CallExit to a CallEnter with the nodes in
  /// between them) with the location contexts it is associated with.
  LocationContextMap LCM;
  const SourceManager &SM;

public:
  /// We keep stack of calls to functions as we're ascending the bug path.
  /// TODO: PathDiagnostic has a stack doing the same thing, shouldn't we use
  /// that instead?
  CallWithEntryStack CallStack;
  /// The bug report we're constructing. For ease of use, this field is kept
  /// public, though some "shortcut" getters are provided for commonly used
  /// methods of PathDiagnostic.
  std::unique_ptr<PathDiagnostic> PD;

public:
  PathDiagnosticConstruct(const PathDiagnosticConsumer *PDC,
                          const ExplodedNode *ErrorNode,
                          const PathSensitiveBugReport *R,
                          const Decl *AnalysisEntryPoint);

  /// \returns the location context associated with the current position in the
  /// bug path.
  const LocationContext *getCurrLocationContext() const {
    assert(CurrentNode && "Already reached the root!");
    return CurrentNode->getLocationContext();
  }

  /// Same as getCurrLocationContext (they should always return the same
  /// location context), but works after reaching the root of the bug path as
  /// well.
  const LocationContext *getLocationContextForActivePath() const {
    return LCM.find(&PD->getActivePath())->getSecond();
  }

  const ExplodedNode *getCurrentNode() const { return CurrentNode; }

  /// Steps the current node to its predecessor.
  /// \returns whether we reached the root of the bug path.
  bool ascendToPrevNode() {
    CurrentNode = CurrentNode->getFirstPred();
    return static_cast<bool>(CurrentNode);
  }

  const ParentMap &getParentMap() const {
    return getCurrLocationContext()->getParentMap();
  }

  const SourceManager &getSourceManager() const { return SM; }

  const Stmt *getParent(const Stmt *S) const {
    return getParentMap().getParent(S);
  }

  void updateLocCtxMap(const PathPieces *Path, const LocationContext *LC) {
    assert(Path && LC);
    LCM[Path] = LC;
  }

  const LocationContext *getLocationContextFor(const PathPieces *Path) const {
    assert(LCM.count(Path) &&
           "Failed to find the context associated with these pieces!");
    return LCM.find(Path)->getSecond();
  }

  bool isInLocCtxMap(const PathPieces *Path) const { return LCM.count(Path); }

  PathPieces &getActivePath() { return PD->getActivePath(); }
  PathPieces &getMutablePieces() { return PD->getMutablePieces(); }

  bool shouldAddPathEdges() const { return Consumer->shouldAddPathEdges(); }
  bool shouldAddControlNotes() const {
    return Consumer->shouldAddControlNotes();
  }
  bool shouldGenerateDiagnostics() const {
    return Consumer->shouldGenerateDiagnostics();
  }
  bool supportsLogicalOpControlFlow() const {
    return Consumer->supportsLogicalOpControlFlow();
  }
};

/// Contains every contextual information needed for constructing a
/// PathDiagnostic object for a given bug report. This class and its fields are
/// immutable, and passes a BugReportConstruct object around during the
/// construction.
class PathDiagnosticBuilder : public BugReporterContext {
  /// A linear path from the error node to the root.
  std::unique_ptr<const ExplodedGraph> BugPath;
  /// The bug report we're describing. Visitors create their diagnostics with
  /// them being the last entities being able to modify it (for example,
  /// changing interestingness here would cause inconsistencies as to how this
  /// file and visitors construct diagnostics), hence its const.
  const PathSensitiveBugReport *R;
  /// The leaf of the bug path. This isn't the same as the bug reports error
  /// node, which refers to the *original* graph, not the bug path.
  const ExplodedNode *const ErrorNode;
  /// The diagnostic pieces visitors emitted, which is expected to be collected
  /// by the time this builder is constructed.
  std::unique_ptr<const VisitorsDiagnosticsTy> VisitorsDiagnostics;

public:
  /// Find a non-invalidated report for a given equivalence class,  and returns
  /// a PathDiagnosticBuilder able to construct bug reports for different
  /// consumers. Returns std::nullopt if no valid report is found.
  static std::optional<PathDiagnosticBuilder>
  findValidReport(ArrayRef<PathSensitiveBugReport *> &bugReports,
                  PathSensitiveBugReporter &Reporter);

  PathDiagnosticBuilder(
      BugReporterContext BRC, std::unique_ptr<ExplodedGraph> BugPath,
      PathSensitiveBugReport *r, const ExplodedNode *ErrorNode,
      std::unique_ptr<VisitorsDiagnosticsTy> VisitorsDiagnostics);

  /// This function is responsible for generating diagnostic pieces that are
  /// *not* provided by bug report visitors.
  /// These diagnostics may differ depending on the consumer's settings,
  /// and are therefore constructed separately for each consumer.
  ///
  /// There are two path diagnostics generation modes: with adding edges (used
  /// for plists) and without  (used for HTML and text). When edges are added,
  /// the path is modified to insert artificially generated edges.
  /// Otherwise, more detailed diagnostics is emitted for block edges,
  /// explaining the transitions in words.
  std::unique_ptr<PathDiagnostic>
  generate(const PathDiagnosticConsumer *PDC) const;

private:
  void updateStackPiecesWithMessage(PathDiagnosticPieceRef P,
                                    const CallWithEntryStack &CallStack) const;
  void generatePathDiagnosticsForNode(PathDiagnosticConstruct &C,
                                      PathDiagnosticLocation &PrevLoc) const;

  void generateMinimalDiagForBlockEdge(PathDiagnosticConstruct &C,
                                       BlockEdge BE) const;

  PathDiagnosticPieceRef
  generateDiagForGotoOP(const PathDiagnosticConstruct &C, const Stmt *S,
                        PathDiagnosticLocation &Start) const;

  PathDiagnosticPieceRef
  generateDiagForSwitchOP(const PathDiagnosticConstruct &C, const CFGBlock *Dst,
                          PathDiagnosticLocation &Start) const;

  PathDiagnosticPieceRef
  generateDiagForBinaryOP(const PathDiagnosticConstruct &C, const Stmt *T,
                          const CFGBlock *Src, const CFGBlock *DstC) const;

  PathDiagnosticLocation
  ExecutionContinues(const PathDiagnosticConstruct &C) const;

  PathDiagnosticLocation
  ExecutionContinues(llvm::raw_string_ostream &os,
                     const PathDiagnosticConstruct &C) const;

  const PathSensitiveBugReport *getBugReport() const { return R; }
};

} // namespace

//===----------------------------------------------------------------------===//
// Base implementation of stack hint generators.
//===----------------------------------------------------------------------===//

StackHintGenerator::~StackHintGenerator() = default;

std::string StackHintGeneratorForSymbol::getMessage(const ExplodedNode *N){
  if (!N)
    return getMessageForSymbolNotFound();

  ProgramPoint P = N->getLocation();
  CallExitEnd CExit = P.castAs<CallExitEnd>();

  // FIXME: Use CallEvent to abstract this over all calls.
  const Stmt *CallSite = CExit.getCalleeContext()->getCallSite();
  const auto *CE = dyn_cast_or_null<CallExpr>(CallSite);
  if (!CE)
    return {};

  // Check if one of the parameters are set to the interesting symbol.
  for (auto [Idx, ArgExpr] : llvm::enumerate(CE->arguments())) {
    SVal SV = N->getSVal(ArgExpr);

    // Check if the variable corresponding to the symbol is passed by value.
    SymbolRef AS = SV.getAsLocSymbol();
    if (AS == Sym) {
      return getMessageForArg(ArgExpr, Idx);
    }

    // Check if the parameter is a pointer to the symbol.
    if (std::optional<loc::MemRegionVal> Reg = SV.getAs<loc::MemRegionVal>()) {
      // Do not attempt to dereference void*.
      if (ArgExpr->getType()->isVoidPointerType())
        continue;
      SVal PSV = N->getState()->getSVal(Reg->getRegion());
      SymbolRef AS = PSV.getAsLocSymbol();
      if (AS == Sym) {
        return getMessageForArg(ArgExpr, Idx);
      }
    }
  }

  // Check if we are returning the interesting symbol.
  SVal SV = N->getSVal(CE);
  SymbolRef RetSym = SV.getAsLocSymbol();
  if (RetSym == Sym) {
    return getMessageForReturn(CE);
  }

  return getMessageForSymbolNotFound();
}

std::string StackHintGeneratorForSymbol::getMessageForArg(const Expr *ArgE,
                                                          unsigned ArgIndex) {
  // Printed parameters start at 1, not 0.
  ++ArgIndex;

  return (llvm::Twine(Msg) + " via " + std::to_string(ArgIndex) +
          llvm::getOrdinalSuffix(ArgIndex) + " parameter").str();
}

//===----------------------------------------------------------------------===//
// Diagnostic cleanup.
//===----------------------------------------------------------------------===//

static PathDiagnosticEventPiece *
eventsDescribeSameCondition(PathDiagnosticEventPiece *X,
                            PathDiagnosticEventPiece *Y) {
  // Prefer diagnostics that come from ConditionBRVisitor over
  // those that came from TrackConstraintBRVisitor,
  // unless the one from ConditionBRVisitor is
  // its generic fallback diagnostic.
  const void *tagPreferred = ConditionBRVisitor::getTag();
  const void *tagLesser = TrackConstraintBRVisitor::getTag();

  if (X->getLocation() != Y->getLocation())
    return nullptr;

  if (X->getTag() == tagPreferred && Y->getTag() == tagLesser)
    return ConditionBRVisitor::isPieceMessageGeneric(X) ? Y : X;

  if (Y->getTag() == tagPreferred && X->getTag() == tagLesser)
    return ConditionBRVisitor::isPieceMessageGeneric(Y) ? X : Y;

  return nullptr;
}

/// An optimization pass over PathPieces that removes redundant diagnostics
/// generated by both ConditionBRVisitor and TrackConstraintBRVisitor.  Both
/// BugReporterVisitors use different methods to generate diagnostics, with
/// one capable of emitting diagnostics in some cases but not in others.  This
/// can lead to redundant diagnostic pieces at the same point in a path.
static void removeRedundantMsgs(PathPieces &path) {
  unsigned N = path.size();
  if (N < 2)
    return;
  // NOTE: this loop intentionally is not using an iterator.  Instead, we
  // are streaming the path and modifying it in place.  This is done by
  // grabbing the front, processing it, and if we decide to keep it append
  // it to the end of the path.  The entire path is processed in this way.
  for (unsigned i = 0; i < N; ++i) {
    auto piece = std::move(path.front());
    path.pop_front();

    switch (piece->getKind()) {
      case PathDiagnosticPiece::Call:
        removeRedundantMsgs(cast<PathDiagnosticCallPiece>(*piece).path);
        break;
      case PathDiagnosticPiece::Macro:
        removeRedundantMsgs(cast<PathDiagnosticMacroPiece>(*piece).subPieces);
        break;
      case PathDiagnosticPiece::Event: {
        if (i == N-1)
          break;

        if (auto *nextEvent =
            dyn_cast<PathDiagnosticEventPiece>(path.front().get())) {
          auto *event = cast<PathDiagnosticEventPiece>(piece.get());
          // Check to see if we should keep one of the two pieces.  If we
          // come up with a preference, record which piece to keep, and consume
          // another piece from the path.
          if (auto *pieceToKeep =
                  eventsDescribeSameCondition(event, nextEvent)) {
            piece = std::move(pieceToKeep == event ? piece : path.front());
            path.pop_front();
            ++i;
          }
        }
        break;
      }
      case PathDiagnosticPiece::ControlFlow:
      case PathDiagnosticPiece::Note:
      case PathDiagnosticPiece::PopUp:
        break;
    }
    path.push_back(std::move(piece));
  }
}

/// Recursively scan through a path and prune out calls and macros pieces
/// that aren't needed.  Return true if afterwards the path contains
/// "interesting stuff" which means it shouldn't be pruned from the parent path.
static bool removeUnneededCalls(const PathDiagnosticConstruct &C,
                                PathPieces &pieces,
                                const PathSensitiveBugReport *R,
                                bool IsInteresting = false) {
  bool containsSomethingInteresting = IsInteresting;
  const unsigned N = pieces.size();

  for (unsigned i = 0 ; i < N ; ++i) {
    // Remove the front piece from the path.  If it is still something we
    // want to keep once we are done, we will push it back on the end.
    auto piece = std::move(pieces.front());
    pieces.pop_front();

    switch (piece->getKind()) {
      case PathDiagnosticPiece::Call: {
        auto &call = cast<PathDiagnosticCallPiece>(*piece);
        // Check if the location context is interesting.
        if (!removeUnneededCalls(
                C, call.path, R,
                R->isInteresting(C.getLocationContextFor(&call.path))))
          continue;

        containsSomethingInteresting = true;
        break;
      }
      case PathDiagnosticPiece::Macro: {
        auto &macro = cast<PathDiagnosticMacroPiece>(*piece);
        if (!removeUnneededCalls(C, macro.subPieces, R, IsInteresting))
          continue;
        containsSomethingInteresting = true;
        break;
      }
      case PathDiagnosticPiece::Event: {
        auto &event = cast<PathDiagnosticEventPiece>(*piece);

        // We never throw away an event, but we do throw it away wholesale
        // as part of a path if we throw the entire path away.
        containsSomethingInteresting |= !event.isPrunable();
        break;
      }
      case PathDiagnosticPiece::ControlFlow:
      case PathDiagnosticPiece::Note:
      case PathDiagnosticPiece::PopUp:
        break;
    }

    pieces.push_back(std::move(piece));
  }

  return containsSomethingInteresting;
}

/// Same logic as above to remove extra pieces.
static void removePopUpNotes(PathPieces &Path) {
  for (unsigned int i = 0; i < Path.size(); ++i) {
    auto Piece = std::move(Path.front());
    Path.pop_front();
    if (!isa<PathDiagnosticPopUpPiece>(*Piece))
      Path.push_back(std::move(Piece));
  }
}

/// Returns true if the given decl has been implicitly given a body, either by
/// the analyzer or by the compiler proper.
static bool hasImplicitBody(const Decl *D) {
  assert(D);
  return D->isImplicit() || !D->hasBody();
}

/// Recursively scan through a path and make sure that all call pieces have
/// valid locations.
static void
adjustCallLocations(PathPieces &Pieces,
                    PathDiagnosticLocation *LastCallLocation = nullptr) {
  for (const auto &I : Pieces) {
    auto *Call = dyn_cast<PathDiagnosticCallPiece>(I.get());

    if (!Call)
      continue;

    if (LastCallLocation) {
      bool CallerIsImplicit = hasImplicitBody(Call->getCaller());
      if (CallerIsImplicit || !Call->callEnter.asLocation().isValid())
        Call->callEnter = *LastCallLocation;
      if (CallerIsImplicit || !Call->callReturn.asLocation().isValid())
        Call->callReturn = *LastCallLocation;
    }

    // Recursively clean out the subclass.  Keep this call around if
    // it contains any informative diagnostics.
    PathDiagnosticLocation *ThisCallLocation;
    if (Call->callEnterWithin.asLocation().isValid() &&
        !hasImplicitBody(Call->getCallee()))
      ThisCallLocation = &Call->callEnterWithin;
    else
      ThisCallLocation = &Call->callEnter;

    assert(ThisCallLocation && "Outermost call has an invalid location");
    adjustCallLocations(Call->path, ThisCallLocation);
  }
}

/// Remove edges in and out of C++ default initializer expressions. These are
/// for fields that have in-class initializers, as opposed to being initialized
/// explicitly in a constructor or braced list.
static void removeEdgesToDefaultInitializers(PathPieces &Pieces) {
  for (PathPieces::iterator I = Pieces.begin(), E = Pieces.end(); I != E;) {
    if (auto *C = dyn_cast<PathDiagnosticCallPiece>(I->get()))
      removeEdgesToDefaultInitializers(C->path);

    if (auto *M = dyn_cast<PathDiagnosticMacroPiece>(I->get()))
      removeEdgesToDefaultInitializers(M->subPieces);

    if (auto *CF = dyn_cast<PathDiagnosticControlFlowPiece>(I->get())) {
      const Stmt *Start = CF->getStartLocation().asStmt();
      const Stmt *End = CF->getEndLocation().asStmt();
      if (isa_and_nonnull<CXXDefaultInitExpr>(Start)) {
        I = Pieces.erase(I);
        continue;
      } else if (isa_and_nonnull<CXXDefaultInitExpr>(End)) {
        PathPieces::iterator Next = std::next(I);
        if (Next != E) {
          if (auto *NextCF =
                  dyn_cast<PathDiagnosticControlFlowPiece>(Next->get())) {
            NextCF->setStartLocation(CF->getStartLocation());
          }
        }
        I = Pieces.erase(I);
        continue;
      }
    }

    I++;
  }
}

/// Remove all pieces with invalid locations as these cannot be serialized.
/// We might have pieces with invalid locations as a result of inlining Body
/// Farm generated functions.
static void removePiecesWithInvalidLocations(PathPieces &Pieces) {
  for (PathPieces::iterator I = Pieces.begin(), E = Pieces.end(); I != E;) {
    if (auto *C = dyn_cast<PathDiagnosticCallPiece>(I->get()))
      removePiecesWithInvalidLocations(C->path);

    if (auto *M = dyn_cast<PathDiagnosticMacroPiece>(I->get()))
      removePiecesWithInvalidLocations(M->subPieces);

    if (!(*I)->getLocation().isValid() ||
        !(*I)->getLocation().asLocation().isValid()) {
      I = Pieces.erase(I);
      continue;
    }
    I++;
  }
}

PathDiagnosticLocation PathDiagnosticBuilder::ExecutionContinues(
    const PathDiagnosticConstruct &C) const {
  if (const Stmt *S = C.getCurrentNode()->getNextStmtForDiagnostics())
    return PathDiagnosticLocation(S, getSourceManager(),
                                  C.getCurrLocationContext());

  return PathDiagnosticLocation::createDeclEnd(C.getCurrLocationContext(),
                                               getSourceManager());
}

PathDiagnosticLocation PathDiagnosticBuilder::ExecutionContinues(
    llvm::raw_string_ostream &os, const PathDiagnosticConstruct &C) const {
  // Slow, but probably doesn't matter.
  if (os.str().empty())
    os << ' ';

  const PathDiagnosticLocation &Loc = ExecutionContinues(C);

  if (Loc.asStmt())
    os << "Execution continues on line "
       << getSourceManager().getExpansionLineNumber(Loc.asLocation())
       << '.';
  else {
    os << "Execution jumps to the end of the ";
    const Decl *D = C.getCurrLocationContext()->getDecl();
    if (isa<ObjCMethodDecl>(D))
      os << "method";
    else if (isa<FunctionDecl>(D))
      os << "function";
    else {
      assert(isa<BlockDecl>(D));
      os << "anonymous block";
    }
    os << '.';
  }

  return Loc;
}

static const Stmt *getEnclosingParent(const Stmt *S, const ParentMap &PM) {
  if (isa<Expr>(S) && PM.isConsumedExpr(cast<Expr>(S)))
    return PM.getParentIgnoreParens(S);

  const Stmt *Parent = PM.getParentIgnoreParens(S);
  if (!Parent)
    return nullptr;

  switch (Parent->getStmtClass()) {
  case Stmt::ForStmtClass:
  case Stmt::DoStmtClass:
  case Stmt::WhileStmtClass:
  case Stmt::ObjCForCollectionStmtClass:
  case Stmt::CXXForRangeStmtClass:
    return Parent;
  default:
    break;
  }

  return nullptr;
}

static PathDiagnosticLocation
getEnclosingStmtLocation(const Stmt *S, const LocationContext *LC,
                         bool allowNestedContexts = false) {
  if (!S)
    return {};

  const SourceManager &SMgr = LC->getDecl()->getASTContext().getSourceManager();

  while (const Stmt *Parent = getEnclosingParent(S, LC->getParentMap())) {
    switch (Parent->getStmtClass()) {
      case Stmt::BinaryOperatorClass: {
        const auto *B = cast<BinaryOperator>(Parent);
        if (B->isLogicalOp())
          return PathDiagnosticLocation(allowNestedContexts ? B : S, SMgr, LC);
        break;
      }
      case Stmt::CompoundStmtClass:
      case Stmt::StmtExprClass:
        return PathDiagnosticLocation(S, SMgr, LC);
      case Stmt::ChooseExprClass:
        // Similar to '?' if we are referring to condition, just have the edge
        // point to the entire choose expression.
        if (allowNestedContexts || cast<ChooseExpr>(Parent)->getCond() == S)
          return PathDiagnosticLocation(Parent, SMgr, LC);
        else
          return PathDiagnosticLocation(S, SMgr, LC);
      case Stmt::BinaryConditionalOperatorClass:
      case Stmt::ConditionalOperatorClass:
        // For '?', if we are referring to condition, just have the edge point
        // to the entire '?' expression.
        if (allowNestedContexts ||
            cast<AbstractConditionalOperator>(Parent)->getCond() == S)
          return PathDiagnosticLocation(Parent, SMgr, LC);
        else
          return PathDiagnosticLocation(S, SMgr, LC);
      case Stmt::CXXForRangeStmtClass:
        if (cast<CXXForRangeStmt>(Parent)->getBody() == S)
          return PathDiagnosticLocation(S, SMgr, LC);
        break;
      case Stmt::DoStmtClass:
          return PathDiagnosticLocation(S, SMgr, LC);
      case Stmt::ForStmtClass:
        if (cast<ForStmt>(Parent)->getBody() == S)
          return PathDiagnosticLocation(S, SMgr, LC);
        break;
      case Stmt::IfStmtClass:
        if (cast<IfStmt>(Parent)->getCond() != S)
          return PathDiagnosticLocation(S, SMgr, LC);
        break;
      case Stmt::ObjCForCollectionStmtClass:
        if (cast<ObjCForCollectionStmt>(Parent)->getBody() == S)
          return PathDiagnosticLocation(S, SMgr, LC);
        break;
      case Stmt::WhileStmtClass:
        if (cast<WhileStmt>(Parent)->getCond() != S)
          return PathDiagnosticLocation(S, SMgr, LC);
        break;
      default:
        break;
    }

    S = Parent;
  }

  assert(S && "Cannot have null Stmt for PathDiagnosticLocation");

  return PathDiagnosticLocation(S, SMgr, LC);
}

//===----------------------------------------------------------------------===//
// "Minimal" path diagnostic generation algorithm.
//===----------------------------------------------------------------------===//

/// If the piece contains a special message, add it to all the call pieces on
/// the active stack. For example, my_malloc allocated memory, so MallocChecker
/// will construct an event at the call to malloc(), and add a stack hint that
/// an allocated memory was returned. We'll use this hint to construct a message
/// when returning from the call to my_malloc
///
///   void *my_malloc() { return malloc(sizeof(int)); }
///   void fishy() {
///     void *ptr = my_malloc(); // returned allocated memory
///   } // leak
void PathDiagnosticBuilder::updateStackPiecesWithMessage(
    PathDiagnosticPieceRef P, const CallWithEntryStack &CallStack) const {
  if (R->hasCallStackHint(P))
    for (const auto &I : CallStack) {
      PathDiagnosticCallPiece *CP = I.first;
      const ExplodedNode *N = I.second;
      std::string stackMsg = R->getCallStackMessage(P, N);

      // The last message on the path to final bug is the most important
      // one. Since we traverse the path backwards, do not add the message
      // if one has been previously added.
      if (!CP->hasCallStackMessage())
        CP->setCallStackMessage(stackMsg);
    }
}

static void CompactMacroExpandedPieces(PathPieces &path,
                                       const SourceManager& SM);

PathDiagnosticPieceRef PathDiagnosticBuilder::generateDiagForSwitchOP(
    const PathDiagnosticConstruct &C, const CFGBlock *Dst,
    PathDiagnosticLocation &Start) const {

  const SourceManager &SM = getSourceManager();
  // Figure out what case arm we took.
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  PathDiagnosticLocation End;

  if (const Stmt *S = Dst->getLabel()) {
    End = PathDiagnosticLocation(S, SM, C.getCurrLocationContext());

    switch (S->getStmtClass()) {
    default:
      os << "No cases match in the switch statement. "
        "Control jumps to line "
        << End.asLocation().getExpansionLineNumber();
      break;
    case Stmt::DefaultStmtClass:
      os << "Control jumps to the 'default' case at line "
        << End.asLocation().getExpansionLineNumber();
      break;

    case Stmt::CaseStmtClass: {
      os << "Control jumps to 'case ";
      const auto *Case = cast<CaseStmt>(S);
      const Expr *LHS = Case->getLHS()->IgnoreParenImpCasts();

      // Determine if it is an enum.
      bool GetRawInt = true;

      if (const auto *DR = dyn_cast<DeclRefExpr>(LHS)) {
        // FIXME: Maybe this should be an assertion.  Are there cases
        // were it is not an EnumConstantDecl?
        const auto *D = dyn_cast<EnumConstantDecl>(DR->getDecl());

        if (D) {
          GetRawInt = false;
          os << *D;
        }
      }

      if (GetRawInt)
        os << LHS->EvaluateKnownConstInt(getASTContext());

      os << ":'  at line " << End.asLocation().getExpansionLineNumber();
      break;
    }
    }
  } else {
    os << "'Default' branch taken. ";
    End = ExecutionContinues(os, C);
  }
  return std::make_shared<PathDiagnosticControlFlowPiece>(Start, End,
                                                       os.str());
}

PathDiagnosticPieceRef PathDiagnosticBuilder::generateDiagForGotoOP(
    const PathDiagnosticConstruct &C, const Stmt *S,
    PathDiagnosticLocation &Start) const {
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  const PathDiagnosticLocation &End =
      getEnclosingStmtLocation(S, C.getCurrLocationContext());
  os << "Control jumps to line " << End.asLocation().getExpansionLineNumber();
  return std::make_shared<PathDiagnosticControlFlowPiece>(Start, End, os.str());
}

PathDiagnosticPieceRef PathDiagnosticBuilder::generateDiagForBinaryOP(
    const PathDiagnosticConstruct &C, const Stmt *T, const CFGBlock *Src,
    const CFGBlock *Dst) const {

  const SourceManager &SM = getSourceManager();

  const auto *B = cast<BinaryOperator>(T);
  std::string sbuf;
  llvm::raw_string_ostream os(sbuf);
  os << "Left side of '";
  PathDiagnosticLocation Start, End;

  if (B->getOpcode() == BO_LAnd) {
    os << "&&"
      << "' is ";

    if (*(Src->succ_begin() + 1) == Dst) {
      os << "false";
      End = PathDiagnosticLocation(B->getLHS(), SM, C.getCurrLocationContext());
      Start =
        PathDiagnosticLocation::createOperatorLoc(B, SM);
    } else {
      os << "true";
      Start =
          PathDiagnosticLocation(B->getLHS(), SM, C.getCurrLocationContext());
      End = ExecutionContinues(C);
    }
  } else {
    assert(B->getOpcode() == BO_LOr);
    os << "||"
      << "' is ";

    if (*(Src->succ_begin() + 1) == Dst) {
      os << "false";
      Start =
          PathDiagnosticLocation(B->getLHS(), SM, C.getCurrLocationContext());
      End = ExecutionContinues(C);
    } else {
      os << "true";
      End = PathDiagnosticLocation(B->getLHS(), SM, C.getCurrLocationContext());
      Start =
        PathDiagnosticLocation::createOperatorLoc(B, SM);
    }
  }
  return std::make_shared<PathDiagnosticControlFlowPiece>(Start, End,
                                                         os.str());
}

void PathDiagnosticBuilder::generateMinimalDiagForBlockEdge(
    PathDiagnosticConstruct &C, BlockEdge BE) const {
  const SourceManager &SM = getSourceManager();
  const LocationContext *LC = C.getCurrLocationContext();
  const CFGBlock *Src = BE.getSrc();
  const CFGBlock *Dst = BE.getDst();
  const Stmt *T = Src->getTerminatorStmt();
  if (!T)
    return;

  auto Start = PathDiagnosticLocation::createBegin(T, SM, LC);
  switch (T->getStmtClass()) {
  default:
    break;

  case Stmt::GotoStmtClass:
  case Stmt::IndirectGotoStmtClass: {
    if (const Stmt *S = C.getCurrentNode()->getNextStmtForDiagnostics())
      C.getActivePath().push_front(generateDiagForGotoOP(C, S, Start));
    break;
  }

  case Stmt::SwitchStmtClass: {
    C.getActivePath().push_front(generateDiagForSwitchOP(C, Dst, Start));
    break;
  }

  case Stmt::BreakStmtClass:
  case Stmt::ContinueStmtClass: {
    std::string sbuf;
    llvm::raw_string_ostream os(sbuf);
    PathDiagnosticLocation End = ExecutionContinues(os, C);
    C.getActivePath().push_front(
        std::make_shared<PathDiagnosticControlFlowPiece>(Start, End, os.str()));
    break;
  }

  // Determine control-flow for ternary '?'.
  case Stmt::BinaryConditionalOperatorClass:
  case Stmt::ConditionalOperatorClass: {
    std::string sbuf;
    llvm::raw_string_ostream os(sbuf);
    os << "'?' condition is ";

    if (*(Src->succ_begin() + 1) == Dst)
      os << "false";
    else
      os << "true";

    PathDiagnosticLocation End = ExecutionContinues(C);

    if (const Stmt *S = End.asStmt())
      End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

    C.getActivePath().push_front(
        std::make_shared<PathDiagnosticControlFlowPiece>(Start, End, os.str()));
    break;
  }

  // Determine control-flow for short-circuited '&&' and '||'.
  case Stmt::BinaryOperatorClass: {
    if (!C.supportsLogicalOpControlFlow())
      break;

    C.getActivePath().push_front(generateDiagForBinaryOP(C, T, Src, Dst));
    break;
  }

  case Stmt::DoStmtClass:
    if (*(Src->succ_begin()) == Dst) {
      std::string sbuf;
      llvm::raw_string_ostream os(sbuf);

      os << "Loop condition is true. ";
      PathDiagnosticLocation End = ExecutionContinues(os, C);

      if (const Stmt *S = End.asStmt())
        End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(Start, End,
                                                           os.str()));
    } else {
      PathDiagnosticLocation End = ExecutionContinues(C);

      if (const Stmt *S = End.asStmt())
        End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(
              Start, End, "Loop condition is false.  Exiting loop"));
    }
    break;

  case Stmt::WhileStmtClass:
  case Stmt::ForStmtClass:
    if (*(Src->succ_begin() + 1) == Dst) {
      std::string sbuf;
      llvm::raw_string_ostream os(sbuf);

      os << "Loop condition is false. ";
      PathDiagnosticLocation End = ExecutionContinues(os, C);
      if (const Stmt *S = End.asStmt())
        End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(Start, End,
                                                           os.str()));
    } else {
      PathDiagnosticLocation End = ExecutionContinues(C);
      if (const Stmt *S = End.asStmt())
        End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(
              Start, End, "Loop condition is true.  Entering loop body"));
    }

    break;

  case Stmt::IfStmtClass: {
    PathDiagnosticLocation End = ExecutionContinues(C);

    if (const Stmt *S = End.asStmt())
      End = getEnclosingStmtLocation(S, C.getCurrLocationContext());

    if (*(Src->succ_begin() + 1) == Dst)
      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(
              Start, End, "Taking false branch"));
    else
      C.getActivePath().push_front(
          std::make_shared<PathDiagnosticControlFlowPiece>(
              Start, End, "Taking true branch"));

    break;
  }
  }
}

//===----------------------------------------------------------------------===//
// Functions for determining if a loop was executed 0 times.
//===----------------------------------------------------------------------===//

static bool isLoop(const Stmt *Term) {
  switch (Term->getStmtClass()) {
    case Stmt::ForStmtClass:
    case Stmt::WhileStmtClass:
    case Stmt::ObjCForCollectionStmtClass:
    case Stmt::CXXForRangeStmtClass:
      return true;
    default:
      // Note that we intentionally do not include do..while here.
      return false;
  }
}

static bool isJumpToFalseBranch(const BlockEdge *BE) {
  const CFGBlock *Src = BE->getSrc();
  assert(Src->succ_size() == 2);
  return (*(Src->succ_begin()+1) == BE->getDst());
}

static bool isContainedByStmt(const ParentMap &PM, const Stmt *S,
                              const Stmt *SubS) {
  while (SubS) {
    if (SubS == S)
      return true;
    SubS = PM.getParent(SubS);
  }
  return false;
}

static const Stmt *getStmtBeforeCond(const ParentMap &PM, const Stmt *Term,
                                     const ExplodedNode *N) {
  while (N) {
    std::optional<StmtPoint> SP = N->getLocation().getAs<StmtPoint>();
    if (SP) {
      const Stmt *S = SP->getStmt();
      if (!isContainedByStmt(PM, Term, S))
        return S;
    }
    N = N->getFirstPred();
  }
  return nullptr;
}

static bool isInLoopBody(const ParentMap &PM, const Stmt *S, const Stmt *Term) {
  const Stmt *LoopBody = nullptr;
  switch (Term->getStmtClass()) {
    case Stmt::CXXForRangeStmtClass: {
      const auto *FR = cast<CXXForRangeStmt>(Term);
      if (isContainedByStmt(PM, FR->getInc(), S))
        return true;
      if (isContainedByStmt(PM, FR->getLoopVarStmt(), S))
        return true;
      LoopBody = FR->getBody();
      break;
    }
    case Stmt::ForStmtClass: {
      const auto *FS = cast<ForStmt>(Term);
      if (isContainedByStmt(PM, FS->getInc(), S))
        return true;
      LoopBody = FS->getBody();
      break;
    }
    case Stmt::ObjCForCollectionStmtClass: {
      const auto *FC = cast<ObjCForCollectionStmt>(Term);
      LoopBody = FC->getBody();
      break;
    }
    case Stmt::WhileStmtClass:
      LoopBody = cast<WhileStmt>(Term)->getBody();
      break;
    default:
      return false;
  }
  return isContainedByStmt(PM, LoopBody, S);
}

/// Adds a sanitized control-flow diagnostic edge to a path.
static void addEdgeToPath(PathPieces &path,
                          PathDiagnosticLocation &PrevLoc,
                          PathDiagnosticLocation NewLoc) {
  if (!NewLoc.isValid())
    return;

  SourceLocation NewLocL = NewLoc.asLocation();
  if (NewLocL.isInvalid())
    return;

  if (!PrevLoc.isValid() || !PrevLoc.asLocation().isValid()) {
    PrevLoc = NewLoc;
    return;
  }

  // Ignore self-edges, which occur when there are multiple nodes at the same
  // statement.
  if (NewLoc.asStmt() && NewLoc.asStmt() == PrevLoc.asStmt())
    return;

  path.push_front(
      std::make_shared<PathDiagnosticControlFlowPiece>(NewLoc, PrevLoc));
  PrevLoc = NewLoc;
}

/// A customized wrapper for CFGBlock::getTerminatorCondition()
/// which returns the element for ObjCForCollectionStmts.
static const Stmt *getTerminatorCondition(const CFGBlock *B) {
  const Stmt *S = B->getTerminatorCondition();
  if (const auto *FS = dyn_cast_or_null<ObjCForCollectionStmt>(S))
    return FS->getElement();
  return S;
}

constexpr llvm::StringLiteral StrEnteringLoop = "Entering loop body";
constexpr llvm::StringLiteral StrLoopBodyZero = "Loop body executed 0 times";
constexpr llvm::StringLiteral StrLoopRangeEmpty =
    "Loop body skipped when range is empty";
constexpr llvm::StringLiteral StrLoopCollectionEmpty =
    "Loop body skipped when collection is empty";

static std::unique_ptr<FilesToLineNumsMap>
findExecutedLines(const SourceManager &SM, const ExplodedNode *N);

void PathDiagnosticBuilder::generatePathDiagnosticsForNode(
    PathDiagnosticConstruct &C, PathDiagnosticLocation &PrevLoc) const {
  ProgramPoint P = C.getCurrentNode()->getLocation();
  const SourceManager &SM = getSourceManager();

  // Have we encountered an entrance to a call?  It may be
  // the case that we have not encountered a matching
  // call exit before this point.  This means that the path
  // terminated within the call itself.
  if (auto CE = P.getAs<CallEnter>()) {

    if (C.shouldAddPathEdges()) {
      // Add an edge to the start of the function.
      const StackFrameContext *CalleeLC = CE->getCalleeContext();
      const Decl *D = CalleeLC->getDecl();
      // Add the edge only when the callee has body. We jump to the beginning
      // of the *declaration*, however we expect it to be followed by the
      // body. This isn't the case for autosynthesized property accessors in
      // Objective-C. No need for a similar extra check for CallExit points
      // because the exit edge comes from a statement (i.e. return),
      // not from declaration.
      if (D->hasBody())
        addEdgeToPath(C.getActivePath(), PrevLoc,
                      PathDiagnosticLocation::createBegin(D, SM));
    }

    // Did we visit an entire call?
    bool VisitedEntireCall = C.PD->isWithinCall();
    C.PD->popActivePath();

    PathDiagnosticCallPiece *Call;
    if (VisitedEntireCall) {
      Call = cast<PathDiagnosticCallPiece>(C.getActivePath().front().get());
    } else {
      // The path terminated within a nested location context, create a new
      // call piece to encapsulate the rest of the path pieces.
      const Decl *Caller = CE->getLocationContext()->getDecl();
      Call = PathDiagnosticCallPiece::construct(C.getActivePath(), Caller);
      assert(C.getActivePath().size() == 1 &&
             C.getActivePath().front().get() == Call);

      // Since we just transferred the path over to the call piece, reset the
      // mapping of the active path to the current location context.
      assert(C.isInLocCtxMap(&C.getActivePath()) &&
             "When we ascend to a previously unvisited call, the active path's "
             "address shouldn't change, but rather should be compacted into "
             "a single CallEvent!");
      C.updateLocCtxMap(&C.getActivePath(), C.getCurrLocationContext());

      // Record the location context mapping for the path within the call.
      assert(!C.isInLocCtxMap(&Call->path) &&
             "When we ascend to a previously unvisited call, this must be the "
             "first time we encounter the caller context!");
      C.updateLocCtxMap(&Call->path, CE->getCalleeContext());
    }
    Call->setCallee(*CE, SM);

    // Update the previous location in the active path.
    PrevLoc = Call->getLocation();

    if (!C.CallStack.empty()) {
      assert(C.CallStack.back().first == Call);
      C.CallStack.pop_back();
    }
    return;
  }

  assert(C.getCurrLocationContext() == C.getLocationContextForActivePath() &&
         "The current position in the bug path is out of sync with the "
         "location context associated with the active path!");

  // Have we encountered an exit from a function call?
  if (std::optional<CallExitEnd> CE = P.getAs<CallExitEnd>()) {

    // We are descending into a call (backwards).  Construct
    // a new call piece to contain the path pieces for that call.
    auto Call = PathDiagnosticCallPiece::construct(*CE, SM);
    // Record the mapping from call piece to LocationContext.
    assert(!C.isInLocCtxMap(&Call->path) &&
           "We just entered a call, this must've been the first time we "
           "encounter its context!");
    C.updateLocCtxMap(&Call->path, CE->getCalleeContext());

    if (C.shouldAddPathEdges()) {
      // Add the edge to the return site.
      addEdgeToPath(C.getActivePath(), PrevLoc, Call->callReturn);
      PrevLoc.invalidate();
    }

    auto *P = Call.get();
    C.getActivePath().push_front(std::move(Call));

    // Make the contents of the call the active path for now.
    C.PD->pushActivePath(&P->path);
    C.CallStack.push_back(CallWithEntry(P, C.getCurrentNode()));
    return;
  }

  if (auto PS = P.getAs<PostStmt>()) {
    if (!C.shouldAddPathEdges())
      return;

    // Add an edge.  If this is an ObjCForCollectionStmt do
    // not add an edge here as it appears in the CFG both
    // as a terminator and as a terminator condition.
    if (!isa<ObjCForCollectionStmt>(PS->getStmt())) {
      PathDiagnosticLocation L =
          PathDiagnosticLocation(PS->getStmt(), SM, C.getCurrLocationContext());
      addEdgeToPath(C.getActivePath(), PrevLoc, L);
    }

  } else if (auto BE = P.getAs<BlockEdge>()) {

    if (C.shouldAddControlNotes()) {
      generateMinimalDiagForBlockEdge(C, *BE);
    }

    if (!C.shouldAddPathEdges()) {
      return;
    }

    // Are we jumping to the head of a loop?  Add a special diagnostic.
    if (const Stmt *Loop = BE->getSrc()->getLoopTarget()) {
      PathDiagnosticLocation L(Loop, SM, C.getCurrLocationContext());
      const Stmt *Body = nullptr;

      if (const auto *FS = dyn_cast<ForStmt>(Loop))
        Body = FS->getBody();
      else if (const auto *WS = dyn_cast<WhileStmt>(Loop))
        Body = WS->getBody();
      else if (const auto *OFS = dyn_cast<ObjCForCollectionStmt>(Loop)) {
        Body = OFS->getBody();
      } else if (const auto *FRS = dyn_cast<CXXForRangeStmt>(Loop)) {
        Body = FRS->getBody();
      }
      // do-while statements are explicitly excluded here

      auto p = std::make_shared<PathDiagnosticEventPiece>(
          L, "Looping back to the head of the loop");
      p->setPrunable(true);

      addEdgeToPath(C.getActivePath(), PrevLoc, p->getLocation());
      // We might've added a very similar control node already
      if (!C.shouldAddControlNotes()) {
        C.getActivePath().push_front(std::move(p));
      }

      if (const auto *CS = dyn_cast_or_null<CompoundStmt>(Body)) {
        addEdgeToPath(C.getActivePath(), PrevLoc,
                      PathDiagnosticLocation::createEndBrace(CS, SM));
      }
    }

    const CFGBlock *BSrc = BE->getSrc();
    const ParentMap &PM = C.getParentMap();

    if (const Stmt *Term = BSrc->getTerminatorStmt()) {
      // Are we jumping past the loop body without ever executing the
      // loop (because the condition was false)?
      if (isLoop(Term)) {
        const Stmt *TermCond = getTerminatorCondition(BSrc);
        bool IsInLoopBody = isInLoopBody(
            PM, getStmtBeforeCond(PM, TermCond, C.getCurrentNode()), Term);

        StringRef str;

        if (isJumpToFalseBranch(&*BE)) {
          if (!IsInLoopBody) {
            if (isa<ObjCForCollectionStmt>(Term)) {
              str = StrLoopCollectionEmpty;
            } else if (isa<CXXForRangeStmt>(Term)) {
              str = StrLoopRangeEmpty;
            } else {
              str = StrLoopBodyZero;
            }
          }
        } else {
          str = StrEnteringLoop;
        }

        if (!str.empty()) {
          PathDiagnosticLocation L(TermCond ? TermCond : Term, SM,
                                   C.getCurrLocationContext());
          auto PE = std::make_shared<PathDiagnosticEventPiece>(L, str);
          PE->setPrunable(true);
          addEdgeToPath(C.getActivePath(), PrevLoc, PE->getLocation());

          // We might've added a very similar control node already
          if (!C.shouldAddControlNotes()) {
            C.getActivePath().push_front(std::move(PE));
          }
        }
      } else if (isa<BreakStmt, ContinueStmt, GotoStmt>(Term)) {
        PathDiagnosticLocation L(Term, SM, C.getCurrLocationContext());
        addEdgeToPath(C.getActivePath(), PrevLoc, L);
      }
    }
  }
}

static std::unique_ptr<PathDiagnostic>
generateDiagnosticForBasicReport(const BasicBugReport *R,
                                 const Decl *AnalysisEntryPoint) {
  const BugType &BT = R->getBugType();
  return std::make_unique<PathDiagnostic>(
      BT.getCheckerName(), R->getDeclWithIssue(), BT.getDescription(),
      R->getDescription(), R->getShortDescription(/*UseFallback=*/false),
      BT.getCategory(), R->getUniqueingLocation(), R->getUniqueingDecl(),
      AnalysisEntryPoint, std::make_unique<FilesToLineNumsMap>());
}

static std::unique_ptr<PathDiagnostic>
generateEmptyDiagnosticForReport(const PathSensitiveBugReport *R,
                                 const SourceManager &SM,
                                 const Decl *AnalysisEntryPoint) {
  const BugType &BT = R->getBugType();
  return std::make_unique<PathDiagnostic>(
      BT.getCheckerName(), R->getDeclWithIssue(), BT.getDescription(),
      R->getDescription(), R->getShortDescription(/*UseFallback=*/false),
      BT.getCategory(), R->getUniqueingLocation(), R->getUniqueingDecl(),
      AnalysisEntryPoint, findExecutedLines(SM, R->getErrorNode()));
}

static const Stmt *getStmtParent(const Stmt *S, const ParentMap &PM) {
  if (!S)
    return nullptr;

  while (true) {
    S = PM.getParentIgnoreParens(S);

    if (!S)
      break;

    if (isa<FullExpr, CXXBindTemporaryExpr, SubstNonTypeTemplateParmExpr>(S))
      continue;

    break;
  }

  return S;
}

static bool isConditionForTerminator(const Stmt *S, const Stmt *Cond) {
  switch (S->getStmtClass()) {
    case Stmt::BinaryOperatorClass: {
      const auto *BO = cast<BinaryOperator>(S);
      if (!BO->isLogicalOp())
        return false;
      return BO->getLHS() == Cond || BO->getRHS() == Cond;
    }
    case Stmt::IfStmtClass:
      return cast<IfStmt>(S)->getCond() == Cond;
    case Stmt::ForStmtClass:
      return cast<ForStmt>(S)->getCond() == Cond;
    case Stmt::WhileStmtClass:
      return cast<WhileStmt>(S)->getCond() == Cond;
    case Stmt::DoStmtClass:
      return cast<DoStmt>(S)->getCond() == Cond;
    case Stmt::ChooseExprClass:
      return cast<ChooseExpr>(S)->getCond() == Cond;
    case Stmt::IndirectGotoStmtClass:
      return cast<IndirectGotoStmt>(S)->getTarget() == Cond;
    case Stmt::SwitchStmtClass:
      return cast<SwitchStmt>(S)->getCond() == Cond;
    case Stmt::BinaryConditionalOperatorClass:
      return cast<BinaryConditionalOperator>(S)->getCond() == Cond;
    case Stmt::ConditionalOperatorClass: {
      const auto *CO = cast<ConditionalOperator>(S);
      return CO->getCond() == Cond ||
             CO->getLHS() == Cond ||
             CO->getRHS() == Cond;
    }
    case Stmt::ObjCForCollectionStmtClass:
      return cast<ObjCForCollectionStmt>(S)->getElement() == Cond;
    case Stmt::CXXForRangeStmtClass: {
      const auto *FRS = cast<CXXForRangeStmt>(S);
      return FRS->getCond() == Cond || FRS->getRangeInit() == Cond;
    }
    default:
      return false;
  }
}

static bool isIncrementOrInitInForLoop(const Stmt *S, const Stmt *FL) {
  if (const auto *FS = dyn_cast<ForStmt>(FL))
    return FS->getInc() == S || FS->getInit() == S;
  if (const auto *FRS = dyn_cast<CXXForRangeStmt>(FL))
    return FRS->getInc() == S || FRS->getRangeStmt() == S ||
           FRS->getLoopVarStmt() || FRS->getRangeInit() == S;
  return false;
}

using OptimizedCallsSet = llvm::DenseSet<const PathDiagnosticCallPiece *>;

/// Adds synthetic edges from top-level statements to their subexpressions.
///
/// This avoids a "swoosh" effect, where an edge from a top-level statement A
/// points to a sub-expression B.1 that's not at the start of B. In these cases,
/// we'd like to see an edge from A to B, then another one from B to B.1.
static void addContextEdges(PathPieces &pieces, const LocationContext *LC) {
  const ParentMap &PM = LC->getParentMap();
  PathPieces::iterator Prev = pieces.end();
  for (PathPieces::iterator I = pieces.begin(), E = Prev; I != E;
       Prev = I, ++I) {
    auto *Piece = dyn_cast<PathDiagnosticControlFlowPiece>(I->get());

    if (!Piece)
      continue;

    PathDiagnosticLocation SrcLoc = Piece->getStartLocation();
    SmallVector<PathDiagnosticLocation, 4> SrcContexts;

    PathDiagnosticLocation NextSrcContext = SrcLoc;
    const Stmt *InnerStmt = nullptr;
    while (NextSrcContext.isValid() && NextSrcContext.asStmt() != InnerStmt) {
      SrcContexts.push_back(NextSrcContext);
      InnerStmt = NextSrcContext.asStmt();
      NextSrcContext = getEnclosingStmtLocation(InnerStmt, LC,
                                                /*allowNested=*/true);
    }

    // Repeatedly split the edge as necessary.
    // This is important for nested logical expressions (||, &&, ?:) where we
    // want to show all the levels of context.
    while (true) {
      const Stmt *Dst = Piece->getEndLocation().getStmtOrNull();

      // We are looking at an edge. Is the destination within a larger
      // expression?
      PathDiagnosticLocation DstContext =
          getEnclosingStmtLocation(Dst, LC, /*allowNested=*/true);
      if (!DstContext.isValid() || DstContext.asStmt() == Dst)
        break;

      // If the source is in the same context, we're already good.
      if (llvm::is_contained(SrcContexts, DstContext))
        break;

      // Update the subexpression node to point to the context edge.
      Piece->setStartLocation(DstContext);

      // Try to extend the previous edge if it's at the same level as the source
      // context.
      if (Prev != E) {
        auto *PrevPiece = dyn_cast<PathDiagnosticControlFlowPiece>(Prev->get());

        if (PrevPiece) {
          if (const Stmt *PrevSrc =
                  PrevPiece->getStartLocation().getStmtOrNull()) {
            const Stmt *PrevSrcParent = getStmtParent(PrevSrc, PM);
            if (PrevSrcParent ==
                getStmtParent(DstContext.getStmtOrNull(), PM)) {
              PrevPiece->setEndLocation(DstContext);
              break;
            }
          }
        }
      }

      // Otherwise, split the current edge into a context edge and a
      // subexpression edge. Note that the context statement may itself have
      // context.
      auto P =
          std::make_shared<PathDiagnosticControlFlowPiece>(SrcLoc, DstContext);
      Piece = P.get();
      I = pieces.insert(I, std::move(P));
    }
  }
}

/// Move edges from a branch condition to a branch target
///        when the condition is simple.
///
/// This restructures some of the work of addContextEdges.  That function
/// creates edges this may destroy, but they work together to create a more
/// aesthetically set of edges around branches.  After the call to
/// addContextEdges, we may have (1) an edge to the branch, (2) an edge from
/// the branch to the branch condition, and (3) an edge from the branch
/// condition to the branch target.  We keep (1), but may wish to remove (2)
/// and move the source of (3) to the branch if the branch condition is simple.
static void simplifySimpleBranches(PathPieces &pieces) {
  for (PathPieces::iterator I = pieces.begin(), E = pieces.end(); I != E; ++I) {
    const auto *PieceI = dyn_cast<PathDiagnosticControlFlowPiece>(I->get());

    if (!PieceI)
      continue;

    const Stmt *s1Start = PieceI->getStartLocation().getStmtOrNull();
    const Stmt *s1End   = PieceI->getEndLocation().getStmtOrNull();

    if (!s1Start || !s1End)
      continue;

    PathPieces::iterator NextI = I; ++NextI;
    if (NextI == E)
      break;

    PathDiagnosticControlFlowPiece *PieceNextI = nullptr;

    while (true) {
      if (NextI == E)
        break;

      const auto *EV = dyn_cast<PathDiagnosticEventPiece>(NextI->get());
      if (EV) {
        StringRef S = EV->getString();
        if (S == StrEnteringLoop || S == StrLoopBodyZero ||
            S == StrLoopCollectionEmpty || S == StrLoopRangeEmpty) {
          ++NextI;
          continue;
        }
        break;
      }

      PieceNextI = dyn_cast<PathDiagnosticControlFlowPiece>(NextI->get());
      break;
    }

    if (!PieceNextI)
      continue;

    const Stmt *s2Start = PieceNextI->getStartLocation().getStmtOrNull();
    const Stmt *s2End   = PieceNextI->getEndLocation().getStmtOrNull();

    if (!s2Start || !s2End || s1End != s2Start)
      continue;

    // We only perform this transformation for specific branch kinds.
    // We don't want to do this for do..while, for example.
    if (!isa<ForStmt, WhileStmt, IfStmt, ObjCForCollectionStmt,
             CXXForRangeStmt>(s1Start))
      continue;

    // Is s1End the branch condition?
    if (!isConditionForTerminator(s1Start, s1End))
      continue;

    // Perform the hoisting by eliminating (2) and changing the start
    // location of (3).
    PieceNextI->setStartLocation(PieceI->getStartLocation());
    I = pieces.erase(I);
  }
}

/// Returns the number of bytes in the given (character-based) SourceRange.
///
/// If the locations in the range are not on the same line, returns
/// std::nullopt.
///
/// Note that this does not do a precise user-visible character or column count.
static std::optional<size_t> getLengthOnSingleLine(const SourceManager &SM,
                                                   SourceRange Range) {
  SourceRange ExpansionRange(SM.getExpansionLoc(Range.getBegin()),
                             SM.getExpansionRange(Range.getEnd()).getEnd());

  FileID FID = SM.getFileID(ExpansionRange.getBegin());
  if (FID != SM.getFileID(ExpansionRange.getEnd()))
    return std::nullopt;

  std::optional<MemoryBufferRef> Buffer = SM.getBufferOrNone(FID);
  if (!Buffer)
    return std::nullopt;

  unsigned BeginOffset = SM.getFileOffset(ExpansionRange.getBegin());
  unsigned EndOffset = SM.getFileOffset(ExpansionRange.getEnd());
  StringRef Snippet = Buffer->getBuffer().slice(BeginOffset, EndOffset);

  // We're searching the raw bytes of the buffer here, which might include
  // escaped newlines and such. That's okay; we're trying to decide whether the
  // SourceRange is covering a large or small amount of space in the user's
  // editor.
  if (Snippet.find_first_of("\r\n") != StringRef::npos)
    return std::nullopt;

  // This isn't Unicode-aware, but it doesn't need to be.
  return Snippet.size();
}

/// \sa getLengthOnSingleLine(SourceManager, SourceRange)
static std::optional<size_t> getLengthOnSingleLine(const SourceManager &SM,
                                                   const Stmt *S) {
  return getLengthOnSingleLine(SM, S->getSourceRange());
}

/// Eliminate two-edge cycles created by addContextEdges().
///
/// Once all the context edges are in place, there are plenty of cases where
/// there's a single edge from a top-level statement to a subexpression,
/// followed by a single path note, and then a reverse edge to get back out to
/// the top level. If the statement is simple enough, the subexpression edges
/// just add noise and make it harder to understand what's going on.
///
/// This function only removes edges in pairs, because removing only one edge
/// might leave other edges dangling.
///
/// This will not remove edges in more complicated situations:
/// - if there is more than one "hop" leading to or from a subexpression.
/// - if there is an inlined call between the edges instead of a single event.
/// - if the whole statement is large enough that having subexpression arrows
///   might be helpful.
static void removeContextCycles(PathPieces &Path, const SourceManager &SM) {
  for (PathPieces::iterator I = Path.begin(), E = Path.end(); I != E; ) {
    // Pattern match the current piece and its successor.
    const auto *PieceI = dyn_cast<PathDiagnosticControlFlowPiece>(I->get());

    if (!PieceI) {
      ++I;
      continue;
    }

    const Stmt *s1Start = PieceI->getStartLocation().getStmtOrNull();
    const Stmt *s1End   = PieceI->getEndLocation().getStmtOrNull();

    PathPieces::iterator NextI = I; ++NextI;
    if (NextI == E)
      break;

    const auto *PieceNextI =
        dyn_cast<PathDiagnosticControlFlowPiece>(NextI->get());

    if (!PieceNextI) {
      if (isa<PathDiagnosticEventPiece>(NextI->get())) {
        ++NextI;
        if (NextI == E)
          break;
        PieceNextI = dyn_cast<PathDiagnosticControlFlowPiece>(NextI->get());
      }

      if (!PieceNextI) {
        ++I;
        continue;
      }
    }

    const Stmt *s2Start = PieceNextI->getStartLocation().getStmtOrNull();
    const Stmt *s2End   = PieceNextI->getEndLocation().getStmtOrNull();

    if (s1Start && s2Start && s1Start == s2End && s2Start == s1End) {
      const size_t MAX_SHORT_LINE_LENGTH = 80;
      std::optional<size_t> s1Length = getLengthOnSingleLine(SM, s1Start);
      if (s1Length && *s1Length <= MAX_SHORT_LINE_LENGTH) {
        std::optional<size_t> s2Length = getLengthOnSingleLine(SM, s2Start);
        if (s2Length && *s2Length <= MAX_SHORT_LINE_LENGTH) {
          Path.erase(I);
          I = Path.erase(NextI);
          continue;
        }
      }
    }

    ++I;
  }
}

/// Return true if X is contained by Y.
static bool lexicalContains(const ParentMap &PM, const Stmt *X, const Stmt *Y) {
  while (X) {
    if (X == Y)
      return true;
    X = PM.getParent(X);
  }
  return false;
}

// Remove short edges on the same line less than 3 columns in difference.
static void removePunyEdges(PathPieces &path, const SourceManager &SM,
                            const ParentMap &PM) {
  bool erased = false;

  for (PathPieces::iterator I = path.begin(), E = path.end(); I != E;
       erased ? I : ++I) {
    erased = false;

    const auto *PieceI = dyn_cast<PathDiagnosticControlFlowPiece>(I->get());

    if (!PieceI)
      continue;

    const Stmt *start = PieceI->getStartLocation().getStmtOrNull();
    const Stmt *end   = PieceI->getEndLocation().getStmtOrNull();

    if (!start || !end)
      continue;

    const Stmt *endParent = PM.getParent(end);
    if (!endParent)
      continue;

    if (isConditionForTerminator(end, endParent))
      continue;

    SourceLocation FirstLoc = start->getBeginLoc();
    SourceLocation SecondLoc = end->getBeginLoc();

    if (!SM.isWrittenInSameFile(FirstLoc, SecondLoc))
      continue;
    if (SM.isBeforeInTranslationUnit(SecondLoc, FirstLoc))
      std::swap(SecondLoc, FirstLoc);

    SourceRange EdgeRange(FirstLoc, SecondLoc);
    std::optional<size_t> ByteWidth = getLengthOnSingleLine(SM, EdgeRange);

    // If the statements are on different lines, continue.
    if (!ByteWidth)
      continue;

    const size_t MAX_PUNY_EDGE_LENGTH = 2;
    if (*ByteWidth <= MAX_PUNY_EDGE_LENGTH) {
      // FIXME: There are enough /bytes/ between the endpoints of the edge, but
      // there might not be enough /columns/. A proper user-visible column count
      // is probably too expensive, though.
      I = path.erase(I);
      erased = true;
      continue;
    }
  }
}

static void removeIdenticalEvents(PathPieces &path) {
  for (PathPieces::iterator I = path.begin(), E = path.end(); I != E; ++I) {
    const auto *PieceI = dyn_cast<PathDiagnosticEventPiece>(I->get());

    if (!PieceI)
      continue;

    PathPieces::iterator NextI = I; ++NextI;
    if (NextI == E)
      return;

    const auto *PieceNextI = dyn_cast<PathDiagnosticEventPiece>(NextI->get());

    if (!PieceNextI)
      continue;

    // Erase the second piece if it has the same exact message text.
    if (PieceI->getString() == PieceNextI->getString()) {
      path.erase(NextI);
    }
  }
}

static bool optimizeEdges(const PathDiagnosticConstruct &C, PathPieces &path,
                          OptimizedCallsSet &OCS) {
  bool hasChanges = false;
  const LocationContext *LC = C.getLocationContextFor(&path);
  assert(LC);
  const ParentMap &PM = LC->getParentMap();
  const SourceManager &SM = C.getSourceManager();

  for (PathPieces::iterator I = path.begin(), E = path.end(); I != E; ) {
    // Optimize subpaths.
    if (auto *CallI = dyn_cast<PathDiagnosticCallPiece>(I->get())) {
      // Record the fact that a call has been optimized so we only do the
      // effort once.
      if (!OCS.count(CallI)) {
        while (optimizeEdges(C, CallI->path, OCS)) {
        }
        OCS.insert(CallI);
      }
      ++I;
      continue;
    }

    // Pattern match the current piece and its successor.
    auto *PieceI = dyn_cast<PathDiagnosticControlFlowPiece>(I->get());

    if (!PieceI) {
      ++I;
      continue;
    }

    const Stmt *s1Start = PieceI->getStartLocation().getStmtOrNull();
    const Stmt *s1End   = PieceI->getEndLocation().getStmtOrNull();
    const Stmt *level1 = getStmtParent(s1Start, PM);
    const Stmt *level2 = getStmtParent(s1End, PM);

    PathPieces::iterator NextI = I; ++NextI;
    if (NextI == E)
      break;

    const auto *PieceNextI = dyn_cast<PathDiagnosticControlFlowPiece>(NextI->get());

    if (!PieceNextI) {
      ++I;
      continue;
    }

    const Stmt *s2Start = PieceNextI->getStartLocation().getStmtOrNull();
    const Stmt *s2End   = PieceNextI->getEndLocation().getStmtOrNull();
    const Stmt *level3 = getStmtParent(s2Start, PM);
    const Stmt *level4 = getStmtParent(s2End, PM);

    // Rule I.
    //
    // If we have two consecutive control edges whose end/begin locations
    // are at the same level (e.g. statements or top-level expressions within
    // a compound statement, or siblings share a single ancestor expression),
    // then merge them if they have no interesting intermediate event.
    //
    // For example:
    //
    // (1.1 -> 1.2) -> (1.2 -> 1.3) becomes (1.1 -> 1.3) because the common
    // parent is '1'.  Here 'x.y.z' represents the hierarchy of statements.
    //
    // NOTE: this will be limited later in cases where we add barriers
    // to prevent this optimization.
    if (level1 && level1 == level2 && level1 == level3 && level1 == level4) {
      PieceI->setEndLocation(PieceNextI->getEndLocation());
      path.erase(NextI);
      hasChanges = true;
      continue;
    }

    // Rule II.
    //
    // Eliminate edges between subexpressions and parent expressions
    // when the subexpression is consumed.
    //
    // NOTE: this will be limited later in cases where we add barriers
    // to prevent this optimization.
    if (s1End && s1End == s2Start && level2) {
      bool removeEdge = false;
      // Remove edges into the increment or initialization of a
      // loop that have no interleaving event.  This means that
      // they aren't interesting.
      if (isIncrementOrInitInForLoop(s1End, level2))
        removeEdge = true;
      // Next only consider edges that are not anchored on
      // the condition of a terminator.  This are intermediate edges
      // that we might want to trim.
      else if (!isConditionForTerminator(level2, s1End)) {
        // Trim edges on expressions that are consumed by
        // the parent expression.
        if (isa<Expr>(s1End) && PM.isConsumedExpr(cast<Expr>(s1End))) {
          removeEdge = true;
        }
        // Trim edges where a lexical containment doesn't exist.
        // For example:
        //
        //  X -> Y -> Z
        //
        // If 'Z' lexically contains Y (it is an ancestor) and
        // 'X' does not lexically contain Y (it is a descendant OR
        // it has no lexical relationship at all) then trim.
        //
        // This can eliminate edges where we dive into a subexpression
        // and then pop back out, etc.
        else if (s1Start && s2End &&
                 lexicalContains(PM, s2Start, s2End) &&
                 !lexicalContains(PM, s1End, s1Start)) {
          removeEdge = true;
        }
        // Trim edges from a subexpression back to the top level if the
        // subexpression is on a different line.
        //
        // A.1 -> A -> B
        // becomes
        // A.1 -> B
        //
        // These edges just look ugly and don't usually add anything.
        else if (s1Start && s2End &&
                 lexicalContains(PM, s1Start, s1End)) {
          SourceRange EdgeRange(PieceI->getEndLocation().asLocation(),
                                PieceI->getStartLocation().asLocation());
          if (!getLengthOnSingleLine(SM, EdgeRange))
            removeEdge = true;
        }
      }

      if (removeEdge) {
        PieceI->setEndLocation(PieceNextI->getEndLocation());
        path.erase(NextI);
        hasChanges = true;
        continue;
      }
    }

    // Optimize edges for ObjC fast-enumeration loops.
    //
    // (X -> collection) -> (collection -> element)
    //
    // becomes:
    //
    // (X -> element)
    if (s1End == s2Start) {
      const auto *FS = dyn_cast_or_null<ObjCForCollectionStmt>(level3);
      if (FS && FS->getCollection()->IgnoreParens() == s2Start &&
          s2End == FS->getElement()) {
        PieceI->setEndLocation(PieceNextI->getEndLocation());
        path.erase(NextI);
        hasChanges = true;
        continue;
      }
    }

    // No changes at this index?  Move to the next one.
    ++I;
  }

  if (!hasChanges) {
    // Adjust edges into subexpressions to make them more uniform
    // and aesthetically pleasing.
    addContextEdges(path, LC);
    // Remove "cyclical" edges that include one or more context edges.
    removeContextCycles(path, SM);
    // Hoist edges originating from branch conditions to branches
    // for simple branches.
    simplifySimpleBranches(path);
    // Remove any puny edges left over after primary optimization pass.
    removePunyEdges(path, SM, PM);
    // Remove identical events.
    removeIdenticalEvents(path);
  }

  return hasChanges;
}

/// Drop the very first edge in a path, which should be a function entry edge.
///
/// If the first edge is not a function entry edge (say, because the first
/// statement had an invalid source location), this function does nothing.
// FIXME: We should just generate invalid edges anyway and have the optimizer
// deal with them.
static void dropFunctionEntryEdge(const PathDiagnosticConstruct &C,
                                  PathPieces &Path) {
  const auto *FirstEdge =
      dyn_cast<PathDiagnosticControlFlowPiece>(Path.front().get());
  if (!FirstEdge)
    return;

  const Decl *D = C.getLocationContextFor(&Path)->getDecl();
  PathDiagnosticLocation EntryLoc =
      PathDiagnosticLocation::createBegin(D, C.getSourceManager());
  if (FirstEdge->getStartLocation() != EntryLoc)
    return;

  Path.pop_front();
}

/// Populate executes lines with lines containing at least one diagnostics.
static void updateExecutedLinesWithDiagnosticPieces(PathDiagnostic &PD) {

  PathPieces path = PD.path.flatten(/*ShouldFlattenMacros=*/true);
  FilesToLineNumsMap &ExecutedLines = PD.getExecutedLines();

  for (const auto &P : path) {
    FullSourceLoc Loc = P->getLocation().asLocation().getExpansionLoc();
    FileID FID = Loc.getFileID();
    unsigned LineNo = Loc.getLineNumber();
    assert(FID.isValid());
    ExecutedLines[FID].insert(LineNo);
  }
}

PathDiagnosticConstruct::PathDiagnosticConstruct(
    const PathDiagnosticConsumer *PDC, const ExplodedNode *ErrorNode,
    const PathSensitiveBugReport *R, const Decl *AnalysisEntryPoint)
    : Consumer(PDC), CurrentNode(ErrorNode),
      SM(CurrentNode->getCodeDecl().getASTContext().getSourceManager()),
      PD(generateEmptyDiagnosticForReport(R, getSourceManager(),
                                          AnalysisEntryPoint)) {
  LCM[&PD->getActivePath()] = ErrorNode->getLocationContext();
}

PathDiagnosticBuilder::PathDiagnosticBuilder(
    BugReporterContext BRC, std::unique_ptr<ExplodedGraph> BugPath,
    PathSensitiveBugReport *r, const ExplodedNode *ErrorNode,
    std::unique_ptr<VisitorsDiagnosticsTy> VisitorsDiagnostics)
    : BugReporterContext(BRC), BugPath(std::move(BugPath)), R(r),
      ErrorNode(ErrorNode),
      VisitorsDiagnostics(std::move(VisitorsDiagnostics)) {}

std::unique_ptr<PathDiagnostic>
PathDiagnosticBuilder::generate(const PathDiagnosticConsumer *PDC) const {
  const Decl *EntryPoint = getBugReporter().getAnalysisEntryPoint();
  PathDiagnosticConstruct Construct(PDC, ErrorNode, R, EntryPoint);

  const SourceManager &SM = getSourceManager();
  const AnalyzerOptions &Opts = getAnalyzerOptions();

  if (!PDC->shouldGenerateDiagnostics())
    return generateEmptyDiagnosticForReport(R, getSourceManager(), EntryPoint);

  // Construct the final (warning) event for the bug report.
  auto EndNotes = VisitorsDiagnostics->find(ErrorNode);
  PathDiagnosticPieceRef LastPiece;
  if (EndNotes != VisitorsDiagnostics->end()) {
    assert(!EndNotes->second.empty());
    LastPiece = EndNotes->second[0];
  } else {
    LastPiece = BugReporterVisitor::getDefaultEndPath(*this, ErrorNode,
                                                      *getBugReport());
  }
  Construct.PD->setEndOfPath(LastPiece);

  PathDiagnosticLocation PrevLoc = Construct.PD->getLocation();
  // From the error node to the root, ascend the bug path and construct the bug
  // report.
  while (Construct.ascendToPrevNode()) {
    generatePathDiagnosticsForNode(Construct, PrevLoc);

    auto VisitorNotes = VisitorsDiagnostics->find(Construct.getCurrentNode());
    if (VisitorNotes == VisitorsDiagnostics->end())
      continue;

    // This is a workaround due to inability to put shared PathDiagnosticPiece
    // into a FoldingSet.
    std::set<llvm::FoldingSetNodeID> DeduplicationSet;

    // Add pieces from custom visitors.
    for (const PathDiagnosticPieceRef &Note : VisitorNotes->second) {
      llvm::FoldingSetNodeID ID;
      Note->Profile(ID);
      if (!DeduplicationSet.insert(ID).second)
        continue;

      if (PDC->shouldAddPathEdges())
        addEdgeToPath(Construct.getActivePath(), PrevLoc, Note->getLocation());
      updateStackPiecesWithMessage(Note, Construct.CallStack);
      Construct.getActivePath().push_front(Note);
    }
  }

  if (PDC->shouldAddPathEdges()) {
    // Add an edge to the start of the function.
    // We'll prune it out later, but it helps make diagnostics more uniform.
    const StackFrameContext *CalleeLC =
        Construct.getLocationContextForActivePath()->getStackFrame();
    const Decl *D = CalleeLC->getDecl();
    addEdgeToPath(Construct.getActivePath(), PrevLoc,
                  PathDiagnosticLocation::createBegin(D, SM));
  }


  // Finally, prune the diagnostic path of uninteresting stuff.
  if (!Construct.PD->path.empty()) {
    if (R->shouldPrunePath() && Opts.ShouldPrunePaths) {
      bool stillHasNotes =
          removeUnneededCalls(Construct, Construct.getMutablePieces(), R);
      assert(stillHasNotes);
      (void)stillHasNotes;
    }

    // Remove pop-up notes if needed.
    if (!Opts.ShouldAddPopUpNotes)
      removePopUpNotes(Construct.getMutablePieces());

    // Redirect all call pieces to have valid locations.
    adjustCallLocations(Construct.getMutablePieces());
    removePiecesWithInvalidLocations(Construct.getMutablePieces());

    if (PDC->shouldAddPathEdges()) {

      // Reduce the number of edges from a very conservative set
      // to an aesthetically pleasing subset that conveys the
      // necessary information.
      OptimizedCallsSet OCS;
      while (optimizeEdges(Construct, Construct.getMutablePieces(), OCS)) {
      }

      // Drop the very first function-entry edge. It's not really necessary
      // for top-level functions.
      dropFunctionEntryEdge(Construct, Construct.getMutablePieces());
    }

    // Remove messages that are basically the same, and edges that may not
    // make sense.
    // We have to do this after edge optimization in the Extensive mode.
    removeRedundantMsgs(Construct.getMutablePieces());
    removeEdgesToDefaultInitializers(Construct.getMutablePieces());
  }

  if (Opts.ShouldDisplayMacroExpansions)
    CompactMacroExpandedPieces(Construct.getMutablePieces(), SM);

  return std::move(Construct.PD);
}

//===----------------------------------------------------------------------===//
// Methods for BugType and subclasses.
//===----------------------------------------------------------------------===//

void BugType::anchor() {}

//===----------------------------------------------------------------------===//
// Methods for BugReport and subclasses.
//===----------------------------------------------------------------------===//

LLVM_ATTRIBUTE_USED static bool
isDependency(const CheckerRegistryData &Registry, StringRef CheckerName) {
  for (const std::pair<StringRef, StringRef> &Pair : Registry.Dependencies) {
    if (Pair.second == CheckerName)
      return true;
  }
  return false;
}

LLVM_ATTRIBUTE_USED static bool isHidden(const CheckerRegistryData &Registry,
                                         StringRef CheckerName) {
  for (const CheckerInfo &Checker : Registry.Checkers) {
    if (Checker.FullName == CheckerName)
      return Checker.IsHidden;
  }
  llvm_unreachable(
      "Checker name not found in CheckerRegistry -- did you retrieve it "
      "correctly from CheckerManager::getCurrentCheckerName?");
}

PathSensitiveBugReport::PathSensitiveBugReport(
    const BugType &bt, StringRef shortDesc, StringRef desc,
    const ExplodedNode *errorNode, PathDiagnosticLocation LocationToUnique,
    const Decl *DeclToUnique)
    : BugReport(Kind::PathSensitive, bt, shortDesc, desc), ErrorNode(errorNode),
      ErrorNodeRange(getStmt() ? getStmt()->getSourceRange() : SourceRange()),
      UniqueingLocation(LocationToUnique), UniqueingDecl(DeclToUnique) {
  assert(!isDependency(ErrorNode->getState()
                           ->getAnalysisManager()
                           .getCheckerManager()
                           ->getCheckerRegistryData(),
                       bt.getCheckerName()) &&
         "Some checkers depend on this one! We don't allow dependency "
         "checkers to emit warnings, because checkers should depend on "
         "*modeling*, not *diagnostics*.");

  assert((bt.getCheckerName().starts_with("debug") ||
          !isHidden(ErrorNode->getState()
                        ->getAnalysisManager()
                        .getCheckerManager()
                        ->getCheckerRegistryData(),
                    bt.getCheckerName())) &&
         "Hidden checkers musn't emit diagnostics as they are by definition "
         "non-user facing!");
}

void PathSensitiveBugReport::addVisitor(
    std::unique_ptr<BugReporterVisitor> visitor) {
  if (!visitor)
    return;

  llvm::FoldingSetNodeID ID;
  visitor->Profile(ID);

  void *InsertPos = nullptr;
  if (CallbacksSet.FindNodeOrInsertPos(ID, InsertPos)) {
    return;
  }

  Callbacks.push_back(std::move(visitor));
}

void PathSensitiveBugReport::clearVisitors() {
  Callbacks.clear();
}

const Decl *PathSensitiveBugReport::getDeclWithIssue() const {
  const ExplodedNode *N = getErrorNode();
  if (!N)
    return nullptr;

  const LocationContext *LC = N->getLocationContext();
  return LC->getStackFrame()->getDecl();
}

void BasicBugReport::Profile(llvm::FoldingSetNodeID& hash) const {
  hash.AddInteger(static_cast<int>(getKind()));
  hash.AddPointer(&BT);
  hash.AddString(getShortDescription());
  assert(Location.isValid());
  Location.Profile(hash);

  for (SourceRange range : Ranges) {
    if (!range.isValid())
      continue;
    hash.Add(range.getBegin());
    hash.Add(range.getEnd());
  }
}

void PathSensitiveBugReport::Profile(llvm::FoldingSetNodeID &hash) const {
  hash.AddInteger(static_cast<int>(getKind()));
  hash.AddPointer(&BT);
  hash.AddString(getShortDescription());
  PathDiagnosticLocation UL = getUniqueingLocation();
  if (UL.isValid()) {
    UL.Profile(hash);
  } else {
    // TODO: The statement may be null if the report was emitted before any
    // statements were executed. In particular, some checkers by design
    // occasionally emit their reports in empty functions (that have no
    // statements in their body). Do we profile correctly in this case?
    hash.AddPointer(ErrorNode->getCurrentOrPreviousStmtForDiagnostics());
  }

  for (SourceRange range : Ranges) {
    if (!range.isValid())
      continue;
    hash.Add(range.getBegin());
    hash.Add(range.getEnd());
  }
}

template <class T>
static void insertToInterestingnessMap(
    llvm::DenseMap<T, bugreporter::TrackingKind> &InterestingnessMap, T Val,
    bugreporter::TrackingKind TKind) {
  auto Result = InterestingnessMap.insert({Val, TKind});

  if (Result.second)
    return;

  // Even if this symbol/region was already marked as interesting as a
  // condition, if we later mark it as interesting again but with
  // thorough tracking, overwrite it. Entities marked with thorough
  // interestiness are the most important (or most interesting, if you will),
  // and we wouldn't like to downplay their importance.

  switch (TKind) {
    case bugreporter::TrackingKind::Thorough:
      Result.first->getSecond() = bugreporter::TrackingKind::Thorough;
      return;
    case bugreporter::TrackingKind::Condition:
      return;
    }

    llvm_unreachable(
        "BugReport::markInteresting currently can only handle 2 different "
        "tracking kinds! Please define what tracking kind should this entitiy"
        "have, if it was already marked as interesting with a different kind!");
}

void PathSensitiveBugReport::markInteresting(SymbolRef sym,
                                             bugreporter::TrackingKind TKind) {
  if (!sym)
    return;

  insertToInterestingnessMap(InterestingSymbols, sym, TKind);

  // FIXME: No tests exist for this code and it is questionable:
  // How to handle multiple metadata for the same region?
  if (const auto *meta = dyn_cast<SymbolMetadata>(sym))
    markInteresting(meta->getRegion(), TKind);
}

void PathSensitiveBugReport::markNotInteresting(SymbolRef sym) {
  if (!sym)
    return;
  InterestingSymbols.erase(sym);

  // The metadata part of markInteresting is not reversed here.
  // Just making the same region not interesting is incorrect
  // in specific cases.
  if (const auto *meta = dyn_cast<SymbolMetadata>(sym))
    markNotInteresting(meta->getRegion());
}

void PathSensitiveBugReport::markInteresting(const MemRegion *R,
                                             bugreporter::TrackingKind TKind) {
  if (!R)
    return;

  R = R->getBaseRegion();
  insertToInterestingnessMap(InterestingRegions, R, TKind);

  if (const auto *SR = dyn_cast<SymbolicRegion>(R))
    markInteresting(SR->getSymbol(), TKind);
}

void PathSensitiveBugReport::markNotInteresting(const MemRegion *R) {
  if (!R)
    return;

  R = R->getBaseRegion();
  InterestingRegions.erase(R);

  if (const auto *SR = dyn_cast<SymbolicRegion>(R))
    markNotInteresting(SR->getSymbol());
}

void PathSensitiveBugReport::markInteresting(SVal V,
                                             bugreporter::TrackingKind TKind) {
  markInteresting(V.getAsRegion(), TKind);
  markInteresting(V.getAsSymbol(), TKind);
}

void PathSensitiveBugReport::markInteresting(const LocationContext *LC) {
  if (!LC)
    return;
  InterestingLocationContexts.insert(LC);
}

std::optional<bugreporter::TrackingKind>
PathSensitiveBugReport::getInterestingnessKind(SVal V) const {
  auto RKind = getInterestingnessKind(V.getAsRegion());
  auto SKind = getInterestingnessKind(V.getAsSymbol());
  if (!RKind)
    return SKind;
  if (!SKind)
    return RKind;

  // If either is marked with throrough tracking, return that, we wouldn't like
  // to downplay a note's importance by 'only' mentioning it as a condition.
  switch(*RKind) {
    case bugreporter::TrackingKind::Thorough:
      return RKind;
    case bugreporter::TrackingKind::Condition:
      return SKind;
  }

  llvm_unreachable(
      "BugReport::getInterestingnessKind currently can only handle 2 different "
      "tracking kinds! Please define what tracking kind should we return here "
      "when the kind of getAsRegion() and getAsSymbol() is different!");
  return std::nullopt;
}

std::optional<bugreporter::TrackingKind>
PathSensitiveBugReport::getInterestingnessKind(SymbolRef sym) const {
  if (!sym)
    return std::nullopt;
  // We don't currently consider metadata symbols to be interesting
  // even if we know their region is interesting. Is that correct behavior?
  auto It = InterestingSymbols.find(sym);
  if (It == InterestingSymbols.end())
    return std::nullopt;
  return It->getSecond();
}

std::optional<bugreporter::TrackingKind>
PathSensitiveBugReport::getInterestingnessKind(const MemRegion *R) const {
  if (!R)
    return std::nullopt;

  R = R->getBaseRegion();
  auto It = InterestingRegions.find(R);
  if (It != InterestingRegions.end())
    return It->getSecond();

  if (const auto *SR = dyn_cast<SymbolicRegion>(R))
    return getInterestingnessKind(SR->getSymbol());
  return std::nullopt;
}

bool PathSensitiveBugReport::isInteresting(SVal V) const {
  return getInterestingnessKind(V).has_value();
}

bool PathSensitiveBugReport::isInteresting(SymbolRef sym) const {
  return getInterestingnessKind(sym).has_value();
}

bool PathSensitiveBugReport::isInteresting(const MemRegion *R) const {
  return getInterestingnessKind(R).has_value();
}

bool PathSensitiveBugReport::isInteresting(const LocationContext *LC)  const {
  if (!LC)
    return false;
  return InterestingLocationContexts.count(LC);
}

const Stmt *PathSensitiveBugReport::getStmt() const {
  if (!ErrorNode)
    return nullptr;

  ProgramPoint ProgP = ErrorNode->getLocation();
  const Stmt *S = nullptr;

  if (std::optional<BlockEntrance> BE = ProgP.getAs<BlockEntrance>()) {
    CFGBlock &Exit = ProgP.getLocationContext()->getCFG()->getExit();
    if (BE->getBlock() == &Exit)
      S = ErrorNode->getPreviousStmtForDiagnostics();
  }
  if (!S)
    S = ErrorNode->getStmtForDiagnostics();

  return S;
}

ArrayRef<SourceRange>
PathSensitiveBugReport::getRanges() const {
  // If no custom ranges, add the range of the statement corresponding to
  // the error node.
  if (Ranges.empty() && isa_and_nonnull<Expr>(getStmt()))
      return ErrorNodeRange;

  return Ranges;
}

PathDiagnosticLocation
PathSensitiveBugReport::getLocation() const {
  assert(ErrorNode && "Cannot create a location with a null node.");
  const Stmt *S = ErrorNode->getStmtForDiagnostics();
    ProgramPoint P = ErrorNode->getLocation();
  const LocationContext *LC = P.getLocationContext();
  SourceManager &SM =
      ErrorNode->getState()->getStateManager().getContext().getSourceManager();

  if (!S) {
    // If this is an implicit call, return the implicit call point location.
      if (std::optional<PreImplicitCall> PIE = P.getAs<PreImplicitCall>())
      return PathDiagnosticLocation(PIE->getLocation(), SM);
    if (auto FE = P.getAs<FunctionExitPoint>()) {
      if (const ReturnStmt *RS = FE->getStmt())
        return PathDiagnosticLocation::createBegin(RS, SM, LC);
    }
    S = ErrorNode->getNextStmtForDiagnostics();
  }

  if (S) {
    // Attributed statements usually have corrupted begin locations,
    // it's OK to ignore attributes for our purposes and deal with
    // the actual annotated statement.
    if (const auto *AS = dyn_cast<AttributedStmt>(S))
      S = AS->getSubStmt();

    // For member expressions, return the location of the '.' or '->'.
    if (const auto *ME = dyn_cast<MemberExpr>(S))
      return PathDiagnosticLocation::createMemberLoc(ME, SM);

    // For binary operators, return the location of the operator.
    if (const auto *B = dyn_cast<BinaryOperator>(S))
      return PathDiagnosticLocation::createOperatorLoc(B, SM);

    if (P.getAs<PostStmtPurgeDeadSymbols>())
      return PathDiagnosticLocation::createEnd(S, SM, LC);

    if (S->getBeginLoc().isValid())
      return PathDiagnosticLocation(S, SM, LC);

    return PathDiagnosticLocation(
        PathDiagnosticLocation::getValidSourceLocation(S, LC), SM);
  }

  return PathDiagnosticLocation::createDeclEnd(ErrorNode->getLocationContext(),
                                               SM);
}

//===----------------------------------------------------------------------===//
// Methods for BugReporter and subclasses.
//===----------------------------------------------------------------------===//

const ExplodedGraph &PathSensitiveBugReporter::getGraph() const {
  return Eng.getGraph();
}

ProgramStateManager &PathSensitiveBugReporter::getStateManager() const {
  return Eng.getStateManager();
}

BugReporter::BugReporter(BugReporterData &D)
    : D(D), UserSuppressions(D.getASTContext()) {}

BugReporter::~BugReporter() {
  // Make sure reports are flushed.
  assert(StrBugTypes.empty() &&
         "Destroying BugReporter before diagnostics are emitted!");

  // Free the bug reports we are tracking.
  for (const auto I : EQClassesVector)
    delete I;
}

void BugReporter::FlushReports() {
  // We need to flush reports in deterministic order to ensure the order
  // of the reports is consistent between runs.
  for (const auto EQ : EQClassesVector)
    FlushReport(*EQ);

  // BugReporter owns and deletes only BugTypes created implicitly through
  // EmitBasicReport.
  // FIXME: There are leaks from checkers that assume that the BugTypes they
  // create will be destroyed by the BugReporter.
  StrBugTypes.clear();
}

//===----------------------------------------------------------------------===//
// PathDiagnostics generation.
//===----------------------------------------------------------------------===//

namespace {

/// A wrapper around an ExplodedGraph that contains a single path from the root
/// to the error node.
class BugPathInfo {
public:
  std::unique_ptr<ExplodedGraph> BugPath;
  PathSensitiveBugReport *Report;
  const ExplodedNode *ErrorNode;
};

/// A wrapper around an ExplodedGraph whose leafs are all error nodes. Can
/// conveniently retrieve bug paths from a single error node to the root.
class BugPathGetter {
  std::unique_ptr<ExplodedGraph> TrimmedGraph;

  using PriorityMapTy = llvm::DenseMap<const ExplodedNode *, unsigned>;

  /// Assign each node with its distance from the root.
  PriorityMapTy PriorityMap;

  /// Since the getErrorNode() or BugReport refers to the original ExplodedGraph,
  /// we need to pair it to the error node of the constructed trimmed graph.
  using ReportNewNodePair =
      std::pair<PathSensitiveBugReport *, const ExplodedNode *>;
  SmallVector<ReportNewNodePair, 32> ReportNodes;

  BugPathInfo CurrentBugPath;

  /// A helper class for sorting ExplodedNodes by priority.
  template <bool Descending>
  class PriorityCompare {
    const PriorityMapTy &PriorityMap;

  public:
    PriorityCompare(const PriorityMapTy &M) : PriorityMap(M) {}

    bool operator()(const ExplodedNode *LHS, const ExplodedNode *RHS) const {
      PriorityMapTy::const_iterator LI = PriorityMap.find(LHS);
      PriorityMapTy::const_iterator RI = PriorityMap.find(RHS);
      PriorityMapTy::const_iterator E = PriorityMap.end();

      if (LI == E)
        return Descending;
      if (RI == E)
        return !Descending;

      return Descending ? LI->second > RI->second
                        : LI->second < RI->second;
    }

    bool operator()(const ReportNewNodePair &LHS,
                    const ReportNewNodePair &RHS) const {
      return (*this)(LHS.second, RHS.second);
    }
  };

public:
  BugPathGetter(const ExplodedGraph *OriginalGraph,
                ArrayRef<PathSensitiveBugReport *> &bugReports);

  BugPathInfo *getNextBugPath();
};

} // namespace

BugPathGetter::BugPathGetter(const ExplodedGraph *OriginalGraph,
                             ArrayRef<PathSensitiveBugReport *> &bugReports) {
  SmallVector<const ExplodedNode *, 32> Nodes;
  for (const auto I : bugReports) {
    assert(I->isValid() &&
           "We only allow BugReporterVisitors and BugReporter itself to "
           "invalidate reports!");
    Nodes.emplace_back(I->getErrorNode());
  }

  // The trimmed graph is created in the body of the constructor to ensure
  // that the DenseMaps have been initialized already.
  InterExplodedGraphMap ForwardMap;
  TrimmedGraph = OriginalGraph->trim(Nodes, &ForwardMap);

  // Find the (first) error node in the trimmed graph.  We just need to consult
  // the node map which maps from nodes in the original graph to nodes
  // in the new graph.
  llvm::SmallPtrSet<const ExplodedNode *, 32> RemainingNodes;

  for (PathSensitiveBugReport *Report : bugReports) {
    const ExplodedNode *NewNode = ForwardMap.lookup(Report->getErrorNode());
    assert(NewNode &&
           "Failed to construct a trimmed graph that contains this error "
           "node!");
    ReportNodes.emplace_back(Report, NewNode);
    RemainingNodes.insert(NewNode);
  }

  assert(!RemainingNodes.empty() && "No error node found in the trimmed graph");

  // Perform a forward BFS to find all the shortest paths.
  std::queue<const ExplodedNode *> WS;

  assert(TrimmedGraph->num_roots() == 1);
  WS.push(*TrimmedGraph->roots_begin());
  unsigned Priority = 0;

  while (!WS.empty()) {
    const ExplodedNode *Node = WS.front();
    WS.pop();

    PriorityMapTy::iterator PriorityEntry;
    bool IsNew;
    std::tie(PriorityEntry, IsNew) = PriorityMap.insert({Node, Priority});
    ++Priority;

    if (!IsNew) {
      assert(PriorityEntry->second <= Priority);
      continue;
    }

    if (RemainingNodes.erase(Node))
      if (RemainingNodes.empty())
        break;

    for (const ExplodedNode *Succ : Node->succs())
      WS.push(Succ);
  }

  // Sort the error paths from longest to shortest.
  llvm::sort(ReportNodes, PriorityCompare<true>(PriorityMap));
}

BugPathInfo *BugPathGetter::getNextBugPath() {
  if (ReportNodes.empty())
    return nullptr;

  const ExplodedNode *OrigN;
  std::tie(CurrentBugPath.Report, OrigN) = ReportNodes.pop_back_val();
  assert(PriorityMap.contains(OrigN) && "error node not accessible from root");

  // Create a new graph with a single path. This is the graph that will be
  // returned to the caller.
  auto GNew = std::make_unique<ExplodedGraph>();

  // Now walk from the error node up the BFS path, always taking the
  // predeccessor with the lowest number.
  ExplodedNode *Succ = nullptr;
  while (true) {
    // Create the equivalent node in the new graph with the same state
    // and location.
    ExplodedNode *NewN = GNew->createUncachedNode(
        OrigN->getLocation(), OrigN->getState(),
        OrigN->getID(), OrigN->isSink());

    // Link up the new node with the previous node.
    if (Succ)
      Succ->addPredecessor(NewN, *GNew);
    else
      CurrentBugPath.ErrorNode = NewN;

    Succ = NewN;

    // Are we at the final node?
    if (OrigN->pred_empty()) {
      GNew->addRoot(NewN);
      break;
    }

    // Find the next predeccessor node.  We choose the node that is marked
    // with the lowest BFS number.
    OrigN = *std::min_element(OrigN->pred_begin(), OrigN->pred_end(),
                              PriorityCompare<false>(PriorityMap));
  }

  CurrentBugPath.BugPath = std::move(GNew);

  return &CurrentBugPath;
}

/// CompactMacroExpandedPieces - This function postprocesses a PathDiagnostic
/// object and collapses PathDiagosticPieces that are expanded by macros.
static void CompactMacroExpandedPieces(PathPieces &path,
                                       const SourceManager& SM) {
  using MacroStackTy = std::vector<
      std::pair<std::shared_ptr<PathDiagnosticMacroPiece>, SourceLocation>>;

  using PiecesTy = std::vector<PathDiagnosticPieceRef>;

  MacroStackTy MacroStack;
  PiecesTy Pieces;

  for (PathPieces::const_iterator I = path.begin(), E = path.end();
       I != E; ++I) {
    const auto &piece = *I;

    // Recursively compact calls.
    if (auto *call = dyn_cast<PathDiagnosticCallPiece>(&*piece)) {
      CompactMacroExpandedPieces(call->path, SM);
    }

    // Get the location of the PathDiagnosticPiece.
    const FullSourceLoc Loc = piece->getLocation().asLocation();

    // Determine the instantiation location, which is the location we group
    // related PathDiagnosticPieces.
    SourceLocation InstantiationLoc = Loc.isMacroID() ?
                                      SM.getExpansionLoc(Loc) :
                                      SourceLocation();

    if (Loc.isFileID()) {
      MacroStack.clear();
      Pieces.push_back(piece);
      continue;
    }

    assert(Loc.isMacroID());

    // Is the PathDiagnosticPiece within the same macro group?
    if (!MacroStack.empty() && InstantiationLoc == MacroStack.back().second) {
      MacroStack.back().first->subPieces.push_back(piece);
      continue;
    }

    // We aren't in the same group.  Are we descending into a new macro
    // or are part of an old one?
    std::shared_ptr<PathDiagnosticMacroPiece> MacroGroup;

    SourceLocation ParentInstantiationLoc = InstantiationLoc.isMacroID() ?
                                          SM.getExpansionLoc(Loc) :
                                          SourceLocation();

    // Walk the entire macro stack.
    while (!MacroStack.empty()) {
      if (InstantiationLoc == MacroStack.back().second) {
        MacroGroup = MacroStack.back().first;
        break;
      }

      if (ParentInstantiationLoc == MacroStack.back().second) {
        MacroGroup = MacroStack.back().first;
        break;
      }

      MacroStack.pop_back();
    }

    if (!MacroGroup || ParentInstantiationLoc == MacroStack.back().second) {
      // Create a new macro group and add it to the stack.
      auto NewGroup = std::make_shared<PathDiagnosticMacroPiece>(
          PathDiagnosticLocation::createSingleLocation(piece->getLocation()));

      if (MacroGroup)
        MacroGroup->subPieces.push_back(NewGroup);
      else {
        assert(InstantiationLoc.isFileID());
        Pieces.push_back(NewGroup);
      }

      MacroGroup = NewGroup;
      MacroStack.push_back(std::make_pair(MacroGroup, InstantiationLoc));
    }

    // Finally, add the PathDiagnosticPiece to the group.
    MacroGroup->subPieces.push_back(piece);
  }

  // Now take the pieces and construct a new PathDiagnostic.
  path.clear();

  path.insert(path.end(), Pieces.begin(), Pieces.end());
}

/// Generate notes from all visitors.
/// Notes associated with @c ErrorNode are generated using
/// @c getEndPath, and the rest are generated with @c VisitNode.
static std::unique_ptr<VisitorsDiagnosticsTy>
generateVisitorsDiagnostics(PathSensitiveBugReport *R,
                            const ExplodedNode *ErrorNode,
                            BugReporterContext &BRC) {
  std::unique_ptr<VisitorsDiagnosticsTy> Notes =
      std::make_unique<VisitorsDiagnosticsTy>();
  PathSensitiveBugReport::VisitorList visitors;

  // Run visitors on all nodes starting from the node *before* the last one.
  // The last node is reserved for notes generated with @c getEndPath.
  const ExplodedNode *NextNode = ErrorNode->getFirstPred();
  while (NextNode) {

    // At each iteration, move all visitors from report to visitor list. This is
    // important, because the Profile() functions of the visitors make sure that
    // a visitor isn't added multiple times for the same node, but it's fine
    // to add the a visitor with Profile() for different nodes (e.g. tracking
    // a region at different points of the symbolic execution).
    for (std::unique_ptr<BugReporterVisitor> &Visitor : R->visitors())
      visitors.push_back(std::move(Visitor));

    R->clearVisitors();

    const ExplodedNode *Pred = NextNode->getFirstPred();
    if (!Pred) {
      PathDiagnosticPieceRef LastPiece;
      for (auto &V : visitors) {
        V->finalizeVisitor(BRC, ErrorNode, *R);

        if (auto Piece = V->getEndPath(BRC, ErrorNode, *R)) {
          assert(!LastPiece &&
                 "There can only be one final piece in a diagnostic.");
          assert(Piece->getKind() == PathDiagnosticPiece::Kind::Event &&
                 "The final piece must contain a message!");
          LastPiece = std::move(Piece);
          (*Notes)[ErrorNode].push_back(LastPiece);
        }
      }
      break;
    }

    for (auto &V : visitors) {
      auto P = V->VisitNode(NextNode, BRC, *R);
      if (P)
        (*Notes)[NextNode].push_back(std::move(P));
    }

    if (!R->isValid())
      break;

    NextNode = Pred;
  }

  return Notes;
}

std::optional<PathDiagnosticBuilder> PathDiagnosticBuilder::findValidReport(
    ArrayRef<PathSensitiveBugReport *> &bugReports,
    PathSensitiveBugReporter &Reporter) {
  Z3CrosscheckOracle Z3Oracle(Reporter.getAnalyzerOptions());

  BugPathGetter BugGraph(&Reporter.getGraph(), bugReports);

  while (BugPathInfo *BugPath = BugGraph.getNextBugPath()) {
    // Find the BugReport with the original location.
    PathSensitiveBugReport *R = BugPath->Report;
    assert(R && "No original report found for sliced graph.");
    assert(R->isValid() && "Report selected by trimmed graph marked invalid.");
    const ExplodedNode *ErrorNode = BugPath->ErrorNode;

    // Register refutation visitors first, if they mark the bug invalid no
    // further analysis is required
    R->addVisitor<LikelyFalsePositiveSuppressionBRVisitor>();

    // Register additional node visitors.
    R->addVisitor<NilReceiverBRVisitor>();
    R->addVisitor<ConditionBRVisitor>();
    R->addVisitor<TagVisitor>();

    BugReporterContext BRC(Reporter);

    // Run all visitors on a given graph, once.
    std::unique_ptr<VisitorsDiagnosticsTy> visitorNotes =
        generateVisitorsDiagnostics(R, ErrorNode, BRC);

    if (R->isValid()) {
      if (Reporter.getAnalyzerOptions().ShouldCrosscheckWithZ3) {
        // If crosscheck is enabled, remove all visitors, add the refutation
        // visitor and check again
        R->clearVisitors();
        Z3CrosscheckVisitor::Z3Result CrosscheckResult;
        R->addVisitor<Z3CrosscheckVisitor>(CrosscheckResult,
                                           Reporter.getAnalyzerOptions());

        // We don't overwrite the notes inserted by other visitors because the
        // refutation manager does not add any new note to the path
        generateVisitorsDiagnostics(R, BugPath->ErrorNode, BRC);
        switch (Z3Oracle.interpretQueryResult(CrosscheckResult)) {
        case Z3CrosscheckOracle::RejectReport:
          ++NumTimesReportRefuted;
          R->markInvalid("Infeasible constraints", /*Data=*/nullptr);
          continue;
        case Z3CrosscheckOracle::RejectEQClass:
          ++NumTimesReportEQClassAborted;
          return {};
        case Z3CrosscheckOracle::AcceptReport:
          ++NumTimesReportPassesZ3;
          break;
        }
      }

      assert(R->isValid());
      return PathDiagnosticBuilder(std::move(BRC), std::move(BugPath->BugPath),
                                   BugPath->Report, BugPath->ErrorNode,
                                   std::move(visitorNotes));
    }
  }

  ++NumTimesReportEQClassWasExhausted;
  return {};
}

std::unique_ptr<DiagnosticForConsumerMapTy>
PathSensitiveBugReporter::generatePathDiagnostics(
    ArrayRef<PathDiagnosticConsumer *> consumers,
    ArrayRef<PathSensitiveBugReport *> &bugReports) {
  assert(!bugReports.empty());

  auto Out = std::make_unique<DiagnosticForConsumerMapTy>();

  std::optional<PathDiagnosticBuilder> PDB =
      PathDiagnosticBuilder::findValidReport(bugReports, *this);

  if (PDB) {
    for (PathDiagnosticConsumer *PC : consumers) {
      if (std::unique_ptr<PathDiagnostic> PD = PDB->generate(PC)) {
        (*Out)[PC] = std::move(PD);
      }
    }
  }

  return Out;
}

void BugReporter::emitReport(std::unique_ptr<BugReport> R) {
  bool ValidSourceLoc = R->getLocation().isValid();
  assert(ValidSourceLoc);
  // If we mess up in a release build, we'd still prefer to just drop the bug
  // instead of trying to go on.
  if (!ValidSourceLoc)
    return;

  // If the user asked to suppress this report, we should skip it.
  if (UserSuppressions.isSuppressed(*R))
    return;

  // Compute the bug report's hash to determine its equivalence class.
  llvm::FoldingSetNodeID ID;
  R->Profile(ID);

  // Lookup the equivance class.  If there isn't one, create it.
  void *InsertPos;
  BugReportEquivClass* EQ = EQClasses.FindNodeOrInsertPos(ID, InsertPos);

  if (!EQ) {
    EQ = new BugReportEquivClass(std::move(R));
    EQClasses.InsertNode(EQ, InsertPos);
    EQClassesVector.push_back(EQ);
  } else
    EQ->AddReport(std::move(R));
}

void PathSensitiveBugReporter::emitReport(std::unique_ptr<BugReport> R) {
  if (auto PR = dyn_cast<PathSensitiveBugReport>(R.get()))
    if (const ExplodedNode *E = PR->getErrorNode()) {
      // An error node must either be a sink or have a tag, otherwise
      // it could get reclaimed before the path diagnostic is created.
      assert((E->isSink() || E->getLocation().getTag()) &&
             "Error node must either be a sink or have a tag");

      const AnalysisDeclContext *DeclCtx =
          E->getLocationContext()->getAnalysisDeclContext();
      // The source of autosynthesized body can be handcrafted AST or a model
      // file. The locations from handcrafted ASTs have no valid source
      // locations and have to be discarded. Locations from model files should
      // be preserved for processing and reporting.
      if (DeclCtx->isBodyAutosynthesized() &&
          !DeclCtx->isBodyAutosynthesizedFromModelFile())
        return;
    }

  BugReporter::emitReport(std::move(R));
}

//===----------------------------------------------------------------------===//
// Emitting reports in equivalence classes.
//===----------------------------------------------------------------------===//

namespace {

struct FRIEC_WLItem {
  const ExplodedNode *N;
  ExplodedNode::const_succ_iterator I, E;

  FRIEC_WLItem(const ExplodedNode *n)
      : N(n), I(N->succ_begin()), E(N->succ_end()) {}
};

} // namespace

BugReport *PathSensitiveBugReporter::findReportInEquivalenceClass(
    BugReportEquivClass &EQ, SmallVectorImpl<BugReport *> &bugReports) {
  // If we don't need to suppress any of the nodes because they are
  // post-dominated by a sink, simply add all the nodes in the equivalence class
  // to 'Nodes'.  Any of the reports will serve as a "representative" report.
  assert(EQ.getReports().size() > 0);
  const BugType& BT = EQ.getReports()[0]->getBugType();
  if (!BT.isSuppressOnSink()) {
    BugReport *R = EQ.getReports()[0].get();
    for (auto &J : EQ.getReports()) {
      if (auto *PR = dyn_cast<PathSensitiveBugReport>(J.get())) {
        R = PR;
        bugReports.push_back(PR);
      }
    }
    return R;
  }

  // For bug reports that should be suppressed when all paths are post-dominated
  // by a sink node, iterate through the reports in the equivalence class
  // until we find one that isn't post-dominated (if one exists).  We use a
  // DFS traversal of the ExplodedGraph to find a non-sink node.  We could write
  // this as a recursive function, but we don't want to risk blowing out the
  // stack for very long paths.
  BugReport *exampleReport = nullptr;

  for (const auto &I: EQ.getReports()) {
    auto *R = dyn_cast<PathSensitiveBugReport>(I.get());
    if (!R)
      continue;

    const ExplodedNode *errorNode = R->getErrorNode();
    if (errorNode->isSink()) {
      llvm_unreachable(
           "BugType::isSuppressSink() should not be 'true' for sink end nodes");
    }
    // No successors?  By definition this nodes isn't post-dominated by a sink.
    if (errorNode->succ_empty()) {
      bugReports.push_back(R);
      if (!exampleReport)
        exampleReport = R;
      continue;
    }

    // See if we are in a no-return CFG block. If so, treat this similarly
    // to being post-dominated by a sink. This works better when the analysis
    // is incomplete and we have never reached the no-return function call(s)
    // that we'd inevitably bump into on this path.
    if (const CFGBlock *ErrorB = errorNode->getCFGBlock())
      if (ErrorB->isInevitablySinking())
        continue;

    // At this point we know that 'N' is not a sink and it has at least one
    // successor.  Use a DFS worklist to find a non-sink end-of-path node.
    using WLItem = FRIEC_WLItem;
    using DFSWorkList = SmallVector<WLItem, 10>;

    llvm::DenseMap<const ExplodedNode *, unsigned> Visited;

    DFSWorkList WL;
    WL.push_back(errorNode);
    Visited[errorNode] = 1;

    while (!WL.empty()) {
      WLItem &WI = WL.back();
      assert(!WI.N->succ_empty());

      for (; WI.I != WI.E; ++WI.I) {
        const ExplodedNode *Succ = *WI.I;
        // End-of-path node?
        if (Succ->succ_empty()) {
          // If we found an end-of-path node that is not a sink.
          if (!Succ->isSink()) {
            bugReports.push_back(R);
            if (!exampleReport)
              exampleReport = R;
            WL.clear();
            break;
          }
          // Found a sink?  Continue on to the next successor.
          continue;
        }
        // Mark the successor as visited.  If it hasn't been explored,
        // enqueue it to the DFS worklist.
        unsigned &mark = Visited[Succ];
        if (!mark) {
          mark = 1;
          WL.push_back(Succ);
          break;
        }
      }

      // The worklist may have been cleared at this point.  First
      // check if it is empty before checking the last item.
      if (!WL.empty() && &WL.back() == &WI)
        WL.pop_back();
    }
  }

  // ExampleReport will be NULL if all the nodes in the equivalence class
  // were post-dominated by sinks.
  return exampleReport;
}

void BugReporter::FlushReport(BugReportEquivClass& EQ) {
  SmallVector<BugReport*, 10> bugReports;
  BugReport *report = findReportInEquivalenceClass(EQ, bugReports);
  if (!report)
    return;

  // See whether we need to silence the checker/package.
  for (const std::string &CheckerOrPackage :
       getAnalyzerOptions().SilencedCheckersAndPackages) {
    if (report->getBugType().getCheckerName().starts_with(CheckerOrPackage))
      return;
  }

  ArrayRef<PathDiagnosticConsumer*> Consumers = getPathDiagnosticConsumers();
  std::unique_ptr<DiagnosticForConsumerMapTy> Diagnostics =
      generateDiagnosticForConsumerMap(report, Consumers, bugReports);

  for (auto &P : *Diagnostics) {
    PathDiagnosticConsumer *Consumer = P.first;
    std::unique_ptr<PathDiagnostic> &PD = P.second;

    // If the path is empty, generate a single step path with the location
    // of the issue.
    if (PD->path.empty()) {
      PathDiagnosticLocation L = report->getLocation();
      auto piece = std::make_unique<PathDiagnosticEventPiece>(
        L, report->getDescription());
      for (SourceRange Range : report->getRanges())
        piece->addRange(Range);
      PD->setEndOfPath(std::move(piece));
    }

    PathPieces &Pieces = PD->getMutablePieces();
    if (getAnalyzerOptions().ShouldDisplayNotesAsEvents) {
      // For path diagnostic consumers that don't support extra notes,
      // we may optionally convert those to path notes.
      for (const auto &I : llvm::reverse(report->getNotes())) {
        PathDiagnosticNotePiece *Piece = I.get();
        auto ConvertedPiece = std::make_shared<PathDiagnosticEventPiece>(
          Piece->getLocation(), Piece->getString());
        for (const auto &R: Piece->getRanges())
          ConvertedPiece->addRange(R);

        Pieces.push_front(std::move(ConvertedPiece));
      }
    } else {
      for (const auto &I : llvm::reverse(report->getNotes()))
        Pieces.push_front(I);
    }

    for (const auto &I : report->getFixits())
      Pieces.back()->addFixit(I);

    updateExecutedLinesWithDiagnosticPieces(*PD);

    // If we are debugging, let's have the entry point as the first note.
    if (getAnalyzerOptions().AnalyzerDisplayProgress ||
        getAnalyzerOptions().AnalyzerNoteAnalysisEntryPoints) {
      const Decl *EntryPoint = getAnalysisEntryPoint();
      Pieces.push_front(std::make_shared<PathDiagnosticEventPiece>(
          PathDiagnosticLocation{EntryPoint->getLocation(), getSourceManager()},
          "[debug] analyzing from " +
              AnalysisDeclContext::getFunctionName(EntryPoint)));
    }
    Consumer->HandlePathDiagnostic(std::move(PD));
  }
}

/// Insert all lines participating in the function signature \p Signature
/// into \p ExecutedLines.
static void populateExecutedLinesWithFunctionSignature(
    const Decl *Signature, const SourceManager &SM,
    FilesToLineNumsMap &ExecutedLines) {
  SourceRange SignatureSourceRange;
  const Stmt* Body = Signature->getBody();
  if (const auto FD = dyn_cast<FunctionDecl>(Signature)) {
    SignatureSourceRange = FD->getSourceRange();
  } else if (const auto OD = dyn_cast<ObjCMethodDecl>(Signature)) {
    SignatureSourceRange = OD->getSourceRange();
  } else {
    return;
  }
  SourceLocation Start = SignatureSourceRange.getBegin();
  SourceLocation End = Body ? Body->getSourceRange().getBegin()
    : SignatureSourceRange.getEnd();
  if (!Start.isValid() || !End.isValid())
    return;
  unsigned StartLine = SM.getExpansionLineNumber(Start);
  unsigned EndLine = SM.getExpansionLineNumber(End);

  FileID FID = SM.getFileID(SM.getExpansionLoc(Start));
  for (unsigned Line = StartLine; Line <= EndLine; Line++)
    ExecutedLines[FID].insert(Line);
}

static void populateExecutedLinesWithStmt(
    const Stmt *S, const SourceManager &SM,
    FilesToLineNumsMap &ExecutedLines) {
  SourceLocation Loc = S->getSourceRange().getBegin();
  if (!Loc.isValid())
    return;
  SourceLocation ExpansionLoc = SM.getExpansionLoc(Loc);
  FileID FID = SM.getFileID(ExpansionLoc);
  unsigned LineNo = SM.getExpansionLineNumber(ExpansionLoc);
  ExecutedLines[FID].insert(LineNo);
}

/// \return all executed lines including function signatures on the path
/// starting from \p N.
static std::unique_ptr<FilesToLineNumsMap>
findExecutedLines(const SourceManager &SM, const ExplodedNode *N) {
  auto ExecutedLines = std::make_unique<FilesToLineNumsMap>();

  while (N) {
    if (N->getFirstPred() == nullptr) {
      // First node: show signature of the entrance point.
      const Decl *D = N->getLocationContext()->getDecl();
      populateExecutedLinesWithFunctionSignature(D, SM, *ExecutedLines);
    } else if (auto CE = N->getLocationAs<CallEnter>()) {
      // Inlined function: show signature.
      const Decl* D = CE->getCalleeContext()->getDecl();
      populateExecutedLinesWithFunctionSignature(D, SM, *ExecutedLines);
    } else if (const Stmt *S = N->getStmtForDiagnostics()) {
      populateExecutedLinesWithStmt(S, SM, *ExecutedLines);

      // Show extra context for some parent kinds.
      const Stmt *P = N->getParentMap().getParent(S);

      // The path exploration can die before the node with the associated
      // return statement is generated, but we do want to show the whole
      // return.
      if (const auto *RS = dyn_cast_or_null<ReturnStmt>(P)) {
        populateExecutedLinesWithStmt(RS, SM, *ExecutedLines);
        P = N->getParentMap().getParent(RS);
      }

      if (isa_and_nonnull<SwitchCase, LabelStmt>(P))
        populateExecutedLinesWithStmt(P, SM, *ExecutedLines);
    }

    N = N->getFirstPred();
  }
  return ExecutedLines;
}

std::unique_ptr<DiagnosticForConsumerMapTy>
BugReporter::generateDiagnosticForConsumerMap(
    BugReport *exampleReport, ArrayRef<PathDiagnosticConsumer *> consumers,
    ArrayRef<BugReport *> bugReports) {
  auto *basicReport = cast<BasicBugReport>(exampleReport);
  auto Out = std::make_unique<DiagnosticForConsumerMapTy>();
  for (auto *Consumer : consumers)
    (*Out)[Consumer] =
        generateDiagnosticForBasicReport(basicReport, AnalysisEntryPoint);
  return Out;
}

static PathDiagnosticCallPiece *
getFirstStackedCallToHeaderFile(PathDiagnosticCallPiece *CP,
                                const SourceManager &SMgr) {
  SourceLocation CallLoc = CP->callEnter.asLocation();

  // If the call is within a macro, don't do anything (for now).
  if (CallLoc.isMacroID())
    return nullptr;

  assert(AnalysisManager::isInCodeFile(CallLoc, SMgr) &&
         "The call piece should not be in a header file.");

  // Check if CP represents a path through a function outside of the main file.
  if (!AnalysisManager::isInCodeFile(CP->callEnterWithin.asLocation(), SMgr))
    return CP;

  const PathPieces &Path = CP->path;
  if (Path.empty())
    return nullptr;

  // Check if the last piece in the callee path is a call to a function outside
  // of the main file.
  if (auto *CPInner = dyn_cast<PathDiagnosticCallPiece>(Path.back().get()))
    return getFirstStackedCallToHeaderFile(CPInner, SMgr);

  // Otherwise, the last piece is in the main file.
  return nullptr;
}

static void resetDiagnosticLocationToMainFile(PathDiagnostic &PD) {
  if (PD.path.empty())
    return;

  PathDiagnosticPiece *LastP = PD.path.back().get();
  assert(LastP);
  const SourceManager &SMgr = LastP->getLocation().getManager();

  // We only need to check if the report ends inside headers, if the last piece
  // is a call piece.
  if (auto *CP = dyn_cast<PathDiagnosticCallPiece>(LastP)) {
    CP = getFirstStackedCallToHeaderFile(CP, SMgr);
    if (CP) {
      // Mark the piece.
       CP->setAsLastInMainSourceFile();

      // Update the path diagnostic message.
      const auto *ND = dyn_cast<NamedDecl>(CP->getCallee());
      if (ND) {
        SmallString<200> buf;
        llvm::raw_svector_ostream os(buf);
        os << " (within a call to '" << ND->getDeclName() << "')";
        PD.appendToDesc(os.str());
      }

      // Reset the report containing declaration and location.
      PD.setDeclWithIssue(CP->getCaller());
      PD.setLocation(CP->getLocation());

      return;
    }
  }
}



std::unique_ptr<DiagnosticForConsumerMapTy>
PathSensitiveBugReporter::generateDiagnosticForConsumerMap(
    BugReport *exampleReport, ArrayRef<PathDiagnosticConsumer *> consumers,
    ArrayRef<BugReport *> bugReports) {
  std::vector<BasicBugReport *> BasicBugReports;
  std::vector<PathSensitiveBugReport *> PathSensitiveBugReports;
  if (isa<BasicBugReport>(exampleReport))
    return BugReporter::generateDiagnosticForConsumerMap(exampleReport,
                                                         consumers, bugReports);

  // Generate the full path sensitive diagnostic, using the generation scheme
  // specified by the PathDiagnosticConsumer. Note that we have to generate
  // path diagnostics even for consumers which do not support paths, because
  // the BugReporterVisitors may mark this bug as a false positive.
  assert(!bugReports.empty());
  MaxBugClassSize.updateMax(bugReports.size());

  // Avoid copying the whole array because there may be a lot of reports.
  ArrayRef<PathSensitiveBugReport *> convertedArrayOfReports(
      reinterpret_cast<PathSensitiveBugReport *const *>(&*bugReports.begin()),
      reinterpret_cast<PathSensitiveBugReport *const *>(&*bugReports.end()));
  std::unique_ptr<DiagnosticForConsumerMapTy> Out = generatePathDiagnostics(
      consumers, convertedArrayOfReports);

  if (Out->empty())
    return Out;

  MaxValidBugClassSize.updateMax(bugReports.size());

  // Examine the report and see if the last piece is in a header. Reset the
  // report location to the last piece in the main source file.
  const AnalyzerOptions &Opts = getAnalyzerOptions();
  for (auto const &P : *Out)
    if (Opts.ShouldReportIssuesInMainSourceFile && !Opts.AnalyzeAll)
      resetDiagnosticLocationToMainFile(*P.second);

  return Out;
}

void BugReporter::EmitBasicReport(const Decl *DeclWithIssue,
                                  const CheckerBase *Checker, StringRef Name,
                                  StringRef Category, StringRef Str,
                                  PathDiagnosticLocation Loc,
                                  ArrayRef<SourceRange> Ranges,
                                  ArrayRef<FixItHint> Fixits) {
  EmitBasicReport(DeclWithIssue, Checker->getCheckerName(), Name, Category, Str,
                  Loc, Ranges, Fixits);
}

void BugReporter::EmitBasicReport(const Decl *DeclWithIssue,
                                  CheckerNameRef CheckName,
                                  StringRef name, StringRef category,
                                  StringRef str, PathDiagnosticLocation Loc,
                                  ArrayRef<SourceRange> Ranges,
                                  ArrayRef<FixItHint> Fixits) {
  // 'BT' is owned by BugReporter.
  BugType *BT = getBugTypeForName(CheckName, name, category);
  auto R = std::make_unique<BasicBugReport>(*BT, str, Loc);
  R->setDeclWithIssue(DeclWithIssue);
  for (const auto &SR : Ranges)
    R->addRange(SR);
  for (const auto &FH : Fixits)
    R->addFixItHint(FH);
  emitReport(std::move(R));
}

BugType *BugReporter::getBugTypeForName(CheckerNameRef CheckName,
                                        StringRef name, StringRef category) {
  SmallString<136> fullDesc;
  llvm::raw_svector_ostream(fullDesc) << CheckName.getName() << ":" << name
                                      << ":" << category;
  std::unique_ptr<BugType> &BT = StrBugTypes[fullDesc];
  if (!BT)
    BT = std::make_unique<BugType>(CheckName, name, category);
  return BT.get();
}
