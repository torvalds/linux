//===--- RefactoringAction.h - Clang refactoring library ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTION_H
#define LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTION_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Refactoring/RefactoringActionRules.h"
#include <vector>

namespace clang {
namespace tooling {

/// A refactoring action is a class that defines a set of related refactoring
/// action rules. These rules get grouped under a common umbrella - a single
/// clang-refactor subcommand.
///
/// A subclass of \c RefactoringAction is responsible for creating the set of
/// grouped refactoring action rules that represent one refactoring operation.
/// Although the rules in one action may have a number of different
/// implementations, they should strive to produce a similar result. It should
/// be easy for users to identify which refactoring action produced the result
/// regardless of which refactoring action rule was used.
///
/// The distinction between actions and rules enables the creation of action
/// that uses very different rules, for example:
///   - local vs global: a refactoring operation like
///     "add missing switch cases" can be applied to one switch when it's
///     selected in an editor, or to all switches in a project when an enum
///     constant is added to an enum.
///   - tool vs editor: some refactoring operation can be initiated in the
///     editor when a declaration is selected, or in a tool when the name of
///     the declaration is passed using a command-line argument.
class RefactoringAction {
public:
  virtual ~RefactoringAction() {}

  /// Returns the name of the subcommand that's used by clang-refactor for this
  /// action.
  virtual StringRef getCommand() const = 0;

  virtual StringRef getDescription() const = 0;

  RefactoringActionRules createActiveActionRules();

protected:
  /// Returns a set of refactoring actions rules that are defined by this
  /// action.
  virtual RefactoringActionRules createActionRules() const = 0;
};

/// Returns the list of all the available refactoring actions.
std::vector<std::unique_ptr<RefactoringAction>> createRefactoringActions();

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTION_H
