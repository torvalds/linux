// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_CONSTRUCT_AT_H
#define _LIBCPP___MEMORY_CONSTRUCT_AT_H

#include <__assert>
#include <__config>
#include <__iterator/access.h>
#include <__memory/addressof.h>
#include <__memory/voidify.h>
#include <__type_traits/enable_if.h>
#include <__type_traits/is_array.h>
#include <__utility/declval.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// construct_at

#if _LIBCPP_STD_VER >= 20

template <class _Tp, class... _Args, class = decltype(::new(std::declval<void*>()) _Tp(std::declval<_Args>()...))>
_LIBCPP_HIDE_FROM_ABI constexpr _Tp* construct_at(_Tp* __location, _Args&&... __args) {
  _LIBCPP_ASSERT_NON_NULL(__location != nullptr, "null pointer given to construct_at");
  return ::new (std::__voidify(*__location)) _Tp(std::forward<_Args>(__args)...);
}

#endif

template <class _Tp, class... _Args, class = decltype(::new(std::declval<void*>()) _Tp(std::declval<_Args>()...))>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _Tp* __construct_at(_Tp* __location, _Args&&... __args) {
#if _LIBCPP_STD_VER >= 20
  return std::construct_at(__location, std::forward<_Args>(__args)...);
#else
  return _LIBCPP_ASSERT_NON_NULL(__location != nullptr, "null pointer given to construct_at"),
         ::new (std::__voidify(*__location)) _Tp(std::forward<_Args>(__args)...);
#endif
}

// destroy_at

// The internal functions are available regardless of the language version (with the exception of the `__destroy_at`
// taking an array).

template <class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator __destroy(_ForwardIterator, _ForwardIterator);

template <class _Tp, __enable_if_t<!is_array<_Tp>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __destroy_at(_Tp* __loc) {
  _LIBCPP_ASSERT_NON_NULL(__loc != nullptr, "null pointer given to destroy_at");
  __loc->~_Tp();
}

#if _LIBCPP_STD_VER >= 20
template <class _Tp, __enable_if_t<is_array<_Tp>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void __destroy_at(_Tp* __loc) {
  _LIBCPP_ASSERT_NON_NULL(__loc != nullptr, "null pointer given to destroy_at");
  std::__destroy(std::begin(*__loc), std::end(*__loc));
}
#endif

template <class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
__destroy(_ForwardIterator __first, _ForwardIterator __last) {
  for (; __first != __last; ++__first)
    std::__destroy_at(std::addressof(*__first));
  return __first;
}

template <class _BidirectionalIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _BidirectionalIterator
__reverse_destroy(_BidirectionalIterator __first, _BidirectionalIterator __last) {
  while (__last != __first) {
    --__last;
    std::__destroy_at(std::addressof(*__last));
  }
  return __last;
}

#if _LIBCPP_STD_VER >= 17

template <class _Tp, enable_if_t<!is_array_v<_Tp>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void destroy_at(_Tp* __loc) {
  std::__destroy_at(__loc);
}

#  if _LIBCPP_STD_VER >= 20
template <class _Tp, enable_if_t<is_array_v<_Tp>, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void destroy_at(_Tp* __loc) {
  std::__destroy_at(__loc);
}
#  endif

template <class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void destroy(_ForwardIterator __first, _ForwardIterator __last) {
  (void)std::__destroy(std::move(__first), std::move(__last));
}

template <class _ForwardIterator, class _Size>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator destroy_n(_ForwardIterator __first, _Size __n) {
  for (; __n > 0; (void)++__first, --__n)
    std::__destroy_at(std::addressof(*__first));
  return __first;
}

#endif // _LIBCPP_STD_VER >= 17

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_CONSTRUCT_AT_H
