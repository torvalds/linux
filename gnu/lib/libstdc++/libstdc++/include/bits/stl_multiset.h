// Multiset implementation -*- C++ -*-

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

/** @file stl_multiset.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_MULTISET_H
#define __GLIBCPP_INTERNAL_MULTISET_H

#include <bits/concept_check.h>

namespace std
{

// Forward declaration of operators < and ==, needed for friend declaration.

template <class _Key, class _Compare = less<_Key>,
          class _Alloc = allocator<_Key> >
class multiset;

template <class _Key, class _Compare, class _Alloc>
inline bool operator==(const multiset<_Key,_Compare,_Alloc>& __x, 
                       const multiset<_Key,_Compare,_Alloc>& __y);

template <class _Key, class _Compare, class _Alloc>
inline bool operator<(const multiset<_Key,_Compare,_Alloc>& __x, 
                      const multiset<_Key,_Compare,_Alloc>& __y);

template <class _Key, class _Compare, class _Alloc>
class multiset
{
  // concept requirements
  __glibcpp_class_requires(_Key, _SGIAssignableConcept)
  __glibcpp_class_requires4(_Compare, bool, _Key, _Key, _BinaryFunctionConcept);

public:

  // typedefs:

  typedef _Key     key_type;
  typedef _Key     value_type;
  typedef _Compare key_compare;
  typedef _Compare value_compare;
private:
  typedef _Rb_tree<key_type, value_type, 
                  _Identity<value_type>, key_compare, _Alloc> _Rep_type;
  _Rep_type _M_t;  // red-black tree representing multiset
public:
  typedef typename _Alloc::pointer pointer;
  typedef typename _Alloc::const_pointer const_pointer;
  typedef typename _Alloc::reference reference;
  typedef typename _Alloc::const_reference const_reference;
  typedef typename _Rep_type::const_iterator iterator;
  typedef typename _Rep_type::const_iterator const_iterator;
  typedef typename _Rep_type::const_reverse_iterator reverse_iterator;
  typedef typename _Rep_type::const_reverse_iterator const_reverse_iterator;
  typedef typename _Rep_type::size_type size_type;
  typedef typename _Rep_type::difference_type difference_type;
  typedef typename _Rep_type::allocator_type allocator_type;

  // allocation/deallocation

  multiset() : _M_t(_Compare(), allocator_type()) {}
  explicit multiset(const _Compare& __comp,
                    const allocator_type& __a = allocator_type())
    : _M_t(__comp, __a) {}

  template <class _InputIterator>
  multiset(_InputIterator __first, _InputIterator __last)
    : _M_t(_Compare(), allocator_type())
    { _M_t.insert_equal(__first, __last); }

  template <class _InputIterator>
  multiset(_InputIterator __first, _InputIterator __last,
           const _Compare& __comp,
           const allocator_type& __a = allocator_type())
    : _M_t(__comp, __a) { _M_t.insert_equal(__first, __last); }

  multiset(const multiset<_Key,_Compare,_Alloc>& __x) : _M_t(__x._M_t) {}

  multiset<_Key,_Compare,_Alloc>&
  operator=(const multiset<_Key,_Compare,_Alloc>& __x) {
    _M_t = __x._M_t; 
    return *this;
  }

  // accessors:

  key_compare key_comp() const { return _M_t.key_comp(); }
  value_compare value_comp() const { return _M_t.key_comp(); }
  allocator_type get_allocator() const { return _M_t.get_allocator(); }

  iterator begin() const { return _M_t.begin(); }
  iterator end() const { return _M_t.end(); }
  reverse_iterator rbegin() const { return _M_t.rbegin(); } 
  reverse_iterator rend() const { return _M_t.rend(); }
  bool empty() const { return _M_t.empty(); }
  size_type size() const { return _M_t.size(); }
  size_type max_size() const { return _M_t.max_size(); }
  void swap(multiset<_Key,_Compare,_Alloc>& __x) { _M_t.swap(__x._M_t); }

  // insert/erase
  iterator insert(const value_type& __x) { 
    return _M_t.insert_equal(__x);
  }
  iterator insert(iterator __position, const value_type& __x) {
    typedef typename _Rep_type::iterator _Rep_iterator;
    return _M_t.insert_equal((_Rep_iterator&)__position, __x);
  }

  template <class _InputIterator>
  void insert(_InputIterator __first, _InputIterator __last) {
    _M_t.insert_equal(__first, __last);
  }
  void erase(iterator __position) { 
    typedef typename _Rep_type::iterator _Rep_iterator;
    _M_t.erase((_Rep_iterator&)__position); 
  }
  size_type erase(const key_type& __x) { 
    return _M_t.erase(__x); 
  }
  void erase(iterator __first, iterator __last) { 
    typedef typename _Rep_type::iterator _Rep_iterator;
    _M_t.erase((_Rep_iterator&)__first, (_Rep_iterator&)__last); 
  }
  void clear() { _M_t.clear(); }

  // multiset operations:

  size_type count(const key_type& __x) const { return _M_t.count(__x); }

#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
//214.  set::find() missing const overload
  iterator find(const key_type& __x) { return _M_t.find(__x); }
  const_iterator find(const key_type& __x) const { return _M_t.find(__x); }
  iterator lower_bound(const key_type& __x) {
    return _M_t.lower_bound(__x);
  }
  const_iterator lower_bound(const key_type& __x) const {
    return _M_t.lower_bound(__x);
  }
  iterator upper_bound(const key_type& __x) {
    return _M_t.upper_bound(__x);
  }
  const_iterator upper_bound(const key_type& __x) const {
    return _M_t.upper_bound(__x);
  }
  pair<iterator,iterator> equal_range(const key_type& __x) {
    return _M_t.equal_range(__x);
  }
  pair<const_iterator,const_iterator> equal_range(const key_type& __x) const {
    return _M_t.equal_range(__x);
  }
#else
  iterator find(const key_type& __x) const { return _M_t.find(__x); }
  iterator lower_bound(const key_type& __x) const {
    return _M_t.lower_bound(__x);
  }
  iterator upper_bound(const key_type& __x) const {
    return _M_t.upper_bound(__x); 
  }
  pair<iterator,iterator> equal_range(const key_type& __x) const {
    return _M_t.equal_range(__x);
  }
#endif

  template <class _K1, class _C1, class _A1>
  friend bool operator== (const multiset<_K1,_C1,_A1>&,
                          const multiset<_K1,_C1,_A1>&);
  template <class _K1, class _C1, class _A1>
  friend bool operator< (const multiset<_K1,_C1,_A1>&,
                         const multiset<_K1,_C1,_A1>&);
};

template <class _Key, class _Compare, class _Alloc>
inline bool operator==(const multiset<_Key,_Compare,_Alloc>& __x, 
                       const multiset<_Key,_Compare,_Alloc>& __y) {
  return __x._M_t == __y._M_t;
}

template <class _Key, class _Compare, class _Alloc>
inline bool operator<(const multiset<_Key,_Compare,_Alloc>& __x, 
                      const multiset<_Key,_Compare,_Alloc>& __y) {
  return __x._M_t < __y._M_t;
}

template <class _Key, class _Compare, class _Alloc>
inline bool operator!=(const multiset<_Key,_Compare,_Alloc>& __x, 
                       const multiset<_Key,_Compare,_Alloc>& __y) {
  return !(__x == __y);
}

template <class _Key, class _Compare, class _Alloc>
inline bool operator>(const multiset<_Key,_Compare,_Alloc>& __x, 
                      const multiset<_Key,_Compare,_Alloc>& __y) {
  return __y < __x;
}

template <class _Key, class _Compare, class _Alloc>
inline bool operator<=(const multiset<_Key,_Compare,_Alloc>& __x, 
                       const multiset<_Key,_Compare,_Alloc>& __y) {
  return !(__y < __x);
}

template <class _Key, class _Compare, class _Alloc>
inline bool operator>=(const multiset<_Key,_Compare,_Alloc>& __x, 
                       const multiset<_Key,_Compare,_Alloc>& __y) {
  return !(__x < __y);
}

template <class _Key, class _Compare, class _Alloc>
inline void swap(multiset<_Key,_Compare,_Alloc>& __x, 
                 multiset<_Key,_Compare,_Alloc>& __y) {
  __x.swap(__y);
}

} // namespace std

#endif /* __GLIBCPP_INTERNAL_MULTISET_H */

// Local Variables:
// mode:C++
// End:
