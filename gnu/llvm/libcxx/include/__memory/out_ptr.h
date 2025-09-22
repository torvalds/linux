// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___OUT_PTR_H
#define _LIBCPP___OUT_PTR_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory/pointer_traits.h>
#include <__memory/shared_ptr.h>
#include <__memory/unique_ptr.h>
#include <__type_traits/is_specialization.h>
#include <__type_traits/is_void.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <tuple>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 23

template <class _Smart, class _Pointer, class... _Args>
class _LIBCPP_TEMPLATE_VIS out_ptr_t {
  static_assert(!__is_specialization_v<_Smart, shared_ptr> || sizeof...(_Args) > 0,
                "Using std::shared_ptr<> without a deleter in std::out_ptr is not supported.");

public:
  _LIBCPP_HIDE_FROM_ABI explicit out_ptr_t(_Smart& __smart, _Args... __args)
      : __s_(__smart), __a_(std::forward<_Args>(__args)...), __p_() {
    using _Ptr = decltype(__smart);
    if constexpr (__resettable_smart_pointer<_Ptr>) {
      __s_.reset();
    } else if constexpr (is_constructible_v<_Smart>) {
      __s_ = _Smart();
    } else {
      static_assert(__resettable_smart_pointer<_Ptr> || is_constructible_v<_Smart>,
                    "The adapted pointer type must have a reset() member function or be default constructible.");
    }
  }

  _LIBCPP_HIDE_FROM_ABI out_ptr_t(const out_ptr_t&) = delete;

  _LIBCPP_HIDE_FROM_ABI ~out_ptr_t() {
    if (!__p_) {
      return;
    }

    using _SmartPtr = __pointer_of_or_t<_Smart, _Pointer>;
    if constexpr (__resettable_smart_pointer_with_args<_Smart, _Pointer, _Args...>) {
      std::apply([&](auto&&... __args) { __s_.reset(static_cast<_SmartPtr>(__p_), std::forward<_Args>(__args)...); },
                 std::move(__a_));
    } else {
      static_assert(is_constructible_v<_Smart, _SmartPtr, _Args...>,
                    "The smart pointer must be constructible from arguments of types _Smart, _Pointer, _Args...");
      std::apply([&](auto&&... __args) { __s_ = _Smart(static_cast<_SmartPtr>(__p_), std::forward<_Args>(__args)...); },
                 std::move(__a_));
    }
  }

  _LIBCPP_HIDE_FROM_ABI operator _Pointer*() const noexcept { return std::addressof(const_cast<_Pointer&>(__p_)); }

  _LIBCPP_HIDE_FROM_ABI operator void**() const noexcept
    requires(!is_same_v<_Pointer, void*>)
  {
    static_assert(is_pointer_v<_Pointer>, "The conversion to void** requires _Pointer to be a raw pointer.");

    return reinterpret_cast<void**>(static_cast<_Pointer*>(*this));
  }

private:
  _Smart& __s_;
  tuple<_Args...> __a_;
  _Pointer __p_ = _Pointer();
};

template <class _Pointer = void, class _Smart, class... _Args>
_LIBCPP_HIDE_FROM_ABI auto out_ptr(_Smart& __s, _Args&&... __args) {
  using _Ptr = conditional_t<is_void_v<_Pointer>, __pointer_of_t<_Smart>, _Pointer>;
  return std::out_ptr_t<_Smart, _Ptr, _Args&&...>(__s, std::forward<_Args>(__args)...);
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___OUT_PTR_H
