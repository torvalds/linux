// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_TRANSFORM_VIEW_H
#define _LIBCPP___RANGES_TRANSFORM_VIEW_H

#include <__compare/three_way_comparable.h>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/copyable.h>
#include <__concepts/derived_from.h>
#include <__concepts/equality_comparable.h>
#include <__concepts/invocable.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/invoke.h>
#include <__functional/perfect_forward.h>
#include <__iterator/concepts.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty.h>
#include <__ranges/movable_box.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/size.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_object.h>
#include <__type_traits/is_reference.h>
#include <__type_traits/maybe_const.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>
#include <__utility/in_place.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <class _Fn, class _View>
concept __regular_invocable_with_range_ref = regular_invocable<_Fn, range_reference_t<_View>>;

template <class _View, class _Fn>
concept __transform_view_constraints =
    view<_View> && is_object_v<_Fn> && regular_invocable<_Fn&, range_reference_t<_View>> &&
    __can_reference<invoke_result_t<_Fn&, range_reference_t<_View>>>;

#  if _LIBCPP_STD_VER >= 23
template <input_range _View, move_constructible _Fn>
#  else
template <input_range _View, copy_constructible _Fn>
#  endif
  requires __transform_view_constraints<_View, _Fn>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS transform_view : public view_interface<transform_view<_View, _Fn>> {
  template <bool>
  class __iterator;
  template <bool>
  class __sentinel;

  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Fn> __func_;
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();

public:
  _LIBCPP_HIDE_FROM_ABI transform_view()
    requires default_initializable<_View> && default_initializable<_Fn>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 transform_view(_View __base, _Fn __func)
      : __func_(std::in_place, std::move(__func)), __base_(std::move(__base)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator<false> begin() { return __iterator<false>{*this, ranges::begin(__base_)}; }
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator<true> begin() const
    requires range<const _View> && __regular_invocable_with_range_ref<const _Fn&, const _View>
  {
    return __iterator<true>(*this, ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel<false> end() { return __sentinel<false>(ranges::end(__base_)); }
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator<false> end()
    requires common_range<_View>
  {
    return __iterator<false>(*this, ranges::end(__base_));
  }
  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel<true> end() const
    requires range<const _View> && __regular_invocable_with_range_ref<const _Fn&, const _View>
  {
    return __sentinel<true>(ranges::end(__base_));
  }
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator<true> end() const
    requires common_range<const _View> && __regular_invocable_with_range_ref<const _Fn&, const _View>
  {
    return __iterator<true>(*this, ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires sized_range<_View>
  {
    return ranges::size(__base_);
  }
  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires sized_range<const _View>
  {
    return ranges::size(__base_);
  }
};

template <class _Range, class _Fn>
transform_view(_Range&&, _Fn) -> transform_view<views::all_t<_Range>, _Fn>;

template <class _View>
struct __transform_view_iterator_concept {
  using type = input_iterator_tag;
};

template <random_access_range _View>
struct __transform_view_iterator_concept<_View> {
  using type = random_access_iterator_tag;
};

template <bidirectional_range _View>
struct __transform_view_iterator_concept<_View> {
  using type = bidirectional_iterator_tag;
};

template <forward_range _View>
struct __transform_view_iterator_concept<_View> {
  using type = forward_iterator_tag;
};

template <class, class>
struct __transform_view_iterator_category_base {};

template <forward_range _View, class _Fn>
struct __transform_view_iterator_category_base<_View, _Fn> {
  using _Cat = typename iterator_traits<iterator_t<_View>>::iterator_category;

  using iterator_category =
      conditional_t< is_reference_v<invoke_result_t<_Fn&, range_reference_t<_View>>>,
                     conditional_t< derived_from<_Cat, contiguous_iterator_tag>, random_access_iterator_tag, _Cat >,
                     input_iterator_tag >;
};

#  if _LIBCPP_STD_VER >= 23
template <input_range _View, move_constructible _Fn>
#  else
template <input_range _View, copy_constructible _Fn>
#  endif
  requires __transform_view_constraints<_View, _Fn>
template <bool _Const>
class transform_view<_View, _Fn>::__iterator : public __transform_view_iterator_category_base<_View, _Fn> {

  using _Parent = __maybe_const<_Const, transform_view>;
  using _Base   = __maybe_const<_Const, _View>;

  _Parent* __parent_ = nullptr;

  template <bool>
  friend class transform_view<_View, _Fn>::__iterator;

  template <bool>
  friend class transform_view<_View, _Fn>::__sentinel;

public:
  iterator_t<_Base> __current_ = iterator_t<_Base>();

  using iterator_concept = typename __transform_view_iterator_concept<_View>::type;
  using value_type       = remove_cvref_t<invoke_result_t<_Fn&, range_reference_t<_Base>>>;
  using difference_type  = range_difference_t<_Base>;

  _LIBCPP_HIDE_FROM_ABI __iterator()
    requires default_initializable<iterator_t<_Base>>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(_Parent& __parent, iterator_t<_Base> __current)
      : __parent_(std::addressof(__parent)), __current_(std::move(__current)) {}

  // Note: `__i` should always be `__iterator<false>`, but directly using
  // `__iterator<false>` is ill-formed when `_Const` is false
  // (see http://wg21.link/class.copy.ctor#5).
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(__iterator<!_Const> __i)
    requires _Const && convertible_to<iterator_t<_View>, iterator_t<_Base>>
      : __parent_(__i.__parent_), __current_(std::move(__i.__current_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr const iterator_t<_Base>& base() const& noexcept { return __current_; }

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_Base> base() && { return std::move(__current_); }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator*() const
      noexcept(noexcept(std::invoke(*__parent_->__func_, *__current_))) {
    return std::invoke(*__parent_->__func_, *__current_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    ++__current_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++__current_; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int)
    requires forward_range<_Base>
  {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
    requires bidirectional_range<_Base>
  {
    --__current_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
    requires bidirectional_range<_Base>
  {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator+=(difference_type __n)
    requires random_access_range<_Base>
  {
    __current_ += __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator-=(difference_type __n)
    requires random_access_range<_Base>
  {
    __current_ -= __n;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator[](difference_type __n) const
      noexcept(noexcept(std::invoke(*__parent_->__func_, __current_[__n])))
    requires random_access_range<_Base>
  {
    return std::invoke(*__parent_->__func_, __current_[__n]);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y)
    requires equality_comparable<iterator_t<_Base>>
  {
    return __x.__current_ == __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(const __iterator& __x, const __iterator& __y)
    requires random_access_range<_Base>
  {
    return __x.__current_ < __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(const __iterator& __x, const __iterator& __y)
    requires random_access_range<_Base>
  {
    return __x.__current_ > __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(const __iterator& __x, const __iterator& __y)
    requires random_access_range<_Base>
  {
    return __x.__current_ <= __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(const __iterator& __x, const __iterator& __y)
    requires random_access_range<_Base>
  {
    return __x.__current_ >= __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr auto operator<=>(const __iterator& __x, const __iterator& __y)
    requires random_access_range<_Base> && three_way_comparable<iterator_t<_Base>>
  {
    return __x.__current_ <=> __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(__iterator __i, difference_type __n)
    requires random_access_range<_Base>
  {
    return __iterator{*__i.__parent_, __i.__current_ + __n};
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(difference_type __n, __iterator __i)
    requires random_access_range<_Base>
  {
    return __iterator{*__i.__parent_, __i.__current_ + __n};
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator-(__iterator __i, difference_type __n)
    requires random_access_range<_Base>
  {
    return __iterator{*__i.__parent_, __i.__current_ - __n};
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr difference_type operator-(const __iterator& __x, const __iterator& __y)
    requires sized_sentinel_for<iterator_t<_Base>, iterator_t<_Base>>
  {
    return __x.__current_ - __y.__current_;
  }
};

#  if _LIBCPP_STD_VER >= 23
template <input_range _View, move_constructible _Fn>
#  else
template <input_range _View, copy_constructible _Fn>
#  endif
  requires __transform_view_constraints<_View, _Fn>
template <bool _Const>
class transform_view<_View, _Fn>::__sentinel {
  using _Parent = __maybe_const<_Const, transform_view>;
  using _Base   = __maybe_const<_Const, _View>;

  sentinel_t<_Base> __end_ = sentinel_t<_Base>();

  template <bool>
  friend class transform_view<_View, _Fn>::__iterator;

  template <bool>
  friend class transform_view<_View, _Fn>::__sentinel;

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(sentinel_t<_Base> __end) : __end_(__end) {}

  // Note: `__i` should always be `__sentinel<false>`, but directly using
  // `__sentinel<false>` is ill-formed when `_Const` is false
  // (see http://wg21.link/class.copy.ctor#5).
  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel(__sentinel<!_Const> __i)
    requires _Const && convertible_to<sentinel_t<_View>, sentinel_t<_Base>>
      : __end_(std::move(__i.__end_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr sentinel_t<_Base> base() const { return __end_; }

  template <bool _OtherConst>
    requires sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator<_OtherConst>& __x, const __sentinel& __y) {
    return __x.__current_ == __y.__end_;
  }

  template <bool _OtherConst>
    requires sized_sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr range_difference_t<__maybe_const<_OtherConst, _View>>
  operator-(const __iterator<_OtherConst>& __x, const __sentinel& __y) {
    return __x.__current_ - __y.__end_;
  }

  template <bool _OtherConst>
    requires sized_sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr range_difference_t<__maybe_const<_OtherConst, _View>>
  operator-(const __sentinel& __x, const __iterator<_OtherConst>& __y) {
    return __x.__end_ - __y.__current_;
  }
};

namespace views {
namespace __transform {
struct __fn {
  template <class _Range, class _Fn>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Fn&& __f) const
      noexcept(noexcept(transform_view(std::forward<_Range>(__range), std::forward<_Fn>(__f))))
          -> decltype(transform_view(std::forward<_Range>(__range), std::forward<_Fn>(__f))) {
    return transform_view(std::forward<_Range>(__range), std::forward<_Fn>(__f));
  }

  template <class _Fn>
    requires constructible_from<decay_t<_Fn>, _Fn>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Fn&& __f) const
      noexcept(is_nothrow_constructible_v<decay_t<_Fn>, _Fn>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Fn>(__f)));
  }
};
} // namespace __transform

inline namespace __cpo {
inline constexpr auto transform = __transform::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_TRANSFORM_VIEW_H
