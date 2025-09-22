// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_TAKE_WHILE_VIEW_H
#define _LIBCPP___RANGES_TAKE_WHILE_VIEW_H

#include <__concepts/constructible.h>
#include <__concepts/convertible_to.h>
#include <__config>
#include <__functional/bind_back.h>
#include <__functional/invoke.h>
#include <__iterator/concepts.h>
#include <__memory/addressof.h>
#include <__ranges/access.h>
#include <__ranges/all.h>
#include <__ranges/concepts.h>
#include <__ranges/movable_box.h>
#include <__ranges/range_adaptor.h>
#include <__ranges/view_interface.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_nothrow_constructible.h>
#include <__type_traits/is_object.h>
#include <__type_traits/maybe_const.h>
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

template <view _View, class _Pred>
  requires input_range<_View> && is_object_v<_Pred> && indirect_unary_predicate<const _Pred, iterator_t<_View>>
class _LIBCPP_ABI_LLVM18_NO_UNIQUE_ADDRESS take_while_view : public view_interface<take_while_view<_View, _Pred>> {
  template <bool>
  class __sentinel;

  _LIBCPP_NO_UNIQUE_ADDRESS _View __base_ = _View();
  _LIBCPP_NO_UNIQUE_ADDRESS __movable_box<_Pred> __pred_;

public:
  _LIBCPP_HIDE_FROM_ABI take_while_view()
    requires default_initializable<_View> && default_initializable<_Pred>
  = default;

  _LIBCPP_HIDE_FROM_ABI constexpr _LIBCPP_EXPLICIT_SINCE_CXX23 take_while_view(_View __base, _Pred __pred)
      : __base_(std::move(__base)), __pred_(std::in_place, std::move(__pred)) {}

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() const&
    requires copy_constructible<_View>
  {
    return __base_;
  }

  _LIBCPP_HIDE_FROM_ABI constexpr _View base() && { return std::move(__base_); }

  _LIBCPP_HIDE_FROM_ABI constexpr const _Pred& pred() const { return *__pred_; }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin()
    requires(!__simple_view<_View>)
  {
    return ranges::begin(__base_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto begin() const
    requires range<const _View> && indirect_unary_predicate<const _Pred, iterator_t<const _View>>
  {
    return ranges::begin(__base_);
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end()
    requires(!__simple_view<_View>)
  {
    return __sentinel</*_Const=*/false>(ranges::end(__base_), std::addressof(*__pred_));
  }

  _LIBCPP_HIDE_FROM_ABI constexpr auto end() const
    requires range<const _View> && indirect_unary_predicate<const _Pred, iterator_t<const _View>>
  {
    return __sentinel</*_Const=*/true>(ranges::end(__base_), std::addressof(*__pred_));
  }
};

template <class _Range, class _Pred>
take_while_view(_Range&&, _Pred) -> take_while_view<views::all_t<_Range>, _Pred>;

template <view _View, class _Pred>
  requires input_range<_View> && is_object_v<_Pred> && indirect_unary_predicate<const _Pred, iterator_t<_View>>
template <bool _Const>
class take_while_view<_View, _Pred>::__sentinel {
  using _Base = __maybe_const<_Const, _View>;

  sentinel_t<_Base> __end_ = sentinel_t<_Base>();
  const _Pred* __pred_     = nullptr;

  friend class __sentinel<!_Const>;

public:
  _LIBCPP_HIDE_FROM_ABI __sentinel() = default;

  _LIBCPP_HIDE_FROM_ABI constexpr explicit __sentinel(sentinel_t<_Base> __end, const _Pred* __pred)
      : __end_(std::move(__end)), __pred_(__pred) {}

  _LIBCPP_HIDE_FROM_ABI constexpr __sentinel(__sentinel<!_Const> __s)
    requires _Const && convertible_to<sentinel_t<_View>, sentinel_t<_Base>>
      : __end_(std::move(__s.__end_)), __pred_(__s.__pred_) {}

  _LIBCPP_HIDE_FROM_ABI constexpr sentinel_t<_Base> base() const { return __end_; }

  _LIBCPP_HIDE_FROM_ABI friend constexpr bool operator==(const iterator_t<_Base>& __x, const __sentinel& __y) {
    return __x == __y.__end_ || !std::invoke(*__y.__pred_, *__x);
  }

  template <bool _OtherConst = !_Const>
    requires sentinel_for<sentinel_t<_Base>, iterator_t<__maybe_const<_OtherConst, _View>>>
  _LIBCPP_HIDE_FROM_ABI friend constexpr bool
  operator==(const iterator_t<__maybe_const<_OtherConst, _View>>& __x, const __sentinel& __y) {
    return __x == __y.__end_ || !std::invoke(*__y.__pred_, *__x);
  }
};

namespace views {
namespace __take_while {

struct __fn {
  template <class _Range, class _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Range&& __range, _Pred&& __pred) const
      noexcept(noexcept(/**/ take_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))))
          -> decltype(/*--*/ take_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred))) {
    return /*-------------*/ take_while_view(std::forward<_Range>(__range), std::forward<_Pred>(__pred));
  }

  template <class _Pred>
    requires constructible_from<decay_t<_Pred>, _Pred>
  [[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator()(_Pred&& __pred) const
      noexcept(is_nothrow_constructible_v<decay_t<_Pred>, _Pred>) {
    return __range_adaptor_closure_t(std::__bind_back(*this, std::forward<_Pred>(__pred)));
  }
};

} // namespace __take_while

inline namespace __cpo {
inline constexpr auto take_while = __take_while::__fn{};
} // namespace __cpo
} // namespace views
} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_TAKE_WHILE_VIEW_H
