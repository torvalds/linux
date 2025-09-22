// Locale support (codecvt) -*- C++ -*-

// Copyright (C) 2000, 2001, 2002, 2003 Free Software Foundation, Inc.
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
// ISO C++ 14882: 22.2.1.5 Template class codecvt
//

// Written by Benjamin Kosnik <bkoz@cygnus.com>

/** @file codecvt.h
 *  This is an internal header file, included by other library headers.
 *  You should not attempt to use it directly.
 */

#ifndef _CPP_BITS_CODECVT_H
#define _CPP_BITS_CODECVT_H	1

#pragma GCC system_header

  //  22.2.1.5  Template class codecvt
  class codecvt_base
  {
  public:
    enum result
    {
      ok,
      partial,
      error,
      noconv
    };
  };

  // Template class __codecvt_abstract_base
  // NB: An abstract base class that fills in the public inlines, so
  // that the specializations don't have to re-copy the public
  // interface.
  template<typename _InternT, typename _ExternT, typename _StateT>
    class __codecvt_abstract_base 
    : public locale::facet, public codecvt_base
    {
    public:
      // Types:
      typedef codecvt_base::result	result;
      typedef _InternT 			intern_type;
      typedef _ExternT 			extern_type;
      typedef _StateT  			state_type;
      
      // 22.2.1.5.1 codecvt members
      result
      out(state_type& __state, const intern_type* __from, 
	  const intern_type* __from_end, const intern_type*& __from_next,
	  extern_type* __to, extern_type* __to_end, 
	  extern_type*& __to_next) const
      { 
	return this->do_out(__state, __from, __from_end, __from_next, 
			    __to, __to_end, __to_next); 
      }

      result
      unshift(state_type& __state, extern_type* __to, extern_type* __to_end,
	      extern_type*& __to_next) const
      { return this->do_unshift(__state, __to,__to_end,__to_next); }

      result
      in(state_type& __state, const extern_type* __from, 
	 const extern_type* __from_end, const extern_type*& __from_next,
	 intern_type* __to, intern_type* __to_end, 
	 intern_type*& __to_next) const
      { 
	return this->do_in(__state, __from, __from_end, __from_next,
			   __to, __to_end, __to_next); 
      }

      int 
      encoding() const throw()
      { return this->do_encoding(); }

      bool 
      always_noconv() const throw()
      { return this->do_always_noconv(); }

      int
      length(const state_type& __state, const extern_type* __from,
	     const extern_type* __end, size_t __max) const
      { return this->do_length(__state, __from, __end, __max); }

      int 
      max_length() const throw()
      { return this->do_max_length(); }

    protected:
      explicit 
      __codecvt_abstract_base(size_t __refs = 0) : locale::facet(__refs) { }

      virtual 
      ~__codecvt_abstract_base() { }

      virtual result
      do_out(state_type& __state, const intern_type* __from, 
	     const intern_type* __from_end, const intern_type*& __from_next,
	     extern_type* __to, extern_type* __to_end,
	     extern_type*& __to_next) const = 0;

      virtual result
      do_unshift(state_type& __state, extern_type* __to, 
		 extern_type* __to_end, extern_type*& __to_next) const = 0;
      
      virtual result
      do_in(state_type& __state, const extern_type* __from, 
	    const extern_type* __from_end, const extern_type*& __from_next, 
	    intern_type* __to, intern_type* __to_end, 
	    intern_type*& __to_next) const = 0;
      
      virtual int 
      do_encoding() const throw() = 0;

      virtual bool 
      do_always_noconv() const throw() = 0;

      virtual int 
      do_length(const state_type&, const extern_type* __from, 
		const extern_type* __end, size_t __max) const = 0;

      virtual int 
      do_max_length() const throw() = 0;
    };

  // 22.2.1.5 Template class codecvt
  // NB: Generic, mostly useless implementation.
  template<typename _InternT, typename _ExternT, typename _StateT>
    class codecvt 
    : public __codecvt_abstract_base<_InternT, _ExternT, _StateT>
    {
    public:      
      // Types:
      typedef codecvt_base::result	result;
      typedef _InternT 			intern_type;
      typedef _ExternT 			extern_type;
      typedef _StateT  			state_type;

    public:
      static locale::id 		id;

      explicit 
      codecvt(size_t __refs = 0) 
      : __codecvt_abstract_base<_InternT, _ExternT, _StateT> (__refs) { }

    protected:
      virtual 
      ~codecvt() { }

      virtual result
      do_out(state_type& __state, const intern_type* __from, 
	     const intern_type* __from_end, const intern_type*& __from_next,
	     extern_type* __to, extern_type* __to_end,
	     extern_type*& __to_next) const;

      virtual result
      do_unshift(state_type& __state, extern_type* __to, 
		 extern_type* __to_end, extern_type*& __to_next) const;
      
      virtual result
      do_in(state_type& __state, const extern_type* __from, 
	    const extern_type* __from_end, const extern_type*& __from_next, 
	    intern_type* __to, intern_type* __to_end, 
	    intern_type*& __to_next) const;
      
      virtual int 
      do_encoding() const throw();

      virtual bool 
      do_always_noconv() const throw();

      virtual int 
      do_length(const state_type&, const extern_type* __from, 
		const extern_type* __end, size_t __max) const;

      virtual int 
      do_max_length() const throw();
    };

  template<typename _InternT, typename _ExternT, typename _StateT>
    locale::id codecvt<_InternT, _ExternT, _StateT>::id;

  // codecvt<char, char, mbstate_t> required specialization
  template<>
    class codecvt<char, char, mbstate_t> 
    : public __codecvt_abstract_base<char, char, mbstate_t>
    {
    public:      
      // Types:
      typedef char 			intern_type;
      typedef char 			extern_type;
      typedef mbstate_t 		state_type;

    public:
      static locale::id id;

      explicit 
      codecvt(size_t __refs = 0);

    protected:
      virtual 
      ~codecvt();

      virtual result
      do_out(state_type& __state, const intern_type* __from, 
	     const intern_type* __from_end, const intern_type*& __from_next,
	     extern_type* __to, extern_type* __to_end,
	     extern_type*& __to_next) const;

      virtual result
      do_unshift(state_type& __state, extern_type* __to, 
		 extern_type* __to_end, extern_type*& __to_next) const;

      virtual result
      do_in(state_type& __state, const extern_type* __from, 
	    const extern_type* __from_end, const extern_type*& __from_next,
	    intern_type* __to, intern_type* __to_end, 
	    intern_type*& __to_next) const;

      virtual int 
      do_encoding() const throw();

      virtual bool 
      do_always_noconv() const throw();

      virtual int 
      do_length(const state_type&, const extern_type* __from, 
		const extern_type* __end, size_t __max) const;

      virtual int 
      do_max_length() const throw();
  };

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  // codecvt<wchar_t, char, mbstate_t> required specialization
  template<>
    class codecvt<wchar_t, char, mbstate_t> 
    : public __codecvt_abstract_base<wchar_t, char, mbstate_t>
    {
    public:
      // Types:
      typedef wchar_t 			intern_type;
      typedef char 			extern_type;
      typedef mbstate_t 		state_type;

    public:
      static locale::id 		id;

      explicit 
      codecvt(size_t __refs = 0);

    protected:
      virtual 
      ~codecvt();

      virtual result
      do_out(state_type& __state, const intern_type* __from, 
	     const intern_type* __from_end, const intern_type*& __from_next,
	     extern_type* __to, extern_type* __to_end,
	     extern_type*& __to_next) const;

      virtual result
      do_unshift(state_type& __state,
		 extern_type* __to, extern_type* __to_end,
		 extern_type*& __to_next) const;

      virtual result
      do_in(state_type& __state,
	     const extern_type* __from, const extern_type* __from_end,
	     const extern_type*& __from_next,
	     intern_type* __to, intern_type* __to_end,
	     intern_type*& __to_next) const;

      virtual 
      int do_encoding() const throw();

      virtual 
      bool do_always_noconv() const throw();

      virtual 
      int do_length(const state_type&, const extern_type* __from,
		    const extern_type* __end, size_t __max) const;

      virtual int 
      do_max_length() const throw();
    };
#endif //_GLIBCPP_USE_WCHAR_T

  // 22.2.1.6  Template class codecvt_byname
  template<typename _InternT, typename _ExternT, typename _StateT>
    class codecvt_byname : public codecvt<_InternT, _ExternT, _StateT>
    {
    public:
      explicit 
      codecvt_byname(const char*, size_t __refs = 0) 
      : codecvt<_InternT, _ExternT, _StateT>(__refs) { }

    protected:
      virtual 
      ~codecvt_byname() { }
    };

  // Include host and configuration specific partial specializations
  // with additional functionality, if possible.
#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  #include <bits/codecvt_specializations.h>
#endif

#endif // _CPP_BITS_CODECVT_H
