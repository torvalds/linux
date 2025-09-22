// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_FIND_H
#define _LIBCPP___ALGORITHM_FIND_H

#include <__algorithm/find_segment_if.h>
#include <__algorithm/min.h>
#include <__algorithm/unwrap_iter.h>
#include <__bit/countr.h>
#include <__bit/invert_if.h>
#include <__config>
#include <__functional/identity.h>
#include <__functional/invoke.h>
#include <__fwd/bit_reference.h>
#include <__iterator/segmented_iterator.h>
#include <__string/constexpr_c_functions.h>
#include <__type_traits/is_integral.h>
#include <__type_traits/is_same.h>
#include <__type_traits/is_signed.h>
#include <__utility/move.h>
#include <limits>

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
#  include <cwchar>
#endif

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// generic implementation
template <class _Iter, class _Sent, class _Tp, class _Proj>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Iter
__find(_Iter __first, _Sent __last, const _Tp& __value, _Proj& __proj) {
  for (; __first != __last; ++__first)
    if (std::__invoke(__proj, *__first) == __value)
      break;
  return __first;
}

// trivially equality comparable implementations
template <class _Tp,
          class _Up,
          class _Proj,
          __enable_if_t<__is_identity<_Proj>::value && __libcpp_is_trivially_equality_comparable<_Tp, _Up>::value &&
                            sizeof(_Tp) == 1,
                        int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp* __find(_Tp* __first, _Tp* __last, const _Up& __value, _Proj&) {
  if (auto __ret = std::__constexpr_memchr(__first, __value, __last - __first))
    return __ret;
  return __last;
}

#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
template <class _Tp,
          class _Up,
          class _Proj,
          __enable_if_t<__is_identity<_Proj>::value && __libcpp_is_trivially_equality_comparable<_Tp, _Up>::value &&
                            sizeof(_Tp) == sizeof(wchar_t) && _LIBCPP_ALIGNOF(_Tp) >= _LIBCPP_ALIGNOF(wchar_t),
                        int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp* __find(_Tp* __first, _Tp* __last, const _Up& __value, _Proj&) {
  if (auto __ret = std::__constexpr_wmemchr(__first, __value, __last - __first))
    return __ret;
  return __last;
}
#endif // _LIBCPP_HAS_NO_WIDE_CHARACTERS

// TODO: This should also be possible to get right with different signedness
// cast integral types to allow vectorization
template <class _Tp,
          class _Up,
          class _Proj,
          __enable_if_t<__is_identity<_Proj>::value && !__libcpp_is_trivially_equality_comparable<_Tp, _Up>::value &&
                            is_integral<_Tp>::value && is_integral<_Up>::value &&
                            is_signed<_Tp>::value == is_signed<_Up>::value,
                        int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _Tp*
__find(_Tp* __first, _Tp* __last, const _Up& __value, _Proj& __proj) {
  if (__value < numeric_limits<_Tp>::min() || __value > numeric_limits<_Tp>::max())
    return __last;
  return std::__find(__first, __last, _Tp(__value), __proj);
}

// __bit_iterator implementation
template <bool _ToFind, class _Cp, bool _IsConst>
_LIBCPP_CONSTEXPR_SINCE_CXX20 _LIBCPP_HIDE_FROM_ABI __bit_iterator<_Cp, _IsConst>
__find_bool(__bit_iterator<_Cp, _IsConst> __first, typename _Cp::size_type __n) {
  using _It            = __bit_iterator<_Cp, _IsConst>;
  using __storage_type = typename _It::__storage_type;

  const int __bits_per_word = _It::__bits_per_word;
  // do first partial word
  if (__first.__ctz_ != 0) {
    __storage_type __clz_f = static_cast<__storage_type>(__bits_per_word - __first.__ctz_);
    __storage_type __dn    = std::min(__clz_f, __n);
    __storage_type __m     = (~__storage_type(0) << __first.__ctz_) & (~__storage_type(0) >> (__clz_f - __dn));
    __storage_type __b     = std::__invert_if<!_ToFind>(*__first.__seg_) & __m;
    if (__b)
      return _It(__first.__seg_, static_cast<unsigned>(std::__libcpp_ctz(__b)));
    if (__n == __dn)
      return __first + __n;
    __n -= __dn;
    ++__first.__seg_;
  }
  // do middle whole words
  for (; __n >= __bits_per_word; ++__first.__seg_, __n -= __bits_per_word) {
    __storage_type __b = std::__invert_if<!_ToFind>(*__first.__seg_);
    if (__b)
      return _It(__first.__seg_, static_cast<unsigned>(std::__libcpp_ctz(__b)));
  }
  // do last partial word
  if (__n > 0) {
    __storage_type __m = ~__storage_type(0) >> (__bits_per_word - __n);
    __storage_type __b = std::__invert_if<!_ToFind>(*__first.__seg_) & __m;
    if (__b)
      return _It(__first.__seg_, static_cast<unsigned>(std::__libcpp_ctz(__b)));
  }
  return _It(__first.__seg_, static_cast<unsigned>(__n));
}

template <class _Cp, bool _IsConst, class _Tp, class _Proj, __enable_if_t<__is_identity<_Proj>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 __bit_iterator<_Cp, _IsConst>
__find(__bit_iterator<_Cp, _IsConst> __first, __bit_iterator<_Cp, _IsConst> __last, const _Tp& __value, _Proj&) {
  if (static_cast<bool>(__value))
    return std::__find_bool<true>(__first, static_cast<typename _Cp::size_type>(__last - __first));
  return std::__find_bool<false>(__first, static_cast<typename _Cp::size_type>(__last - __first));
}

// segmented iterator implementation

template <class>
struct __find_segment;

template <class _SegmentedIterator,
          class _Tp,
          class _Proj,
          __enable_if_t<__is_segmented_iterator<_SegmentedIterator>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _SegmentedIterator
__find(_SegmentedIterator __first, _SegmentedIterator __last, const _Tp& __value, _Proj& __proj) {
  return std::__find_segment_if(std::move(__first), std::move(__last), __find_segment<_Tp>(__value), __proj);
}

template <class _Tp>
struct __find_segment {
  const _Tp& __value_;

  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR __find_segment(const _Tp& __value) : __value_(__value) {}

  template <class _InputIterator, class _Proj>
  _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR _InputIterator
  operator()(_InputIterator __first, _InputIterator __last, _Proj& __proj) const {
    return std::__find(__first, __last, __value_, __proj);
  }
};

// public API
template <class _InputIterator, class _Tp>
_LIBCPP_NODISCARD inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _InputIterator
find(_InputIterator __first, _InputIterator __last, const _Tp& __value) {
  __identity __proj;
  return std::__rewrap_iter(
      __first, std::__find(std::__unwrap_iter(__first), std::__unwrap_iter(__last), __value, __proj));
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_FIND_H
