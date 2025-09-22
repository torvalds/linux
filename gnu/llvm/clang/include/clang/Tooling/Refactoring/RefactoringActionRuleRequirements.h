//===--- RefactoringActionRuleRequirements.h - Clang refactoring library --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULEREQUIREMENTS_H
#define LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULEREQUIREMENTS_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Refactoring/ASTSelection.h"
#include "clang/Tooling/Refactoring/RefactoringDiagnostic.h"
#include "clang/Tooling/Refactoring/RefactoringOption.h"
#include "clang/Tooling/Refactoring/RefactoringRuleContext.h"
#include "llvm/Support/Error.h"
#include <type_traits>

namespace clang {
namespace tooling {

/// A refactoring action rule requirement determines when a refactoring action
/// rule can be invoked. The rule can be invoked only when all of the
/// requirements are satisfied.
///
/// Subclasses must implement the
/// 'Expected<T> evaluate(RefactoringRuleContext &) const' member function.
/// \c T is used to determine the return type that is passed to the
/// refactoring rule's constructor.
/// For example, the \c SourceRangeSelectionRequirement subclass defines
/// 'Expected<SourceRange> evaluate(RefactoringRuleContext &Context) const'
/// function. When this function returns a non-error value, the resulting
/// source range is passed to the specific refactoring action rule
/// constructor (provided all other requirements are satisfied).
class RefactoringActionRuleRequirement {
  // Expected<T> evaluate(RefactoringRuleContext &Context) const;
};

/// A base class for any requirement that expects some part of the source to be
/// selected in an editor (or the refactoring tool with the -selection option).
class SourceSelectionRequirement : public RefactoringActionRuleRequirement {};

/// A selection requirement that is satisfied when any portion of the source
/// text is selected.
class SourceRangeSelectionRequirement : public SourceSelectionRequirement {
public:
  Expected<SourceRange> evaluate(RefactoringRuleContext &Context) const {
    if (Context.getSelectionRange().isValid())
      return Context.getSelectionRange();
    return Context.createDiagnosticError(diag::err_refactor_no_selection);
  }
};

/// An AST selection requirement is satisfied when any portion of the AST
/// overlaps with the selection range.
///
/// The requirement will be evaluated only once during the initiation and
/// search of matching refactoring action rules.
class ASTSelectionRequirement : public SourceRangeSelectionRequirement {
public:
  Expected<SelectedASTNode> evaluate(RefactoringRuleContext &Context) const;
};

/// A selection requirement that is satisfied when the selection range overlaps
/// with a number of neighbouring statements in the AST. The statemenst must be
/// contained in declaration like a function. The selection range must be a
/// non-empty source selection (i.e. cursors won't be accepted).
///
/// The requirement will be evaluated only once during the initiation and search
/// of matching refactoring action rules.
///
/// \see CodeRangeASTSelection
class CodeRangeASTSelectionRequirement : public ASTSelectionRequirement {
public:
  Expected<CodeRangeASTSelection>
  evaluate(RefactoringRuleContext &Context) const;
};

/// A base class for any requirement that requires some refactoring options.
class RefactoringOptionsRequirement : public RefactoringActionRuleRequirement {
public:
  virtual ~RefactoringOptionsRequirement() {}

  /// Returns the set of refactoring options that are used when evaluating this
  /// requirement.
  virtual ArrayRef<std::shared_ptr<RefactoringOption>>
  getRefactoringOptions() const = 0;
};

/// A requirement that evaluates to the value of the given \c OptionType when
/// the \c OptionType is a required option. When the \c OptionType is an
/// optional option, the requirement will evaluate to \c None if the option is
/// not specified or to an appropriate value otherwise.
template <typename OptionType>
class OptionRequirement : public RefactoringOptionsRequirement {
public:
  OptionRequirement() : Opt(createRefactoringOption<OptionType>()) {}

  ArrayRef<std::shared_ptr<RefactoringOption>>
  getRefactoringOptions() const final {
    return Opt;
  }

  Expected<typename OptionType::ValueType>
  evaluate(RefactoringRuleContext &) const {
    return static_cast<OptionType *>(Opt.get())->getValue();
  }

private:
  /// The partially-owned option.
  ///
  /// The ownership of the option is shared among the different requirements
  /// because the same option can be used by multiple rules in one refactoring
  /// action.
  std::shared_ptr<RefactoringOption> Opt;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULEREQUIREMENTS_H
