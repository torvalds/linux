//===- PathDiagnostic.cpp - Path-Specific Diagnostic Handling -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PathDiagnostic-related interfaces.
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/ParentMap.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ProgramPoint.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace clang;
using namespace ento;

static StringRef StripTrailingDots(StringRef s) { return s.rtrim('.'); }

PathDiagnosticPiece::PathDiagnosticPiece(StringRef s,
                                         Kind k, DisplayHint hint)
    : str(StripTrailingDots(s)), kind(k), Hint(hint) {}

PathDiagnosticPiece::PathDiagnosticPiece(Kind k, DisplayHint hint)
    : kind(k), Hint(hint) {}

PathDiagnosticPiece::~PathDiagnosticPiece() = default;

PathDiagnosticEventPiece::~PathDiagnosticEventPiece() = default;

PathDiagnosticCallPiece::~PathDiagnosticCallPiece() = default;

PathDiagnosticControlFlowPiece::~PathDiagnosticControlFlowPiece() = default;

PathDiagnosticMacroPiece::~PathDiagnosticMacroPiece() = default;

PathDiagnosticNotePiece::~PathDiagnosticNotePiece() = default;

PathDiagnosticPopUpPiece::~PathDiagnosticPopUpPiece() = default;

void PathPieces::flattenTo(PathPieces &Primary, PathPieces &Current,
                           bool ShouldFlattenMacros) const {
  for (auto &Piece : *this) {
    switch (Piece->getKind()) {
    case PathDiagnosticPiece::Call: {
      auto &Call = cast<PathDiagnosticCallPiece>(*Piece);
      if (auto CallEnter = Call.getCallEnterEvent())
        Current.push_back(std::move(CallEnter));
      Call.path.flattenTo(Primary, Primary, ShouldFlattenMacros);
      if (auto callExit = Call.getCallExitEvent())
        Current.push_back(std::move(callExit));
      break;
    }
    case PathDiagnosticPiece::Macro: {
      auto &Macro = cast<PathDiagnosticMacroPiece>(*Piece);
      if (ShouldFlattenMacros) {
        Macro.subPieces.flattenTo(Primary, Primary, ShouldFlattenMacros);
      } else {
        Current.push_back(Piece);
        PathPieces NewPath;
        Macro.subPieces.flattenTo(Primary, NewPath, ShouldFlattenMacros);
        // FIXME: This probably shouldn't mutate the original path piece.
        Macro.subPieces = NewPath;
      }
      break;
    }
    case PathDiagnosticPiece::Event:
    case PathDiagnosticPiece::ControlFlow:
    case PathDiagnosticPiece::Note:
    case PathDiagnosticPiece::PopUp:
      Current.push_back(Piece);
      break;
    }
  }
}

PathDiagnostic::~PathDiagnostic() = default;

PathDiagnostic::PathDiagnostic(
    StringRef CheckerName, const Decl *declWithIssue, StringRef bugtype,
    StringRef verboseDesc, StringRef shortDesc, StringRef category,
    PathDiagnosticLocation LocationToUnique, const Decl *DeclToUnique,
    const Decl *AnalysisEntryPoint,
    std::unique_ptr<FilesToLineNumsMap> ExecutedLines)
    : CheckerName(CheckerName), DeclWithIssue(declWithIssue),
      BugType(StripTrailingDots(bugtype)),
      VerboseDesc(StripTrailingDots(verboseDesc)),
      ShortDesc(StripTrailingDots(shortDesc)),
      Category(StripTrailingDots(category)), UniqueingLoc(LocationToUnique),
      UniqueingDecl(DeclToUnique), AnalysisEntryPoint(AnalysisEntryPoint),
      ExecutedLines(std::move(ExecutedLines)), path(pathImpl) {
  assert(AnalysisEntryPoint);
}

void PathDiagnosticConsumer::anchor() {}

PathDiagnosticConsumer::~PathDiagnosticConsumer() {
  // Delete the contents of the FoldingSet if it isn't empty already.
  for (auto &Diag : Diags)
    delete &Diag;
}

void PathDiagnosticConsumer::HandlePathDiagnostic(
    std::unique_ptr<PathDiagnostic> D) {
  if (!D || D->path.empty())
    return;

  // We need to flatten the locations (convert Stmt* to locations) because
  // the referenced statements may be freed by the time the diagnostics
  // are emitted.
  D->flattenLocations();

  // If the PathDiagnosticConsumer does not support diagnostics that
  // cross file boundaries, prune out such diagnostics now.
  if (!supportsCrossFileDiagnostics()) {
    // Verify that the entire path is from the same FileID.
    FileID FID;
    const SourceManager &SMgr = D->path.front()->getLocation().getManager();
    SmallVector<const PathPieces *, 5> WorkList;
    WorkList.push_back(&D->path);
    SmallString<128> buf;
    llvm::raw_svector_ostream warning(buf);
    warning << "warning: Path diagnostic report is not generated. Current "
            << "output format does not support diagnostics that cross file "
            << "boundaries. Refer to --analyzer-output for valid output "
            << "formats\n";

    while (!WorkList.empty()) {
      const PathPieces &path = *WorkList.pop_back_val();

      for (const auto &I : path) {
        const PathDiagnosticPiece *piece = I.get();
        FullSourceLoc L = piece->getLocation().asLocation().getExpansionLoc();

        if (FID.isInvalid()) {
          FID = SMgr.getFileID(L);
        } else if (SMgr.getFileID(L) != FID) {
          llvm::errs() << warning.str();
          return;
        }

        // Check the source ranges.
        ArrayRef<SourceRange> Ranges = piece->getRanges();
        for (const auto &I : Ranges) {
          SourceLocation L = SMgr.getExpansionLoc(I.getBegin());
          if (!L.isFileID() || SMgr.getFileID(L) != FID) {
            llvm::errs() << warning.str();
            return;
          }
          L = SMgr.getExpansionLoc(I.getEnd());
          if (!L.isFileID() || SMgr.getFileID(L) != FID) {
            llvm::errs() << warning.str();
            return;
          }
        }

        if (const auto *call = dyn_cast<PathDiagnosticCallPiece>(piece))
          WorkList.push_back(&call->path);
        else if (const auto *macro = dyn_cast<PathDiagnosticMacroPiece>(piece))
          WorkList.push_back(&macro->subPieces);
      }
    }

    if (FID.isInvalid())
      return; // FIXME: Emit a warning?
  }

  // Profile the node to see if we already have something matching it
  llvm::FoldingSetNodeID profile;
  D->Profile(profile);
  void *InsertPos = nullptr;

  if (PathDiagnostic *orig = Diags.FindNodeOrInsertPos(profile, InsertPos)) {
    // Keep the PathDiagnostic with the shorter path.
    // Note, the enclosing routine is called in deterministic order, so the
    // results will be consistent between runs (no reason to break ties if the
    // size is the same).
    const unsigned orig_size = orig->full_size();
    const unsigned new_size = D->full_size();
    if (orig_size <= new_size)
      return;

    assert(orig != D.get());
    Diags.RemoveNode(orig);
    delete orig;
  }

  Diags.InsertNode(D.release());
}

static std::optional<bool> comparePath(const PathPieces &X,
                                       const PathPieces &Y);

static std::optional<bool>
compareControlFlow(const PathDiagnosticControlFlowPiece &X,
                   const PathDiagnosticControlFlowPiece &Y) {
  FullSourceLoc XSL = X.getStartLocation().asLocation();
  FullSourceLoc YSL = Y.getStartLocation().asLocation();
  if (XSL != YSL)
    return XSL.isBeforeInTranslationUnitThan(YSL);
  FullSourceLoc XEL = X.getEndLocation().asLocation();
  FullSourceLoc YEL = Y.getEndLocation().asLocation();
  if (XEL != YEL)
    return XEL.isBeforeInTranslationUnitThan(YEL);
  return std::nullopt;
}

static std::optional<bool> compareMacro(const PathDiagnosticMacroPiece &X,
                                        const PathDiagnosticMacroPiece &Y) {
  return comparePath(X.subPieces, Y.subPieces);
}

static std::optional<bool> compareCall(const PathDiagnosticCallPiece &X,
                                       const PathDiagnosticCallPiece &Y) {
  FullSourceLoc X_CEL = X.callEnter.asLocation();
  FullSourceLoc Y_CEL = Y.callEnter.asLocation();
  if (X_CEL != Y_CEL)
    return X_CEL.isBeforeInTranslationUnitThan(Y_CEL);
  FullSourceLoc X_CEWL = X.callEnterWithin.asLocation();
  FullSourceLoc Y_CEWL = Y.callEnterWithin.asLocation();
  if (X_CEWL != Y_CEWL)
    return X_CEWL.isBeforeInTranslationUnitThan(Y_CEWL);
  FullSourceLoc X_CRL = X.callReturn.asLocation();
  FullSourceLoc Y_CRL = Y.callReturn.asLocation();
  if (X_CRL != Y_CRL)
    return X_CRL.isBeforeInTranslationUnitThan(Y_CRL);
  return comparePath(X.path, Y.path);
}

static std::optional<bool> comparePiece(const PathDiagnosticPiece &X,
                                        const PathDiagnosticPiece &Y) {
  if (X.getKind() != Y.getKind())
    return X.getKind() < Y.getKind();

  FullSourceLoc XL = X.getLocation().asLocation();
  FullSourceLoc YL = Y.getLocation().asLocation();
  if (XL != YL)
    return XL.isBeforeInTranslationUnitThan(YL);

  if (X.getString() != Y.getString())
    return X.getString() < Y.getString();

  if (X.getRanges().size() != Y.getRanges().size())
    return X.getRanges().size() < Y.getRanges().size();

  const SourceManager &SM = XL.getManager();

  for (unsigned i = 0, n = X.getRanges().size(); i < n; ++i) {
    SourceRange XR = X.getRanges()[i];
    SourceRange YR = Y.getRanges()[i];
    if (XR != YR) {
      if (XR.getBegin() != YR.getBegin())
        return SM.isBeforeInTranslationUnit(XR.getBegin(), YR.getBegin());
      return SM.isBeforeInTranslationUnit(XR.getEnd(), YR.getEnd());
    }
  }

  switch (X.getKind()) {
    case PathDiagnosticPiece::ControlFlow:
      return compareControlFlow(cast<PathDiagnosticControlFlowPiece>(X),
                                cast<PathDiagnosticControlFlowPiece>(Y));
    case PathDiagnosticPiece::Macro:
      return compareMacro(cast<PathDiagnosticMacroPiece>(X),
                          cast<PathDiagnosticMacroPiece>(Y));
    case PathDiagnosticPiece::Call:
      return compareCall(cast<PathDiagnosticCallPiece>(X),
                         cast<PathDiagnosticCallPiece>(Y));
    case PathDiagnosticPiece::Event:
    case PathDiagnosticPiece::Note:
    case PathDiagnosticPiece::PopUp:
      return std::nullopt;
  }
  llvm_unreachable("all cases handled");
}

static std::optional<bool> comparePath(const PathPieces &X,
                                       const PathPieces &Y) {
  if (X.size() != Y.size())
    return X.size() < Y.size();

  PathPieces::const_iterator X_I = X.begin(), X_end = X.end();
  PathPieces::const_iterator Y_I = Y.begin(), Y_end = Y.end();

  for (; X_I != X_end && Y_I != Y_end; ++X_I, ++Y_I)
    if (std::optional<bool> b = comparePiece(**X_I, **Y_I))
      return *b;

  return std::nullopt;
}

static bool compareCrossTUSourceLocs(FullSourceLoc XL, FullSourceLoc YL) {
  if (XL.isInvalid() && YL.isValid())
    return true;
  if (XL.isValid() && YL.isInvalid())
    return false;
  std::pair<FileID, unsigned> XOffs = XL.getDecomposedLoc();
  std::pair<FileID, unsigned> YOffs = YL.getDecomposedLoc();
  const SourceManager &SM = XL.getManager();
  std::pair<bool, bool> InSameTU = SM.isInTheSameTranslationUnit(XOffs, YOffs);
  if (InSameTU.first)
    return XL.isBeforeInTranslationUnitThan(YL);
  OptionalFileEntryRef XFE =
      SM.getFileEntryRefForID(XL.getSpellingLoc().getFileID());
  OptionalFileEntryRef YFE =
      SM.getFileEntryRefForID(YL.getSpellingLoc().getFileID());
  if (!XFE || !YFE)
    return XFE && !YFE;
  int NameCmp = XFE->getName().compare(YFE->getName());
  if (NameCmp != 0)
    return NameCmp < 0;
  // Last resort: Compare raw file IDs that are possibly expansions.
  return XL.getFileID() < YL.getFileID();
}

static bool compare(const PathDiagnostic &X, const PathDiagnostic &Y) {
  FullSourceLoc XL = X.getLocation().asLocation();
  FullSourceLoc YL = Y.getLocation().asLocation();
  if (XL != YL)
    return compareCrossTUSourceLocs(XL, YL);
  FullSourceLoc XUL = X.getUniqueingLoc().asLocation();
  FullSourceLoc YUL = Y.getUniqueingLoc().asLocation();
  if (XUL != YUL)
    return compareCrossTUSourceLocs(XUL, YUL);
  if (X.getBugType() != Y.getBugType())
    return X.getBugType() < Y.getBugType();
  if (X.getCategory() != Y.getCategory())
    return X.getCategory() < Y.getCategory();
  if (X.getVerboseDescription() != Y.getVerboseDescription())
    return X.getVerboseDescription() < Y.getVerboseDescription();
  if (X.getShortDescription() != Y.getShortDescription())
    return X.getShortDescription() < Y.getShortDescription();
  auto CompareDecls = [&XL](const Decl *D1,
                            const Decl *D2) -> std::optional<bool> {
    if (D1 == D2)
      return std::nullopt;
    if (!D1)
      return true;
    if (!D2)
      return false;
    SourceLocation D1L = D1->getLocation();
    SourceLocation D2L = D2->getLocation();
    if (D1L != D2L) {
      const SourceManager &SM = XL.getManager();
      return compareCrossTUSourceLocs(FullSourceLoc(D1L, SM),
                                      FullSourceLoc(D2L, SM));
    }
    return std::nullopt;
  };
  if (auto Result = CompareDecls(X.getDeclWithIssue(), Y.getDeclWithIssue()))
    return *Result;
  if (XUL.isValid()) {
    if (auto Result = CompareDecls(X.getUniqueingDecl(), Y.getUniqueingDecl()))
      return *Result;
  }
  PathDiagnostic::meta_iterator XI = X.meta_begin(), XE = X.meta_end();
  PathDiagnostic::meta_iterator YI = Y.meta_begin(), YE = Y.meta_end();
  if (XE - XI != YE - YI)
    return (XE - XI) < (YE - YI);
  for ( ; XI != XE ; ++XI, ++YI) {
    if (*XI != *YI)
      return (*XI) < (*YI);
  }
  return *comparePath(X.path, Y.path);
}

void PathDiagnosticConsumer::FlushDiagnostics(
                                     PathDiagnosticConsumer::FilesMade *Files) {
  if (flushed)
    return;

  flushed = true;

  std::vector<const PathDiagnostic *> BatchDiags;
  for (const auto &D : Diags)
    BatchDiags.push_back(&D);

  // Sort the diagnostics so that they are always emitted in a deterministic
  // order.
  int (*Comp)(const PathDiagnostic *const *, const PathDiagnostic *const *) =
      [](const PathDiagnostic *const *X, const PathDiagnostic *const *Y) {
        assert(*X != *Y && "PathDiagnostics not uniqued!");
        if (compare(**X, **Y))
          return -1;
        assert(compare(**Y, **X) && "Not a total order!");
        return 1;
      };
  array_pod_sort(BatchDiags.begin(), BatchDiags.end(), Comp);

  FlushDiagnosticsImpl(BatchDiags, Files);

  // Delete the flushed diagnostics.
  for (const auto D : BatchDiags)
    delete D;

  // Clear out the FoldingSet.
  Diags.clear();
}

PathDiagnosticConsumer::FilesMade::~FilesMade() {
  for (auto It = Set.begin(); It != Set.end();)
    (It++)->~PDFileEntry();
}

void PathDiagnosticConsumer::FilesMade::addDiagnostic(const PathDiagnostic &PD,
                                                      StringRef ConsumerName,
                                                      StringRef FileName) {
  llvm::FoldingSetNodeID NodeID;
  NodeID.Add(PD);
  void *InsertPos;
  PDFileEntry *Entry = Set.FindNodeOrInsertPos(NodeID, InsertPos);
  if (!Entry) {
    Entry = Alloc.Allocate<PDFileEntry>();
    Entry = new (Entry) PDFileEntry(NodeID);
    Set.InsertNode(Entry, InsertPos);
  }

  // Allocate persistent storage for the file name.
  char *FileName_cstr = (char*) Alloc.Allocate(FileName.size(), 1);
  memcpy(FileName_cstr, FileName.data(), FileName.size());

  Entry->files.push_back(std::make_pair(ConsumerName,
                                        StringRef(FileName_cstr,
                                                  FileName.size())));
}

PathDiagnosticConsumer::PDFileEntry::ConsumerFiles *
PathDiagnosticConsumer::FilesMade::getFiles(const PathDiagnostic &PD) {
  llvm::FoldingSetNodeID NodeID;
  NodeID.Add(PD);
  void *InsertPos;
  PDFileEntry *Entry = Set.FindNodeOrInsertPos(NodeID, InsertPos);
  if (!Entry)
    return nullptr;
  return &Entry->files;
}

//===----------------------------------------------------------------------===//
// PathDiagnosticLocation methods.
//===----------------------------------------------------------------------===//

SourceLocation PathDiagnosticLocation::getValidSourceLocation(
    const Stmt *S, LocationOrAnalysisDeclContext LAC, bool UseEndOfStatement) {
  SourceLocation L = UseEndOfStatement ? S->getEndLoc() : S->getBeginLoc();
  assert(!LAC.isNull() &&
         "A valid LocationContext or AnalysisDeclContext should be passed to "
         "PathDiagnosticLocation upon creation.");

  // S might be a temporary statement that does not have a location in the
  // source code, so find an enclosing statement and use its location.
  if (!L.isValid()) {
    AnalysisDeclContext *ADC;
    if (LAC.is<const LocationContext*>())
      ADC = LAC.get<const LocationContext*>()->getAnalysisDeclContext();
    else
      ADC = LAC.get<AnalysisDeclContext*>();

    ParentMap &PM = ADC->getParentMap();

    const Stmt *Parent = S;
    do {
      Parent = PM.getParent(Parent);

      // In rare cases, we have implicit top-level expressions,
      // such as arguments for implicit member initializers.
      // In this case, fall back to the start of the body (even if we were
      // asked for the statement end location).
      if (!Parent) {
        const Stmt *Body = ADC->getBody();
        if (Body)
          L = Body->getBeginLoc();
        else
          L = ADC->getDecl()->getEndLoc();
        break;
      }

      L = UseEndOfStatement ? Parent->getEndLoc() : Parent->getBeginLoc();
    } while (!L.isValid());
  }

  // FIXME: Ironically, this assert actually fails in some cases.
  //assert(L.isValid());
  return L;
}

static PathDiagnosticLocation
getLocationForCaller(const StackFrameContext *SFC,
                     const LocationContext *CallerCtx,
                     const SourceManager &SM) {
  const CFGBlock &Block = *SFC->getCallSiteBlock();
  CFGElement Source = Block[SFC->getIndex()];

  switch (Source.getKind()) {
  case CFGElement::Statement:
  case CFGElement::Constructor:
  case CFGElement::CXXRecordTypedCall:
    return PathDiagnosticLocation(Source.castAs<CFGStmt>().getStmt(),
                                  SM, CallerCtx);
  case CFGElement::Initializer: {
    const CFGInitializer &Init = Source.castAs<CFGInitializer>();
    return PathDiagnosticLocation(Init.getInitializer()->getInit(),
                                  SM, CallerCtx);
  }
  case CFGElement::AutomaticObjectDtor: {
    const CFGAutomaticObjDtor &Dtor = Source.castAs<CFGAutomaticObjDtor>();
    return PathDiagnosticLocation::createEnd(Dtor.getTriggerStmt(),
                                             SM, CallerCtx);
  }
  case CFGElement::DeleteDtor: {
    const CFGDeleteDtor &Dtor = Source.castAs<CFGDeleteDtor>();
    return PathDiagnosticLocation(Dtor.getDeleteExpr(), SM, CallerCtx);
  }
  case CFGElement::BaseDtor:
  case CFGElement::MemberDtor: {
    const AnalysisDeclContext *CallerInfo = CallerCtx->getAnalysisDeclContext();
    if (const Stmt *CallerBody = CallerInfo->getBody())
      return PathDiagnosticLocation::createEnd(CallerBody, SM, CallerCtx);
    return PathDiagnosticLocation::create(CallerInfo->getDecl(), SM);
  }
  case CFGElement::NewAllocator: {
    const CFGNewAllocator &Alloc = Source.castAs<CFGNewAllocator>();
    return PathDiagnosticLocation(Alloc.getAllocatorExpr(), SM, CallerCtx);
  }
  case CFGElement::TemporaryDtor: {
    // Temporary destructors are for temporaries. They die immediately at around
    // the location of CXXBindTemporaryExpr. If they are lifetime-extended,
    // they'd be dealt with via an AutomaticObjectDtor instead.
    const auto &Dtor = Source.castAs<CFGTemporaryDtor>();
    return PathDiagnosticLocation::createEnd(Dtor.getBindTemporaryExpr(), SM,
                                             CallerCtx);
  }
  case CFGElement::ScopeBegin:
  case CFGElement::ScopeEnd:
  case CFGElement::CleanupFunction:
    llvm_unreachable("not yet implemented!");
  case CFGElement::LifetimeEnds:
  case CFGElement::LoopExit:
    llvm_unreachable("CFGElement kind should not be on callsite!");
  }

  llvm_unreachable("Unknown CFGElement kind");
}

PathDiagnosticLocation
PathDiagnosticLocation::createBegin(const Decl *D,
                                    const SourceManager &SM) {
  return PathDiagnosticLocation(D->getBeginLoc(), SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createBegin(const Stmt *S,
                                    const SourceManager &SM,
                                    LocationOrAnalysisDeclContext LAC) {
  assert(S && "Statement cannot be null");
  return PathDiagnosticLocation(getValidSourceLocation(S, LAC),
                                SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createEnd(const Stmt *S,
                                  const SourceManager &SM,
                                  LocationOrAnalysisDeclContext LAC) {
  if (const auto *CS = dyn_cast<CompoundStmt>(S))
    return createEndBrace(CS, SM);
  return PathDiagnosticLocation(getValidSourceLocation(S, LAC, /*End=*/true),
                                SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createOperatorLoc(const BinaryOperator *BO,
                                          const SourceManager &SM) {
  return PathDiagnosticLocation(BO->getOperatorLoc(), SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createConditionalColonLoc(
                                            const ConditionalOperator *CO,
                                            const SourceManager &SM) {
  return PathDiagnosticLocation(CO->getColonLoc(), SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createMemberLoc(const MemberExpr *ME,
                                        const SourceManager &SM) {

  assert(ME->getMemberLoc().isValid() || ME->getBeginLoc().isValid());

  // In some cases, getMemberLoc isn't valid -- in this case we'll return with
  // some other related valid SourceLocation.
  if (ME->getMemberLoc().isValid())
    return PathDiagnosticLocation(ME->getMemberLoc(), SM, SingleLocK);

  return PathDiagnosticLocation(ME->getBeginLoc(), SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createBeginBrace(const CompoundStmt *CS,
                                         const SourceManager &SM) {
  SourceLocation L = CS->getLBracLoc();
  return PathDiagnosticLocation(L, SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createEndBrace(const CompoundStmt *CS,
                                       const SourceManager &SM) {
  SourceLocation L = CS->getRBracLoc();
  return PathDiagnosticLocation(L, SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::createDeclBegin(const LocationContext *LC,
                                        const SourceManager &SM) {
  // FIXME: Should handle CXXTryStmt if analyser starts supporting C++.
  if (const auto *CS = dyn_cast_or_null<CompoundStmt>(LC->getDecl()->getBody()))
    if (!CS->body_empty()) {
      SourceLocation Loc = (*CS->body_begin())->getBeginLoc();
      return PathDiagnosticLocation(Loc, SM, SingleLocK);
    }

  return PathDiagnosticLocation();
}

PathDiagnosticLocation
PathDiagnosticLocation::createDeclEnd(const LocationContext *LC,
                                      const SourceManager &SM) {
  SourceLocation L = LC->getDecl()->getBodyRBrace();
  return PathDiagnosticLocation(L, SM, SingleLocK);
}

PathDiagnosticLocation
PathDiagnosticLocation::create(const ProgramPoint& P,
                               const SourceManager &SMng) {
  const Stmt* S = nullptr;
  if (std::optional<BlockEdge> BE = P.getAs<BlockEdge>()) {
    const CFGBlock *BSrc = BE->getSrc();
    if (BSrc->getTerminator().isVirtualBaseBranch()) {
      // TODO: VirtualBaseBranches should also appear for destructors.
      // In this case we should put the diagnostic at the end of decl.
      return PathDiagnosticLocation::createBegin(
          P.getLocationContext()->getDecl(), SMng);

    } else {
      S = BSrc->getTerminatorCondition();
      if (!S) {
        // If the BlockEdge has no terminator condition statement but its
        // source is the entry of the CFG (e.g. a checker crated the branch at
        // the beginning of a function), use the function's declaration instead.
        assert(BSrc == &BSrc->getParent()->getEntry() && "CFGBlock has no "
               "TerminatorCondition and is not the enrty block of the CFG");
        return PathDiagnosticLocation::createBegin(
            P.getLocationContext()->getDecl(), SMng);
      }
    }
  } else if (std::optional<StmtPoint> SP = P.getAs<StmtPoint>()) {
    S = SP->getStmt();
    if (P.getAs<PostStmtPurgeDeadSymbols>())
      return PathDiagnosticLocation::createEnd(S, SMng, P.getLocationContext());
  } else if (std::optional<PostInitializer> PIP = P.getAs<PostInitializer>()) {
    return PathDiagnosticLocation(PIP->getInitializer()->getSourceLocation(),
                                  SMng);
  } else if (std::optional<PreImplicitCall> PIC = P.getAs<PreImplicitCall>()) {
    return PathDiagnosticLocation(PIC->getLocation(), SMng);
  } else if (std::optional<PostImplicitCall> PIE =
                 P.getAs<PostImplicitCall>()) {
    return PathDiagnosticLocation(PIE->getLocation(), SMng);
  } else if (std::optional<CallEnter> CE = P.getAs<CallEnter>()) {
    return getLocationForCaller(CE->getCalleeContext(),
                                CE->getLocationContext(),
                                SMng);
  } else if (std::optional<CallExitEnd> CEE = P.getAs<CallExitEnd>()) {
    return getLocationForCaller(CEE->getCalleeContext(),
                                CEE->getLocationContext(),
                                SMng);
  } else if (auto CEB = P.getAs<CallExitBegin>()) {
    if (const ReturnStmt *RS = CEB->getReturnStmt())
      return PathDiagnosticLocation::createBegin(RS, SMng,
                                                 CEB->getLocationContext());
    return PathDiagnosticLocation(
        CEB->getLocationContext()->getDecl()->getSourceRange().getEnd(), SMng);
  } else if (std::optional<BlockEntrance> BE = P.getAs<BlockEntrance>()) {
    if (std::optional<CFGElement> BlockFront = BE->getFirstElement()) {
      if (auto StmtElt = BlockFront->getAs<CFGStmt>()) {
        return PathDiagnosticLocation(StmtElt->getStmt()->getBeginLoc(), SMng);
      } else if (auto NewAllocElt = BlockFront->getAs<CFGNewAllocator>()) {
        return PathDiagnosticLocation(
            NewAllocElt->getAllocatorExpr()->getBeginLoc(), SMng);
      }
      llvm_unreachable("Unexpected CFG element at front of block");
    }

    return PathDiagnosticLocation(
        BE->getBlock()->getTerminatorStmt()->getBeginLoc(), SMng);
  } else if (std::optional<FunctionExitPoint> FE =
                 P.getAs<FunctionExitPoint>()) {
    return PathDiagnosticLocation(FE->getStmt(), SMng,
                                  FE->getLocationContext());
  } else {
    llvm_unreachable("Unexpected ProgramPoint");
  }

  return PathDiagnosticLocation(S, SMng, P.getLocationContext());
}

PathDiagnosticLocation PathDiagnosticLocation::createSingleLocation(
                                           const PathDiagnosticLocation &PDL) {
  FullSourceLoc L = PDL.asLocation();
  return PathDiagnosticLocation(L, L.getManager(), SingleLocK);
}

FullSourceLoc
  PathDiagnosticLocation::genLocation(SourceLocation L,
                                      LocationOrAnalysisDeclContext LAC) const {
  assert(isValid());
  // Note that we want a 'switch' here so that the compiler can warn us in
  // case we add more cases.
  switch (K) {
    case SingleLocK:
    case RangeK:
      break;
    case StmtK:
      // Defensive checking.
      if (!S)
        break;
      return FullSourceLoc(getValidSourceLocation(S, LAC),
                           const_cast<SourceManager&>(*SM));
    case DeclK:
      // Defensive checking.
      if (!D)
        break;
      return FullSourceLoc(D->getLocation(), const_cast<SourceManager&>(*SM));
  }

  return FullSourceLoc(L, const_cast<SourceManager&>(*SM));
}

PathDiagnosticRange
  PathDiagnosticLocation::genRange(LocationOrAnalysisDeclContext LAC) const {
  assert(isValid());
  // Note that we want a 'switch' here so that the compiler can warn us in
  // case we add more cases.
  switch (K) {
    case SingleLocK:
      return PathDiagnosticRange(SourceRange(Loc,Loc), true);
    case RangeK:
      break;
    case StmtK: {
      const Stmt *S = asStmt();
      switch (S->getStmtClass()) {
        default:
          break;
        case Stmt::DeclStmtClass: {
          const auto *DS = cast<DeclStmt>(S);
          if (DS->isSingleDecl()) {
            // Should always be the case, but we'll be defensive.
            return SourceRange(DS->getBeginLoc(),
                               DS->getSingleDecl()->getLocation());
          }
          break;
        }
          // FIXME: Provide better range information for different
          //  terminators.
        case Stmt::IfStmtClass:
        case Stmt::WhileStmtClass:
        case Stmt::DoStmtClass:
        case Stmt::ForStmtClass:
        case Stmt::ChooseExprClass:
        case Stmt::IndirectGotoStmtClass:
        case Stmt::SwitchStmtClass:
        case Stmt::BinaryConditionalOperatorClass:
        case Stmt::ConditionalOperatorClass:
        case Stmt::ObjCForCollectionStmtClass: {
          SourceLocation L = getValidSourceLocation(S, LAC);
          return SourceRange(L, L);
        }
      }
      SourceRange R = S->getSourceRange();
      if (R.isValid())
        return R;
      break;
    }
    case DeclK:
      if (const auto *MD = dyn_cast<ObjCMethodDecl>(D))
        return MD->getSourceRange();
      if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
        if (Stmt *Body = FD->getBody())
          return Body->getSourceRange();
      }
      else {
        SourceLocation L = D->getLocation();
        return PathDiagnosticRange(SourceRange(L, L), true);
      }
  }

  return SourceRange(Loc, Loc);
}

void PathDiagnosticLocation::flatten() {
  if (K == StmtK) {
    K = RangeK;
    S = nullptr;
    D = nullptr;
  }
  else if (K == DeclK) {
    K = SingleLocK;
    S = nullptr;
    D = nullptr;
  }
}

//===----------------------------------------------------------------------===//
// Manipulation of PathDiagnosticCallPieces.
//===----------------------------------------------------------------------===//

std::shared_ptr<PathDiagnosticCallPiece>
PathDiagnosticCallPiece::construct(const CallExitEnd &CE,
                                   const SourceManager &SM) {
  const Decl *caller = CE.getLocationContext()->getDecl();
  PathDiagnosticLocation pos = getLocationForCaller(CE.getCalleeContext(),
                                                    CE.getLocationContext(),
                                                    SM);
  return std::shared_ptr<PathDiagnosticCallPiece>(
      new PathDiagnosticCallPiece(caller, pos));
}

PathDiagnosticCallPiece *
PathDiagnosticCallPiece::construct(PathPieces &path,
                                   const Decl *caller) {
  std::shared_ptr<PathDiagnosticCallPiece> C(
      new PathDiagnosticCallPiece(path, caller));
  path.clear();
  auto *R = C.get();
  path.push_front(std::move(C));
  return R;
}

void PathDiagnosticCallPiece::setCallee(const CallEnter &CE,
                                        const SourceManager &SM) {
  const StackFrameContext *CalleeCtx = CE.getCalleeContext();
  Callee = CalleeCtx->getDecl();

  callEnterWithin = PathDiagnosticLocation::createBegin(Callee, SM);
  callEnter = getLocationForCaller(CalleeCtx, CE.getLocationContext(), SM);

  // Autosynthesized property accessors are special because we'd never
  // pop back up to non-autosynthesized code until we leave them.
  // This is not generally true for autosynthesized callees, which may call
  // non-autosynthesized callbacks.
  // Unless set here, the IsCalleeAnAutosynthesizedPropertyAccessor flag
  // defaults to false.
  if (const auto *MD = dyn_cast<ObjCMethodDecl>(Callee))
    IsCalleeAnAutosynthesizedPropertyAccessor = (
        MD->isPropertyAccessor() &&
        CalleeCtx->getAnalysisDeclContext()->isBodyAutosynthesized());
}

static void describeTemplateParameters(raw_ostream &Out,
                                       const ArrayRef<TemplateArgument> TAList,
                                       const LangOptions &LO,
                                       StringRef Prefix = StringRef(),
                                       StringRef Postfix = StringRef());

static void describeTemplateParameter(raw_ostream &Out,
                                      const TemplateArgument &TArg,
                                      const LangOptions &LO) {

  if (TArg.getKind() == TemplateArgument::ArgKind::Pack) {
    describeTemplateParameters(Out, TArg.getPackAsArray(), LO);
  } else {
    TArg.print(PrintingPolicy(LO), Out, /*IncludeType*/ true);
  }
}

static void describeTemplateParameters(raw_ostream &Out,
                                       const ArrayRef<TemplateArgument> TAList,
                                       const LangOptions &LO,
                                       StringRef Prefix, StringRef Postfix) {
  if (TAList.empty())
    return;

  Out << Prefix;
  for (int I = 0, Last = TAList.size() - 1; I != Last; ++I) {
    describeTemplateParameter(Out, TAList[I], LO);
    Out << ", ";
  }
  describeTemplateParameter(Out, TAList[TAList.size() - 1], LO);
  Out << Postfix;
}

static void describeClass(raw_ostream &Out, const CXXRecordDecl *D,
                          StringRef Prefix = StringRef()) {
  if (!D->getIdentifier())
    return;
  Out << Prefix << '\'' << *D;
  if (const auto T = dyn_cast<ClassTemplateSpecializationDecl>(D))
    describeTemplateParameters(Out, T->getTemplateArgs().asArray(),
                               D->getLangOpts(), "<", ">");

  Out << '\'';
}

static bool describeCodeDecl(raw_ostream &Out, const Decl *D,
                             bool ExtendedDescription,
                             StringRef Prefix = StringRef()) {
  if (!D)
    return false;

  if (isa<BlockDecl>(D)) {
    if (ExtendedDescription)
      Out << Prefix << "anonymous block";
    return ExtendedDescription;
  }

  if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
    Out << Prefix;
    if (ExtendedDescription && !MD->isUserProvided()) {
      if (MD->isExplicitlyDefaulted())
        Out << "defaulted ";
      else
        Out << "implicit ";
    }

    if (const auto *CD = dyn_cast<CXXConstructorDecl>(MD)) {
      if (CD->isDefaultConstructor())
        Out << "default ";
      else if (CD->isCopyConstructor())
        Out << "copy ";
      else if (CD->isMoveConstructor())
        Out << "move ";

      Out << "constructor";
      describeClass(Out, MD->getParent(), " for ");
    } else if (isa<CXXDestructorDecl>(MD)) {
      if (!MD->isUserProvided()) {
        Out << "destructor";
        describeClass(Out, MD->getParent(), " for ");
      } else {
        // Use ~Foo for explicitly-written destructors.
        Out << "'" << *MD << "'";
      }
    } else if (MD->isCopyAssignmentOperator()) {
        Out << "copy assignment operator";
        describeClass(Out, MD->getParent(), " for ");
    } else if (MD->isMoveAssignmentOperator()) {
        Out << "move assignment operator";
        describeClass(Out, MD->getParent(), " for ");
    } else {
      if (MD->getParent()->getIdentifier())
        Out << "'" << *MD->getParent() << "::" << *MD << "'";
      else
        Out << "'" << *MD << "'";
    }

    return true;
  }

  Out << Prefix << '\'' << cast<NamedDecl>(*D);

  // Adding template parameters.
  if (const auto FD = dyn_cast<FunctionDecl>(D))
    if (const TemplateArgumentList *TAList =
                                    FD->getTemplateSpecializationArgs())
      describeTemplateParameters(Out, TAList->asArray(), FD->getLangOpts(), "<",
                                 ">");

  Out << '\'';
  return true;
}

std::shared_ptr<PathDiagnosticEventPiece>
PathDiagnosticCallPiece::getCallEnterEvent() const {
  // We do not produce call enters and call exits for autosynthesized property
  // accessors. We do generally produce them for other functions coming from
  // the body farm because they may call callbacks that bring us back into
  // visible code.
  if (!Callee || IsCalleeAnAutosynthesizedPropertyAccessor)
    return nullptr;

  SmallString<256> buf;
  llvm::raw_svector_ostream Out(buf);

  Out << "Calling ";
  describeCodeDecl(Out, Callee, /*ExtendedDescription=*/true);

  assert(callEnter.asLocation().isValid());
  return std::make_shared<PathDiagnosticEventPiece>(callEnter, Out.str());
}

std::shared_ptr<PathDiagnosticEventPiece>
PathDiagnosticCallPiece::getCallEnterWithinCallerEvent() const {
  if (!callEnterWithin.asLocation().isValid())
    return nullptr;
  if (Callee->isImplicit() || !Callee->hasBody())
    return nullptr;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(Callee))
    if (MD->isDefaulted())
      return nullptr;

  SmallString<256> buf;
  llvm::raw_svector_ostream Out(buf);

  Out << "Entered call";
  describeCodeDecl(Out, Caller, /*ExtendedDescription=*/false, " from ");

  return std::make_shared<PathDiagnosticEventPiece>(callEnterWithin, Out.str());
}

std::shared_ptr<PathDiagnosticEventPiece>
PathDiagnosticCallPiece::getCallExitEvent() const {
  // We do not produce call enters and call exits for autosynthesized property
  // accessors. We do generally produce them for other functions coming from
  // the body farm because they may call callbacks that bring us back into
  // visible code.
  if (NoExit || IsCalleeAnAutosynthesizedPropertyAccessor)
    return nullptr;

  SmallString<256> buf;
  llvm::raw_svector_ostream Out(buf);

  if (!CallStackMessage.empty()) {
    Out << CallStackMessage;
  } else {
    bool DidDescribe = describeCodeDecl(Out, Callee,
                                        /*ExtendedDescription=*/false,
                                        "Returning from ");
    if (!DidDescribe)
      Out << "Returning to caller";
  }

  assert(callReturn.asLocation().isValid());
  return std::make_shared<PathDiagnosticEventPiece>(callReturn, Out.str());
}

static void compute_path_size(const PathPieces &pieces, unsigned &size) {
  for (const auto &I : pieces) {
    const PathDiagnosticPiece *piece = I.get();
    if (const auto *cp = dyn_cast<PathDiagnosticCallPiece>(piece))
      compute_path_size(cp->path, size);
    else
      ++size;
  }
}

unsigned PathDiagnostic::full_size() {
  unsigned size = 0;
  compute_path_size(path, size);
  return size;
}

//===----------------------------------------------------------------------===//
// FoldingSet profiling methods.
//===----------------------------------------------------------------------===//

void PathDiagnosticLocation::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.Add(Range.getBegin());
  ID.Add(Range.getEnd());
  ID.Add(static_cast<const SourceLocation &>(Loc));
}

void PathDiagnosticPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.AddInteger((unsigned) getKind());
  ID.AddString(str);
  // FIXME: Add profiling support for code hints.
  ID.AddInteger((unsigned) getDisplayHint());
  ArrayRef<SourceRange> Ranges = getRanges();
  for (const auto &I : Ranges) {
    ID.Add(I.getBegin());
    ID.Add(I.getEnd());
  }
}

void PathDiagnosticCallPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticPiece::Profile(ID);
  for (const auto &I : path)
    ID.Add(*I);
}

void PathDiagnosticSpotPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticPiece::Profile(ID);
  ID.Add(Pos);
}

void PathDiagnosticControlFlowPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticPiece::Profile(ID);
  for (const auto &I : *this)
    ID.Add(I);
}

void PathDiagnosticMacroPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticSpotPiece::Profile(ID);
  for (const auto &I : subPieces)
    ID.Add(*I);
}

void PathDiagnosticNotePiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticSpotPiece::Profile(ID);
}

void PathDiagnosticPopUpPiece::Profile(llvm::FoldingSetNodeID &ID) const {
  PathDiagnosticSpotPiece::Profile(ID);
}

void PathDiagnostic::Profile(llvm::FoldingSetNodeID &ID) const {
  ID.Add(getLocation());
  ID.Add(getUniqueingLoc());
  ID.AddString(BugType);
  ID.AddString(VerboseDesc);
  ID.AddString(Category);
}

void PathDiagnostic::FullProfile(llvm::FoldingSetNodeID &ID) const {
  Profile(ID);
  for (const auto &I : path)
    ID.Add(*I);
  for (meta_iterator I = meta_begin(), E = meta_end(); I != E; ++I)
    ID.AddString(*I);
}

LLVM_DUMP_METHOD void PathPieces::dump() const {
  unsigned index = 0;
  for (PathPieces::const_iterator I = begin(), E = end(); I != E; ++I) {
    llvm::errs() << "[" << index++ << "]  ";
    (*I)->dump();
    llvm::errs() << "\n";
  }
}

LLVM_DUMP_METHOD void PathDiagnosticCallPiece::dump() const {
  llvm::errs() << "CALL\n--------------\n";

  if (const Stmt *SLoc = getLocation().getStmtOrNull())
    SLoc->dump();
  else if (const auto *ND = dyn_cast_or_null<NamedDecl>(getCallee()))
    llvm::errs() << *ND << "\n";
  else
    getLocation().dump();
}

LLVM_DUMP_METHOD void PathDiagnosticEventPiece::dump() const {
  llvm::errs() << "EVENT\n--------------\n";
  llvm::errs() << getString() << "\n";
  llvm::errs() << " ---- at ----\n";
  getLocation().dump();
}

LLVM_DUMP_METHOD void PathDiagnosticControlFlowPiece::dump() const {
  llvm::errs() << "CONTROL\n--------------\n";
  getStartLocation().dump();
  llvm::errs() << " ---- to ----\n";
  getEndLocation().dump();
}

LLVM_DUMP_METHOD void PathDiagnosticMacroPiece::dump() const {
  llvm::errs() << "MACRO\n--------------\n";
  // FIXME: Print which macro is being invoked.
}

LLVM_DUMP_METHOD void PathDiagnosticNotePiece::dump() const {
  llvm::errs() << "NOTE\n--------------\n";
  llvm::errs() << getString() << "\n";
  llvm::errs() << " ---- at ----\n";
  getLocation().dump();
}

LLVM_DUMP_METHOD void PathDiagnosticPopUpPiece::dump() const {
  llvm::errs() << "POP-UP\n--------------\n";
  llvm::errs() << getString() << "\n";
  llvm::errs() << " ---- at ----\n";
  getLocation().dump();
}

LLVM_DUMP_METHOD void PathDiagnosticLocation::dump() const {
  if (!isValid()) {
    llvm::errs() << "<INVALID>\n";
    return;
  }

  switch (K) {
  case RangeK:
    // FIXME: actually print the range.
    llvm::errs() << "<range>\n";
    break;
  case SingleLocK:
    asLocation().dump();
    llvm::errs() << "\n";
    break;
  case StmtK:
    if (S)
      S->dump();
    else
      llvm::errs() << "<NULL STMT>\n";
    break;
  case DeclK:
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(D))
      llvm::errs() << *ND << "\n";
    else if (isa<BlockDecl>(D))
      // FIXME: Make this nicer.
      llvm::errs() << "<block>\n";
    else if (D)
      llvm::errs() << "<unknown decl>\n";
    else
      llvm::errs() << "<NULL DECL>\n";
    break;
  }
}
