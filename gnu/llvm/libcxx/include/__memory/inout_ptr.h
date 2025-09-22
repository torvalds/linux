// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___INOUT_PTR_H
#define _LIBCPP___INOUT_PTR_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory/pointer_traits.h>
#include <__memory/shared_ptr.h>
#include <__memory/unique_ptr.h>
#include <__type_traits/is_same.h>
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
class _LIBCPP_TEMPLATE_VIS inout_ptr_t {
  static_assert(!__is_specialization_v<_Smart, shared_ptr>, "std::shared_ptr<> is not supported with std::inout_ptr.");

public:
  _LIBCPP_HIDE_FROM_ABI explicit inout_ptr_t(_Smart& __smart, _Args... __args)
      : __s_(__smart), __a_(std::forward<_Args>(__args)...), __p_([&__smart] {
          if constexpr (is_pointer_v<_Smart>) {
            return __smart;
          } else {
            return __smart.get();
          }
        }()) {
    if constexpr (requires { __s_.release(); }) {
      __s_.release();
    } else {
      __s_ = _Smart();
    }
  }

  _LIBCPP_HIDE_FROM_ABI inout_ptr_t(const inout_ptr_t&) = delete;

  _LIBCPP_HIDE_FROM_ABI ~inout_ptr_t() {
    // LWG-3897 inout_ptr will not update raw pointer to null
    if constexpr (!is_pointer_v<_Smart>) {
      if (!__p_) {
        return;
      }
    }

    using _SmartPtr = __pointer_of_or_t<_Smart, _Pointer>;
    if constexpr (is_pointer_v<_Smart>) {
      std::apply([&](auto&&... __args) { __s_ = _Smart(static_cast<_SmartPtr>(__p_), std::forward<_Args>(__args)...); },
                 std::move(__a_));
    } else if constexpr (__resettable_smart_pointer_with_args<_Smart, _Pointer, _Args...>) {
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
  _Pointer __p_;
};

template <class _Pointer = void, class _Smart, class... _Args>
_LIBCPP_HIDE_FROM_ABI auto inout_ptr(_Smart& __s, _Args&&... __args) {
  using _Ptr = conditional_t<is_void_v<_Pointer>, __pointer_of_t<_Smart>, _Pointer>;
  return std::inout_ptr_t<_Smart, _Ptr, _Args&&...>(__s, std::forward<_Args>(__args)...);
}

#endif // _LIBCPP_STD_VER >= 23

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___INOUT_PTR_H
