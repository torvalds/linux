//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STRING_CONSTEXPR_C_FUNCTIONS_H
#define _LIBCPP___STRING_CONSTEXPR_C_FUNCTIONS_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory/construct_at.h>
#include <__type_traits/datasizeof.h>
#include <__type_traits/is_always_bitcastable.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_equality_comparable.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_trivially_copyable.h>
#include <__type_traits/is_trivially_lexicographically_comparable.h>
#include <__type_traits/remove_cv.h>
#include <__utility/is_pointer_in_range.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// Type used to encode that a function takes an integer that represents a number
// of elements as opposed to a number of bytes.
enum class __element_count : size_t {};

template <class _Tp>
inline const bool __is_char_type = false;

template <>
inline const bool __is_char_type<char> = true;

#ifndef _LIBCPP_HAS_NO_CHAR8_T
template <>
inline const bool __is_char_type<char8_t> = true;
#endif

template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 size_t __constexpr_strlen(const _Tp* __str) _NOEXCEPT {
  static_assert(__is_char_type<_Tp>, "__constexpr_strlen only works with char and char8_t");
  // GCC currently doesn't support __builtin_strlen for heap-allocated memory during constant evaluation.
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=70816
  if (__libcpp_is_constant_evaluated()) {
#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_COMPILER_CLANG_BASED)
    if constexpr (is_same_v<_Tp, char>)
      return __builtin_strlen(__str);
#endif
    size_t __i = 0;
    for (; __str[__i] != '\0'; ++__i)
      ;
    return __i;
  }
  return __builtin_strlen(reinterpret_cast<const char*>(__str));
}

// Because of __libcpp_is_trivially_lexicographically_comparable we know that comparing the object representations is
// equivalent to a std::memcmp. Since we have multiple objects contiguously in memory, we can call memcmp once instead
// of invoking it on every object individually.
template <class _Tp, class _Up>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 int
__constexpr_memcmp(const _Tp* __lhs, const _Up* __rhs, __element_count __n) {
  static_assert(__libcpp_is_trivially_lexicographically_comparable<_Tp, _Up>::value,
                "_Tp and _Up have to be trivially lexicographically comparable");

  auto __count = static_cast<size_t>(__n);

  if (__libcpp_is_constant_evaluated()) {
#ifdef _LIBCPP_COMPILER_CLANG_BASED
    if (sizeof(_Tp) == 1 && !is_same<_Tp, bool>::value)
      return __builtin_memcmp(__lhs, __rhs, __count * sizeof(_Tp));
#endif

    while (__count != 0) {
      if (*__lhs < *__rhs)
        return -1;
      if (*__rhs < *__lhs)
        return 1;

      --__count;
      ++__lhs;
      ++__rhs;
    }
    return 0;
  } else {
    return __builtin_memcmp(__lhs, __rhs, __count * sizeof(_Tp));
  }
}

// Because of __libcpp_is_trivially_equality_comparable we know that comparing the object representations is equivalent
// to a std::memcmp(...) == 0. Since we have multiple objects contiguously in memory, we can call memcmp once instead
// of invoking it on every object individually.
template <class _Tp, class _Up>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 bool
__constexpr_memcmp_equal(const _Tp* __lhs, const _Up* __rhs, __element_count __n) {
  static_assert(__libcpp_is_trivially_equality_comparable<_Tp, _Up>::value,
                "_Tp and _Up have to be trivially equality comparable");

  auto __count = static_cast<size_t>(__n);

  if (__libcpp_is_constant_evaluated()) {
#ifdef _LIBCPP_COMPILER_CLANG_BASED
    if (sizeof(_Tp) == 1 && is_integral<_Tp>::value && !is_same<_Tp, bool>::value)
      return __builtin_memcmp(__lhs, __rhs, __count * sizeof(_Tp)) == 0;
#endif
    while (__count != 0) {
      if (*__lhs != *__rhs)
        return false;

      --__count;
      ++__lhs;
      ++__rhs;
    }
    return true;
  } else {
    return ::__builtin_memcmp(__lhs, __rhs, __count * sizeof(_Tp)) == 0;
  }
}

template <class _Tp, class _Up>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp* __constexpr_memchr(_Tp* __str, _Up __value, size_t __count) {
  static_assert(sizeof(_Tp) == 1 && __libcpp_is_trivially_equality_comparable<_Tp, _Up>::value,
                "Calling memchr on non-trivially equality comparable types is unsafe.");

  if (__libcpp_is_constant_evaluated()) {
// use __builtin_char_memchr to optimize constexpr evaluation if we can
#if _LIBCPP_STD_VER >= 17 && __has_builtin(__builtin_char_memchr)
    if constexpr (is_same_v<remove_cv_t<_Tp>, char> && is_same_v<remove_cv_t<_Up>, char>)
      return __builtin_char_memchr(__str, __value, __count);
#endif

    for (; __count; --__count) {
      if (*__str == __value)
        return __str;
      ++__str;
    }
    return nullptr;
  } else {
    char __value_buffer = 0;
    __builtin_memcpy(&__value_buffer, &__value, sizeof(char));
    return static_cast<_Tp*>(__builtin_memchr(__str, __value_buffer, __count));
  }
}

// This function performs an assignment to an existing, already alive TriviallyCopyable object
// from another TriviallyCopyable object.
//
// It basically works around the fact that TriviallyCopyable objects are not required to be
// syntactically copy/move constructible or copy/move assignable. Technically, only one of the
// four operations is required to be syntactically valid -- but at least one definitely has to
// be valid.
//
// This is necessary in order to implement __constexpr_memmove below in a way that mirrors as
// closely as possible what the compiler's __builtin_memmove is able to do.
template <class _Tp, class _Up, __enable_if_t<is_assignable<_Tp&, _Up const&>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp& __assign_trivially_copyable(_Tp& __dest, _Up const& __src) {
  __dest = __src;
  return __dest;
}

// clang-format off
template <class _Tp, class _Up, __enable_if_t<!is_assignable<_Tp&, _Up const&>::value &&
                                               is_assignable<_Tp&, _Up&&>::value, int> = 0>
// clang-format on
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp& __assign_trivially_copyable(_Tp& __dest, _Up& __src) {
  __dest =
      static_cast<_Up&&>(__src); // this is safe, we're not actually moving anything since the assignment is trivial
  return __dest;
}

// clang-format off
template <class _Tp, class _Up, __enable_if_t<!is_assignable<_Tp&, _Up const&>::value &&
                                              !is_assignable<_Tp&, _Up&&>::value &&
                                               is_constructible<_Tp, _Up const&>::value, int> = 0>
// clang-format on
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp& __assign_trivially_copyable(_Tp& __dest, _Up const& __src) {
  // _Tp is trivially destructible, so we don't need to call its destructor to end the lifetime of the object
  // that was there previously
  std::__construct_at(std::addressof(__dest), __src);
  return __dest;
}

// clang-format off
template <class _Tp, class _Up, __enable_if_t<!is_assignable<_Tp&, _Up const&>::value &&
                                              !is_assignable<_Tp&, _Up&&>::value &&
                                              !is_constructible<_Tp, _Up const&>::value &&
                                               is_constructible<_Tp, _Up&&>::value, int> = 0>
// clang-format on
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp& __assign_trivially_copyable(_Tp& __dest, _Up& __src) {
  // _Tp is trivially destructible, so we don't need to call its destructor to end the lifetime of the object
  // that was there previously
  std::__construct_at(
      std::addressof(__dest),
      static_cast<_Up&&>(__src)); // this is safe, we're not actually moving anything since the constructor is trivial
  return __dest;
}

template <class _Tp, class _Up, __enable_if_t<__is_always_bitcastable<_Up, _Tp>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp*
__constexpr_memmove(_Tp* __dest, _Up* __src, __element_count __n) {
  size_t __count = static_cast<size_t>(__n);
  if (__libcpp_is_constant_evaluated()) {
#ifdef _LIBCPP_COMPILER_CLANG_BASED
    if (is_same<__remove_cv_t<_Tp>, __remove_cv_t<_Up> >::value) {
      ::__builtin_memmove(__dest, __src, __count * sizeof(_Tp));
      return __dest;
    }
#endif
    if (std::__is_pointer_in_range(__src, __src + __count, __dest)) {
      for (; __count > 0; --__count)
        std::__assign_trivially_copyable(__dest[__count - 1], __src[__count - 1]);
    } else {
      for (size_t __i = 0; __i != __count; ++__i)
        std::__assign_trivially_copyable(__dest[__i], __src[__i]);
    }
  } else if (__count > 0) {
    ::__builtin_memmove(__dest, __src, (__count - 1) * sizeof(_Tp) + __datasizeof_v<_Tp>);
  }
  return __dest;
}

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___STRING_CONSTEXPR_C_FUNCTIONS_H
