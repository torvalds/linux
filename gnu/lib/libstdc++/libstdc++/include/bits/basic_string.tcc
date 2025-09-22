// Components for manipulating sequences of characters -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002, 2003
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

//
// ISO C++ 14882: 21  Strings library
//

// This file is included by <string>.  It is not meant to be included
// separately.

// Written by Jason Merrill based upon the specification by Takanori Adachi
// in ANSI X3J16/94-0013R2.  Rewritten by Nathan Myers to ISO-14882.

#ifndef _CPP_BITS_STRING_TCC
#define _CPP_BITS_STRING_TCC 1

#pragma GCC system_header

namespace std
{
  template<typename _CharT, typename _Traits, typename _Alloc>
    const typename basic_string<_CharT, _Traits, _Alloc>::size_type 
    basic_string<_CharT, _Traits, _Alloc>::
    _Rep::_S_max_size = (((npos - sizeof(_Rep))/sizeof(_CharT)) - 1) / 4;

  template<typename _CharT, typename _Traits, typename _Alloc>
    const _CharT 
    basic_string<_CharT, _Traits, _Alloc>::
    _Rep::_S_terminal = _CharT();

  template<typename _CharT, typename _Traits, typename _Alloc>
    const typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::npos;

  // Linker sets _S_empty_rep_storage to all 0s (one reference, empty string)
  // at static init time (before static ctors are run).
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::_S_empty_rep_storage[
    (sizeof(_Rep) + sizeof(_CharT) + sizeof(size_type) - 1)/sizeof(size_type)];

  // NB: This is the special case for Input Iterators, used in
  // istreambuf_iterators, etc.
  // Input Iterators have a cost structure very different from
  // pointers, calling for a different coding style.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InIter>
      _CharT*
      basic_string<_CharT, _Traits, _Alloc>::
      _S_construct(_InIter __beg, _InIter __end, const _Alloc& __a,
		   input_iterator_tag)
      {
	if (__beg == __end && __a == _Alloc())
	  return _S_empty_rep()._M_refcopy();
	// Avoid reallocation for common case.
	_CharT __buf[100];
	size_type __i = 0;
	while (__beg != __end && __i < sizeof(__buf) / sizeof(_CharT))
	  { 
	    __buf[__i++] = *__beg; 
	    ++__beg; 
	  }
	_Rep* __r = _Rep::_S_create(__i, __a);
	traits_type::copy(__r->_M_refdata(), __buf, __i);
	__r->_M_length = __i;
	try 
	  {
	    // NB: this loop looks precisely this way because
	    // it avoids comparing __beg != __end any more
	    // than strictly necessary; != might be expensive!
	    for (;;)
	      {
		_CharT* __p = __r->_M_refdata() + __r->_M_length;
		_CharT* __last = __r->_M_refdata() + __r->_M_capacity;
		for (;;)
		  {
		    if (__beg == __end)
		      {
			__r->_M_length = __p - __r->_M_refdata();
			*__p = _Rep::_S_terminal;       // grrr.
			return __r->_M_refdata();
		      }
		    if (__p == __last)
		      break;
		    *__p++ = *__beg; 
		    ++__beg;
		  }
		// Allocate more space.
		size_type __len = __p - __r->_M_refdata();
		_Rep* __another = _Rep::_S_create(__len + 1, __a);
		traits_type::copy(__another->_M_refdata(), 
				  __r->_M_refdata(), __len);
		__r->_M_destroy(__a);
		__r = __another;
		__r->_M_length = __len;
	      }
	  }
	catch(...) 
	  {
	    __r->_M_destroy(__a); 
	    __throw_exception_again;
	  }
	return 0;
      }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    template <class _InIter>
      _CharT*
      basic_string<_CharT, _Traits, _Alloc>::
      _S_construct(_InIter __beg, _InIter __end, const _Alloc& __a, 
		   forward_iterator_tag)
      {
	if (__beg == __end && __a == _Alloc())
	  return _S_empty_rep()._M_refcopy();

	// NB: Not required, but considered best practice.
	if (__builtin_expect(__beg == _InIter(), 0))
	  __throw_logic_error("attempt to create string with null pointer");

	size_type __dnew = static_cast<size_type>(std::distance(__beg, __end));
	
	// Check for out_of_range and length_error exceptions.
	_Rep* __r = _Rep::_S_create(__dnew, __a);
	try 
	  { _S_copy_chars(__r->_M_refdata(), __beg, __end); }
	catch(...) 
	  { 
	    __r->_M_destroy(__a); 
	    __throw_exception_again;
	  }
	__r->_M_length = __dnew;

	__r->_M_refdata()[__dnew] = _Rep::_S_terminal;  // grrr.
	return __r->_M_refdata();
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    basic_string<_CharT, _Traits, _Alloc>::
    _S_construct(size_type __n, _CharT __c, const _Alloc& __a)
    {
      if (__n == 0 && __a == _Alloc())
	return _S_empty_rep()._M_refcopy();

      // Check for out_of_range and length_error exceptions.
      _Rep* __r = _Rep::_S_create(__n, __a);
      try 
	{ 
	  if (__n) 
	    traits_type::assign(__r->_M_refdata(), __n, __c); 
	}
      catch(...) 
	{ 
	  __r->_M_destroy(__a); 
	  __throw_exception_again;
	}
      __r->_M_length = __n;
      __r->_M_refdata()[__n] = _Rep::_S_terminal;  // grrr
      return __r->_M_refdata();
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const basic_string& __str)
    : _M_dataplus(__str._M_rep()->_M_grab(_Alloc(), __str.get_allocator()),
		 __str.get_allocator())
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const _Alloc& __a)
    : _M_dataplus(_S_construct(size_type(), _CharT(), __a), __a)
    { }
 
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const basic_string& __str, size_type __pos, size_type __n)
    : _M_dataplus(_S_construct(__str._M_check(__pos), 
			       __str._M_fold(__pos, __n), _Alloc()), _Alloc())
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const basic_string& __str, size_type __pos,
		 size_type __n, const _Alloc& __a)
    : _M_dataplus(_S_construct(__str._M_check(__pos), 
			       __str._M_fold(__pos, __n), __a), __a)
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const _CharT* __s, size_type __n, const _Alloc& __a)
    : _M_dataplus(_S_construct(__s, __s + __n, __a), __a)
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(const _CharT* __s, const _Alloc& __a)
    : _M_dataplus(_S_construct(__s, __s ? __s + traits_type::length(__s) :
			       __s + npos, __a), __a)
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(size_type __n, _CharT __c, const _Alloc& __a)
    : _M_dataplus(_S_construct(__n, __c, __a), __a)
    { }
 
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InputIter>
    basic_string<_CharT, _Traits, _Alloc>::
    basic_string(_InputIter __beg, _InputIter __end, const _Alloc& __a)
    : _M_dataplus(_S_construct(__beg, __end, __a), __a)
    { }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::assign(const basic_string& __str)
    {
      if (_M_rep() != __str._M_rep())
	{
	  // XXX MT
	  allocator_type __a = this->get_allocator();
	  _CharT* __tmp = __str._M_rep()->_M_grab(__a, __str.get_allocator());
	  _M_rep()->_M_dispose(__a);
	  _M_data(__tmp);
	}
      return *this;
    }

   template<typename _CharT, typename _Traits, typename _Alloc>
     basic_string<_CharT, _Traits, _Alloc>&
     basic_string<_CharT, _Traits, _Alloc>::
     assign(const basic_string& __str, size_type __pos, size_type __n)
     {
       const size_type __strsize = __str.size();
       if (__pos > __strsize)
	 __throw_out_of_range("basic_string::assign");
       const bool __testn = __n < __strsize - __pos;
       const size_type __newsize = __testn ? __n : __strsize - __pos;
       return this->assign(__str._M_data() + __pos, __newsize);
     }

   template<typename _CharT, typename _Traits, typename _Alloc>
     basic_string<_CharT, _Traits, _Alloc>&
     basic_string<_CharT, _Traits, _Alloc>::
     assign(const _CharT* __s, size_type __n)
     {
       if (__n > this->max_size())
	 __throw_length_error("basic_string::assign");
       if (_M_rep()->_M_is_shared() || less<const _CharT*>()(__s, _M_data())
	   || less<const _CharT*>()(_M_data() + this->size(), __s))
	 return _M_replace_safe(_M_ibegin(), _M_iend(), __s, __s + __n);
       else
	 {
	   // Work in-place
	   const size_type __pos = __s - _M_data();
	   if (__pos >= __n)
	     traits_type::copy(_M_data(), __s, __n);
	   else if (__pos)
	     traits_type::move(_M_data(), __s, __n);
	   _M_rep()->_M_length = __n;
	   _M_data()[__n] = _Rep::_S_terminal;  // grr.
	   return *this;
	 }
     }

   template<typename _CharT, typename _Traits, typename _Alloc>
     basic_string<_CharT, _Traits, _Alloc>&
     basic_string<_CharT, _Traits, _Alloc>::
     insert(size_type __pos1, const basic_string& __str,
            size_type __pos2, size_type __n)
     {
       const size_type __strsize = __str.size();
       if (__pos2 > __strsize)
	 __throw_out_of_range("basic_string::insert");
       const bool __testn = __n < __strsize - __pos2;
       const size_type __newsize = __testn ? __n : __strsize - __pos2;
       return this->insert(__pos1, __str._M_data() + __pos2, __newsize);
     }

   template<typename _CharT, typename _Traits, typename _Alloc>
     basic_string<_CharT, _Traits, _Alloc>&
     basic_string<_CharT, _Traits, _Alloc>::
     insert(size_type __pos, const _CharT* __s, size_type __n)
     {
       const size_type __size = this->size();
       if (__pos > __size)
         __throw_out_of_range("basic_string::insert");
       if (__size > this->max_size() - __n)
         __throw_length_error("basic_string::insert");
       if (_M_rep()->_M_is_shared() || less<const _CharT*>()(__s, _M_data())
           || less<const _CharT*>()(_M_data() + __size, __s))
         return _M_replace_safe(_M_ibegin() + __pos, _M_ibegin() + __pos,
                                __s, __s + __n);
       else
         {
           // Work in-place. If _M_mutate reallocates the string, __s
           // does not point anymore to valid data, therefore we save its
           // offset, then we restore it.
           const size_type __off = __s - _M_data();
           _M_mutate(__pos, 0, __n);
           __s = _M_data() + __off;
           _CharT* __p = _M_data() + __pos;
           if (__s  + __n <= __p)
             traits_type::copy(__p, __s, __n);
           else if (__s >= __p)
             traits_type::copy(__p, __s + __n, __n);
           else
             {
               traits_type::copy(__p, __s, __p - __s);
               traits_type::copy(__p + (__p-__s), __p + __n, __n - (__p-__s));
             }
           return *this;
         }
     }
 
   template<typename _CharT, typename _Traits, typename _Alloc>
     basic_string<_CharT, _Traits, _Alloc>&
     basic_string<_CharT, _Traits, _Alloc>::
     replace(size_type __pos, size_type __n1, const _CharT* __s,
	     size_type __n2)
     {
       const size_type __size = this->size();
       if (__pos > __size)
         __throw_out_of_range("basic_string::replace");
       const bool __testn1 = __n1 < __size - __pos;
       const size_type __foldn1 = __testn1 ? __n1 : __size - __pos;
       if (__size - __foldn1 > this->max_size() - __n2)
         __throw_length_error("basic_string::replace");
       if (_M_rep()->_M_is_shared() || less<const _CharT*>()(__s, _M_data())
           || less<const _CharT*>()(_M_data() + __size, __s))
         return _M_replace_safe(_M_ibegin() + __pos,
				_M_ibegin() + __pos + __foldn1, __s, __s + __n2);
       // Todo: optimized in-place replace.
       else
	 return _M_replace(_M_ibegin() + __pos, _M_ibegin() + __pos + __foldn1,
			   __s, __s + __n2,
			   typename iterator_traits<const _CharT*>::iterator_category());
     }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    basic_string<_CharT, _Traits, _Alloc>::_Rep::
    _M_destroy(const _Alloc& __a) throw ()
    {
      size_type __size = sizeof(_Rep) + (_M_capacity + 1) * sizeof(_CharT);
      _Raw_bytes_alloc(__a).deallocate(reinterpret_cast<char*>(this), __size);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    basic_string<_CharT, _Traits, _Alloc>::_M_leak_hard()
    {
      if (_M_rep()->_M_is_shared()) 
	_M_mutate(0, 0, 0);
      _M_rep()->_M_set_leaked();
    }

  // _M_mutate and, below, _M_clone, include, in the same form, an exponential
  // growth policy, necessary to meet amortized linear time requirements of
  // the library: see http://gcc.gnu.org/ml/libstdc++/2001-07/msg00085.html.
  // The policy is active for allocations requiring an amount of memory above
  // system pagesize. This is consistent with the requirements of the standard:
  // see, f.i., http://gcc.gnu.org/ml/libstdc++/2001-07/msg00130.html
  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    basic_string<_CharT, _Traits, _Alloc>::
    _M_mutate(size_type __pos, size_type __len1, size_type __len2)
    {
      size_type       __old_size = this->size();
      const size_type __new_size = __old_size + __len2 - __len1;
      const _CharT*        __src = _M_data()  + __pos + __len1;
      const size_type __how_much = __old_size - __pos - __len1;
      
      if (_M_rep()->_M_is_shared() || __new_size > capacity())
	{
	  // Must reallocate.
	  allocator_type __a = get_allocator();
	  // See below (_S_create) for the meaning and value of these
	  // constants.
	  const size_type __pagesize = 4096;
	  const size_type __malloc_header_size = 4 * sizeof (void*);
	  // The biggest string which fits in a memory page
	  const size_type __page_capacity = (__pagesize - __malloc_header_size
					     - sizeof(_Rep) - sizeof(_CharT)) 
	    				     / sizeof(_CharT);
	  _Rep* __r;
	  if (__new_size > capacity() && __new_size > __page_capacity)
	    // Growing exponentially.
	    __r = _Rep::_S_create(__new_size > 2*capacity() ?
				  __new_size : 2*capacity(), __a);
	  else
	    __r = _Rep::_S_create(__new_size, __a);
	  try 
	    {
	      if (__pos)
		traits_type::copy(__r->_M_refdata(), _M_data(), __pos);
	      if (__how_much)
		traits_type::copy(__r->_M_refdata() + __pos + __len2, 
				  __src, __how_much);
	    }
	  catch(...) 
	    { 
	      __r->_M_dispose(get_allocator()); 
	      __throw_exception_again;
	    }
	  _M_rep()->_M_dispose(__a);
	  _M_data(__r->_M_refdata());
      }
      else if (__how_much && __len1 != __len2)
	{
	  // Work in-place
	  traits_type::move(_M_data() + __pos + __len2, __src, __how_much);
	}
      _M_rep()->_M_set_sharable();
      _M_rep()->_M_length = __new_size;
      _M_data()[__new_size] = _Rep::_S_terminal; // grrr. (per 21.3.4)
    // You cannot leave those LWG people alone for a second.
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    basic_string<_CharT, _Traits, _Alloc>::reserve(size_type __res)
    {
      if (__res > this->capacity() || _M_rep()->_M_is_shared())
        {
	  if (__res > this->max_size())
	    __throw_length_error("basic_string::reserve");
	  // Make sure we don't shrink below the current size
	  if (__res < this->size())
	    __res = this->size();
	  allocator_type __a = get_allocator();
	  _CharT* __tmp = _M_rep()->_M_clone(__a, __res - this->size());
	  _M_rep()->_M_dispose(__a);
	  _M_data(__tmp);
        }
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    void basic_string<_CharT, _Traits, _Alloc>::swap(basic_string& __s)
    {
      if (_M_rep()->_M_is_leaked()) 
	_M_rep()->_M_set_sharable();
      if (__s._M_rep()->_M_is_leaked()) 
	__s._M_rep()->_M_set_sharable();
      if (this->get_allocator() == __s.get_allocator())
	{
	  _CharT* __tmp = _M_data();
	  _M_data(__s._M_data());
	  __s._M_data(__tmp);
	}
      // The code below can usually be optimized away.
      else 
	{
	  basic_string __tmp1(_M_ibegin(), _M_iend(), __s.get_allocator());
	  basic_string __tmp2(__s._M_ibegin(), __s._M_iend(), 
			      this->get_allocator());
	  *this = __tmp2;
	  __s = __tmp1;
	}
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::_Rep*
    basic_string<_CharT, _Traits, _Alloc>::_Rep::
    _S_create(size_t __capacity, const _Alloc& __alloc)
    {
      typedef basic_string<_CharT, _Traits, _Alloc> __string_type;
#ifdef _GLIBCPP_RESOLVE_LIB_DEFECTS
      // 83.  String::npos vs. string::max_size()
      if (__capacity > _S_max_size)
#else
      if (__capacity == npos)
#endif
	__throw_length_error("basic_string::_S_create");

      // NB: Need an array of char_type[__capacity], plus a
      // terminating null char_type() element, plus enough for the
      // _Rep data structure. Whew. Seemingly so needy, yet so elemental.
      size_t __size = (__capacity + 1) * sizeof(_CharT) + sizeof(_Rep);

      // The standard places no restriction on allocating more memory
      // than is strictly needed within this layer at the moment or as
      // requested by an explicit application call to reserve().  Many
      // malloc implementations perform quite poorly when an
      // application attempts to allocate memory in a stepwise fashion
      // growing each allocation size by only 1 char.  Additionally,
      // it makes little sense to allocate less linear memory than the
      // natural blocking size of the malloc implementation.
      // Unfortunately, we would need a somewhat low-level calculation
      // with tuned parameters to get this perfect for any particular
      // malloc implementation.  Fortunately, generalizations about
      // common features seen among implementations seems to suffice.

      // __pagesize need not match the actual VM page size for good
      // results in practice, thus we pick a common value on the low
      // side.  __malloc_header_size is an estimate of the amount of
      // overhead per memory allocation (in practice seen N * sizeof
      // (void*) where N is 0, 2 or 4).  According to folklore,
      // picking this value on the high side is better than
      // low-balling it (especially when this algorithm is used with
      // malloc implementations that allocate memory blocks rounded up
      // to a size which is a power of 2).
      const size_t __pagesize = 4096; // must be 2^i * __subpagesize
      const size_t __subpagesize = 128; // should be >> __malloc_header_size
      const size_t __malloc_header_size = 4 * sizeof (void*);
      if ((__size + __malloc_header_size) > __pagesize)
	{
	  size_t __extra =
	    (__pagesize - ((__size + __malloc_header_size) % __pagesize))
	    % __pagesize;
	  __capacity += __extra / sizeof(_CharT);
	  __size = (__capacity + 1) * sizeof(_CharT) + sizeof(_Rep);
	}
      else if (__size > __subpagesize)
	{
	  size_t __extra =
	    (__subpagesize - ((__size + __malloc_header_size) % __subpagesize))
	    % __subpagesize;
	  __capacity += __extra / sizeof(_CharT);
	  __size = (__capacity + 1) * sizeof(_CharT) + sizeof(_Rep);
	}

      // NB: Might throw, but no worries about a leak, mate: _Rep()
      // does not throw.
      void* __place = _Raw_bytes_alloc(__alloc).allocate(__size);
      _Rep *__p = new (__place) _Rep;
      __p->_M_capacity = __capacity;
      __p->_M_set_sharable();  // One reference.
      __p->_M_length = 0;
      return __p;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    _CharT*
    basic_string<_CharT, _Traits, _Alloc>::_Rep::
    _M_clone(const _Alloc& __alloc, size_type __res)
    {
      // Requested capacity of the clone.
      const size_type __requested_cap = _M_length + __res;
      // See above (_S_create) for the meaning and value of these constants.
      const size_type __pagesize = 4096;
      const size_type __malloc_header_size = 4 * sizeof (void*);
      // The biggest string which fits in a memory page.
      const size_type __page_capacity =
        (__pagesize - __malloc_header_size - sizeof(_Rep) - sizeof(_CharT))
        / sizeof(_CharT);
      _Rep* __r;
      if (__requested_cap > _M_capacity && __requested_cap > __page_capacity)
        // Growing exponentially.
        __r = _Rep::_S_create(__requested_cap > 2*_M_capacity ?
                              __requested_cap : 2*_M_capacity, __alloc);
      else
        __r = _Rep::_S_create(__requested_cap, __alloc);
      
      if (_M_length)
	{
	  try 
	    { traits_type::copy(__r->_M_refdata(), _M_refdata(), _M_length); }
	  catch(...)  
	    { 
	      __r->_M_destroy(__alloc); 
	      __throw_exception_again;
	    }
	}
      __r->_M_length = _M_length;
      __r->_M_refdata()[_M_length] = _Rep::_S_terminal;
      return __r->_M_refdata();
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    void
    basic_string<_CharT, _Traits, _Alloc>::resize(size_type __n, _CharT __c)
    {
      if (__n > max_size())
	__throw_length_error("basic_string::resize");
      size_type __size = this->size();
      if (__size < __n)
	this->append(__n - __size, __c);
      else if (__n < __size)
	this->erase(__n);
      // else nothing (in particular, avoid calling _M_mutate() unnecessarily.)
    }

  // This is the general replace helper, which currently gets instantiated both
  // for input iterators and reverse iterators. It buffers internally and then
  // calls _M_replace_safe.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _InputIter>
      basic_string<_CharT, _Traits, _Alloc>&
      basic_string<_CharT, _Traits, _Alloc>::
      _M_replace(iterator __i1, iterator __i2, _InputIter __k1, 
		 _InputIter __k2, input_iterator_tag)
      {
	// Save concerned source string data in a temporary.
	basic_string __s(__k1, __k2);
	return _M_replace_safe(__i1, __i2, __s._M_ibegin(), __s._M_iend());
      }

  // This is a special replace helper, which does not buffer internally
  // and can be used in "safe" situations involving forward iterators,
  // i.e., when source and destination ranges are known to not overlap.
  template<typename _CharT, typename _Traits, typename _Alloc>
    template<typename _ForwardIter>
      basic_string<_CharT, _Traits, _Alloc>&
      basic_string<_CharT, _Traits, _Alloc>::
      _M_replace_safe(iterator __i1, iterator __i2, _ForwardIter __k1, 
		      _ForwardIter __k2)
      {
	size_type __dnew = static_cast<size_type>(std::distance(__k1, __k2));
	size_type __dold = __i2 - __i1;
	size_type __dmax = this->max_size();

	if (__dmax <= __dnew)
	  __throw_length_error("basic_string::_M_replace");
	size_type __off = __i1 - _M_ibegin();
	_M_mutate(__off, __dold, __dnew);

	// Invalidated __i1, __i2
        if (__dnew)
	  _S_copy_chars(_M_data() + __off, __k1, __k2);

	return *this;
      }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    replace(size_type __pos1, size_type __n1, const basic_string& __str,
	    size_type __pos2, size_type __n2)
    {
      const size_type __strsize = __str.size();
      if (__pos2 > __strsize)
	__throw_out_of_range("basic_string::replace");
      const bool __testn2 = __n2 < __strsize - __pos2;
      const size_type __foldn2 = __testn2 ? __n2 : __strsize - __pos2;
      return this->replace(__pos1, __n1,
			   __str._M_data() + __pos2, __foldn2);      
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    append(const basic_string& __str)
    {
      // Iff appending itself, string needs to pre-reserve the
      // correct size so that _M_mutate does not clobber the
      // iterators formed here.
      size_type __size = __str.size();
      size_type __len = __size + this->size();
      if (__len > this->capacity())
	this->reserve(__len);
      return _M_replace_safe(_M_iend(), _M_iend(), __str._M_ibegin(),
			     __str._M_iend());
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    append(const basic_string& __str, size_type __pos, size_type __n)
    {
      // Iff appending itself, string needs to pre-reserve the
      // correct size so that _M_mutate does not clobber the
      // iterators formed here.
      size_type __len = std::min(size_type(__str.size() - __pos),
				 __n) + this->size();
      if (__len > this->capacity())
	this->reserve(__len);
      return _M_replace_safe(_M_iend(), _M_iend(), __str._M_check(__pos),
			     __str._M_fold(__pos, __n));
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    append(const _CharT* __s, size_type __n)
    {
      size_type __len = __n + this->size();
      if (__len > this->capacity())
	this->reserve(__len);
      return _M_replace_safe(_M_iend(), _M_iend(), __s, __s + __n);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    append(size_type __n, _CharT __c)
    {
      size_type __len = __n + this->size();
      if (__len > this->capacity())
	this->reserve(__len);
       return this->replace(_M_iend(), _M_iend(), __n, __c);
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>
    operator+(const _CharT* __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    {
      typedef basic_string<_CharT, _Traits, _Alloc> __string_type;
      typedef typename __string_type::size_type	  __size_type;
      __size_type __len = _Traits::length(__lhs);
      __string_type __str;
      __str.reserve(__len + __rhs.size());
      __str.append(__lhs, __lhs + __len);
      __str.append(__rhs);
      return __str;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>
    operator+(_CharT __lhs, const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    {
      typedef basic_string<_CharT, _Traits, _Alloc> __string_type;
      typedef typename __string_type::size_type	  __size_type;
      __string_type __str;
      __size_type __len = __rhs.size();
      __str.reserve(__len + 1);
      __str.append(__size_type(1), __lhs);
      __str.append(__rhs);
      return __str;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>&
    basic_string<_CharT, _Traits, _Alloc>::
    replace(iterator __i1, iterator __i2, size_type __n2, _CharT __c)
    {
      size_type __n1 = __i2 - __i1;
      size_type __off1 = __i1 - _M_ibegin();
      if (max_size() - (this->size() - __n1) <= __n2)
	__throw_length_error("basic_string::replace");
      _M_mutate (__off1, __n1, __n2);
      // Invalidated __i1, __i2
      if (__n2)
	traits_type::assign(_M_data() + __off1, __n2, __c);
      return *this;
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    copy(_CharT* __s, size_type __n, size_type __pos) const
    {
      if (__pos > this->size())
	__throw_out_of_range("basic_string::copy");
      
      if (__n > this->size() - __pos)
	__n = this->size() - __pos;
      
      traits_type::copy(__s, _M_data() + __pos, __n);
      // 21.3.5.7 par 3: do not append null.  (good.)
      return __n;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find(const _CharT* __s, size_type __pos, size_type __n) const
    {
      size_type __size = this->size();
      size_t __xpos = __pos;
      const _CharT* __data = _M_data();
      for (; __xpos + __n <= __size; ++__xpos)
	if (traits_type::compare(__data + __xpos, __s, __n) == 0)
	  return __xpos;
      return npos;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find(_CharT __c, size_type __pos) const
    {
      size_type __size = this->size();
      size_type __ret = npos;
      if (__pos < __size)
	{
	  const _CharT* __data = _M_data();
	  size_type __n = __size - __pos;
	  const _CharT* __p = traits_type::find(__data + __pos, __n, __c);
	  if (__p)
	    __ret = __p - __data;
	}
      return __ret;
    }


  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    rfind(const _CharT* __s, size_type __pos, size_type __n) const
    {
      size_type __size = this->size();
      if (__n <= __size)
	{
	  __pos = std::min(size_type(__size - __n), __pos);
	  const _CharT* __data = _M_data();
	  do 
	    {
	      if (traits_type::compare(__data + __pos, __s, __n) == 0)
		return __pos;
	    } 
	  while (__pos-- > 0);
	}
      return npos;
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    rfind(_CharT __c, size_type __pos) const
    {
      size_type __size = this->size();
      if (__size)
	{
	  size_t __xpos = __size - 1;
	  if (__xpos > __pos)
	    __xpos = __pos;
      
	  for (++__xpos; __xpos-- > 0; )
	    if (traits_type::eq(_M_data()[__xpos], __c))
	      return __xpos;
	}
      return npos;
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_first_of(const _CharT* __s, size_type __pos, size_type __n) const
    {
      for (; __n && __pos < this->size(); ++__pos)
	{
	  const _CharT* __p = traits_type::find(__s, __n, _M_data()[__pos]);
	  if (__p)
	    return __pos;
	}
      return npos;
    }
 
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_last_of(const _CharT* __s, size_type __pos, size_type __n) const
    {
      size_type __size = this->size();
      if (__size && __n)
	{ 
	  if (--__size > __pos) 
	    __size = __pos;
	  do
	    {
	      if (traits_type::find(__s, __n, _M_data()[__size]))
		return __size;
	    } 
	  while (__size-- != 0);
	}
      return npos;
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_first_not_of(const _CharT* __s, size_type __pos, size_type __n) const
    {
      size_t __xpos = __pos;
      for (; __xpos < this->size(); ++__xpos)
	if (!traits_type::find(__s, __n, _M_data()[__xpos]))
	  return __xpos;
      return npos;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_first_not_of(_CharT __c, size_type __pos) const
    {
      size_t __xpos = __pos;
      for (; __xpos < this->size(); ++__xpos)
	if (!traits_type::eq(_M_data()[__xpos], __c))
	  return __xpos;
      return npos;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_last_not_of(const _CharT* __s, size_type __pos, size_type __n) const
    {
      size_type __size = this->size();
      if (__size)
	{ 
	  if (--__size > __pos) 
	    __size = __pos;
	  do
	    {
	      if (!traits_type::find(__s, __n, _M_data()[__size]))
		return __size;
	    } 
	  while (__size--);
	}
      return npos;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    typename basic_string<_CharT, _Traits, _Alloc>::size_type
    basic_string<_CharT, _Traits, _Alloc>::
    find_last_not_of(_CharT __c, size_type __pos) const
    {
      size_type __size = this->size();
      if (__size)
	{ 
	  if (--__size > __pos) 
	    __size = __pos;
	  do
	    {
	      if (!traits_type::eq(_M_data()[__size], __c))
		return __size;
	    } 
	  while (__size--);
	}
      return npos;
    }
  
  template<typename _CharT, typename _Traits, typename _Alloc>
    int
    basic_string<_CharT, _Traits, _Alloc>::
    compare(size_type __pos, size_type __n, const basic_string& __str) const
    {
      size_type __size = this->size();
      size_type __osize = __str.size();
      if (__pos > __size)
	__throw_out_of_range("basic_string::compare");
      
      size_type __rsize= std::min(size_type(__size - __pos), __n);
      size_type __len = std::min(__rsize, __osize);
      int __r = traits_type::compare(_M_data() + __pos, __str.data(), __len);
      if (!__r)
	__r = __rsize - __osize;
      return __r;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    int
    basic_string<_CharT, _Traits, _Alloc>::
    compare(size_type __pos1, size_type __n1, const basic_string& __str,
	    size_type __pos2, size_type __n2) const
    {
      size_type __size = this->size();
      size_type __osize = __str.size();
      if (__pos1 > __size || __pos2 > __osize)
	__throw_out_of_range("basic_string::compare");
      
      size_type __rsize = std::min(size_type(__size - __pos1), __n1);
      size_type __rosize = std::min(size_type(__osize - __pos2), __n2);
      size_type __len = std::min(__rsize, __rosize);
      int __r = traits_type::compare(_M_data() + __pos1, 
				     __str.data() + __pos2, __len);
      if (!__r)
	__r = __rsize - __rosize;
      return __r;
    }


  template<typename _CharT, typename _Traits, typename _Alloc>
    int
    basic_string<_CharT, _Traits, _Alloc>::
    compare(const _CharT* __s) const
    {
      size_type __size = this->size();
      size_type __osize = traits_type::length(__s);
      size_type __len = std::min(__size, __osize);
      int __r = traits_type::compare(_M_data(), __s, __len);
      if (!__r)
	__r = __size - __osize;
      return __r;
    }


  template<typename _CharT, typename _Traits, typename _Alloc>
    int
    basic_string <_CharT, _Traits, _Alloc>::
    compare(size_type __pos, size_type __n1, const _CharT* __s) const
    {
      size_type __size = this->size();
      if (__pos > __size)
	__throw_out_of_range("basic_string::compare");
      
      size_type __osize = traits_type::length(__s);
      size_type __rsize = std::min(size_type(__size - __pos), __n1);
      size_type __len = std::min(__rsize, __osize);
      int __r = traits_type::compare(_M_data() + __pos, __s, __len);
      if (!__r)
	__r = __rsize - __osize;
      return __r;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    int
    basic_string <_CharT, _Traits, _Alloc>::
    compare(size_type __pos, size_type __n1, const _CharT* __s, 
	    size_type __n2) const
    {
      size_type __size = this->size();
      if (__pos > __size)
	__throw_out_of_range("basic_string::compare");
      
      size_type __rsize = std::min(size_type(__size - __pos), __n1);
      size_type __len = std::min(__rsize, __n2);
      int __r = traits_type::compare(_M_data() + __pos, __s, __len);
      if (!__r)
	__r = __rsize - __n2;
      return __r;
    }

  template <class _CharT, class _Traits, class _Alloc>
    void
    _S_string_copy(const basic_string<_CharT, _Traits, _Alloc>& __str,
		   _CharT* __buf, typename _Alloc::size_type __bufsiz)
    {
      typedef typename _Alloc::size_type size_type;
      size_type __strsize = __str.size();
      size_type __bytes = std::min(__strsize, __bufsiz - 1);
      _Traits::copy(__buf, __str.data(), __bytes);
      __buf[__bytes] = _CharT();
    }

  // Inhibit implicit instantiations for required instantiations,
  // which are defined via explicit instantiations elsewhere.  
  // NB: This syntax is a GNU extension.
#if defined(_GLIBCPP_EXTERN_TEMPLATE)
  extern template class basic_string<char>;
  extern template 
    basic_istream<char>& 
    operator>>(basic_istream<char>&, string&);
  extern template 
    basic_ostream<char>& 
    operator<<(basic_ostream<char>&, const string&);
  extern template 
    basic_istream<char>& 
    getline(basic_istream<char>&, string&, char);
  extern template 
    basic_istream<char>& 
    getline(basic_istream<char>&, string&);

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  extern template class basic_string<wchar_t>;
  extern template 
    basic_istream<wchar_t>& 
    operator>>(basic_istream<wchar_t>&, wstring&);
  extern template 
    basic_ostream<wchar_t>& 
    operator<<(basic_ostream<wchar_t>&, const wstring&);
  extern template 
    basic_istream<wchar_t>& 
    getline(basic_istream<wchar_t>&, wstring&, wchar_t);
  extern template 
    basic_istream<wchar_t>& 
    getline(basic_istream<wchar_t>&, wstring&);
#endif
#endif
} // namespace std

#endif
