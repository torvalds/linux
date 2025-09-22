// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___FUNCTIONAL_ALLOCATOR_ARG_T_H
#define _LIBCPP___FUNCTIONAL_ALLOCATOR_ARG_T_H

#include <__config>
#include <__memory/uses_allocator.h>
#include <__type_traits/integral_constant.h>
#include <__type_traits/is_constructible.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

struct _LIBCPP_TEMPLATE_VIS allocator_arg_t {
  explicit allocator_arg_t() = default;
};

#if _LIBCPP_STD_VER >= 17
inline constexpr allocator_arg_t allocator_arg = allocator_arg_t();
#elif !defined(_LIBCPP_CXX03_LANG)
constexpr allocator_arg_t allocator_arg = allocator_arg_t();
#endif

#ifndef _LIBCPP_CXX03_LANG

// allocator construction

template <class _Tp, class _Alloc, class... _Args>
struct __uses_alloc_ctor_imp {
  typedef _LIBCPP_NODEBUG __remove_cvref_t<_Alloc> _RawAlloc;
  static const bool __ua = uses_allocator<_Tp, _RawAlloc>::value;
  static const bool __ic = is_constructible<_Tp, allocator_arg_t, _Alloc, _Args...>::value;
  static const int value = __ua ? 2 - __ic : 0;
};

template <class _Tp, class _Alloc, class... _Args>
struct __uses_alloc_ctor : integral_constant<int, __uses_alloc_ctor_imp<_Tp, _Alloc, _Args...>::value> {};

template <class _Tp, class _Allocator, class... _Args>
inline _LIBCPP_HIDE_FROM_ABI void
__user_alloc_construct_impl(integral_constant<int, 0>, _Tp* __storage, const _Allocator&, _Args&&... __args) {
  new (__storage) _Tp(std::forward<_Args>(__args)...);
}

// FIXME: This should have a version which takes a non-const alloc.
template <class _Tp, class _Allocator, class... _Args>
inline _LIBCPP_HIDE_FROM_ABI void
__user_alloc_construct_impl(integral_constant<int, 1>, _Tp* __storage, const _Allocator& __a, _Args&&... __args) {
  new (__storage) _Tp(allocator_arg, __a, std::forward<_Args>(__args)...);
}

// FIXME: This should have a version which takes a non-const alloc.
template <class _Tp, class _Allocator, class... _Args>
inline _LIBCPP_HIDE_FROM_ABI void
__user_alloc_construct_impl(integral_constant<int, 2>, _Tp* __storage, const _Allocator& __a, _Args&&... __args) {
  new (__storage) _Tp(std::forward<_Args>(__args)..., __a);
}

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___FUNCTIONAL_ALLOCATOR_ARG_T_H
