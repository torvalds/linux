// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ITERATOR_MOVE_ITERATOR_H
#define _LIBCPP___ITERATOR_MOVE_ITERATOR_H

#include <__compare/compare_three_way_result.h>
#include <__compare/three_way_comparable.h>
#include <__concepts/assignable.h>
#include <__concepts/convertible_to.h>
#include <__concepts/derived_from.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/move_sentinel.h>
#include <__iterator/readable_traits.h>
#include <__type_traits/conditional.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_assignable.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/is_convertible.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/is_same.h>
#include <__type_traits/remove_reference.h>
#include <__utility/declval.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20
template <class _Iter, class = void>
struct __move_iter_category_base {};

template <class _Iter>
  requires requires { typename iterator_traits<_Iter>::iterator_category; }
struct __move_iter_category_base<_Iter> {
  using iterator_category =
      _If< derived_from<typename iterator_traits<_Iter>::iterator_category, random_access_iterator_tag>,
           random_access_iterator_tag,
           typename iterator_traits<_Iter>::iterator_category >;
};

template <class _Iter, class _Sent>
concept __move_iter_comparable = requires {
  { std::declval<const _Iter&>() == std::declval<_Sent>() } -> convertible_to<bool>;
};
#endif // _LIBCPP_STD_VER >= 20

template <class _Iter>
class _LIBCPP_TEMPLATE_VIS move_iterator
#if _LIBCPP_STD_VER >= 20
    : public __move_iter_category_base<_Iter>
#endif
{
#if _LIBCPP_STD_VER >= 20

private:
  _LIBCPP_HIDE_FROM_ABI static constexpr auto __get_iter_concept() {
    if constexpr (random_access_iterator<_Iter>) {
      return random_access_iterator_tag{};
    } else if constexpr (bidirectional_iterator<_Iter>) {
      return bidirectional_iterator_tag{};
    } else if constexpr (forward_iterator<_Iter>) {
      return forward_iterator_tag{};
    } else {
      return input_iterator_tag{};
    }
  }
#endif // _LIBCPP_STD_VER >= 20

public:
#if _LIBCPP_STD_VER >= 20
  using iterator_type    = _Iter;
  using iterator_concept = decltype(__get_iter_concept());
  // iterator_category is inherited and not always present
  using value_type      = iter_value_t<_Iter>;
  using difference_type = iter_difference_t<_Iter>;
  using pointer         = _Iter;
  using reference       = iter_rvalue_reference_t<_Iter>;
#else
  typedef _Iter iterator_type;
  typedef _If< __has_random_access_iterator_category<_Iter>::value,
               random_access_iterator_tag,
               typename iterator_traits<_Iter>::iterator_category >
      iterator_category;
  typedef typename iterator_traits<iterator_type>::value_type value_type;
  typedef typename iterator_traits<iterator_type>::difference_type difference_type;
  typedef iterator_type pointer;

  typedef typename iterator_traits<iterator_type>::reference __reference;
  typedef __conditional_t<is_reference<__reference>::value, __libcpp_remove_reference_t<__reference>&&, __reference>
      reference;
#endif // _LIBCPP_STD_VER >= 20

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 explicit move_iterator(_Iter __i) : __current_(std::move(__i)) {}

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator& operator++() {
    ++__current_;
    return *this;
  }

  _LIBCPP_DEPRECATED_IN_CXX20 _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 pointer operator->() const {
    return __current_;
  }

#if _LIBCPP_STD_VER >= 20
  _LIBCPP_HIDE_FROM_ABI constexpr move_iterator()
    requires is_constructible_v<_Iter>
      : __current_() {}

  template <class _Up>
    requires(!_IsSame<_Up, _Iter>::value) && convertible_to<const _Up&, _Iter>
  _LIBCPP_HIDE_FROM_ABI constexpr move_iterator(const move_iterator<_Up>& __u) : __current_(__u.base()) {}

  template <class _Up>
    requires(!_IsSame<_Up, _Iter>::value) && convertible_to<const _Up&, _Iter> && assignable_from<_Iter&, const _Up&>
  _LIBCPP_HIDE_FROM_ABI constexpr move_iterator& operator=(const move_iterator<_Up>& __u) {
    __current_ = __u.base();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Iter& base() const& noexcept { return __current_; }
  _LIBCPP_HIDE_FROM_ABI constexpr _Iter base() && { return std::move(__current_); }

  _LIBCPP_HIDE_FROM_ABI constexpr reference operator*() const { return ranges::iter_move(__current_); }
  _LIBCPP_HIDE_FROM_ABI constexpr reference operator[](difference_type __n) const {
    return ranges::iter_move(__current_ + __n);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto operator++(int)
    requires forward_iterator<_Iter>
  {
    move_iterator __tmp(*this);
    ++__current_;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++__current_; }
#else
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator() : __current_() {}

  template <class _Up, __enable_if_t< !is_same<_Up, _Iter>::value && is_convertible<const _Up&, _Iter>::value, int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator(const move_iterator<_Up>& __u)
      : __current_(__u.base()) {}

  template <class _Up,
            __enable_if_t< !is_same<_Up, _Iter>::value && is_convertible<const _Up&, _Iter>::value &&
                               is_assignable<_Iter&, const _Up&>::value,
                           int> = 0>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator& operator=(const move_iterator<_Up>& __u) {
    __current_ = __u.base();
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 _Iter base() const { return __current_; }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 reference operator*() const {
    return static_cast<reference>(*__current_);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 reference operator[](difference_type __n) const {
    return static_cast<reference>(__current_[__n]);
  }

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator operator++(int) {
    move_iterator __tmp(*this);
    ++__current_;
    return __tmp;
  }
#endif // _LIBCPP_STD_VER >= 20

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator& operator--() {
    --__current_;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator operator--(int) {
    move_iterator __tmp(*this);
    --__current_;
    return __tmp;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator operator+(difference_type __n) const {
    return move_iterator(__current_ + __n);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator& operator+=(difference_type __n) {
    __current_ += __n;
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator operator-(difference_type __n) const {
    return move_iterator(__current_ - __n);
  }
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator& operator-=(difference_type __n) {
    __current_ -= __n;
    return *this;
  }

#if _LIBCPP_STD_VER >= 20
  template <sentinel_for<_Iter> _Sent>
  friend _LIBCPP_HIDE_FROM_ABI constexpr bool operator==(const move_iterator& __x, const move_sentinel<_Sent>& __y)
    requires __move_iter_comparable<_Iter, _Sent>
  {
    return __x.base() == __y.base();
  }

  template <sized_sentinel_for<_Iter> _Sent>
  friend _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Iter>
  operator-(const move_sentinel<_Sent>& __x, const move_iterator& __y) {
    return __x.base() - __y.base();
  }

  template <sized_sentinel_for<_Iter> _Sent>
  friend _LIBCPP_HIDE_FROM_ABI constexpr iter_difference_t<_Iter>
  operator-(const move_iterator& __x, const move_sentinel<_Sent>& __y) {
    return __x.base() - __y.base();
  }

  friend _LIBCPP_HIDE_FROM_ABI constexpr iter_rvalue_reference_t<_Iter>
  iter_move(const move_iterator& __i) noexcept(noexcept(ranges::iter_move(__i.__current_))) {
    return ranges::iter_move(__i.__current_);
  }

  template <indirectly_swappable<_Iter> _It2>
  friend _LIBCPP_HIDE_FROM_ABI constexpr void
  iter_swap(const move_iterator& __x,
            const move_iterator<_It2>& __y) noexcept(noexcept(ranges::iter_swap(__x.__current_, __y.__current_))) {
    return ranges::iter_swap(__x.__current_, __y.__current_);
  }
#endif // _LIBCPP_STD_VER >= 20

private:
  template <class _It2>
  friend class move_iterator;

  _Iter __current_;
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(move_iterator);

template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator==(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() == __y.base();
}

#if _LIBCPP_STD_VER <= 17
template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator!=(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() != __y.base();
}
#endif // _LIBCPP_STD_VER <= 17

template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator<(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() < __y.base();
}

template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator>(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() > __y.base();
}

template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator<=(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() <= __y.base();
}

template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 bool
operator>=(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() >= __y.base();
}

#if _LIBCPP_STD_VER >= 20
template <class _Iter1, three_way_comparable_with<_Iter1> _Iter2>
inline _LIBCPP_HIDE_FROM_ABI constexpr auto
operator<=>(const move_iterator<_Iter1>& __x,
            const move_iterator<_Iter2>& __y) -> compare_three_way_result_t<_Iter1, _Iter2> {
  return __x.base() <=> __y.base();
}
#endif // _LIBCPP_STD_VER >= 20

#ifndef _LIBCPP_CXX03_LANG
template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 auto
operator-(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) -> decltype(__x.base() - __y.base()) {
  return __x.base() - __y.base();
}
#else
template <class _Iter1, class _Iter2>
inline _LIBCPP_HIDE_FROM_ABI typename move_iterator<_Iter1>::difference_type
operator-(const move_iterator<_Iter1>& __x, const move_iterator<_Iter2>& __y) {
  return __x.base() - __y.base();
}
#endif // !_LIBCPP_CXX03_LANG

#if _LIBCPP_STD_VER >= 20
template <class _Iter>
inline _LIBCPP_HIDE_FROM_ABI constexpr move_iterator<_Iter>
operator+(iter_difference_t<_Iter> __n, const move_iterator<_Iter>& __x)
  requires requires {
    { __x.base() + __n } -> same_as<_Iter>;
  }
{
  return __x + __n;
}
#else
template <class _Iter>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator<_Iter>
operator+(typename move_iterator<_Iter>::difference_type __n, const move_iterator<_Iter>& __x) {
  return move_iterator<_Iter>(__x.base() + __n);
}
#endif // _LIBCPP_STD_VER >= 20

#if _LIBCPP_STD_VER >= 20
template <class _Iter1, class _Iter2>
  requires(!sized_sentinel_for<_Iter1, _Iter2>)
inline constexpr bool disable_sized_sentinel_for<move_iterator<_Iter1>, move_iterator<_Iter2>> = true;
#endif // _LIBCPP_STD_VER >= 20

template <class _Iter>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 move_iterator<_Iter> make_move_iterator(_Iter __i) {
  return move_iterator<_Iter>(std::move(__i));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ITERATOR_MOVE_ITERATOR_H
