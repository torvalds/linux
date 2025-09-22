//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___MEMORY_BUILTIN_NEW_ALLOCATOR_H
#define _LIBCPP___MEMORY_BUILTIN_NEW_ALLOCATOR_H

#include <__config>
#include <__memory/unique_ptr.h>
#include <cstddef>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_BEGIN_NAMESPACE_STD

// __builtin_new_allocator -- A non-templated helper for allocating and
// deallocating memory using __builtin_operator_new and
// __builtin_operator_delete. It should be used in preference to
// `std::allocator<T>` to avoid additional instantiations.
struct __builtin_new_allocator {
  struct __builtin_new_deleter {
    typedef void* pointer_type;

    _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR explicit __builtin_new_deleter(size_t __size, size_t __align)
        : __size_(__size), __align_(__align) {}

    _LIBCPP_HIDE_FROM_ABI void operator()(void* __p) const _NOEXCEPT {
      std::__libcpp_deallocate(__p, __size_, __align_);
    }

  private:
    size_t __size_;
    size_t __align_;
  };

  typedef unique_ptr<void, __builtin_new_deleter> __holder_t;

  _LIBCPP_HIDE_FROM_ABI static __holder_t __allocate_bytes(size_t __s, size_t __align) {
    return __holder_t(std::__libcpp_allocate(__s, __align), __builtin_new_deleter(__s, __align));
  }

  _LIBCPP_HIDE_FROM_ABI static void __deallocate_bytes(void* __p, size_t __s, size_t __align) _NOEXCEPT {
    std::__libcpp_deallocate(__p, __s, __align);
  }

  template <class _Tp>
  _LIBCPP_NODEBUG _LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI static __holder_t __allocate_type(size_t __n) {
    return __allocate_bytes(__n * sizeof(_Tp), _LIBCPP_ALIGNOF(_Tp));
  }

  template <class _Tp>
  _LIBCPP_NODEBUG _LIBCPP_ALWAYS_INLINE _LIBCPP_HIDE_FROM_ABI static void
  __deallocate_type(void* __p, size_t __n) _NOEXCEPT {
    __deallocate_bytes(__p, __n * sizeof(_Tp), _LIBCPP_ALIGNOF(_Tp));
  }
};

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP___MEMORY_BUILTIN_NEW_ALLOCATOR_H
