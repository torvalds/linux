// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___RANGES_RANGE_ADAPTOR_H
#define _LIBCPP___RANGES_RANGE_ADAPTOR_H

#include <__concepts/constructible.h>
#include <__concepts/derived_from.h>
#include <__concepts/invocable.h>
#include <__concepts/same_as.h>
#include <__config>
#include <__functional/compose.h>
#include <__functional/invoke.h>
#include <__ranges/concepts.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_class.h>
#include <__type_traits/is_nothrow_constructible.h>
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

// CRTP base that one can derive from in order to be considered a range adaptor closure
// by the library. When deriving from this class, a pipe operator will be provided to
// make the following hold:
// - `x | f` is equivalent to `f(x)`
// - `f1 | f2` is an adaptor closure `g` such that `g(x)` is equivalent to `f2(f1(x))`
template <class _Tp>
  requires is_class_v<_Tp> && same_as<_Tp, remove_cv_t<_Tp>>
struct __range_adaptor_closure;

// Type that wraps an arbitrary function object and makes it into a range adaptor closure,
// i.e. something that can be called via the `x | f` notation.
template <class _Fn>
struct __range_adaptor_closure_t : _Fn, __range_adaptor_closure<__range_adaptor_closure_t<_Fn>> {
  _LIBCPP_HIDE_FROM_ABI constexpr explicit __range_adaptor_closure_t(_Fn&& __f) : _Fn(std::move(__f)) {}
};
_LIBCPP_CTAD_SUPPORTED_FOR_TYPE(__range_adaptor_closure_t);

template <class _Tp>
_Tp __derived_from_range_adaptor_closure(__range_adaptor_closure<_Tp>*);

template <class _Tp>
concept _RangeAdaptorClosure = !ranges::range<remove_cvref_t<_Tp>> && requires {
  // Ensure that `remove_cvref_t<_Tp>` is derived from `__range_adaptor_closure<remove_cvref_t<_Tp>>` and isn't derived
  // from `__range_adaptor_closure<U>` for any other type `U`.
  { ranges::__derived_from_range_adaptor_closure((remove_cvref_t<_Tp>*)nullptr) } -> same_as<remove_cvref_t<_Tp>>;
};

template <ranges::range _Range, _RangeAdaptorClosure _Closure>
  requires invocable<_Closure, _Range>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr decltype(auto)
operator|(_Range&& __range, _Closure&& __closure) noexcept(is_nothrow_invocable_v<_Closure, _Range>) {
  return std::invoke(std::forward<_Closure>(__closure), std::forward<_Range>(__range));
}

template <_RangeAdaptorClosure _Closure, _RangeAdaptorClosure _OtherClosure>
  requires constructible_from<decay_t<_Closure>, _Closure> && constructible_from<decay_t<_OtherClosure>, _OtherClosure>
[[nodiscard]] _LIBCPP_HIDE_FROM_ABI constexpr auto operator|(_Closure&& __c1, _OtherClosure&& __c2) noexcept(
    is_nothrow_constructible_v<decay_t<_Closure>, _Closure> &&
    is_nothrow_constructible_v<decay_t<_OtherClosure>, _OtherClosure>) {
  return __range_adaptor_closure_t(std::__compose(std::forward<_OtherClosure>(__c2), std::forward<_Closure>(__c1)));
}

template <class _Tp>
  requires is_class_v<_Tp> && same_as<_Tp, remove_cv_t<_Tp>>
struct __range_adaptor_closure {};

#  if _LIBCPP_STD_VER >= 23
template <class _Tp>
  requires is_class_v<_Tp> && same_as<_Tp, remove_cv_t<_Tp>>
class range_adaptor_closure : public __range_adaptor_closure<_Tp> {};
#  endif // _LIBCPP_STD_VER >= 23

} // namespace ranges

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___RANGES_RANGE_ADAPTOR_H
