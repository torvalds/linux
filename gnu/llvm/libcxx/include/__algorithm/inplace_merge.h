//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_INPLACE_MERGE_H
#define _LIBCPP___ALGORITHM_INPLACE_MERGE_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/lower_bound.h>
#include <__algorithm/min.h>
#include <__algorithm/move.h>
#include <__algorithm/rotate.h>
#include <__algorithm/upper_bound.h>
#include <__config>
#include <__functional/identity.h>
#include <__iterator/advance.h>
#include <__iterator/distance.h>
#include <__iterator/iterator_traits.h>
#include <__iterator/reverse_iterator.h>
#include <__memory/destruct_n.h>
#include <__memory/temporary_buffer.h>
#include <__memory/unique_ptr.h>
#include <__utility/pair.h>
#include <new>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

template <class _Predicate>
class __invert // invert the sense of a comparison
{
private:
  _Predicate __p_;

public:
  _LIBCPP_HIDE_FROM_ABI __invert() {}

  _LIBCPP_HIDE_FROM_ABI explicit __invert(_Predicate __p) : __p_(__p) {}

  template <class _T1>
  _LIBCPP_HIDE_FROM_ABI bool operator()(const _T1& __x) {
    return !__p_(__x);
  }

  template <class _T1, class _T2>
  _LIBCPP_HIDE_FROM_ABI bool operator()(const _T1& __x, const _T2& __y) {
    return __p_(__y, __x);
  }
};

template <class _AlgPolicy,
          class _Compare,
          class _InputIterator1,
          class _Sent1,
          class _InputIterator2,
          class _Sent2,
          class _OutputIterator>
_LIBCPP_HIDE_FROM_ABI void __half_inplace_merge(
    _InputIterator1 __first1,
    _Sent1 __last1,
    _InputIterator2 __first2,
    _Sent2 __last2,
    _OutputIterator __result,
    _Compare&& __comp) {
  for (; __first1 != __last1; ++__result) {
    if (__first2 == __last2) {
      std::__move<_AlgPolicy>(__first1, __last1, __result);
      return;
    }

    if (__comp(*__first2, *__first1)) {
      *__result = _IterOps<_AlgPolicy>::__iter_move(__first2);
      ++__first2;
    } else {
      *__result = _IterOps<_AlgPolicy>::__iter_move(__first1);
      ++__first1;
    }
  }
  // __first2 through __last2 are already in the right spot.
}

template <class _AlgPolicy, class _Compare, class _BidirectionalIterator>
_LIBCPP_HIDE_FROM_ABI void __buffered_inplace_merge(
    _BidirectionalIterator __first,
    _BidirectionalIterator __middle,
    _BidirectionalIterator __last,
    _Compare&& __comp,
    typename iterator_traits<_BidirectionalIterator>::difference_type __len1,
    typename iterator_traits<_BidirectionalIterator>::difference_type __len2,
    typename iterator_traits<_BidirectionalIterator>::value_type* __buff) {
  typedef typename iterator_traits<_BidirectionalIterator>::value_type value_type;
  __destruct_n __d(0);
  unique_ptr<value_type, __destruct_n&> __h2(__buff, __d);
  if (__len1 <= __len2) {
    value_type* __p = __buff;
    for (_BidirectionalIterator __i = __first; __i != __middle;
         __d.template __incr<value_type>(), (void)++__i, (void)++__p)
      ::new ((void*)__p) value_type(_IterOps<_AlgPolicy>::__iter_move(__i));
    std::__half_inplace_merge<_AlgPolicy>(__buff, __p, __middle, __last, __first, __comp);
  } else {
    value_type* __p = __buff;
    for (_BidirectionalIterator __i = __middle; __i != __last;
         __d.template __incr<value_type>(), (void)++__i, (void)++__p)
      ::new ((void*)__p) value_type(_IterOps<_AlgPolicy>::__iter_move(__i));
    typedef reverse_iterator<_BidirectionalIterator> _RBi;
    typedef reverse_iterator<value_type*> _Rv;
    typedef __invert<_Compare> _Inverted;
    std::__half_inplace_merge<_AlgPolicy>(
        _Rv(__p), _Rv(__buff), _RBi(__middle), _RBi(__first), _RBi(__last), _Inverted(__comp));
  }
}

template <class _AlgPolicy, class _Compare, class _BidirectionalIterator>
void __inplace_merge(
    _BidirectionalIterator __first,
    _BidirectionalIterator __middle,
    _BidirectionalIterator __last,
    _Compare&& __comp,
    typename iterator_traits<_BidirectionalIterator>::difference_type __len1,
    typename iterator_traits<_BidirectionalIterator>::difference_type __len2,
    typename iterator_traits<_BidirectionalIterator>::value_type* __buff,
    ptrdiff_t __buff_size) {
  using _Ops = _IterOps<_AlgPolicy>;

  typedef typename iterator_traits<_BidirectionalIterator>::difference_type difference_type;
  while (true) {
    // if __middle == __last, we're done
    if (__len2 == 0)
      return;
    if (__len1 <= __buff_size || __len2 <= __buff_size)
      return std::__buffered_inplace_merge<_AlgPolicy>(__first, __middle, __last, __comp, __len1, __len2, __buff);
    // shrink [__first, __middle) as much as possible (with no moves), returning if it shrinks to 0
    for (; true; ++__first, (void)--__len1) {
      if (__len1 == 0)
        return;
      if (__comp(*__middle, *__first))
        break;
    }
    // __first < __middle < __last
    // *__first > *__middle
    // partition [__first, __m1) [__m1, __middle) [__middle, __m2) [__m2, __last) such that
    //     all elements in:
    //         [__first, __m1)  <= [__middle, __m2)
    //         [__middle, __m2) <  [__m1, __middle)
    //         [__m1, __middle) <= [__m2, __last)
    //     and __m1 or __m2 is in the middle of its range
    _BidirectionalIterator __m1; // "median" of [__first, __middle)
    _BidirectionalIterator __m2; // "median" of [__middle, __last)
    difference_type __len11;     // distance(__first, __m1)
    difference_type __len21;     // distance(__middle, __m2)
    // binary search smaller range
    if (__len1 < __len2) { // __len >= 1, __len2 >= 2
      __len21 = __len2 / 2;
      __m2    = __middle;
      _Ops::advance(__m2, __len21);
      __m1    = std::__upper_bound<_AlgPolicy>(__first, __middle, *__m2, __comp, std::__identity());
      __len11 = _Ops::distance(__first, __m1);
    } else {
      if (__len1 == 1) { // __len1 >= __len2 && __len2 > 0, therefore __len2 == 1
                         // It is known *__first > *__middle
        _Ops::iter_swap(__first, __middle);
        return;
      }
      // __len1 >= 2, __len2 >= 1
      __len11 = __len1 / 2;
      __m1    = __first;
      _Ops::advance(__m1, __len11);
      __m2    = std::lower_bound(__middle, __last, *__m1, __comp);
      __len21 = _Ops::distance(__middle, __m2);
    }
    difference_type __len12 = __len1 - __len11; // distance(__m1, __middle)
    difference_type __len22 = __len2 - __len21; // distance(__m2, __last)
    // [__first, __m1) [__m1, __middle) [__middle, __m2) [__m2, __last)
    // swap middle two partitions
    __middle = std::__rotate<_AlgPolicy>(__m1, __middle, __m2).first;
    // __len12 and __len21 now have swapped meanings
    // merge smaller range with recursive call and larger with tail recursion elimination
    if (__len11 + __len21 < __len12 + __len22) {
      std::__inplace_merge<_AlgPolicy>(__first, __m1, __middle, __comp, __len11, __len21, __buff, __buff_size);
      __first  = __middle;
      __middle = __m2;
      __len1   = __len12;
      __len2   = __len22;
    } else {
      std::__inplace_merge<_AlgPolicy>(__middle, __m2, __last, __comp, __len12, __len22, __buff, __buff_size);
      __last   = __middle;
      __middle = __m1;
      __len1   = __len11;
      __len2   = __len21;
    }
  }
}

template <class _AlgPolicy, class _BidirectionalIterator, class _Compare>
_LIBCPP_HIDE_FROM_ABI void __inplace_merge(
    _BidirectionalIterator __first, _BidirectionalIterator __middle, _BidirectionalIterator __last, _Compare&& __comp) {
  typedef typename iterator_traits<_BidirectionalIterator>::value_type value_type;
  typedef typename iterator_traits<_BidirectionalIterator>::difference_type difference_type;
  difference_type __len1     = _IterOps<_AlgPolicy>::distance(__first, __middle);
  difference_type __len2     = _IterOps<_AlgPolicy>::distance(__middle, __last);
  difference_type __buf_size = std::min(__len1, __len2);
  // TODO: Remove the use of std::get_temporary_buffer
  _LIBCPP_SUPPRESS_DEPRECATED_PUSH
  pair<value_type*, ptrdiff_t> __buf = std::get_temporary_buffer<value_type>(__buf_size);
  _LIBCPP_SUPPRESS_DEPRECATED_POP
  unique_ptr<value_type, __return_temporary_buffer> __h(__buf.first);
  return std::__inplace_merge<_AlgPolicy>(
      std::move(__first), std::move(__middle), std::move(__last), __comp, __len1, __len2, __buf.first, __buf.second);
}

template <class _BidirectionalIterator, class _Compare>
inline _LIBCPP_HIDE_FROM_ABI void inplace_merge(
    _BidirectionalIterator __first, _BidirectionalIterator __middle, _BidirectionalIterator __last, _Compare __comp) {
  std::__inplace_merge<_ClassicAlgPolicy>(
      std::move(__first), std::move(__middle), std::move(__last), static_cast<__comp_ref_type<_Compare> >(__comp));
}

template <class _BidirectionalIterator>
inline _LIBCPP_HIDE_FROM_ABI void
inplace_merge(_BidirectionalIterator __first, _BidirectionalIterator __middle, _BidirectionalIterator __last) {
  std::inplace_merge(std::move(__first), std::move(__middle), std::move(__last), __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_INPLACE_MERGE_H
