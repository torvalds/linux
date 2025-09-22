//===--- ASTMatchFinder.h - Structural query framework ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Provides a way to construct an ASTConsumer that runs given matchers
//  over the AST and invokes a given callback on every match.
//
//  The general idea is to construct a matcher expression that describes a
//  subtree match on the AST. Next, a callback that is executed every time the
//  expression matches is registered, and the matcher is run over the AST of
//  some code. Matched subexpressions can be bound to string IDs and easily
//  be accessed from the registered callback. The callback can than use the
//  AST nodes that the subexpressions matched on to output information about
//  the match or construct changes that can be applied to the code.
//
//  Example:
//  class HandleMatch : public MatchFinder::MatchCallback {
//  public:
//    virtual void Run(const MatchFinder::MatchResult &Result) {
//      const CXXRecordDecl *Class =
//          Result.Nodes.GetDeclAs<CXXRecordDecl>("id");
//      ...
//    }
//  };
//
//  int main(int argc, char **argv) {
//    ClangTool Tool(argc, argv);
//    MatchFinder finder;
//    finder.AddMatcher(Id("id", record(hasName("::a_namespace::AClass"))),
//                      new HandleMatch);
//    return Tool.Run(newFrontendActionFactory(&finder));
//  }
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_ASTMATCHFINDER_H
#define LLVM_CLANG_ASTMATCHERS_ASTMATCHFINDER_H

#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Timer.h"
#include <optional>

namespace clang {

namespace ast_matchers {

/// A class to allow finding matches over the Clang AST.
///
/// After creation, you can add multiple matchers to the MatchFinder via
/// calls to addMatcher(...).
///
/// Once all matchers are added, newASTConsumer() returns an ASTConsumer
/// that will trigger the callbacks specified via addMatcher(...) when a match
/// is found.
///
/// The order of matches is guaranteed to be equivalent to doing a pre-order
/// traversal on the AST, and applying the matchers in the order in which they
/// were added to the MatchFinder.
///
/// See ASTMatchers.h for more information about how to create matchers.
///
/// Not intended to be subclassed.
class MatchFinder {
public:
  /// Contains all information for a given match.
  ///
  /// Every time a match is found, the MatchFinder will invoke the registered
  /// MatchCallback with a MatchResult containing information about the match.
  struct MatchResult {
    MatchResult(const BoundNodes &Nodes, clang::ASTContext *Context);

    /// Contains the nodes bound on the current match.
    ///
    /// This allows user code to easily extract matched AST nodes.
    const BoundNodes Nodes;

    /// Utilities for interpreting the matched AST structures.
    /// @{
    clang::ASTContext * const Context;
    clang::SourceManager * const SourceManager;
    /// @}
  };

  /// Called when the Match registered for it was successfully found
  /// in the AST.
  class MatchCallback {
  public:
    virtual ~MatchCallback();

    /// Called on every match by the \c MatchFinder.
    virtual void run(const MatchResult &Result) = 0;

    /// Called at the start of each translation unit.
    ///
    /// Optionally override to do per translation unit tasks.
    virtual void onStartOfTranslationUnit() {}

    /// Called at the end of each translation unit.
    ///
    /// Optionally override to do per translation unit tasks.
    virtual void onEndOfTranslationUnit() {}

    /// An id used to group the matchers.
    ///
    /// This id is used, for example, for the profiling output.
    /// It defaults to "<unknown>".
    virtual StringRef getID() const;

    /// TraversalKind to use while matching and processing
    /// the result nodes. This API is temporary to facilitate
    /// third parties porting existing code to the default
    /// behavior of clang-tidy.
    virtual std::optional<TraversalKind> getCheckTraversalKind() const;
  };

  /// Called when parsing is finished. Intended for testing only.
  class ParsingDoneTestCallback {
  public:
    virtual ~ParsingDoneTestCallback();
    virtual void run() = 0;
  };

  struct MatchFinderOptions {
    struct Profiling {
      Profiling(llvm::StringMap<llvm::TimeRecord> &Records)
          : Records(Records) {}

      /// Per bucket timing information.
      llvm::StringMap<llvm::TimeRecord> &Records;
    };

    /// Enables per-check timers.
    ///
    /// It prints a report after match.
    std::optional<Profiling> CheckProfiling;
  };

  MatchFinder(MatchFinderOptions Options = MatchFinderOptions());
  ~MatchFinder();

  /// Adds a matcher to execute when running over the AST.
  ///
  /// Calls 'Action' with the BoundNodes on every match.
  /// Adding more than one 'NodeMatch' allows finding different matches in a
  /// single pass over the AST.
  ///
  /// Does not take ownership of 'Action'.
  /// @{
  void addMatcher(const DeclarationMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const TypeMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const StatementMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const NestedNameSpecifierMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const NestedNameSpecifierLocMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const TypeLocMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const CXXCtorInitializerMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const TemplateArgumentLocMatcher &NodeMatch,
                  MatchCallback *Action);
  void addMatcher(const AttrMatcher &NodeMatch, MatchCallback *Action);
  /// @}

  /// Adds a matcher to execute when running over the AST.
  ///
  /// This is similar to \c addMatcher(), but it uses the dynamic interface. It
  /// is more flexible, but the lost type information enables a caller to pass
  /// a matcher that cannot match anything.
  ///
  /// \returns \c true if the matcher is a valid top-level matcher, \c false
  ///   otherwise.
  bool addDynamicMatcher(const internal::DynTypedMatcher &NodeMatch,
                         MatchCallback *Action);

  /// Creates a clang ASTConsumer that finds all matches.
  std::unique_ptr<clang::ASTConsumer> newASTConsumer();

  /// Calls the registered callbacks on all matches on the given \p Node.
  ///
  /// Note that there can be multiple matches on a single node, for
  /// example when using decl(forEachDescendant(stmt())).
  ///
  /// @{
  template <typename T> void match(const T &Node, ASTContext &Context) {
    match(clang::DynTypedNode::create(Node), Context);
  }
  void match(const clang::DynTypedNode &Node, ASTContext &Context);
  /// @}

  /// Finds all matches in the given AST.
  void matchAST(ASTContext &Context);

  /// Registers a callback to notify the end of parsing.
  ///
  /// The provided closure is called after parsing is done, before the AST is
  /// traversed. Useful for benchmarking.
  /// Each call to FindAll(...) will call the closure once.
  void registerTestCallbackAfterParsing(ParsingDoneTestCallback *ParsingDone);

  /// For each \c Matcher<> a \c MatchCallback that will be called
  /// when it matches.
  struct MatchersByType {
    std::vector<std::pair<internal::DynTypedMatcher, MatchCallback *>>
        DeclOrStmt;
    std::vector<std::pair<TypeMatcher, MatchCallback *>> Type;
    std::vector<std::pair<NestedNameSpecifierMatcher, MatchCallback *>>
        NestedNameSpecifier;
    std::vector<std::pair<NestedNameSpecifierLocMatcher, MatchCallback *>>
        NestedNameSpecifierLoc;
    std::vector<std::pair<TypeLocMatcher, MatchCallback *>> TypeLoc;
    std::vector<std::pair<CXXCtorInitializerMatcher, MatchCallback *>> CtorInit;
    std::vector<std::pair<TemplateArgumentLocMatcher, MatchCallback *>>
        TemplateArgumentLoc;
    std::vector<std::pair<AttrMatcher, MatchCallback *>> Attr;
    /// All the callbacks in one container to simplify iteration.
    llvm::SmallPtrSet<MatchCallback *, 16> AllCallbacks;
  };

private:
  MatchersByType Matchers;

  MatchFinderOptions Options;

  /// Called when parsing is done.
  ParsingDoneTestCallback *ParsingDone;
};

/// Returns the results of matching \p Matcher on \p Node.
///
/// Collects the \c BoundNodes of all callback invocations when matching
/// \p Matcher on \p Node and returns the collected results.
///
/// Multiple results occur when using matchers like \c forEachDescendant,
/// which generate a result for each sub-match.
///
/// If you want to find all matches on the sub-tree rooted at \c Node (rather
/// than only the matches on \c Node itself), surround the \c Matcher with a
/// \c findAll().
///
/// \see selectFirst
/// @{
template <typename MatcherT, typename NodeT>
SmallVector<BoundNodes, 1>
match(MatcherT Matcher, const NodeT &Node, ASTContext &Context);

template <typename MatcherT>
SmallVector<BoundNodes, 1> match(MatcherT Matcher, const DynTypedNode &Node,
                                 ASTContext &Context);
/// @}

/// Returns the results of matching \p Matcher on the translation unit of
/// \p Context and collects the \c BoundNodes of all callback invocations.
template <typename MatcherT>
SmallVector<BoundNodes, 1> match(MatcherT Matcher, ASTContext &Context);

/// Returns the first result of type \c NodeT bound to \p BoundTo.
///
/// Returns \c NULL if there is no match, or if the matching node cannot be
/// casted to \c NodeT.
///
/// This is useful in combanation with \c match():
/// \code
///   const Decl *D = selectFirst<Decl>("id", match(Matcher.bind("id"),
///                                                 Node, Context));
/// \endcode
template <typename NodeT>
const NodeT *
selectFirst(StringRef BoundTo, const SmallVectorImpl<BoundNodes> &Results) {
  for (const BoundNodes &N : Results) {
    if (const NodeT *Node = N.getNodeAs<NodeT>(BoundTo))
      return Node;
  }
  return nullptr;
}

namespace internal {
class CollectMatchesCallback : public MatchFinder::MatchCallback {
public:
  void run(const MatchFinder::MatchResult &Result) override {
    Nodes.push_back(Result.Nodes);
  }

  std::optional<TraversalKind> getCheckTraversalKind() const override {
    return std::nullopt;
  }

  SmallVector<BoundNodes, 1> Nodes;
};
}

template <typename MatcherT>
SmallVector<BoundNodes, 1> match(MatcherT Matcher, const DynTypedNode &Node,
                                 ASTContext &Context) {
  internal::CollectMatchesCallback Callback;
  MatchFinder Finder;
  Finder.addMatcher(Matcher, &Callback);
  Finder.match(Node, Context);
  return std::move(Callback.Nodes);
}

template <typename MatcherT, typename NodeT>
SmallVector<BoundNodes, 1>
match(MatcherT Matcher, const NodeT &Node, ASTContext &Context) {
  return match(Matcher, DynTypedNode::create(Node), Context);
}

template <typename MatcherT>
SmallVector<BoundNodes, 1>
match(MatcherT Matcher, ASTContext &Context) {
  internal::CollectMatchesCallback Callback;
  MatchFinder Finder;
  Finder.addMatcher(Matcher, &Callback);
  Finder.matchAST(Context);
  return std::move(Callback.Nodes);
}

inline SmallVector<BoundNodes, 1>
matchDynamic(internal::DynTypedMatcher Matcher, const DynTypedNode &Node,
             ASTContext &Context) {
  internal::CollectMatchesCallback Callback;
  MatchFinder Finder;
  Finder.addDynamicMatcher(Matcher, &Callback);
  Finder.match(Node, Context);
  return std::move(Callback.Nodes);
}

template <typename NodeT>
SmallVector<BoundNodes, 1> matchDynamic(internal::DynTypedMatcher Matcher,
                                        const NodeT &Node,
                                        ASTContext &Context) {
  return matchDynamic(Matcher, DynTypedNode::create(Node), Context);
}

inline SmallVector<BoundNodes, 1>
matchDynamic(internal::DynTypedMatcher Matcher, ASTContext &Context) {
  internal::CollectMatchesCallback Callback;
  MatchFinder Finder;
  Finder.addDynamicMatcher(Matcher, &Callback);
  Finder.matchAST(Context);
  return std::move(Callback.Nodes);
}

} // end namespace ast_matchers
} // end namespace clang

#endif
