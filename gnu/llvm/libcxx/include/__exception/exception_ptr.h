//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___EXCEPTION_EXCEPTION_PTR_H
#define _LIBCPP___EXCEPTION_EXCEPTION_PTR_H

#include <__config>
#include <__exception/operations.h>
#include <__memory/addressof.h>
#include <__memory/construct_at.h>
#include <__type_traits/decay.h>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <typeinfo>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#ifndef _LIBCPP_ABI_MICROSOFT

#  if _LIBCPP_AVAILABILITY_HAS_INIT_PRIMARY_EXCEPTION

namespace __cxxabiv1 {

extern "C" {
_LIBCPP_OVERRIDABLE_FUNC_VIS void* __cxa_allocate_exception(size_t) throw();
_LIBCPP_OVERRIDABLE_FUNC_VIS void __cxa_free_exception(void*) throw();

struct __cxa_exception;
_LIBCPP_OVERRIDABLE_FUNC_VIS __cxa_exception* __cxa_init_primary_exception(
    void*,
    std::type_info*,
#    if defined(_WIN32)
    void(__thiscall*)(void*)) throw();
#    elif defined(__wasm__)
    // In Wasm, a destructor returns its argument
    void* (*)(void*)) throw();
#    else
    void (*)(void*)) throw();
#    endif
}

} // namespace __cxxabiv1

#  endif

#endif

namespace std { // purposefully not using versioning namespace

#ifndef _LIBCPP_ABI_MICROSOFT

class _LIBCPP_EXPORTED_FROM_ABI exception_ptr {
  void* __ptr_;

  static exception_ptr __from_native_exception_pointer(void*) _NOEXCEPT;

  template <class _Ep>
  friend _LIBCPP_HIDE_FROM_ABI exception_ptr make_exception_ptr(_Ep) _NOEXCEPT;

public:
  // exception_ptr is basically a COW string.
  using __trivially_relocatable = exception_ptr;

  _LIBCPP_HIDE_FROM_ABI exception_ptr() _NOEXCEPT : __ptr_() {}
  _LIBCPP_HIDE_FROM_ABI exception_ptr(nullptr_t) _NOEXCEPT : __ptr_() {}

  exception_ptr(const exception_ptr&) _NOEXCEPT;
  exception_ptr& operator=(const exception_ptr&) _NOEXCEPT;
  ~exception_ptr() _NOEXCEPT;

  _LIBCPP_HIDE_FROM_ABI explicit operator bool() const _NOEXCEPT { return __ptr_ != nullptr; }

  friend _LIBCPP_HIDE_FROM_ABI bool operator==(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT {
    return __x.__ptr_ == __y.__ptr_;
  }

  friend _LIBCPP_HIDE_FROM_ABI bool operator!=(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT {
    return !(__x == __y);
  }

  friend _LIBCPP_EXPORTED_FROM_ABI exception_ptr current_exception() _NOEXCEPT;
  friend _LIBCPP_EXPORTED_FROM_ABI void rethrow_exception(exception_ptr);
};

template <class _Ep>
_LIBCPP_HIDE_FROM_ABI exception_ptr make_exception_ptr(_Ep __e) _NOEXCEPT {
#  ifndef _LIBCPP_HAS_NO_EXCEPTIONS
#    if _LIBCPP_AVAILABILITY_HAS_INIT_PRIMARY_EXCEPTION && __cplusplus >= 201103L
  using _Ep2 = __decay_t<_Ep>;

  void* __ex = __cxxabiv1::__cxa_allocate_exception(sizeof(_Ep));
#      ifdef __wasm__
  // In Wasm, a destructor returns its argument
  (void)__cxxabiv1::__cxa_init_primary_exception(
      __ex, const_cast<std::type_info*>(&typeid(_Ep)), [](void* __p) -> void* {
#      else
  (void)__cxxabiv1::__cxa_init_primary_exception(__ex, const_cast<std::type_info*>(&typeid(_Ep)), [](void* __p) {
#      endif
        std::__destroy_at(static_cast<_Ep2*>(__p));
#      ifdef __wasm__
        return __p;
#      endif
      });

  try {
    ::new (__ex) _Ep2(__e);
    return exception_ptr::__from_native_exception_pointer(__ex);
  } catch (...) {
    __cxxabiv1::__cxa_free_exception(__ex);
    return current_exception();
  }
#    else
  try {
    throw __e;
  } catch (...) {
    return current_exception();
  }
#    endif
#  else
  ((void)__e);
  std::abort();
#  endif
}

#else // _LIBCPP_ABI_MICROSOFT

class _LIBCPP_EXPORTED_FROM_ABI exception_ptr {
  _LIBCPP_DIAGNOSTIC_PUSH
  _LIBCPP_CLANG_DIAGNOSTIC_IGNORED("-Wunused-private-field")
  void* __ptr1_;
  void* __ptr2_;
  _LIBCPP_DIAGNOSTIC_POP

public:
  exception_ptr() _NOEXCEPT;
  exception_ptr(nullptr_t) _NOEXCEPT;
  exception_ptr(const exception_ptr& __other) _NOEXCEPT;
  exception_ptr& operator=(const exception_ptr& __other) _NOEXCEPT;
  exception_ptr& operator=(nullptr_t) _NOEXCEPT;
  ~exception_ptr() _NOEXCEPT;
  explicit operator bool() const _NOEXCEPT;
};

_LIBCPP_EXPORTED_FROM_ABI bool operator==(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT;

inline _LIBCPP_HIDE_FROM_ABI bool operator!=(const exception_ptr& __x, const exception_ptr& __y) _NOEXCEPT {
  return !(__x == __y);
}

_LIBCPP_EXPORTED_FROM_ABI void swap(exception_ptr&, exception_ptr&) _NOEXCEPT;

_LIBCPP_EXPORTED_FROM_ABI exception_ptr __copy_exception_ptr(void* __except, const void* __ptr);
_LIBCPP_EXPORTED_FROM_ABI exception_ptr current_exception() _NOEXCEPT;
_LIBCPP_NORETURN _LIBCPP_EXPORTED_FROM_ABI void rethrow_exception(exception_ptr);

// This is a built-in template function which automagically extracts the required
// information.
template <class _E>
void* __GetExceptionInfo(_E);

template <class _Ep>
_LIBCPP_HIDE_FROM_ABI exception_ptr make_exception_ptr(_Ep __e) _NOEXCEPT {
  return __copy_exception_ptr(std::addressof(__e), __GetExceptionInfo(__e));
}

#endif // _LIBCPP_ABI_MICROSOFT
} // namespace std

#endif // _LIBCPP___EXCEPTION_EXCEPTION_PTR_H
