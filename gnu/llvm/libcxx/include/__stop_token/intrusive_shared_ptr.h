// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___STOP_TOKEN_INTRUSIVE_SHARED_PTR_H
#define _LIBCPP___STOP_TOKEN_INTRUSIVE_SHARED_PTR_H

#include <__atomic/atomic.h>
#include <__atomic/memory_order.h>
#include <__config>
#include <__type_traits/is_reference.h>
#include <__utility/move.h>
#include <__utility/swap.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

#if _LIBCPP_STD_VER >= 20

// For intrusive_shared_ptr to work with a type T, specialize __intrusive_shared_ptr_traits<T> and implement
// the following function:
//
// static std::atomic<U>& __get_atomic_ref_count(T&);
//
// where U must be an integral type representing the number of references to the object.
template <class _Tp>
struct __intrusive_shared_ptr_traits;

// A reference counting shared_ptr for types whose reference counter
// is stored inside the class _Tp itself.
// When the reference count goes to zero, the destructor of _Tp will be called
template <class _Tp>
struct __intrusive_shared_ptr {
  _LIBCPP_HIDE_FROM_ABI __intrusive_shared_ptr() = default;

  _LIBCPP_HIDE_FROM_ABI explicit __intrusive_shared_ptr(_Tp* __raw_ptr) : __raw_ptr_(__raw_ptr) {
    if (__raw_ptr_)
      __increment_ref_count(*__raw_ptr_);
  }

  _LIBCPP_HIDE_FROM_ABI __intrusive_shared_ptr(const __intrusive_shared_ptr& __other) noexcept
      : __raw_ptr_(__other.__raw_ptr_) {
    if (__raw_ptr_)
      __increment_ref_count(*__raw_ptr_);
  }

  _LIBCPP_HIDE_FROM_ABI __intrusive_shared_ptr(__intrusive_shared_ptr&& __other) noexcept
      : __raw_ptr_(__other.__raw_ptr_) {
    __other.__raw_ptr_ = nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI __intrusive_shared_ptr& operator=(const __intrusive_shared_ptr& __other) noexcept {
    if (__other.__raw_ptr_ != __raw_ptr_) {
      if (__other.__raw_ptr_) {
        __increment_ref_count(*__other.__raw_ptr_);
      }
      if (__raw_ptr_) {
        __decrement_ref_count(*__raw_ptr_);
      }
      __raw_ptr_ = __other.__raw_ptr_;
    }
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI __intrusive_shared_ptr& operator=(__intrusive_shared_ptr&& __other) noexcept {
    __intrusive_shared_ptr(std::move(__other)).swap(*this);
    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI ~__intrusive_shared_ptr() {
    if (__raw_ptr_) {
      __decrement_ref_count(*__raw_ptr_);
    }
  }

  _LIBCPP_HIDE_FROM_ABI _Tp* operator->() const noexcept { return __raw_ptr_; }
  _LIBCPP_HIDE_FROM_ABI _Tp& operator*() const noexcept { return *__raw_ptr_; }
  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const noexcept { return __raw_ptr_ != nullptr; }

  _LIBCPP_HIDE_FROM_ABI void swap(__intrusive_shared_ptr& __other) { std::swap(__raw_ptr_, __other.__raw_ptr_); }

  _LIBCPP_HIDE_FROM_ABI friend void swap(__intrusive_shared_ptr& __lhs, __intrusive_shared_ptr& __rhs) {
    __lhs.swap(__rhs);
  }

  _LIBCPP_HIDE_FROM_ABI friend bool constexpr
  operator==(const __intrusive_shared_ptr&, const __intrusive_shared_ptr&) = default;

  _LIBCPP_HIDE_FROM_ABI friend bool constexpr operator==(const __intrusive_shared_ptr& __ptr, std::nullptr_t) {
    return __ptr.__raw_ptr_ == nullptr;
  }

private:
  _Tp* __raw_ptr_ = nullptr;

  // the memory order for increment/decrement the counter is the same for shared_ptr
  // increment is relaxed and decrement is acq_rel
  _LIBCPP_HIDE_FROM_ABI static void __increment_ref_count(_Tp& __obj) {
    __get_atomic_ref_count(__obj).fetch_add(1, std::memory_order_relaxed);
  }

  _LIBCPP_HIDE_FROM_ABI static void __decrement_ref_count(_Tp& __obj) {
    if (__get_atomic_ref_count(__obj).fetch_sub(1, std::memory_order_acq_rel) == 1) {
      delete &__obj;
    }
  }

  _LIBCPP_HIDE_FROM_ABI static decltype(auto) __get_atomic_ref_count(_Tp& __obj) {
    using __ret_type = decltype(__intrusive_shared_ptr_traits<_Tp>::__get_atomic_ref_count(__obj));
    static_assert(
        std::is_reference_v<__ret_type>, "__get_atomic_ref_count should return a reference to the atomic counter");
    return __intrusive_shared_ptr_traits<_Tp>::__get_atomic_ref_count(__obj);
  }
};

#endif // _LIBCPP_STD_VER >= 20

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___STOP_TOKEN_INTRUSIVE_SHARED_PTR_H
