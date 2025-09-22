//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_ROTATE_H
#define _LIBCPP___ALGORITHM_ROTATE_H

#include <__algorithm/iterator_operations.h>
#include <__algorithm/move.h>
#include <__algorithm/move_backward.h>
#include <__algorithm/swap_ranges.h>
#include <__config>
#include <__iterator/iterator_traits.h>
#include <__type_traits/is_trivially_assignable.h>
#include <__utility/move.h>
#include <__utility/pair.h>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _AlgPolicy, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator
__rotate_left(_ForwardIterator __first, _ForwardIterator __last) {
  typedef typename iterator_traits<_ForwardIterator>::value_type value_type;
  using _Ops = _IterOps<_AlgPolicy>;

  value_type __tmp       = _Ops::__iter_move(__first);
  _ForwardIterator __lm1 = std::__move<_AlgPolicy>(_Ops::next(__first), __last, __first).second;
  *__lm1                 = std::move(__tmp);
  return __lm1;
}

template <class _AlgPolicy, class _BidirectionalIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _BidirectionalIterator
__rotate_right(_BidirectionalIterator __first, _BidirectionalIterator __last) {
  typedef typename iterator_traits<_BidirectionalIterator>::value_type value_type;
  using _Ops = _IterOps<_AlgPolicy>;

  _BidirectionalIterator __lm1 = _Ops::prev(__last);
  value_type __tmp             = _Ops::__iter_move(__lm1);
  _BidirectionalIterator __fp1 = std::__move_backward<_AlgPolicy>(__first, __lm1, std::move(__last)).second;
  *__first                     = std::move(__tmp);
  return __fp1;
}

template <class _AlgPolicy, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 _ForwardIterator
__rotate_forward(_ForwardIterator __first, _ForwardIterator __middle, _ForwardIterator __last) {
  _ForwardIterator __i = __middle;
  while (true) {
    _IterOps<_AlgPolicy>::iter_swap(__first, __i);
    ++__first;
    if (++__i == __last)
      break;
    if (__first == __middle)
      __middle = __i;
  }
  _ForwardIterator __r = __first;
  if (__first != __middle) {
    __i = __middle;
    while (true) {
      _IterOps<_AlgPolicy>::iter_swap(__first, __i);
      ++__first;
      if (++__i == __last) {
        if (__first == __middle)
          break;
        __i = __middle;
      } else if (__first == __middle)
        __middle = __i;
    }
  }
  return __r;
}

template <typename _Integral>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 _Integral __algo_gcd(_Integral __x, _Integral __y) {
  do {
    _Integral __t = __x % __y;
    __x           = __y;
    __y           = __t;
  } while (__y);
  return __x;
}

template <class _AlgPolicy, typename _RandomAccessIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX17 _RandomAccessIterator
__rotate_gcd(_RandomAccessIterator __first, _RandomAccessIterator __middle, _RandomAccessIterator __last) {
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  typedef typename iterator_traits<_RandomAccessIterator>::value_type value_type;
  using _Ops = _IterOps<_AlgPolicy>;

  const difference_type __m1 = __middle - __first;
  const difference_type __m2 = _Ops::distance(__middle, __last);
  if (__m1 == __m2) {
    std::__swap_ranges<_AlgPolicy>(__first, __middle, __middle, __last);
    return __middle;
  }
  const difference_type __g = std::__algo_gcd(__m1, __m2);
  for (_RandomAccessIterator __p = __first + __g; __p != __first;) {
    value_type __t(_Ops::__iter_move(--__p));
    _RandomAccessIterator __p1 = __p;
    _RandomAccessIterator __p2 = __p1 + __m1;
    do {
      *__p1                     = _Ops::__iter_move(__p2);
      __p1                      = __p2;
      const difference_type __d = _Ops::distance(__p2, __last);
      if (__m1 < __d)
        __p2 += __m1;
      else
        __p2 = __first + (__m1 - __d);
    } while (__p2 != __p);
    *__p1 = std::move(__t);
  }
  return __first + __m2;
}

template <class _AlgPolicy, class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _ForwardIterator
__rotate_impl(_ForwardIterator __first, _ForwardIterator __middle, _ForwardIterator __last, std::forward_iterator_tag) {
  typedef typename iterator_traits<_ForwardIterator>::value_type value_type;
  if (is_trivially_move_assignable<value_type>::value) {
    if (_IterOps<_AlgPolicy>::next(__first) == __middle)
      return std::__rotate_left<_AlgPolicy>(__first, __last);
  }
  return std::__rotate_forward<_AlgPolicy>(__first, __middle, __last);
}

template <class _AlgPolicy, class _BidirectionalIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _BidirectionalIterator __rotate_impl(
    _BidirectionalIterator __first,
    _BidirectionalIterator __middle,
    _BidirectionalIterator __last,
    bidirectional_iterator_tag) {
  typedef typename iterator_traits<_BidirectionalIterator>::value_type value_type;
  if (is_trivially_move_assignable<value_type>::value) {
    if (_IterOps<_AlgPolicy>::next(__first) == __middle)
      return std::__rotate_left<_AlgPolicy>(__first, __last);
    if (_IterOps<_AlgPolicy>::next(__middle) == __last)
      return std::__rotate_right<_AlgPolicy>(__first, __last);
  }
  return std::__rotate_forward<_AlgPolicy>(__first, __middle, __last);
}

template <class _AlgPolicy, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 _RandomAccessIterator __rotate_impl(
    _RandomAccessIterator __first,
    _RandomAccessIterator __middle,
    _RandomAccessIterator __last,
    random_access_iterator_tag) {
  typedef typename iterator_traits<_RandomAccessIterator>::value_type value_type;
  if (is_trivially_move_assignable<value_type>::value) {
    if (_IterOps<_AlgPolicy>::next(__first) == __middle)
      return std::__rotate_left<_AlgPolicy>(__first, __last);
    if (_IterOps<_AlgPolicy>::next(__middle) == __last)
      return std::__rotate_right<_AlgPolicy>(__first, __last);
    return std::__rotate_gcd<_AlgPolicy>(__first, __middle, __last);
  }
  return std::__rotate_forward<_AlgPolicy>(__first, __middle, __last);
}

template <class _AlgPolicy, class _Iterator, class _Sentinel>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 pair<_Iterator, _Iterator>
__rotate(_Iterator __first, _Iterator __middle, _Sentinel __last) {
  using _Ret            = pair<_Iterator, _Iterator>;
  _Iterator __last_iter = _IterOps<_AlgPolicy>::next(__middle, __last);

  if (__first == __middle)
    return _Ret(__last_iter, __last_iter);
  if (__middle == __last)
    return _Ret(std::move(__first), std::move(__last_iter));

  using _IterCategory = typename _IterOps<_AlgPolicy>::template __iterator_category<_Iterator>;
  auto __result = std::__rotate_impl<_AlgPolicy>(std::move(__first), std::move(__middle), __last_iter, _IterCategory());

  return _Ret(std::move(__result), std::move(__last_iter));
}

template <class _ForwardIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 _ForwardIterator
rotate(_ForwardIterator __first, _ForwardIterator __middle, _ForwardIterator __last) {
  return std::__rotate<_ClassicAlgPolicy>(std::move(__first), std::move(__middle), std::move(__last)).first;
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_ROTATE_H
