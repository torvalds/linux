// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_REPEAT_VIEW_H
#define _LIBCPP___RANGES_REPEAT_VIEW_H

#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/same_as.h>
#include <__concepts/semiregular.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/unreachable_sentinel.h>
#include <__memory/addressof.h>
#include <__ranges/iota_view.h>
#include <__ranges/movable_box.h>
#include <__ranges/view_interface.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_object.h>
#include <__type_traits/make_unsigned.h>
#include <__type_traits/remove_cv.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>
#include <__utility/piecewise_construct.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

namespace ranges {

template <class _Tp>
concept __integer_like_with_usable_difference_type =
    __signed_integer_like<_Tp> || (__integer_like<_Tp> && weakly_incrementable<_Tp>);

template <class _Tp>
struct __repeat_view_iterator_difference {
  using type = _IotaDiffT<_Tp>;
};

template <__signed_integer_like _Tp>
struct __repeat_view_iterator_difference<_Tp> {
  using type = _Tp;
};

template <class _Tp>
using __repeat_view_iterator_difference_t = typename __repeat_view_iterator_difference<_Tp>::type;

namespace views::__drop {
struct __fn;
} // namespace views::__drop

namespace views::__take {
struct __fn;
} // namespace views::__take

template <move_constructible _Tp, semiregular _Bound = unreachable_sentinel_t>
  requires(is_object_v<_Tp> && same_as<_Tp, remove_cv_t<_Tp>> &&
           (__integer_like_with_usable_difference_type<_Bound> || same_as<_Bound, unreachable_sentinel_t>))
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS repeat_view : public view_interface<repeat_view<_Tp, _Bound>> {
  friend struct views::__take::__fn;
  friend struct views::__drop::__fn;
  class __iterator;

public:
  _LIBCPP_HIDE_FROM_ABI repeat_view()
    requires default_initializable<_Tp>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit repeat_view(const _Tp& __value, _Bound __bound_sentinel = _Bound())
    requires copy_constructible<_Tp>
      : __value_(in_place, __value), __bound_(__bound_sentinel) {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(__bound_ >= 0, "The value of bound must be greater than or equal to 0");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr explicit repeat_view(_Tp&& __value, _Bound __bound_sentinel = _Bound())
      : __value_(in_place, std::move(__value)), __bound_(__bound_sentinel) {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(__bound_ >= 0, "The value of bound must be greater than or equal to 0");
  }

  template <class... _TpArgs, class... _BoundArgs>
    requires(constructible_from<_Tp, _TpArgs...> && constructible_from<_Bound, _BoundArgs...>)
  _LIBCPP_HIDE_FROM_ABI constexpr explicit repeat_view(
      piecewise_construct_t, tuple<_TpArgs...> __value_args, tuple<_BoundArgs...> __bound_args = tuple<>{})
      : __value_(in_place, std::make_from_tuple<_Tp>(std::move(__value_args))),
        __bound_(std::make_from_tuple<_Bound>(std::move(__bound_args))) {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(
          __bound_ >= 0, "The behavior is undefined if Bound is not unreachable_sentinel_t and bound is negative");
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator begin() const { return __iterator(std::addressof(*__value_)); }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator end() const
    requires(!same_as<_Bound, unreachable_sentinel_t>)
  {
    return __iterator(std::addressof(*__value_), __bound_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr unreachable_sentinel_t end() const noexcept { return unreachable_sentinel; }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires(!same_as<_Bound, unreachable_sentinel_t>)
  {
    return std::__to_unsigned_like(__bound_);
  }

private:
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Tp> __value_;
  _LIBCPP_NO_UNIQUE_ADDRESS _Bound __bound_ = _Bound();
};

template <class _Tp, class _Bound = unreachable_sentinel_t>
repeat_view(_Tp, _Bound = _Bound()) -> repeat_view<_Tp, _Bound>;

// [range.repeat.iterator]
template <move_constructible _Tp, semiregular _Bound>
  requires(is_object_v<_Tp> && same_as<_Tp, remove_cv_t<_Tp>> &&
           (__integer_like_with_usable_difference_type<_Bound> || same_as<_Bound, unreachable_sentinel_t>))
class repeat_view<_Tp, _Bound>::__iterator {
  friend class repeat_view;

  using _IndexT = conditional_t<same_as<_Bound, unreachable_sentinel_t>, ptrdiff_t, _Bound>;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __iterator(const _Tp* __value, _IndexT __bound_sentinel = _IndexT())
      : __value_(__value), __current_(__bound_sentinel) {}

public:
  using iterator_concept  = random_access_iterator_tag;
  using iterator_category = random_access_iterator_tag;
  using value_type        = _Tp;
  using difference_type   = __repeat_view_iterator_difference_t<_IndexT>;

  _LIBCPP_HIDE_FROM_ABI __iterator() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp& operator*() const noexcept { return *__value_; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    ++__current_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int) {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--() {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(__current_ > 0, "The value of bound must be greater than or equal to 0");
    --__current_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int) {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator+=(difference_type __n) {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(__current_ + __n >= 0, "The value of bound must be greater than or equal to 0");
    __current_ += __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator-=(difference_type __n) {
    if constexpr (!same_as<_Bound, unreachable_sentinel_t>)
      _LIBCPP_ASSERT_UNCATEGORIZED(__current_ - __n >= 0, "The value of bound must be greater than or equal to 0");
    __current_ -= __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Tp& operator[](difference_type __n) const noexcept { return *(*this + __n); }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y) {
    return __x.__current_ == __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr auto operator<=>(const __iterator& __x, const __iterator& __y) {
    return __x.__current_ <=> __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(__iterator __i, difference_type __n) {
    __i += __n;
    return __i;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(difference_type __n, __iterator __i) {
    __i += __n;
    return __i;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator-(__iterator __i, difference_type __n) {
    __i -= __n;
    return __i;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr difference_type operator-(const __iterator& __x, const __iterator& __y) {
    return static_cast<difference_type>(__x.__current_) - static_cast<difference_type>(__y.__current_);
  }

private:
  const _Tp* __value_ = nullptr;
  _IndexT __current_  = _IndexT();
};

// clang-format off
namespace views {
namespace __repeat {
struct __fn {
  template <class _Tp>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static constexpr auto operator()(_Tp&& __value)
    noexcept(noexcept(ranges::repeat_view<decay_t<_Tp>>(std::forward<_Tp>(__value))))
    -> decltype(      ranges::repeat_view<decay_t<_Tp>>(std::forward<_Tp>(__value)))
    { return          ranges::repeat_view<decay_t<_Tp>>(std::forward<_Tp>(__value)); }

  template <class _Tp, class _Bound>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI static constexpr auto operator()(_Tp&& __value, _Bound&& __bound_sentinel)
    noexcept(noexcept(ranges::repeat_view(std::forward<_Tp>(__value), std::forward<_Bound>(__bound_sentinel))))
    -> decltype(      ranges::repeat_view(std::forward<_Tp>(__value), std::forward<_Bound>(__bound_sentinel)))
    { return          ranges::repeat_view(std::forward<_Tp>(__value), std::forward<_Bound>(__bound_sentinel)); }
};
} // namespace __repeat
// clang-format on

inline namespace __cpo {
inline constexpr auto repeat = __repeat::__fn{};
} // namespace __cpo
} // namespace views

template <class _Tp>
inline constexpr bool __is_repeat_specialization = false;

template <class _Tp, class _Bound>
inline constexpr bool __is_repeat_specialization<repeat_view<_Tp, _Bound>> = true;

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_REPEAT_VIEW_H
