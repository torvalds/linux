// Pair implementation -*- C++ -*-

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

/** @file stl_pair.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_PAIR_H
#define __GLIBCPP_INTERNAL_PAIR_H

namespace std
{

/// pair holds two objects of arbitrary type.
template <class _T1, class _T2>
struct pair {
  typedef _T1 first_type;    ///<  @c first_type is the first bound type
  typedef _T2 second_type;   ///<  @c second_type is the second bound type

  _T1 first;                 ///< @c first is a copy of the first object
  _T2 second;                ///< @c second is a copy of the second object
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
//265.  std::pair::pair() effects overly restrictive
  /** The default constructor creates @c first and @c second using their
   *  respective default constructors.  */
  pair() : first(), second() {}
#else
  pair() : first(_T1()), second(_T2()) {}
#endif
  /** Two objects may be passed to a @c pair constructor to be copied.  */
  pair(const _T1& __a, const _T2& __b) : first(__a), second(__b) {}

  /** There is also a templated copy ctor for the @c pair class itself.  */
  template <class _U1, class _U2>
  pair(const pair<_U1, _U2>& __p) : first(__p.first), second(__p.second) {}
};

/// Two pairs of the same type are equal iff their members are equal.
template <class _T1, class _T2>
inline bool operator==(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y)
{ 
  return __x.first == __y.first && __x.second == __y.second; 
}

/// <http://gcc.gnu.org/onlinedocs/libstdc++/20_util/howto.html#pairlt>
template <class _T1, class _T2>
inline bool operator<(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y)
{ 
  return __x.first < __y.first || 
         (!(__y.first < __x.first) && __x.second < __y.second); 
}

/// Uses @c operator== to find the result.
template <class _T1, class _T2>
inline bool operator!=(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y) {
  return !(__x == __y);
}

/// Uses @c operator< to find the result.
template <class _T1, class _T2>
inline bool operator>(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y) {
  return __y < __x;
}

/// Uses @c operator< to find the result.
template <class _T1, class _T2>
inline bool operator<=(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y) {
  return !(__y < __x);
}

/// Uses @c operator< to find the result.
template <class _T1, class _T2>
inline bool operator>=(const pair<_T1, _T2>& __x, const pair<_T1, _T2>& __y) {
  return !(__x < __y);
}

/**
 *  @brief A convenience wrapper for creating a pair from two objects.
 *  @param  x  The first object.
 *  @param  y  The second object.
 *  @return   A newly-constructed pair<> object of the appropriate type.
 *
 *  The standard requires that the objects be passed by reference-to-const,
 *  but LWG issue #181 says they should be passed by const value.  We follow
 *  the LWG by default.
*/
template <class _T1, class _T2>
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
//181.  make_pair() unintended behavior
inline pair<_T1, _T2> make_pair(_T1 __x, _T2 __y)
#else
inline pair<_T1, _T2> make_pair(const _T1& __x, const _T2& __y)
#endif
{
  return pair<_T1, _T2>(__x, __y);
}

} // namespace std

#endif /* __GLIBCPP_INTERNAL_PAIR_H */

// Local Variables:
// mode:C++
// End:
