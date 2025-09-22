// Raw memory manipulators -*- C++ -*-

// Copyright (C) 2001 Free Software Foundation, Inc.
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

#ifndef _CPP_BITS_STL_UNINITIALIZED_H
#define _CPP_BITS_STL_UNINITIALIZED_H 1

#include <cstring>

namespace std
{

  // uninitialized_copy

  template<typename _InputIter, typename _ForwardIter>
    inline _ForwardIter 
    __uninitialized_copy_aux(_InputIter __first, _InputIter __last,
			     _ForwardIter __result,
			     __true_type)
    { return copy(__first, __last, __result); }

  template<typename _InputIter, typename _ForwardIter>
    _ForwardIter 
    __uninitialized_copy_aux(_InputIter __first, _InputIter __last,
			     _ForwardIter __result,
			     __false_type)
    {
      _ForwardIter __cur = __result;
      try {
	for ( ; __first != __last; ++__first, ++__cur)
	  _Construct(&*__cur, *__first);
	return __cur;
      }
      catch(...)
	{
	  _Destroy(__result, __cur);
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
  template<typename _InputIter, typename _ForwardIter>
    inline _ForwardIter
    uninitialized_copy(_InputIter __first, _InputIter __last, _ForwardIter __result)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename __type_traits<_ValueType>::is_POD_type _Is_POD;
      return __uninitialized_copy_aux(__first, __last, __result, _Is_POD());
    }

  inline char*
  uninitialized_copy(const char* __first, const char* __last, char* __result)
  {
    memmove(__result, __first, __last - __first);
    return __result + (__last - __first);
  }

  inline wchar_t* 
  uninitialized_copy(const wchar_t* __first, const wchar_t* __last,
		     wchar_t* __result)
  {
    memmove(__result, __first, sizeof(wchar_t) * (__last - __first));
    return __result + (__last - __first);
  }

  // Valid if copy construction is equivalent to assignment, and if the
  // destructor is trivial.
  template<typename _ForwardIter, typename _Tp>
    inline void
    __uninitialized_fill_aux(_ForwardIter __first, _ForwardIter __last, 
			     const _Tp& __x, __true_type)
    { fill(__first, __last, __x); }

  template<typename _ForwardIter, typename _Tp>
    void
    __uninitialized_fill_aux(_ForwardIter __first, _ForwardIter __last, 
			     const _Tp& __x, __false_type)
    {
      _ForwardIter __cur = __first;
      try {
	for ( ; __cur != __last; ++__cur)
	  _Construct(&*__cur, __x);
      }
      catch(...)
	{
	  _Destroy(__first, __cur);
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
  template<typename _ForwardIter, typename _Tp>
    inline void
    uninitialized_fill(_ForwardIter __first, _ForwardIter __last, const _Tp& __x)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename __type_traits<_ValueType>::is_POD_type _Is_POD;
      __uninitialized_fill_aux(__first, __last, __x, _Is_POD());
    }

  // Valid if copy construction is equivalent to assignment, and if the
  //  destructor is trivial.
  template<typename _ForwardIter, typename _Size, typename _Tp>
    inline _ForwardIter
    __uninitialized_fill_n_aux(_ForwardIter __first, _Size __n,
			       const _Tp& __x, __true_type)
    {
      return fill_n(__first, __n, __x);
    }

  template<typename _ForwardIter, typename _Size, typename _Tp>
    _ForwardIter
    __uninitialized_fill_n_aux(_ForwardIter __first, _Size __n,
			       const _Tp& __x, __false_type)
    {
      _ForwardIter __cur = __first;
      try {
	for ( ; __n > 0; --__n, ++__cur)
	  _Construct(&*__cur, __x);
	return __cur;
      }
      catch(...)
	{ 
	  _Destroy(__first, __cur);
	  __throw_exception_again; 
	}
    }

  /**
   *  @brief Copies the value x into the range [first,first+n).
   *  @param  first  An input iterator.
   *  @param  n      The number of copies to make.
   *  @param  x      The source value.
   *  @return   first+n
   *
   *  Like fill_n(), but does not require an initialized output range.
  */
  template<typename _ForwardIter, typename _Size, typename _Tp>
    inline _ForwardIter 
    uninitialized_fill_n(_ForwardIter __first, _Size __n, const _Tp& __x)
    {
      typedef typename iterator_traits<_ForwardIter>::value_type _ValueType;
      typedef typename __type_traits<_ValueType>::is_POD_type _Is_POD;
      return __uninitialized_fill_n_aux(__first, __n, __x, _Is_POD());
    }

  // Extensions: __uninitialized_copy_copy, __uninitialized_copy_fill, 
  // __uninitialized_fill_copy.

  // __uninitialized_copy_copy
  // Copies [first1, last1) into [result, result + (last1 - first1)), and
  //  copies [first2, last2) into
  //  [result, result + (last1 - first1) + (last2 - first2)).

  template<typename _InputIter1, typename _InputIter2, typename _ForwardIter>
    inline _ForwardIter
    __uninitialized_copy_copy(_InputIter1 __first1, _InputIter1 __last1,
			      _InputIter2 __first2, _InputIter2 __last2,
			      _ForwardIter __result)
    {
      _ForwardIter __mid = uninitialized_copy(__first1, __last1, __result);
      try {
	return uninitialized_copy(__first2, __last2, __mid);
      }
      catch(...)
	{ 
	  _Destroy(__result, __mid);
	  __throw_exception_again; 
	}
    }

  // __uninitialized_fill_copy
  // Fills [result, mid) with x, and copies [first, last) into
  //  [mid, mid + (last - first)).
  template<typename _ForwardIter, typename _Tp, typename _InputIter>
    inline _ForwardIter 
    __uninitialized_fill_copy(_ForwardIter __result, _ForwardIter __mid,
			      const _Tp& __x,
			      _InputIter __first, _InputIter __last)
    {
      uninitialized_fill(__result, __mid, __x);
      try {
	return uninitialized_copy(__first, __last, __mid);
      }
      catch(...)
	{
	  _Destroy(__result, __mid);
	  __throw_exception_again; 
	}
    }

  // __uninitialized_copy_fill
  // Copies [first1, last1) into [first2, first2 + (last1 - first1)), and
  //  fills [first2 + (last1 - first1), last2) with x.
  template<typename _InputIter, typename _ForwardIter, typename _Tp>
    inline void
    __uninitialized_copy_fill(_InputIter __first1, _InputIter __last1,
			      _ForwardIter __first2, _ForwardIter __last2,
			      const _Tp& __x)
    {
      _ForwardIter __mid2 = uninitialized_copy(__first1, __last1, __first2);
      try {
	uninitialized_fill(__mid2, __last2, __x);
      }
      catch(...)
	{
	  _Destroy(__first2, __mid2);
	  __throw_exception_again; 
	}
    }

} // namespace std

#endif /* _CPP_BITS_STL_UNINITIALIZED_H */

// Local Variables:
// mode:C++
// End:
