// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_JOIN_VIEW_H
#define _LIBCPP___RANGES_JOIN_VIEW_H

#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/copyable.h>
#include <__concepts/derived_from.h>
#include <__concepts/equality_comparable.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/iterator_with_data.h>
#include <__iterator/segmented_iterator.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/view_interface.h>
#include <__type_traits/common_type.h>
#include <__type_traits/maybe_const.h>
#include <__utility/as_lvalue.h>
#include <__utility/empty.h>
#include <__utility/forward.h>
#include <optional>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

namespace ranges {
template <class>
struct __join_view_iterator_category {};

template <class _View>
  requires is_reference_v<range_reference_t<_View>> && forward_range<_View> && forward_range<range_reference_t<_View>>
struct __join_view_iterator_category<_View> {
  using _OuterC = typename iterator_traits<iterator_t<_View>>::iterator_category;
  using _InnerC = typename iterator_traits<iterator_t<range_reference_t<_View>>>::iterator_category;

  using iterator_category =
      _If< derived_from<_OuterC, bidirectional_iterator_tag> && derived_from<_InnerC, bidirectional_iterator_tag> &&
               common_range<range_reference_t<_View>>,
           bidirectional_iterator_tag,
           _If< derived_from<_OuterC, forward_iterator_tag> && derived_from<_InnerC, forward_iterator_tag>,
                forward_iterator_tag,
                input_iterator_tag > >;
};

template <input_range _View>
  requires view<_View> && input_range<range_reference_t<_View>>
class join_view : public view_interface<join_view<_View>> {
private:
  using _InnerRange = range_reference_t<_View>;

  template <bool>
  struct __iterator;

  template <bool>
  struct __sentinel;

  template <class>
  friend struct std::__segmented_iterator_traits;

  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();

  static constexpr bool _UseOuterCache = !forward_range<_View>;
  using _OuterCache                    = _If<_UseOuterCache, __non_propagating_cache<iterator_t<_View>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _OuterCache __outer_;

  static constexpr bool _UseInnerCache = !is_reference_v<_InnerRange>;
  using _InnerCache = _If<_UseInnerCache, __non_propagating_cache<remove_cvref_t<_InnerRange>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _InnerCache __inner_;

public:
  _LIBCPP_HIDE_FROM_ABI join_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit join_view(_View __base) : __base_(std::move(__base)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() {
    if constexpr (forward_range<_View>) {
      constexpr bool __use_const = __simple_view<_View> && is_reference_v<range_reference_t<_View>>;
      return __iterator<__use_const>{*this, ranges::begin(__base_)};
    } else {
      __outer_.__emplace(ranges::begin(__base_));
      return __iterator<false>{*this};
    }
  }

  template <class _V2 = _View>
  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires forward_range<const _V2> && is_reference_v<range_reference_t<const _V2>> &&
             input_range<range_reference_t<const _V2>>
  {
    return __iterator<true>{*this, ranges::begin(__base_)};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() {
    if constexpr (forward_range<_View> && is_reference_v<_InnerRange> && forward_range<_InnerRange> &&
                  common_range<_View> && common_range<_InnerRange>)
      return __iterator<__simple_view<_View>>{*this, ranges::end(__base_)};
    else
      return __sentinel<__simple_view<_View>>{*this};
  }

  template <class _V2 = _View>
  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires forward_range<const _V2> && is_reference_v<range_reference_t<const _V2>> &&
             input_range<range_reference_t<const _V2>>
  {
    using _ConstInnerRange = range_reference_t<const _View>;
    if constexpr (forward_range<_ConstInnerRange> && common_range<const _View> && common_range<_ConstInnerRange>) {
      return __iterator<true>{*this, ranges::end(__base_)};
    } else {
      return __sentinel<true>{*this};
    }
  }
};

template <input_range _View>
  requires view<_View> && input_range<range_reference_t<_View>>
template <bool _Const>
struct join_view<_View>::__sentinel {
private:
  template <bool>
  friend struct __sentinel;

  using _Parent            = __maybe_const<_Const, join_view>;
  using _Base              = __maybe_const<_Const, _View>;
  sentinel_t<_Base> __end_ = sentinel_t<_Base>();

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(_Parent& __parent) : __end_(ranges::end(__parent.__base_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel(__sentinel<!_Const> __s)
    requires _Const && convertible_to<sentinel_t<_View>, sentinel_t<_Base>>
      : __end_(std::move(__s.__end_)) {}

  template <bool _OtherConst>
    requires sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator<_OtherConst>& __x, const __sentinel& __y) {
    return __x.__get_outer() == __y.__end_;
  }
};

// https://reviews.llvm.org/D142811#inline-1383022
// To simplify the segmented iterator traits specialization,
// make the iterator `final`
template <input_range _View>
  requires view<_View> && input_range<range_reference_t<_View>>
template <bool _Const>
struct join_view<_View>::__iterator final : public __join_view_iterator_category<__maybe_const<_Const, _View>> {
  friend join_view;

  template <class>
  friend struct std::__segmented_iterator_traits;

  static constexpr bool __is_join_view_iterator = true;

private:
  using _Parent     = __maybe_const<_Const, join_view<_View>>;
  using _Base       = __maybe_const<_Const, _View>;
  using _Outer      = iterator_t<_Base>;
  using _Inner      = iterator_t<range_reference_t<_Base>>;
  using _InnerRange = range_reference_t<_View>;

  static_assert(!_Const || forward_range<_Base>, "Const can only be true when Base models forward_range.");

  static constexpr bool __ref_is_glvalue = is_reference_v<range_reference_t<_Base>>;

  static constexpr bool _OuterPresent           = forward_range<_Base>;
  using _OuterType                              = _If<_OuterPresent, _Outer, std::__empty>;
  _LIBCPP_NO_UNIQUE_ADDRESS _OuterType __outer_ = _OuterType();

  optional<_Inner> __inner_;
  _Parent* __parent_ = nullptr;

  _LIBCPP_HIDE_FROM_ABI constexpr void __satisfy() {
    for (; __get_outer() != ranges::end(__parent_->__base_); ++__get_outer()) {
      auto&& __inner = [this]() -> auto&& {
        if constexpr (__ref_is_glvalue)
          return *__get_outer();
        else
          return __parent_->__inner_.__emplace_from([&]() -> decltype(auto) { return *__get_outer(); });
      }();
      __inner_ = ranges::begin(__inner);
      if (*__inner_ != ranges::end(__inner))
        return;
    }

    if constexpr (__ref_is_glvalue)
      __inner_.reset();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _Outer& __get_outer() {
    if constexpr (forward_range<_Base>) {
      return __outer_;
    } else {
      return *__parent_->__outer_;
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Outer& __get_outer() const {
    if constexpr (forward_range<_Base>) {
      return __outer_;
    } else {
      return *__parent_->__outer_;
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(_Parent& __parent, _Outer __outer)
    requires forward_range<_Base>
      : __outer_(std::move(__outer)), __parent_(std::addressof(__parent)) {
    __satisfy();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __iterator(_Parent& __parent)
    requires(!forward_range<_Base>)
      : __parent_(std::addressof(__parent)) {
    __satisfy();
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(_Parent* __parent, _Outer __outer, _Inner __inner)
    requires forward_range<_Base>
      : __outer_(std::move(__outer)), __inner_(std::move(__inner)), __parent_(__parent) {}

public:
  using iterator_concept =
      _If< __ref_is_glvalue && bidirectional_range<_Base> && bidirectional_range<range_reference_t<_Base>> &&
               common_range<range_reference_t<_Base>>,
           bidirectional_iterator_tag,
           _If< __ref_is_glvalue && forward_range<_Base> && forward_range<range_reference_t<_Base>>,
                forward_iterator_tag,
                input_iterator_tag > >;

  using value_type = range_value_t<range_reference_t<_Base>>;

  using difference_type = common_type_t< range_difference_t<_Base>, range_difference_t<range_reference_t<_Base>>>;

  _LIBCPP_HIDE_FROM_ABI __iterator() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(__iterator<!_Const> __i)
    requires _Const && convertible_to<iterator_t<_View>, _Outer> && convertible_to<iterator_t<_InnerRange>, _Inner>
      : __outer_(std::move(__i.__outer_)), __inner_(std::move(__i.__inner_)), __parent_(__i.__parent_) {}

  _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator*() const { return **__inner_; }

  _LIBCPP_HIDE_FROM_ABI constexpr _Inner operator->() const
    requires __has_arrow<_Inner> && copyable<_Inner>
  {
    return *__inner_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    auto __get_inner_range = [&]() -> decltype(auto) {
      if constexpr (__ref_is_glvalue)
        return *__get_outer();
      else
        return *__parent_->__inner_;
    };
    if (++*__inner_ == ranges::end(std::__as_lvalue(__get_inner_range()))) {
      ++__get_outer();
      __satisfy();
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++*this; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int)
    requires __ref_is_glvalue && forward_range<_Base> && forward_range<range_reference_t<_Base>>
  {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
    requires __ref_is_glvalue && bidirectional_range<_Base> && bidirectional_range<range_reference_t<_Base>> &&
             common_range<range_reference_t<_Base>>
  {
    if (__outer_ == ranges::end(__parent_->__base_))
      __inner_ = ranges::end(std::__as_lvalue(*--__outer_));

    // Skip empty inner ranges when going backwards.
    while (*__inner_ == ranges::begin(std::__as_lvalue(*__outer_))) {
      __inner_ = ranges::end(std::__as_lvalue(*--__outer_));
    }

    --*__inner_;
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
    requires __ref_is_glvalue && bidirectional_range<_Base> && bidirectional_range<range_reference_t<_Base>> &&
             common_range<range_reference_t<_Base>>
  {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y)
    requires __ref_is_glvalue && forward_range<_Base> && equality_comparable<iterator_t<range_reference_t<_Base>>>
  {
    return __x.__outer_ == __y.__outer_ && __x.__inner_ == __y.__inner_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr decltype(auto)
  iter_move(const __iterator& __i) noexcept(noexcept(ranges::iter_move(*__i.__inner_))) {
    return ranges::iter_move(*__i.__inner_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void
  iter_swap(const __iterator& __x,
            const __iterator& __y) noexcept(noexcept(ranges::iter_swap(*__x.__inner_, *__y.__inner_)))
    requires indirectly_swappable<_Inner>
  {
    return ranges::iter_swap(*__x.__inner_, *__y.__inner_);
  }
};

template <class _Range>
explicit join_view(_Range&&) -> join_view<views::all_t<_Range>>;

namespace views {
namespace __join_view {
struct __fn : __range_adaptor_closure<__fn> {
  template <class _Range>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const
      noexcept(noexcept(join_view<all_t<_Range&&>>(std::forward<_Range>(__range))))
          -> decltype(join_view<all_t<_Range&&>>(std::forward<_Range>(__range))) {
    return join_view<all_t<_Range&&>>(std::forward<_Range>(__range));
  }
};
} // namespace __join_view
inline namespace __cpo {
inline constexpr auto join = __join_view::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

template <class _JoinViewIterator>
  requires(_JoinViewIterator::__is_join_view_iterator && ranges::common_range<typename _JoinViewIterator::_Parent> &&
           __has_random_access_iterator_category<typename _JoinViewIterator::_Outer>::value &&
           __has_random_access_iterator_category<typename _JoinViewIterator::_Inner>::value)
struct __segmented_iterator_traits<_JoinViewIterator> {
  using __segment_iterator =
      _LIBCPP_NODEBUG __iterator_with_data<typename _JoinViewIterator::_Outer, typename _JoinViewIterator::_Parent*>;
  using __local_iterator = typename _JoinViewIterator::_Inner;

  // TODO: Would it make sense to enable the optimization for other iterator types?

  static constexpr _LIBCPP_HIDE_FROM_ABI __segment_iterator __segment(_JoinViewIterator __iter) {
    if (ranges::empty(__iter.__parent_->__base_))
      return {};
    if (!__iter.__inner_.has_value())
      return __segment_iterator(--__iter.__outer_, __iter.__parent_);
    return __segment_iterator(__iter.__outer_, __iter.__parent_);
  }

  static constexpr _LIBCPP_HIDE_FROM_ABI __local_iterator __local(_JoinViewIterator __iter) {
    if (ranges::empty(__iter.__parent_->__base_))
      return {};
    if (!__iter.__inner_.has_value())
      return ranges::end(*--__iter.__outer_);
    return *__iter.__inner_;
  }

  static constexpr _LIBCPP_HIDE_FROM_ABI __local_iterator __begin(__segment_iterator __iter) {
    return ranges::begin(*__iter.__get_iter());
  }

  static constexpr _LIBCPP_HIDE_FROM_ABI __local_iterator __end(__segment_iterator __iter) {
    return ranges::end(*__iter.__get_iter());
  }

  static constexpr _LIBCPP_HIDE_FROM_ABI _JoinViewIterator
  __compose(__segment_iterator __seg_iter, __local_iterator __local_iter) {
    return _JoinViewIterator(
        std::move(__seg_iter).__get_data(), std::move(__seg_iter).__get_iter(), std::move(__local_iter));
  }
};

#endif // #if _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_JOIN_VIEW_H
