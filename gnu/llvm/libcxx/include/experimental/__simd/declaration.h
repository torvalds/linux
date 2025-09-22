// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP_EXPERIMENTAL___SIMD_DECLARATION_H
#define _LIBCPP_EXPERIMENTAL___SIMD_DECLARATION_H

#include <cstddef>
#include <experimental/__config>

#if _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)

_LIBCPP_BEGIN_NAMESPACE_EXPERIMENTAL
inline namespace parallelism_v2 {
namespace simd_abi {
template <int>
struct __vec_ext;
struct __scalar;

using scalar = __scalar;

// TODO: make this platform dependent
template <int _Np>
using fixed_size = __vec_ext<_Np>;

template <class _Tp>
inline constexpr int max_fixed_size = 32;

// TODO: make this platform dependent
template <class _Tp>
using compatible = __vec_ext<16 / sizeof(_Tp)>;

// TODO: make this platform dependent
template <class _Tp>
using native = __vec_ext<_LIBCPP_NATIVE_SIMD_WIDTH_IN_BYTES / sizeof(_Tp)>;

// TODO: make this platform dependent
template <class _Tp, size_t _Np, class... _Abis>
struct deduce {
  using type = fixed_size<_Np>;
};

// TODO: make this platform dependent
template <class _Tp, size_t _Np, class... _Abis>
using deduce_t = typename deduce<_Tp, _Np, _Abis...>::type;

} // namespace simd_abi

template <class _Tp, class _Abi>
struct __simd_storage;

template <class _Tp, class _Abi>
struct __mask_storage;

template <class _Tp, class _Abi>
struct __simd_operations;

template <class _Tp, class _Abi>
struct __mask_operations;

struct element_aligned_tag;
struct vector_aligned_tag;
template <size_t>
struct overaligned_tag;

template <class _Tp, class _Abi = simd_abi::compatible<_Tp>>
class simd;

template <class _Tp, class _Abi = simd_abi::compatible<_Tp>>
class simd_mask;

} // namespace parallelism_v2
_LIBCPP_END_NAMESPACE_EXPERIMENTAL

#endif // _LIBCPP_STD_VER >= 17 && defined(_LIBCPP_ENABLE_EXPERIMENTAL)
#endif // _LIBCPP_EXPERIMENTAL___SIMD_DECLARATION_H
