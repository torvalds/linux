// std::rel_ops implementation -*- C++ -*-

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
 * Copyright (c) 1996,1997
 * Silicon Graphics
 *
 * Permission to use, copy, modify, distribute and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  Silicon Graphics makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 */

/** @file stl_relops.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 *
 *  @if maint
 *  Inclusion of this file has been removed from
 *  all of the other STL headers for safety reasons, except std_utility.h.
 *  For more information, see the thread of about twenty messages starting
 *  with http://gcc.gnu.org/ml/libstdc++/2001-01/msg00223.html , or the
 *  FAQ at http://gcc.gnu.org/onlinedocs/libstdc++/faq/index.html#4_4 .
 *
 *  Short summary:  the rel_ops operators should be avoided for the present.
 *  @endif
 */

#ifndef _CPP_BITS_STL_RELOPS_H
#define _CPP_BITS_STL_RELOPS_H 1

namespace std
{
  namespace rel_ops
  {
      /** @namespace std::rel_ops
       *  @brief  The generated relational operators are sequestered here.
       */

/**
 *  @brief Defines @c != for arbitrary types, in terms of @c ==.
 *  @param  x  A thing.
 *  @param  y  Another thing.
 *  @return   x != y
 *
 *  This function uses @c == to determine its result.
*/
template <class _Tp>
inline bool operator!=(const _Tp& __x, const _Tp& __y) {
  return !(__x == __y);
}

/**
 *  @brief Defines @c > for arbitrary types, in terms of @c <.
 *  @param  x  A thing.
 *  @param  y  Another thing.
 *  @return   x > y
 *
 *  This function uses @c < to determine its result.
*/
template <class _Tp>
inline bool operator>(const _Tp& __x, const _Tp& __y) {
  return __y < __x;
}

/**
 *  @brief Defines @c <= for arbitrary types, in terms of @c <.
 *  @param  x  A thing.
 *  @param  y  Another thing.
 *  @return   x <= y
 *
 *  This function uses @c < to determine its result.
*/
template <class _Tp>
inline bool operator<=(const _Tp& __x, const _Tp& __y) {
  return !(__y < __x);
}

/**
 *  @brief Defines @c >= for arbitrary types, in terms of @c <.
 *  @param  x  A thing.
 *  @param  y  Another thing.
 *  @return   x >= y
 *
 *  This function uses @c < to determine its result.
*/
template <class _Tp>
inline bool operator>=(const _Tp& __x, const _Tp& __y) {
  return !(__x < __y);
}

  } // namespace rel_ops
} // namespace std

#endif /* _CPP_BITS_STL_RELOPS_H */

// Local Variables:
// mode:C++
// End:
