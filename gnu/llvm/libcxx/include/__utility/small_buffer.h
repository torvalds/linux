//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___UTILITY_SMALL_BUFFER_H
#define _LIBCPP___UTILITY_SMALL_BUFFER_H

#include <__config>
#include <__memory/construct_at.h>
#include <__type_traits/decay.h>
#include <__type_traits/is_trivially_constructible.h>
#include <__type_traits/is_trivially_destructible.h>
#include <__utility/exception_guard.h>
#include <__utility/forward.h>
#include <cstddef>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 23

// __small_buffer is a helper class to perform the well known SBO (small buffer optimization). It is mainly useful to
// allow type-erasing classes like move_only_function to store small objects in a local buffer without requiring an
// allocation.
//
// This small buffer class only allows storing trivially relocatable objects inside the local storage to allow
// __small_buffer to be trivially relocatable itself. Since the buffer doesn't know what's stored inside it, the user
// has to manage the object's lifetime, in particular the destruction of the object.

_LIBCPP_BEGIN_NAMESPACE_STD

template <size_t _BufferSize, size_t _BufferAlignment>
  requires(_BufferSize > 0 && _BufferAlignment > 0)
class __small_buffer {
public:
  template <class _Tp, class _Decayed = decay_t<_Tp>>
  static constexpr bool __fits_in_buffer =
      is_trivially_move_constructible_v<_Decayed> && is_trivially_destructible_v<_Decayed> &&
      sizeof(_Decayed) <= _BufferSize && alignof(_Decayed) <= _BufferAlignment;

  _LIBCPP_HIDE_FROM_ABI __small_buffer()           = default;
  __small_buffer(const __small_buffer&)            = delete;
  __small_buffer& operator=(const __small_buffer&) = delete;
  _LIBCPP_HIDE_FROM_ABI ~__small_buffer()          = default;

  // Relocates the buffer - __delete() should never be called on a moved-from __small_buffer
  _LIBCPP_HIDE_FROM_ABI __small_buffer(__small_buffer&&)            = default;
  _LIBCPP_HIDE_FROM_ABI __small_buffer& operator=(__small_buffer&&) = default;

  template <class _Stored>
  _LIBCPP_HIDE_FROM_ABI _Stored* __get() {
    if constexpr (__fits_in_buffer<_Stored>)
      return std::launder(reinterpret_cast<_Stored*>(__buffer_));
    else
      return *std::launder(reinterpret_cast<_Stored**>(__buffer_));
  }

  template <class _Stored>
  _LIBCPP_HIDE_FROM_ABI _Stored* __alloc() {
    if constexpr (__fits_in_buffer<_Stored>) {
      return std::launder(reinterpret_cast<_Stored*>(__buffer_));
    } else {
      byte* __allocation = static_cast<byte*>(::operator new[](sizeof(_Stored), align_val_t{alignof(_Stored)}));
      std::construct_at(reinterpret_cast<byte**>(__buffer_), __allocation);
      return std::launder(reinterpret_cast<_Stored*>(__allocation));
    }
  }

  template <class _Stored>
  _LIBCPP_HIDE_FROM_ABI void __dealloc() noexcept {
    if constexpr (!__fits_in_buffer<_Stored>)
      ::operator delete[](*reinterpret_cast<void**>(__buffer_), sizeof(_Stored), align_val_t{alignof(_Stored)});
  }

  template <class _Stored, class... _Args>
  _LIBCPP_HIDE_FROM_ABI void __construct(_Args&&... __args) {
    _Stored* __buffer = __alloc<_Stored>();
    auto __guard      = std::__make_exception_guard([&] { __dealloc<_Stored>(); });
    std::construct_at(__buffer, std::forward<_Args>(__args)...);
    __guard.__complete();
  }

private:
  alignas(_BufferAlignment) byte __buffer_[_BufferSize];
};

#  undef _LIBCPP_SMALL_BUFFER_TRIVIAL_ABI

_LIBCPP_END_NAMESPACE_STD

#endif // _LIBCPP_STD_VER >= 23

#endif // _LIBCPP___UTILITY_SMALL_BUFFER_H
