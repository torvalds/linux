// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_ZIP_VIEW_H
#define _LIBCPP___RANGES_ZIP_VIEW_H

#include <__config>

#include <__algorithm/ranges_min.h>
#include <__compare/three_way_comparable.h>
#include <__concepts/convertible_to.h>
#include <__concepts/equality_comparable.h>
#include <__functional/invoke.h>
#include <__functional/operations.h>
#include <__iterator/concepts.h>
#include <__iterator/incrementable_traits.h>
#include <__iterator/iter_move.h>
#include <__iterator/iter_swap.h>
#include <__iterator/iterator_traits.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/empty_view.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/size.h>
#include <__ranges/view_interface.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/make_unsigned.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/integer_sequence.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

namespace ranges {

template <class... _Ranges>
concept __zip_is_common =
    (sizeof...(_Ranges) == 1 && (common_range<_Ranges> && ...)) ||
    (!(bidirectional_range<_Ranges> && ...) && (common_range<_Ranges> && ...)) ||
    ((random_access_range<_Ranges> && ...) && (sized_range<_Ranges> && ...));

template <typename _Tp, typename _Up>
auto __tuple_or_pair_test() -> pair<_Tp, _Up>;

template <typename... _Types>
  requires(sizeof...(_Types) != 2)
auto __tuple_or_pair_test() -> tuple<_Types...>;

template <class... _Types>
using __tuple_or_pair = decltype(__tuple_or_pair_test<_Types...>());

template <class _Fun, class _Tuple>
_LIBCPP_HIDE_FROM_ABI constexpr auto __tuple_transform(_Fun&& __f, _Tuple&& __tuple) {
  return std::apply(
      [&]<class... _Types>(_Types&&... __elements) {
        return __tuple_or_pair<invoke_result_t<_Fun&, _Types>...>(
            std::invoke(__f, std::forward<_Types>(__elements))...);
      },
      std::forward<_Tuple>(__tuple));
}

template <class _Fun, class _Tuple>
_LIBCPP_HIDE_FROM_ABI constexpr void __tuple_for_each(_Fun&& __f, _Tuple&& __tuple) {
  std::apply(
      [&]<class... _Types>(_Types&&... __elements) {
        (static_cast<void>(std::invoke(__f, std::forward<_Types>(__elements))), ...);
      },
      std::forward<_Tuple>(__tuple));
}

template <class _Fun, class _Tuple1, class _Tuple2, size_t... _Indices>
_LIBCPP_HIDE_FROM_ABI constexpr __tuple_or_pair<
    invoke_result_t<_Fun&,
                    typename tuple_element<_Indices, remove_cvref_t<_Tuple1>>::type,
                    typename tuple_element<_Indices, remove_cvref_t<_Tuple2>>::type>...>
__tuple_zip_transform(_Fun&& __f, _Tuple1&& __tuple1, _Tuple2&& __tuple2, index_sequence<_Indices...>) {
  return {std::invoke(__f,
                      std::get<_Indices>(std::forward<_Tuple1>(__tuple1)),
                      std::get<_Indices>(std::forward<_Tuple2>(__tuple2)))...};
}

template <class _Fun, class _Tuple1, class _Tuple2>
_LIBCPP_HIDE_FROM_ABI constexpr auto __tuple_zip_transform(_Fun&& __f, _Tuple1&& __tuple1, _Tuple2&& __tuple2) {
  return ranges::__tuple_zip_transform(
      __f,
      std::forward<_Tuple1>(__tuple1),
      std::forward<_Tuple2>(__tuple2),
      std::make_index_sequence<tuple_size<remove_cvref_t<_Tuple1>>::value>());
}

template <class _Fun, class _Tuple1, class _Tuple2, size_t... _Indices>
_LIBCPP_HIDE_FROM_ABI constexpr void
__tuple_zip_for_each(_Fun&& __f, _Tuple1&& __tuple1, _Tuple2&& __tuple2, index_sequence<_Indices...>) {
  (std::invoke(
       __f, std::get<_Indices>(std::forward<_Tuple1>(__tuple1)), std::get<_Indices>(std::forward<_Tuple2>(__tuple2))),
   ...);
}

template <class _Fun, class _Tuple1, class _Tuple2>
_LIBCPP_HIDE_FROM_ABI constexpr auto __tuple_zip_for_each(_Fun&& __f, _Tuple1&& __tuple1, _Tuple2&& __tuple2) {
  return ranges::__tuple_zip_for_each(
      __f,
      std::forward<_Tuple1>(__tuple1),
      std::forward<_Tuple2>(__tuple2),
      std::make_index_sequence<tuple_size<remove_cvref_t<_Tuple1>>::value>());
}

template <class _Tuple1, class _Tuple2>
_LIBCPP_HIDE_FROM_ABI constexpr bool __tuple_any_equals(const _Tuple1& __tuple1, const _Tuple2& __tuple2) {
  const auto __equals = ranges::__tuple_zip_transform(std::equal_to<>(), __tuple1, __tuple2);
  return std::apply([](auto... __bools) { return (__bools || ...); }, __equals);
}

// abs in cstdlib is not constexpr
// TODO : remove __abs once P0533R9 is implemented.
template <class _Tp>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp __abs(_Tp __t) {
  return __t < 0 ? -__t : __t;
}

template <input_range... _Views>
  requires(view<_Views> && ...) && (sizeof...(_Views) > 0)
class zip_view : public view_interface<zip_view<_Views...>> {
  _LIBCPP_NO_UNIQUE_ADDRESS tuple<_Views...> __views_;

  template <bool>
  class __iterator;

  template <bool>
  class __sentinel;

public:
  _LIBCPP_HIDE_FROM_ABI zip_view() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit zip_view(_Views... __views) : __views_(std::move(__views)...) {}

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin()
    requires(!(__simple_view<_Views> && ...))
  {
    return __iterator<false>(ranges::__tuple_transform(ranges::begin, __views_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires(range<const _Views> && ...)
  {
    return __iterator<true>(ranges::__tuple_transform(ranges::begin, __views_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires(!(__simple_view<_Views> && ...))
  {
    if constexpr (!__zip_is_common<_Views...>) {
      return __sentinel<false>(ranges::__tuple_transform(ranges::end, __views_));
    } else if constexpr ((random_access_range<_Views> && ...)) {
      return begin() + iter_difference_t<__iterator<false>>(size());
    } else {
      return __iterator<false>(ranges::__tuple_transform(ranges::end, __views_));
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires(range<const _Views> && ...)
  {
    if constexpr (!__zip_is_common<const _Views...>) {
      return __sentinel<true>(ranges::__tuple_transform(ranges::end, __views_));
    } else if constexpr ((random_access_range<const _Views> && ...)) {
      return begin() + iter_difference_t<__iterator<true>>(size());
    } else {
      return __iterator<true>(ranges::__tuple_transform(ranges::end, __views_));
    }
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size()
    requires(sized_range<_Views> && ...)
  {
    return std::apply(
        [](auto... __sizes) {
          using _CT = make_unsigned_t<common_type_t<decltype(__sizes)...>>;
          return ranges::min({_CT(__sizes)...});
        },
        ranges::__tuple_transform(ranges::size, __views_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto size() const
    requires(sized_range<const _Views> && ...)
  {
    return std::apply(
        [](auto... __sizes) {
          using _CT = make_unsigned_t<common_type_t<decltype(__sizes)...>>;
          return ranges::min({_CT(__sizes)...});
        },
        ranges::__tuple_transform(ranges::size, __views_));
  }
};

template <class... _Ranges>
zip_view(_Ranges&&...) -> zip_view<views::all_t<_Ranges>...>;

template <bool _Const, class... _Views>
concept __zip_all_random_access = (random_access_range<__maybe_const<_Const, _Views>> && ...);

template <bool _Const, class... _Views>
concept __zip_all_bidirectional = (bidirectional_range<__maybe_const<_Const, _Views>> && ...);

template <bool _Const, class... _Views>
concept __zip_all_forward = (forward_range<__maybe_const<_Const, _Views>> && ...);

template <bool _Const, class... _Views>
consteval auto __get_zip_view_iterator_tag() {
  if constexpr (__zip_all_random_access<_Const, _Views...>) {
    return random_access_iterator_tag();
  } else if constexpr (__zip_all_bidirectional<_Const, _Views...>) {
    return bidirectional_iterator_tag();
  } else if constexpr (__zip_all_forward<_Const, _Views...>) {
    return forward_iterator_tag();
  } else {
    return input_iterator_tag();
  }
}

template <bool _Const, class... _Views>
struct __zip_view_iterator_category_base {};

template <bool _Const, class... _Views>
  requires __zip_all_forward<_Const, _Views...>
struct __zip_view_iterator_category_base<_Const, _Views...> {
  using iterator_category = input_iterator_tag;
};

template <input_range... _Views>
  requires(view<_Views> && ...) && (sizeof...(_Views) > 0)
template <bool _Const>
class zip_view<_Views...>::__iterator : public __zip_view_iterator_category_base<_Const, _Views...> {
  __tuple_or_pair<iterator_t<__maybe_const<_Const, _Views>>...> __current_;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __iterator(
      __tuple_or_pair<iterator_t<__maybe_const<_Const, _Views>>...> __current)
      : __current_(std::move(__current)) {}

  template <bool>
  friend class zip_view<_Views...>::__iterator;

  template <bool>
  friend class zip_view<_Views...>::__sentinel;

  friend class zip_view<_Views...>;

public:
  using iterator_concept = decltype(__get_zip_view_iterator_tag<_Const, _Views...>());
  using value_type       = __tuple_or_pair<range_value_t<__maybe_const<_Const, _Views>>...>;
  using difference_type  = common_type_t<range_difference_t<__maybe_const<_Const, _Views>>...>;

  _LIBCPP_HIDE_FROM_ABI __iterator() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator(__iterator<!_Const> __i)
    requires _Const && (convertible_to<iterator_t<_Views>, iterator_t<__maybe_const<_Const, _Views>>> && ...)
      : __current_(std::move(__i.__current_)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr auto operator*() const {
    return ranges::__tuple_transform([](auto& __i) -> decltype(auto) { return *__i; }, __current_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator++() {
    ranges::__tuple_for_each([](auto& __i) { ++__i; }, __current_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr void operator++(int) { ++*this; }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator++(int)
    requires __zip_all_forward<_Const, _Views...>
  {
    auto __tmp = *this;
    ++*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator--()
    requires __zip_all_bidirectional<_Const, _Views...>
  {
    ranges::__tuple_for_each([](auto& __i) { --__i; }, __current_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator operator--(int)
    requires __zip_all_bidirectional<_Const, _Views...>
  {
    auto __tmp = *this;
    --*this;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator+=(difference_type __x)
    requires __zip_all_random_access<_Const, _Views...>
  {
    ranges::__tuple_for_each([&]<class _Iter>(_Iter& __i) { __i += iter_difference_t<_Iter>(__x); }, __current_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr __iterator& operator-=(difference_type __x)
    requires __zip_all_random_access<_Const, _Views...>
  {
    ranges::__tuple_for_each([&]<class _Iter>(_Iter& __i) { __i -= iter_difference_t<_Iter>(__x); }, __current_);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto operator[](difference_type __n) const
    requires __zip_all_random_access<_Const, _Views...>
  {
    return ranges::__tuple_transform(
        [&]<class _Iter>(_Iter& __i) -> decltype(auto) { return __i[iter_difference_t<_Iter>(__n)]; }, __current_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator& __x, const __iterator& __y)
    requires(equality_comparable<iterator_t<__maybe_const<_Const, _Views>>> && ...)
  {
    if constexpr (__zip_all_bidirectional<_Const, _Views...>) {
      return __x.__current_ == __y.__current_;
    } else {
      return ranges::__tuple_any_equals(__x.__current_, __y.__current_);
    }
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<(const __iterator& __x, const __iterator& __y)
    requires __zip_all_random_access<_Const, _Views...>
  {
    return __x.__current_ < __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>(const __iterator& __x, const __iterator& __y)
    requires __zip_all_random_access<_Const, _Views...>
  {
    return __y < __x;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator<=(const __iterator& __x, const __iterator& __y)
    requires __zip_all_random_access<_Const, _Views...>
  {
    return !(__y < __x);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator>=(const __iterator& __x, const __iterator& __y)
    requires __zip_all_random_access<_Const, _Views...>
  {
    return !(__x < __y);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr auto operator<=>(const __iterator& __x, const __iterator& __y)
    requires __zip_all_random_access<_Const, _Views...> &&
             (three_way_comparable<iterator_t<__maybe_const<_Const, _Views>>> && ...)
  {
    return __x.__current_ <=> __y.__current_;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(const __iterator& __i, difference_type __n)
    requires __zip_all_random_access<_Const, _Views...>
  {
    auto __r = __i;
    __r += __n;
    return __r;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator+(difference_type __n, const __iterator& __i)
    requires __zip_all_random_access<_Const, _Views...>
  {
    return __i + __n;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr __iterator operator-(const __iterator& __i, difference_type __n)
    requires __zip_all_random_access<_Const, _Views...>
  {
    auto __r = __i;
    __r -= __n;
    return __r;
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr difference_type operator-(const __iterator& __x, const __iterator& __y)
    requires(sized_sentinel_for<iterator_t<__maybe_const<_Const, _Views>>, iterator_t<__maybe_const<_Const, _Views>>> &&
             ...)
  {
    const auto __diffs = ranges::__tuple_zip_transform(minus<>(), __x.__current_, __y.__current_);
    return std::apply(
        [](auto... __ds) {
          return ranges::min({difference_type(__ds)...}, [](auto __a, auto __b) {
            return ranges::__abs(__a) < ranges::__abs(__b);
          });
        },
        __diffs);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr auto iter_move(const __iterator& __i) noexcept(
      (noexcept(ranges::iter_move(std::declval<const iterator_t<__maybe_const<_Const, _Views>>&>())) && ...) &&
      (is_nothrow_move_constructible_v<range_rvalue_reference_t<__maybe_const<_Const, _Views>>> && ...)) {
    return ranges::__tuple_transform(ranges::iter_move, __i.__current_);
  }

  _LIBCPP_HIDE_FROM_ABI friend constexpr void iter_swap(const __iterator& __l, const __iterator& __r) noexcept(
      (noexcept(ranges::iter_swap(std::declval<const iterator_t<__maybe_const<_Const, _Views>>&>(),
                                  std::declval<const iterator_t<__maybe_const<_Const, _Views>>&>())) &&
       ...))
    requires(indirectly_swappable<iterator_t<__maybe_const<_Const, _Views>>> && ...)
  {
    ranges::__tuple_zip_for_each(ranges::iter_swap, __l.__current_, __r.__current_);
  }
};

template <input_range... _Views>
  requires(view<_Views> && ...) && (sizeof...(_Views) > 0)
template <bool _Const>
class zip_view<_Views...>::__sentinel {
  __tuple_or_pair<sentinel_t<__maybe_const<_Const, _Views>>...> __end_;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(
      __tuple_or_pair<sentinel_t<__maybe_const<_Const, _Views>>...> __end)
      : __end_(__end) {}

  friend class zip_view<_Views...>;

  // hidden friend cannot access private member of iterator because they are friends of friends
  template <bool _OtherConst>
  _LIBCPP_HIDE_FROM_ABI static constexpr decltype(auto)
  __iter_current(zip_view<_Views...>::__iterator<_OtherConst> const& __it) {
    return (__it.__current_);
  }

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel(__sentinel<!_Const> __i)
    requires _Const && (convertible_to<sentinel_t<_Views>, sentinel_t<__maybe_const<_Const, _Views>>> && ...)
      : __end_(std::move(__i.__end_)) {}

  template <bool _OtherConst>
    requires(sentinel_for<sentinel_t<__maybe_const<_Const, _Views>>, iterator_t<__maybe_const<_OtherConst, _Views>>> &&
             ...)
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const __iterator<_OtherConst>& __x, const __sentinel& __y) {
    return ranges::__tuple_any_equals(__iter_current(__x), __y.__end_);
  }

  template <bool _OtherConst>
    requires(
        sized_sentinel_for<sentinel_t<__maybe_const<_Const, _Views>>, iterator_t<__maybe_const<_OtherConst, _Views>>> &&
        ...)
  _LIBCPP_HIDE_FROM_ABI friend constexpr common_type_t<range_difference_t<__maybe_const<_OtherConst, _Views>>...>
  operator-(const __iterator<_OtherConst>& __x, const __sentinel& __y) {
    const auto __diffs = ranges::__tuple_zip_transform(minus<>(), __iter_current(__x), __y.__end_);
    return std::apply(
        [](auto... __ds) {
          using _Diff = common_type_t<range_difference_t<__maybe_const<_OtherConst, _Views>>...>;
          return ranges::min({_Diff(__ds)...}, [](auto __a, auto __b) {
            return ranges::__abs(__a) < ranges::__abs(__b);
          });
        },
        __diffs);
  }

  template <bool _OtherConst>
    requires(
        sized_sentinel_for<sentinel_t<__maybe_const<_Const, _Views>>, iterator_t<__maybe_const<_OtherConst, _Views>>> &&
        ...)
  _LIBCPP_HIDE_FROM_ABI friend constexpr common_type_t<range_difference_t<__maybe_const<_OtherConst, _Views>>...>
  operator-(const __sentinel& __y, const __iterator<_OtherConst>& __x) {
    return -(__x - __y);
  }
};

template <class... _Views>
inline constexpr bool enable_borrowed_range<zip_view<_Views...>> = (enable_borrowed_range<_Views> && ...);

namespace views {
namespace __zip {

struct __fn {
  _LIBCPP_HIDE_FROM_ABI static constexpr auto operator()() noexcept { return empty_view<tuple<>>{}; }

  template <class... _Ranges>
  _LIBCPP_HIDE_FROM_ABI static constexpr auto
  operator()(_Ranges&&... __rs) noexcept(noexcept(zip_view<all_t<_Ranges&&>...>(std::forward<_Ranges>(__rs)...)))
      -> decltype(zip_view<all_t<_Ranges&&>...>(std::forward<_Ranges>(__rs)...)) {
    return zip_view<all_t<_Ranges>...>(std::forward<_Ranges>(__rs)...);
  }
};

} // namespace __zip
inline namespace __cpo {
inline constexpr auto zip = __zip::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_ZIP_VIEW_H
