// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_REVERSE_VIEW_H
#define _LIBCPP___RANGES_REVERSE_VIEW_H

#include <__concepts/constructible.h>
#include <__config>
#include <__iterator/concepts.h>
#include <__iterator/next.h>
#include <__iterator/reverse_iterator.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/enable_borrowed_range.h>
#include <__ranges/non_propagating_cache.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/size.h>
#include <__ranges/subrange.h>
#include <__ranges/view_interface.h>
#include <__type_traits/conditional.h>
#include <__type_traits/remove_cvref.h>
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
template <view _View>
  requires bidirectional_range<_View>
class reverse_view : public view_interface<reverse_view<_View>> {
  // We cache begin() whenever ranges::next is not guaranteed O(1) to provide an
  // amortized O(1) begin() method.
  static constexpr bool _UseCache = !random_access_range<_View> && !common_range<_View>;
  using _Cache = _If<_UseCache, __non_propagating_cache<reverse_iterator<iterator_t<_View>>>, __empty_cache>;
  _LIBCPP_NO_UNIQUE_ADDRESS _Cache __cached_begin_ = _Cache();
  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_          = _View();

public:
  _LIBCPP_HIDE_FROM_ABI reverse_view()
    requires default_initializable<_View>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit reverse_view(_View __view) : __base_(std::move(__view)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr reverse_iterator<iterator_t<_View>> begin() {
    if constexpr (_UseCache)
      if (__cached_begin_.__has_value())
        return *__cached_begin_;

    auto __tmp = std::make_reverse_iterator(ranges::next(ranges::begin(__base_), ranges::end(__base_)));
    if constexpr (_UseCache)
      __cached_begin_.__emplace(__tmp);
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr reverse_iterator<iterator_t<_View>> begin()
    requires common_range<_View>
  {
    return std::make_reverse_iterator(ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires common_range<const _View>
  {
    return std::make_reverse_iterator(ranges::end(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr reverse_iterator<iterator_t<_View>> end() {
    return std::make_reverse_iterator(ranges::begin(__base_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires common_range<const _View>
  {
    return std::make_reverse_iterator(ranges::begin(__base_));
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

template <class _Range>
reverse_view(_Range&&) -> reverse_view<views::all_t<_Range>>;

template <class _Tp>
inline constexpr bool enable_borrowed_range<reverse_view<_Tp>> = enable_borrowed_range<_Tp>;

namespace views {
namespace __reverse {
template <class _Tp>
inline constexpr bool __is_reverse_view = false;

template <class _Tp>
inline constexpr bool __is_reverse_view<reverse_view<_Tp>> = true;

template <class _Tp>
inline constexpr bool __is_sized_reverse_subrange = false;

template <class _Iter>
inline constexpr bool
    __is_sized_reverse_subrange<subrange<reverse_iterator<_Iter>, reverse_iterator<_Iter>, subrange_kind::sized>> =
        true;

template <class _Tp>
inline constexpr bool __is_unsized_reverse_subrange = false;

template <class _Iter, subrange_kind _Kind>
inline constexpr bool __is_unsized_reverse_subrange<subrange<reverse_iterator<_Iter>, reverse_iterator<_Iter>, _Kind>> =
    _Kind == subrange_kind::unsized;

template <class _Tp>
struct __unwrapped_reverse_subrange {
  using type =
      void; // avoid SFINAE-ing out the overload below -- let the concept requirements do it for better diagnostics
};

template <class _Iter, subrange_kind _Kind>
struct __unwrapped_reverse_subrange<subrange<reverse_iterator<_Iter>, reverse_iterator<_Iter>, _Kind>> {
  using type = subrange<_Iter, _Iter, _Kind>;
};

struct __fn : __range_adaptor_closure<__fn> {
  template <class _Range>
    requires __is_reverse_view<remove_cvref_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const
      noexcept(noexcept(std::forward<_Range>(__range).base())) -> decltype(std::forward<_Range>(__range).base()) {
    return std::forward<_Range>(__range).base();
  }

  template <class _Range,
            class _UnwrappedSubrange = typename __unwrapped_reverse_subrange<remove_cvref_t<_Range>>::type>
    requires __is_sized_reverse_subrange<remove_cvref_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const
      noexcept(noexcept(_UnwrappedSubrange(__range.end().base(), __range.begin().base(), __range.size())))
          -> decltype(_UnwrappedSubrange(__range.end().base(), __range.begin().base(), __range.size())) {
    return _UnwrappedSubrange(__range.end().base(), __range.begin().base(), __range.size());
  }

  template <class _Range,
            class _UnwrappedSubrange = typename __unwrapped_reverse_subrange<remove_cvref_t<_Range>>::type>
    requires __is_unsized_reverse_subrange<remove_cvref_t<_Range>>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const
      noexcept(noexcept(_UnwrappedSubrange(__range.end().base(), __range.begin().base())))
          -> decltype(_UnwrappedSubrange(__range.end().base(), __range.begin().base())) {
    return _UnwrappedSubrange(__range.end().base(), __range.begin().base());
  }

  template <class _Range>
    requires(!__is_reverse_view<remove_cvref_t<_Range>> && !__is_sized_reverse_subrange<remove_cvref_t<_Range>> &&
             !__is_unsized_reverse_subrange<remove_cvref_t<_Range>>)
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range) const noexcept(noexcept(reverse_view{
      std::forward<_Range>(__range)})) -> decltype(reverse_view{std::forward<_Range>(__range)}) {
    return reverse_view{std::forward<_Range>(__range)};
  }
};
} // namespace __reverse

inline namespace __cpo {
inline constexpr auto reverse = __reverse::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_REVERSE_VIEW_H
