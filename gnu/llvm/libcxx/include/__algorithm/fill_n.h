//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FILL_N_H
#define _LIBCPP___ALGORITHM_FILL_N_H

#include <__algorithm/min.h>
#include <__config>
#include <__fwd/bit_reference.h>
#include <__iterator/iterator_traits.h>
#include <__memory/pointer_traits.h>
#include <__utility/convert_to_integral.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// fill_n isn't specialized for std::memset, because the compiler already optimizes the loop to a call to std::memset.

template <class _OutputIterator, class _Size, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
__fill_n(_OutputIterator __first, _Size __n, const _Tp& __value);

template <bool _FillVal, class _Cp>
_LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_HIDE_FROM_ABI void
__fill_n_bool(__bit_iterator<_Cp, false> __first, typename _Cp::size_type __n) {
  using _It            = __bit_iterator<_Cp, false>;
  using __storage_type = typename _It::__storage_type;

  const int __bits_per_word = _It::__bits_per_word;
  // do first partial word
  if (__first.__ctz_ != 0) {
    __storage_type __clz_f = static_cast<__storage_type>(__bits_per_word - __first.__ctz_);
    __storage_type __dn    = std::min(__clz_f, __n);
    __storage_type __m     = (~__storage_type(0) << __first.__ctz_) & (~__storage_type(0) >> (__clz_f - __dn));
    if (_FillVal)
      *__first.__seg_ |= __m;
    else
      *__first.__seg_ &= ~__m;
    __n -= __dn;
    ++__first.__seg_;
  }
  // do middle whole words
  __storage_type __nw = __n / __bits_per_word;
  std::__fill_n(std::__to_address(__first.__seg_), __nw, _FillVal ? static_cast<__storage_type>(-1) : 0);
  __n -= __nw * __bits_per_word;
  // do last partial word
  if (__n > 0) {
    __first.__seg_ += __nw;
    __storage_type __m = ~__storage_type(0) >> (__bits_per_word - __n);
    if (_FillVal)
      *__first.__seg_ |= __m;
    else
      *__first.__seg_ &= ~__m;
  }
}

template <class _Cp, class _Size>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __bit_iterator<_Cp, false>
__fill_n(__bit_iterator<_Cp, false> __first, _Size __n, const bool& __value) {
  if (__n > 0) {
    if (__value)
      std::__fill_n_bool<true>(__first, __n);
    else
      std::__fill_n_bool<false>(__first, __n);
  }
  return __first + __n;
}

template <class _OutputIterator, class _Size, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
__fill_n(_OutputIterator __first, _Size __n, const _Tp& __value) {
  for (; __n > 0; ++__first, (void)--__n)
    *__first = __value;
  return __first;
}

template <class _OutputIterator, class _Size, class _Tp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _OutputIterator
fill_n(_OutputIterator __first, _Size __n, const _Tp& __value) {
  return std::__fill_n(__first, std::__convert_to_integral(__n), __value);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_FILL_N_H
