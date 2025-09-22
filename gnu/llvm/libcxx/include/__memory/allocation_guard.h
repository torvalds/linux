// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_ALLOCATION_GUARD_H
#define _LIBCPP___MEMORY_ALLOCATION_GUARD_H

#include <__config>
#include <__memory/addressof.h>
#include <__memory/allocator_traits.h>
#include <__utility/move.h>
#include <cstddef>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// Helper class to allocate memory using an Allocator in an exception safe
// manner.
//
// The intended usage of this class is as follows:
//
// 0
// 1     __allocation_guard<SomeAllocator> guard(alloc, 10);
// 2     do_some_initialization_that_may_throw(guard.__get());
// 3     save_allocated_pointer_in_a_noexcept_operation(guard.__release_ptr());
// 4
//
// If line (2) throws an exception during initialization of the memory, the
// guard's destructor will be called, and the memory will be released using
// Allocator deallocation. Otherwise, we release the memory from the guard on
// line (3) in an operation that can't throw -- after that, the guard is not
// responsible for the memory anymore.
//
// This is similar to a unique_ptr, except it's easier to use with a
// custom allocator.
template <class _Alloc>
struct __allocation_guard {
  using _Pointer = typename allocator_traits<_Alloc>::pointer;
  using _Size    = typename allocator_traits<_Alloc>::size_type;

  template <class _AllocT> // we perform the allocator conversion inside the constructor
  _LIBCPP_HIDE_FROM_ABI explicit __allocation_guard(_AllocT __alloc, _Size __n)
      : __alloc_(std::move(__alloc)),
        __n_(__n),
        __ptr_(allocator_traits<_Alloc>::allocate(__alloc_, __n_)) // initialization order is important
  {}

  _LIBCPP_HIDE_FROM_ABI ~__allocation_guard() _NOEXCEPT { __destroy(); }

  _LIBCPP_HIDE_FROM_ABI __allocation_guard(const __allocation_guard&) = delete;
  _LIBCPP_HIDE_FROM_ABI __allocation_guard(__allocation_guard&& __other) _NOEXCEPT
      : __alloc_(std::move(__other.__alloc_)),
        __n_(__other.__n_),
        __ptr_(__other.__ptr_) {
    __other.__ptr_ = nullptr;
  }

  _LIBCPP_HIDE_FROM_ABI __allocation_guard& operator=(const __allocation_guard& __other) = delete;
  _LIBCPP_HIDE_FROM_ABI __allocation_guard& operator=(__allocation_guard&& __other) _NOEXCEPT {
    if (std::addressof(__other) != this) {
      __destroy();

      __alloc_       = std::move(__other.__alloc_);
      __n_           = __other.__n_;
      __ptr_         = __other.__ptr_;
      __other.__ptr_ = nullptr;
    }

    return *this;
  }

  _LIBCPP_HIDE_FROM_ABI _Pointer
  __release_ptr() _NOEXCEPT { // not called __release() because it's a keyword in objective-c++
    _Pointer __tmp = __ptr_;
    __ptr_         = nullptr;
    return __tmp;
  }

  _LIBCPP_HIDE_FROM_ABI _Pointer __get() const _NOEXCEPT { return __ptr_; }

private:
  _LIBCPP_HIDE_FROM_ABI void __destroy() _NOEXCEPT {
    if (__ptr_ != nullptr) {
      allocator_traits<_Alloc>::deallocate(__alloc_, __ptr_, __n_);
    }
  }

  _Alloc __alloc_;
  _Size __n_;
  _Pointer __ptr_;
};

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___MEMORY_ALLOCATION_GUARD_H
