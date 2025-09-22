// vector<bool> specialization -*- C++ -*-

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
 * Copyright (c) 1996-1999
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

/** @file stl_bvector.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _BVECTOR_H
#define _BVECTOR_H 1

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  typedef unsigned long _Bit_type;
  enum { _S_word_bit = int(CHAR_BIT * sizeof(_Bit_type)) };

  struct _Bit_reference
  {
    _Bit_type * _M_p;
    _Bit_type _M_mask;

    _Bit_reference(_Bit_type * __x, _Bit_type __y)
    : _M_p(__x), _M_mask(__y) { }

    _Bit_reference() : _M_p(0), _M_mask(0) { }

    operator bool() const
    { return !!(*_M_p & _M_mask); }

    _Bit_reference&
    operator=(bool __x)
    {
      if (__x)
	*_M_p |= _M_mask;
      else
	*_M_p &= ~_M_mask;
      return *this;
    }

    _Bit_reference&
    operator=(const _Bit_reference& __x)
    { return *this = bool(__x); }

    bool
    operator==(const _Bit_reference& __x) const
    { return bool(*this) == bool(__x); }

    bool
    operator<(const _Bit_reference& __x) const
    { return !bool(*this) && bool(__x); }

    void
    flip()
    { *_M_p ^= _M_mask; }
  };

  struct _Bit_iterator_base
  : public std::iterator<std::random_access_iterator_tag, bool>
  {
    _Bit_type * _M_p;
    unsigned int _M_offset;

    _Bit_iterator_base(_Bit_type * __x, unsigned int __y)
    : _M_p(__x), _M_offset(__y) { }

    void
    _M_bump_up()
    {
      if (_M_offset++ == int(_S_word_bit) - 1)
	{
	  _M_offset = 0;
	  ++_M_p;
	}
    }

    void
    _M_bump_down()
    {
      if (_M_offset-- == 0)
	{
	  _M_offset = int(_S_word_bit) - 1;
	  --_M_p;
	}
    }

    void
    _M_incr(ptrdiff_t __i)
    {
      difference_type __n = __i + _M_offset;
      _M_p += __n / int(_S_word_bit);
      __n = __n % int(_S_word_bit);
      if (__n < 0)
	{
	  __n += int(_S_word_bit);
	  --_M_p;
	}
      _M_offset = static_cast<unsigned int>(__n);
    }

    bool
    operator==(const _Bit_iterator_base& __i) const
    { return _M_p == __i._M_p && _M_offset == __i._M_offset; }

    bool
    operator<(const _Bit_iterator_base& __i) const
    {
      return _M_p < __i._M_p
	     || (_M_p == __i._M_p && _M_offset < __i._M_offset);
    }

    bool
    operator!=(const _Bit_iterator_base& __i) const
    { return !(*this == __i); }

    bool
    operator>(const _Bit_iterator_base& __i) const
    { return __i < *this; }

    bool
    operator<=(const _Bit_iterator_base& __i) const
    { return !(__i < *this); }

    bool
    operator>=(const _Bit_iterator_base& __i) const
    { return !(*this < __i); }
  };

  inline ptrdiff_t
  operator-(const _Bit_iterator_base& __x, const _Bit_iterator_base& __y)
  {
    return (int(_S_word_bit) * (__x._M_p - __y._M_p)
	    + __x._M_offset - __y._M_offset);
  }

  struct _Bit_iterator : public _Bit_iterator_base
  {
    typedef _Bit_reference  reference;
    typedef _Bit_reference* pointer;
    typedef _Bit_iterator   iterator;

    _Bit_iterator() : _Bit_iterator_base(0, 0) { }

    _Bit_iterator(_Bit_type * __x, unsigned int __y)
    : _Bit_iterator_base(__x, __y) { }

    reference
    operator*() const
    { return reference(_M_p, 1UL << _M_offset); }

    iterator&
    operator++()
    {
      _M_bump_up();
      return *this;
    }

    iterator
    operator++(int)
    {
      iterator __tmp = *this;
      _M_bump_up();
      return __tmp;
    }

    iterator&
    operator--()
    {
      _M_bump_down();
      return *this;
    }

    iterator
    operator--(int)
    {
      iterator __tmp = *this;
      _M_bump_down();
      return __tmp;
    }

    iterator&
    operator+=(difference_type __i)
    {
      _M_incr(__i);
      return *this;
    }

    iterator&
    operator-=(difference_type __i)
    {
      *this += -__i;
      return *this;
    }

    iterator
    operator+(difference_type __i) const
    {
      iterator __tmp = *this;
      return __tmp += __i;
    }

    iterator
    operator-(difference_type __i) const
    {
      iterator __tmp = *this;
      return __tmp -= __i;
    }

    reference
    operator[](difference_type __i) const
    { return *(*this + __i); }
  };

  inline _Bit_iterator
  operator+(ptrdiff_t __n, const _Bit_iterator& __x)
  { return __x + __n; }

  struct _Bit_const_iterator : public _Bit_iterator_base
  {
    typedef bool                 reference;
    typedef bool                 const_reference;
    typedef const bool*          pointer;
    typedef _Bit_const_iterator  const_iterator;

    _Bit_const_iterator() : _Bit_iterator_base(0, 0) { }

    _Bit_const_iterator(_Bit_type * __x, unsigned int __y)
    : _Bit_iterator_base(__x, __y) { }

    _Bit_const_iterator(const _Bit_iterator& __x)
    : _Bit_iterator_base(__x._M_p, __x._M_offset) { }

    const_reference
    operator*() const
    { return _Bit_reference(_M_p, 1UL << _M_offset); }

    const_iterator&
    operator++()
    {
      _M_bump_up();
      return *this;
    }

    const_iterator
    operator++(int)
    {
      const_iterator __tmp = *this;
      _M_bump_up();
      return __tmp;
    }

    const_iterator&
    operator--()
    {
      _M_bump_down();
      return *this;
    }

    const_iterator
    operator--(int)
    {
      const_iterator __tmp = *this;
      _M_bump_down();
      return __tmp;
    }

    const_iterator&
    operator+=(difference_type __i)
    {
      _M_incr(__i);
      return *this;
    }

    const_iterator&
    operator-=(difference_type __i)
    {
      *this += -__i;
      return *this;
    }

    const_iterator 
    operator+(difference_type __i) const
    {
      const_iterator __tmp = *this;
      return __tmp += __i;
    }

    const_iterator
    operator-(difference_type __i) const
    {
      const_iterator __tmp = *this;
      return __tmp -= __i;
    }

    const_reference
    operator[](difference_type __i) const
    { return *(*this + __i); }
  };

  inline _Bit_const_iterator
  operator+(ptrdiff_t __n, const _Bit_const_iterator& __x)
  { return __x + __n; }

  inline void
  __fill_bvector(_Bit_iterator __first, _Bit_iterator __last, bool __x)
  {
    for (; __first != __last; ++__first)
      *__first = __x;
  }

  inline void
  fill(_Bit_iterator __first, _Bit_iterator __last, const bool& __x)
  {
    if (__first._M_p != __last._M_p)
      {
	std::fill(__first._M_p + 1, __last._M_p, __x ? ~0 : 0);
	__fill_bvector(__first, _Bit_iterator(__first._M_p + 1, 0), __x);
	__fill_bvector(_Bit_iterator(__last._M_p, 0), __last, __x);
      }
    else
      __fill_bvector(__first, __last, __x);
  }

  template<class _Alloc>
    struct _Bvector_base
    {
      typedef typename _Alloc::template rebind<_Bit_type>::other
        _Bit_alloc_type;
      
      struct _Bvector_impl
      : public _Bit_alloc_type
      {
	_Bit_iterator 	_M_start;
	_Bit_iterator 	_M_finish;
	_Bit_type* 	_M_end_of_storage;
	_Bvector_impl(const _Bit_alloc_type& __a)
	: _Bit_alloc_type(__a), _M_start(), _M_finish(), _M_end_of_storage(0)
	{ }
      };

    public:
      typedef _Alloc allocator_type;

      _Bit_alloc_type&
      _M_get_Bit_allocator()
      { return *static_cast<_Bit_alloc_type*>(&this->_M_impl); }

      const _Bit_alloc_type&
      _M_get_Bit_allocator() const
      { return *static_cast<const _Bit_alloc_type*>(&this->_M_impl); }

      allocator_type
      get_allocator() const
      { return allocator_type(_M_get_Bit_allocator()); }

      _Bvector_base(const allocator_type& __a) : _M_impl(__a) { }

      ~_Bvector_base()
      { this->_M_deallocate(); }

    protected:
      _Bvector_impl _M_impl;

      _Bit_type*
      _M_allocate(size_t __n)
      { return _M_impl.allocate((__n + int(_S_word_bit) - 1)
				/ int(_S_word_bit)); }

      void
      _M_deallocate()
      {
	if (_M_impl._M_start._M_p)
	  _M_impl.deallocate(_M_impl._M_start._M_p,
			     _M_impl._M_end_of_storage - _M_impl._M_start._M_p);
      }
    };

_GLIBCXX_END_NESTED_NAMESPACE

// Declare a partial specialization of vector<T, Alloc>.
#include <bits/stl_vector.h>

_GLIBCXX_BEGIN_NESTED_NAMESPACE(std, _GLIBCXX_STD)

  /**
   *  @brief  A specialization of vector for booleans which offers fixed time
   *  access to individual elements in any order.
   *
   *  Note that vector<bool> does not actually meet the requirements for being
   *  a container.  This is because the reference and pointer types are not
   *  really references and pointers to bool.  See DR96 for details.  @see
   *  vector for function documentation.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  In some terminology a %vector can be described as a dynamic
   *  C-style array, it offers fast and efficient access to individual
   *  elements in any order and saves the user from worrying about
   *  memory and size allocation.  Subscripting ( @c [] ) access is
   *  also provided as with C-style arrays.
  */
template<typename _Alloc>
  class vector<bool, _Alloc> : protected _Bvector_base<_Alloc>
  {
    typedef _Bvector_base<_Alloc>			 _Base;

  public:
    typedef bool                                         value_type;
    typedef size_t                                       size_type;
    typedef ptrdiff_t                                    difference_type;
    typedef _Bit_reference                               reference;
    typedef bool                                         const_reference;
    typedef _Bit_reference*                              pointer;
    typedef const bool*                                  const_pointer;
    typedef _Bit_iterator                                iterator;
    typedef _Bit_const_iterator                          const_iterator;
    typedef std::reverse_iterator<const_iterator>        const_reverse_iterator;
    typedef std::reverse_iterator<iterator>              reverse_iterator;
    typedef _Alloc                        		 allocator_type;

    allocator_type get_allocator() const
    { return _Base::get_allocator(); }

  protected:
    using _Base::_M_allocate;
    using _Base::_M_deallocate;
    using _Base::_M_get_Bit_allocator;

  public:
    explicit
    vector(const allocator_type& __a = allocator_type())
    : _Base(__a) { }

    explicit
    vector(size_type __n, const bool& __value = bool(), 
	   const allocator_type& __a = allocator_type())
    : _Base(__a)
    {
      _M_initialize(__n);
      std::fill(this->_M_impl._M_start._M_p, this->_M_impl._M_end_of_storage, 
		__value ? ~0 : 0);
    }

    vector(const vector& __x)
    : _Base(__x._M_get_Bit_allocator())
    {
      _M_initialize(__x.size());
      _M_copy_aligned(__x.begin(), __x.end(), this->_M_impl._M_start);
    }

    template<class _InputIterator>
      vector(_InputIterator __first, _InputIterator __last,
	     const allocator_type& __a = allocator_type())
      : _Base(__a)
      {
	typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	_M_initialize_dispatch(__first, __last, _Integral());
      }

    ~vector() { }

    vector&
    operator=(const vector& __x)
    {
      if (&__x == this)
	return *this;
      if (__x.size() > capacity())
	{
	  this->_M_deallocate();
	  _M_initialize(__x.size());
	}
      this->_M_impl._M_finish = _M_copy_aligned(__x.begin(), __x.end(),
						begin());
      return *this;
    }

    // assign(), a generalized assignment member function.  Two
    // versions: one that takes a count, and one that takes a range.
    // The range version is a member template, so we dispatch on whether
    // or not the type is an integer.
    void
    assign(size_type __n, const bool& __x)
    { _M_fill_assign(__n, __x); }

    template<class _InputIterator>
      void
      assign(_InputIterator __first, _InputIterator __last)
      {
	typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	_M_assign_dispatch(__first, __last, _Integral());
      }

    iterator
    begin()
    { return this->_M_impl._M_start; }

    const_iterator
    begin() const
    { return this->_M_impl._M_start; }

    iterator
    end()
    { return this->_M_impl._M_finish; }

    const_iterator
    end() const
    { return this->_M_impl._M_finish; }

    reverse_iterator
    rbegin()
    { return reverse_iterator(end()); }

    const_reverse_iterator
    rbegin() const
    { return const_reverse_iterator(end()); }

    reverse_iterator
    rend()
    { return reverse_iterator(begin()); }

    const_reverse_iterator
    rend() const
    { return const_reverse_iterator(begin()); }

    size_type
    size() const
    { return size_type(end() - begin()); }

    size_type
    max_size() const
    {
      const size_type __asize = _M_get_Bit_allocator().max_size();
      return (__asize <= size_type(-1) / int(_S_word_bit) ?
	      __asize * int(_S_word_bit) : size_type(-1));
    }

    size_type
    capacity() const
    { return size_type(const_iterator(this->_M_impl._M_end_of_storage, 0)
		       - begin()); }

    bool
    empty() const
    { return begin() == end(); }

    reference
    operator[](size_type __n)
    {
      return *iterator(this->_M_impl._M_start._M_p
		       + __n / int(_S_word_bit), __n % int(_S_word_bit));
    }

    const_reference
    operator[](size_type __n) const
    {
      return *const_iterator(this->_M_impl._M_start._M_p
			     + __n / int(_S_word_bit), __n % int(_S_word_bit));
    }

  protected:
    void
    _M_range_check(size_type __n) const
    {
      if (__n >= this->size())
        __throw_out_of_range(__N("vector<bool>::_M_range_check"));
    }

  public:
    reference
    at(size_type __n)
    { _M_range_check(__n); return (*this)[__n]; }

    const_reference
    at(size_type __n) const
    { _M_range_check(__n); return (*this)[__n]; }

    void
    reserve(size_type __n)
    {
      if (__n > this->max_size())
	__throw_length_error(__N("vector::reserve"));
      if (this->capacity() < __n)
	{
	  _Bit_type* __q = this->_M_allocate(__n);
	  this->_M_impl._M_finish = _M_copy_aligned(begin(), end(),
						    iterator(__q, 0));
	  this->_M_deallocate();
	  this->_M_impl._M_start = iterator(__q, 0);
	  this->_M_impl._M_end_of_storage = (__q + (__n + int(_S_word_bit) - 1)
					     / int(_S_word_bit));
	}
    }

    reference
    front()
    { return *begin(); }

    const_reference
    front() const
    { return *begin(); }

    reference
    back()
    { return *(end() - 1); }

    const_reference
    back() const
    { return *(end() - 1); }

    // _GLIBCXX_RESOLVE_LIB_DEFECTS
    // DR 464. Suggestion for new member functions in standard containers.
    // N.B. DR 464 says nothing about vector<bool> but we need something
    // here due to the way we are implementing DR 464 in the debug-mode
    // vector class.
    void
    data() { }

    void
    push_back(bool __x)
    {
      if (this->_M_impl._M_finish._M_p != this->_M_impl._M_end_of_storage)
        *this->_M_impl._M_finish++ = __x;
      else
        _M_insert_aux(end(), __x);
    }

    void
    swap(vector<bool, _Alloc>& __x)
    {
      std::swap(this->_M_impl._M_start, __x._M_impl._M_start);
      std::swap(this->_M_impl._M_finish, __x._M_impl._M_finish);
      std::swap(this->_M_impl._M_end_of_storage, 
		__x._M_impl._M_end_of_storage);

      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 431. Swapping containers with unequal allocators.
      std::__alloc_swap<typename _Base::_Bit_alloc_type>::
	_S_do_it(_M_get_Bit_allocator(), __x._M_get_Bit_allocator());
    }

    // [23.2.5]/1, third-to-last entry in synopsis listing
    static void
    swap(reference __x, reference __y)
    {
      bool __tmp = __x;
      __x = __y;
      __y = __tmp;
    }

    iterator
    insert(iterator __position, const bool& __x = bool())
    {
      const difference_type __n = __position - begin();
      if (this->_M_impl._M_finish._M_p != this->_M_impl._M_end_of_storage
	  && __position == end())
        *this->_M_impl._M_finish++ = __x;
      else
        _M_insert_aux(__position, __x);
      return begin() + __n;
    }

    template<class _InputIterator>
      void
      insert(iterator __position,
	     _InputIterator __first, _InputIterator __last)
      {
	typedef typename std::__is_integer<_InputIterator>::__type _Integral;
	_M_insert_dispatch(__position, __first, __last, _Integral());
      }

    void
    insert(iterator __position, size_type __n, const bool& __x)
    { _M_fill_insert(__position, __n, __x); }

    void
    pop_back()
    { --this->_M_impl._M_finish; }

    iterator
    erase(iterator __position)
    {
      if (__position + 1 != end())
        std::copy(__position + 1, end(), __position);
      --this->_M_impl._M_finish;
      return __position;
    }

    iterator
    erase(iterator __first, iterator __last)
    {
      _M_erase_at_end(std::copy(__last, end(), __first));
      return __first;
    }

    void
    resize(size_type __new_size, bool __x = bool())
    {
      if (__new_size < size())
        _M_erase_at_end(begin() + difference_type(__new_size));
      else
        insert(end(), __new_size - size(), __x);
    }

    void
    flip()
    {
      for (_Bit_type * __p = this->_M_impl._M_start._M_p;
	   __p != this->_M_impl._M_end_of_storage; ++__p)
        *__p = ~*__p;
    }

    void
    clear()
    { _M_erase_at_end(begin()); }

   
  protected:
    // Precondition: __first._M_offset == 0 && __result._M_offset == 0.
    iterator
    _M_copy_aligned(const_iterator __first, const_iterator __last,
		    iterator __result)
    {
      _Bit_type* __q = std::copy(__first._M_p, __last._M_p, __result._M_p);
      return std::copy(const_iterator(__last._M_p, 0), __last,
		       iterator(__q, 0));
    }

    void
    _M_initialize(size_type __n)
    {
      _Bit_type* __q = this->_M_allocate(__n);
      this->_M_impl._M_end_of_storage = (__q
					 + ((__n + int(_S_word_bit) - 1)
					    / int(_S_word_bit)));
      this->_M_impl._M_start = iterator(__q, 0);
      this->_M_impl._M_finish = this->_M_impl._M_start + difference_type(__n);
    }

    // Check whether it's an integral type.  If so, it's not an iterator.
    template<class _Integer>
      void
      _M_initialize_dispatch(_Integer __n, _Integer __x, __true_type)
      {
	_M_initialize(__n);
	std::fill(this->_M_impl._M_start._M_p, 
		  this->_M_impl._M_end_of_storage, __x ? ~0 : 0);
      }

    template<class _InputIterator>
      void 
      _M_initialize_dispatch(_InputIterator __first, _InputIterator __last,
			     __false_type)
      { _M_initialize_range(__first, __last, 
			    std::__iterator_category(__first)); }

    template<class _InputIterator>
      void
      _M_initialize_range(_InputIterator __first, _InputIterator __last,
			  std::input_iterator_tag)
      {
	for (; __first != __last; ++__first)
	  push_back(*__first);
      }

    template<class _ForwardIterator>
      void
      _M_initialize_range(_ForwardIterator __first, _ForwardIterator __last,
			  std::forward_iterator_tag)
      {
	const size_type __n = std::distance(__first, __last);
	_M_initialize(__n);
	std::copy(__first, __last, this->_M_impl._M_start);
      }

    template<class _Integer>
      void
      _M_assign_dispatch(_Integer __n, _Integer __val, __true_type)
      { _M_fill_assign((size_t) __n, (bool) __val); }

    template<class _InputIterator>
      void
      _M_assign_dispatch(_InputIterator __first, _InputIterator __last,
			 __false_type)
      { _M_assign_aux(__first, __last, std::__iterator_category(__first)); }

    void
    _M_fill_assign(size_t __n, bool __x)
    {
      if (__n > size())
	{
	  std::fill(this->_M_impl._M_start._M_p, 
		    this->_M_impl._M_end_of_storage, __x ? ~0 : 0);
	  insert(end(), __n - size(), __x);
	}
      else
	{
	  _M_erase_at_end(begin() + __n);
	  std::fill(this->_M_impl._M_start._M_p, 
		    this->_M_impl._M_end_of_storage, __x ? ~0 : 0);
	}
    }

    template<class _InputIterator>
      void
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
    
    template<class _ForwardIterator>
      void
      _M_assign_aux(_ForwardIterator __first, _ForwardIterator __last,
		    std::forward_iterator_tag)
      {
	const size_type __len = std::distance(__first, __last);
	if (__len < size())
	  _M_erase_at_end(std::copy(__first, __last, begin()));
	else
	  {
	    _ForwardIterator __mid = __first;
	    std::advance(__mid, size());
	    std::copy(__first, __mid, begin());
	    insert(end(), __mid, __last);
	  }
      }

    // Check whether it's an integral type.  If so, it's not an iterator.
    template<class _Integer>
      void
      _M_insert_dispatch(iterator __pos, _Integer __n, _Integer __x,
			 __true_type)
      { _M_fill_insert(__pos, __n, __x); }

    template<class _InputIterator>
      void
      _M_insert_dispatch(iterator __pos,
			 _InputIterator __first, _InputIterator __last,
			 __false_type)
      { _M_insert_range(__pos, __first, __last,
			std::__iterator_category(__first)); }

    void
    _M_fill_insert(iterator __position, size_type __n, bool __x)
    {
      if (__n == 0)
	return;
      if (capacity() - size() >= __n)
	{
	  std::copy_backward(__position, end(),
			     this->_M_impl._M_finish + difference_type(__n));
	  std::fill(__position, __position + difference_type(__n), __x);
	  this->_M_impl._M_finish += difference_type(__n);
	}
      else
	{
	  const size_type __len = size() + std::max(size(), __n);
	  _Bit_type * __q = this->_M_allocate(__len);
	  iterator __i = _M_copy_aligned(begin(), __position,
					 iterator(__q, 0));
	  std::fill(__i, __i + difference_type(__n), __x);
	  this->_M_impl._M_finish = std::copy(__position, end(),
					      __i + difference_type(__n));
	  this->_M_deallocate();
	  this->_M_impl._M_end_of_storage = (__q + ((__len
						     + int(_S_word_bit) - 1)
						    / int(_S_word_bit)));
	  this->_M_impl._M_start = iterator(__q, 0);
	}
    }

    template<class _InputIterator>
      void
      _M_insert_range(iterator __pos, _InputIterator __first, 
		      _InputIterator __last, std::input_iterator_tag)
      {
	for (; __first != __last; ++__first)
	  {
	    __pos = insert(__pos, *__first);
	    ++__pos;
	  }
      }

    template<class _ForwardIterator>
      void
      _M_insert_range(iterator __position, _ForwardIterator __first, 
		      _ForwardIterator __last, std::forward_iterator_tag)
      {
	if (__first != __last)
	  {
	    size_type __n = std::distance(__first, __last);
	    if (capacity() - size() >= __n)
	      {
		std::copy_backward(__position, end(),
				   this->_M_impl._M_finish
				   + difference_type(__n));
		std::copy(__first, __last, __position);
		this->_M_impl._M_finish += difference_type(__n);
	      }
	    else
	      {
		const size_type __len = size() + std::max(size(), __n);
		_Bit_type * __q = this->_M_allocate(__len);
		iterator __i = _M_copy_aligned(begin(), __position,
					       iterator(__q, 0));
		__i = std::copy(__first, __last, __i);
		this->_M_impl._M_finish = std::copy(__position, end(), __i);
		this->_M_deallocate();
		this->_M_impl._M_end_of_storage = (__q
						   + ((__len
						       + int(_S_word_bit) - 1)
						      / int(_S_word_bit)));
		this->_M_impl._M_start = iterator(__q, 0);
	      }
	  }
      }

    void
    _M_insert_aux(iterator __position, bool __x)
    {
      if (this->_M_impl._M_finish._M_p != this->_M_impl._M_end_of_storage)
	{
	  std::copy_backward(__position, this->_M_impl._M_finish, 
			     this->_M_impl._M_finish + 1);
	  *__position = __x;
	  ++this->_M_impl._M_finish;
	}
      else
	{
	  const size_type __len = size() ? 2 * size()
	                                 : static_cast<size_type>(_S_word_bit);
	  _Bit_type * __q = this->_M_allocate(__len);
	  iterator __i = _M_copy_aligned(begin(), __position,
					 iterator(__q, 0));
	  *__i++ = __x;
	  this->_M_impl._M_finish = std::copy(__position, end(), __i);
	  this->_M_deallocate();
	  this->_M_impl._M_end_of_storage = (__q + ((__len
						     + int(_S_word_bit) - 1)
						    / int(_S_word_bit)));
	  this->_M_impl._M_start = iterator(__q, 0);
	}
    }

    void
    _M_erase_at_end(iterator __pos)
    { this->_M_impl._M_finish = __pos; }
  };

_GLIBCXX_END_NESTED_NAMESPACE

#endif
