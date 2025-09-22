// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_COUNT_H
#define _LIBCPP___ALGORITHM_COUNT_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/min.h>
#include <__bit/invert_if.h>
#include <__bit/popcount.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__fwd/bit_reference.h>
#include <__iterator/iterator_traits.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// generic implementation
template <class _AlgPolicy, class _Iter, class _Sent, class _Tp, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename _IterOps<_AlgPolicy>::template __difference_type<_Iter>
__count(_Iter __first, _Sent __last, const _Tp& __value, _Proj& __proj) {
  typename _IterOps<_AlgPolicy>::template __difference_type<_Iter> __r(0);
  for (; __first != __last; ++__first)
    if (std::__invoke(__proj, *__first) == __value)
      ++__r;
  return __r;
}

// __bit_iterator implementation
template <bool _ToCount, class _Cp, bool _IsConst>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 typename __bit_iterator<_Cp, _IsConst>::difference_type
__count_bool(__bit_iterator<_Cp, _IsConst> __first, typename _Cp::size_type __n) {
  using _It             = __bit_iterator<_Cp, _IsConst>;
  using __storage_type  = typename _It::__storage_type;
  using difference_type = typename _It::difference_type;

  const int __bits_per_word = _It::__bits_per_word;
  difference_type __r       = 0;
  // do first partial word
  if (__first.__ctz_ != 0) {
    __storage_type __clz_f = static_cast<__storage_type>(__bits_per_word - __first.__ctz_);
    __storage_type __dn    = std::min(__clz_f, __n);
    __storage_type __m     = (~__storage_type(0) << __first.__ctz_) & (~__storage_type(0) >> (__clz_f - __dn));
    __r                    = std::__libcpp_popcount(std::__invert_if<!_ToCount>(*__first.__seg_) & __m);
    __n -= __dn;
    ++__first.__seg_;
  }
  // do middle whole words
  for (; __n >= __bits_per_word; ++__first.__seg_, __n -= __bits_per_word)
    __r += std::__libcpp_popcount(std::__invert_if<!_ToCount>(*__first.__seg_));
  // do last partial word
  if (__n > 0) {
    __storage_type __m = ~__storage_type(0) >> (__bits_per_word - __n);
    __r += std::__libcpp_popcount(std::__invert_if<!_ToCount>(*__first.__seg_) & __m);
  }
  return __r;
}

template <class, class _Cp, bool _IsConst, class _Tp, class _Proj, __enable_if_t<__is_identity<_Proj>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __iter_diff_t<__bit_iterator<_Cp, _IsConst> >
__count(__bit_iterator<_Cp, _IsConst> __first, __bit_iterator<_Cp, _IsConst> __last, const _Tp& __value, _Proj&) {
  if (__value)
    return std::__count_bool<true>(__first, static_cast<typename _Cp::size_type>(__last - __first));
  return std::__count_bool<false>(__first, static_cast<typename _Cp::size_type>(__last - __first));
}

template <class _InputIterator, class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __iter_diff_t<_InputIterator>
count(_InputIterator __first, _InputIterator __last, const _Tp& __value) {
  __identity __proj;
  return std::__count<_ClassicAlgPolicy>(__first, __last, __value, __proj);
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_COUNT_H
