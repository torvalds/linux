// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL___SIMD_REFERENCE_H
#define _LIBCPP_EXPERIMENTAL___SIMD_REFERENCE_H

#include <__type_traits/is_assignable.h>
#include <__type_traits/is_same.h>
#include <__utility/forward.h>
#include <__utility/move.h>
#include <cstddef>
#include <experimental/__config>
#include <experimental/__simd/utility.h>

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL
inline namespace parallelism_v2 {
template <class _Tp, class _Storage, class _Vp>
class __simd_reference {
  template <class, class>
  friend class simd;
  template <class, class>
  friend class simd_mask;

  _Storage& __s_;
  size_t __idx_;

  _LIBCPP_HIDE_FROM_ABI __simd_reference(_Storage& __s, size_t __idx) : __s_(__s), __idx_(__idx) {}

  _LIBCPP_HIDE_FROM_ABI _Vp __get() const noexcept { return __s_.__get(__idx_); }

  _LIBCPP_HIDE_FROM_ABI void __set(_Vp __v) {
    if constexpr (is_same_v<_Vp, bool>)
      __s_.__set(__idx_, experimental::__set_all_bits<_Tp>(__v));
    else
      __s_.__set(__idx_, __v);
  }

public:
  using value_type = _Vp;

  __simd_reference()                        = delete;
  __simd_reference(const __simd_reference&) = delete;

  _LIBCPP_HIDE_FROM_ABI operator value_type() const noexcept { return __get(); }

  template <class _Up, enable_if_t<is_assignable_v<value_type&, _Up&&>, int> = 0>
  _LIBCPP_HIDE_FROM_ABI __simd_reference operator=(_Up&& __v) && noexcept {
    __set(static_cast<value_type>(std::forward<_Up>(__v)));
    return {__s_, __idx_};
  }

  // Note: This approach might not fully align with the specification,
  // which might be a wording defect. (https://wg21.link/N4808 section 9.6.3)
  template <class _Tp1, class _Storage1, class _Vp1>
  friend void
  swap(__simd_reference<_Tp1, _Storage1, _Vp1>&& __a, __simd_reference<_Tp1, _Storage1, _Vp1>&& __b) noexcept;

  template <class _Tp1, class _Storage1, class _Vp1>
  friend void swap(_Vp1& __a, __simd_reference<_Tp1, _Storage1, _Vp1>&& __b) noexcept;

  template <class _Tp1, class _Storage1, class _Vp1>
  friend void swap(__simd_reference<_Tp1, _Storage1, _Vp1>&& __a, _Vp1& __b) noexcept;
};

template <class _Tp, class _Storage, class _Vp>
_LIBCPP_HIDE_FROM_ABI void
swap(__simd_reference<_Tp, _Storage, _Vp>&& __a, __simd_reference<_Tp, _Storage, _Vp>&& __b) noexcept {
  _Vp __tmp(std::move(__a));
  std::move(__a) = std::move(__b);
  std::move(__b) = std::move(__tmp);
}

template <class _Tp, class _Storage, class _Vp>
_LIBCPP_HIDE_FROM_ABI void swap(_Vp& __a, __simd_reference<_Tp, _Storage, _Vp>&& __b) noexcept {
  _Vp __tmp(std::move(__a));
  __a            = std::move(__b);
  std::move(__b) = std::move(__tmp);
}

template <class _Tp, class _Storage, class _Vp>
_LIBCPP_HIDE_FROM_ABI void swap(__simd_reference<_Tp, _Storage, _Vp>&& __a, _Vp& __b) noexcept {
  _Vp __tmp(std::move(__a));
  std::move(__a) = std::move(__b);
  __b            = std::move(__tmp);
}

} // namespace parallelism_v2
_LIBCPP_END_NAMESPACE_EXPERIMENTAL

#endif // _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_POP_MACROS

#endif // _LIBCPP_EXPERIMENTAL___SIMD_REFERENCE_H
