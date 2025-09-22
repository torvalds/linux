// Deque implementation (out of line) -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
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
 * Copyright (c) 1997
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

/** @file deque.tcc
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _DEQUE_TCC
#define _DEQUE_TCC 1

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  template <typename _Tp, typename _Alloc>
    deque<_Tp, _Alloc>&
    deque<_Tp, _Alloc>::
    operator=(const deque& __x)
    {
      const size_type __len = size();
      if (&__x != this)
	{
	  if (__len >= __x.size())
	    _M_erase_at_end(std::copy(__x.begin(), __x.end(),
				      this->_M_impl._M_start));
	  else
	    {
	      const_iterator __mid = __x.begin() + difference_type(__len);
	      std::copy(__x.begin(), __mid, this->_M_impl._M_start);
	      insert(this->_M_impl._M_finish, __mid, __x.end());
	    }
	}
      return *this;
    }

  template <typename _Tp, typename _Alloc>
    typename deque<_Tp, _Alloc>::iterator
    deque<_Tp, _Alloc>::
    insert(iterator __position, const value_type& __x)
    {
      if (__position._M_cur == this->_M_impl._M_start._M_cur)
	{
	  push_front(__x);
	  return this->_M_impl._M_start;
	}
      else if (__position._M_cur == this->_M_impl._M_finish._M_cur)
	{
	  push_back(__x);
	  iterator __tmp = this->_M_impl._M_finish;
	  --__tmp;
	  return __tmp;
	}
      else
        return _M_insert_aux(__position, __x);
    }

  template <typename _Tp, typename _Alloc>
    typename deque<_Tp, _Alloc>::iterator
    deque<_Tp, _Alloc>::
    erase(iterator __position)
    {
      iterator __next = __position;
      ++__next;
      const difference_type __index = __position - begin();
      if (static_cast<size_type>(__index) < (size() >> 1))
	{
	  if (__position != begin())
	    std::copy_backward(begin(), __position, __next);
	  pop_front();
	}
      else
	{
	  if (__next != end())
	    std::copy(__next, end(), __position);
	  pop_back();
	}
      return begin() + __index;
    }

  template <typename _Tp, typename _Alloc>
    typename deque<_Tp, _Alloc>::iterator
    deque<_Tp, _Alloc>::
    erase(iterator __first, iterator __last)
    {
      if (__first == begin() && __last == end())
	{
	  clear();
	  return end();
	}
      else
	{
	  const difference_type __n = __last - __first;
	  const difference_type __elems_before = __first - begin();
	  if (static_cast<size_type>(__elems_before) <= (size() - __n) / 2)
	    {
	      if (__first != begin())
		std::copy_backward(begin(), __first, __last);
	      _M_erase_at_begin(begin() + __n);
	    }
	  else
	    {
	      if (__last != end())
		std::copy(__last, end(), __first);
	      _M_erase_at_end(end() - __n);
	    }
	  return begin() + __elems_before;
	}
    }

  template <typename _Tp, class _Alloc>
    template <typename _InputIterator>
      void
      deque<_Tp, _Alloc>::
      _M_assign_aux(_InputIterator __first, _InputIterator __last,
		    std::input_iterator_tag)
      {
        iterator __cur = begin();
        for (; __first != __last && __cur != end(); ++__cur, ++__first)
          *__cur = *__first;
        if (__first == __last)
          _M_erase_at_end(__cur);
        else
          insert(end(), __first, __last);
      }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_fill_insert(iterator __pos, size_type __n, const value_type& __x)
    {
      if (__pos._M_cur == this->_M_impl._M_start._M_cur)
	{
	  iterator __new_start = _M_reserve_elements_at_front(__n);
	  try
	    {
	      std::__uninitialized_fill_a(__new_start, this->_M_impl._M_start,
					  __x, _M_get_Tp_allocator());
	      this->_M_impl._M_start = __new_start;
	    }
	  catch(...)
	    {
	      _M_destroy_nodes(__new_start._M_node,
			       this->_M_impl._M_start._M_node);
	      __throw_exception_again;
	    }
	}
      else if (__pos._M_cur == this->_M_impl._M_finish._M_cur)
	{
	  iterator __new_finish = _M_reserve_elements_at_back(__n);
	  try
	    {
	      std::__uninitialized_fill_a(this->_M_impl._M_finish,
					  __new_finish, __x,
					  _M_get_Tp_allocator());
	      this->_M_impl._M_finish = __new_finish;
	    }
	  catch(...)
	    {
	      _M_destroy_nodes(this->_M_impl._M_finish._M_node + 1,
			       __new_finish._M_node + 1);
	      __throw_exception_again;
	    }
	}
      else
        _M_insert_aux(__pos, __n, __x);
    }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_fill_initialize(const value_type& __value)
    {
      _Map_pointer __cur;
      try
        {
          for (__cur = this->_M_impl._M_start._M_node;
	       __cur < this->_M_impl._M_finish._M_node;
	       ++__cur)
            std::__uninitialized_fill_a(*__cur, *__cur + _S_buffer_size(),
					__value, _M_get_Tp_allocator());
          std::__uninitialized_fill_a(this->_M_impl._M_finish._M_first,
				      this->_M_impl._M_finish._M_cur,
				      __value, _M_get_Tp_allocator());
        }
      catch(...)
        {
          std::_Destroy(this->_M_impl._M_start, iterator(*__cur, __cur),
			_M_get_Tp_allocator());
          __throw_exception_again;
        }
    }

  template <typename _Tp, typename _Alloc>
    template <typename _InputIterator>
      void
      deque<_Tp, _Alloc>::
      _M_range_initialize(_InputIterator __first, _InputIterator __last,
                          std::input_iterator_tag)
      {
        this->_M_initialize_map(0);
        try
          {
            for (; __first != __last; ++__first)
              push_back(*__first);
          }
        catch(...)
          {
            clear();
            __throw_exception_again;
          }
      }

  template <typename _Tp, typename _Alloc>
    template <typename _ForwardIterator>
      void
      deque<_Tp, _Alloc>::
      _M_range_initialize(_ForwardIterator __first, _ForwardIterator __last,
                          std::forward_iterator_tag)
      {
        const size_type __n = std::distance(__first, __last);
        this->_M_initialize_map(__n);

        _Map_pointer __cur_node;
        try
          {
            for (__cur_node = this->_M_impl._M_start._M_node;
                 __cur_node < this->_M_impl._M_finish._M_node;
                 ++__cur_node)
	      {
		_ForwardIterator __mid = __first;
		std::advance(__mid, _S_buffer_size());
		std::__uninitialized_copy_a(__first, __mid, *__cur_node,
					    _M_get_Tp_allocator());
		__first = __mid;
	      }
            std::__uninitialized_copy_a(__first, __last,
					this->_M_impl._M_finish._M_first,
					_M_get_Tp_allocator());
          }
        catch(...)
          {
            std::_Destroy(this->_M_impl._M_start,
			  iterator(*__cur_node, __cur_node),
			  _M_get_Tp_allocator());
            __throw_exception_again;
          }
      }

  // Called only if _M_impl._M_finish._M_cur == _M_impl._M_finish._M_last - 1.
  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_push_back_aux(const value_type& __t)
    {
      value_type __t_copy = __t;
      _M_reserve_map_at_back();
      *(this->_M_impl._M_finish._M_node + 1) = this->_M_allocate_node();
      try
        {
          this->_M_impl.construct(this->_M_impl._M_finish._M_cur, __t_copy);
          this->_M_impl._M_finish._M_set_node(this->_M_impl._M_finish._M_node
					      + 1);
          this->_M_impl._M_finish._M_cur = this->_M_impl._M_finish._M_first;
        }
      catch(...)
        {
          _M_deallocate_node(*(this->_M_impl._M_finish._M_node + 1));
          __throw_exception_again;
        }
    }

  // Called only if _M_impl._M_start._M_cur == _M_impl._M_start._M_first.
  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_push_front_aux(const value_type& __t)
    {
      value_type __t_copy = __t;
      _M_reserve_map_at_front();
      *(this->_M_impl._M_start._M_node - 1) = this->_M_allocate_node();
      try
        {
          this->_M_impl._M_start._M_set_node(this->_M_impl._M_start._M_node
					     - 1);
          this->_M_impl._M_start._M_cur = this->_M_impl._M_start._M_last - 1;
          this->_M_impl.construct(this->_M_impl._M_start._M_cur, __t_copy);
        }
      catch(...)
        {
          ++this->_M_impl._M_start;
          _M_deallocate_node(*(this->_M_impl._M_start._M_node - 1));
          __throw_exception_again;
        }
    }

  // Called only if _M_impl._M_finish._M_cur == _M_impl._M_finish._M_first.
  template <typename _Tp, typename _Alloc>
    void deque<_Tp, _Alloc>::
    _M_pop_back_aux()
    {
      _M_deallocate_node(this->_M_impl._M_finish._M_first);
      this->_M_impl._M_finish._M_set_node(this->_M_impl._M_finish._M_node - 1);
      this->_M_impl._M_finish._M_cur = this->_M_impl._M_finish._M_last - 1;
      this->_M_impl.destroy(this->_M_impl._M_finish._M_cur);
    }

  // Called only if _M_impl._M_start._M_cur == _M_impl._M_start._M_last - 1.
  // Note that if the deque has at least one element (a precondition for this
  // member function), and if
  //   _M_impl._M_start._M_cur == _M_impl._M_start._M_last,
  // then the deque must have at least two nodes.
  template <typename _Tp, typename _Alloc>
    void deque<_Tp, _Alloc>::
    _M_pop_front_aux()
    {
      this->_M_impl.destroy(this->_M_impl._M_start._M_cur);
      _M_deallocate_node(this->_M_impl._M_start._M_first);
      this->_M_impl._M_start._M_set_node(this->_M_impl._M_start._M_node + 1);
      this->_M_impl._M_start._M_cur = this->_M_impl._M_start._M_first;
    }

  template <typename _Tp, typename _Alloc>
    template <typename _InputIterator>
      void
      deque<_Tp, _Alloc>::
      _M_range_insert_aux(iterator __pos,
                          _InputIterator __first, _InputIterator __last,
                          std::input_iterator_tag)
      { std::copy(__first, __last, std::inserter(*this, __pos)); }

  template <typename _Tp, typename _Alloc>
    template <typename _ForwardIterator>
      void
      deque<_Tp, _Alloc>::
      _M_range_insert_aux(iterator __pos,
                          _ForwardIterator __first, _ForwardIterator __last,
                          std::forward_iterator_tag)
      {
        const size_type __n = std::distance(__first, __last);
        if (__pos._M_cur == this->_M_impl._M_start._M_cur)
	  {
	    iterator __new_start = _M_reserve_elements_at_front(__n);
	    try
	      {
		std::__uninitialized_copy_a(__first, __last, __new_start,
					    _M_get_Tp_allocator());
		this->_M_impl._M_start = __new_start;
	      }
	    catch(...)
	      {
		_M_destroy_nodes(__new_start._M_node,
				 this->_M_impl._M_start._M_node);
		__throw_exception_again;
	      }
	  }
        else if (__pos._M_cur == this->_M_impl._M_finish._M_cur)
	  {
	    iterator __new_finish = _M_reserve_elements_at_back(__n);
	    try
	      {
		std::__uninitialized_copy_a(__first, __last,
					    this->_M_impl._M_finish,
					    _M_get_Tp_allocator());
		this->_M_impl._M_finish = __new_finish;
	      }
	    catch(...)
	      {
		_M_destroy_nodes(this->_M_impl._M_finish._M_node + 1,
				 __new_finish._M_node + 1);
		__throw_exception_again;
	      }
	  }
        else
          _M_insert_aux(__pos, __first, __last, __n);
      }

  template <typename _Tp, typename _Alloc>
    typename deque<_Tp, _Alloc>::iterator
    deque<_Tp, _Alloc>::
    _M_insert_aux(iterator __pos, const value_type& __x)
    {
      difference_type __index = __pos - this->_M_impl._M_start;
      value_type __x_copy = __x; // XXX copy
      if (static_cast<size_type>(__index) < size() / 2)
	{
	  push_front(front());
	  iterator __front1 = this->_M_impl._M_start;
	  ++__front1;
	  iterator __front2 = __front1;
	  ++__front2;
	  __pos = this->_M_impl._M_start + __index;
	  iterator __pos1 = __pos;
	  ++__pos1;
	  std::copy(__front2, __pos1, __front1);
	}
      else
	{
	  push_back(back());
	  iterator __back1 = this->_M_impl._M_finish;
	  --__back1;
	  iterator __back2 = __back1;
	  --__back2;
	  __pos = this->_M_impl._M_start + __index;
	  std::copy_backward(__pos, __back2, __back1);
	}
      *__pos = __x_copy;
      return __pos;
    }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_insert_aux(iterator __pos, size_type __n, const value_type& __x)
    {
      const difference_type __elems_before = __pos - this->_M_impl._M_start;
      const size_type __length = this->size();
      value_type __x_copy = __x;
      if (__elems_before < difference_type(__length / 2))
	{
	  iterator __new_start = _M_reserve_elements_at_front(__n);
	  iterator __old_start = this->_M_impl._M_start;
	  __pos = this->_M_impl._M_start + __elems_before;
	  try
	    {
	      if (__elems_before >= difference_type(__n))
		{
		  iterator __start_n = (this->_M_impl._M_start
					+ difference_type(__n));
		  std::__uninitialized_copy_a(this->_M_impl._M_start,
					      __start_n, __new_start,
					      _M_get_Tp_allocator());
		  this->_M_impl._M_start = __new_start;
		  std::copy(__start_n, __pos, __old_start);
		  std::fill(__pos - difference_type(__n), __pos, __x_copy);
		}
	      else
		{
		  std::__uninitialized_copy_fill(this->_M_impl._M_start,
						 __pos, __new_start,
						 this->_M_impl._M_start,
						 __x_copy,
						 _M_get_Tp_allocator());
		  this->_M_impl._M_start = __new_start;
		  std::fill(__old_start, __pos, __x_copy);
		}
	    }
	  catch(...)
	    {
	      _M_destroy_nodes(__new_start._M_node,
			       this->_M_impl._M_start._M_node);
	      __throw_exception_again;
	    }
	}
      else
	{
	  iterator __new_finish = _M_reserve_elements_at_back(__n);
	  iterator __old_finish = this->_M_impl._M_finish;
	  const difference_type __elems_after =
	    difference_type(__length) - __elems_before;
	  __pos = this->_M_impl._M_finish - __elems_after;
	  try
	    {
	      if (__elems_after > difference_type(__n))
		{
		  iterator __finish_n = (this->_M_impl._M_finish
					 - difference_type(__n));
		  std::__uninitialized_copy_a(__finish_n,
					      this->_M_impl._M_finish,
					      this->_M_impl._M_finish,
					      _M_get_Tp_allocator());
		  this->_M_impl._M_finish = __new_finish;
		  std::copy_backward(__pos, __finish_n, __old_finish);
		  std::fill(__pos, __pos + difference_type(__n), __x_copy);
		}
	      else
		{
		  std::__uninitialized_fill_copy(this->_M_impl._M_finish,
						 __pos + difference_type(__n),
						 __x_copy, __pos,
						 this->_M_impl._M_finish,
						 _M_get_Tp_allocator());
		  this->_M_impl._M_finish = __new_finish;
		  std::fill(__pos, __old_finish, __x_copy);
		}
	    }
	  catch(...)
	    {
	      _M_destroy_nodes(this->_M_impl._M_finish._M_node + 1,
			       __new_finish._M_node + 1);
	      __throw_exception_again;
	    }
	}
    }

  template <typename _Tp, typename _Alloc>
    template <typename _ForwardIterator>
      void
      deque<_Tp, _Alloc>::
      _M_insert_aux(iterator __pos,
                    _ForwardIterator __first, _ForwardIterator __last,
                    size_type __n)
      {
        const difference_type __elemsbefore = __pos - this->_M_impl._M_start;
        const size_type __length = size();
        if (static_cast<size_type>(__elemsbefore) < __length / 2)
	  {
	    iterator __new_start = _M_reserve_elements_at_front(__n);
	    iterator __old_start = this->_M_impl._M_start;
	    __pos = this->_M_impl._M_start + __elemsbefore;
	    try
	      {
		if (__elemsbefore >= difference_type(__n))
		  {
		    iterator __start_n = (this->_M_impl._M_start
					  + difference_type(__n));
		    std::__uninitialized_copy_a(this->_M_impl._M_start,
						__start_n, __new_start,
						_M_get_Tp_allocator());
		    this->_M_impl._M_start = __new_start;
		    std::copy(__start_n, __pos, __old_start);
		    std::copy(__first, __last, __pos - difference_type(__n));
		  }
		else
		  {
		    _ForwardIterator __mid = __first;
		    std::advance(__mid, difference_type(__n) - __elemsbefore);
		    std::__uninitialized_copy_copy(this->_M_impl._M_start,
						   __pos, __first, __mid,
						   __new_start,
						   _M_get_Tp_allocator());
		    this->_M_impl._M_start = __new_start;
		    std::copy(__mid, __last, __old_start);
		  }
	      }
	    catch(...)
	      {
		_M_destroy_nodes(__new_start._M_node,
				 this->_M_impl._M_start._M_node);
		__throw_exception_again;
	      }
	  }
        else
        {
          iterator __new_finish = _M_reserve_elements_at_back(__n);
          iterator __old_finish = this->_M_impl._M_finish;
          const difference_type __elemsafter =
            difference_type(__length) - __elemsbefore;
          __pos = this->_M_impl._M_finish - __elemsafter;
          try
            {
              if (__elemsafter > difference_type(__n))
		{
		  iterator __finish_n = (this->_M_impl._M_finish
					 - difference_type(__n));
		  std::__uninitialized_copy_a(__finish_n,
					      this->_M_impl._M_finish,
					      this->_M_impl._M_finish,
					      _M_get_Tp_allocator());
		  this->_M_impl._M_finish = __new_finish;
		  std::copy_backward(__pos, __finish_n, __old_finish);
		  std::copy(__first, __last, __pos);
		}
              else
		{
		  _ForwardIterator __mid = __first;
		  std::advance(__mid, __elemsafter);
		  std::__uninitialized_copy_copy(__mid, __last, __pos,
						 this->_M_impl._M_finish,
						 this->_M_impl._M_finish,
						 _M_get_Tp_allocator());
		  this->_M_impl._M_finish = __new_finish;
		  std::copy(__first, __mid, __pos);
		}
            }
          catch(...)
            {
              _M_destroy_nodes(this->_M_impl._M_finish._M_node + 1,
			       __new_finish._M_node + 1);
              __throw_exception_again;
            }
        }
      }

   template<typename _Tp, typename _Alloc>
     void
     deque<_Tp, _Alloc>::
     _M_destroy_data_aux(iterator __first, iterator __last)
     {
       for (_Map_pointer __node = __first._M_node + 1;
	    __node < __last._M_node; ++__node)
	 std::_Destroy(*__node, *__node + _S_buffer_size(),
		       _M_get_Tp_allocator());

       if (__first._M_node != __last._M_node)
	 {
	   std::_Destroy(__first._M_cur, __first._M_last,
			 _M_get_Tp_allocator());
	   std::_Destroy(__last._M_first, __last._M_cur,
			 _M_get_Tp_allocator());
	 }
       else
	 std::_Destroy(__first._M_cur, __last._M_cur,
		       _M_get_Tp_allocator());
     }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_new_elements_at_front(size_type __new_elems)
    {
      if (this->max_size() - this->size() < __new_elems)
	__throw_length_error(__N("deque::_M_new_elements_at_front"));

      const size_type __new_nodes = ((__new_elems + _S_buffer_size() - 1)
				     / _S_buffer_size());
      _M_reserve_map_at_front(__new_nodes);
      size_type __i;
      try
        {
          for (__i = 1; __i <= __new_nodes; ++__i)
            *(this->_M_impl._M_start._M_node - __i) = this->_M_allocate_node();
        }
      catch(...)
        {
          for (size_type __j = 1; __j < __i; ++__j)
            _M_deallocate_node(*(this->_M_impl._M_start._M_node - __j));
          __throw_exception_again;
        }
    }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_new_elements_at_back(size_type __new_elems)
    {
      if (this->max_size() - this->size() < __new_elems)
	__throw_length_error(__N("deque::_M_new_elements_at_back"));

      const size_type __new_nodes = ((__new_elems + _S_buffer_size() - 1)
				     / _S_buffer_size());
      _M_reserve_map_at_back(__new_nodes);
      size_type __i;
      try
        {
          for (__i = 1; __i <= __new_nodes; ++__i)
            *(this->_M_impl._M_finish._M_node + __i) = this->_M_allocate_node();
        }
      catch(...)
        {
          for (size_type __j = 1; __j < __i; ++__j)
            _M_deallocate_node(*(this->_M_impl._M_finish._M_node + __j));
          __throw_exception_again;
        }
    }

  template <typename _Tp, typename _Alloc>
    void
    deque<_Tp, _Alloc>::
    _M_reallocate_map(size_type __nodes_to_add, bool __add_at_front)
    {
      const size_type __old_num_nodes
	= this->_M_impl._M_finish._M_node - this->_M_impl._M_start._M_node + 1;
      const size_type __new_num_nodes = __old_num_nodes + __nodes_to_add;

      _Map_pointer __new_nstart;
      if (this->_M_impl._M_map_size > 2 * __new_num_nodes)
	{
	  __new_nstart = this->_M_impl._M_map + (this->_M_impl._M_map_size
					 - __new_num_nodes) / 2
	                 + (__add_at_front ? __nodes_to_add : 0);
	  if (__new_nstart < this->_M_impl._M_start._M_node)
	    std::copy(this->_M_impl._M_start._M_node,
		      this->_M_impl._M_finish._M_node + 1,
		      __new_nstart);
	  else
	    std::copy_backward(this->_M_impl._M_start._M_node,
			       this->_M_impl._M_finish._M_node + 1,
			       __new_nstart + __old_num_nodes);
	}
      else
	{
	  size_type __new_map_size = this->_M_impl._M_map_size
	                             + std::max(this->_M_impl._M_map_size,
						__nodes_to_add) + 2;

	  _Map_pointer __new_map = this->_M_allocate_map(__new_map_size);
	  __new_nstart = __new_map + (__new_map_size - __new_num_nodes) / 2
	                 + (__add_at_front ? __nodes_to_add : 0);
	  std::copy(this->_M_impl._M_start._M_node,
		    this->_M_impl._M_finish._M_node + 1,
		    __new_nstart);
	  _M_deallocate_map(this->_M_impl._M_map, this->_M_impl._M_map_size);

	  this->_M_impl._M_map = __new_map;
	  this->_M_impl._M_map_size = __new_map_size;
	}

      this->_M_impl._M_start._M_set_node(__new_nstart);
      this->_M_impl._M_finish._M_set_node(__new_nstart + __old_num_nodes - 1);
    }

  // Overload for deque::iterators, exploiting the "segmented-iterator
  // optimization".  NB: leave const_iterators alone!
  template<typename _Tp>
    void
    fill(const _Deque_iterator<_Tp, _Tp&, _Tp*>& __first,
	 const _Deque_iterator<_Tp, _Tp&, _Tp*>& __last, const _Tp& __value)
    {
      typedef typename _Deque_iterator<_Tp, _Tp&, _Tp*>::_Self _Self;

      for (typename _Self::_Map_pointer __node = __first._M_node + 1;
           __node < __last._M_node; ++__node)
	std::fill(*__node, *__node + _Self::_S_buffer_size(), __value);

      if (__first._M_node != __last._M_node)
	{
	  std::fill(__first._M_cur, __first._M_last, __value);
	  std::fill(__last._M_first, __last._M_cur, __value);
	}
      else
	std::fill(__first._M_cur, __last._M_cur, __value);
    }

_GLIBCXX_END_NESTED_NAMESPACE

#endif
