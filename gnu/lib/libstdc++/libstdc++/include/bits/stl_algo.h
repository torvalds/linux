// Algorithm implementation -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

/*
 *
 * Copyright (c) 1994
 * Hewlett-Packard Company
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Hewlett-Packard Company makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 *
 * Copyright (c) 1996
 * Silicon Graphics Computer Systems, Inc.
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file stl_algo.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_ALGO_H
#define __GLIBCPP_INTERNAL_ALGO_H

#include <bits/stl_heap.h>
#include <bits/stl_tempbuf.h>     // for _Temporary_buffer

// See concept_check.h for the __glibcpp_*_requires macros.

namespace std
{

  /**
   *  @brief Find the median of three values.
   *  @param  a  A value.
   *  @param  b  A value.
   *  @param  c  A value.
   *  @return One of @p a, @p b or @p c.
   *
   *  If @c {l,m,n} is some convolution of @p {a,b,c} such that @c l<=m<=n
   *  then the value returned will be @c m.
   *  This is an SGI extension.
   *  @ingroup SGIextensions
  */
  template<typename _Tp>
  inline const _Tp&
    __median(const _Tp& __a, const _Tp& __b, const _Tp& __c)
    {
      // concept requirements
      __glibcpp_function_requires(_LessThanComparableConcept<_Tp>)
      if (__a < __b)
	if (__b < __c)
	  return __b;
	else if (__a < __c)
	  return __c;
	else
	  return __a;
      else if (__a < __c)
	return __a;
      else if (__b < __c)
	return __c;
      else
	return __b;
    }

  /**
   *  @brief Find the median of three values using a predicate for comparison.
   *  @param  a     A value.
   *  @param  b     A value.
   *  @param  c     A value.
   *  @param  comp  A binary predicate.
   *  @return One of @p a, @p b or @p c.
   *
   *  If @c {l,m,n} is some convolution of @p {a,b,c} such that @p comp(l,m)
   *  and @p comp(m,n) are both true then the value returned will be @c m.
   *  This is an SGI extension.
   *  @ingroup SGIextensions
  */
  template<typename _Tp, typename _Compare>
    inline const _Tp&
    __median(const _Tp& __a, const _Tp& __b, const _Tp& __c, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_BinaryFunctionConcept<_Compare,bool,_Tp,_Tp>)
      if (__comp(__a, __b))
	if (__comp(__b, __c))
	  return __b;
	else if (__comp(__a, __c))
	  return __c;
	else
	  return __a;
      else if (__comp(__a, __c))
	return __a;
      else if (__comp(__b, __c))
	return __c;
      else
	return __b;
    }

  /**
   *  @brief Apply a function to every element of a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  f      A unary function object.
   *  @return   @p f.
   *
   *  Applies the function object @p f to each element in the range
   *  @p [first,last).  @p f must not modify the order of the sequence.
   *  If @p f has a return value it is ignored.
  */
  template<typename _InputIter, typename _Function>
    _Function
    for_each(_InputIter __first, _InputIter __last, _Function __f)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      for ( ; __first != __last; ++__first)
	__f(*__first);
      return __f;
    }

  /**
   *  @if maint
   *  This is an overload used by find() for the Input Iterator case.
   *  @endif
  */
  template<typename _InputIter, typename _Tp>
    inline _InputIter
    find(_InputIter __first, _InputIter __last,
	 const _Tp& __val,
	 input_iterator_tag)
    {
      while (__first != __last && !(*__first == __val))
	++__first;
      return __first;
    }

  /**
   *  @if maint
   *  This is an overload used by find_if() for the Input Iterator case.
   *  @endif
  */
  template<typename _InputIter, typename _Predicate>
    inline _InputIter
    find_if(_InputIter __first, _InputIter __last,
	    _Predicate __pred,
	    input_iterator_tag)
    {
      while (__first != __last && !__pred(*__first))
	++__first;
      return __first;
    }

  /**
   *  @if maint
   *  This is an overload used by find() for the RAI case.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Tp>
    _RandomAccessIter
    find(_RandomAccessIter __first, _RandomAccessIter __last,
	 const _Tp& __val,
	 random_access_iterator_tag)
    {
      typename iterator_traits<_RandomAccessIter>::difference_type __trip_count
	= (__last - __first) >> 2;

      for ( ; __trip_count > 0 ; --__trip_count) {
	if (*__first == __val) return __first;
	++__first;

	if (*__first == __val) return __first;
	++__first;

	if (*__first == __val) return __first;
	++__first;

	if (*__first == __val) return __first;
	++__first;
      }

      switch(__last - __first) {
      case 3:
	if (*__first == __val) return __first;
	++__first;
      case 2:
	if (*__first == __val) return __first;
	++__first;
      case 1:
	if (*__first == __val) return __first;
	++__first;
      case 0:
      default:
	return __last;
      }
    }

  /**
   *  @if maint
   *  This is an overload used by find_if() for the RAI case.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Predicate>
    _RandomAccessIter
    find_if(_RandomAccessIter __first, _RandomAccessIter __last,
	    _Predicate __pred,
	    random_access_iterator_tag)
    {
      typename iterator_traits<_RandomAccessIter>::difference_type __trip_count
	= (__last - __first) >> 2;

      for ( ; __trip_count > 0 ; --__trip_count) {
	if (__pred(*__first)) return __first;
	++__first;

	if (__pred(*__first)) return __first;
	++__first;

	if (__pred(*__first)) return __first;
	++__first;

	if (__pred(*__first)) return __first;
	++__first;
      }

      switch(__last - __first) {
      case 3:
	if (__pred(*__first)) return __first;
	++__first;
      case 2:
	if (__pred(*__first)) return __first;
	++__first;
      case 1:
	if (__pred(*__first)) return __first;
	++__first;
      case 0:
      default:
	return __last;
      }
    }

  /**
   *  @brief Find the first occurrence of a value in a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  val    The value to find.
   *  @return   The first iterator @c i in the range @p [first,last)
   *  such that @c *i == @p val, or @p last if no such iterator exists.
  */
  template<typename _InputIter, typename _Tp>
    inline _InputIter
    find(_InputIter __first, _InputIter __last,
	 const _Tp& __val)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_EqualOpConcept<
		typename iterator_traits<_InputIter>::value_type, _Tp>)
      return find(__first, __last, __val, __iterator_category(__first));
    }

  /**
   *  @brief Find the first element in a sequence for which a predicate is true.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  pred   A predicate.
   *  @return   The first iterator @c i in the range @p [first,last)
   *  such that @p pred(*i) is true, or @p last if no such iterator exists.
  */
  template<typename _InputIter, typename _Predicate>
    inline _InputIter
    find_if(_InputIter __first, _InputIter __last,
	    _Predicate __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	      typename iterator_traits<_InputIter>::value_type>)
      return find_if(__first, __last, __pred, __iterator_category(__first));
    }

  /**
   *  @brief Find two adjacent values in a sequence that are equal.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @return   The first iterator @c i such that @c i and @c i+1 are both
   *  valid iterators in @p [first,last) and such that @c *i == @c *(i+1),
   *  or @p last if no such iterator exists.
  */
  template<typename _ForwardIter>
    _ForwardIter
    adjacent_find(_ForwardIter __first, _ForwardIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_ForwardIter>::value_type>)
      if (__first == __last)
	return __last;
      _ForwardIter __next = __first;
      while(++__next != __last) {
	if (*__first == *__next)
	  return __first;
	__first = __next;
      }
      return __last;
    }

  /**
   *  @brief Find two adjacent values in a sequence using a predicate.
   *  @param  first         A forward iterator.
   *  @param  last          A forward iterator.
   *  @param  binary_pred   A binary predicate.
   *  @return   The first iterator @c i such that @c i and @c i+1 are both
   *  valid iterators in @p [first,last) and such that
   *  @p binary_pred(*i,*(i+1)) is true, or @p last if no such iterator
   *  exists.
  */
  template<typename _ForwardIter, typename _BinaryPredicate>
    _ForwardIter
    adjacent_find(_ForwardIter __first, _ForwardIter __last,
		  _BinaryPredicate __binary_pred)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)
      if (__first == __last)
	return __last;
      _ForwardIter __next = __first;
      while(++__next != __last) {
	if (__binary_pred(*__first, *__next))
	  return __first;
	__first = __next;
      }
      return __last;
    }

  /**
   *  @brief Count the number of copies of a value in a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  value  The value to be counted.
   *  @return   The number of iterators @c i in the range @p [first,last)
   *  for which @c *i == @p value
  */
  template<typename _InputIter, typename _Tp>
    typename iterator_traits<_InputIter>::difference_type
    count(_InputIter __first, _InputIter __last, const _Tp& __value)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_InputIter>::value_type >)
      __glibcpp_function_requires(_EqualityComparableConcept<_Tp>)
      typename iterator_traits<_InputIter>::difference_type __n = 0;
      for ( ; __first != __last; ++__first)
	if (*__first == __value)
	  ++__n;
      return __n;
    }

  /**
   *  @brief Count the elements of a sequence for which a predicate is true.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  pred   A predicate.
   *  @return   The number of iterators @c i in the range @p [first,last)
   *  for which @p pred(*i) is true.
  */
  template<typename _InputIter, typename _Predicate>
    typename iterator_traits<_InputIter>::difference_type
    count_if(_InputIter __first, _InputIter __last, _Predicate __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIter>::value_type>)
      typename iterator_traits<_InputIter>::difference_type __n = 0;
      for ( ; __first != __last; ++__first)
	if (__pred(*__first))
	  ++__n;
      return __n;
    }


  /**
   *  @brief Search a sequence for a matching sub-sequence.
   *  @param  first1  A forward iterator.
   *  @param  last1   A forward iterator.
   *  @param  first2  A forward iterator.
   *  @param  last2   A forward iterator.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that @c *(i+N) == @p *(first2+N)
   *  for each @c N in the range @p [0,last2-first2), or @p last1 if no
   *  such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2) and
   *  returns an iterator to the first element of the sub-sequence, or
   *  @p last1 if the sub-sequence is not found.
   *
   *  Because the sub-sequence must lie completely within the range
   *  @p [first1,last1) it must start at a position less than
   *  @p last1-(last2-first2) where @p last2-first2 is the length of the
   *  sub-sequence.
   *  This means that the returned iterator @c i will be in the range
   *  @p [first1,last1-(last2-first2))
  */
  template<typename _ForwardIter1, typename _ForwardIter2>
    _ForwardIter1
    search(_ForwardIter1 __first1, _ForwardIter1 __last1,
	   _ForwardIter2 __first2, _ForwardIter2 __last2)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter1>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter2>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIter1>::value_type,
	    typename iterator_traits<_ForwardIter2>::value_type>)

      // Test for empty ranges
      if (__first1 == __last1 || __first2 == __last2)
	return __first1;

      // Test for a pattern of length 1.
      _ForwardIter2 __tmp(__first2);
      ++__tmp;
      if (__tmp == __last2)
	return find(__first1, __last1, *__first2);

      // General case.

      _ForwardIter2 __p1, __p;

      __p1 = __first2; ++__p1;

      _ForwardIter1 __current = __first1;

      while (__first1 != __last1) {
	__first1 = find(__first1, __last1, *__first2);
	if (__first1 == __last1)
	  return __last1;

	__p = __p1;
	__current = __first1;
	if (++__current == __last1)
	  return __last1;

	while (*__current == *__p) {
	  if (++__p == __last2)
	    return __first1;
	  if (++__current == __last1)
	    return __last1;
	}

	++__first1;
      }
      return __first1;
    }

  /**
   *  @brief Search a sequence for a matching sub-sequence using a predicate.
   *  @param  first1     A forward iterator.
   *  @param  last1      A forward iterator.
   *  @param  first2     A forward iterator.
   *  @param  last2      A forward iterator.
   *  @param  predicate  A binary predicate.
   *  @return   The first iterator @c i in the range
   *  @p [first1,last1-(last2-first2)) such that
   *  @p predicate(*(i+N),*(first2+N)) is true for each @c N in the range
   *  @p [0,last2-first2), or @p last1 if no such iterator exists.
   *
   *  Searches the range @p [first1,last1) for a sub-sequence that compares
   *  equal value-by-value with the sequence given by @p [first2,last2),
   *  using @p predicate to determine equality, and returns an iterator
   *  to the first element of the sub-sequence, or @p last1 if no such
   *  iterator exists.
   *
   *  @see search(_ForwardIter1, _ForwardIter1, _ForwardIter2, _ForwardIter2)
  */
  template<typename _ForwardIter1, typename _ForwardIter2, typename _BinaryPred>
    _ForwardIter1
    search(_ForwardIter1 __first1, _ForwardIter1 __last1,
	   _ForwardIter2 __first2, _ForwardIter2 __last2,
	   _BinaryPred  __predicate)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter1>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter2>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPred,
	    typename iterator_traits<_ForwardIter1>::value_type,
	    typename iterator_traits<_ForwardIter2>::value_type>)

      // Test for empty ranges
      if (__first1 == __last1 || __first2 == __last2)
	return __first1;

      // Test for a pattern of length 1.
      _ForwardIter2 __tmp(__first2);
      ++__tmp;
      if (__tmp == __last2) {
	while (__first1 != __last1 && !__predicate(*__first1, *__first2))
	  ++__first1;
	return __first1;
      }

      // General case.

      _ForwardIter2 __p1, __p;

      __p1 = __first2; ++__p1;

      _ForwardIter1 __current = __first1;

      while (__first1 != __last1) {
	while (__first1 != __last1) {
	  if (__predicate(*__first1, *__first2))
	    break;
	  ++__first1;
	}
	while (__first1 != __last1 && !__predicate(*__first1, *__first2))
	  ++__first1;
	if (__first1 == __last1)
	  return __last1;

	__p = __p1;
	__current = __first1;
	if (++__current == __last1) return __last1;

	while (__predicate(*__current, *__p)) {
	  if (++__p == __last2)
	    return __first1;
	  if (++__current == __last1)
	    return __last1;
	}

	++__first1;
      }
      return __first1;
    }

  /**
   *  @brief Search a sequence for a number of consecutive values.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  count  The number of consecutive values.
   *  @param  val    The value to find.
   *  @return   The first iterator @c i in the range @p [first,last-count)
   *  such that @c *(i+N) == @p val for each @c N in the range @p [0,count),
   *  or @p last if no such iterator exists.
   *
   *  Searches the range @p [first,last) for @p count consecutive elements
   *  equal to @p val.
  */
  template<typename _ForwardIter, typename _Integer, typename _Tp>
    _ForwardIter
    search_n(_ForwardIter __first, _ForwardIter __last,
	     _Integer __count, const _Tp& __val)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_ForwardIter>::value_type>)
      __glibcpp_function_requires(_EqualityComparableConcept<_Tp>)

      if (__count <= 0)
	return __first;
      else {
	__first = find(__first, __last, __val);
	while (__first != __last) {
	  typename iterator_traits<_ForwardIter>::difference_type __n = __count;
	  --__n;
	  _ForwardIter __i = __first;
	  ++__i;
	  while (__i != __last && __n != 0 && *__i == __val) {
	    ++__i;
	    --__n;
	  }
	  if (__n == 0)
	    return __first;
	  else
	    __first = find(__i, __last, __val);
	}
	return __last;
      }
    }

  /**
   *  @brief Search a sequence for a number of consecutive values using a
   *         predicate.
   *  @param  first        A forward iterator.
   *  @param  last         A forward iterator.
   *  @param  count        The number of consecutive values.
   *  @param  val          The value to find.
   *  @param  binary_pred  A binary predicate.
   *  @return   The first iterator @c i in the range @p [first,last-count)
   *  such that @p binary_pred(*(i+N),val) is true for each @c N in the
   *  range @p [0,count), or @p last if no such iterator exists.
   *
   *  Searches the range @p [first,last) for @p count consecutive elements
   *  for which the predicate returns true.
  */
  template<typename _ForwardIter, typename _Integer, typename _Tp,
           typename _BinaryPred>
    _ForwardIter
    search_n(_ForwardIter __first, _ForwardIter __last,
	     _Integer __count, const _Tp& __val,
	     _BinaryPred __binary_pred)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPred,
	    typename iterator_traits<_ForwardIter>::value_type, _Tp>)

      if (__count <= 0)
	return __first;
      else {
	while (__first != __last) {
	  if (__binary_pred(*__first, __val))
	    break;
	  ++__first;
	}
	while (__first != __last) {
	  typename iterator_traits<_ForwardIter>::difference_type __n = __count;
	  --__n;
	  _ForwardIter __i = __first;
	  ++__i;
	  while (__i != __last && __n != 0 && __binary_pred(*__i, __val)) {
	    ++__i;
	    --__n;
	  }
	  if (__n == 0)
	    return __first;
	  else {
	    while (__i != __last) {
	      if (__binary_pred(*__i, __val))
		break;
	      ++__i;
	    }
	    __first = __i;
	  }
	}
	return __last;
      }
    }

  /**
   *  @brief Swap the elements of two sequences.
   *  @param  first1  A forward iterator.
   *  @param  last1   A forward iterator.
   *  @param  first2  A forward iterator.
   *  @return   An iterator equal to @p first2+(last1-first1).
   *
   *  Swaps each element in the range @p [first1,last1) with the
   *  corresponding element in the range @p [first2,(last1-first1)).
   *  The ranges must not overlap.
  */
  template<typename _ForwardIter1, typename _ForwardIter2>
    _ForwardIter2
    swap_ranges(_ForwardIter1 __first1, _ForwardIter1 __last1,
		_ForwardIter2 __first2)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter1>)
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter2>)
      __glibcpp_function_requires(_ConvertibleConcept<
	    typename iterator_traits<_ForwardIter1>::value_type,
	    typename iterator_traits<_ForwardIter2>::value_type>)
      __glibcpp_function_requires(_ConvertibleConcept<
	    typename iterator_traits<_ForwardIter2>::value_type,
	    typename iterator_traits<_ForwardIter1>::value_type>)

      for ( ; __first1 != __last1; ++__first1, ++__first2)
	iter_swap(__first1, __first2);
      return __first2;
    }

  /**
   *  @brief Perform an operation on a sequence.
   *  @param  first     An input iterator.
   *  @param  last      An input iterator.
   *  @param  result    An output iterator.
   *  @param  unary_op  A unary operator.
   *  @return   An output iterator equal to @p result+(last-first).
   *
   *  Applies the operator to each element in the input range and assigns
   *  the results to successive elements of the output sequence.
   *  Evaluates @p *(result+N)=unary_op(*(first+N)) for each @c N in the
   *  range @p [0,last-first).
   *
   *  @p unary_op must not alter its argument.
  */
  template<typename _InputIter, typename _OutputIter, typename _UnaryOperation>
    _OutputIter
    transform(_InputIter __first, _InputIter __last,
	      _OutputIter __result, _UnaryOperation __unary_op)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
            // "the type returned by a _UnaryOperation"
            __typeof__(__unary_op(*__first))>)

      for ( ; __first != __last; ++__first, ++__result)
	*__result = __unary_op(*__first);
      return __result;
    }

  /**
   *  @brief Perform an operation on corresponding elements of two sequences.
   *  @param  first1     An input iterator.
   *  @param  last1      An input iterator.
   *  @param  first2     An input iterator.
   *  @param  result     An output iterator.
   *  @param  binary_op  A binary operator.
   *  @return   An output iterator equal to @p result+(last-first).
   *
   *  Applies the operator to the corresponding elements in the two
   *  input ranges and assigns the results to successive elements of the
   *  output sequence.
   *  Evaluates @p *(result+N)=binary_op(*(first1+N),*(first2+N)) for each
   *  @c N in the range @p [0,last1-first1).
   *
   *  @p binary_op must not alter either of its arguments.
  */
  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _BinaryOperation>
    _OutputIter
    transform(_InputIter1 __first1, _InputIter1 __last1,
	      _InputIter2 __first2, _OutputIter __result,
	      _BinaryOperation __binary_op)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
            // "the type returned by a _BinaryOperation"
            __typeof__(__binary_op(*__first1,*__first2))>)

      for ( ; __first1 != __last1; ++__first1, ++__first2, ++__result)
	*__result = __binary_op(*__first1, *__first2);
      return __result;
    }

  /**
   *  @brief Replace each occurrence of one value in a sequence with another
   *         value.
   *  @param  first      A forward iterator.
   *  @param  last       A forward iterator.
   *  @param  old_value  The value to be replaced.
   *  @param  new_value  The replacement value.
   *  @return   replace() returns no value.
   *
   *  For each iterator @c i in the range @p [first,last) if @c *i ==
   *  @p old_value then the assignment @c *i = @p new_value is performed.
  */
  template<typename _ForwardIter, typename _Tp>
    void
    replace(_ForwardIter __first, _ForwardIter __last,
	    const _Tp& __old_value, const _Tp& __new_value)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIter>::value_type, _Tp>)
      __glibcpp_function_requires(_ConvertibleConcept<_Tp,
	    typename iterator_traits<_ForwardIter>::value_type>)

      for ( ; __first != __last; ++__first)
	if (*__first == __old_value)
	  *__first = __new_value;
    }

  /**
   *  @brief Replace each value in a sequence for which a predicate returns
   *         true with another value.
   *  @param  first      A forward iterator.
   *  @param  last       A forward iterator.
   *  @param  pred       A predicate.
   *  @param  new_value  The replacement value.
   *  @return   replace_if() returns no value.
   *
   *  For each iterator @c i in the range @p [first,last) if @p pred(*i)
   *  is true then the assignment @c *i = @p new_value is performed.
  */
  template<typename _ForwardIter, typename _Predicate, typename _Tp>
    void
    replace_if(_ForwardIter __first, _ForwardIter __last,
	       _Predicate __pred, const _Tp& __new_value)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_ConvertibleConcept<_Tp,
	    typename iterator_traits<_ForwardIter>::value_type>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIter>::value_type>)

      for ( ; __first != __last; ++__first)
	if (__pred(*__first))
	  *__first = __new_value;
    }

  /**
   *  @brief Copy a sequence, replacing each element of one value with another
   *         value.
   *  @param  first      An input iterator.
   *  @param  last       An input iterator.
   *  @param  result     An output iterator.
   *  @param  old_value  The value to be replaced.
   *  @param  new_value  The replacement value.
   *  @return   The end of the output sequence, @p result+(last-first).
   *
   *  Copies each element in the input range @p [first,last) to the
   *  output range @p [result,result+(last-first)) replacing elements
   *  equal to @p old_value with @p new_value.
  */
  template<typename _InputIter, typename _OutputIter, typename _Tp>
    _OutputIter
    replace_copy(_InputIter __first, _InputIter __last,
		 _OutputIter __result,
		 const _Tp& __old_value, const _Tp& __new_value)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIter>::value_type, _Tp>)

      for ( ; __first != __last; ++__first, ++__result)
	*__result = *__first == __old_value ? __new_value : *__first;
      return __result;
    }

  /**
   *  @brief Copy a sequence, replacing each value for which a predicate
   *         returns true with another value.
   *  @param  first      An input iterator.
   *  @param  last       An input iterator.
   *  @param  result     An output iterator.
   *  @param  pred       A predicate.
   *  @param  new_value  The replacement value.
   *  @return   The end of the output sequence, @p result+(last-first).
   *
   *  Copies each element in the range @p [first,last) to the range
   *  @p [result,result+(last-first)) replacing elements for which
   *  @p pred returns true with @p new_value.
  */
  template<typename _InputIter, typename _OutputIter, typename _Predicate,
           typename _Tp>
    _OutputIter
    replace_copy_if(_InputIter __first, _InputIter __last,
		    _OutputIter __result,
		    _Predicate __pred, const _Tp& __new_value)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIter>::value_type>)

      for ( ; __first != __last; ++__first, ++__result)
	*__result = __pred(*__first) ? __new_value : *__first;
      return __result;
    }

  /**
   *  @brief Assign the result of a function object to each value in a
   *         sequence.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  gen    A function object taking no arguments.
   *  @return   generate() returns no value.
   *
   *  Performs the assignment @c *i = @p gen() for each @c i in the range
   *  @p [first,last).
  */
  template<typename _ForwardIter, typename _Generator>
    void
    generate(_ForwardIter __first, _ForwardIter __last, _Generator __gen)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_GeneratorConcept<_Generator,
	    typename iterator_traits<_ForwardIter>::value_type>)

      for ( ; __first != __last; ++__first)
	*__first = __gen();
    }

  /**
   *  @brief Assign the result of a function object to each value in a
   *         sequence.
   *  @param  first  A forward iterator.
   *  @param  n      The length of the sequence.
   *  @param  gen    A function object taking no arguments.
   *  @return   The end of the sequence, @p first+n
   *
   *  Performs the assignment @c *i = @p gen() for each @c i in the range
   *  @p [first,first+n).
  */
  template<typename _OutputIter, typename _Size, typename _Generator>
    _OutputIter
    generate_n(_OutputIter __first, _Size __n, _Generator __gen)
    {
      // concept requirements
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
            // "the type returned by a _Generator"
            __typeof__(__gen())>)

      for ( ; __n > 0; --__n, ++__first)
	*__first = __gen();
      return __first;
    }

  /**
   *  @brief Copy a sequence, removing elements of a given value.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @param  value   The value to be removed.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) not equal to @p value
   *  to the range beginning at @p result.
   *  remove_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIter, typename _OutputIter, typename _Tp>
    _OutputIter
    remove_copy(_InputIter __first, _InputIter __last,
		_OutputIter __result, const _Tp& __value)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIter>::value_type, _Tp>)

      for ( ; __first != __last; ++__first)
	if (!(*__first == __value)) {
	  *__result = *__first;
	  ++__result;
	}
      return __result;
    }

  /**
   *  @brief Copy a sequence, removing elements for which a predicate is true.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @param  pred    A predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) for which
   *  @p pred returns true to the range beginning at @p result.
   *
   *  remove_copy_if() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIter, typename _OutputIter, typename _Predicate>
    _OutputIter
    remove_copy_if(_InputIter __first, _InputIter __last,
		   _OutputIter __result, _Predicate __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_InputIter>::value_type>)

      for ( ; __first != __last; ++__first)
	if (!__pred(*__first)) {
	  *__result = *__first;
	  ++__result;
	}
      return __result;
    }

  /**
   *  @brief Remove elements from a sequence.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  value  The value to be removed.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  All elements equal to @p value are removed from the range
   *  @p [first,last).
   *
   *  remove() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIter, typename _Tp>
    _ForwardIter
    remove(_ForwardIter __first, _ForwardIter __last,
	   const _Tp& __value)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_ConvertibleConcept<_Tp,
	    typename iterator_traits<_ForwardIter>::value_type>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIter>::value_type, _Tp>)

      __first = find(__first, __last, __value);
      _ForwardIter __i = __first;
      return __first == __last ? __first
			       : remove_copy(++__i, __last, __first, __value);
    }

  /**
   *  @brief Remove elements from a sequence using a predicate.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @param  pred   A predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  All elements for which @p pred returns true are removed from the range
   *  @p [first,last).
   *
   *  remove_if() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIter, typename _Predicate>
    _ForwardIter
    remove_if(_ForwardIter __first, _ForwardIter __last,
	      _Predicate __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIter>::value_type>)

      __first = find_if(__first, __last, __pred);
      _ForwardIter __i = __first;
      return __first == __last ? __first
			       : remove_copy_if(++__i, __last, __first, __pred);
    }

  /**
   *  @if maint
   *  This is an uglified unique_copy(_InputIter, _InputIter, _OutputIter)
   *  overloaded for output iterators.
   *  @endif
  */
  template<typename _InputIter, typename _OutputIter>
    _OutputIter
    __unique_copy(_InputIter __first, _InputIter __last,
		  _OutputIter __result,
		  output_iterator_tag)
    {
      // concept requirements -- taken care of in dispatching function
      typename iterator_traits<_InputIter>::value_type __value = *__first;
      *__result = __value;
      while (++__first != __last)
	if (!(__value == *__first)) {
	  __value = *__first;
	  *++__result = __value;
	}
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified unique_copy(_InputIter, _InputIter, _OutputIter)
   *  overloaded for forward iterators.
   *  @endif
  */
  template<typename _InputIter, typename _ForwardIter>
    _ForwardIter
    __unique_copy(_InputIter __first, _InputIter __last,
		  _ForwardIter __result,
		  forward_iterator_tag)
    {
      // concept requirements -- taken care of in dispatching function
      *__result = *__first;
      while (++__first != __last)
	if (!(*__result == *__first))
	  *++__result = *__first;
      return ++__result;
    }

  /**
   *  @brief Copy a sequence, removing consecutive duplicate values.
   *  @param  first   An input iterator.
   *  @param  last    An input iterator.
   *  @param  result  An output iterator.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) to the range
   *  beginning at @p result, except that only the first element is copied
   *  from groups of consecutive elements that compare equal.
   *  unique_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIter, typename _OutputIter>
    inline _OutputIter
    unique_copy(_InputIter __first, _InputIter __last,
		_OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)
      __glibcpp_function_requires(_EqualityComparableConcept<
	    typename iterator_traits<_InputIter>::value_type>)

      typedef typename iterator_traits<_OutputIter>::iterator_category _IterType;

      if (__first == __last) return __result;
      return __unique_copy(__first, __last, __result, _IterType());
    }

  /**
   *  @if maint
   *  This is an uglified
   *  unique_copy(_InputIter, _InputIter, _OutputIter, _BinaryPredicate)
   *  overloaded for output iterators.
   *  @endif
  */
  template<typename _InputIter, typename _OutputIter, typename _BinaryPredicate>
    _OutputIter
    __unique_copy(_InputIter __first, _InputIter __last,
		  _OutputIter __result,
		  _BinaryPredicate __binary_pred,
		  output_iterator_tag)
    {
      // concept requirements -- iterators already checked
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	  typename iterator_traits<_InputIter>::value_type,
	  typename iterator_traits<_InputIter>::value_type>)

      typename iterator_traits<_InputIter>::value_type __value = *__first;
      *__result = __value;
      while (++__first != __last)
	if (!__binary_pred(__value, *__first)) {
	  __value = *__first;
	  *++__result = __value;
	}
      return ++__result;
    }

  /**
   *  @if maint
   *  This is an uglified
   *  unique_copy(_InputIter, _InputIter, _OutputIter, _BinaryPredicate)
   *  overloaded for forward iterators.
   *  @endif
  */
  template<typename _InputIter, typename _ForwardIter, typename _BinaryPredicate>
    _ForwardIter
    __unique_copy(_InputIter __first, _InputIter __last,
		  _ForwardIter __result,
		  _BinaryPredicate __binary_pred,
		  forward_iterator_tag)
    {
      // concept requirements -- iterators already checked
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIter>::value_type,
	    typename iterator_traits<_InputIter>::value_type>)

      *__result = *__first;
      while (++__first != __last)
	if (!__binary_pred(*__result, *__first)) *++__result = *__first;
      return ++__result;
    }

  /**
   *  @brief Copy a sequence, removing consecutive values using a predicate.
   *  @param  first        An input iterator.
   *  @param  last         An input iterator.
   *  @param  result       An output iterator.
   *  @param  binary_pred  A binary predicate.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies each element in the range @p [first,last) to the range
   *  beginning at @p result, except that only the first element is copied
   *  from groups of consecutive elements for which @p binary_pred returns
   *  true.
   *  unique_copy() is stable, so the relative order of elements that are
   *  copied is unchanged.
  */
  template<typename _InputIter, typename _OutputIter, typename _BinaryPredicate>
    inline _OutputIter
    unique_copy(_InputIter __first, _InputIter __last,
		_OutputIter __result,
		_BinaryPredicate __binary_pred)
    {
      // concept requirements -- predicates checked later
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter>::value_type>)

      typedef typename iterator_traits<_OutputIter>::iterator_category _IterType;

      if (__first == __last) return __result;
      return __unique_copy(__first, __last,
__result, __binary_pred, _IterType());
    }

  /**
   *  @brief Remove consecutive duplicate values from a sequence.
   *  @param  first  A forward iterator.
   *  @param  last   A forward iterator.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Removes all but the first element from each group of consecutive
   *  values that compare equal.
   *  unique() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIter>
    _ForwardIter
    unique(_ForwardIter __first, _ForwardIter __last)
    {
	  // concept requirements
	  __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
	  __glibcpp_function_requires(_EqualityComparableConcept<
		    typename iterator_traits<_ForwardIter>::value_type>)

	  __first = adjacent_find(__first, __last);
	  return unique_copy(__first, __last, __first);
    }

  /**
   *  @brief Remove consecutive values from a sequence using a predicate.
   *  @param  first        A forward iterator.
   *  @param  last         A forward iterator.
   *  @param  binary_pred  A binary predicate.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Removes all but the first element from each group of consecutive
   *  values for which @p binary_pred returns true.
   *  unique() is stable, so the relative order of elements that are
   *  not removed is unchanged.
   *  Elements between the end of the resulting sequence and @p last
   *  are still present, but their value is unspecified.
  */
  template<typename _ForwardIter, typename _BinaryPredicate>
    _ForwardIter
    unique(_ForwardIter __first, _ForwardIter __last,
           _BinaryPredicate __binary_pred)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
		typename iterator_traits<_ForwardIter>::value_type,
		typename iterator_traits<_ForwardIter>::value_type>)

      __first = adjacent_find(__first, __last, __binary_pred);
      return unique_copy(__first, __last, __first, __binary_pred);
    }

  /**
   *  @if maint
   *  This is an uglified reverse(_BidirectionalIter, _BidirectionalIter)
   *  overloaded for bidirectional iterators.
   *  @endif
  */
  template<typename _BidirectionalIter>
    void
    __reverse(_BidirectionalIter __first, _BidirectionalIter __last,
			  bidirectional_iterator_tag)
    {
	  while (true)
	    if (__first == __last || __first == --__last)
		  return;
	    else
		  iter_swap(__first++, __last);
    }

  /**
   *  @if maint
   *  This is an uglified reverse(_BidirectionalIter, _BidirectionalIter)
   *  overloaded for bidirectional iterators.
   *  @endif
  */
  template<typename _RandomAccessIter>
    void
    __reverse(_RandomAccessIter __first, _RandomAccessIter __last,
			  random_access_iterator_tag)
    {
	  while (__first < __last)
	    iter_swap(__first++, --__last);
    }

  /**
   *  @brief Reverse a sequence.
   *  @param  first  A bidirectional iterator.
   *  @param  last   A bidirectional iterator.
   *  @return   reverse() returns no value.
   *
   *  Reverses the order of the elements in the range @p [first,last),
   *  so that the first element becomes the last etc.
   *  For every @c i such that @p 0<=i<=(last-first)/2), @p reverse()
   *  swaps @p *(first+i) and @p *(last-(i+1))
  */
  template<typename _BidirectionalIter>
    inline void
    reverse(_BidirectionalIter __first, _BidirectionalIter __last)
    {
	  // concept requirements
	  __glibcpp_function_requires(_Mutable_BidirectionalIteratorConcept<
		    _BidirectionalIter>)
	  __reverse(__first, __last, __iterator_category(__first));
    }

  /**
   *  @brief Copy a sequence, reversing its elements.
   *  @param  first   A bidirectional iterator.
   *  @param  last    A bidirectional iterator.
   *  @param  result  An output iterator.
   *  @return  An iterator designating the end of the resulting sequence.
   *
   *  Copies the elements in the range @p [first,last) to the range
   *  @p [result,result+(last-first)) such that the order of the
   *  elements is reversed.
   *  For every @c i such that @p 0<=i<=(last-first), @p reverse_copy()
   *  performs the assignment @p *(result+(last-first)-i) = *(first+i).
   *  The ranges @p [first,last) and @p [result,result+(last-first))
   *  must not overlap.
  */
  template<typename _BidirectionalIter, typename _OutputIter>
    _OutputIter
    reverse_copy(_BidirectionalIter __first, _BidirectionalIter __last,
			     _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
		typename iterator_traits<_BidirectionalIter>::value_type>)

      while (__first != __last) {
	--__last;
	*__result = *__last;
	++__result;
      }
      return __result;
    }


  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm specialized on RAIs.
   *  It returns the greatest common divisor of two integer values.
   *  @endif
  */
  template<typename _EuclideanRingElement>
    _EuclideanRingElement
    __gcd(_EuclideanRingElement __m, _EuclideanRingElement __n)
    {
      while (__n != 0) {
	_EuclideanRingElement __t = __m % __n;
	__m = __n;
	__n = __t;
      }
      return __m;
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _ForwardIter>
    void
    __rotate(_ForwardIter __first,
	     _ForwardIter __middle,
	     _ForwardIter __last,
	      forward_iterator_tag)
    {
      if ((__first == __middle) || (__last  == __middle))
	return;

      _ForwardIter __first2 = __middle;
      do {
	swap(*__first++, *__first2++);
	if (__first == __middle)
	  __middle = __first2;
      } while (__first2 != __last);

      __first2 = __middle;

      while (__first2 != __last) {
	swap(*__first++, *__first2++);
	if (__first == __middle)
	  __middle = __first2;
	else if (__first2 == __last)
	  __first2 = __middle;
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _BidirectionalIter>
    void
    __rotate(_BidirectionalIter __first,
	     _BidirectionalIter __middle,
	     _BidirectionalIter __last,
	      bidirectional_iterator_tag)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_BidirectionalIteratorConcept<
	    _BidirectionalIter>)

      if ((__first == __middle) || (__last  == __middle))
	return;

      __reverse(__first,  __middle, bidirectional_iterator_tag());
      __reverse(__middle, __last,   bidirectional_iterator_tag());

      while (__first != __middle && __middle != __last)
	swap (*__first++, *--__last);

      if (__first == __middle) {
	__reverse(__middle, __last,   bidirectional_iterator_tag());
      }
      else {
	__reverse(__first,  __middle, bidirectional_iterator_tag());
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the rotate algorithm.
   *  @endif
  */
  template<typename _RandomAccessIter>
    void
    __rotate(_RandomAccessIter __first,
	     _RandomAccessIter __middle,
	     _RandomAccessIter __last,
	     random_access_iterator_tag)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)

      if ((__first == __middle) || (__last  == __middle))
	return;

      typedef typename iterator_traits<_RandomAccessIter>::difference_type _Distance;
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      _Distance __n = __last   - __first;
      _Distance __k = __middle - __first;
      _Distance __l = __n - __k;

      if (__k == __l) {
	swap_ranges(__first, __middle, __middle);
	return;
      }

      _Distance __d = __gcd(__n, __k);

      for (_Distance __i = 0; __i < __d; __i++) {
	_ValueType __tmp = *__first;
	_RandomAccessIter __p = __first;

	if (__k < __l) {
	  for (_Distance __j = 0; __j < __l/__d; __j++) {
	    if (__p > __first + __l) {
	      *__p = *(__p - __l);
	      __p -= __l;
	    }

	    *__p = *(__p + __k);
	    __p += __k;
	  }
	}

	else {
	  for (_Distance __j = 0; __j < __k/__d - 1; __j ++) {
	    if (__p < __last - __k) {
	      *__p = *(__p + __k);
	      __p += __k;
	    }

	    *__p = * (__p - __l);
	    __p -= __l;
	  }
	}

	*__p = __tmp;
	++__first;
      }
    }

  /**
   *  @brief Rotate the elements of a sequence.
   *  @param  first   A forward iterator.
   *  @param  middle  A forward iterator.
   *  @param  last    A forward iterator.
   *  @return  Nothing.
   *
   *  Rotates the elements of the range @p [first,last) by @p (middle-first)
   *  positions so that the element at @p middle is moved to @p first, the
   *  element at @p middle+1 is moved to @first+1 and so on for each element
   *  in the range @p [first,last).
   *
   *  This effectively swaps the ranges @p [first,middle) and
   *  @p [middle,last).
   *
   *  Performs @p *(first+(n+(last-middle))%(last-first))=*(first+n) for
   *  each @p n in the range @p [0,last-first).
  */
  template<typename _ForwardIter>
    inline void
    rotate(_ForwardIter __first, _ForwardIter __middle, _ForwardIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)

      typedef typename iterator_traits<_ForwardIter>::iterator_category _IterType;
      __rotate(__first, __middle, __last, _IterType());
    }

  /**
   *  @brief Copy a sequence, rotating its elements.
   *  @param  first   A forward iterator.
   *  @param  middle  A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  result  An output iterator.
   *  @return   An iterator designating the end of the resulting sequence.
   *
   *  Copies the elements of the range @p [first,last) to the range
   *  beginning at @result, rotating the copied elements by @p (middle-first)
   *  positions so that the element at @p middle is moved to @p result, the
   *  element at @p middle+1 is moved to @result+1 and so on for each element
   *  in the range @p [first,last).
   *
   *  Performs @p *(result+(n+(last-middle))%(last-first))=*(first+n) for
   *  each @p n in the range @p [0,last-first).
  */
  template<typename _ForwardIter, typename _OutputIter>
    _OutputIter
    rotate_copy(_ForwardIter __first, _ForwardIter __middle,
                _ForwardIter __last, _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
		typename iterator_traits<_ForwardIter>::value_type>)

      return copy(__first, __middle, copy(__middle, __last, __result));
    }


  /**
   *  @if maint
   *  Return a random number in the range [0, __n).  This function encapsulates
   *  whether we're using rand (part of the standard C library) or lrand48
   *  (not standard, but a much better choice whenever it's available).
   *
   *  XXX There is no corresponding encapsulation fn to seed the generator.
   *  @endif
  */
  template<typename _Distance>
    inline _Distance
    __random_number(_Distance __n)
    {
  #ifdef _GLIBCPP_HAVE_DRAND48
      return lrand48() % __n;
  #else
      return rand() % __n;
  #endif
    }


  /**
   *  @brief Randomly shuffle the elements of a sequence.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @return  Nothing.
   *
   *  Reorder the elements in the range @p [first,last) using a random
   *  distribution, so that every possible ordering of the sequence is
   *  equally likely.
  */
  template<typename _RandomAccessIter>
    inline void
    random_shuffle(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)

      if (__first == __last) return;
      for (_RandomAccessIter __i = __first + 1; __i != __last; ++__i)
	iter_swap(__i, __first + __random_number((__i - __first) + 1));
    }

  /**
   *  @brief Shuffle the elements of a sequence using a random number
   *         generator.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  rand    The RNG functor or function.
   *  @return  Nothing.
   *
   *  Reorders the elements in the range @p [first,last) using @p rand to
   *  provide a random distribution. Calling @p rand(N) for a positive
   *  integer @p N should return a randomly chosen integer from the
   *  range [0,N).
  */
  template<typename _RandomAccessIter, typename _RandomNumberGenerator>
    void
    random_shuffle(_RandomAccessIter __first, _RandomAccessIter __last,
		   _RandomNumberGenerator& __rand)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)

      if (__first == __last) return;
      for (_RandomAccessIter __i = __first + 1; __i != __last; ++__i)
	iter_swap(__i, __first + __rand((__i - __first) + 1));
    }


  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIter, typename _Predicate>
    _ForwardIter
    __partition(_ForwardIter __first, _ForwardIter __last,
		_Predicate   __pred,
		forward_iterator_tag)
    {
      if (__first == __last) return __first;

      while (__pred(*__first))
	if (++__first == __last) return __first;

      _ForwardIter __next = __first;

      while (++__next != __last)
	if (__pred(*__next)) {
	  swap(*__first, *__next);
	  ++__first;
	}

      return __first;
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _BidirectionalIter, typename _Predicate>
    _BidirectionalIter
    __partition(_BidirectionalIter __first, _BidirectionalIter __last,
		_Predicate __pred,
		bidirectional_iterator_tag)
    {
      while (true) {
	while (true)
	  if (__first == __last)
	    return __first;
	  else if (__pred(*__first))
	    ++__first;
	  else
	    break;
	--__last;
	while (true)
	  if (__first == __last)
	    return __first;
	  else if (!__pred(*__last))
	    --__last;
	  else
	    break;
	iter_swap(__first, __last);
	++__first;
      }
    }

  /**
   *  @brief Move elements for which a predicate is true to the beginning
   *         of a sequence.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  pred    A predicate functor.
   *  @return  An iterator @p middle such that @p pred(i) is true for each
   *  iterator @p i in the range @p [first,middle) and false for each @p i
   *  in the range @p [middle,last).
   *  
   *  @p pred must not modify its operand. @p partition() does not preserve
   *  the relative ordering of elements in each group, use
   *  @p stable_partition() if this is needed.
  */
  template<typename _ForwardIter, typename _Predicate>
    inline _ForwardIter
    partition(_ForwardIter __first, _ForwardIter __last,
	      _Predicate   __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIter>::value_type>)

      return __partition(__first, __last, __pred, __iterator_category(__first));
    }


  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIter, typename _Predicate, typename _Distance>
    _ForwardIter
    __inplace_stable_partition(_ForwardIter __first, _ForwardIter __last,
			       _Predicate __pred, _Distance __len)
    {
      if (__len == 1)
	return __pred(*__first) ? __last : __first;
      _ForwardIter __middle = __first;
      advance(__middle, __len / 2);
      _ForwardIter __begin = __inplace_stable_partition(__first, __middle,
							__pred,
							__len / 2);
      _ForwardIter __end = __inplace_stable_partition(__middle, __last,
						      __pred,
						      __len - __len / 2);
      rotate(__begin, __middle, __end);
      advance(__begin, distance(__middle, __end));
      return __begin;
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _ForwardIter, typename _Pointer, typename _Predicate,
	   typename _Distance>
    _ForwardIter
    __stable_partition_adaptive(_ForwardIter __first, _ForwardIter __last,
				_Predicate __pred, _Distance __len,
				_Pointer __buffer,
				_Distance __buffer_size)
    {
      if (__len <= __buffer_size) {
	_ForwardIter __result1 = __first;
	_Pointer __result2 = __buffer;
	for ( ; __first != __last ; ++__first)
	  if (__pred(*__first)) {
	    *__result1 = *__first;
	    ++__result1;
	  }
	  else {
	    *__result2 = *__first;
	    ++__result2;
	  }
	copy(__buffer, __result2, __result1);
	return __result1;
      }
      else {
	_ForwardIter __middle = __first;
	advance(__middle, __len / 2);
	_ForwardIter __begin = __stable_partition_adaptive(__first, __middle,
							   __pred,
							   __len / 2,
							   __buffer, __buffer_size);
	_ForwardIter __end = __stable_partition_adaptive( __middle, __last,
							  __pred,
							  __len - __len / 2,
							  __buffer, __buffer_size);
	rotate(__begin, __middle, __end);
	advance(__begin, distance(__middle, __end));
	return __begin;
      }
    }

  /**
   *  @brief Move elements for which a predicate is true to the beginning
   *         of a sequence, preserving relative ordering.
   *  @param  first   A forward iterator.
   *  @param  last    A forward iterator.
   *  @param  pred    A predicate functor.
   *  @return  An iterator @p middle such that @p pred(i) is true for each
   *  iterator @p i in the range @p [first,middle) and false for each @p i
   *  in the range @p [middle,last).
   *  
   *  Performs the same function as @p partition() with the additional
   *  guarantee that the relative ordering of elements in each group is
   *  preserved, so any two elements @p x and @p y in the range
   *  @p [first,last) such that @p pred(x)==pred(y) will have the same
   *  relative ordering after calling @p stable_partition().
  */
  template<typename _ForwardIter, typename _Predicate>
    _ForwardIter
    stable_partition(_ForwardIter __first, _ForwardIter __last,
		     _Predicate __pred)
    {
      // concept requirements
      __glibcpp_function_requires(_Mutable_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_UnaryPredicateConcept<_Predicate,
	    typename iterator_traits<_ForwardIter>::value_type>)

      if (__first == __last)
	return __first;
      else
      {
	typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
	typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

	_Temporary_buffer<_ForwardIter, _ValueType> __buf(__first, __last);
	if (__buf.size() > 0)
	  return __stable_partition_adaptive(__first, __last, __pred,
					     _DistanceType(__buf.requested_size()),
					     __buf.begin(), __buf.size());
	else
	  return __inplace_stable_partition(__first, __last, __pred,
					    _DistanceType(__buf.requested_size()));
      }
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Tp>
    _RandomAccessIter
    __unguarded_partition(_RandomAccessIter __first, _RandomAccessIter __last,
			  _Tp __pivot)
    {
      while (true) {
	while (*__first < __pivot)
	  ++__first;
	--__last;
	while (__pivot < *__last)
	  --__last;
	if (!(__first < __last))
	  return __first;
	iter_swap(__first, __last);
	++__first;
      }
    }

  /**
   *  @if maint
   *  This is a helper function...
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Tp, typename _Compare>
    _RandomAccessIter
    __unguarded_partition(_RandomAccessIter __first, _RandomAccessIter __last,
			  _Tp __pivot, _Compare __comp)
    {
      while (true) {
	while (__comp(*__first, __pivot))
	  ++__first;
	--__last;
	while (__comp(__pivot, *__last))
	  --__last;
	if (!(__first < __last))
	  return __first;
	iter_swap(__first, __last);
	++__first;
      }
    }


  /**
   *  @if maint
   *  @doctodo
   *  This controls some aspect of the sort routines.
   *  @endif
  */
  enum { _M_threshold = 16 };

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Tp>
    void
    __unguarded_linear_insert(_RandomAccessIter __last, _Tp __val)
    {
      _RandomAccessIter __next = __last;
      --__next;
      while (__val < *__next) {
	*__last = *__next;
	__last = __next;
	--__next;
      }
      *__last = __val;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Tp, typename _Compare>
    void
    __unguarded_linear_insert(_RandomAccessIter __last, _Tp __val, _Compare __comp)
    {
      _RandomAccessIter __next = __last;
      --__next;
      while (__comp(__val, *__next)) {
	*__last = *__next;
	__last = __next;
	--__next;
      }
      *__last = __val;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter>
    void
    __insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      if (__first == __last) return;

      for (_RandomAccessIter __i = __first + 1; __i != __last; ++__i)
      {
	typename iterator_traits<_RandomAccessIter>::value_type __val = *__i;
	if (__val < *__first) {
	  copy_backward(__first, __i, __i + 1);
	  *__first = __val;
	}
	else
	  __unguarded_linear_insert(__i, __val);
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Compare>
    void
    __insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last,
		     _Compare __comp)
    {
      if (__first == __last) return;

      for (_RandomAccessIter __i = __first + 1; __i != __last; ++__i)
      {
	typename iterator_traits<_RandomAccessIter>::value_type __val = *__i;
	if (__comp(__val, *__first)) {
	  copy_backward(__first, __i, __i + 1);
	  *__first = __val;
	}
	else
	  __unguarded_linear_insert(__i, __val, __comp);
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter>
    inline void
    __unguarded_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      for (_RandomAccessIter __i = __first; __i != __last; ++__i)
	__unguarded_linear_insert(__i, _ValueType(*__i));
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Compare>
    inline void
    __unguarded_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last,
			       _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      for (_RandomAccessIter __i = __first; __i != __last; ++__i)
	__unguarded_linear_insert(__i, _ValueType(*__i), __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter>
    void
    __final_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      if (__last - __first > _M_threshold) {
	__insertion_sort(__first, __first + _M_threshold);
	__unguarded_insertion_sort(__first + _M_threshold, __last);
      }
      else
	__insertion_sort(__first, __last);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Compare>
    void
    __final_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last,
			   _Compare __comp)
    {
      if (__last - __first > _M_threshold) {
	__insertion_sort(__first, __first + _M_threshold, __comp);
	__unguarded_insertion_sort(__first + _M_threshold, __last, __comp);
      }
      else
	__insertion_sort(__first, __last, __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _Size>
    inline _Size
    __lg(_Size __n)
    {
      _Size __k;
      for (__k = 0; __n != 1; __n >>= 1) ++__k;
      return __k;
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Size>
    void
    __introsort_loop(_RandomAccessIter __first, _RandomAccessIter __last,
		     _Size __depth_limit)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      while (__last - __first > _M_threshold) {
	if (__depth_limit == 0) {
	  partial_sort(__first, __last, __last);
	  return;
	}
	--__depth_limit;
	_RandomAccessIter __cut =
	  __unguarded_partition(__first, __last,
				_ValueType(__median(*__first,
						    *(__first + (__last - __first)/2),
						    *(__last - 1))));
	__introsort_loop(__cut, __last, __depth_limit);
	__last = __cut;
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the sort routine.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Size, typename _Compare>
    void
    __introsort_loop(_RandomAccessIter __first, _RandomAccessIter __last,
		     _Size __depth_limit, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      while (__last - __first > _M_threshold) {
	if (__depth_limit == 0) {
	  partial_sort(__first, __last, __last, __comp);
	  return;
	}
	--__depth_limit;
	_RandomAccessIter __cut =
	  __unguarded_partition(__first, __last,
				_ValueType(__median(*__first,
						    *(__first + (__last - __first)/2),
						    *(__last - 1), __comp)),
	   __comp);
	__introsort_loop(__cut, __last, __depth_limit, __comp);
	__last = __cut;
      }
    }

  /**
   *  @brief Sort the elements of a sequence.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p *(i+1)<*i is false for each iterator @p i in the range
   *  @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is not preserved, use
   *  @p stable_sort() if this is needed.
  */
  template<typename _RandomAccessIter>
    inline void
    sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<_ValueType>)

      if (__first != __last) {
	__introsort_loop(__first, __last, __lg(__last - __first) * 2);
	__final_insertion_sort(__first, __last);
      }
    }

  /**
   *  @brief Sort the elements of a sequence using a predicate for comparison.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p comp(*(i+1),*i) is false for every iterator @p i in the
   *  range @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is not preserved, use
   *  @p stable_sort() if this is needed.
  */
  template<typename _RandomAccessIter, typename _Compare>
    inline void
    sort(_RandomAccessIter __first, _RandomAccessIter __last, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _ValueType, _ValueType>)

      if (__first != __last) {
	__introsort_loop(__first, __last, __lg(__last - __first) * 2, __comp);
	__final_insertion_sort(__first, __last, __comp);
      }
    }


  /**
   *  @if maint
   *  This is a helper function for the stable sorting routines.
   *  @endif
  */
  template<typename _RandomAccessIter>
    void
    __inplace_stable_sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      if (__last - __first < 15) {
	__insertion_sort(__first, __last);
	return;
      }
      _RandomAccessIter __middle = __first + (__last - __first) / 2;
      __inplace_stable_sort(__first, __middle);
      __inplace_stable_sort(__middle, __last);
      __merge_without_buffer(__first, __middle, __last,
			     __middle - __first,
			     __last - __middle);
    }

  /**
   *  @if maint
   *  This is a helper function for the stable sorting routines.
   *  @endif
  */
  template<typename _RandomAccessIter, typename _Compare>
    void
    __inplace_stable_sort(_RandomAccessIter __first, _RandomAccessIter __last,
			  _Compare __comp)
    {
      if (__last - __first < 15) {
	__insertion_sort(__first, __last, __comp);
	return;
      }
      _RandomAccessIter __middle = __first + (__last - __first) / 2;
      __inplace_stable_sort(__first, __middle, __comp);
      __inplace_stable_sort(__middle, __last, __comp);
      __merge_without_buffer(__first, __middle, __last,
			     __middle - __first,
			     __last - __middle,
			     __comp);
    }

  template<typename _RandomAccessIter1, typename _RandomAccessIter2,
	   typename _Distance>
    void
    __merge_sort_loop(_RandomAccessIter1 __first, _RandomAccessIter1 __last,
		      _RandomAccessIter2 __result, _Distance __step_size)
    {
      _Distance __two_step = 2 * __step_size;

      while (__last - __first >= __two_step) {
	__result = merge(__first, __first + __step_size,
			 __first + __step_size, __first + __two_step,
			 __result);
	__first += __two_step;
      }

      __step_size = min(_Distance(__last - __first), __step_size);
      merge(__first, __first + __step_size, __first + __step_size, __last,
	    __result);
    }

  template<typename _RandomAccessIter1, typename _RandomAccessIter2,
	   typename _Distance, typename _Compare>
    void
    __merge_sort_loop(_RandomAccessIter1 __first, _RandomAccessIter1 __last,
		      _RandomAccessIter2 __result, _Distance __step_size,
		      _Compare __comp)
    {
      _Distance __two_step = 2 * __step_size;

      while (__last - __first >= __two_step) {
	__result = merge(__first, __first + __step_size,
			 __first + __step_size, __first + __two_step,
			 __result,
			 __comp);
	__first += __two_step;
      }
      __step_size = min(_Distance(__last - __first), __step_size);

      merge(__first, __first + __step_size,
	    __first + __step_size, __last,
	    __result,
	    __comp);
    }

  enum { _M_chunk_size = 7 };

  template<typename _RandomAccessIter, typename _Distance>
    void
    __chunk_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last,
			   _Distance __chunk_size)
    {
      while (__last - __first >= __chunk_size) {
	__insertion_sort(__first, __first + __chunk_size);
	__first += __chunk_size;
      }
      __insertion_sort(__first, __last);
    }

  template<typename _RandomAccessIter, typename _Distance, typename _Compare>
    void
    __chunk_insertion_sort(_RandomAccessIter __first, _RandomAccessIter __last,
			   _Distance __chunk_size, _Compare __comp)
    {
      while (__last - __first >= __chunk_size) {
	__insertion_sort(__first, __first + __chunk_size, __comp);
	__first += __chunk_size;
      }
      __insertion_sort(__first, __last, __comp);
    }

  template<typename _RandomAccessIter, typename _Pointer>
    void
    __merge_sort_with_buffer(_RandomAccessIter __first, _RandomAccessIter __last,
                             _Pointer __buffer)
    {
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _Distance;

      _Distance __len = __last - __first;
      _Pointer __buffer_last = __buffer + __len;

      _Distance __step_size = _M_chunk_size;
      __chunk_insertion_sort(__first, __last, __step_size);

      while (__step_size < __len) {
	__merge_sort_loop(__first, __last, __buffer, __step_size);
	__step_size *= 2;
	__merge_sort_loop(__buffer, __buffer_last, __first, __step_size);
	__step_size *= 2;
      }
    }

  template<typename _RandomAccessIter, typename _Pointer, typename _Compare>
    void
    __merge_sort_with_buffer(_RandomAccessIter __first, _RandomAccessIter __last,
                             _Pointer __buffer, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _Distance;

      _Distance __len = __last - __first;
      _Pointer __buffer_last = __buffer + __len;

      _Distance __step_size = _M_chunk_size;
      __chunk_insertion_sort(__first, __last, __step_size, __comp);

      while (__step_size < __len) {
	__merge_sort_loop(__first, __last, __buffer, __step_size, __comp);
	__step_size *= 2;
	__merge_sort_loop(__buffer, __buffer_last, __first, __step_size, __comp);
	__step_size *= 2;
      }
    }

  template<typename _RandomAccessIter, typename _Pointer, typename _Distance>
    void
    __stable_sort_adaptive(_RandomAccessIter __first, _RandomAccessIter __last,
                           _Pointer __buffer, _Distance __buffer_size)
    {
      _Distance __len = (__last - __first + 1) / 2;
      _RandomAccessIter __middle = __first + __len;
      if (__len > __buffer_size) {
	__stable_sort_adaptive(__first, __middle, __buffer, __buffer_size);
	__stable_sort_adaptive(__middle, __last, __buffer, __buffer_size);
      }
      else {
	__merge_sort_with_buffer(__first, __middle, __buffer);
	__merge_sort_with_buffer(__middle, __last, __buffer);
      }
      __merge_adaptive(__first, __middle, __last, _Distance(__middle - __first),
                       _Distance(__last - __middle), __buffer, __buffer_size);
    }

  template<typename _RandomAccessIter, typename _Pointer, typename _Distance,
           typename _Compare>
    void
    __stable_sort_adaptive(_RandomAccessIter __first, _RandomAccessIter __last,
                           _Pointer __buffer, _Distance __buffer_size,
                           _Compare __comp)
    {
      _Distance __len = (__last - __first + 1) / 2;
      _RandomAccessIter __middle = __first + __len;
      if (__len > __buffer_size) {
	__stable_sort_adaptive(__first, __middle, __buffer, __buffer_size,
                               __comp);
	__stable_sort_adaptive(__middle, __last, __buffer, __buffer_size,
                               __comp);
      }
      else {
	__merge_sort_with_buffer(__first, __middle, __buffer, __comp);
	__merge_sort_with_buffer(__middle, __last, __buffer, __comp);
      }
      __merge_adaptive(__first, __middle, __last, _Distance(__middle - __first),
                       _Distance(__last - __middle), __buffer, __buffer_size,
                       __comp);
    }

  /**
   *  @brief Sort the elements of a sequence, preserving the relative order
   *         of equivalent elements.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p *(i+1)<*i is false for each iterator @p i in the range
   *  @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is preserved, so any two
   *  elements @p x and @p y in the range @p [first,last) such that
   *  @p x<y is false and @p y<x is false will have the same relative
   *  ordering after calling @p stable_sort().
  */
  template<typename _RandomAccessIter>
    inline void
    stable_sort(_RandomAccessIter __first, _RandomAccessIter __last)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<_ValueType>)

      _Temporary_buffer<_RandomAccessIter, _ValueType> buf(__first, __last);
      if (buf.begin() == 0)
	__inplace_stable_sort(__first, __last);
      else
	__stable_sort_adaptive(__first, __last, buf.begin(), _DistanceType(buf.size()));
    }

  /**
   *  @brief Sort the elements of a sequence using a predicate for comparison,
   *         preserving the relative order of equivalent elements.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the elements in the range @p [first,last) in ascending order,
   *  such that @p comp(*(i+1),*i) is false for each iterator @p i in the
   *  range @p [first,last-1).
   *
   *  The relative ordering of equivalent elements is preserved, so any two
   *  elements @p x and @p y in the range @p [first,last) such that
   *  @p comp(x,y) is false and @p comp(y,x) is false will have the same
   *  relative ordering after calling @p stable_sort().
  */
  template<typename _RandomAccessIter, typename _Compare>
    inline void
    stable_sort(_RandomAccessIter __first, _RandomAccessIter __last, _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
							  _ValueType, _ValueType>)

      _Temporary_buffer<_RandomAccessIter, _ValueType> buf(__first, __last);
      if (buf.begin() == 0)
	__inplace_stable_sort(__first, __last, __comp);
      else
	__stable_sort_adaptive(__first, __last, buf.begin(), _DistanceType(buf.size()),
			       __comp);
    }

  /**
   *  @brief Sort the smallest elements of a sequence.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Sorts the smallest @p (middle-first) elements in the range
   *  @p [first,last) and moves them to the range @p [first,middle). The
   *  order of the remaining elements in the range @p [middle,last) is
   *  undefined.
   *  After the sort if @p i and @j are iterators in the range
   *  @p [first,middle) such that @i precedes @j and @k is an iterator in
   *  the range @p [middle,last) then @p *j<*i and @p *k<*i are both false.
  */
  template<typename _RandomAccessIter>
    void
    partial_sort(_RandomAccessIter __first,
		 _RandomAccessIter __middle,
		 _RandomAccessIter __last)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<_ValueType>)

      make_heap(__first, __middle);
      for (_RandomAccessIter __i = __middle; __i < __last; ++__i)
	if (*__i < *__first)
	  __pop_heap(__first, __middle, __i, _ValueType(*__i));
      sort_heap(__first, __middle);
    }

  /**
   *  @brief Sort the smallest elements of a sequence using a predicate
   *         for comparison.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Sorts the smallest @p (middle-first) elements in the range
   *  @p [first,last) and moves them to the range @p [first,middle). The
   *  order of the remaining elements in the range @p [middle,last) is
   *  undefined.
   *  After the sort if @p i and @j are iterators in the range
   *  @p [first,middle) such that @i precedes @j and @k is an iterator in
   *  the range @p [middle,last) then @p *comp(j,*i) and @p comp(*k,*i)
   *  are both false.
  */
  template<typename _RandomAccessIter, typename _Compare>
    void
    partial_sort(_RandomAccessIter __first,
		 _RandomAccessIter __middle,
		 _RandomAccessIter __last,
		 _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<
	    _RandomAccessIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
							  _ValueType, _ValueType>)

      make_heap(__first, __middle, __comp);
      for (_RandomAccessIter __i = __middle; __i < __last; ++__i)
	if (__comp(*__i, *__first))
	  __pop_heap(__first, __middle, __i, _ValueType(*__i), __comp);
      sort_heap(__first, __middle, __comp);
    }

  /**
   *  @brief Copy the smallest elements of a sequence.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  result_first   A random-access iterator.
   *  @param  result_last    Another random-access iterator.
   *  @return   An iterator indicating the end of the resulting sequence.
   *
   *  Copies and sorts the smallest N values from the range @p [first,last)
   *  to the range beginning at @p result_first, where the number of
   *  elements to be copied, @p N, is the smaller of @p (last-first) and
   *  @p (result_last-result_first).
   *  After the sort if @p i and @j are iterators in the range
   *  @p [result_first,result_first+N) such that @i precedes @j then
   *  @p *j<*i is false.
   *  The value returned is @p result_first+N.
  */
  template<typename _InputIter, typename _RandomAccessIter>
    _RandomAccessIter
    partial_sort_copy(_InputIter __first, _InputIter __last,
		      _RandomAccessIter __result_first,
		      _RandomAccessIter __result_last)
    {
      typedef typename iterator_traits<_InputIter>::value_type _InputValueType;
      typedef typename iterator_traits<_RandomAccessIter>::value_type _OutputValueType;
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_ConvertibleConcept<_InputValueType, _OutputValueType>)
      __glibcpp_function_requires(_LessThanComparableConcept<_OutputValueType>)
      __glibcpp_function_requires(_LessThanComparableConcept<_InputValueType>)

      if (__result_first == __result_last) return __result_last;
      _RandomAccessIter __result_real_last = __result_first;
      while(__first != __last && __result_real_last != __result_last) {
	*__result_real_last = *__first;
	++__result_real_last;
	++__first;
      }
      make_heap(__result_first, __result_real_last);
      while (__first != __last) {
	if (*__first < *__result_first)
	  __adjust_heap(__result_first, _DistanceType(0),
			_DistanceType(__result_real_last - __result_first),
			_InputValueType(*__first));
	++__first;
      }
      sort_heap(__result_first, __result_real_last);
      return __result_real_last;
    }

  /**
   *  @brief Copy the smallest elements of a sequence using a predicate for
   *         comparison.
   *  @param  first   An input iterator.
   *  @param  last    Another input iterator.
   *  @param  result_first   A random-access iterator.
   *  @param  result_last    Another random-access iterator.
   *  @param  comp    A comparison functor.
   *  @return   An iterator indicating the end of the resulting sequence.
   *
   *  Copies and sorts the smallest N values from the range @p [first,last)
   *  to the range beginning at @p result_first, where the number of
   *  elements to be copied, @p N, is the smaller of @p (last-first) and
   *  @p (result_last-result_first).
   *  After the sort if @p i and @j are iterators in the range
   *  @p [result_first,result_first+N) such that @i precedes @j then
   *  @p comp(*j,*i) is false.
   *  The value returned is @p result_first+N.
  */
  template<typename _InputIter, typename _RandomAccessIter, typename _Compare>
    _RandomAccessIter
    partial_sort_copy(_InputIter __first, _InputIter __last,
		      _RandomAccessIter __result_first,
		      _RandomAccessIter __result_last,
		      _Compare __comp)
    {
      typedef typename iterator_traits<_InputIter>::value_type _InputValueType;
      typedef typename iterator_traits<_RandomAccessIter>::value_type _OutputValueType;
      typedef typename iterator_traits<_RandomAccessIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<_RandomAccessIter>)
      __glibcpp_function_requires(_ConvertibleConcept<_InputValueType, _OutputValueType>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
				  _OutputValueType, _OutputValueType>)

      if (__result_first == __result_last) return __result_last;
      _RandomAccessIter __result_real_last = __result_first;
      while(__first != __last && __result_real_last != __result_last) {
	*__result_real_last = *__first;
	++__result_real_last;
	++__first;
      }
      make_heap(__result_first, __result_real_last, __comp);
      while (__first != __last) {
	if (__comp(*__first, *__result_first))
	  __adjust_heap(__result_first, _DistanceType(0),
			_DistanceType(__result_real_last - __result_first),
			_InputValueType(*__first),
			__comp);
	++__first;
      }
      sort_heap(__result_first, __result_real_last, __comp);
      return __result_real_last;
    }

  /**
   *  @brief Sort a sequence just enough to find a particular position.
   *  @param  first   An iterator.
   *  @param  nth     Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Rearranges the elements in the range @p [first,last) so that @p *nth
   *  is the same element that would have been in that position had the
   *  whole sequence been sorted. 
   *  whole sequence been sorted. The elements either side of @p *nth are
   *  not completely sorted, but for any iterator @i in the range
   *  @p [first,nth) and any iterator @j in the range @p [nth,last) it
   *  holds that @p *j<*i is false.
  */
  template<typename _RandomAccessIter>
    void
    nth_element(_RandomAccessIter __first,
		_RandomAccessIter __nth,
		_RandomAccessIter __last)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<_RandomAccessIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<_ValueType>)

      while (__last - __first > 3) {
	_RandomAccessIter __cut =
	  __unguarded_partition(__first, __last,
				_ValueType(__median(*__first,
						    *(__first + (__last - __first)/2),
						    *(__last - 1))));
	if (__cut <= __nth)
	  __first = __cut;
	else
	  __last = __cut;
      }
      __insertion_sort(__first, __last);
    }

  /**
   *  @brief Sort a sequence just enough to find a particular position
   *         using a predicate for comparison.
   *  @param  first   An iterator.
   *  @param  nth     Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A comparison functor.
   *  @return  Nothing.
   *
   *  Rearranges the elements in the range @p [first,last) so that @p *nth
   *  is the same element that would have been in that position had the
   *  whole sequence been sorted. The elements either side of @p *nth are
   *  not completely sorted, but for any iterator @i in the range
   *  @p [first,nth) and any iterator @j in the range @p [nth,last) it
   *  holds that @p comp(*j,*i) is false.
  */
  template<typename _RandomAccessIter, typename _Compare>
    void
    nth_element(_RandomAccessIter __first,
		_RandomAccessIter __nth,
		_RandomAccessIter __last,
			    _Compare __comp)
    {
      typedef typename iterator_traits<_RandomAccessIter>::value_type _ValueType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_RandomAccessIteratorConcept<_RandomAccessIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
				  _ValueType, _ValueType>)

      while (__last - __first > 3) {
	_RandomAccessIter __cut =
	  __unguarded_partition(__first, __last,
				_ValueType(__median(*__first,
						    *(__first + (__last - __first)/2),
						    *(__last - 1),
						    __comp)),
				__comp);
	if (__cut <= __nth)
	  __first = __cut;
	else
	  __last = __cut;
      }
      __insertion_sort(__first, __last, __comp);
    }


  /**
   *  @brief Finds the first position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *  @ingroup binarysearch
  */
  template<typename _ForwardIter, typename _Tp>
    _ForwardIter
    lower_bound(_ForwardIter __first, _ForwardIter __last, const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      // Note that these are slightly stricter than those of the 4-argument
      // version, defined next.  The difference is in the strictness of the
      // comparison operations... so for looser checking, define your own
      // comparison function, as was intended.
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_SameTypeConcept<_Tp, _ValueType>)
      __glibcpp_function_requires(_LessThanComparableConcept<_Tp>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (*__middle < __val) {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
	else
	  __len = __half;
      }
      return __first;
    }

  /**
   *  @brief Finds the first position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *  @ingroup binarysearch
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIter, typename _Tp, typename _Compare>
    _ForwardIter
    lower_bound(_ForwardIter __first, _ForwardIter __last,
		const _Tp& __val, _Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _ValueType, _Tp>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (__comp(*__middle, __val)) {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
	else
	  __len = __half;
      }
      return __first;
    }

  /**
   *  @brief Finds the last position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An iterator pointing to the first element greater than @a val.
   *  @ingroup binarysearch
  */
  template<typename _ForwardIter, typename _Tp>
    _ForwardIter
    upper_bound(_ForwardIter __first, _ForwardIter __last, const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      // See comments on lower_bound.
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_SameTypeConcept<_Tp, _ValueType>)
      __glibcpp_function_requires(_LessThanComparableConcept<_Tp>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (__val < *__middle)
	  __len = __half;
	else {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
      }
      return __first;
    }

  /**
   *  @brief Finds the last position in which @a val could be inserted
   *         without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element greater than @a val.
   *  @ingroup binarysearch
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIter, typename _Tp, typename _Compare>
    _ForwardIter
    upper_bound(_ForwardIter __first, _ForwardIter __last,
		const _Tp& __val, _Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _Tp, _ValueType>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (__comp(__val, *__middle))
	  __len = __half;
	else {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
      }
      return __first;
    }

  /**
   *  @brief Finds the largest subrange in which @a val could be inserted
   *         at any place in it without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  An pair of iterators defining the subrange.
   *  @ingroup binarysearch
   *
   *  This is equivalent to
   *  @code
   *    std::make_pair(lower_bound(first, last, val),
   *                   upper_bound(first, last, val))
   *  @endcode
   *  but does not actually call those functions.
  */
  template<typename _ForwardIter, typename _Tp>
    pair<_ForwardIter, _ForwardIter>
    equal_range(_ForwardIter __first, _ForwardIter __last, const _Tp& __val)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      // See comments on lower_bound.
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_SameTypeConcept<_Tp, _ValueType>)
      __glibcpp_function_requires(_LessThanComparableConcept<_Tp>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle, __left, __right;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (*__middle < __val) {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
	else if (__val < *__middle)
	  __len = __half;
	else {
	  __left = lower_bound(__first, __middle, __val);
	  advance(__first, __len);
	  __right = upper_bound(++__middle, __first, __val);
	  return pair<_ForwardIter, _ForwardIter>(__left, __right);
	}
      }
      return pair<_ForwardIter, _ForwardIter>(__first, __first);
    }

  /**
   *  @brief Finds the largest subrange in which @a val could be inserted
   *         at any place in it without changing the ordering.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An pair of iterators defining the subrange.
   *  @ingroup binarysearch
   *
   *  This is equivalent to
   *  @code
   *    std::make_pair(lower_bound(first, last, val, comp),
   *                   upper_bound(first, last, val, comp))
   *  @endcode
   *  but does not actually call those functions.
  */
  template<typename _ForwardIter, typename _Tp, typename _Compare>
    pair<_ForwardIter, _ForwardIter>
    equal_range(_ForwardIter __first, _ForwardIter __last, const _Tp& __val,
		_Compare __comp)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename iterator_traits<_ForwardIter>::difference_type _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _ValueType, _Tp>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _Tp, _ValueType>)

      _DistanceType __len = distance(__first, __last);
      _DistanceType __half;
      _ForwardIter __middle, __left, __right;

      while (__len > 0) {
	__half = __len >> 1;
	__middle = __first;
	advance(__middle, __half);
	if (__comp(*__middle, __val)) {
	  __first = __middle;
	  ++__first;
	  __len = __len - __half - 1;
	}
	else if (__comp(__val, *__middle))
	  __len = __half;
	else {
	  __left = lower_bound(__first, __middle, __val, __comp);
	  advance(__first, __len);
	  __right = upper_bound(++__middle, __first, __val, __comp);
	  return pair<_ForwardIter, _ForwardIter>(__left, __right);
	}
      }
      return pair<_ForwardIter, _ForwardIter>(__first, __first);
    }

  /**
   *  @brief Determines whether an element exists in a range.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @return  True if @a val (or its equivelent) is in [@a first,@a last ].
   *  @ingroup binarysearch
   *
   *  Note that this does not actually return an iterator to @a val.  For
   *  that, use std::find or a container's specialized find member functions.
  */
  template<typename _ForwardIter, typename _Tp>
    bool
    binary_search(_ForwardIter __first, _ForwardIter __last,
                  const _Tp& __val)
    {
      // concept requirements
      // See comments on lower_bound.
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_SameTypeConcept<_Tp,
		typename iterator_traits<_ForwardIter>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<_Tp>)

      _ForwardIter __i = lower_bound(__first, __last, __val);
      return __i != __last && !(__val < *__i);
    }

  /**
   *  @brief Determines whether an element exists in a range.
   *  @param  first   An iterator.
   *  @param  last    Another iterator.
   *  @param  val     The search term.
   *  @param  comp    A functor to use for comparisons.
   *  @return  True if @a val (or its equivelent) is in [@a first,@a last ].
   *  @ingroup binarysearch
   *
   *  Note that this does not actually return an iterator to @a val.  For
   *  that, use std::find or a container's specialized find member functions.
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _ForwardIter, typename _Tp, typename _Compare>
    bool
    binary_search(_ForwardIter __first, _ForwardIter __last,
                  const _Tp& __val, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
		typename iterator_traits<_ForwardIter>::value_type, _Tp>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare, _Tp,
		typename iterator_traits<_ForwardIter>::value_type>)

      _ForwardIter __i = lower_bound(__first, __last, __val, __comp);
      return __i != __last && !__comp(__val, *__i);
    }

  /**
   *  @brief Merges two sorted ranges.
   *  @param  first1  An iterator.
   *  @param  first2  Another iterator.
   *  @param  last1   Another iterator.
   *  @param  last2   Another iterator.
   *  @param  result  An iterator pointing to the end of the merged range.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *
   *  Merges the ranges [first1,last1) and [first2,last2) into the sorted range
   *  [result, result + (last1-first1) + (last2-first2)).  Both input ranges
   *  must be sorted, and the output range must not overlap with either of
   *  the input ranges.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
  */
  template<typename _InputIter1, typename _InputIter2, typename _OutputIter>
    _OutputIter
    merge(_InputIter1 __first1, _InputIter1 __last1,
	  _InputIter2 __first2, _InputIter2 __last2,
	  _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2) {
	if (*__first2 < *__first1) {
	  *__result = *__first2;
	  ++__first2;
	}
	else {
	  *__result = *__first1;
	  ++__first1;
	}
	++__result;
      }
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  /**
   *  @brief Merges two sorted ranges.
   *  @param  first1  An iterator.
   *  @param  first2  Another iterator.
   *  @param  last1   Another iterator.
   *  @param  last2   Another iterator.
   *  @param  result  An iterator pointing to the end of the merged range.
   *  @param  comp    A functor to use for comparisons.
   *  @return  An iterator pointing to the first element "not less than" @a val.
   *
   *  Merges the ranges [first1,last1) and [first2,last2) into the sorted range
   *  [result, result + (last1-first1) + (last2-first2)).  Both input ranges
   *  must be sorted, and the output range must not overlap with either of
   *  the input ranges.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _Compare>
    _OutputIter
    merge(_InputIter1 __first1, _InputIter1 __last1,
	  _InputIter2 __first2, _InputIter2 __last2,
	  _OutputIter __result, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2) {
	if (__comp(*__first2, *__first1)) {
	  *__result = *__first2;
	  ++__first2;
	}
	else {
	  *__result = *__first1;
	  ++__first1;
	}
	++__result;
      }
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter, typename _Distance>
    void
    __merge_without_buffer(_BidirectionalIter __first,
			   _BidirectionalIter __middle,
			   _BidirectionalIter __last,
			   _Distance __len1, _Distance __len2)
    {
      if (__len1 == 0 || __len2 == 0)
	return;
      if (__len1 + __len2 == 2) {
	if (*__middle < *__first)
	      iter_swap(__first, __middle);
	return;
      }
      _BidirectionalIter __first_cut = __first;
      _BidirectionalIter __second_cut = __middle;
      _Distance __len11 = 0;
      _Distance __len22 = 0;
      if (__len1 > __len2) {
	__len11 = __len1 / 2;
	advance(__first_cut, __len11);
	__second_cut = lower_bound(__middle, __last, *__first_cut);
	__len22 = distance(__middle, __second_cut);
      }
      else {
	__len22 = __len2 / 2;
	advance(__second_cut, __len22);
	__first_cut = upper_bound(__first, __middle, *__second_cut);
	__len11 = distance(__first, __first_cut);
      }
      rotate(__first_cut, __middle, __second_cut);
      _BidirectionalIter __new_middle = __first_cut;
      advance(__new_middle, distance(__middle, __second_cut));
      __merge_without_buffer(__first, __first_cut, __new_middle,
			     __len11, __len22);
      __merge_without_buffer(__new_middle, __second_cut, __last,
			     __len1 - __len11, __len2 - __len22);
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter, typename _Distance, typename _Compare>
    void
    __merge_without_buffer(_BidirectionalIter __first,
                           _BidirectionalIter __middle,
			   _BidirectionalIter __last,
			   _Distance __len1, _Distance __len2,
			   _Compare __comp)
    {
      if (__len1 == 0 || __len2 == 0)
	return;
      if (__len1 + __len2 == 2) {
	if (__comp(*__middle, *__first))
	      iter_swap(__first, __middle);
	return;
      }
      _BidirectionalIter __first_cut = __first;
      _BidirectionalIter __second_cut = __middle;
      _Distance __len11 = 0;
      _Distance __len22 = 0;
      if (__len1 > __len2) {
	__len11 = __len1 / 2;
	advance(__first_cut, __len11);
	__second_cut = lower_bound(__middle, __last, *__first_cut, __comp);
	__len22 = distance(__middle, __second_cut);
      }
      else {
	__len22 = __len2 / 2;
	advance(__second_cut, __len22);
	__first_cut = upper_bound(__first, __middle, *__second_cut, __comp);
	__len11 = distance(__first, __first_cut);
      }
      rotate(__first_cut, __middle, __second_cut);
      _BidirectionalIter __new_middle = __first_cut;
      advance(__new_middle, distance(__middle, __second_cut));
      __merge_without_buffer(__first, __first_cut, __new_middle,
			     __len11, __len22, __comp);
      __merge_without_buffer(__new_middle, __second_cut, __last,
			     __len1 - __len11, __len2 - __len22, __comp);
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter1, typename _BidirectionalIter2,
	   typename _Distance>
    _BidirectionalIter1
    __rotate_adaptive(_BidirectionalIter1 __first,
		      _BidirectionalIter1 __middle,
		      _BidirectionalIter1 __last,
		      _Distance __len1, _Distance __len2,
		      _BidirectionalIter2 __buffer,
		      _Distance __buffer_size)
    {
      _BidirectionalIter2 __buffer_end;
      if (__len1 > __len2 && __len2 <= __buffer_size) {
	__buffer_end = copy(__middle, __last, __buffer);
	copy_backward(__first, __middle, __last);
	return copy(__buffer, __buffer_end, __first);
      }
      else if (__len1 <= __buffer_size) {
	__buffer_end = copy(__first, __middle, __buffer);
	copy(__middle, __last, __first);
	return copy_backward(__buffer, __buffer_end, __last);
      }
      else {
	rotate(__first, __middle, __last);
	advance(__first, distance(__middle, __last));
	return __first;
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter1, typename _BidirectionalIter2,
	   typename _BidirectionalIter3>
    _BidirectionalIter3
    __merge_backward(_BidirectionalIter1 __first1, _BidirectionalIter1 __last1,
		     _BidirectionalIter2 __first2, _BidirectionalIter2 __last2,
		     _BidirectionalIter3 __result)
    {
      if (__first1 == __last1)
	return copy_backward(__first2, __last2, __result);
      if (__first2 == __last2)
	return copy_backward(__first1, __last1, __result);
      --__last1;
      --__last2;
      while (true) {
	if (*__last2 < *__last1) {
	  *--__result = *__last1;
	  if (__first1 == __last1)
	    return copy_backward(__first2, ++__last2, __result);
	  --__last1;
	}
	else {
	  *--__result = *__last2;
	  if (__first2 == __last2)
	    return copy_backward(__first1, ++__last1, __result);
	  --__last2;
	}
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter1, typename _BidirectionalIter2,
	   typename _BidirectionalIter3, typename _Compare>
    _BidirectionalIter3
    __merge_backward(_BidirectionalIter1 __first1, _BidirectionalIter1 __last1,
		     _BidirectionalIter2 __first2, _BidirectionalIter2 __last2,
		     _BidirectionalIter3 __result,
		     _Compare __comp)
    {
      if (__first1 == __last1)
	return copy_backward(__first2, __last2, __result);
      if (__first2 == __last2)
	return copy_backward(__first1, __last1, __result);
      --__last1;
      --__last2;
      while (true) {
	if (__comp(*__last2, *__last1)) {
	  *--__result = *__last1;
	  if (__first1 == __last1)
	    return copy_backward(__first2, ++__last2, __result);
	  --__last1;
	}
	else {
	  *--__result = *__last2;
	  if (__first2 == __last2)
	    return copy_backward(__first1, ++__last1, __result);
	  --__last2;
	}
      }
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter, typename _Distance, typename _Pointer>
    void
    __merge_adaptive(_BidirectionalIter __first,
                     _BidirectionalIter __middle,
		     _BidirectionalIter __last,
		     _Distance __len1, _Distance __len2,
		     _Pointer __buffer, _Distance __buffer_size)
    {
	  if (__len1 <= __len2 && __len1 <= __buffer_size) {
	    _Pointer __buffer_end = copy(__first, __middle, __buffer);
	    merge(__buffer, __buffer_end, __middle, __last, __first);
	  }
	  else if (__len2 <= __buffer_size) {
	    _Pointer __buffer_end = copy(__middle, __last, __buffer);
	    __merge_backward(__first, __middle, __buffer, __buffer_end, __last);
	  }
	  else {
	    _BidirectionalIter __first_cut = __first;
	    _BidirectionalIter __second_cut = __middle;
	    _Distance __len11 = 0;
	    _Distance __len22 = 0;
	    if (__len1 > __len2) {
		  __len11 = __len1 / 2;
		  advance(__first_cut, __len11);
		  __second_cut = lower_bound(__middle, __last, *__first_cut);
		  __len22 = distance(__middle, __second_cut);
	    }
	    else {
		  __len22 = __len2 / 2;
		  advance(__second_cut, __len22);
		  __first_cut = upper_bound(__first, __middle, *__second_cut);
		  __len11 = distance(__first, __first_cut);
	    }
	    _BidirectionalIter __new_middle =
		  __rotate_adaptive(__first_cut, __middle, __second_cut,
				    __len1 - __len11, __len22, __buffer,
				    __buffer_size);
	    __merge_adaptive(__first, __first_cut, __new_middle, __len11,
			     __len22, __buffer, __buffer_size);
	    __merge_adaptive(__new_middle, __second_cut, __last, __len1 - __len11,
			     __len2 - __len22, __buffer, __buffer_size);
	  }
    }

  /**
   *  @if maint
   *  This is a helper function for the merge routines.
   *  @endif
  */
  template<typename _BidirectionalIter, typename _Distance, typename _Pointer,
	   typename _Compare>
    void
    __merge_adaptive(_BidirectionalIter __first,
                     _BidirectionalIter __middle,
		     _BidirectionalIter __last,
		     _Distance __len1, _Distance __len2,
		     _Pointer __buffer, _Distance __buffer_size,
		     _Compare __comp)
    {
	  if (__len1 <= __len2 && __len1 <= __buffer_size) {
	    _Pointer __buffer_end = copy(__first, __middle, __buffer);
	    merge(__buffer, __buffer_end, __middle, __last, __first, __comp);
	  }
	  else if (__len2 <= __buffer_size) {
	    _Pointer __buffer_end = copy(__middle, __last, __buffer);
	    __merge_backward(__first, __middle, __buffer, __buffer_end, __last,
					     __comp);
	  }
	  else {
	    _BidirectionalIter __first_cut = __first;
	    _BidirectionalIter __second_cut = __middle;
	    _Distance __len11 = 0;
	    _Distance __len22 = 0;
	    if (__len1 > __len2) {
		  __len11 = __len1 / 2;
		  advance(__first_cut, __len11);
		  __second_cut = lower_bound(__middle, __last, *__first_cut, __comp);
		  __len22 = distance(__middle, __second_cut);
	    }
	    else {
		  __len22 = __len2 / 2;
		  advance(__second_cut, __len22);
		  __first_cut = upper_bound(__first, __middle, *__second_cut, __comp);
		  __len11 = distance(__first, __first_cut);
	    }
	    _BidirectionalIter __new_middle =
		  __rotate_adaptive(__first_cut, __middle, __second_cut,
				    __len1 - __len11, __len22, __buffer,
				    __buffer_size);
	    __merge_adaptive(__first, __first_cut, __new_middle, __len11,
			     __len22, __buffer, __buffer_size, __comp);
	    __merge_adaptive(__new_middle, __second_cut, __last, __len1 - __len11,
			     __len2 - __len22, __buffer, __buffer_size, __comp);
	  }
    }

  /**
   *  @brief Merges two sorted ranges in place.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @return  Nothing.
   *
   *  Merges two sorted and consecutive ranges, [first,middle) and
   *  [middle,last), and puts the result in [first,last).  The output will
   *  be sorted.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  If enough additional memory is available, this takes (last-first)-1
   *  comparisons.  Otherwise an NlogN algorithm is used, where N is
   *  distance(first,last).
  */
  template<typename _BidirectionalIter>
    void
    inplace_merge(_BidirectionalIter __first,
		  _BidirectionalIter __middle,
		  _BidirectionalIter __last)
    {
      typedef typename iterator_traits<_BidirectionalIter>::value_type
          _ValueType;
      typedef typename iterator_traits<_BidirectionalIter>::difference_type
          _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_BidirectionalIteratorConcept<
	    _BidirectionalIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<_ValueType>)

      if (__first == __middle || __middle == __last)
	return;

      _DistanceType __len1 = distance(__first, __middle);
      _DistanceType __len2 = distance(__middle, __last);

      _Temporary_buffer<_BidirectionalIter, _ValueType> __buf(__first, __last);
      if (__buf.begin() == 0)
	__merge_without_buffer(__first, __middle, __last, __len1, __len2);
      else
	__merge_adaptive(__first, __middle, __last, __len1, __len2,
			 __buf.begin(), _DistanceType(__buf.size()));
    }

  /**
   *  @brief Merges two sorted ranges in place.
   *  @param  first   An iterator.
   *  @param  middle  Another iterator.
   *  @param  last    Another iterator.
   *  @param  comp    A functor to use for comparisons.
   *  @return  Nothing.
   *
   *  Merges two sorted and consecutive ranges, [first,middle) and
   *  [middle,last), and puts the result in [first,last).  The output will
   *  be sorted.  The sort is @e stable, that is, for equivalent
   *  elements in the two ranges, elements from the first range will always
   *  come before elements from the second.
   *
   *  If enough additional memory is available, this takes (last-first)-1
   *  comparisons.  Otherwise an NlogN algorithm is used, where N is
   *  distance(first,last).
   *
   *  The comparison function should have the same effects on ordering as
   *  the function used for the initial sort.
  */
  template<typename _BidirectionalIter, typename _Compare>
    void
    inplace_merge(_BidirectionalIter __first,
		  _BidirectionalIter __middle,
		  _BidirectionalIter __last,
		  _Compare __comp)
    {
      typedef typename iterator_traits<_BidirectionalIter>::value_type
          _ValueType;
      typedef typename iterator_traits<_BidirectionalIter>::difference_type
          _DistanceType;

      // concept requirements
      __glibcpp_function_requires(_Mutable_BidirectionalIteratorConcept<
	    _BidirectionalIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    _ValueType, _ValueType>)

      if (__first == __middle || __middle == __last)
	return;

      _DistanceType __len1 = distance(__first, __middle);
      _DistanceType __len2 = distance(__middle, __last);

      _Temporary_buffer<_BidirectionalIter, _ValueType> __buf(__first, __last);
      if (__buf.begin() == 0)
	__merge_without_buffer(__first, __middle, __last, __len1, __len2, __comp);
      else
	__merge_adaptive(__first, __middle, __last, __len1, __len2,
			 __buf.begin(), _DistanceType(__buf.size()),
			 __comp);
    }

  // Set algorithms: includes, set_union, set_intersection, set_difference,
  // set_symmetric_difference.  All of these algorithms have the precondition
  // that their input ranges are sorted and the postcondition that their output
  // ranges are sorted.

  template<typename _InputIter1, typename _InputIter2>
    bool
    includes(_InputIter1 __first1, _InputIter1 __last1,
	     _InputIter2 __first2, _InputIter2 __last2)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first2 < *__first1)
	  return false;
	else if(*__first1 < *__first2)
	  ++__first1;
	else
	  ++__first1, ++__first2;

      return __first2 == __last2;
    }

  template<typename _InputIter1, typename _InputIter2, typename _Compare>
    bool
    includes(_InputIter1 __first1, _InputIter1 __last1,
	     _InputIter2 __first2, _InputIter2 __last2, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first2, *__first1))
	  return false;
	else if(__comp(*__first1, *__first2))
	  ++__first1;
	else
	  ++__first1, ++__first2;

      return __first2 == __last2;
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter>
    _OutputIter
    set_union(_InputIter1 __first1, _InputIter1 __last1,
	      _InputIter2 __first2, _InputIter2 __last2,
	      _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2) {
	if (*__first1 < *__first2) {
	  *__result = *__first1;
	  ++__first1;
	}
	else if (*__first2 < *__first1) {
	  *__result = *__first2;
	  ++__first2;
	}
	else {
	  *__result = *__first1;
	  ++__first1;
	  ++__first2;
	}
	++__result;
      }
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _Compare>
    _OutputIter
    set_union(_InputIter1 __first1, _InputIter1 __last1,
	      _InputIter2 __first2, _InputIter2 __last2,
	      _OutputIter __result, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2) {
	if (__comp(*__first1, *__first2)) {
	  *__result = *__first1;
	  ++__first1;
	}
	else if (__comp(*__first2, *__first1)) {
	  *__result = *__first2;
	  ++__first2;
	}
	else {
	  *__result = *__first1;
	  ++__first1;
	  ++__first2;
	}
	++__result;
      }
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter>
    _OutputIter
    set_intersection(_InputIter1 __first1, _InputIter1 __last1,
		     _InputIter2 __first2, _InputIter2 __last2,
		     _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2)
	  ++__first1;
	else if (*__first2 < *__first1)
	  ++__first2;
	else {
	  *__result = *__first1;
	  ++__first1;
	  ++__first2;
	  ++__result;
	}
      return __result;
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _Compare>
    _OutputIter
    set_intersection(_InputIter1 __first1, _InputIter1 __last1,
		     _InputIter2 __first2, _InputIter2 __last2,
		     _OutputIter __result, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2))
	  ++__first1;
	else if (__comp(*__first2, *__first1))
	  ++__first2;
	else {
	  *__result = *__first1;
	  ++__first1;
	  ++__first2;
	  ++__result;
	}
      return __result;
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter>
    _OutputIter
    set_difference(_InputIter1 __first1, _InputIter1 __last1,
		   _InputIter2 __first2, _InputIter2 __last2,
		   _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2) {
	  *__result = *__first1;
	  ++__first1;
	  ++__result;
	}
	else if (*__first2 < *__first1)
	  ++__first2;
	else {
	  ++__first1;
	  ++__first2;
	}
      return copy(__first1, __last1, __result);
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _Compare>
    _OutputIter
    set_difference(_InputIter1 __first1, _InputIter1 __last1,
		   _InputIter2 __first2, _InputIter2 __last2,
		   _OutputIter __result, _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2)) {
	  *__result = *__first1;
	  ++__first1;
	  ++__result;
	}
	else if (__comp(*__first2, *__first1))
	  ++__first2;
	else {
	  ++__first1;
	  ++__first2;
	}
      return copy(__first1, __last1, __result);
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter>
    _OutputIter
    set_symmetric_difference(_InputIter1 __first1, _InputIter1 __last1,
			     _InputIter2 __first2, _InputIter2 __last2,
			     _OutputIter __result)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_InputIter1>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (*__first1 < *__first2) {
	  *__result = *__first1;
	  ++__first1;
	  ++__result;
	}
	else if (*__first2 < *__first1) {
	  *__result = *__first2;
	  ++__first2;
	  ++__result;
	}
	else {
	  ++__first1;
	  ++__first2;
	}
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  template<typename _InputIter1, typename _InputIter2, typename _OutputIter,
	   typename _Compare>
    _OutputIter
    set_symmetric_difference(_InputIter1 __first1, _InputIter1 __last1,
			     _InputIter2 __first2, _InputIter2 __last2,
			     _OutputIter __result,
			     _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter1>)
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter2>)
      __glibcpp_function_requires(_SameTypeConcept<
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)
      __glibcpp_function_requires(_OutputIteratorConcept<_OutputIter,
	    typename iterator_traits<_InputIter1>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_InputIter1>::value_type,
	    typename iterator_traits<_InputIter2>::value_type>)

      while (__first1 != __last1 && __first2 != __last2)
	if (__comp(*__first1, *__first2)) {
	  *__result = *__first1;
	  ++__first1;
	  ++__result;
	}
	else if (__comp(*__first2, *__first1)) {
	  *__result = *__first2;
	  ++__first2;
	  ++__result;
	}
	else {
	  ++__first1;
	  ++__first2;
	}
      return copy(__first2, __last2, copy(__first1, __last1, __result));
    }

  // min_element and max_element, with and without an explicitly supplied
  // comparison function.

  template<typename _ForwardIter>
    _ForwardIter
    max_element(_ForwardIter __first, _ForwardIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_ForwardIter>::value_type>)

      if (__first == __last) return __first;
      _ForwardIter __result = __first;
      while (++__first != __last)
	if (*__result < *__first)
	  __result = __first;
      return __result;
    }

  template<typename _ForwardIter, typename _Compare>
    _ForwardIter
    max_element(_ForwardIter __first, _ForwardIter __last,
		_Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_ForwardIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)

      if (__first == __last) return __first;
      _ForwardIter __result = __first;
      while (++__first != __last)
	if (__comp(*__result, *__first)) __result = __first;
      return __result;
    }

  template<typename _ForwardIter>
    _ForwardIter
    min_element(_ForwardIter __first, _ForwardIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_ForwardIter>::value_type>)

      if (__first == __last) return __first;
      _ForwardIter __result = __first;
      while (++__first != __last)
	if (*__first < *__result)
	  __result = __first;
      return __result;
    }

  template<typename _ForwardIter, typename _Compare>
    _ForwardIter
    min_element(_ForwardIter __first, _ForwardIter __last,
		_Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_ForwardIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)

      if (__first == __last) return __first;
      _ForwardIter __result = __first;
      while (++__first != __last)
	if (__comp(*__first, *__result))
	  __result = __first;
      return __result;
    }

  // next_permutation and prev_permutation, with and without an explicitly
  // supplied comparison function.

  template<typename _BidirectionalIter>
    bool
    next_permutation(_BidirectionalIter __first, _BidirectionalIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_BidirectionalIter>::value_type>)

      if (__first == __last)
	return false;
      _BidirectionalIter __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;) {
	_BidirectionalIter __ii = __i;
	--__i;
	if (*__i < *__ii) {
	  _BidirectionalIter __j = __last;
	  while (!(*__i < *--__j))
	    {}
	  iter_swap(__i, __j);
	  reverse(__ii, __last);
	  return true;
	}
	if (__i == __first) {
	  reverse(__first, __last);
	  return false;
	}
      }
    }

  template<typename _BidirectionalIter, typename _Compare>
    bool
    next_permutation(_BidirectionalIter __first, _BidirectionalIter __last,
		     _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_BidirectionalIter>::value_type,
	    typename iterator_traits<_BidirectionalIter>::value_type>)

      if (__first == __last)
	return false;
      _BidirectionalIter __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;) {
	_BidirectionalIter __ii = __i;
	--__i;
	if (__comp(*__i, *__ii)) {
	  _BidirectionalIter __j = __last;
	  while (!__comp(*__i, *--__j))
	    {}
	  iter_swap(__i, __j);
	  reverse(__ii, __last);
	  return true;
	}
	if (__i == __first) {
	  reverse(__first, __last);
	  return false;
	}
      }
    }

  template<typename _BidirectionalIter>
    bool
    prev_permutation(_BidirectionalIter __first, _BidirectionalIter __last)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter>)
      __glibcpp_function_requires(_LessThanComparableConcept<
	    typename iterator_traits<_BidirectionalIter>::value_type>)

      if (__first == __last)
	return false;
      _BidirectionalIter __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;) {
	_BidirectionalIter __ii = __i;
	--__i;
	if (*__ii < *__i) {
	  _BidirectionalIter __j = __last;
	  while (!(*--__j < *__i))
	    {}
	  iter_swap(__i, __j);
	  reverse(__ii, __last);
	  return true;
	}
	if (__i == __first) {
	  reverse(__first, __last);
	  return false;
	}
      }
    }

  template<typename _BidirectionalIter, typename _Compare>
    bool
    prev_permutation(_BidirectionalIter __first, _BidirectionalIter __last,
		     _Compare __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_Compare,
	    typename iterator_traits<_BidirectionalIter>::value_type,
	    typename iterator_traits<_BidirectionalIter>::value_type>)

      if (__first == __last)
	return false;
      _BidirectionalIter __i = __first;
      ++__i;
      if (__i == __last)
	return false;
      __i = __last;
      --__i;

      for(;;) {
	_BidirectionalIter __ii = __i;
	--__i;
	if (__comp(*__ii, *__i)) {
	  _BidirectionalIter __j = __last;
	  while (!__comp(*--__j, *__i))
	    {}
	  iter_swap(__i, __j);
	  reverse(__ii, __last);
	  return true;
	}
	if (__i == __first) {
	  reverse(__first, __last);
	  return false;
	}
      }
    }

  // find_first_of, with and without an explicitly supplied comparison function.

  template<typename _InputIter, typename _ForwardIter>
    _InputIter
    find_first_of(_InputIter __first1, _InputIter __last1,
		  _ForwardIter __first2, _ForwardIter __last2)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)

      for ( ; __first1 != __last1; ++__first1)
	for (_ForwardIter __iter = __first2; __iter != __last2; ++__iter)
	  if (*__first1 == *__iter)
	    return __first1;
      return __last1;
    }

  template<typename _InputIter, typename _ForwardIter, typename _BinaryPredicate>
    _InputIter
    find_first_of(_InputIter __first1, _InputIter __last1,
		  _ForwardIter __first2, _ForwardIter __last2,
		  _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_InputIteratorConcept<_InputIter>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_InputIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_InputIter>::value_type,
	    typename iterator_traits<_ForwardIter>::value_type>)

      for ( ; __first1 != __last1; ++__first1)
	for (_ForwardIter __iter = __first2; __iter != __last2; ++__iter)
	  if (__comp(*__first1, *__iter))
	    return __first1;
      return __last1;
    }


  // find_end, with and without an explicitly supplied comparison function.
  // Search [first2, last2) as a subsequence in [first1, last1), and return
  // the *last* possible match.  Note that find_end for bidirectional iterators
  // is much faster than for forward iterators.

  // find_end for forward iterators.
  template<typename _ForwardIter1, typename _ForwardIter2>
    _ForwardIter1
    __find_end(_ForwardIter1 __first1, _ForwardIter1 __last1,
	       _ForwardIter2 __first2, _ForwardIter2 __last2,
	       forward_iterator_tag, forward_iterator_tag)
    {
      if (__first2 == __last2)
	return __last1;
      else {
	_ForwardIter1 __result = __last1;
	while (1) {
	  _ForwardIter1 __new_result
	    = search(__first1, __last1, __first2, __last2);
	  if (__new_result == __last1)
	    return __result;
	  else {
	    __result = __new_result;
	    __first1 = __new_result;
	    ++__first1;
	  }
	}
      }
    }

  template<typename _ForwardIter1, typename _ForwardIter2,
	   typename _BinaryPredicate>
    _ForwardIter1
    __find_end(_ForwardIter1 __first1, _ForwardIter1 __last1,
	       _ForwardIter2 __first2, _ForwardIter2 __last2,
	       forward_iterator_tag, forward_iterator_tag,
	       _BinaryPredicate __comp)
    {
      if (__first2 == __last2)
	return __last1;
      else {
	_ForwardIter1 __result = __last1;
	while (1) {
	  _ForwardIter1 __new_result
	    = search(__first1, __last1, __first2, __last2, __comp);
	  if (__new_result == __last1)
	    return __result;
	  else {
	    __result = __new_result;
	    __first1 = __new_result;
	    ++__first1;
	  }
	}
      }
    }

  // find_end for bidirectional iterators.  Requires partial specialization.
  template<typename _BidirectionalIter1, typename _BidirectionalIter2>
    _BidirectionalIter1
    __find_end(_BidirectionalIter1 __first1, _BidirectionalIter1 __last1,
	       _BidirectionalIter2 __first2, _BidirectionalIter2 __last2,
	       bidirectional_iterator_tag, bidirectional_iterator_tag)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter1>)
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter2>)

      typedef reverse_iterator<_BidirectionalIter1> _RevIter1;
      typedef reverse_iterator<_BidirectionalIter2> _RevIter2;

      _RevIter1 __rlast1(__first1);
      _RevIter2 __rlast2(__first2);
      _RevIter1 __rresult = search(_RevIter1(__last1), __rlast1,
				   _RevIter2(__last2), __rlast2);

      if (__rresult == __rlast1)
	return __last1;
      else {
	_BidirectionalIter1 __result = __rresult.base();
	advance(__result, -distance(__first2, __last2));
	return __result;
      }
    }

  template<typename _BidirectionalIter1, typename _BidirectionalIter2,
	   typename _BinaryPredicate>
    _BidirectionalIter1
    __find_end(_BidirectionalIter1 __first1, _BidirectionalIter1 __last1,
	       _BidirectionalIter2 __first2, _BidirectionalIter2 __last2,
	       bidirectional_iterator_tag, bidirectional_iterator_tag,
	       _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter1>)
      __glibcpp_function_requires(_BidirectionalIteratorConcept<_BidirectionalIter2>)

      typedef reverse_iterator<_BidirectionalIter1> _RevIter1;
      typedef reverse_iterator<_BidirectionalIter2> _RevIter2;

      _RevIter1 __rlast1(__first1);
      _RevIter2 __rlast2(__first2);
      _RevIter1 __rresult = search(_RevIter1(__last1), __rlast1,
				   _RevIter2(__last2), __rlast2,
				   __comp);

      if (__rresult == __rlast1)
	return __last1;
      else {
	_BidirectionalIter1 __result = __rresult.base();
	advance(__result, -distance(__first2, __last2));
	return __result;
      }
    }

  // Dispatching functions for find_end.

  template<typename _ForwardIter1, typename _ForwardIter2>
    inline _ForwardIter1
    find_end(_ForwardIter1 __first1, _ForwardIter1 __last1,
	     _ForwardIter2 __first2, _ForwardIter2 __last2)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter1>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter2>)
      __glibcpp_function_requires(_EqualOpConcept<
	    typename iterator_traits<_ForwardIter1>::value_type,
	    typename iterator_traits<_ForwardIter2>::value_type>)

      return __find_end(__first1, __last1, __first2, __last2,
			__iterator_category(__first1),
			__iterator_category(__first2));
    }

  template<typename _ForwardIter1, typename _ForwardIter2,
	   typename _BinaryPredicate>
    inline _ForwardIter1
    find_end(_ForwardIter1 __first1, _ForwardIter1 __last1,
	     _ForwardIter2 __first2, _ForwardIter2 __last2,
	     _BinaryPredicate __comp)
    {
      // concept requirements
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter1>)
      __glibcpp_function_requires(_ForwardIteratorConcept<_ForwardIter2>)
      __glibcpp_function_requires(_BinaryPredicateConcept<_BinaryPredicate,
	    typename iterator_traits<_ForwardIter1>::value_type,
	    typename iterator_traits<_ForwardIter2>::value_type>)

      return __find_end(__first1, __last1, __first2, __last2,
			__iterator_category(__first1),
			__iterator_category(__first2),
			__comp);
    }

} // namespace std

#endif /* __GLIBCPP_INTERNAL_ALGO_H */

