//===------ Primitives.h - Types for the constexpr VM -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities and helper functions for all primitive types:
//  - Integral
//  - Floating
//  - Boolean
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_INTERP_PRIMITIVES_H
#define LLVM_CLANG_AST_INTERP_PRIMITIVES_H

#include "clang/AST/ComparisonCategories.h"

namespace clang {
namespace interp {

/// Helper to compare two comparable types.
template <typename T> ComparisonCategoryResult Compare(const T &X, const T &Y) {
  if (X < Y)
    return ComparisonCategoryResult::Less;
  if (X > Y)
    return ComparisonCategoryResult::Greater;
  return ComparisonCategoryResult::Equal;
}

} // namespace interp
} // namespace clang

#endif
