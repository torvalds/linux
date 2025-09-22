// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL___SIMD_SIMD_H
#define _LIBCPP_EXPERIMENTAL___SIMD_SIMD_H

#include <__type_traits/is_same.h>
#include <__type_traits/remove_cvref.h>
#include <__utility/forward.h>
#include <cstddef>
#include <experimental/__config>
#include <experimental/__simd/declaration.h>
#include <experimental/__simd/reference.h>
#include <experimental/__simd/traits.h>
#include <experimental/__simd/utility.h>

#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL
inline namespace parallelism_v2 {

// class template simd [simd.class]
// TODO: implement simd class
template <class _Tp, class _Abi>
class simd {
  using _Impl    = __simd_operations<_Tp, _Abi>;
  using _Storage = typename _Impl::_SimdStorage;

  _Storage __s_;

public:
  using value_type = _Tp;
  using reference  = __simd_reference<_Tp, _Storage, value_type>;
  using mask_type  = simd_mask<_Tp, _Abi>;
  using abi_type   = _Abi;

  static _LIBCPP_HIDE_FROM_ABI constexpr size_t size() noexcept { return simd_size_v<value_type, abi_type>; }

  _LIBCPP_HIDE_FROM_ABI simd() noexcept = default;

  // broadcast constructor
  template <class _Up, enable_if_t<__can_broadcast_v<value_type, __remove_cvref_t<_Up>>, int> = 0>
  _LIBCPP_HIDE_FROM_ABI simd(_Up&& __v) noexcept : __s_(_Impl::__broadcast(static_cast<value_type>(__v))) {}

  // implicit type conversion constructor
  template <class _Up,
            enable_if_t<!is_same_v<_Up, _Tp> && is_same_v<abi_type, simd_abi::fixed_size<size()>> &&
                            __is_non_narrowing_convertible_v<_Up, value_type>,
                        int> = 0>
  _LIBCPP_HIDE_FROM_ABI simd(const simd<_Up, simd_abi::fixed_size<size()>>& __v) noexcept {
    for (size_t __i = 0; __i < size(); __i++) {
      (*this)[__i] = static_cast<value_type>(__v[__i]);
    }
  }

  // generator constructor
  template <class _Generator, enable_if_t<__can_generate_v<value_type, _Generator, size()>, int> = 0>
  explicit _LIBCPP_HIDE_FROM_ABI simd(_Generator&& __g) noexcept
      : __s_(_Impl::__generate(std::forward<_Generator>(__g))) {}

  // load constructor
  template <class _Up, class _Flags, enable_if_t<__is_vectorizable_v<_Up> && is_simd_flag_type_v<_Flags>, int> = 0>
  _LIBCPP_HIDE_FROM_ABI simd(const _Up* __mem, _Flags) {
    _Impl::__load(__s_, _Flags::template __apply<simd>(__mem));
  }

  // copy functions
  template <class _Up, class _Flags, enable_if_t<__is_vectorizable_v<_Up> && is_simd_flag_type_v<_Flags>, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void copy_from(const _Up* __mem, _Flags) {
    _Impl::__load(__s_, _Flags::template __apply<simd>(__mem));
  }

  template <class _Up, class _Flags, enable_if_t<__is_vectorizable_v<_Up> && is_simd_flag_type_v<_Flags>, int> = 0>
  _LIBCPP_HIDE_FROM_ABI void copy_to(_Up* __mem, _Flags) const {
    _Impl::__store(__s_, _Flags::template __apply<simd>(__mem));
  }

  // scalar access [simd.subscr]
  _LIBCPP_HIDE_FROM_ABI reference operator[](size_t __i) noexcept { return reference(__s_, __i); }
  _LIBCPP_HIDE_FROM_ABI value_type operator[](size_t __i) const noexcept { return __s_.__get(__i); }
};

template <class _Tp, class _Abi>
inline constexpr bool is_simd_v<simd<_Tp, _Abi>> = true;

template <class _Tp>
using native_simd = simd<_Tp, simd_abi::native<_Tp>>;

template <class _Tp, int _Np>
using fixed_size_simd = simd<_Tp, simd_abi::fixed_size<_Np>>;

} // namespace parallelism_v2
_LIBCPP_END_NAMESPACE_EXPERIMENTAL

#endif // _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)
#endif // _LIBCPP_EXPERIMENTAL___SIMD_SIMD_H
