// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ADVANCE_H
#define _LIBCPP___ITERATOR_ADVANCE_H

#include <__assert>
#include <__concepts/assignable.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_integral.h>
#include <__utility/convert_to_integral.h>
#include <__utility/declval.h>
#include <__utility/move.h>
#include <__utility/unreachable.h>
#include <limits>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _InputIter>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 void
__advance(_InputIter& __i, typename iterator_traits<_InputIter>::difference_type __n, input_iterator_tag) {
  for (; __n > 0; --__n)
    ++__i;
}

template <class _BiDirIter>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 void
__advance(_BiDirIter& __i, typename iterator_traits<_BiDirIter>::difference_type __n, bidirectional_iterator_tag) {
  if (__n >= 0)
    for (; __n > 0; --__n)
      ++__i;
  else
    for (; __n < 0; ++__n)
      --__i;
}

template <class _RandIter>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 void
__advance(_RandIter& __i, typename iterator_traits<_RandIter>::difference_type __n, random_access_iterator_tag) {
  __i += __n;
}

template < class _InputIter,
           class _Distance,
           class _IntegralDistance = decltype(std::__convert_to_integral(std::declval<_Distance>())),
           __enable_if_t<is_integral<_IntegralDistance>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 void advance(_InputIter& __i, _Distance __orig_n) {
  typedef typename iterator_traits<_InputIter>::difference_type _Difference;
  _Difference __n = static_cast<_Difference>(std::__convert_to_integral(__orig_n));
  // Calling `advance` with a negative value on a non-bidirectional iterator is a no-op in the current implementation.
  _LIBCPP_ASSERT_PEDANTIC(__n >= 0 || __has_bidirectional_iterator_category<_InputIter>::value,
                          "Attempt to advance(it, n) with negative n on a non-bidirectional iterator");
  std::__advance(__i, __n, typename iterator_traits<_InputIter>::iterator_category());
}

#if _LIBCPP_STD_VER >= 20

// [range.iter.op.advance]

namespace ranges {
namespace __advance {

struct __fn {
private:
  template <class _Ip>
  _LIBCPP_HIDE_FROM_ABI static constexpr void __advance_forward(_Ip& __i, iter_difference_t<_Ip> __n) {
    while (__n > 0) {
      --__n;
      ++__i;
    }
  }

  template <class _Ip>
  _LIBCPP_HIDE_FROM_ABI static constexpr void __advance_backward(_Ip& __i, iter_difference_t<_Ip> __n) {
    while (__n < 0) {
      ++__n;
      --__i;
    }
  }

public:
  // Preconditions: If `I` does not model `bidirectional_iterator`, `n` is not negative.
  template <input_or_output_iterator _Ip>
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()(_Ip& __i, iter_difference_t<_Ip> __n) const {
    // Calling `advance` with a negative value on a non-bidirectional iterator is a no-op in the current implementation.
    _LIBCPP_ASSERT_PEDANTIC(
        __n >= 0 || bidirectional_iterator<_Ip>, "If `n < 0`, then `bidirectional_iterator<I>` must be true.");

    // If `I` models `random_access_iterator`, equivalent to `i += n`.
    if constexpr (random_access_iterator<_Ip>) {
      __i += __n;
      return;
    } else if constexpr (bidirectional_iterator<_Ip>) {
      // Otherwise, if `n` is non-negative, increments `i` by `n`.
      __advance_forward(__i, __n);
      // Otherwise, decrements `i` by `-n`.
      __advance_backward(__i, __n);
      return;
    } else {
      // Otherwise, if `n` is non-negative, increments `i` by `n`.
      __advance_forward(__i, __n);
      return;
    }
  }

  // Preconditions: Either `assignable_from<I&, S> || sized_sentinel_for<S, I>` is modeled, or [i, bound_sentinel)
  // denotes a range.
  template <input_or_output_iterator _Ip, sentinel_for<_Ip> _Sp>
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()(_Ip& __i, _Sp __bound_sentinel) const {
    // If `I` and `S` model `assignable_from<I&, S>`, equivalent to `i = std::move(bound_sentinel)`.
    if constexpr (assignable_from<_Ip&, _Sp>) {
      __i = std::move(__bound_sentinel);
    }
    // Otherwise, if `S` and `I` model `sized_sentinel_for<S, I>`, equivalent to `ranges::advance(i, bound_sentinel -
    // i)`.
    else if constexpr (sized_sentinel_for<_Sp, _Ip>) {
      (*this)(__i, __bound_sentinel - __i);
    }
    // Otherwise, while `bool(i != bound_sentinel)` is true, increments `i`.
    else {
      while (__i != __bound_sentinel) {
        ++__i;
      }
    }
  }

  // Preconditions:
  //   * If `n > 0`, [i, bound_sentinel) denotes a range.
  //   * If `n == 0`, [i, bound_sentinel) or [bound_sentinel, i) denotes a range.
  //   * If `n < 0`, [bound_sentinel, i) denotes a range, `I` models `bidirectional_iterator`, and `I` and `S` model
  //   `same_as<I, S>`.
  // Returns: `n - M`, where `M` is the difference between the ending and starting position.
  template <input_or_output_iterator _Ip, sentinel_for<_Ip> _Sp>
  _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Ip>
  operator()(_Ip& __i, iter_difference_t<_Ip> __n, _Sp __bound_sentinel) const {
    // Calling `advance` with a negative value on a non-bidirectional iterator is a no-op in the current implementation.
    _LIBCPP_ASSERT_PEDANTIC((__n >= 0) || (bidirectional_iterator<_Ip> && same_as<_Ip, _Sp>),
                            "If `n < 0`, then `bidirectional_iterator<I> && same_as<I, S>` must be true.");
    // If `S` and `I` model `sized_sentinel_for<S, I>`:
    if constexpr (sized_sentinel_for<_Sp, _Ip>) {
      // If |n| >= |bound_sentinel - i|, equivalent to `ranges::advance(i, bound_sentinel)`.
      // __magnitude_geq(a, b) returns |a| >= |b|, assuming they have the same sign.
      auto __magnitude_geq = [](auto __a, auto __b) { return __a == 0 ? __b == 0 : __a > 0 ? __a >= __b : __a <= __b; };
      if (const auto __m = __bound_sentinel - __i; __magnitude_geq(__n, __m)) {
        (*this)(__i, __bound_sentinel);
        return __n - __m;
      }

      // Otherwise, equivalent to `ranges::advance(i, n)`.
      (*this)(__i, __n);
      return 0;
    } else {
      // Otherwise, if `n` is non-negative, while `bool(i != bound_sentinel)` is true, increments `i` but at
      // most `n` times.
      while (__n > 0 && __i != __bound_sentinel) {
        ++__i;
        --__n;
      }

      // Otherwise, while `bool(i != bound_sentinel)` is true, decrements `i` but at most `-n` times.
      if constexpr (bidirectional_iterator<_Ip> && same_as<_Ip, _Sp>) {
        while (__n < 0 && __i != __bound_sentinel) {
          --__i;
          ++__n;
        }
      }
      return __n;
    }

    __libcpp_unreachable();
  }
};

} // namespace __advance

inline namespace __cpo {
inline constexpr auto advance = __advance::__fn{};
} // namespace __cpo
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_ADVANCE_H
