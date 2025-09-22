//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MUTEX_ONCE_FLAG_H
#define _LIBCPP___MUTEX_ONCE_FLAG_H

#include <__config>
#include <__functional/invoke.h>
#include <__memory/shared_ptr.h> // __libcpp_acquire_load
#include <__tuple/tuple_indices.h>
#include <__tuple/tuple_size.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <cstdint>
#ifndef _LIBCPP_CXX03_LANG
#  include <tuple>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

struct _LIBCPP_TEMPLATE_VIS once_flag;

#ifndef _LIBCPP_CXX03_LANG

template <class _Callable, class... _Args>
_LIBCPP_HIDE_FROM_ABI void call_once(once_flag&, _Callable&&, _Args&&...);

#else // _LIBCPP_CXX03_LANG

template <class _Callable>
_LIBCPP_HIDE_FROM_ABI void call_once(once_flag&, _Callable&);

template <class _Callable>
_LIBCPP_HIDE_FROM_ABI void call_once(once_flag&, const _Callable&);

#endif // _LIBCPP_CXX03_LANG

struct _LIBCPP_TEMPLATE_VIS once_flag {
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR once_flag() _NOEXCEPT : __state_(_Unset) {}
  once_flag(const once_flag&)            = delete;
  once_flag& operator=(const once_flag&) = delete;

#if defined(_LIBCPP_ABI_MICROSOFT)
  typedef uintptr_t _State_type;
#else
  typedef unsigned long _State_type;
#endif

  static const _State_type _Unset    = 0;
  static const _State_type _Pending  = 1;
  static const _State_type _Complete = ~_State_type(0);

private:
  _State_type __state_;

#ifndef _LIBCPP_CXX03_LANG
  template <class _Callable, class... _Args>
  friend void call_once(once_flag&, _Callable&&, _Args&&...);
#else  // _LIBCPP_CXX03_LANG
  template <class _Callable>
  friend void call_once(once_flag&, _Callable&);

  template <class _Callable>
  friend void call_once(once_flag&, const _Callable&);
#endif // _LIBCPP_CXX03_LANG
};

#ifndef _LIBCPP_CXX03_LANG

template <class _Fp>
class __call_once_param {
  _Fp& __f_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit __call_once_param(_Fp& __f) : __f_(__f) {}

  _LIBCPP_HIDE_FROM_ABI void operator()() {
    typedef typename __make_tuple_indices<tuple_size<_Fp>::value, 1>::type _Index;
    __execute(_Index());
  }

private:
  template <size_t... _Indices>
  _LIBCPP_HIDE_FROM_ABI void __execute(__tuple_indices<_Indices...>) {
    std::__invoke(std::get<0>(std::move(__f_)), std::get<_Indices>(std::move(__f_))...);
  }
};

#else

template <class _Fp>
class __call_once_param {
  _Fp& __f_;

public:
  _LIBCPP_HIDE_FROM_ABI explicit __call_once_param(_Fp& __f) : __f_(__f) {}

  _LIBCPP_HIDE_FROM_ABI void operator()() { __f_(); }
};

#endif

template <class _Fp>
void _LIBCPP_HIDE_FROM_ABI __call_once_proxy(void* __vp) {
  __call_once_param<_Fp>* __p = static_cast<__call_once_param<_Fp>*>(__vp);
  (*__p)();
}

_LIBCPP_EXPORTED_FROM_ABI void __call_once(volatile once_flag::_State_type&, void*, void (*)(void*));

#ifndef _LIBCPP_CXX03_LANG

template <class _Callable, class... _Args>
inline _LIBCPP_HIDE_FROM_ABI void call_once(once_flag& __flag, _Callable&& __func, _Args&&... __args) {
  if (__libcpp_acquire_load(&__flag.__state_) != once_flag::_Complete) {
    typedef tuple<_Callable&&, _Args&&...> _Gp;
    _Gp __f(std::forward<_Callable>(__func), std::forward<_Args>(__args)...);
    __call_once_param<_Gp> __p(__f);
    std::__call_once(__flag.__state_, &__p, &__call_once_proxy<_Gp>);
  }
}

#else // _LIBCPP_CXX03_LANG

template <class _Callable>
inline _LIBCPP_HIDE_FROM_ABI void call_once(once_flag& __flag, _Callable& __func) {
  if (__libcpp_acquire_load(&__flag.__state_) != once_flag::_Complete) {
    __call_once_param<_Callable> __p(__func);
    std::__call_once(__flag.__state_, &__p, &__call_once_proxy<_Callable>);
  }
}

template <class _Callable>
inline _LIBCPP_HIDE_FROM_ABI void call_once(once_flag& __flag, const _Callable& __func) {
  if (__libcpp_acquire_load(&__flag.__state_) != once_flag::_Complete) {
    __call_once_param<const _Callable> __p(__func);
    std::__call_once(__flag.__state_, &__p, &__call_once_proxy<const _Callable>);
  }
}

#endif // _LIBCPP_CXX03_LANG

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MUTEX_ONCE_FLAG_H
