// Raw memory manipulators -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006
// Free Software Foundation, Inc.
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
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
 * Copyright (c) 1996,1997
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

/** @file stl_uninitialized.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _STL_UNINITIALIZED_H
#define _STL_UNINITIALIZED_H 1

#include <cstring>

_GLIBCXX_BEGIN_NAMESPACE(std)

  // uninitialized_copy
  template<typename _InputIterator, typename _ForwardIterator>
    inline _ForwardIterator
    __uninitialized_copy_aux(_InputIterator __first, _InputIterator __last,
			     _ForwardIterator __result,
			     __true_type)
    { return std::copy(__first, __last, __result); }

  template<typename _InputIterator, typename _ForwardIterator>
    inline _ForwardIterator
    __uninitialized_copy_aux(_InputIterator __first, _InputIterator __last,
			     _ForwardIterator __result,
			     __false_type)
    {
      _ForwardIterator __cur = __result;
      try
	{
	  for (; __first != __last; ++__first, ++__cur)
	    std::_Construct(&*__cur, *__first);
	  return __cur;
	}
      catch(...)
	{
	  std::_Destroy(__result, __cur);
	  __throw_exception_again;
	}
    }

  /**
   *  @brief Copies the range [first,last) into result.
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  result An output iterator.
   *  @return   result + (first - last)
   *
   *  Like copy(), but does not require an initialized output range.
  */
  template<typename _InputIterator, typename _ForwardIterator>
    inline _ForwardIterator
    uninitialized_copy(_InputIterator __first, _InputIterator __last,
		       _ForwardIterator __result)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
      typedef typename std::__is_scalar<_ValueType>::__type _Is_POD;
      return std::__uninitialized_copy_aux(__first, __last, __result,
					   _Is_POD());
    }

  inline char*
  uninitialized_copy(const char* __first, const char* __last, char* __result)
  {
    std::memmove(__result, __first, __last - __first);
    return __result + (__last - __first);
  }

  inline wchar_t*
  uninitialized_copy(const wchar_t* __first, const wchar_t* __last,
		     wchar_t* __result)
  {
    std::memmove(__result, __first, sizeof(wchar_t) * (__last - __first));
    return __result + (__last - __first);
  }

  // Valid if copy construction is equivalent to assignment, and if the
  // destructor is trivial.
  template<typename _ForwardIterator, typename _Tp>
    inline void
    __uninitialized_fill_aux(_ForwardIterator __first,
			     _ForwardIterator __last,
			     const _Tp& __x, __true_type)
    { std::fill(__first, __last, __x); }

  template<typename _ForwardIterator, typename _Tp>
    void
    __uninitialized_fill_aux(_ForwardIterator __first, _ForwardIterator __last,
			     const _Tp& __x, __false_type)
    {
      _ForwardIterator __cur = __first;
      try
	{
	  for (; __cur != __last; ++__cur)
	    std::_Construct(&*__cur, __x);
	}
      catch(...)
	{
	  std::_Destroy(__first, __cur);
	  __throw_exception_again;
	}
    }

  /**
   *  @brief Copies the value x into the range [first,last).
   *  @param  first  An input iterator.
   *  @param  last   An input iterator.
   *  @param  x      The source value.
   *  @return   Nothing.
   *
   *  Like fill(), but does not require an initialized output range.
  */
  template<typename _ForwardIterator, typename _Tp>
    inline void
    uninitialized_fill(_ForwardIterator __first, _ForwardIterator __last,
		       const _Tp& __x)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
      typedef typename std::__is_scalar<_ValueType>::__type _Is_POD;
      std::__uninitialized_fill_aux(__first, __last, __x, _Is_POD());
    }

  // Valid if copy construction is equivalent to assignment, and if the
  //  destructor is trivial.
  template<typename _ForwardIterator, typename _Size, typename _Tp>
    inline void
    __uninitialized_fill_n_aux(_ForwardIterator __first, _Size __n,
			       const _Tp& __x, __true_type)
    { std::fill_n(__first, __n, __x); }

  template<typename _ForwardIterator, typename _Size, typename _Tp>
    void
    __uninitialized_fill_n_aux(_ForwardIterator __first, _Size __n,
			       const _Tp& __x, __false_type)
    {
      _ForwardIterator __cur = __first;
      try
	{
	  for (; __n > 0; --__n, ++__cur)
	    std::_Construct(&*__cur, __x);
	}
      catch(...)
	{
	  std::_Destroy(__first, __cur);
	  __throw_exception_again;
	}
    }

  /**
   *  @brief Copies the value x into the range [first,first+n).
   *  @param  first  An input iterator.
   *  @param  n      The number of copies to make.
   *  @param  x      The source value.
   *  @return   Nothing.
   *
   *  Like fill_n(), but does not require an initialized output range.
  */
  template<typename _ForwardIterator, typename _Size, typename _Tp>
    inline void
    uninitialized_fill_n(_ForwardIterator __first, _Size __n, const _Tp& __x)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type _ValueType;
      typedef typename std::__is_scalar<_ValueType>::__type _Is_POD;
      std::__uninitialized_fill_n_aux(__first, __n, __x, _Is_POD());
    }

  // Extensions: versions of uninitialized_copy, uninitialized_fill,
  //  and uninitialized_fill_n that take an allocator parameter.
  //  We dispatch back to the standard versions when we're given the
  //  default allocator.  For nondefault allocators we do not use 
  //  any of the POD optimizations.

  template<typename _InputIterator, typename _ForwardIterator,
	   typename _Allocator>
    _ForwardIterator
    __uninitialized_copy_a(_InputIterator __first, _InputIterator __last,
			   _ForwardIterator __result,
			   _Allocator __alloc)
    {
      _ForwardIterator __cur = __result;
      try
	{
	  for (; __first != __last; ++__first, ++__cur)
	    __alloc.construct(&*__cur, *__first);
	  return __cur;
	}
      catch(...)
	{
	  std::_Destroy(__result, __cur, __alloc);
	  __throw_exception_again;
	}
    }

  template<typename _InputIterator, typename _ForwardIterator, typename _Tp>
    inline _ForwardIterator
    __uninitialized_copy_a(_InputIterator __first, _InputIterator __last,
			   _ForwardIterator __result,
			   allocator<_Tp>)
    { return std::uninitialized_copy(__first, __last, __result); }

  template<typename _ForwardIterator, typename _Tp, typename _Allocator>
    void
    __uninitialized_fill_a(_ForwardIterator __first, _ForwardIterator __last,
			   const _Tp& __x, _Allocator __alloc)
    {
      _ForwardIterator __cur = __first;
      try
	{
	  for (; __cur != __last; ++__cur)
	    __alloc.construct(&*__cur, __x);
	}
      catch(...)
	{
	  std::_Destroy(__first, __cur, __alloc);
	  __throw_exception_again;
	}
    }

  template<typename _ForwardIterator, typename _Tp, typename _Tp2>
    inline void
    __uninitialized_fill_a(_ForwardIterator __first, _ForwardIterator __last,
			   const _Tp& __x, allocator<_Tp2>)
    { std::uninitialized_fill(__first, __last, __x); }

  template<typename _ForwardIterator, typename _Size, typename _Tp,
	   typename _Allocator>
    void
    __uninitialized_fill_n_a(_ForwardIterator __first, _Size __n, 
			     const _Tp& __x,
			     _Allocator __alloc)
    {
      _ForwardIterator __cur = __first;
      try
	{
	  for (; __n > 0; --__n, ++__cur)
	    __alloc.construct(&*__cur, __x);
	}
      catch(...)
	{
	  std::_Destroy(__first, __cur, __alloc);
	  __throw_exception_again;
	}
    }

  template<typename _ForwardIterator, typename _Size, typename _Tp,
	   typename _Tp2>
    inline void
    __uninitialized_fill_n_a(_ForwardIterator __first, _Size __n, 
			     const _Tp& __x,
			     allocator<_Tp2>)
    { std::uninitialized_fill_n(__first, __n, __x); }


  // Extensions: __uninitialized_copy_copy, __uninitialized_copy_fill,
  // __uninitialized_fill_copy.  All of these algorithms take a user-
  // supplied allocator, which is used for construction and destruction.

  // __uninitialized_copy_copy
  // Copies [first1, last1) into [result, result + (last1 - first1)), and
  //  copies [first2, last2) into
  //  [result, result + (last1 - first1) + (last2 - first2)).

  template<typename _InputIterator1, typename _InputIterator2,
	   typename _ForwardIterator, typename _Allocator>
    inline _ForwardIterator
    __uninitialized_copy_copy(_InputIterator1 __first1,
			      _InputIterator1 __last1,
			      _InputIterator2 __first2,
			      _InputIterator2 __last2,
			      _ForwardIterator __result,
			      _Allocator __alloc)
    {
      _ForwardIterator __mid = std::__uninitialized_copy_a(__first1, __last1,
							   __result,
							   __alloc);
      try
	{
	  return std::__uninitialized_copy_a(__first2, __last2, __mid, __alloc);
	}
      catch(...)
	{
	  std::_Destroy(__result, __mid, __alloc);
	  __throw_exception_again;
	}
    }

  // __uninitialized_fill_copy
  // Fills [result, mid) with x, and copies [first, last) into
  //  [mid, mid + (last - first)).
  template<typename _ForwardIterator, typename _Tp, typename _InputIterator,
	   typename _Allocator>
    inline _ForwardIterator
    __uninitialized_fill_copy(_ForwardIterator __result, _ForwardIterator __mid,
			      const _Tp& __x, _InputIterator __first,
			      _InputIterator __last,
			      _Allocator __alloc)
    {
      std::__uninitialized_fill_a(__result, __mid, __x, __alloc);
      try
	{
	  return std::__uninitialized_copy_a(__first, __last, __mid, __alloc);
	}
      catch(...)
	{
	  std::_Destroy(__result, __mid, __alloc);
	  __throw_exception_again;
	}
    }

  // __uninitialized_copy_fill
  // Copies [first1, last1) into [first2, first2 + (last1 - first1)), and
  //  fills [first2 + (last1 - first1), last2) with x.
  template<typename _InputIterator, typename _ForwardIterator, typename _Tp,
	   typename _Allocator>
    inline void
    __uninitialized_copy_fill(_InputIterator __first1, _InputIterator __last1,
			      _ForwardIterator __first2,
			      _ForwardIterator __last2, const _Tp& __x,
			      _Allocator __alloc)
    {
      _ForwardIterator __mid2 = std::__uninitialized_copy_a(__first1, __last1,
							    __first2,
							    __alloc);
      try
	{
	  std::__uninitialized_fill_a(__mid2, __last2, __x, __alloc);
	}
      catch(...)
	{
	  std::_Destroy(__first2, __mid2, __alloc);
	  __throw_exception_again;
	}
    }

_GLIBCXX_END_NAMESPACE

#endif /* _STL_UNINITIALIZED_H */
