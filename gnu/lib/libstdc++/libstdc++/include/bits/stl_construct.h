// nonstandard construct and destroy functions -*- C++ -*-

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

/** @file stl_construct.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_STL_CONSTRUCT_H
#define _CPP_BITS_STL_CONSTRUCT_H 1

#include <bits/type_traits.h>
#include <new>

namespace std
{
  /**
   * @if maint
   * Constructs an object in existing memory by invoking an allocated
   * object's constructor with an initializer.
   * @endif
   */
  template <class _T1, class _T2>
    inline void
    _Construct(_T1* __p, const _T2& __value)
    { new (static_cast<void*>(__p)) _T1(__value); }
  
  /**
   * @if maint
   * Constructs an object in existing memory by invoking an allocated
   * object's default constructor (no initializers).
   * @endif
   */
  template <class _T1>
    inline void
    _Construct(_T1* __p)
    { new (static_cast<void*>(__p)) _T1(); }

  /**
   * @if maint
   * Destroy a range of objects with nontrivial destructors.  
   *
   * This is a helper function used only by _Destroy().
   * @endif
   */
  template <class _ForwardIterator>
    inline void
    __destroy_aux(_ForwardIterator __first, _ForwardIterator __last, __false_type)
    { for ( ; __first != __last; ++__first) _Destroy(&*__first); }

  /**
   * @if maint
   * Destroy a range of objects with trivial destructors.  Since the destructors
   * are trivial, there's nothing to do and hopefully this function will be
   * entirely optimized away.
   *
   * This is a helper function used only by _Destroy().
   * @endif
   */
  template <class _ForwardIterator> 
    inline void
    __destroy_aux(_ForwardIterator, _ForwardIterator, __true_type)
    { }

  /**
   * @if maint
   * Destroy the object pointed to by a pointer type.
   * @endif
   */
  template <class _Tp>
    inline void
    _Destroy(_Tp* __pointer)
    { __pointer->~_Tp(); }
  
  /**
   * @if maint
   * Destroy a range of objects.  If the value_type of the object has
   * a trivial destructor, the compiler should optimize all of this
   * away, otherwise the objects' destructors must be invoked.
   * @endif
   */
  template <class _ForwardIterator>
    inline void
    _Destroy(_ForwardIterator __first, _ForwardIterator __last)
    {
      typedef typename iterator_traits<_ForwardIterator>::value_type
                       _Value_type;
      typedef typename __type_traits<_Value_type>::has_trivial_destructor
                       _Has_trivial_destructor;

      __destroy_aux(__first, __last, _Has_trivial_destructor());
    }
} // namespace std

#endif /* _CPP_BITS_STL_CONSTRUCT_H */

