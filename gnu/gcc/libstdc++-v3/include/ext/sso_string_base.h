// Short-string-optimized versatile string base -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
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

/** @file ext/sso_string_base.h
 *  This file is a GNU extension to the Standard C++ Library.
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _SSO_STRING_BASE_H
#define _SSO_STRING_BASE_H 1

_GLIBCXX_BEGIN_NAMESPACE(__gnu_cxx)

  template<typename _CharT, typename _Traits, typename _Alloc>
    class __sso_string_base
    : protected __vstring_utility<_CharT, _Traits, _Alloc>
    {
    public:
      typedef _Traits					    traits_type;
      typedef typename _Traits::char_type		    value_type;

      typedef __vstring_utility<_CharT, _Traits, _Alloc>    _Util_Base;
      typedef typename _Util_Base::_CharT_alloc_type        _CharT_alloc_type;
      typedef typename _CharT_alloc_type::size_type	    size_type;
      
    private:
      // Data Members:
      typename _Util_Base::template _Alloc_hider<_CharT_alloc_type>
                                                            _M_dataplus;
      size_type                                             _M_string_length;

      enum { _S_local_capacity = 15 };
      
      union
      {
	_CharT           _M_local_data[_S_local_capacity + 1];
	size_type        _M_allocated_capacity;
      };

      void
      _M_data(_CharT* __p)
      { _M_dataplus._M_p = __p; }

      void
      _M_length(size_type __length)
      { _M_string_length = __length; }

      void
      _M_capacity(size_type __capacity)
      { _M_allocated_capacity = __capacity; }

      bool
      _M_is_local() const
      { return _M_data() == _M_local_data; }

      // Create & Destroy
      _CharT*
      _M_create(size_type&, size_type);
      
      void
      _M_dispose()
      {
	if (!_M_is_local())
	  _M_destroy(_M_allocated_capacity);
      }

      void
      _M_destroy(size_type __size) throw()
      { _M_get_allocator().deallocate(_M_data(), __size + 1); }

      // _M_construct_aux is used to implement the 21.3.1 para 15 which
      // requires special behaviour if _InIterator is an integral type
      template<typename _InIterator>
        void
        _M_construct_aux(_InIterator __beg, _InIterator __end, 
			 std::__false_type)
	{
          typedef typename iterator_traits<_InIterator>::iterator_category _Tag;
          _M_construct(__beg, __end, _Tag());
	}

      template<typename _InIterator>
        void
        _M_construct_aux(_InIterator __beg, _InIterator __end, 
			 std::__true_type)
	{ _M_construct(static_cast<size_type>(__beg),
		       static_cast<value_type>(__end)); }

      template<typename _InIterator>
        void
        _M_construct(_InIterator __beg, _InIterator __end)
	{
	  typedef typename std::__is_integer<_InIterator>::__type _Integral;
	  _M_construct_aux(__beg, __end, _Integral());
        }

      // For Input Iterators, used in istreambuf_iterators, etc.
      template<typename _InIterator>
        void
        _M_construct(_InIterator __beg, _InIterator __end,
		     std::input_iterator_tag);
      
      // For forward_iterators up to random_access_iterators, used for
      // string::iterator, _CharT*, etc.
      template<typename _FwdIterator>
        void
        _M_construct(_FwdIterator __beg, _FwdIterator __end,
		     std::forward_iterator_tag);

      void
      _M_construct(size_type __req, _CharT __c);

    public:
      size_type
      _M_max_size() const
      { return (_M_get_allocator().max_size() - 1) / 2; }

      _CharT*
      _M_data() const
      { return _M_dataplus._M_p; }

      size_type
      _M_length() const
      { return _M_string_length; }

      size_type
      _M_capacity() const
      {
	return _M_is_local() ? size_type(_S_local_capacity)
	                     : _M_allocated_capacity; 
      }

      bool
      _M_is_shared() const
      { return false; }

      void
      _M_set_leaked() { }

      void
      _M_leak() { }

      void
      _M_set_length(size_type __n)
      {
	_M_length(__n);
	traits_type::assign(_M_data()[__n], _CharT());
      }

      __sso_string_base()
      : _M_dataplus(_Alloc(), _M_local_data)
      { _M_set_length(0); }

      __sso_string_base(const _Alloc& __a);

      __sso_string_base(const __sso_string_base& __rcs);

      __sso_string_base(size_type __n, _CharT __c, const _Alloc& __a);

      template<typename _InputIterator>
        __sso_string_base(_InputIterator __beg, _InputIterator __end,
			  const _Alloc& __a);

      ~__sso_string_base()
      { _M_dispose(); }

      _CharT_alloc_type&
      _M_get_allocator()
      { return _M_dataplus; }

      const _CharT_alloc_type&
      _M_get_allocator() const
      { return _M_dataplus; }

      void
      _M_swap(__sso_string_base& __rcs);

      void
      _M_assign(const __sso_string_base& __rcs);

      void
      _M_reserve(size_type __res);

      void
      _M_mutate(size_type __pos, size_type __len1, const _CharT* __s,
		size_type __len2);

      void
      _M_erase(size_type __pos, size_type __n);

      void
      _M_clear()
      { _M_set_length(0); }

      bool
      _M_compare(const __sso_string_base&) const
      { return false; }
    };

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_swap(__sso_string_base& __rcs)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 431. Swapping containers with unequal allocators.
      std::__alloc_swap<_CharT_alloc_type>::_S_do_it(_M_get_allocator(),
						     __rcs._M_get_allocator());

      if (_M_is_local())
	if (__rcs._M_is_local())
	  {
	    if (_M_length() && __rcs._M_length())
	      {
		_CharT __tmp_data[_S_local_capacity + 1];
		traits_type::copy(__tmp_data, __rcs._M_local_data,
				  _S_local_capacity + 1);
		traits_type::copy(__rcs._M_local_data, _M_local_data,
				  _S_local_capacity + 1);
		traits_type::copy(_M_local_data, __tmp_data,
				  _S_local_capacity + 1);
	      }
	    else if (__rcs._M_length())
	      {
		traits_type::copy(_M_local_data, __rcs._M_local_data,
				  _S_local_capacity + 1);
		_M_length(__rcs._M_length());
		__rcs._M_set_length(0);
		return;
	      }
	    else if (_M_length())
	      {
		traits_type::copy(__rcs._M_local_data, _M_local_data,
				  _S_local_capacity + 1);
		__rcs._M_length(_M_length());
		_M_set_length(0);
		return;
	      }
	  }
	else
	  {
	    const size_type __tmp_capacity = __rcs._M_allocated_capacity;
	    traits_type::copy(__rcs._M_local_data, _M_local_data,
			      _S_local_capacity + 1);
	    _M_data(__rcs._M_data());
	    __rcs._M_data(__rcs._M_local_data);
	    _M_capacity(__tmp_capacity);
	  }
      else
	{
	  const size_type __tmp_capacity = _M_allocated_capacity;
	  if (__rcs._M_is_local())
	    {
	      traits_type::copy(_M_local_data, __rcs._M_local_data,
				_S_local_capacity + 1);
	      __rcs._M_data(_M_data());
	      _M_data(_M_local_data);
	    }
	  else
	    {
	      _CharT* __tmp_ptr = _M_data();
	      _M_data(__rcs._M_data());
	      __rcs._M_data(__tmp_ptr);
	      _M_capacity(__rcs._M_allocated_capacity);
	    }
	  __rcs._M_capacity(__tmp_capacity);
	}

      const size_type __tmp_length = _M_length();
      _M_length(__rcs._M_length());
      __rcs._M_length(__tmp_length);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_create(size_type& __capacity, size_type __old_capacity)
    {
      // _GLIBCXX_RESOLVE_LIB_DEFECTS
      // 83.  String::npos vs. string::max_size()
      if (__capacity > _M_max_size())
	std::__throw_length_error(__N("__sso_string_base::_M_create"));

      // The below implements an exponential growth policy, necessary to
      // meet amortized linear time requirements of the library: see
      // http://gcc.gnu.org/ml/libstdc++/2001-07/msg00085.html.
      if (__capacity > __old_capacity && __capacity < 2 * __old_capacity)
	{
	  __capacity = 2 * __old_capacity;
	  // Never allocate a string bigger than max_size.
	  if (__capacity > _M_max_size())
	    __capacity = _M_max_size();
	}

      // NB: Need an array of char_type[__capacity], plus a terminating
      // null char_type() element.
      return _M_get_allocator().allocate(__capacity + 1);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string_base<_CharT, _Traits, _Alloc>::
    __sso_string_base(const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_set_length(0); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string_base<_CharT, _Traits, _Alloc>::
    __sso_string_base(const __sso_string_base& __rcs)
    : _M_dataplus(__rcs._M_get_allocator(), _M_local_data)
    { _M_construct(__rcs._M_data(), __rcs._M_data() + __rcs._M_length()); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    __sso_string_base<_CharT, _Traits, _Alloc>::
    __sso_string_base(size_type __n, _CharT __c, const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_construct(__n, __c); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InputIterator>
    __sso_string_base<_CharT, _Traits, _Alloc>::
    __sso_string_base(_InputIterator __beg, _InputIterator __end,
		      const _Alloc& __a)
    : _M_dataplus(__a, _M_local_data)
    { _M_construct(__beg, __end); }

  // NB: This is the special case for Input Iterators, used in
  // istreambuf_iterators, etc.
  // Input Iterators have a cost structure very different from
  // pointers, calling for a different coding style.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIterator>
      void
      __sso_string_base<_CharT, _Traits, _Alloc>::
      _M_construct(_InIterator __beg, _InIterator __end,
		   std::input_iterator_tag)
      {
	size_type __len = 0;
	size_type __capacity = size_type(_S_local_capacity);

	while (__beg != __end && __len < __capacity)
	  {
	    _M_data()[__len++] = *__beg;
	    ++__beg;
	  }
	
	try
	  {
	    while (__beg != __end)
	      {
		if (__len == __capacity)
		  {
		    // Allocate more space.
		    __capacity = __len + 1;
		    _CharT* __another = _M_create(__capacity, __len);
		    _S_copy(__another, _M_data(), __len);
		    _M_dispose();
		    _M_data(__another);
		    _M_capacity(__capacity);
		  }
		_M_data()[__len++] = *__beg;
		++__beg;
	      }
	  }
	catch(...)
	  {
	    _M_dispose();
	    __throw_exception_again;
	  }

	_M_set_length(__len);
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIterator>
      void
      __sso_string_base<_CharT, _Traits, _Alloc>::
      _M_construct(_InIterator __beg, _InIterator __end,
		   std::forward_iterator_tag)
      {
	// NB: Not required, but considered best practice.
	if (__builtin_expect(_S_is_null_pointer(__beg) && __beg != __end, 0))
	  std::__throw_logic_error(__N("__sso_string_base::"
				       "_M_construct NULL not valid"));

	size_type __dnew = static_cast<size_type>(std::distance(__beg, __end));

	if (__dnew > size_type(_S_local_capacity))
	  {
	    _M_data(_M_create(__dnew, size_type(0)));
	    _M_capacity(__dnew);
	  }

	// Check for out_of_range and length_error exceptions.
	try
	  { _S_copy_chars(_M_data(), __beg, __end); }
	catch(...)
	  {
	    _M_dispose();
	    __throw_exception_again;
	  }

	_M_set_length(__dnew);
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_construct(size_type __n, _CharT __c)
    {
      if (__n > size_type(_S_local_capacity))
	{
	  _M_data(_M_create(__n, size_type(0)));
	  _M_capacity(__n);
	}

      if (__n)
	_S_assign(_M_data(), __n, __c);

      _M_set_length(__n);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_assign(const __sso_string_base& __rcs)
    {
      if (this != &__rcs)
	{
	  const size_type __rsize = __rcs._M_length();
	  const size_type __capacity = _M_capacity();

	  if (__rsize > __capacity)
	    {
	      size_type __new_capacity = __rsize;
	      _CharT* __tmp = _M_create(__new_capacity, __capacity);
	      _M_dispose();
	      _M_data(__tmp);
	      _M_capacity(__new_capacity);
	    }

	  if (__rsize)
	    _S_copy(_M_data(), __rcs._M_data(), __rsize);

	  _M_set_length(__rsize);
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_reserve(size_type __res)
    {
      // Make sure we don't shrink below the current size.
      if (__res < _M_length())
	__res = _M_length();

      const size_type __capacity = _M_capacity();
      if (__res != __capacity)
	{
	  if (__res > __capacity
	      || __res > size_type(_S_local_capacity))
	    {
	      _CharT* __tmp = _M_create(__res, __capacity);
	      _S_copy(__tmp, _M_data(), _M_length() + 1);
	      _M_dispose();
	      _M_data(__tmp);
	      _M_capacity(__res);
	    }
	  else if (!_M_is_local())
	    {
	      _S_copy(_M_local_data, _M_data(), _M_length() + 1);
	      _M_destroy(__capacity);
	      _M_data(_M_local_data);
	    }
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_mutate(size_type __pos, size_type __len1, const _CharT* __s,
	      const size_type __len2)
    {
      const size_type __how_much = _M_length() - __pos - __len1;
      
      size_type __new_capacity = _M_length() + __len2 - __len1;
      _CharT* __r = _M_create(__new_capacity, _M_capacity());

      if (__pos)
	_S_copy(__r, _M_data(), __pos);
      if (__s && __len2)
	_S_copy(__r + __pos, __s, __len2);
      if (__how_much)
	_S_copy(__r + __pos + __len2,
		_M_data() + __pos + __len1, __how_much);
      
      _M_dispose();
      _M_data(__r);
      _M_capacity(__new_capacity);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    __sso_string_base<_CharT, _Traits, _Alloc>::
    _M_erase(size_type __pos, size_type __n)
    {
      const size_type __how_much = _M_length() - __pos - __n;

      if (__how_much && __n)
	_S_move(_M_data() + __pos, _M_data() + __pos + __n,
		__how_much);

      _M_set_length(_M_length() - __n);
    }

  template<>
    inline bool
    __sso_string_base<char, std::char_traits<char>,
		      std::allocator<char> >::
    _M_compare(const __sso_string_base& __rcs) const
    {
      if (this == &__rcs)
	return true;
      return false;
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    inline bool
    __sso_string_base<wchar_t, std::char_traits<wchar_t>,
		      std::allocator<wchar_t> >::
    _M_compare(const __sso_string_base& __rcs) const
    {
      if (this == &__rcs)
	return true;
      return false;
    }
#endif

_GLIBCXX_END_NAMESPACE

#endif /* _SSO_STRING_BASE_H */
