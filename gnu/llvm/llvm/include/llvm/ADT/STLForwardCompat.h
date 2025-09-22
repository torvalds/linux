//===- STLForwardCompat.h - Library features from future STLs ------C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains library features backported from future STL versions.
///
/// These should be replaced with their STL counterparts as the C++ version LLVM
/// is compiled with is updated.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STLFORWARDCOMPAT_H
#define LLVM_ADT_STLFORWARDCOMPAT_H

#include <optional>
#include <type_traits>

namespace llvm {

//===----------------------------------------------------------------------===//
//     Features from C++20
//===----------------------------------------------------------------------===//

template <typename T>
struct remove_cvref // NOLINT(readability-identifier-naming)
{
  using type = std::remove_cv_t<std::remove_reference_t<T>>;
};

template <typename T>
using remove_cvref_t // NOLINT(readability-identifier-naming)
    = typename llvm::remove_cvref<T>::type;

//===----------------------------------------------------------------------===//
//     Features from C++23
//===----------------------------------------------------------------------===//

// TODO: Remove this in favor of std::optional<T>::transform once we switch to
// C++23.
template <typename T, typename Function>
auto transformOptional(const std::optional<T> &O, const Function &F)
    -> std::optional<decltype(F(*O))> {
  if (O)
    return F(*O);
  return std::nullopt;
}

// TODO: Remove this in favor of std::optional<T>::transform once we switch to
// C++23.
template <typename T, typename Function>
auto transformOptional(std::optional<T> &&O, const Function &F)
    -> std::optional<decltype(F(*std::move(O)))> {
  if (O)
    return F(*std::move(O));
  return std::nullopt;
}

/// Returns underlying integer value of an enum. Backport of C++23
/// std::to_underlying.
template <typename Enum>
[[nodiscard]] constexpr std::underlying_type_t<Enum> to_underlying(Enum E) {
  return static_cast<std::underlying_type_t<Enum>>(E);
}

} // namespace llvm

#endif // LLVM_ADT_STLFORWARDCOMPAT_H
