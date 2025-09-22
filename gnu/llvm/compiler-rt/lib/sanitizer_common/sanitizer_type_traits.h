//===-- sanitizer_type_traits.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements a subset of C++ type traits. This is so we can avoid depending
// on system C++ headers.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_TYPE_TRAITS_H
#define SANITIZER_TYPE_TRAITS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __sanitizer {

struct true_type {
  static const bool value = true;
};

struct false_type {
  static const bool value = false;
};

// is_same<T, U>
//
// Type trait to compare if types are the same.
// E.g.
//
// ```
// is_same<int,int>::value - True
// is_same<int,char>::value - False
// ```
template <typename T, typename U>
struct is_same : public false_type {};

template <typename T>
struct is_same<T, T> : public true_type {};

// conditional<B, T, F>
//
// Defines type as T if B is true or as F otherwise.
// E.g. the following is true
//
// ```
// is_same<int, conditional<true, int, double>::type>::value
// is_same<double, conditional<false, int, double>::type>::value
// ```
template <bool B, class T, class F>
struct conditional {
  using type = T;
};

template <class T, class F>
struct conditional<false, T, F> {
  using type = F;
};

template <class T>
struct remove_reference {
  using type = T;
};
template <class T>
struct remove_reference<T&> {
  using type = T;
};
template <class T>
struct remove_reference<T&&> {
  using type = T;
};

template <class T>
WARN_UNUSED_RESULT inline typename remove_reference<T>::type&& move(T&& t) {
  return static_cast<typename remove_reference<T>::type&&>(t);
}

template <class T>
WARN_UNUSED_RESULT inline constexpr T&& forward(
    typename remove_reference<T>::type& t) {
  return static_cast<T&&>(t);
}

template <class T>
WARN_UNUSED_RESULT inline constexpr T&& forward(
    typename remove_reference<T>::type&& t) {
  return static_cast<T&&>(t);
}

template <class T, T v>
struct integral_constant {
  static constexpr const T value = v;
  typedef T value_type;
  typedef integral_constant type;
  constexpr operator value_type() const { return value; }
  constexpr value_type operator()() const { return value; }
};

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#if __has_builtin(__is_trivially_destructible)

template <class T>
struct is_trivially_destructible
    : public integral_constant<bool, __is_trivially_destructible(T)> {};

#elif __has_builtin(__has_trivial_destructor)

template <class T>
struct is_trivially_destructible
    : public integral_constant<bool, __has_trivial_destructor(T)> {};

#else

template <class T>
struct is_trivially_destructible
    : public integral_constant<bool, /* less efficient fallback */ false> {};

#endif

#if __has_builtin(__is_trivially_copyable)

template <class T>
struct is_trivially_copyable
    : public integral_constant<bool, __is_trivially_copyable(T)> {};

#else

template <class T>
struct is_trivially_copyable
    : public integral_constant<bool, /* less efficient fallback */ false> {};

#endif

}  // namespace __sanitizer

#endif
