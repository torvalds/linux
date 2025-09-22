//===--- RefactoringOptions.h - Clang refactoring library -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGOPTIONS_H
#define LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGOPTIONS_H

#include "clang/Basic/LLVM.h"
#include "clang/Tooling/Refactoring/RefactoringActionRuleRequirements.h"
#include "clang/Tooling/Refactoring/RefactoringOption.h"
#include "clang/Tooling/Refactoring/RefactoringOptionVisitor.h"
#include "llvm/Support/Error.h"
#include <optional>
#include <type_traits>

namespace clang {
namespace tooling {

/// A refactoring option that stores a value of type \c T.
template <typename T,
          typename = std::enable_if_t<traits::IsValidOptionType<T>::value>>
class OptionalRefactoringOption : public RefactoringOption {
public:
  void passToVisitor(RefactoringOptionVisitor &Visitor) final {
    Visitor.visit(*this, Value);
  }

  bool isRequired() const override { return false; }

  using ValueType = std::optional<T>;

  const ValueType &getValue() const { return Value; }

protected:
  std::optional<T> Value;
};

/// A required refactoring option that stores a value of type \c T.
template <typename T,
          typename = std::enable_if_t<traits::IsValidOptionType<T>::value>>
class RequiredRefactoringOption : public OptionalRefactoringOption<T> {
public:
  using ValueType = T;

  const ValueType &getValue() const {
    return *OptionalRefactoringOption<T>::Value;
  }
  bool isRequired() const final { return true; }
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_REFACTORINGOPTIONS_H
