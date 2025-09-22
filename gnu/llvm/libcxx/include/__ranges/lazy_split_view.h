// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_LAZY_SPLIT_VIEW_H
#define _LIBCPP___RANGES_LAZY_SPLIT_VIEW_H

#include <__algorithm/ranges_find.h>
#include <__algorithm/ranges_mismatch.h>
#include <__assert>
#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__concepts/derived_from.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/ranges_operations.h>
#include <__iterator/concepts.h>
#include <__iterator/default_sentinel.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/indirectly_comparable.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/single_view.h>
#include <__ranges/subrange.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/maybe_const.h>
#include <__type_traits/remove_reference.h>
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

template <auto>
struct __require_constant;

template <class _Range>
concept __tiny_range = sized_range<_Range> && requires {
  typename __require_constant<remove_reference_t<_Range>::size()>;
} && (remove_reference_t<_Range>::size() <= 1);

template <input_range _View, forward_range _Pattern>
  requires view<_View> && view<_Pattern> &&
           indirectly_comparable<iterator_t<_View>, iterator_t<_Pattern>, ranges::equal_to> &&
           (forward_range<_View> || __tiny_range<_Pattern>)
class lazy_split_view : public view_interface<lazy_split_view<_View, _Pattern>> {
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_       = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS _Pattern __pattern_ = _Pattern();

  using _MaybeCurrent = _If<!forward_range<_View>, __non_propagating_cache<iterator_t<_View>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _MaybeCurrent __current_ = _MaybeCurrent();

  template <bool>
  struct __outer_iterator;
  template <bool>
  struct __inner_iterator;

public:
  _LIBCPP_HIDE_FROM_ABI lazy_split_view()
    requires default_initializable<_View> && default_initializable<_Pattern>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 lazy_split_view(_View __base, _Pattern __pattern)
      : __base_(std::move(__base)), __pattern_(std::move(__pattern)) {}

  template <input_range _Range>
    requires constructible_from<_View, views::all_t<_Range>> &&
                 constructible_from<_Pattern, single_view<range_value_t<_Range>>>
  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 lazy_split_view(_Range&& __r, range_value_t<_Range> __e)
      : __base_(views::all(std::forward<_Range>(__r))), __pattern_(views::single(std::move(__e))) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }
  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() {
    if constexpr (forward_range<_View>) {
      return __outer_iterator < __simple_view<_View> && __simple_view < _Pattern >> {*this, ranges::begin(__base_)};
    } else {
      __current_.__emplace(ranges::begin(__base_));
      return __outer_iterator<false>{*this};
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires forward_range<_View> && forward_range<const _View>
  {
    return __outer_iterator<true>{*this, ranges::begin(__base_)};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires forward_range<_View> && common_range<_View>
  {
    return __outer_iterator < __simple_view<_View> && __simple_view < _Pattern >> {*this, ranges::end(__base_)};
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const {
    if constexpr (forward_range<_View> && forward_range<const _View> && common_range<const _View>) {
      return __outer_iterator<true>{*this, ranges::end(__base_)};
    } else {
      return default_sentinel;
    }
  }

private:
  template <class>
  struct __outer_iterator_category {};

  template <forward_range _Tp>
  struct __outer_iterator_category<_Tp> {
    using iterator_category = input_iterator_tag;
  };

  template <bool _Const>
  struct __outer_iterator : __outer_iterator_category<__maybe_const<_Const, _View>> {
  private:
    template <bool>
    friend struct __inner_iterator;
    friend __outer_iterator<true>;

    using _Parent = __maybe_const<_Const, lazy_split_view>;
    using _Base   = __maybe_const<_Const, _View>;

    _Parent* __parent_                                 = nullptr;
    using _MaybeCurrent                                = _If<forward_range<_View>, iterator_t<_Base>, __empty_cache>;
    _LIBCPP_NO_UNIQUE_ADDRESS _MaybeCurrent __current_ = _MaybeCurrent();
    bool __trailing_empty_                             = false;

    [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto& __current() noexcept {
      if constexpr (forward_range<_View>) {
        return __current_;
      } else {
        return *__parent_->__current_;
      }
    }

    [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr const auto& __current() const noexcept {
      if constexpr (forward_range<_View>) {
        return __current_;
      } else {
        return *__parent_->__current_;
      }
    }

    // Workaround for the GCC issue that doesn't allow calling `__parent_->__base_` from friend functions (because
    // `__base_` is private).
    [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto& __parent_base() const noexcept { return __parent_->__base_; }

  public:
    // using iterator_category = inherited;
    using iterator_concept = conditional_t<forward_range<_Base>, forward_iterator_tag, input_iterator_tag>;
    using difference_type  = range_difference_t<_Base>;

    struct value_type : view_interface<value_type> {
    private:
      __outer_iterator __i_ = __outer_iterator();

    public:
      _LIBCPP_HIDE_FROM_ABI value_type() = default;
      _LIBCPP_HIDE_FROM_ABI constexpr explicit value_type(__outer_iterator __i) : __i_(std::move(__i)) {}

      _LIBCPP_HIDE_FROM_ABI constexpr __inner_iterator<_Const> begin() const { return __inner_iterator<_Const>{__i_}; }
      _LIBCPP_HIDE_FROM_ABI constexpr default_sentinel_t end() const noexcept { return default_sentinel; }
    };

    _LIBCPP_HIDE_FROM_ABI __outer_iterator() = default;

    _LIBCPP_HIDE_FROM_ABI constexpr explicit __outer_iterator(_Parent& __parent)
      requires(!forward_range<_Base>)
        : __parent_(std::addressof(__parent)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr __outer_iterator(_Parent& __parent, iterator_t<_Base> __current)
      requires forward_range<_Base>
        : __parent_(std::addressof(__parent)), __current_(std::move(__current)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr __outer_iterator(__outer_iterator<!_Const> __i)
      requires _Const && convertible_to<iterator_t<_View>, iterator_t<_Base>>
        : __parent_(__i.__parent_), __current_(std::move(__i.__current_)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr value_type operator*() const { return value_type{*this}; }

    _LIBCPP_HIDE_FROM_ABI constexpr __outer_iterator& operator++() {
      const auto __end = ranges::end(__parent_->__base_);
      if (__current() == __end) {
        __trailing_empty_ = false;
        return *this;
      }

      const auto [__pbegin, __pend] = ranges::subrange{__parent_->__pattern_};
      if (__pbegin == __pend) {
        // Empty pattern: split on every element in the input range
        ++__current();

      } else if constexpr (__tiny_range<_Pattern>) {
        // One-element pattern: we can use `ranges::find`.
        __current() = ranges::find(std::move(__current()), __end, *__pbegin);
        if (__current() != __end) {
          // Make sure we point to after the separator we just found.
          ++__current();
          if (__current() == __end)
            __trailing_empty_ = true;
        }

      } else {
        // General case for n-element pattern.
        do {
          const auto [__b, __p] = ranges::mismatch(__current(), __end, __pbegin, __pend);
          if (__p == __pend) {
            __current() = __b;
            if (__current() == __end) {
              __trailing_empty_ = true;
            }
            break; // The pattern matched; skip it.
          }
        } while (++__current() != __end);
      }

      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator++(int) {
      if constexpr (forward_range<_Base>) {
        auto __tmp = *this;
        ++*this;
        return __tmp;

      } else {
        ++*this;
      }
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __outer_iterator& __x, const __outer_iterator& __y)
      requires forward_range<_Base>
    {
      return __x.__current_ == __y.__current_ && __x.__trailing_empty_ == __y.__trailing_empty_;
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __outer_iterator& __x, default_sentinel_t) {
      _LIBCPP_ASSERT_NON_NULL(__x.__parent_ != nullptr, "Cannot call comparison on a default-constructed iterator.");
      return __x.__current() == ranges::end(__x.__parent_base()) && !__x.__trailing_empty_;
    }
  };

  template <class>
  struct __inner_iterator_category {};

  template <forward_range _Tp>
  struct __inner_iterator_category<_Tp> {
    using iterator_category =
        _If< derived_from<typename iterator_traits<iterator_t<_Tp>>::iterator_category, forward_iterator_tag>,
             forward_iterator_tag,
             typename iterator_traits<iterator_t<_Tp>>::iterator_category >;
  };

  template <bool _Const>
  struct __inner_iterator : __inner_iterator_category<__maybe_const<_Const, _View>> {
  private:
    using _Base = __maybe_const<_Const, _View>;
    // Workaround for a GCC issue.
    static constexpr bool _OuterConst = _Const;
    __outer_iterator<_Const> __i_     = __outer_iterator<_OuterConst>();
    bool __incremented_               = false;

    // Note: these private functions are necessary because GCC doesn't allow calls to private members of `__i_` from
    // free functions that are friends of `inner-iterator`.

    _LIBCPP_HIDE_FROM_ABI constexpr bool __is_done() const {
      _LIBCPP_ASSERT_NON_NULL(__i_.__parent_ != nullptr, "Cannot call comparison on a default-constructed iterator.");

      auto [__pcur, __pend] = ranges::subrange{__i_.__parent_->__pattern_};
      auto __end            = ranges::end(__i_.__parent_->__base_);

      if constexpr (__tiny_range<_Pattern>) {
        const auto& __cur = __i_.__current();
        if (__cur == __end)
          return true;
        if (__pcur == __pend)
          return __incremented_;

        return *__cur == *__pcur;

      } else {
        auto __cur = __i_.__current();
        if (__cur == __end)
          return true;
        if (__pcur == __pend)
          return __incremented_;

        do {
          if (*__cur != *__pcur)
            return false;
          if (++__pcur == __pend)
            return true;
        } while (++__cur != __end);

        return false;
      }
    }

    [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto& __outer_current() noexcept { return __i_.__current(); }

    [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr const auto& __outer_current() const noexcept {
      return __i_.__current();
    }

  public:
    // using iterator_category = inherited;
    using iterator_concept = typename __outer_iterator<_Const>::iterator_concept;
    using value_type       = range_value_t<_Base>;
    using difference_type  = range_difference_t<_Base>;

    _LIBCPP_HIDE_FROM_ABI __inner_iterator() = default;

    _LIBCPP_HIDE_FROM_ABI constexpr explicit __inner_iterator(__outer_iterator<_Const> __i) : __i_(std::move(__i)) {}

    _LIBCPP_HIDE_FROM_ABI constexpr const iterator_t<_Base>& base() const& noexcept { return __i_.__current(); }
    _LIBCPP_HIDE_FROM_ABI constexpr iterator_t<_Base> base() &&
      requires forward_range<_View>
    {
      return std::move(__i_.__current());
    }

    _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator*() const { return *__i_.__current(); }

    _LIBCPP_HIDE_FROM_ABI constexpr __inner_iterator& operator++() {
      __incremented_ = true;

      if constexpr (!forward_range<_Base>) {
        if constexpr (_Pattern::size() == 0) {
          return *this;
        }
      }

      ++__i_.__current();
      return *this;
    }

    _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto) operator++(int) {
      if constexpr (forward_range<_Base>) {
        auto __tmp = *this;
        ++*this;
        return __tmp;

      } else {
        ++*this;
      }
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __inner_iterator& __x, const __inner_iterator& __y)
      requires forward_range<_Base>
    {
      return __x.__outer_current() == __y.__outer_current();
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __inner_iterator& __x, default_sentinel_t) {
      return __x.__is_done();
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr decltype(auto)
    iter_move(const __inner_iterator& __i) noexcept(noexcept(ranges::iter_move(__i.__outer_current()))) {
      return ranges::iter_move(__i.__outer_current());
    }

    _LIBCPP_HIDE_FROM_ABI friend constexpr void iter_swap(
        const __inner_iterator& __x,
        const __inner_iterator& __y) noexcept(noexcept(ranges::iter_swap(__x.__outer_current(), __y.__outer_current())))
      requires indirectly_swappable<iterator_t<_Base>>
    {
      ranges::iter_swap(__x.__outer_current(), __y.__outer_current());
    }
  };
};

template <class _Range, class _Pattern>
lazy_split_view(_Range&&, _Pattern&&) -> lazy_split_view<views::all_t<_Range>, views::all_t<_Pattern>>;

template <input_range _Range>
lazy_split_view(_Range&&,
                range_value_t<_Range>) -> lazy_split_view<views::all_t<_Range>, single_view<range_value_t<_Range>>>;

namespace views {
namespace __lazy_split_view {
struct __fn {
  template <class _Range, class _Pattern>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Pattern&& __pattern) const
      noexcept(noexcept(lazy_split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern))))
          -> decltype(lazy_split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern))) {
    return lazy_split_view(std::forward<_Range>(__range), std::forward<_Pattern>(__pattern));
  }

  template <class _Pattern>
    requires constructible_from<decay_t<_Pattern>, _Pattern>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pattern&& __pattern) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pattern>, _Pattern>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pattern>(__pattern)));
  }
};
} // namespace __lazy_split_view

inline namespace __cpo {
inline constexpr auto lazy_split = __lazy_split_view::__fn{};
} // namespace __cpo
} // namespace views

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_LAZY_SPLIT_VIEW_H
