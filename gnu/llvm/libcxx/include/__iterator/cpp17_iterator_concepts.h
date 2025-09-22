//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_CPP17_ITERATOR_CONCEPTS_H
#define _LIBCPP___ITERATOR_CPP17_ITERATOR_CONCEPTS_H

#include <__concepts/boolean_testable.h>
#include <__concepts/convertible_to.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_signed.h>
#include <__type_traits/is_void.h>
#include <__utility/as_const.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <__utility/swap.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Tp>
concept __cpp17_move_constructible = is_move_constructible_v<_Tp>;

template <class _Tp>
concept __cpp17_copy_constructible = __cpp17_move_constructible<_Tp> && is_copy_constructible_v<_Tp>;

template <class _Tp>
concept __cpp17_move_assignable = requires(_Tp __lhs, _Tp __rhs) {
  { __lhs = std::move(__rhs) } -> same_as<_Tp&>;
};

template <class _Tp>
concept __cpp17_copy_assignable = __cpp17_move_assignable<_Tp> && requires(_Tp __lhs, _Tp __rhs) {
  { __lhs = __rhs } -> same_as<_Tp&>;
  { __lhs = std::as_const(__rhs) } -> same_as<_Tp&>;
};

template <class _Tp>
concept __cpp17_destructible = requires(_Tp __v) { __v.~_Tp(); };

template <class _Tp>
concept __cpp17_equality_comparable = requires(_Tp __lhs, _Tp __rhs) {
  { __lhs == __rhs } -> __boolean_testable;
  { std::as_const(__lhs) == __rhs } -> __boolean_testable;
  { __lhs == std::as_const(__rhs) } -> __boolean_testable;
  { std::as_const(__lhs) == std::as_const(__rhs) } -> __boolean_testable;
};

template <class _Tp>
concept __cpp17_default_constructible = is_default_constructible_v<_Tp>;

template <class _Iter>
concept __cpp17_iterator =
    __cpp17_copy_constructible<_Iter> && __cpp17_copy_assignable<_Iter> && __cpp17_destructible<_Iter> &&
    (is_signed_v<__iter_diff_t<_Iter>> || is_void_v<__iter_diff_t<_Iter>>) && requires(_Iter __iter) {
      { *__iter };
      { ++__iter } -> same_as<_Iter&>;
    };

template <class _Iter>
concept __cpp17_input_iterator =
    __cpp17_iterator<_Iter> && __cpp17_equality_comparable<_Iter> && requires(_Iter __lhs, _Iter __rhs) {
      { __lhs != __rhs } -> __boolean_testable;
      { std::as_const(__lhs) != __rhs } -> __boolean_testable;
      { __lhs != std::as_const(__rhs) } -> __boolean_testable;
      { std::as_const(__lhs) != std::as_const(__rhs) } -> __boolean_testable;

      { *__lhs } -> same_as<__iter_reference<_Iter>>;
      { *std::as_const(__lhs) } -> same_as<__iter_reference<_Iter>>;

      { ++__lhs } -> same_as<_Iter&>;
      { (void)__lhs++ };
      { *__lhs++ };
    };

template <class _Iter, class _WriteTo>
concept __cpp17_output_iterator = __cpp17_iterator<_Iter> && requires(_Iter __iter, _WriteTo __write) {
  { *__iter = std::forward<_WriteTo>(__write) };
  { ++__iter } -> same_as<_Iter&>;
  { __iter++ } -> convertible_to<const _Iter&>;
  { *__iter++ = std::forward<_WriteTo>(__write) };
};

template <class _Iter>
concept __cpp17_forward_iterator =
    __cpp17_input_iterator<_Iter> && __cpp17_default_constructible<_Iter> && requires(_Iter __iter) {
      { __iter++ } -> convertible_to<const _Iter&>;
      { *__iter++ } -> same_as<__iter_reference<_Iter>>;
    };

template <class _Iter>
concept __cpp17_bidirectional_iterator = __cpp17_forward_iterator<_Iter> && requires(_Iter __iter) {
  { --__iter } -> same_as<_Iter&>;
  { __iter-- } -> convertible_to<const _Iter&>;
  { *__iter-- } -> same_as<__iter_reference<_Iter>>;
};

template <class _Iter>
concept __cpp17_random_access_iterator =
    __cpp17_bidirectional_iterator<_Iter> && requires(_Iter __iter, __iter_diff_t<_Iter> __n) {
      { __iter += __n } -> same_as<_Iter&>;

      { __iter + __n } -> same_as<_Iter>;
      { __n + __iter } -> same_as<_Iter>;
      { std::as_const(__iter) + __n } -> same_as<_Iter>;
      { __n + std::as_const(__iter) } -> same_as<_Iter>;

      { __iter -= __n } -> same_as<_Iter&>;
      { __iter - __n } -> same_as<_Iter>;
      { std::as_const(__iter) - __n } -> same_as<_Iter>;

      { __iter - __iter } -> same_as<__iter_diff_t<_Iter>>;
      { std::as_const(__iter) - __iter } -> same_as<__iter_diff_t<_Iter>>;
      { __iter - std::as_const(__iter) } -> same_as<__iter_diff_t<_Iter>>;
      { std::as_const(__iter) - std::as_const(__iter) } -> same_as<__iter_diff_t<_Iter>>;

      { __iter[__n] } -> convertible_to<__iter_reference<_Iter>>;
      { std::as_const(__iter)[__n] } -> convertible_to<__iter_reference<_Iter>>;

      { __iter < __iter } -> __boolean_testable;
      { std::as_const(__iter) < __iter } -> __boolean_testable;
      { __iter < std::as_const(__iter) } -> __boolean_testable;
      { std::as_const(__iter) < std::as_const(__iter) } -> __boolean_testable;

      { __iter > __iter } -> __boolean_testable;
      { std::as_const(__iter) > __iter } -> __boolean_testable;
      { __iter > std::as_const(__iter) } -> __boolean_testable;
      { std::as_const(__iter) > std::as_const(__iter) } -> __boolean_testable;

      { __iter >= __iter } -> __boolean_testable;
      { std::as_const(__iter) >= __iter } -> __boolean_testable;
      { __iter >= std::as_const(__iter) } -> __boolean_testable;
      { std::as_const(__iter) >= std::as_const(__iter) } -> __boolean_testable;

      { __iter <= __iter } -> __boolean_testable;
      { std::as_const(__iter) <= __iter } -> __boolean_testable;
      { __iter <= std::as_const(__iter) } -> __boolean_testable;
      { std::as_const(__iter) <= std::as_const(__iter) } -> __boolean_testable;
    };

_LIBCPP_END_NAMESPACE_STD

#  ifndef _LIBCPP_DISABLE_ITERATOR_CHECKS
#    define _LIBCPP_REQUIRE_CPP17_INPUT_ITERATOR(iter_t, message)                                                      \
      static_assert(::std::__cpp17_input_iterator<iter_t>, message)
#    define _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(iter_t, write_t, message)                                            \
      static_assert(::std::__cpp17_output_iterator<iter_t, write_t>, message)
#    define _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(iter_t, message)                                                    \
      static_assert(::std::__cpp17_forward_iterator<iter_t>, message)
#    define _LIBCPP_REQUIRE_CPP17_BIDIRECTIONAL_ITERATOR(iter_t, message)                                              \
      static_assert(::std::__cpp17_bidirectional_iterator<iter_t>, message)
#    define _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(iter_t, message)                                              \
      static_assert(::std::__cpp17_random_access_iterator<iter_t>, message)
#  else
#    define _LIBCPP_REQUIRE_CPP17_INPUT_ITERATOR(iter_t, message) static_assert(true)
#    define _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(iter_t, write_t, message) static_assert(true)
#    define _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(iter_t, message) static_assert(true)
#    define _LIBCPP_REQUIRE_CPP17_BIDIRECTIONAL_ITERATOR(iter_t, message) static_assert(true)
#    define _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(iter_t, message) static_assert(true)
#  endif

#else // _LIBCPP_STD_VER >= 20

#  define _LIBCPP_REQUIRE_CPP17_INPUT_ITERATOR(iter_t, message) static_assert(true)
#  define _LIBCPP_REQUIRE_CPP17_OUTPUT_ITERATOR(iter_t, write_t, message) static_assert(true)
#  define _LIBCPP_REQUIRE_CPP17_FORWARD_ITERATOR(iter_t, message) static_assert(true)
#  define _LIBCPP_REQUIRE_CPP17_BIDIRECTIONAL_ITERATOR(iter_t, message) static_assert(true)
#  define _LIBCPP_REQUIRE_CPP17_RANDOM_ACCESS_ITERATOR(iter_t, message) static_assert(true)

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_CPP17_ITERATOR_CONCEPTS_H
