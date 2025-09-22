// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_CHUNK_BY_VIEW_H
#define _LIBCPP___RANGES_CHUNK_BY_VIEW_H

#include <__algorithm/ranges_adjacent_find.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/next.h>
#include <__iterator/prev.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/movable_box.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/reverse_view.h>
#include <__ranges/subrange.h>
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

#if _LIBCPP_STD_VER >= 23

namespace ranges {

template <forward_range _View, indirect_binary_predicate<iterator_t<_View>, iterator_t<_View>> _Pred>
  requires view<_View> && is_object_v<_Pred>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS chunk_by_view : public view_interface<chunk_by_view<_View, _Pred>> {
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Pred> __pred_;

  // We cache the result of begin() to allow providing an amortized O(1).
  using _Cache = __non_propagating_cache<iterator_t<_View>>;
  _Cache __cached_begin_;

  class __iterator;

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> __find_next(iterator_t<_View> __current) {
    // Note: this duplicates a check in `optional` but provides a better error message.
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __pred_.__has_value(), "Trying to call __find_next() on a chunk_by_view that does not have a valid predicate.");
    auto __reversed_pred = [this]<class _Tp, class _Up>(_Tp&& __x, _Up&& __y) -> bool {
      return !std::invoke(*__pred_, std::forward<_Tp>(__x), std::forward<_Up>(__y));
    };
    return ranges::next(
        ranges::adjacent_find(__current, ranges::end(__base_), __reversed_pred), 1, ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_View> __find_prev(iterator_t<_View> __current)
    requires bidirectional_range<_View>
  {
    // Attempting to decrement a begin iterator is a no-op (`__find_prev` would return the same argument given to it).
    _LIBCPP_ASSERT_PEDANTIC(__current != ranges::begin(__base_), "Trying to call __find_prev() on a begin iterator.");
    // Note: this duplicates a check in `optional` but provides a better error message.
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __pred_.__has_value(), "Trying to call __find_prev() on a chunk_by_view that does not have a valid predicate.");

    auto __first = ranges::begin(__base_);
    reverse_view __reversed{subrange{__first, __current}};
    auto __reversed_pred = [this]<class _Tp, class _Up>(_Tp&& __x, _Up&& __y) -> bool {
      return !std::invoke(*__pred_, std::forward<_Up>(__y), std::forward<_Tp>(__x));
    };
    return ranges::prev(ranges::adjacent_find(__reversed, __reversed_pred).base(), 1, std::move(__first));
  }

public:
  _LIBCPP_HIDE_FROM_ABI chunk_by_view()
    requires default_initializable<_View> && default_initializable<_Pred>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit chunk_by_view(_View __base, _Pred __pred)
      : __base_(std::move(__base)), __pred_(in_place, std::move(__pred)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Pred& pred() const { return *__pred_; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator begin() {
    // Note: this duplicates a check in `optional` but provides a better error message.
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __pred_.__has_value(), "Trying to call begin() on a chunk_by_view that does not have a valid predicate.");

    auto __first = ranges::begin(__base_);
    if (!__cached_begin_.__has_value()) {
      __cached_begin_.__emplace(__find_next(__first));
    }
    return {*this, std::move(__first), *__cached_begin_};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() {
    if constexpr (common_range<_View>) {
      return __iterator{*this, ranges::end(__base_), ranges::end(__base_)};
    } else {
      return default_sentinel;
    }
  }
};

template <class _Range, class _Pred>
chunk_by_view(_Range&&, _Pred) -> chunk_by_view<views::all_t<_Range>, _Pred>;

template <forward_range _View, indirect_binary_predicate<iterator_t<_View>, iterator_t<_View>> _Pred>
  requires view<_View> && is_object_v<_Pred>
class chunk_by_view<_View, _Pred>::__iterator {
  friend chunk_by_view;

  chunk_by_view* __parent_                               = nullptr;
  _LIBCPP_NO_UNIQUE_ADDRESS iterator_t<_View> __current_ = iterator_t<_View>();
  _LIBCPP_NO_UNIQUE_ADDRESS iterator_t<_View> __next_    = iterator_t<_View>();

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(
      chunk_by_view& __parent, iterator_t<_View> __current, iterator_t<_View> __next)
      : __parent_(std::addressof(__parent)), __current_(__current), __next_(__next) {}

public:
  using value_type        = subrange<iterator_t<_View>>;
  using difference_type   = range_difference_t<_View>;
  using iterator_category = input_iterator_tag;
  using iterator_concept  = conditional_t<bidirectional_range<_View>, bidirectional_iterator_tag, forward_iterator_tag>;

  _LIBCPP_HIDE_FROM_ABI __iterator() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr value_type operator*() const {
    // If the iterator is at end, this would return an empty range which can be checked by the calling code and doesn't
    // necessarily lead to a bad access.
    _LIBCPP_ASSERT_PEDANTIC(__current_ != __next_, "Trying to dereference past-the-end chunk_by_view iterator.");
    return {__current_, __next_};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    // Attempting to increment an end iterator is a no-op (`__find_next` would return the same argument given to it).
    _LIBCPP_ASSERT_PEDANTIC(__current_ != __next_, "Trying to increment past end chunk_by_view iterator.");
    __current_ = __next_;
    __next_    = __parent_->__find_next(__current_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int) {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
    requires bidirectional_range<_View>
  {
    __next_    = __current_;
    __current_ = __parent_->__find_prev(__next_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
    requires bidirectional_range<_View>
  {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y) {
    return __x.__current_ == __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, default_sentinel_t) {
    return __x.__current_ == __x.__next_;
  }
};

namespace views {
namespace __chunk_by {
struct __fn {
  template <class _Range, class _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Pred&& __pred) const
      noexcept(noexcept(/**/ chunk_by_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))))
          -> decltype(/*--*/ chunk_by_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))) {
    return /*-------------*/ chunk_by_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred));
  }

  template <class _Pred>
    requires constructible_from<decay_t<_Pred>, _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pred&& __pred) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pred>, _Pred>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pred>(__pred)));
  }
};
} // namespace __chunk_by

inline namespace __cpo {
inline constexpr auto chunk_by = __chunk_by::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_CHUNK_BY_VIEW_H
