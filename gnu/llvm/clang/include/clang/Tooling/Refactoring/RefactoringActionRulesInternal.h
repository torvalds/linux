//===--- RefactoringActionRulesInternal.h - Clang refactoring library -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULESINTERNAL_H
#define LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULESINTERNAL_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Refactoring/RefactoringActionRule.h"
#include "clang/Tooling/Refactoring/RefactoringActionRuleRequirements.h"
#include "clang/Tooling/Refactoring/RefactoringResultConsumer.h"
#include "clang/Tooling/Refactoring/RefactoringRuleContext.h"
#include "llvm/Support/Error.h"
#include <type_traits>

namespace clang {
namespace tooling {
namespace internal {

inline llvm::Error findError() { return llvm::Error::success(); }

inline void ignoreError() {}

template <typename FirstT, typename... RestT>
void ignoreError(Expected<FirstT> &First, Expected<RestT> &... Rest) {
  if (!First)
    llvm::consumeError(First.takeError());
  ignoreError(Rest...);
}

/// Scans the tuple and returns a valid \c Error if any of the values are
/// invalid.
template <typename FirstT, typename... RestT>
llvm::Error findError(Expected<FirstT> &First, Expected<RestT> &... Rest) {
  if (!First) {
    ignoreError(Rest...);
    return First.takeError();
  }
  return findError(Rest...);
}

template <typename RuleType, typename... RequirementTypes, size_t... Is>
void invokeRuleAfterValidatingRequirements(
    RefactoringResultConsumer &Consumer, RefactoringRuleContext &Context,
    const std::tuple<RequirementTypes...> &Requirements,
    std::index_sequence<Is...>) {
  // Check if the requirements we're interested in can be evaluated.
  auto Values =
      std::make_tuple(std::get<Is>(Requirements).evaluate(Context)...);
  auto Err = findError(std::get<Is>(Values)...);
  if (Err)
    return Consumer.handleError(std::move(Err));
  // Construct the target action rule by extracting the evaluated
  // requirements from Expected<> wrappers and then run it.
  auto Rule =
      RuleType::initiate(Context, std::move((*std::get<Is>(Values)))...);
  if (!Rule)
    return Consumer.handleError(Rule.takeError());
  Rule->invoke(Consumer, Context);
}

inline void visitRefactoringOptionsImpl(RefactoringOptionVisitor &) {}

/// Scans the list of requirements in a rule and visits all the refactoring
/// options that are used by all the requirements.
template <typename FirstT, typename... RestT>
void visitRefactoringOptionsImpl(RefactoringOptionVisitor &Visitor,
                                 const FirstT &First, const RestT &... Rest) {
  struct OptionGatherer {
    RefactoringOptionVisitor &Visitor;

    void operator()(const RefactoringOptionsRequirement &Requirement) {
      for (const auto &Option : Requirement.getRefactoringOptions())
        Option->passToVisitor(Visitor);
    }
    void operator()(const RefactoringActionRuleRequirement &) {}
  };
  (OptionGatherer{Visitor})(First);
  return visitRefactoringOptionsImpl(Visitor, Rest...);
}

template <typename... RequirementTypes, size_t... Is>
void visitRefactoringOptions(
    RefactoringOptionVisitor &Visitor,
    const std::tuple<RequirementTypes...> &Requirements,
    std::index_sequence<Is...>) {
  visitRefactoringOptionsImpl(Visitor, std::get<Is>(Requirements)...);
}

/// A type trait that returns true when the given type list has at least one
/// type whose base is the given base type.
template <typename Base, typename First, typename... Rest>
struct HasBaseOf : std::conditional_t<HasBaseOf<Base, First>::value ||
                                          HasBaseOf<Base, Rest...>::value,
                                      std::true_type, std::false_type> {};

template <typename Base, typename T>
struct HasBaseOf<Base, T> : std::is_base_of<Base, T> {};

/// A type trait that returns true when the given type list contains types that
/// derive from Base.
template <typename Base, typename First, typename... Rest>
struct AreBaseOf : std::conditional_t<AreBaseOf<Base, First>::value &&
                                          AreBaseOf<Base, Rest...>::value,
                                      std::true_type, std::false_type> {};

template <typename Base, typename T>
struct AreBaseOf<Base, T> : std::is_base_of<Base, T> {};

} // end namespace internal

template <typename RuleType, typename... RequirementTypes>
std::unique_ptr<RefactoringActionRule>
createRefactoringActionRule(const RequirementTypes &... Requirements) {
  static_assert(std::is_base_of<RefactoringActionRuleBase, RuleType>::value,
                "Expected a refactoring action rule type");
  static_assert(internal::AreBaseOf<RefactoringActionRuleRequirement,
                                    RequirementTypes...>::value,
                "Expected a list of refactoring action rules");

  class Rule final : public RefactoringActionRule {
  public:
    Rule(std::tuple<RequirementTypes...> Requirements)
        : Requirements(Requirements) {}

    void invoke(RefactoringResultConsumer &Consumer,
                RefactoringRuleContext &Context) override {
      internal::invokeRuleAfterValidatingRequirements<RuleType>(
          Consumer, Context, Requirements,
          std::index_sequence_for<RequirementTypes...>());
    }

    bool hasSelectionRequirement() override {
      return internal::HasBaseOf<SourceSelectionRequirement,
                                 RequirementTypes...>::value;
    }

    void visitRefactoringOptions(RefactoringOptionVisitor &Visitor) override {
      internal::visitRefactoringOptions(
          Visitor, Requirements,
          std::index_sequence_for<RequirementTypes...>());
    }
  private:
    std::tuple<RequirementTypes...> Requirements;
  };

  return std::make_unique<Rule>(std::make_tuple(Requirements...));
}

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGACTIONRULESINTERNAL_H
