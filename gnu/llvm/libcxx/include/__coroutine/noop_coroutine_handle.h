//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___COROUTINE_NOOP_COROUTINE_HANDLE_H
#define _LIBCPP___COROUTINE_NOOP_COROUTINE_HANDLE_H

#include <__config>
#include <__coroutine/coroutine_handle.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

#if _LIBCPP_STD_VER >= 20

_LIBCPP_BEGIN_NAMESPACE_STD

#  if __has_builtin(__builtin_coro_noop) || defined(_LIBCPP_COMPILER_GCC)

// [coroutine.noop]
// [coroutine.promise.noop]
struct noop_coroutine_promise {};

// [coroutine.handle.noop]
template <>
struct _LIBCPP_TEMPLATE_VIS coroutine_handle<noop_coroutine_promise> {
public:
  // [coroutine.handle.noop.conv], conversion
  _LIBCPP_HIDE_FROM_ABI constexpr operator coroutine_handle<>() const noexcept {
    return coroutine_handle<>::from_address(address());
  }

  // [coroutine.handle.noop.observers], observers
  _LIBCPP_HIDE_FROM_ABI constexpr explicit operator bool() const noexcept { return true; }
  _LIBCPP_HIDE_FROM_ABI constexpr bool done() const noexcept { return false; }

  // [coroutine.handle.noop.resumption], resumption
  _LIBCPP_HIDE_FROM_ABI constexpr void operator()() const noexcept {}
  _LIBCPP_HIDE_FROM_ABI constexpr void resume() const noexcept {}
  _LIBCPP_HIDE_FROM_ABI constexpr void destroy() const noexcept {}

  // [coroutine.handle.noop.promise], promise access
  _LIBCPP_HIDE_FROM_ABI noop_coroutine_promise& promise() const noexcept {
    return *static_cast<noop_coroutine_promise*>(
        __builtin_coro_promise(this->__handle_, alignof(noop_coroutine_promise), false));
  }

  // [coroutine.handle.noop.address], address
  _LIBCPP_HIDE_FROM_ABI constexpr void* address() const noexcept { return __handle_; }

private:
  _LIBCPP_HIDE_FROM_ABI friend coroutine_handle<noop_coroutine_promise> noop_coroutine() noexcept;

#    if __has_builtin(__builtin_coro_noop)
  _LIBCPP_HIDE_FROM_ABI coroutine_handle() noexcept { this->__handle_ = __builtin_coro_noop(); }

  void* __handle_ = nullptr;

#    elif defined(_LIBCPP_COMPILER_GCC)
  // GCC doesn't implement __builtin_coro_noop().
  // Construct the coroutine frame manually instead.
  struct __noop_coroutine_frame_ty_ {
    static void __dummy_resume_destroy_func() {}

    void (*__resume_)()  = __dummy_resume_destroy_func;
    void (*__destroy_)() = __dummy_resume_destroy_func;
    struct noop_coroutine_promise __promise_;
  };

  static __noop_coroutine_frame_ty_ __noop_coroutine_frame_;

  void* __handle_ = &__noop_coroutine_frame_;

  _LIBCPP_HIDE_FROM_ABI coroutine_handle() noexcept = default;

#    endif // __has_builtin(__builtin_coro_noop)
};

using noop_coroutine_handle = coroutine_handle<noop_coroutine_promise>;

#    if defined(_LIBCPP_COMPILER_GCC)
inline noop_coroutine_handle::__noop_coroutine_frame_ty_ noop_coroutine_handle::__noop_coroutine_frame_{};
#    endif

// [coroutine.noop.coroutine]
inline _LIBCPP_HIDE_FROM_ABI noop_coroutine_handle noop_coroutine() noexcept { return noop_coroutine_handle(); }

#  endif // __has_builtin(__builtin_coro_noop) || defined(_LIBCPP_COMPILER_GCC)

_LIBCPP_END_NAMESPACE_STD

#endif // __LIBCPP_STD_VER >= 20

#endif // _LIBCPP___COROUTINE_NOOP_COROUTINE_HANDLE_H
