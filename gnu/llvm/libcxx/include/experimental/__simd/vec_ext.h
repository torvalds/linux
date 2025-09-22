// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL___SIMD_VEC_EXT_H
#define _LIBCPP_EXPERIMENTAL___SIMD_VEC_EXT_H

#include <__assert>
#include <__bit/bit_ceil.h>
#include <__utility/forward.h>
#include <__utility/integer_sequence.h>
#include <cstddef>
#include <experimental/__config>
#include <experimental/__simd/declaration.h>
#include <experimental/__simd/traits.h>
#include <experimental/__simd/utility.h>

#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL
inline namespace parallelism_v2 {
namespace simd_abi {
template <int _Np>
struct __vec_ext {
  static constexpr size_t __simd_size = _Np;
};
} // namespace simd_abi

template <int _Np>
inline constexpr bool is_abi_tag_v<simd_abi::__vec_ext<_Np>> = _Np > 0 && _Np <= 32;

template <class _Tp, int _Np>
struct __simd_storage<_Tp, simd_abi::__vec_ext<_Np>> {
  _Tp __data __attribute__((__vector_size__(std::__bit_ceil((sizeof(_Tp) * _Np)))));

  _LIBCPP_HIDE_FROM_ABI _Tp __get(size_t __idx) const noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__idx >= 0 && __idx < _Np, "Index is out of bounds");
    return __data[__idx];
  }
  _LIBCPP_HIDE_FROM_ABI void __set(size_t __idx, _Tp __v) noexcept {
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__idx >= 0 && __idx < _Np, "Index is out of bounds");
    __data[__idx] = __v;
  }
};

template <class _Tp, int _Np>
struct __mask_storage<_Tp, simd_abi::__vec_ext<_Np>>
    : __simd_storage<decltype(experimental::__choose_mask_type<_Tp>()), simd_abi::__vec_ext<_Np>> {};

template <class _Tp, int _Np>
struct __simd_operations<_Tp, simd_abi::__vec_ext<_Np>> {
  using _SimdStorage = __simd_storage<_Tp, simd_abi::__vec_ext<_Np>>;
  using _MaskStorage = __mask_storage<_Tp, simd_abi::__vec_ext<_Np>>;

  static _LIBCPP_HIDE_FROM_ABI _SimdStorage __broadcast(_Tp __v) noexcept {
    _SimdStorage __result;
    for (int __i = 0; __i < _Np; ++__i) {
      __result.__set(__i, __v);
    }
    return __result;
  }

  template <class _Generator, size_t... _Is>
  static _LIBCPP_HIDE_FROM_ABI _SimdStorage __generate_init(_Generator&& __g, std::index_sequence<_Is...>) {
    return _SimdStorage{{__g(std::integral_constant<size_t, _Is>())...}};
  }

  template <class _Generator>
  static _LIBCPP_HIDE_FROM_ABI _SimdStorage __generate(_Generator&& __g) noexcept {
    return __generate_init(std::forward<_Generator>(__g), std::make_index_sequence<_Np>());
  }

  template <class _Up>
  static _LIBCPP_HIDE_FROM_ABI void __load(_SimdStorage& __s, const _Up* __mem) noexcept {
    for (size_t __i = 0; __i < _Np; __i++)
      __s.__data[__i] = static_cast<_Tp>(__mem[__i]);
  }

  template <class _Up>
  static _LIBCPP_HIDE_FROM_ABI void __store(_SimdStorage __s, _Up* __mem) noexcept {
    for (size_t __i = 0; __i < _Np; __i++)
      __mem[__i] = static_cast<_Up>(__s.__data[__i]);
  }
};

template <class _Tp, int _Np>
struct __mask_operations<_Tp, simd_abi::__vec_ext<_Np>> {
  using _MaskStorage = __mask_storage<_Tp, simd_abi::__vec_ext<_Np>>;

  static _LIBCPP_HIDE_FROM_ABI _MaskStorage __broadcast(bool __v) noexcept {
    _MaskStorage __result;
    auto __all_bits_v = experimental::__set_all_bits<_Tp>(__v);
    for (int __i = 0; __i < _Np; ++__i) {
      __result.__set(__i, __all_bits_v);
    }
    return __result;
  }

  static _LIBCPP_HIDE_FROM_ABI void __load(_MaskStorage& __s, const bool* __mem) noexcept {
    for (size_t __i = 0; __i < _Np; __i++)
      __s.__data[__i] = experimental::__set_all_bits<_Tp>(__mem[__i]);
  }

  static _LIBCPP_HIDE_FROM_ABI void __store(_MaskStorage __s, bool* __mem) noexcept {
    for (size_t __i = 0; __i < _Np; __i++)
      __mem[__i] = static_cast<bool>(__s.__data[__i]);
  }
};

} // namespace parallelism_v2
_LIBCPP_END_NAMESPACE_EXPERIMENTAL

#endif // _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)
#endif // _LIBCPP_EXPERIMENTAL___SIMD_VEC_EXT_H
