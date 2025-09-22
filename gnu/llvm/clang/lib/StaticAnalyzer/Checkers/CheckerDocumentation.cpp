//===- CheckerDocumentation.cpp - Documentation checker ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This checker lists all the checker callbacks and provides documentation for
// checker writers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramStateTrait.h"

using namespace clang;
using namespace ento;

// All checkers should be placed into anonymous namespace.
// We place the CheckerDocumentation inside ento namespace to make the
// it visible in doxygen.
namespace clang {
namespace ento {

/// This checker documents the callback functions checkers can use to implement
/// the custom handling of the specific events during path exploration as well
/// as reporting bugs. Most of the callbacks are targeted at path-sensitive
/// checking.
///
/// \sa CheckerContext
class CheckerDocumentation
    : public Checker<
          // clang-format off
          check::ASTCodeBody,
          check::ASTDecl<FunctionDecl>,
          check::BeginFunction,
          check::Bind,
          check::BranchCondition,
          check::ConstPointerEscape,
          check::DeadSymbols,
          check::EndAnalysis,
          check::EndFunction,
          check::EndOfTranslationUnit,
          check::Event<ImplicitNullDerefEvent>,
          check::LiveSymbols,
          check::Location,
          check::NewAllocator,
          check::ObjCMessageNil,
          check::PointerEscape,
          check::PostCall,
          check::PostObjCMessage,
          check::PostStmt<DeclStmt>,
          check::PreCall,
          check::PreObjCMessage,
          check::PreStmt<ReturnStmt>,
          check::RegionChanges,
          eval::Assume,
          eval::Call
          // clang-format on
          > {
public:
  /// Pre-visit the Statement.
  ///
  /// The method will be called before the analyzer core processes the
  /// statement. The notification is performed for every explored CFGElement,
  /// which does not include the control flow statements such as IfStmt. The
  /// callback can be specialized to be called with any subclass of Stmt.
  ///
  /// See checkBranchCondition() callback for performing custom processing of
  /// the branching statements.
  ///
  /// check::PreStmt<ReturnStmt>
  void checkPreStmt(const ReturnStmt *DS, CheckerContext &C) const {}

  /// Post-visit the Statement.
  ///
  /// The method will be called after the analyzer core processes the
  /// statement. The notification is performed for every explored CFGElement,
  /// which does not include the control flow statements such as IfStmt. The
  /// callback can be specialized to be called with any subclass of Stmt.
  ///
  /// check::PostStmt<DeclStmt>
  void checkPostStmt(const DeclStmt *DS, CheckerContext &C) const;

  /// Pre-visit the Objective C message.
  ///
  /// This will be called before the analyzer core processes the method call.
  /// This is called for any action which produces an Objective-C message send,
  /// including explicit message syntax and property access.
  ///
  /// check::PreObjCMessage
  void checkPreObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const {}

  /// Post-visit the Objective C message.
  /// \sa checkPreObjCMessage()
  ///
  /// check::PostObjCMessage
  void checkPostObjCMessage(const ObjCMethodCall &M, CheckerContext &C) const {}

  /// Visit an Objective-C message whose receiver is nil.
  ///
  /// This will be called when the analyzer core processes a method call whose
  /// receiver is definitely nil. In this case, check{Pre/Post}ObjCMessage and
  /// check{Pre/Post}Call will not be called.
  ///
  /// check::ObjCMessageNil
  void checkObjCMessageNil(const ObjCMethodCall &M, CheckerContext &C) const {}

  /// Pre-visit an abstract "call" event.
  ///
  /// This is used for checkers that want to check arguments or attributed
  /// behavior for functions and methods no matter how they are being invoked.
  ///
  /// Note that this includes ALL cross-body invocations, so if you want to
  /// limit your checks to, say, function calls, you should test for that at the
  /// beginning of your callback function.
  ///
  /// check::PreCall
  void checkPreCall(const CallEvent &Call, CheckerContext &C) const {}

  /// Post-visit an abstract "call" event.
  /// \sa checkPreObjCMessage()
  ///
  /// check::PostCall
  void checkPostCall(const CallEvent &Call, CheckerContext &C) const {}

  /// Pre-visit of the condition statement of a branch (such as IfStmt).
  void checkBranchCondition(const Stmt *Condition, CheckerContext &Ctx) const {}

  /// Post-visit the C++ operator new's allocation call.
  ///
  /// Execution of C++ operator new consists of the following phases: (1) call
  /// default or overridden operator new() to allocate memory (2) cast the
  /// return value of operator new() from void pointer type to class pointer
  /// type, (3) assuming that the value is non-null, call the object's
  /// constructor over this pointer, (4) declare that the value of the
  /// new-expression is this pointer. This callback is called between steps
  /// (2) and (3). Post-call for the allocator is called after step (1).
  /// Pre-statement for the new-expression is called on step (4) when the value
  /// of the expression is evaluated.
  void checkNewAllocator(const CXXAllocatorCall &, CheckerContext &) const {}

  /// Called on a load from and a store to a location.
  ///
  /// The method will be called each time a location (pointer) value is
  /// accessed.
  /// \param Loc    The value of the location (pointer).
  /// \param IsLoad The flag specifying if the location is a store or a load.
  /// \param S      The load is performed while processing the statement.
  ///
  /// check::Location
  void checkLocation(SVal Loc, bool IsLoad, const Stmt *S,
                     CheckerContext &) const {}

  /// Called on binding of a value to a location.
  ///
  /// \param Loc The value of the location (pointer).
  /// \param Val The value which will be stored at the location Loc.
  /// \param S   The bind is performed while processing the statement S.
  ///
  /// check::Bind
  void checkBind(SVal Loc, SVal Val, const Stmt *S, CheckerContext &) const {}

  /// Called whenever a symbol becomes dead.
  ///
  /// This callback should be used by the checkers to aggressively clean
  /// up/reduce the checker state, which is important for reducing the overall
  /// memory usage. Specifically, if a checker keeps symbol specific information
  /// in the state, it can and should be dropped after the symbol becomes dead.
  /// In addition, reporting a bug as soon as the checker becomes dead leads to
  /// more precise diagnostics. (For example, one should report that a malloced
  /// variable is not freed right after it goes out of scope.)
  ///
  /// \param SR The SymbolReaper object can be queried to determine which
  ///           symbols are dead.
  ///
  /// check::DeadSymbols
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const {}


  /// Called when the analyzer core starts analyzing a function,
  /// regardless of whether it is analyzed at the top level or is inlined.
  ///
  /// check::BeginFunction
  void checkBeginFunction(CheckerContext &Ctx) const {}

  /// Called when the analyzer core reaches the end of a
  /// function being analyzed regardless of whether it is analyzed at the top
  /// level or is inlined.
  ///
  /// check::EndFunction
  void checkEndFunction(const ReturnStmt *RS, CheckerContext &Ctx) const {}

  /// Called after all the paths in the ExplodedGraph reach end of path
  /// - the symbolic execution graph is fully explored.
  ///
  /// This callback should be used in cases when a checker needs to have a
  /// global view of the information generated on all paths. For example, to
  /// compare execution summary/result several paths.
  /// See IdempotentOperationChecker for a usage example.
  ///
  /// check::EndAnalysis
  void checkEndAnalysis(ExplodedGraph &G,
                        BugReporter &BR,
                        ExprEngine &Eng) const {}

  /// Called after analysis of a TranslationUnit is complete.
  ///
  /// check::EndOfTranslationUnit
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                 AnalysisManager &Mgr,
                                 BugReporter &BR) const {}

  /// Evaluates function call.
  ///
  /// The analysis core treats all function calls in the same way. However, some
  /// functions have special meaning, which should be reflected in the program
  /// state. This callback allows a checker to provide domain specific knowledge
  /// about the particular functions it knows about.
  ///
  /// \returns true if the call has been successfully evaluated
  /// and false otherwise. Note, that only one checker can evaluate a call. If
  /// more than one checker claims that they can evaluate the same call the
  /// first one wins.
  ///
  /// eval::Call
  bool evalCall(const CallEvent &Call, CheckerContext &C) const { return true; }

  /// Handles assumptions on symbolic values.
  ///
  /// This method is called when a symbolic expression is assumed to be true or
  /// false. For example, the assumptions are performed when evaluating a
  /// condition at a branch. The callback allows checkers track the assumptions
  /// performed on the symbols of interest and change the state accordingly.
  ///
  /// eval::Assume
  ProgramStateRef evalAssume(ProgramStateRef State,
                                 SVal Cond,
                                 bool Assumption) const { return State; }

  /// Allows modifying SymbolReaper object. For example, checkers can explicitly
  /// register symbols of interest as live. These symbols will not be marked
  /// dead and removed.
  ///
  /// check::LiveSymbols
  void checkLiveSymbols(ProgramStateRef State, SymbolReaper &SR) const {}

  /// Called when the contents of one or more regions change.
  ///
  /// This can occur in many different ways: an explicit bind, a blanket
  /// invalidation of the region contents, or by passing a region to a function
  /// call whose behavior the analyzer cannot model perfectly.
  ///
  /// \param State The current program state.
  /// \param Invalidated A set of all symbols potentially touched by the change.
  /// \param ExplicitRegions The regions explicitly requested for invalidation.
  ///        For a function call, this would be the arguments. For a bind, this
  ///        would be the region being bound to.
  /// \param Regions The transitive closure of regions accessible from,
  ///        \p ExplicitRegions, i.e. all regions that may have been touched
  ///        by this change. For a simple bind, this list will be the same as
  ///        \p ExplicitRegions, since a bind does not affect the contents of
  ///        anything accessible through the base region.
  /// \param LCtx LocationContext that is useful for getting various contextual
  ///        info, like callstack, CFG etc.
  /// \param Call The opaque call triggering this invalidation. Will be 0 if the
  ///        change was not triggered by a call.
  ///
  /// check::RegionChanges
  ProgramStateRef
    checkRegionChanges(ProgramStateRef State,
                       const InvalidatedSymbols *Invalidated,
                       ArrayRef<const MemRegion *> ExplicitRegions,
                       ArrayRef<const MemRegion *> Regions,
                       const LocationContext *LCtx,
                       const CallEvent *Call) const {
    return State;
  }

  /// Called when pointers escape.
  ///
  /// This notifies the checkers about pointer escape, which occurs whenever
  /// the analyzer cannot track the symbol any more. For example, as a
  /// result of assigning a pointer into a global or when it's passed to a
  /// function call the analyzer cannot model.
  ///
  /// \param State The state at the point of escape.
  /// \param Escaped The list of escaped symbols.
  /// \param Call The corresponding CallEvent, if the symbols escape as
  /// parameters to the given call.
  /// \param Kind How the symbols have escaped.
  /// \returns Checkers can modify the state by returning a new state.
  ProgramStateRef checkPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const {
    return State;
  }

  /// Called when const pointers escape.
  ///
  /// Note: in most cases checkPointerEscape callback is sufficient.
  /// \sa checkPointerEscape
  ProgramStateRef checkConstPointerEscape(ProgramStateRef State,
                                     const InvalidatedSymbols &Escaped,
                                     const CallEvent *Call,
                                     PointerEscapeKind Kind) const {
    return State;
  }

  /// check::Event<ImplicitNullDerefEvent>
  void checkEvent(ImplicitNullDerefEvent Event) const {}

  /// Check every declaration in the AST.
  ///
  /// An AST traversal callback, which should only be used when the checker is
  /// not path sensitive. It will be called for every Declaration in the AST and
  /// can be specialized to only be called on subclasses of Decl, for example,
  /// FunctionDecl.
  ///
  /// check::ASTDecl<FunctionDecl>
  void checkASTDecl(const FunctionDecl *D,
                    AnalysisManager &Mgr,
                    BugReporter &BR) const {}

  /// Check every declaration that has a statement body in the AST.
  ///
  /// As AST traversal callback, which should only be used when the checker is
  /// not path sensitive. It will be called for every Declaration in the AST.
  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const {}
};

void CheckerDocumentation::checkPostStmt(const DeclStmt *DS,
                                         CheckerContext &C) const {
}

void registerCheckerDocumentationChecker(CheckerManager &Mgr) {
  Mgr.registerChecker<CheckerDocumentation>();
}

bool shouldRegisterCheckerDocumentationChecker(const CheckerManager &) {
  return false;
}

} // end namespace ento
} // end namespace clang
