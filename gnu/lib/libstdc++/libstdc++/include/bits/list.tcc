// List implementation (out of line) -*- C++ -*-

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

/** @file list.tcc
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_LIST_TCC
#define __GLIBCPP_INTERNAL_LIST_TCC

namespace std
{
  template<typename _Tp, typename _Alloc>
    void
    _List_base<_Tp,_Alloc>::
    __clear()
    {
      typedef _List_node<_Tp>  _Node;
      _Node* __cur = static_cast<_Node*>(_M_node->_M_next);
      while (__cur != _M_node)
      {
        _Node* __tmp = __cur;
        __cur = static_cast<_Node*>(__cur->_M_next);
        _Destroy(&__tmp->_M_data);
        _M_put_node(__tmp);
      }
      _M_node->_M_next = _M_node;
      _M_node->_M_prev = _M_node;
    }
  
  template<typename _Tp, typename _Alloc>
    typename list<_Tp,_Alloc>::iterator
    list<_Tp,_Alloc>::
    insert(iterator __position, const value_type& __x)
    {
      _Node* __tmp = _M_create_node(__x);
      __tmp->_M_next = __position._M_node;
      __tmp->_M_prev = __position._M_node->_M_prev;
      __position._M_node->_M_prev->_M_next = __tmp;
      __position._M_node->_M_prev = __tmp;
      return __tmp;
    }
  
  template<typename _Tp, typename _Alloc>
    typename list<_Tp,_Alloc>::iterator
    list<_Tp,_Alloc>::
    erase(iterator __position)
    {
      _List_node_base* __next_node = __position._M_node->_M_next;
      _List_node_base* __prev_node = __position._M_node->_M_prev;
      _Node* __n = static_cast<_Node*>(__position._M_node);
      __prev_node->_M_next = __next_node;
      __next_node->_M_prev = __prev_node;
      _Destroy(&__n->_M_data);
      _M_put_node(__n);
      return iterator(static_cast<_Node*>(__next_node));
    }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    resize(size_type __new_size, const value_type& __x)
    {
      iterator __i = begin();
      size_type __len = 0;
      for ( ; __i != end() && __len < __new_size; ++__i, ++__len)
        ;
      if (__len == __new_size)
        erase(__i, end());
      else                          // __i == end()
        insert(end(), __new_size - __len, __x);
    }
  
  template<typename _Tp, typename _Alloc>
    list<_Tp,_Alloc>&
    list<_Tp,_Alloc>::
    operator=(const list& __x)
    {
      if (this != &__x)
      {
        iterator __first1 = begin();
        iterator __last1 = end();
        const_iterator __first2 = __x.begin();
        const_iterator __last2 = __x.end();
        while (__first1 != __last1 && __first2 != __last2)
          *__first1++ = *__first2++;
        if (__first2 == __last2)
          erase(__first1, __last1);
        else
          insert(__last1, __first2, __last2);
      }
      return *this;
    }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    _M_fill_assign(size_type __n, const value_type& __val)
    {
      iterator __i = begin();
      for ( ; __i != end() && __n > 0; ++__i, --__n)
        *__i = __val;
      if (__n > 0)
        insert(end(), __n, __val);
      else
        erase(__i, end());
    }
  
  template<typename _Tp, typename _Alloc>
    template <typename _InputIter>
      void
      list<_Tp,_Alloc>::
      _M_assign_dispatch(_InputIter __first2, _InputIter __last2, __false_type)
      {
        iterator __first1 = begin();
        iterator __last1 = end();
        for (; __first1 != __last1 && __first2 != __last2; ++__first1, ++__first2)
          *__first1 = *__first2;
        if (__first2 == __last2)
          erase(__first1, __last1);
        else
          insert(__last1, __first2, __last2);
      }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    remove(const value_type& __value)
    {
      iterator __first = begin();
      iterator __last = end();
      while (__first != __last)
      {
        iterator __next = __first;
        ++__next;
        if (*__first == __value)
          erase(__first);
        __first = __next;
      }
    }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    unique()
    {
      iterator __first = begin();
      iterator __last = end();
      if (__first == __last) return;
      iterator __next = __first;
      while (++__next != __last)
      {
        if (*__first == *__next)
          erase(__next);
        else
          __first = __next;
        __next = __first;
      }
    }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    merge(list& __x)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 300. list::merge() specification incomplete	
      if (this != &__x)
	{
	  iterator __first1 = begin();
	  iterator __last1 = end();
	  iterator __first2 = __x.begin();
	  iterator __last2 = __x.end();
	  while (__first1 != __last1 && __first2 != __last2)
	    if (*__first2 < *__first1)
	      {
		iterator __next = __first2;
		_M_transfer(__first1, __first2, ++__next);
		__first2 = __next;
	      }
	    else
	      ++__first1;
	  if (__first2 != __last2)
	    _M_transfer(__last1, __first2, __last2);
	}
    }
  
  // FIXME put this somewhere else
  inline void
  __List_base_reverse(_List_node_base* __p)
  {
    _List_node_base* __tmp = __p;
    do {
      std::swap(__tmp->_M_next, __tmp->_M_prev);
      __tmp = __tmp->_M_prev;     // Old next node is now prev.
    } while (__tmp != __p);
  }
  
  template<typename _Tp, typename _Alloc>
    void
    list<_Tp,_Alloc>::
    sort()
    {
      // Do nothing if the list has length 0 or 1.
      if (_M_node->_M_next != _M_node && _M_node->_M_next->_M_next != _M_node)
      {
        list __carry;
        list __counter[64];
        int __fill = 0;
        while (!empty())
        {
          __carry.splice(__carry.begin(), *this, begin());
          int __i = 0;
          while(__i < __fill && !__counter[__i].empty())
          {
            __counter[__i].merge(__carry);
            __carry.swap(__counter[__i++]);
          }
          __carry.swap(__counter[__i]);
          if (__i == __fill) ++__fill;
        }
  
        for (int __i = 1; __i < __fill; ++__i)
          __counter[__i].merge(__counter[__i-1]);
        swap(__counter[__fill-1]);
      }
    }
  
  template<typename _Tp, typename _Alloc>
    template <typename _Predicate>
      void
      list<_Tp,_Alloc>::
      remove_if(_Predicate __pred)
      {
        iterator __first = begin();
        iterator __last = end();
        while (__first != __last)
        {
          iterator __next = __first;
          ++__next;
          if (__pred(*__first)) erase(__first);
          __first = __next;
        }
      }
  
  template<typename _Tp, typename _Alloc>
    template <typename _BinaryPredicate>
      void
      list<_Tp,_Alloc>::
      unique(_BinaryPredicate __binary_pred)
      {
        iterator __first = begin();
        iterator __last = end();
        if (__first == __last) return;
        iterator __next = __first;
        while (++__next != __last)
        {
          if (__binary_pred(*__first, *__next))
            erase(__next);
          else
            __first = __next;
          __next = __first;
        }
      }
  
  template<typename _Tp, typename _Alloc>
    template <typename _StrictWeakOrdering>
      void
      list<_Tp,_Alloc>::
      merge(list& __x, _StrictWeakOrdering __comp)
      {
	// _GLIBCXX_RESOLVE_LIB_DEFECTS
	// 300. list::merge() specification incomplete
	if (this != &__x)
	  {	
	    iterator __first1 = begin();
	    iterator __last1 = end();
	    iterator __first2 = __x.begin();
	    iterator __last2 = __x.end();
	    while (__first1 != __last1 && __first2 != __last2)
	      if (__comp(*__first2, *__first1))
		{
		  iterator __next = __first2;
		  _M_transfer(__first1, __first2, ++__next);
		  __first2 = __next;
		}
	      else
		++__first1;
	    if (__first2 != __last2) _M_transfer(__last1, __first2, __last2);
	  }
      }
  
  template<typename _Tp, typename _Alloc>
    template <typename _StrictWeakOrdering>
    void
    list<_Tp,_Alloc>::
    sort(_StrictWeakOrdering __comp)
    {
      // Do nothing if the list has length 0 or 1.
      if (_M_node->_M_next != _M_node && _M_node->_M_next->_M_next != _M_node)
      {
        list __carry;
        list __counter[64];
        int __fill = 0;
        while (!empty())
        {
          __carry.splice(__carry.begin(), *this, begin());
          int __i = 0;
          while(__i < __fill && !__counter[__i].empty())
          {
            __counter[__i].merge(__carry, __comp);
            __carry.swap(__counter[__i++]);
          }
          __carry.swap(__counter[__i]);
          if (__i == __fill) ++__fill;
        }
  
        for (int __i = 1; __i < __fill; ++__i)
          __counter[__i].merge(__counter[__i-1], __comp);
        swap(__counter[__fill-1]);
      }
    }
} // namespace std

#endif /* __GLIBCPP_INTERNAL_LIST_TCC */
