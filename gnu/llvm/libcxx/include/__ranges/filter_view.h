// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_FILTER_VIEW_H
#define _LIBCPP___RANGES_FILTER_VIEW_H

#include <__algorithm/ranges_find_if.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/copyable.h>
#include <__concepts/derived_from.h>
#include <__concepts/equality_comparable.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/invoke.h>
#include <__functional/reference_wrapper.h>
#include <__iterator/concepts.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/movable_box.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_object.h>
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
template <input_range _View, indirect_unary_predicate<iterator_t<_View>> _Pred>
  requires view<_View> && is_object_v<_Pred>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS filter_view : public view_interface<filter_view<_View, _Pred>> {
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Pred> __pred_;

  // We cache the result of begin() to allow providing an amortized O(1) begin() whenever
  // the underlying range is at least a forward_range.
  static constexpr bool _UseCache = forward_range<_View>;
  using _Cache                    = _If<_UseCache, __non_propagating_cache<iterator_t<_View>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _Cache __cached_begin_ = _Cache();

  class __iterator;
  class __sentinel;

public:
  _LIBCPP_HIDE_FROM_ABI filter_view()
    requires default_initializable<_View> && default_initializable<_Pred>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 filter_view(_View __base, _Pred __pred)
      : __base_(std::move(__base)), __pred_(in_place, std::move(__pred)) {}

  template <class _Vp = _View>
  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_Vp>
  {
    return __base_;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr _Pred const& pred() const { return *__pred_; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator begin() {
    // Note: this duplicates a check in `optional` but provides a better error message.
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __pred_.__has_value(), "Trying to call begin() on a filter_view that does not have a valid predicate.");
    if constexpr (_UseCache) {
      if (!__cached_begin_.__has_value()) {
        __cached_begin_.__emplace(ranges::find_if(__base_, std::ref(*__pred_)));
      }
      return {*this, *__cached_begin_};
    } else {
      return {*this, ranges::find_if(__base_, std::ref(*__pred_))};
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() {
    if constexpr (common_range<_View>)
      return __iterator{*this, ranges::end(__base_)};
    else
      return __sentinel{*this};
  }
};

template <class _Range, class _Pred>
filter_view(_Range&&, _Pred) -> filter_view<views::all_t<_Range>, _Pred>;

template <class _View>
struct __filter_iterator_category {};

template <forward_range _View>
struct __filter_iterator_category<_View> {
  using _Cat = typename iterator_traits<iterator_t<_View>>::iterator_category;
  using iterator_category =
      _If<derived_from<_Cat, bidirectional_iterator_tag>,
          bidirectional_iterator_tag,
          _If<derived_from<_Cat, forward_iterator_tag>,
              forward_iterator_tag,
              /* else */ _Cat >>;
};

template <input_range _View, indirect_unary_predicate<iterator_t<_View>> _Pred>
  requires view<_View> && is_object_v<_Pred>
class filter_view<_View, _Pred>::__iterator : public __filter_iterator_category<_View> {
public:
  _LIBCPP_NO_UNIQUE_ADDRESS iterator_t<_View> __current_ = iterator_t<_View>();
  _LIBCPP_NO_UNIQUE_ADDRESS filter_view* __parent_       = nullptr;

  using iterator_concept =
      _If<bidirectional_range<_View>,
          bidirectional_iterator_tag,
          _If<forward_range<_View>,
              forward_iterator_tag,
              /* else */ input_iterator_tag >>;
  // using iterator_category = inherited;
  using value_type      = range_value_t<_View>;
  using difference_type = range_difference_t<_View>;

  _LIBCPP_HIDE_FROM_ABI __iterator()
    requires default_initializable<iterator_t<_View>>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(filter_view& __parent, iterator_t<_View> __current)
      : __current_(std::move(__current)), __parent_(std::addressof(__parent)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> const& base() const& noexcept { return __current_; }
  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> base() && { return std::move(__current_); }

  _LIBCPP_HIDE_FROM_ABI constexpr range_reference_t<_View> operator*() const { return *__current_; }
  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> operator->() const
    requires __has_arrow<iterator_t<_View>> && copyable<iterator_t<_View>>
  {
    return __current_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    __current_ =
        ranges::find_if(std::move(++__current_), ranges::end(__parent_->__base_), std::ref(*__parent_->__pred_));
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++*this; }
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int)
    requires forward_range<_View>
  {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
    requires bidirectional_range<_View>
  {
    do {
      --__current_;
    } while (!std::invoke(*__parent_->__pred_, *__current_));
    return *this;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
    requires bidirectional_range<_View>
  {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(__iterator const& __x, __iterator const& __y)
    requires equality_comparable<iterator_t<_View>>
  {
    return __x.__current_ == __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr range_rvalue_reference_t<_View>
  iter_move(__iterator const& __it) noexcept(noexcept(ranges::iter_move(__it.__current_))) {
    return ranges::iter_move(__it.__current_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void
  iter_swap(__iterator const& __x,
            __iterator const& __y) noexcept(noexcept(ranges::iter_swap(__x.__current_, __y.__current_)))
    requires indirectly_swappable<iterator_t<_View>>
  {
    return ranges::iter_swap(__x.__current_, __y.__current_);
  }
};

template <input_range _View, indirect_unary_predicate<iterator_t<_View>> _Pred>
  requires view<_View> && is_object_v<_Pred>
class filter_view<_View, _Pred>::__sentinel {
public:
  sentinel_t<_View> __end_ = sentinel_t<_View>();

  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(filter_view& __parent) : __end_(ranges::end(__parent.__base_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr sentinel_t<_View> base() const { return __end_; }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(__iterator const& __x, __sentinel const& __y) {
    return __x.__current_ == __y.__end_;
  }
};

namespace views {
namespace __filter {
struct __fn {
  template <class _Range, class _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Pred&& __pred) const
      noexcept(noexcept(filter_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))))
          -> decltype(filter_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))) {
    return filter_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred));
  }

  template <class _Pred>
    requires constructible_from<decay_t<_Pred>, _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pred&& __pred) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pred>, _Pred>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pred>(__pred)));
  }
};
} // namespace __filter

inline namespace __cpo {
inline constexpr auto filter = __filter::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_FILTER_VIEW_H
