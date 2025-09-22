//== CheckerContext.h - Context info for path-sensitive checkers--*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines CheckerContext that provides contextual info for
// path-sensitive checkers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERCONTEXT_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_CHECKERCONTEXT_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ExprEngine.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"
#include <optional>

namespace clang {
namespace ento {

class CheckerContext {
  ExprEngine &Eng;
  /// The current exploded(symbolic execution) graph node.
  ExplodedNode *Pred;
  /// The flag is true if the (state of the execution) has been modified
  /// by the checker using this context. For example, a new transition has been
  /// added or a bug report issued.
  bool Changed;
  /// The tagged location, which is used to generate all new nodes.
  const ProgramPoint Location;
  NodeBuilder &NB;

public:
  /// If we are post visiting a call, this flag will be set if the
  /// call was inlined.  In all other cases it will be false.
  const bool wasInlined;

  CheckerContext(NodeBuilder &builder,
                 ExprEngine &eng,
                 ExplodedNode *pred,
                 const ProgramPoint &loc,
                 bool wasInlined = false)
    : Eng(eng),
      Pred(pred),
      Changed(false),
      Location(loc),
      NB(builder),
      wasInlined(wasInlined) {
    assert(Pred->getState() &&
           "We should not call the checkers on an empty state.");
  }

  AnalysisManager &getAnalysisManager() {
    return Eng.getAnalysisManager();
  }

  ConstraintManager &getConstraintManager() {
    return Eng.getConstraintManager();
  }

  StoreManager &getStoreManager() {
    return Eng.getStoreManager();
  }

  /// Returns the previous node in the exploded graph, which includes
  /// the state of the program before the checker ran. Note, checkers should
  /// not retain the node in their state since the nodes might get invalidated.
  ExplodedNode *getPredecessor() { return Pred; }
  const ProgramStateRef &getState() const { return Pred->getState(); }

  /// Check if the checker changed the state of the execution; ex: added
  /// a new transition or a bug report.
  bool isDifferent() { return Changed; }

  /// Returns the number of times the current block has been visited
  /// along the analyzed path.
  unsigned blockCount() const {
    return NB.getContext().blockCount();
  }

  ASTContext &getASTContext() {
    return Eng.getContext();
  }

  const ASTContext &getASTContext() const { return Eng.getContext(); }

  const LangOptions &getLangOpts() const {
    return Eng.getContext().getLangOpts();
  }

  const LocationContext *getLocationContext() const {
    return Pred->getLocationContext();
  }

  const StackFrameContext *getStackFrame() const {
    return Pred->getStackFrame();
  }

  /// Return true if the current LocationContext has no caller context.
  bool inTopFrame() const { return getLocationContext()->inTopFrame();  }

  BugReporter &getBugReporter() {
    return Eng.getBugReporter();
  }

  const SourceManager &getSourceManager() {
    return getBugReporter().getSourceManager();
  }

  Preprocessor &getPreprocessor() { return getBugReporter().getPreprocessor(); }

  SValBuilder &getSValBuilder() {
    return Eng.getSValBuilder();
  }

  SymbolManager &getSymbolManager() {
    return getSValBuilder().getSymbolManager();
  }

  ProgramStateManager &getStateManager() {
    return Eng.getStateManager();
  }

  AnalysisDeclContext *getCurrentAnalysisDeclContext() const {
    return Pred->getLocationContext()->getAnalysisDeclContext();
  }

  /// Get the blockID.
  unsigned getBlockID() const {
    return NB.getContext().getBlock()->getBlockID();
  }

  /// If the given node corresponds to a PostStore program point,
  /// retrieve the location region as it was uttered in the code.
  ///
  /// This utility can be useful for generating extensive diagnostics, for
  /// example, for finding variables that the given symbol was assigned to.
  static const MemRegion *getLocationRegionIfPostStore(const ExplodedNode *N) {
    ProgramPoint L = N->getLocation();
    if (std::optional<PostStore> PSL = L.getAs<PostStore>())
      return reinterpret_cast<const MemRegion*>(PSL->getLocationValue());
    return nullptr;
  }

  /// Get the value of arbitrary expressions at this point in the path.
  SVal getSVal(const Stmt *S) const {
    return Pred->getSVal(S);
  }

  /// Returns true if the value of \p E is greater than or equal to \p
  /// Val under unsigned comparison
  bool isGreaterOrEqual(const Expr *E, unsigned long long Val);

  /// Returns true if the value of \p E is negative.
  bool isNegative(const Expr *E);

  /// Generates a new transition in the program state graph
  /// (ExplodedGraph). Uses the default CheckerContext predecessor node.
  ///
  /// @param State The state of the generated node. If not specified, the state
  ///        will not be changed, but the new node will have the checker's tag.
  /// @param Tag The tag is used to uniquely identify the creation site. If no
  ///        tag is specified, a default tag, unique to the given checker,
  ///        will be used. Tags are used to prevent states generated at
  ///        different sites from caching out.
  ExplodedNode *addTransition(ProgramStateRef State = nullptr,
                              const ProgramPointTag *Tag = nullptr) {
    return addTransitionImpl(State ? State : getState(), false, nullptr, Tag);
  }

  /// Generates a new transition with the given predecessor.
  /// Allows checkers to generate a chain of nodes.
  ///
  /// @param State The state of the generated node.
  /// @param Pred The transition will be generated from the specified Pred node
  ///             to the newly generated node.
  /// @param Tag The tag to uniquely identify the creation site.
  ExplodedNode *addTransition(ProgramStateRef State, ExplodedNode *Pred,
                              const ProgramPointTag *Tag = nullptr) {
    return addTransitionImpl(State, false, Pred, Tag);
  }

  /// Generate a sink node. Generating a sink stops exploration of the
  /// given path. To create a sink node for the purpose of reporting an error,
  /// checkers should use generateErrorNode() instead.
  ExplodedNode *generateSink(ProgramStateRef State, ExplodedNode *Pred,
                             const ProgramPointTag *Tag = nullptr) {
    return addTransitionImpl(State ? State : getState(), true, Pred, Tag);
  }

  /// Add a sink node to the current path of execution, halting analysis.
  void addSink(ProgramStateRef State = nullptr,
               const ProgramPointTag *Tag = nullptr) {
    if (!State)
      State = getState();
    addTransition(State, generateSink(State, getPredecessor()));
  }

  /// Generate a transition to a node that will be used to report
  /// an error. This node will be a sink. That is, it will stop exploration of
  /// the given path.
  ///
  /// @param State The state of the generated node.
  /// @param Tag The tag to uniquely identify the creation site. If null,
  ///        the default tag for the checker will be used.
  ExplodedNode *generateErrorNode(ProgramStateRef State = nullptr,
                                  const ProgramPointTag *Tag = nullptr) {
    return generateSink(State, Pred,
                       (Tag ? Tag : Location.getTag()));
  }

  /// Generate a transition to a node that will be used to report
  /// an error. This node will be a sink. That is, it will stop exploration of
  /// the given path.
  ///
  /// @param State The state of the generated node.
  /// @param Pred The transition will be generated from the specified Pred node
  ///             to the newly generated node.
  /// @param Tag The tag to uniquely identify the creation site. If null,
  ///        the default tag for the checker will be used.
  ExplodedNode *generateErrorNode(ProgramStateRef State,
                                  ExplodedNode *Pred,
                                  const ProgramPointTag *Tag = nullptr) {
    return generateSink(State, Pred,
                       (Tag ? Tag : Location.getTag()));
  }

  /// Generate a transition to a node that will be used to report
  /// an error. This node will not be a sink. That is, exploration will
  /// continue along this path.
  ///
  /// @param State The state of the generated node.
  /// @param Tag The tag to uniquely identify the creation site. If null,
  ///        the default tag for the checker will be used.
  ExplodedNode *
  generateNonFatalErrorNode(ProgramStateRef State = nullptr,
                            const ProgramPointTag *Tag = nullptr) {
    return addTransition(State, (Tag ? Tag : Location.getTag()));
  }

  /// Generate a transition to a node that will be used to report
  /// an error. This node will not be a sink. That is, exploration will
  /// continue along this path.
  ///
  /// @param State The state of the generated node.
  /// @param Pred The transition will be generated from the specified Pred node
  ///             to the newly generated node.
  /// @param Tag The tag to uniquely identify the creation site. If null,
  ///        the default tag for the checker will be used.
  ExplodedNode *
  generateNonFatalErrorNode(ProgramStateRef State,
                            ExplodedNode *Pred,
                            const ProgramPointTag *Tag = nullptr) {
    return addTransition(State, Pred, (Tag ? Tag : Location.getTag()));
  }

  /// Emit the diagnostics report.
  void emitReport(std::unique_ptr<BugReport> R) {
    Changed = true;
    Eng.getBugReporter().emitReport(std::move(R));
  }

  /// Produce a program point tag that displays an additional path note
  /// to the user. This is a lightweight alternative to the
  /// BugReporterVisitor mechanism: instead of visiting the bug report
  /// node-by-node to restore the sequence of events that led to discovering
  /// a bug, you can add notes as you add your transitions.
  ///
  /// @param Cb Callback with 'BugReporterContext &, BugReport &' parameters.
  /// @param IsPrunable Whether the note is prunable. It allows BugReporter
  ///        to omit the note from the report if it would make the displayed
  ///        bug path significantly shorter.
  LLVM_ATTRIBUTE_RETURNS_NONNULL
  const NoteTag *getNoteTag(NoteTag::Callback &&Cb, bool IsPrunable = false) {
    return Eng.getDataTags().make<NoteTag>(std::move(Cb), IsPrunable);
  }

  /// A shorthand version of getNoteTag that doesn't require you to accept
  /// the 'BugReporterContext' argument when you don't need it.
  ///
  /// @param Cb Callback only with 'BugReport &' parameter.
  /// @param IsPrunable Whether the note is prunable. It allows BugReporter
  ///        to omit the note from the report if it would make the displayed
  ///        bug path significantly shorter.
  const NoteTag
  *getNoteTag(std::function<std::string(PathSensitiveBugReport &)> &&Cb,
              bool IsPrunable = false) {
    return getNoteTag(
        [Cb](BugReporterContext &,
             PathSensitiveBugReport &BR) { return Cb(BR); },
        IsPrunable);
  }

  /// A shorthand version of getNoteTag that doesn't require you to accept
  /// the arguments when you don't need it.
  ///
  /// @param Cb Callback without parameters.
  /// @param IsPrunable Whether the note is prunable. It allows BugReporter
  ///        to omit the note from the report if it would make the displayed
  ///        bug path significantly shorter.
  const NoteTag *getNoteTag(std::function<std::string()> &&Cb,
                            bool IsPrunable = false) {
    return getNoteTag([Cb](BugReporterContext &,
                           PathSensitiveBugReport &) { return Cb(); },
                      IsPrunable);
  }

  /// A shorthand version of getNoteTag that accepts a plain note.
  ///
  /// @param Note The note.
  /// @param IsPrunable Whether the note is prunable. It allows BugReporter
  ///        to omit the note from the report if it would make the displayed
  ///        bug path significantly shorter.
  const NoteTag *getNoteTag(StringRef Note, bool IsPrunable = false) {
    return getNoteTag(
        [Note = std::string(Note)](BugReporterContext &,
               PathSensitiveBugReport &) { return Note; },
        IsPrunable);
  }

  /// A shorthand version of getNoteTag that accepts a lambda with stream for
  /// note.
  ///
  /// @param Cb Callback with 'BugReport &' and 'llvm::raw_ostream &'.
  /// @param IsPrunable Whether the note is prunable. It allows BugReporter
  ///        to omit the note from the report if it would make the displayed
  ///        bug path significantly shorter.
  const NoteTag *getNoteTag(
      std::function<void(PathSensitiveBugReport &BR, llvm::raw_ostream &OS)> &&Cb,
      bool IsPrunable = false) {
    return getNoteTag(
        [Cb](PathSensitiveBugReport &BR) -> std::string {
          llvm::SmallString<128> Str;
          llvm::raw_svector_ostream OS(Str);
          Cb(BR, OS);
          return std::string(OS.str());
        },
        IsPrunable);
  }

  /// Returns the word that should be used to refer to the declaration
  /// in the report.
  StringRef getDeclDescription(const Decl *D);

  /// Get the declaration of the called function (path-sensitive).
  const FunctionDecl *getCalleeDecl(const CallExpr *CE) const;

  /// Get the name of the called function (path-sensitive).
  StringRef getCalleeName(const FunctionDecl *FunDecl) const;

  /// Get the identifier of the called function (path-sensitive).
  const IdentifierInfo *getCalleeIdentifier(const CallExpr *CE) const {
    const FunctionDecl *FunDecl = getCalleeDecl(CE);
    if (FunDecl)
      return FunDecl->getIdentifier();
    else
      return nullptr;
  }

  /// Get the name of the called function (path-sensitive).
  StringRef getCalleeName(const CallExpr *CE) const {
    const FunctionDecl *FunDecl = getCalleeDecl(CE);
    return getCalleeName(FunDecl);
  }

  /// Returns true if the given function is an externally-visible function in
  /// the top-level namespace, such as \c malloc.
  ///
  /// If a name is provided, the function must additionally match the given
  /// name.
  ///
  /// Note that this also accepts functions from the \c std namespace (because
  /// headers like <cstdlib> declare them there) and does not check if the
  /// function is declared as 'extern "C"' or if it uses C++ name mangling.
  static bool isCLibraryFunction(const FunctionDecl *FD,
                                 StringRef Name = StringRef());

  /// In builds that use source hardening (-D_FORTIFY_SOURCE), many standard
  /// functions are implemented as macros that expand to calls of hardened
  /// functions that take additional arguments compared to the "usual"
  /// variant and perform additional input validation. For example, a `memcpy`
  /// call may expand to `__memcpy_chk()` or `__builtin___memcpy_chk()`.
  ///
  /// This method returns true if `FD` declares a fortified variant of the
  /// standard library function `Name`.
  ///
  /// NOTE: This method relies on heuristics; extend it if you need to handle a
  /// hardened variant that's not yet covered by it.
  static bool isHardenedVariantOf(const FunctionDecl *FD, StringRef Name);

  /// Depending on wither the location corresponds to a macro, return
  /// either the macro name or the token spelling.
  ///
  /// This could be useful when checkers' logic depends on whether a function
  /// is called with a given macro argument. For example:
  ///   s = socket(AF_INET,..)
  /// If AF_INET is a macro, the result should be treated as a source of taint.
  ///
  /// \sa clang::Lexer::getSpelling(), clang::Lexer::getImmediateMacroName().
  StringRef getMacroNameOrSpelling(SourceLocation &Loc);

private:
  ExplodedNode *addTransitionImpl(ProgramStateRef State,
                                 bool MarkAsSink,
                                 ExplodedNode *P = nullptr,
                                 const ProgramPointTag *Tag = nullptr) {
    // The analyzer may stop exploring if it sees a state it has previously
    // visited ("cache out"). The early return here is a defensive check to
    // prevent accidental caching out by checker API clients. Unless there is a
    // tag or the client checker has requested that the generated node be
    // marked as a sink, we assume that a client requesting a transition to a
    // state that is the same as the predecessor state has made a mistake. We
    // return the predecessor rather than cache out.
    //
    // TODO: We could potentially change the return to an assertion to alert
    // clients to their mistake, but several checkers (including
    // DereferenceChecker, CallAndMessageChecker, and DynamicTypePropagation)
    // rely upon the defensive behavior and would need to be updated.
    if (!State || (State == Pred->getState() && !Tag && !MarkAsSink))
      return Pred;

    Changed = true;
    const ProgramPoint &LocalLoc = (Tag ? Location.withTag(Tag) : Location);
    if (!P)
      P = Pred;

    ExplodedNode *node;
    if (MarkAsSink)
      node = NB.generateSink(LocalLoc, State, P);
    else
      node = NB.generateNode(LocalLoc, State, P);
    return node;
  }
};

} // end GR namespace

} // end clang namespace

#endif
