//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef _LIBCPP___ALGORITHM_SORT_H
#define _LIBCPP___ALGORITHM_SORT_H

#include <__algorithm/comp.h>
#include <__algorithm/comp_ref_type.h>
#include <__algorithm/iter_swap.h>
#include <__algorithm/iterator_operations.h>
#include <__algorithm/min_element.h>
#include <__algorithm/partial_sort.h>
#include <__algorithm/unwrap_iter.h>
#include <__assert>
#include <__bit/blsr.h>
#include <__bit/countl.h>
#include <__bit/countr.h>
#include <__config>
#include <__debug_utils/randomize_range.h>
#include <__debug_utils/strict_weak_ordering_check.h>
#include <__functional/operations.h>
#include <__functional/ranges_operations.h>
#include <__iterator/iterator_traits.h>
#include <__type_traits/conditional.h>
#include <__type_traits/disjunction.h>
#include <__type_traits/is_arithmetic.h>
#include <__type_traits/is_constant_evaluated.h>
#include <__utility/move.h>
#include <__utility/pair.h>
#include <climits>
#include <cstdint>

#if !defined(_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER)
#  pragma GCC system_header
#endif

_LIBCPP_PUSH_MACROS
#include <__undef_macros>

_LIBCPP_BEGIN_NAMESPACE_STD

// stable, 2-3 compares, 0-2 swaps

template <class _AlgPolicy, class _Compare, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 unsigned
__sort3(_ForwardIterator __x, _ForwardIterator __y, _ForwardIterator __z, _Compare __c) {
  using _Ops = _IterOps<_AlgPolicy>;

  unsigned __r = 0;
  if (!__c(*__y, *__x)) // if x <= y
  {
    if (!__c(*__z, *__y))      // if y <= z
      return __r;              // x <= y && y <= z
                               // x <= y && y > z
    _Ops::iter_swap(__y, __z); // x <= z && y < z
    __r = 1;
    if (__c(*__y, *__x)) // if x > y
    {
      _Ops::iter_swap(__x, __y); // x < y && y <= z
      __r = 2;
    }
    return __r; // x <= y && y < z
  }
  if (__c(*__z, *__y)) // x > y, if y > z
  {
    _Ops::iter_swap(__x, __z); // x < y && y < z
    __r = 1;
    return __r;
  }
  _Ops::iter_swap(__x, __y); // x > y && y <= z
  __r = 1;                   // x < y && x <= z
  if (__c(*__z, *__y))       // if y > z
  {
    _Ops::iter_swap(__y, __z); // x <= y && y < z
    __r = 2;
  }
  return __r;
} // x <= y && y <= z

// stable, 3-6 compares, 0-5 swaps

template <class _AlgPolicy, class _Compare, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI void
__sort4(_ForwardIterator __x1, _ForwardIterator __x2, _ForwardIterator __x3, _ForwardIterator __x4, _Compare __c) {
  using _Ops = _IterOps<_AlgPolicy>;
  std::__sort3<_AlgPolicy, _Compare>(__x1, __x2, __x3, __c);
  if (__c(*__x4, *__x3)) {
    _Ops::iter_swap(__x3, __x4);
    if (__c(*__x3, *__x2)) {
      _Ops::iter_swap(__x2, __x3);
      if (__c(*__x2, *__x1)) {
        _Ops::iter_swap(__x1, __x2);
      }
    }
  }
}

// stable, 4-10 compares, 0-9 swaps

template <class _AlgPolicy, class _Comp, class _ForwardIterator>
_LIBCPP_HIDE_FROM_ABI void
__sort5(_ForwardIterator __x1,
        _ForwardIterator __x2,
        _ForwardIterator __x3,
        _ForwardIterator __x4,
        _ForwardIterator __x5,
        _Comp __comp) {
  using _Ops = _IterOps<_AlgPolicy>;

  std::__sort4<_AlgPolicy, _Comp>(__x1, __x2, __x3, __x4, __comp);
  if (__comp(*__x5, *__x4)) {
    _Ops::iter_swap(__x4, __x5);
    if (__comp(*__x4, *__x3)) {
      _Ops::iter_swap(__x3, __x4);
      if (__comp(*__x3, *__x2)) {
        _Ops::iter_swap(__x2, __x3);
        if (__comp(*__x2, *__x1)) {
          _Ops::iter_swap(__x1, __x2);
        }
      }
    }
  }
}

// The comparator being simple is a prerequisite for using the branchless optimization.
template <class _Tp>
struct __is_simple_comparator : false_type {};
template <>
struct __is_simple_comparator<__less<>&> : true_type {};
template <class _Tp>
struct __is_simple_comparator<less<_Tp>&> : true_type {};
template <class _Tp>
struct __is_simple_comparator<greater<_Tp>&> : true_type {};
#if _LIBCPP_STD_VER >= 20
template <>
struct __is_simple_comparator<ranges::less&> : true_type {};
template <>
struct __is_simple_comparator<ranges::greater&> : true_type {};
#endif

template <class _Compare, class _Iter, class _Tp = typename iterator_traits<_Iter>::value_type>
using __use_branchless_sort =
    integral_constant<bool,
                      __libcpp_is_contiguous_iterator<_Iter>::value && sizeof(_Tp) <= sizeof(void*) &&
                          is_arithmetic<_Tp>::value && __is_simple_comparator<_Compare>::value>;

namespace __detail {

// Size in bits for the bitset in use.
enum { __block_size = sizeof(uint64_t) * 8 };

} // namespace __detail

// Ensures that __c(*__x, *__y) is true by swapping *__x and *__y if necessary.
template <class _Compare, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI void __cond_swap(_RandomAccessIterator __x, _RandomAccessIterator __y, _Compare __c) {
  // Note: this function behaves correctly even with proxy iterators (because it relies on `value_type`).
  using value_type = typename iterator_traits<_RandomAccessIterator>::value_type;
  bool __r         = __c(*__x, *__y);
  value_type __tmp = __r ? *__x : *__y;
  *__y             = __r ? *__y : *__x;
  *__x             = __tmp;
}

// Ensures that *__x, *__y and *__z are ordered according to the comparator __c,
// under the assumption that *__y and *__z are already ordered.
template <class _Compare, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI void
__partially_sorted_swap(_RandomAccessIterator __x, _RandomAccessIterator __y, _RandomAccessIterator __z, _Compare __c) {
  // Note: this function behaves correctly even with proxy iterators (because it relies on `value_type`).
  using value_type = typename iterator_traits<_RandomAccessIterator>::value_type;
  bool __r         = __c(*__z, *__x);
  value_type __tmp = __r ? *__z : *__x;
  *__z             = __r ? *__x : *__z;
  __r              = __c(__tmp, *__y);
  *__x             = __r ? *__x : *__y;
  *__y             = __r ? *__y : __tmp;
}

template <class,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort3_maybe_branchless(
    _RandomAccessIterator __x1, _RandomAccessIterator __x2, _RandomAccessIterator __x3, _Compare __c) {
  std::__cond_swap<_Compare>(__x2, __x3, __c);
  std::__partially_sorted_swap<_Compare>(__x1, __x2, __x3, __c);
}

template <class _AlgPolicy,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<!__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort3_maybe_branchless(
    _RandomAccessIterator __x1, _RandomAccessIterator __x2, _RandomAccessIterator __x3, _Compare __c) {
  std::__sort3<_AlgPolicy, _Compare>(__x1, __x2, __x3, __c);
}

template <class,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort4_maybe_branchless(
    _RandomAccessIterator __x1,
    _RandomAccessIterator __x2,
    _RandomAccessIterator __x3,
    _RandomAccessIterator __x4,
    _Compare __c) {
  std::__cond_swap<_Compare>(__x1, __x3, __c);
  std::__cond_swap<_Compare>(__x2, __x4, __c);
  std::__cond_swap<_Compare>(__x1, __x2, __c);
  std::__cond_swap<_Compare>(__x3, __x4, __c);
  std::__cond_swap<_Compare>(__x2, __x3, __c);
}

template <class _AlgPolicy,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<!__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort4_maybe_branchless(
    _RandomAccessIterator __x1,
    _RandomAccessIterator __x2,
    _RandomAccessIterator __x3,
    _RandomAccessIterator __x4,
    _Compare __c) {
  std::__sort4<_AlgPolicy, _Compare>(__x1, __x2, __x3, __x4, __c);
}

template <class _AlgPolicy,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort5_maybe_branchless(
    _RandomAccessIterator __x1,
    _RandomAccessIterator __x2,
    _RandomAccessIterator __x3,
    _RandomAccessIterator __x4,
    _RandomAccessIterator __x5,
    _Compare __c) {
  std::__cond_swap<_Compare>(__x1, __x2, __c);
  std::__cond_swap<_Compare>(__x4, __x5, __c);
  std::__partially_sorted_swap<_Compare>(__x3, __x4, __x5, __c);
  std::__cond_swap<_Compare>(__x2, __x5, __c);
  std::__partially_sorted_swap<_Compare>(__x1, __x3, __x4, __c);
  std::__partially_sorted_swap<_Compare>(__x2, __x3, __x4, __c);
}

template <class _AlgPolicy,
          class _Compare,
          class _RandomAccessIterator,
          __enable_if_t<!__use_branchless_sort<_Compare, _RandomAccessIterator>::value, int> = 0>
inline _LIBCPP_HIDE_FROM_ABI void __sort5_maybe_branchless(
    _RandomAccessIterator __x1,
    _RandomAccessIterator __x2,
    _RandomAccessIterator __x3,
    _RandomAccessIterator __x4,
    _RandomAccessIterator __x5,
    _Compare __c) {
  std::__sort5<_AlgPolicy, _Compare, _RandomAccessIterator>(
      std::move(__x1), std::move(__x2), std::move(__x3), std::move(__x4), std::move(__x5), __c);
}

// Assumes size > 0
template <class _AlgPolicy, class _Compare, class _BidirectionalIterator>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX14 void
__selection_sort(_BidirectionalIterator __first, _BidirectionalIterator __last, _Compare __comp) {
  _BidirectionalIterator __lm1 = __last;
  for (--__lm1; __first != __lm1; ++__first) {
    _BidirectionalIterator __i = std::__min_element<_Compare>(__first, __last, __comp);
    if (__i != __first)
      _IterOps<_AlgPolicy>::iter_swap(__first, __i);
  }
}

// Sort the iterator range [__first, __last) using the comparator __comp using
// the insertion sort algorithm.
template <class _AlgPolicy, class _Compare, class _BidirectionalIterator>
_LIBCPP_HIDE_FROM_ABI void
__insertion_sort(_BidirectionalIterator __first, _BidirectionalIterator __last, _Compare __comp) {
  using _Ops = _IterOps<_AlgPolicy>;

  typedef typename iterator_traits<_BidirectionalIterator>::value_type value_type;
  if (__first == __last)
    return;
  _BidirectionalIterator __i = __first;
  for (++__i; __i != __last; ++__i) {
    _BidirectionalIterator __j = __i;
    --__j;
    if (__comp(*__i, *__j)) {
      value_type __t(_Ops::__iter_move(__i));
      _BidirectionalIterator __k = __j;
      __j                        = __i;
      do {
        *__j = _Ops::__iter_move(__k);
        __j  = __k;
      } while (__j != __first && __comp(__t, *--__k));
      *__j = std::move(__t);
    }
  }
}

// Sort the iterator range [__first, __last) using the comparator __comp using
// the insertion sort algorithm.  Insertion sort has two loops, outer and inner.
// The implementation below has no bounds check (unguarded) for the inner loop.
// Assumes that there is an element in the position (__first - 1) and that each
// element in the input range is greater or equal to the element at __first - 1.
template <class _AlgPolicy, class _Compare, class _RandomAccessIterator>
_LIBCPP_HIDE_FROM_ABI void
__insertion_sort_unguarded(_RandomAccessIterator const __first, _RandomAccessIterator __last, _Compare __comp) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  typedef typename iterator_traits<_RandomAccessIterator>::value_type value_type;
  if (__first == __last)
    return;
  const _RandomAccessIterator __leftmost = __first - difference_type(1);
  (void)__leftmost; // can be unused when assertions are disabled
  for (_RandomAccessIterator __i = __first + difference_type(1); __i != __last; ++__i) {
    _RandomAccessIterator __j = __i - difference_type(1);
    if (__comp(*__i, *__j)) {
      value_type __t(_Ops::__iter_move(__i));
      _RandomAccessIterator __k = __j;
      __j                       = __i;
      do {
        *__j = _Ops::__iter_move(__k);
        __j  = __k;
        _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
            __k != __leftmost,
            "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      } while (__comp(__t, *--__k)); // No need for bounds check due to the assumption stated above.
      *__j = std::move(__t);
    }
  }
}

template <class _AlgPolicy, class _Comp, class _RandomAccessIterator>
_LIBCPP_HIDE_FROM_ABI bool
__insertion_sort_incomplete(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) {
  using _Ops = _IterOps<_AlgPolicy>;

  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  switch (__last - __first) {
  case 0:
  case 1:
    return true;
  case 2:
    if (__comp(*--__last, *__first))
      _Ops::iter_swap(__first, __last);
    return true;
  case 3:
    std::__sort3_maybe_branchless<_AlgPolicy, _Comp>(__first, __first + difference_type(1), --__last, __comp);
    return true;
  case 4:
    std::__sort4_maybe_branchless<_AlgPolicy, _Comp>(
        __first, __first + difference_type(1), __first + difference_type(2), --__last, __comp);
    return true;
  case 5:
    std::__sort5_maybe_branchless<_AlgPolicy, _Comp>(
        __first,
        __first + difference_type(1),
        __first + difference_type(2),
        __first + difference_type(3),
        --__last,
        __comp);
    return true;
  }
  typedef typename iterator_traits<_RandomAccessIterator>::value_type value_type;
  _RandomAccessIterator __j = __first + difference_type(2);
  std::__sort3_maybe_branchless<_AlgPolicy, _Comp>(__first, __first + difference_type(1), __j, __comp);
  const unsigned __limit = 8;
  unsigned __count       = 0;
  for (_RandomAccessIterator __i = __j + difference_type(1); __i != __last; ++__i) {
    if (__comp(*__i, *__j)) {
      value_type __t(_Ops::__iter_move(__i));
      _RandomAccessIterator __k = __j;
      __j                       = __i;
      do {
        *__j = _Ops::__iter_move(__k);
        __j  = __k;
      } while (__j != __first && __comp(__t, *--__k));
      *__j = std::move(__t);
      if (++__count == __limit)
        return ++__i == __last;
    }
    __j = __i;
  }
  return true;
}

template <class _AlgPolicy, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI void __swap_bitmap_pos(
    _RandomAccessIterator __first, _RandomAccessIterator __last, uint64_t& __left_bitset, uint64_t& __right_bitset) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename std::iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  // Swap one pair on each iteration as long as both bitsets have at least one
  // element for swapping.
  while (__left_bitset != 0 && __right_bitset != 0) {
    difference_type __tz_left  = __libcpp_ctz(__left_bitset);
    __left_bitset              = __libcpp_blsr(__left_bitset);
    difference_type __tz_right = __libcpp_ctz(__right_bitset);
    __right_bitset             = __libcpp_blsr(__right_bitset);
    _Ops::iter_swap(__first + __tz_left, __last - __tz_right);
  }
}

template <class _Compare,
          class _RandomAccessIterator,
          class _ValueType = typename iterator_traits<_RandomAccessIterator>::value_type>
inline _LIBCPP_HIDE_FROM_ABI void
__populate_left_bitset(_RandomAccessIterator __first, _Compare __comp, _ValueType& __pivot, uint64_t& __left_bitset) {
  // Possible vectorization. With a proper "-march" flag, the following loop
  // will be compiled into a set of SIMD instructions.
  _RandomAccessIterator __iter = __first;
  for (int __j = 0; __j < __detail::__block_size;) {
    bool __comp_result = !__comp(*__iter, __pivot);
    __left_bitset |= (static_cast<uint64_t>(__comp_result) << __j);
    __j++;
    ++__iter;
  }
}

template <class _Compare,
          class _RandomAccessIterator,
          class _ValueType = typename iterator_traits<_RandomAccessIterator>::value_type>
inline _LIBCPP_HIDE_FROM_ABI void
__populate_right_bitset(_RandomAccessIterator __lm1, _Compare __comp, _ValueType& __pivot, uint64_t& __right_bitset) {
  // Possible vectorization. With a proper "-march" flag, the following loop
  // will be compiled into a set of SIMD instructions.
  _RandomAccessIterator __iter = __lm1;
  for (int __j = 0; __j < __detail::__block_size;) {
    bool __comp_result = __comp(*__iter, __pivot);
    __right_bitset |= (static_cast<uint64_t>(__comp_result) << __j);
    __j++;
    --__iter;
  }
}

template <class _AlgPolicy,
          class _Compare,
          class _RandomAccessIterator,
          class _ValueType = typename iterator_traits<_RandomAccessIterator>::value_type>
inline _LIBCPP_HIDE_FROM_ABI void __bitset_partition_partial_blocks(
    _RandomAccessIterator& __first,
    _RandomAccessIterator& __lm1,
    _Compare __comp,
    _ValueType& __pivot,
    uint64_t& __left_bitset,
    uint64_t& __right_bitset) {
  typedef typename std::iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  difference_type __remaining_len = __lm1 - __first + 1;
  difference_type __l_size;
  difference_type __r_size;
  if (__left_bitset == 0 && __right_bitset == 0) {
    __l_size = __remaining_len / 2;
    __r_size = __remaining_len - __l_size;
  } else if (__left_bitset == 0) {
    // We know at least one side is a full block.
    __l_size = __remaining_len - __detail::__block_size;
    __r_size = __detail::__block_size;
  } else { // if (__right_bitset == 0)
    __l_size = __detail::__block_size;
    __r_size = __remaining_len - __detail::__block_size;
  }
  // Record the comparison outcomes for the elements currently on the left side.
  if (__left_bitset == 0) {
    _RandomAccessIterator __iter = __first;
    for (int __j = 0; __j < __l_size; __j++) {
      bool __comp_result = !__comp(*__iter, __pivot);
      __left_bitset |= (static_cast<uint64_t>(__comp_result) << __j);
      ++__iter;
    }
  }
  // Record the comparison outcomes for the elements currently on the right
  // side.
  if (__right_bitset == 0) {
    _RandomAccessIterator __iter = __lm1;
    for (int __j = 0; __j < __r_size; __j++) {
      bool __comp_result = __comp(*__iter, __pivot);
      __right_bitset |= (static_cast<uint64_t>(__comp_result) << __j);
      --__iter;
    }
  }
  std::__swap_bitmap_pos<_AlgPolicy, _RandomAccessIterator>(__first, __lm1, __left_bitset, __right_bitset);
  __first += (__left_bitset == 0) ? __l_size : 0;
  __lm1 -= (__right_bitset == 0) ? __r_size : 0;
}

template <class _AlgPolicy, class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI void __swap_bitmap_pos_within(
    _RandomAccessIterator& __first, _RandomAccessIterator& __lm1, uint64_t& __left_bitset, uint64_t& __right_bitset) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename std::iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  if (__left_bitset) {
    // Swap within the left side.  Need to find set positions in the reverse
    // order.
    while (__left_bitset != 0) {
      difference_type __tz_left = __detail::__block_size - 1 - __libcpp_clz(__left_bitset);
      __left_bitset &= (static_cast<uint64_t>(1) << __tz_left) - 1;
      _RandomAccessIterator __it = __first + __tz_left;
      if (__it != __lm1) {
        _Ops::iter_swap(__it, __lm1);
      }
      --__lm1;
    }
    __first = __lm1 + difference_type(1);
  } else if (__right_bitset) {
    // Swap within the right side.  Need to find set positions in the reverse
    // order.
    while (__right_bitset != 0) {
      difference_type __tz_right = __detail::__block_size - 1 - __libcpp_clz(__right_bitset);
      __right_bitset &= (static_cast<uint64_t>(1) << __tz_right) - 1;
      _RandomAccessIterator __it = __lm1 - __tz_right;
      if (__it != __first) {
        _Ops::iter_swap(__it, __first);
      }
      ++__first;
    }
  }
}

// Partition [__first, __last) using the comparator __comp.  *__first has the
// chosen pivot.  Elements that are equivalent are kept to the left of the
// pivot.  Returns the iterator for the pivot and a bool value which is true if
// the provided range is already sorted, false otherwise.  We assume that the
// length of the range is at least three elements.
//
// __bitset_partition uses bitsets for storing outcomes of the comparisons
// between the pivot and other elements.
template <class _AlgPolicy, class _RandomAccessIterator, class _Compare>
_LIBCPP_HIDE_FROM_ABI std::pair<_RandomAccessIterator, bool>
__bitset_partition(_RandomAccessIterator __first, _RandomAccessIterator __last, _Compare __comp) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename std::iterator_traits<_RandomAccessIterator>::value_type value_type;
  typedef typename std::iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  _LIBCPP_ASSERT_INTERNAL(__last - __first >= difference_type(3), "");
  const _RandomAccessIterator __begin = __first; // used for bounds checking, those are not moved around
  const _RandomAccessIterator __end   = __last;
  (void)__end; //

  value_type __pivot(_Ops::__iter_move(__first));
  // Find the first element greater than the pivot.
  if (__comp(__pivot, *(__last - difference_type(1)))) {
    // Not guarded since we know the last element is greater than the pivot.
    do {
      ++__first;
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __first != __end,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
    } while (!__comp(__pivot, *__first));
  } else {
    while (++__first < __last && !__comp(__pivot, *__first)) {
    }
  }
  // Find the last element less than or equal to the pivot.
  if (__first < __last) {
    // It will be always guarded because __introsort will do the median-of-three
    // before calling this.
    do {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __last != __begin,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      --__last;
    } while (__comp(__pivot, *__last));
  }
  // If the first element greater than the pivot is at or after the
  // last element less than or equal to the pivot, then we have covered the
  // entire range without swapping elements.  This implies the range is already
  // partitioned.
  bool __already_partitioned = __first >= __last;
  if (!__already_partitioned) {
    _Ops::iter_swap(__first, __last);
    ++__first;
  }

  // In [__first, __last) __last is not inclusive. From now on, it uses last
  // minus one to be inclusive on both sides.
  _RandomAccessIterator __lm1 = __last - difference_type(1);
  uint64_t __left_bitset      = 0;
  uint64_t __right_bitset     = 0;

  // Reminder: length = __lm1 - __first + 1.
  while (__lm1 - __first >= 2 * __detail::__block_size - 1) {
    // Record the comparison outcomes for the elements currently on the left
    // side.
    if (__left_bitset == 0)
      std::__populate_left_bitset<_Compare>(__first, __comp, __pivot, __left_bitset);
    // Record the comparison outcomes for the elements currently on the right
    // side.
    if (__right_bitset == 0)
      std::__populate_right_bitset<_Compare>(__lm1, __comp, __pivot, __right_bitset);
    // Swap the elements recorded to be the candidates for swapping in the
    // bitsets.
    std::__swap_bitmap_pos<_AlgPolicy, _RandomAccessIterator>(__first, __lm1, __left_bitset, __right_bitset);
    // Only advance the iterator if all the elements that need to be moved to
    // other side were moved.
    __first += (__left_bitset == 0) ? difference_type(__detail::__block_size) : difference_type(0);
    __lm1 -= (__right_bitset == 0) ? difference_type(__detail::__block_size) : difference_type(0);
  }
  // Now, we have a less-than a block worth of elements on at least one of the
  // sides.
  std::__bitset_partition_partial_blocks<_AlgPolicy, _Compare>(
      __first, __lm1, __comp, __pivot, __left_bitset, __right_bitset);
  // At least one the bitsets would be empty.  For the non-empty one, we need to
  // properly partition the elements that appear within that bitset.
  std::__swap_bitmap_pos_within<_AlgPolicy>(__first, __lm1, __left_bitset, __right_bitset);

  // Move the pivot to its correct position.
  _RandomAccessIterator __pivot_pos = __first - difference_type(1);
  if (__begin != __pivot_pos) {
    *__begin = _Ops::__iter_move(__pivot_pos);
  }
  *__pivot_pos = std::move(__pivot);
  return std::make_pair(__pivot_pos, __already_partitioned);
}

// Partition [__first, __last) using the comparator __comp.  *__first has the
// chosen pivot.  Elements that are equivalent are kept to the right of the
// pivot.  Returns the iterator for the pivot and a bool value which is true if
// the provided range is already sorted, false otherwise.  We assume that the
// length of the range is at least three elements.
template <class _AlgPolicy, class _RandomAccessIterator, class _Compare>
_LIBCPP_HIDE_FROM_ABI std::pair<_RandomAccessIterator, bool>
__partition_with_equals_on_right(_RandomAccessIterator __first, _RandomAccessIterator __last, _Compare __comp) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  typedef typename std::iterator_traits<_RandomAccessIterator>::value_type value_type;
  _LIBCPP_ASSERT_INTERNAL(__last - __first >= difference_type(3), "");
  const _RandomAccessIterator __begin = __first; // used for bounds checking, those are not moved around
  const _RandomAccessIterator __end   = __last;
  (void)__end; //
  value_type __pivot(_Ops::__iter_move(__first));
  // Find the first element greater or equal to the pivot.  It will be always
  // guarded because __introsort will do the median-of-three before calling
  // this.
  do {
    ++__first;
    _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
        __first != __end,
        "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
  } while (__comp(*__first, __pivot));

  // Find the last element less than the pivot.
  if (__begin == __first - difference_type(1)) {
    while (__first < __last && !__comp(*--__last, __pivot))
      ;
  } else {
    // Guarded.
    do {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __last != __begin,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      --__last;
    } while (!__comp(*__last, __pivot));
  }

  // If the first element greater than or equal to the pivot is at or after the
  // last element less than the pivot, then we have covered the entire range
  // without swapping elements.  This implies the range is already partitioned.
  bool __already_partitioned = __first >= __last;
  // Go through the remaining elements.  Swap pairs of elements (one to the
  // right of the pivot and the other to left of the pivot) that are not on the
  // correct side of the pivot.
  while (__first < __last) {
    _Ops::iter_swap(__first, __last);
    do {
      ++__first;
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __first != __end,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
    } while (__comp(*__first, __pivot));
    do {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __last != __begin,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      --__last;
    } while (!__comp(*__last, __pivot));
  }
  // Move the pivot to its correct position.
  _RandomAccessIterator __pivot_pos = __first - difference_type(1);
  if (__begin != __pivot_pos) {
    *__begin = _Ops::__iter_move(__pivot_pos);
  }
  *__pivot_pos = std::move(__pivot);
  return std::make_pair(__pivot_pos, __already_partitioned);
}

// Similar to the above function.  Elements equivalent to the pivot are put to
// the left of the pivot.  Returns the iterator to the pivot element.
template <class _AlgPolicy, class _RandomAccessIterator, class _Compare>
_LIBCPP_HIDE_FROM_ABI _RandomAccessIterator
__partition_with_equals_on_left(_RandomAccessIterator __first, _RandomAccessIterator __last, _Compare __comp) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  typedef typename std::iterator_traits<_RandomAccessIterator>::value_type value_type;
  const _RandomAccessIterator __begin = __first; // used for bounds checking, those are not moved around
  const _RandomAccessIterator __end   = __last;
  (void)__end; //
  value_type __pivot(_Ops::__iter_move(__first));
  if (__comp(__pivot, *(__last - difference_type(1)))) {
    // Guarded.
    do {
      ++__first;
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __first != __end,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
    } while (!__comp(__pivot, *__first));
  } else {
    while (++__first < __last && !__comp(__pivot, *__first)) {
    }
  }

  if (__first < __last) {
    // It will be always guarded because __introsort will do the
    // median-of-three before calling this.
    do {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __last != __begin,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      --__last;
    } while (__comp(__pivot, *__last));
  }
  while (__first < __last) {
    _Ops::iter_swap(__first, __last);
    do {
      ++__first;
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __first != __end,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
    } while (!__comp(__pivot, *__first));
    do {
      _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(
          __last != __begin,
          "Would read out of bounds, does your comparator satisfy the strict-weak ordering requirement?");
      --__last;
    } while (__comp(__pivot, *__last));
  }
  _RandomAccessIterator __pivot_pos = __first - difference_type(1);
  if (__begin != __pivot_pos) {
    *__begin = _Ops::__iter_move(__pivot_pos);
  }
  *__pivot_pos = std::move(__pivot);
  return __first;
}

// The main sorting function.  Implements introsort combined with other ideas:
//  - option of using block quick sort for partitioning,
//  - guarded and unguarded insertion sort for small lengths,
//  - Tuckey's ninther technique for computing the pivot,
//  - check on whether partition was not required.
// The implementation is partly based on Orson Peters' pattern-defeating
// quicksort, published at: <https://github.com/orlp/pdqsort>.
template <class _AlgPolicy, class _Compare, class _RandomAccessIterator, bool _UseBitSetPartition>
void __introsort(_RandomAccessIterator __first,
                 _RandomAccessIterator __last,
                 _Compare __comp,
                 typename iterator_traits<_RandomAccessIterator>::difference_type __depth,
                 bool __leftmost = true) {
  using _Ops = _IterOps<_AlgPolicy>;
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  using _Comp_ref = __comp_ref_type<_Compare>;
  // Upper bound for using insertion sort for sorting.
  _LIBCPP_CONSTEXPR difference_type __limit = 24;
  // Lower bound for using Tuckey's ninther technique for median computation.
  _LIBCPP_CONSTEXPR difference_type __ninther_threshold = 128;
  while (true) {
    difference_type __len = __last - __first;
    switch (__len) {
    case 0:
    case 1:
      return;
    case 2:
      if (__comp(*--__last, *__first))
        _Ops::iter_swap(__first, __last);
      return;
    case 3:
      std::__sort3_maybe_branchless<_AlgPolicy, _Compare>(__first, __first + difference_type(1), --__last, __comp);
      return;
    case 4:
      std::__sort4_maybe_branchless<_AlgPolicy, _Compare>(
          __first, __first + difference_type(1), __first + difference_type(2), --__last, __comp);
      return;
    case 5:
      std::__sort5_maybe_branchless<_AlgPolicy, _Compare>(
          __first,
          __first + difference_type(1),
          __first + difference_type(2),
          __first + difference_type(3),
          --__last,
          __comp);
      return;
    }
    // Use insertion sort if the length of the range is below the specified limit.
    if (__len < __limit) {
      if (__leftmost) {
        std::__insertion_sort<_AlgPolicy, _Compare>(__first, __last, __comp);
      } else {
        std::__insertion_sort_unguarded<_AlgPolicy, _Compare>(__first, __last, __comp);
      }
      return;
    }
    if (__depth == 0) {
      // Fallback to heap sort as Introsort suggests.
      std::__partial_sort<_AlgPolicy, _Compare>(__first, __last, __last, __comp);
      return;
    }
    --__depth;
    {
      difference_type __half_len = __len / 2;
      // Use Tuckey's ninther technique or median of 3 for pivot selection
      // depending on the length of the range being sorted.
      if (__len > __ninther_threshold) {
        std::__sort3<_AlgPolicy, _Compare>(__first, __first + __half_len, __last - difference_type(1), __comp);
        std::__sort3<_AlgPolicy, _Compare>(
            __first + difference_type(1), __first + (__half_len - 1), __last - difference_type(2), __comp);
        std::__sort3<_AlgPolicy, _Compare>(
            __first + difference_type(2), __first + (__half_len + 1), __last - difference_type(3), __comp);
        std::__sort3<_AlgPolicy, _Compare>(
            __first + (__half_len - 1), __first + __half_len, __first + (__half_len + 1), __comp);
        _Ops::iter_swap(__first, __first + __half_len);
      } else {
        std::__sort3<_AlgPolicy, _Compare>(__first + __half_len, __first, __last - difference_type(1), __comp);
      }
    }
    // The elements to the left of the current iterator range are already
    // sorted.  If the current iterator range to be sorted is not the
    // leftmost part of the entire iterator range and the pivot is same as
    // the highest element in the range to the left, then we know that all
    // the elements in the range [first, pivot] would be equal to the pivot,
    // assuming the equal elements are put on the left side when
    // partitioned.  This also means that we do not need to sort the left
    // side of the partition.
    if (!__leftmost && !__comp(*(__first - difference_type(1)), *__first)) {
      __first = std::__partition_with_equals_on_left<_AlgPolicy, _RandomAccessIterator, _Comp_ref>(
          __first, __last, _Comp_ref(__comp));
      continue;
    }
    // Use bitset partition only if asked for.
    auto __ret                = _UseBitSetPartition
                                  ? std::__bitset_partition<_AlgPolicy, _RandomAccessIterator, _Compare>(__first, __last, __comp)
                                  : std::__partition_with_equals_on_right<_AlgPolicy, _RandomAccessIterator, _Compare>(
                         __first, __last, __comp);
    _RandomAccessIterator __i = __ret.first;
    // [__first, __i) < *__i and *__i <= [__i+1, __last)
    // If we were given a perfect partition, see if insertion sort is quick...
    if (__ret.second) {
      bool __fs = std::__insertion_sort_incomplete<_AlgPolicy, _Compare>(__first, __i, __comp);
      if (std::__insertion_sort_incomplete<_AlgPolicy, _Compare>(__i + difference_type(1), __last, __comp)) {
        if (__fs)
          return;
        __last = __i;
        continue;
      } else {
        if (__fs) {
          __first = ++__i;
          continue;
        }
      }
    }
    // Sort the left partiton recursively and the right partition with tail recursion elimination.
    std::__introsort<_AlgPolicy, _Compare, _RandomAccessIterator, _UseBitSetPartition>(
        __first, __i, __comp, __depth, __leftmost);
    __leftmost = false;
    __first    = ++__i;
  }
}

template <typename _Number>
inline _LIBCPP_HIDE_FROM_ABI _Number __log2i(_Number __n) {
  if (__n == 0)
    return 0;
  if (sizeof(__n) <= sizeof(unsigned))
    return sizeof(unsigned) * CHAR_BIT - 1 - __libcpp_clz(static_cast<unsigned>(__n));
  if (sizeof(__n) <= sizeof(unsigned long))
    return sizeof(unsigned long) * CHAR_BIT - 1 - __libcpp_clz(static_cast<unsigned long>(__n));
  if (sizeof(__n) <= sizeof(unsigned long long))
    return sizeof(unsigned long long) * CHAR_BIT - 1 - __libcpp_clz(static_cast<unsigned long long>(__n));

  _Number __log2 = 0;
  while (__n > 1) {
    __log2++;
    __n >>= 1;
  }
  return __log2;
}

template <class _Comp, class _RandomAccessIterator>
void __sort(_RandomAccessIterator, _RandomAccessIterator, _Comp);

extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<char>&, char*>(char*, char*, __less<char>&);
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<wchar_t>&, wchar_t*>(wchar_t*, wchar_t*, __less<wchar_t>&);
#endif
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<signed char>&, signed char*>(signed char*, signed char*, __less<signed char>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<unsigned char>&, unsigned char*>(unsigned char*, unsigned char*, __less<unsigned char>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<short>&, short*>(short*, short*, __less<short>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<unsigned short>&, unsigned short*>(unsigned short*, unsigned short*, __less<unsigned short>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<int>&, int*>(int*, int*, __less<int>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<unsigned>&, unsigned*>(unsigned*, unsigned*, __less<unsigned>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<long>&, long*>(long*, long*, __less<long>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<unsigned long>&, unsigned long*>(unsigned long*, unsigned long*, __less<unsigned long>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<long long>&, long long*>(long long*, long long*, __less<long long>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<unsigned long long>&, unsigned long long*>(
    unsigned long long*, unsigned long long*, __less<unsigned long long>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<float>&, float*>(float*, float*, __less<float>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void __sort<__less<double>&, double*>(double*, double*, __less<double>&);
extern template _LIBCPP_EXPORTED_FROM_ABI void
__sort<__less<long double>&, long double*>(long double*, long double*, __less<long double>&);

template <class _AlgPolicy, class _RandomAccessIterator, class _Comp>
_LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__sort_dispatch(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp& __comp) {
  typedef typename iterator_traits<_RandomAccessIterator>::difference_type difference_type;
  difference_type __depth_limit = 2 * std::__log2i(__last - __first);

  // Only use bitset partitioning for arithmetic types.  We should also check
  // that the default comparator is in use so that we are sure that there are no
  // branches in the comparator.
  std::__introsort<_AlgPolicy,
                   _Comp&,
                   _RandomAccessIterator,
                   __use_branchless_sort<_Comp, _RandomAccessIterator>::value>(__first, __last, __comp, __depth_limit);
}

template <class _Type, class... _Options>
using __is_any_of = _Or<is_same<_Type, _Options>...>;

template <class _Type>
using __sort_is_specialized_in_library = __is_any_of<
    _Type,
    char,
#ifndef _LIBCPP_HAS_NO_WIDE_CHARACTERS
    wchar_t,
#endif
    signed char,
    unsigned char,
    short,
    unsigned short,
    int,
    unsigned int,
    long,
    unsigned long,
    long long,
    unsigned long long,
    float,
    double,
    long double>;

template <class _AlgPolicy, class _Type, __enable_if_t<__sort_is_specialized_in_library<_Type>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI void __sort_dispatch(_Type* __first, _Type* __last, __less<>&) {
  __less<_Type> __comp;
  std::__sort<__less<_Type>&, _Type*>(__first, __last, __comp);
}

template <class _AlgPolicy, class _Type, __enable_if_t<__sort_is_specialized_in_library<_Type>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI void __sort_dispatch(_Type* __first, _Type* __last, less<_Type>&) {
  __less<_Type> __comp;
  std::__sort<__less<_Type>&, _Type*>(__first, __last, __comp);
}

#if _LIBCPP_STD_VER >= 14
template <class _AlgPolicy, class _Type, __enable_if_t<__sort_is_specialized_in_library<_Type>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI void __sort_dispatch(_Type* __first, _Type* __last, less<>&) {
  __less<_Type> __comp;
  std::__sort<__less<_Type>&, _Type*>(__first, __last, __comp);
}
#endif

#if _LIBCPP_STD_VER >= 20
template <class _AlgPolicy, class _Type, __enable_if_t<__sort_is_specialized_in_library<_Type>::value, int> = 0>
_LIBCPP_HIDE_FROM_ABI void __sort_dispatch(_Type* __first, _Type* __last, ranges::less&) {
  __less<_Type> __comp;
  std::__sort<__less<_Type>&, _Type*>(__first, __last, __comp);
}
#endif

template <class _AlgPolicy, class _RandomAccessIterator, class _Comp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
__sort_impl(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp& __comp) {
  std::__debug_randomize_range<_AlgPolicy>(__first, __last);

  if (__libcpp_is_constant_evaluated()) {
    std::__partial_sort<_AlgPolicy>(
        std::__unwrap_iter(__first), std::__unwrap_iter(__last), std::__unwrap_iter(__last), __comp);
  } else {
    std::__sort_dispatch<_AlgPolicy>(std::__unwrap_iter(__first), std::__unwrap_iter(__last), __comp);
  }
  std::__check_strict_weak_ordering_sorted(std::__unwrap_iter(__first), std::__unwrap_iter(__last), __comp);
}

template <class _RandomAccessIterator, class _Comp>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
sort(_RandomAccessIterator __first, _RandomAccessIterator __last, _Comp __comp) {
  std::__sort_impl<_ClassicAlgPolicy>(std::move(__first), std::move(__last), __comp);
}

template <class _RandomAccessIterator>
inline _LIBCPP_HIDE_FROM_ABI _LIBCPP_CONSTEXPR_SINCE_CXX20 void
sort(_RandomAccessIterator __first, _RandomAccessIterator __last) {
  std::sort(__first, __last, __less<>());
}

_LIBCPP_END_NAMESPACE_STD

_LIBCPP_POP_MACROS

#endif // _LIBCPP___ALGORITHM_SORT_H
