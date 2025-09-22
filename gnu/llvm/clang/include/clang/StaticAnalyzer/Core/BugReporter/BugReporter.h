//===- BugReporter.h - Generate PathDiagnostics -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines BugReporter, a utility class for generating
//  PathDiagnostics for analyses based on ProgramState.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTER_H
#define LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTER_H

#include "clang/Analysis/PathDiagnostic.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugSuppression.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ExplodedGraph.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SymExpr.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/ilist.h"
#include "llvm/ADT/ilist_node.h"
#include "llvm/ADT/iterator_range.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace clang {

class AnalyzerOptions;
class ASTContext;
class Decl;
class LocationContext;
class SourceManager;
class Stmt;

namespace ento {

class BugType;
class CheckerBase;
class ExplodedGraph;
class ExplodedNode;
class ExprEngine;
class MemRegion;

//===----------------------------------------------------------------------===//
// Interface for individual bug reports.
//===----------------------------------------------------------------------===//

/// A mapping from diagnostic consumers to the diagnostics they should
/// consume.
using DiagnosticForConsumerMapTy =
    llvm::DenseMap<PathDiagnosticConsumer *, std::unique_ptr<PathDiagnostic>>;

/// Interface for classes constructing Stack hints.
///
/// If a PathDiagnosticEvent occurs in a different frame than the final
/// diagnostic the hints can be used to summarize the effect of the call.
class StackHintGenerator {
public:
  virtual ~StackHintGenerator() = 0;

  /// Construct the Diagnostic message for the given ExplodedNode.
  virtual std::string getMessage(const ExplodedNode *N) = 0;
};

/// Constructs a Stack hint for the given symbol.
///
/// The class knows how to construct the stack hint message based on
/// traversing the CallExpr associated with the call and checking if the given
/// symbol is returned or is one of the arguments.
/// The hint can be customized by redefining 'getMessageForX()' methods.
class StackHintGeneratorForSymbol : public StackHintGenerator {
private:
  SymbolRef Sym;
  std::string Msg;

public:
  StackHintGeneratorForSymbol(SymbolRef S, StringRef M) : Sym(S), Msg(M) {}
  ~StackHintGeneratorForSymbol() override = default;

  /// Search the call expression for the symbol Sym and dispatch the
  /// 'getMessageForX()' methods to construct a specific message.
  std::string getMessage(const ExplodedNode *N) override;

  /// Produces the message of the following form:
  ///   'Msg via Nth parameter'
  virtual std::string getMessageForArg(const Expr *ArgE, unsigned ArgIndex);

  virtual std::string getMessageForReturn(const CallExpr *CallExpr) {
    return Msg;
  }

  virtual std::string getMessageForSymbolNotFound() {
    return Msg;
  }
};

/// This class provides an interface through which checkers can create
/// individual bug reports.
class BugReport {
public:
  enum class Kind { Basic, PathSensitive };

protected:
  friend class BugReportEquivClass;
  friend class BugReporter;

  Kind K;
  const BugType& BT;
  std::string ShortDescription;
  std::string Description;

  SmallVector<SourceRange, 4> Ranges;
  SmallVector<std::shared_ptr<PathDiagnosticNotePiece>, 4> Notes;
  SmallVector<FixItHint, 4> Fixits;

  BugReport(Kind kind, const BugType &bt, StringRef desc)
      : BugReport(kind, bt, "", desc) {}

  BugReport(Kind K, const BugType &BT, StringRef ShortDescription,
            StringRef Description)
      : K(K), BT(BT), ShortDescription(ShortDescription),
        Description(Description) {}

public:
  virtual ~BugReport() = default;

  Kind getKind() const { return K; }

  const BugType& getBugType() const { return BT; }

  /// A verbose warning message that is appropriate for displaying next to
  /// the source code that introduces the problem. The description should be
  /// at least a full sentence starting with a capital letter. The period at
  /// the end of the warning is traditionally omitted. If the description
  /// consists of multiple sentences, periods between the sentences are
  /// encouraged, but the period at the end of the description is still omitted.
  StringRef getDescription() const { return Description; }

  /// A short general warning message that is appropriate for displaying in
  /// the list of all reported bugs. It should describe what kind of bug is found
  /// but does not need to try to go into details of that specific bug.
  /// Grammatical conventions of getDescription() apply here as well.
  StringRef getShortDescription(bool UseFallback = true) const {
    if (ShortDescription.empty() && UseFallback)
      return Description;
    return ShortDescription;
  }

  /// The primary location of the bug report that points at the undesirable
  /// behavior in the code. UIs should attach the warning description to this
  /// location. The warning description should describe the bad behavior
  /// at this location.
  virtual PathDiagnosticLocation getLocation() const = 0;

  /// The smallest declaration that contains the bug location.
  /// This is purely cosmetic; the declaration can be displayed to the user
  /// but it does not affect whether the report is emitted.
  virtual const Decl *getDeclWithIssue() const = 0;

  /// Get the location on which the report should be uniqued. Two warnings are
  /// considered to be equivalent whenever they have the same bug types,
  /// descriptions, and uniqueing locations. Out of a class of equivalent
  /// warnings only one gets displayed to the user. For most warnings the
  /// uniqueing location coincides with their location, but sometimes
  /// it makes sense to use different locations. For example, a leak
  /// checker can place the warning at the location where the last reference
  /// to the leaking resource is dropped but at the same time unique the warning
  /// by where that resource is acquired (allocated).
  virtual PathDiagnosticLocation getUniqueingLocation() const = 0;

  /// Get the declaration that corresponds to (usually contains) the uniqueing
  /// location. This is not actively used for uniqueing, i.e. otherwise
  /// identical reports that have different uniqueing decls will be considered
  /// equivalent.
  virtual const Decl *getUniqueingDecl() const = 0;

  /// Add new item to the list of additional notes that need to be attached to
  /// this report. If the report is path-sensitive, these notes will not be
  /// displayed as part of the execution path explanation, but will be displayed
  /// separately. Use bug visitors if you need to add an extra path note.
  void addNote(StringRef Msg, const PathDiagnosticLocation &Pos,
               ArrayRef<SourceRange> Ranges = {}) {
    auto P = std::make_shared<PathDiagnosticNotePiece>(Pos, Msg);

    for (const auto &R : Ranges)
      P->addRange(R);

    Notes.push_back(std::move(P));
  }

  ArrayRef<std::shared_ptr<PathDiagnosticNotePiece>> getNotes() {
    return Notes;
  }

  /// Add a range to a bug report.
  ///
  /// Ranges are used to highlight regions of interest in the source code.
  /// They should be at the same source code line as the BugReport location.
  /// By default, the source range of the statement corresponding to the error
  /// node will be used; add a single invalid range to specify absence of
  /// ranges.
  void addRange(SourceRange R) {
    assert((R.isValid() || Ranges.empty()) && "Invalid range can only be used "
                           "to specify that the report does not have a range.");
    Ranges.push_back(R);
  }

  /// Get the SourceRanges associated with the report.
  virtual ArrayRef<SourceRange> getRanges() const {
    return Ranges;
  }

  /// Add a fix-it hint to the bug report.
  ///
  /// Fix-it hints are the suggested edits to the code that would resolve
  /// the problem explained by the bug report. Fix-it hints should be
  /// as conservative as possible because it is not uncommon for the user
  /// to blindly apply all fixits to their project. Note that it is very hard
  /// to produce a good fix-it hint for most path-sensitive warnings.
  void addFixItHint(const FixItHint &F) {
    Fixits.push_back(F);
  }

  llvm::ArrayRef<FixItHint> getFixits() const { return Fixits; }

  /// Reports are uniqued to ensure that we do not emit multiple diagnostics
  /// for each bug.
  virtual void Profile(llvm::FoldingSetNodeID& hash) const = 0;
};

class BasicBugReport : public BugReport {
  PathDiagnosticLocation Location;
  const Decl *DeclWithIssue = nullptr;

public:
  BasicBugReport(const BugType &bt, StringRef desc, PathDiagnosticLocation l)
      : BugReport(Kind::Basic, bt, desc), Location(l) {}

  static bool classof(const BugReport *R) {
    return R->getKind() == Kind::Basic;
  }

  PathDiagnosticLocation getLocation() const override {
    assert(Location.isValid());
    return Location;
  }

  const Decl *getDeclWithIssue() const override {
    return DeclWithIssue;
  }

  PathDiagnosticLocation getUniqueingLocation() const override {
    return getLocation();
  }

  const Decl *getUniqueingDecl() const override {
    return getDeclWithIssue();
  }

  /// Specifically set the Decl where an issue occurred. This isn't necessary
  /// for BugReports that cover a path as it will be automatically inferred.
  void setDeclWithIssue(const Decl *declWithIssue) {
    DeclWithIssue = declWithIssue;
  }

  void Profile(llvm::FoldingSetNodeID& hash) const override;
};

class PathSensitiveBugReport : public BugReport {
public:
  using VisitorList = SmallVector<std::unique_ptr<BugReporterVisitor>, 8>;
  using visitor_iterator = VisitorList::iterator;
  using visitor_range = llvm::iterator_range<visitor_iterator>;

protected:
  /// The ExplodedGraph node against which the report was thrown. It corresponds
  /// to the end of the execution path that demonstrates the bug.
  const ExplodedNode *ErrorNode = nullptr;

  /// The range that corresponds to ErrorNode's program point. It is usually
  /// highlighted in the report.
  const SourceRange ErrorNodeRange;

  /// Profile to identify equivalent bug reports for error report coalescing.

  /// A (stack of) a set of symbols that are registered with this
  /// report as being "interesting", and thus used to help decide which
  /// diagnostics to include when constructing the final path diagnostic.
  /// The stack is largely used by BugReporter when generating PathDiagnostics
  /// for multiple PathDiagnosticConsumers.
  llvm::DenseMap<SymbolRef, bugreporter::TrackingKind> InterestingSymbols;

  /// A (stack of) set of regions that are registered with this report as being
  /// "interesting", and thus used to help decide which diagnostics
  /// to include when constructing the final path diagnostic.
  /// The stack is largely used by BugReporter when generating PathDiagnostics
  /// for multiple PathDiagnosticConsumers.
  llvm::DenseMap<const MemRegion *, bugreporter::TrackingKind>
      InterestingRegions;

  /// A set of location contexts that correspoind to call sites which should be
  /// considered "interesting".
  llvm::SmallSet<const LocationContext *, 2> InterestingLocationContexts;

  /// A set of custom visitors which generate "event" diagnostics at
  /// interesting points in the path.
  VisitorList Callbacks;

  /// Used for ensuring the visitors are only added once.
  llvm::FoldingSet<BugReporterVisitor> CallbacksSet;

  /// When set, this flag disables all callstack pruning from a diagnostic
  /// path.  This is useful for some reports that want maximum fidelty
  /// when reporting an issue.
  bool DoNotPrunePath = false;

  /// Used to track unique reasons why a bug report might be invalid.
  ///
  /// \sa markInvalid
  /// \sa removeInvalidation
  using InvalidationRecord = std::pair<const void *, const void *>;

  /// If non-empty, this bug report is likely a false positive and should not be
  /// shown to the user.
  ///
  /// \sa markInvalid
  /// \sa removeInvalidation
  llvm::SmallSet<InvalidationRecord, 4> Invalidations;

  /// Conditions we're already tracking.
  llvm::SmallSet<const ExplodedNode *, 4> TrackedConditions;

  /// Reports with different uniqueing locations are considered to be different
  /// for the purposes of deduplication.
  PathDiagnosticLocation UniqueingLocation;
  const Decl *UniqueingDecl;

  const Stmt *getStmt() const;

  /// If an event occurs in a different frame than the final diagnostic,
  /// supply a message that will be used to construct an extra hint on the
  /// returns from all the calls on the stack from this event to the final
  /// diagnostic.
  // FIXME: Allow shared_ptr keys in DenseMap?
  std::map<PathDiagnosticPieceRef, std::unique_ptr<StackHintGenerator>>
      StackHints;

public:
  PathSensitiveBugReport(const BugType &bt, StringRef desc,
                         const ExplodedNode *errorNode)
      : PathSensitiveBugReport(bt, desc, desc, errorNode) {}

  PathSensitiveBugReport(const BugType &bt, StringRef shortDesc, StringRef desc,
                         const ExplodedNode *errorNode)
      : PathSensitiveBugReport(bt, shortDesc, desc, errorNode,
                               /*LocationToUnique*/ {},
                               /*DeclToUnique*/ nullptr) {}

  /// Create a PathSensitiveBugReport with a custom uniqueing location.
  ///
  /// The reports that have the same report location, description, bug type, and
  /// ranges are uniqued - only one of the equivalent reports will be presented
  /// to the user. This method allows to rest the location which should be used
  /// for uniquing reports. For example, memory leaks checker, could set this to
  /// the allocation site, rather then the location where the bug is reported.
  PathSensitiveBugReport(const BugType &bt, StringRef desc,
                         const ExplodedNode *errorNode,
                         PathDiagnosticLocation LocationToUnique,
                         const Decl *DeclToUnique)
      : PathSensitiveBugReport(bt, desc, desc, errorNode, LocationToUnique,
                               DeclToUnique) {}

  PathSensitiveBugReport(const BugType &bt, StringRef shortDesc, StringRef desc,
                         const ExplodedNode *errorNode,
                         PathDiagnosticLocation LocationToUnique,
                         const Decl *DeclToUnique);

  static bool classof(const BugReport *R) {
    return R->getKind() == Kind::PathSensitive;
  }

  const ExplodedNode *getErrorNode() const { return ErrorNode; }

  /// Indicates whether or not any path pruning should take place
  /// when generating a PathDiagnostic from this BugReport.
  bool shouldPrunePath() const { return !DoNotPrunePath; }

  /// Disable all path pruning when generating a PathDiagnostic.
  void disablePathPruning() { DoNotPrunePath = true; }

  /// Get the location on which the report should be uniqued.
  PathDiagnosticLocation getUniqueingLocation() const override {
    return UniqueingLocation;
  }

  /// Get the declaration containing the uniqueing location.
  const Decl *getUniqueingDecl() const override {
    return UniqueingDecl;
  }

  const Decl *getDeclWithIssue() const override;

  ArrayRef<SourceRange> getRanges() const override;

  PathDiagnosticLocation getLocation() const override;

  /// Marks a symbol as interesting. Different kinds of interestingness will
  /// be processed differently by visitors (e.g. if the tracking kind is
  /// condition, will append "will be used as a condition" to the message).
  void markInteresting(SymbolRef sym, bugreporter::TrackingKind TKind =
                                          bugreporter::TrackingKind::Thorough);

  void markNotInteresting(SymbolRef sym);

  /// Marks a region as interesting. Different kinds of interestingness will
  /// be processed differently by visitors (e.g. if the tracking kind is
  /// condition, will append "will be used as a condition" to the message).
  void markInteresting(
      const MemRegion *R,
      bugreporter::TrackingKind TKind = bugreporter::TrackingKind::Thorough);

  void markNotInteresting(const MemRegion *R);

  /// Marks a symbolic value as interesting. Different kinds of interestingness
  /// will be processed differently by visitors (e.g. if the tracking kind is
  /// condition, will append "will be used as a condition" to the message).
  void markInteresting(SVal V, bugreporter::TrackingKind TKind =
                                   bugreporter::TrackingKind::Thorough);
  void markInteresting(const LocationContext *LC);

  bool isInteresting(SymbolRef sym) const;
  bool isInteresting(const MemRegion *R) const;
  bool isInteresting(SVal V) const;
  bool isInteresting(const LocationContext *LC) const;

  std::optional<bugreporter::TrackingKind>
  getInterestingnessKind(SymbolRef sym) const;

  std::optional<bugreporter::TrackingKind>
  getInterestingnessKind(const MemRegion *R) const;

  std::optional<bugreporter::TrackingKind> getInterestingnessKind(SVal V) const;

  /// Returns whether or not this report should be considered valid.
  ///
  /// Invalid reports are those that have been classified as likely false
  /// positives after the fact.
  bool isValid() const {
    return Invalidations.empty();
  }

  /// Marks the current report as invalid, meaning that it is probably a false
  /// positive and should not be reported to the user.
  ///
  /// The \p Tag and \p Data arguments are intended to be opaque identifiers for
  /// this particular invalidation, where \p Tag represents the visitor
  /// responsible for invalidation, and \p Data represents the reason this
  /// visitor decided to invalidate the bug report.
  ///
  /// \sa removeInvalidation
  void markInvalid(const void *Tag, const void *Data) {
    Invalidations.insert(std::make_pair(Tag, Data));
  }

  /// Profile to identify equivalent bug reports for error report coalescing.
  /// Reports are uniqued to ensure that we do not emit multiple diagnostics
  /// for each bug.
  void Profile(llvm::FoldingSetNodeID &hash) const override;

  /// Add custom or predefined bug report visitors to this report.
  ///
  /// The visitors should be used when the default trace is not sufficient.
  /// For example, they allow constructing a more elaborate trace.
  /// @{
  void addVisitor(std::unique_ptr<BugReporterVisitor> visitor);

  template <class VisitorType, class... Args>
  void addVisitor(Args &&... ConstructorArgs) {
    addVisitor(
        std::make_unique<VisitorType>(std::forward<Args>(ConstructorArgs)...));
  }
  /// @}

  /// Remove all visitors attached to this bug report.
  void clearVisitors();

  /// Iterators through the custom diagnostic visitors.
  visitor_iterator visitor_begin() { return Callbacks.begin(); }
  visitor_iterator visitor_end() { return Callbacks.end(); }
  visitor_range visitors() { return {visitor_begin(), visitor_end()}; }

  /// Notes that the condition of the CFGBlock associated with \p Cond is
  /// being tracked.
  /// \returns false if the condition is already being tracked.
  bool addTrackedCondition(const ExplodedNode *Cond) {
    return TrackedConditions.insert(Cond).second;
  }

  void addCallStackHint(PathDiagnosticPieceRef Piece,
                        std::unique_ptr<StackHintGenerator> StackHint) {
    StackHints[Piece] = std::move(StackHint);
  }

  bool hasCallStackHint(PathDiagnosticPieceRef Piece) const {
    return StackHints.count(Piece) > 0;
  }

  /// Produce the hint for the given node. The node contains
  /// information about the call for which the diagnostic can be generated.
  std::string
  getCallStackMessage(PathDiagnosticPieceRef Piece,
                      const ExplodedNode *N) const {
    auto I = StackHints.find(Piece);
    if (I != StackHints.end())
      return I->second->getMessage(N);
    return "";
  }
};

//===----------------------------------------------------------------------===//
// BugTypes (collections of related reports).
//===----------------------------------------------------------------------===//

class BugReportEquivClass : public llvm::FoldingSetNode {
  friend class BugReporter;

  /// List of *owned* BugReport objects.
  llvm::SmallVector<std::unique_ptr<BugReport>, 4> Reports;

  void AddReport(std::unique_ptr<BugReport> &&R) {
    Reports.push_back(std::move(R));
  }

public:
  BugReportEquivClass(std::unique_ptr<BugReport> R) { AddReport(std::move(R)); }

  ArrayRef<std::unique_ptr<BugReport>> getReports() const { return Reports; }

  void Profile(llvm::FoldingSetNodeID& ID) const {
    assert(!Reports.empty());
    Reports.front()->Profile(ID);
  }
};

//===----------------------------------------------------------------------===//
// BugReporter and friends.
//===----------------------------------------------------------------------===//

class BugReporterData {
public:
  virtual ~BugReporterData() = default;

  virtual ArrayRef<PathDiagnosticConsumer*> getPathDiagnosticConsumers() = 0;
  virtual ASTContext &getASTContext() = 0;
  virtual SourceManager &getSourceManager() = 0;
  virtual AnalyzerOptions &getAnalyzerOptions() = 0;
  virtual Preprocessor &getPreprocessor() = 0;
};

/// BugReporter is a utility class for generating PathDiagnostics for analysis.
/// It collects the BugReports and BugTypes and knows how to generate
/// and flush the corresponding diagnostics.
///
/// The base class is used for generating path-insensitive
class BugReporter {
private:
  BugReporterData& D;

  /// The top-level entry point for the issue to be reported.
  const Decl *AnalysisEntryPoint = nullptr;

  /// Generate and flush the diagnostics for the given bug report.
  void FlushReport(BugReportEquivClass& EQ);

  /// The set of bug reports tracked by the BugReporter.
  llvm::FoldingSet<BugReportEquivClass> EQClasses;

  /// A vector of BugReports for tracking the allocated pointers and cleanup.
  std::vector<BugReportEquivClass *> EQClassesVector;

  /// User-provided in-code suppressions.
  BugSuppression UserSuppressions;

public:
  BugReporter(BugReporterData &d);
  virtual ~BugReporter();

  /// Generate and flush diagnostics for all bug reports.
  void FlushReports();

  ArrayRef<PathDiagnosticConsumer*> getPathDiagnosticConsumers() {
    return D.getPathDiagnosticConsumers();
  }

  /// Iterator over the set of BugReports tracked by the BugReporter.
  using EQClasses_iterator = llvm::FoldingSet<BugReportEquivClass>::iterator;
  llvm::iterator_range<EQClasses_iterator> equivalenceClasses() {
    return EQClasses;
  }

  ASTContext &getContext() { return D.getASTContext(); }

  const SourceManager &getSourceManager() { return D.getSourceManager(); }

  const AnalyzerOptions &getAnalyzerOptions() { return D.getAnalyzerOptions(); }

  Preprocessor &getPreprocessor() { return D.getPreprocessor(); }

  /// Get the top-level entry point for the issue to be reported.
  const Decl *getAnalysisEntryPoint() const { return AnalysisEntryPoint; }

  void setAnalysisEntryPoint(const Decl *EntryPoint) {
    assert(EntryPoint);
    AnalysisEntryPoint = EntryPoint;
  }

  /// Add the given report to the set of reports tracked by BugReporter.
  ///
  /// The reports are usually generated by the checkers. Further, they are
  /// folded based on the profile value, which is done to coalesce similar
  /// reports.
  virtual void emitReport(std::unique_ptr<BugReport> R);

  void EmitBasicReport(const Decl *DeclWithIssue, const CheckerBase *Checker,
                       StringRef BugName, StringRef BugCategory,
                       StringRef BugStr, PathDiagnosticLocation Loc,
                       ArrayRef<SourceRange> Ranges = std::nullopt,
                       ArrayRef<FixItHint> Fixits = std::nullopt);

  void EmitBasicReport(const Decl *DeclWithIssue, CheckerNameRef CheckerName,
                       StringRef BugName, StringRef BugCategory,
                       StringRef BugStr, PathDiagnosticLocation Loc,
                       ArrayRef<SourceRange> Ranges = std::nullopt,
                       ArrayRef<FixItHint> Fixits = std::nullopt);

private:
  llvm::StringMap<std::unique_ptr<BugType>> StrBugTypes;

  /// Returns a BugType that is associated with the given name and
  /// category.
  BugType *getBugTypeForName(CheckerNameRef CheckerName, StringRef name,
                             StringRef category);

  virtual BugReport *
  findReportInEquivalenceClass(BugReportEquivClass &eqClass,
                               SmallVectorImpl<BugReport *> &bugReports) {
    return eqClass.getReports()[0].get();
  }

protected:
  /// Generate the diagnostics for the given bug report.
  virtual std::unique_ptr<DiagnosticForConsumerMapTy>
  generateDiagnosticForConsumerMap(BugReport *exampleReport,
                                   ArrayRef<PathDiagnosticConsumer *> consumers,
                                   ArrayRef<BugReport *> bugReports);
};

/// GRBugReporter is used for generating path-sensitive reports.
class PathSensitiveBugReporter final : public BugReporter {
  ExprEngine& Eng;

  BugReport *findReportInEquivalenceClass(
      BugReportEquivClass &eqClass,
      SmallVectorImpl<BugReport *> &bugReports) override;

  /// Generate the diagnostics for the given bug report.
  std::unique_ptr<DiagnosticForConsumerMapTy>
  generateDiagnosticForConsumerMap(BugReport *exampleReport,
                                   ArrayRef<PathDiagnosticConsumer *> consumers,
                                   ArrayRef<BugReport *> bugReports) override;
public:
  PathSensitiveBugReporter(BugReporterData& d, ExprEngine& eng)
      : BugReporter(d), Eng(eng) {}

  /// getGraph - Get the exploded graph created by the analysis engine
  ///  for the analyzed method or function.
  const ExplodedGraph &getGraph() const;

  /// getStateManager - Return the state manager used by the analysis
  ///  engine.
  ProgramStateManager &getStateManager() const;

  /// \p bugReports A set of bug reports within a *single* equivalence class
  ///
  /// \return A mapping from consumers to the corresponding diagnostics.
  /// Iterates through the bug reports within a single equivalence class,
  /// stops at a first non-invalidated report.
  std::unique_ptr<DiagnosticForConsumerMapTy> generatePathDiagnostics(
      ArrayRef<PathDiagnosticConsumer *> consumers,
      ArrayRef<PathSensitiveBugReport *> &bugReports);

  void emitReport(std::unique_ptr<BugReport> R) override;
};


class BugReporterContext {
  PathSensitiveBugReporter &BR;

  virtual void anchor();

public:
  BugReporterContext(PathSensitiveBugReporter &br) : BR(br) {}

  virtual ~BugReporterContext() = default;

  PathSensitiveBugReporter& getBugReporter() { return BR; }
  const PathSensitiveBugReporter &getBugReporter() const { return BR; }

  ProgramStateManager& getStateManager() const {
    return BR.getStateManager();
  }

  ASTContext &getASTContext() const {
    return BR.getContext();
  }

  const SourceManager& getSourceManager() const {
    return BR.getSourceManager();
  }

  const AnalyzerOptions &getAnalyzerOptions() const {
    return BR.getAnalyzerOptions();
  }
};

/// The tag that carries some information with it.
///
/// It can be valuable to produce tags with some bits of information and later
/// reuse them for a better diagnostic.
///
/// Please make sure that derived class' constuctor is private and that the user
/// can only create objects using DataTag::Factory.  This also means that
/// DataTag::Factory should be friend for every derived class.
class DataTag : public ProgramPointTag {
public:
  StringRef getTagDescription() const override { return "Data Tag"; }

  // Manage memory for DataTag objects.
  class Factory {
    std::vector<std::unique_ptr<DataTag>> Tags;

  public:
    template <class DataTagType, class... Args>
    const DataTagType *make(Args &&... ConstructorArgs) {
      // We cannot use std::make_unique because we cannot access the private
      // constructor from inside it.
      Tags.emplace_back(
          new DataTagType(std::forward<Args>(ConstructorArgs)...));
      return static_cast<DataTagType *>(Tags.back().get());
    }
  };

protected:
  DataTag(void *TagKind) : ProgramPointTag(TagKind) {}
};

/// The tag upon which the TagVisitor reacts. Add these in order to display
/// additional PathDiagnosticEventPieces along the path.
class NoteTag : public DataTag {
public:
  using Callback = std::function<std::string(BugReporterContext &,
                                             PathSensitiveBugReport &)>;

private:
  static int Kind;

  const Callback Cb;
  const bool IsPrunable;

  NoteTag(Callback &&Cb, bool IsPrunable)
      : DataTag(&Kind), Cb(std::move(Cb)), IsPrunable(IsPrunable) {}

public:
  static bool classof(const ProgramPointTag *T) {
    return T->getTagKind() == &Kind;
  }

  std::optional<std::string> generateMessage(BugReporterContext &BRC,
                                             PathSensitiveBugReport &R) const {
    std::string Msg = Cb(BRC, R);
    if (Msg.empty())
      return std::nullopt;

    return std::move(Msg);
  }

  StringRef getTagDescription() const override {
    // TODO: Remember a few examples of generated messages
    // and display them in the ExplodedGraph dump by
    // returning them from this function.
    return "Note Tag";
  }

  bool isPrunable() const { return IsPrunable; }

  friend class Factory;
  friend class TagVisitor;
};

} // namespace ento

} // namespace clang

#endif // LLVM_CLANG_STATICANALYZER_CORE_BUGREPORTER_BUGREPORTER_H
