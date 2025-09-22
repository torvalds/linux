//===- DenseMapInfoVariant.h - Type traits for DenseMap<variant> *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines DenseMapInfo traits for DenseMap<std::variant<Ts...>>.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DENSEMAPINFOVARIANT_H
#define LLVM_ADT_DENSEMAPINFOVARIANT_H

#include "llvm/ADT/DenseMapInfo.h"
#include <utility>
#include <variant>

namespace llvm {

// Provide DenseMapInfo for variants whose all alternatives have DenseMapInfo.
template <typename... Ts> struct DenseMapInfo<std::variant<Ts...>> {
  using Variant = std::variant<Ts...>;
  using FirstT = std::variant_alternative_t<0, Variant>;

  static inline Variant getEmptyKey() {
    return Variant(std::in_place_index<0>, DenseMapInfo<FirstT>::getEmptyKey());
  }

  static inline Variant getTombstoneKey() {
    return Variant(std::in_place_index<0>,
                   DenseMapInfo<FirstT>::getTombstoneKey());
  }

  static unsigned getHashValue(const Variant &Val) {
    return std::visit(
        [&Val](auto &&Alternative) {
          using T = std::decay_t<decltype(Alternative)>;
          // Include index in hash to make sure same value as different
          // alternatives don't collide.
          return DenseMapInfo<std::pair<size_t, T>>::getHashValuePiecewise(
              Val.index(), Alternative);
        },
        Val);
  }

  static bool isEqual(const Variant &LHS, const Variant &RHS) {
    if (LHS.index() != RHS.index())
      return false;
    if (LHS.valueless_by_exception())
      return true;
    // We want to dispatch to DenseMapInfo<T>::isEqual(LHS.get(I), RHS.get(I))
    // We know the types are the same, but std::visit(V, LHS, RHS) doesn't.
    // We erase the type held in LHS to void*, and dispatch over RHS.
    const void *ErasedLHS =
        std::visit([](const auto &LHS) -> const void * { return &LHS; }, LHS);
    return std::visit(
        [&](const auto &RHS) -> bool {
          using T = std::remove_cv_t<std::remove_reference_t<decltype(RHS)>>;
          return DenseMapInfo<T>::isEqual(*static_cast<const T *>(ErasedLHS),
                                          RHS);
        },
        RHS);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_DENSEMAPINFOVARIANT_H
