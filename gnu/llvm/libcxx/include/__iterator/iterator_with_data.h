//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_ITERATOR_WITH_DATA_H
#define _LIBCPP___ITERATOR_ITERATOR_WITH_DATA_H

#include <__compare/compare_three_way_result.h>
#include <__compare/three_way_comparable.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/readable_traits.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

template <forward_iterator _Iterator, class _Data>
class __iterator_with_data {
  _Iterator __iter_{};
  _Data __data_{};

public:
  using value_type      = iter_value_t<_Iterator>;
  using difference_type = iter_difference_t<_Iterator>;

  _LIBCPP_HIDE_FROM_ABI __iterator_with_data() = default;

  constexpr _LIBCPP_HIDE_FROM_ABI __iterator_with_data(_Iterator __iter, _Data __data)
      : __iter_(std::move(__iter)), __data_(std::move(__data)) {}

  constexpr _LIBCPP_HIDE_FROM_ABI _Iterator __get_iter() const { return __iter_; }

  constexpr _LIBCPP_HIDE_FROM_ABI _Data __get_data() && { return std::move(__data_); }

  friend constexpr _LIBCPP_HIDE_FROM_ABI bool
  operator==(const __iterator_with_data& __lhs, const __iterator_with_data& __rhs) {
    return __lhs.__iter_ == __rhs.__iter_;
  }

  constexpr _LIBCPP_HIDE_FROM_ABI __iterator_with_data& operator++() {
    ++__iter_;
    return *this;
  }

  constexpr _LIBCPP_HIDE_FROM_ABI __iterator_with_data operator++(int) {
    auto __tmp = *this;
    __iter_++;
    return __tmp;
  }

  constexpr _LIBCPP_HIDE_FROM_ABI __iterator_with_data& operator--()
    requires bidirectional_iterator<_Iterator>
  {
    --__iter_;
    return *this;
  }

  constexpr _LIBCPP_HIDE_FROM_ABI __iterator_with_data operator--(int)
    requires bidirectional_iterator<_Iterator>
  {
    auto __tmp = *this;
    --__iter_;
    return __tmp;
  }

  constexpr _LIBCPP_HIDE_FROM_ABI iter_reference_t<_Iterator> operator*() const { return *__iter_; }

  _LIBCPP_HIDE_FROM_ABI friend constexpr iter_rvalue_reference_t<_Iterator>
  iter_move(const __iterator_with_data& __iter) noexcept(noexcept(ranges::iter_move(__iter.__iter_))) {
    return ranges::iter_move(__iter.__iter_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void
  iter_swap(const __iterator_with_data& __lhs,
            const __iterator_with_data& __rhs) noexcept(noexcept(ranges::iter_swap(__lhs.__iter_, __rhs.__iter_)))
    requires indirectly_swappable<_Iterator>
  {
    return ranges::iter_swap(__lhs.__data_, __rhs.__iter_);
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_ITERATOR_WITH_DATA_H
