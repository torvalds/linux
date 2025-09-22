// Components for manipulating sequences of characters -*- C++ -*-

// Copyright (C) 1997, 1998, 1999, 2000, 2001, 2002
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
// ISO C++ 14882: 21 Strings library
//

/** @file basic_string.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_STRING_H
#define _CPP_BITS_STRING_H        1

#pragma GCC system_header

#include <bits/atomicity.h>

namespace std
{
  /**
   *  @class basic_string basic_string.h <string>
   *  @brief  Managing sequences of characters and character-like objects.
   *
   *  @ingroup Containers
   *  @ingroup Sequences
   *
   *  Meets the requirements of a <a href="tables.html#65">container</a>, a
   *  <a href="tables.html#66">reversible container</a>, and a
   *  <a href="tables.html#67">sequence</a>.  Of the
   *  <a href="tables.html#68">optional sequence requirements</a>, only
   *  @c push_back, @c at, and array access are supported.
   *
   *  @doctodo
   *
   *
   *  @if maint
   *  Documentation?  What's that?
   *  Nathan Myers <ncm@cantrip.org>.
   *
   *  A string looks like this:
   *
   *  @code
   *                                        [_Rep]
   *                                        _M_length
   *   [basic_string<char_type>]            _M_capacity
   *   _M_dataplus                          _M_state
   *   _M_p ---------------->               unnamed array of char_type
   *  @endcode
   *
   *  Where the _M_p points to the first character in the string, and
   *  you cast it to a pointer-to-_Rep and subtract 1 to get a
   *  pointer to the header.
   *
   *  This approach has the enormous advantage that a string object
   *  requires only one allocation.  All the ugliness is confined
   *  within a single pair of inline functions, which each compile to
   *  a single "add" instruction: _Rep::_M_data(), and
   *  string::_M_rep(); and the allocation function which gets a
   *  block of raw bytes and with room enough and constructs a _Rep
   *  object at the front.
   *
   *  The reason you want _M_data pointing to the character array and
   *  not the _Rep is so that the debugger can see the string
   *  contents. (Probably we should add a non-inline member to get
   *  the _Rep for the debugger to use, so users can check the actual
   *  string length.)
   *
   *  Note that the _Rep object is a POD so that you can have a
   *  static "empty string" _Rep object already "constructed" before
   *  static constructors have run.  The reference-count encoding is
   *  chosen so that a 0 indicates one reference, so you never try to
   *  destroy the empty-string _Rep object.
   *
   *  All but the last paragraph is considered pretty conventional
   *  for a C++ string implementation.
   *  @endif
  */
  // 21.3  Template class basic_string
  template<typename _CharT, typename _Traits, typename _Alloc>
    class basic_string
    {
      // Types:
    public:
      typedef _Traits 					    traits_type;
      typedef typename _Traits::char_type 		    value_type;
      typedef _Alloc 					    allocator_type;
      typedef typename _Alloc::size_type 		    size_type;
      typedef typename _Alloc::difference_type 		    difference_type;
      typedef typename _Alloc::reference 		    reference;
      typedef typename _Alloc::const_reference 		    const_reference;
      typedef typename _Alloc::pointer 			    pointer;
      typedef typename _Alloc::const_pointer 	   	    const_pointer;
      typedef __gnu_cxx::__normal_iterator<pointer, basic_string>  iterator;
      typedef __gnu_cxx::__normal_iterator<const_pointer, basic_string>
                                                            const_iterator;
      typedef std::reverse_iterator<const_iterator> 	const_reverse_iterator;
      typedef std::reverse_iterator<iterator> 		    reverse_iterator;

    private:
      // _Rep: string representation
      //   Invariants:
      //   1. String really contains _M_length + 1 characters; last is set
      //      to 0 only on call to c_str().  We avoid instantiating
      //      _CharT() where the interface does not require it.
      //   2. _M_capacity >= _M_length
      //      Allocated memory is always _M_capacity + (1 * sizeof(_CharT)).
      //   3. _M_references has three states:
      //      -1: leaked, one reference, no ref-copies allowed, non-const.
      //       0: one reference, non-const.
      //     n>0: n + 1 references, operations require a lock, const.
      //   4. All fields==0 is an empty string, given the extra storage
      //      beyond-the-end for a null terminator; thus, the shared
      //      empty string representation needs no constructor.
      struct _Rep
      {
	// Types:
	typedef typename _Alloc::template rebind<char>::other _Raw_bytes_alloc;

	// (Public) Data members:

	// The maximum number of individual char_type elements of an
	// individual string is determined by _S_max_size. This is the
	// value that will be returned by max_size().  (Whereas npos
	// is the maximum number of bytes the allocator can allocate.)
	// If one was to divvy up the theoretical largest size string,
	// with a terminating character and m _CharT elements, it'd
	// look like this:
	// npos = sizeof(_Rep) + (m * sizeof(_CharT)) + sizeof(_CharT)
	// Solving for m:
	// m = ((npos - sizeof(_Rep))/sizeof(CharT)) - 1
	// In addition, this implementation quarters this ammount.
	static const size_type 	_S_max_size;
	static const _CharT 	_S_terminal;

	size_type 		_M_length;
	size_type 		_M_capacity;
	_Atomic_word		_M_references;

        bool
	_M_is_leaked() const
        { return _M_references < 0; }

        bool
	_M_is_shared() const
        { return _M_references > 0; }

        void
	_M_set_leaked()
        { _M_references = -1; }

        void
	_M_set_sharable()
        { _M_references = 0; }

	_CharT*
	_M_refdata() throw()
	{ return reinterpret_cast<_CharT*>(this + 1); }

	_CharT&
	operator[](size_t __s) throw()
	{ return _M_refdata() [__s]; }

	_CharT*
	_M_grab(const _Alloc& __alloc1, const _Alloc& __alloc2)
	{
	  return (!_M_is_leaked() && __alloc1 == __alloc2)
	          ? _M_refcopy() : _M_clone(__alloc1);
	}

	// Create & Destroy
	static _Rep*
	_S_create(size_t, const _Alloc&);

	void
	_M_dispose(const _Alloc& __a)
	{
	  if (__exchange_and_add(&_M_references, -1) <= 0)
	    _M_destroy(__a);
	}  // XXX MT

	void
	_M_destroy(const _Alloc&) throw();

	_CharT*
	_M_refcopy() throw()
	{
	  __atomic_add(&_M_references, 1);
	  return _M_refdata();
	}  // XXX MT

	_CharT*
	_M_clone(const _Alloc&, size_type __res = 0);
      };

      // Use empty-base optimization: http://www.cantrip.org/emptyopt.html
      struct _Alloc_hider : _Alloc
      {
	_Alloc_hider(_CharT* __dat, const _Alloc& __a)
	: _Alloc(__a), _M_p(__dat) { }

	_CharT* _M_p; // The actual data.
      };

    public:
      // Data Members (public):
      // NB: This is an unsigned type, and thus represents the maximum
      // size that the allocator can hold.
      static const size_type 	npos = static_cast<size_type>(-1);

    private:
      // Data Members (private):
      mutable _Alloc_hider 	_M_dataplus;

      // The following storage is init'd to 0 by the linker, resulting
      // (carefully) in an empty string with one reference.
      static size_type _S_empty_rep_storage[(sizeof(_Rep) + sizeof(_CharT) + sizeof(size_type) - 1)/sizeof(size_type)];

      _CharT*
      _M_data() const
      { return  _M_dataplus._M_p; }

      _CharT*
      _M_data(_CharT* __p)
      { return (_M_dataplus._M_p = __p); }

      _Rep*
      _M_rep() const
      { return &((reinterpret_cast<_Rep*> (_M_data()))[-1]); }

      // For the internal use we have functions similar to `begin'/`end'
      // but they do not call _M_leak.
      iterator
      _M_ibegin() const { return iterator(_M_data()); }

      iterator
      _M_iend() const { return iterator(_M_data() + this->size()); }

      void
      _M_leak()    // for use in begin() & non-const op[]
      {
	if (!_M_rep()->_M_is_leaked())
	  _M_leak_hard();
      }

      iterator
      _M_check(size_type __pos) const
      {
	if (__pos > this->size())
	  __throw_out_of_range("basic_string::_M_check");
	return _M_ibegin() + __pos;
      }

      // NB: _M_fold doesn't check for a bad __pos1 value.
      iterator
      _M_fold(size_type __pos, size_type __off) const
      {
	bool __testoff =  __off < this->size() - __pos;
	size_type __newoff = __testoff ? __off : this->size() - __pos;
	return (_M_ibegin() + __pos + __newoff);
      }

      // _S_copy_chars is a separate template to permit specialization
      // to optimize for the common case of pointers as iterators.
      template<class _Iterator>
        static void
        _S_copy_chars(_CharT* __p, _Iterator __k1, _Iterator __k2)
        {
	  for (; __k1 != __k2; ++__k1, ++__p)
	    traits_type::assign(*__p, *__k1); // These types are off.
	}

      static void
      _S_copy_chars(_CharT* __p, iterator __k1, iterator __k2)
      { _S_copy_chars(__p, __k1.base(), __k2.base()); }

      static void
      _S_copy_chars(_CharT* __p, const_iterator __k1, const_iterator __k2)
      { _S_copy_chars(__p, __k1.base(), __k2.base()); }

      static void
      _S_copy_chars(_CharT* __p, _CharT* __k1, _CharT* __k2)
      { traits_type::copy(__p, __k1, __k2 - __k1); }

      static void
      _S_copy_chars(_CharT* __p, const _CharT* __k1, const _CharT* __k2)
      { traits_type::copy(__p, __k1, __k2 - __k1); }

      void
      _M_mutate(size_type __pos, size_type __len1, size_type __len2);

      void
      _M_leak_hard();

      static _Rep&
      _S_empty_rep()
      { return *reinterpret_cast<_Rep*>(&_S_empty_rep_storage); }

    public:
      // Construct/copy/destroy:
      // NB: We overload ctors in some cases instead of using default
      // arguments, per 17.4.4.4 para. 2 item 2.

      inline
      basic_string();

      explicit
      basic_string(const _Alloc& __a);

      // NB: per LWG issue 42, semantics different from IS:
      basic_string(const basic_string& __str);
      basic_string(const basic_string& __str, size_type __pos,
		   size_type __n = npos);
      basic_string(const basic_string& __str, size_type __pos,
		   size_type __n, const _Alloc& __a);

      basic_string(const _CharT* __s, size_type __n,
		   const _Alloc& __a = _Alloc());
      basic_string(const _CharT* __s, const _Alloc& __a = _Alloc());
      basic_string(size_type __n, _CharT __c, const _Alloc& __a = _Alloc());

      template<class _InputIterator>
        basic_string(_InputIterator __beg, _InputIterator __end,
		     const _Alloc& __a = _Alloc());

      ~basic_string()
      { _M_rep()->_M_dispose(this->get_allocator()); }

      basic_string&
      operator=(const basic_string& __str) { return this->assign(__str); }

      basic_string&
      operator=(const _CharT* __s) { return this->assign(__s); }

      basic_string&
      operator=(_CharT __c) { return this->assign(1, __c); }

      // Iterators:
      iterator
      begin()
      {
	_M_leak();
	return iterator(_M_data());
      }

      const_iterator
      begin() const
      { return const_iterator(_M_data()); }

      iterator
      end()
      {
         _M_leak();
	 return iterator(_M_data() + this->size());
      }

      const_iterator
      end() const
      { return const_iterator(_M_data() + this->size()); }

      reverse_iterator
      rbegin()
      { return reverse_iterator(this->end()); }

      const_reverse_iterator
      rbegin() const
      { return const_reverse_iterator(this->end()); }

      reverse_iterator
      rend()
      { return reverse_iterator(this->begin()); }

      const_reverse_iterator
      rend() const
      { return const_reverse_iterator(this->begin()); }

    public:
      // Capacity:
      size_type
      size() const { return _M_rep()->_M_length; }

      size_type
      length() const { return _M_rep()->_M_length; }

      size_type
      max_size() const { return _Rep::_S_max_size; }

      void
      resize(size_type __n, _CharT __c);

      void
      resize(size_type __n) { this->resize(__n, _CharT()); }

      size_type
      capacity() const { return _M_rep()->_M_capacity; }

      void
      reserve(size_type __res_arg = 0);

      void
      clear() { _M_mutate(0, this->size(), 0); }

      bool
      empty() const { return this->size() == 0; }

      // Element access:
      const_reference
      operator[] (size_type __pos) const
      { return _M_data()[__pos]; }

      reference
      operator[](size_type __pos)
      {
	_M_leak();
	return _M_data()[__pos];
      }

      const_reference
      at(size_type __n) const
      {
	if (__n >= this->size())
	  __throw_out_of_range("basic_string::at");
	return _M_data()[__n];
      }

      reference
      at(size_type __n)
      {
	if (__n >= size())
	  __throw_out_of_range("basic_string::at");
	_M_leak();
	return _M_data()[__n];
      }

      // Modifiers:
      basic_string&
      operator+=(const basic_string& __str) { return this->append(__str); }

      basic_string&
      operator+=(const _CharT* __s) { return this->append(__s); }

      basic_string&
      operator+=(_CharT __c) { return this->append(size_type(1), __c); }

      basic_string&
      append(const basic_string& __str);

      basic_string&
      append(const basic_string& __str, size_type __pos, size_type __n);

      basic_string&
      append(const _CharT* __s, size_type __n);

      basic_string&
      append(const _CharT* __s)
      { return this->append(__s, traits_type::length(__s)); }

      basic_string&
      append(size_type __n, _CharT __c);

      template<class _InputIterator>
        basic_string&
        append(_InputIterator __first, _InputIterator __last)
        { return this->replace(_M_iend(), _M_iend(), __first, __last); }

      void
      push_back(_CharT __c)
      { this->replace(_M_iend(), _M_iend(), 1, __c); }

      basic_string&
      assign(const basic_string& __str);

      basic_string&
      assign(const basic_string& __str, size_type __pos, size_type __n);

      basic_string&
      assign(const _CharT* __s, size_type __n);

      basic_string&
      assign(const _CharT* __s)
      { return this->assign(__s, traits_type::length(__s)); }

      basic_string&
      assign(size_type __n, _CharT __c)
      { return this->replace(_M_ibegin(), _M_iend(), __n, __c); }

      template<class _InputIterator>
        basic_string&
        assign(_InputIterator __first, _InputIterator __last)
        { return this->replace(_M_ibegin(), _M_iend(), __first, __last); }

      void
      insert(iterator __p, size_type __n, _CharT __c)
      {	this->replace(__p, __p, __n, __c);  }

      template<class _InputIterator>
        void insert(iterator __p, _InputIterator __beg, _InputIterator __end)
        { this->replace(__p, __p, __beg, __end); }

      basic_string&
      insert(size_type __pos1, const basic_string& __str)
      { return this->insert(__pos1, __str, 0, __str.size()); }

      basic_string&
      insert(size_type __pos1, const basic_string& __str,
	     size_type __pos2, size_type __n);

      basic_string&
      insert(size_type __pos, const _CharT* __s, size_type __n);

      basic_string&
      insert(size_type __pos, const _CharT* __s)
      { return this->insert(__pos, __s, traits_type::length(__s)); }

      basic_string&
      insert(size_type __pos, size_type __n, _CharT __c)
      {
	this->insert(_M_check(__pos), __n, __c);
	return *this;
      }

      iterator
      insert(iterator __p, _CharT __c = _CharT())
      {
	size_type __pos = __p - _M_ibegin();
	this->insert(_M_check(__pos), size_type(1), __c);
	_M_rep()->_M_set_leaked();
 	return this->_M_ibegin() + __pos;
      }

      basic_string&
      erase(size_type __pos = 0, size_type __n = npos)
      {
	return this->replace(_M_check(__pos), _M_fold(__pos, __n),
			     _M_data(), _M_data());
      }

      iterator
      erase(iterator __position)
      {
	size_type __i = __position - _M_ibegin();
        this->replace(__position, __position + 1, _M_data(), _M_data());
	_M_rep()->_M_set_leaked();
	return _M_ibegin() + __i;
      }

      iterator
      erase(iterator __first, iterator __last)
      {
        size_type __i = __first - _M_ibegin();
	this->replace(__first, __last, _M_data(), _M_data());
	_M_rep()->_M_set_leaked();
       return _M_ibegin() + __i;
      }

      basic_string&
      replace(size_type __pos, size_type __n, const basic_string& __str)
      { return this->replace(__pos, __n, __str._M_data(), __str.size()); }

      basic_string&
      replace(size_type __pos1, size_type __n1, const basic_string& __str,
	      size_type __pos2, size_type __n2);

      basic_string&
      replace(size_type __pos, size_type __n1, const _CharT* __s,
	      size_type __n2);

      basic_string&
      replace(size_type __pos, size_type __n1, const _CharT* __s)
      { return this->replace(__pos, __n1, __s, traits_type::length(__s)); }

      basic_string&
      replace(size_type __pos, size_type __n1, size_type __n2, _CharT __c)
      { return this->replace(_M_check(__pos), _M_fold(__pos, __n1), __n2, __c); }

      basic_string&
      replace(iterator __i1, iterator __i2, const basic_string& __str)
      { return this->replace(__i1, __i2, __str._M_data(), __str.size()); }

      basic_string&
      replace(iterator __i1, iterator __i2,
                           const _CharT* __s, size_type __n)
      { return this->replace(__i1 - _M_ibegin(), __i2 - __i1, __s, __n); }

      basic_string&
      replace(iterator __i1, iterator __i2, const _CharT* __s)
      { return this->replace(__i1, __i2, __s, traits_type::length(__s)); }

      basic_string&
      replace(iterator __i1, iterator __i2, size_type __n, _CharT __c);

      template<class _InputIterator>
        basic_string&
        replace(iterator __i1, iterator __i2,
		_InputIterator __k1, _InputIterator __k2)
        { return _M_replace(__i1, __i2, __k1, __k2,
	     typename iterator_traits<_InputIterator>::iterator_category()); }

      // Specializations for the common case of pointer and iterator:
      // useful to avoid the overhead of temporary buffering in _M_replace.
      basic_string&
      replace(iterator __i1, iterator __i2, _CharT* __k1, _CharT* __k2)
        { return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			       __k1, __k2 - __k1); }

      basic_string&
      replace(iterator __i1, iterator __i2, const _CharT* __k1, const _CharT* __k2)
        { return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			       __k1, __k2 - __k1); }

      basic_string&
      replace(iterator __i1, iterator __i2, iterator __k1, iterator __k2)
        { return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			       __k1.base(), __k2 - __k1);
	}

      basic_string&
      replace(iterator __i1, iterator __i2, const_iterator __k1, const_iterator __k2)
        { return this->replace(__i1 - _M_ibegin(), __i2 - __i1,
			       __k1.base(), __k2 - __k1);
	}

    private:
      template<class _InputIterator>
        basic_string&
        _M_replace(iterator __i1, iterator __i2, _InputIterator __k1,
		   _InputIterator __k2, input_iterator_tag);

      template<class _ForwardIterator>
        basic_string&
        _M_replace_safe(iterator __i1, iterator __i2, _ForwardIterator __k1,
		   _ForwardIterator __k2);

      // _S_construct_aux is used to implement the 21.3.1 para 15 which
      // requires special behaviour if _InIter is an integral type
      template<class _InIter>
        static _CharT*
        _S_construct_aux(_InIter __beg, _InIter __end, const _Alloc& __a,
			 __false_type)
	{
          typedef typename iterator_traits<_InIter>::iterator_category _Tag;
          return _S_construct(__beg, __end, __a, _Tag());
	}

      template<class _InIter>
        static _CharT*
        _S_construct_aux(_InIter __beg, _InIter __end, const _Alloc& __a,
			 __true_type)
	{
	  return _S_construct(static_cast<size_type>(__beg),
			      static_cast<value_type>(__end), __a);
	}

      template<class _InIter>
        static _CharT*
        _S_construct(_InIter __beg, _InIter __end, const _Alloc& __a)
	{
	  typedef typename _Is_integer<_InIter>::_Integral _Integral;
	  return _S_construct_aux(__beg, __end, __a, _Integral());
        }

      // For Input Iterators, used in istreambuf_iterators, etc.
      template<class _InIter>
        static _CharT*
         _S_construct(_InIter __beg, _InIter __end, const _Alloc& __a,
		      input_iterator_tag);

      // For forward_iterators up to random_access_iterators, used for
      // string::iterator, _CharT*, etc.
      template<class _FwdIter>
        static _CharT*
        _S_construct(_FwdIter __beg, _FwdIter __end, const _Alloc& __a,
		     forward_iterator_tag);

      static _CharT*
      _S_construct(size_type __req, _CharT __c, const _Alloc& __a);

    public:

      size_type
      copy(_CharT* __s, size_type __n, size_type __pos = 0) const;

      void
      swap(basic_string<_CharT, _Traits, _Alloc>& __s);

      // String operations:
      const _CharT*
      c_str() const
      {
	// MT: This assumes concurrent writes are OK.
	size_type __n = this->size();
	traits_type::assign(_M_data()[__n], _Rep::_S_terminal);
        return _M_data();
      }

      const _CharT*
      data() const { return _M_data(); }

      allocator_type
      get_allocator() const { return _M_dataplus; }

      size_type
      find(const _CharT* __s, size_type __pos, size_type __n) const;

      size_type
      find(const basic_string& __str, size_type __pos = 0) const
      { return this->find(__str.data(), __pos, __str.size()); }

      size_type
      find(const _CharT* __s, size_type __pos = 0) const
      { return this->find(__s, __pos, traits_type::length(__s)); }

      size_type
      find(_CharT __c, size_type __pos = 0) const;

      size_type
      rfind(const basic_string& __str, size_type __pos = npos) const
      { return this->rfind(__str.data(), __pos, __str.size()); }

      size_type
      rfind(const _CharT* __s, size_type __pos, size_type __n) const;

      size_type
      rfind(const _CharT* __s, size_type __pos = npos) const
      { return this->rfind(__s, __pos, traits_type::length(__s)); }

      size_type
      rfind(_CharT __c, size_type __pos = npos) const;

      size_type
      find_first_of(const basic_string& __str, size_type __pos = 0) const
      { return this->find_first_of(__str.data(), __pos, __str.size()); }

      size_type
      find_first_of(const _CharT* __s, size_type __pos, size_type __n) const;

      size_type
      find_first_of(const _CharT* __s, size_type __pos = 0) const
      { return this->find_first_of(__s, __pos, traits_type::length(__s)); }

      size_type
      find_first_of(_CharT __c, size_type __pos = 0) const
      { return this->find(__c, __pos); }

      size_type
      find_last_of(const basic_string& __str, size_type __pos = npos) const
      { return this->find_last_of(__str.data(), __pos, __str.size()); }

      size_type
      find_last_of(const _CharT* __s, size_type __pos, size_type __n) const;

      size_type
      find_last_of(const _CharT* __s, size_type __pos = npos) const
      { return this->find_last_of(__s, __pos, traits_type::length(__s)); }

      size_type
      find_last_of(_CharT __c, size_type __pos = npos) const
      { return this->rfind(__c, __pos); }

      size_type
      find_first_not_of(const basic_string& __str, size_type __pos = 0) const
      { return this->find_first_not_of(__str.data(), __pos, __str.size()); }

      size_type
      find_first_not_of(const _CharT* __s, size_type __pos,
			size_type __n) const;

      size_type
      find_first_not_of(const _CharT* __s, size_type __pos = 0) const
      { return this->find_first_not_of(__s, __pos, traits_type::length(__s)); }

      size_type
      find_first_not_of(_CharT __c, size_type __pos = 0) const;

      size_type
      find_last_not_of(const basic_string& __str, size_type __pos = npos) const
      { return this->find_last_not_of(__str.data(), __pos, __str.size()); }

      size_type
      find_last_not_of(const _CharT* __s, size_type __pos,
		       size_type __n) const;
      size_type
      find_last_not_of(const _CharT* __s, size_type __pos = npos) const
      { return this->find_last_not_of(__s, __pos, traits_type::length(__s)); }

      size_type
      find_last_not_of(_CharT __c, size_type __pos = npos) const;

      basic_string
      substr(size_type __pos = 0, size_type __n = npos) const
      {
	if (__pos > this->size())
	  __throw_out_of_range("basic_string::substr");
	return basic_string(*this, __pos, __n);
      }

      int
      compare(const basic_string& __str) const
      {
	size_type __size = this->size();
	size_type __osize = __str.size();
	size_type __len = std::min(__size, __osize);

	int __r = traits_type::compare(_M_data(), __str.data(), __len);
	if (!__r)
	  __r =  __size - __osize;
	return __r;
      }

      int
      compare(size_type __pos, size_type __n, const basic_string& __str) const;

      int
      compare(size_type __pos1, size_type __n1, const basic_string& __str,
	      size_type __pos2, size_type __n2) const;

      int
      compare(const _CharT* __s) const;

      // _GLIBCPP_RESOLVE_LIB_DEFECTS
      // 5 String::compare specification questionable
      int
      compare(size_type __pos, size_type __n1, const _CharT* __s) const;

      int
      compare(size_type __pos, size_type __n1, const _CharT* __s,
	      size_type __n2) const;
  };


  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>::
    basic_string()
    : _M_dataplus(_S_empty_rep()._M_refcopy(), _Alloc()) { }

  // operator+
  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    {
      basic_string<_CharT, _Traits, _Alloc> __str(__lhs);
      __str.append(__rhs);
      return __str;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT,_Traits,_Alloc>
    operator+(const _CharT* __lhs,
	      const basic_string<_CharT,_Traits,_Alloc>& __rhs);

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_string<_CharT,_Traits,_Alloc>
    operator+(_CharT __lhs, const basic_string<_CharT,_Traits,_Alloc>& __rhs);

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	     const _CharT* __rhs)
    {
      basic_string<_CharT, _Traits, _Alloc> __str(__lhs);
      __str.append(__rhs);
      return __str;
    }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_string<_CharT, _Traits, _Alloc>
    operator+(const basic_string<_CharT, _Traits, _Alloc>& __lhs, _CharT __rhs)
    {
      typedef basic_string<_CharT, _Traits, _Alloc> 	__string_type;
      typedef typename __string_type::size_type		__size_type;
      __string_type __str(__lhs);
      __str.append(__size_type(1), __rhs);
      return __str;
    }

  // operator ==
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) == 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) == 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator==(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) == 0; }

  // operator !=
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) != 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) != 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator!=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) != 0; }

  // operator <
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) < 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const _CharT* __rhs)
    { return __lhs.compare(__rhs) < 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<(const _CharT* __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) > 0; }

  // operator >
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) > 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	      const _CharT* __rhs)
    { return __lhs.compare(__rhs) > 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>(const _CharT* __lhs,
	      const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) < 0; }

  // operator <=
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) <= 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) <= 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator<=(const _CharT* __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
  { return __rhs.compare(__lhs) >= 0; }

  // operator >=
  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __lhs.compare(__rhs) >= 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const basic_string<_CharT, _Traits, _Alloc>& __lhs,
	       const _CharT* __rhs)
    { return __lhs.compare(__rhs) >= 0; }

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline bool
    operator>=(const _CharT* __lhs,
	     const basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { return __rhs.compare(__lhs) <= 0; }


  template<typename _CharT, typename _Traits, typename _Alloc>
    inline void
    swap(basic_string<_CharT, _Traits, _Alloc>& __lhs,
	 basic_string<_CharT, _Traits, _Alloc>& __rhs)
    { __lhs.swap(__rhs); }

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_istream<_CharT, _Traits>&
    operator>>(basic_istream<_CharT, _Traits>& __is,
	       basic_string<_CharT, _Traits, _Alloc>& __str);

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_ostream<_CharT, _Traits>&
    operator<<(basic_ostream<_CharT, _Traits>& __os,
	       const basic_string<_CharT, _Traits, _Alloc>& __str);

  template<typename _CharT, typename _Traits, typename _Alloc>
    basic_istream<_CharT,_Traits>&
    getline(basic_istream<_CharT, _Traits>& __is,
	    basic_string<_CharT, _Traits, _Alloc>& __str, _CharT __delim);

  template<typename _CharT, typename _Traits, typename _Alloc>
    inline basic_istream<_CharT,_Traits>&
    getline(basic_istream<_CharT, _Traits>& __is,
	    basic_string<_CharT, _Traits, _Alloc>& __str);
} // namespace std

#endif /* _CPP_BITS_STRING_H */
