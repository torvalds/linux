//===- CustomizableOptional.h - Optional with custom storage ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_BASIC_CUSTOMIZABLEOPTIONAL_H
#define CLANG_BASIC_CUSTOMIZABLEOPTIONAL_H

#include "llvm/ADT/Hashing.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <new>
#include <optional>
#include <utility>

namespace clang {

namespace optional_detail {
template <typename> class OptionalStorage;
} // namespace optional_detail

// Optional type which internal storage can be specialized by providing
// OptionalStorage. The interface follows std::optional.
template <typename T> class CustomizableOptional {
  optional_detail::OptionalStorage<T> Storage;

public:
  using value_type = T;

  constexpr CustomizableOptional() = default;
  constexpr CustomizableOptional(std::nullopt_t) {}

  constexpr CustomizableOptional(const T &y) : Storage(std::in_place, y) {}
  constexpr CustomizableOptional(const CustomizableOptional &O) = default;

  constexpr CustomizableOptional(T &&y)
      : Storage(std::in_place, std::move(y)) {}
  constexpr CustomizableOptional(CustomizableOptional &&O) = default;

  template <typename... ArgTypes>
  constexpr CustomizableOptional(std::in_place_t, ArgTypes &&...Args)
      : Storage(std::in_place, std::forward<ArgTypes>(Args)...) {}

  // Allow conversion from std::optional<T>.
  constexpr CustomizableOptional(const std::optional<T> &y)
      : CustomizableOptional(y ? *y : CustomizableOptional()) {}
  constexpr CustomizableOptional(std::optional<T> &&y)
      : CustomizableOptional(y ? std::move(*y) : CustomizableOptional()) {}

  CustomizableOptional &operator=(T &&y) {
    Storage = std::move(y);
    return *this;
  }
  CustomizableOptional &operator=(CustomizableOptional &&O) = default;

  /// Create a new object by constructing it in place with the given arguments.
  template <typename... ArgTypes> void emplace(ArgTypes &&...Args) {
    Storage.emplace(std::forward<ArgTypes>(Args)...);
  }

  CustomizableOptional &operator=(const T &y) {
    Storage = y;
    return *this;
  }
  CustomizableOptional &operator=(const CustomizableOptional &O) = default;

  void reset() { Storage.reset(); }

  LLVM_DEPRECATED("Use &*X instead.", "&*X")
  constexpr const T *getPointer() const { return &Storage.value(); }
  LLVM_DEPRECATED("Use &*X instead.", "&*X")
  T *getPointer() { return &Storage.value(); }
  LLVM_DEPRECATED("std::optional::value is throwing. Use *X instead", "*X")
  constexpr const T &value() const & { return Storage.value(); }
  LLVM_DEPRECATED("std::optional::value is throwing. Use *X instead", "*X")
  T &value() & { return Storage.value(); }

  constexpr explicit operator bool() const { return has_value(); }
  constexpr bool has_value() const { return Storage.has_value(); }
  constexpr const T *operator->() const { return &Storage.value(); }
  T *operator->() { return &Storage.value(); }
  constexpr const T &operator*() const & { return Storage.value(); }
  T &operator*() & { return Storage.value(); }

  template <typename U> constexpr T value_or(U &&alt) const & {
    return has_value() ? operator*() : std::forward<U>(alt);
  }

  LLVM_DEPRECATED("std::optional::value is throwing. Use *X instead", "*X")
  T &&value() && { return std::move(Storage.value()); }
  T &&operator*() && { return std::move(Storage.value()); }

  template <typename U> T value_or(U &&alt) && {
    return has_value() ? std::move(operator*()) : std::forward<U>(alt);
  }
};

template <typename T>
CustomizableOptional(const T &) -> CustomizableOptional<T>;

template <class T>
llvm::hash_code hash_value(const CustomizableOptional<T> &O) {
  return O ? llvm::hash_combine(true, *O) : llvm::hash_value(false);
}

template <typename T, typename U>
constexpr bool operator==(const CustomizableOptional<T> &X,
                          const CustomizableOptional<U> &Y) {
  if (X && Y)
    return *X == *Y;
  return X.has_value() == Y.has_value();
}

template <typename T, typename U>
constexpr bool operator!=(const CustomizableOptional<T> &X,
                          const CustomizableOptional<U> &Y) {
  return !(X == Y);
}

template <typename T, typename U>
constexpr bool operator<(const CustomizableOptional<T> &X,
                         const CustomizableOptional<U> &Y) {
  if (X && Y)
    return *X < *Y;
  return X.has_value() < Y.has_value();
}

template <typename T, typename U>
constexpr bool operator<=(const CustomizableOptional<T> &X,
                          const CustomizableOptional<U> &Y) {
  return !(Y < X);
}

template <typename T, typename U>
constexpr bool operator>(const CustomizableOptional<T> &X,
                         const CustomizableOptional<U> &Y) {
  return Y < X;
}

template <typename T, typename U>
constexpr bool operator>=(const CustomizableOptional<T> &X,
                          const CustomizableOptional<U> &Y) {
  return !(X < Y);
}

template <typename T>
constexpr bool operator==(const CustomizableOptional<T> &X, std::nullopt_t) {
  return !X;
}

template <typename T>
constexpr bool operator==(std::nullopt_t, const CustomizableOptional<T> &X) {
  return X == std::nullopt;
}

template <typename T>
constexpr bool operator!=(const CustomizableOptional<T> &X, std::nullopt_t) {
  return !(X == std::nullopt);
}

template <typename T>
constexpr bool operator!=(std::nullopt_t, const CustomizableOptional<T> &X) {
  return X != std::nullopt;
}

template <typename T>
constexpr bool operator<(const CustomizableOptional<T> &, std::nullopt_t) {
  return false;
}

template <typename T>
constexpr bool operator<(std::nullopt_t, const CustomizableOptional<T> &X) {
  return X.has_value();
}

template <typename T>
constexpr bool operator<=(const CustomizableOptional<T> &X, std::nullopt_t) {
  return !(std::nullopt < X);
}

template <typename T>
constexpr bool operator<=(std::nullopt_t, const CustomizableOptional<T> &X) {
  return !(X < std::nullopt);
}

template <typename T>
constexpr bool operator>(const CustomizableOptional<T> &X, std::nullopt_t) {
  return std::nullopt < X;
}

template <typename T>
constexpr bool operator>(std::nullopt_t, const CustomizableOptional<T> &X) {
  return X < std::nullopt;
}

template <typename T>
constexpr bool operator>=(const CustomizableOptional<T> &X, std::nullopt_t) {
  return std::nullopt <= X;
}

template <typename T>
constexpr bool operator>=(std::nullopt_t, const CustomizableOptional<T> &X) {
  return X <= std::nullopt;
}

template <typename T>
constexpr bool operator==(const CustomizableOptional<T> &X, const T &Y) {
  return X && *X == Y;
}

template <typename T>
constexpr bool operator==(const T &X, const CustomizableOptional<T> &Y) {
  return Y && X == *Y;
}

template <typename T>
constexpr bool operator!=(const CustomizableOptional<T> &X, const T &Y) {
  return !(X == Y);
}

template <typename T>
constexpr bool operator!=(const T &X, const CustomizableOptional<T> &Y) {
  return !(X == Y);
}

template <typename T>
constexpr bool operator<(const CustomizableOptional<T> &X, const T &Y) {
  return !X || *X < Y;
}

template <typename T>
constexpr bool operator<(const T &X, const CustomizableOptional<T> &Y) {
  return Y && X < *Y;
}

template <typename T>
constexpr bool operator<=(const CustomizableOptional<T> &X, const T &Y) {
  return !(Y < X);
}

template <typename T>
constexpr bool operator<=(const T &X, const CustomizableOptional<T> &Y) {
  return !(Y < X);
}

template <typename T>
constexpr bool operator>(const CustomizableOptional<T> &X, const T &Y) {
  return Y < X;
}

template <typename T>
constexpr bool operator>(const T &X, const CustomizableOptional<T> &Y) {
  return Y < X;
}

template <typename T>
constexpr bool operator>=(const CustomizableOptional<T> &X, const T &Y) {
  return !(X < Y);
}

template <typename T>
constexpr bool operator>=(const T &X, const CustomizableOptional<T> &Y) {
  return !(X < Y);
}

} // namespace clang

#endif // CLANG_BASIC_CUSTOMIZABLEOPTIONAL_H
