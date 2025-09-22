// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_SPLIT_VIEW_H
#define _LIBCPP___RANGES_SPLIT_VIEW_H

#include <__algorithm/ranges_search.h>
#include <__concepts/constructible.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/ranges_operations.h>
#include <__iterator/indirectly_comparable.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/single_view.h>
#include <__ranges/subrange.h>
#include <__ranges/view_interface.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__utility/forward.h>
#include <__utility/move.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {

template <forward_range _View, forward_range _Pattern>
  requires view<_View> && view<_Pattern> &&
           indirectly_comparable<iterator_t<_View>, iterator_t<_Pattern>, ranges::equal_to>
class split_view : public view_interface<split_view<_View, _Pattern>> {
private:
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_       = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS _Pattern __pattern_ = _Pattern();
  using _Cache                                  = __non_propagating_cache<subrange<iterator_t<_View>>>;
  _Cache __cached_begin_                        = _Cache();

  template <class, class>
  friend struct __iterator;

  template <class, class>
  friend struct __sentinel;

  struct __iterator;
  struct __sentinel;

  _LIBCPP_HIDE_FROM_ABI constexpr subrange<iterator_t<_View>> __find_next(iterator_t<_View> __it) {
    auto [__begin, __end] = ranges::search(subrange(__it, ranges::end(__base_)), __pattern_);
    if (__begin != ranges::end(__base_) && ranges::empty(__pattern_)) {
      ++__begin;
      ++__end;
    }
    return {__begin, __end};
  }

public:
  _LIBCPP_HIDE_FROM_ABI split_view()
    requires default_initializable<_View> && default_initializable<_Pattern>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 split_view(_View __base, _Pattern __pattern)
      : __base_(std::move(__base)), __pattern_(std::move((__pattern))) {}

  template <forward_range _Range>
    requires constructible_from<_View, views::all_t<_Range>> &&
                 constructible_from<_Pattern, single_view<range_value_t<_Range>>>
  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23
  split_view(_Range&& __range, range_value_t<_Range> __elem)
      : __base_(views::all(std::forward<_Range>(__range))), __pattern_(views::single(std::move(__elem))) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator begin() {
    if (!__cached_begin_.__has_value()) {
      __cached_begin_.__emplace(__find_next(ranges::begin(__base_)));
    }
    return {*this, ranges::begin(__base_), *__cached_begin_};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() {
    if constexpr (common_range<_View>) {
      return __iterator{*this, ranges::end(__base_), {}};
    } else {
      return __sentinel{*this};
    }
  }
};

template <class _Range, class _Pattern>
split_view(_Range&&, _Pattern&&) -> split_view<views::all_t<_Range>, views::all_t<_Pattern>>;

template <forward_range _Range>
split_view(_Range&&, range_value_t<_Range>) -> split_view<views::all_t<_Range>, single_view<range_value_t<_Range>>>;

template <forward_range _View, forward_range _Pattern>
  requires view<_View> && view<_Pattern> &&
           indirectly_comparable<iterator_t<_View>, iterator_t<_Pattern>, ranges::equal_to>
struct split_view<_View, _Pattern>::__iterator {
private:
  split_view* __parent_                                         = nullptr;
  _LIBCPP_NO_UNIQUE_ADDRESS iterator_t<_View> __cur_            = iterator_t<_View>();
  _LIBCPP_NO_UNIQUE_ADDRESS subrange<iterator_t<_View>> __next_ = subrange<iterator_t<_View>>();
  bool __trailing_empty_                                        = false;

  friend struct __sentinel;

public:
  using iterator_concept  = forward_iterator_tag;
  using iterator_category = input_iterator_tag;
  using value_type        = subrange<iterator_t<_View>>;
  using difference_type   = range_difference_t<_View>;

  _LIBCPP_HIDE_FROM_ABI __iterator() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(
      split_view<_View, _Pattern>& __parent, iterator_t<_View> __current, subrange<iterator_t<_View>> __next)
      : __parent_(std::addressof(__parent)), __cur_(std::move(__current)), __next_(std::move(__next)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> base() const { return __cur_; }

  _LIBCPP_HIDE_FROM_ABI constexpr value_type operator*() const { return {__cur_, __next_.begin()}; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    __cur_ = __next_.begin();
    if (__cur_ != ranges::end(__parent_->__base_)) {
      __cur_ = __next_.end();
      if (__cur_ == ranges::end(__parent_->__base_)) {
        __trailing_empty_ = true;
        __next_           = {__cur_, __cur_};
      } else {
        __next_ = __parent_->__find_next(__cur_);
      }
    } else {
      __trailing_empty_ = false;
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int) {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y) {
    return __x.__cur_ == __y.__cur_ && __x.__trailing_empty_ == __y.__trailing_empty_;
  }
};

template <forward_range _View, forward_range _Pattern>
  requires view<_View> && view<_Pattern> &&
           indirectly_comparable<iterator_t<_View>, iterator_t<_Pattern>, ranges::equal_to>
struct split_view<_View, _Pattern>::__sentinel {
private:
  _LIBCPP_NO_UNIQUE_ADDRESS sentinel_t<_View> __end_ = sentinel_t<_View>();

  _LIBCPP_HIDE_FROM_ABI static constexpr bool __equals(const __iterator& __x, const __sentinel& __y) {
    return __x.__cur_ == __y.__end_ && !__x.__trailing_empty_;
  }

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(split_view<_View, _Pattern>& __parent)
      : __end_(ranges::end(__parent.__base_)) {}

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __sentinel& __y) {
    return __equals(__x, __y);
  }
};

namespace views {
namespace __split_view {
struct __fn {
  // clang-format off
  template <class _Range, class _Pattern>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI
  constexpr auto operator()(_Range&& __range, _Pattern&& __pattern) const
    noexcept(noexcept(split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern))))
    -> decltype(      split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern)))
    { return          split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern)); }
  // clang-format on

  template <class _Pattern>
    requires constructible_from<decay_t<_Pattern>, _Pattern>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pattern&& __pattern) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pattern>, _Pattern>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pattern>(__pattern)));
  }
};
} // namespace __split_view

inline namespace __cpo {
inline constexpr auto split = __split_view::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_SPLIT_VIEW_H
