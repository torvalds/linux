//===---- MatchSwitch.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the `ASTMatchSwitch` abstraction for building a "switch"
//  statement, where each case of the switch is defined by an AST matcher. The
//  cases are considered in order, like pattern matching in functional
//  languages.
//
//  Currently, the design is catered towards simplifying the implementation of
//  `DataflowAnalysis` transfer functions. Based on experience here, this
//  library may be generalized and moved to ASTMatchers.
//
//===----------------------------------------------------------------------===//
//
// FIXME: Rename to ASTMatchSwitch.h

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_MATCHSWITCH_H_
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_MATCHSWITCH_H_

#include "clang/AST/ASTContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "llvm/ADT/StringRef.h"
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace clang {
namespace dataflow {

/// A common form of state shared between the cases of a transfer function.
template <typename LatticeT> struct TransferState {
  TransferState(LatticeT &Lattice, Environment &Env)
      : Lattice(Lattice), Env(Env) {}

  /// Current lattice element.
  LatticeT &Lattice;
  Environment &Env;
};

/// A read-only version of TransferState.
///
/// FIXME: this type is being used as a general (typed) view type for untyped
/// dataflow analysis state, rather than strictly for transfer-function
/// purposes. Move it (and rename it) to DataflowAnalysis.h.
template <typename LatticeT> struct TransferStateForDiagnostics {
  TransferStateForDiagnostics(const LatticeT &Lattice, const Environment &Env)
      : Lattice(Lattice), Env(Env) {}

  /// Current lattice element.
  const LatticeT &Lattice;
  const Environment &Env;
};

template <typename T>
using MatchSwitchMatcher = ast_matchers::internal::Matcher<T>;

template <typename T, typename State, typename Result = void>
using MatchSwitchAction = std::function<Result(
    const T *, const ast_matchers::MatchFinder::MatchResult &, State &)>;

template <typename BaseT, typename State, typename Result = void>
using ASTMatchSwitch =
    std::function<Result(const BaseT &, ASTContext &, State &)>;

/// Collects cases of a "match switch": a collection of matchers paired with
/// callbacks, which together define a switch that can be applied to a node
/// whose type derives from `BaseT`. This structure can simplify the definition
/// of `transfer` functions that rely on pattern-matching.
///
/// For example, consider an analysis that handles particular function calls. It
/// can define the `ASTMatchSwitch` once, in the constructor of the analysis,
/// and then reuse it each time that `transfer` is called, with a fresh state
/// value.
///
/// \code
/// ASTMatchSwitch<Stmt, TransferState<MyLattice> BuildSwitch() {
///   return ASTMatchSwitchBuilder<TransferState<MyLattice>>()
///     .CaseOf(callExpr(callee(functionDecl(hasName("foo")))), TransferFooCall)
///     .CaseOf(callExpr(argumentCountIs(2),
///                      callee(functionDecl(hasName("bar")))),
///             TransferBarCall)
///     .Build();
/// }
/// \endcode
template <typename BaseT, typename State, typename Result = void>
class ASTMatchSwitchBuilder {
public:
  /// Registers an action that will be triggered by the match of a pattern
  /// against the input statement.
  ///
  /// Requirements:
  ///
  ///  `NodeT` should be derived from `BaseT`.
  template <typename NodeT>
  ASTMatchSwitchBuilder &&CaseOf(MatchSwitchMatcher<BaseT> M,
                                 MatchSwitchAction<NodeT, State, Result> A) && {
    static_assert(std::is_base_of<BaseT, NodeT>::value,
                  "NodeT must be derived from BaseT.");
    Matchers.push_back(std::move(M));
    Actions.push_back(
        [A = std::move(A)](const BaseT *Node,
                           const ast_matchers::MatchFinder::MatchResult &R,
                           State &S) { return A(cast<NodeT>(Node), R, S); });
    return std::move(*this);
  }

  ASTMatchSwitch<BaseT, State, Result> Build() && {
    return [Matcher = BuildMatcher(), Actions = std::move(Actions)](
               const BaseT &Node, ASTContext &Context, State &S) -> Result {
      auto Results = ast_matchers::matchDynamic(Matcher, Node, Context);
      if (Results.empty()) {
        return Result();
      }
      // Look through the map for the first binding of the form "TagN..." use
      // that to select the action.
      for (const auto &Element : Results[0].getMap()) {
        llvm::StringRef ID(Element.first);
        size_t Index = 0;
        if (ID.consume_front("Tag") && !ID.getAsInteger(10, Index) &&
            Index < Actions.size()) {
          return Actions[Index](
              &Node,
              ast_matchers::MatchFinder::MatchResult(Results[0], &Context), S);
        }
      }
      return Result();
    };
  }

private:
  ast_matchers::internal::DynTypedMatcher BuildMatcher() {
    using ast_matchers::anything;
    using ast_matchers::stmt;
    using ast_matchers::unless;
    using ast_matchers::internal::DynTypedMatcher;
    if (Matchers.empty())
      return stmt(unless(anything()));
    for (int I = 0, N = Matchers.size(); I < N; ++I) {
      std::string Tag = ("Tag" + llvm::Twine(I)).str();
      // Many matchers are not bindable, so ensure that tryBind will work.
      Matchers[I].setAllowBind(true);
      auto M = *Matchers[I].tryBind(Tag);
      // Each anyOf explicitly controls the traversal kind. The anyOf itself is
      // set to `TK_AsIs` to ensure no nodes are skipped, thereby deferring to
      // the kind of the branches. Then, each branch is either left as is, if
      // the kind is already set, or explicitly set to `TK_AsIs`. We choose this
      // setting because it is the default interpretation of matchers.
      Matchers[I] =
          !M.getTraversalKind() ? M.withTraversalKind(TK_AsIs) : std::move(M);
    }
    // The matcher type on the cases ensures that `Expr` kind is compatible with
    // all of the matchers.
    return DynTypedMatcher::constructVariadic(
        DynTypedMatcher::VO_AnyOf, ASTNodeKind::getFromNodeKind<BaseT>(),
        std::move(Matchers));
  }

  std::vector<ast_matchers::internal::DynTypedMatcher> Matchers;
  std::vector<MatchSwitchAction<BaseT, State, Result>> Actions;
};

} // namespace dataflow
} // namespace clang
#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_MATCHSWITCH_H_
