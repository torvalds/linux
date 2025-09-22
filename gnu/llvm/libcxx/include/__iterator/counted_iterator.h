// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_COUNTED_ITERATOR_H
#define _LIBCPP___ITERATOR_COUNTED_ITERATOR_H

#include <__assert>
#include <__concepts/assignable.h>
#include <__concepts/common_with.h>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/readable_traits.h>
#include <__memory/pointer_traits.h>
#include <__type_traits/add_pointer.h>
#include <__type_traits/conditional.h>
#include <__utility/move.h>
#include <compare>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

template <class>
struct __counted_iterator_concept {};

template <class _Iter>
  requires requires { typename _Iter::iterator_concept; }
struct __counted_iterator_concept<_Iter> {
  using iterator_concept = typename _Iter::iterator_concept;
};

template <class>
struct __counted_iterator_category {};

template <class _Iter>
  requires requires { typename _Iter::iterator_category; }
struct __counted_iterator_category<_Iter> {
  using iterator_category = typename _Iter::iterator_category;
};

template <class>
struct __counted_iterator_value_type {};

template <indirectly_readable _Iter>
struct __counted_iterator_value_type<_Iter> {
  using value_type = iter_value_t<_Iter>;
};

template <input_or_output_iterator _Iter>
class counted_iterator
    : public __counted_iterator_concept<_Iter>,
      public __counted_iterator_category<_Iter>,
      public __counted_iterator_value_type<_Iter> {
public:
  using iterator_type   = _Iter;
  using difference_type = iter_difference_t<_Iter>;

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator()
    requires default_initializable<_Iter>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator(_Iter __iter, iter_difference_t<_Iter> __n)
      : __current_(std::move(__iter)), __count_(__n) {
    _LIBCPP_ASSERT_UNCATEGORIZED(__n >= 0, "__n must not be negative.");
  }

  template <class _I2>
    requires convertible_to<const _I2&, _Iter>
  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator(const counted_iterator<_I2>& __other)
      : __current_(__other.__current_), __count_(__other.__count_) {}

  template <class _I2>
    requires assignable_from<_Iter&, const _I2&>
  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator& operator=(const counted_iterator<_I2>& __other) {
    __current_ = __other.__current_;
    __count_   = __other.__count_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Iter& base() const& noexcept { return __current_; }

  _LIBCPP_HIDE_FROM_ABI constexpr _Iter base() && { return std::move(__current_); }

  _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Iter> count() const noexcept { return __count_; }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator*() {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__count_ > 0, "Iterator is equal to or past end.");
    return *__current_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator*() const
    requires __dereferenceable<const _Iter>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__count_ > 0, "Iterator is equal to or past end.");
    return *__current_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto operator->() const noexcept
    requires contiguous_iterator<_Iter>
  {
    return std::to_address(__current_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator& operator++() {
    _LIBCPP_ASSERT_UNCATEGORIZED(__count_ > 0, "Iterator already at or past end.");
    ++__current_;
    --__count_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator++(int) {
    _LIBCPP_ASSERT_UNCATEGORIZED(__count_ > 0, "Iterator already at or past end.");
    --__count_;
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
    try {
      return __current_++;
    } catch (...) {
      ++__count_;
      throw;
    }
#  else
    return __current_++;
#  endif // _LIBCPP_HAS_NO_EXCEPTIONS
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator operator++(int)
    requires forward_iterator<_Iter>
  {
    _LIBCPP_ASSERT_UNCATEGORIZED(__count_ > 0, "Iterator already at or past end.");
    counted_iterator __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator& operator--()
    requires bidirectional_iterator<_Iter>
  {
    --__current_;
    ++__count_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator operator--(int)
    requires bidirectional_iterator<_Iter>
  {
    counted_iterator __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator operator+(iter_difference_t<_Iter> __n) const
    requires random_access_iterator<_Iter>
  {
    return counted_iterator(__current_ + __n, __count_ - __n);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr counted_iterator
  operator+(iter_difference_t<_Iter> __n, const counted_iterator& __x)
    requires random_access_iterator<_Iter>
  {
    return __x + __n;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator& operator+=(iter_difference_t<_Iter> __n)
    requires random_access_iterator<_Iter>
  {
    _LIBCPP_ASSERT_UNCATEGORIZED(__n <= __count_, "Cannot advance iterator past end.");
    __current_ += __n;
    __count_ -= __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator operator-(iter_difference_t<_Iter> __n) const
    requires random_access_iterator<_Iter>
  {
    return counted_iterator(__current_ - __n, __count_ + __n);
  }

  template <common_with<_Iter> _I2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr iter_difference_t<_I2>
  operator-(const counted_iterator& __lhs, const counted_iterator<_I2>& __rhs) {
    return __rhs.__count_ - __lhs.__count_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr iter_difference_t<_Iter>
  operator-(const counted_iterator& __lhs, default_sentinel_t) {
    return -__lhs.__count_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr iter_difference_t<_Iter>
  operator-(default_sentinel_t, const counted_iterator& __rhs) {
    return __rhs.__count_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr counted_iterator& operator-=(iter_difference_t<_Iter> __n)
    requires random_access_iterator<_Iter>
  {
    _LIBCPP_ASSERT_UNCATEGORIZED(
        -__n <= __count_,
        "Attempt to subtract too large of a size: "
        "counted_iterator would be decremented before the "
        "first element of its range.");
    __current_ -= __n;
    __count_ += __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator[](iter_difference_t<_Iter> __n) const
    requires random_access_iterator<_Iter>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__n < __count_, "Subscript argument must be less than size.");
    return __current_[__n];
  }

  template <common_with<_Iter> _I2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool
  operator==(const counted_iterator& __lhs, const counted_iterator<_I2>& __rhs) {
    return __lhs.__count_ == __rhs.__count_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const counted_iterator& __lhs, default_sentinel_t) {
    return __lhs.__count_ == 0;
  }

  template <common_with<_Iter> _I2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr strong_ordering
  operator<=>(const counted_iterator& __lhs, const counted_iterator<_I2>& __rhs) {
    return __rhs.__count_ <=> __lhs.__count_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr iter_rvalue_reference_t<_Iter>
  iter_move(const counted_iterator& __i) noexcept(noexcept(ranges::iter_move(__i.__current_)))
    requires input_iterator<_Iter>
  {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__i.__count_ > 0, "Iterator must not be past end of range.");
    return ranges::iter_move(__i.__current_);
  }

  template <indirectly_swappable<_Iter> _I2>
  _LIBCPP_HIDE_FROM_ABI friend constexpr void
  iter_swap(const counted_iterator& __x,
            const counted_iterator<_I2>& __y) noexcept(noexcept(ranges::iter_swap(__x.__current_, __y.__current_))) {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __x.__count_ > 0 && __y.__count_ > 0, "Iterators must not be past end of range.");
    return ranges::iter_swap(__x.__current_, __y.__current_);
  }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS _Iter __current_ = _Iter();
  iter_difference_t<_Iter> __count_          = 0;
  template <input_or_output_iterator _OtherIter>
  friend class counted_iterator;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(counted_iterator);

template <input_iterator _Iter>
  requires same_as<_ITER_TRAITS<_Iter>, iterator_traits<_Iter>>
struct iterator_traits<counted_iterator<_Iter>> : iterator_traits<_Iter> {
  using pointer = conditional_t<contiguous_iterator<_Iter>, add_pointer_t<iter_reference_t<_Iter>>, void>;
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_COUNTED_ITERATOR_H
