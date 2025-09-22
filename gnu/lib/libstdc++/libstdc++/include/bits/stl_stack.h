// Stack implementation -*- C++ -*-

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

/** @file stl_stack.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_STACK_H
#define __GLIBCPP_INTERNAL_STACK_H

#include <bits/concept_check.h>

namespace std
{
  // Forward declarations of operators == and <, needed for friend declaration.
  
  template <typename _Tp, typename _Sequence = deque<_Tp> >
  class stack;
  
  template <typename _Tp, typename _Seq>
  inline bool operator==(const stack<_Tp,_Seq>& __x,
	                 const stack<_Tp,_Seq>& __y);
  
  template <typename _Tp, typename _Seq>
  inline bool operator<(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y);
  
  
  /**
   *  @brief  A standard container giving FILO behavior.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets many of the requirements of a
   *  <a href="tables.html#65">container</a>,
   *  but does not define anything to do with iterators.  Very few of the
   *  other standard container interfaces are defined.
   *
   *  This is not a true container, but an @e adaptor.  It holds another
   *  container, and provides a wrapper interface to that container.  The
   *  wrapper is what enforces strict first-in-last-out %stack behavior.
   *
   *  The second template parameter defines the type of the underlying
   *  sequence/container.  It defaults to std::deque, but it can be any type
   *  that supports @c back, @c push_back, and @c pop_front, such as
   *  std::list, std::vector, or an appropriate user-defined type.
   *
   *  Members not found in "normal" containers are @c container_type,
   *  which is a typedef for the second Sequence parameter, and @c push,
   *  @c pop, and @c top, which are standard %stack/FILO operations.
  */
  template <typename _Tp, typename _Sequence>
    class stack
  {
    // concept requirements
    typedef typename _Sequence::value_type _Sequence_value_type;
    __glibcpp_class_requires(_Tp, _SGIAssignableConcept)
    __glibcpp_class_requires(_Sequence, _BackInsertionSequenceConcept)
    __glibcpp_class_requires2(_Tp, _Sequence_value_type, _SameTypeConcept)
  
    template <typename _Tp1, typename _Seq1>
    friend bool operator== (const stack<_Tp1, _Seq1>&,
                            const stack<_Tp1, _Seq1>&);
    template <typename _Tp1, typename _Seq1>
    friend bool operator< (const stack<_Tp1, _Seq1>&,
                           const stack<_Tp1, _Seq1>&);
  
  public:
    typedef typename _Sequence::value_type                value_type;
    typedef typename _Sequence::reference                 reference;
    typedef typename _Sequence::const_reference           const_reference;
    typedef typename _Sequence::size_type                 size_type;
    typedef          _Sequence                            container_type;
  
  protected:
    //  See queue::c for notes on this name.
    _Sequence c;
  
  public:
    // XXX removed old def ctor, added def arg to this one to match 14882
    /**
     *  @brief  Default constructor creates no elements.
    */
    explicit
    stack(const _Sequence& __c = _Sequence())
    : c(__c) {}
  
    /**
     *  Returns true if the %stack is empty.
    */
    bool
    empty() const { return c.empty(); }
  
    /**  Returns the number of elements in the %stack.  */
    size_type
    size() const { return c.size(); }
  
    /**
     *  Returns a read/write reference to the data at the first element of the
     *  %stack.
    */
    reference
    top() { return c.back(); }
  
    /**
     *  Returns a read-only (constant) reference to the data at the first
     *  element of the %stack.
    */
    const_reference
    top() const { return c.back(); }
  
    /**
     *  @brief  Add data to the top of the %stack.
     *  @param  x  Data to be added.
     *
     *  This is a typical %stack operation.  The function creates an element at
     *  the top of the %stack and assigns the given data to it.
     *  The time complexity of the operation depends on the underlying
     *  sequence.
    */
    void
    push(const value_type& __x) { c.push_back(__x); }
  
    /**
     *  @brief  Removes first element.
     *
     *  This is a typical %stack operation.  It shrinks the %stack by one.
     *  The time complexity of the operation depends on the underlying
     *  sequence.
     *
     *  Note that no data is returned, and if the first element's data is
     *  needed, it should be retrieved before pop() is called.
    */
    void
    pop() { c.pop_back(); }
  };
  
  
  /**
   *  @brief  Stack equality comparison.
   *  @param  x  A %stack.
   *  @param  y  A %stack of the same type as @a x.
   *  @return  True iff the size and elements of the stacks are equal.
   *
   *  This is an equivalence relation.  Complexity and semantics depend on the
   *  underlying sequence type, but the expected rules are:  this relation is
   *  linear in the size of the sequences, and stacks are considered equivalent
   *  if their sequences compare equal.
  */
  template <typename _Tp, typename _Seq>
    inline bool
    operator==(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return __x.c == __y.c; }
  
  /**
   *  @brief  Stack ordering relation.
   *  @param  x  A %stack.
   *  @param  y  A %stack of the same type as @a x.
   *  @return  True iff @a x is lexographically less than @a y.
   *
   *  This is an total ordering relation.  Complexity and semantics depend on
   *  the underlying sequence type, but the expected rules are:  this relation
   *  is linear in the size of the sequences, the elements must be comparable
   *  with @c <, and std::lexographical_compare() is usually used to make the
   *  determination.
  */
  template <typename _Tp, typename _Seq>
    inline bool
    operator<(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return __x.c < __y.c; }
  
  /// Based on operator==
  template <typename _Tp, typename _Seq>
    inline bool
    operator!=(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return !(__x == __y); }
  
  /// Based on operator<
  template <typename _Tp, typename _Seq>
    inline bool
    operator>(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return __y < __x; }
  
  /// Based on operator<
  template <typename _Tp, typename _Seq>
    inline bool
    operator<=(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return !(__y < __x); }
  
  /// Based on operator<
  template <typename _Tp, typename _Seq>
    inline bool
    operator>=(const stack<_Tp,_Seq>& __x, const stack<_Tp,_Seq>& __y)
    { return !(__x < __y); }
} // namespace std

#endif /* __GLIBCPP_INTERNAL_STACK_H */
