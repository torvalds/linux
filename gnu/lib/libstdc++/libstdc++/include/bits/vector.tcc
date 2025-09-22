// Vector implementation (out of line) -*- C++ -*-

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
 * representations about the suitability of this  software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 */

/** @file vector.tcc
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef __GLIBCPP_INTERNAL_VECTOR_TCC
#define __GLIBCPP_INTERNAL_VECTOR_TCC

namespace std
{
  template<typename _Tp, typename _Alloc>
    void
    vector<_Tp,_Alloc>::
    reserve(size_type __n)
    {
      if (__n > this->max_size())
	__throw_length_error("vector::reserve");
      if (this->capacity() < __n)
	{
	  const size_type __old_size = size();
	  pointer __tmp = _M_allocate_and_copy(__n, _M_start, _M_finish);
	  _Destroy(_M_start, _M_finish);
	  _M_deallocate(_M_start, _M_end_of_storage - _M_start);
	  _M_start = __tmp;
	  _M_finish = __tmp + __old_size;
	  _M_end_of_storage = _M_start + __n;
	}
    }
  
  template<typename _Tp, typename _Alloc>
    typename vector<_Tp,_Alloc>::iterator
    vector<_Tp,_Alloc>::
    insert(iterator __position, const value_type& __x)
    {
      size_type __n = __position - begin();
      if (_M_finish != _M_end_of_storage && __position == end())
      {
        _Construct(_M_finish, __x);
        ++_M_finish;
      }
      else
        _M_insert_aux(__position, __x);
      return begin() + __n;
    }
  
  template<typename _Tp, typename _Alloc>
    typename vector<_Tp,_Alloc>::iterator
    vector<_Tp,_Alloc>::
    erase(iterator __position)
    {
      if (__position + 1 != end())
        copy(__position + 1, end(), __position);
      --_M_finish;
      _Destroy(_M_finish);
      return __position;
    }
  
  template<typename _Tp, typename _Alloc>
    typename vector<_Tp,_Alloc>::iterator
    vector<_Tp,_Alloc>::
    erase(iterator __first, iterator __last)
    {
      iterator __i(copy(__last, end(), __first));
      _Destroy(__i, end());
      _M_finish = _M_finish - (__last - __first);
      return __first;
    }
  
  template<typename _Tp, typename _Alloc>
    vector<_Tp,_Alloc>&
    vector<_Tp,_Alloc>::
    operator=(const vector<_Tp,_Alloc>& __x)
    {
      if (&__x != this)
      {
        const size_type __xlen = __x.size();
        if (__xlen > capacity())
        {
          pointer __tmp = _M_allocate_and_copy(__xlen, __x.begin(), __x.end());
          _Destroy(_M_start, _M_finish);
          _M_deallocate(_M_start, _M_end_of_storage - _M_start);
          _M_start = __tmp;
          _M_end_of_storage = _M_start + __xlen;
        }
        else if (size() >= __xlen)
        {
          iterator __i(copy(__x.begin(), __x.end(), begin()));
          _Destroy(__i, end());
        }
        else
        {
          copy(__x.begin(), __x.begin() + size(), _M_start);
          uninitialized_copy(__x.begin() + size(), __x.end(), _M_finish);
        }
        _M_finish = _M_start + __xlen;
      }
      return *this;
    }
  
  template<typename _Tp, typename _Alloc>
    void
    vector<_Tp,_Alloc>::
    _M_fill_assign(size_t __n, const value_type& __val)
    {
      if (__n > capacity())
      {
        vector __tmp(__n, __val, get_allocator());
        __tmp.swap(*this);
      }
      else if (__n > size())
      {
        fill(begin(), end(), __val);
        _M_finish = uninitialized_fill_n(_M_finish, __n - size(), __val);
      }
      else
        erase(fill_n(begin(), __n, __val), end());
    }
  
  template<typename _Tp, typename _Alloc> template<typename _InputIter>
    void
    vector<_Tp,_Alloc>::
    _M_assign_aux(_InputIter __first, _InputIter __last, input_iterator_tag)
    {
      iterator __cur(begin());
      for ( ; __first != __last && __cur != end(); ++__cur, ++__first)
        *__cur = *__first;
      if (__first == __last)
        erase(__cur, end());
      else
        insert(end(), __first, __last);
    }
  
  template<typename _Tp, typename _Alloc> template<typename _ForwardIter>
    void
    vector<_Tp,_Alloc>::
    _M_assign_aux(_ForwardIter __first, _ForwardIter __last,
                  forward_iterator_tag)
    {
      size_type __len = distance(__first, __last);
  
      if (__len > capacity())
      {
        pointer __tmp(_M_allocate_and_copy(__len, __first, __last));
        _Destroy(_M_start, _M_finish);
        _M_deallocate(_M_start, _M_end_of_storage - _M_start);
        _M_start = __tmp;
        _M_end_of_storage = _M_finish = _M_start + __len;
      }
      else if (size() >= __len)
      {
        iterator __new_finish(copy(__first, __last, _M_start));
        _Destroy(__new_finish, end());
        _M_finish = __new_finish.base();
      }
      else
      {
        _ForwardIter __mid = __first;
        advance(__mid, size());
        copy(__first, __mid, _M_start);
        _M_finish = uninitialized_copy(__mid, __last, _M_finish);
      }
    }
  
  template<typename _Tp, typename _Alloc>
    void
    vector<_Tp,_Alloc>::
    _M_insert_aux(iterator __position, const _Tp& __x)
    {
      if (_M_finish != _M_end_of_storage)
      {
        _Construct(_M_finish, *(_M_finish - 1));
        ++_M_finish;
        _Tp __x_copy = __x;
        copy_backward(__position, iterator(_M_finish-2), iterator(_M_finish-1));
        *__position = __x_copy;
      }
      else
      {
        const size_type __old_size = size();
        const size_type __len = __old_size != 0 ? 2 * __old_size : 1;
        iterator __new_start(_M_allocate(__len));
        iterator __new_finish(__new_start);
        try
          {
            __new_finish = uninitialized_copy(iterator(_M_start), __position,
                                              __new_start);
            _Construct(__new_finish.base(), __x);
            ++__new_finish;
            __new_finish = uninitialized_copy(__position, iterator(_M_finish),
                                              __new_finish);
          }
        catch(...)
          {
            _Destroy(__new_start,__new_finish);
            _M_deallocate(__new_start.base(),__len);
            __throw_exception_again;
          }
        _Destroy(begin(), end());
        _M_deallocate(_M_start, _M_end_of_storage - _M_start);
        _M_start = __new_start.base();
        _M_finish = __new_finish.base();
        _M_end_of_storage = __new_start.base() + __len;
      }
    }
  
  #ifdef _GLIBCPP_DEPRECATED
  template<typename _Tp, typename _Alloc>
    void
    vector<_Tp,_Alloc>::
    _M_insert_aux(iterator __position)
    {
      if (_M_finish != _M_end_of_storage)
      {
        _Construct(_M_finish, *(_M_finish - 1));
        ++_M_finish;
        copy_backward(__position, iterator(_M_finish - 2),
                      iterator(_M_finish - 1));
        *__position = value_type();
      }
      else
      {
        const size_type __old_size = size();
        const size_type __len = __old_size != 0 ? 2 * __old_size : 1;
        pointer __new_start = _M_allocate(__len);
        pointer __new_finish = __new_start;
        try
          {
            __new_finish = uninitialized_copy(iterator(_M_start), __position,
                                              __new_start);
            _Construct(__new_finish);
            ++__new_finish;
            __new_finish = uninitialized_copy(__position, iterator(_M_finish),
                                              __new_finish);
          }
        catch(...)
          {
            _Destroy(__new_start,__new_finish);
            _M_deallocate(__new_start,__len);
            __throw_exception_again;
          }
        _Destroy(begin(), end());
        _M_deallocate(_M_start, _M_end_of_storage - _M_start);
        _M_start = __new_start;
        _M_finish = __new_finish;
        _M_end_of_storage = __new_start + __len;
      }
    }
  #endif
  
  template<typename _Tp, typename _Alloc>
    void
    vector<_Tp,_Alloc>::
    _M_fill_insert(iterator __position, size_type __n, const value_type& __x)
    {
      if (__n != 0)
      {
        if (size_type(_M_end_of_storage - _M_finish) >= __n) 
	  {
           value_type __x_copy = __x;
	   const size_type __elems_after = end() - __position;
	   iterator __old_finish(_M_finish);
	   if (__elems_after > __n)
	     {
	       uninitialized_copy(_M_finish - __n, _M_finish, _M_finish);
	       _M_finish += __n;
	       copy_backward(__position, __old_finish - __n, __old_finish);
	       fill(__position, __position + __n, __x_copy);
	     }
	   else
	     {
	       uninitialized_fill_n(_M_finish, __n - __elems_after, __x_copy);
	       _M_finish += __n - __elems_after;
	       uninitialized_copy(__position, __old_finish, _M_finish);
	       _M_finish += __elems_after;
	       fill(__position, __old_finish, __x_copy);
	     }
	  }
        else
	  {
	    const size_type __old_size = size();
	    const size_type __len = __old_size + max(__old_size, __n);
	    iterator __new_start(_M_allocate(__len));
	    iterator __new_finish(__new_start);
	    try
	      {
		__new_finish = uninitialized_copy(begin(), __position,
						  __new_start);
		__new_finish = uninitialized_fill_n(__new_finish, __n, __x);
		__new_finish = uninitialized_copy(__position, end(), 
						  __new_finish);
	      }
	    catch(...)
	      {
		_Destroy(__new_start,__new_finish);
		_M_deallocate(__new_start.base(),__len);
		__throw_exception_again;
	      }
	    _Destroy(_M_start, _M_finish);
	    _M_deallocate(_M_start, _M_end_of_storage - _M_start);
	    _M_start = __new_start.base();
	    _M_finish = __new_finish.base();
	    _M_end_of_storage = __new_start.base() + __len;
	  }
      }
    }
  
  template<typename _Tp, typename _Alloc> template<typename _InputIterator>
    void
    vector<_Tp,_Alloc>::
    _M_range_insert(iterator __pos,
                    _InputIterator __first, _InputIterator __last,
                    input_iterator_tag)
    {
      for ( ; __first != __last; ++__first)
      {
        __pos = insert(__pos, *__first);
        ++__pos;
      }
    }
  
  template<typename _Tp, typename _Alloc> template<typename _ForwardIterator>
    void
    vector<_Tp,_Alloc>::
    _M_range_insert(iterator __position,_ForwardIterator __first, 
		    _ForwardIterator __last, forward_iterator_tag)
    {
      if (__first != __last)
      {
        size_type __n = distance(__first, __last);
        if (size_type(_M_end_of_storage - _M_finish) >= __n)
        {
          const size_type __elems_after = end() - __position;
          iterator __old_finish(_M_finish);
          if (__elems_after > __n)
          {
            uninitialized_copy(_M_finish - __n, _M_finish, _M_finish);
            _M_finish += __n;
            copy_backward(__position, __old_finish - __n, __old_finish);
            copy(__first, __last, __position);
          }
          else
          {
            _ForwardIterator __mid = __first;
            advance(__mid, __elems_after);
            uninitialized_copy(__mid, __last, _M_finish);
            _M_finish += __n - __elems_after;
            uninitialized_copy(__position, __old_finish, _M_finish);
            _M_finish += __elems_after;
            copy(__first, __mid, __position);
          }
        }
        else
        {
          const size_type __old_size = size();
          const size_type __len = __old_size + max(__old_size, __n);
          iterator __new_start(_M_allocate(__len));
          iterator __new_finish(__new_start);
          try
            {
              __new_finish = uninitialized_copy(iterator(_M_start),
                                                __position, __new_start);
              __new_finish = uninitialized_copy(__first, __last, __new_finish);
              __new_finish = uninitialized_copy(__position, iterator(_M_finish),
                                                __new_finish);
            }
          catch(...)
            {
              _Destroy(__new_start,__new_finish);
              _M_deallocate(__new_start.base(), __len);
              __throw_exception_again;
            }
          _Destroy(_M_start, _M_finish);
          _M_deallocate(_M_start, _M_end_of_storage - _M_start);
          _M_start = __new_start.base();
          _M_finish = __new_finish.base();
          _M_end_of_storage = __new_start.base() + __len;
        }
      }
    }
} // namespace std

#endif /* __GLIBCPP_INTERNAL_VECTOR_TCC */
